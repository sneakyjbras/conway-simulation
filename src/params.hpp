#pragma once
// params.hpp – C++23
//
// Configuration data classes for the simulation.
//
// Design rationale
// ────────────────
// Configuration was previously split across three places: the four positional
// CLI arguments, four `static constexpr` threshold fields inside Simulation,
// and ad-hoc constructor parameters.  This header consolidates everything into
// a small set of plain aggregate structs with sensible defaults, so that:
//
//   • Optional tuning knobs need not be threaded through the bash scripts —
//     callers override only what they care about; the rest fall back to the
//     defaults below.
//   • The dispatch thresholds become *runtime* state (not compile-time
//     constants), which is what lets the simulated-annealing controller in
//     annealing.hpp adjust them per generation.
//   • The Python parameter sweep (scripts/tune.py) can drive every tunable
//     value from the command line.
//
// These are intentionally trivial aggregates (no invariants, no methods):
// they are passed by const-reference into Simulation and copied cheaply.

#include <cstdint>
#include <string>

// ── Problem dimensions ──────────────────────────────────────────────────────
// The fixed, deterministic size of the computation itself.  Unlike the scratch
// vectors, these do not depend on the state distribution — for a given N the
// problem volume is always N³.
struct ProblemSize
{
  std::uint32_t grid_dimension{0}; // N — logical cells per axis
  std::uint32_t generations{0};    // number of generations to simulate
};

// ── Initial-state distribution ──────────────────────────────────────────────
// Controls how the generator seeds the initial grid, and (via AllocConfig)
// how much headroom the scratch vectors reserve.  A uniform fill spreads alive
// cells evenly; a Gaussian fill clusters them, producing higher local churn
// and therefore needing a larger scratch reserve.
enum class DistributionType : std::uint8_t
{
  kUniform  = 0,
  kGaussian = 1,
};

// ── Scratch-vector reserve policy ───────────────────────────────────────────
// The scratch vectors (changes_, dirty_indices_) hold transient per-step data
// whose size tracks the *state distribution*, not the problem volume.  We
// reserve once, up front, to guarantee no reallocation inside run().
//
//   reserved_elements = density × N³ × factor
//
// where `factor` is chosen per distribution.  Uniform fills are predictable
// (≈ density × N³ alive cells) so 1.5× gives comfortable headroom.  Gaussian
// fills are spikier, so 3.0× absorbs the heavier tail.  Both factors are
// tunable by the Python sweep.
struct AllocConfig
{
  float uniform_reserve_factor{1.5f};
  float gaussian_reserve_factor{3.0f};
};

// ── Dispatch thresholds ─────────────────────────────────────────────────────
// Dual-threshold hysteresis gate between the dense O(N³) path and the sparse
// O(dirty) path.  Two independent signals each have an enter/exit pair so the
// mode cannot flip-flop on the boundary:
//
//   dirty_ratio  = dirty_count / N³     (fraction of cells that could change)
//   change_ratio = changes      / N³     (fraction that actually changed)
//
// Enter sparse only when BOTH ratios are low; exit when EITHER is high.
// These are mutable at runtime so the annealing controller can shift them.
struct DispatchThresholds
{
  float enter_sparse{0.50f}; // dirty_ratio ≤ this to enter sparse
  float exit_sparse{0.65f};  // dirty_ratio ≥ this to exit  sparse
  float enter_churn{0.05f};  // change_ratio ≤ this to enter sparse
  float exit_churn{0.10f};   // change_ratio ≥ this to exit  sparse
};

// ── Simulated-annealing parameters ──────────────────────────────────────────
// The system can grow more chaotic or more stable as it evolves.  A static
// threshold tuned for an early chaotic state is too conservative once the grid
// settles.  The annealing controller (annealing.hpp) tracks a chaos metric
// (EMA of change_ratio) and a cooling temperature, and nudges the dispatch
// thresholds each generation: high chaos raises the bar to enter sparse, low
// chaos lowers the bar to stay sparse longer.
//
// Disabled by default — when `enabled == false` the controller is never
// constructed and behaviour is bit-identical to the static-threshold path.
struct AnnealingParams
{
  float         t_initial{0.10f};    // starting temperature
  float         t_final{0.001f};     // temperature floor
  float         cooling_rate{0.05f}; // per-generation exponential decay
  std::uint32_t window_size{10};     // EMA window for the chaos metric
  bool          enabled{false};      // off by default
};

// ── Top-level simulation parameters ─────────────────────────────────────────
// The single object handed to Simulation.  Bundles the required problem
// description with all optional tuning knobs.  `runs`, `csv_path`, and the
// nested tuning structs all carry defaults so a minimal four-argument
// invocation behaves exactly as before.
struct SimulationParams
{
  ProblemSize        problem{};
  float              density{0.0f};
  std::int32_t       seed{0};
  DistributionType   distribution{DistributionType::kUniform};
  AllocConfig        alloc{};
  DispatchThresholds thresholds{};
  AnnealingParams    annealing{};

  // Monte Carlo / output options (consumed by Runner, not Simulation).
  std::uint32_t runs{1};    // number of independent timed trials
  std::string   csv_path{}; // empty → no CSV written
};
