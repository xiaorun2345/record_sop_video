/*
 * name: xiaorun
 * email: 15610499173@163.com
 */

#ifndef TOOLCHAINS_RK3588_EXAMPLES_RK3588_SOP_INCLUDE_GEOMETRY_UTILS_H_
#define TOOLCHAINS_RK3588_EXAMPLES_RK3588_SOP_INCLUDE_GEOMETRY_UTILS_H_

#include <string>
#include <vector>

#include "sop_types.h"

/**
 * @brief 获取检测框中心点。
 *
 * 这是 SOP 里最常用的采样点：物体取 bbox 中心，手取 wrist 点。
 */
ImagePoint GetBoxCenter(const BoundingBox& box);

/**
 * @brief 判断点是否在 ROI 内。
 *
 * 使用多边形而不是矩形，原因是工位 ROI 往往不是标准矩形。
 */
bool IsPointInRoi(const ImagePoint& point, const RoiRegion& roi);

/**
 * @brief 按名称查找 ROI。
 *
 * 返回指针而不是拷贝，避免频繁复制点集。
 */
const RoiRegion* FindRoiByName(const std::vector<RoiRegion>& rois, const std::string& name);

/**
 * @brief 判断检测目标是否存在。
 */
bool HasObjectLabel(const std::vector<ObjectDetection>& objects, const std::string& label);

/**
 * @brief 统计指定类别的检测目标数量。
 */
int CountObjectLabel(const std::vector<ObjectDetection>& objects, const std::string& label);

/**
 * @brief 判断手部腕点是否在 ROI 内。
 */
bool HasHandInRoi(const std::vector<HandPose>& hands, const RoiRegion& roi, int image_width, int image_height);

#endif  // TOOLCHAINS_RK3588_EXAMPLES_RK3588_SOP_INCLUDE_GEOMETRY_UTILS_H_
