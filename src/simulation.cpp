// simulation.cpp - C++23
#include "simulation.hpp"

#include <print> // changed from <iostream>

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

int Simulation::total_alive_neighbors(int x, int y, int z,
                                      std::array<int, generator::N_SPECIES + 1>* species_counts) const {

  int total   = 0;
  const int n = grid_dimension_;

  for (int dz = -1; dz <= 1; ++dz) {
    int nz = z + dz;
    if (nz < 0)
      nz += n;
    else if (nz >= n)
      nz -= n;

    for (int dy = -1; dy <= 1; ++dy) {
      int ny = y + dy;
      if (ny < 0)
        ny += n;
      else if (ny >= n)
        ny -= n;

      for (int dx = -1; dx <= 1; ++dx) {
        if (dx == 0 && dy == 0 && dz == 0) {
          continue; // skip the cell itself
        }

        int nx = x + dx;
        if (nx < 0)
          nx += n;
        else if (nx >= n)
          nx -= n;

        const auto idx     = index_3d(nx, ny, nz);
        const auto species = grid_[idx];

        if (species != static_cast<unsigned char>(0)) {
          ++total;

          if (species_counts != nullptr) {
            (*species_counts)[static_cast<std::size_t>(species)]++;
          }
        }
      }
    }
  }

  return total;
}

int Simulation::total_alive_neighbors(int x, int y, int z) const {
  // Alive-cell path: we only care about the total, not per-species.
  return total_alive_neighbors(x, y, z, nullptr);
}

void Simulation::run() {
  // Allocate once; we just overwrite contents every generation.
  std::vector<unsigned char> next(grid_.size());

  for (int gen = 0; gen < generations_; ++gen) {
    // Per-generation counts for maxima tracking
    std::array<std::uint64_t, generator::N_SPECIES + 1> counts{};
    counts.fill(0);

    for (int z = 0; z < grid_dimension_; ++z) {
      for (int y = 0; y < grid_dimension_; ++y) {
        for (int x = 0; x < grid_dimension_; ++x) {
          const auto idx     = index_3d(x, y, z);
          const auto current = grid_[idx];

          unsigned char new_value = 0;

          if (current != static_cast<unsigned char>(0)) {
            // ALIVE cell: use simple total count.
            const int total_neighbors = total_alive_neighbors(x, y, z);

            // Your survival rule: stays alive if 5–13 neighbors.
            if (total_neighbors >= 5 && total_neighbors <= 13) {
              new_value = current;
            }
          } else {
            // DEAD cell: we need per-species neighbor counts.
            std::array<int, generator::N_SPECIES + 1> species_counts{};
            species_counts.fill(0);

            const int total_neighbors = total_alive_neighbors(x, y, z, &species_counts);

            // Your birth rule: 7–10 neighbors -> cell becomes alive.
            if (total_neighbors >= 7 && total_neighbors <= 10) {
              int best_species      = 0;
              int best_count        = 0;
              const int max_species = static_cast<int>(generator::N_SPECIES);

              // Pick species with highest count.
              // Tie-breaker: lowest species id wins.
              for (int s = 1; s <= max_species; ++s) {
                const int c = species_counts[static_cast<std::size_t>(s)];

                if (c > best_count) {
                  best_count   = c;
                  best_species = s;
                } else if (c == best_count && c > 0 && s < best_species) {
                  best_species = s;
                }
              }

              if (best_count > 0) {
                new_value = static_cast<unsigned char>(best_species);
              }
            }
          }

          next[idx] = new_value;

          if (new_value != static_cast<unsigned char>(0)) {
            counts[static_cast<std::size_t>(new_value)]++;
          }
        }
      }
    }

    // Update maxima per species for this generation
    const auto max_species = static_cast<std::size_t>(generator::N_SPECIES);
    for (std::size_t s = 1; s <= max_species; ++s) {
      if (counts[s] > max_count_[s]) {
        max_count_[s] = counts[s];
        max_gen_[s]   = static_cast<std::uint64_t>(gen);
      }
    }

    grid_.swap(next);
  }
}

void Simulation::print_results() const {
  for (int s = 1; s <= generator::N_SPECIES; ++s) {
    std::println("{} {} {}", s, max_count_[static_cast<std::size_t>(s)], max_gen_[static_cast<std::size_t>(s)]);
  }
}
