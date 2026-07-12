// simulation_init.cpp – C++23
//
// Single Responsibility: SETUP.
// Everything that runs once, before the generation loop — construction, scratch
// allocation, initial-grid population, ghost-cell halo fill, and the initial
// species-count seed.  The per-generation hot path lives in simulation_step.cpp;
// orchestration in simulation.cpp.

#include "simulation.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace
{

constexpr unsigned char DEAD = 0;

// Build the 26 signed flat neighbour offsets for a ghost-padded grid.
// Order is the canonical dz,dy,dx scan with the self-cell (0,0,0) skipped.
[[nodiscard]] std::array<std::ptrdiff_t, 26> make_neighbor_offsets(std::ptrdiff_t stride_y,
                                                                   std::ptrdiff_t stride_z) noexcept
{
  std::array<std::ptrdiff_t, 26> offs{};
  std::size_t                    k = 0;
  for (int dz = -1; dz <= 1; ++dz)
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx)
      {
        if (dx == 0 && dy == 0 && dz == 0)
          continue;
        offs[k++] = static_cast<std::ptrdiff_t>(dz) * stride_z
                    + static_cast<std::ptrdiff_t>(dy) * stride_y + static_cast<std::ptrdiff_t>(dx);
      }
  return offs;
}

// Estimate the index-list reserve for the dirty set: the alive estimate
// (density × N³ × distribution factor) doubled for the neighbour shell, then
// clamped to N³.  Mirrors the scratch-sizing rationale in params.hpp.
// Takes the whole SimulationParams (rather than separate N/density scalars,
// which clang-tidy flags as an easily-swapped convertible pair).
[[nodiscard]] std::size_t estimate_dirty_reserve(const SimulationParams& params) noexcept
{
  const auto  N      = static_cast<std::size_t>(params.problem.grid_dimension);
  const auto  n3     = N * N * N;
  const float factor = (params.distribution == DistributionType::kGaussian)
                           ? params.alloc.gaussian_reserve_factor
                           : params.alloc.uniform_reserve_factor;
  const auto  expected_alive =
      static_cast<std::size_t>(static_cast<float>(n3) * params.density * factor);
  return std::min(expected_alive * 2, n3);
}

} // namespace
// ── Construction ─────────────────────────────────────────────────────────────

Simulation::Simulation(const SimulationParams& params)
  : grid_dimension_(static_cast<int>(params.problem.grid_dimension))
  , ghost_dim_(static_cast<int>(params.problem.grid_dimension) + 2)
  , generations_(static_cast<int>(params.problem.generations))
  , density_(params.density)
  , seed_(params.seed)
  , thresholds_(params.thresholds)
  , alloc_(params.alloc)
  , distribution_(params.distribution)
  , export_frames_path_(params.export_frames_path)
  , annealing_params_(params.annealing)
  , stride_y_(static_cast<std::ptrdiff_t>(ghost_dim_))
  , stride_z_(static_cast<std::ptrdiff_t>(ghost_dim_) * static_cast<std::ptrdiff_t>(ghost_dim_))
  , neighbor_offsets_(make_neighbor_offsets(stride_y_, stride_z_))
  , geom_{stride_y_, stride_z_, static_cast<std::ptrdiff_t>(grid_dimension_), neighbor_offsets_}
  , grid_(static_cast<std::size_t>(ghost_dim_) * static_cast<std::size_t>(ghost_dim_)
              * static_cast<std::size_t>(ghost_dim_),
          DEAD)
  , next_grid_(grid_.size(), DEAD)
  , dirty_set_(estimate_dirty_reserve(params), geom_)
  , tile_set_(static_cast<std::uint32_t>(grid_dimension_), geom_)
  , debug_printer_(false)
{
  max_count_.fill(0);
  max_gen_.fill(0);
  current_counts_.fill(0);

  // ── Scratch-vector pre-allocation ───────────────────────────────────────
  // Reserve changes_ once, here, so no reallocation can occur inside run().
  // Unlike the grid vectors (fixed problem volume N³), the scratch vectors
  // track the state distribution: their working size scales with the number of
  // alive/dirty cells.  The factor provides headroom for distribution
  // spikiness — uniform 1.5×, Gaussian 3.0× (both tunable via AllocConfig).
  // The dirty-set index lists are reserved inside DirtySet's constructor using
  // the same estimate.
  const auto n3 = static_cast<std::size_t>(grid_dimension_)
                  * static_cast<std::size_t>(grid_dimension_)
                  * static_cast<std::size_t>(grid_dimension_);

  const float reserve_factor = (distribution_ == DistributionType::kGaussian)
                                   ? alloc_.gaussian_reserve_factor
                                   : alloc_.uniform_reserve_factor;

  const auto expected_alive =
      static_cast<std::size_t>(static_cast<float>(n3) * density_ * reserve_factor);
  changes_.reserve(expected_alive);

  // Construct the annealing controller only when enabled.  When disabled the
  // pointer stays null and run() uses the static thresholds unchanged.
  if (annealing_params_.enabled)
    annealing_ctrl_ = std::make_unique<AnnealingController>(annealing_params_, thresholds_);

  initialize_from_generator();
}

// ── Index helpers ─────────────────────────────────────────────────────────────

std::size_t Simulation::ghost_idx(int gx, int gy, int gz) const noexcept
{
  return static_cast<std::size_t>(gz) * static_cast<std::size_t>(stride_z_)
         + static_cast<std::size_t>(gy) * static_cast<std::size_t>(stride_y_)
         + static_cast<std::size_t>(gx);
}

std::size_t Simulation::cell_idx(int x, int y, int z) const noexcept
{
  return ghost_idx(x + 1, y + 1, z + 1);
}

// ── Initialisation ────────────────────────────────────────────────────────────

void Simulation::initialize_from_generator()
{
  const auto src =
      generator::gen_initial_grid({.grid_dimension = static_cast<std::uint64_t>(grid_dimension_),
                                   .density        = density_,
                                   .seed           = seed_,
                                   .distribution   = distribution_});

  const std::size_t N  = static_cast<std::size_t>(grid_dimension_);
  const std::size_t N2 = N * N;

  for (int z = 0; z < grid_dimension_; ++z)
    for (int y = 0; y < grid_dimension_; ++y)
      for (int x = 0; x < grid_dimension_; ++x)
      {
        const std::size_t src_idx = static_cast<std::size_t>(z) * N2
                                    + static_cast<std::size_t>(y) * N + static_cast<std::size_t>(x);
        grid_[cell_idx(x, y, z)] = src[src_idx];
      }

  fill_ghost_cells(grid_);

  // Initialise running species counts from the initial grid.
  compute_initial_counts();

  // Initialise per-tile alive-cell counts from the initial grid.
  tile_set_.init_counts_from_grid(grid_.data());
}

// ── Ghost-cell fill ───────────────────────────────────────────────────────────

void Simulation::fill_ghost_cells(std::vector<unsigned char>& g) const noexcept
{
  const int N = grid_dimension_;

  // Pass 1: ±z faces (interior gy/gx only)
  for (int gy = 1; gy <= N; ++gy)
  {
    const std::ptrdiff_t gy_off = static_cast<std::ptrdiff_t>(gy) * stride_y_;
    for (int gx = 1; gx <= N; ++gx)
    {
      g[static_cast<std::size_t>(0 * stride_z_ + gy_off + gx)] =
          g[static_cast<std::size_t>(N * stride_z_ + gy_off + gx)];
      g[static_cast<std::size_t>((N + 1) * stride_z_ + gy_off + gx)] =
          g[static_cast<std::size_t>(1 * stride_z_ + gy_off + gx)];
    }
  }

  // Pass 2: ±y rows (full gz range so z-corners propagate)
  for (int gz = 0; gz <= N + 1; ++gz)
  {
    const std::ptrdiff_t gz_off = static_cast<std::ptrdiff_t>(gz) * stride_z_;
    for (int gx = 1; gx <= N; ++gx)
    {
      g[static_cast<std::size_t>(gz_off + 0 * stride_y_ + gx)] =
          g[static_cast<std::size_t>(gz_off + N * stride_y_ + gx)];
      g[static_cast<std::size_t>(gz_off + (N + 1) * stride_y_ + gx)] =
          g[static_cast<std::size_t>(gz_off + 1 * stride_y_ + gx)];
    }
  }

  // Pass 3: ±x columns (full gy+gz range so all edges/corners are covered)
  for (int gz = 0; gz <= N + 1; ++gz)
  {
    const std::ptrdiff_t gz_off = static_cast<std::ptrdiff_t>(gz) * stride_z_;
    for (int gy = 0; gy <= N + 1; ++gy)
    {
      const std::ptrdiff_t gy_off = static_cast<std::ptrdiff_t>(gy) * stride_y_;
      g[static_cast<std::size_t>(gz_off + gy_off + 0)] =
          g[static_cast<std::size_t>(gz_off + gy_off + N)];
      g[static_cast<std::size_t>(gz_off + gy_off + N + 1)] =
          g[static_cast<std::size_t>(gz_off + gy_off + 1)];
    }
  }
}

// ── compute_initial_counts ────────────────────────────────────────────────────
// Counts alive cells of each species in the initial (logical) grid and stores
// the result in current_counts_.  Called once from initialize_from_generator().

void Simulation::compute_initial_counts() noexcept
{
  current_counts_.fill(0);
  for (int z = 0; z < grid_dimension_; ++z)
    for (int y = 0; y < grid_dimension_; ++y)
      for (int x = 0; x < grid_dimension_; ++x)
      {
        const unsigned char v = grid_[cell_idx(x, y, z)];
        if (v != DEAD)
          current_counts_[static_cast<std::size_t>(v)]++;
      }
}
