#!/usr/bin/env bash
# scripts/fmt_diff.sh — format files changed vs origin/main using repo .clang-format
set -euo pipefail
BASE="${1:-origin/main}"
git diff --name-only "$BASE"...   | grep -E '\.(c|cc|cpp|cxx|h|hh|hpp|hxx)$'   | xargs -r clang-format -i
echo "[fmt_diff] formatted files changed vs ${BASE}."
