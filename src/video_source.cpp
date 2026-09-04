/*
 * name: xiaorun
 * email: 15610499173@163.com
 */

#include "video_source.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <cmath>
#include <string>
#include <vector>

#include "time_utils.h"

#if RK3588_SOP_HAS_OPENCV
#include <opencv2/opencv.hpp>
#endif

#if RK3588_SOP_ENABLE_ORBBEC
#include <libobsensor/ObSensor.hpp>
#include <libobsensor/hpp/Utils.hpp>
#endif

#if RK3588_SOP_ENABLE_ORBBEC
/**
 * @brief 把 Orbbec 彩色帧转成工程内部 BGR888。
 *
 * Orbbec 可能输出 RGB/BGR/MJPG/YUYV/UYVY，工程内部统一使用 BGR，
 * 这样 OpenCV、GStreamer 的 video/x-raw,format=BGR 以及显示链路一致。
 */
static bool ConvertOrbbecColorToRgb(const std::shared_ptr<ob::ColorFrame>& color_frame, ImageFrame* image) {
  if (!color_frame || image == nullptr || image->width <= 0 || image->height <= 0) {
    return false;
  }
  const int width = image->width;
  const int height = image->height;
  const std::size_t expected_rgb_size = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3U;
  const std::uint8_t* data = static_cast<const std::uint8_t*>(color_frame->data());
  if (data == nullptr) {
    return false;
  }

  image->pixel_format = PixelFormat::BGR;
  image->bgr_data.resize(expected_rgb_size);
  const OBFormat format = color_frame->format();
  if (format == OB_FORMAT_RGB) {
    for (int i = 0; i < width * height; ++i) {
      image->bgr_data[static_cast<std::size_t>(i) * 3U] = data[static_cast<std::size_t>(i) * 3U + 2U];
      image->bgr_data[static_cast<std::size_t>(i) * 3U + 1U] = data[static_cast<std::size_t>(i) * 3U + 1U];
      image->bgr_data[static_cast<std::size_t>(i) * 3U + 2U] = data[static_cast<std::size_t>(i) * 3U];
    }
    return true;
  }
  if (format == OB_FORMAT_BGR) {
    std::memcpy(image->bgr_data.data(), data, expected_rgb_size);
    return true;
  }

#if RK3588_SOP_HAS_OPENCV
  if (format == OB_FORMAT_MJPG) {
    const int data_size = static_cast<int>(color_frame->dataSize());
    cv::Mat encoded(1, data_size, CV_8UC1, const_cast<std::uint8_t*>(data));
    cv::Mat decoded = cv::imdecode(encoded, cv::IMREAD_COLOR);
    if (decoded.empty()) {
      return false;
    }
    if (decoded.cols != width || decoded.rows != height) {
      cv::resize(decoded, decoded, cv::Size(width, height));
    }
    image->bgr_data.assign(decoded.data, decoded.data + decoded.total() * decoded.elemSize());
    return true;
  }
  if (format == OB_FORMAT_YUYV) {
    cv::Mat yuyv(height, width, CV_8UC2, const_cast<std::uint8_t*>(data));
    cv::Mat bgr;
    cv::cvtColor(yuyv, bgr, cv::COLOR_YUV2BGR_YUY2);
    image->bgr_data.assign(bgr.data, bgr.data + bgr.total() * bgr.elemSize());
    return true;
  }
  if (format == OB_FORMAT_UYVY) {
    cv::Mat uyvy(height, width, CV_8UC2, const_cast<std::uint8_t*>(data));
    cv::Mat bgr;
    cv::cvtColor(uyvy, bgr, cv::COLOR_YUV2BGR_UYVY);
    image->bgr_data.assign(bgr.data, bgr.data + bgr.total() * bgr.elemSize());
    return true;
  }
#endif

  image->bgr_data.clear();
  std::cerr << "暂不支持的 Orbbec 彩色格式: " << static_cast<int>(format) << std::endl;
  return false;
}

static std::shared_ptr<ob::VideoStreamProfile> SelectOrbbecColorProfile(
    const std::shared_ptr<ob::StreamProfileList>& profiles, const int width, const int height, const int fps) {
  if (!profiles || profiles->count() == 0) {
    return nullptr;
  }
  for (const OBFormat format : {OB_FORMAT_RGB, OB_FORMAT_BGR, OB_FORMAT_YUYV, OB_FORMAT_UYVY, OB_FORMAT_MJPG}) {
    try {
      return profiles->getVideoStreamProfile(width, height, format, fps);
    } catch (...) {
    }
  }
  try {
    return profiles->getVideoStreamProfile(width, height, OB_FORMAT_ANY, fps);
  } catch (...) {
  }
  return profiles->getProfile(OB_PROFILE_DEFAULT)->as<ob::VideoStreamProfile>();
}

static std::shared_ptr<ob::VideoStreamProfile> SelectOrbbecTransformDepthProfile(
    const std::shared_ptr<ob::StreamProfileList>& profiles, const int fps) {
  if (!profiles || profiles->count() == 0) {
    return nullptr;
  }
  try {
    return profiles->getVideoStreamProfile(640, OB_HEIGHT_ANY, OB_FORMAT_ANY, fps);
  } catch (...) {
  }
  std::shared_ptr<ob::VideoStreamProfile> best;
  for (uint32_t i = 0; i < profiles->count(); ++i) {
    std::shared_ptr<ob::VideoStreamProfile> profile;
    try {
      profile = profiles->getProfile(i)->as<ob::VideoStreamProfile>();
    } catch (...) {
      continue;
    }
    if (profile->fps() != static_cast<uint32_t>(fps)) {
      continue;
    }
    if (!best || profile->width() * profile->height() > best->width() * best->height()) {
      best = profile;
    }
  }
  if (best) {
    return best;
  }
  return profiles->getProfile(OB_PROFILE_DEFAULT)->as<ob::VideoStreamProfile>();
}
#endif


#if RK3588_SOP_HAS_OPENCV || RK3588_SOP_ENABLE_ORBBEC
class VideoSource::Impl {
 public:
  // SDK 和 OpenCV 对象放到实现类中，避免头文件暴露第三方依赖。
#if RK3588_SOP_HAS_OPENCV
  cv::VideoCapture capture;
#endif
#if RK3588_SOP_ENABLE_ORBBEC
  std::shared_ptr<ob::Pipeline> pipeline;
  std::shared_ptr<ob::Config> config;
  OBCalibrationParam calibration_param{};
  bool has_calibration_param = false;
  bool transform_depth_to_color = false;
  bool software_depth_fallback = false;
#endif
};
#endif

#if RK3588_SOP_HAS_OPENCV
class ScopedFfmpegCaptureOptions {
 public:
  explicit ScopedFfmpegCaptureOptions(const std::string& value) : had_old_value_(std::getenv(kName) != nullptr) {
    if (had_old_value_) {
      old_value_ = std::getenv(kName);
    }
    if (!value.empty()) {
      setenv(kName, value.c_str(), 1);
    }
  }

  ~ScopedFfmpegCaptureOptions() {
    if (had_old_value_) {
      setenv(kName, old_value_.c_str(), 1);
    } else {
      unsetenv(kName);
    }
  }

 private:
  static constexpr const char* kName = "OPENCV_FFMPEG_CAPTURE_OPTIONS";
  bool had_old_value_ = false;
  std::string old_value_;
};

static bool ContainsBytes(const std::string& data, const std::string& pattern) {
  return data.find(pattern) != std::string::npos;
}

static std::string ReadVideoProbeBytes(const std::string& uri) {
  std::ifstream input(uri, std::ios::binary);
  if (!input.is_open()) {
    return "";
  }

  input.seekg(0, std::ios::end);
  const std::streamoff file_size = input.tellg();
  if (file_size <= 0) {
    return "";
  }

  constexpr std::streamoff kProbeBytes = 8 * 1024 * 1024;
  const std::streamoff head_size = std::min(file_size, kProbeBytes);
  std::string data(static_cast<std::size_t>(head_size), '\0');
  input.seekg(0, std::ios::beg);
  input.read(&data[0], head_size);

  if (file_size > head_size) {
    const std::streamoff tail_size = std::min(file_size - head_size, kProbeBytes);
    std::string tail(static_cast<std::size_t>(tail_size), '\0');
    input.seekg(file_size - tail_size, std::ios::beg);
    input.read(&tail[0], tail_size);
    data += tail;
  }
  return data;
}

static std::string DetectFfmpegSoftwareDecoderOption(const std::string& uri) {
  const std::string probe = ReadVideoProbeBytes(uri);
  if (probe.empty()) {
    return "";
  }
  if (ContainsBytes(probe, "hvc1") || ContainsBytes(probe, "hev1")) {
    return "video_codec;hevc";
  }
  if (ContainsBytes(probe, "avc1") || ContainsBytes(probe, "avc3")) {
    return "video_codec;h264";
  }
  if (ContainsBytes(probe, "MJPG") || ContainsBytes(probe, "mjpg")) {
    return "video_codec;mjpeg";
  }
  if (ContainsBytes(probe, "mp4v")) {
    return "video_codec;mpeg4";
  }
  return "";
}

static bool OpenVideoWithSoftwareDecode(cv::VideoCapture* capture, const std::string& uri) {
  if (capture == nullptr) {
    return false;
  }

  const std::string decoder_option = DetectFfmpegSoftwareDecoderOption(uri);
  if (!decoder_option.empty()) {
    ScopedFfmpegCaptureOptions options(decoder_option);
    if (capture->open(uri, cv::CAP_FFMPEG)) {
      return true;
    }
    capture->release();
  }

#if CV_VERSION_MAJOR > 4 || (CV_VERSION_MAJOR == 4 && CV_VERSION_MINOR >= 6)
  const std::vector<int> params = {
      cv::CAP_PROP_HW_ACCELERATION, cv::VIDEO_ACCELERATION_NONE,
      cv::CAP_PROP_HW_ACCELERATION_USE_OPENCL, 0,
      cv::CAP_PROP_OPEN_TIMEOUT_MSEC, 5000,
      cv::CAP_PROP_READ_TIMEOUT_MSEC, 5000,
  };
  if (capture->open(uri, cv::CAP_FFMPEG, params)) {
    return true;
  }
#endif
  if (capture->open(uri, cv::CAP_FFMPEG)) {
    return true;
  }
  return capture->open(uri);
}
#endif

VideoSource::VideoSource(const VideoInputConfig& config) : config_(config) {
#if RK3588_SOP_HAS_OPENCV || RK3588_SOP_ENABLE_ORBBEC
  impl_ = std::make_shared<Impl>();
#endif
}

bool VideoSource::Open() {
  if (config_.type == "orbbec") {
#if RK3588_SOP_ENABLE_ORBBEC
    try {
      // Gemini 同时打开彩色流和深度流，深度流必须按 D2C 对齐到彩色坐标。
      impl_->pipeline = std::make_shared<ob::Pipeline>();
      impl_->config = std::make_shared<ob::Config>();

      std::shared_ptr<ob::StreamProfileList> color_profiles = impl_->pipeline->getStreamProfileList(OB_SENSOR_COLOR);
      std::shared_ptr<ob::VideoStreamProfile> color_profile =
          SelectOrbbecColorProfile(color_profiles, config_.width, config_.height, config_.fps);
      if (!color_profile) {
        std::cerr << "未找到可用的 Orbbec 彩色流 profile" << std::endl;
        return false;
      }
      impl_->config->enableStream(color_profile);

      std::shared_ptr<ob::StreamProfileList> depth_profiles;
      // 优先获取与彩色流匹配的 D2C 深度 profile，保证 RGB 像素能查深度。
      depth_profiles = impl_->pipeline->getD2CDepthProfileList(color_profile, ALIGN_D2C_SW_MODE);
      if (!depth_profiles || depth_profiles->count() == 0) {
        impl_->transform_depth_to_color = true;
        depth_profiles = impl_->pipeline->getStreamProfileList(OB_SENSOR_DEPTH);
      }
      std::shared_ptr<ob::VideoStreamProfile> depth_profile =
          impl_->transform_depth_to_color ? SelectOrbbecTransformDepthProfile(depth_profiles, config_.fps)
                                          : depth_profiles->getProfile(OB_PROFILE_DEFAULT)->as<ob::VideoStreamProfile>();
      if (!depth_profile) {
        std::cerr << "未找到可用的 Orbbec 深度流 profile" << std::endl;
        return false;
      }
      impl_->config->enableStream(depth_profile);
      impl_->config->setAlignMode(impl_->transform_depth_to_color ? ALIGN_DISABLE : ALIGN_D2C_SW_MODE);
      // 有些 Gemini/OpenNI 设备不支持硬件帧同步；不支持时仍可继续使用 D2C 对齐深度。
      try {
        impl_->pipeline->enableFrameSync();
      } catch (const ob::Error& error) {
        std::cerr << "奥比中光帧同步不可用，继续使用 D2C 对齐: " << error.getMessage() << std::endl;
      }
      impl_->pipeline->start(impl_->config);
      impl_->calibration_param = impl_->pipeline->getCalibrationParam(impl_->config);
      impl_->has_calibration_param = true;
      std::cout << "奥比中光 Gemini 已打开: color=" << color_profile->width() << "x" << color_profile->height()
                << "@" << color_profile->fps() << ", depth_raw=" << depth_profile->width() << "x"
                << depth_profile->height() << "@" << depth_profile->fps()
                << (impl_->transform_depth_to_color ? ", depth_align=SDK transformation" : ", depth_align=D2C profile")
                << std::endl;
      return true;
    } catch (const ob::Error& error) {
      std::cerr << "打开奥比中光 Gemini 失败: " << error.getMessage() << std::endl;
      return false;
    } catch (const std::exception& error) {
      std::cerr << "打开奥比中光 Gemini 失败: " << error.what() << std::endl;
      return false;
    }
#else
    std::cerr << "当前未启用奥比中光 SDK，请使用 -DENABLE_ORBBEC=ON 重新编译" << std::endl;
    return false;
#endif
  }

#if RK3588_SOP_HAS_OPENCV
  if (config_.type == "camera") {
    impl_->capture.open(std::stoi(config_.uri), cv::CAP_V4L2);
    impl_->capture.set(cv::CAP_PROP_FRAME_WIDTH, config_.width);
    impl_->capture.set(cv::CAP_PROP_FRAME_HEIGHT, config_.height);
    impl_->capture.set(cv::CAP_PROP_FPS, config_.fps);
  } else if (config_.type == "video") {
    if (!OpenVideoWithSoftwareDecode(&impl_->capture, config_.uri)) {
      std::cerr << "视频文件软件解码打开失败: " << config_.uri << std::endl;
    }
  } else if (config_.type == "gstreamer") {
    impl_->capture.open(config_.uri, cv::CAP_GSTREAMER);
  } else {
    std::cerr << "不支持的视频输入类型: " << config_.type << std::endl;
    return false;
  }

  if (!impl_->capture.isOpened()) {
    std::cerr << "无法打开视频输入: " << config_.uri << std::endl;
    return false;
  }
  return true;
#else
  std::cerr << "当前构建未找到 OpenCV，无法打开真实视频输入" << std::endl;
  return false;
#endif
}

bool VideoSource::Read(ImageFrame* frame) {
  if (frame == nullptr) {
    return false;
  }
  last_capture_read_ms_ = 0.0;
  last_color_convert_ms_ = 0.0;
  last_frame_copy_ms_ = 0.0;
  if (config_.type == "orbbec") {
    // Orbbec 输入天然走 RGB-D，单独读取彩色帧时复用 ReadRgbd。
    RgbdFrame rgbd_frame;
    if (!ReadRgbd(&rgbd_frame)) {
      return false;
    }
    *frame = rgbd_frame.color;
    return true;
  }

#if RK3588_SOP_HAS_OPENCV
  cv::Mat image;
  const std::chrono::steady_clock::time_point read_begin = std::chrono::steady_clock::now();
  if (!impl_->capture.read(image) || image.empty()) {
    return false;
  }
  const std::chrono::steady_clock::time_point read_end = std::chrono::steady_clock::now();
  last_capture_read_ms_ = std::chrono::duration<double, std::milli>(read_end - read_begin).count();
  if (image.channels() != 3) {
    const std::chrono::steady_clock::time_point convert_begin = std::chrono::steady_clock::now();
    cv::cvtColor(image, image, cv::COLOR_GRAY2BGR);
    const std::chrono::steady_clock::time_point convert_end = std::chrono::steady_clock::now();
    last_color_convert_ms_ = std::chrono::duration<double, std::milli>(convert_end - convert_begin).count();
  }
  ++frame_id_;
  frame->frame_id = frame_id_;
  frame->timestamp_sec = NowInSeconds();
  if (config_.type == "video") {
    const double pos_msec = impl_->capture.get(cv::CAP_PROP_POS_MSEC);
    if (pos_msec > 0.0) {
      frame->timestamp_sec = pos_msec / 1000.0;
    } else if (config_.fps > 0) {
      frame->timestamp_sec = static_cast<double>(frame_id_ - 1) / static_cast<double>(config_.fps);
    }
  }
  frame->frame_id_name = config_.type == "video" ? "video" : "camera";
  frame->width = image.cols;
  frame->height = image.rows;
  frame->pixel_format = PixelFormat::BGR;
  const std::chrono::steady_clock::time_point copy_begin = std::chrono::steady_clock::now();
  frame->bgr_data.assign(image.data, image.data + image.total() * image.elemSize());
  const std::chrono::steady_clock::time_point copy_end = std::chrono::steady_clock::now();
  last_frame_copy_ms_ = std::chrono::duration<double, std::milli>(copy_end - copy_begin).count();
  return true;
#else
  return false;
#endif
}

bool VideoSource::ReadRgbd(RgbdFrame* frame) {
  if (frame == nullptr) {
    return false;
  }

  if (config_.type == "orbbec") {
#if RK3588_SOP_ENABLE_ORBBEC
    try {
      // waitForFrames 返回一组同步后的帧；部分设备启动初期会先返回单路帧。
      std::shared_ptr<ob::FrameSet> frameset;
      for (int attempt = 0; attempt < 5; ++attempt) {
        frameset = impl_->pipeline->waitForFrames(1000);
        if (frameset != nullptr && frameset->colorFrame() != nullptr && frameset->depthFrame() != nullptr) {
          break;
        }
      }
      if (frameset == nullptr || frameset->colorFrame() == nullptr || frameset->depthFrame() == nullptr) {
        return false;
      }
      // 丢弃已经积压的旧帧，只处理队列中最新的一组 RGB-D 帧，避免算法
      // 推理速度低于相机帧率时延迟持续累积。
      // SDK 内部队列在处理线程落后时可能积累几十到上百帧；只清理几帧
      // 仍会把数秒前的画面送入推流。持续快速取帧，直到队列基本为空。
      for (int drain = 0; drain < 120; ++drain) {
        try {
          std::shared_ptr<ob::FrameSet> newer = impl_->pipeline->waitForFrames(1);
          if (newer != nullptr && newer->colorFrame() != nullptr && newer->depthFrame() != nullptr) {
            frameset = std::move(newer);
          } else {
            break;
          }
        } catch (...) {
          break;
        }
      }

      std::shared_ptr<ob::ColorFrame> color_frame = frameset->colorFrame();
      std::shared_ptr<ob::DepthFrame> depth_frame = frameset->depthFrame();
      const int color_width = static_cast<int>(color_frame->width());
      const int color_height = static_cast<int>(color_frame->height());
      bool software_depth_resize = impl_->software_depth_fallback;
      if (impl_->transform_depth_to_color && !software_depth_resize) {
        std::shared_ptr<ob::Frame> transformed_depth =
            ob::CoordinateTransformHelper::transformationDepthFrameToColorCamera(
                impl_->pipeline->getDevice(), depth_frame, static_cast<uint32_t>(color_width),
                static_cast<uint32_t>(color_height));
        if (transformed_depth == nullptr || transformed_depth->as<ob::DepthFrame>() == nullptr) {
          software_depth_resize = true;
          impl_->software_depth_fallback = true;
        } else {
          depth_frame = transformed_depth->as<ob::DepthFrame>();
        }
      }
      const int depth_width = static_cast<int>(depth_frame->width());
      const int depth_height = static_cast<int>(depth_frame->height());
      // D2C 对齐后深度图尺寸必须和彩色图一致，否则不能用 RGB 坐标查深度。
      if (!software_depth_resize && (color_width != depth_width || color_height != depth_height)) {
        std::cerr << "深度图未对齐到彩色图，拒绝输出 RGB-D 帧" << std::endl;
        return false;
      }
      if (!logged_rgbd_profile_) {
        std::cout << "奥比中光 RGB-D 实际输出: color=" << color_width << "x" << color_height
                  << ", depth=" << depth_width << "x" << depth_height
                  << ", depth_scale_m=" << depth_frame->getValueScale() * 0.001F
                  << ", aligned_to_color=true"
                  << (software_depth_resize ? ", align_method=software_resize"
                                             : (impl_->transform_depth_to_color ? ", align_method=transformation"
                                                                                 : ", align_method=d2c"))
                  << std::endl;
        logged_rgbd_profile_ = true;
      }

      ++frame_id_;
      frame->color.frame_id = frame_id_;
      frame->color.timestamp_sec = NowInSeconds();
      frame->color.frame_id_name = "orbbec_color";
      frame->color.width = color_width;
      frame->color.height = color_height;
      if (!ConvertOrbbecColorToRgb(color_frame, &frame->color)) {
        return false;
      }

      frame->depth_width = software_depth_resize ? color_width : depth_width;
      frame->depth_height = software_depth_resize ? color_height : depth_height;
      // SDK 深度比例通常为毫米单位，内部统一换算成米。
      frame->depth_scale = depth_frame->getValueScale() * 0.001F;
      frame->depth_aligned_to_color = true;
      if (!software_depth_resize) {
        frame->depth_data.resize(static_cast<std::size_t>(depth_width) * static_cast<std::size_t>(depth_height));
        std::memcpy(frame->depth_data.data(), depth_frame->data(), frame->depth_data.size() * sizeof(std::uint16_t));
      } else {
        const auto* raw = static_cast<const std::uint16_t*>(depth_frame->data());
        if (raw == nullptr || depth_width <= 0 || depth_height <= 0) return false;
        frame->depth_data.resize(static_cast<std::size_t>(color_width) * static_cast<std::size_t>(color_height));
        for (int y = 0; y < color_height; ++y) {
          const int sy = std::min(depth_height - 1, y * depth_height / color_height);
          for (int x = 0; x < color_width; ++x) {
            const int sx = std::min(depth_width - 1, x * depth_width / color_width);
            frame->depth_data[static_cast<std::size_t>(y) * color_width + x] =
                raw[static_cast<std::size_t>(sy) * depth_width + sx];
          }
        }
      }
      return true;
    } catch (const ob::Error& error) {
      std::cerr << "读取奥比中光 RGB-D 帧失败: " << error.getMessage() << std::endl;
      return false;
    }
#else
    std::cerr << "奥比中光后端未启用" << std::endl;
    return false;
#endif
  }

  if (!Read(&frame->color)) {
    return false;
  }

  frame->depth_width = frame->color.width;
  frame->depth_height = frame->color.height;
  frame->depth_scale = 0.001F;
  frame->depth_aligned_to_color = false;
  frame->depth_data.clear();
  return true;
}

bool VideoSource::QueryPoint3D(const RgbdFrame& frame, const int pixel_x, const int pixel_y, Point3D* point) const {
  // 目标中心和手心保留较大搜索范围，以兼容深度图局部空洞。
  return QueryPoint3D(frame, pixel_x, pixel_y, 25, point);
}

bool VideoSource::QueryPoint3D(const RgbdFrame& frame, const int pixel_x, const int pixel_y,
                               const int max_search_radius, Point3D* point) const {
  if (point == nullptr) {
    return false;
  }
  *point = Point3D{};

  if (config_.type == "orbbec") {
#if RK3588_SOP_ENABLE_ORBBEC
    // 先从已对齐深度图取稳定深度，再交给 SDK 做 2D 到 3D 坐标转换。
    Point3D depth_point;
    if (!QueryAlignedDepthPoint(frame, pixel_x, pixel_y, max_search_radius, &depth_point)) {
      return false;
    }
    if (!impl_->has_calibration_param) {
      *point = depth_point;
      return true;
    }
    OBPoint2f pixel{static_cast<float>(pixel_x), static_cast<float>(pixel_y)};
    OBPoint3f camera_point{};
    const float depth_mm = depth_point.z * 1000.0F;
    // 不手写内参反投影，优先使用 Orbbec SDK 标定参数完成坐标转换。
    const bool ok = ob::CoordinateTransformHelper::calibration2dTo3d(
        impl_->calibration_param, pixel, depth_mm, OB_SENSOR_COLOR, OB_SENSOR_COLOR, &camera_point);
    if (!ok) {
      *point = depth_point;
      return true;
    }
    point->x = camera_point.x * 0.001F;
    point->y = camera_point.y * 0.001F;
    point->z = camera_point.z * 0.001F;
    point->valid = true;
    return true;
#else
    return false;
#endif
  }

  return QueryAlignedDepthPoint(frame, pixel_x, pixel_y, max_search_radius, point);
}

bool VideoSource::ProjectPointToColor(const RgbdFrame& frame, const Point3D& point, ImagePoint* pixel) const {
  if (pixel == nullptr || !point.valid || point.z <= 0.0F || frame.color.width <= 0 || frame.color.height <= 0) {
    return false;
  }

  float pixel_x = 0.0F;
  float pixel_y = 0.0F;
  bool projected_by_sdk = false;
  if (config_.type == "orbbec") {
#if RK3588_SOP_ENABLE_ORBBEC
    if (impl_->has_calibration_param) {
      const OBPoint3f camera_point{point.x * 1000.0F, point.y * 1000.0F, point.z * 1000.0F};
      OBPoint2f color_pixel{};
      // 约束点位于彩色相机坐标系，SDK 使用相同标定参数投回彩色图。
      projected_by_sdk = ob::CoordinateTransformHelper::calibration3dTo2d(
          impl_->calibration_param, camera_point, OB_SENSOR_COLOR, OB_SENSOR_COLOR, &color_pixel);
      if (projected_by_sdk) {
        pixel_x = color_pixel.x;
        pixel_y = color_pixel.y;
      }
    }
#endif
  }

  if (!projected_by_sdk) {
    // 非 Orbbec 或没有标定参数时使用与简化反投影一致的针孔模型保底。
    const float fx = static_cast<float>(std::max(frame.color.width, 1));
    const float fy = static_cast<float>(std::max(frame.color.width, 1));
    const float cx = static_cast<float>(frame.color.width) * 0.5F;
    const float cy = static_cast<float>(frame.color.height) * 0.5F;
    pixel_x = point.x * fx / point.z + cx;
    pixel_y = point.y * fy / point.z + cy;
  }

  if (!std::isfinite(pixel_x) || !std::isfinite(pixel_y)) {
    return false;
  }
  const int x = static_cast<int>(std::lround(pixel_x));
  const int y = static_cast<int>(std::lround(pixel_y));
  if (x < 0 || y < 0 || x >= frame.color.width || y >= frame.color.height) {
    return false;
  }
  *pixel = ImagePoint{x, y};
  return true;
}

void VideoSource::Close() {
  if (config_.type == "orbbec") {
#if RK3588_SOP_ENABLE_ORBBEC
    if (impl_ && impl_->pipeline) {
      impl_->pipeline->stop();
    }
#endif
    return;
  }
#if RK3588_SOP_HAS_OPENCV
  if (impl_ && impl_->capture.isOpened()) {
    impl_->capture.release();
  }
#endif
}

bool VideoSource::QueryAlignedDepthPoint(const RgbdFrame& frame, const int pixel_x, const int pixel_y,
                                         const int max_search_radius, Point3D* point) const {
  if (point == nullptr || !frame.depth_aligned_to_color || frame.depth_data.empty()) {
    return false;
  }
  if (pixel_x < 0 || pixel_y < 0 || pixel_x >= frame.depth_width || pixel_y >= frame.depth_height) {
    return false;
  }

  // 深度图在手部边缘和反光区域容易出现空洞。
  // 从小到大扩大搜索半径，优先使用离查询像素最近的有效深度。
  std::vector<std::uint16_t> depths;
  static const int radii[] = {1, 2, 3, 5, 10, 15, 25};
  for (const int radius : radii) {
    if (radius > std::max(0, max_search_radius)) {
      continue;
    }
    depths.clear();
    for (int dy = -radius; dy <= radius; ++dy) {
      for (int dx = -radius; dx <= radius; ++dx) {
        const int x = pixel_x + dx;
        const int y = pixel_y + dy;
        if (x < 0 || y < 0 || x >= frame.depth_width || y >= frame.depth_height) {
          continue;
        }
        const std::uint16_t depth =
            frame.depth_data[static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.depth_width) +
                             static_cast<std::size_t>(x)];
        if (depth > 0U) {
          depths.push_back(depth);
        }
      }
    }
    if (depths.size() >= 3U) {
      break;
    }
  }
  if (depths.empty()) {
    return false;
  }

  std::sort(depths.begin(), depths.end());
  // 扩大邻域后可能混入背景深度，取偏近的四分位值更接近手表面。
  const std::uint16_t depth_value = depths[std::min(depths.size() / 4U, depths.size() - 1U)];
  const float z = static_cast<float>(depth_value) * frame.depth_scale;
  const float fx = static_cast<float>(std::max(frame.color.width, 1));
  const float fy = static_cast<float>(std::max(frame.color.width, 1));
  const float cx = static_cast<float>(frame.color.width) * 0.5F;
  const float cy = static_cast<float>(frame.color.height) * 0.5F;
  point->x = (static_cast<float>(pixel_x) - cx) * z / fx;
  point->y = (static_cast<float>(pixel_y) - cy) * z / fy;
  point->z = z;
  point->valid = true;
  return true;
}
