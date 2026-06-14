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

#include "annealing.hpp"
#include "debug_printer.hpp"
#include "dirty_set.hpp"
#include "generator.hpp"
#include "grid_types.hpp"
#include "params.hpp"
#include "tile_set.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

class Simulation
{
public:
  explicit Simulation(const SimulationParams& params);

  // Movable (transfers all owned buffers and the optional annealing
  // controller); copying is disabled because the grids can be large and
  // accidental deep copies would be costly.  unique_ptr makes the class
  // move-only by default, so these are spelled out for clarity.
  Simulation(Simulation&&) noexcept            = default;
  Simulation& operator=(Simulation&&) noexcept = default;
  Simulation(const Simulation&)                = delete;
  Simulation& operator=(const Simulation&)     = delete;
  ~Simulation()                                = default;

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
  // Dispatch thresholds are now runtime state (see thresholds_ below) so the
  // annealing controller can adjust them per generation.  TILE_DIM lives in
  // grid_types.hpp, shared with the TileSet.

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

  // ── Species-count maintenance ───────────────────────────────────────────
  void compute_initial_counts() noexcept;

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

  // ── Runtime configuration ───────────────────────────────────────────────
  // Dispatch thresholds are mutable so the annealing controller can shift them
  // per generation.  Distribution and alloc config drive scratch reservation.
  DispatchThresholds thresholds_;
  AllocConfig        alloc_;
  DistributionType   distribution_;

  // ── Simulated-annealing controller ──────────────────────────────────────
  // Held via unique_ptr: present only when annealing is enabled, null
  // otherwise.  When null, the static thresholds_ are used unchanged
  // (bit-identical to the pre-annealing dispatch path).  This is the idiomatic
  // owning handle for an optional sub-system and keeps Simulation move-only.
  AnnealingParams                      annealing_params_;
  std::unique_ptr<AnnealingController> annealing_ctrl_;

  std::ptrdiff_t stride_y_; // ghost_dim_
  std::ptrdiff_t stride_z_; // ghost_dim_ * ghost_dim_

  std::array<std::ptrdiff_t, 26> neighbor_offsets_;

  // Read-only geometry snapshot shared with the dirty-set and tile-set.
  GridGeometry geom_;

  std::vector<unsigned char> grid_;
  std::vector<unsigned char> next_grid_;

  // ── Grid sub-objects ────────────────────────────────────────────────────
  // DirtySet owns the activity tracking (dirty byte map + index lists and the
  // three rebuild strategies).  TileSet owns the coarse tile active-set and
  // per-tile alive counts.  Both hold geometry by value and receive the grid
  // buffer per call, so Simulation remains movable.
  DirtySet dirty_set_;
  TileSet  tile_set_;

  // ── Changes vector ─────────────────────────────────────────────────────────
  // Populated only during step_generation_sparse(). Each entry is
  // {flat_ghost_idx, new_value} for cells whose value actually changed this step.
  // Empty after dense steps.
  ChangesVector changes_;

  // ── Running species counts ─────────────────────────────────────────────────
  // Maintained incrementally in sparse mode; resynced from step output in dense mode.
  std::array<std::uint64_t, generator::N_SPECIES + 1> current_counts_{};

  // ── Mode state ────────────────────────────────────────────────────────────
  enum class Mode : std::uint8_t
  {
    kDense,
    kSparse
  };
  Mode mode_{Mode::kDense};

  // ── Maxima ──────────────────────────────────────────────────────────────
  std::array<std::uint64_t, generator::N_SPECIES + 1> max_count_{};
  std::array<std::uint64_t, generator::N_SPECIES + 1> max_gen_{};

  // ── Mode counters ────────────────────────────────────────────────────────
  std::size_t dense_gens_{0};
  std::size_t sparse_gens_{0};

  DebugPrinter debug_printer_;
};
