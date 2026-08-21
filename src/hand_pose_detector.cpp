/*
 * name: xiaorun
 * email: 15610499173@163.com
 */

#include "hand_pose_detector.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>

#if RK3588_SOP_ENABLE_RKNN && RK3588_SOP_HAS_OPENCV
#include <opencv2/opencv.hpp>
#endif

#if RK3588_SOP_ENABLE_RKNN && RK3588_SOP_HAS_OPENCV
struct RknnHandTensor {
  std::vector<float> data;
};

struct HandLetterboxMeta {
  float scale = 1.0F;
  int pad_left = 0;
  int pad_top = 0;
  int input_w = 0;
  int input_h = 0;
  int src_w = 0;
  int src_h = 0;
};

struct PalmDetection {
  cv::Rect2f box;
  std::vector<cv::Point2f> keypoints;
  float score = 0.0F;
};

struct RotatedHandRoi {
  cv::Mat rgb_input;
  cv::Matx23f input_to_image{};
  cv::Rect bounds;
};

static bool ReadRknnModelFile(const std::string& file_path, std::vector<std::uint8_t>* data) {
  if (data == nullptr) {
    return false;
  }
  std::ifstream input(file_path, std::ios::binary | std::ios::ate);
  if (!input.is_open()) {
    std::cerr << "无法打开 RKNN 手部模型: " << file_path << std::endl;
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

static float Sigmoid(const float value) {
  return 1.0F / (1.0F + std::exp(-value));
}

static std::vector<cv::Vec4f> GeneratePalmAnchors(const int input_size) {
  std::vector<cv::Vec4f> anchors;
  anchors.reserve(2016);
  for (const std::pair<int, int>& spec : {std::make_pair(8, 2), std::make_pair(16, 6)}) {
    const int stride = spec.first;
    const int anchors_num = spec.second;
    const int grid_size = input_size / stride;
    for (int y = 0; y < grid_size; ++y) {
      for (int x = 0; x < grid_size; ++x) {
        const float cx = (static_cast<float>(x) + 0.5F) / static_cast<float>(grid_size);
        const float cy = (static_cast<float>(y) + 0.5F) / static_cast<float>(grid_size);
        for (int i = 0; i < anchors_num; ++i) {
          anchors.emplace_back(cx, cy, 1.0F, 1.0F);
        }
      }
    }
  }
  return anchors;
}

static cv::Mat FrameToMat(const ImageFrame& frame) {
  return cv::Mat(frame.height, frame.width, CV_8UC3, const_cast<std::uint8_t*>(frame.bgr_data.data()));
}

static cv::Mat PreprocessFrameToRgbLetterbox(const cv::Mat& frame, const bool src_is_rgb, const cv::Size& input_size,
                                             HandLetterboxMeta* meta) {
  cv::Mat rgb;
  if (src_is_rgb) {
    rgb = frame;
  } else {
    cv::cvtColor(frame, rgb, cv::COLOR_BGR2RGB);
  }
  const float scale = std::min(static_cast<float>(input_size.width) / static_cast<float>(frame.cols),
                               static_cast<float>(input_size.height) / static_cast<float>(frame.rows));
  const int resized_width = std::max(1, static_cast<int>(std::round(static_cast<float>(frame.cols) * scale)));
  const int resized_height = std::max(1, static_cast<int>(std::round(static_cast<float>(frame.rows) * scale)));
  cv::Mat resized;
  cv::resize(rgb, resized, cv::Size(resized_width, resized_height));

  const int pad_left = (input_size.width - resized_width) / 2;
  const int pad_top = (input_size.height - resized_height) / 2;
  cv::Mat padded(input_size, CV_8UC3, cv::Scalar(0, 0, 0));
  resized.copyTo(padded(cv::Rect(pad_left, pad_top, resized_width, resized_height)));

  if (meta != nullptr) {
    meta->scale = scale;
    meta->pad_left = pad_left;
    meta->pad_top = pad_top;
    meta->input_w = input_size.width;
    meta->input_h = input_size.height;
    meta->src_w = frame.cols;
    meta->src_h = frame.rows;
  }
  return padded;
}

static cv::Point2f MapInputToSource(const float x, const float y, const HandLetterboxMeta& meta) {
  const float src_x = std::max(0.0F, std::min(static_cast<float>(meta.src_w - 1),
                                             (x - static_cast<float>(meta.pad_left)) / meta.scale));
  const float src_y = std::max(0.0F, std::min(static_cast<float>(meta.src_h - 1),
                                             (y - static_cast<float>(meta.pad_top)) / meta.scale));
  return cv::Point2f(src_x, src_y);
}

static cv::Rect2f MapNormBoxToSource(const cv::Rect2f& box, const HandLetterboxMeta& meta) {
  const cv::Point2f p1 = MapInputToSource(box.x * meta.input_w, box.y * meta.input_h, meta);
  const cv::Point2f p2 =
      MapInputToSource((box.x + box.width) * meta.input_w, (box.y + box.height) * meta.input_h, meta);
  return cv::Rect2f(p1.x, p1.y, std::max(1.0F, p2.x - p1.x), std::max(1.0F, p2.y - p1.y));
}

static float CalculateRectIou(const cv::Rect2f& lhs, const cv::Rect2f& rhs) {
  const float x1 = std::max(lhs.x, rhs.x);
  const float y1 = std::max(lhs.y, rhs.y);
  const float x2 = std::min(lhs.x + lhs.width, rhs.x + rhs.width);
  const float y2 = std::min(lhs.y + lhs.height, rhs.y + rhs.height);
  const float inter_width = std::max(0.0F, x2 - x1);
  const float inter_height = std::max(0.0F, y2 - y1);
  const float inter_area = inter_width * inter_height;
  const float union_area = lhs.width * lhs.height + rhs.width * rhs.height - inter_area;
  return union_area > 0.0F ? inter_area / union_area : 0.0F;
}

static bool MakeRotatedHandRoi(const cv::Mat& frame, const bool src_is_rgb, const PalmDetection& palm,
                               RotatedHandRoi* roi) {
  if (frame.empty() || roi == nullptr || palm.box.width <= 0.0F || palm.box.height <= 0.0F) {
    return false;
  }

  cv::Point2f center(palm.box.x + palm.box.width * 0.5F, palm.box.y + palm.box.height * 0.5F);
  cv::Point2f up_axis(0.0F, -1.0F);
  if (palm.keypoints.size() >= 3U) {
    const cv::Point2f wrist = palm.keypoints[0];
    const cv::Point2f middle_mcp = palm.keypoints[2];
    cv::Point2f wrist_to_middle = middle_mcp - wrist;
    const float norm = std::sqrt(wrist_to_middle.x * wrist_to_middle.x + wrist_to_middle.y * wrist_to_middle.y);
    if (norm > 1.0F) {
      up_axis = wrist_to_middle * (1.0F / norm);
      const float side_for_shift = std::max(palm.box.width, palm.box.height) * 2.6F;
      center += up_axis * (side_for_shift * 0.12F);
    }
  }

  const float side = std::max(32.0F, std::max(palm.box.width, palm.box.height) * 2.6F);
  const cv::Point2f y_axis = -up_axis;
  const cv::Point2f x_axis(y_axis.y, -y_axis.x);
  constexpr float kInputSize = 224.0F;
  const float pixel_scale = side / kInputSize;
  roi->input_to_image = cv::Matx23f(
      x_axis.x * pixel_scale, y_axis.x * pixel_scale,
      center.x - (x_axis.x + y_axis.x) * (kInputSize * 0.5F) * pixel_scale,
      x_axis.y * pixel_scale, y_axis.y * pixel_scale,
      center.y - (x_axis.y + y_axis.y) * (kInputSize * 0.5F) * pixel_scale);

  cv::Mat crop;
  cv::Mat transform(2, 3, CV_32F, roi->input_to_image.val);
  cv::warpAffine(frame, crop, transform, cv::Size(static_cast<int>(kInputSize), static_cast<int>(kInputSize)),
                 cv::INTER_LINEAR | cv::WARP_INVERSE_MAP, cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
  if (crop.empty()) {
    return false;
  }
  if (src_is_rgb) {
    roi->rgb_input = crop;
  } else {
    cv::cvtColor(crop, roi->rgb_input, cv::COLOR_BGR2RGB);
  }

  std::vector<cv::Point2f> corners = {
      cv::Point2f(0.0F, 0.0F), cv::Point2f(kInputSize, 0.0F),
      cv::Point2f(kInputSize, kInputSize), cv::Point2f(0.0F, kInputSize),
  };
  std::vector<cv::Point2f> image_corners;
  cv::transform(corners, image_corners, roi->input_to_image);
  roi->bounds = cv::boundingRect(image_corners) & cv::Rect(0, 0, frame.cols, frame.rows);
  return true;
}

static bool RunRknnHandModel(const rknn_context context, const rknn_input_output_num& io_num,
                             const rknn_tensor_attr& input_attr,
                             const std::vector<rknn_tensor_attr>& output_attrs, const cv::Mat& rgb_input,
                             std::vector<RknnHandTensor>* tensors, std::int64_t* run_us) {
  if (context == 0 || tensors == nullptr || rgb_input.empty()) {
    return false;
  }

  rknn_input input;
  std::memset(&input, 0, sizeof(input));
  input.index = 0;
  input.type = RKNN_TENSOR_UINT8;
  input.fmt = input_attr.fmt;
  input.size = static_cast<std::uint32_t>(rgb_input.total() * rgb_input.elemSize());
  input.buf = const_cast<std::uint8_t*>(rgb_input.data);
  input.pass_through = 0;

  int ret = rknn_inputs_set(context, 1, &input);
  if (ret != RKNN_SUCC) {
    std::cerr << "RKNN 手部模型输入设置失败: " << ret << std::endl;
    return false;
  }
  ret = rknn_run(context, nullptr);
  if (ret != RKNN_SUCC) {
    std::cerr << "RKNN 手部模型推理失败: " << ret << std::endl;
    return false;
  }
  rknn_perf_run perf_run;
  std::memset(&perf_run, 0, sizeof(perf_run));
  if (run_us != nullptr &&
      rknn_query(context, RKNN_QUERY_PERF_RUN, &perf_run, sizeof(perf_run)) == RKNN_SUCC &&
      perf_run.run_duration > 0) {
    *run_us = perf_run.run_duration;
  }

  std::vector<rknn_output> outputs(io_num.n_output);
  for (rknn_output& output : outputs) {
    std::memset(&output, 0, sizeof(output));
    output.want_float = 1;
  }
  ret = rknn_outputs_get(context, io_num.n_output, outputs.data(), nullptr);
  if (ret != RKNN_SUCC) {
    std::cerr << "RKNN 手部模型输出获取失败: " << ret << std::endl;
    return false;
  }

  tensors->clear();
  tensors->resize(outputs.size());
  for (std::size_t i = 0; i < outputs.size(); ++i) {
    const int count = static_cast<int>(outputs[i].size / sizeof(float));
    (*tensors)[i].data.assign(static_cast<float*>(outputs[i].buf), static_cast<float*>(outputs[i].buf) + count);
    (void)output_attrs;
  }
  rknn_outputs_release(context, io_num.n_output, outputs.data());
  return true;
}

static bool DecodePalmDetections(const std::vector<RknnHandTensor>& outputs, const HandLetterboxMeta& meta,
                                 const std::vector<cv::Vec4f>& anchors, const int max_hands,
                                 const float score_threshold, std::vector<PalmDetection>* detections) {
  if (outputs.size() < 2 || detections == nullptr) {
    return false;
  }
  detections->clear();

  const RknnHandTensor* regs = &outputs[0];
  const RknnHandTensor* scores = &outputs[1];
  if (regs->data.size() != anchors.size() * 18U) {
    regs = &outputs[1];
    scores = &outputs[0];
  }
  if (regs->data.size() != anchors.size() * 18U || scores->data.size() != anchors.size()) {
    return false;
  }

  std::vector<cv::Rect2f> boxes;
  std::vector<float> probs;
  for (std::size_t i = 0; i < anchors.size(); ++i) {
    const float prob = Sigmoid(scores->data[i]);
    if (prob < score_threshold) {
      continue;
    }
    const float* reg = regs->data.data() + i * 18U;
    const float cx = reg[0] / 192.0F + anchors[i][0];
    const float cy = reg[1] / 192.0F + anchors[i][1];
    const float width = reg[2] / 192.0F;
    const float height = reg[3] / 192.0F;
    boxes.emplace_back(cx - width * 0.5F, cy - height * 0.5F, width, height);
    probs.push_back(prob);
  }
  if (boxes.empty()) {
    return true;
  }

  std::vector<int> order(boxes.size());
  for (std::size_t i = 0; i < order.size(); ++i) {
    order[i] = static_cast<int>(i);
  }
  std::sort(order.begin(), order.end(), [&](const int lhs, const int rhs) {
    return probs[lhs] > probs[rhs];
  });

  for (const int index : order) {
    const cv::Rect2f box = MapNormBoxToSource(boxes[index], meta);
    const float* reg = regs->data.data() + static_cast<std::size_t>(index) * 18U;
    std::vector<cv::Point2f> keypoints;
    keypoints.reserve(7);
    for (int keypoint_index = 0; keypoint_index < 7; ++keypoint_index) {
      const float keypoint_x = reg[4 + keypoint_index * 2] / 192.0F + anchors[static_cast<std::size_t>(index)][0];
      const float keypoint_y = reg[5 + keypoint_index * 2] / 192.0F + anchors[static_cast<std::size_t>(index)][1];
      keypoints.push_back(MapInputToSource(keypoint_x * meta.input_w, keypoint_y * meta.input_h, meta));
    }
    bool suppressed = false;
    for (const PalmDetection& kept : *detections) {
      if (CalculateRectIou(box, kept.box) > 0.3F) {
        suppressed = true;
        break;
      }
    }
    if (!suppressed) {
      detections->push_back(PalmDetection{box, keypoints, probs[index]});
      if (static_cast<int>(detections->size()) >= max_hands) {
        break;
      }
    }
  }
  return true;
}

static bool DecodeRknnLandmarks(const std::vector<RknnHandTensor>& outputs, const cv::Matx23f& input_to_image,
                                const int image_width, const int image_height, const float score, HandPose* hand) {
  if (hand == nullptr) {
    return false;
  }

  const RknnHandTensor* landmark_tensor = nullptr;
  for (const RknnHandTensor& tensor : outputs) {
    if (tensor.data.size() == 63U) {
      landmark_tensor = &tensor;
      break;
    }
  }
  if (landmark_tensor == nullptr) {
    return false;
  }

  hand->handedness = "Unknown";
  hand->score = score;
  hand->landmarks.clear();
  hand->landmarks.reserve(21);
  for (int i = 0; i < 21; ++i) {
    float x_input = landmark_tensor->data[static_cast<std::size_t>(i) * 3U];
    float y_input = landmark_tensor->data[static_cast<std::size_t>(i) * 3U + 1U];
    const float z = landmark_tensor->data[static_cast<std::size_t>(i) * 3U + 2U];
    if (std::max(std::abs(x_input), std::abs(y_input)) <= 2.0F) {
      x_input *= 224.0F;
      y_input *= 224.0F;
    }
    const float image_x = input_to_image(0, 0) * x_input + input_to_image(0, 1) * y_input + input_to_image(0, 2);
    const float image_y = input_to_image(1, 0) * x_input + input_to_image(1, 1) * y_input + input_to_image(1, 2);
    const float norm_x = std::max(0.0F, std::min(1.0F, image_x / static_cast<float>(image_width)));
    const float norm_y = std::max(0.0F, std::min(1.0F, image_y / static_cast<float>(image_height)));
    hand->landmarks.push_back(HandLandmark{norm_x, norm_y, z, score});
  }
  return hand->landmarks.size() == 21U;
}

static BoundingBox RectToBoundingBox(const cv::Rect2f& rect, const int image_width, const int image_height) {
  const int x1 = std::max(0, static_cast<int>(std::floor(rect.x)));
  const int y1 = std::max(0, static_cast<int>(std::floor(rect.y)));
  const int x2 = std::min(image_width, static_cast<int>(std::ceil(rect.x + rect.width)));
  const int y2 = std::min(image_height, static_cast<int>(std::ceil(rect.y + rect.height)));
  return BoundingBox{x1, y1, std::max(1, x2 - x1), std::max(1, y2 - y1)};
}
#endif

HandPoseDetector::HandPoseDetector(const HandPoseConfig& config) : config_(config) {}

HandPoseDetector::~HandPoseDetector() {
#if RK3588_SOP_ENABLE_RKNN
  ReleaseRknn();
#endif
}

bool HandPoseDetector::Init() {
  if (config_.backend == "rknn") {
#if RK3588_SOP_ENABLE_RKNN
    return InitRknn();
#else
    std::cerr << "当前未启用 RKNN，请使用 -DENABLE_RKNN=ON 重新编译" << std::endl;
    return false;
#endif
  }
  std::cerr << "不支持的手部检测后端: " << config_.backend << std::endl;
  return false;
}

bool HandPoseDetector::Detect(const ImageFrame& frame, std::vector<HandPose>* hands) {
  // hands 使用输出参数，入口 clear，保证每帧结果独立。
  if (hands == nullptr) {
    return false;
  }
  hands->clear();
  if (config_.backend == "rknn") {
    return DetectWithRknn(frame, hands);
  }
  return false;
}

bool HandPoseDetector::DetectWithRknn(const ImageFrame& frame, std::vector<HandPose>* hands) {
#if RK3588_SOP_ENABLE_RKNN && RK3588_SOP_HAS_OPENCV
  if (hands == nullptr || !rknn_initialized_) {
    return false;
  }
  const std::size_t expected_size =
      static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height) * 3U;
  if (frame.width <= 0 || frame.height <= 0 || frame.bgr_data.size() < expected_size) {
    return false;
  }

  cv::Mat bgr = FrameToMat(frame);
  last_detector_rknn_run_us_ = 0;
  last_landmark_rknn_run_us_ = 0;
  HandLetterboxMeta detector_meta;
  cv::Mat detector_input = PreprocessFrameToRgbLetterbox(bgr, frame.pixel_format == PixelFormat::RGB, cv::Size(192, 192), &detector_meta);
  std::vector<RknnHandTensor> detector_outputs;
  if (!RunRknnHandModel(rknn_detector_context_, rknn_detector_io_num_, rknn_detector_input_attr_,
                        rknn_detector_output_attrs_, detector_input, &detector_outputs,
                        &last_detector_rknn_run_us_)) {
    return false;
  }

  std::vector<PalmDetection> palms;
  const std::vector<cv::Vec4f> anchors = GeneratePalmAnchors(192);
  if (!DecodePalmDetections(detector_outputs, detector_meta, anchors, config_.max_num_hands,
                            config_.min_detection_confidence, &palms)) {
    return false;
  }

  for (const PalmDetection& palm : palms) {
    RotatedHandRoi roi;
    if (!MakeRotatedHandRoi(bgr, frame.pixel_format == PixelFormat::RGB, palm, &roi)) {
      continue;
    }
    std::vector<RknnHandTensor> landmark_outputs;
    std::int64_t landmark_run_us = 0;
    if (!RunRknnHandModel(rknn_landmark_context_, rknn_landmark_io_num_, rknn_landmark_input_attr_,
                          rknn_landmark_output_attrs_, roi.rgb_input, &landmark_outputs, &landmark_run_us)) {
      continue;
    }
    last_landmark_rknn_run_us_ += landmark_run_us;

    HandPose hand;
    if (DecodeRknnLandmarks(landmark_outputs, roi.input_to_image, frame.width, frame.height, palm.score, &hand)) {
      hand.box = roi.bounds.width > 0 && roi.bounds.height > 0 ? RectToBoundingBox(roi.bounds, frame.width, frame.height)
                                                               : RectToBoundingBox(palm.box, frame.width, frame.height);
      hands->push_back(hand);
      if (static_cast<int>(hands->size()) >= config_.max_num_hands) {
        break;
      }
    }
  }
  return true;
#else
  (void)frame;
  (void)hands;
  std::cerr << "当前未启用 RKNN 或 OpenCV，无法使用 RKNN 手部关键点后端" << std::endl;
  return false;
#endif
}

#if RK3588_SOP_ENABLE_RKNN
bool HandPoseDetector::InitRknn() {
#if RK3588_SOP_HAS_OPENCV
  if (config_.model_path.empty() || config_.landmark_model_path.empty()) {
    std::cerr << "RKNN 手部模型路径为空，请配置 hand.model_path 和 hand.landmark_model_path" << std::endl;
    return false;
  }

  std::vector<std::uint8_t> detector_model_data;
  std::vector<std::uint8_t> landmark_model_data;
  if (!ReadRknnModelFile(config_.model_path, &detector_model_data) ||
      !ReadRknnModelFile(config_.landmark_model_path, &landmark_model_data)) {
    return false;
  }

  int ret = rknn_init(&rknn_detector_context_, detector_model_data.data(),
                      static_cast<std::uint32_t>(detector_model_data.size()), 0, nullptr);
  if (ret != RKNN_SUCC) {
    std::cerr << "初始化 RKNN palm detector 失败: " << ret << std::endl;
    ReleaseRknn();
    return false;
  }
  ret = rknn_init(&rknn_landmark_context_, landmark_model_data.data(),
                  static_cast<std::uint32_t>(landmark_model_data.size()), 0, nullptr);
  if (ret != RKNN_SUCC) {
    std::cerr << "初始化 RKNN hand landmark 失败: " << ret << std::endl;
    ReleaseRknn();
    return false;
  }

#ifdef RKNN_NPU_CORE_0_1_2
  rknn_set_core_mask(rknn_detector_context_, RKNN_NPU_CORE_0_1_2);
  rknn_set_core_mask(rknn_landmark_context_, RKNN_NPU_CORE_0_1_2);
#endif

  std::memset(&rknn_detector_io_num_, 0, sizeof(rknn_detector_io_num_));
  ret = rknn_query(rknn_detector_context_, RKNN_QUERY_IN_OUT_NUM, &rknn_detector_io_num_,
                   sizeof(rknn_detector_io_num_));
  if (ret != RKNN_SUCC || rknn_detector_io_num_.n_input != 1 || rknn_detector_io_num_.n_output < 2) {
    std::cerr << "查询 RKNN palm detector 输入输出失败: " << ret << std::endl;
    ReleaseRknn();
    return false;
  }
  std::memset(&rknn_detector_input_attr_, 0, sizeof(rknn_detector_input_attr_));
  rknn_detector_input_attr_.index = 0;
  ret = rknn_query(rknn_detector_context_, RKNN_QUERY_INPUT_ATTR, &rknn_detector_input_attr_,
                   sizeof(rknn_tensor_attr));
  if (ret != RKNN_SUCC) {
    std::cerr << "查询 RKNN palm detector 输入属性失败: " << ret << std::endl;
    ReleaseRknn();
    return false;
  }
  rknn_detector_output_attrs_.resize(rknn_detector_io_num_.n_output);
  for (std::uint32_t i = 0; i < rknn_detector_io_num_.n_output; ++i) {
    std::memset(&rknn_detector_output_attrs_[i], 0, sizeof(rknn_tensor_attr));
    rknn_detector_output_attrs_[i].index = i;
    ret = rknn_query(rknn_detector_context_, RKNN_QUERY_OUTPUT_ATTR, &rknn_detector_output_attrs_[i],
                     sizeof(rknn_tensor_attr));
    if (ret != RKNN_SUCC) {
      std::cerr << "查询 RKNN palm detector 输出属性失败: " << ret << std::endl;
      ReleaseRknn();
      return false;
    }
  }

  std::memset(&rknn_landmark_io_num_, 0, sizeof(rknn_landmark_io_num_));
  ret = rknn_query(rknn_landmark_context_, RKNN_QUERY_IN_OUT_NUM, &rknn_landmark_io_num_,
                   sizeof(rknn_landmark_io_num_));
  if (ret != RKNN_SUCC || rknn_landmark_io_num_.n_input != 1 || rknn_landmark_io_num_.n_output < 1) {
    std::cerr << "查询 RKNN hand landmark 输入输出失败: " << ret << std::endl;
    ReleaseRknn();
    return false;
  }
  std::memset(&rknn_landmark_input_attr_, 0, sizeof(rknn_landmark_input_attr_));
  rknn_landmark_input_attr_.index = 0;
  ret = rknn_query(rknn_landmark_context_, RKNN_QUERY_INPUT_ATTR, &rknn_landmark_input_attr_,
                   sizeof(rknn_tensor_attr));
  if (ret != RKNN_SUCC) {
    std::cerr << "查询 RKNN hand landmark 输入属性失败: " << ret << std::endl;
    ReleaseRknn();
    return false;
  }
  rknn_landmark_output_attrs_.resize(rknn_landmark_io_num_.n_output);
  for (std::uint32_t i = 0; i < rknn_landmark_io_num_.n_output; ++i) {
    std::memset(&rknn_landmark_output_attrs_[i], 0, sizeof(rknn_tensor_attr));
    rknn_landmark_output_attrs_[i].index = i;
    ret = rknn_query(rknn_landmark_context_, RKNN_QUERY_OUTPUT_ATTR, &rknn_landmark_output_attrs_[i],
                     sizeof(rknn_tensor_attr));
    if (ret != RKNN_SUCC) {
      std::cerr << "查询 RKNN hand landmark 输出属性失败: " << ret << std::endl;
      ReleaseRknn();
      return false;
    }
  }

  rknn_initialized_ = true;
  std::cout << "RKNN HandPose 初始化完成: detector=" << config_.model_path
            << ", landmark=" << config_.landmark_model_path
            << ", max_hands=" << config_.max_num_hands << std::endl;
  std::cout << "RKNN hand detector input dims=";
  for (std::uint32_t d = 0; d < rknn_detector_input_attr_.n_dims; ++d) {
    std::cout << rknn_detector_input_attr_.dims[d]
              << (d + 1U == rknn_detector_input_attr_.n_dims ? "" : "x");
  }
  std::cout << ", landmark input dims=";
  for (std::uint32_t d = 0; d < rknn_landmark_input_attr_.n_dims; ++d) {
    std::cout << rknn_landmark_input_attr_.dims[d]
              << (d + 1U == rknn_landmark_input_attr_.n_dims ? "" : "x");
  }
  std::cout << std::endl;
  return true;
#else
  std::cerr << "当前构建未找到 OpenCV，无法使用 RKNN 手部关键点后端" << std::endl;
  return false;
#endif
}

void HandPoseDetector::ReleaseRknn() {
  if (rknn_detector_context_ != 0) {
    rknn_destroy(rknn_detector_context_);
  }
  if (rknn_landmark_context_ != 0) {
    rknn_destroy(rknn_landmark_context_);
  }
  rknn_detector_context_ = 0;
  rknn_landmark_context_ = 0;
  rknn_initialized_ = false;
}
#endif
