/*
 * name: xiaorun
 * email: 15610499173@163.com
 */

#include "geometry_utils.h"

ImagePoint GetBoxCenter(const BoundingBox& box) {
  // 直接整数除法即可：SOP 只需要像素级中心点，不需要亚像素精度。
  return ImagePoint{box.x + box.width / 2, box.y + box.height / 2};
}

bool IsPointInRoi(const ImagePoint& point, const RoiRegion& roi) {
  // 射线法判断点是否在多边形 ROI 内。
  // 这是个很适合工程落地的小算法：实现短、依赖少、对任意凸/凹多边形都可用。
  bool inside = false;
  const int count = static_cast<int>(roi.points.size());
  for (int i = 0, j = count - 1; i < count; j = i++) {
    const ImagePoint& pi = roi.points[i];
    const ImagePoint& pj = roi.points[j];
    const bool intersect = ((pi.y > point.y) != (pj.y > point.y)) &&
                           (point.x < (pj.x - pi.x) * (point.y - pi.y) / static_cast<double>(pj.y - pi.y) + pi.x);
    if (intersect) {
      inside = !inside;
    }
  }
  return inside;
}

const RoiRegion* FindRoiByName(const std::vector<RoiRegion>& rois, const std::string& name) {
  // 返回 const 指针，表示只读访问；调用方不会误改 ROI 配置。
  for (const RoiRegion& roi : rois) {
    if (roi.name == name) {
      return &roi;
    }
  }
  return nullptr;
}

bool HasObjectLabel(const std::vector<ObjectDetection>& objects, const std::string& label) {
  // 线性扫描就够了。每帧目标数量很小，没必要引入哈希结构增加复杂度。
  for (const ObjectDetection& object : objects) {
    if (object.label == label) {
      return true;
    }
  }
  return false;
}

int CountObjectLabel(const std::vector<ObjectDetection>& objects, const std::string& label) {
  int count = 0;
  for (const ObjectDetection& object : objects) {
    if (object.label == label) {
      ++count;
    }
  }
  return count;
}

bool HasHandInRoi(const std::vector<HandPose>& hands, const RoiRegion& roi, const int image_width, const int image_height) {
  // 手部关键点归一化坐标转成图像像素坐标后再做 ROI 判断。
  // 这里只看 wrist 点，因为 SOP 里最关心的是“手是否进入工位区域”。
  for (const HandPose& hand : hands) {
    if (hand.landmarks.empty()) {
      continue;
    }
    const HandLandmark& wrist = hand.landmarks[0];
    const ImagePoint point{static_cast<int>(wrist.x * image_width), static_cast<int>(wrist.y * image_height)};
    if (IsPointInRoi(point, roi)) {
      return true;
    }
  }
  return false;
}
