#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${ROOT}/build"

if [[ ! -x "${BIN}/backend_server" || ! -x "${BIN}/pax_balancer" || ! -x "${BIN}/pax_client" ]]; then
  echo "Build first, e.g.: mkdir -p build && cd build && cmake .. && cmake --build ." >&2
  exit 1
fi

pids=()
cleanup() {
  for pid in "${pids[@]:-}"; do
    kill "$pid" 2>/dev/null || true
  done
}
trap cleanup EXIT INT TERM

for port in 6025 6026 6027 6028; do
  "${BIN}/backend_server" "$port" &
  pids+=($!)
done

sleep 1
"${BIN}/pax_balancer" &
pids+=($!)

sleep 1
"${BIN}/pax_client" 127.0.0.1 6020 5
