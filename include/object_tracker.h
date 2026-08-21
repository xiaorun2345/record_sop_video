/*
 * name: xiaorun
 * email: 15610499173@163.com
 */

#ifndef TOOLCHAINS_RK3588_EXAMPLES_RK3588_SOP_INCLUDE_OBJECT_TRACKER_H_
#define TOOLCHAINS_RK3588_EXAMPLES_RK3588_SOP_INCLUDE_OBJECT_TRACKER_H_

#include <string>
#include <vector>

#include "sop_types.h"

class ObjectTracker {
 public:
  void Update(std::vector<ObjectDetection>* detections);

 private:
  struct Track {
    int id = -1;
    std::string label;
    BoundingBox box;
    int missed_frames = 0;
  };

  static float CalculateIou(const BoundingBox& lhs, const BoundingBox& rhs);

  std::vector<Track> tracks_;
  int next_id_ = 1;
  float match_iou_threshold_ = 0.3F;
  int max_missed_frames_ = 15;
};

#endif  // TOOLCHAINS_RK3588_EXAMPLES_RK3588_SOP_INCLUDE_OBJECT_TRACKER_H_
