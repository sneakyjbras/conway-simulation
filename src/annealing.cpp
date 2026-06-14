// annealing.cpp – C++23
//
// Implementation of the simulated-annealing threshold controller.
// See annealing.hpp for the design rationale.

#include "annealing.hpp"

#include <algorithm>
#include <cmath>

AnnealingController::AnnealingController(const AnnealingParams&    params,
                                         const DispatchThresholds& base_thresholds) noexcept
  : params_(params)
  , base_(base_thresholds)
  , ema_alpha_(2.0f / (static_cast<float>(std::max<std::uint32_t>(params.window_size, 1u)) + 1.0f))
{
}

DispatchThresholds AnnealingController::step(float change_ratio, std::uint32_t gen) noexcept
{
  // ── Update the chaos metric (EMA of change_ratio) ─────────────────────────
  // Prime the average on the first observation so it starts at the true value
  // rather than decaying up from zero.
  if (!primed_)
  {
    ema_chaos_ = change_ratio;
    primed_    = true;
  }
  else
  {
    ema_chaos_ = ema_alpha_ * change_ratio + (1.0f - ema_alpha_) * ema_chaos_;
  }

  // ── Cooling temperature ───────────────────────────────────────────────────
  // Exponential decay from t_initial, floored at t_final.
  const float raw_temp =
      params_.t_initial * std::exp(-params_.cooling_rate * static_cast<float>(gen));
  const float temperature = std::clamp(raw_temp, params_.t_final, params_.t_initial);

  // ── Threshold adjustment ──────────────────────────────────────────────────
  // The shift magnitude is temperature × chaos.  High chaos + high temperature
  // (early, turbulent) → large shift toward staying dense.  As either decays,
  // the thresholds converge back to the base values.
  const float shift = temperature * ema_chaos_;

  DispatchThresholds out = base_;
  out.enter_sparse       = base_.enter_sparse + shift;
  out.exit_sparse        = base_.exit_sparse - shift;
  out.enter_churn        = base_.enter_churn + shift;
  out.exit_churn         = base_.exit_churn - shift;

  // ── Clamp to keep the gate well-formed ────────────────────────────────────
  // enter must not exceed exit (otherwise the hysteresis band inverts), and
  // every threshold stays within [0, 1].
  out.enter_sparse = std::clamp(out.enter_sparse, 0.0f, 1.0f);
  out.exit_sparse  = std::clamp(out.exit_sparse, 0.0f, 1.0f);
  out.enter_churn  = std::clamp(out.enter_churn, 0.0f, 1.0f);
  out.exit_churn   = std::clamp(out.exit_churn, 0.0f, 1.0f);

  if (out.enter_sparse > out.exit_sparse)
    out.enter_sparse = out.exit_sparse;
  if (out.enter_churn > out.exit_churn)
    out.enter_churn = out.exit_churn;

  return out;
}
