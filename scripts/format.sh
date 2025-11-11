#!/usr/bin/env bash
# scripts/fmt.sh — format ALL tracked C/C++ files using repo .clang-format
set -euo pipefail
git ls-files '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hh' '*.hpp' '*.hxx' | xargs -r clang-format -i
echo "[fmt] formatted tracked C/C++ files."
