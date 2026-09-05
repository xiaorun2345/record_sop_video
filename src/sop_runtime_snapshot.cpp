#include "sop_runtime_snapshot.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace {

std::string Json(const std::string& value) {
  std::ostringstream output;
  output << '"';
  for (const char character : value) {
    switch (character) {
      case '\\': output << "\\\\"; break;
      case '"': output << "\\\""; break;
      case '\n': output << "\\n"; break;
      case '\r': output << "\\r"; break;
      case '\t': output << "\\t"; break;
      default: output << character; break;
    }
  }
  output << '"';
  return output.str();
}

std::string Bool(const bool value) { return value ? "true" : "false"; }

}  // namespace

bool WriteSopRuntimeSnapshot(const std::string& path, const SopRuntimeReport& report,
                             const PerceptionResult& perception) {
  if (path.empty() || !report.valid) return false;
  std::ostringstream json;
  json << std::fixed << std::setprecision(3);
  json << "{\"schemaVersion\":\"1.0\",\"updatedAtMs\":"
       << std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::system_clock::now().time_since_epoch()).count()
       << ",\"frameId\":" << report.frame_id
       << ",\"timestampSec\":" << report.timestamp_sec
       << ",\"imageWidth\":" << perception.image_width
       << ",\"imageHeight\":" << perception.image_height
       << ",\"executionMode\":" << Json(report.execution_mode)
       << ",\"currentStepIndex\":" << report.current_step_index
       << ",\"finished\":" << Bool(report.finished) << ",\"steps\":[";
  for (std::size_t index = 0; index < report.steps.size(); ++index) {
    if (index) json << ',';
    const SopStepRuntimeReport& step = report.steps[index];
    json << "{\"index\":" << step.index << ",\"id\":" << Json(step.id)
         << ",\"name\":" << Json(step.name) << ",\"enabled\":" << Bool(step.enabled)
         << ",\"completed\":" << Bool(step.completed) << ",\"state\":" << Json(step.state)
         << ",\"confirmCount\":" << step.confirm_count << ",\"confirmTarget\":" << step.confirm_target
         << ",\"elapsedSec\":" << step.elapsed_sec
         << ",\"handRoiConfigured\":" << Bool(step.hand_roi_configured)
         << ",\"handRoiSatisfied\":" << Bool(step.hand_roi_satisfied)
         << ",\"spatialSatisfied\":" << Bool(step.spatial_satisfied) << ",\"objects\":[";
    for (std::size_t object_index = 0; object_index < step.objects.size(); ++object_index) {
      if (object_index) json << ',';
      const SopObjectCheckResult& object = step.objects[object_index];
      json << "{\"id\":" << Json(object.object_id) << ",\"label\":" << Json(object.label)
           << ",\"requiredCount\":" << object.required_count << ",\"currentCount\":" << object.current_count
           << ",\"bestCount\":" << object.best_count << ",\"roiSatisfied\":" << Bool(object.roi_satisfied)
           << ",\"relationSatisfied\":" << Bool(object.relation_satisfied)
           << ",\"satisfiedNow\":" << Bool(object.satisfied_now) << ",\"reason\":" << Json(object.reason)
           << ",\"matchedTrackIds\":[";
      for (std::size_t track_index = 0; track_index < object.matched_track_ids.size(); ++track_index) {
        if (track_index) json << ',';
        json << object.matched_track_ids[track_index];
      }
      json << "]}";
    }
    json << "]}";
  }
  json << "],\"alerts\":[";
  for (std::size_t index = 0; index < report.alerts.size(); ++index) {
    if (index) json << ',';
    const SopAlert& alert = report.alerts[index];
    json << "{\"level\":" << Json(alert.level) << ",\"message\":" << Json(alert.message)
         << ",\"stepId\":" << Json(alert.step_id) << "}";
  }
  json << "]}";

  const std::string temporary_path = path + ".tmp";
  std::ofstream output(temporary_path, std::ios::trunc);
  if (!output) return false;
  output << json.str() << '\n';
  output.close();
  return std::rename(temporary_path.c_str(), path.c_str()) == 0;
}
