// simulation.cpp - C++23
#include "simulation.hpp"

#include <cmath>
#include <iostream>

Simulation::Simulation(int number_of_generations, int grid_dimension, float initial_density, int random_seed)
    : generations_(number_of_generations), grid_dimension_(grid_dimension), density_(initial_density),
      seed_(random_seed), grid_(static_cast<std::size_t>(grid_dimension) * static_cast<std::size_t>(grid_dimension) *
                                    static_cast<std::size_t>(grid_dimension),
                                0) {
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

int Simulation::total_alive_neighbors(int x, int y, int z) const {
  const int n         = grid_dimension_;
  int total_neighbors = 0;

  for (int dz = -1; dz <= 1; ++dz) {
    int nz = z + dz;
    if (nz < 0) {
      nz += n;
    } else if (nz >= n) {
      nz -= n;
    }

    for (int dy = -1; dy <= 1; ++dy) {
      int ny = y + dy;
      if (ny < 0) {
        ny += n;
      } else if (ny >= n) {
        ny -= n;
      }

      for (int dx = -1; dx <= 1; ++dx) {
        if (dx == 0 && dy == 0 && dz == 0) {
          continue; // skip self
        }

        int nx = x + dx;
        if (nx < 0) {
          nx += n;
        } else if (nx >= n) {
          nx -= n;
        }

        const std::size_t nidx = index_3d(nx, ny, nz);
        if (grid_[nidx] != static_cast<unsigned char>(0)) {
          ++total_neighbors;
        }
      }
    }
  }

  return total_neighbors;
}

void Simulation::run() {
  const int n                     = grid_dimension_;
  std::vector<unsigned char> next = grid_;

  for (int gen = 1; gen <= generations_; ++gen) {
    std::array<std::uint64_t, generator::N_SPECIES + 1> counts{}; // zero-initialized

    for (int z = 0; z < n; ++z) {
      for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
          const std::size_t idx       = index_3d(x, y, z);
          const unsigned char current = grid_[idx];

          const int total_neighbors = total_alive_neighbors(x, y, z);

          unsigned char new_value;
          if (total_neighbors >= 5 && total_neighbors <= 13) {
            // Cell keeps current state (alive stays same species, dead stays dead)
            new_value = current;
          } else {
            // Cell dies / stays dead
            new_value = static_cast<unsigned char>(0);
          }

          next[idx] = new_value;

          if (new_value != static_cast<unsigned char>(0)) {
            counts[static_cast<std::size_t>(new_value)]++;
          }
        }
      }
    }

    // Update maxima per species
    for (int s = 1; s <= generator::N_SPECIES; ++s) {
      const std::size_t si = static_cast<std::size_t>(s);
      if (counts[si] > max_count_[si]) {
        max_count_[si] = counts[si];
        max_gen_[si]   = gen;
      }
    }

    grid_.swap(next);
  }
}

void Simulation::print_results() const {
  for (int s = 1; s <= generator::N_SPECIES; ++s) {
    std::cout << s << ' ' << max_count_[static_cast<std::size_t>(s)] << ' ' << max_gen_[static_cast<std::size_t>(s)]
              << '\n';
  }
}
