// simulation_neighbors.cpp - C++23
#include "simulation.hpp"

#include <array>
#include <vector>

namespace sim_detail {

// Fast neighbor counting for *power-of-two* grid dimensions.
//
// Uses bitwise AND with (n - 1) to wrap coordinates instead of branches / modulo.
// This path is only valid when 'grid_dimension' is a power of two and
// 'use_bitmask_wrap' is true.
template <bool CountSpecies>
int alive_neighbors_bitmask(int x, int y, int z, int grid_dimension, int wrap_mask,
                            const std::vector<unsigned char>& grid,
                            std::array<int, generator::N_SPECIES + 1>* species_counts) {
  int total                 = 0;
  const int n               = grid_dimension;
  constexpr int max_survive = 13;
  constexpr int max_birth   = 10;
  const int max_neighbors   = CountSpecies ? max_birth : max_survive;

  const int mask         = wrap_mask;
  const std::size_t n_sz = static_cast<std::size_t>(n);

  for (int dz = -1; dz <= 1; ++dz) {
    const int z2 = (z + dz) & mask;

    for (int dy = -1; dy <= 1; ++dy) {
      const int y2 = (y + dy) & mask;

      for (int dx = -1; dx <= 1; ++dx) {
        if (dx == 0 && dy == 0 && dz == 0) {
          continue; // skip the cell itself
        }

        const int x2 = (x + dx) & mask;

        const std::size_t idx =
            (static_cast<std::size_t>(z2) * n_sz + static_cast<std::size_t>(y2)) * n_sz + static_cast<std::size_t>(x2);

        const unsigned char value = grid[idx];

        if (value != static_cast<unsigned char>(0)) {
          ++total;

          if constexpr (CountSpecies) {
            (*species_counts)[static_cast<std::size_t>(value)]++;
          }

          if (total > max_neighbors) {
            return total; // early exit
          }
        }
      }
    }
  }

  return total;
}

// Generic neighbor counting for *any* grid dimension.
//
// Handles toroidal wrap with simple branchy arithmetic (no bit tricks).
// Used when the dimension is NOT a power of two.
template <bool CountSpecies>
int alive_neighbors_modular(int x, int y, int z, int grid_dimension, const std::vector<unsigned char>& grid,
                            std::array<int, generator::N_SPECIES + 1>* species_counts) {
  int total                 = 0;
  const int n               = grid_dimension;
  constexpr int max_survive = 13;
  constexpr int max_birth   = 10;
  const int max_neighbors   = CountSpecies ? max_birth : max_survive;

  const int n_minus_1    = n - 1;
  const std::size_t n_sz = static_cast<std::size_t>(n);

  for (int dz = -1; dz <= 1; ++dz) {
    int z2 = z + dz;
    if (z2 < 0) {
      z2 = n_minus_1;
    } else if (z2 >= n) {
      z2 = 0;
    }

    for (int dy = -1; dy <= 1; ++dy) {
      int y2 = y + dy;
      if (y2 < 0) {
        y2 = n_minus_1;
      } else if (y2 >= n) {
        y2 = 0;
      }

      for (int dx = -1; dx <= 1; ++dx) {
        if (dx == 0 && dy == 0 && dz == 0) {
          continue; // skip the cell itself
        }

        int x2 = x + dx;
        if (x2 < 0) {
          x2 = n_minus_1;
        } else if (x2 >= n) {
          x2 = 0;
        }

        const std::size_t idx =
            (static_cast<std::size_t>(z2) * n_sz + static_cast<std::size_t>(y2)) * n_sz + static_cast<std::size_t>(x2);

        const unsigned char value = grid[idx];

        if (value != static_cast<unsigned char>(0)) {
          ++total;

          if constexpr (CountSpecies) {
            (*species_counts)[static_cast<std::size_t>(value)]++;
          }

          if (total > max_neighbors) {
            return total; // early exit
          }
        }
      }
    }
  }

  return total;
}

// Small dispatcher that chooses which implementation to use.
//
// - If 'use_bitmask_wrap' is true, we know the dimension is a power of two
//   and can take the fast bitmask path.
// - Otherwise we use the generic modular/branchy path.
template <bool CountSpecies>
int alive_neighbors_common(int x, int y, int z, int grid_dimension, bool use_bitmask_wrap, int wrap_mask,
                           const std::vector<unsigned char>& grid,
                           std::array<int, generator::N_SPECIES + 1>* species_counts) {
  if (grid_dimension <= 0) {
    return 0; // degenerate case: empty grid, no neighbors
  }

  if (use_bitmask_wrap) {
    // Power-of-two size: use the & (n-1) trick for toroidal wrap.
    return alive_neighbors_bitmask<CountSpecies>(x, y, z, grid_dimension, wrap_mask, grid, species_counts);
  }

  // General size: use branchy wrap-around (0..n-1) for toroidal behavior.
  return alive_neighbors_modular<CountSpecies>(x, y, z, grid_dimension, grid, species_counts);
}

} // namespace sim_detail

// Alive-cell path: only total, early-exit at >13 neighbors.
int Simulation::alive_neighbors_total(int x, int y, int z) const {
  return sim_detail::alive_neighbors_common<false>(x, y, z, grid_dimension_, use_bitmask_wrap_, wrap_mask_, grid_,
                                                   nullptr);
}

// Dead-cell path: total + per-species counts, early-exit at >10 neighbors.
int Simulation::alive_neighbors_with_species(int x, int y, int z,
                                             std::array<int, generator::N_SPECIES + 1>& species_counts) const {
  return sim_detail::alive_neighbors_common<true>(x, y, z, grid_dimension_, use_bitmask_wrap_, wrap_mask_, grid_,
                                                  &species_counts);
}
