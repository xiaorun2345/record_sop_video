#include "sop_state_machine.h"

#include "geometry_utils.h"

#include <algorithm>
#include <cmath>

namespace {

float BoxIou(const BoundingBox& lhs, const BoundingBox& rhs) {
  const int left = std::max(lhs.x, rhs.x);
  const int top = std::max(lhs.y, rhs.y);
  const int right = std::min(lhs.x + lhs.width, rhs.x + rhs.width);
  const int bottom = std::min(lhs.y + lhs.height, rhs.y + rhs.height);
  const double intersection = static_cast<double>(std::max(0, right - left)) * std::max(0, bottom - top);
  const double area_lhs = static_cast<double>(std::max(0, lhs.width)) * std::max(0, lhs.height);
  const double area_rhs = static_cast<double>(std::max(0, rhs.width)) * std::max(0, rhs.height);
  const double union_area = area_lhs + area_rhs - intersection;
  return union_area > 0.0 ? static_cast<float>(intersection / union_area) : 0.0F;
}

}  // namespace

SopStateMachine::SopStateMachine(const std::vector<SopStepConfig>& steps,
                                 const std::vector<RoiRegion>& rois,
                                 const std::string& execution_mode)
    : steps_(steps), rois_(rois), execution_mode_(execution_mode == "unordered" ? "unordered" : "ordered") {}

void SopStateMachine::EnsureStateShape(const double timestamp_sec) {
  const std::size_t count = steps_.size();
  if (state_.confirm_counts.size() != count) state_.confirm_counts.assign(count, 0);
  if (state_.step_start_times.size() != count) state_.step_start_times.assign(count, timestamp_sec);
  if (state_.required_object_max_counts_by_step.size() != count) state_.required_object_max_counts_by_step.assign(count, {});
  if (state_.completed_steps.size() != count) state_.completed_steps.assign(count, false);
  for (std::size_t i = 0; i < count; ++i) {
    if (state_.required_object_max_counts_by_step[i].size() != steps_[i].required_objects.size()) {
      state_.required_object_max_counts_by_step[i].assign(steps_[i].required_objects.size(), 0);
    }
  }
}

void SopStateMachine::Reset(const double timestamp_sec) {
  state_ = SopRuntimeState{};
  state_.step_start_sec = timestamp_sec;
  EnsureStateShape(timestamp_sec);
  report_ = SopRuntimeReport{};
  report_.execution_mode = execution_mode_;
}

void SopStateMachine::AdvanceOrderedDisabledSteps() {
  while (state_.current_step_index < static_cast<int>(steps_.size()) &&
         (!steps_[static_cast<std::size_t>(state_.current_step_index)].enabled ||
          state_.completed_steps[static_cast<std::size_t>(state_.current_step_index)])) {
    state_.completed_steps[static_cast<std::size_t>(state_.current_step_index)] = true;
    ++state_.current_step_index;
    state_.confirm_count = 0;
    if (state_.current_step_index < static_cast<int>(steps_.size())) {
      state_.step_start_sec = state_.step_start_times[static_cast<std::size_t>(state_.current_step_index)];
    }
  }
  if (state_.current_step_index >= static_cast<int>(steps_.size())) state_.finished = true;
}

const SopRuntimeState& SopStateMachine::Update(const PerceptionResult& result) {
  state_.alerts.clear();
  EnsureStateShape(result.timestamp_sec);
  if (steps_.empty()) {
    state_.finished = true;
    RefreshReport(result);
    return state_;
  }

  if (execution_mode_ == "unordered") {
    for (std::size_t index = 0; index < steps_.size(); ++index) {
      const SopStepConfig& step = steps_[index];
      if (!step.enabled || state_.completed_steps[index]) continue;
      SopStepRuntimeReport step_report = EvaluateStep(static_cast<int>(index), result);
      for (std::size_t object_index = 0; object_index < step_report.objects.size(); ++object_index) {
        state_.required_object_max_counts_by_step[index][object_index] = std::max(
            state_.required_object_max_counts_by_step[index][object_index],
            step_report.objects[object_index].current_count);
      }
      const double elapsed = result.timestamp_sec - state_.step_start_times[index];
      const bool stage_ready = elapsed >= step.min_stage_sec;
      const bool satisfied = stage_ready && IsCurrentStepSatisfied(step, result, &step_report);
      if (satisfied) ++state_.confirm_counts[index];
      else state_.confirm_counts[index] = 0;
      if (state_.confirm_counts[index] >= std::max(1, step.min_confirm_frames)) {
        state_.completed_steps[index] = true;
        state_.confirm_counts[index] = 0;
      }
      if (elapsed >= step.timeout_sec && !state_.completed_steps[index]) {
        state_.alerts.push_back(SopAlert{"warning", step.warning_message, step.id});
      }
    }
    state_.finished = true;
    for (std::size_t index = 0; index < steps_.size(); ++index) {
      if (steps_[index].enabled && !state_.completed_steps[index]) { state_.finished = false; break; }
    }
    state_.current_step_index = 0;
    while (state_.current_step_index < static_cast<int>(steps_.size()) &&
           (state_.completed_steps[static_cast<std::size_t>(state_.current_step_index)] ||
            !steps_[static_cast<std::size_t>(state_.current_step_index)].enabled)) {
      ++state_.current_step_index;
    }
  } else {
    AdvanceOrderedDisabledSteps();
    if (!state_.finished) {
      const std::size_t index = static_cast<std::size_t>(state_.current_step_index);
      const SopStepConfig& step = steps_[index];
      SopStepRuntimeReport step_report = EvaluateStep(state_.current_step_index, result);
      for (std::size_t object_index = 0; object_index < step_report.objects.size(); ++object_index) {
        state_.required_object_max_counts_by_step[index][object_index] = std::max(
            state_.required_object_max_counts_by_step[index][object_index],
            step_report.objects[object_index].current_count);
      }
      const double elapsed = result.timestamp_sec - state_.step_start_sec;
      const bool stage_ready = elapsed >= step.min_stage_sec;
      const bool satisfied = stage_ready && IsCurrentStepSatisfied(step, result, &step_report);
      if (satisfied) ++state_.confirm_count;
      else state_.confirm_count = 0;
      state_.confirm_counts[index] = state_.confirm_count;
      if (state_.confirm_count >= std::max(1, step.min_confirm_frames)) {
        state_.completed_steps[index] = true;
        ++state_.current_step_index;
        state_.confirm_count = 0;
        if (state_.current_step_index < static_cast<int>(steps_.size())) {
          state_.step_start_sec = result.timestamp_sec;
          state_.step_start_times[static_cast<std::size_t>(state_.current_step_index)] = result.timestamp_sec;
        }
        AdvanceOrderedDisabledSteps();
      }
      if (!state_.finished && elapsed >= step.timeout_sec) {
        state_.alerts.push_back(SopAlert{"warning", step.warning_message, step.id});
      }
    }
  }

  state_.required_object_max_counts = state_.current_step_index < static_cast<int>(steps_.size())
      ? state_.required_object_max_counts_by_step[static_cast<std::size_t>(state_.current_step_index)]
      : std::vector<int>{};
  RefreshReport(result);
  return state_;
}

const SopStepConfig* SopStateMachine::CurrentStep() const {
  if (state_.finished || state_.current_step_index >= static_cast<int>(steps_.size())) return nullptr;
  return &steps_[static_cast<std::size_t>(state_.current_step_index)];
}

const std::vector<SopStepConfig>& SopStateMachine::steps() const { return steps_; }
const SopRuntimeState& SopStateMachine::state() const { return state_; }
const SopRuntimeReport& SopStateMachine::report() const { return report_; }

bool SopStateMachine::EvaluateObject(const RequiredObjectConfig& required_object,
                                     const PerceptionResult& result,
                                     SopObjectCheckResult* check) const {
  if (check == nullptr) return false;
  check->object_id = required_object.id;
  check->label = required_object.label;
  check->required_count = required_object.min_count;
  for (const ObjectDetection& object : result.objects) {
    if (object.label != required_object.label) continue;
    bool in_roi = required_object.roi_names.empty();
    if (!in_roi) {
      const ImagePoint center = GetBoxCenter(object.box);
      for (const std::string& roi_name : required_object.roi_names) {
        const RoiRegion* roi = FindRoiByName(rois_, roi_name);
        if (roi != nullptr && IsPointInRoi(center, *roi)) { in_roi = true; break; }
      }
    }
    if (!in_roi) continue;
    ++check->current_count;
    if (object.track_id >= 0) check->matched_track_ids.push_back(object.track_id);
  }
  check->roi_satisfied = check->current_count >= required_object.min_count;
  check->satisfied_now = check->roi_satisfied;
  check->reason = check->roi_satisfied ? "目标数量和位置满足" : "目标数量或位置未满足";
  return check->satisfied_now;
}

bool SopStateMachine::EvaluateRelation(const RequiredObjectConfig& required_object,
                                       const PerceptionResult& result) const {
  if (required_object.relation_target_id.empty()) return true;
  if (required_object.relation_type != "overlaps") return false;
  const ObjectDetection* source = nullptr;
  for (const ObjectDetection& object : result.objects) {
    if (object.label == required_object.label) { source = &object; break; }
  }
  if (source == nullptr) return false;
  for (const SopStepConfig& step : steps_) {
    for (const RequiredObjectConfig& target_config : step.required_objects) {
      if (target_config.id != required_object.relation_target_id) continue;
      for (const ObjectDetection& target : result.objects) {
        if (target.label == target_config.label && BoxIou(source->box, target.box) >= 0.10F) return true;
      }
    }
  }
  return false;
}

bool SopStateMachine::EvaluateHandRoi(const SopStepConfig& step, const PerceptionResult& result) const {
  if (step.hand_roi.empty()) return true;
  const RoiRegion* roi = FindRoiByName(rois_, step.hand_roi);
  if (roi == nullptr) return false;
  const int max_x = std::max(1, result.image_width);
  const int max_y = std::max(1, result.image_height);
  for (const HandPose& hand : result.hands) {
    if (hand.landmarks.empty()) continue;
    const HandLandmark& wrist = hand.landmarks.front();
    const ImagePoint point{static_cast<int>(wrist.x * max_x), static_cast<int>(wrist.y * max_y)};
    if (IsPointInRoi(point, *roi)) return true;
  }
  return false;
}

SopStepRuntimeReport SopStateMachine::EvaluateStep(const int index, const PerceptionResult& result) const {
  SopStepRuntimeReport output;
  if (index < 0 || index >= static_cast<int>(steps_.size())) return output;
  const SopStepConfig& step = steps_[static_cast<std::size_t>(index)];
  output.index = index;
  output.id = step.id;
  output.name = step.name;
  output.enabled = step.enabled;
  output.completed = state_.completed_steps.size() > static_cast<std::size_t>(index) && state_.completed_steps[static_cast<std::size_t>(index)];
  output.confirm_count = state_.confirm_counts.size() > static_cast<std::size_t>(index) ? state_.confirm_counts[static_cast<std::size_t>(index)] : 0;
  output.confirm_target = std::max(1, step.min_confirm_frames);
  const double start = state_.step_start_times.size() > static_cast<std::size_t>(index)
      ? state_.step_start_times[static_cast<std::size_t>(index)] : state_.step_start_sec;
  output.elapsed_sec = std::max(0.0, result.timestamp_sec - start);
  output.hand_roi_configured = !step.hand_roi.empty();
  output.hand_roi_satisfied = EvaluateHandRoi(step, result);
  output.spatial_satisfied = true;
  for (const RequiredObjectConfig& required_object : step.required_objects) {
    SopObjectCheckResult check;
    EvaluateObject(required_object, result, &check);
    check.relation_satisfied = EvaluateRelation(required_object, result);
    check.satisfied_now = check.satisfied_now && check.relation_satisfied;
    if (!check.relation_satisfied) check.reason = "对象关系未满足";
    output.objects.push_back(std::move(check));
  }
  if (step.max_hand_object_distance_m > 0.0) {
    const HandPose* hand = result.hands.empty() ? nullptr : &result.hands.front();
    const ObjectDetection* object = nullptr;
    for (const ObjectDetection& candidate : result.objects) {
      if (!step.required_objects.empty() && candidate.label == step.required_objects.front().label) { object = &candidate; break; }
    }
    if (hand == nullptr || object == nullptr || !hand->wrist_position.valid || !object->position.valid) {
      output.spatial_satisfied = false;
    } else {
      const double dx = hand->wrist_position.x - object->position.x;
      const double dy = hand->wrist_position.y - object->position.y;
      const double dz = hand->wrist_position.z - object->position.z;
      output.spatial_satisfied = std::sqrt(dx * dx + dy * dy + dz * dz) <= step.max_hand_object_distance_m;
    }
  }
  const bool objects_ok = !output.objects.empty() && std::all_of(output.objects.begin(), output.objects.end(),
      [](const SopObjectCheckResult& check) { return check.satisfied_now; });
  const bool satisfied = step.enabled && objects_ok && output.hand_roi_satisfied && output.spatial_satisfied;
  output.state = output.completed ? "completed" : (satisfied ? "confirming" : "waiting");
  if (output.elapsed_sec >= step.timeout_sec && !output.completed) output.state = "timeout";
  return output;
}

bool SopStateMachine::IsCurrentStepSatisfied(const SopStepConfig& step, const PerceptionResult& result,
                                             SopStepRuntimeReport* step_report) const {
  (void)result;
  if (step_report == nullptr || !step.enabled || step_report->objects.empty() ||
      !step_report->hand_roi_satisfied || !step_report->spatial_satisfied) return false;
  return std::all_of(step_report->objects.begin(), step_report->objects.end(),
                     [](const SopObjectCheckResult& check) { return check.satisfied_now; });
}

void SopStateMachine::RefreshReport(const PerceptionResult& result) {
  report_ = SopRuntimeReport{};
  report_.valid = true;
  report_.frame_id = result.frame_id;
  report_.timestamp_sec = result.timestamp_sec;
  report_.execution_mode = execution_mode_;
  report_.current_step_index = state_.current_step_index;
  report_.finished = state_.finished;
  for (std::size_t index = 0; index < steps_.size(); ++index) {
    SopStepRuntimeReport step_report = EvaluateStep(static_cast<int>(index), result);
    if (state_.required_object_max_counts_by_step.size() > index) {
      for (std::size_t object_index = 0; object_index < step_report.objects.size(); ++object_index) {
        if (state_.required_object_max_counts_by_step[index].size() > object_index) {
          step_report.objects[object_index].best_count = state_.required_object_max_counts_by_step[index][object_index];
        }
      }
    }
    report_.steps.push_back(std::move(step_report));
  }
  report_.alerts = state_.alerts;
}
