/*
 * name: xiaorun
 * email: 15610499173@163.com
 */

#include "time_utils.h"

#include <chrono>

double NowInSeconds() {
  const std::chrono::steady_clock::duration now = std::chrono::steady_clock::now().time_since_epoch();
  return std::chrono::duration<double>(now).count();
}
