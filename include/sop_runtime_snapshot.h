#ifndef TOOLCHAINS_RK3588_EXAMPLES_RK3588_SOP_INCLUDE_SOP_RUNTIME_SNAPSHOT_H_
#define TOOLCHAINS_RK3588_EXAMPLES_RK3588_SOP_INCLUDE_SOP_RUNTIME_SNAPSHOT_H_

#include <string>

#include "sop_state_machine.h"

/** 将最近一帧 SOP 判定结果原子写入 Web 后端可读取的快照文件。 */
bool WriteSopRuntimeSnapshot(const std::string& path, const SopRuntimeReport& report,
                             const PerceptionResult& perception);

#endif
