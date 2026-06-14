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
# clang-tidy's frontend (clang 18 as of this writing) reports __cpp_concepts
# as 201907L (the C++20 value) under -std=c++23, but libstdc++ 13's <expected>
# header requires __cpp_concepts >= 202002L to define std::expected /
# std::unexpected. This is a known clang/libstdc++ feature-test-macro mismatch
# for several C++23 library additions, not a defect in this codebase: g++
# itself compiles and links the project correctly (it defines the macro at
# the higher value), and a newer clang will report the higher value too.
#
# The fix is the standard workaround: tell clang-tidy's frontend to report the
# higher value so libstdc++'s header guard takes the C++23 branch, and
# suppress the resulting "builtin macro redefined" warning (which would
# otherwise itself be promoted to an error by WarningsAsErrors: '*').
#
# Remove this once the installed clang-tidy reports __cpp_concepts >= 202002L
# natively under -std=c++23 (check with the one-liner below):
#   printf '#if __cpp_concepts >= 202002L\n#error OK\n#endif\n' | \
#     clang-tidy --quiet -p build -- -std=c++23 -
EXTRA_ARGS=(
  "--extra-arg=-D__cpp_concepts=202002L"
  "--extra-arg=-Wno-builtin-macro-redefined"
)

for source in "${SOURCES[@]}"; do
  echo "clang-tidy ${source}"
  clang-tidy \
    --config-file "${CLANG_TIDY_CONFIG}" \
    -p "${BUILD_DIR}" \
    --quiet \
    "${EXTRA_ARGS[@]}" \
    "${source}"
done
