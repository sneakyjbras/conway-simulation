#!/usr/bin/env bash
# run.sh — rebuild and run life3d
#
# Usage:
#   ./run.sh [--clean] [--type Debug|Release] [--threads N] [--debug] [--] <generations> <N> <density> <seed>
#
# Examples:
#   ./run.sh --clean -- 4 8 0.25 123
#   ./run.sh -- 200 256 0.18 42

set -euo pipefail

BUILD_TYPE="Release"
THREADS=""
CLEAN=0
DEBUG_FLAG=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --clean)   CLEAN=1;                    shift   ;;
    --type)    BUILD_TYPE="${2:-Release}"; shift 2 ;;
    --threads) THREADS="${2:-}";           shift 2 ;;
    --debug)   DEBUG_FLAG="--debug";      shift   ;;
    --)        shift; break                        ;;
    *)         break                               ;;
  esac
done

if [[ $# -lt 4 ]]; then
  echo "Usage: $0 [--clean] [--type Debug|Release] [--threads N] [--debug] -- <generations> <N> <density> <seed>" >&2
  exit 1
fi

# Auto-detect CPU thread count for cmake --build parallelism.
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
  echo "[run.sh] Cleaning ${BUILD_DIR}..."
  rm -rf "${BUILD_DIR}"
fi

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

echo "[run.sh] Configuring (type=${BUILD_TYPE})..."
cmake -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" .. -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

echo "[run.sh] Building with ${THREADS} threads..."
cmake --build . -j "${THREADS}"

echo "[run.sh] Running: ./life3d $*"
./life3d "$@"
