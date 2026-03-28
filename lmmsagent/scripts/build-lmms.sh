#!/usr/bin/env bash
set -euo pipefail

LMMS_DIR="${1:?Usage: build-lmms.sh /path/to/lmms /path/to/build}"
BUILD_DIR="${2:?Usage: build-lmms.sh /path/to/lmms /path/to/build}"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

cmake -S "$LMMS_DIR" -B "$BUILD_DIR"
cmake --build "$BUILD_DIR" -j"$JOBS"
