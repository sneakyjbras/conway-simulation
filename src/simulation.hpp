#pragma once
// simulation.hpp – C++23
//
// Layers of optimisation (in order of implementation):
//
// ① Ghost-cell (halo) layer
//    Grid stored as (N+2)³.  The outer ring mirrors the opposite face for
//    toroidal wrap-around, eliminating all boundary branches from the hot
//    inner loop.  Three O(N²) cascaded passes fill all 26 ghost regions
//    before each step.  This is also the correct foundation for the
//    future MPI halo-exchange phase.
//
// ② Precomputed 26-neighbour offset table
//    26 signed flat ptrdiff_t offsets into the ghost array, built once at
//    construction.  The inner loop is 26 plain pointer loads with no
//    coordinate arithmetic.
//
// ③ Pre-allocated next_grid_
//    Both grids live as members; zero heap allocation inside the run loop.
//
// ④ OpenMP (shared-memory step)
//    The z-loop in step_generation_dense() carries a parallel for with
//    thread-local count reduction.
//
// ⑤ Adaptive dirty-set / tile optimisation  ← NEW
//    See the large comment block at the top of simulation.cpp for full
//    details.  Short version:
//
//    dirty_[idx] = 1  iff cell idx is alive OR has at least one alive
//                     neighbour.  Cells with dirty_==0 are provably dead
//                     with no alive neighbours and cannot change state —
//                     they are skipped entirely.
//
//    Tiles of TILE_DIM³ cells are marked active when any of their cells
//    is dirty.  In sparse mode the step skips inactive tiles wholesale
//    (memcpy from current) and processes only dirty cells within active
//    tiles.
//
//    Mode dispatch in run():
//      dirty_ratio > DENSE_THRESHOLD  →  step_generation_dense()
//      dirty_ratio ≤ DENSE_THRESHOLD  →  step_generation_sparse()

#include "debug_printer.hpp"
#include "generator.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

class Simulation {
public:
  Simulation(int number_of_generations, int grid_dimension,
             float initial_density, int random_seed);

  void run();
  void print_results() const;

  void set_debug_enabled(bool enabled) noexcept { debug_printer_.enable(enabled); }

  // Mode counters — readable after run() for testing / reporting.
  [[nodiscard]] std::size_t dense_gens()  const noexcept { return dense_gens_;  }
  [[nodiscard]] std::size_t sparse_gens() const noexcept { return sparse_gens_; }

private:
  // ── Tuning constants ────────────────────────────────────────────────────
  // Fraction of N³ cells that are dirty above which we use the full sweep.
  // Below this fraction the tile-skipping sparse path pays off.
  static constexpr float DENSE_THRESHOLD = 0.60f;

  // Edge length of each cubic tile (cells per axis).
  // 8³ = 512 cells per tile; fits comfortably in L1/L2 cache.
  static constexpr int   TILE_DIM        = 8;

  // ── Ghost-cell helpers ──────────────────────────────────────────────────
  [[nodiscard]] std::size_t ghost_idx(int gx, int gy, int gz) const noexcept;
  [[nodiscard]] std::size_t cell_idx (int  x, int  y, int  z) const noexcept;

  void fill_ghost_cells(std::vector<unsigned char>& g) const noexcept;

  // ── Simulation phases ───────────────────────────────────────────────────
  void initialize_from_generator();

  // Full O(N³) sweep — used when dirty_ratio > DENSE_THRESHOLD.
  void step_generation_dense (const std::vector<unsigned char>& current,
                               std::vector<unsigned char>&       next,
                               std::array<std::uint64_t, generator::N_SPECIES + 1>& counts) noexcept;

  // Tile-skipping sparse sweep — used when dirty_ratio ≤ DENSE_THRESHOLD.
  void step_generation_sparse(const std::vector<unsigned char>& current,
                               std::vector<unsigned char>&       next,
                               std::array<std::uint64_t, generator::N_SPECIES + 1>& counts) noexcept;

  // ── Dirty-set maintenance ───────────────────────────────────────────────
  // Ratio of dirty interior cells to N³.
  [[nodiscard]] float dirty_ratio() const noexcept;

  // Full rebuild from grid_ — O(N³ × 26) with early exit.
  // Used after dense step and at initialisation.
  void rebuild_dirty_full() noexcept;

  // Incremental rebuild — O(prev_dirty_count × 26).
  // Reads prev_dirty_ (old dirty set), writes dirty_.
  // Proof of correctness: cells absent from prev_dirty_ were dead with no
  // alive neighbours, so they remain dead after the step and cannot become
  // dirty.  Only cells in prev_dirty_ need re-evaluation.
  void rebuild_dirty_incremental() noexcept;

  // Scan dirty_ and mark each tile active/dormant.
  void rebuild_tile_set() noexcept;

  // ── Maxima tracking ─────────────────────────────────────────────────────
  void update_maxima_for_generation(
      const std::array<std::uint64_t, generator::N_SPECIES + 1>& counts,
      std::uint64_t gen) noexcept;

  // ── Core grid state ─────────────────────────────────────────────────────
  int           grid_dimension_; // N  (logical cells per axis)
  int           ghost_dim_;      // N+2
  int           generations_;
  float         density_;
  std::int32_t  seed_;

  std::ptrdiff_t stride_y_; // ghost_dim_
  std::ptrdiff_t stride_z_; // ghost_dim_ * ghost_dim_

  std::array<std::ptrdiff_t, 26> neighbor_offsets_;

  std::vector<unsigned char> grid_;
  std::vector<unsigned char> next_grid_;

  // ── Dirty-set state ─────────────────────────────────────────────────────
  // Same ghost-padded size as grid_.
  // dirty_[idx] = 1 iff cell idx must be computed in the next step.
  std::vector<std::uint8_t> dirty_;
  std::vector<std::uint8_t> prev_dirty_;  // scratch for incremental rebuild
  std::size_t               dirty_count_{0};

  // ── Tile active-set ─────────────────────────────────────────────────────
  int                       tiles_per_axis_{0};
  std::vector<std::uint8_t> tile_active_; // one byte per tile

  // ── Maxima ──────────────────────────────────────────────────────────────
  std::array<std::uint64_t, generator::N_SPECIES + 1> max_count_{};
  std::array<std::uint64_t, generator::N_SPECIES + 1> max_gen_{};

  // ── Mode counters ────────────────────────────────────────────────────────
  std::size_t dense_gens_{0};
  std::size_t sparse_gens_{0};

  DebugPrinter debug_printer_;
};
