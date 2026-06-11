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
// ④ AVX2 SIMD vectorisation (x-axis, 32 cells per cycle)
//    step_generation_dense processes neighbour counts for 32 consecutive
//    cells simultaneously using _mm256_loadu_si256 + _mm256_min_epu8 +
//    _mm256_adds_epu8.  Enabled automatically when __AVX2__ is defined
//    (i.e. -march=native on any modern x86-64 build host).
//
// ⑤ Adaptive dirty-set / tile optimisation
//    dirty_[idx] = 1  iff cell idx is alive OR has at least one alive
//                     neighbour.  Cells with dirty_==0 are provably dead
//                     with no alive neighbours and cannot change state.
//
//    Tiles of TILE_DIM³ cells are marked active when any of their cells
//    is dirty.  In sparse mode the step skips inactive tiles wholesale
//    and processes only dirty cells within active tiles.
//
//    Mode dispatch in run():
//      dual-threshold hysteresis with dirty_ratio and change_ratio
//      kDense  → step_generation_dense()
//      kSparse → step_generation_sparse()

#include "debug_printer.hpp"
#include "generator.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

class Simulation
{
public:
  Simulation(int number_of_generations, int grid_dimension, float initial_density, int random_seed);

  void run();
  void print_results() const;

  void set_debug_enabled(bool enabled) noexcept
  {
    debug_printer_.enable(enabled);
  }

  // Mode counters — readable after run() for testing / reporting.
  [[nodiscard]] std::size_t dense_gens() const noexcept
  {
    return dense_gens_;
  }
  [[nodiscard]] std::size_t sparse_gens() const noexcept
  {
    return sparse_gens_;
  }

private:
  // ── Tuning constants ────────────────────────────────────────────────────
  static constexpr float ENTER_SPARSE_THRESHOLD = 0.50f; // dirty_ratio to enter sparse
  static constexpr float EXIT_SPARSE_THRESHOLD  = 0.65f; // dirty_ratio to exit  sparse
  static constexpr float ENTER_CHANGE_RATIO     = 0.05f; // change_ratio to enter sparse
  static constexpr float EXIT_CHANGE_RATIO      = 0.10f; // change_ratio to exit  sparse

  // Edge length of each cubic tile (cells per axis).
  // 8³ = 512 cells per tile; fits comfortably in L1/L2 cache.
  static constexpr int TILE_DIM = 8;

  // ── Ghost-cell helpers ──────────────────────────────────────────────────
  [[nodiscard]] std::size_t ghost_idx(int gx, int gy, int gz) const noexcept;
  [[nodiscard]] std::size_t cell_idx(int x, int y, int z) const noexcept;

  void fill_ghost_cells(std::vector<unsigned char>& g) const noexcept;

  // ── Simulation phases ───────────────────────────────────────────────────
  void initialize_from_generator();

  // Full O(N³) sweep — used when kDense.
  void step_generation_dense(const std::vector<unsigned char>&                    current,
                             std::vector<unsigned char>&                          next,
                             std::array<std::uint64_t, generator::N_SPECIES + 1>& counts) noexcept;

  // Tile-skipping sparse sweep — used when kSparse.
  void step_generation_sparse(const std::vector<unsigned char>&                    current,
                              std::vector<unsigned char>&                          next,
                              std::array<std::uint64_t, generator::N_SPECIES + 1>& counts) noexcept;

  // ── Dirty-set maintenance ───────────────────────────────────────────────
  [[nodiscard]] float dirty_ratio() const noexcept;
  void                rebuild_dirty_full() noexcept;
  void                rebuild_dirty_incremental() noexcept;
  void                rebuild_tile_set() noexcept;
  void                rebuild_dirty_from_changes() noexcept;
  void                update_tile_counts_from_changes() noexcept;
  void                compute_initial_counts() noexcept;

  // ── Maxima tracking ─────────────────────────────────────────────────────
  void
  update_maxima_for_generation(const std::array<std::uint64_t, generator::N_SPECIES + 1>& counts,
                               std::uint64_t gen) noexcept;

  // ── Core grid state ─────────────────────────────────────────────────────
  int          grid_dimension_; // N  (logical cells per axis)
  int          ghost_dim_;      // N+2
  int          generations_;
  float        density_;
  std::int32_t seed_;

  std::ptrdiff_t stride_y_; // ghost_dim_
  std::ptrdiff_t stride_z_; // ghost_dim_ * ghost_dim_

  std::array<std::ptrdiff_t, 26> neighbor_offsets_;

  std::vector<unsigned char> grid_;
  std::vector<unsigned char> next_grid_;

  // ── Dirty-set state ─────────────────────────────────────────────────────
  std::vector<std::uint8_t> dirty_;
  std::size_t               dirty_count_{0};

  // ── Activity list ─────────────────────────────────────────────────────────
  // Flat ghost-array indices of all dirty cells, maintained alongside dirty_[].
  // Swapped with prev_dirty_indices_ at the start of each sparse rebuild pass.
  std::vector<std::ptrdiff_t> dirty_indices_;
  std::vector<std::ptrdiff_t> prev_dirty_indices_;

  // ── Changes vector ─────────────────────────────────────────────────────────
  // Populated only during step_generation_sparse(). Each entry is
  // {flat_ghost_idx, new_value} for cells whose value actually changed this step.
  // Empty after dense steps.
  std::vector<std::pair<std::ptrdiff_t, unsigned char>> changes_;

  // ── Running species counts ─────────────────────────────────────────────────
  // Maintained incrementally in sparse mode; resynced from step output in dense mode.
  std::array<std::uint64_t, generator::N_SPECIES + 1> current_counts_{};

  // ── Tile alive counts ──────────────────────────────────────────────────────
  // Number of alive cells inside each tile; same flat indexing as tile_active_.
  // Updated incrementally from changes_ in sparse mode.
  std::vector<std::uint32_t> tile_alive_count_;

  // ── Tile active-set ─────────────────────────────────────────────────────
  int                       tiles_per_axis_{0};
  std::vector<std::uint8_t> tile_active_;

  // ── Mode state ────────────────────────────────────────────────────────────
  enum class Mode : std::uint8_t { kDense, kSparse };
  Mode mode_{Mode::kDense};

  // ── Maxima ──────────────────────────────────────────────────────────────
  std::array<std::uint64_t, generator::N_SPECIES + 1> max_count_{};
  std::array<std::uint64_t, generator::N_SPECIES + 1> max_gen_{};

  // ── Mode counters ────────────────────────────────────────────────────────
  std::size_t dense_gens_{0};
  std::size_t sparse_gens_{0};

  DebugPrinter debug_printer_;
};
