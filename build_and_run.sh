#!/usr/bin/env bash
# build_and_run.sh — build then immediately run life3d
#
# Usage:
#   ./build_and_run.sh <generations> <N> <density> <seed>
#
# Example:
#   ./build_and_run.sh 1000 64 0.4 0

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ $# -lt 4 ]]; then
  echo "Usage: $0 <generations> <N> <density> <seed>" >&2
  exit 1
fi

"${ROOT_DIR}/build.sh"
exec "${ROOT_DIR}/run.sh" "$@"
