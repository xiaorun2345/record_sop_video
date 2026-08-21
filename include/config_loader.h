/*
 * name: xiaorun
 * email: 15610499173@163.com
 */

#ifndef TOOLCHAINS_RK3588_EXAMPLES_RK3588_SOP_INCLUDE_CONFIG_LOADER_H_
#define TOOLCHAINS_RK3588_EXAMPLES_RK3588_SOP_INCLUDE_CONFIG_LOADER_H_

#include <string>

#include "sop_types.h"

/**
 * @brief SOP 配置加载器。
 */
class ConfigLoader {
 public:
  /**
   * @brief 从文本配置加载应用配置。
   * @param file_path 配置文件路径。
   * @param config 输出配置。
   * @return 加载是否成功。
   */
  bool Load(const std::string& file_path, SopAppConfig* config) const;
};

#endif  // TOOLCHAINS_RK3588_EXAMPLES_RK3588_SOP_INCLUDE_CONFIG_LOADER_H_
