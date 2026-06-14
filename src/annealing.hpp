#pragma once
// annealing.hpp – C++23
//
// Simulated-annealing controller for the dispatch thresholds.
//
// Motivation
// ──────────
// The dense/sparse dispatch is gated by four thresholds (DispatchThresholds).
// A single static setting is a compromise: thresholds tuned for an early,
// chaotic, churn-heavy state are too eager to fall back to the expensive dense
// path once the grid stabilises, and vice-versa.
//
// This controller adapts the thresholds each generation from two signals:
//
//   • A *chaos metric* — an exponential moving average (EMA) of the per-step
//     change_ratio.  High when the system is turbulent, low when it settles.
//
//   • A *cooling temperature* — an exponentially-decaying schedule
//         T(gen) = clamp(t_initial · exp(−cooling_rate · gen), t_final, t_initial)
//     that shrinks the controller's influence as the run progresses, so late
//     generations converge to the base thresholds.
//
// The adjustment widens or narrows the sparse-entry band:
//
//   enter_sparse(gen) = base.enter_sparse + T(gen) · chaos
//   exit_sparse(gen)  = base.exit_sparse  − T(gen) · chaos
//
// When chaos is high the band shifts so the system is more willing to stay
// dense (safe, never skips work it shouldn't); when chaos is low the band
// relaxes so the system stays sparse longer (cheap).  The churn thresholds are
// shifted analogously.  Results are always clamped so enter ≤ exit.
//
// The controller is a small, self-contained value type with one method,
// step(), called once per generation.  It owns no grid state.

#include "params.hpp"

#include <cstdint>

class AnnealingController
{
public:
  AnnealingController(const AnnealingParams&    params,
                      const DispatchThresholds& base_thresholds) noexcept;

  // Per-generation observation fed to step().  Bundled into a struct (rather
  // than two adjacent scalar parameters) so the float change_ratio and the
  // std::uint32_t generation index cannot be swapped at a call site.
  struct Observation
  {
    float         change_ratio; // fraction of cells that changed last step
    std::uint32_t generation;   // 0-based generation index
  };

  // Advance one generation.  Returns the thresholds to use for this generation.
  [[nodiscard]] DispatchThresholds step(const Observation& obs) noexcept;

  // Current chaos metric (EMA of change_ratio) — exposed for diagnostics.
  [[nodiscard]] float chaos() const noexcept
  {
    return ema_chaos_;
  }

private:
  AnnealingParams    params_;
  DispatchThresholds base_;
  float              ema_chaos_{0.0f};
  float              ema_alpha_; // smoothing factor 2/(window+1)
  bool               primed_{false};
};
