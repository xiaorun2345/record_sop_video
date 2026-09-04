#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec python3 -m uvicorn web.backend.api_server:app --host "${SOP_API_HOST:-0.0.0.0}" --port "${SOP_API_PORT:-8080}"
