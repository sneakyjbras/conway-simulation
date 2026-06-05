#!/usr/bin/env bash
# build.sh — configure and compile life3d
#
# Usage:
#   ./build.sh [--clean] [--type Debug|Release]

set -euo pipefail

BUILD_TYPE="Release"
CLEAN=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --clean) CLEAN=1;                    shift   ;;
    --type)  BUILD_TYPE="${2:-Release}"; shift 2 ;;
    *) echo "Unknown option: $1" >&2; exit 1     ;;
  esac
done

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

if [[ "${CLEAN}" -eq 1 && -d "${BUILD_DIR}" ]]; then
  echo "[build] Cleaning ${BUILD_DIR}..."
  rm -rf "${BUILD_DIR}"
fi

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

echo "[build] Configuring (type=${BUILD_TYPE})..."
cmake -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
      -G "Unix Makefiles" .. > /dev/null

echo "[build] Compiling..."
cmake --build . -j "$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

echo "[build] Done — binary at ${BUILD_DIR}/life3d"
