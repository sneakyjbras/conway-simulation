#!/usr/bin/env bash
# Run clang-format against all C++ headers and sources under src/.
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${PROJECT_ROOT}"

# The concrete clang-format binary can be pinned via ${CLANG_FORMAT} so CI runs
# a fixed version (e.g. clang-format-21) independent of the runner default.
CLANG_FORMAT="${CLANG_FORMAT:-clang-format}"

if ! command -v "${CLANG_FORMAT}" >/dev/null 2>&1; then
  echo "${CLANG_FORMAT} is required for the C++ formatting workflow." >&2
  echo "Install ${CLANG_FORMAT} on your system and rerun this script." >&2
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
  exec "${CLANG_FORMAT}" --dry-run --Werror "$@" "${FILES[@]}"
fi

exec "${CLANG_FORMAT}" -i "$@" "${FILES[@]}"
