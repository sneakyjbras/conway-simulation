// generator.cpp – C++23
//
// Generates the initial grid using the same xorshift RNG as the original
// code so results remain bit-for-bit reproducible.
// Output is now a flat std::vector instead of a char*** triple-pointer,
// eliminating N² separate heap allocations and freeing the caller from
// manual memory management.

#include "generator.hpp"

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

} // namespace

// ── Public API ───────────────────────────────────────────────────────────────

std::vector<unsigned char> gen_initial_grid(std::uint64_t N, float density, std::int32_t input_seed)
{
  // Flat layout: cell(x, y, z) = grid[z*N*N + y*N + x]
  // (Same z-major order used by the Simulation class so the copy-in is a
  // simple element-wise transfer without any axis permutation.)
  const std::size_t total =
      static_cast<std::size_t>(N) * static_cast<std::size_t>(N) * static_cast<std::size_t>(N);
  std::vector<unsigned char> grid(total, 0);

  init_r4uni(input_seed);

  // Iterate in the same x-outer, z-inner order as the original so the RNG
  // sequence — and therefore the initial state — is identical.
  std::size_t idx = 0;
  for (std::uint64_t x = 0; x < N; ++x)
  {
    for (std::uint64_t y = 0; y < N; ++y)
    {
      for (std::uint64_t z = 0; z < N; ++z)
      {
        if (r4_uni() < density)
        {
          auto species = static_cast<std::uint32_t>(r4_uni() * static_cast<float>(N_SPECIES)) + 1u;
          if (species > N_SPECIES)
          {
            species = N_SPECIES;
          }
          // Store using the z-major flat layout.
          const std::size_t flat_idx =
              (static_cast<std::size_t>(z) * N + static_cast<std::size_t>(y)) * N
              + static_cast<std::size_t>(x);
          grid[flat_idx] = static_cast<unsigned char>(species);
        }
        ++idx;
      }
    }
  }

  return grid;
}

} // namespace generator
