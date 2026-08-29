/*
 * name: xiaorun
 * email: 15610499173@163.com
 */

#ifndef TOOLCHAINS_RK3588_EXAMPLES_RK3588_SOP_INCLUDE_VIDEO_SOURCE_H_
#define TOOLCHAINS_RK3588_EXAMPLES_RK3588_SOP_INCLUDE_VIDEO_SOURCE_H_

#include <memory>

#include "sop_types.h"

/**
 * @brief 视频输入源，支持视频文件、单目摄像头、GStreamer 和奥比中光接口。
 */
class VideoSource {
 public:
  using Ptr = std::shared_ptr<VideoSource>;

  /**
   * @brief 构造视频输入源。
   * @param config 输入配置。
   */
  explicit VideoSource(const VideoInputConfig& config);

  /**
   * @brief 打开输入源。
   * @return 是否打开成功。
   */
  bool Open();

  /**
   * @brief 读取一帧彩色图像。
   * @param frame 输出图像帧。
   * @return 是否读取成功。
   */
  bool Read(ImageFrame* frame);

  /**
   * @brief 读取一帧 RGB-D 图像。
   * @param frame 输出 RGB-D 帧。
   * @return 是否读取成功。
   */
  bool ReadRgbd(RgbdFrame* frame);

  double last_capture_read_ms() const {
    return last_capture_read_ms_;
  }

  double last_color_convert_ms() const {
    return last_color_convert_ms_;
  }

  double last_frame_copy_ms() const {
    return last_frame_copy_ms_;
  }

  /**
   * @brief 查询 RGB 像素对应的三维点。
   * @param frame RGB-D 帧。
   * @param pixel_x RGB 图像横坐标。
   * @param pixel_y RGB 图像纵坐标。
   * @param point 输出三维点。
   * @return 是否查询成功。
   */
  bool QueryPoint3D(const RgbdFrame& frame, int pixel_x, int pixel_y, Point3D* point) const;

  /**
   * @brief 在指定最大搜索半径内查询 RGB 像素对应的三维点。
   *
   * 手指关键点使用较小半径，避免在指尖深度空洞处误取远处背景。
   */
  bool QueryPoint3D(const RgbdFrame& frame, int pixel_x, int pixel_y,
                    int max_search_radius, Point3D* point) const;

  /**
   * @brief 把彩色相机坐标系下的三维点投影回彩色图像素。
   */
  bool ProjectPointToColor(const RgbdFrame& frame, const Point3D& point, ImagePoint* pixel) const;

  /**
   * @brief 释放输入源。
   */
  void Close();

 private:
  /**
   * @brief 从已对齐深度图读取深度值。
   */
  bool QueryAlignedDepthPoint(const RgbdFrame& frame, int pixel_x, int pixel_y,
                              int max_search_radius, Point3D* point) const;

  VideoInputConfig config_;
  std::int64_t frame_id_ = 0;
  double last_capture_read_ms_ = 0.0;
  double last_color_convert_ms_ = 0.0;
  double last_frame_copy_ms_ = 0.0;
  bool logged_rgbd_profile_ = false;

#if RK3588_SOP_HAS_OPENCV || RK3588_SOP_ENABLE_ORBBEC
  class Impl;
  std::shared_ptr<Impl> impl_;
#endif
};

#endif  // TOOLCHAINS_RK3588_EXAMPLES_RK3588_SOP_INCLUDE_VIDEO_SOURCE_H_
