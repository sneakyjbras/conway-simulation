#!/usr/bin/env python3
"""benchgate.py — statistics engine for scripts/benchgate.sh.

Consumes the Monte Carlo CSV files that ``life3d --runs N --csv PATH``
produces (one row per trial, columns:
``run,gen,N,density,seed,dist,elapsed_s,arch`` — see scripts/tune.py's
SUMMARY_RE for the trailing comment format this mirrors) and decides
MERGE / NO-MERGE for a baseline-vs-candidate performance comparison.

Statistical rule
-----------------
For each benchmark case we pool every individual trial's ``elapsed_s``
across all ``--repeats`` invocations (so a single noisy repeat cannot
swing the verdict) and compare baseline vs. candidate with a two-sample
Welch test (unequal variances, no assumption of equal sample size).  The
test statistic

    z = (mean_candidate - mean_baseline) / sqrt(var_b/n_b + var_c/n_c)

is treated as approximately standard-normal (n = runs * repeats is large
by default, so the normal approximation to Welch's t is accurate; no
third-party stats package is required — stdlib ``math.erf`` gives the
normal CDF).

Two hurdles, not one
--------------------
Statistical significance alone is the wrong gate for a timing harness:
with a few hundred pooled samples, Welch will happily flag a 2% wobble
that is pure machine jitter (background load, turbo/thermal drift, cache
state) — so two *identical* binaries can "regress". We therefore require
a change to clear BOTH hurdles before it counts:

  1. significance  : two-sided p < ALPHA (default 0.05), AND
  2. practical size: |mean delta| as a fraction of the baseline mean
                     exceeds MARGIN (default 0.05 = 5%).

MARGIN is the documented noise floor. A case is a *regression* only when
the candidate is slower AND both hurdles are cleared; an *improvement*
only when it is faster AND both are cleared; everything in between —
including the small, significant-but-tiny wobbles that identical binaries
produce — is *neutral*.

Aggregate axis
--------------
For the overall verdict we re-express every sample as a fraction of its
own case's baseline mean (``x / mean_baseline_case``) and pool those
normalized samples across *all* cases into one aggregate baseline array
and one aggregate candidate array. Comparing those two arrays with the
same two-hurdle rule gives one scale-free aggregate decision (a straight
pooled mean would let a slow-but-small case be swamped by a fast-but-large
one).

VERDICT = MERGE  iff  (a) no case is a regression (both hurdles), AND
                       (b) the aggregate is a significant improvement that
                           also clears MARGIN.
Otherwise VERDICT = NO-MERGE:
  - if any case regressed, that case is named as the cause (exit 1);
  - else the change is a wash / not a measurable win — neutral (exit 2).

Exit codes: 0 = MERGE, 1 = NO-MERGE (regression), 2 = NO-MERGE (neutral).
"""

from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

ALPHA_DEFAULT = 0.05
MARGIN_DEFAULT = 0.05


def read_elapsed(csv_path: Path) -> list[float]:
    """Read the elapsed_s column from a life3d --csv output file."""
    vals: list[float] = []
    if not csv_path.exists():
        return vals
    with csv_path.open(encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or line.startswith("run,"):
                continue
            parts = line.split(",")
            if len(parts) < 7:
                continue
            try:
                vals.append(float(parts[6]))
            except ValueError:
                continue
    return vals


def mean(xs: list[float]) -> float:
    return sum(xs) / len(xs)


def variance(xs: list[float], m: float | None = None) -> float:
    m = mean(xs) if m is None else m
    n = len(xs)
    if n < 2:
        return 0.0
    return sum((x - m) ** 2 for x in xs) / (n - 1)


def norm_cdf(x: float) -> float:
    return 0.5 * (1.0 + math.erf(x / math.sqrt(2.0)))


def welch_z(baseline: list[float], candidate: list[float]) -> tuple[float, float, float, float, float]:
    """Welch z-statistic for candidate - baseline. Positive z = candidate slower."""
    mb, mc = mean(baseline), mean(candidate)
    vb, vc = variance(baseline, mb), variance(candidate, mc)
    nb, nc = len(baseline), len(candidate)
    se = math.sqrt(vb / nb + vc / nc) if nb > 0 and nc > 0 else float("inf")
    z = (mc - mb) / se if se > 0 else 0.0
    return z, mb, mc, math.sqrt(vb), math.sqrt(vc)


def p_value(z: float) -> float:
    return 2.0 * (1.0 - norm_cdf(abs(z)))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--data-dir", required=True, help="dir containing baseline/<case>/repeatK.csv and candidate/<case>/repeatK.csv")
    ap.add_argument("--cases", required=True, help="comma-separated case labels")
    ap.add_argument("--repeats", type=int, required=True, help="number of repeats performed")
    ap.add_argument("--alpha", type=float, default=ALPHA_DEFAULT, help="two-sided significance threshold")
    ap.add_argument("--margin", type=float, default=MARGIN_DEFAULT,
                    help="practical effect-size margin (fraction of baseline mean) a "
                         "change must exceed on top of significance; the noise floor")
    args = ap.parse_args()

    data_dir = Path(args.data_dir)
    cases = [c for c in args.cases.split(",") if c]
    alpha = args.alpha
    margin_pct = args.margin * 100.0

    agg_baseline_norm: list[float] = []
    agg_candidate_norm: list[float] = []
    regressions: list[str] = []
    improvements: list[str] = []

    print(f"[rule] regression/improvement requires p<{alpha} AND |delta| > {margin_pct:.1f}% (noise floor)")
    print(f"{'case':<16} {'baseline mean±std (s)':<26} {'candidate mean±std (s)':<26} {'%Δ':>8}  {'p':>8}  verdict")
    print("-" * 100)

    for case in cases:
        baseline_samples: list[float] = []
        candidate_samples: list[float] = []
        for k in range(1, args.repeats + 1):
            baseline_samples += read_elapsed(data_dir / "baseline" / case / f"repeat{k}.csv")
            candidate_samples += read_elapsed(data_dir / "candidate" / case / f"repeat{k}.csv")

        if not baseline_samples or not candidate_samples:
            print(f"{case:<16} NO DATA (baseline={len(baseline_samples)} candidate={len(candidate_samples)} samples)")
            regressions.append(f"{case} (missing data)")
            continue

        z, mb, mc, sb, sc = welch_z(baseline_samples, candidate_samples)
        p = p_value(z)
        pct_delta = (mc - mb) / mb * 100.0 if mb else 0.0

        significant = p < alpha
        clears_margin = abs(pct_delta) > margin_pct
        if significant and clears_margin and pct_delta > 0:
            verdict = "REGRESSION"
            regressions.append(case)
        elif significant and clears_margin and pct_delta < 0:
            verdict = "improvement"
            improvements.append(case)
        else:
            verdict = "neutral"

        print(
            f"{case:<16} {mb:.6f} ± {sb:.6f}          {mc:.6f} ± {sc:.6f}          "
            f"{pct_delta:+7.2f}%  {p:8.4f}  {verdict}"
        )

        # normalize by the baseline case mean so cases of very different
        # magnitude can be pooled onto one common scale for the aggregate test.
        agg_baseline_norm += [x / mb for x in baseline_samples]
        agg_candidate_norm += [x / mb for x in candidate_samples]

    print("-" * 100)

    if not agg_baseline_norm or not agg_candidate_norm:
        print("VERDICT: NO-MERGE (no benchmark data collected)")
        return 2

    agg_z, agg_mb, agg_mc, agg_sb, agg_sc = welch_z(agg_baseline_norm, agg_candidate_norm)
    agg_p = p_value(agg_z)
    agg_pct = (agg_mc - agg_mb) / agg_mb * 100.0

    print(
        f"Aggregate (pooled, normalized to per-case baseline mean): "
        f"baseline={agg_mb:.4f} candidate={agg_mc:.4f} delta={agg_pct:+.2f}% "
        f"z={agg_z:+.3f} p={agg_p:.4f} (alpha={alpha})"
    )

    if regressions:
        print(f"VERDICT: NO-MERGE — regression (p<{alpha} and delta>{margin_pct:.1f}%) in: {', '.join(regressions)}")
        return 1

    if agg_p < alpha and abs(agg_pct) > margin_pct and agg_z < 0:
        extra = f" (also per-case improvement in: {', '.join(improvements)})" if improvements else ""
        print(f"VERDICT: MERGE — aggregate improvement clears both hurdles "
              f"(p={agg_p:.4f} < {alpha}, delta={agg_pct:+.2f}% beyond {margin_pct:.1f}% margin){extra}")
        return 0

    print(
        f"VERDICT: NO-MERGE (neutral) — no case regressed, but the aggregate change "
        f"is not a significant improvement beyond the {margin_pct:.1f}% margin "
        f"(delta={agg_pct:+.2f}%, p={agg_p:.4f})"
    )
    return 2


if __name__ == "__main__":
    sys.exit(main())
