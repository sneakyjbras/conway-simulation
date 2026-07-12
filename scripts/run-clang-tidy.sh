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

# ── clang / libstdc++ <expected> workaround ─────────────────────────────────
EXTRA_ARGS=(
  "--extra-arg=-D__cpp_concepts=202002L"
  "--extra-arg=-Wno-builtin-macro-redefined"
)

FAILED_SOURCES=()

for source in "${SOURCES[@]}"; do
  echo
  echo "clang-tidy ${source}"

  if ! clang-tidy \
    --config-file "${CLANG_TIDY_CONFIG}" \
    -p "${BUILD_DIR}" \
    --quiet \
    "${EXTRA_ARGS[@]}" \
    "${source}"; then

    FAILED_SOURCES+=("${source}")
  fi
done

echo

if [[ ${#FAILED_SOURCES[@]} -ne 0 ]]; then
  echo "clang-tidy failed for ${#FAILED_SOURCES[@]} file(s):" >&2

  for source in "${FAILED_SOURCES[@]}"; do
    echo "  - ${source}" >&2
  done

  exit 1
fi

echo "clang-tidy passed for all ${#SOURCES[@]} file(s)."
