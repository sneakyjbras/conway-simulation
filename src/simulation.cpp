// simulation.cpp - C++23
#include "simulation.hpp"

#include "debug_printer.hpp"

#include <print> // instead of <iostream>

Simulation::Simulation(int number_of_generations, int grid_dimension, float initial_density, int random_seed)
    : generations_(number_of_generations), grid_dimension_(grid_dimension), density_(initial_density),
      seed_(random_seed), grid_(static_cast<std::size_t>(grid_dimension) * static_cast<std::size_t>(grid_dimension) *
                                    static_cast<std::size_t>(grid_dimension),
                                0),
      use_bitmask_wrap_{false}, wrap_mask_{0}, debug_printer_{false} {

  // Configure fast toroidal wrap-around if the dimension is a power of two.
  if (grid_dimension_ > 0 && (grid_dimension_ & (grid_dimension_ - 1)) == 0) {
    use_bitmask_wrap_ = true;
    wrap_mask_        = grid_dimension_ - 1;
  }

  // Init maxima arrays (index 0 is unused, but keep everything clean).
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
  for (int z = 0; z < grid_dimension_; ++z) {
    for (int y = 0; y < grid_dimension_; ++y) {
      for (int x = 0; x < grid_dimension_; ++x) {
        grid_[index_3d(x, y, z)] = static_cast<unsigned char>(g[x][y][z]);
      }
    }
  }
  generator::free_grid(g, grid_dimension_);
}

// Wrapper that preserves the old API.
int Simulation::total_alive_neighbors(int x, int y, int z,
                                      std::array<int, generator::N_SPECIES + 1>* per_species) const {
  if (per_species == nullptr) {
    return alive_neighbors_total(x, y, z);
  }
  return alive_neighbors_with_species(x, y, z, *per_species);
}

int Simulation::total_alive_neighbors(int x, int y, int z) const {
  return alive_neighbors_total(x, y, z);
}

// Alive-cell path: only total, early-exit at >13 neighbors.
int Simulation::alive_neighbors_total(int x, int y, int z) const {
  int total                 = 0;
  const int n               = grid_dimension_;
  constexpr int max_survive = 13;

  if (use_bitmask_wrap_) {
    const int mask = wrap_mask_;
    for (int dz = -1; dz <= 1; ++dz) {
      const int nz = (z + dz) & mask;
      for (int dy = -1; dy <= 1; ++dy) {
        const int ny = (y + dy) & mask;
        for (int dx = -1; dx <= 1; ++dx) {
          if (dx == 0 && dy == 0 && dz == 0) {
            continue; // skip the cell itself
          }

          const int nx        = (x + dx) & mask;
          const auto idx      = index_3d(nx, ny, nz);
          const auto neighbor = grid_[idx];

          if (neighbor != static_cast<unsigned char>(0)) {
            ++total;
            if (total > max_survive) {
              // Early exit: an alive cell with >13 neighbors will die.
              return total;
            }
          }
        }
      }
    }
  } else {
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

          const auto idx      = index_3d(nx, ny, nz);
          const auto neighbor = grid_[idx];

          if (neighbor != static_cast<unsigned char>(0)) {
            ++total;
            if (total > max_survive) {
              return total;
            }
          }
        }
      }
    }
  }

  return total;
}

// Dead-cell path: total + per-species counts, early-exit at >10 neighbors.
int Simulation::alive_neighbors_with_species(int x, int y, int z,
                                             std::array<int, generator::N_SPECIES + 1>& species_counts) const {
  int total               = 0;
  const int n             = grid_dimension_;
  constexpr int max_birth = 10;

  if (use_bitmask_wrap_) {
    const int mask = wrap_mask_;
    for (int dz = -1; dz <= 1; ++dz) {
      const int nz = (z + dz) & mask;
      for (int dy = -1; dy <= 1; ++dy) {
        const int ny = (y + dy) & mask;
        for (int dx = -1; dx <= 1; ++dx) {
          if (dx == 0 && dy == 0 && dz == 0) {
            continue; // skip the cell itself
          }

          const int nx        = (x + dx) & mask;
          const auto idx      = index_3d(nx, ny, nz);
          const auto neighbor = grid_[idx];

          if (neighbor != static_cast<unsigned char>(0)) {
            ++total;
            species_counts[static_cast<std::size_t>(neighbor)]++;

            if (total > max_birth) {
              // Early exit: a dead cell with >10 neighbors will stay dead.
              return total;
            }
          }
        }
      }
    }
  } else {
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

          const auto idx      = index_3d(nx, ny, nz);
          const auto neighbor = grid_[idx];

          if (neighbor != static_cast<unsigned char>(0)) {
            ++total;
            species_counts[static_cast<std::size_t>(neighbor)]++;

            if (total > max_birth) {
              return total;
            }
          }
        }
      }
    }
  }

  return total;
}

void Simulation::run() {
  // Allocate once; we just overwrite contents every generation.
  std::vector<unsigned char> next(grid_.size());

  // Precompute constants used in the tight loops.
  const std::size_t n           = static_cast<std::size_t>(grid_dimension_);
  const std::size_t n2          = n * n;
  const std::size_t max_species = static_cast<std::size_t>(generator::N_SPECIES);

  // Reuse a single histogram for dead cells; we’ll clear it per-use.
  std::array<int, generator::N_SPECIES + 1> species_counts{};

  for (int gen = 0; gen <= generations_; ++gen) {
    // Print current state before evolving to next generation
    debug_printer_.print_generation(gen, grid_, grid_dimension_);

    // Per-generation counts for maxima tracking (of the *current* generation)
    std::array<std::uint64_t, generator::N_SPECIES + 1> counts{};
    counts.fill(0);

    for (int z = 0; z < grid_dimension_; ++z) {
      const std::size_t z_base = static_cast<std::size_t>(z) * n2;

      for (int y = 0; y < grid_dimension_; ++y) {
        const std::size_t base = z_base + static_cast<std::size_t>(y) * n;

        for (int x = 0; x < grid_dimension_; ++x) {
          const std::size_t idx = base + static_cast<std::size_t>(x);
          const auto current    = grid_[idx];

          // Count the current generation’s species BEFORE computing the next one.
          if (current != static_cast<unsigned char>(0)) {
            counts[static_cast<std::size_t>(current)]++;
          }

          unsigned char new_value = 0;

          if (current != static_cast<unsigned char>(0)) {
            // ALIVE cell: use simple total count (with early exit inside).
            const int total_neighbors = alive_neighbors_total(x, y, z);

            // Survival rule: stays alive if 5–13 neighbors.
            if (total_neighbors >= 5 && total_neighbors <= 13) {
              new_value = current;
            }
          } else {
            // DEAD cell: reuse the same species_counts buffer, but reset it.
            species_counts.fill(0);

            const int total_neighbors = alive_neighbors_with_species(x, y, z, species_counts);

            // Birth rule: 7–10 neighbors -> cell becomes alive.
            if (total_neighbors >= 7 && total_neighbors <= 10) {
              int best_species = 0;
              int best_count   = 0;
              const int max_s  = static_cast<int>(generator::N_SPECIES);

              // Pick species with highest count.
              // Tie-breaker: lowest species id wins.
              for (int s = 1; s <= max_s; ++s) {
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
        }
      }
    }

    // Update maxima per species based on the *current* generation (pre-evolution)
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
