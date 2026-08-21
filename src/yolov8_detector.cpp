/*
 * name: xiaorun
 * email: 15610499173@163.com
 */

#include "yolov8_detector.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>

#if RK3588_SOP_HAS_OPENCV
#include <opencv2/opencv.hpp>
#endif

Yolov8Detector::Yolov8Detector(const DetectorConfig& config) : config_(config) {}

Yolov8Detector::~Yolov8Detector() {
#if RK3588_SOP_ENABLE_RKNN
  ReleaseRknn();
#endif
}

#if RK3588_SOP_ENABLE_RKNN
/**
 * @brief 把 .rknn 模型文件一次性读入内存。
 *
 * RKNN 的 rknn_init() 接收的是一段连续内存，而不是文件路径。
 * 这里使用 std::vector<std::uint8_t> 保存二进制内容，有两个好处：
 *   1. vector 自己管理内存，函数返回或异常路径不会泄漏。
 *   2. vector::data() 返回连续内存指针，可以直接传给 RKNN C API。
 *
 * 注意 std::ios::ate 的作用：打开文件后光标直接在末尾，tellg() 可以拿到文件大小，
 * 这样能一次 resize 到准确大小，避免循环读文件造成额外复杂度。
 */
static bool ReadBinaryFile(const std::string& file_path, std::vector<std::uint8_t>* data) {
  if (data == nullptr) {
    return false;
  }
  std::ifstream input(file_path, std::ios::binary | std::ios::ate);
  if (!input.is_open()) {
    std::cerr << "无法打开模型文件: " << file_path << std::endl;
    return false;
  }
  const std::streampos size = input.tellg();
  if (size <= 0) {
    return false;
  }
  data->resize(static_cast<std::size_t>(size));
  input.seekg(0, std::ios::beg);
  input.read(reinterpret_cast<char*>(data->data()), size);
  return input.good();
}

/**
 * @brief 把 RGB 图像从 HWC 排列转换成 CHW 排列。
 *
 * HWC: 内存顺序是 RGBRGBRGB...，也就是一个像素的 3 个通道挨在一起。
 * CHW: 内存顺序是 RRR...GGG...BBB...，也就是一个通道的整张图挨在一起。
 *
 * RKNN 模型的输入布局由转换模型时决定，不能在 C++ 里固定假设。
 * 所以预处理先统一输出 HWC RGB；如果 RKNN 查询到模型需要 NCHW，再走这个函数。
 */
static void HwcToChwUint8(const std::vector<std::uint8_t>& hwc, const int width, const int height,
                          std::vector<std::uint8_t>* chw) {
  if (chw == nullptr) {
    return;
  }
  chw->resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3U);
  const std::size_t plane = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  for (std::size_t i = 0; i < plane; ++i) {
    (*chw)[i] = hwc[i * 3U];
    (*chw)[plane + i] = hwc[i * 3U + 1U];
    (*chw)[plane * 2U + i] = hwc[i * 3U + 2U];
  }
}

/**
 * @brief 把 0~255 的 uint8 图像转换成 0~1 的 float 图像。
 *
 * 大多数 RKNN 量化/非量化模型输入是 UINT8；少数模型可能要求 FLOAT32。
 * 这里不把 float 逻辑塞进 letterbox，是为了让预处理保持简单：
 * letterbox 只负责几何变换和 BGR->RGB，RKNN 输入类型适配留在 DetectWithRknn()。
 */
static void Uint8ToFloat(const std::vector<std::uint8_t>& src, std::vector<float>* dst) {
  if (dst == nullptr) {
    return;
  }
  dst->resize(src.size());
  for (std::size_t i = 0; i < src.size(); ++i) {
    (*dst)[i] = static_cast<float>(src[i]) / 255.0F;
  }
}

static float SoftmaxExpectedValue(const float* values, const int count) {
  if (values == nullptr || count <= 0) {
    return 0.0F;
  }
  float max_value = values[0];
  for (int i = 1; i < count; ++i) {
    max_value = std::max(max_value, values[i]);
  }
  float sum = 0.0F;
  float expected = 0.0F;
  for (int i = 0; i < count; ++i) {
    const float weight = std::exp(values[i] - max_value);
    sum += weight;
    expected += weight * static_cast<float>(i);
  }
  return sum > 0.0F ? expected / sum : 0.0F;
}

static bool GetTensorDims3(const rknn_tensor_attr& attr, int* dim0, int* dim1, int* dim2) {
  if (dim0 == nullptr || dim1 == nullptr || dim2 == nullptr) {
    return false;
  }
  std::vector<int> dims;
  for (std::uint32_t i = 0; i < attr.n_dims; ++i) {
    if (attr.dims[i] > 1) {
      dims.push_back(attr.dims[i]);
    }
  }
  if (dims.size() != 3U) {
    return false;
  }
  *dim0 = dims[0];
  *dim1 = dims[1];
  *dim2 = dims[2];
  return true;
}

static float ReadChannelValue(const float* data, const rknn_tensor_attr& attr, const int channel, const int cell_index,
                              const int channels, const int grid_size) {
  if (data == nullptr) {
    return 0.0F;
  }
  if (attr.fmt == RKNN_TENSOR_NCHW) {
    return data[static_cast<std::size_t>(channel) * static_cast<std::size_t>(grid_size) +
                static_cast<std::size_t>(cell_index)];
  }
  return data[static_cast<std::size_t>(cell_index) * static_cast<std::size_t>(channels) +
              static_cast<std::size_t>(channel)];
}

static BoundingBox RestoreCornerBox(const float x1_model, const float y1_model, const float x2_model,
                                    const float y2_model, const LetterboxInfo& letterbox, const int image_width,
                                    const int image_height) {
  const float x1_raw = (x1_model - static_cast<float>(letterbox.pad_x)) / letterbox.scale;
  const float y1_raw = (y1_model - static_cast<float>(letterbox.pad_y)) / letterbox.scale;
  const float x2_raw = (x2_model - static_cast<float>(letterbox.pad_x)) / letterbox.scale;
  const float y2_raw = (y2_model - static_cast<float>(letterbox.pad_y)) / letterbox.scale;
  const int x1 = std::max(0, std::min(image_width - 1, static_cast<int>(std::round(x1_raw))));
  const int y1 = std::max(0, std::min(image_height - 1, static_cast<int>(std::round(y1_raw))));
  const int x2 = std::max(0, std::min(image_width - 1, static_cast<int>(std::round(x2_raw))));
  const int y2 = std::max(0, std::min(image_height - 1, static_cast<int>(std::round(y2_raw))));
  return BoundingBox{x1, y1, std::max(0, x2 - x1), std::max(0, y2 - y1)};
}
#endif

bool Yolov8Detector::Init() {
  if (config_.backend == "rknn") {
#if RK3588_SOP_ENABLE_RKNN
    return InitRknn();
#else
    std::cerr << "当前未启用 RKNN，请使用 -DENABLE_RKNN=ON 重新编译" << std::endl;
    return false;
#endif
  }
  std::cerr << "不支持的 YOLOv8 后端: " << config_.backend << std::endl;
  return false;
}

bool Yolov8Detector::Detect(const ImageFrame& frame, std::vector<ObjectDetection>* detections) {
  // 输出参数使用指针，是为了明确告诉调用者：这个 vector 会被函数写入。
  // 函数入口统一 clear，避免调用者复用同一个 vector 时混入上一帧检测结果。
  if (detections == nullptr) {
    return false;
  }
  detections->clear();
  if (config_.backend == "rknn") {
    return DetectWithRknn(frame, detections);
  }
  return false;
}

bool Yolov8Detector::DetectWithRknn(const ImageFrame& frame, std::vector<ObjectDetection>* detections) {
  // 整体流程：
  //   1. 原图 BGR888 -> 640x640 RGB888 letterbox。
  //   2. 根据 RKNN 输入属性适配 NHWC/NCHW、UINT8/FLOAT32。
  //   3. 调用 rknn_inputs_set/rknn_run/rknn_outputs_get。
  //   4. 解码 YOLOv8 输出，恢复到原图坐标并 NMS。
  std::vector<std::uint8_t> input_data;
  LetterboxInfo letterbox;
  if (!PreprocessLetterbox(frame, &input_data, &letterbox)) {
    std::cerr << "YOLOv8 预处理失败" << std::endl;
    return false;
  }

#if RK3588_SOP_ENABLE_RKNN
  if (!rknn_initialized_) {
    std::cerr << "RKNN YOLOv8 尚未初始化" << std::endl;
    return false;
  }

  // 取第 0 个输入 tensor。当前 AI-SOP YOLOv8 模型只有一个图像输入。
  // 使用 front() 前 InitRknn() 已经保证 n_input >= 1。
  const rknn_tensor_attr& input_attr = rknn_input_attrs_.front();

  // 这些临时 buffer 必须定义在 rknn_inputs_set() 调用之前，并且在调用期间保持有效。
  // input_buf 最终会指向 input_data、nchw_u8 或 float_input 的 data()。
  // 这样避免 new/delete，也避免把临时 vector.data() 指针悬空。
  std::vector<std::uint8_t> nchw_u8;
  std::vector<float> float_input;
  void* input_buf = input_data.data();
  std::uint32_t input_size = static_cast<std::uint32_t>(input_data.size());
  rknn_tensor_type input_type = input_attr.type;
  rknn_tensor_format input_fmt = input_attr.fmt;

  // RKNN_TENSOR_NHWC 和 RKNN_TENSOR_NCHW 是模型输入布局，不是 OpenCV 图像格式。
  // NHWC: [height][width][channel]；NCHW: [channel][height][width]。
  // 转换模型时可能改变布局，所以上板时通过 rknn_query 的属性判断最稳。
  if (input_fmt == RKNN_TENSOR_NCHW) {
    HwcToChwUint8(input_data, letterbox.input_width, letterbox.input_height, &nchw_u8);
    input_buf = nchw_u8.data();
    input_size = static_cast<std::uint32_t>(nchw_u8.size());
  } else if (input_fmt != RKNN_TENSOR_NHWC) {
    std::cerr << "暂不支持的 RKNN 输入布局: " << input_fmt << std::endl;
    return false;
  }

  // 如果模型输入是 FLOAT32，则把 uint8 像素归一化成 0~1。
  // 量化模型这里仍声明为 UINT8 图像输入，让 RKNN runtime 在 pass_through=0 时按
  // 模型量化参数转换到内部 INT8 tensor。直接把 0~255 图像声明成 INT8 会破坏像素值。
  if (input_type == RKNN_TENSOR_FLOAT32) {
    const std::vector<std::uint8_t>& u8_ref = input_fmt == RKNN_TENSOR_NCHW ? nchw_u8 : input_data;
    Uint8ToFloat(u8_ref, &float_input);
    input_buf = float_input.data();
    input_size = static_cast<std::uint32_t>(float_input.size() * sizeof(float));
  } else {
    input_type = RKNN_TENSOR_UINT8;
  }

  // rknn_input 是 C 结构体，没有构造函数。
  // memset 清零可以确保没有未初始化字段传给 RKNN runtime。
  rknn_input input;
  std::memset(&input, 0, sizeof(input));
  input.index = 0;
  input.type = input_type;
  input.fmt = input_fmt;
  input.size = input_size;
  input.buf = input_buf;
  input.pass_through = 0;

  int ret = rknn_inputs_set(rknn_context_, 1, &input);
  if (ret != RKNN_SUCC) {
    std::cerr << "rknn_inputs_set 失败: " << ret << std::endl;
    return false;
  }
  ret = rknn_run(rknn_context_, nullptr);
  if (ret != RKNN_SUCC) {
    std::cerr << "rknn_run 失败: " << ret << std::endl;
    return false;
  }

  rknn_perf_run perf_run;
  std::memset(&perf_run, 0, sizeof(perf_run));
  if (rknn_query(rknn_context_, RKNN_QUERY_PERF_RUN, &perf_run, sizeof(perf_run)) == RKNN_SUCC &&
      perf_run.run_duration > 0) {
    last_rknn_run_us_ = perf_run.run_duration;
  }

  // 输出数量由模型决定。AI-SOP 当前通常是 1 个输出，但这里按 n_output 分配，
  // 以后如果模型导出成多输出，也不会在数组大小上出错。
  std::vector<rknn_output> outputs(rknn_io_num_.n_output);
  for (rknn_output& output : outputs) {
    std::memset(&output, 0, sizeof(output));
    output.want_float = 1;
    output.is_prealloc = 0;
  }
  ret = rknn_outputs_get(rknn_context_, rknn_io_num_.n_output, outputs.data(), nullptr);
  if (ret != RKNN_SUCC) {
    std::cerr << "rknn_outputs_get 失败: " << ret << std::endl;
    return false;
  }

  const bool ok = DecodeRknnOutputs(outputs, letterbox, frame.width, frame.height, detections);
  rknn_outputs_release(rknn_context_, rknn_io_num_.n_output, outputs.data());
  return ok;
#else
  (void)detections;
  std::cerr << "RKNN 未启用，已完成预处理但不会执行推理" << std::endl;
  return false;
#endif
}

#if RK3588_SOP_ENABLE_RKNN
bool Yolov8Detector::InitRknn() {
  // RKNN 初始化只做一次，不在每帧重复加载模型。
  // 模型加载很慢，而且每帧创建/销毁 context 会造成 NPU 资源抖动。
  std::vector<std::uint8_t> model_data;
  if (!ReadBinaryFile(config_.model_path, &model_data)) {
    return false;
  }

  int ret = rknn_init(&rknn_context_, model_data.data(), static_cast<std::uint32_t>(model_data.size()), 0, nullptr);
  if (ret != RKNN_SUCC) {
    std::cerr << "rknn_init 失败: " << ret << std::endl;
    return false;
  }

#ifdef RKNN_NPU_CORE_0_1_2
  // 如果当前 rknn_api.h 定义了三核宏，则让 RK3588 三个 NPU core 都参与。
  // 用 #ifdef 包住是为了兼容不同 RKNN runtime 版本：老版本头文件可能没有这个宏。
  rknn_set_core_mask(rknn_context_, RKNN_NPU_CORE_0_1_2);
#endif

  std::memset(&rknn_io_num_, 0, sizeof(rknn_io_num_));
  ret = rknn_query(rknn_context_, RKNN_QUERY_IN_OUT_NUM, &rknn_io_num_, sizeof(rknn_io_num_));
  if (ret != RKNN_SUCC || rknn_io_num_.n_input < 1 || rknn_io_num_.n_output < 1) {
    std::cerr << "查询 RKNN 输入输出数量失败: " << ret << std::endl;
    ReleaseRknn();
    return false;
  }

  // 查询输入属性比写死输入格式更可靠。
  // 关键字段包括 dims/fmt/type，后续输入 buffer 会根据它们适配。
  rknn_input_attrs_.resize(rknn_io_num_.n_input);
  for (std::uint32_t i = 0; i < rknn_io_num_.n_input; ++i) {
    std::memset(&rknn_input_attrs_[i], 0, sizeof(rknn_input_attrs_[i]));
    rknn_input_attrs_[i].index = i;
    ret = rknn_query(rknn_context_, RKNN_QUERY_INPUT_ATTR, &rknn_input_attrs_[i], sizeof(rknn_tensor_attr));
    if (ret != RKNN_SUCC) {
      std::cerr << "查询 RKNN 输入属性失败: " << ret << std::endl;
      ReleaseRknn();
      return false;
    }
  }

  // 输出属性用于判断 YOLOv8 输出布局，例如 (1,8,8400) 或 (1,8400,8)。
  rknn_output_attrs_.resize(rknn_io_num_.n_output);
  for (std::uint32_t i = 0; i < rknn_io_num_.n_output; ++i) {
    std::memset(&rknn_output_attrs_[i], 0, sizeof(rknn_output_attrs_[i]));
    rknn_output_attrs_[i].index = i;
    ret = rknn_query(rknn_context_, RKNN_QUERY_OUTPUT_ATTR, &rknn_output_attrs_[i], sizeof(rknn_tensor_attr));
    if (ret != RKNN_SUCC) {
      std::cerr << "查询 RKNN 输出属性失败: " << ret << std::endl;
      ReleaseRknn();
      return false;
    }
  }

  rknn_initialized_ = true;
  std::cout << "RKNN YOLOv8 初始化完成: " << config_.model_path
            << ", input=" << config_.input_size << "x" << config_.input_size
            << ", outputs=" << rknn_io_num_.n_output << std::endl;
  const rknn_tensor_attr& input_attr = rknn_input_attrs_.front();
  std::cout << "RKNN input attr: dims=";
  for (std::uint32_t d = 0; d < input_attr.n_dims; ++d) {
    std::cout << input_attr.dims[d] << (d + 1U == input_attr.n_dims ? "" : "x");
  }
  std::cout << ", fmt=" << input_attr.fmt << ", type=" << get_type_string(input_attr.type)
            << ", qnt=" << input_attr.qnt_type << ", zp=" << input_attr.zp << ", scale=" << input_attr.scale
            << std::endl;
  for (std::size_t i = 0; i < rknn_output_attrs_.size(); ++i) {
    std::cout << "RKNN output attr[" << i << "]: dims=";
    for (std::uint32_t d = 0; d < rknn_output_attrs_[i].n_dims; ++d) {
      std::cout << rknn_output_attrs_[i].dims[d] << (d + 1U == rknn_output_attrs_[i].n_dims ? "" : "x");
    }
    std::cout << ", fmt=" << rknn_output_attrs_[i].fmt << ", type=" << get_type_string(rknn_output_attrs_[i].type)
              << ", qnt=" << rknn_output_attrs_[i].qnt_type << ", zp=" << rknn_output_attrs_[i].zp
              << ", scale=" << rknn_output_attrs_[i].scale << std::endl;
  }
  return true;
}

void Yolov8Detector::ReleaseRknn() {
  // rknn_destroy 可以释放 NPU runtime 资源。
  // 同时判断 rknn_initialized_ 和 rknn_context_，是为了覆盖“初始化中途失败但 context 已创建”的情况。
  if (rknn_initialized_ || rknn_context_ != 0) {
    rknn_destroy(rknn_context_);
  }
  rknn_context_ = 0;
  rknn_initialized_ = false;
}

// 把 RKNN 的 float 输出转成工程内部 ObjectDetection。
// 这里不直接依赖固定 shape，是为了让同一份代码兼容 RKNN 转换前后的布局差异。
bool Yolov8Detector::DecodeRknnOutputs(const std::vector<rknn_output>& outputs, const LetterboxInfo& letterbox,
                                       const int image_width, const int image_height,
                                       std::vector<ObjectDetection>* detections) const {
  if (detections == nullptr) {
    return false;
  }
  detections->clear();
  const int class_count = static_cast<int>(config_.labels.size());
  if (class_count <= 0) {
    return false;
  }

  // 先尝试官方 rknn_model_zoo 的三分支输出格式：
  // [box_0, cls_0, score_0, box_1, cls_1, score_1, box_2, cls_2, score_2]
  // README 说明了优化后的 YOLOv8 RKNN 模型是按三组输出组织的。
  if (outputs.size() >= 3U && outputs.size() % 3U == 0U) {
    std::vector<ObjectDetection> branch_detections;
    const std::size_t branch_count = outputs.size() / 3U;
    bool branch_ok = true;
    for (std::size_t branch = 0; branch < branch_count; ++branch) {
      const rknn_output& box_output = outputs[branch * 3U];
      const rknn_output& cls_output = outputs[branch * 3U + 1U];
      const rknn_output& score_output = outputs[branch * 3U + 2U];
      const rknn_tensor_attr& box_attr = rknn_output_attrs_[branch * 3U];
      const rknn_tensor_attr& cls_attr = rknn_output_attrs_[branch * 3U + 1U];
      const float* box_data = static_cast<const float*>(box_output.buf);
      const float* cls_data = static_cast<const float*>(cls_output.buf);
      if (box_data == nullptr || cls_data == nullptr || score_output.buf == nullptr) {
        branch_ok = false;
        break;
      }

      int box_d0 = 0;
      int box_d1 = 0;
      int box_d2 = 0;
      int cls_d0 = 0;
      int cls_d1 = 0;
      int cls_d2 = 0;
      if (!GetTensorDims3(box_attr, &box_d0, &box_d1, &box_d2) ||
          !GetTensorDims3(cls_attr, &cls_d0, &cls_d1, &cls_d2)) {
        branch_ok = false;
        break;
      }

      int box_channels = 0;
      int grid_h = 0;
      int grid_w = 0;
      if (box_attr.fmt == RKNN_TENSOR_NCHW) {
        box_channels = box_d0;
        grid_h = box_d1;
        grid_w = box_d2;
      } else if (box_attr.fmt == RKNN_TENSOR_NHWC) {
        grid_h = box_d0;
        grid_w = box_d1;
        box_channels = box_d2;
      } else {
        branch_ok = false;
        break;
      }
      if (box_channels % 4 != 0 || grid_h <= 0 || grid_w <= 0) {
        branch_ok = false;
        break;
      }
      const int dfl_bins = box_channels / 4;
      const std::size_t grid_size = static_cast<std::size_t>(grid_h) * static_cast<std::size_t>(grid_w);
      const float stride_x = static_cast<float>(letterbox.input_width) / static_cast<float>(grid_w);
      const float stride_y = static_cast<float>(letterbox.input_height) / static_cast<float>(grid_h);

      int cls_channels = 0;
      int cls_h = 0;
      int cls_w = 0;
      if (cls_attr.fmt == RKNN_TENSOR_NCHW) {
        cls_channels = cls_d0;
        cls_h = cls_d1;
        cls_w = cls_d2;
      } else if (cls_attr.fmt == RKNN_TENSOR_NHWC) {
        cls_h = cls_d0;
        cls_w = cls_d1;
        cls_channels = cls_d2;
      } else {
        branch_ok = false;
        break;
      }
      if (cls_channels != class_count || cls_h != grid_h || cls_w != grid_w) {
        branch_ok = false;
        break;
      }

      for (int y = 0; y < grid_h; ++y) {
        for (int x = 0; x < grid_w; ++x) {
          const std::size_t cell_index = static_cast<std::size_t>(y) * static_cast<std::size_t>(grid_w) +
                                         static_cast<std::size_t>(x);
          int best_class = 0;
          const float best_score_base = ReadChannelValue(cls_data, cls_attr, 0, static_cast<int>(cell_index),
                                                         class_count, static_cast<int>(grid_size));
          float best_score = best_score_base;
          for (int c = 1; c < class_count; ++c) {
            const float score = ReadChannelValue(cls_data, cls_attr, c, static_cast<int>(cell_index),
                                                class_count, static_cast<int>(grid_size));
            if (score > best_score) {
              best_score = score;
              best_class = c;
            }
          }
          if (best_score < config_.conf_threshold) {
            continue;
          }

          float distances[4] = {};
          for (int side = 0; side < 4; ++side) {
            std::vector<float> logits(static_cast<std::size_t>(dfl_bins));
            for (int k = 0; k < dfl_bins; ++k) {
              const int channel = side * dfl_bins + k;
              logits[static_cast<std::size_t>(k)] =
                  ReadChannelValue(box_data, box_attr, channel, static_cast<int>(cell_index), box_channels,
                                   static_cast<int>(grid_size));
            }
            distances[side] = SoftmaxExpectedValue(logits.data(), dfl_bins);
          }

          const float x1 = (static_cast<float>(x) + 0.5F - distances[0]) * stride_x;
          const float y1 = (static_cast<float>(y) + 0.5F - distances[1]) * stride_y;
          const float x2 = (static_cast<float>(x) + 0.5F + distances[2]) * stride_x;
          const float y2 = (static_cast<float>(y) + 0.5F + distances[3]) * stride_y;
          const BoundingBox box = RestoreCornerBox(x1, y1, x2, y2, letterbox, image_width, image_height);
          if (box.width <= 0 || box.height <= 0) {
            continue;
          }
          branch_detections.push_back(ObjectDetection{config_.labels[best_class], best_score, box, Point3D{}});
        }
      }
    }
    if (branch_ok) {
      *detections = ApplyNms(branch_detections);
      return true;
    }
  }

  std::vector<ObjectDetection> merged;
  for (std::size_t i = 0; i < outputs.size(); ++i) {
    const rknn_output& output = outputs[i];
    const rknn_tensor_attr& attr = rknn_output_attrs_[i];
    const float* data = static_cast<const float*>(output.buf);
    if (data == nullptr || output.size < sizeof(float)) {
      continue;
    }
    const int count = static_cast<int>(output.size / sizeof(float));

    // attr.dims 里经常包含 batch 维度 1。
    // 解 YOLO 输出时 batch=1 没有信息量，所以这里把大于 1 的维度取出来，
    // 后面只看最后两个有效维度来判断是 [8,8400] 还是 [8400,8]。
    std::vector<int> dims;
    for (std::uint32_t d = 0; d < attr.n_dims; ++d) {
      if (attr.dims[d] > 1) {
        dims.push_back(attr.dims[d]);
      }
    }

    int rows = 0;
    int cols = 0;
    bool transposed = false;
    // YOLOv8 常见两种输出：
    //   无 objectness: 4 + class_count，例如 AI-SOP 是 4 + 4 = 8。
    //   有 objectness: 5 + class_count，一些 YOLO 导出版本会多一个 obj 分数。
    const int item_without_objectness = 4 + class_count;
    const int item_with_objectness = 5 + class_count;
    if (!ResolveOutputLayout(count, dims, item_without_objectness, &rows, &cols, &transposed) &&
        !ResolveOutputLayout(count, dims, item_with_objectness, &rows, &cols, &transposed)) {
      std::cerr << "暂不支持的 YOLOv8 输出形状, output=" << i << ", floats=" << count << std::endl;
      continue;
    }

    std::vector<ObjectDetection> decoded;
    if (!transposed) {
      DecodeYolov8Output(data, rows, cols, letterbox, image_width, image_height, &decoded);
    } else {
      // RKNN 有时会返回 [item_size, candidate_count]。
      // DecodeYolov8Output() 期望每一行是一条候选框，所以这里转成
      // [candidate_count, item_size]。虽然多一次拷贝，但代码直观，便于上板调试。
      std::vector<float> reordered(static_cast<std::size_t>(rows) * static_cast<std::size_t>(cols));
      for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
          reordered[static_cast<std::size_t>(row) * static_cast<std::size_t>(cols) + static_cast<std::size_t>(col)] =
              data[static_cast<std::size_t>(col) * static_cast<std::size_t>(rows) + static_cast<std::size_t>(row)];
        }
      }
      DecodeYolov8Output(reordered.data(), rows, cols, letterbox, image_width, image_height, &decoded);
    }
    merged.insert(merged.end(), decoded.begin(), decoded.end());
  }

  *detections = ApplyNms(merged);
  return true;
}
#endif

bool Yolov8Detector::PreprocessLetterbox(const ImageFrame& frame, std::vector<std::uint8_t>* input_data, LetterboxInfo* info) const {
  if (input_data == nullptr || info == nullptr || frame.width <= 0 || frame.height <= 0 || config_.input_size <= 0) {
    return false;
  }
  const std::size_t expected_size = static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height) * 3U;
  if (frame.bgr_data.size() < expected_size) {
    return false;
  }

  // info 记录的参数后处理必须使用。
  // 如果这里只输出图像不记录 scale/pad，检测框就无法准确映射回原图。
  info->input_width = config_.input_size;
  info->input_height = config_.input_size;
  // 等比例缩放，保持目标形状不变，剩余区域用 114 补边。
  info->scale = std::min(static_cast<float>(config_.input_size) / static_cast<float>(frame.width),
                         static_cast<float>(config_.input_size) / static_cast<float>(frame.height));
  info->resized_width = std::max(1, static_cast<int>(std::round(static_cast<float>(frame.width) * info->scale)));
  info->resized_height = std::max(1, static_cast<int>(std::round(static_cast<float>(frame.height) * info->scale)));
  info->pad_x = (config_.input_size - info->resized_width) / 2;
  info->pad_y = (config_.input_size - info->resized_height) / 2;

#if RK3588_SOP_HAS_OPENCV
  cv::Mat src(frame.height, frame.width, CV_8UC3, const_cast<std::uint8_t*>(frame.bgr_data.data()));
  cv::Mat rgb;
  if (frame.pixel_format == PixelFormat::RGB) {
    rgb = src;
  } else {
    cv::cvtColor(src, rgb, cv::COLOR_BGR2RGB);
  }

  cv::Mat resized;
  cv::resize(rgb, resized, cv::Size(info->resized_width, info->resized_height), 0.0, 0.0, cv::INTER_LINEAR);

  // 官方 YOLOv8 letterbox 使用 114 灰色填充，和训练/导出时的预处理保持一致。
  input_data->assign(static_cast<std::size_t>(config_.input_size) * static_cast<std::size_t>(config_.input_size) * 3U, 114U);
  for (int y = 0; y < resized.rows; ++y) {
    const std::uint8_t* row = resized.ptr<std::uint8_t>(y);
    for (int x = 0; x < resized.cols; ++x) {
      const std::size_t out_index = (static_cast<std::size_t>(y + info->pad_y) *
                                         static_cast<std::size_t>(config_.input_size) +
                                     static_cast<std::size_t>(x + info->pad_x)) * 3U;
      const std::size_t in_index = static_cast<std::size_t>(x) * 3U;
      (*input_data)[out_index] = row[in_index];
      (*input_data)[out_index + 1U] = row[in_index + 1U];
      (*input_data)[out_index + 2U] = row[in_index + 2U];
    }
  }
#else
  input_data->assign(static_cast<std::size_t>(config_.input_size) * static_cast<std::size_t>(config_.input_size) * 3U, 114U);
  for (int dst_y = 0; dst_y < info->resized_height; ++dst_y) {
    const int src_y = std::min(frame.height - 1, static_cast<int>(std::floor(static_cast<float>(dst_y) / info->scale)));
    for (int dst_x = 0; dst_x < info->resized_width; ++dst_x) {
      const int src_x = std::min(frame.width - 1, static_cast<int>(std::floor(static_cast<float>(dst_x) / info->scale)));
      std::uint8_t rgb[3] = {0U, 0U, 0U};
      SampleFrameToRgb(frame, src_x, src_y, rgb);
      const std::size_t out_index = (static_cast<std::size_t>(dst_y + info->pad_y) *
                                         static_cast<std::size_t>(config_.input_size) +
                                     static_cast<std::size_t>(dst_x + info->pad_x)) * 3U;
      (*input_data)[out_index] = rgb[0];
      (*input_data)[out_index + 1U] = rgb[1];
      (*input_data)[out_index + 2U] = rgb[2];
    }
  }
#endif
  return true;
}

void Yolov8Detector::SampleFrameToRgb(const ImageFrame& frame, const int src_x, const int src_y, std::uint8_t* rgb) const {
  if (rgb == nullptr) {
    return;
  }
  const std::size_t index = (static_cast<std::size_t>(src_y) * static_cast<std::size_t>(frame.width) +
                             static_cast<std::size_t>(src_x)) * 3U;
  if (frame.pixel_format == PixelFormat::RGB) {
    rgb[0] = frame.bgr_data[index];
    rgb[1] = frame.bgr_data[index + 1U];
    rgb[2] = frame.bgr_data[index + 2U];
    return;
  }
  rgb[0] = frame.bgr_data[index + 2U];
  rgb[1] = frame.bgr_data[index + 1U];
  rgb[2] = frame.bgr_data[index];
}


bool Yolov8Detector::ResolveOutputLayout(const int float_count, const std::vector<int>& dims, const int item_size,
                                         int* rows, int* cols, bool* transposed) const {
  if (rows == nullptr || cols == nullptr || transposed == nullptr || float_count <= 0 || item_size <= 4) {
    return false;
  }
  *rows = 0;
  *cols = 0;
  *transposed = false;

  // 优先相信 RKNN 查询到的维度。
  // dims 最后两个维度通常就是 [候选框数, 每框字段数] 或反过来。
  if (dims.size() >= 2U) {
    const int dim_a = dims[dims.size() - 2U];
    const int dim_b = dims[dims.size() - 1U];
    if (dim_b == item_size && dim_a * dim_b <= float_count) {
      *rows = dim_a;
      *cols = dim_b;
      *transposed = false;
      return true;
    }
    if (dim_a == item_size && dim_a * dim_b <= float_count) {
      *rows = dim_b;
      *cols = dim_a;
      *transposed = true;
      return true;
    }
  }

  // 如果输出属性没有给出可用维度，就退回到总 float 数推断。
  // 例如 67200 / 8 = 8400，说明每个候选框 8 个字段。
  if (float_count % item_size == 0) {
    *rows = float_count / item_size;
    *cols = item_size;
    *transposed = false;
    return true;
  }
  return false;
}

bool Yolov8Detector::DecodeYolov8Output(const float* data, const int rows, const int cols, const LetterboxInfo& letterbox,
                                        const int image_width, const int image_height,
                                        std::vector<ObjectDetection>* detections) const {
  if (data == nullptr || detections == nullptr || rows <= 0 || cols <= 0) {
    return false;
  }

  // 支持 Ultralytics YOLOv8 常见输出:
  // [cx, cy, w, h, class scores...] 或 [cx, cy, w, h, obj, class scores...]。
  const int class_count = static_cast<int>(config_.labels.size());
  const bool has_objectness = cols >= 5 + class_count;
  const int class_offset = has_objectness ? 5 : 4;
  if (class_count <= 0 || cols < class_offset + class_count) {
    return false;
  }

  std::vector<ObjectDetection> candidates;
  candidates.reserve(static_cast<std::size_t>(rows));
  const bool debug_yolo = std::getenv("RK3588_SOP_DEBUG_YOLO") != nullptr;
  float debug_best_score = -1.0F;
  int debug_best_row = -1;
  int debug_best_class = -1;
  const float* debug_best_pred = nullptr;
  for (int row = 0; row < rows; ++row) {
    // data 是一维连续内存，row * cols 表示第 row 条候选框的起始地址。
    // 这种写法比 vector<vector<float>> 少一次内存分配，适合推理后处理。
    const float* pred = data + row * cols;
    int class_index = 0;
    float best_score = pred[class_offset];
    for (int i = 1; i < class_count; ++i) {
      if (pred[class_offset + i] > best_score) {
        best_score = pred[class_offset + i];
        class_index = i;
      }
    }
    if (debug_yolo && best_score > debug_best_score) {
      debug_best_score = best_score;
      debug_best_row = row;
      debug_best_class = class_index;
      debug_best_pred = pred;
    }
    // Ultralytics YOLOv8 exported detection heads normally expose class scores
    // as probabilities. Applying sigmoid again turns background zeros into 0.5
    // and floods the frame with false positives.
    if (has_objectness) {
      best_score *= pred[4];
    }
    if (best_score < config_.conf_threshold) {
      continue;
    }

    // 模型坐标在 640x640 letterbox 图上，需要扣除 padding 后映射回原图。
    const BoundingBox box = RestoreBox(pred[0], pred[1], pred[2], pred[3], letterbox, image_width, image_height);
    if (box.width <= 0 || box.height <= 0) {
      continue;
    }
    candidates.push_back(ObjectDetection{config_.labels[class_index], best_score, box, Point3D{}});
  }

  if (debug_yolo && debug_best_pred != nullptr) {
    std::cerr << "YOLO debug: rows=" << rows << ", cols=" << cols << ", best_row=" << debug_best_row
              << ", best_class=" << debug_best_class << ", best_raw_score=" << debug_best_score
              << ", box_cxcywh=" << debug_best_pred[0] << "," << debug_best_pred[1] << "," << debug_best_pred[2]
              << "," << debug_best_pred[3] << ", threshold=" << config_.conf_threshold << std::endl;
  }

  *detections = ApplyNms(candidates);
  return true;
}

// YOLOv8 输出的是中心点坐标和宽高，不是左上角坐标。
// 这个函数负责两件事：中心点框 -> 左上右下框；模型输入图坐标 -> 原图坐标。
BoundingBox Yolov8Detector::RestoreBox(const float cx, const float cy, const float width, const float height,
                                       const LetterboxInfo& letterbox, const int image_width, const int image_height) const {
  // YOLOv8 输出中心点和宽高，这里先转成模型输入图上的左上/右下坐标。
  const float x1_model = cx - width * 0.5F;
  const float y1_model = cy - height * 0.5F;
  const float x2_model = cx + width * 0.5F;
  const float y2_model = cy + height * 0.5F;

  const float x1_raw = (x1_model - static_cast<float>(letterbox.pad_x)) / letterbox.scale;
  const float y1_raw = (y1_model - static_cast<float>(letterbox.pad_y)) / letterbox.scale;
  const float x2_raw = (x2_model - static_cast<float>(letterbox.pad_x)) / letterbox.scale;
  const float y2_raw = (y2_model - static_cast<float>(letterbox.pad_y)) / letterbox.scale;

  const int x1 = std::max(0, std::min(image_width - 1, static_cast<int>(std::round(x1_raw))));
  const int y1 = std::max(0, std::min(image_height - 1, static_cast<int>(std::round(y1_raw))));
  const int x2 = std::max(0, std::min(image_width - 1, static_cast<int>(std::round(x2_raw))));
  const int y2 = std::max(0, std::min(image_height - 1, static_cast<int>(std::round(y2_raw))));
  return BoundingBox{x1, y1, std::max(0, x2 - x1), std::max(0, y2 - y1)};
}

// IoU = 交集面积 / 并集面积。
// NMS 用它判断两个框是否指向同一个物体。
float Yolov8Detector::CalculateIou(const BoundingBox& lhs, const BoundingBox& rhs) const {
  const int lhs_x2 = lhs.x + lhs.width;
  const int lhs_y2 = lhs.y + lhs.height;
  const int rhs_x2 = rhs.x + rhs.width;
  const int rhs_y2 = rhs.y + rhs.height;
  const int inter_x1 = std::max(lhs.x, rhs.x);
  const int inter_y1 = std::max(lhs.y, rhs.y);
  const int inter_x2 = std::min(lhs_x2, rhs_x2);
  const int inter_y2 = std::min(lhs_y2, rhs_y2);
  const int inter_width = std::max(0, inter_x2 - inter_x1);
  const int inter_height = std::max(0, inter_y2 - inter_y1);
  const float inter_area = static_cast<float>(inter_width * inter_height);
  const float union_area = static_cast<float>(lhs.width * lhs.height + rhs.width * rhs.height) - inter_area;
  if (union_area <= 0.0F) {
    return 0.0F;
  }
  return inter_area / union_area;
}

std::vector<ObjectDetection> Yolov8Detector::ApplyNms(const std::vector<ObjectDetection>& detections) const {
  // NMS 贪心流程：
  //   1. 先按置信度从高到低排序。
  //   2. 每次取当前最高分框作为保留框。
  //   3. 删除同类别且 IoU 过大的低分框。
  //
  // 这里不用 std::vector<bool> 标记删除。vector<bool> 是 C++ 标准库的特殊压缩容器，
  // 它返回的不是普通 bool 引用，初学和移植时容易踩坑。
  // 当前每帧候选框数量经过置信度过滤后不会太大，用更直观的 remaining 临时列表更好维护。
  std::vector<ObjectDetection> remaining = detections;
  std::sort(remaining.begin(), remaining.end(), [](const ObjectDetection& lhs, const ObjectDetection& rhs) {
    return lhs.score > rhs.score;
  });

  std::vector<ObjectDetection> kept;
  while (!remaining.empty()) {
    const ObjectDetection best = remaining.front();
    kept.push_back(best);

    std::vector<ObjectDetection> next_round;
    next_round.reserve(remaining.size());
    for (std::size_t i = 1; i < remaining.size(); ++i) {
      const bool same_label = best.label == remaining[i].label;
      const bool highly_overlapped = CalculateIou(best.box, remaining[i].box) > config_.iou_threshold;
      if (!(same_label && highly_overlapped)) {
        next_round.push_back(remaining[i]);
      }
    }
    remaining = std::move(next_round);
  }
  return kept;
}
