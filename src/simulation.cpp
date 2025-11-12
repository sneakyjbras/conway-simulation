#include "simulation.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <format>
#include <print>
#include <random>

namespace {
// Named constants to avoid magic numbers
inline constexpr int kMooreNeighborhood = 26; // 3x3x3 minus center
} // namespace

// Simple xorshift32 RNG -> uniform [0,1)
auto r4_uni(std::uint32_t& state) -> float {
  // xorshift32
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  // Convert to float in [0,1)
  constexpr float inv_2_32 = 1.0f / 4294967296.0f;
  return static_cast<float>(state) * inv_2_32;
}

Simulation::Simulation(int number_of_generations, int grid_dimension, float initial_density, int random_seed)
    : generations_(number_of_generations), grid_dimension_(grid_dimension), density_(initial_density),
      seed_(random_seed), grid_(static_cast<std::size_t>(grid_dimension) * grid_dimension * grid_dimension, 0) {
  for (std::size_t s = 1; s <= N_SPECIES; ++s) {
    max_count_[s] = 0;
    max_gen_[s]   = 0;
  }
  initialize_from_generator();
}

Simulation::~Simulation() = default;

auto Simulation::index_3d(int coordinate_x, int coordinate_y, int coordinate_z) const -> std::size_t {
  const std::size_t n = static_cast<std::size_t>(grid_dimension_);
  return (static_cast<std::size_t>(coordinate_z) * n + static_cast<std::size_t>(coordinate_y)) * n +
         static_cast<std::size_t>(coordinate_x);
}

auto Simulation::neighbors_count(int coordinate_x, int coordinate_y, int coordinate_z, unsigned char species) const
    -> int {
  const int n = grid_dimension_;
  int count   = 0;
  for (int dz = -1; dz <= 1; ++dz) {
    for (int dy = -1; dy <= 1; ++dy) {
      for (int dx = -1; dx <= 1; ++dx) {
        if (dx == 0 && dy == 0 && dz == 0)
          continue;
        const int nx     = (coordinate_x + dx + n) % n;
        const int ny     = (coordinate_y + dy + n) % n;
        const int nz     = (coordinate_z + dz + n) % n;
        const auto value = grid_[index_3d(nx, ny, nz)];
        if (value == species)
          ++count;
      }
    }
  }
  return count;
}

auto Simulation::initialize_from_generator() -> void {
  // Fill grid with species according to initial density and seed
  std::uint32_t state = static_cast<std::uint32_t>(seed_);
  const int n         = grid_dimension_;
  for (int z = 0; z < n; ++z) {
    for (int y = 0; y < n; ++y) {
      for (int x = 0; x < n; ++x) {
        float u = r4_uni(state);
        if (u < density_) {
          // choose species uniformly in [1, N_SPECIES]
          const unsigned species =
              1u + static_cast<unsigned>(std::floor(r4_uni(state) * static_cast<float>(N_SPECIES)));
          grid_[index_3d(x, y, z)] = static_cast<unsigned char>(species);
        } else {
          grid_[index_3d(x, y, z)] = 0;
        }
      }
    }
  }
}

auto Simulation::run() -> void {
  // Very simple evolution: for each generation, for each cell
  // adopt the most common neighbor species (if any), else stay.
  const int n                     = grid_dimension_;
  std::vector<unsigned char> next = grid_;
  for (int gen = 1; gen <= generations_; ++gen) {
    std::array<std::uint64_t, N_SPECIES + 1> counts{};

    for (int z = 0; z < n; ++z) {
      for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
          unsigned char current = grid_[index_3d(x, y, z)];
          // Tally neighbors by species
          int best_species = 0;
          int best_count   = 0;
          for (int s = 1; s <= static_cast<int>(N_SPECIES); ++s) {
            const int c = neighbors_count(x, y, z, static_cast<unsigned char>(s));
            if (c > best_count) {
              best_count   = c;
              best_species = s;
            }
          }
          // simple rule: if any species has >=3 neighbors, adopt it
          if (best_count >= 3) {
            next[index_3d(x, y, z)] = static_cast<unsigned char>(best_species);
          } else {
            next[index_3d(x, y, z)] = current;
          }
          ++counts[next[index_3d(x, y, z)]];
        }
      }
    }

    // Track maxima
    for (std::size_t s = 1; s <= N_SPECIES; ++s) {
      if (counts[s] > max_count_[s]) {
        max_count_[s] = counts[s];
        max_gen_[s]   = gen;
      }
    }

    grid_.swap(next);
  }
}

auto Simulation::print_results() const -> void {
  // Exactly 9 lines, species 1..9, printed to stdout.
  for (std::size_t s = 1; s <= N_SPECIES; ++s) {
    std::print("{} {}\n", s, max_gen_[s]);
  }
}

// Legacy helper to satisfy existing callers/tests.
auto gen_initial_grid(int grid_dimension, float density, int input_seed) -> char*** {
  // Allocate a 3D C-array shape: grid[z][y][x]
  const int n = grid_dimension;
  auto grid   = new char**[static_cast<std::size_t>(n)];
  for (int z = 0; z < n; ++z) {
    grid[z] = new char*[static_cast<std::size_t>(n)];
    for (int y = 0; y < n; ++y) {
      grid[z][y] = new char[static_cast<std::size_t>(n)]{};
    }
  }

  std::uint32_t state = static_cast<std::uint32_t>(input_seed);
  for (int z = 0; z < n; ++z) {
    for (int y = 0; y < n; ++y) {
      for (int x = 0; x < n; ++x) {
        float u = r4_uni(state);
        if (u < density) {
          const unsigned species =
              1u + static_cast<unsigned>(std::floor(r4_uni(state) * static_cast<float>(N_SPECIES)));
          grid[z][y][x] = static_cast<char>(species);
        } else {
          grid[z][y][x] = 0;
        }
      }
    }
  }
  return grid;
}

auto Simulation::free_grid(char*** grid, int grid_dimension) -> void {
  if (!grid)
    return;
  for (int z = 0; z < grid_dimension; ++z) {
    for (int y = 0; y < grid_dimension; ++y) {
      delete[] grid[z][y];
    }
    delete[] grid[z];
  }
  delete[] grid;
}
