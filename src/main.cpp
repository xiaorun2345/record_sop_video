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
#include "object_tracker.h"
#include "sop_state_machine.h"
#include "time_utils.h"
#include "video_source.h"
#include "visualizer.h"
#include "yolov8_detector.h"

#if RK3588_SOP_HAS_OPENCV
#include <opencv2/opencv.hpp>
#endif

#if RK3588_SOP_HAS_OPENCV
static std::string FormatMs(const double value) {
  std::ostringstream stream;
  stream << std::fixed << std::setprecision(1) << value;
  return stream.str();
}

static cv::Mat MakeDisplayImage(const ImageFrame& frame) {
  cv::Mat image(frame.height, frame.width, CV_8UC3, const_cast<std::uint8_t*>(frame.bgr_data.data()));
  return image;
}

static void DrawRuntimeTimingPanel(ImageFrame* frame, const double yolo_ms, const double yolo_npu_ms,
                                   const double hand_ms, const double hand_det_npu_ms,
                                   const double hand_lm_npu_ms, const double crop_ms,
                                   const double capture_read_ms, const double frame_copy_ms,
                                   const double spatial_ms, const double state_ms,
                                   const double draw_ms, const double process_ms) {
  if (frame == nullptr || frame->bgr_data.empty()) {
    return;
  }
  cv::Mat image(frame->height, frame->width, CV_8UC3, frame->bgr_data.data());
  const int panel_width = std::min(300, std::max(240, frame->width / 3));
  const int x = std::max(0, frame->width - panel_width - 10);
  const int y = 10;
  const int line_height = 24;
  const int panel_height = 11 * line_height + 18;
  const cv::Rect panel_rect(x, y, panel_width, panel_height);
  const cv::Mat panel_src = image(panel_rect);
  cv::Mat panel_overlay = panel_src.clone();
  cv::rectangle(panel_overlay, cv::Rect(0, 0, panel_width, panel_height), cv::Scalar(18, 18, 18), -1);

  const double hand_npu_ms = hand_det_npu_ms + hand_lm_npu_ms;
  const double yolo_cpu_ms = std::max(0.0, yolo_ms - yolo_npu_ms);
  const double hand_cpu_ms = std::max(0.0, hand_ms - hand_npu_ms);
  const cv::Scalar title_color(255, 255, 255);
  const cv::Scalar cpu_color(0, 255, 255);
  const cv::Scalar npu_color(0, 255, 0);
  const cv::Scalar text_color(230, 230, 230);

  int text_y = y + 24;
  cv::putText(image, "TIMING ms", cv::Point(x + 10, text_y), cv::FONT_HERSHEY_SIMPLEX, 0.62, title_color, 2);
  text_y += line_height;
  cv::putText(image, "step", cv::Point(x + 10, text_y), cv::FONT_HERSHEY_SIMPLEX, 0.52, text_color, 1);
  cv::putText(image, "CPU", cv::Point(x + 112, text_y), cv::FONT_HERSHEY_SIMPLEX, 0.52, cpu_color, 1);
  cv::putText(image, "NPU", cv::Point(x + 202, text_y), cv::FONT_HERSHEY_SIMPLEX, 0.52, npu_color, 1);

  auto draw_line = [&](const std::string& name, const double cpu_ms, const double npu_ms) {
    text_y += line_height;
    cv::putText(panel_overlay, name, cv::Point(10, text_y - y), cv::FONT_HERSHEY_SIMPLEX, 0.5, text_color, 1);
    cv::putText(panel_overlay, FormatMs(cpu_ms), cv::Point(112, text_y - y), cv::FONT_HERSHEY_SIMPLEX, 0.5,
                cpu_color, 1);
    cv::putText(panel_overlay, FormatMs(npu_ms), cv::Point(202, text_y - y), cv::FONT_HERSHEY_SIMPLEX, 0.5,
                npu_color, 1);
  };

  draw_line("read", capture_read_ms + frame_copy_ms, 0.0);
  draw_line("yolo", yolo_cpu_ms, yolo_npu_ms);
  draw_line("hand", hand_cpu_ms, hand_npu_ms);
  draw_line("palm", 0.0, hand_det_npu_ms);
  draw_line("lmk", 0.0, hand_lm_npu_ms);
  draw_line("crop", crop_ms, 0.0);
  draw_line("3d", spatial_ms, 0.0);
  draw_line("state", state_ms, 0.0);
  draw_line("draw", draw_ms, 0.0);
  draw_line("process", process_ms, yolo_npu_ms + hand_npu_ms);

  cv::addWeighted(panel_overlay, 0.58, panel_src, 0.42, 0.0, panel_src);
  cv::rectangle(panel_src, cv::Rect(0, 0, panel_width, panel_height), cv::Scalar(80, 80, 80), 1);
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
 *   - 手部同时查询 wrist 和 palm，画面显示使用 palm。
 *
 * 这样做的优点是采样点固定、逻辑稳定、调试时容易对照画面。
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
    const HandLandmark& wrist = hand.landmarks[0];
    const int x = static_cast<int>(wrist.x * static_cast<float>(rgbd_frame.color.width));
    const int y = static_cast<int>(wrist.y * static_cast<float>(rgbd_frame.color.height));
    source.QueryPoint3D(rgbd_frame, x, y, &hand.wrist_position);
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
 * @brief 处理单帧 SOP 流程。
 *
 * 这一段是“主链路”：
 *   1. 同一帧图像分别送入物体检测和手部检测。
 *   2. 用深度图补 3D 坐标。
 *   3. 把感知结果交给状态机判断步骤是否推进。
 *   4. 把结果交给可视化和日志输出。
 *
 * 函数保持在 main.cpp 里，而没有再拆成多个对象，是因为这里本质上是控制流编排，
 * 过度封装会让你在 RK3588 现场排查时来回跳文件。
 */
bool ProcessFrame(RgbdFrame* rgbd_frame, const VideoSource& source, Yolov8Detector* detector,
                  HandPoseDetector* hand_detector, ObjectTracker* object_tracker, SopStateMachine* state_machine,
                  const Visualizer& visualizer) {
  if (rgbd_frame == nullptr || detector == nullptr || hand_detector == nullptr || object_tracker == nullptr ||
      state_machine == nullptr) {
    return false;
  }

  PerceptionResult result;
  result.frame_id = rgbd_frame->color.frame_id;
  result.timestamp_sec = rgbd_frame->color.timestamp_sec;
  result.depth_aligned_to_color = rgbd_frame->depth_aligned_to_color;

  std::vector<ObjectDetection> objects;
  std::vector<HandPose> hands;
  const std::chrono::steady_clock::time_point frame_begin = std::chrono::steady_clock::now();

  // 两个 RKNN context 串行执行，避免 YOLO 和手部模型同时抢同一个 NPU 队列。
  const std::chrono::steady_clock::time_point object_begin = std::chrono::steady_clock::now();
  const bool object_ok = detector->Detect(rgbd_frame->color, &objects);
  const std::chrono::steady_clock::time_point object_end = std::chrono::steady_clock::now();
  BoundingBox hand_crop_box{0, 0, rgbd_frame->color.width, rgbd_frame->color.height};
  const std::chrono::steady_clock::time_point crop_begin = std::chrono::steady_clock::now();
  ImageFrame hand_frame = MakeHandDetectionFrame(rgbd_frame->color, &hand_crop_box);
  const std::chrono::steady_clock::time_point crop_end = std::chrono::steady_clock::now();
  const std::chrono::steady_clock::time_point hand_begin = std::chrono::steady_clock::now();
  const bool hand_ok = hand_detector->Detect(hand_frame, &hands);
  const std::chrono::steady_clock::time_point hand_end = std::chrono::steady_clock::now();

  result.object_inference_ms = std::chrono::duration<double, std::milli>(object_end - object_begin).count();
  result.hand_inference_ms = std::chrono::duration<double, std::milli>(hand_end - hand_begin).count();
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

  result.objects = std::move(objects);
  result.hands = std::move(hands);

  const std::chrono::steady_clock::time_point spatial_begin = std::chrono::steady_clock::now();
  FillSpatialPosition(*rgbd_frame, source, &result);
  const std::chrono::steady_clock::time_point spatial_end = std::chrono::steady_clock::now();
  const std::chrono::steady_clock::time_point state_begin = std::chrono::steady_clock::now();
  state_machine->Update(result);
  const std::chrono::steady_clock::time_point state_end = std::chrono::steady_clock::now();
  const std::chrono::steady_clock::time_point draw_begin = std::chrono::steady_clock::now();
  visualizer.Draw(&rgbd_frame->color, result, *state_machine);
  const std::chrono::steady_clock::time_point draw_end = std::chrono::steady_clock::now();
  const std::chrono::steady_clock::time_point frame_end = std::chrono::steady_clock::now();

  const double crop_ms = std::chrono::duration<double, std::milli>(crop_end - crop_begin).count();
  const double spatial_ms = std::chrono::duration<double, std::milli>(spatial_end - spatial_begin).count();
  const double state_ms = std::chrono::duration<double, std::milli>(state_end - state_begin).count();
  const double draw_ms = std::chrono::duration<double, std::milli>(draw_end - draw_begin).count();
  const double process_ms = std::chrono::duration<double, std::milli>(frame_end - frame_begin).count();
  const double yolo_npu_ms = static_cast<double>(detector->last_rknn_run_us()) / 1000.0;
  const double hand_det_npu_ms = static_cast<double>(hand_detector->last_detector_rknn_run_us()) / 1000.0;
  const double hand_lm_npu_ms = static_cast<double>(hand_detector->last_landmark_rknn_run_us()) / 1000.0;

#if RK3588_SOP_HAS_OPENCV
  DrawRuntimeTimingPanel(&rgbd_frame->color, result.object_inference_ms, yolo_npu_ms,
                         result.hand_inference_ms, hand_det_npu_ms, hand_lm_npu_ms,
                         crop_ms, source.last_capture_read_ms(), source.last_frame_copy_ms(),
                         spatial_ms, state_ms, draw_ms, process_ms);
#endif

  const SopStepConfig* step = state_machine->CurrentStep();
  std::cout << "frame=" << rgbd_frame->color.frame_id;
  if (step != nullptr) {
    std::cout << ", step=" << step->id;
  } else {
    std::cout << ", perception_mode";
  }
  std::cout << ", objects=" << result.objects.size() << ", hands=" << result.hands.size()
            << ", yolo_ms=" << result.object_inference_ms << ", hand_ms=" << result.hand_inference_ms;
  if (detector->last_rknn_run_us() > 0) {
    std::cout << ", rknn_run_ms=" << yolo_npu_ms;
  }
  if (hand_detector->last_detector_rknn_run_us() > 0) {
    std::cout << ", hand_det_rknn_ms=" << hand_det_npu_ms;
  }
  if (hand_detector->last_landmark_rknn_run_us() > 0) {
    std::cout << ", hand_lm_rknn_ms=" << hand_lm_npu_ms;
  }
  std::cout << ", crop_ms=" << crop_ms
            << ", crop_box=" << hand_crop_box.x << "/" << hand_crop_box.y << "/"
            << hand_crop_box.width << "x" << hand_crop_box.height
            << ", cap_read_ms=" << source.last_capture_read_ms()
            << ", color_convert_ms=" << source.last_color_convert_ms()
            << ", frame_copy_ms=" << source.last_frame_copy_ms()
            << ", spatial_ms=" << spatial_ms << ", state_ms=" << state_ms
            << ", draw_ms=" << draw_ms << ", process_ms=" << process_ms;
  if (!result.objects.empty() && result.objects.front().position.valid) {
    std::cout << ", object_z=" << result.objects.front().position.z;
  }
  if (!result.hands.empty() && result.hands.front().wrist_position.valid) {
    std::cout << ", wrist_z=" << result.hands.front().wrist_position.z;
  }
  if (!result.hands.empty()) {
    const HandPose& hand = result.hands.front();
    // 现场排查手心 3D 时，同时打印二维落点和深度查询状态。
    if (hand.palm_pixel_valid) {
      std::cout << ", palm_xy=" << hand.palm_pixel.x << "/" << hand.palm_pixel.y;
    }
    if (hand.palm_position.valid) {
      std::cout << ", palm_z=" << hand.palm_position.z;
    } else {
      std::cout << ", palm_depth=invalid";
    }
  }
  std::cout << ", depth_aligned=" << (result.depth_aligned_to_color ? "true" : "false");
  std::cout << std::endl;

  const std::vector<SopAlert>& alerts = state_machine->state().alerts;
  for (const SopAlert& alert : alerts) {
    std::cout << "[" << alert.level << "] " << alert.step_id << ": " << alert.message << std::endl;
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

  // 当前先跑通 YOLO + 手部关键点，不启用 SOP 步骤和 ROI 约束。
  config.steps.clear();
  config.rois.clear();

  Yolov8Detector detector(config.detector);
  HandPoseDetector hand_detector(config.hand_pose);
  if (!detector.Init() || !hand_detector.Init()) {
    return 1;
  }

  SopStateMachine state_machine(config.steps);
  ObjectTracker object_tracker;
  state_machine.Reset(NowInSeconds());
  Visualizer visualizer;
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
    const std::chrono::steady_clock::time_point loop_begin = std::chrono::steady_clock::now();
    RgbdFrame frame;
    const std::chrono::steady_clock::time_point read_begin = std::chrono::steady_clock::now();
    if (!source.ReadRgbd(&frame)) {
      break;
    }
    const std::chrono::steady_clock::time_point read_end = std::chrono::steady_clock::now();
    if (!state_initialized_from_frame) {
      state_machine.Reset(frame.color.timestamp_sec);
      state_initialized_from_frame = true;
    }

    if (!ProcessFrame(&frame, source, &detector, &hand_detector, &object_tracker, &state_machine, visualizer)) {
      break;
    }

#if RK3588_SOP_HAS_OPENCV
    double show_ms = 0.0;
    double wait_ms = 0.0;
    double write_ms = 0.0;
    if (show_window && !frame.color.bgr_data.empty()) {
      cv::Mat image = MakeDisplayImage(frame.color);
      try {
        const std::chrono::steady_clock::time_point show_begin = std::chrono::steady_clock::now();
        cv::imshow("rk3588_sop", image);
        const std::chrono::steady_clock::time_point show_end = std::chrono::steady_clock::now();
        const int wait_delay_ms = 1;
        const std::chrono::steady_clock::time_point wait_begin = std::chrono::steady_clock::now();
        if (cv::waitKey(wait_delay_ms) == 27) {
          break;
        }
        const std::chrono::steady_clock::time_point wait_end = std::chrono::steady_clock::now();
        show_ms = std::chrono::duration<double, std::milli>(show_end - show_begin).count();
        wait_ms = std::chrono::duration<double, std::milli>(wait_end - wait_begin).count();
      } catch (const cv::Exception& error) {
        std::cerr << "窗口显示不可用，自动切换为无窗口日志模式: " << error.what() << std::endl;
        show_window = false;
      }
    }
    if (config.save_video && writer.isOpened() && !frame.color.bgr_data.empty()) {
      cv::Mat image = MakeDisplayImage(frame.color);
      const std::chrono::steady_clock::time_point write_begin = std::chrono::steady_clock::now();
      writer.write(image);
      const std::chrono::steady_clock::time_point write_end = std::chrono::steady_clock::now();
      write_ms = std::chrono::duration<double, std::milli>(write_end - write_begin).count();
    }
    const std::chrono::steady_clock::time_point loop_end = std::chrono::steady_clock::now();
    std::cout << "frame_io=" << frame.color.frame_id
              << ", read_total_ms=" << std::chrono::duration<double, std::milli>(read_end - read_begin).count()
              << ", show_ms=" << show_ms << ", wait_ms=" << wait_ms
              << ", write_ms=" << write_ms
              << ", loop_total_ms=" << std::chrono::duration<double, std::milli>(loop_end - loop_begin).count()
              << std::endl;
#endif
  }

  source.Close();
  return state_machine.state().finished ? 0 : 2;
}
