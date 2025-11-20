// generator.cpp - C++23
#include "generator.hpp"

#include <cstdint>
#include <limits>

namespace generator {

static std::uint32_t g_seed = 0;

// Initialize the RNG state.
void init_r4uni(std::int32_t input_seed) {
  // Just mix the user seed into a 32-bit state; reproducible and simple.
  g_seed = static_cast<std::uint32_t>(input_seed) + 987654321u;
}

// Very simple xorshift32-based RNG in [0, 1).
float r4_uni() {
  // xorshift32
  g_seed ^= (g_seed << 13);
  g_seed ^= (g_seed >> 17);
  g_seed ^= (g_seed << 5);

  constexpr float inv_2_32 = 1.0f / 4294967296.0f; // 1 / 2^32
  return static_cast<float>(g_seed) * inv_2_32;    // in [0,1)
}

unsigned char*** gen_initial_grid(std::uint64_t N, float density, std::int32_t input_seed) {
  const std::size_t N_sz = static_cast<std::size_t>(N);

  // Allocate grid[x][y][z]
  auto grid = new unsigned char**[N_sz];
  for (std::size_t x = 0; x < N_sz; ++x) {
    grid[x] = new unsigned char*[N_sz];
    for (std::size_t y = 0; y < N_sz; ++y) {
      grid[x][y] = new unsigned char[N_sz];
    }
  }

  init_r4uni(input_seed);

  for (std::size_t x = 0; x < N_sz; ++x) {
    for (std::size_t y = 0; y < N_sz; ++y) {
      for (std::size_t z = 0; z < N_sz; ++z) {
        unsigned char value = 0;

        if (r4_uni() < density) {
          // species in [1, N_SPECIES]
          std::uint32_t raw     = static_cast<std::uint32_t>(r4_uni() * static_cast<float>(N_SPECIES));
          std::uint32_t species = raw + 1u;
          if (species == 0u || species > N_SPECIES) {
            species = N_SPECIES;
          }
          value = static_cast<unsigned char>(species);
        }

        grid[x][y][z] = value;
      }
    }
  }

  return grid;
}

void free_grid(unsigned char*** grid, std::uint64_t N) {
  if (!grid) {
    return;
  }

  const std::size_t N_sz = static_cast<std::size_t>(N);

  for (std::size_t x = 0; x < N_sz; ++x) {
    if (!grid[x]) {
      continue;
    }
    for (std::size_t y = 0; y < N_sz; ++y) {
      delete[] grid[x][y];
    }
    delete[] grid[x];
  }
  delete[] grid;
}

} // namespace generator
