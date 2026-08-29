/*
 * name: xiaorun
 * email: 15610499173@163.com
 */

#include <chrono>
#include <algorithm>
#include <array>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "config_loader.h"
#include "geometry_utils.h"
#include "hand_pose_detector.h"
#include "hand_skeleton_constraint.h"
#include "object_tracker.h"
#include "serial_light_controller.h"
#include "sop_state_machine.h"
#include "time_utils.h"
#include "video_source.h"
#include "visualizer.h"
#include "yolov8_detector.h"

#if RK3588_SOP_HAS_OPENCV
#include <opencv2/opencv.hpp>

static cv::Mat ResizeForPreview(const cv::Mat& image, const int max_width, const int max_height) {
  if (image.empty() || max_width <= 0 || max_height <= 0) {
    return image;
  }
  const double scale_w = static_cast<double>(max_width) / static_cast<double>(image.cols);
  const double scale_h = static_cast<double>(max_height) / static_cast<double>(image.rows);
  const double scale = std::min(scale_w, scale_h);
  if (scale <= 0.0 || std::abs(scale - 1.0) < 1e-6) {
    return image;
  }
  cv::Mat preview;
  cv::resize(image, preview, cv::Size(), scale, scale, cv::INTER_AREA);
  return preview;
}

static cv::Mat MakeDisplayImage(const ImageFrame& frame) {
  cv::Mat image(frame.height, frame.width, CV_8UC3, const_cast<std::uint8_t*>(frame.bgr_data.data()));
  return image;
}

static std::string FormatMs(const double value) {
  std::ostringstream stream;
  stream << std::fixed << std::setprecision(1) << value;
  return stream.str();
}

static void DrawCheckMark(cv::Mat* image, const cv::Rect& rect, const bool ok) {
  if (image == nullptr) {
    return;
  }
  const cv::Scalar color = ok ? cv::Scalar(98, 214, 154) : cv::Scalar(244, 119, 119);
  cv::rectangle(*image, rect, color, 1);
  if (ok) {
    cv::line(*image, cv::Point(rect.x + 3, rect.y + rect.height / 2),
             cv::Point(rect.x + rect.width / 2 - 1, rect.y + rect.height - 3), color, 2);
    cv::line(*image, cv::Point(rect.x + rect.width / 2 - 1, rect.y + rect.height - 3),
             cv::Point(rect.x + rect.width - 3, rect.y + 3), color, 2);
  } else {
    cv::line(*image, cv::Point(rect.x + 3, rect.y + 3), cv::Point(rect.x + rect.width - 3, rect.y + rect.height - 3),
             color, 2);
    cv::line(*image, cv::Point(rect.x + rect.width - 3, rect.y + 3), cv::Point(rect.x + 3, rect.y + rect.height - 3),
             color, 2);
  }
}

static void DrawStatusPanel(cv::Mat* image, const PerceptionResult& result, const SopStateMachine& state_machine,
                            const FrameProcessMetrics& metrics) {
  if (image == nullptr || image->empty()) {
    return;
  }
  (void)result;

  const SopStepConfig* step = state_machine.CurrentStep();
  const SopStepConfig* checklist_step = step;
  if (checklist_step == nullptr && !state_machine.steps().empty()) {
    checklist_step = &state_machine.steps().back();
  }
  const bool has_checklist = checklist_step != nullptr && !checklist_step->required_objects.empty();

  static const std::array<std::pair<const char*, double FrameProcessMetrics::*>, 11> timing_rows = {{
      {"read", &FrameProcessMetrics::read_ms},
      {"yolo_npu", &FrameProcessMetrics::yolo_npu_ms},
      {"yolo", &FrameProcessMetrics::object_inference_ms},
      {"palm_npu", &FrameProcessMetrics::hand_det_npu_ms},
      {"hand", &FrameProcessMetrics::hand_inference_ms},
      {"lmk_npu", &FrameProcessMetrics::hand_lm_npu_ms},
      {"crop", &FrameProcessMetrics::crop_ms},
      {"3d", &FrameProcessMetrics::spatial_ms},
      {"state", &FrameProcessMetrics::state_ms},
      {"draw", &FrameProcessMetrics::draw_ms},
      {"process", &FrameProcessMetrics::process_ms},
  }};

  const int timing_rows_count = static_cast<int>(timing_rows.size());
  const int checklist_rows = has_checklist ? static_cast<int>(checklist_step->required_objects.size()) : 0;
  const int rows = std::max(timing_rows_count, checklist_rows);
  const int panel_width = std::min(520, std::max(360, image->cols / 2));
  const int line_height = 18;
  const int header_height = 34;
  const int panel_height = std::min(image->rows - 20, header_height + rows * line_height + 10);
  if (panel_height <= 0) {
    return;
  }

  const int x = std::max(10, image->cols - panel_width - 10);
  const int y = 10;
  const cv::Rect panel_rect(x, y, panel_width, panel_height);
  cv::Mat panel_src = (*image)(panel_rect);
  cv::Mat panel_overlay = panel_src.clone();
  cv::rectangle(panel_overlay, cv::Rect(0, 0, panel_width, panel_height), cv::Scalar(18, 18, 18), -1);
  cv::rectangle(panel_overlay, cv::Rect(0, 0, panel_width, panel_height), cv::Scalar(80, 80, 80), 1);

  const bool finished = state_machine.state().finished;
  cv::putText(panel_overlay, "TIMING / CHECK", cv::Point(12, 22), cv::FONT_HERSHEY_SIMPLEX, 0.52,
              cv::Scalar(255, 255, 255), 1);
  if (step != nullptr) {
    cv::putText(panel_overlay, step->id, cv::Point(panel_width - 110, 22), cv::FONT_HERSHEY_SIMPLEX, 0.5,
                finished ? cv::Scalar(98, 214, 154) : cv::Scalar(65, 216, 232), 1);
  }

  cv::putText(panel_overlay, "time", cv::Point(12, 38), cv::FONT_HERSHEY_SIMPLEX, 0.42, cv::Scalar(130, 130, 130), 1);
  cv::putText(panel_overlay, "check", cv::Point(panel_width - 92, 38), cv::FONT_HERSHEY_SIMPLEX, 0.42,
              cv::Scalar(130, 130, 130), 1);

  const int left_x = 12;
  const int value_x = 84;
  const int right_x = panel_width / 2 + 12;
  const int right_value_x = right_x + 70;
  int row_y = 56;
  for (int i = 0; i < rows; ++i) {
    if (i < timing_rows_count) {
      const auto& item = timing_rows[static_cast<std::size_t>(i)];
      cv::putText(panel_overlay, item.first, cv::Point(left_x, row_y), cv::FONT_HERSHEY_SIMPLEX, 0.42,
                  cv::Scalar(230, 230, 230), 1);
      const std::string value = FormatMs(metrics.*(item.second));
      cv::putText(panel_overlay, value, cv::Point(value_x, row_y), cv::FONT_HERSHEY_SIMPLEX, 0.42,
                  cv::Scalar(65, 216, 232), 1);
    }

    if (has_checklist && i < checklist_rows) {
      const RequiredObjectConfig& required_object = checklist_step->required_objects[static_cast<std::size_t>(i)];
      int current_count = CountObjectLabel(result.objects, required_object.label);
      const std::vector<int>& max_counts = state_machine.state().required_object_max_counts;
      const std::size_t checklist_index = static_cast<std::size_t>(i);
      if (checklist_index < max_counts.size()) {
        current_count = std::max(current_count, max_counts[checklist_index]);
      }
      const bool satisfied = current_count >= required_object.min_count;
      DrawCheckMark(&panel_overlay, cv::Rect(right_x, row_y - 11, 12, 12), satisfied);
      const cv::Scalar text_color = satisfied ? cv::Scalar(98, 214, 154) : cv::Scalar(244, 119, 119);
      const std::string line_text = required_object.label + " " + std::to_string(current_count) + "/" +
                                    std::to_string(required_object.min_count);
      cv::putText(panel_overlay, line_text, cv::Point(right_value_x, row_y), cv::FONT_HERSHEY_SIMPLEX, 0.4,
                  text_color, 1);
    }

    row_y += line_height;
  }

  cv::addWeighted(panel_overlay, 0.62, panel_src, 0.38, 0.0, panel_src);
}
#endif

/**
 * @brief 把裁剪图归一化关键点映射回整张原图归一化坐标。
 */
static void RemapHandsFromCrop(const BoundingBox& crop_box, const int image_width, const int image_height,
                               std::vector<HandPose>* hands) {
  if (hands == nullptr || image_width <= 0 || image_height <= 0) {
    return;
  }
  for (HandPose& hand : *hands) {
    for (HandLandmark& landmark : hand.landmarks) {
      landmark.x = (static_cast<float>(crop_box.x) + landmark.x * static_cast<float>(crop_box.width)) /
                   static_cast<float>(image_width);
      landmark.y = (static_cast<float>(crop_box.y) + landmark.y * static_cast<float>(crop_box.height)) /
                   static_cast<float>(image_height);
    }
  }
}

static ImageFrame MakeHandDetectionFrame(const ImageFrame& frame, BoundingBox* crop_box) {
  BoundingBox full_box{0, 0, frame.width, frame.height};
  if (crop_box != nullptr) {
    *crop_box = full_box;
  }
  return frame;
}

static bool GetPalmCenterPixel(const HandPose& hand, const int image_width, const int image_height, ImagePoint* point) {
  if (point == nullptr || image_width <= 0 || image_height <= 0 || hand.landmarks.empty()) {
    return false;
  }
  static const std::array<int, 5> palm_indices = {0, 5, 9, 13, 17};
  float sum_x = 0.0F;
  float sum_y = 0.0F;
  int count = 0;
  for (const int index : palm_indices) {
    if (index >= static_cast<int>(hand.landmarks.size())) {
      continue;
    }
    sum_x += hand.landmarks[static_cast<std::size_t>(index)].x;
    sum_y += hand.landmarks[static_cast<std::size_t>(index)].y;
    ++count;
  }
  if (count == 0) {
    return false;
  }
  point->x = std::max(0, std::min(image_width - 1, static_cast<int>((sum_x / static_cast<float>(count)) *
                                                                    static_cast<float>(image_width))));
  point->y = std::max(0, std::min(image_height - 1, static_cast<int>((sum_y / static_cast<float>(count)) *
                                                                     static_cast<float>(image_height))));
  return true;
}

static bool LandmarkToPixel(const HandPose& hand, const int landmark_index, const int image_width,
                            const int image_height, ImagePoint* point) {
  if (point == nullptr || landmark_index < 0 || landmark_index >= static_cast<int>(hand.landmarks.size()) ||
      image_width <= 0 || image_height <= 0) {
    return false;
  }
  const HandLandmark& landmark = hand.landmarks[static_cast<std::size_t>(landmark_index)];
  point->x = std::max(0, std::min(image_width - 1, static_cast<int>(landmark.x * static_cast<float>(image_width))));
  point->y = std::max(0, std::min(image_height - 1, static_cast<int>(landmark.y * static_cast<float>(image_height))));
  return true;
}

static bool AverageValidPoints3D(const std::vector<Point3D>& points, Point3D* output) {
  if (output == nullptr) {
    return false;
  }
  float sum_x = 0.0F;
  float sum_y = 0.0F;
  float sum_z = 0.0F;
  int count = 0;
  for (const Point3D& point : points) {
    if (!point.valid) {
      continue;
    }
    sum_x += point.x;
    sum_y += point.y;
    sum_z += point.z;
    ++count;
  }
  if (count == 0) {
    return false;
  }
  output->x = sum_x / static_cast<float>(count);
  output->y = sum_y / static_cast<float>(count);
  output->z = sum_z / static_cast<float>(count);
  output->valid = true;
  return true;
}

static bool QueryPalmPosition3D(const RgbdFrame& rgbd_frame, const VideoSource& source, const HandPose& hand,
                                const ImagePoint& palm_center, Point3D* palm_position) {
  if (palm_position == nullptr) {
    return false;
  }

  // 手心平均点优先；如果这个点正好落在深度空洞，再查询掌部关键点。
  if (source.QueryPoint3D(rgbd_frame, palm_center.x, palm_center.y, palm_position)) {
    return true;
  }

  static const std::array<int, 5> palm_indices = {0, 5, 9, 13, 17};
  std::vector<Point3D> valid_points;
  valid_points.reserve(palm_indices.size());
  for (const int index : palm_indices) {
    ImagePoint pixel;
    Point3D point;
    if (LandmarkToPixel(hand, index, rgbd_frame.color.width, rgbd_frame.color.height, &pixel) &&
        source.QueryPoint3D(rgbd_frame, pixel.x, pixel.y, &point)) {
      valid_points.push_back(point);
    }
  }

  // 只要掌部关键点中有有效深度，就用这些点的平均 3D 作为手心空间位置。
  if (AverageValidPoints3D(valid_points, palm_position)) {
    return true;
  }

  // 如果关键点都落在深度空洞，再在手框内部均匀采样。
  // 这一步只用于手部，避免扩大所有目标的深度搜索范围后误取背景。
  if (hand.box.width > 0 && hand.box.height > 0) {
    valid_points.clear();
    constexpr int kGridSize = 5;
    for (int row = 1; row <= kGridSize; ++row) {
      for (int column = 1; column <= kGridSize; ++column) {
        const int x = hand.box.x + hand.box.width * column / (kGridSize + 1);
        const int y = hand.box.y + hand.box.height * row / (kGridSize + 1);
        Point3D point;
        if (source.QueryPoint3D(rgbd_frame, x, y, &point)) {
          valid_points.push_back(point);
        }
      }
    }
    if (AverageValidPoints3D(valid_points, palm_position)) {
      return true;
    }
  }

  return false;
}

/**
 * @brief 给感知结果补充三维位置。
 *
 * 这个函数只做一件事：把 2D 检测结果补成能用于状态机判断的 3D 信息。
 *
 * 约定很简单：
 *   - 物体用检测框中心点取深度。
 *   - 手部查询 21 个关键点、wrist 和 palm，关键点使用小邻域避免取到背景。
 *
 * 这里只负责原始三维观测，不在这里保存历史或执行骨骼约束。
 */
void FillSpatialPosition(const RgbdFrame& rgbd_frame, const VideoSource& source, PerceptionResult* result) {
  if (result == nullptr) {
    return;
  }

  for (ObjectDetection& object : result->objects) {
    const ImagePoint center = GetBoxCenter(object.box);
    source.QueryPoint3D(rgbd_frame, center.x, center.y, &object.position);
  }

  for (HandPose& hand : result->hands) {
    if (hand.landmarks.empty()) {
      continue;
    }
    // 21 个关键点逐点查询真实深度。手指使用小邻域，避免深度空洞时取到远处背景。
    const std::size_t joint_count = std::min(hand.landmarks.size(), hand.joints_3d.size());
    for (std::size_t joint_index = 0; joint_index < joint_count; ++joint_index) {
      ImagePoint pixel;
      Point3D measured_position;
      if (LandmarkToPixel(hand, static_cast<int>(joint_index), rgbd_frame.color.width,
                          rgbd_frame.color.height, &pixel)) {
        source.QueryPoint3D(rgbd_frame, pixel.x, pixel.y, 3, &measured_position);
      }
      hand.joints_3d[joint_index].measured_position = measured_position;
    }
    hand.wrist_position = hand.joints_3d[0].measured_position;
    ImagePoint palm_center;
    if (GetPalmCenterPixel(hand, rgbd_frame.color.width, rgbd_frame.color.height, &palm_center)) {
      // 保存手心二维点，保证画图和查询深度使用同一个像素坐标。
      hand.palm_pixel = palm_center;
      hand.palm_pixel_valid = true;
      QueryPalmPosition3D(rgbd_frame, source, hand, palm_center, &hand.palm_position);
    }
  }
}

/**
 * @brief 把约束后的 21 个三维关节投影回当前彩色图。
 */
static void ProjectConstrainedHands(const RgbdFrame& rgbd_frame, const VideoSource& source,
                                    std::vector<HandPose>* hands) {
  if (hands == nullptr) {
    return;
  }
  for (HandPose& hand : *hands) {
    for (HandJoint3D& joint : hand.joints_3d) {
      joint.projected_pixel_valid = source.ProjectPointToColor(
          rgbd_frame, joint.constrained_position, &joint.projected_pixel);
    }
  }
}

/**
 * @brief 处理单帧 SOP 流程。
 *
 * 这一段是“主链路”：
 *   1. 同一帧图像分别送入物体检测和手部检测。
 *   2. 用深度图补充目标和手部 21 点的原始 3D 坐标。
 *   3. 执行手部骨骼约束，并把约束后三维点投影回彩色图。
 *   4. 把感知结果交给状态机判断步骤是否推进。
 *   5. 把结果交给可视化和日志输出。
 *
 * 函数保持在 main.cpp 里，而没有再拆成多个对象，是因为这里本质上是控制流编排，
 * 过度封装会让你在 RK3588 现场排查时来回跳文件。
 */
bool ProcessFrame(RgbdFrame* rgbd_frame, const VideoSource& source, Yolov8Detector* detector,
                  HandPoseDetector* hand_detector, HandSkeletonConstraint* hand_skeleton_constraint,
                  ObjectTracker* object_tracker, SopStateMachine* state_machine,
                  SerialLightController* serial_light, const Visualizer& visualizer,
                  FrameProcessMetrics* metrics) {
  if (rgbd_frame == nullptr || detector == nullptr || hand_detector == nullptr || object_tracker == nullptr ||
      hand_skeleton_constraint == nullptr || state_machine == nullptr) {
    return false;
  }

  PerceptionResult result;
  result.frame_id = rgbd_frame->color.frame_id;
  result.timestamp_sec = rgbd_frame->color.timestamp_sec;
  result.depth_aligned_to_color = rgbd_frame->depth_aligned_to_color;

  std::vector<ObjectDetection> objects;
  std::vector<HandPose> hands;
  const auto process_begin = std::chrono::steady_clock::now();

  // 两个 RKNN context 串行执行，避免 YOLO 和手部模型同时抢同一个 NPU 队列。
  const auto yolo_begin = std::chrono::steady_clock::now();
  const bool object_ok = detector->Detect(rgbd_frame->color, &objects);
  if (metrics != nullptr) {
    metrics->object_inference_ms = std::chrono::duration<double, std::milli>(
                                      std::chrono::steady_clock::now() - yolo_begin)
                                      .count();
    metrics->yolo_npu_ms = static_cast<double>(detector->last_rknn_run_us()) / 1000.0;
  }
  BoundingBox hand_crop_box{0, 0, rgbd_frame->color.width, rgbd_frame->color.height};
  const auto crop_begin = std::chrono::steady_clock::now();
  ImageFrame hand_frame = MakeHandDetectionFrame(rgbd_frame->color, &hand_crop_box);
  const auto crop_end = std::chrono::steady_clock::now();
  const auto hand_begin = std::chrono::steady_clock::now();
  const bool hand_ok = hand_detector->Detect(hand_frame, &hands);
  if (metrics != nullptr) {
    metrics->crop_ms = std::chrono::duration<double, std::milli>(crop_end - crop_begin).count();
    metrics->hand_inference_ms = std::chrono::duration<double, std::milli>(
                                     std::chrono::steady_clock::now() - hand_begin).count();
    metrics->hand_det_npu_ms = static_cast<double>(hand_detector->last_detector_rknn_run_us()) / 1000.0;
    metrics->hand_lm_npu_ms = static_cast<double>(hand_detector->last_landmark_rknn_run_us()) / 1000.0;
  }
  result.synchronized = object_ok && hand_ok;
  if (!object_ok) {
    std::cerr << "YOLOv8 检测失败" << std::endl;
    return false;
  }
  if (!hand_ok) {
    std::cerr << "手部关键点检测失败" << std::endl;
    return false;
  }
  RemapHandsFromCrop(hand_crop_box, rgbd_frame->color.width, rgbd_frame->color.height, &hands);
  object_tracker->Update(&objects);

  const auto spatial_begin = std::chrono::steady_clock::now();
  result.objects = std::move(objects);
  result.hands = std::move(hands);

  FillSpatialPosition(*rgbd_frame, source, &result);
  hand_skeleton_constraint->Update(result.timestamp_sec, &result.hands);
  ProjectConstrainedHands(*rgbd_frame, source, &result.hands);
  if (metrics != nullptr) {
    metrics->spatial_ms = std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now() - spatial_begin)
                               .count();
  }

  const auto state_begin = std::chrono::steady_clock::now();
  state_machine->Update(result);
  if (metrics != nullptr) {
    metrics->state_ms = std::chrono::duration<double, std::milli>(
                             std::chrono::steady_clock::now() - state_begin)
                             .count();
  }

  const auto draw_begin = std::chrono::steady_clock::now();
  visualizer.Draw(&rgbd_frame->color, result, *state_machine);
#if RK3588_SOP_HAS_OPENCV
  if (metrics != nullptr && !rgbd_frame->color.bgr_data.empty()) {
    cv::Mat image = MakeDisplayImage(rgbd_frame->color);
    DrawStatusPanel(&image, result, *state_machine, *metrics);
  }
#endif
  if (metrics != nullptr) {
    metrics->draw_ms = std::chrono::duration<double, std::milli>(
                           std::chrono::steady_clock::now() - draw_begin)
                           .count();
    metrics->process_ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - process_begin)
                              .count();
  }

  const bool two_hands_alert = result.hands.size() >= 2;
  if (serial_light != nullptr) {
    serial_light->SetAlert(two_hands_alert);
  }
  if (two_hands_alert) {
    std::cout << "[warning] two_hands: 检测到 2 个手部目标，触发串口报警灯" << std::endl;
  }
  return true;
}

/**
 * @brief SOP 程序入口。
 *
 * 参数约定：
 *   argv[1]：配置文件路径，默认 config/sop_config.txt。
 * 主流程顺序很固定：
 *   1. 读取配置。
 *   2. 初始化目标检测器和手部检测器。
 *   3. 打开视频输入。
 *   4. 循环读帧、推理、状态机更新、可视化。
 */
int main(int argc, char** argv) {
  const std::string default_config_path = "config/sop_config.txt";
  const std::string config_path = argc > 1 ? argv[1] : default_config_path;

  SopAppConfig config;
  ConfigLoader loader;
  if (!loader.Load(config_path, &config)) {
    std::cerr << "加载配置失败: " << config_path << std::endl;
    return 1;
  }

  Yolov8Detector detector(config.detector);
  HandPoseDetector hand_detector(config.hand_pose);
  if (!detector.Init() || !hand_detector.Init()) {
    return 1;
  }

  SopStateMachine state_machine(config.steps);
  ObjectTracker object_tracker;
  HandSkeletonConstraint hand_skeleton_constraint(config.hand_skeleton);
  state_machine.Reset(NowInSeconds());
  Visualizer visualizer;
  SerialLightController serial_light(config.serial_light.device_path, config.serial_light.baud_rate);
  SerialLightController* serial_light_ptr = nullptr;
  if (config.serial_light.enabled) {
    serial_light_ptr = &serial_light;
    if (!serial_light.Open()) {
      std::cerr << "串口报警灯不可用，SOP 主流程继续运行" << std::endl;
    }
  }
  bool show_window = config.show_window;
  bool state_initialized_from_frame = false;

  // 打开视频源。config.input.type 决定是 video/camera/gstreamer/orbbec。
  // 这里要求真实输入必须成功，避免把部署问题伪装成算法成功。
  VideoSource source(config.input);
  const bool source_ok = source.Open();
  if (!source_ok) {
    std::cerr << "视频输入打开失败，实机流程停止。请检查 input.type、相机、SDK 和权限配置" << std::endl;
    return 1;
  }

#if RK3588_SOP_HAS_OPENCV
  cv::VideoWriter writer;
  if (config.save_video) {
    const std::filesystem::path output_path(config.output_video_path);
    if (output_path.has_parent_path()) {
      std::filesystem::create_directories(output_path.parent_path());
    }
    writer.open(config.output_video_path, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), config.input.fps,
                cv::Size(config.input.width, config.input.height));
    if (!writer.isOpened()) {
      std::filesystem::path fallback_path = output_path;
      fallback_path.replace_extension(".avi");
      writer.open(fallback_path.string(), cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), config.input.fps,
                  cv::Size(config.input.width, config.input.height));
      if (writer.isOpened()) {
        std::cout << "结果视频保存到: " << fallback_path.string() << std::endl;
      }
    } else {
      std::cout << "结果视频保存到: " << config.output_video_path << std::endl;
    }
    if (!writer.isOpened()) {
      std::cerr << "结果视频保存打开失败: " << config.output_video_path << std::endl;
    }
  }
#endif

  while (!state_machine.state().finished) {
    RgbdFrame frame;
    if (!source.ReadRgbd(&frame)) {
      break;
    }
    if (!state_initialized_from_frame) {
      state_machine.Reset(frame.color.timestamp_sec);
      state_initialized_from_frame = true;
    }

    FrameProcessMetrics metrics;
    if (source.last_capture_read_ms() > 0.0) {
      metrics.read_ms = source.last_capture_read_ms() + source.last_color_convert_ms() + source.last_frame_copy_ms();
    }
    if (!ProcessFrame(&frame, source, &detector, &hand_detector, &hand_skeleton_constraint,
                      &object_tracker, &state_machine, serial_light_ptr, visualizer, &metrics)) {
      break;
    }

#if RK3588_SOP_HAS_OPENCV
    if (show_window && !frame.color.bgr_data.empty()) {
      cv::Mat image = MakeDisplayImage(frame.color);
      try {
        cv::Mat preview = ResizeForPreview(image, 1280, 720);
        cv::imshow("rk3588_sop", preview);
        const int wait_delay_ms = 1;
        if (cv::waitKey(wait_delay_ms) == 27) {
          break;
        }
      } catch (const cv::Exception& error) {
        std::cerr << "窗口显示不可用，自动切换为无窗口日志模式: " << error.what() << std::endl;
        show_window = false;
      }
    }
    if (config.save_video && writer.isOpened() && !frame.color.bgr_data.empty()) {
      cv::Mat image = MakeDisplayImage(frame.color);
      writer.write(image);
    }
#endif
  }

  source.Close();
  serial_light.TurnOff();
  return state_machine.state().finished ? 0 : 2;
}
