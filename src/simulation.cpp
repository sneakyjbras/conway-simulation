// simulation.cpp - C++23
#include "simulation.hpp"

#include <cmath>
#include <iostream>

Simulation::Simulation(int number_of_generations, int grid_dimension, float initial_density, int random_seed)
    : generations_(number_of_generations), grid_dimension_(grid_dimension), density_(initial_density),
      seed_(random_seed), grid_(static_cast<std::size_t>(grid_dimension) * grid_dimension * grid_dimension, 0) {
  // Init maxima arrays
  for (std::size_t s = 1; s <= static_cast<std::size_t>(generator::N_SPECIES); ++s) {
    max_count_[s] = 0;
    max_gen_[s]   = 0;
  }
  initialize_from_generator();
}

Simulation::~Simulation() = default;

std::size_t Simulation::index_3d(int x, int y, int z) const {
  const std::size_t n = static_cast<std::size_t>(grid_dimension_);
  return (static_cast<std::size_t>(z) * n + static_cast<std::size_t>(y)) * n + static_cast<std::size_t>(x);
}

int Simulation::neighbors_count(int x, int y, int z, unsigned char species) const {
  const int n = grid_dimension_;
  int count   = 0;
  for (int dz = -1; dz <= 1; ++dz) {
    for (int dy = -1; dy <= 1; ++dy) {
      for (int dx = -1; dx <= 1; ++dx) {
        if (dx == 0 && dy == 0 && dz == 0)
          continue;
        const int nx = (x + dx + n) % n;
        const int ny = (y + dy + n) % n;
        const int nz = (z + dz + n) % n;
        if (grid_[index_3d(nx, ny, nz)] == species)
          ++count;
      }
    }
  }
  return count;
}

void Simulation::initialize_from_generator() {
  // Get a temporary grid from the generator, copy it into our flat vector, then free it.
  char*** g = generator::gen_initial_grid(grid_dimension_, density_, seed_);
  for (int x = 0; x < grid_dimension_; ++x) {
    for (int y = 0; y < grid_dimension_; ++y) {
      for (int z = 0; z < grid_dimension_; ++z) {
        grid_[index_3d(x, y, z)] = static_cast<unsigned char>(g[x][y][z]);
      }
    }
  }
  generator::free_grid(g, grid_dimension_);
}

void Simulation::run() {
  const int n                     = grid_dimension_;
  std::vector<unsigned char> next = grid_;

  for (int gen = 1; gen <= generations_; ++gen) {
    std::array<std::uint64_t, generator::N_SPECIES + 1> counts{};

    for (int z = 0; z < n; ++z) {
      for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
          unsigned char current = grid_[index_3d(x, y, z)];
          int best_species      = 0;
          int best_count        = 0;

          for (int s = 1; s <= generator::N_SPECIES; ++s) {
            const int c = neighbors_count(x, y, z, static_cast<unsigned char>(s));
            if (c > best_count) {
              best_count   = c;
              best_species = s;
            }
          }

          // Simple rule: if any species has >= 3 neighbors, adopt it; else keep current.
          if (best_count >= 3) {
            next[index_3d(x, y, z)] = static_cast<unsigned char>(best_species);
          } else {
            next[index_3d(x, y, z)] = current;
          }

          ++counts[next[index_3d(x, y, z)]];
        }
      }
    }

    for (int s = 1; s <= generator::N_SPECIES; ++s) {
      if (counts[static_cast<std::size_t>(s)] > max_count_[static_cast<std::size_t>(s)]) {
        max_count_[static_cast<std::size_t>(s)] = counts[static_cast<std::size_t>(s)];
        max_gen_[static_cast<std::size_t>(s)]   = gen;
      }
    }

    grid_.swap(next);
  }
}

void Simulation::print_results() const {
  for (int s = 1; s <= generator::N_SPECIES; ++s) {
    std::cout << s << ' ' << max_gen_[static_cast<std::size_t>(s)] << '\n';
  }
}
