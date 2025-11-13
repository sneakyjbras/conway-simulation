// simulation.cpp - C++23
#include "simulation.hpp"

#include <cmath>
#include <iostream>

// Helper to encapsulate species normalization / mapping.
// Still uses a switch on 1..9 as requested, but keeps this concern separate.
namespace {
[[nodiscard]] constexpr unsigned char normalize_species(unsigned char species) noexcept {
  switch (species) {
  case 1:
    return static_cast<unsigned char>(1);
  case 2:
    return static_cast<unsigned char>(2);
  case 3:
    return static_cast<unsigned char>(3);
  case 4:
    return static_cast<unsigned char>(4);
  case 5:
    return static_cast<unsigned char>(5);
  case 6:
    return static_cast<unsigned char>(6);
  case 7:
    return static_cast<unsigned char>(7);
  case 8:
    return static_cast<unsigned char>(8);
  case 9:
    return static_cast<unsigned char>(9);
  default:
    // For any other value, just use it as-is.
    return species;
  }
}
} // anonymous namespace

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

int Simulation::neighbors_count(int x, int y, int z, unsigned char species) const {
  const int n = grid_dimension_;
  int count   = 0;

  // Use the encapsulated normalization (still a switch under the hood).
  const unsigned char target_species = normalize_species(species);

  for (int dz = -1; dz <= 1; ++dz) {
    for (int dy = -1; dy <= 1; ++dy) {
      for (int dx = -1; dx <= 1; ++dx) {
        if (dx == 0 && dy == 0 && dz == 0)
          continue;
        const int nx = (x + dx + n) % n;
        const int ny = (y + dy + n) % n;
        const int nz = (z + dz + n) % n;
        if (grid_[index_3d(nx, ny, nz)] == target_species)
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
          const std::size_t idx = index_3d(x, y, z);

          unsigned char current = grid_[idx];
          int total_neighbors   = 0;

          // Sum neighbors across all species (logic unchanged)
          for (int s = 1; s <= generator::N_SPECIES; ++s) {
            total_neighbors += neighbors_count(x, y, z, static_cast<unsigned char>(s));
          }

          unsigned char new_value;
          if (total_neighbors >= 5 && total_neighbors <= 13) {
            // cell continues to be alive (keep current species)
            new_value = current;
          } else {
            // cell dies
            new_value = static_cast<unsigned char>(0);
          }

          // Write to next grid using the cached index
          next[idx] = new_value;

          // Only count if alive (0 means dead), same logic as before
          if (new_value != static_cast<unsigned char>(0)) {
            counts[static_cast<std::size_t>(new_value)]++;
          }
        }
      }
    }

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
