#!/usr/bin/env bash

# name: xiaorun
# email: 15610499173@163.com

set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-/tmp/rk3588_sop_build}"
OUTPUT_DIR="${PROJECT_DIR}/output"
OUTPUT_BIN="${OUTPUT_DIR}/rk3588_sop"
BUILD_TYPE="${BUILD_TYPE:-Release}"
RKNN_ROOT="${RKNN_ROOT:-/usr}"
ENABLE_ORBBEC="${ENABLE_ORBBEC:-AUTO}"
ORBBEC_SDK_ROOT="${ORBBEC_SDK_ROOT:-}"

usage() {
  cat <<EOF
Usage: scripts/build.sh [--debug] [--orbbec] [--clean]

Environment:
  BUILD_DIR         Internal CMake build directory, default: /tmp/rk3588_sop_build
  RKNN_ROOT         RKNN runtime root, default: /usr
  ENABLE_ORBBEC     ON/OFF/AUTO, default: AUTO
  ORBBEC_SDK_ROOT   Orbbec SDK path, optional when SDK is in third_party/orbbec_sdk or /opt/orbbec
EOF
}

find_orbbec_sdk_root() {
  local candidate
  for candidate in \
    "${PROJECT_DIR}/third_party/orbbec_sdk" \
    "${PROJECT_DIR}/third_party/OrbbecSDK" \
    /opt/orbbec/OrbbecSDK_v*/SDK \
    /opt/orbbec/OrbbecSDK_v* \
    /usr/local/orbbec/OrbbecSDK_v*/SDK \
    /usr/local/orbbec/OrbbecSDK_v* \
    "${HOME}/Desktop"/OrbbecSDK_v*/SDK \
    "${HOME}/桌面"/OrbbecSDK_v*/SDK; do
    [[ -e "${candidate}" ]] || continue
    if [[ -f "${candidate}/include/libobsensor/ObSensor.hpp" && -f "${candidate}/lib/libOrbbecSDK.so" ]]; then
      printf '%s\n' "${candidate}"
      return 0
    fi
    if [[ -f "${candidate}/SDK/include/libobsensor/ObSensor.hpp" && -f "${candidate}/SDK/lib/libOrbbecSDK.so" ]]; then
      printf '%s\n' "${candidate}"
      return 0
    fi
  done
  return 1
}

CLEAN=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --debug)
      BUILD_TYPE="Debug"
      ;;
    --orbbec)
      ENABLE_ORBBEC="ON"
      ;;
    --clean)
      CLEAN=1
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "未知参数: $1" >&2
      usage
      exit 1
      ;;
  esac
  shift
done

if [[ "${CLEAN}" == "1" ]]; then
  rm -rf "${BUILD_DIR}"
fi

if [[ -z "${ORBBEC_SDK_ROOT}" ]]; then
  ORBBEC_SDK_ROOT="$(find_orbbec_sdk_root || true)"
fi

if [[ "${ENABLE_ORBBEC}" == "AUTO" ]]; then
  if [[ -n "${ORBBEC_SDK_ROOT}" ]]; then
    ENABLE_ORBBEC="ON"
  else
    ENABLE_ORBBEC="OFF"
  fi
fi

CMAKE_ARGS=(
  -S "${PROJECT_DIR}"
  -B "${BUILD_DIR}"
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
  -DENABLE_RKNN=ON
  -DRKNN_ROOT="${RKNN_ROOT}"
  -DENABLE_ORBBEC="${ENABLE_ORBBEC}"
)

if [[ "${ENABLE_ORBBEC}" == "ON" && -n "${ORBBEC_SDK_ROOT}" ]]; then
  CMAKE_ARGS+=(-DORBBEC_SDK_ROOT="${ORBBEC_SDK_ROOT}")
fi

cmake "${CMAKE_ARGS[@]}"
cmake --build "${BUILD_DIR}" -j"$(nproc)"

mkdir -p "${OUTPUT_DIR}"
cp -f "${BUILD_DIR}/rk3588_sop" "${OUTPUT_BIN}"
chmod +x "${OUTPUT_BIN}"

echo "输出程序: ${OUTPUT_BIN}"
echo "默认配置: ${PROJECT_DIR}/config/sop_config.txt"
echo "奥比中光 SDK: ${ENABLE_ORBBEC}${ORBBEC_SDK_ROOT:+ (${ORBBEC_SDK_ROOT})}"
