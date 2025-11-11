#!/usr/bin/env bash
# run.sh — clean rebuild and run wrapper for life3d (CMake + OpenMP)
# Usage:
#   ./run.sh [--clean] [--type Debug|Release] [--threads N] [--] <generations> <N> <density> <seed>
# Examples:
#   ./run.sh --clean --type Release -- 4 64 .25 123
#   ./run.sh --threads 8 -- 200 256 .18 42

set -euo pipefail

# Defaults
BUILD_TYPE="Release"
THREADS=""
CLEAN=0

# Parse flags
while [[ $# -gt 0 ]]; do
  case "$1" in
    --clean) CLEAN=1; shift ;;
    --type)  BUILD_TYPE="${2:-Release}"; shift 2 ;;
    --threads) THREADS="${2:-}"; shift 2 ;;
    --) shift; break ;;
    *) break ;;
  esac
done

# Remaining args are program args
if [[ $# -lt 4 ]]; then
  echo "Usage: $0 [--clean] [--type Debug|Release] [--threads N] -- <generations> <N> <density> <seed>" >&2
  exit 1
fi

# Detect core count if --threads not provided
if [[ -z "${THREADS}" ]]; then
  if command -v nproc >/dev/null 2>&1; then
    THREADS="$(nproc)"
  elif [[ "$(uname -s)" == "Darwin" ]]; then
    THREADS="$(sysctl -n hw.ncpu)"
  else
    THREADS="4"
  fi
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

if [[ "${CLEAN}" -eq 1 && -d "${BUILD_DIR}" ]]; then
  echo "[run.sh] Cleaning build directory..."
  rm -rf "${BUILD_DIR}"
fi

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

echo "[run.sh] Configuring (CMAKE_BUILD_TYPE=${BUILD_TYPE})..."
cmake -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" ..

echo "[run.sh] Building with ${THREADS} threads..."
cmake --build . -j "${THREADS}"

echo "[run.sh] Running: ./life3d $*"
./life3d "$@"

