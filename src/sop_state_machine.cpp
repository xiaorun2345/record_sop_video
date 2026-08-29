/*
 * name: xiaorun
 * email: 15610499173@163.com
 */

#include "sop_state_machine.h"

#include "geometry_utils.h"

#include <algorithm>
#include <cmath>


SopStateMachine::SopStateMachine(const std::vector<SopStepConfig>& steps) : steps_(steps) {
  // 这里只保存配置副本，不保存指针引用。
  // 好处是外部 config 生命周期结束后，状态机仍然可独立运行。
}

void SopStateMachine::Reset(const double timestamp_sec) {
  // 直接用默认构造值整体清零，比逐字段赋值更不容易漏状态。
  state_ = SopRuntimeState{};
  state_.step_start_sec = timestamp_sec;
}

const SopRuntimeState& SopStateMachine::Update(const PerceptionResult& result) {
  // 每帧重新生成 alerts，避免上一帧告警残留。
  state_.alerts.clear();
  if (steps_.empty()) {
    return state_;
  }
  // 已完成或步骤越界时直接保持完成状态，避免重复报警。
  if (state_.finished || state_.current_step_index >= static_cast<int>(steps_.size())) {
    state_.finished = true;
    return state_;
  }

  // 当前步骤是顺序推进的，因此只看 current_step_index 对应的那一项。
  const SopStepConfig& step = steps_[state_.current_step_index];
  UpdateRequiredObjectHistory(step, result);

  // 最小驻留时间检查：当前步骤持续时间未达到阈值时，不允许确认，
  // 防止上一帧刚切换过来就因残留检测结果误判为完成。
  const double stage_elapsed = result.timestamp_sec - state_.step_start_sec;
  if (stage_elapsed < step.min_stage_sec) {
    state_.confirm_count = 0;
    UpdateTimeoutAlert(step, result.timestamp_sec);
    return state_;
  }

  // 连续多帧满足才推进步骤，抑制单帧误检造成的误判。
  if (IsCurrentStepSatisfied(step, result)) {
    ++state_.confirm_count;
  } else {
    state_.confirm_count = 0;
  }

  if (state_.confirm_count >= step.min_confirm_frames) {
    // 连续满足足够帧后再推进，能过滤掉单帧误检。
    ++state_.current_step_index;
    state_.confirm_count = 0;
    state_.step_start_sec = result.timestamp_sec;
    state_.required_object_max_counts.clear();
    if (state_.current_step_index >= static_cast<int>(steps_.size())) {
      state_.finished = true;
    }
    return state_;
  }

  UpdateSpatialAlert(step, result);
  UpdateTimeoutAlert(step, result.timestamp_sec);
  return state_;
}

const SopStepConfig* SopStateMachine::CurrentStep() const {
  // 完成后返回 nullptr，调用方可以直接据此判断是否结束。
  if (state_.finished || state_.current_step_index >= static_cast<int>(steps_.size())) {
    return nullptr;
  }
  return &steps_[state_.current_step_index];
}

const std::vector<SopStepConfig>& SopStateMachine::steps() const { return steps_; }

const SopRuntimeState& SopStateMachine::state() const { return state_; }

static const ObjectDetection* FindPrimaryObject(const PerceptionResult& result, const SopStepConfig& step) {
  // 当前只需要一个"主物体"做空间距离判断，所以返回第一项命中的 label。
  // 不做复杂排序，是为了保持逻辑简单直观。
  for (const RequiredObjectConfig& required_object : step.required_objects) {
    for (const ObjectDetection& object : result.objects) {
      if (object.label == required_object.label) {
        return &object;
      }
    }
  }
  return nullptr;
}

static const HandPose* FindPrimaryHand(const PerceptionResult& result) {
  // 目前 SOP 只关心第一只有效手。后续如果需要双手协作，再扩展这里。
  for (const HandPose& hand : result.hands) {
    if (!hand.landmarks.empty()) {
      return &hand;
    }
  }
  return nullptr;
}

bool SopStateMachine::IsCurrentStepSatisfied(const SopStepConfig& step, const PerceptionResult& result) const {
  if (step.required_objects.empty()) {
    return false;
  }
  if (state_.required_object_max_counts.size() != step.required_objects.size()) {
    return false;
  }

  // 只要当前步骤中“曾经达到过”的数量满足要求，就保持满足状态。
  // 这样检测抖动或短时遮挡不会把已经打过勾的物料重新变红。
  for (std::size_t i = 0; i < step.required_objects.size(); ++i) {
    const RequiredObjectConfig& required_object = step.required_objects[i];
    if (state_.required_object_max_counts[i] < required_object.min_count) {
      return false;
    }
  }

  // 配置了 3D 距离阈值时，手腕与目标中心必须满足空间约束。
  if (step.max_hand_object_distance_m > 0.0) {
    // 只有配置了 3D 距离阈值，才需要依赖深度数据。
    // 这让纯 2D 场景也能运行同一套状态机。
    const ObjectDetection* object = FindPrimaryObject(result, step);
    const HandPose* hand = FindPrimaryHand(result);
    if (object == nullptr || hand == nullptr || !object->position.valid || !hand->wrist_position.valid) {
      return false;
    }
    const double dx = static_cast<double>(object->position.x - hand->wrist_position.x);
    const double dy = static_cast<double>(object->position.y - hand->wrist_position.y);
    const double dz = static_cast<double>(object->position.z - hand->wrist_position.z);
    const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (distance > step.max_hand_object_distance_m) {
      return false;
    }
  }

  return true;
}

void SopStateMachine::UpdateRequiredObjectHistory(const SopStepConfig& step, const PerceptionResult& result) {
  if (state_.required_object_max_counts.size() != step.required_objects.size()) {
    state_.required_object_max_counts.assign(step.required_objects.size(), 0);
  }
  for (std::size_t i = 0; i < step.required_objects.size(); ++i) {
    const int current_count = CountObjectLabel(result.objects, step.required_objects[i].label);
    state_.required_object_max_counts[i] = std::max(state_.required_object_max_counts[i], current_count);
  }
}

void SopStateMachine::UpdateTimeoutAlert(const SopStepConfig& step, const double timestamp_sec) {
  // 超过步骤允许时间仍未满足条件时输出预警。
  // 这里是“按步骤计时”，不是全局计时。每次步骤推进时 step_start_sec 会重置。
  if (timestamp_sec - state_.step_start_sec < step.timeout_sec) {
    return;
  }
  state_.alerts.push_back(SopAlert{"warning", step.warning_message, step.id});
}

void SopStateMachine::UpdateSpatialAlert(const SopStepConfig& step, const PerceptionResult& result) {
  // 只有当步骤真的启用了 3D 距离约束时，才会生成空间相关告警。
  if (step.max_hand_object_distance_m <= 0.0) {
    return;
  }
  if (!result.depth_aligned_to_color) {
    state_.alerts.push_back(SopAlert{"warning", "深度未对齐到彩色图，无法稳定计算 3D 位置", step.id});
  }
}
