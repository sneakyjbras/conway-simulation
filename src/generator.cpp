// generator.cpp - C++23
#include "generator.hpp"

#include <cstdlib>
#include <new>

namespace generator {

static unsigned int g_seed = 0;

// Port of the original init_r4uni / r4_uni for reproducible results.
void init_r4uni(int input_seed) {
  g_seed = static_cast<unsigned int>(input_seed) + 987654321u;
}

float r4_uni() {
  int seed_in = static_cast<int>(g_seed);

  g_seed ^= (g_seed << 13);
  g_seed ^= (g_seed >> 17);
  g_seed ^= (g_seed << 5);

  // Matches the original scaling
  return 0.5f + 0.2328306e-09f * static_cast<float>(seed_in + static_cast<int>(g_seed));
}

char*** gen_initial_grid(int N, float density, int input_seed) {
  // Allocate 3D grid as grid[x][y][z] using straightforward triple-new.
  // Simple to read and free; not optimized for cache.
  auto grid = new char**[static_cast<std::size_t>(N)];
  for (int x = 0; x < N; ++x) {
    grid[x] = new char*[static_cast<std::size_t>(N)];
    for (int y = 0; y < N; ++y) {
      grid[x][y] = new char[static_cast<std::size_t>(N)]{}; // zero-initialize
    }
  }

  init_r4uni(input_seed);

  for (int x = 0; x < N; ++x) {
    for (int y = 0; y < N; ++y) {
      for (int z = 0; z < N; ++z) {
        if (r4_uni() < density) {
          // species in [1, N_SPECIES]
          int species = static_cast<int>(r4_uni() * static_cast<float>(N_SPECIES)) + 1;
          if (species < 1)
            species = 1;
          if (species > N_SPECIES)
            species = N_SPECIES;
          grid[x][y][z] = static_cast<char>(species);
        } else {
          grid[x][y][z] = 0;
        }
      }
    }
  }

  return grid;
}

void free_grid(char*** grid, int N) {
  if (!grid)
    return;
  for (int x = 0; x < N; ++x) {
    if (!grid[x])
      continue;
    for (int y = 0; y < N; ++y) {
      delete[] grid[x][y];
    }
    delete[] grid[x];
  }
  delete[] grid;
}

} // namespace generator
