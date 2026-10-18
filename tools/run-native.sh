#!/usr/bin/bash
# Development launch: read-only bridge, disposable profile overlay, never the
# installed root helper. The patched module comes from .cache/native and the
# vendor loader from the installed package (the official loader must be present).
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
native="$root/.cache/native"
launcher="$root/.cache/runtime/parsec-linux-host"

[ -x "$launcher" ] || { echo "Build the runtime first: make runtime" >&2; exit 1; }
[ -f "$native/parsecd-150-105c.so" ] || { echo "Build the module first: make native" >&2; exit 1; }

loader=/usr/lib/parsec-linux-host/vendor/parsecd
[ -x "$loader" ] || loader=/usr/bin/parsecd
[ -x "$loader" ] || { echo "The official vendor loader is not installed." >&2; exit 1; }
ln -sf "$loader" "$native/parsecd"

exec env PARSEC_HOST_VENDOR_DIR="$native" \
    "$launcher" open --temporary-profile --read-only "$@"
