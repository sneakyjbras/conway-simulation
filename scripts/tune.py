#!/usr/bin/env python3
"""tune.py — parameter sweep for the life3d serial simulation.

Purpose
-------
This script is the primary tool for discovering optimal configuration values
for the adaptive dispatch thresholds, the scratch-vector reserve factors, and
the simulated-annealing schedule.  It runs the compiled `life3d` binary across
a Cartesian product of candidate configurations, captures the Monte Carlo
timing summary each one writes to its CSV, and ranks the configurations by mean
wall-clock time.

Statistics are the whole point: a single timing run is dominated by noise, so
every configuration is run with --runs N and compared on its mean (with min and
max retained to judge spread).  The fastest configurations printed at the end
are the candidates whose defaults belong in src/params.hpp.

Run it after any architectural change to re-calibrate, since the optimal
thresholds depend on cache sizes, SIMD width, and core layout.

Usage
-----
    python3 scripts/tune.py [--binary PATH] [--runs N] [--out CSV]
                            [--gen G] [--grid N] [--density D] [--seed S]
                            [--quick]

Only the standard library is required to run the sweep itself.  pandas is used
for the final ranking table if available; a plain-text fallback is used if not.
"""

from __future__ import annotations

import argparse
import itertools
import os
import re
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path

# ── Sweep grid ────────────────────────────────────────────────────────────────
# Each axis lists the candidate values for one tunable.  The full sweep is the
# Cartesian product; use --quick to collapse to a single representative value
# per axis for a fast smoke test.

ENTER_SPARSE = [0.30, 0.40, 0.50, 0.60]
EXIT_SPARSE = [0.55, 0.65, 0.75]
ENTER_CHURN = [0.02, 0.05, 0.10]
EXIT_CHURN = [0.05, 0.10, 0.20]
UNIFORM_FACTOR = [1.0, 1.5, 2.0]
SA_ENABLED = [False, True]

QUICK_OVERRIDES = {
    "enter_sparse": [0.50],
    "exit_sparse": [0.65],
    "enter_churn": [0.05],
    "exit_churn": [0.10],
    "uniform_factor": [1.5],
    "sa_enabled": [False, True],
}


@dataclass(frozen=True)
class Config:
    enter_sparse: float
    exit_sparse: float
    enter_churn: float
    exit_churn: float
    uniform_factor: float
    sa_enabled: bool

    def to_flags(self) -> list[str]:
        flags = [
            "--enter-sparse", f"{self.enter_sparse}",
            "--exit-sparse", f"{self.exit_sparse}",
            "--enter-churn", f"{self.enter_churn}",
            "--exit-churn", f"{self.exit_churn}",
            "--uniform-factor", f"{self.uniform_factor}",
        ]
        if self.sa_enabled:
            flags.append("--sa")
        return flags

    def label(self) -> str:
        sa = "sa" if self.sa_enabled else "no-sa"
        return (f"es={self.enter_sparse} xs={self.exit_sparse} "
                f"ec={self.enter_churn} xc={self.exit_churn} "
                f"uf={self.uniform_factor} {sa}")


@dataclass
class Result:
    config: Config
    mean_s: float
    min_s: float
    max_s: float
    std_dev_s: float
    arch: str = ""


# Parses the trailing summary comment written by Runner::write_csv, e.g.:
#   # mean=0.002707 min=0.002509 max=0.002832 std_dev=0.000141 runs=3 arch=x86_64
SUMMARY_RE = re.compile(
    r"#\s*mean=(?P<mean>[0-9.eE+-]+)\s+min=(?P<min>[0-9.eE+-]+)\s+"
    r"max=(?P<max>[0-9.eE+-]+)\s+std_dev=(?P<std>[0-9.eE+-]+)\s+"
    r"runs=(?P<runs>[0-9]+)\s+arch=(?P<arch>\S+)"
)


def build_grid(quick: bool) -> list[Config]:
    axes = {
        "enter_sparse": ENTER_SPARSE,
        "exit_sparse": EXIT_SPARSE,
        "enter_churn": ENTER_CHURN,
        "exit_churn": EXIT_CHURN,
        "uniform_factor": UNIFORM_FACTOR,
        "sa_enabled": SA_ENABLED,
    }
    if quick:
        axes = {**axes, **QUICK_OVERRIDES}

    configs: list[Config] = []
    for combo in itertools.product(*axes.values()):
        c = Config(**dict(zip(axes.keys(), combo)))
        # Skip ill-formed gates where enter would exceed exit.
        if c.enter_sparse > c.exit_sparse or c.enter_churn > c.exit_churn:
            continue
        configs.append(c)
    return configs


def parse_summary(csv_path: Path) -> tuple[float, float, float, float, str] | None:
    try:
        text = csv_path.read_text(encoding="utf-8")
    except OSError:
        return None
    for line in reversed(text.splitlines()):
        m = SUMMARY_RE.search(line)
        if m:
            return (
                float(m["mean"]),
                float(m["min"]),
                float(m["max"]),
                float(m["std"]),
                m["arch"],
            )
    return None


def run_config(binary: str, cfg: Config, args: argparse.Namespace,
               tmp_dir: Path) -> Result | None:
    csv_path = tmp_dir / "sweep_run.csv"
    cmd = [
        binary,
        str(args.gen), str(args.grid), str(args.density), str(args.seed),
        "--runs", str(args.runs),
        "--csv", str(csv_path),
        *cfg.to_flags(),
    ]
    try:
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, timeout=args.timeout)
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as exc:
        print(f"  warning: config failed ({cfg.label()}): {exc}", file=sys.stderr)
        return None

    parsed = parse_summary(csv_path)
    if parsed is None:
        print(f"  warning: no summary for ({cfg.label()})", file=sys.stderr)
        return None

    mean_s, min_s, max_s, std_s, arch = parsed
    return Result(config=cfg, mean_s=mean_s, min_s=min_s, max_s=max_s,
                  std_dev_s=std_s, arch=arch)


def write_results_csv(results: list[Result], out_path: Path) -> None:
    lines = ["enter_sparse,exit_sparse,enter_churn,exit_churn,"
             "uniform_factor,sa,mean_s,min_s,max_s,std_dev_s,arch"]
    for r in sorted(results, key=lambda x: x.mean_s):
        c = r.config
        lines.append(
            f"{c.enter_sparse},{c.exit_sparse},{c.enter_churn},{c.exit_churn},"
            f"{c.uniform_factor},{int(c.sa_enabled)},"
            f"{r.mean_s:.6f},{r.min_s:.6f},{r.max_s:.6f},{r.std_dev_s:.6f},{r.arch}"
        )
    out_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def print_top(results: list[Result], n: int = 5) -> None:
    ranked = sorted(results, key=lambda x: x.mean_s)
    print(f"\nTop {min(n, len(ranked))} configurations by mean wall-clock:")
    print(f"{'rank':>4}  {'mean(s)':>10}  {'min(s)':>10}  {'max(s)':>10}  config")
    for i, r in enumerate(ranked[:n], start=1):
        print(f"{i:>4}  {r.mean_s:>10.6f}  {r.min_s:>10.6f}  "
              f"{r.max_s:>10.6f}  {r.config.label()}")


def main() -> int:
    default_binary = str(Path(__file__).resolve().parent.parent / "build" / "life3d")

    p = argparse.ArgumentParser(description="Parameter sweep for life3d.")
    p.add_argument("--binary", default=default_binary, help="path to life3d binary")
    p.add_argument("--runs", type=int, default=5, help="Monte Carlo trials per config")
    p.add_argument("--out", default="sweep_results.csv", help="output ranking CSV")
    p.add_argument("--gen", type=int, default=100, help="generations")
    p.add_argument("--grid", type=int, default=64, help="grid dimension N")
    p.add_argument("--density", type=float, default=0.08, help="initial density")
    p.add_argument("--seed", type=int, default=42, help="RNG seed")
    p.add_argument("--timeout", type=float, default=120.0, help="per-config timeout (s)")
    p.add_argument("--quick", action="store_true",
                   help="collapse the grid to a fast smoke test")
    args = p.parse_args()

    if not os.access(args.binary, os.X_OK):
        print(f"error: binary not executable: {args.binary}", file=sys.stderr)
        print("       build it first with ./build.sh --type Release", file=sys.stderr)
        return 1

    configs = build_grid(args.quick)
    print(f"Sweeping {len(configs)} configurations "
          f"({args.runs} trials each) on "
          f"gen={args.gen} N={args.grid} density={args.density} seed={args.seed}")

    results: list[Result] = []
    with tempfile.TemporaryDirectory() as td:
        tmp_dir = Path(td)
        for i, cfg in enumerate(configs, start=1):
            print(f"[{i}/{len(configs)}] {cfg.label()}", flush=True)
            res = run_config(args.binary, cfg, args, tmp_dir)
            if res is not None:
                results.append(res)

    if not results:
        print("error: no successful configurations", file=sys.stderr)
        return 1

    out_path = Path(args.out)
    write_results_csv(results, out_path)
    print(f"\nWrote {len(results)} results to {out_path}")
    print_top(results, n=5)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
