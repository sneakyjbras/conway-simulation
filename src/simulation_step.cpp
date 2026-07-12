// simulation_step.cpp – C++23
//
// Single Responsibility: PHYSICS (the per-generation hot path).
// The two cellular-automaton step kernels and the majority-species helper they
// share.  These functions read `current`, write `next`, accumulate per-species
// `counts`, and (sparse only) record `changes_`.  They mutate no other state.
//
// The hot per-cell loops are branchless: the survival test is a bitwise-&
// mask (no short-circuit), the per-species neighbour scan is unconditional
// (dead neighbours land in sp[0], which pick_majority_species never reads),
// and only the rare birth path sits behind an [[unlikely]] branch.

#include "simulation.hpp"

#ifdef __AVX2__
#include <immintrin.h>
#endif

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
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

          // Branchless survival — this is the hot path and, at high density,
          // `cur != DEAD` is ~50/50 and mispredicts constantly.  is_alive is a
          // mask combined with bitwise & (not &&, which would short-circuit
          // back into a branch).  counts[0] is never incremented because
          // is_alive is 0 exactly when cur is DEAD, so this matches the old
          // `if (cur != DEAD) counts[cur]++`.
          const bool is_alive = (cur != DEAD);
          counts[static_cast<std::size_t>(cur)] += static_cast<std::uint64_t>(is_alive);

          // survives ⇔ alive AND total ∈ [5,13].  The unsigned subtraction
          // folds the two-sided range test into one compare: (t-5) ∈ [0,8].
          const bool    survives = is_alive & (static_cast<unsigned>(total - 5) <= 8U);
          unsigned char new_val  = static_cast<unsigned char>(static_cast<int>(cur) * survives);

          // Birth is rare (dead cell, total ∈ [7,10]); keep it behind an
          // unlikely branch so the per-species scan runs only when needed.
          // sp[v]++ is unconditional — dead neighbours land in sp[0], which
          // pick_majority_species never reads (it scans s = 1..N_S).
          const bool birth_ok = (!is_alive) & (static_cast<unsigned>(total - 7) <= 3U);
          if (birth_ok) [[unlikely]]
          {
            sp.fill(0);
            for (int k = 0; k < 26; ++k)
              sp[static_cast<std::size_t>((p + i)[offsets[k]])]++;
            new_val = static_cast<unsigned char>(pick_majority_species(sp));
          }
          next_p[i] = new_val;
        }
      }
#endif

      // Scalar path — handles N % 32 tail, or full row when AVX2 absent.
      for (; x < N; ++x)
      {
        const std::ptrdiff_t idx = row + static_cast<std::ptrdiff_t>(x);
        const auto*          ptr = current.data() + idx;
        const unsigned char  cur = *ptr;

        const bool is_alive = (cur != DEAD);
        counts[static_cast<std::size_t>(cur)] += static_cast<std::uint64_t>(is_alive);

        int total = 0;
        for (int k = 0; k < 26; ++k)
          total += (ptr[offsets[k]] != DEAD);

        const bool    survives = is_alive & (static_cast<unsigned>(total - 5) <= 8U);
        unsigned char new_val  = static_cast<unsigned char>(static_cast<int>(cur) * survives);

        const bool birth_ok = (!is_alive) & (static_cast<unsigned>(total - 7) <= 3U);
        if (birth_ok) [[unlikely]]
        {
          sp.fill(0);
          for (int k = 0; k < 26; ++k)
            sp[static_cast<std::size_t>(ptr[offsets[k]])]++;
          new_val = static_cast<unsigned char>(pick_majority_species(sp));
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

          const bool is_alive = (cur != DEAD);
          counts[static_cast<std::size_t>(cur)] += static_cast<std::uint64_t>(is_alive);

          // Non-dirty cells within an active tile are provably dead.  This
          // branch is kept: it is spatially well-predicted and gates the
          // expensive neighbour scan, so it earns its place (unlike the
          // per-cell alive test, which does not).
          if (!d_data[static_cast<std::size_t>(idx)])
          {
            next[static_cast<std::size_t>(idx)] = DEAD;
            continue;
          }

          int total = 0;
          for (int k = 0; k < 26; ++k)
            total += (ptr[offsets[k]] != DEAD);

          const bool    survives = is_alive & (static_cast<unsigned>(total - 5) <= 8U);
          unsigned char new_val  = static_cast<unsigned char>(static_cast<int>(cur) * survives);

          const bool birth_ok = (!is_alive) & (static_cast<unsigned>(total - 7) <= 3U);
          if (birth_ok) [[unlikely]]
          {
            sp.fill(0);
            for (int k = 0; k < 26; ++k)
              sp[static_cast<std::size_t>(ptr[offsets[k]])]++;
            new_val = static_cast<unsigned char>(pick_majority_species(sp));
          }
          next[static_cast<std::size_t>(idx)] = new_val;
          if (new_val != cur)
            changes_.emplace_back(idx, new_val);
        }
      }
    }
  }
}
