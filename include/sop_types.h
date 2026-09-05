/*
 * name: xiaorun
 * email: 15610499173@163.com
 */

#ifndef TOOLCHAINS_RK3588_EXAMPLES_RK3588_SOP_INCLUDE_SOP_TYPES_H_
#define TOOLCHAINS_RK3588_EXAMPLES_RK3588_SOP_INCLUDE_SOP_TYPES_H_

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

enum class PixelFormat {
  BGR = 0,
  RGB = 1,
};

/**
 * @brief 三维空间点，单位米。
 */
struct Point3D {
  float x = 0.0F;     // 相机坐标系 X，单位米。
  float y = 0.0F;     // 相机坐标系 Y，单位米。
  float z = 0.0F;     // 相机坐标系 Z，单位米。
  bool valid = false; // 三维点是否有效。
};

/**
 * @brief 图像帧数据，像素顺序由 pixel_format 指定。
 */
struct ImageFrame {
  using Ptr = std::shared_ptr<ImageFrame>;
  using ConstPtr = std::shared_ptr<const ImageFrame>;

  std::int64_t frame_id = 0;                  // 连续帧编号。
  double timestamp_sec = 0.0;                 // 单调时钟时间戳，单位秒。
  std::string frame_id_name = "camera";       // 图像坐标系名称。
  int width = 0;                              // 图像宽度。
  int height = 0;                             // 图像高度。
  PixelFormat pixel_format = PixelFormat::BGR; // 当前帧像素顺序。
  std::vector<std::uint8_t> bgr_data;         // HWC 图像数据，RGB/BGR 由 pixel_format 决定。
};

/**
 * @brief RGB-D 图像帧数据，深度图必须明确是否已对齐到彩色图。
 */
struct RgbdFrame {
  using Ptr = std::shared_ptr<RgbdFrame>;
  using ConstPtr = std::shared_ptr<const RgbdFrame>;

  ImageFrame color;                           // 彩色图像。
  int depth_width = 0;                        // 深度图宽度。
  int depth_height = 0;                       // 深度图高度。
  float depth_scale = 0.001F;                 // 深度原始值到米的比例。
  bool depth_aligned_to_color = false;        // 深度图是否已对齐到彩色图坐标。
  std::vector<std::uint16_t> depth_data;      // 深度原始数据。
};

/**
 * @brief 矩形检测框。
 */
struct BoundingBox {
  int x = 0;       // 左上角横坐标。
  int y = 0;       // 左上角纵坐标。
  int width = 0;   // 检测框宽度。
  int height = 0;  // 检测框高度。
};

/**
 * @brief 目标检测结果。
 */
struct ObjectDetection {
  std::string label;       // 目标类别名称。
  float score = 0.0F;      // 检测置信度。
  BoundingBox box;         // 二维检测框。
  Point3D position;        // 检测目标的三维位置。
  int track_id = -1;       // 多目标跟踪 ID，-1 表示未跟踪。
};

/**
 * @brief 二维图像点。
 */
struct ImagePoint {
  int x = 0;  // 图像横坐标。
  int y = 0;  // 图像纵坐标。
};

/**
 * @brief 手部关键点，x/y 默认是归一化图像坐标。
 */
struct HandLandmark {
  float x = 0.0F;           // 归一化横坐标。
  float y = 0.0F;           // 归一化纵坐标。
  float z = 0.0F;           // 模型输出的相对深度值。
  float visibility = 0.0F;  // 关键点可见性。
};

/**
 * @brief 单个手部关节的三维状态。
 *
 * measured_position 保存深度相机原始观测，constrained_position 保存骨骼约束结果。
 * 两者分开保存，现场排查时才能判断误差来自深度查询还是约束算法。
 */
struct HandJoint3D {
  Point3D measured_position;       // 深度相机查询到的原始三维点。
  Point3D constrained_position;    // 固定骨长和时序约束后的三维点。
  ImagePoint projected_pixel;      // 约束后三维点投影到彩色图的像素。
  float confidence = 0.0F;         // 当前三维点的综合可信度，范围 0 到 1。
  bool projected_pixel_valid = false; // 投影像素是否有效。
  bool predicted = false;          // 是否因深度缺失或异常而使用历史姿态恢复。
};

/**
 * @brief 手指方向和掌面法向量。
 *
 * finger_direction 顺序固定为拇指、食指、中指、无名指和小指。
 * 方向向量使用 Point3D 承载，x/y/z 为单位向量分量，valid 表示是否可用。
 */
struct HandDirection3D {
  std::array<Point3D, 5> finger_direction;
  Point3D palm_normal;
};

/**
 * @brief 单手姿态检测结果。
 */
struct HandPose {
  std::string handedness;                 // 左手或右手。
  float score = 0.0F;                     // 手部检测置信度。
  BoundingBox box;                        // 手部二维框。
  std::vector<HandLandmark> landmarks;    // 21 个手部关键点。
  Point3D wrist_position;                 // 手腕三维位置。
  ImagePoint palm_pixel;                  // 手心二维像素位置。
  bool palm_pixel_valid = false;          // 手心二维像素是否有效。
  Point3D palm_position;                  // 手心三维位置。
  std::array<HandJoint3D, 21> joints_3d;  // 21 个关键点对应的真实三维状态。
  HandDirection3D directions;             // 约束后计算的手指方向和掌面法向量。
  int skeleton_track_id = -1;             // 骨骼约束模块内部的手部时序 ID。
};

/**
 * @brief 多边形感兴趣区域。
 */
struct RoiRegion {
  std::string name;                 // ROI 名称。
  std::vector<ImagePoint> points;   // 多边形顶点。
};

/**
 * @brief 单帧感知结果。
 */
struct PerceptionResult {
  std::int64_t frame_id = 0;                // 对应图像帧编号。
  double timestamp_sec = 0.0;               // 对应图像时间戳。
  int image_width = 0;                      // 对应彩色图宽度。
  int image_height = 0;                     // 对应彩色图高度。
  bool depth_aligned_to_color = false;      // 深度是否已对齐到彩色图。
  bool synchronized = true;                 // 多检测分支是否基于同一帧。
  double object_inference_ms = 0.0;         // 目标检测耗时，单位毫秒。
  double hand_inference_ms = 0.0;           // 手部检测耗时，单位毫秒。
  std::vector<ObjectDetection> objects;     // 目标检测结果。
  std::vector<HandPose> hands;              // 手部关键点结果。
};

/**
 * @brief 单帧处理耗时信息。
 */
struct FrameProcessMetrics {
  double read_ms = 0.0;               // 读帧和帧拷贝耗时。
  double object_inference_ms = 0.0;   // 目标检测耗时。
  double hand_inference_ms = 0.0;     // 手部检测耗时。
  double crop_ms = 0.0;               // 手部裁剪耗时。
  double spatial_ms = 0.0;            // 三维查询和骨骼约束耗时。
  double state_ms = 0.0;              // SOP 状态机耗时。
  double draw_ms = 0.0;               // 结果绘制耗时。
  double process_ms = 0.0;            // 单帧总耗时。
  double yolo_npu_ms = 0.0;           // YOLO NPU 耗时。
  double hand_det_npu_ms = 0.0;       // 手掌检测 NPU 耗时。
  double hand_lm_npu_ms = 0.0;        // 手部关键点 NPU 耗时。
};

/**
 * @brief SOP 预警信息。
 */
struct SopAlert {
  std::string level;    // 预警等级。
  std::string message;  // 预警内容。
  std::string step_id;  // 关联 SOP 步骤 ID。
};

/**
 * @brief 单个必检物体规则。
 */
struct RequiredObjectConfig {
  std::string id;    // 配置对象 ID，用于对象关系匹配。
  std::string label;  // 物体类别名称。
  int min_count = 1;  // 最少需要出现的数量。
  std::vector<std::string> roi_names;  // 目标中心点允许落入的 ROI；为空表示不限位置。
  std::string relation_target_id;      // 关联目标对象 ID。
  std::string relation_type;            // 目前支持 overlaps。
};

/**
 * @brief SOP 步骤配置。
 */
struct SopStepConfig {
  std::string id;                            // 步骤 ID。
  std::string name;                          // 步骤名称。
  std::vector<RequiredObjectConfig> required_objects;  // 当前步骤要求出现的目标及数量。
  std::string hand_roi;                      // 手部需要进入的 ROI 名称。
  int min_confirm_frames = 8;                // 连续确认帧数。
  double timeout_sec = 10.0;                 // 步骤超时时间，单位秒。
  double min_stage_sec = 0.8;                // 步骤最小驻留时间，防止切换瞬间误判，单位秒。
  double max_hand_object_distance_m = 0.0;   // 手腕与目标中心的最大 3D 距离，0 表示关闭。
  std::string warning_message;               // 超时或异常时的预警内容。
  bool enabled = true;                       // 是否参与运行。
};

/**
 * @brief SOP 运行状态。
 */
struct SopRuntimeState {
  int current_step_index = 0;       // 当前步骤下标。
  bool finished = false;            // SOP 是否完成。
  int confirm_count = 0;            // 当前步骤连续满足帧数。
  double step_start_sec = 0.0;      // 当前步骤开始时间。
  std::vector<int> required_object_max_counts;  // 当前步骤每个必检物体的历史最大数量。
  std::vector<SopAlert> alerts;     // 当前帧预警信息。
  std::vector<int> confirm_counts;  // 有序/无序模式下每个步骤的连续确认帧数。
  std::vector<double> step_start_times;  // 每个步骤的开始时间。
  std::vector<std::vector<int>> required_object_max_counts_by_step;  // 每个步骤的历史最大数量。
  std::vector<bool> completed_steps;  // 无序模式下各步骤是否已完成。
};

/** @brief 单个必检对象在当前帧的可解释判定结果。 */
struct SopObjectCheckResult {
  std::string object_id;
  std::string label;
  int required_count = 0;
  int current_count = 0;
  int best_count = 0;
  bool roi_satisfied = true;
  bool relation_satisfied = true;
  bool satisfied_now = false;
  std::vector<int> matched_track_ids;
  std::string reason;
};

/** @brief 单个步骤的实时判定结果。 */
struct SopStepRuntimeReport {
  int index = 0;
  std::string id;
  std::string name;
  bool enabled = true;
  bool completed = false;
  std::string state = "waiting";
  int confirm_count = 0;
  int confirm_target = 1;
  double elapsed_sec = 0.0;
  bool hand_roi_configured = false;
  bool hand_roi_satisfied = true;
  bool spatial_satisfied = true;
  std::vector<SopObjectCheckResult> objects;
};

/** @brief C++ 运行时向 Web 工作台暴露的完整快照。 */
struct SopRuntimeReport {
  bool valid = false;
  std::int64_t frame_id = 0;
  double timestamp_sec = 0.0;
  std::string execution_mode = "ordered";
  int current_step_index = 0;
  bool finished = false;
  std::vector<SopStepRuntimeReport> steps;
  std::vector<SopAlert> alerts;
};

/**
 * @brief 视频输入配置。
 */
struct VideoInputConfig {
  std::string type = "video";  // 输入类型: video/camera/gstreamer/orbbec。
  std::string uri;             // 视频路径、摄像头编号或 GStreamer pipeline。
  int width = 1280;            // 期望输入宽度。
  int height = 720;            // 期望输入高度。
  int fps = 30;                // 期望帧率。
};

/**
 * @brief YOLOv8 模型配置。
 */
struct DetectorConfig {
  std::string backend = "rknn";         // 检测后端: rknn。
  std::string model_path;               // 模型路径。
  std::vector<std::string> labels;      // 类别名称表。
  float conf_threshold = 0.35F;         // 置信度阈值。
  float iou_threshold = 0.45F;          // NMS IOU 阈值。
  int input_size = 640;                 // YOLOv8 输入尺寸。
};

/**
 * @brief 手部关键点模型配置。
 */
struct HandPoseConfig {
  std::string backend = "rknn";          // 手部检测后端: rknn。
  std::string model_path;                // RKNN palm detector 模型路径。
  std::string landmark_model_path;       // RKNN hand landmark 模型路径。
  int input_size = 0;                    // 保留字段。
  int max_num_hands = 2;                 // 最大检测手数。
  float min_detection_confidence = 0.5F;  // 最小检测置信度。
  float min_tracking_confidence = 0.5F;   // 最小跟踪置信度。
};

/**
 * @brief 手部三维骨骼约束配置。
 */
struct HandSkeletonConfig {
  bool enabled = true;               // 是否启用 21 点三维骨骼约束。
  int calibration_frames = 45;       // 每根骨骼用于稳定估计长度的有效样本数。
  float smoothing = 0.35F;           // 新姿态在时序平滑中的权重，范围 0 到 1。
  int max_prediction_frames = 8;     // 深度缺失后允许沿用运动学预测的最大帧数。
};

/**
 * @brief 串口报警灯配置。
 */
struct SerialLightConfig {
  bool enabled = true;                         // 是否启用 CH341 串口报警灯。
  std::string device_path = "/dev/ch341-light"; // 固定设备路径，由 udev 初始化脚本创建。
  int baud_rate = 9600;                        // 报警灯串口波特率。
};

/**
 * @brief SOP 应用配置。
 */
struct SopAppConfig {
  VideoInputConfig input;               // 视频输入配置。
  DetectorConfig detector;              // YOLOv8 检测配置。
  HandPoseConfig hand_pose;             // 手部关键点配置。
  HandSkeletonConfig hand_skeleton;     // 手部 21 点三维骨骼约束配置。
  SerialLightConfig serial_light;        // CH341 串口报警灯配置。
  std::vector<RoiRegion> rois;          // ROI 配置列表。
  std::vector<SopStepConfig> steps;     // SOP 步骤列表。
  std::string execution_mode = "ordered";  // ordered 或 unordered。
  bool show_window = true;              // 是否显示窗口。
  bool save_video = false;              // 是否保存结果视频。
  std::string output_video_path;        // 结果视频路径。
};

#endif  // TOOLCHAINS_RK3588_EXAMPLES_RK3588_SOP_INCLUDE_SOP_TYPES_H_
