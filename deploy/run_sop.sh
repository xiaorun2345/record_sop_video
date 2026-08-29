#!/usr/bin/env bash

set -euo pipefail

APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export LD_LIBRARY_PATH="${APP_DIR}/output/lib:${LD_LIBRARY_PATH:-}"

exec "${APP_DIR}/output/rk3588_sop" "${APP_DIR}/config/sop_config.txt"
