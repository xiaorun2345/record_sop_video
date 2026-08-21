/*
 * name: xiaorun
 * email: 15610499173@163.com
 */

#ifndef TOOLCHAINS_RK3588_EXAMPLES_RK3588_SOP_INCLUDE_HAND_POSE_DETECTOR_H_
#define TOOLCHAINS_RK3588_EXAMPLES_RK3588_SOP_INCLUDE_HAND_POSE_DETECTOR_H_

#include <memory>
#include <vector>

#include "sop_types.h"

#if RK3588_SOP_ENABLE_RKNN
#include "rknn_api.h"
#endif

/**
 * @brief 手部关键点检测器。
 *
 * 输出是 std::vector<HandPose>：每只手包含左右手分类、置信度和 21 个关键点。
 * 实机配置使用 RKNN palm detector + hand landmark 模型。
 * 本类内部统一输出项目自己的 HandPose 结构，外部 SOP 流程不依赖具体后端类型。
 */
class HandPoseDetector {
 public:
  using Ptr = std::shared_ptr<HandPoseDetector>;

  /**
   * @brief 保存手部检测配置。
   */
  explicit HandPoseDetector(const HandPoseConfig& config);

  ~HandPoseDetector();

  /**
   * @brief 初始化手部检测后端。
   */
  bool Init();

  /**
   * @brief 检测一帧图像中的手部关键点。
   * @param frame 输入图像，模型前会统一转换为 RGB888。
   * @param hands 输出手部列表，函数开头会 clear。
   */
  bool Detect(const ImageFrame& frame, std::vector<HandPose>* hands);

  std::int64_t last_detector_rknn_run_us() const {
#if RK3588_SOP_ENABLE_RKNN
    return last_detector_rknn_run_us_;
#else
    return 0;
#endif
  }

  std::int64_t last_landmark_rknn_run_us() const {
#if RK3588_SOP_ENABLE_RKNN
    return last_landmark_rknn_run_us_;
#else
    return 0;
#endif
  }

 private:
  /**
   * @brief RKNN palm detector + hand landmark 后端。
   */
  bool DetectWithRknn(const ImageFrame& frame, std::vector<HandPose>* hands);

#if RK3588_SOP_ENABLE_RKNN
  bool InitRknn();
  void ReleaseRknn();

  rknn_context rknn_detector_context_ = 0;
  rknn_context rknn_landmark_context_ = 0;
  rknn_input_output_num rknn_detector_io_num_{};
  rknn_input_output_num rknn_landmark_io_num_{};
  rknn_tensor_attr rknn_detector_input_attr_{};
  rknn_tensor_attr rknn_landmark_input_attr_{};
  std::vector<rknn_tensor_attr> rknn_detector_output_attrs_;
  std::vector<rknn_tensor_attr> rknn_landmark_output_attrs_;
  std::int64_t last_detector_rknn_run_us_ = 0;
  std::int64_t last_landmark_rknn_run_us_ = 0;
  bool rknn_initialized_ = false;
#endif

  HandPoseConfig config_;  // 模型路径、最大手数、检测阈值等配置。
};

#endif  // TOOLCHAINS_RK3588_EXAMPLES_RK3588_SOP_INCLUDE_HAND_POSE_DETECTOR_H_
