#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export RK3588_SOP_RTMP_URL="${RK3588_SOP_RTMP_URL:-rtmp://127.0.0.1:1935/sop}"
export RK3588_SOP_NO_WINDOW="${RK3588_SOP_NO_WINDOW:-1}"
exec "$ROOT/output/rk3588_sop" "$ROOT/config/sop_config.txt"
