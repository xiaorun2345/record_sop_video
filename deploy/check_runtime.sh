#!/usr/bin/env bash

set -u

APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export LD_LIBRARY_PATH="${APP_DIR}/output/lib:${LD_LIBRARY_PATH:-}"
STATUS=0

check_cmd() {
  local command="$1"
  if ! command -v "${command}" >/dev/null 2>&1; then
    echo "缺少命令: ${command}"
    STATUS=1
  fi
}

check_file() {
  local file="$1"
  if [[ ! -e "${file}" ]]; then
    echo "缺少文件/设备: ${file}"
    STATUS=1
  fi
}

check_cmd gst-inspect-1.0
check_cmd ldd

check_file "${APP_DIR}/output/rk3588_sop"
check_file "${APP_DIR}/standalone_camera/bin/orbbec_1080p"
check_file "${APP_DIR}/output/lib/librknnrt.so"
check_file "${APP_DIR}/output/lib/libOrbbecSDK.so.1.10"

if command -v gst-inspect-1.0 >/dev/null 2>&1; then
  if ! gst-inspect-1.0 mpph264enc >/dev/null 2>&1 &&
      ! gst-inspect-1.0 | grep -q 'mpph264enc'; then
    echo "缺少硬件编码插件: mpph264enc"
    STATUS=1
  fi
fi

if command -v ldd >/dev/null 2>&1; then
  if ldd "${APP_DIR}/output/rk3588_sop" | grep -E 'not found|GLIBC_' >/dev/null; then
    echo "主程序动态库依赖异常:"
    ldd "${APP_DIR}/output/rk3588_sop" | grep -E 'not found|GLIBC_'
    STATUS=1
  fi
  if ldd "${APP_DIR}/standalone_camera/bin/orbbec_1080p" | grep -E 'not found|GLIBC_' >/dev/null; then
    echo "录像程序动态库依赖异常:"
    ldd "${APP_DIR}/standalone_camera/bin/orbbec_1080p" | grep -E 'not found|GLIBC_'
    STATUS=1
  fi
fi

if [[ "${STATUS}" == "0" ]]; then
  echo "运行环境检查通过。"
else
  echo "运行环境检查未通过。请先补齐上面列出的系统依赖或权限。"
fi

exit "${STATUS}"
