#!/usr/bin/env bash
# test.sh — correctness, mode-coverage, and performance tests for life3d
#
# Usage:
#   ./test.sh [--binary path/to/life3d] [--threads N] [--bench]
#
# By default looks for ./build/life3d.

set -euo pipefail

BINARY=""
THREADS=""
RUN_BENCH=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --binary)  BINARY="$2";  shift 2 ;;
    --threads) THREADS="$2"; shift 2 ;;
    --bench)   RUN_BENCH=1;  shift   ;;
    *)  echo "Unknown option: $1" >&2; exit 1 ;;
  esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BINARY="${BINARY:-${SCRIPT_DIR}/build/life3d}"

if [[ ! -x "${BINARY}" ]]; then
  echo "[test] Binary not found at ${BINARY}. Run ./run.sh first." >&2
  exit 1
fi

if [[ -z "${THREADS}" ]]; then
  THREADS="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
fi
export OMP_NUM_THREADS="${THREADS}"
echo "[test] binary=${BINARY}  OMP_NUM_THREADS=${THREADS}"
echo ""

# ── Helpers ───────────────────────────────────────────────────────────────────
PASS=0; FAIL=0

check() {
  local label="$1" expected="$2" actual="$3"
  if [[ "${actual}" == "${expected}" ]]; then
    echo "[PASS] ${label}"; PASS=$((PASS+1))
  else
    echo "[FAIL] ${label}"
    echo "       expected: ${expected}"
    echo "       actual  : ${actual}"
    FAIL=$((FAIL+1))
  fi
}

run_sim() {
  # run_sim <gen> <N> <density> <seed> — prints stdout; timing/mode to stderr
  "${BINARY}" "$1" "$2" "$3" "$4" 2>/dev/null
}

run_sim_stderr() {
  # run_sim_stderr <gen> <N> <density> <seed> — prints stderr only
  "${BINARY}" "$1" "$2" "$3" "$4" 2>&1 1>/dev/null
}

# ── Determinism ───────────────────────────────────────────────────────────────
echo "── Determinism ──────────────────────────────────────────────────────────"

OUT_A="$(run_sim 10 16 0.25 42)"
OUT_B="$(run_sim 10 16 0.25 42)"
check "Same params → identical output (dense path, N=16)" "${OUT_A}" "${OUT_B}"

OUT_C="$(run_sim 50 32 0.08 42)"
OUT_D="$(run_sim 50 32 0.08 42)"
check "Same params → identical output (sparse path, N=32)" "${OUT_C}" "${OUT_D}"

OUT_E="$(run_sim 10 16 0.25 99)"
if [[ "${OUT_A}" != "${OUT_E}" ]]; then
  echo "[PASS] Different seeds produce different output"; PASS=$((PASS+1))
else
  echo "[WARN] Different seeds produced the same output (may be coincidence)"
fi

echo ""

# ── Golden-file regression (dense path — d=0.25) ──────────────────────────────
echo "── Golden regression: dense path (d=0.25) ───────────────────────────────"

dense_cases=(
  "10  16  0.25  42"
  "50  64  0.25  1"
  "100 32  0.25  99"
)
for case_str in "${dense_cases[@]}"; do
  read -r gen N density seed <<< "${case_str}"
  golden="${SCRIPT_DIR}/tests/golden_gen${gen}_N${N}_d${density}_s${seed}.txt"
  if [[ -f "${golden}" ]]; then
    expected="$(cat "${golden}")"
    actual="$(run_sim "${gen}" "${N}" "${density}" "${seed}")"
    check "golden gen=${gen} N=${N} d=${density} s=${seed}" "${expected}" "${actual}"
  else
    echo "[INFO] Generating golden: ${golden}"
    mkdir -p "${SCRIPT_DIR}/tests"
    run_sim "${gen}" "${N}" "${density}" "${seed}" > "${golden}"
    echo "       Saved — re-run to verify."
  fi
done

echo ""

# ── Golden-file regression (sparse path — d=0.08) ────────────────────────────
echo "── Golden regression: sparse path (d=0.08) ──────────────────────────────"

sparse_cases=(
  "50  32  0.08  42"
  "100 64  0.08  7"
)
for case_str in "${sparse_cases[@]}"; do
  read -r gen N density seed <<< "${case_str}"
  golden="${SCRIPT_DIR}/tests/golden_gen${gen}_N${N}_d${density}_s${seed}.txt"
  if [[ -f "${golden}" ]]; then
    expected="$(cat "${golden}")"
    actual="$(run_sim "${gen}" "${N}" "${density}" "${seed}")"
    check "golden gen=${gen} N=${N} d=${density} s=${seed}" "${expected}" "${actual}"
  else
    echo "[INFO] Generating golden: ${golden}"
    mkdir -p "${SCRIPT_DIR}/tests"
    run_sim "${gen}" "${N}" "${density}" "${seed}" > "${golden}"
    echo "       Saved — re-run to verify."
  fi
done

echo ""

# ── Mode coverage ─────────────────────────────────────────────────────────────
# Both modes must fire for a simulation to be trustworthy.
echo "── Mode coverage ────────────────────────────────────────────────────────"

# d=0.08: gen 0 is dense (initial dirty_ratio ≈ 88%), remainder sparse
MODE_D008="$(run_sim_stderr 100 32 0.08 42)"
echo "  d=0.08 N=32 gen=100: ${MODE_D008}"

DENSE_N="$(echo "${MODE_D008}" | grep -oP 'dense=\K[0-9]+' || echo 0)"
SPARSE_N="$(echo "${MODE_D008}" | grep -oP 'sparse=\K[0-9]+' || echo 0)"

check "dense  mode fires (initial dirty_ratio > threshold)" "yes" \
      "$([[ "${DENSE_N}"  -gt 0 ]] && echo yes || echo no)"
check "sparse mode fires (population collapses after gen 0)" "yes" \
      "$([[ "${SPARSE_N}" -gt 0 ]] && echo yes || echo no)"

# d=0.25: stays dense throughout (3D Life sustains high density at this init)
MODE_D025="$(run_sim_stderr 10 16 0.25 42)"
echo "  d=0.25 N=16 gen=10:  ${MODE_D025}"
DENSE_25="$(echo "${MODE_D025}" | grep -oP 'dense=\K[0-9]+' || echo 0)"
check "dense-only path for d=0.25 (expected high dirty ratio)" "yes" \
      "$([[ "${DENSE_25}" -gt 0 ]] && echo yes || echo no)"

echo ""

# ── Edge cases ────────────────────────────────────────────────────────────────
echo "── Edge cases ───────────────────────────────────────────────────────────"

# Density 0 — all cells dead; sparse mode throughout; all counts must be 0
OUT_EMPTY="$(run_sim 5 16 0.0 1)"
ALL_ZERO=1
while IFS= read -r line; do
  cnt="$(echo "${line}" | awk '{print $2}')"
  [[ "${cnt}" != "0" ]] && ALL_ZERO=0
done <<< "${OUT_EMPTY}"
check "density=0 → all max_count = 0" 1 ${ALL_ZERO}

OUT_EMPTY2="$(run_sim 5 16 0.0 999)"
check "density=0 is seed-independent" "${OUT_EMPTY}" "${OUT_EMPTY2}"

# Output format: exactly 9 lines, species IDs 1–9
OUT_FMT="$(run_sim 5 8 0.3 7)"
check "output has exactly 9 lines"   "9" "$(echo "${OUT_FMT}" | wc -l | tr -d ' ')"
check "first line species ID = 1"    "1" "$(echo "${OUT_FMT}" | awk 'NR==1{print $1}')"
check "last line species ID = 9"     "9" "$(echo "${OUT_FMT}" | awk 'NR==9{print $1}')"

# Single generation — should not crash
run_sim 1 4 0.5 123 > /dev/null
echo "[PASS] gen=1 N=4 completes without crash"; PASS=$((PASS+1))

# Larger grid, single generation
run_sim 1 64 0.25 7 > /dev/null
echo "[PASS] gen=1 N=64 completes without crash"; PASS=$((PASS+1))

echo ""

# ── Performance benchmarks ────────────────────────────────────────────────────
if [[ "${RUN_BENCH}" -eq 1 ]]; then
  echo "── Performance benchmarks ───────────────────────────────────────────────"
  echo "   OMP_NUM_THREADS=${OMP_NUM_THREADS}"
  echo ""

  bench() {
    local label="$1"; shift
    printf "   %-52s  " "${label}"
    local t
    t="$("${BINARY}" "$@" 2>&1 1>/dev/null)"
    echo "${t}"
  }

  echo "   Dense mode (d=0.25 — stays above threshold)"
  bench "gen=50,  N=64,  d=0.25, seed=1"   50   64  0.25 1
  bench "gen=50,  N=128, d=0.25, seed=1"   50  128  0.25 1
  bench "gen=20,  N=256, d=0.25, seed=1"   20  256  0.25 1
  bench "gen=5,   N=512, d=0.25, seed=1"    5  512  0.25 1
  echo ""
  echo "   Sparse mode (d=0.08 — drops below threshold after gen 0)"
  bench "gen=200, N=64,  d=0.08, seed=1"  200   64  0.08 1
  bench "gen=200, N=128, d=0.08, seed=1"  200  128  0.08 1
  bench "gen=100, N=256, d=0.08, seed=1"  100  256  0.08 1
  bench "gen=50,  N=512, d=0.08, seed=1"   50  512  0.08 1
  echo ""
fi

# ── Summary ───────────────────────────────────────────────────────────────────
echo "────────────────────────────────────────────────────────────────────────"
echo "Results: ${PASS} passed, ${FAIL} failed"
[[ "${FAIL}" -eq 0 ]]
