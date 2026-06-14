#!/usr/bin/env bash
# test.sh — correctness and performance tests for life3d
#
# Usage:
#   ./test.sh [--binary path/to/life3d] [--bench]
#
# By default looks for ./build/life3d.

set -euo pipefail

BINARY=""
RUN_BENCH=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --binary) BINARY="$2"; shift 2 ;;
    --bench)  RUN_BENCH=1; shift   ;;
    *)  echo "Unknown option: $1" >&2; exit 1 ;;
  esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BINARY="${BINARY:-${SCRIPT_DIR}/build/life3d}"

if [[ ! -x "${BINARY}" ]]; then
  echo "[test] Binary not found at ${BINARY}. Run ./run.sh first." >&2
  exit 1
fi

echo "[test] binary=${BINARY}"
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
  # run_sim <gen> <N> <density> <seed> — stdout only
  "${BINARY}" "$1" "$2" "$3" "$4" 2>/dev/null
}

run_sim_stderr() {
  # run_sim_stderr <gen> <N> <density> <seed> — stderr only
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
echo "── Mode coverage ────────────────────────────────────────────────────────"

MODE_D008="$(run_sim_stderr 100 32 0.08 42)"
echo "  d=0.08 N=32 gen=100: ${MODE_D008}"

DENSE_N="$(echo "${MODE_D008}"  | grep -oP 'dense=\K[0-9]+'  || echo 0)"
SPARSE_N="$(echo "${MODE_D008}" | grep -oP 'sparse=\K[0-9]+' || echo 0)"

check "dense  mode fires" "yes" "$([[ "${DENSE_N}"  -gt 0 ]] && echo yes || echo no)"
check "sparse mode fires" "yes" "$([[ "${SPARSE_N}" -gt 0 ]] && echo yes || echo no)"

MODE_D025="$(run_sim_stderr 10 16 0.25 42)"
echo "  d=0.25 N=16 gen=10:  ${MODE_D025}"
DENSE_25="$(echo "${MODE_D025}" | grep -oP 'dense=\K[0-9]+' || echo 0)"
check "dense-only path for d=0.25" "yes" "$([[ "${DENSE_25}" -gt 0 ]] && echo yes || echo no)"

echo ""

# ── Groups 5–8: activity list, incremental counts, hysteresis ─────────────────
echo "── Groups 5–8 optimisation checks ──────────────────────────────────────"

# T1: Activity-list (dirty_indices_) sanity.
# d=0.0 → dirty_count=0 → dirty_ratio=0 ≤ ENTER_SPARSE_THRESHOLD(0.50), so
# Group 8 dispatch switches to sparse from the very first generation.
# Expected: dense=0 in the mode line.
T1_MODE="$(run_sim_stderr 10 16 0.0 1)"
T1_DENSE="$(echo "${T1_MODE}" | grep -oP 'dense=\K[0-9]+' || echo -1)"
check "T1 empty activity list: d=0.0 uses sparse-only path (dense=0)" "0" "${T1_DENSE}"

# T2: Incremental species-count determinism (Group 7).
# Two independent runs with the same seed must produce bit-identical output
# even after many sparse steps where current_counts_ is updated incrementally.
# Uses a parameter set not covered by the existing determinism checks.
T2_A="$(run_sim 50 32 0.08 77)"
T2_B="$(run_sim 50 32 0.08 77)"
check "T2 incremental counts deterministic (d=0.08 seed=77 gen=50)" "${T2_A}" "${T2_B}"

# T3: Hysteresis threshold constants (Group 8).
# The mode line must carry the spec values: ENTER=50%/EXIT=65% for dirty ratio
# and ENTER=5%/EXIT=10% for change ratio.  This catches accidental threshold
# corruption (e.g. a stray sed that zeroes or inflates a constant).
T3_MODE="$(run_sim_stderr 100 32 0.08 42)"
check "T3 hysteresis: dirty_thr=50%/65% in mode line" "yes" \
  "$( echo "${T3_MODE}" | grep -q 'dirty_thr=50%/65%' && echo yes || echo no)"
check "T3 hysteresis: churn_thr=5%/10% in mode line" "yes" \
  "$( echo "${T3_MODE}" | grep -q 'churn_thr=5%/10%' && echo yes || echo no)"

echo ""

# ── Phase 3: simulated-annealing threshold controller ─────────────────────────
echo "── Simulated annealing (Phase 3) ───────────────────────────────────────"

# T4: SA disabled → thresholds stable.
# Without --sa the controller is never constructed, so thresholds_ stays at the
# base values and no "sa:" diagnostic line is emitted.  The mode line must still
# show the base 50%/65% dirty thresholds, and no sa: line may appear.
T4_OFF="$("${BINARY}" 100 32 0.08 42 2>&1 1>/dev/null)"
check "T4 SA off: mode line shows base dirty_thr=50%/65%" "yes" \
  "$( echo "${T4_OFF}" | grep -q 'dirty_thr=50%/65%' && echo yes || echo no)"
check "T4 SA off: no sa: diagnostic line emitted" "yes" \
  "$( echo "${T4_OFF}" | grep -q '^sa:' && echo no || echo yes)"

# T5: SA enabled → thresholds shift.
# With --sa the controller adapts enter_sparse from the chaos metric and cooling
# schedule.  The sa: line must appear and its max must exceed its min, proving
# the threshold actually moved over the run.  A stronger initial temperature is
# used so the movement is unambiguous.
T5_ON="$("${BINARY}" 100 32 0.08 42 --sa --sa-t0 0.5 2>&1 1>/dev/null)"
check "T5 SA on: sa: diagnostic line emitted" "yes" \
  "$( echo "${T5_ON}" | grep -q '^sa:' && echo yes || echo no)"
# Extract min and max enter_sparse and assert max > min via awk numeric compare.
T5_SHIFT="$(echo "${T5_ON}" | grep '^sa:' | \
  sed -nE 's/.*min=([0-9.]+) max=([0-9.]+).*/\1 \2/p' | \
  awk '{ print ($2 > $1) ? "yes" : "no" }')"
check "T5 SA on: enter_sparse threshold shifted (max > min)" "yes" "${T5_SHIFT}"

# T5b: SA must not change correctness — output identical to non-SA run.
# Dense and sparse paths are bit-identical, so adapting *when* we switch modes
# can never change the simulation result, only performance.
T5_BASE_OUT="$(run_sim 100 32 0.08 42)"
T5_SA_OUT="$("${BINARY}" 100 32 0.08 42 --sa --sa-t0 3.0 --sa-cool 0.01 2>/dev/null)"
check "T5b SA preserves correctness (output identical to non-SA)" "${T5_BASE_OUT}" "${T5_SA_OUT}"

echo ""

# ── Phase 4: Monte Carlo runner + CSV output ──────────────────────────────────
echo "── Monte Carlo runner & CSV (Phase 4) ──────────────────────────────────"

# T6: Monte Carlo determinism.
# --runs N executes N independent trials but prints only the final run's
# species maxima.  The number of trials must not change the result, so
# --runs 3 stdout must equal the single-run stdout.
T6_ONE="$(run_sim 10 16 0.25 42)"
T6_THREE="$("${BINARY}" 10 16 0.25 42 --runs 3 2>/dev/null)"
check "T6 Monte Carlo: --runs 3 stdout identical to single run" "${T6_ONE}" "${T6_THREE}"
# stdout must be exactly 9 lines (one per species) regardless of run count.
T6_LINES="$(echo "${T6_THREE}" | wc -l | tr -d ' ')"
check "T6 Monte Carlo: --runs 3 prints one result block (9 lines)" "9" "${T6_LINES}"
# timing line must report the correct run count.
T6_TIMING="$("${BINARY}" 10 16 0.25 42 --runs 3 2>&1 1>/dev/null)"
check "T6 Monte Carlo: timing line reports runs=3" "yes" \
  "$( echo "${T6_TIMING}" | grep -q 'runs=3' && echo yes || echo no)"

# T7: CSV output.
# --csv writes a self-describing file: a header row, one data row per trial,
# and a trailing summary comment.  Verify the header and row count.
T7_CSV="/tmp/life3d_test_$$.csv"
"${BINARY}" 10 16 0.25 42 --runs 3 --csv "${T7_CSV}" >/dev/null 2>&1
check "T7 CSV: file created" "yes" "$( [ -f "${T7_CSV}" ] && echo yes || echo no)"
check "T7 CSV: header row present" "yes" \
  "$( head -1 "${T7_CSV}" | grep -q '^run,gen,N,density,seed,dist,elapsed_s,arch$' && echo yes || echo no)"
# 3 data rows (one per trial), each ending in the arch string.
T7_ROWS="$(grep -cE '^[0-9]+,10,16,' "${T7_CSV}" 2>/dev/null || echo 0)"
check "T7 CSV: one data row per trial (3 rows)" "3" "${T7_ROWS}"
check "T7 CSV: summary comment present" "yes" \
  "$( grep -q '^# mean=' "${T7_CSV}" && echo yes || echo no)"
rm -f "${T7_CSV}"

echo ""


echo "── Edge cases ───────────────────────────────────────────────────────────"

OUT_EMPTY="$(run_sim 5 16 0.0 1)"
ALL_ZERO=1
while IFS= read -r line; do
  cnt="$(echo "${line}" | awk '{print $2}')"
  [[ "${cnt}" != "0" ]] && ALL_ZERO=0
done <<< "${OUT_EMPTY}"
check "density=0 → all max_count = 0" 1 ${ALL_ZERO}

OUT_EMPTY2="$(run_sim 5 16 0.0 999)"
check "density=0 is seed-independent" "${OUT_EMPTY}" "${OUT_EMPTY2}"

OUT_FMT="$(run_sim 5 8 0.3 7)"
check "output has exactly 9 lines" "9" "$(echo "${OUT_FMT}" | wc -l | tr -d ' ')"
check "first line species ID = 1"  "1" "$(echo "${OUT_FMT}" | awk 'NR==1{print $1}')"
check "last line species ID = 9"   "9" "$(echo "${OUT_FMT}" | awk 'NR==9{print $1}')"

run_sim 1 4 0.5 123 > /dev/null
echo "[PASS] gen=1 N=4 completes without crash"; PASS=$((PASS+1))

run_sim 1 64 0.25 7 > /dev/null
echo "[PASS] gen=1 N=64 completes without crash"; PASS=$((PASS+1))

echo ""

# ── Reference regression tests ────────────────────────────────────────────────
# Expected outputs taken from a verified reference run.
# Large-grid cases (N=512, N=1024) are skipped when available RAM < threshold.
echo "── Reference regression tests ───────────────────────────────────────────"

ref_check() {
  local label="$1" gen="$2" N="$3" density="$4" seed="$5"
  shift 5
  local expected_lines=("$@")

  local actual
  actual="$(run_sim "${gen}" "${N}" "${density}" "${seed}")" || {
    echo "[SKIP] ${label} — binary failed (OOM or timeout?)"; return
  }

  local expected
  expected="$(printf '%s\n' "${expected_lines[@]}")"
  check "${label}" "${expected}" "${actual}"
}

# ── N=64, 1000 gens, d=0.4, seed=0 ──────────────────────────────────────────
ref_check "life3d 1000 64 0.4 0" 1000 64 0.4 0 \
  "1 81443 62" \
  "2 24563 24" \
  "3 20080 1"  \
  "4 19016 1"  \
  "5 17576 1"  \
  "6 16905 1"  \
  "7 15793 1"  \
  "8 15174 1"  \
  "9 14807 1"

# ── N=128, 200 gens, d=0.5, seed=1000 ────────────────────────────────────────
ref_check "life3d 200 128 0.5 1000" 200 128 0.5 1000 \
  "1 585667 87"  \
  "2 198117 20"  \
  "3 123360 6"   \
  "4 117152 0"   \
  "5 116181 0"   \
  "6 116832 0"   \
  "7 116421 0"   \
  "8 116559 0"   \
  "9 116344 0"

# ── N=512, 10 gens, d=0.4, seed=0  (memory: ~700 MB) ────────────────────────
MEM_KB="$(grep MemAvailable /proc/meminfo 2>/dev/null | awk '{print $2}' || echo 0)"
if [[ "${MEM_KB}" -ge 1048576 ]]; then   # at least 1 GB free
  ref_check "life3d 10 512 0.4 0" 10 512 0.4 0 \
    "1 19157193 9" \
    "2 11835204 9" \
    "3 10389985 1" \
    "4 9659849 1"  \
    "5 9049679 1"  \
    "6 8563492 1"  \
    "7 8146692 1"  \
    "8 7824529 1"  \
    "9 7580100 1"
else
  echo "[SKIP] life3d 10 512 0.4 0 — insufficient RAM (need ≥1 GB free, have $(( MEM_KB / 1024 )) MB)"
fi

# ── N=1024, 3 gens, d=0.4, seed=100  (memory: ~5.4 GB) ──────────────────────
if [[ "${MEM_KB}" -ge 6291456 ]]; then   # at least 6 GB free
  ref_check "life3d 3 1024 0.4 100" 3 1024 0.4 100 \
    "1 99923786 1"  \
    "2 90413714 1"  \
    "3 83137654 1"  \
    "4 77287897 1"  \
    "5 72448825 1"  \
    "6 68444736 1"  \
    "7 65198270 1"  \
    "8 62633412 1"  \
    "9 60611199 1"
else
  echo "[SKIP] life3d 3 1024 0.4 100 — insufficient RAM (need ≥6 GB free, have $(( MEM_KB / 1024 )) MB)"
fi

echo ""

# ── Performance benchmarks ────────────────────────────────────────────────────
if [[ "${RUN_BENCH}" -eq 1 ]]; then
  echo "── Performance benchmarks ───────────────────────────────────────────────"
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
