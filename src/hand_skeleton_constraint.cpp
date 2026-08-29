/*
 * name: xiaorun
 * email: 15610499173@163.com
 */

#include "hand_skeleton_constraint.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

constexpr std::size_t kJointCount = 21;
constexpr std::size_t kBoneCount = 20;

// 连接顺序与标准 21 点手部拓扑一致，每根手指从手腕或掌指关节指向指尖。
const std::array<std::pair<int, int>, kBoneCount> kBones = {{
    {0, 1}, {1, 2}, {2, 3}, {3, 4},
    {0, 5}, {5, 6}, {6, 7}, {7, 8},
    {0, 9}, {9, 10}, {10, 11}, {11, 12},
    {0, 13}, {13, 14}, {14, 15}, {15, 16},
    {0, 17}, {17, 18}, {18, 19}, {19, 20},
}};

const std::array<std::array<int, 5>, 5> kFingerChains = {{
    {{0, 1, 2, 3, 4}},
    {{0, 5, 6, 7, 8}},
    {{0, 9, 10, 11, 12}},
    {{0, 13, 14, 15, 16}},
    {{0, 17, 18, 19, 20}},
}};

float ClampValue(const float value, const float lower, const float upper) {
  return std::max(lower, std::min(upper, value));
}

bool IsFinitePoint(const Point3D& point) {
  return point.valid && std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z) &&
         point.z > 0.08F && point.z < 5.0F;
}

Point3D AddPoint(const Point3D& lhs, const Point3D& rhs) {
  return Point3D{lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z, lhs.valid && rhs.valid};
}

Point3D SubtractPoint(const Point3D& lhs, const Point3D& rhs) {
  return Point3D{lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z, lhs.valid && rhs.valid};
}

Point3D ScalePoint(const Point3D& point, const float scale) {
  return Point3D{point.x * scale, point.y * scale, point.z * scale, point.valid};
}

float DotPoint(const Point3D& lhs, const Point3D& rhs) {
  return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

Point3D CrossPoint(const Point3D& lhs, const Point3D& rhs) {
  return Point3D{lhs.y * rhs.z - lhs.z * rhs.y,
                 lhs.z * rhs.x - lhs.x * rhs.z,
                 lhs.x * rhs.y - lhs.y * rhs.x,
                 lhs.valid && rhs.valid};
}

float PointLength(const Point3D& point) {
  return std::sqrt(DotPoint(point, point));
}

float PointDistance(const Point3D& lhs, const Point3D& rhs) {
  return PointLength(SubtractPoint(lhs, rhs));
}

Point3D NormalizePoint(const Point3D& point) {
  const float length = PointLength(point);
  if (!point.valid || length < 1.0e-6F) {
    return Point3D{};
  }
  return Point3D{point.x / length, point.y / length, point.z / length, true};
}

Point3D BlendPoint(const Point3D& previous, const Point3D& current, const float current_weight) {
  if (!previous.valid) {
    return current;
  }
  if (!current.valid) {
    return previous;
  }
  const float weight = ClampValue(current_weight, 0.0F, 1.0F);
  return Point3D{previous.x * (1.0F - weight) + current.x * weight,
                 previous.y * (1.0F - weight) + current.y * weight,
                 previous.z * (1.0F - weight) + current.z * weight,
                 true};
}

float MedianValue(const std::vector<float>& values) {
  if (values.empty()) {
    return 0.0F;
  }
  std::vector<float> sorted = values;
  std::sort(sorted.begin(), sorted.end());
  const std::size_t middle = sorted.size() / 2U;
  if (sorted.size() % 2U == 0U) {
    return (sorted[middle - 1U] + sorted[middle]) * 0.5F;
  }
  return sorted[middle];
}

}  // 匿名命名空间

HandSkeletonConstraint::HandSkeletonConstraint(const HandSkeletonConfig& config) : config_(config) {
  config_.calibration_frames = std::max(5, config_.calibration_frames);
  config_.smoothing = ClampValue(config_.smoothing, 0.0F, 1.0F);
  config_.max_prediction_frames = std::max(0, config_.max_prediction_frames);
}

void HandSkeletonConstraint::Reset() {
  tracks_.clear();
  next_track_id_ = 1;
}

void HandSkeletonConstraint::Update(const double timestamp_sec, std::vector<HandPose>* hands) {
  if (hands == nullptr) {
    return;
  }

  if (!config_.enabled) {
    // 关闭约束时仍复制原始三维点，使后续投影和可视化不需要特殊分支。
    for (HandPose& hand : *hands) {
      for (HandJoint3D& joint : hand.joints_3d) {
        joint.constrained_position = joint.measured_position;
        joint.confidence = joint.measured_position.valid ? hand.score : 0.0F;
        joint.predicted = false;
      }
    }
    return;
  }

  std::vector<bool> used_tracks(tracks_.size(), false);
  std::vector<int> assigned_tracks(hands->size(), -1);
  if (hands->size() == 2U && tracks_.size() >= 2U) {
    // 双手数量很小，枚举不同历史组合可以得到全局最小代价，避免贪心匹配交换左右手 ID。
    float best_cost = std::numeric_limits<float>::max();
    for (std::size_t first_track = 0; first_track < tracks_.size(); ++first_track) {
      for (std::size_t second_track = 0; second_track < tracks_.size(); ++second_track) {
        if (first_track == second_track) {
          continue;
        }
        const float first_cost = CalculateTrackCost((*hands)[0], tracks_[first_track]);
        const float second_cost = CalculateTrackCost((*hands)[1], tracks_[second_track]);
        if (first_cost <= 2.5F && second_cost <= 2.5F && first_cost + second_cost < best_cost) {
          best_cost = first_cost + second_cost;
          assigned_tracks[0] = static_cast<int>(first_track);
          assigned_tracks[1] = static_cast<int>(second_track);
        }
      }
    }
  }

  for (std::size_t hand_index = 0; hand_index < hands->size(); ++hand_index) {
    HandPose& hand = (*hands)[hand_index];
    int track_index = assigned_tracks[hand_index];
    if (track_index < 0) {
      track_index = MatchTrack(hand, used_tracks);
    }
    if (track_index < 0) {
      track_index = CreateTrack(hand);
      used_tracks.push_back(false);
    }
    used_tracks[static_cast<std::size_t>(track_index)] = true;
    HandTrack& track = tracks_[static_cast<std::size_t>(track_index)];
    ProcessHand(timestamp_sec, &track, &hand);
    track.missed_frames = 0;
    track.box = hand.box;
  }

  // 没有匹配到的历史手只保留有限帧，防止离开画面后旧姿态长期占用状态。
  for (std::size_t index = 0; index < tracks_.size(); ++index) {
    if (index >= used_tracks.size() || !used_tracks[index]) {
      ++tracks_[index].missed_frames;
    }
  }
  tracks_.erase(std::remove_if(tracks_.begin(), tracks_.end(), [&](const HandTrack& track) {
                  return track.missed_frames > config_.max_prediction_frames;
                }),
                tracks_.end());
}

int HandSkeletonConstraint::MatchTrack(const HandPose& hand, const std::vector<bool>& used_tracks) const {
  int best_index = -1;
  float best_cost = std::numeric_limits<float>::max();
  for (std::size_t index = 0; index < tracks_.size(); ++index) {
    if (index < used_tracks.size() && used_tracks[index]) {
      continue;
    }
    const float cost = CalculateTrackCost(hand, tracks_[index]);
    if (cost < best_cost) {
      best_cost = cost;
      best_index = static_cast<int>(index);
    }
  }

  // 手框中心相距超过约两个手框宽度时视为新手，避免把离场历史错误套到另一只手。
  return best_cost <= 2.5F ? best_index : -1;
}

float HandSkeletonConstraint::CalculateTrackCost(const HandPose& hand, const HandTrack& track) const {
  const float hand_center_x = static_cast<float>(hand.box.x) + static_cast<float>(hand.box.width) * 0.5F;
  const float hand_center_y = static_cast<float>(hand.box.y) + static_cast<float>(hand.box.height) * 0.5F;
  const float track_center_x = static_cast<float>(track.box.x) + static_cast<float>(track.box.width) * 0.5F;
  const float track_center_y = static_cast<float>(track.box.y) + static_cast<float>(track.box.height) * 0.5F;
  const float dx = hand_center_x - track_center_x;
  const float dy = hand_center_y - track_center_y;
  const float hand_scale = static_cast<float>(std::max(1, std::max(hand.box.width, hand.box.height)));
  const float normalized_distance = std::sqrt(dx * dx + dy * dy) / hand_scale;
  return 1.0F - CalculateBoxIou(hand.box, track.box) + normalized_distance;
}

int HandSkeletonConstraint::CreateTrack(const HandPose& hand) {
  HandTrack track;
  track.id = next_track_id_++;
  track.box = hand.box;
  tracks_.push_back(track);
  return static_cast<int>(tracks_.size() - 1U);
}

void HandSkeletonConstraint::ProcessHand(const double timestamp_sec, HandTrack* track, HandPose* hand) {
  if (track == nullptr || hand == nullptr) {
    return;
  }
  hand->skeleton_track_id = track->id;

  const double delta_sec = track->last_timestamp_sec > 0.0 ? timestamp_sec - track->last_timestamp_sec : 0.0;
  const float maximum_motion = delta_sec > 0.0 ? std::max(0.08F, static_cast<float>(delta_sec) * 3.0F) : 0.25F;
  std::array<Point3D, kJointCount> working_positions;
  std::array<float, kJointCount> confidence{};

  // 手腕深度是掌部最稳定的参考；没有手腕时退回手心，避免把背景深度带进手指。
  float reference_depth = 0.0F;
  if (IsFinitePoint(hand->joints_3d[0].measured_position)) {
    reference_depth = hand->joints_3d[0].measured_position.z;
  } else if (IsFinitePoint(hand->palm_position)) {
    reference_depth = hand->palm_position.z;
  }

  for (std::size_t index = 0; index < kJointCount; ++index) {
    HandJoint3D& joint = hand->joints_3d[index];
    Point3D measured = joint.measured_position;
    bool measurement_valid = IsFinitePoint(measured);
    if (measurement_valid && reference_depth > 0.0F && std::abs(measured.z - reference_depth) > 0.30F) {
      measurement_valid = false;
    }
    if (measurement_valid && track->has_pose && track->positions[index].valid &&
        PointDistance(measured, track->positions[index]) > maximum_motion) {
      measurement_valid = false;
    }

    if (measurement_valid) {
      working_positions[index] = measured;
      confidence[index] = ClampValue(hand->score, 0.0F, 1.0F);
      joint.predicted = false;
      track->prediction_age[index] = 0;
    } else if (track->has_pose && track->positions[index].valid &&
               track->prediction_age[index] < config_.max_prediction_frames) {
      // 遮挡点先沿用上一帧受约束姿态，后面的固定骨长迭代会随可见父关节一起移动。
      working_positions[index] = track->positions[index];
      confidence[index] = 0.20F;
      joint.predicted = true;
      ++track->prediction_age[index];
    } else {
      working_positions[index] = Point3D{};
      confidence[index] = 0.0F;
      joint.predicted = false;
    }
    joint.confidence = confidence[index];
  }

  // 相邻骨段在一帧内翻转超过 120 度通常来自二维误检或错误深度，而不是正常手指运动。
  // 这种观测保留在 measured_position 供排查，但约束输入退回上一帧姿态。
  if (track->has_pose) {
    for (const std::pair<int, int>& bone : kBones) {
      const std::size_t parent_index = static_cast<std::size_t>(bone.first);
      const std::size_t child_index = static_cast<std::size_t>(bone.second);
      if (confidence[parent_index] <= 0.0F || confidence[child_index] <= 0.0F ||
          !track->positions[parent_index].valid || !track->positions[child_index].valid) {
        continue;
      }
      const Point3D current_direction = NormalizePoint(
          SubtractPoint(working_positions[child_index], working_positions[parent_index]));
      const Point3D previous_direction = NormalizePoint(
          SubtractPoint(track->positions[child_index], track->positions[parent_index]));
      if (current_direction.valid && previous_direction.valid &&
          DotPoint(current_direction, previous_direction) < -0.5F) {
        if (track->prediction_age[child_index] < config_.max_prediction_frames) {
          working_positions[child_index] = track->positions[child_index];
          confidence[child_index] = 0.20F;
          hand->joints_3d[child_index].confidence = 0.20F;
          hand->joints_3d[child_index].predicted = true;
          ++track->prediction_age[child_index];
        } else {
          working_positions[child_index] = Point3D{};
          confidence[child_index] = 0.0F;
          hand->joints_3d[child_index].confidence = 0.0F;
          hand->joints_3d[child_index].predicted = false;
        }
      }
    }
  }

  UpdateBoneLengths(*hand, track);
  ApplyBoneConstraints(*track, confidence, &working_positions);
  LimitInvalidJointBending(&working_positions);

  std::array<Point3D, kJointCount> smoothed_positions = working_positions;
  for (std::size_t index = 0; index < kJointCount; ++index) {
    if (smoothed_positions[index].valid && track->has_pose && track->positions[index].valid) {
      // 所有点统一做轻量时序平滑；原始测量仍保留，方便对照是否产生过度滞后。
      smoothed_positions[index] = BlendPoint(track->positions[index], smoothed_positions[index], config_.smoothing);
    }
  }

  // 线性平滑可能轻微缩短旋转中的骨段，因此输出前再恢复一次固定骨长。
  ApplyBoneConstraints(*track, confidence, &smoothed_positions);
  LimitInvalidJointBending(&smoothed_positions);
  ApplyBoneConstraints(*track, confidence, &smoothed_positions);
  for (std::size_t index = 0; index < kJointCount; ++index) {
    hand->joints_3d[index].constrained_position = smoothed_positions[index];
    track->positions[index] = smoothed_positions[index];
  }

  track->has_pose = true;
  track->last_timestamp_sec = timestamp_sec;
  if (hand->joints_3d[0].constrained_position.valid) {
    hand->wrist_position = hand->joints_3d[0].constrained_position;
  }
  CalculateDirections(track, hand);
}

void HandSkeletonConstraint::UpdateBoneLengths(const HandPose& hand, HandTrack* track) {
  if (track == nullptr) {
    return;
  }
  for (std::size_t bone_index = 0; bone_index < kBoneCount; ++bone_index) {
    const int parent_index = kBones[bone_index].first;
    const int child_index = kBones[bone_index].second;
    const HandJoint3D& parent_joint = hand.joints_3d[static_cast<std::size_t>(parent_index)];
    const HandJoint3D& child_joint = hand.joints_3d[static_cast<std::size_t>(child_index)];
    if (parent_joint.predicted || child_joint.predicted ||
        parent_joint.confidence <= 0.0F || child_joint.confidence <= 0.0F) {
      continue;
    }
    const Point3D& parent = parent_joint.measured_position;
    const Point3D& child = child_joint.measured_position;
    if (!IsFinitePoint(parent) || !IsFinitePoint(child)) {
      continue;
    }

    const float length = PointDistance(parent, child);
    if (length < 0.005F || length > 0.15F) {
      continue;
    }
    std::vector<float>& samples = track->bone_samples[bone_index];
    const float current_length = MedianValue(samples);
    if (samples.size() >= 5U && (length < current_length * 0.65F || length > current_length * 1.45F)) {
      // 已有初步骨长后拒绝明显离群值，防止背景深度污染后续标定。
      continue;
    }
    if (samples.size() < static_cast<std::size_t>(config_.calibration_frames)) {
      samples.push_back(length);
      track->bone_lengths[bone_index] = MedianValue(samples);
    }
  }
}

void HandSkeletonConstraint::ApplyBoneConstraints(const HandTrack& track,
                                                  const std::array<float, kJointCount>& confidence,
                                                  std::array<Point3D, kJointCount>* positions) const {
  if (positions == nullptr) {
    return;
  }

  // 少量迭代即可在 RK3588 CPU 上实时运行；每次只处理 20 根骨骼，不引入重型优化库。
  constexpr int kConstraintIterations = 6;
  for (int iteration = 0; iteration < kConstraintIterations; ++iteration) {
    for (std::size_t bone_index = 0; bone_index < kBoneCount; ++bone_index) {
      const int parent_index = kBones[bone_index].first;
      const int child_index = kBones[bone_index].second;
      Point3D& parent = (*positions)[static_cast<std::size_t>(parent_index)];
      Point3D& child = (*positions)[static_cast<std::size_t>(child_index)];
      const float target_length = track.bone_lengths[bone_index];
      if (!parent.valid || !child.valid || target_length <= 0.0F) {
        continue;
      }
      const Point3D delta = SubtractPoint(child, parent);
      const float current_length = PointLength(delta);
      if (current_length < 1.0e-6F) {
        continue;
      }

      const Point3D correction = ScalePoint(delta, (current_length - target_length) / current_length);
      // 置信度越低，位置越允许被骨长约束移动；手腕作为整只手的根节点只做很小修正。
      float parent_mobility = parent_index == 0 ? 0.005F : std::max(0.05F, 1.0F - confidence[parent_index]);
      float child_mobility = std::max(0.05F, 1.0F - confidence[child_index]);
      const float mobility_sum = parent_mobility + child_mobility;
      parent_mobility /= mobility_sum;
      child_mobility /= mobility_sum;
      parent = AddPoint(parent, ScalePoint(correction, parent_mobility));
      child = SubtractPoint(child, ScalePoint(correction, child_mobility));
      parent.valid = true;
      child.valid = true;
    }
  }
}

void HandSkeletonConstraint::LimitInvalidJointBending(std::array<Point3D, kJointCount>* positions) const {
  if (positions == nullptr) {
    return;
  }
  constexpr float kMinimumJointAngleRad = 12.0F * 3.14159265358979323846F / 180.0F;
  const float maximum_cosine = std::cos(kMinimumJointAngleRad);

  for (const std::array<int, 5>& finger : kFingerChains) {
    for (int joint_offset = 1; joint_offset <= 3; ++joint_offset) {
      const int parent_index = finger[static_cast<std::size_t>(joint_offset - 1)];
      const int joint_index = finger[static_cast<std::size_t>(joint_offset)];
      const int child_index = finger[static_cast<std::size_t>(joint_offset + 1)];
      Point3D& parent = (*positions)[static_cast<std::size_t>(parent_index)];
      Point3D& joint = (*positions)[static_cast<std::size_t>(joint_index)];
      Point3D& child = (*positions)[static_cast<std::size_t>(child_index)];
      if (!parent.valid || !joint.valid || !child.valid) {
        continue;
      }

      const Point3D toward_parent = NormalizePoint(SubtractPoint(parent, joint));
      const Point3D toward_child = NormalizePoint(SubtractPoint(child, joint));
      const float cosine = DotPoint(toward_parent, toward_child);
      if (!toward_parent.valid || !toward_child.valid || cosine <= maximum_cosine) {
        continue;
      }

      // 遮挡预测点如果折回到父骨骼方向，保留原来的弯曲平面并把夹角拉回生理下限。
      Point3D perpendicular = SubtractPoint(toward_child, ScalePoint(toward_parent, cosine));
      perpendicular = NormalizePoint(perpendicular);
      if (!perpendicular.valid) {
        const Point3D fallback_axis{1.0F, 0.0F, 0.0F, true};
        perpendicular = NormalizePoint(CrossPoint(toward_parent, fallback_axis));
        if (!perpendicular.valid) {
          perpendicular = Point3D{0.0F, 1.0F, 0.0F, true};
        }
      }
      const Point3D limited_direction =
          AddPoint(ScalePoint(toward_parent, maximum_cosine),
                   ScalePoint(perpendicular, std::sin(kMinimumJointAngleRad)));
      const float child_length = PointDistance(joint, child);
      child = AddPoint(joint, ScalePoint(NormalizePoint(limited_direction), child_length));
      child.valid = true;
    }
  }
}

void HandSkeletonConstraint::CalculateDirections(HandTrack* track, HandPose* hand) const {
  if (track == nullptr || hand == nullptr) {
    return;
  }
  static const std::array<std::pair<int, int>, 5> direction_endpoints = {{
      {1, 4}, {5, 8}, {9, 12}, {13, 16}, {17, 20},
  }};
  for (std::size_t finger_index = 0; finger_index < direction_endpoints.size(); ++finger_index) {
    const Point3D& start = hand->joints_3d[static_cast<std::size_t>(direction_endpoints[finger_index].first)]
                               .constrained_position;
    const Point3D& end = hand->joints_3d[static_cast<std::size_t>(direction_endpoints[finger_index].second)]
                             .constrained_position;
    hand->directions.finger_direction[finger_index] = NormalizePoint(SubtractPoint(end, start));
  }

  const Point3D& wrist = hand->joints_3d[0].constrained_position;
  const Point3D& index_mcp = hand->joints_3d[5].constrained_position;
  const Point3D& pinky_mcp = hand->joints_3d[17].constrained_position;
  Point3D palm_normal = NormalizePoint(CrossPoint(SubtractPoint(index_mcp, wrist),
                                                  SubtractPoint(pinky_mcp, wrist)));
  if (palm_normal.valid && track->palm_normal.valid && DotPoint(palm_normal, track->palm_normal) < 0.0F) {
    palm_normal = ScalePoint(palm_normal, -1.0F);
  }
  hand->directions.palm_normal = palm_normal;
  track->palm_normal = palm_normal;
}

float HandSkeletonConstraint::CalculateBoxIou(const BoundingBox& lhs, const BoundingBox& rhs) {
  const int intersection_x1 = std::max(lhs.x, rhs.x);
  const int intersection_y1 = std::max(lhs.y, rhs.y);
  const int intersection_x2 = std::min(lhs.x + lhs.width, rhs.x + rhs.width);
  const int intersection_y2 = std::min(lhs.y + lhs.height, rhs.y + rhs.height);
  const int intersection_width = std::max(0, intersection_x2 - intersection_x1);
  const int intersection_height = std::max(0, intersection_y2 - intersection_y1);
  const float intersection_area = static_cast<float>(intersection_width * intersection_height);
  const float union_area = static_cast<float>(lhs.width * lhs.height + rhs.width * rhs.height) - intersection_area;
  return union_area > 0.0F ? intersection_area / union_area : 0.0F;
}
