/*
 * name: xiaorun
 * email: 15610499173@163.com
 */

#include "config_loader.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <utility>

// 这个文件只做一件事：把文本配置文件解析成 SopAppConfig。
// 不引入更重的配置框架，是因为当前工程的配置量很小，
// 纯文本 key=value 更容易在 RK3588 现场直接改和排错。

/**
 * @brief 去除字符串两端空白。
 *
 * 这里不用 std::regex 或额外库，只做最基本的文本清理。
 */
static std::string TrimText(const std::string& text) {
  const std::string::size_type begin = text.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return "";
  }
  const std::string::size_type end = text.find_last_not_of(" \t\r\n");
  return text.substr(begin, end - begin + 1);
}

/**
 * @brief 按分隔符切分字符串。
 *
 * 会自动 trim 每个片段，并丢弃空片段，适合解析 "a,b,c" 这类配置值。
 */
static std::vector<std::string> SplitText(const std::string& text, const char delimiter) {
  std::vector<std::string> values;
  std::stringstream stream(text);
  std::string item;
  while (std::getline(stream, item, delimiter)) {
    const std::string trimmed = TrimText(item);
    if (!trimmed.empty()) {
      values.push_back(trimmed);
    }
  }
  return values;
}

static std::vector<std::string> SplitPreserveEmpty(const std::string& text, const char delimiter) {
  std::vector<std::string> values;
  std::stringstream stream(text);
  std::string item;
  while (std::getline(stream, item, delimiter)) values.push_back(TrimText(item));
  if (!text.empty() && text.back() == delimiter) values.emplace_back();
  return values;
}

/**
 * @brief 解析布尔值。
 *
 * 配置文件里允许 true/1/yes/on 这些写法，减少现场改配置时的摩擦。
 */
static bool ParseBoolValue(const std::string& value) {
  return value == "true" || value == "1" || value == "yes" || value == "on";
}

/**
 * @brief 解析带数字后缀的配置序号。
 *
 * 例如 roi.0、roi.1、step.0、step.1。返回 -1 表示不是这个前缀。
 */
static int ParseIndexedKey(const std::string& key, const std::string& prefix) {
  if (key.rfind(prefix, 0) != 0) {
    return -1;
  }
  try {
    return std::stoi(key.substr(prefix.size()));
  } catch (...) {
    return -1;
  }
}

/**
 * @brief 解析 ROI 配置。
 *
 * 格式示例：name:x1,y1;x2,y2;x3,y3
 * 至少 3 个点，才能形成多边形。
 */
static bool ParseRoiValue(const std::string& value, RoiRegion* roi) {
  const std::vector<std::string> parts = SplitText(value, ':');
  if (parts.size() != 2 || roi == nullptr) {
    return false;
  }
  roi->name = parts[0];
  for (const std::string& point_text : SplitText(parts[1], ';')) {
    const std::vector<std::string> xy = SplitText(point_text, ',');
    if (xy.size() != 2) {
      return false;
    }
    roi->points.push_back(ImagePoint{std::stoi(xy[0]), std::stoi(xy[1])});
  }
  return roi->points.size() >= 3;
}

/**
 * @brief 解析 SOP 步骤配置。
 *
 * 格式示例：id|name|required_objects|hand_roi|min_frames|timeout|warning|optional_distance|min_stage|enabled
 *
 * required_objects 支持两种写法：
 *   - base,frame
 *   - cover_cloth:1,padding_board:2
 *   - object_id~cover_cloth~1~roi_a+roi_b~target_id~overlaps
 */
static bool ParseStepValue(const std::string& value, SopStepConfig* step) {
  const std::vector<std::string> parts = SplitPreserveEmpty(value, '|');
  if ((parts.size() < 7 || parts.size() > 10) || step == nullptr) {
    return false;
  }
  step->id = parts[0];
  step->name = parts[1];
  step->required_objects.clear();
  for (const std::string& item : SplitText(parts[2], ',')) {
    const std::vector<std::string> rich_parts = SplitText(item, '~');
    const std::vector<std::string> object_parts = SplitText(item, ':');
    RequiredObjectConfig required_object;
    if (rich_parts.size() >= 3) {
      required_object.id = rich_parts[0];
      required_object.label = rich_parts[1];
      required_object.min_count = std::stoi(rich_parts[2]);
      if (rich_parts.size() >= 4 && rich_parts[3] != "-") required_object.roi_names = SplitText(rich_parts[3], '+');
      if (rich_parts.size() >= 5 && rich_parts[4] != "-") required_object.relation_target_id = rich_parts[4];
      if (rich_parts.size() >= 6 && rich_parts[5] != "-") required_object.relation_type = rich_parts[5];
    } else if (object_parts.size() == 1) {
      required_object.label = object_parts[0];
      required_object.min_count = 1;
    } else if (object_parts.size() == 2) {
      required_object.label = object_parts[0];
      required_object.min_count = std::stoi(object_parts[1]);
    } else {
      return false;
    }
    if (required_object.label.empty() || required_object.min_count <= 0) {
      return false;
    }
    step->required_objects.push_back(required_object);
  }
  step->hand_roi = parts[3];
  step->min_confirm_frames = std::stoi(parts[4]);
  step->timeout_sec = std::stod(parts[5]);
  step->warning_message = parts[6];
  if (parts.size() >= 8) {
    step->max_hand_object_distance_m = std::stod(parts[7]);
  }
  if (parts.size() >= 9) {
    step->min_stage_sec = std::stod(parts[8]);
  }
  if (parts.size() >= 10) {
    step->enabled = ParseBoolValue(parts[9]);
  }
  return !step->id.empty() && !step->name.empty() && !step->required_objects.empty();
}

bool ConfigLoader::Load(const std::string& file_path, SopAppConfig* config) const {
  // 这里仍然保持“宽松解析”：
  //   - 空行和注释行跳过。
  //   - 没有等号的行忽略。
  // 这样现场改配置时不容易因为一行格式问题直接把程序卡死。
  if (config == nullptr) {
    return false;
  }

  std::ifstream input(file_path);
  if (!input.is_open()) {
    std::cerr << "无法打开配置文件: " << file_path << std::endl;
    return false;
  }

  std::map<std::string, std::string> kv;
  std::string line;
  while (std::getline(input, line)) {
    line = TrimText(line);
    if (line.empty() || line[0] == '#') {
      continue;
    }
    const std::string::size_type pos = line.find('=');
    if (pos == std::string::npos) {
      continue;
    }
    kv[TrimText(line.substr(0, pos))] = TrimText(line.substr(pos + 1));
  }

  // 逐字段取值。这里用 kv["key"] 的原因是：配置文件缺字段时会得到空字符串，
  // 这会在后面的 stoi/stof 处立刻报错，便于尽早发现配置缺失。
  config->input.type = kv["input.type"];
  config->input.uri = kv["input.uri"];
  config->input.width = std::stoi(kv["input.width"]);
  config->input.height = std::stoi(kv["input.height"]);
  config->input.fps = std::stoi(kv["input.fps"]);

  config->detector.backend = kv["detector.backend"];
  config->detector.model_path = kv["detector.model_path"];
  config->detector.labels = SplitText(kv["detector.labels"], ',');
  config->detector.conf_threshold = std::stof(kv["detector.conf_threshold"]);
  config->detector.iou_threshold = std::stof(kv["detector.iou_threshold"]);
  config->detector.input_size = std::stoi(kv["detector.input_size"]);

  config->hand_pose.backend = kv["hand.backend"];
  config->hand_pose.model_path = kv["hand.model_path"];
  if (kv.count("hand.landmark_model_path") > 0) {
    config->hand_pose.landmark_model_path = kv["hand.landmark_model_path"];
  }
  if (kv.count("hand.input_size") > 0) {
    config->hand_pose.input_size = std::stoi(kv["hand.input_size"]);
  }
  config->hand_pose.max_num_hands = std::stoi(kv["hand.max_num_hands"]);
  config->hand_pose.min_detection_confidence = std::stof(kv["hand.min_detection_confidence"]);
  config->hand_pose.min_tracking_confidence = std::stof(kv["hand.min_tracking_confidence"]);

  // 骨骼约束配置保持可选，旧配置文件不增加字段也能继续运行。
  if (kv.count("hand.constraint.enabled") > 0) {
    config->hand_skeleton.enabled = ParseBoolValue(kv["hand.constraint.enabled"]);
  }
  if (kv.count("hand.constraint.calibration_frames") > 0) {
    config->hand_skeleton.calibration_frames =
        std::max(5, std::stoi(kv["hand.constraint.calibration_frames"]));
  }
  if (kv.count("hand.constraint.smoothing") > 0) {
    config->hand_skeleton.smoothing =
        std::max(0.0F, std::min(1.0F, std::stof(kv["hand.constraint.smoothing"])));
  }
  if (kv.count("hand.constraint.max_prediction_frames") > 0) {
    config->hand_skeleton.max_prediction_frames =
        std::max(0, std::stoi(kv["hand.constraint.max_prediction_frames"]));
  }

  if (kv.count("serial_light.enabled") > 0) {
    config->serial_light.enabled = ParseBoolValue(kv["serial_light.enabled"]);
  }
  if (kv.count("serial_light.device_path") > 0) {
    config->serial_light.device_path = kv["serial_light.device_path"];
  }
  if (kv.count("serial_light.baud_rate") > 0) {
    config->serial_light.baud_rate = std::stoi(kv["serial_light.baud_rate"]);
  }

  config->show_window = ParseBoolValue(kv["show_window"]);
  if (kv.count("sop.execution_mode") > 0) config->execution_mode = kv["sop.execution_mode"];
  config->save_video = ParseBoolValue(kv["save_video"]);
  config->output_video_path = kv["output_video_path"];

  // 先收集，再排序。这样 roi.0/roi.1/roi.2 的顺序不依赖 map 的字典序。
  std::vector<std::pair<int, RoiRegion>> indexed_rois;
  std::vector<std::pair<int, SopStepConfig>> indexed_steps;
  for (const std::pair<const std::string, std::string>& item : kv) {
    const int roi_index = ParseIndexedKey(item.first, "roi.");
    if (roi_index >= 0) {
      RoiRegion roi;
      if (ParseRoiValue(item.second, &roi)) {
        indexed_rois.emplace_back(roi_index, roi);
      }
      continue;
    }

    const int step_index = ParseIndexedKey(item.first, "step.");
    if (step_index >= 0) {
      SopStepConfig step;
      if (ParseStepValue(item.second, &step)) {
        indexed_steps.emplace_back(step_index, step);
      }
    }
  }

  std::sort(indexed_rois.begin(), indexed_rois.end(), [](const std::pair<int, RoiRegion>& lhs, const std::pair<int, RoiRegion>& rhs) {
    return lhs.first < rhs.first;
  });
  std::sort(indexed_steps.begin(), indexed_steps.end(), [](const std::pair<int, SopStepConfig>& lhs, const std::pair<int, SopStepConfig>& rhs) {
    return lhs.first < rhs.first;
  });

  config->rois.clear();
  for (const std::pair<int, RoiRegion>& item : indexed_rois) {
    // 只把排序后的 value 拷回最终配置，不保留中间索引。

    config->rois.push_back(item.second);
  }
  config->steps.clear();
  for (const std::pair<int, SopStepConfig>& item : indexed_steps) {
    // SOP 步骤同样按索引排序后写回。

    config->steps.push_back(item.second);
  }

  return !config->steps.empty();
}
