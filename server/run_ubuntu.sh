#!/usr/bin/env bash
set -Eeuo pipefail

cd "$(dirname "$0")"

if [[ ! -x ".venv/bin/python" ]]; then
  echo "Virtual environment not found. Run deploy_ubuntu_26_04.sh first." >&2
  exit 1
fi

set -a
if [[ -f ".env" ]]; then
  # shellcheck disable=SC1091
  source ".env"
fi
set +a

exec .venv/bin/python -m uvicorn app.main:app \
  --host "${SERVER_HOST:-0.0.0.0}" \
  --port "${SERVER_PORT:-8080}"
