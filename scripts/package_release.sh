#!/usr/bin/env bash

set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST_DIR="${PROJECT_DIR}/dist"
PACKAGE_NAME="${PACKAGE_NAME:-rk3588_sop_runtime}"
PACKAGE_ROOT="${DIST_DIR}/${PACKAGE_NAME}"
ARCHIVE_PATH="${DIST_DIR}/${PACKAGE_NAME}.tar.gz"

require_file() {
  local file="$1"
  if [[ ! -f "${file}" ]]; then
    echo "缺少文件: ${file}" >&2
    exit 1
  fi
}

copy_dir() {
  local src="$1"
  local dst="$2"
  mkdir -p "$(dirname "${dst}")"
  cp -a "${src}" "${dst}"
}

require_file "${PROJECT_DIR}/output/rk3588_sop"
require_file "${PROJECT_DIR}/standalone_camera/bin/orbbec_1080p"
require_file "${PROJECT_DIR}/models/yolov8.rknn"
require_file "${PROJECT_DIR}/models/hand_detector.rknn"
require_file "${PROJECT_DIR}/models/hand_landmarks.rknn"
require_file "${PROJECT_DIR}/config/sop_config.txt"
require_file "${PROJECT_DIR}/third_party/orbbec_sdk/config/99-obsensor-libusb.rules"

rm -rf "${PACKAGE_ROOT}" "${ARCHIVE_PATH}"
mkdir -p "${PACKAGE_ROOT}"

copy_dir "${PROJECT_DIR}/config" "${PACKAGE_ROOT}/config"
copy_dir "${PROJECT_DIR}/models" "${PACKAGE_ROOT}/models"
mkdir -p "${PACKAGE_ROOT}/output"
cp -a "${PROJECT_DIR}/output/rk3588_sop" "${PACKAGE_ROOT}/output/"
copy_dir "${PROJECT_DIR}/output/lib" "${PACKAGE_ROOT}/output/lib"
copy_dir "${PROJECT_DIR}/standalone_camera/bin" "${PACKAGE_ROOT}/standalone_camera/bin"
copy_dir "${PROJECT_DIR}/third_party/orbbec_sdk/config" "${PACKAGE_ROOT}/third_party/orbbec_sdk/config"
cp -a "${PROJECT_DIR}/deploy/." "${PACKAGE_ROOT}/"

mkdir -p "${PACKAGE_ROOT}/recordings"

find "${PACKAGE_ROOT}" -type f -name '*.sh' -exec chmod +x {} +
chmod +x "${PACKAGE_ROOT}/output/rk3588_sop"
chmod +x "${PACKAGE_ROOT}/standalone_camera/bin/orbbec_1080p"

tar -C "${DIST_DIR}" -czf "${ARCHIVE_PATH}" "${PACKAGE_NAME}"

echo "部署包已生成: ${ARCHIVE_PATH}"
du -h "${ARCHIVE_PATH}"
