#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT/web/mediamtx/mediamtx"
CFG="$ROOT/web/mediamtx/mediamtx.yml"
if [[ ! -x "$BIN" ]]; then echo "MediaMTX 尚未编译: $BIN" >&2; exit 1; fi
exec "$BIN" "$CFG"
