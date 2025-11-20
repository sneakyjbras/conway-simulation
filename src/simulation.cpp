// simulation.cpp - C++23
#include "simulation.hpp"

#include "debug_printer.hpp"

#include <print> // instead of <iostream>

Simulation::Simulation(std::uint64_t number_of_generations, std::uint64_t grid_dimension, float initial_density,
                       std::int32_t random_seed)
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

std::size_t Simulation::index_3d(std::uint64_t x, std::uint64_t y, std::uint64_t z) const noexcept {
  const std::size_t n = static_cast<std::size_t>(grid_dimension_);
  return (static_cast<std::size_t>(z) * n + static_cast<std::size_t>(y)) * n + static_cast<std::size_t>(x);
}

void Simulation::initialize_from_generator() {
  // Get a temporary grid from the generator, copy it into our flat vector, then free it.
  char*** g = generator::gen_initial_grid(grid_dimension_, density_, seed_);

  for (std::uint64_t z = 0; z < grid_dimension_; ++z) {
    for (std::uint64_t y = 0; y < grid_dimension_; ++y) {
      for (std::uint64_t x = 0; x < grid_dimension_; ++x) {
        grid_[index_3d(x, y, z)] =
            g[static_cast<std::size_t>(x)][static_cast<std::size_t>(y)][static_cast<std::size_t>(z)];
      }
    }
  }

  generator::free_grid(g, grid_dimension_);
}

// Wrapper that preserves the old API shape (now using 64-bit quantities).
std::uint64_t
Simulation::total_alive_neighbors(std::uint64_t x, std::uint64_t y, std::uint64_t z,
                                  std::array<std::uint64_t, generator::N_SPECIES + 1>* per_species) const {
  if (per_species == nullptr) {
    return alive_neighbors_total(x, y, z);
  }
  return alive_neighbors_with_species(x, y, z, *per_species);
}

std::uint64_t Simulation::total_alive_neighbors(std::uint64_t x, std::uint64_t y, std::uint64_t z) const {
  return alive_neighbors_total(x, y, z);
}

void Simulation::run() {
  // Allocate once; we just overwrite contents every generation.
  std::vector<unsigned char> next(grid_.size());

  for (std::uint64_t gen = 0; gen <= generations_; ++gen) {
    // 1. Print current state before evolving to next generation.
    // DebugPrinter still uses int-based API, so cast.
    debug_printer_.print_generation(static_cast<std::uint64_t>(gen), grid_, grid_dimension_);

    // 2. Per-generation counts for maxima tracking (of the *current* generation).
    std::array<std::uint64_t, generator::N_SPECIES + 1> counts{};
    counts.fill(0);

    // 3. Compute the next generation and fill 'counts' from the current grid.
    step_generation(next, counts);

    // 4. Update per-species maxima using this generation's counts.
    update_maxima_for_generation(counts, gen);

    // 5. Make the newly computed generation the current one.
    grid_.swap(next);
  }
}

int Simulation::pick_majority_species(const std::array<std::uint64_t, generator::N_SPECIES + 1>& species_counts) {
  int best_species         = 0;
  std::uint64_t best_count = 0;
  const int max_s          = static_cast<int>(generator::N_SPECIES);

  for (int s = 1; s <= max_s; ++s) {
    const std::uint64_t c = species_counts[static_cast<std::size_t>(s)];
    if (c > best_count || (c == best_count && c > 0 && s < best_species)) {
      best_count   = c;
      best_species = s;
    }
  }

  // If all counts are zero, this returns 0 (no birth).
  return best_species;
}

void Simulation::step_generation(std::vector<unsigned char>& next,
                                 std::array<std::uint64_t, generator::N_SPECIES + 1>& counts) {
  // Precompute constants used in the tight loops.
  const std::size_t n  = static_cast<std::size_t>(grid_dimension_);
  const std::size_t n2 = n * n;

  // Reuse a single histogram for dead cells; we’ll clear it per-use.
  std::array<std::uint64_t, generator::N_SPECIES + 1> species_counts{};

  // NOTE: The inner triple loop over (z, y, x) for a fixed generation is embarrassingly parallel:
  // - 'grid_' is read-only within this loop
  // - 'next' is write-only at distinct indices
  // To parallelize (e.g. with OpenMP), make 'species_counts' thread-local and
  // reduce 'counts' across threads after the z/y/x loops.
  for (std::uint64_t z = 0; z < grid_dimension_; ++z) {
    const std::size_t z_base = static_cast<std::size_t>(z) * n2;

    for (std::uint64_t y = 0; y < grid_dimension_; ++y) {
      const std::size_t row = z_base + static_cast<std::size_t>(y) * n;

      for (std::uint64_t x = 0; x < grid_dimension_; ++x) {
        const std::size_t idx = row + static_cast<std::size_t>(x);
        const auto current    = grid_[idx];

        // Count the current generation’s species BEFORE computing the next one.
        if (current != sim_detail::DEAD_CELL) {
          counts[static_cast<std::size_t>(current)]++;
        }

        unsigned char new_value = sim_detail::DEAD_CELL;

        if (current != sim_detail::DEAD_CELL) {
          // ALIVE cell: use simple total count (with early exit inside).
          const std::uint64_t total_neighbors = alive_neighbors_total(x, y, z);

          // Survival rule: stays alive if 5–13 neighbors.
          if (total_neighbors >= 5 && total_neighbors <= 13) {
            new_value = current;
          }
        } else {
          // DEAD cell: reuse the same species_counts buffer, but reset it.
          species_counts.fill(0);

          const std::uint64_t total_neighbors = alive_neighbors_with_species(x, y, z, species_counts);

          // Birth rule: 7–10 neighbors -> cell becomes alive.
          if (total_neighbors >= 7 && total_neighbors <= 10) {
            const int best_species = pick_majority_species(species_counts);
            if (best_species != 0) {
              new_value = static_cast<unsigned char>(best_species);
            }
          }
        }

        next[idx] = new_value;
      }
    }
  }
}

void Simulation::update_maxima_for_generation(const std::array<std::uint64_t, generator::N_SPECIES + 1>& counts,
                                              std::uint64_t gen) {
  const std::size_t max_species = static_cast<std::size_t>(generator::N_SPECIES);

  for (std::size_t s = 1; s <= max_species; ++s) {
    if (counts[s] > max_count_[s]) {
      max_count_[s] = counts[s];
      max_gen_[s]   = gen;
    }
  }
}

void Simulation::print_results() const {
  for (int s = 1; s <= generator::N_SPECIES; ++s) {
    std::println("{} {} {}", s, max_count_[static_cast<std::size_t>(s)], max_gen_[static_cast<std::size_t>(s)]);
  }
}
