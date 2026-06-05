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

Simulation::Simulation(int   number_of_generations,
                       int   grid_dimension,
                       float initial_density,
                       int   random_seed)
  : grid_dimension_(grid_dimension)
  , ghost_dim_(grid_dimension + 2)
  , generations_(number_of_generations)
  , density_(initial_density)
  , seed_(static_cast<std::int32_t>(random_seed))
  , stride_y_(static_cast<std::ptrdiff_t>(ghost_dim_))
  , stride_z_(static_cast<std::ptrdiff_t>(ghost_dim_) * static_cast<std::ptrdiff_t>(ghost_dim_))
  , grid_(static_cast<std::size_t>(ghost_dim_) * static_cast<std::size_t>(ghost_dim_)
              * static_cast<std::size_t>(ghost_dim_),
          DEAD)
  , next_grid_(grid_.size(), DEAD)
  , dirty_(grid_.size(), 0)
  , prev_dirty_(grid_.size(), 0)
  , tiles_per_axis_((grid_dimension_ + TILE_DIM - 1) / TILE_DIM)
  , tile_active_(static_cast<std::size_t>(tiles_per_axis_)
                     * static_cast<std::size_t>(tiles_per_axis_)
                     * static_cast<std::size_t>(tiles_per_axis_),
                 0)
  , debug_printer_(false)
{
  // ── Precompute the 26 neighbour offsets ─────────────────────────────────
  std::size_t k = 0;
  for (int dz = -1; dz <= 1; ++dz)
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx)
      {
        if (dx == 0 && dy == 0 && dz == 0)
          continue;
        neighbor_offsets_[k++] = static_cast<std::ptrdiff_t>(dz) * stride_z_
                                 + static_cast<std::ptrdiff_t>(dy) * stride_y_
                                 + static_cast<std::ptrdiff_t>(dx);
      }
  assert(k == 26);

  max_count_.fill(0);
  max_gen_.fill(0);

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
      generator::gen_initial_grid(static_cast<std::uint64_t>(grid_dimension_), density_, seed_);

  const std::size_t N  = static_cast<std::size_t>(grid_dimension_);
  const std::size_t N2 = N * N;

  for (int z = 0; z < grid_dimension_; ++z)
    for (int y = 0; y < grid_dimension_; ++y)
      for (int x = 0; x < grid_dimension_; ++x)
      {
        const std::size_t src_idx = static_cast<std::size_t>(z) * N2
                                    + static_cast<std::size_t>(y) * N + static_cast<std::size_t>(x);
        grid_[cell_idx(x, y, z)]  = src[src_idx];
      }

  fill_ghost_cells(grid_);
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

// ── Dirty-set: full rebuild ───────────────────────────────────────────────────

void Simulation::rebuild_dirty_full() noexcept
{
  const int   N    = grid_dimension_;
  const auto* g    = grid_.data();
  const auto* offs = neighbor_offsets_.data();

  std::fill(dirty_.begin(), dirty_.end(), std::uint8_t{0});
  dirty_count_ = 0;

  for (int z = 0; z < N; ++z)
  {
    const std::ptrdiff_t z_base = static_cast<std::ptrdiff_t>(z + 1) * stride_z_;
    for (int y = 0; y < N; ++y)
    {
      const std::ptrdiff_t row = z_base + static_cast<std::ptrdiff_t>(y + 1) * stride_y_ + 1;
      for (int x = 0; x < N; ++x)
      {
        const std::ptrdiff_t idx = row + static_cast<std::ptrdiff_t>(x);

        bool d = (g[idx] != DEAD);
        if (!d)
          for (int k = 0; k < 26 && !d; ++k)
            d = (g[idx + offs[k]] != DEAD);

        const std::uint8_t flag               = d ? std::uint8_t{1} : std::uint8_t{0};
        dirty_[static_cast<std::size_t>(idx)] = flag;
        dirty_count_ += flag;
      }
    }
  }
}

// ── Dirty-set: incremental rebuild ───────────────────────────────────────────

void Simulation::rebuild_dirty_incremental() noexcept
{
  const int   N    = grid_dimension_;
  const auto* g    = grid_.data();
  const auto* offs = neighbor_offsets_.data();

  dirty_count_ = 0;

  for (int z = 0; z < N; ++z)
  {
    const std::ptrdiff_t z_base = static_cast<std::ptrdiff_t>(z + 1) * stride_z_;
    for (int y = 0; y < N; ++y)
    {
      const std::ptrdiff_t row = z_base + static_cast<std::ptrdiff_t>(y + 1) * stride_y_ + 1;
      for (int x = 0; x < N; ++x)
      {
        const std::ptrdiff_t idx = row + static_cast<std::ptrdiff_t>(x);

        if (!prev_dirty_[static_cast<std::size_t>(idx)])
        {
          dirty_[static_cast<std::size_t>(idx)] = 0;
          continue;
        }

        bool d = (g[idx] != DEAD);
        if (!d)
          for (int k = 0; k < 26 && !d; ++k)
            d = (g[idx + offs[k]] != DEAD);

        const std::uint8_t flag               = d ? std::uint8_t{1} : std::uint8_t{0};
        dirty_[static_cast<std::size_t>(idx)] = flag;
        dirty_count_ += flag;
      }
    }
  }
}

// ── Tile active-set ───────────────────────────────────────────────────────────

void Simulation::rebuild_tile_set() noexcept
{
  const int N   = grid_dimension_;
  const int tpa = tiles_per_axis_;

  std::fill(tile_active_.begin(), tile_active_.end(), std::uint8_t{0});

  for (int tz = 0; tz < tpa; ++tz)
  {
    const int z0 = tz * TILE_DIM;
    const int z1 = std::min(z0 + TILE_DIM, N);

    for (int ty = 0; ty < tpa; ++ty)
    {
      const int y0 = ty * TILE_DIM;
      const int y1 = std::min(y0 + TILE_DIM, N);

      for (int tx = 0; tx < tpa; ++tx)
      {
        const int x0 = tx * TILE_DIM;
        const int x1 = std::min(x0 + TILE_DIM, N);
        const int ti = tz * tpa * tpa + ty * tpa + tx;

        bool active = false;
        for (int z = z0; z < z1 && !active; ++z)
        {
          const std::ptrdiff_t z_base = static_cast<std::ptrdiff_t>(z + 1) * stride_z_;
          for (int y = y0; y < y1 && !active; ++y)
          {
            const std::ptrdiff_t row = z_base + static_cast<std::ptrdiff_t>(y + 1) * stride_y_ + 1;
            for (int x = x0; x < x1 && !active; ++x)
              active = (dirty_[static_cast<std::size_t>(row + x)] != 0);
          }
        }

        tile_active_[static_cast<std::size_t>(ti)] = active ? std::uint8_t{1} : std::uint8_t{0};
      }
    }
  }
}

// ── dirty_ratio helper ────────────────────────────────────────────────────────

float Simulation::dirty_ratio() const noexcept
{
  const std::size_t N3 = static_cast<std::size_t>(grid_dimension_)
                         * static_cast<std::size_t>(grid_dimension_)
                         * static_cast<std::size_t>(grid_dimension_);
  if (N3 == 0)
    return 0.0f;
  return static_cast<float>(dirty_count_) / static_cast<float>(N3);
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
              total_v           = _mm256_adds_epu8(       // NOLINT(portability-simd-intrinsics)
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
  const int   tpa     = tiles_per_axis_;
  const int   n_tiles = tpa * tpa * tpa;
  const auto* offsets = neighbor_offsets_.data();
  const auto* d_data  = dirty_.data();

  counts.fill(0);
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

    if (!tile_active_[static_cast<std::size_t>(ti)])
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
  rebuild_dirty_full();
  rebuild_tile_set();

  for (int gen = 0; gen <= generations_; ++gen)
  {
    debug_printer_.print_generation(static_cast<std::uint64_t>(gen), grid_,
                                    static_cast<std::uint64_t>(grid_dimension_),
                                    static_cast<std::uint64_t>(ghost_dim_));

    std::array<std::uint64_t, generator::N_SPECIES + 1> counts{};
    counts.fill(0);

    if (dirty_ratio() > DENSE_THRESHOLD)
    {
      ++dense_gens_;
      step_generation_dense(grid_, next_grid_, counts);
      grid_.swap(next_grid_);
      fill_ghost_cells(grid_);
      rebuild_dirty_full();
    }
    else
    {
      ++sparse_gens_;
      step_generation_sparse(grid_, next_grid_, counts);
      grid_.swap(next_grid_);
      fill_ghost_cells(grid_);

      dirty_.swap(prev_dirty_);
      rebuild_dirty_incremental();
    }

    rebuild_tile_set();
    update_maxima_for_generation(counts, static_cast<std::uint64_t>(gen));
  }

  std::fprintf(stderr, "mode: dense=%zu sparse=%zu threshold=%.0f%%\n", dense_gens_, sparse_gens_,
               static_cast<double>(DENSE_THRESHOLD) * 100.0);
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
