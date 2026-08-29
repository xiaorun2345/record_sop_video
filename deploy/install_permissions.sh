#!/usr/bin/env bash

set -euo pipefail

APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RULE_FILE="${APP_DIR}/third_party/orbbec_sdk/config/99-obsensor-libusb.rules"

if [[ ! -f "${RULE_FILE}" ]]; then
  echo "缺少 Orbbec udev 规则: ${RULE_FILE}" >&2
  exit 1
fi

sudo cp -f "${RULE_FILE}" /etc/udev/rules.d/99-obsensor-libusb.rules
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=usb --attr-match=idVendor=2bc5 || true

if getent group video >/dev/null; then
  sudo usermod -aG video "${USER}" || true
fi
if getent group render >/dev/null; then
  sudo usermod -aG render "${USER}" || true
fi

echo "权限规则已安装。建议拔插一次 Orbbec 相机；如刚加入 video/render 组，需要重新登录后生效。"
