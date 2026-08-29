#!/usr/bin/env bash

set -euo pipefail

APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export LD_LIBRARY_PATH="${APP_DIR}/output/lib:${LD_LIBRARY_PATH:-}"

cd "${APP_DIR}"
exec "${APP_DIR}/standalone_camera/bin/orbbec_1080p" "$@"
