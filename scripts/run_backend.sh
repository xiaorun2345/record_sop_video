#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UVICORN_LOG_ARGS=()
if [[ "${SOP_ACCESS_LOG:-0}" == "1" || "${SOP_ACCESS_LOG:-0}" == "true" ]]; then
  UVICORN_LOG_ARGS+=(--access-log)
else
  UVICORN_LOG_ARGS+=(--no-access-log)
fi
exec python3 -m uvicorn web.backend.api_server:app --host "${SOP_API_HOST:-0.0.0.0}" --port "${SOP_API_PORT:-8080}" "${UVICORN_LOG_ARGS[@]}"
