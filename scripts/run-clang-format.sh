#!/usr/bin/env bash
# Run clang-format against all C++ headers and sources under src/.
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${PROJECT_ROOT}"

if ! command -v clang-format >/dev/null 2>&1; then
  echo "clang-format is required for the C++ formatting workflow." >&2
  echo "Install clang-format on your system and rerun this script." >&2
  exit 1
fi

mapfile -t FILES < <(
  find src -type f \( -name '*.hpp' -o -name '*.cpp' \) | sort
)

if [[ ${#FILES[@]} -eq 0 ]]; then
  echo "No C++ source files found under src/."
  exit 0
fi

if [[ ${1:-} == "--check" ]]; then
  shift
  exec clang-format --dry-run --Werror "$@" "${FILES[@]}"
fi

exec clang-format -i "$@" "${FILES[@]}"
