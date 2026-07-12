#!/usr/bin/env bash
# benchgate.sh — benchmark-gated merge harness for life3d.
#
# Decides MERGE / NO-MERGE for a performance change by building a baseline
# git ref and a candidate git ref into separate build directories (via
# temporary git worktrees — the caller's working tree and ./build are never
# touched), running the correctness suite (test.sh) against the candidate as
# a hard gate, then running a fixed dense/sparse benchmark matrix (mirroring
# test.sh --bench) against both refs, pooling samples across --repeats
# repetitions, and handing the pooled per-case samples to scripts/benchgate.py
# for the statistical verdict (Welch z-test per case + aggregate; see that
# file's docstring for the exact rule and threshold).
#
# A case counts as a regression / improvement only when it clears BOTH a
# significance hurdle (Welch p < 0.05) AND a practical-size hurdle (|mean
# delta| > 5% of the baseline mean, the documented noise floor) — so machine
# jitter on identical binaries reads as neutral, never a false regression.
#
# Usage:
#   ./scripts/benchgate.sh [--baseline REF] [--candidate REF]
#                           [--repeats K] [--runs N] [--matrix SPEC]
#                           [--margin FRACTION]
#
#   --baseline REF   git ref to compare against (default: main)
#   --candidate REF  git ref being evaluated (default: current HEAD)
#   --repeats K      number of independent baseline-vs-candidate comparison
#                     rounds per case; samples are pooled across them
#                     (default: 3)
#   --runs N         Monte Carlo trials per life3d invocation, i.e. the
#                     --runs flag passed through to life3d (default: 30)
#   --matrix SPEC    override the benchmark matrix. ';'-separated list of
#                     'label:gen:N:density:seed' entries, e.g.
#                     'dense_N64:50:64:0.25:1;sparse_N64:200:64:0.08:1'
#   --margin FRAC    practical noise-floor margin as a fraction of the
#                     baseline mean (default: 0.05 = 5%)
#
# Exit codes: 0 = MERGE, 1 = NO-MERGE (correctness failure or regression),
#             2 = NO-MERGE (neutral — no significant win, but no regression).

set -euo pipefail

BASELINE_REF="main"
CANDIDATE_REF="HEAD"
REPEATS=3
RUNS=30
MATRIX_OVERRIDE=""
MARGIN=""

usage() {
  sed -n '2,38p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --baseline)  BASELINE_REF="$2";  shift 2 ;;
    --candidate) CANDIDATE_REF="$2"; shift 2 ;;
    --repeats)   REPEATS="$2";       shift 2 ;;
    --runs)      RUNS="$2";          shift 2 ;;
    --matrix)    MATRIX_OVERRIDE="$2"; shift 2 ;;
    --margin)    MARGIN="$2";        shift 2 ;;
    -h|--help)   usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 1 ;;
  esac
done

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Default benchmark matrix: mirrors test.sh --bench's dense (d=0.25) and
# sparse (d=0.08) cases, at two grid sizes each, kept small enough that a
# --repeats 3 --runs 30 run finishes in a few minutes.
DEFAULT_MATRIX=(
  "dense_N64:50:64:0.25:1"
  "dense_N128:50:128:0.25:1"
  "sparse_N64:200:64:0.08:1"
  "sparse_N128:200:128:0.08:1"
)

if [[ -n "${MATRIX_OVERRIDE}" ]]; then
  IFS=';' read -r -a CASES <<< "${MATRIX_OVERRIDE}"
else
  CASES=("${DEFAULT_MATRIX[@]}")
fi

BASELINE_SHA="$(git -C "${ROOT_DIR}" rev-parse "${BASELINE_REF}")"
CANDIDATE_SHA="$(git -C "${ROOT_DIR}" rev-parse "${CANDIDATE_REF}")"

WORK_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/benchgate.XXXXXX")"

cleanup() {
  set +e
  git -C "${ROOT_DIR}" worktree remove --force "${WORK_ROOT}/baseline" >/dev/null 2>&1
  git -C "${ROOT_DIR}" worktree remove --force "${WORK_ROOT}/candidate" >/dev/null 2>&1
  git -C "${ROOT_DIR}" worktree prune >/dev/null 2>&1
  rm -rf "${WORK_ROOT}"
}
trap cleanup EXIT

echo "[benchgate] baseline=${BASELINE_REF} (${BASELINE_SHA:0:12})"
echo "[benchgate] candidate=${CANDIDATE_REF} (${CANDIDATE_SHA:0:12})"
echo "[benchgate] work dir=${WORK_ROOT}"
echo ""

setup_ref() {
  local sha="$1" name="$2"
  local wt_dir="${WORK_ROOT}/${name}"
  echo "[benchgate] checking out ${name} (${sha:0:12}) -> ${wt_dir}" >&2
  git -C "${ROOT_DIR}" worktree add --detach --quiet "${wt_dir}" "${sha}" >&2
  echo "[benchgate] building ${name} (Release)..." >&2
  ( cd "${wt_dir}" && ./build.sh --type Release >&2 )
  echo "${wt_dir}/build/life3d"
}

BASELINE_BIN="$(setup_ref "${BASELINE_SHA}" baseline)"
CANDIDATE_BIN="$(setup_ref "${CANDIDATE_SHA}" candidate)"
CANDIDATE_WT="${WORK_ROOT}/candidate"

echo ""
echo "[benchgate] ── correctness gate (candidate) ──────────────────────────"
if ! "${CANDIDATE_WT}/test.sh" --binary "${CANDIDATE_BIN}"; then
  echo ""
  echo "VERDICT: NO-MERGE — candidate failed test.sh (correctness beats speed)"
  exit 1
fi
echo ""

echo "[benchgate] ── benchmark matrix (repeats=${REPEATS} runs=${RUNS}) ────"
DATA_DIR="${WORK_ROOT}/data"
mkdir -p "${DATA_DIR}"

# Interleave baseline and candidate at the granularity of a single repeat, so
# each baseline sample and the candidate sample it is compared against are
# collected close together in time. This cancels slow drift in machine load
# (background jobs, turbo/thermal state) that would otherwise bias a
# "run all baseline, then all candidate" schedule.
#
# ABBA ordering: the two refs in a pair are always run back-to-back, but which
# runs *first* alternates with the repeat index (odd repeat: baseline then
# candidate; even repeat: candidate then baseline). Whatever consistent
# first-vs-second position effect exists — CPU frequency ramp, cache warmth,
# the second short process paying a different startup cost — then falls on
# baseline and candidate equally instead of always penalising the same ref,
# which is what manufactures a false regression on very short cases. Pooling
# across repeats then absorbs the fast per-run jitter.
CASE_LABELS=()
for case in "${CASES[@]}"; do
  IFS=':' read -r label gen N density seed <<< "${case}"
  CASE_LABELS+=("${label}")
  mkdir -p "${DATA_DIR}/baseline/${label}" "${DATA_DIR}/candidate/${label}"
done

run_one() {
  # run_one <which> <label> <gen> <N> <density> <seed> <k>
  local which="$1" label="$2" gen="$3" N="$4" density="$5" seed="$6" k="$7" bin
  if [[ "${which}" == "baseline" ]]; then bin="${BASELINE_BIN}"; else bin="${CANDIDATE_BIN}"; fi
  echo "  repeat ${k}/${REPEATS} [${which}] ${label} (gen=${gen} N=${N} d=${density} seed=${seed})"
  "${bin}" "${gen}" "${N}" "${density}" "${seed}" --runs "${RUNS}" \
    --csv "${DATA_DIR}/${which}/${label}/repeat${k}.csv" >/dev/null 2>&1
}

for ((k = 1; k <= REPEATS; k++)); do
  for case in "${CASES[@]}"; do
    IFS=':' read -r label gen N density seed <<< "${case}"
    if (( k % 2 == 1 )); then
      run_one baseline  "${label}" "${gen}" "${N}" "${density}" "${seed}" "${k}"
      run_one candidate "${label}" "${gen}" "${N}" "${density}" "${seed}" "${k}"
    else
      run_one candidate "${label}" "${gen}" "${N}" "${density}" "${seed}" "${k}"
      run_one baseline  "${label}" "${gen}" "${N}" "${density}" "${seed}" "${k}"
    fi
  done
done

echo ""
echo "[benchgate] ── verdict ─────────────────────────────────────────────"
CASES_CSV="$(IFS=,; echo "${CASE_LABELS[*]}")"

MARGIN_ARG=()
if [[ -n "${MARGIN}" ]]; then MARGIN_ARG=(--margin "${MARGIN}"); fi

set +e
python3 "${ROOT_DIR}/scripts/benchgate.py" \
  --data-dir "${DATA_DIR}" \
  --cases "${CASES_CSV}" \
  --repeats "${REPEATS}" \
  "${MARGIN_ARG[@]}"
PY_EXIT=$?
set -e

exit "${PY_EXIT}"
