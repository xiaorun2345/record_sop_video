/*
 * name: xiaorun
 * email: 15610499173@163.com
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

struct LetterboxInfo {
  float scale = 1.0F;
  int pad_x = 0;
  int pad_y = 0;
  int resized_width = 0;
  int resized_height = 0;
  int input_size = 640;
};

static LetterboxInfo ComputeLetterbox(const int width, const int height, const int input_size) {
  LetterboxInfo info;
  info.input_size = input_size;
  info.scale = std::min(static_cast<float>(input_size) / static_cast<float>(width),
                        static_cast<float>(input_size) / static_cast<float>(height));
  info.resized_width = std::max(1, static_cast<int>(std::round(static_cast<float>(width) * info.scale)));
  info.resized_height = std::max(1, static_cast<int>(std::round(static_cast<float>(height) * info.scale)));
  info.pad_x = (input_size - info.resized_width) / 2;
  info.pad_y = (input_size - info.resized_height) / 2;
  return info;
}

int main(int argc, char** argv) {
  const int width = argc > 1 ? std::stoi(argv[1]) : 1280;
  const int height = argc > 2 ? std::stoi(argv[2]) : 720;
  const int input_size = argc > 3 ? std::stoi(argv[3]) : 640;
  if (width <= 0 || height <= 0 || input_size <= 0) {
    std::cerr << "Usage: yolov8_preprocess_test [width height input_size]" << std::endl;
    return 1;
  }

  const LetterboxInfo info = ComputeLetterbox(width, height, input_size);
  const std::size_t input_bytes = static_cast<std::size_t>(input_size) * static_cast<std::size_t>(input_size) * 3U;
  std::vector<std::uint8_t> input(input_bytes, 114U);

  std::cout << "YOLOv8 preprocess test" << std::endl;
  std::cout << "  source: " << width << "x" << height << std::endl;
  std::cout << "  input: " << input_size << "x" << input_size << std::endl;
  std::cout << "  resized: " << info.resized_width << "x" << info.resized_height << std::endl;
  std::cout << "  scale: " << info.scale << std::endl;
  std::cout << "  pad: " << info.pad_x << "," << info.pad_y << std::endl;
  std::cout << "  fill: 114" << std::endl;
  std::cout << "  buffer bytes: " << input.size() << std::endl;
  return 0;
}
