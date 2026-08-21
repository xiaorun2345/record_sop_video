/*
 * name: xiaorun
 * email: 15610499173@163.com
 */

#include "object_tracker.h"

#include "Hungarian.h"

#include <algorithm>
#include <vector>

void ObjectTracker::Update(std::vector<ObjectDetection>* detections) {
  if (detections == nullptr) {
    return;
  }

  if (detections->empty()) {
    for (Track& track : tracks_) {
      ++track.missed_frames;
    }
    tracks_.erase(std::remove_if(tracks_.begin(), tracks_.end(), [&](const Track& track) {
                    return track.missed_frames > max_missed_frames_;
                  }),
                  tracks_.end());
    return;
  }

  for (ObjectDetection& detection : *detections) {
    detection.track_id = -1;
  }

  if (tracks_.empty()) {
    for (ObjectDetection& detection : *detections) {
      Track track;
      track.id = next_id_++;
      track.label = detection.label;
      track.box = detection.box;
      tracks_.push_back(track);
      detection.track_id = track.id;
    }
    return;
  }

  std::vector<std::vector<double>> cost_matrix(detections->size(), std::vector<double>(tracks_.size(), 1000.0));
  for (std::size_t detection_index = 0; detection_index < detections->size(); ++detection_index) {
    for (std::size_t track_index = 0; track_index < tracks_.size(); ++track_index) {
      if (tracks_[track_index].label != (*detections)[detection_index].label) {
        continue;
      }
      const float iou = CalculateIou(tracks_[track_index].box, (*detections)[detection_index].box);
      cost_matrix[detection_index][track_index] = 1.0 - static_cast<double>(iou);
    }
  }

  HungarianAlgorithm hungarian;
  std::vector<int> assignment;
  hungarian.Solve(cost_matrix, assignment);

  std::vector<bool> matched_tracks(tracks_.size(), false);
  for (std::size_t detection_index = 0; detection_index < detections->size(); ++detection_index) {
    ObjectDetection& detection = (*detections)[detection_index];
    const int track_index = detection_index < assignment.size() ? assignment[detection_index] : -1;
    if (track_index >= 0 && static_cast<std::size_t>(track_index) < tracks_.size()) {
      Track& track = tracks_[static_cast<std::size_t>(track_index)];
      const float iou = CalculateIou(track.box, detection.box);
      if (track.label != detection.label || iou < match_iou_threshold_) {
        continue;
      }
      track.box = detection.box;
      track.missed_frames = 0;
      detection.track_id = track.id;
      matched_tracks[static_cast<std::size_t>(track_index)] = true;
    }
  }

  for (ObjectDetection& detection : *detections) {
    if (detection.track_id >= 0) {
      continue;
    }
    Track track;
    track.id = next_id_++;
    track.label = detection.label;
    track.box = detection.box;
    tracks_.push_back(track);
    matched_tracks.push_back(true);
    detection.track_id = track.id;
  }

  for (std::size_t i = 0; i < tracks_.size(); ++i) {
    if (!matched_tracks[i]) {
      ++tracks_[i].missed_frames;
    }
  }
  tracks_.erase(std::remove_if(tracks_.begin(), tracks_.end(), [&](const Track& track) {
                  return track.missed_frames > max_missed_frames_;
                }),
                tracks_.end());
}

float ObjectTracker::CalculateIou(const BoundingBox& lhs, const BoundingBox& rhs) {
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
  return union_area > 0.0F ? inter_area / union_area : 0.0F;
}
