/*
 * Standalone RKNN hand pose test for wgh22/rk3588 hand models.
 *
 * Usage:
 *   ./rknn_hand_pose_test [video] [hand_detector.rknn] [hand_landmarks.rknn] [output.avi]
 */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <opencv2/opencv.hpp>
#include "rknn_api.h"

static constexpr int kMaxHands = 2;

struct RknnTensor {
  std::vector<float> data;
  std::vector<int> dims;
};

struct LetterboxMeta {
  float scale = 1.0F;
  int pad_left = 0;
  int pad_top = 0;
  int input_w = 0;
  int input_h = 0;
  int src_w = 0;
  int src_h = 0;
};

struct Detection {
  cv::Rect2f box;
  float score = 0.0F;
};

class RknnModel {
 public:
  ~RknnModel() {
    if (ctx_ != 0) {
      rknn_destroy(ctx_);
    }
  }

  bool Load(const std::string& path) {
    std::vector<std::uint8_t> model_data;
    if (!ReadFile(path, &model_data)) {
      std::cerr << "read model failed: " << path << std::endl;
      return false;
    }
    int ret = rknn_init(&ctx_, model_data.data(), static_cast<std::uint32_t>(model_data.size()), 0, nullptr);
    if (ret != RKNN_SUCC) {
      std::cerr << "rknn_init failed: " << path << ", ret=" << ret << std::endl;
      return false;
    }
#ifdef RKNN_NPU_CORE_0_1_2
    rknn_set_core_mask(ctx_, RKNN_NPU_CORE_0_1_2);
#endif
    ret = rknn_query(ctx_, RKNN_QUERY_IN_OUT_NUM, &io_num_, sizeof(io_num_));
    if (ret != RKNN_SUCC || io_num_.n_input != 1 || io_num_.n_output < 1) {
      std::cerr << "query io failed: " << path << ", ret=" << ret << std::endl;
      return false;
    }
    input_attr_ = {};
    input_attr_.index = 0;
    ret = rknn_query(ctx_, RKNN_QUERY_INPUT_ATTR, &input_attr_, sizeof(input_attr_));
    if (ret != RKNN_SUCC) {
      std::cerr << "query input failed: " << path << ", ret=" << ret << std::endl;
      return false;
    }
    output_attrs_.resize(io_num_.n_output);
    for (std::uint32_t i = 0; i < io_num_.n_output; ++i) {
      output_attrs_[i] = {};
      output_attrs_[i].index = i;
      ret = rknn_query(ctx_, RKNN_QUERY_OUTPUT_ATTR, &output_attrs_[i], sizeof(rknn_tensor_attr));
      if (ret != RKNN_SUCC) {
        std::cerr << "query output failed: " << path << ", ret=" << ret << std::endl;
        return false;
      }
    }
    PrintAttrs(path);
    return true;
  }

  bool Infer(const cv::Mat& rgb_input, std::vector<RknnTensor>* tensors, double* run_ms) {
    if (ctx_ == 0 || tensors == nullptr || rgb_input.empty() || rgb_input.channels() != 3) {
      return false;
    }
    rknn_input input{};
    input.index = 0;
    input.type = RKNN_TENSOR_UINT8;
    input.fmt = input_attr_.fmt;
    input.size = static_cast<std::uint32_t>(rgb_input.total() * rgb_input.elemSize());
    input.buf = const_cast<std::uint8_t*>(rgb_input.data);
    input.pass_through = 0;

    int ret = rknn_inputs_set(ctx_, 1, &input);
    if (ret != RKNN_SUCC) {
      std::cerr << "rknn_inputs_set failed: " << ret << std::endl;
      return false;
    }
    ret = rknn_run(ctx_, nullptr);
    if (ret != RKNN_SUCC) {
      std::cerr << "rknn_run failed: " << ret << std::endl;
      return false;
    }
    rknn_perf_run perf{};
    if (rknn_query(ctx_, RKNN_QUERY_PERF_RUN, &perf, sizeof(perf)) == RKNN_SUCC && perf.run_duration > 0 &&
        run_ms != nullptr) {
      *run_ms = static_cast<double>(perf.run_duration) / 1000.0;
    }

    std::vector<rknn_output> outputs(io_num_.n_output);
    for (rknn_output& output : outputs) {
      std::memset(&output, 0, sizeof(output));
      output.want_float = 1;
      output.is_prealloc = 0;
    }
    ret = rknn_outputs_get(ctx_, io_num_.n_output, outputs.data(), nullptr);
    if (ret != RKNN_SUCC) {
      std::cerr << "rknn_outputs_get failed: " << ret << std::endl;
      return false;
    }

    tensors->clear();
    tensors->resize(outputs.size());
    for (std::size_t i = 0; i < outputs.size(); ++i) {
      const int float_count = static_cast<int>(outputs[i].size / sizeof(float));
      (*tensors)[i].data.assign(static_cast<float*>(outputs[i].buf), static_cast<float*>(outputs[i].buf) + float_count);
      for (std::uint32_t d = 0; d < output_attrs_[i].n_dims; ++d) {
        (*tensors)[i].dims.push_back(static_cast<int>(output_attrs_[i].dims[d]));
      }
    }
    rknn_outputs_release(ctx_, io_num_.n_output, outputs.data());
    return true;
  }

 private:
  static bool ReadFile(const std::string& path, std::vector<std::uint8_t>* data) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input.is_open()) {
      return false;
    }
    const std::streampos size = input.tellg();
    if (size <= 0) {
      return false;
    }
    data->resize(static_cast<std::size_t>(size));
    input.seekg(0, std::ios::beg);
    input.read(reinterpret_cast<char*>(data->data()), size);
    return input.good();
  }

  void PrintAttrs(const std::string& path) const {
    std::cout << "loaded " << path << std::endl;
    std::cout << "  input dims=";
    for (std::uint32_t d = 0; d < input_attr_.n_dims; ++d) {
      std::cout << input_attr_.dims[d] << (d + 1 == input_attr_.n_dims ? "" : "x");
    }
    std::cout << ", fmt=" << get_format_string(input_attr_.fmt) << ", type=" << get_type_string(input_attr_.type)
              << std::endl;
    for (std::size_t i = 0; i < output_attrs_.size(); ++i) {
      std::cout << "  output[" << i << "] dims=";
      for (std::uint32_t d = 0; d < output_attrs_[i].n_dims; ++d) {
        std::cout << output_attrs_[i].dims[d] << (d + 1 == output_attrs_[i].n_dims ? "" : "x");
      }
      std::cout << ", type=" << get_type_string(output_attrs_[i].type) << std::endl;
    }
  }

  rknn_context ctx_ = 0;
  rknn_input_output_num io_num_{};
  rknn_tensor_attr input_attr_{};
  std::vector<rknn_tensor_attr> output_attrs_;
};

static float Sigmoid(const float x) {
  return 1.0F / (1.0F + std::exp(-x));
}

static std::vector<cv::Vec4f> GeneratePalmAnchors(const int input_size) {
  std::vector<cv::Vec4f> anchors;
  anchors.reserve(2016);
  for (const std::pair<int, int>& spec : {std::make_pair(8, 2), std::make_pair(16, 6)}) {
    const int stride = spec.first;
    const int anchors_num = spec.second;
    const int grid_size = input_size / stride;
    for (int y = 0; y < grid_size; ++y) {
      for (int x = 0; x < grid_size; ++x) {
        const float cx = (static_cast<float>(x) + 0.5F) / static_cast<float>(grid_size);
        const float cy = (static_cast<float>(y) + 0.5F) / static_cast<float>(grid_size);
        for (int i = 0; i < anchors_num; ++i) {
          anchors.emplace_back(cx, cy, 1.0F, 1.0F);
        }
      }
    }
  }
  return anchors;
}

static cv::Mat PreprocessBgrToRgbLetterbox(const cv::Mat& bgr, const cv::Size& input_size, LetterboxMeta* meta) {
  cv::Mat rgb;
  cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
  const float scale = std::min(static_cast<float>(input_size.width) / static_cast<float>(bgr.cols),
                               static_cast<float>(input_size.height) / static_cast<float>(bgr.rows));
  const int new_w = std::max(1, static_cast<int>(std::round(static_cast<float>(bgr.cols) * scale)));
  const int new_h = std::max(1, static_cast<int>(std::round(static_cast<float>(bgr.rows) * scale)));
  cv::Mat resized;
  cv::resize(rgb, resized, cv::Size(new_w, new_h));
  const int left = (input_size.width - new_w) / 2;
  const int top = (input_size.height - new_h) / 2;
  cv::Mat padded(input_size, CV_8UC3, cv::Scalar(0, 0, 0));
  resized.copyTo(padded(cv::Rect(left, top, new_w, new_h)));
  if (meta != nullptr) {
    meta->scale = scale;
    meta->pad_left = left;
    meta->pad_top = top;
    meta->input_w = input_size.width;
    meta->input_h = input_size.height;
    meta->src_w = bgr.cols;
    meta->src_h = bgr.rows;
  }
  return padded;
}

static cv::Point2f MapInputToSource(const float x, const float y, const LetterboxMeta& meta) {
  const float src_x = std::clamp((x - static_cast<float>(meta.pad_left)) / meta.scale, 0.0F,
                                 static_cast<float>(meta.src_w - 1));
  const float src_y = std::clamp((y - static_cast<float>(meta.pad_top)) / meta.scale, 0.0F,
                                 static_cast<float>(meta.src_h - 1));
  return cv::Point2f(src_x, src_y);
}

static cv::Rect2f MapNormBoxToSource(const cv::Rect2f& norm_box, const LetterboxMeta& meta) {
  const cv::Point2f p1 = MapInputToSource(norm_box.x * meta.input_w, norm_box.y * meta.input_h, meta);
  const cv::Point2f p2 =
      MapInputToSource((norm_box.x + norm_box.width) * meta.input_w, (norm_box.y + norm_box.height) * meta.input_h, meta);
  return cv::Rect2f(p1.x, p1.y, std::max(1.0F, p2.x - p1.x), std::max(1.0F, p2.y - p1.y));
}

static cv::Rect MakeSquareRoi(const cv::Rect2f& box, const int width, const int height, const float scale) {
  const float cx = box.x + box.width * 0.5F;
  const float cy = box.y + box.height * 0.5F;
  const float side = std::max(box.width, box.height) * scale;
  const int x1 = std::max(0, static_cast<int>(std::floor(cx - side * 0.5F)));
  const int y1 = std::max(0, static_cast<int>(std::floor(cy - side * 0.5F)));
  const int x2 = std::min(width, static_cast<int>(std::ceil(cx + side * 0.5F)));
  const int y2 = std::min(height, static_cast<int>(std::ceil(cy + side * 0.5F)));
  return cv::Rect(x1, y1, std::max(1, x2 - x1), std::max(1, y2 - y1));
}

static float CalculateIou(const cv::Rect2f& lhs, const cv::Rect2f& rhs) {
  const float x1 = std::max(lhs.x, rhs.x);
  const float y1 = std::max(lhs.y, rhs.y);
  const float x2 = std::min(lhs.x + lhs.width, rhs.x + rhs.width);
  const float y2 = std::min(lhs.y + lhs.height, rhs.y + rhs.height);
  const float inter_w = std::max(0.0F, x2 - x1);
  const float inter_h = std::max(0.0F, y2 - y1);
  const float inter = inter_w * inter_h;
  const float area = lhs.width * lhs.height + rhs.width * rhs.height - inter;
  return area > 0.0F ? inter / area : 0.0F;
}

static bool DecodePalms(const std::vector<RknnTensor>& outputs, const LetterboxMeta& meta,
                        const std::vector<cv::Vec4f>& anchors, std::vector<Detection>* detections) {
  if (outputs.size() < 2 || detections == nullptr) {
    return false;
  }
  detections->clear();

  const RknnTensor* regs = &outputs[0];
  const RknnTensor* scores = &outputs[1];
  if (regs->data.size() != anchors.size() * 18U) {
    regs = &outputs[1];
    scores = &outputs[0];
  }
  if (regs->data.size() != anchors.size() * 18U || scores->data.size() != anchors.size()) {
    std::cerr << "unexpected detector output sizes: regs=" << regs->data.size() << ", scores=" << scores->data.size()
              << std::endl;
    return false;
  }

  std::vector<cv::Rect2f> boxes;
  std::vector<float> probs;
  boxes.reserve(64);
  probs.reserve(64);
  constexpr float kScoreThreshold = 0.55F;
  for (std::size_t i = 0; i < anchors.size(); ++i) {
    const float prob = Sigmoid(scores->data[i]);
    if (prob < kScoreThreshold) {
      continue;
    }
    const float* reg = regs->data.data() + i * 18U;
    const float cx = reg[0] / 192.0F + anchors[i][0];
    const float cy = reg[1] / 192.0F + anchors[i][1];
    const float w = reg[2] / 192.0F;
    const float h = reg[3] / 192.0F;
    boxes.emplace_back(cx - w * 0.5F, cy - h * 0.5F, w, h);
    probs.push_back(prob);
  }
  if (boxes.empty()) {
    return false;
  }

  std::vector<int> order(boxes.size());
  for (std::size_t i = 0; i < order.size(); ++i) {
    order[i] = static_cast<int>(i);
  }
  std::sort(order.begin(), order.end(), [&](const int lhs, const int rhs) {
    return probs[lhs] > probs[rhs];
  });
  for (const int index : order) {
    const cv::Rect2f source_box = MapNormBoxToSource(boxes[index], meta);
    bool suppressed = false;
    for (const Detection& kept : *detections) {
      if (CalculateIou(source_box, kept.box) > 0.3F) {
        suppressed = true;
        break;
      }
    }
    if (!suppressed) {
      detections->push_back(Detection{source_box, probs[index]});
      if (static_cast<int>(detections->size()) >= kMaxHands) {
        break;
      }
    }
  }
  return !detections->empty();
}

static bool DecodeLandmarks(const std::vector<RknnTensor>& outputs, const LetterboxMeta& meta, const cv::Rect& roi,
                            std::vector<cv::Point2f>* points) {
  if (points == nullptr) {
    return false;
  }
  const RknnTensor* landmark_tensor = nullptr;
  for (const RknnTensor& tensor : outputs) {
    if (tensor.data.size() == 63U) {
      landmark_tensor = &tensor;
      break;
    }
  }
  if (landmark_tensor == nullptr) {
    return false;
  }

  points->clear();
  points->reserve(21);
  for (int i = 0; i < 21; ++i) {
    float x_in = landmark_tensor->data[static_cast<std::size_t>(i) * 3U];
    float y_in = landmark_tensor->data[static_cast<std::size_t>(i) * 3U + 1U];
    if (std::max(std::abs(x_in), std::abs(y_in)) <= 2.0F) {
      x_in *= static_cast<float>(meta.input_w);
      y_in *= static_cast<float>(meta.input_h);
    }
    const cv::Point2f p_roi = MapInputToSource(x_in, y_in, meta);
    points->emplace_back(static_cast<float>(roi.x) + p_roi.x, static_cast<float>(roi.y) + p_roi.y);
  }
  return true;
}

int main(int argc, char** argv) {
  const std::string video_path = argc > 1 ? argv[1] : "config/demo.mp4";
  const std::string det_model = argc > 2 ? argv[2] : "/tmp/wgh22_rk3588/hand_detector.rknn";
  const std::string lm_model = argc > 3 ? argv[3] : "/tmp/wgh22_rk3588/hand_landmarks.rknn";
  const std::string output_path = argc > 4 ? argv[4] : "output/rknn_hand_pose_test.avi";
  const bool show_window = std::getenv("RKNN_HAND_TEST_NO_SHOW") == nullptr;
  const bool save_video = std::getenv("RKNN_HAND_TEST_SAVE") != nullptr;

  RknnModel detector;
  RknnModel landmarker;
  if (!detector.Load(det_model) || !landmarker.Load(lm_model)) {
    return 1;
  }

  setenv("OPENCV_FFMPEG_CAPTURE_OPTIONS", "video_codec;hevc", 1);
  cv::VideoCapture cap;
  if (!cap.open(video_path, cv::CAP_FFMPEG)) {
    std::cerr << "open video failed: " << video_path << std::endl;
    return 1;
  }
  std::cout << "video backend=" << cap.getBackendName() << ", fps=" << cap.get(cv::CAP_PROP_FPS)
            << ", convert_rgb=" << cap.get(cv::CAP_PROP_CONVERT_RGB) << std::endl;

  cv::Mat frame;
  if (!cap.read(frame) || frame.empty()) {
    std::cerr << "read first frame failed" << std::endl;
    return 1;
  }
  cap.set(cv::CAP_PROP_POS_FRAMES, 0);

  const double fps = cap.get(cv::CAP_PROP_FPS) > 0.0 ? cap.get(cv::CAP_PROP_FPS) : 30.0;
  cv::VideoWriter writer;
  if (save_video) {
    writer.open(output_path, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), fps, frame.size());
    if (!writer.isOpened()) {
      std::cerr << "open writer failed: " << output_path << std::endl;
      return 1;
    }
  }

  const std::vector<cv::Vec4f> anchors = GeneratePalmAnchors(192);
  int frame_id = 0;
  int hand_frames = 0;
  double total_wall_ms = 0.0;
  double total_read_ms = 0.0;
  double total_loop_ms = 0.0;
  double total_det_ms = 0.0;
  double total_lm_ms = 0.0;

  while (true) {
    const auto loop_begin = std::chrono::steady_clock::now();
    const auto read_begin = std::chrono::steady_clock::now();
    if (!cap.read(frame) || frame.empty()) {
      break;
    }
    const auto read_end = std::chrono::steady_clock::now();
    const double read_ms = std::chrono::duration<double, std::milli>(read_end - read_begin).count();
    ++frame_id;
    const auto begin = std::chrono::steady_clock::now();

    LetterboxMeta det_meta;
    cv::Mat det_input = PreprocessBgrToRgbLetterbox(frame, cv::Size(192, 192), &det_meta);
    std::vector<RknnTensor> det_outputs;
    double det_ms = 0.0;
    std::vector<Detection> detections;
    const bool det_ok = detector.Infer(det_input, &det_outputs, &det_ms) &&
                        DecodePalms(det_outputs, det_meta, anchors, &detections);

    std::vector<std::vector<cv::Point2f>> all_landmarks;
    std::vector<cv::Rect> rois;
    double lm_ms = 0.0;
    if (det_ok) {
      for (const Detection& detection : detections) {
        const cv::Rect roi = MakeSquareRoi(detection.box, frame.cols, frame.rows, 2.0F);
        cv::Mat roi_bgr = frame(roi).clone();
        LetterboxMeta lm_meta;
        cv::Mat lm_input = PreprocessBgrToRgbLetterbox(roi_bgr, cv::Size(224, 224), &lm_meta);
        std::vector<RknnTensor> lm_outputs;
        std::vector<cv::Point2f> landmarks;
        double single_lm_ms = 0.0;
        if (landmarker.Infer(lm_input, &lm_outputs, &single_lm_ms) &&
            DecodeLandmarks(lm_outputs, lm_meta, roi, &landmarks)) {
          lm_ms += single_lm_ms;
          rois.push_back(roi);
          all_landmarks.push_back(std::move(landmarks));
        }
      }
      if (!all_landmarks.empty()) {
        ++hand_frames;
      }
    }

    const auto end = std::chrono::steady_clock::now();
    const double wall_ms = std::chrono::duration<double, std::milli>(end - begin).count();
    total_wall_ms += wall_ms;
    total_det_ms += det_ms;
    total_lm_ms += lm_ms;

    if (det_ok) {
      for (std::size_t i = 0; i < detections.size(); ++i) {
        const cv::Scalar box_color = i == 0U ? cv::Scalar(0, 128, 255) : cv::Scalar(255, 0, 255);
        if (i < rois.size()) {
          cv::rectangle(frame, rois[i], box_color, 2);
        }
        cv::rectangle(frame, detections[i].box, cv::Scalar(0, 255, 255), 2);
      }
      cv::putText(frame, "hands " + std::to_string(all_landmarks.size()), cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX,
                  0.8, cv::Scalar(0, 255, 255), 2);
    }
    static const std::vector<std::pair<int, int>> kHandConnections = {
        {0, 1},   {1, 2},   {2, 3},   {3, 4},
        {0, 5},   {5, 6},   {6, 7},   {7, 8},
        {0, 9},   {9, 10},  {10, 11}, {11, 12},
        {0, 13},  {13, 14}, {14, 15}, {15, 16},
        {0, 17},  {17, 18}, {18, 19}, {19, 20},
        {5, 9},   {9, 13},  {13, 17},
    };
    for (std::size_t hand_index = 0; hand_index < all_landmarks.size(); ++hand_index) {
      const std::vector<cv::Point2f>& landmarks = all_landmarks[hand_index];
      const cv::Scalar line_color = hand_index == 0U ? cv::Scalar(255, 255, 255) : cv::Scalar(255, 255, 0);
      for (const std::pair<int, int>& connection : kHandConnections) {
        if (connection.first < static_cast<int>(landmarks.size()) &&
            connection.second < static_cast<int>(landmarks.size())) {
          cv::line(frame, landmarks[connection.first], landmarks[connection.second], line_color, 2);
        }
      }
      for (std::size_t i = 0; i < landmarks.size(); ++i) {
        const cv::Scalar color = i == 0U ? cv::Scalar(0, 0, 255) : cv::Scalar(0, 255, 0);
        const int radius = i == 0U ? 5 : 4;
        cv::circle(frame, landmarks[i], radius, color, -1);
        cv::circle(frame, landmarks[i], radius, cv::Scalar(255, 255, 255), 1);
      }
    }
    cv::putText(frame, "wall_ms=" + std::to_string(wall_ms) + " det=" + std::to_string(det_ms) +
                           " lm=" + std::to_string(lm_ms),
                cv::Point(10, 65), cv::FONT_HERSHEY_SIMPLEX, 0.55, cv::Scalar(0, 255, 0), 2);
    cv::putText(frame, "read_ms=" + std::to_string(read_ms), cv::Point(10, 95), cv::FONT_HERSHEY_SIMPLEX, 0.55,
                cv::Scalar(0, 255, 0), 2);
    const auto show_begin = std::chrono::steady_clock::now();
    if (show_window) {
      cv::imshow("RKNN Hand Pose Test", frame);
    }
    const auto show_end = std::chrono::steady_clock::now();
    const double show_ms = std::chrono::duration<double, std::milli>(show_end - show_begin).count();

    const auto write_begin = std::chrono::steady_clock::now();
    if (save_video && writer.isOpened()) {
      writer.write(frame);
    }
    const auto write_end = std::chrono::steady_clock::now();
    const double write_ms = std::chrono::duration<double, std::milli>(write_end - write_begin).count();

    const auto before_wait = std::chrono::steady_clock::now();
    const double loop_no_wait_ms = std::chrono::duration<double, std::milli>(before_wait - loop_begin).count();
    const int target_frame_ms = fps > 0.0 ? std::max(1, static_cast<int>(1000.0 / fps)) : 1;
    const int delay_ms = std::max(1, target_frame_ms - static_cast<int>(std::round(loop_no_wait_ms)));
    if (show_window) {
      if ((cv::waitKey(delay_ms) & 0xFF) == 27) {
        break;
      }
    }

    total_read_ms += read_ms;
    total_loop_ms += loop_no_wait_ms;
    if (frame_id % 30 == 0) {
      std::cout << "frame=" << frame_id << ", hand_frames=" << hand_frames << ", wall_ms=" << wall_ms
                << ", read_ms=" << read_ms << ", loop_no_wait_ms=" << loop_no_wait_ms << ", wait_ms=" << delay_ms
                << ", show_ms=" << show_ms << ", write_ms=" << write_ms << ", det_ms=" << det_ms
                << ", lm_ms=" << lm_ms << std::endl;
    }
  }

  if (frame_id > 0) {
    std::cout << "done frames=" << frame_id << ", hand_frames=" << hand_frames
              << ", avg_wall_ms=" << total_wall_ms / frame_id << ", avg_read_ms=" << total_read_ms / frame_id
              << ", avg_loop_no_wait_ms=" << total_loop_ms / frame_id << ", avg_det_ms=" << total_det_ms / frame_id
              << ", avg_lm_ms=" << total_lm_ms / frame_id
              << ", output=" << (save_video ? output_path : "disabled") << std::endl;
  }
  return 0;
}
