/*
 * name: xiaorun
 * email: 15610499173@163.com
 */

#include "visualizer.h"

#include <algorithm>
#include <array>
#include <iomanip>
#include <iostream>
#include <sstream>

#if RK3588_SOP_HAS_OPENCV
#include <opencv2/opencv.hpp>
#endif

static cv::Scalar IndexedColor(const int index) {
  static const std::array<cv::Scalar, 4> colors = {
      cv::Scalar(0, 255, 0),
      cv::Scalar(255, 0, 255),
      cv::Scalar(0, 165, 255),
      cv::Scalar(255, 255, 0),
  };
  return colors[static_cast<std::size_t>(index) % colors.size()];
}

static std::string FormatPoint3D(const Point3D& point) {
  std::ostringstream stream;
  stream << std::fixed << std::setprecision(2) << point.x << "," << point.y << "," << point.z << "m";
  return stream.str();
}

void Visualizer::Draw(ImageFrame* frame, const PerceptionResult& result, const SopStateMachine& state_machine) const {
  // OpenCV 显示、绘制和编码统一按 BGR 处理；Orbbec 输入则先从内部 RGB 转成 BGR。
#if RK3588_SOP_HAS_OPENCV
  if (frame == nullptr || frame->bgr_data.empty() || frame->width <= 0 || frame->height <= 0) {
    return;
  }

  const bool needs_rgb_to_bgr = frame->pixel_format == PixelFormat::RGB;
  cv::Mat image;
  cv::Mat converted;
  if (needs_rgb_to_bgr) {
    const cv::Mat rgb(frame->height, frame->width, CV_8UC3, frame->bgr_data.data());
    cv::cvtColor(rgb, converted, cv::COLOR_RGB2BGR);
    image = converted;
  } else {
    image = cv::Mat(frame->height, frame->width, CV_8UC3, frame->bgr_data.data());
  }
  // 检测框旁显示类别和 3D 坐标，便于检查 RGB-D 对齐效果。
  for (std::size_t i = 0; i < result.objects.size(); ++i) {
    const ObjectDetection& object = result.objects[i];
    const cv::Scalar color = IndexedColor(static_cast<int>(i));
    cv::Rect rect(object.box.x, object.box.y, object.box.width, object.box.height);
    cv::rectangle(image, rect, color, 4);
    std::string label = object.label;
    if (object.track_id >= 0) {
      label += " #" + std::to_string(object.track_id);
    }
    const int label_y = std::max(object.box.y - 8, 24);
    cv::putText(image, label, cv::Point(object.box.x, label_y), cv::FONT_HERSHEY_SIMPLEX, 0.8, color, 3);
    if (object.position.valid) {
      const cv::Point center(object.box.x + object.box.width / 2, object.box.y + object.box.height / 2);
      cv::circle(image, center, 6, color, -1);
      cv::circle(image, center, 8, cv::Scalar(255, 255, 255), 1);
      cv::putText(image, FormatPoint3D(object.position), cv::Point(center.x + 8, std::max(18, center.y - 8)),
                  cv::FONT_HERSHEY_SIMPLEX, 0.52, color, 2);
    }
  }
  // 手部关键点用于观察 RKNN 手部关键点输出是否稳定。
  for (std::size_t hand_index = 0; hand_index < result.hands.size(); ++hand_index) {
    const HandPose& hand = result.hands[hand_index];
    static const std::array<std::pair<int, int>, 23> hand_connections = {{
        {0, 1}, {1, 2}, {2, 3}, {3, 4},
        {0, 5}, {5, 6}, {6, 7}, {7, 8},
        {0, 9}, {9, 10}, {10, 11}, {11, 12},
        {0, 13}, {13, 14}, {14, 15}, {15, 16},
        {0, 17}, {17, 18}, {18, 19}, {19, 20},
        // 增加掌部横向连接，让 5、9、13、17 四个掌指关节点形成掌骨轮廓。
        {5, 9}, {9, 13}, {13, 17},
    }};
    const cv::Scalar hand_color = hand_index == 0U ? cv::Scalar(255, 255, 255) : cv::Scalar(255, 255, 0);
    if (hand.box.width > 0 && hand.box.height > 0) {
      cv::Rect hand_rect(hand.box.x, hand.box.y, hand.box.width, hand.box.height);
      cv::rectangle(image, hand_rect, hand_color, 2);
      std::string hand_label = "hand " + std::to_string(hand_index);
      if (hand.skeleton_track_id >= 0) {
        hand_label += " #" + std::to_string(hand.skeleton_track_id);
      }
      hand_label += " " + std::to_string(hand.score);
      cv::putText(image, hand_label, cv::Point(hand.box.x, std::max(20, hand.box.y - 6)),
                  cv::FONT_HERSHEY_SIMPLEX, 0.5, hand_color, 1);
    }
    std::vector<cv::Point> points;
    std::vector<bool> point_has_3d;
    points.reserve(hand.landmarks.size());
    point_has_3d.reserve(hand.landmarks.size());
    for (std::size_t landmark_index = 0; landmark_index < hand.landmarks.size(); ++landmark_index) {
      const HandLandmark& landmark = hand.landmarks[landmark_index];
      if (landmark_index < hand.joints_3d.size()) {
        const HandJoint3D& joint = hand.joints_3d[landmark_index];
        // 3D 有效性以 constrained_position 为准，不能因为 SDK 投影失败就丢掉真实三维状态。
        const bool has_constrained_3d = joint.constrained_position.valid;
        if (joint.projected_pixel_valid) {
          const ImagePoint& pixel = joint.projected_pixel;
          points.emplace_back(pixel.x, pixel.y);
        } else {
          // 投影失败时仍使用 RKNN 二维位置显示，避免把“投影失败”误报成“深度查询失败”。
          points.emplace_back(static_cast<int>(landmark.x * frame->width),
                             static_cast<int>(landmark.y * frame->height));
        }
        point_has_3d.push_back(has_constrained_3d);
      } else {
        // 当前点没有有效三维位置时回退到 RKNN 二维点，保证骨架不会整段消失。
        points.emplace_back(static_cast<int>(landmark.x * frame->width),
                            static_cast<int>(landmark.y * frame->height));
        point_has_3d.push_back(false);
      }
    }
    for (const std::pair<int, int>& connection : hand_connections) {
      if (connection.first < static_cast<int>(points.size()) && connection.second < static_cast<int>(points.size())) {
        const bool constrained_line = point_has_3d[static_cast<std::size_t>(connection.first)] &&
                                      point_has_3d[static_cast<std::size_t>(connection.second)];
        const cv::Scalar line_color = constrained_line ? hand_color : cv::Scalar(140, 140, 140);
        cv::line(image, points[connection.first], points[connection.second], line_color, constrained_line ? 3 : 1);
      }
    }
    for (std::size_t i = 0; i < points.size(); ++i) {
      const int radius = i == 0U ? 3 : 2;
      cv::Scalar joint_color(255, 0, 0);
      if (i < hand.joints_3d.size() && point_has_3d[i]) {
        // 橙色表示遮挡预测点，黄色表示由当前帧真实深度参与约束的点。
        joint_color = hand.joints_3d[i].predicted ? cv::Scalar(0, 165, 255) : cv::Scalar(0, 255, 255);
      }
      cv::circle(image, points[i], radius, joint_color, -1);
      cv::circle(image, points[i], radius + 1, cv::Scalar(255, 255, 255), 1);
    }

    if (hand.palm_pixel_valid) {
      // 黄色十字始终表示实际查询深度的手心像素。
      cv::Point palm_center(hand.palm_pixel.x, hand.palm_pixel.y);
      cv::drawMarker(image, palm_center, cv::Scalar(0, 0, 0), cv::MARKER_CROSS, 32, 7);
      cv::drawMarker(image, palm_center, cv::Scalar(0, 255, 255), cv::MARKER_CROSS, 28, 4);
      if (hand.palm_position.valid) {
        cv::circle(image, palm_center, 14, cv::Scalar(0, 255, 0), 3);
        cv::putText(image, "palm " + FormatPoint3D(hand.palm_position),
                    cv::Point(palm_center.x + 10, std::max(18, palm_center.y - 10)),
                    cv::FONT_HERSHEY_SIMPLEX, 0.52, cv::Scalar(0, 255, 0), 2);
      } else {
        // 深度无效时仍然画出手心二维点，方便判断是落点问题还是深度图空洞。
        cv::putText(image, "palm no depth",
                    cv::Point(palm_center.x + 10, std::max(18, palm_center.y - 10)),
                    cv::FONT_HERSHEY_SIMPLEX, 0.52, cv::Scalar(0, 0, 255), 2);
      }
    }
  }
  const SopStepConfig* step = state_machine.CurrentStep();
  // OpenCV Hershey 字体不支持中文，画面上只使用 ASCII 文本。
  const std::string step_text = state_machine.steps().empty() ? "RK3588 RGB-D Perception"
                                                              : (step == nullptr ? "SOP finished" : "Step: " + step->id);
  // 标题采用紧凑的双线字体，并增加深色描边，保证明暗画面下都清晰可读。
  cv::putText(image, step_text, cv::Point(18, 36), cv::FONT_HERSHEY_DUPLEX, 0.72, cv::Scalar(20, 20, 20), 4);
  cv::putText(image, step_text, cv::Point(18, 36), cv::FONT_HERSHEY_DUPLEX, 0.72, cv::Scalar(255, 255, 255), 1);
  int alert_y = 80;
  for (const SopAlert& alert : state_machine.state().alerts) {
    const std::string alert_text = alert.level + ": " + alert.step_id;
    cv::putText(image, alert_text, cv::Point(20, alert_y), cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 0, 255), 2);
    alert_y += 32;
  }

  if (needs_rgb_to_bgr) {
    frame->bgr_data.assign(image.data, image.data + image.total() * image.elemSize());
    frame->pixel_format = PixelFormat::BGR;
  }
#else
  (void)frame;
  (void)result;
  const SopStepConfig* step = state_machine.CurrentStep();
  // 当前步骤和告警放在图像左上角，便于现场拍照留档。
  if (step != nullptr) {
    std::cout << "当前 SOP 步骤: " << step->name << std::endl;
  }
#endif
}
