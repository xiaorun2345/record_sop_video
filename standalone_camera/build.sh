#!/usr/bin/env bash

set -euo pipefail

CAMERA_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${CAMERA_DIR}/.." && pwd)"
ORBBEC_SDK_ROOT="${ORBBEC_SDK_ROOT:-${PROJECT_DIR}/third_party/orbbec_sdk}"
SOURCE_FILE="${CAMERA_DIR}/orbbec_1080p.cpp"
OUTPUT_DIR="${CAMERA_DIR}/bin"
OUTPUT_FILE="${OUTPUT_DIR}/orbbec_1080p"

if [[ ! -f "${ORBBEC_SDK_ROOT}/include/libobsensor/ObSensor.hpp" ||
      ! -f "${ORBBEC_SDK_ROOT}/lib/libOrbbecSDK.so" ]]; then
  echo "未找到 Orbbec SDK: ${ORBBEC_SDK_ROOT}" >&2
  echo "可通过 ORBBEC_SDK_ROOT=/path/to/SDK 指定 SDK 路径" >&2
  exit 1
fi

if ! pkg-config --exists opencv4 gstreamer-1.0 gstreamer-app-1.0; then
  echo "未找到 OpenCV 4 或 GStreamer appsrc 的 pkg-config 配置" >&2
  exit 1
fi

mkdir -p "${OUTPUT_DIR}"

g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic \
  "${SOURCE_FILE}" \
  -I"${ORBBEC_SDK_ROOT}/include" \
  $(pkg-config --cflags --libs opencv4 gstreamer-1.0 gstreamer-app-1.0) \
  -L"${ORBBEC_SDK_ROOT}/lib" \
  -Wl,-rpath,"${ORBBEC_SDK_ROOT}/lib" \
  -lOrbbecSDK \
  -pthread \
  -o "${OUTPUT_FILE}"

echo "编译完成: ${OUTPUT_FILE}"
