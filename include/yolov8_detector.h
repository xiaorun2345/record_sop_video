/*
 * name: xiaorun
 * email: 15610499173@163.com
 */

#ifndef TOOLCHAINS_RK3588_EXAMPLES_RK3588_SOP_INCLUDE_YOLOV8_DETECTOR_H_
#define TOOLCHAINS_RK3588_EXAMPLES_RK3588_SOP_INCLUDE_YOLOV8_DETECTOR_H_

#include <cstdint>
#include <memory>
#include <vector>

#if RK3588_SOP_ENABLE_RKNN
#include "rknn_api.h"
#endif

#include "sop_types.h"

/**
 * @brief YOLOv8 letterbox 预处理的几何参数。
 *
 * YOLOv8 训练/导出时通常固定输入为 640x640，但相机原图一般是 1280x720。
 * 如果直接强行 resize 到 640x640，物体会被拉伸，检测框回映射也会变形。
 * letterbox 的做法是：
 *   1. 按同一比例缩放原图，保持宽高比不变。
 *   2. 剩余区域补灰边，通常填 114。
 *   3. 后处理时用这里保存的 scale/pad 把模型坐标映射回原图坐标。
 */
struct LetterboxInfo {
  float scale = 1.0F;     // 原图缩放到模型输入图时使用的统一比例。
  int pad_x = 0;          // 缩放图左侧补边像素数。
  int pad_y = 0;          // 缩放图上侧补边像素数。
  int resized_width = 0;  // 保持比例缩放后的图像宽度。
  int resized_height = 0; // 保持比例缩放后的图像高度。
  int input_width = 0;    // 模型输入宽度，目前配置为 detector.input_size。
  int input_height = 0;   // 模型输入高度，目前配置为 detector.input_size。
};

/**
 * @brief YOLOv8 目标检测器。
 *
 * 这个类只负责“彩色图 -> 目标框”的完整链路：
 *   - rknn: 用 RKNN runtime 调用 RK3588 NPU。
 *
 * 这里没有把预处理、推理、后处理拆成更多类，原因是当前工程只有一个 YOLOv8 模型，
 * 拆太细会增加文件和对象关系，移植到 RK3588 时反而不方便定位问题。
 */
class Yolov8Detector {
 public:
  using Ptr = std::shared_ptr<Yolov8Detector>;

  /**
   * @brief 保存检测配置。
   * @param config 来自配置文件的 detector.* 字段。
   */
  explicit Yolov8Detector(const DetectorConfig& config);

  /**
   * @brief 释放 RKNN 上下文。
   *
   * RKNN context 是 C API 资源，不会被 C++ 自动释放，因此析构时统一调用 ReleaseRknn()。
   */
  ~Yolov8Detector();

  /**
   * @brief 初始化检测后端。
   * @return true 表示后端可用；false 表示模型、库或编译选项不满足。
   */
  bool Init();

  /**
   * @brief 对一帧图像执行目标检测。
   * @param frame 输入图像，模型前会统一转换为 RGB888 HWC 排列。
   * @param detections 输出目标列表，函数开头会 clear，避免混入上一帧结果。
   * @return true 表示推理链路执行成功；没有检测到目标也可以返回 true。
   */
  bool Detect(const ImageFrame& frame, std::vector<ObjectDetection>* detections);

  std::int64_t last_rknn_run_us() const {
    return last_rknn_run_us_;
  }

 private:
  /**
   * @brief RKNN 后端：预处理 -> NPU 推理 -> 后处理。
   */
  bool DetectWithRknn(const ImageFrame& frame, std::vector<ObjectDetection>* detections);

#if RK3588_SOP_ENABLE_RKNN
  /**
   * @brief 加载 .rknn 文件并创建 RKNN runtime context。
   *
   * 同时查询输入输出 tensor 属性。后续 DetectWithRknn() 会根据这些属性决定
   * 输入布局是 NHWC 还是 NCHW、输入类型是 UINT8 还是 FLOAT32。
   */
  bool InitRknn();

  /**
   * @brief 销毁 RKNN runtime context。
   */
  void ReleaseRknn();

  /**
   * @brief 解码 RKNN 输出张量为 ObjectDetection。
   *
   * AI-SOP 模型的 ONNX 输出已确认是 (1, 8, 8400)：
   *   4 个 box 数值 + 4 个类别分数，8400 个候选框。
   * RKNN 输出维度可能保持 (1,8,8400)，也可能转成 (1,8400,8)，所以这里会自动判断是否转置。
   */
  bool DecodeRknnOutputs(const std::vector<rknn_output>& outputs, const LetterboxInfo& letterbox,
                         int image_width, int image_height, std::vector<ObjectDetection>* detections) const;
#endif

  /**
   * @brief 执行 YOLOv8 letterbox 预处理。
   *
   * 输入是工程内部 BGR888，输出是 RGB888。RKNN 侧如果需要 NCHW 或 FLOAT32，
   * DetectWithRknn() 再做轻量转换，避免预处理函数承担太多后端细节。
   */
  bool PreprocessLetterbox(const ImageFrame& frame, std::vector<std::uint8_t>* input_data, LetterboxInfo* info) const;

  /**
   * @brief 从输入图像取一个像素，并转成 RGB。
   */
  void SampleFrameToRgb(const ImageFrame& frame, int src_x, int src_y, std::uint8_t* rgb) const;

  /**
   * @brief 解码单个 YOLOv8 输出矩阵。
   *
   * 支持两种常见布局：
   *   - [cx, cy, w, h, class0, class1, ...]
   *   - [cx, cy, w, h, obj, class0, class1, ...]
   */
  bool DecodeYolov8Output(const float* data, int rows, int cols, const LetterboxInfo& letterbox, int image_width,
                          int image_height, std::vector<ObjectDetection>* detections) const;

  /**
   * @brief 根据 RKNN 输出维度判断 rows/cols 和是否需要转置。
   *
   * 这个函数避免把 AI-SOP 输出形状写死，便于同一代码兼容 ONNX/RKNN 轻微布局差异。
   */
  bool ResolveOutputLayout(int float_count, const std::vector<int>& dims, int item_size, int* rows, int* cols,
                           bool* transposed) const;

  /**
   * @brief 把 YOLOv8 的中心点框从模型输入图坐标映射回原图坐标。
   */
  BoundingBox RestoreBox(float cx, float cy, float width, float height, const LetterboxInfo& letterbox,
                         int image_width, int image_height) const;

  /**
   * @brief 计算两个框的 IoU，用于 NMS。
   */
  float CalculateIou(const BoundingBox& lhs, const BoundingBox& rhs) const;

  /**
   * @brief 非极大值抑制，过滤同类别重复框。
   */
  std::vector<ObjectDetection> ApplyNms(const std::vector<ObjectDetection>& detections) const;

  DetectorConfig config_;  // 检测模型路径、类别名、阈值、输入尺寸等配置。
#if RK3588_SOP_ENABLE_RKNN
  rknn_context rknn_context_ = 0;                   // RKNN runtime 句柄，生命周期由本类管理。
  rknn_input_output_num rknn_io_num_{};             // 模型输入/输出 tensor 数量。
  std::vector<rknn_tensor_attr> rknn_input_attrs_;  // 输入 tensor 属性，决定数据布局和类型。
  std::vector<rknn_tensor_attr> rknn_output_attrs_; // 输出 tensor 属性，决定后处理如何解析。
  std::int64_t last_rknn_run_us_ = 0;               // RKNN runtime 报告的最近一次纯 NPU 推理耗时。
  bool rknn_initialized_ = false;                   // 防止未初始化就执行推理。
#endif
};

#endif  // TOOLCHAINS_RK3588_EXAMPLES_RK3588_SOP_INCLUDE_YOLOV8_DETECTOR_H_
