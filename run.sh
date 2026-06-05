#!/usr/bin/env bash
# run.sh — build if needed, then run life3d
#
# Usage:
#   ./run.sh <generations> <N> <density> <seed>
#
# Example:
#   ./run.sh 1000 64 0.4 0

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BINARY="${ROOT_DIR}/build/life3d"

if [[ $# -lt 4 ]]; then
  echo "Usage: $0 <generations> <N> <density> <seed>" >&2
  exit 1
fi

if [[ ! -x "${BINARY}" ]]; then
  echo "[run] Binary not found — building first..."
  "${ROOT_DIR}/build.sh"
fi

exec "${BINARY}" "$@"
