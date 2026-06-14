// generator.cpp – C++23
//
// Generates the initial grid using the same xorshift RNG as the original
// code so results remain bit-for-bit reproducible.
// Output is now a flat std::vector instead of a char*** triple-pointer,
// eliminating N² separate heap allocations and freeing the caller from
// manual memory management.

#include "generator.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

namespace generator
{

namespace
{

// ── RNG ─────────────────────────────────────────────────────────────────────
// Kept as a plain translation-unit static so the sequence matches the
// original exactly.  Thread safety is not required here: generation happens
// once, serially, before any parallel work begins.

static std::uint32_t g_seed = 0;

void init_r4uni(std::int32_t input_seed) noexcept
{
  g_seed = static_cast<std::uint32_t>(input_seed) + 987654321u;
}

float r4_uni() noexcept
{
  const std::int32_t seed_in = static_cast<std::int32_t>(g_seed);
  g_seed ^= (g_seed << 13);
  g_seed ^= (g_seed >> 17);
  g_seed ^= (g_seed << 5);
  return 0.5f + 0.2328306e-09f * static_cast<float>(seed_in + static_cast<std::int32_t>(g_seed));
}

// Pick a species label in [1, N_SPECIES] from a uniform draw.
[[nodiscard]] unsigned char pick_species() noexcept
{
  auto species = static_cast<std::uint32_t>(r4_uni() * static_cast<float>(N_SPECIES)) + 1u;
  if (species > N_SPECIES)
    species = N_SPECIES;
  return static_cast<unsigned char>(species);
}

// ── Uniform fill ────────────────────────────────────────────────────────────
// Bit-for-bit identical to the original implementation: same x-outer/z-inner
// iteration order and same RNG call sequence.  Do not modify — the golden
// regression files depend on this exact sequence.
void fill_uniform(std::vector<unsigned char>& grid, std::uint64_t N, float density) noexcept
{
  for (std::uint64_t x = 0; x < N; ++x)
    for (std::uint64_t y = 0; y < N; ++y)
      for (std::uint64_t z = 0; z < N; ++z)
      {
        if (r4_uni() < density)
        {
          const std::size_t flat_idx =
              (static_cast<std::size_t>(z) * N + static_cast<std::size_t>(y)) * N
              + static_cast<std::size_t>(x);
          grid[flat_idx] = pick_species();
        }
      }
}

// ── Gaussian fill ───────────────────────────────────────────────────────────
// Alive cells cluster toward the grid centre.  For each cell we compute its
// normalised radial distance from the centre and accept it with a Gaussian
// probability density × exp(−r² / 2σ²).  σ is a fixed fraction of the grid so
// the cluster scales with N.  Uses the same xorshift RNG for reproducibility.
//
// This produces a spikier alive distribution than uniform — denser core,
// sparser edges — which is the case the larger Gaussian scratch-reserve factor
// is designed to absorb.
void fill_gaussian(std::vector<unsigned char>& grid, std::uint64_t N, float density) noexcept
{
  const float center  = 0.5f * static_cast<float>(N - 1);
  const float sigma   = 0.25f * static_cast<float>(N); // cluster radius ≈ N/4
  const float inv_2s2 = (sigma > 0.0f) ? 1.0f / (2.0f * sigma * sigma) : 0.0f;

  for (std::uint64_t x = 0; x < N; ++x)
    for (std::uint64_t y = 0; y < N; ++y)
      for (std::uint64_t z = 0; z < N; ++z)
      {
        const float dx = static_cast<float>(x) - center;
        const float dy = static_cast<float>(y) - center;
        const float dz = static_cast<float>(z) - center;
        const float r2 = dx * dx + dy * dy + dz * dz;
        const float p  = density * std::exp(-r2 * inv_2s2);

        if (r4_uni() < p)
        {
          const std::size_t flat_idx =
              (static_cast<std::size_t>(z) * N + static_cast<std::size_t>(y)) * N
              + static_cast<std::size_t>(x);
          grid[flat_idx] = pick_species();
        }
      }
}

} // namespace

// ── Public API ───────────────────────────────────────────────────────────────

std::vector<unsigned char>
gen_initial_grid(std::uint64_t N, float density, std::int32_t input_seed, DistributionType dist)
{
  // Flat layout: cell(x, y, z) = grid[z*N*N + y*N + x]
  // (Same z-major order used by the Simulation class so the copy-in is a
  // simple element-wise transfer without any axis permutation.)
  const std::size_t total =
      static_cast<std::size_t>(N) * static_cast<std::size_t>(N) * static_cast<std::size_t>(N);
  std::vector<unsigned char> grid(total, 0);

  init_r4uni(input_seed);

  switch (dist)
  {
  case DistributionType::kGaussian:
    fill_gaussian(grid, N, density);
    break;
  case DistributionType::kUniform:
  default:
    fill_uniform(grid, N, density);
    break;
  }

  return grid;
}

} // namespace generator
