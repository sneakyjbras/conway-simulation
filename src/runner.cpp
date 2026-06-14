// runner.cpp – C++23
//
// Implementation of the Monte Carlo timing driver.  See runner.hpp.

#include "runner.hpp"

#include "simulation.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <utility>

#ifdef _OPENMP
#include <omp.h>
#else
#include <chrono>
#endif

Runner::Runner(SimulationParams params) noexcept
  : params_(std::move(params))
{
}

double Runner::now_seconds() noexcept
{
#ifdef _OPENMP
  return omp_get_wtime();
#else
  // Fallback for builds without the OpenMP runtime (e.g. some ARM toolchains).
  const auto t = std::chrono::steady_clock::now().time_since_epoch();
  return std::chrono::duration<double>(t).count();
#endif
}

std::string Runner::detect_arch() noexcept
{
#if defined(__x86_64__) || defined(_M_X64)
  return "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
  return "aarch64";
#elif defined(__arm__) || defined(_M_ARM)
  return "arm";
#elif defined(__riscv)
  return "riscv";
#else
  return "unknown";
#endif
}

RunStats Runner::execute()
{
  const std::uint32_t n = std::max<std::uint32_t>(params_.runs, 1u);

  std::vector<double> times;
  times.reserve(n);

  // Each trial constructs a fresh Simulation so allocation and initialisation
  // are included in the same controlled way every run.  Only the final trial
  // prints its species maxima, preserving the single-run stdout contract.
  for (std::uint32_t r = 0; r < n; ++r)
  {
    Simulation simulation(params_);
    simulation.set_debug_enabled(false);

    const double t0 = now_seconds();
    simulation.run();
    const double t1 = now_seconds();

    times.push_back(t1 - t0);

    if (r + 1 == n)
      simulation.print_results();
  }

  // ── Aggregate statistics ──────────────────────────────────────────────────
  RunStats stats;
  stats.runs = n;
  stats.arch = detect_arch();

  stats.min_s = *std::min_element(times.begin(), times.end());
  stats.max_s = *std::max_element(times.begin(), times.end());

  const double sum = std::accumulate(times.begin(), times.end(), 0.0);
  stats.mean_s     = sum / static_cast<double>(n);

  // Population standard deviation (N divisor, not N−1): we are describing the
  // sample we have, not inferring a wider population.
  double sq = 0.0;
  for (const double t : times)
  {
    const double d = t - stats.mean_s;
    sq += d * d;
  }
  stats.std_dev_s = std::sqrt(sq / static_cast<double>(n));

  if (!params_.csv_path.empty())
    write_csv(times, stats, params_.csv_path);

  // Human-readable summary to stderr (does not pollute the stdout contract).
  std::fprintf(stderr, "timing: mean=%.6fs min=%.6fs max=%.6fs std=%.6fs runs=%u arch=%s\n",
               stats.mean_s, stats.min_s, stats.max_s, stats.std_dev_s, stats.runs,
               stats.arch.c_str());

  return stats;
}

void Runner::write_csv(const std::vector<double>& times,
                       const RunStats&            stats,
                       std::string_view           path) const
{
  std::FILE* f = std::fopen(std::string{path}.c_str(), "w");
  if (f == nullptr)
  {
    std::fprintf(stderr, "warning: could not open CSV path '%.*s' for writing\n",
                 static_cast<int>(path.size()), path.data());
    return;
  }

  // Header documents every column so the file is self-describing for the
  // Python sweep and any later analysis.
  std::fprintf(f, "run,gen,N,density,seed,dist,elapsed_s,arch\n");

  const char* dist_name =
      (params_.distribution == DistributionType::kGaussian) ? "gauss" : "uniform";

  for (std::size_t i = 0; i < times.size(); ++i)
  {
    std::fprintf(f, "%zu,%u,%u,%.6f,%d,%s,%.6f,%s\n", i, params_.problem.generations,
                 params_.problem.grid_dimension, static_cast<double>(params_.density), params_.seed,
                 dist_name, times[i], stats.arch.c_str());
  }

  // Trailing summary comment — parsed by tune.py to rank configurations.
  std::fprintf(f, "# mean=%.6f min=%.6f max=%.6f std_dev=%.6f runs=%u arch=%s\n", stats.mean_s,
               stats.min_s, stats.max_s, stats.std_dev_s, stats.runs, stats.arch.c_str());

  std::fclose(f);
}
