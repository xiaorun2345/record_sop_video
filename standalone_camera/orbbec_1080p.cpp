#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#include <libobsensor/ObSensor.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

namespace {

constexpr int kFps = 30;
constexpr std::size_t kMaxQueuedFrames = 4;

struct AppOptions {
  double record_seconds = 0.0;
  int width = 1920;
  int height = 1080;
};

bool ParseArgs(const int argc, char** argv, AppOptions* options) {
  if (options == nullptr) {
    return false;
  }
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--record-seconds" && i + 1 < argc) {
      options->record_seconds = std::strtod(argv[++i], nullptr);
    } else if (arg.rfind("--record-seconds=", 0) == 0) {
      options->record_seconds = std::strtod(arg.substr(17).c_str(), nullptr);
    } else if (arg == "--resolution" && i + 1 < argc) {
      const std::string value = argv[++i];
      if (value == "640x480") { options->width = 640; options->height = 480; }
      else if (value != "1920x1080") { std::cerr << "分辨率仅支持 1920x1080 或 640x480" << std::endl; return false; }
    } else if (arg.rfind("--resolution=", 0) == 0) {
      const std::string value = arg.substr(13);
      if (value == "640x480") { options->width = 640; options->height = 480; }
      else if (value != "1920x1080") { std::cerr << "分辨率仅支持 1920x1080 或 640x480" << std::endl; return false; }
    } else if (arg == "-h" || arg == "--help") {
      std::cout << "Usage: orbbec_1080p [--resolution 1920x1080|640x480] [--record-seconds N]" << std::endl;
      return false;
    } else {
      std::cerr << "未知参数: " << arg << std::endl;
      return false;
    }
  }
  if (options->record_seconds < 0.0) {
    options->record_seconds = 0.0;
  }
  return true;
}

std::shared_ptr<ob::VideoStreamProfile> Find1080pColorProfile(
    const std::shared_ptr<ob::StreamProfileList>& profiles, int width, int height) {
  if (!profiles) {
    return nullptr;
  }

  // 1080p 彩色流通常使用 MJPEG；同时兼容设备提供的其他常见格式。
  for (const OBFormat format : {OB_FORMAT_MJPG, OB_FORMAT_RGB, OB_FORMAT_BGR,
                                OB_FORMAT_YUYV, OB_FORMAT_UYVY}) {
    try {
      return profiles->getVideoStreamProfile(width, height, format, kFps);
    } catch (const ob::Error&) {
      // 当前格式没有精确匹配，继续尝试下一个格式。
    }
  }
  return nullptr;
}

bool ColorFrameToBgr(const std::shared_ptr<ob::ColorFrame>& frame, cv::Mat* bgr) {
    if (!frame || bgr == nullptr || frame->data() == nullptr ||
      frame->data() == nullptr) {
    return false;
  }

  const auto* data = static_cast<const std::uint8_t*>(frame->data());
  const int width = static_cast<int>(frame->width());
  const int height = static_cast<int>(frame->height());
  const std::size_t rgb_size = static_cast<std::size_t>(width) * height * 3U;

  switch (frame->format()) {
    case OB_FORMAT_BGR: {
      if (frame->dataSize() < rgb_size) {
        return false;
      }
      cv::Mat view(height, width, CV_8UC3, const_cast<std::uint8_t*>(data));
      *bgr = view.clone();
      return true;
    }
    case OB_FORMAT_RGB: {
      if (frame->dataSize() < rgb_size) {
        return false;
      }
      cv::Mat rgb(height, width, CV_8UC3, const_cast<std::uint8_t*>(data));
      cv::cvtColor(rgb, *bgr, cv::COLOR_RGB2BGR);
      return true;
    }
    case OB_FORMAT_MJPG: {
      cv::Mat encoded(1, static_cast<int>(frame->dataSize()), CV_8UC1,
                      const_cast<std::uint8_t*>(data));
      *bgr = cv::imdecode(encoded, cv::IMREAD_COLOR);
      return !bgr->empty() && bgr->cols == width && bgr->rows == height;
    }
    case OB_FORMAT_YUYV: {
      cv::Mat yuyv(height, width, CV_8UC2, const_cast<std::uint8_t*>(data));
      cv::cvtColor(yuyv, *bgr, cv::COLOR_YUV2BGR_YUY2);
      return true;
    }
    case OB_FORMAT_UYVY: {
      cv::Mat uyvy(height, width, CV_8UC2, const_cast<std::uint8_t*>(data));
      cv::cvtColor(uyvy, *bgr, cv::COLOR_YUV2BGR_UYVY);
      return true;
    }
    default:
      return false;
  }
}

enum class RecordCommandType {
  kStart,
  kFrame,
  kStop,
  kExit,
};

struct RecordCommand {
  RecordCommandType type = RecordCommandType::kFrame;
  std::string path;
  cv::Mat frame;
  std::uint64_t capture_time_ns = 0;
};

std::uint64_t SteadyTimeNs() {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                        std::chrono::steady_clock::now().time_since_epoch())
                                        .count());
}

std::string MakeRecordingPath() {
  const auto now = std::chrono::system_clock::now();
  const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                                now.time_since_epoch()) %
                            1000;
  const std::time_t time = std::chrono::system_clock::to_time_t(now);
  std::tm local_time{};
  localtime_r(&time, &local_time);

  const std::filesystem::path directory = std::filesystem::current_path() / "recordings";
  std::filesystem::create_directories(directory);

  std::ostringstream filename;
  filename << "orbbec_" << std::put_time(&local_time, "%Y%m%d_%H%M%S") << '_'
           << std::setw(3) << std::setfill('0') << milliseconds.count() << ".mp4";
  return (directory / filename.str()).string();
}

std::string EscapeGstreamerString(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const char character : value) {
    if (character == '\\' || character == '"') {
      escaped.push_back('\\');
    }
    escaped.push_back(character);
  }
  return escaped;
}

bool HasGstreamerElement(const char* name) {
  GstElementFactory* factory = gst_element_factory_find(name);
  if (factory == nullptr) {
    return false;
  }
  gst_object_unref(factory);
  return true;
}

class HardwareVideoRecorder {
 public:
  HardwareVideoRecorder(int width, int height) : width_(width), height_(height), worker_(&HardwareVideoRecorder::WorkerLoop, this) {}

  ~HardwareVideoRecorder() {
    Shutdown();
  }

  HardwareVideoRecorder(const HardwareVideoRecorder&) = delete;
  HardwareVideoRecorder& operator=(const HardwareVideoRecorder&) = delete;

  bool Start() {
    bool expected = false;
    if (!recording_.compare_exchange_strong(expected, true)) {
      return false;
    }

    try {
      const std::string path = MakeRecordingPath();
      {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        commands_.push_back({RecordCommandType::kStart, path, {}, 0});
      }
      dropped_frames_ = 0;
      queue_condition_.notify_one();
      std::cout << "开始录像: " << path << std::endl;
      return true;
    } catch (const std::exception& error) {
      recording_ = false;
      std::cerr << "无法创建录像文件: " << error.what() << std::endl;
      return false;
    }
  }

  bool Stop() {
    if (!recording_.exchange(false)) {
      return false;
    }
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      commands_.push_back({RecordCommandType::kStop, {}, {}, 0});
    }
    queue_condition_.notify_one();
    std::cout << "停止录像，正在后台封装 MP4" << std::endl;
    return true;
  }

  void Submit(const cv::Mat& frame) {
    if (!recording_.load(std::memory_order_relaxed) || frame.empty()) {
      return;
    }

    // 相机线程只复制一帧并入队，编码、写盘和 MP4 收尾全部在后台执行。
    cv::Mat frame_copy = frame.clone();
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (!recording_.load(std::memory_order_relaxed)) {
      return;
    }
    if (queued_frame_count_ >= kMaxQueuedFrames) {
      for (auto iterator = commands_.begin(); iterator != commands_.end(); ++iterator) {
        if (iterator->type == RecordCommandType::kFrame) {
          commands_.erase(iterator);
          --queued_frame_count_;
          ++dropped_frames_;
          break;
        }
      }
    }
    commands_.push_back(
        {RecordCommandType::kFrame, {}, std::move(frame_copy), SteadyTimeNs()});
    ++queued_frame_count_;
    queue_condition_.notify_one();
  }

  bool IsRecording() const {
    return recording_.load(std::memory_order_relaxed);
  }

  std::uint64_t DroppedFrames() const {
    return dropped_frames_.load(std::memory_order_relaxed);
  }

 private:
  struct EncodingSession {
    GstElement* pipeline = nullptr;
    GstElement* app_source = nullptr;
    std::uint64_t first_capture_time_ns = 0;
      std::string path;
    int width = 1920;
    int height = 1080;
  };

  static void ReportGstreamerError(GstElement* pipeline, const char* prefix) {
    GstBus* bus = gst_element_get_bus(pipeline);
    GstMessage* message = gst_bus_pop_filtered(bus, GST_MESSAGE_ERROR);
    if (message != nullptr) {
      GError* error = nullptr;
      gchar* debug = nullptr;
      gst_message_parse_error(message, &error, &debug);
      std::cerr << prefix << ": " << (error ? error->message : "未知错误") << std::endl;
      g_clear_error(&error);
      g_free(debug);
      gst_message_unref(message);
    } else {
      std::cerr << prefix << std::endl;
    }
    gst_object_unref(bus);
  }

  static bool OpenSession(const std::string& path, int width, int height, EncodingSession* session) {
    if (!HasGstreamerElement("mpph264enc")) {
      std::cerr << "没有可用的硬件 H.264 编码器: 需要 mpph264enc" << std::endl;
      return false;
    }

    const std::string pipeline_description =
        "appsrc name=video_source is-live=true format=time block=false "
        "caps=video/x-raw,format=BGR,width=" + std::to_string(session->width) + ",height=" + std::to_string(session->height) + ",framerate=30/1 "
        "! queue max-size-buffers=4 leaky=downstream "
        "! videoconvert ! video/x-raw,format=NV12 "
        "! mpph264enc bps=12000000 gop=30 rc-mode=cbr header-mode=each-idr "
        "! h264parse ! mp4mux "
        "! filesink sync=false location=\"" +
        EscapeGstreamerString(path) + "\"";
    std::cout << "录像编码器: mpph264enc" << std::endl;

    GError* error = nullptr;
    session->width = width; session->height = height;
    session->pipeline = gst_parse_launch(pipeline_description.c_str(), &error);
    if (session->pipeline == nullptr || error != nullptr) {
      std::cerr << "创建 MPP 硬件编码管线失败: "
                << (error ? error->message : "未知错误") << std::endl;
      g_clear_error(&error);
      if (session->pipeline != nullptr) {
        gst_object_unref(session->pipeline);
        session->pipeline = nullptr;
      }
      return false;
    }

    session->app_source = gst_bin_get_by_name(GST_BIN(session->pipeline), "video_source");
    session->path = path;
    session->first_capture_time_ns = 0;
    if (session->app_source == nullptr ||
        gst_element_set_state(session->pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
      ReportGstreamerError(session->pipeline, "启动 MPP 硬件编码失败");
      if (session->app_source != nullptr) {
        gst_object_unref(session->app_source);
      }
      gst_element_set_state(session->pipeline, GST_STATE_NULL);
      gst_object_unref(session->pipeline);
      *session = {};
      return false;
    }
    return true;
  }

  static bool PushFrame(cv::Mat frame, const std::uint64_t capture_time_ns,
                        EncodingSession* session) {
    if (session->pipeline == nullptr || session->app_source == nullptr || frame.empty() ||
        !frame.isContinuous()) {
      return false;
    }

    const std::size_t byte_count = frame.total() * frame.elemSize();
    auto* frame_holder = new cv::Mat(std::move(frame));
    GstBuffer* buffer = gst_buffer_new_wrapped_full(
        GST_MEMORY_FLAG_READONLY, frame_holder->data, byte_count, 0, byte_count,
        frame_holder, [](gpointer data) { delete static_cast<cv::Mat*>(data); });
    if (buffer == nullptr) {
      delete frame_holder;
      return false;
    }

    if (session->first_capture_time_ns == 0) {
      session->first_capture_time_ns = capture_time_ns;
    }
    GST_BUFFER_PTS(buffer) = capture_time_ns - session->first_capture_time_ns;
    GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale(1, GST_SECOND, kFps);

    const GstFlowReturn result = gst_app_src_push_buffer(GST_APP_SRC(session->app_source), buffer);
    return result == GST_FLOW_OK;
  }

  static void CloseSession(EncodingSession* session) {
    if (session->pipeline == nullptr) {
      return;
    }

    gst_app_src_end_of_stream(GST_APP_SRC(session->app_source));
    GstBus* bus = gst_element_get_bus(session->pipeline);
    GstMessage* message = gst_bus_timed_pop_filtered(
        bus, 10 * GST_SECOND,
        static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
    if (message == nullptr) {
      std::cerr << "录像封装超时: " << session->path << std::endl;
    } else if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
      GError* error = nullptr;
      gchar* debug = nullptr;
      gst_message_parse_error(message, &error, &debug);
      std::cerr << "录像编码失败: " << (error ? error->message : "未知错误") << std::endl;
      g_clear_error(&error);
      g_free(debug);
      gst_message_unref(message);
    } else {
      gst_message_unref(message);
      std::cout << "录像已保存: " << session->path << std::endl;
    }
    gst_object_unref(bus);
    gst_element_set_state(session->pipeline, GST_STATE_NULL);
    gst_object_unref(session->app_source);
    gst_object_unref(session->pipeline);
    *session = {};
  }

  void WorkerLoop() {
    EncodingSession session;
    while (true) {
      RecordCommand command;
      {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        queue_condition_.wait(lock, [this] { return !commands_.empty(); });
        command = std::move(commands_.front());
        commands_.pop_front();
        if (command.type == RecordCommandType::kFrame) {
          --queued_frame_count_;
        }
      }

      if (command.type == RecordCommandType::kStart) {
        CloseSession(&session);
        if (!OpenSession(command.path, width_, height_, &session)) {
          recording_ = false;
        }
      } else if (command.type == RecordCommandType::kFrame) {
        if (session.pipeline != nullptr &&
            !PushFrame(std::move(command.frame), command.capture_time_ns, &session)) {
          std::cerr << "向 MPP 编码器提交帧失败" << std::endl;
          recording_ = false;
          CloseSession(&session);
        }
      } else if (command.type == RecordCommandType::kStop) {
        CloseSession(&session);
      } else if (command.type == RecordCommandType::kExit) {
        CloseSession(&session);
        return;
      }
    }
  }

  void Shutdown() {
    Stop();
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      commands_.push_back({RecordCommandType::kExit, {}, {}, 0});
    }
    queue_condition_.notify_one();
    if (worker_.joinable()) {
      worker_.join();
    }
  }

  std::atomic<bool> recording_{false};
  int width_ = 1920;
  int height_ = 1080;
  std::atomic<std::uint64_t> dropped_frames_{0};
  std::mutex queue_mutex_;
  std::condition_variable queue_condition_;
  std::deque<RecordCommand> commands_;
  std::size_t queued_frame_count_ = 0;
  std::thread worker_;
};

}  // namespace

int main(int argc, char** argv) {
  gst_init(nullptr, nullptr);
  AppOptions options;
  if (!ParseArgs(argc, argv, &options)) {
    return 1;
  }

  try {
    auto camera_pipeline = std::make_shared<ob::Pipeline>();
    auto profiles = camera_pipeline->getStreamProfileList(OB_SENSOR_COLOR);
    auto color_profile = Find1080pColorProfile(profiles, options.width, options.height);
    if (!color_profile) {
      std::cerr << "相机不支持所选分辨率@30fps 彩色流" << std::endl;
      return 1;
    }

    auto config = std::make_shared<ob::Config>();
    config->enableStream(color_profile);
    camera_pipeline->start(config);

    std::cout << "已打开 Orbbec 彩色相机: " << color_profile->width() << "x"
              << color_profile->height() << "@" << color_profile->fps()
              << "，S 开始录像，Q 停止录像，Esc 退出" << std::endl;

    const std::string window_name = "Orbbec " + std::to_string(options.width) + "x" + std::to_string(options.height);
    cv::namedWindow(window_name, cv::WINDOW_NORMAL);
    cv::resizeWindow(window_name, 1280, 720);
    HardwareVideoRecorder recorder(options.width, options.height);
    const auto auto_record_start = std::chrono::steady_clock::now();
    bool auto_record_started = false;

    auto fps_start = std::chrono::steady_clock::now();
    int fps_frame_count = 0;
    double measured_fps = 0.0;

    while (true) {
      auto frames = camera_pipeline->waitForFrames(1000);
      if (!frames) {
        std::cerr << "等待相机画面超时" << std::endl;
        continue;
      }

      auto color_frame = frames->colorFrame();
      cv::Mat image;
      if (!ColorFrameToBgr(color_frame, &image)) {
        std::cerr << "无法转换相机彩色帧，格式值: "
                  << (color_frame ? static_cast<int>(color_frame->format()) : -1) << std::endl;
        continue;
      }

      if (options.record_seconds > 0.0 && !auto_record_started) {
        recorder.Start();
        auto_record_started = true;
      }
      recorder.Submit(image);

      ++fps_frame_count;
      const auto now = std::chrono::steady_clock::now();
      const double elapsed_seconds = std::chrono::duration<double>(now - fps_start).count();
      if (elapsed_seconds >= 1.0) {
        measured_fps = static_cast<double>(fps_frame_count) / elapsed_seconds;
        std::cout << "实际帧率: " << std::fixed << std::setprecision(1) << measured_fps
                  << " FPS" << std::endl;
        fps_frame_count = 0;
        fps_start = now;
      }

      std::ostringstream fps_text;
      fps_text << "FPS: " << std::fixed << std::setprecision(1) << measured_fps;
      cv::putText(image, fps_text.str(), cv::Point(30, 55), cv::FONT_HERSHEY_SIMPLEX,
                  1.2, cv::Scalar(0, 255, 0), 2, cv::LINE_AA);
      if (recorder.IsRecording()) {
        cv::circle(image, cv::Point(38, 98), 10, cv::Scalar(0, 0, 255), -1, cv::LINE_AA);
        const std::string record_text =
            "REC  DROP: " + std::to_string(recorder.DroppedFrames());
        cv::putText(image, record_text, cv::Point(58, 108), cv::FONT_HERSHEY_SIMPLEX,
                    1.0, cv::Scalar(0, 0, 255), 2, cv::LINE_AA);
      }
      cv::imshow(window_name, image);
      const int key = cv::waitKey(1) & 0xff;
      if (key == 's' || key == 'S') {
        recorder.Start();
      } else if (key == 'q' || key == 'Q') {
        recorder.Stop();
      } else if (key == 27) {
        break;
      }

      if (auto_record_started) {
        const double auto_record_elapsed =
            std::chrono::duration<double>(now - auto_record_start).count();
        if (auto_record_elapsed >= options.record_seconds) {
          recorder.Stop();
          break;
        }
      }
    }

    recorder.Stop();
    camera_pipeline->stop();
    cv::destroyAllWindows();
    return 0;
  } catch (const ob::Error& error) {
    std::cerr << "Orbbec SDK 错误: " << error.getMessage() << std::endl;
  } catch (const cv::Exception& error) {
    std::cerr << "OpenCV 错误: " << error.what() << std::endl;
  } catch (const std::exception& error) {
    std::cerr << "程序错误: " << error.what() << std::endl;
  }
  return 1;
}
