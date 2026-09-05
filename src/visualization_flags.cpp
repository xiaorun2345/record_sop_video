#include "visualization_flags.h"

#include <filesystem>

bool VisualizationFlagEnabled(const std::string& name, const bool default_value) {
  if (name.empty()) return default_value;
  const std::filesystem::path marker = "/tmp/rk3588_sop_visual_" + name;
  std::error_code error;
  if (std::filesystem::exists(marker, error)) return true;
  const std::filesystem::path disabled = marker.string() + ".off";
  if (std::filesystem::exists(disabled, error)) return false;
  return default_value;
}
