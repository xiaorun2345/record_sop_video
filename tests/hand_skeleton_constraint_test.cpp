/*
 * name: xiaorun
 * email: 15610499173@163.com
 */

#include "config_loader.h"
#include "hand_skeleton_constraint.h"
#include "sop_state_machine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

Point3D MakePoint(const float x, const float y, const float z) {
  return Point3D{x, y, z, true};
}

float Distance(const Point3D& lhs, const Point3D& rhs) {
  const float dx = lhs.x - rhs.x;
  const float dy = lhs.y - rhs.y;
  const float dz = lhs.z - rhs.z;
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

float Length(const Point3D& value) {
  return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

void Require(const bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "测试失败: " << message << std::endl;
    std::exit(1);
  }
}

ObjectDetection MakeObject(const std::string& label) {
  ObjectDetection object;
  object.label = label;
  object.score = 0.9F;
  object.box = BoundingBox{0, 0, 10, 10};
  return object;
}

HandPose MakeMeasuredHand(const float offset_x, const int box_x) {
  // 使用一只张开的平面手作为确定性输入，所有坐标单位都是米。
  static const std::array<std::array<float, 3>, 21> positions = {{
      {{0.000F, 0.000F, 0.700F}},
      {{-0.020F, -0.012F, 0.700F}}, {{-0.038F, -0.024F, 0.700F}},
      {{-0.054F, -0.038F, 0.700F}}, {{-0.068F, -0.052F, 0.700F}},
      {{-0.030F, -0.032F, 0.700F}}, {{-0.030F, -0.060F, 0.700F}},
      {{-0.030F, -0.084F, 0.700F}}, {{-0.030F, -0.105F, 0.700F}},
      {{0.000F, -0.035F, 0.700F}}, {{0.000F, -0.066F, 0.700F}},
      {{0.000F, -0.092F, 0.700F}}, {{0.000F, -0.115F, 0.700F}},
      {{0.026F, -0.032F, 0.700F}}, {{0.026F, -0.061F, 0.700F}},
      {{0.026F, -0.085F, 0.700F}}, {{0.026F, -0.106F, 0.700F}},
      {{0.048F, -0.025F, 0.700F}}, {{0.048F, -0.050F, 0.700F}},
      {{0.048F, -0.071F, 0.700F}}, {{0.048F, -0.089F, 0.700F}},
  }};

  HandPose hand;
  hand.score = 0.95F;
  hand.box = BoundingBox{box_x, 100, 140, 180};
  for (std::size_t index = 0; index < positions.size(); ++index) {
    hand.joints_3d[index].measured_position =
        MakePoint(positions[index][0] + offset_x, positions[index][1], positions[index][2]);
  }
  return hand;
}

HandSkeletonConfig MakeTestConfig() {
  HandSkeletonConfig config;
  config.enabled = true;
  config.calibration_frames = 5;
  config.smoothing = 1.0F;
  config.max_prediction_frames = 3;
  return config;
}

void CalibrateHand(HandSkeletonConstraint* constraint, const float offset_x, const int box_x,
                   double* timestamp_sec, HandPose* output) {
  Require(constraint != nullptr && timestamp_sec != nullptr && output != nullptr, "标定测试参数不能为空");
  for (int frame = 0; frame < 6; ++frame) {
    std::vector<HandPose> hands{MakeMeasuredHand(offset_x, box_x)};
    constraint->Update(*timestamp_sec, &hands);
    *timestamp_sec += 1.0 / 30.0;
    *output = hands.front();
  }
}

void TestBoneLengthAndOutlier() {
  HandSkeletonConstraint constraint(MakeTestConfig());
  double timestamp_sec = 1.0;
  HandPose calibrated;
  CalibrateHand(&constraint, 0.0F, 100, &timestamp_sec, &calibrated);
  const float expected_length = Distance(calibrated.joints_3d[7].constrained_position,
                                         calibrated.joints_3d[8].constrained_position);

  HandPose outlier = MakeMeasuredHand(0.0F, 100);
  outlier.joints_3d[8].measured_position.y -= 0.075F;
  std::vector<HandPose> hands{outlier};
  constraint.Update(timestamp_sec, &hands);
  const float constrained_length = Distance(hands[0].joints_3d[7].constrained_position,
                                            hands[0].joints_3d[8].constrained_position);
  Require(std::abs(constrained_length - expected_length) < 0.004F,
          "异常指尖不能改变已经标定的末端骨长");
}

void TestOcclusionRecoveryAndDirection() {
  HandSkeletonConstraint constraint(MakeTestConfig());
  double timestamp_sec = 2.0;
  HandPose calibrated;
  CalibrateHand(&constraint, 0.0F, 120, &timestamp_sec, &calibrated);

  HandPose occluded = MakeMeasuredHand(0.006F, 124);
  occluded.joints_3d[12].measured_position = Point3D{};
  std::vector<HandPose> hands{occluded};
  constraint.Update(timestamp_sec, &hands);
  const HandJoint3D& recovered_tip = hands[0].joints_3d[12];
  Require(recovered_tip.predicted, "缺失的中指指尖必须标记为预测点");
  Require(recovered_tip.constrained_position.valid, "短时间遮挡的指尖应由历史骨架恢复");

  const Point3D& index_direction = hands[0].directions.finger_direction[1];
  Require(index_direction.valid, "食指方向应可计算");
  Require(std::abs(Length(index_direction) - 1.0F) < 1.0e-4F, "手指方向必须是单位向量");
  Require(hands[0].directions.palm_normal.valid, "掌面法向量应可计算");
}

void TestPredictionExpiry() {
  HandSkeletonConstraint constraint(MakeTestConfig());
  double timestamp_sec = 2.5;
  HandPose calibrated;
  CalibrateHand(&constraint, 0.0F, 140, &timestamp_sec, &calibrated);

  HandPose last_output;
  for (int frame = 0; frame < 5; ++frame) {
    HandPose occluded = MakeMeasuredHand(0.0F, 140);
    occluded.joints_3d[8].measured_position = Point3D{};
    std::vector<HandPose> hands{occluded};
    constraint.Update(timestamp_sec, &hands);
    timestamp_sec += 1.0 / 30.0;
    last_output = hands.front();
  }
  Require(!last_output.joints_3d[8].constrained_position.valid,
          "遮挡超过最大预测帧数后不能继续输出旧指尖位置");
}

void TestTwoHandOrderChange() {
  HandSkeletonConstraint constraint(MakeTestConfig());
  std::vector<HandPose> first_frame{MakeMeasuredHand(-0.12F, 40), MakeMeasuredHand(0.12F, 360)};
  constraint.Update(3.0, &first_frame);
  const int left_id = first_frame[0].skeleton_track_id;
  const int right_id = first_frame[1].skeleton_track_id;
  Require(left_id >= 0 && right_id >= 0 && left_id != right_id, "双手必须分配不同的骨骼时序 ID");

  std::vector<HandPose> second_frame{MakeMeasuredHand(0.122F, 363), MakeMeasuredHand(-0.118F, 43)};
  constraint.Update(3.0 + 1.0 / 30.0, &second_frame);
  Require(second_frame[0].skeleton_track_id == right_id, "检测顺序交换后右手 ID 应保持不变");
  Require(second_frame[1].skeleton_track_id == left_id, "检测顺序交换后左手 ID 应保持不变");
}

void TestSopQuantityRequirement() {
  ConfigLoader loader;
  SopAppConfig config;
  const std::string path = "/tmp/rk3588_sop_quantity_test.cfg";
  const std::string content =
      "input.type=video\n"
      "input.uri=config/demo.mp4\n"
      "input.width=640\n"
      "input.height=480\n"
      "input.fps=30\n"
      "detector.backend=rknn\n"
      "detector.model_path=models/ai_sop_best_int8.rknn\n"
      "detector.labels=cover_cloth,long_handle,manual,padding_board,small_red_lever,top_pad,vertical_support_bracket\n"
      "detector.conf_threshold=0.25\n"
      "detector.iou_threshold=0.45\n"
      "detector.input_size=640\n"
      "hand.backend=rknn\n"
      "hand.model_path=models/hand_detector.rknn\n"
      "hand.landmark_model_path=models/hand_landmarks.rknn\n"
      "hand.max_num_hands=2\n"
      "hand.min_detection_confidence=0.5\n"
      "hand.min_tracking_confidence=0.5\n"
      "step.0=quantity_check|数量校验|cover_cloth:1,long_handle:1,manual:1,padding_board:2,small_red_lever:1,top_pad:2,vertical_support_bracket:4|work_area|1|10|not enough|0|0.0\n";
  {
    std::ofstream file(path);
    file << content;
  }
  Require(loader.Load(path, &config), "数量校验配置应能被成功解析");
  Require(config.steps.size() == 1, "应只解析出一个步骤");
  Require(config.steps[0].required_objects.size() == 7, "应解析出 7 个必检物料");

  SopStateMachine machine(config.steps);
  machine.Reset(0.0);

  PerceptionResult insufficient;
  insufficient.timestamp_sec = 1.0;
  insufficient.objects = {
      MakeObject("cover_cloth"), MakeObject("long_handle"), MakeObject("manual"), MakeObject("padding_board"),
      MakeObject("padding_board"), MakeObject("small_red_lever"), MakeObject("top_pad"),
      MakeObject("vertical_support_bracket"), MakeObject("vertical_support_bracket"),
      MakeObject("vertical_support_bracket"),
  };
  machine.Update(insufficient);
  Require(!machine.state().finished, "数量不足时不应完成步骤");

  PerceptionResult enough;
  enough.timestamp_sec = 2.0;
  enough.objects = {
      MakeObject("cover_cloth"), MakeObject("long_handle"), MakeObject("manual"), MakeObject("padding_board"),
      MakeObject("padding_board"), MakeObject("small_red_lever"), MakeObject("top_pad"), MakeObject("top_pad"),
      MakeObject("vertical_support_bracket"), MakeObject("vertical_support_bracket"),
      MakeObject("vertical_support_bracket"), MakeObject("vertical_support_bracket"),
  };
  machine.Update(enough);
  Require(machine.state().finished, "数量满足时应完成步骤");
}

void TestSopQuantityLatch() {
  ConfigLoader loader;
  SopAppConfig config;
  const std::string path = "/tmp/rk3588_sop_quantity_latch_test.cfg";
  const std::string content =
      "input.type=video\n"
      "input.uri=config/demo.mp4\n"
      "input.width=640\n"
      "input.height=480\n"
      "input.fps=30\n"
      "detector.backend=rknn\n"
      "detector.model_path=models/ai_sop_best_int8.rknn\n"
      "detector.labels=cover_cloth,long_handle,manual,padding_board,small_red_lever,top_pad,vertical_support_bracket\n"
      "detector.conf_threshold=0.25\n"
      "detector.iou_threshold=0.45\n"
      "detector.input_size=640\n"
      "hand.backend=rknn\n"
      "hand.model_path=models/hand_detector.rknn\n"
      "hand.landmark_model_path=models/hand_landmarks.rknn\n"
      "hand.max_num_hands=2\n"
      "hand.min_detection_confidence=0.5\n"
      "hand.min_tracking_confidence=0.5\n"
      "step.0=quantity_check|数量校验|cover_cloth:1,long_handle:1,manual:1,padding_board:2,small_red_lever:1,top_pad:2,vertical_support_bracket:4|work_area|1|10|not enough|0|0.0\n";
  {
    std::ofstream file(path);
    file << content;
  }
  Require(loader.Load(path, &config), "数量锁定配置应能被成功解析");

  SopStateMachine machine(config.steps);
  machine.Reset(0.0);

  PerceptionResult enough;
  enough.timestamp_sec = 1.0;
  enough.objects = {
      MakeObject("cover_cloth"), MakeObject("long_handle"), MakeObject("manual"), MakeObject("padding_board"),
      MakeObject("padding_board"), MakeObject("small_red_lever"), MakeObject("top_pad"), MakeObject("top_pad"),
      MakeObject("vertical_support_bracket"), MakeObject("vertical_support_bracket"),
      MakeObject("vertical_support_bracket"), MakeObject("vertical_support_bracket"),
  };
  machine.Update(enough);
  Require(machine.state().finished, "数量达到要求时应完成步骤");

  PerceptionResult less;
  less.timestamp_sec = 2.0;
  less.objects = {
      MakeObject("cover_cloth"), MakeObject("long_handle"), MakeObject("manual"), MakeObject("padding_board"),
      MakeObject("small_red_lever"), MakeObject("top_pad"), MakeObject("vertical_support_bracket"),
  };
  machine.Update(less);
  Require(machine.state().finished, "达到要求后即使当前帧数量变少也不应回退");
}

}  // 匿名命名空间

int main() {
  TestBoneLengthAndOutlier();
  TestOcclusionRecoveryAndDirection();
  TestPredictionExpiry();
  TestTwoHandOrderChange();
  TestSopQuantityRequirement();
  TestSopQuantityLatch();
  std::cout << "手部三维骨骼约束测试通过" << std::endl;
  return 0;
}
