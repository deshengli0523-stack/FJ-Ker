#!/usr/bin/env bash
set -Eeuo pipefail

INSTALL_ROOT="/opt/fj-ker"
PORT="8080"
BIND_HOST="127.0.0.1"
PUBLIC_BASE_URL=""
API_TOKEN=""
DASHSCOPE_API_KEY=""
QWEN_MODEL="qwen3.7-plus"
MAX_PAGES="20"
MAX_UPLOAD_BYTES="2000000"
SERVICE_USER="fjker"
OPEN_FIREWALL="0"
ACCEPT_PUBLIC_HTTP_RISK="0"
SKIP_PLAYWRIGHT_BROWSER_INSTALL="0"
SERVICE_NAME="fj-ker"
MATHJAX_URL="https://cdn.jsdelivr.net/npm/mathjax@3/es5/tex-svg.js"

usage() {
  cat <<'EOF'
Usage:
  sudo bash server/deploy_ubuntu_26_04.sh --dashscope-api-key KEY [options]

Required:
  --dashscope-api-key KEY

Options:
  --api-token TOKEN                         Reuse an existing device token.
  --install-root PATH                       Default: /opt/fj-ker
  --port PORT                               Default: 8080
  --bind-host HOST                          Default: 127.0.0.1. Use 0.0.0.0 for public IP access.
  --public-base-url URL                     Example: http://115.28.131.113:8080
  --qwen-model MODEL                        Default: qwen3.7-plus
  --max-pages N                             Default: 20
  --max-upload-bytes N                      Default: 2000000
  --service-user USER                       Default: fjker
  --open-firewall                           Add a UFW allow rule for PORT/tcp when ufw exists.
  --accept-public-http-risk                 Required with --open-firewall.
  --skip-playwright-browser-install         Skip Chromium download.
  -h, --help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --dashscope-api-key)
      DASHSCOPE_API_KEY="${2:-}"
      shift 2
      ;;
    --api-token)
      API_TOKEN="${2:-}"
      shift 2
      ;;
    --install-root)
      INSTALL_ROOT="${2:-}"
      shift 2
      ;;
    --port)
      PORT="${2:-}"
      shift 2
      ;;
    --bind-host)
      BIND_HOST="${2:-}"
      shift 2
      ;;
    --public-base-url)
      PUBLIC_BASE_URL="${2:-}"
      shift 2
      ;;
    --qwen-model)
      QWEN_MODEL="${2:-}"
      shift 2
      ;;
    --max-pages)
      MAX_PAGES="${2:-}"
      shift 2
      ;;
    --max-upload-bytes)
      MAX_UPLOAD_BYTES="${2:-}"
      shift 2
      ;;
    --service-user)
      SERVICE_USER="${2:-}"
      shift 2
      ;;
    --open-firewall)
      OPEN_FIREWALL="1"
      shift
      ;;
    --accept-public-http-risk)
      ACCEPT_PUBLIC_HTTP_RISK="1"
      shift
      ;;
    --skip-playwright-browser-install)
      SKIP_PLAYWRIGHT_BROWSER_INSTALL="1"
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

step() {
  echo
  echo "==> $*"
}

die() {
  echo "ERROR: $*" >&2
  exit 1
}

require_root() {
  if [[ "${EUID}" -ne 0 ]]; then
    die "Run with sudo or as root."
  fi
}

generate_token() {
  if command -v openssl >/dev/null 2>&1; then
    openssl rand -base64 32 | tr '+/' '-_' | tr -d '='
  else
    python3 - <<'PY'
import secrets
print(secrets.token_urlsafe(32))
PY
  fi
}

copy_tree() {
  local source_root="$1"
  local dest_root="$2"
  mkdir -p "$dest_root"
  find "$dest_root" -mindepth 1 -maxdepth 1 ! -name logs -exec rm -rf {} +
  tar \
    --exclude='./.git' \
    --exclude='./.agents' \
    --exclude='./.codex' \
    --exclude='./server/.venv' \
    --exclude='./server/.pytest_cache' \
    --exclude='./server/.test_deps' \
    --exclude='./server/app/__pycache__' \
    --exclude='./server/tests/__pycache__' \
    -C "$source_root" -cf - . | tar -C "$dest_root" -xf -
}

wait_health() {
  local url="$1"
  for _ in $(seq 1 45); do
    if curl -fsS --max-time 2 "$url" >/dev/null 2>&1; then
      return 0
    fi
    sleep 1
  done
  return 1
}

require_root

[[ -n "$DASHSCOPE_API_KEY" ]] || die "--dashscope-api-key is required."

if [[ "$OPEN_FIREWALL" == "1" && "$ACCEPT_PUBLIC_HTTP_RISK" != "1" ]]; then
  die "--open-firewall requires --accept-public-http-risk. Public HTTP is plaintext."
fi

if [[ "$OPEN_FIREWALL" == "1" && ( "$BIND_HOST" == "127.0.0.1" || "$BIND_HOST" == "localhost" ) ]]; then
  die "--open-firewall requires --bind-host 0.0.0.0 or another non-loopback address."
fi

if [[ -z "$API_TOKEN" ]]; then
  API_TOKEN="$(generate_token)"
fi

if [[ -z "$PUBLIC_BASE_URL" ]]; then
  PUBLIC_BASE_URL="http://<YOUR_PUBLIC_IP>:${PORT}"
fi

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
SERVER_ROOT="${INSTALL_ROOT}/server"
LOG_ROOT="${INSTALL_ROOT}/logs"
PLAYWRIGHT_BROWSERS_PATH="${INSTALL_ROOT}/.cache/ms-playwright"
VENV_PYTHON="${SERVER_ROOT}/.venv/bin/python"
ENV_PATH="${SERVER_ROOT}/.env"
INFO_PATH="${SERVER_ROOT}/DEPLOYMENT_INFO.txt"
SERVICE_FILE="/etc/systemd/system/${SERVICE_NAME}.service"

step "Installing OS packages"
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y python3 python3-venv python3-pip curl ca-certificates tar fontconfig fonts-noto-cjk
fc-cache -f

PYTHON_BIN="$(command -v python3)"
"${PYTHON_BIN}" - <<'PY'
import sys
if sys.version_info < (3, 11):
    raise SystemExit("Python >= 3.11 is required")
print("Python", sys.version)
PY

step "Creating service user"
if ! id -u "$SERVICE_USER" >/dev/null 2>&1; then
  useradd --system --home "$INSTALL_ROOT" --shell /usr/sbin/nologin "$SERVICE_USER"
fi

step "Copying project files to ${INSTALL_ROOT}"
mkdir -p "$INSTALL_ROOT" "$LOG_ROOT" "$PLAYWRIGHT_BROWSERS_PATH"
if [[ "$(readlink -f "$SOURCE_ROOT")" != "$(readlink -f "$INSTALL_ROOT" 2>/dev/null || true)" ]]; then
  copy_tree "$SOURCE_ROOT" "$INSTALL_ROOT"
fi

[[ -d "$SERVER_ROOT" ]] || die "Server directory not found at $SERVER_ROOT"

step "Creating Python virtual environment"
if [[ ! -x "$VENV_PYTHON" ]]; then
  "$PYTHON_BIN" -m venv "${SERVER_ROOT}/.venv"
fi

step "Installing Python dependencies"
"$VENV_PYTHON" -m pip install --upgrade pip setuptools wheel
"$VENV_PYTHON" -m pip install -e "$SERVER_ROOT"

if [[ "$SKIP_PLAYWRIGHT_BROWSER_INSTALL" != "1" ]]; then
  step "Installing Playwright Chromium"
  if ! "$VENV_PYTHON" -m playwright install-deps chromium; then
    echo "WARNING: Playwright OS dependency installation failed." >&2
  fi
  if ! PLAYWRIGHT_BROWSERS_PATH="$PLAYWRIGHT_BROWSERS_PATH" "$VENV_PYTHON" -m playwright install chromium; then
    echo "WARNING: Playwright Chromium install failed. Pillow fallback rendering remains available." >&2
  fi
fi

step "Caching MathJax for offline PC-side formula rendering"
MATHJAX_DIR="${SERVER_ROOT}/app/templates/mathjax"
mkdir -p "$MATHJAX_DIR"
if ! curl -fsSL --retry 3 --retry-delay 2 "$MATHJAX_URL" -o "${MATHJAX_DIR}/tex-svg.js"; then
  echo "WARNING: MathJax download failed. Renderer will use the CDN at runtime." >&2
fi

step "Writing environment file"
cat > "$ENV_PATH" <<EOF
DASHSCOPE_API_KEY=${DASHSCOPE_API_KEY}
QWEN_MODEL=${QWEN_MODEL}
FJKER_API_TOKEN=${API_TOKEN}
SERVER_HOST=${BIND_HOST}
SERVER_PORT=${PORT}
MAX_PAGES=${MAX_PAGES}
MAX_UPLOAD_BYTES=${MAX_UPLOAD_BYTES}
EOF
chown "$SERVICE_USER:$SERVICE_USER" "$ENV_PATH"
chmod 600 "$ENV_PATH"

step "Writing systemd service"
cat > "$SERVICE_FILE" <<EOF
[Unit]
Description=FJ-ker FastAPI server
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=${SERVICE_USER}
Group=${SERVICE_USER}
WorkingDirectory=${SERVER_ROOT}
EnvironmentFile=${ENV_PATH}
Environment=PLAYWRIGHT_BROWSERS_PATH=${PLAYWRIGHT_BROWSERS_PATH}
ExecStart=${VENV_PYTHON} -m uvicorn app.main:app --host ${BIND_HOST} --port ${PORT}
Restart=always
RestartSec=3
StandardOutput=append:${LOG_ROOT}/server.log
StandardError=append:${LOG_ROOT}/server.err.log
NoNewPrivileges=true
PrivateTmp=true

[Install]
WantedBy=multi-user.target
EOF

chown -R "$SERVICE_USER:$SERVICE_USER" "$INSTALL_ROOT"
chmod 755 "$INSTALL_ROOT" "$SERVER_ROOT" "$LOG_ROOT"

step "Starting systemd service"
systemctl daemon-reload
systemctl enable --now "$SERVICE_NAME.service"

HEALTH_URL="http://127.0.0.1:${PORT}/health"
step "Waiting for local health check: ${HEALTH_URL}"
if ! wait_health "$HEALTH_URL"; then
  echo "Recent service status:" >&2
  systemctl --no-pager --full status "$SERVICE_NAME.service" >&2 || true
  echo "Recent logs:" >&2
  journalctl -u "$SERVICE_NAME.service" -n 100 --no-pager >&2 || true
  die "Service started but health check did not pass."
fi

if [[ "$OPEN_FIREWALL" == "1" ]]; then
  step "Opening UFW port ${PORT}/tcp when available"
  if command -v ufw >/dev/null 2>&1; then
    ufw allow "${PORT}/tcp" || true
    ufw status || true
  else
    echo "UFW is not installed. Open TCP ${PORT} in your cloud security group/firewall." >&2
  fi
fi

step "Writing deployment info"
cat > "$INFO_PATH" <<EOF
FJ-ker deployment complete

InstallRoot=${INSTALL_ROOT}
ServerRoot=${SERVER_ROOT}
HealthUrl=${HEALTH_URL}
PublicBaseUrl=${PUBLIC_BASE_URL}
ServiceName=${SERVICE_NAME}.service
LogFiles=${LOG_ROOT}/server.log ${LOG_ROOT}/server.err.log

Firmware settings:
#define SERVER_BASE_URL "${PUBLIC_BASE_URL}"
#define FJKER_API_TOKEN "${API_TOKEN}"

Keep this file private. It contains the device API token.
EOF
chown "$SERVICE_USER:$SERVICE_USER" "$INFO_PATH"
chmod 600 "$INFO_PATH"

cat <<EOF

Deployment complete.
Local health: ${HEALTH_URL}
Public base URL for firmware: ${PUBLIC_BASE_URL}
Deployment info: ${INFO_PATH}

Add these to firmware/include/secrets.h:
#define SERVER_BASE_URL "${PUBLIC_BASE_URL}"
#define FJKER_API_TOKEN "${API_TOKEN}"

Useful commands:
  sudo systemctl status ${SERVICE_NAME} --no-pager
  sudo journalctl -u ${SERVICE_NAME} -f
  sudo tail -n 100 ${LOG_ROOT}/server.log
EOF
