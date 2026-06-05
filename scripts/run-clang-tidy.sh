#!/usr/bin/env bash
# Run clang-tidy against C++ sources under src/ using the CMake compile
# command database from build/compile_commands.json.
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${PROJECT_ROOT}"

if ! command -v clang-tidy >/dev/null 2>&1; then
  echo "clang-tidy is required for the C++ linting workflow." >&2
  echo "Install clang-tidy on your system and rerun this script." >&2
  exit 1
fi

CLANG_TIDY_CONFIG="${PROJECT_ROOT}/.clang-tidy"
if [[ ! -f "${CLANG_TIDY_CONFIG}" ]]; then
  echo "Missing clang-tidy configuration: ${CLANG_TIDY_CONFIG}" >&2
  exit 1
fi

BUILD_DIR="${PROJECT_ROOT}/build"
if [[ ! -f "${BUILD_DIR}/compile_commands.json" ]]; then
  echo "compile_commands.json not found at ${BUILD_DIR}." >&2
  echo "Generate it first:  cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON" >&2
  exit 1
fi

mapfile -t SOURCES < <(
  find src -type f -name '*.cpp' | sort
)

if [[ ${#SOURCES[@]} -eq 0 ]]; then
  echo "No C++ source files found under src/."
  exit 0
fi

for source in "${SOURCES[@]}"; do
  echo "clang-tidy ${source}"
  clang-tidy \
    --config-file "${CLANG_TIDY_CONFIG}" \
    -p "${BUILD_DIR}" \
    --quiet \
    "${source}"
done
