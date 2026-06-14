#pragma once
// runner.hpp – C++23
//
// Monte Carlo driver: runs the simulation N times, times each trial, and
// aggregates the wall-clock statistics.
//
// Why statistics matter here
// ──────────────────────────
// A single timing sample is almost worthless for an optimisation project: it
// conflates the quantity of interest (the algorithm's cost) with scheduler
// jitter, cache warmth, turbo-frequency drift, and co-tenant noise.  The mean
// over several trials estimates the central cost; the min approximates the
// "clean" run with least interference; the max exposes worst-case stalls; and
// the spread (max − min, std-dev) tells us how much to trust the mean.  These
// four numbers, recorded per configuration in CSV, are the raw material for
// deciding which optimisation actually helped and which was within noise.
//
// The Runner is deliberately the *only* component that touches timing.  The
// Simulation class stays a pure computation engine with no clock calls, so
// timing concerns never leak into the hot path.  Timing uses omp_get_wtime()
// when OpenMP is available (high-resolution, monotonic) and falls back to
// std::chrono::steady_clock otherwise — important for ARM cross-compiles where
// the OpenMP runtime may be absent.

#include "params.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Aggregate timing statistics over a Monte Carlo batch.
struct RunStats
{
  double        mean_s{0.0};
  double        min_s{0.0};
  double        max_s{0.0};
  double        std_dev_s{0.0};
  std::string   arch; // "x86_64", "aarch64", "arm", or "unknown"
  std::uint32_t runs{0};
};

class Runner
{
public:
  // Takes the params by value so callers can std::move into it.
  explicit Runner(SimulationParams params) noexcept;

  // Execute params_.runs independent simulations, timing each.  Writes a CSV
  // if params_.csv_path is non-empty.  Prints the final run's species maxima
  // to stdout (preserving the single-run output contract).  Returns the
  // aggregate statistics.
  [[nodiscard]] RunStats execute();

private:
  SimulationParams params_;

  // Compile-time host architecture string.
  [[nodiscard]] static std::string detect_arch() noexcept;

  // High-resolution wall clock (omp_get_wtime when available).
  [[nodiscard]] static double now_seconds() noexcept;

  // Write one row per trial plus a summary comment line.
  void
  write_csv(const std::vector<double>& times, const RunStats& stats, std::string_view path) const;
};
