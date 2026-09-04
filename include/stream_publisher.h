#ifndef RK3588_SOP_STREAM_PUBLISHER_H_
#define RK3588_SOP_STREAM_PUBLISHER_H_
#include <cstdint>
#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <thread>
class StreamPublisher {
 public:
  ~StreamPublisher() { Close(); }
  bool Open(int width, int height, int fps, const std::string& url);
  bool OpenFile(int width, int height, int fps, const std::string& path);
  bool PushBgr(const std::uint8_t* data, std::size_t size);
  void Close();
  bool opened() const { return opened_; }
 private:
  void* pipeline_ = nullptr;
  void* appsrc_ = nullptr;
  bool opened_ = false;
  bool stopping_ = false;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<std::vector<std::uint8_t>> queue_;
  std::thread worker_;
  void WorkerLoop();
};
#endif
