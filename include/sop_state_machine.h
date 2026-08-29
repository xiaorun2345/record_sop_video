/*
 * name: xiaorun
 * email: 15610499173@163.com
 */

#ifndef TOOLCHAINS_RK3588_EXAMPLES_RK3588_SOP_INCLUDE_SOP_STATE_MACHINE_H_
#define TOOLCHAINS_RK3588_EXAMPLES_RK3588_SOP_INCLUDE_SOP_STATE_MACHINE_H_

#include <memory>
#include <vector>

#include "sop_types.h"

/**
 * @brief SOP 状态机，用于判断安装顺序和预警。
 *
 * 这是工程里最核心的“规则层”：
 *   - 上层把每帧感知结果喂给它。
 *   - 它只做状态推进、超时检查、空间约束检查。
 *
 * 不把规则散落到 main() 里，是为了让流程变更时只改这一处。
 */
class SopStateMachine {
 public:
  using Ptr = std::shared_ptr<SopStateMachine>;

  /**
   * @brief 构造 SOP 状态机。
   * @param steps SOP 步骤配置。
   */
  explicit SopStateMachine(const std::vector<SopStepConfig>& steps);

  /**
   * @brief 重置 SOP 状态。
   * @param timestamp_sec 当前时间戳。
   */
  void Reset(double timestamp_sec);

  /**
   * @brief 根据单帧感知结果更新 SOP。
   * @param result 感知结果。
   * @return 当前 SOP 状态。
   */
  const SopRuntimeState& Update(const PerceptionResult& result);

  /**
   * @brief 获取当前步骤配置。
   * @return 当前步骤配置，完成后返回空指针。
   */
  const SopStepConfig* CurrentStep() const;

  /**
   * @brief 获取全部步骤配置。
   * @return 步骤配置列表。
   */
  const std::vector<SopStepConfig>& steps() const;

  /**
   * @brief 获取当前状态。
   * @return SOP 状态。
   */
  const SopRuntimeState& state() const;

 private:
  /**
   * @brief 判断当前步骤是否满足。
   *
   * 这里按“必须同时满足的条件”逐条判断，比把所有逻辑堆成一行更容易排查。
   */
  bool IsCurrentStepSatisfied(const SopStepConfig& step, const PerceptionResult& result) const;

  /**
   * @brief 生成超时预警。
   */
  void UpdateTimeoutAlert(const SopStepConfig& step, double timestamp_sec);

  /**
   * @brief 生成 3D 空间预警。
   */
  void UpdateSpatialAlert(const SopStepConfig& step, const PerceptionResult& result);

  /**
   * @brief 记录当前步骤下每个必检物体出现过的最大数量。
   */
  void UpdateRequiredObjectHistory(const SopStepConfig& step, const PerceptionResult& result);

  std::vector<SopStepConfig> steps_;
  SopRuntimeState state_;
};

#endif  // TOOLCHAINS_RK3588_EXAMPLES_RK3588_SOP_INCLUDE_SOP_STATE_MACHINE_H_
