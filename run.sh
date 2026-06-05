#!/usr/bin/env bash
# run.sh — run life3d (must build first with ./build.sh)
#
# Usage:
#   ./run.sh <generations> <N> <density> <seed>
#
# Example:
#   ./run.sh 1000 64 0.4 0

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BINARY="${ROOT_DIR}/build/life3d"

if [[ ! -x "${BINARY}" ]]; then
  echo "[run] Binary not found. Run ./build.sh first." >&2
  exit 1
fi

if [[ $# -lt 4 ]]; then
  echo "Usage: $0 <generations> <N> <density> <seed>" >&2
  exit 1
fi

exec "${BINARY}" "$@"
