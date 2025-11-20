// generator.cpp - C++23
#include "generator.hpp"

#include <cstdlib>
#include <new>

namespace generator {

static std::uint32_t g_seed = 0;

// Port of the original init_r4uni / r4_uni for reproducible results.
void init_r4uni(std::int32_t input_seed) {
  // Mix the user-provided seed into an internal 32-bit state.
  g_seed = static_cast<std::uint32_t>(input_seed) + 987654321u;
}

float r4_uni() {
  // Record the previous state so the sequence matches the original as closely
  // as possible given the new types.
  const std::int32_t seed_in = static_cast<std::int32_t>(g_seed);

  // xorshift-style update of g_seed
  g_seed ^= (g_seed << 13);
  g_seed ^= (g_seed >> 17);
  g_seed ^= (g_seed << 5);

  // Matches the original scaling
  return 0.5f + 0.2328306e-09f * static_cast<float>(seed_in + static_cast<std::int32_t>(g_seed));
}

char*** gen_initial_grid(std::uint64_t N, float density, std::int32_t input_seed) {
  // Allocate 3D grid as grid[x][y][z] using straightforward triple-new.
  // Simple to read and free; not optimized for cache.
  auto grid = new char**[static_cast<std::size_t>(N)];
  for (std::uint64_t x = 0; x < N; ++x) {
    grid[x] = new char*[static_cast<std::size_t>(N)];
    for (std::uint64_t y = 0; y < N; ++y) {
      grid[x][y] = new char[static_cast<std::size_t>(N)]{}; // zero-initialize
    }
  }

  init_r4uni(input_seed);

  for (std::uint64_t x = 0; x < N; ++x) {
    for (std::uint64_t y = 0; y < N; ++y) {
      for (std::uint64_t z = 0; z < N; ++z) {
        if (r4_uni() < density) {
          // species in [1, N_SPECIES]
          std::uint32_t species = static_cast<std::uint32_t>(r4_uni() * static_cast<float>(N_SPECIES)) + 1u;

          if (species > N_SPECIES) {
            species = N_SPECIES;
          }

          grid[x][y][z] = static_cast<char>(species);
        } else {
          grid[x][y][z] = 0;
        }
      }
    }
  }

  return grid;
}

void free_grid(char*** grid, std::uint64_t N) {
  if (!grid) {
    return;
  }

  for (std::uint64_t x = 0; x < N; ++x) {
    if (!grid[x]) {
      continue;
    }
    for (std::uint64_t y = 0; y < N; ++y) {
      delete[] grid[x][y];
    }
    delete[] grid[x];
  }
  delete[] grid;
}

} // namespace generator
