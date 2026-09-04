#include "stream_publisher.h"
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#include <cstring>

void StreamPublisher::WorkerLoop() {
  for (;;) {
    std::vector<std::uint8_t> frame;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
      if (queue_.empty() && stopping_) break;
      frame = std::move(queue_.back());
      queue_.clear();
    }
    if (!appsrc_ || frame.empty()) continue;
    GstBus* bus = gst_element_get_bus(GST_ELEMENT(pipeline_));
    if (bus) {
      GstMessage* message = gst_bus_pop_filtered(bus, static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
      gst_object_unref(bus);
      if (message) {
        gst_message_unref(message);
        std::lock_guard<std::mutex> lock(mutex_);
        opened_ = false; stopping_ = true; queue_.clear(); cv_.notify_all(); break;
      }
    }
    GstBuffer* buffer = gst_buffer_new_allocate(nullptr, frame.size(), nullptr);
    if (!buffer) continue;
    GstMapInfo map{};
    if (!gst_buffer_map(buffer, &map, GST_MAP_WRITE)) { gst_buffer_unref(buffer); continue; }
    std::memcpy(map.data, frame.data(), frame.size());
    gst_buffer_unmap(buffer, &map);
    if (gst_app_src_push_buffer(GST_APP_SRC(appsrc_), buffer) != GST_FLOW_OK) {
      std::lock_guard<std::mutex> lock(mutex_);
      opened_ = false; stopping_ = true; queue_.clear(); cv_.notify_all(); break;
    }
  }
}

bool StreamPublisher::Open(int width, int height, int fps, const std::string& url) {
  if (width <= 0 || height <= 0 || fps <= 0 || url.empty()) return false;
  gst_init(nullptr, nullptr);
  const std::size_t frame_bytes = static_cast<std::size_t>(width) * height * 3U;
  const std::size_t queue_bytes = frame_bytes * 2U;
  std::string pipe = "appsrc name=src is-live=true block=false max-bytes=" + std::to_string(queue_bytes) +
      " format=time do-timestamp=true caps=\"video/x-raw,format=BGR,width=" + std::to_string(width) +
      ",height=" + std::to_string(height) + ",framerate=" + std::to_string(fps) + "/1\" ! queue leaky=downstream max-size-buffers=2 max-size-bytes=" +
      std::to_string(queue_bytes) + " ! videoconvert ! video/x-raw,format=NV12 ! mpph264enc bps=2500000 gop=30 ! h264parse config-interval=1 ! flvmux streamable=true ! rtmpsink sync=false location=" + url;
  GError* error = nullptr;
  GstElement* pipeline = gst_parse_launch(pipe.c_str(), &error);
  if (!pipeline) { if (error) g_error_free(error); return false; }
  GstElement* source = gst_bin_get_by_name(GST_BIN(pipeline), "src");
  if (!source || gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
    if (source) gst_object_unref(source);
    gst_object_unref(pipeline);
    return false;
  }
  pipeline_ = pipeline; appsrc_ = source;
  { std::lock_guard<std::mutex> lock(mutex_); stopping_ = false; opened_ = true; }
  worker_ = std::thread(&StreamPublisher::WorkerLoop, this);
  return true;
}

bool StreamPublisher::OpenFile(int width, int height, int fps, const std::string& path) {
  if (width <= 0 || height <= 0 || fps <= 0 || path.empty()) return false;
  gst_init(nullptr, nullptr);
  std::string pipe = "appsrc name=src is-live=true block=false max-bytes=0 format=time do-timestamp=true caps=\"video/x-raw,format=BGR,width=" + std::to_string(width) + ",height=" + std::to_string(height) + ",framerate=" + std::to_string(fps) + "/1\" ! queue max-size-buffers=4 leaky=downstream ! videoconvert ! video/x-raw,format=NV12 ! mpph264enc bps=12000000 gop=30 ! h264parse ! mp4mux ! filesink sync=false location=\"" + path + "\"";
  GError* error = nullptr; GstElement* pipeline = gst_parse_launch(pipe.c_str(), &error);
  if (!pipeline || error) { if (error) g_error_free(error); if (pipeline) gst_object_unref(pipeline); return false; }
  GstElement* source = gst_bin_get_by_name(GST_BIN(pipeline), "src");
  if (!source || gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) { if (source) gst_object_unref(source); gst_object_unref(pipeline); return false; }
  pipeline_ = pipeline; appsrc_ = source; { std::lock_guard<std::mutex> lock(mutex_); stopping_ = false; opened_ = true; } worker_ = std::thread(&StreamPublisher::WorkerLoop, this); return true;
}

bool StreamPublisher::PushBgr(const std::uint8_t* data, std::size_t size) {
  if (!data || !size) return false;
  std::lock_guard<std::mutex> lock(mutex_);
  if (!opened_ || stopping_) return false;
  queue_.emplace_back(data, data + size);
  while (queue_.size() > 2) queue_.pop_front();
  cv_.notify_one(); return true;
}

void StreamPublisher::Close() {
  { std::lock_guard<std::mutex> lock(mutex_); if (!pipeline_ && !worker_.joinable()) return; stopping_ = true; opened_ = false; queue_.clear(); }
  cv_.notify_all(); if (worker_.joinable()) worker_.join();
  if (appsrc_) gst_app_src_end_of_stream(GST_APP_SRC(appsrc_));
  if (pipeline_) gst_element_set_state(GST_ELEMENT(pipeline_), GST_STATE_NULL);
  if (appsrc_) gst_object_unref(appsrc_);
  if (pipeline_) gst_object_unref(pipeline_);
  appsrc_ = nullptr; pipeline_ = nullptr;
}
