#!/usr/bin/env bash
# Launch Don't Starve Together Linux x86_64 Dedicated Server with DST HyperCore Server v1.3.0.
# Safe to place either in <DST_SERVER>/bin64/ or run from anywhere pointing DST_BIN64_DIR.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN64_DIR="${DST_BIN64_DIR:-$SCRIPT_DIR}"
SO_PATH="${HYPERCORE_SO:-$BIN64_DIR/libhypercore.so}"
SERVER_BIN="${DST_SERVER_BIN:-$BIN64_DIR/dontstarve_dedicated_server_nullrenderer_x64}"

if [[ ! -x "$SERVER_BIN" ]]; then
    if [[ -x "$BIN64_DIR/bin64/dontstarve_dedicated_server_nullrenderer_x64" ]]; then
        BIN64_DIR="$BIN64_DIR/bin64"
        SERVER_BIN="$BIN64_DIR/dontstarve_dedicated_server_nullrenderer_x64"
        [[ -f "$SO_PATH" ]] || SO_PATH="$BIN64_DIR/libhypercore.so"
    else
        echo "[HyperCore] ERROR: Server binary not found: $SERVER_BIN" >&2
        exit 1
    fi
fi

if [[ ! -f "$SO_PATH" ]]; then
    echo "[HyperCore] ERROR: libhypercore.so not found: $SO_PATH" >&2
    exit 1
fi

cd "$BIN64_DIR"
export LD_LIBRARY_PATH="./lib64:${LD_LIBRARY_PATH:-}"
if [[ -n "${LD_PRELOAD:-}" ]]; then
    export LD_PRELOAD="$SO_PATH:$LD_PRELOAD"
else
    export LD_PRELOAD="$SO_PATH"
fi

exec "$SERVER_BIN" "$@"
