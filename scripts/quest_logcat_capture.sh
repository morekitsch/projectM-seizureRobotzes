#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="${ROOT_DIR:-/home/here/Documents/swirl/logs}"
PREFIX="${PREFIX:-quest-logcat}"
MAX_KB="${MAX_KB:-4096}"
KEEP_FILES="${KEEP_FILES:-8}"

PID_FILE="$ROOT_DIR/${PREFIX}.pid"
CURRENT_FILE="$ROOT_DIR/${PREFIX}.log"
LATEST_PATH_FILE="$ROOT_DIR/${PREFIX}-latest.path"

rotate_logs() {
  local keep="$KEEP_FILES"
  local i=0
  if [ "$keep" -lt 1 ]; then
    keep=1
  fi
  for ((i=keep-1; i>=1; --i)); do
    if [ -f "${CURRENT_FILE}.${i}" ]; then
      mv "${CURRENT_FILE}.${i}" "${CURRENT_FILE}.$((i+1))"
    fi
  done
  if [ -f "$CURRENT_FILE" ]; then
    mv "$CURRENT_FILE" "${CURRENT_FILE}.1"
  fi
  : > "$CURRENT_FILE"
  printf '%s\n' "$CURRENT_FILE" > "$LATEST_PATH_FILE"
}

stop_capture() {
  if [ ! -f "$PID_FILE" ]; then
    echo "not_running"
    return 0
  fi

  local pid
  pid="$(cat "$PID_FILE" 2>/dev/null || true)"
  if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
    kill "$pid" || true
    sleep 0.2
    if kill -0 "$pid" 2>/dev/null; then
      kill -9 "$pid" || true
    fi
    echo "stopped pid=$pid"
  else
    echo "not_running"
  fi

  rm -f "$PID_FILE"
}

run_capture() {
  mkdir -p "$ROOT_DIR"
  printf '%s\n' "$CURRENT_FILE" > "$LATEST_PATH_FILE"
  : > "$CURRENT_FILE"

  local max_bytes=$((MAX_KB * 1024))
  if [ "$max_bytes" -lt 1024 ]; then
    max_bytes=1024
  fi

  echo "capture_started $(date -Is)" >> "$CURRENT_FILE"
  while true; do
    adb start-server >/dev/null 2>&1 || true
    while IFS= read -r line; do
      printf '%s\n' "$line" >> "$CURRENT_FILE"
      local size_bytes
      size_bytes="$(stat -c%s "$CURRENT_FILE" 2>/dev/null || echo 0)"
      if [ "$size_bytes" -ge "$max_bytes" ]; then
        rotate_logs
        echo "log_rotated $(date -Is)" >> "$CURRENT_FILE"
      fi
    done < <(adb logcat -v threadtime -T 1 2>&1)
    echo "logcat_restarting $(date -Is)" >> "$CURRENT_FILE"
    sleep 1
  done
}

start_capture() {
  mkdir -p "$ROOT_DIR"
  stop_capture >/dev/null 2>&1 || true

  local launch_log="$ROOT_DIR/${PREFIX}-launch-$(date +%Y%m%d-%H%M%S).log"
  nohup setsid "$0" run > "$launch_log" 2>&1 < /dev/null &
  local pid=$!
  printf '%s\n' "$pid" > "$PID_FILE"
  printf '%s\n' "$CURRENT_FILE" > "$LATEST_PATH_FILE"

  sleep 1
  if kill -0 "$pid" 2>/dev/null; then
    echo "started pid=$pid"
    echo "current_file=$CURRENT_FILE"
    echo "latest_path_file=$LATEST_PATH_FILE"
    echo "launch_log=$launch_log"
    return 0
  fi

  echo "failed_to_start"
  echo "launch_log=$launch_log"
  return 1
}

status_capture() {
  local pid=""
  if [ -f "$PID_FILE" ]; then
    pid="$(cat "$PID_FILE" 2>/dev/null || true)"
  fi

  if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
    echo "running pid=$pid"
  else
    echo "not_running"
  fi
  echo "current_file=$CURRENT_FILE"
  echo "latest_path_file=$LATEST_PATH_FILE"
}

case "${1:-start}" in
  start)
    start_capture
    ;;
  stop)
    stop_capture
    ;;
  status)
    status_capture
    ;;
  run)
    run_capture
    ;;
  *)
    echo "Usage: $0 {start|stop|status|run}" >&2
    exit 2
    ;;
esac
