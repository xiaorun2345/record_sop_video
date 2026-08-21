/*
 * name: xiaorun
 * email: 15610499173@163.com
 */

#ifndef TOOLCHAINS_RK3588_EXAMPLES_RK3588_SOP_INCLUDE_SOP_TYPES_H_
#define TOOLCHAINS_RK3588_EXAMPLES_RK3588_SOP_INCLUDE_SOP_TYPES_H_

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
  bool depth_aligned_to_color = false;      // 深度是否已对齐到彩色图。
  bool synchronized = true;                 // 多检测分支是否基于同一帧。
  double object_inference_ms = 0.0;         // 目标检测耗时，单位毫秒。
  double hand_inference_ms = 0.0;           // 手部检测耗时，单位毫秒。
  std::vector<ObjectDetection> objects;     // 目标检测结果。
  std::vector<HandPose> hands;              // 手部关键点结果。
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
 * @brief SOP 步骤配置。
 */
struct SopStepConfig {
  std::string id;                            // 步骤 ID。
  std::string name;                          // 步骤名称。
  std::vector<std::string> required_objects;  // 当前步骤要求出现的目标类别。
  std::string hand_roi;                      // 手部需要进入的 ROI 名称。
  int min_confirm_frames = 8;                // 连续确认帧数。
  double timeout_sec = 10.0;                 // 步骤超时时间，单位秒。
  double min_stage_sec = 0.8;                // 步骤最小驻留时间，防止切换瞬间误判，单位秒。
  double max_hand_object_distance_m = 0.0;   // 手腕与目标中心的最大 3D 距离，0 表示关闭。
  std::string warning_message;               // 超时或异常时的预警内容。
};

/**
 * @brief SOP 运行状态。
 */
struct SopRuntimeState {
  int current_step_index = 0;       // 当前步骤下标。
  bool finished = false;            // SOP 是否完成。
  int confirm_count = 0;            // 当前步骤连续满足帧数。
  double step_start_sec = 0.0;      // 当前步骤开始时间。
  std::vector<SopAlert> alerts;     // 当前帧预警信息。
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
 * @brief SOP 应用配置。
 */
struct SopAppConfig {
  VideoInputConfig input;               // 视频输入配置。
  DetectorConfig detector;              // YOLOv8 检测配置。
  HandPoseConfig hand_pose;             // 手部关键点配置。
  std::vector<RoiRegion> rois;          // ROI 配置列表。
  std::vector<SopStepConfig> steps;     // SOP 步骤列表。
  bool show_window = true;              // 是否显示窗口。
  bool save_video = false;              // 是否保存结果视频。
  std::string output_video_path;        // 结果视频路径。
};

#endif  // TOOLCHAINS_RK3588_EXAMPLES_RK3588_SOP_INCLUDE_SOP_TYPES_H_
