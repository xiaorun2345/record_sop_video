/*
 * name: xiaorun
 * email: 15610499173@163.com
 */

#ifndef TOOLCHAINS_RK3588_EXAMPLES_RK3588_SOP_INCLUDE_HAND_SKELETON_CONSTRAINT_H_
#define TOOLCHAINS_RK3588_EXAMPLES_RK3588_SOP_INCLUDE_HAND_SKELETON_CONSTRAINT_H_

#include <array>
#include <utility>
#include <vector>

#include "sop_types.h"

/**
 * @brief 手部 21 点三维骨骼约束。
 *
 * 本类只处理工程内部数据，不依赖 RKNN、OpenCV 或深度相机 SDK。
 * 输入是已经完成深度查询的 HandPose，输出仍写回同一个 HandPose，
 * 因此检测、深度查询、运动学约束和可视化之间保持清晰边界。
 */
class HandSkeletonConstraint {
 public:
  explicit HandSkeletonConstraint(const HandSkeletonConfig& config);

  /**
   * @brief 清除骨长标定结果和全部手部历史。
   */
  void Reset();

  /**
   * @brief 更新一帧手部三维姿态。
   * @param timestamp_sec 当前帧时间戳，单位秒。
   * @param hands 已完成 21 点深度查询的手部列表。
   */
  void Update(double timestamp_sec, std::vector<HandPose>* hands);

 private:
  static constexpr std::size_t kJointCount = 21;
  static constexpr std::size_t kBoneCount = 20;

  struct HandTrack {
    int id = -1;
    BoundingBox box;
    double last_timestamp_sec = 0.0;
    int missed_frames = 0;
    bool has_pose = false;
    std::array<Point3D, kJointCount> positions;
    std::array<int, kJointCount> prediction_age{};
    std::array<float, kBoneCount> bone_lengths{};
    std::array<std::vector<float>, kBoneCount> bone_samples;
    Point3D palm_normal;
  };

  int MatchTrack(const HandPose& hand, const std::vector<bool>& used_tracks) const;
  float CalculateTrackCost(const HandPose& hand, const HandTrack& track) const;
  int CreateTrack(const HandPose& hand);
  void ProcessHand(double timestamp_sec, HandTrack* track, HandPose* hand);
  void UpdateBoneLengths(const HandPose& hand, HandTrack* track);
  void ApplyBoneConstraints(const HandTrack& track, const std::array<float, kJointCount>& confidence,
                            std::array<Point3D, kJointCount>* positions) const;
  void LimitInvalidJointBending(std::array<Point3D, kJointCount>* positions) const;
  void CalculateDirections(HandTrack* track, HandPose* hand) const;

  static float CalculateBoxIou(const BoundingBox& lhs, const BoundingBox& rhs);

  HandSkeletonConfig config_;
  std::vector<HandTrack> tracks_;
  int next_track_id_ = 1;
};

#endif  // TOOLCHAINS_RK3588_EXAMPLES_RK3588_SOP_INCLUDE_HAND_SKELETON_CONSTRAINT_H_
