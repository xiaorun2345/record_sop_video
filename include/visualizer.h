/*
 * name: xiaorun
 * email: 15610499173@163.com
 */

#ifndef TOOLCHAINS_RK3588_EXAMPLES_RK3588_SOP_INCLUDE_VISUALIZER_H_
#define TOOLCHAINS_RK3588_EXAMPLES_RK3588_SOP_INCLUDE_VISUALIZER_H_

#include "sop_state_machine.h"
#include "sop_types.h"

/**
 * @brief SOP 可视化输出。
 *
 * 这个类只负责把感知结果画回图像，不参与业务判断。
 * 这样 UI 层和规则层分离，便于后续在没有窗口的 RK3588 上只保留日志输出。
 */
class Visualizer {
 public:
  /**
   * @brief 绘制感知结果和 SOP 状态。
   * @param frame 图像帧。
   * @param result 感知结果。
   * @param state_machine SOP 状态机。
   */
  void Draw(ImageFrame* frame, const PerceptionResult& result, const SopStateMachine& state_machine) const;
};

#endif  // TOOLCHAINS_RK3588_EXAMPLES_RK3588_SOP_INCLUDE_VISUALIZER_H_
