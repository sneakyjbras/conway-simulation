// simulation.cpp – C++23
//
// ═══════════════════════════════════════════════════════════════════════════
// Adaptive dirty-set / two-mode dispatch
// ═══════════════════════════════════════════════════════════════════════════
//
// Core insight
// ────────────
// A cell's next state is a pure function of its own state and its 26
// neighbours' states.  Therefore a cell can only CHANGE if it, or at least
// one of its 26 neighbours, is currently alive.  Cells that are dead with
// zero alive neighbours are provably unchanged and can be skipped entirely.
//
// dirty_[idx] = 1 iff cell idx is alive OR has ≥ 1 alive neighbour.
// dirty_[idx] = 0 implies:  (a) cell is dead, (b) all 26 neighbours dead.
//
// Two-mode dispatch in run()
// ──────────────────────────
//  dirty_ratio = dirty_count / N³
//
//  dirty_ratio > DENSE_THRESHOLD (0.60)
//      → step_generation_dense()   — SIMD-vectorised full O(N³) sweep.
//        Better when most cells need computing.
//
//  dirty_ratio ≤ DENSE_THRESHOLD
//      → step_generation_sparse()  — tile-aware, skip-friendly sweep.
//        Better once the simulation stabilises and large dead regions form.
//        Typically fires after a handful of generations.
//
// SIMD neighbour counting (AVX2)
// ──────────────────────────────
// step_generation_dense uses _mm256_loadu_si256 to load 32 consecutive
// cells at each of the 26 neighbour offsets, clamping values to 0/1 with
// _mm256_min_epu8 and accumulating with _mm256_adds_epu8.  This processes
// 32 cells' neighbour totals per loop iteration instead of one.
//
// The birth path (dead cell → alive) requires per-species counts of the
// 26 neighbours; this minority path falls back to the scalar loop.
//
// Tile active-set
// ───────────────
// Interior cells are grouped into TILE_DIM³ tiles (default 8³ = 512 cells).
// A tile is ACTIVE if any of its cells has dirty_ = 1.
// In sparse mode dormant tiles are memcpy'd and active tiles are iterated.
//
// Dirty-set rebuild after each step
// ──────────────────────────────────
// Dense mode:  rebuild_dirty_full()        — O(N³) scan of new grid.
// Sparse mode: rebuild_dirty_incremental() — O(prev_dirty_count × 26).

#include "simulation.hpp"

#include "debug_printer.hpp"

#ifdef __AVX2__
#include <immintrin.h>
#endif

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <vector>

namespace
{

constexpr unsigned char DEAD = 0;
constexpr int           N_S  = static_cast<int>(generator::N_SPECIES);

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

// Return the species with the highest count in sp[1..N_SPECIES].
// Tie-break: lowest species ID wins.  Returns 0 if all counts are zero.
[[nodiscard]] inline int
pick_majority_species(const std::array<int, generator::N_SPECIES + 1>& sp) noexcept
{
  int best_s = 0;
  int best_c = 0;
  for (int s = 1; s <= N_S; ++s)
  {
    const int c = sp[static_cast<std::size_t>(s)];
    if (c > best_c || (c == best_c && c > 0 && s < best_s))
    {
      best_c = c;
      best_s = s;
    }
  }
  return best_s;
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

// ── Dense step: full O(N³) sweep with AVX2 x-axis vectorisation ──────────────
//
// For each (z, y) row, the x-inner loop is processed 32 cells at a time
// using AVX2 SIMD when __AVX2__ is defined:
//
//   For each of the 26 neighbour offsets:
//     Load 32 consecutive bytes → clamp to {0,1} → saturating-add to accumulator.
//   Accumulator holds neighbour total (≤ 26, safe in uint8) for 32 cells.
//
// Birth cells (dead, total in [7,10]) still need a per-species count of their
// 26 neighbours for the majority-species rule; that path is scalar.
//
// The scalar fallback handles the N % 32 tail cells (or the whole loop when
// __AVX2__ is not available).

void Simulation::step_generation_dense(
    const std::vector<unsigned char>&                    current,
    std::vector<unsigned char>&                          next,
    std::array<std::uint64_t, generator::N_SPECIES + 1>& counts) noexcept
{
  const int            N       = grid_dimension_;
  const std::ptrdiff_t s_y     = stride_y_;
  const std::ptrdiff_t s_z     = stride_z_;
  const auto*          offsets = neighbor_offsets_.data();

  counts.fill(0);
  std::array<int, generator::N_SPECIES + 1> sp{};

#ifdef __AVX2__
  const __m256i             ones_v = _mm256_set1_epi8(1);
  const __m256i             zero_v = _mm256_setzero_si256();
  alignas(32) unsigned char tot_buf[32]{};
  alignas(32) unsigned char cur_buf[32]{};
#endif

  for (int z = 0; z < N; ++z)
  {
    const std::ptrdiff_t z_base = static_cast<std::ptrdiff_t>(z + 1) * s_z;
    for (int y = 0; y < N; ++y)
    {
      const std::ptrdiff_t row = z_base + static_cast<std::ptrdiff_t>(y + 1) * s_y + 1;

      int x = 0;

#ifdef __AVX2__
      // AVX2 path: accumulate neighbour totals for 32 cells simultaneously.
      // p points to the first cell in this 32-wide chunk (ghost cells at p[-1]
      // and p[N] are valid because of the halo layer).
      for (; x + 32 <= N; x += 32)
      {
        const auto* p = current.data() + row + x;

        __m256i total_v = zero_v;
        for (int dz = -1; dz <= 1; ++dz)
          for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
            {
              if (dz == 0 && dy == 0 && dx == 0)
                continue;
              const __m256i nbr = _mm256_loadu_si256( // NOLINT(portability-simd-intrinsics)
                  reinterpret_cast<const __m256i*>(p + dz * s_z + dy * s_y + dx));
              total_v           = _mm256_adds_epu8( // NOLINT(portability-simd-intrinsics)
                  total_v, _mm256_min_epu8(nbr, ones_v)); // NOLINT(portability-simd-intrinsics)
            }

        _mm256_store_si256(reinterpret_cast<__m256i*>(tot_buf),
                           total_v); // NOLINT(portability-simd-intrinsics)
        _mm256_store_si256(          // NOLINT(portability-simd-intrinsics)
            reinterpret_cast<__m256i*>(cur_buf),
            _mm256_loadu_si256(
                reinterpret_cast<const __m256i*>(p))); // NOLINT(portability-simd-intrinsics)

        auto* next_p = next.data() + row + x;
        for (int i = 0; i < 32; ++i)
        {
          const unsigned char cur   = cur_buf[i];
          const int           total = static_cast<int>(tot_buf[i]);

          if (cur != DEAD)
          {
            counts[cur]++;
            next_p[i] = (total >= 5 && total <= 13) ? cur : DEAD;
          }
          else if (total >= 7 && total <= 10)
          {
            // Birth: need majority species — scalar per-species count.
            sp.fill(0);
            for (int k = 0; k < 26; ++k)
            {
              const unsigned char v = (p + i)[offsets[k]];
              if (v != DEAD)
                sp[v]++;
            }
            const int best = pick_majority_species(sp);
            next_p[i]      = (best != 0) ? static_cast<unsigned char>(best) : DEAD;
          }
          else
          {
            next_p[i] = DEAD;
          }
        }
      }
#endif

      // Scalar path — handles N % 32 tail, or full row when AVX2 absent.
      for (; x < N; ++x)
      {
        const std::ptrdiff_t idx = row + static_cast<std::ptrdiff_t>(x);
        const auto*          ptr = current.data() + idx;
        const unsigned char  cur = *ptr;

        if (cur != DEAD)
          counts[cur]++;

        int total = 0;
        for (int k = 0; k < 26; ++k)
          total += (ptr[offsets[k]] != DEAD);

        unsigned char new_val = DEAD;
        if (cur != DEAD)
        {
          if (total >= 5 && total <= 13) [[likely]]
            new_val = cur;
        }
        else if (total >= 7 && total <= 10)
        {
          sp.fill(0);
          for (int k = 0; k < 26; ++k)
          {
            const unsigned char v = ptr[offsets[k]];
            if (v != DEAD)
              sp[v]++;
          }
          const int best = pick_majority_species(sp);
          if (best != 0)
            new_val = static_cast<unsigned char>(best);
        }
        next[static_cast<std::size_t>(idx)] = new_val;
      }
    }
  }
}

// ── Sparse step: tile-aware, dirty-cell-skipping sweep ───────────────────────

void Simulation::step_generation_sparse(
    const std::vector<unsigned char>&                    current,
    std::vector<unsigned char>&                          next,
    std::array<std::uint64_t, generator::N_SPECIES + 1>& counts) noexcept
{
  const int   N       = grid_dimension_;
  const int   tpa     = tile_set_.tiles_per_axis();
  const int   n_tiles = tpa * tpa * tpa;
  const auto* offsets = neighbor_offsets_.data();
  const auto* d_data  = dirty_set_.dirty_data();
  const auto* t_data  = tile_set_.active().data();

  counts.fill(0);
  changes_.clear();
  std::array<int, generator::N_SPECIES + 1> sp{};

  for (int ti = 0; ti < n_tiles; ++ti)
  {
    const int tz = ti / (tpa * tpa);
    const int ty = (ti / tpa) % tpa;
    const int tx = ti % tpa;

    const int z0 = tz * TILE_DIM;
    const int z1 = std::min(z0 + TILE_DIM, N);
    const int y0 = ty * TILE_DIM;
    const int y1 = std::min(y0 + TILE_DIM, N);
    const int x0 = tx * TILE_DIM;
    const int x1 = std::min(x0 + TILE_DIM, N);

    if (!t_data[static_cast<std::size_t>(ti)])
    {
      // Dormant tile: all cells are dead — propagate zeros in bulk.
      for (int z = z0; z < z1; ++z)
      {
        const std::ptrdiff_t z_base = static_cast<std::ptrdiff_t>(z + 1) * stride_z_;
        for (int y = y0; y < y1; ++y)
        {
          const std::ptrdiff_t row =
              z_base + static_cast<std::ptrdiff_t>(y + 1) * stride_y_ + 1 + x0;
          std::memcpy(next.data() + row, current.data() + row, static_cast<std::size_t>(x1 - x0));
        }
      }
      continue;
    }

    // Active tile: process cell by cell (non-dirty cells guaranteed dead).
    for (int z = z0; z < z1; ++z)
    {
      const std::ptrdiff_t z_base = static_cast<std::ptrdiff_t>(z + 1) * stride_z_;
      for (int y = y0; y < y1; ++y)
      {
        const std::ptrdiff_t row_base = z_base + static_cast<std::ptrdiff_t>(y + 1) * stride_y_ + 1;
        for (int x = x0; x < x1; ++x)
        {
          const std::ptrdiff_t idx = row_base + static_cast<std::ptrdiff_t>(x);
          const auto*          ptr = current.data() + idx;
          const unsigned char  cur = *ptr;

          if (cur != DEAD)
            counts[cur]++;

          // Non-dirty cells within an active tile are provably dead.
          if (!d_data[static_cast<std::size_t>(idx)])
          {
            next[static_cast<std::size_t>(idx)] = DEAD;
            continue;
          }

          int total = 0;
          for (int k = 0; k < 26; ++k)
            total += (ptr[offsets[k]] != DEAD);

          unsigned char new_val = DEAD;
          if (cur != DEAD)
          {
            if (total >= 5 && total <= 13) [[likely]]
              new_val = cur;
          }
          else if (total >= 7 && total <= 10)
          {
            sp.fill(0);
            for (int k = 0; k < 26; ++k)
            {
              const unsigned char v = ptr[offsets[k]];
              if (v != DEAD)
                sp[v]++;
            }
            const int best = pick_majority_species(sp);
            if (best != 0)
              new_val = static_cast<unsigned char>(best);
          }
          next[static_cast<std::size_t>(idx)] = new_val;
          if (new_val != cur)
            changes_.emplace_back(idx, new_val);
        }
      }
    }
  }
}

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

  // Simulated-annealing diagnostics: track the range of the enter_sparse
  // threshold across the run so the adaptation is observable after run().
  // Seeded to the base value; widened each generation when SA is active.
  float sa_enter_min = thresholds_.enter_sparse;
  float sa_enter_max = thresholds_.enter_sparse;

  for (int gen = 0; gen <= generations_; ++gen)
  {
    debug_printer_.print_generation(static_cast<std::uint64_t>(gen), grid_,
                                    static_cast<std::uint64_t>(grid_dimension_));

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
