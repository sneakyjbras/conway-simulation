// simulation.cpp – C++23
//
// Single Responsibility: ORCHESTRATION.
// The generation loop and its control logic — two-mode dispatch, hysteresis,
// the simulated-annealing hook, per-generation maxima tracking, and final
// result output.  Physics kernels live in simulation_step.cpp, one-time setup
// in simulation_init.cpp.
//
// ═══════════════════════════════════════════════════════════════════════════
// Adaptive dirty-set / two-mode dispatch
// ═══════════════════════════════════════════════════════════════════════════
// A cell can only CHANGE if it, or one of its 26 neighbours, is alive.  Cells
// that are dead with zero alive neighbours are provably unchanged and skipped.
// dirty_ratio and change_ratio gate between a SIMD full sweep (dense) and a
// tile-aware skip-friendly sweep (sparse), with a dual-threshold hysteresis
// band to prevent flip-flopping.

#include "simulation.hpp"

#include "debug_printer.hpp"
#include "frame_exporter.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <vector>

namespace
{

constexpr unsigned char DEAD = 0;
constexpr int           N_S  = static_cast<int>(generator::N_SPECIES);

} // namespace

// ── Maxima tracking ───────────────────────────────────────────────────────────

void Simulation::update_maxima_for_generation(
    const std::array<std::uint64_t, generator::N_SPECIES + 1>& counts,
    std::uint64_t                                              gen) noexcept
{
  for (std::size_t s = 1; s <= static_cast<std::size_t>(N_S); ++s)
  {
    if (counts[s] > max_count_[s])
    {
      max_count_[s] = counts[s];
      max_gen_[s]   = gen;
    }
  }
}

// ── Run loop ──────────────────────────────────────────────────────────────────

void Simulation::run()
{
  dirty_set_.rebuild_full(grid_.data());
  tile_set_.rebuild(dirty_set_.dirty_indices());

  // ── Visualization frame export (decision resolved ONCE, here) ────────────
  // Built only when a path was supplied.  For a normal run this optional stays
  // empty, so the loop's per-generation `if (exporter)` is a single predictable
  // not-taken branch — and the hot step kernels are never touched at all.
  std::optional<FrameExporter> exporter;
  if (!export_frames_path_.empty())
    exporter.emplace(export_frames_path_,
                     FrameExporter::Spec{static_cast<std::uint32_t>(grid_dimension_),
                                         static_cast<std::uint32_t>(generations_ + 1)});

  // Simulated-annealing diagnostics: track the range of the enter_sparse
  // threshold across the run so the adaptation is observable after run().
  // Seeded to the base value; widened each generation when SA is active.
  float sa_enter_min = thresholds_.enter_sparse;
  float sa_enter_max = thresholds_.enter_sparse;

  for (int gen = 0; gen <= generations_; ++gen)
  {
    debug_printer_.print_generation(static_cast<std::uint64_t>(gen), grid_,
                                    static_cast<std::uint64_t>(grid_dimension_));

    // Capture this generation's central z-slice before stepping (state at gen).
    if (exporter)
      exporter->write_slice(static_cast<std::uint32_t>(gen), grid_.data(), stride_y_, stride_z_);

    std::array<std::uint64_t, generator::N_SPECIES + 1> counts{};
    counts.fill(0);

    // ── Group 8: dual-threshold hysteresis mode dispatch ─────────────────────
    // change_ratio = changes from last step / N³.  In dense mode, changes_
    // is cleared so cr = 0; hysteresis prevents flip-flop at the boundary.
    const auto N3_f = static_cast<float>(static_cast<std::size_t>(grid_dimension_)
                                         * static_cast<std::size_t>(grid_dimension_)
                                         * static_cast<std::size_t>(grid_dimension_));

    const float dr = dirty_set_.dirty_ratio();
    const float cr = (N3_f > 0.0f) ? static_cast<float>(changes_.size()) / N3_f : 0.0f;

    // Simulated annealing: when enabled, adapt the dispatch thresholds from
    // the chaos metric and cooling schedule before this generation's dispatch.
    if (annealing_ctrl_)
    {
      thresholds_ = annealing_ctrl_->step(
          {.change_ratio = cr, .generation = static_cast<std::uint32_t>(gen)});
      sa_enter_min = std::min(sa_enter_min, thresholds_.enter_sparse);
      sa_enter_max = std::max(sa_enter_max, thresholds_.enter_sparse);
    }

    if (mode_ == Mode::kSparse)
    {
      if (dr >= thresholds_.exit_sparse || cr >= thresholds_.exit_churn)
        mode_ = Mode::kDense;
    }
    else // kDense
    {
      if (dr <= thresholds_.enter_sparse && cr <= thresholds_.enter_churn)
        mode_ = Mode::kSparse;
    }

    if (mode_ == Mode::kDense)
    {
      ++dense_gens_;
      changes_.clear(); // clear stale sparse changes so cr = 0 next gen
      step_generation_dense(grid_, next_grid_, counts);

      // Group 7: sync species counts from dense step (old-gen approximation).
      current_counts_ = counts;

      grid_.swap(next_grid_);
      fill_ghost_cells(grid_);
      dirty_set_.rebuild_full(grid_.data()); // ← full rebuild always in dense mode
    }
    else // kSparse
    {
      ++sparse_gens_;
      step_generation_sparse(grid_, next_grid_, counts);

      // Group 7: incremental species-count update BEFORE swap (grid_ = old grid).
      for (const auto& entry : changes_)
      {
        if (entry.second == DEAD)
          --current_counts_[static_cast<std::size_t>(
              grid_[static_cast<std::size_t>(entry.first)])]; // old species
        else
          ++current_counts_[static_cast<std::size_t>(entry.second)]; // new birth
      }

      grid_.swap(next_grid_);
      fill_ghost_cells(grid_);

      dirty_set_.swap_indices();

      // Group 6: choose dirty-set rebuild strategy based on churn.
      if (changes_.size() * 10 < dirty_set_.prev_size())
        dirty_set_.rebuild_from_changes(grid_.data(), changes_);
      else
        dirty_set_.rebuild_incremental(grid_.data());
    }

    // Groups 5+8: tile bookkeeping.
    tile_set_.update_counts_from_changes(changes_);
    tile_set_.rebuild(dirty_set_.dirty_indices());

    update_maxima_for_generation(counts, static_cast<std::uint64_t>(gen));
  }

  std::fprintf(stderr,
               "mode: dense=%zu sparse=%zu dirty_thr=%.0f%%/%.0f%% churn_thr=%.0f%%/%.0f%%\n",
               dense_gens_, sparse_gens_, static_cast<double>(thresholds_.enter_sparse) * 100.0,
               static_cast<double>(thresholds_.exit_sparse) * 100.0,
               static_cast<double>(thresholds_.enter_churn) * 100.0,
               static_cast<double>(thresholds_.exit_churn) * 100.0);

  // Simulated-annealing trace: the span of the enter_sparse threshold over the
  // run.  Emitted only when SA is active.  max > min proves the controller
  // adapted the threshold; equal values mean it never moved (e.g. no churn).
  if (annealing_ctrl_)
  {
    std::fprintf(stderr, "sa: enter_sparse min=%.4f max=%.4f chaos=%.4f\n",
                 static_cast<double>(sa_enter_min), static_cast<double>(sa_enter_max),
                 static_cast<double>(annealing_ctrl_->chaos()));
  }
}

// ── Output ────────────────────────────────────────────────────────────────────

void Simulation::print_results() const
{
  for (int s = 1; s <= N_S; ++s)
  {
    const auto ss = static_cast<std::size_t>(s);
    std::fprintf(stdout, "%d %llu %llu\n", s, static_cast<unsigned long long>(max_count_[ss]),
                 static_cast<unsigned long long>(max_gen_[ss]));
  }
}
