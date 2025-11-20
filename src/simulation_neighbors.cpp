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
std::uint64_t alive_neighbors_bitmask(std::uint64_t x, std::uint64_t y, std::uint64_t z, std::uint64_t grid_dimension,
                                      std::uint64_t wrap_mask, const std::vector<unsigned char>& grid,
                                      std::array<std::uint64_t, generator::N_SPECIES + 1>* species_counts) {
  std::uint64_t total                 = 0;
  const std::uint64_t n               = grid_dimension;
  constexpr std::uint64_t max_survive = 13;
  constexpr std::uint64_t max_birth   = 10;
  const std::uint64_t max_neighbors   = CountSpecies ? max_birth : max_survive;

  const std::uint64_t mask = wrap_mask;
  const std::size_t n_sz   = static_cast<std::size_t>(n);

  for (int dz = -1; dz <= 1; ++dz) {
    const std::uint64_t z2 = (z + static_cast<std::uint64_t>(dz)) & mask;

    for (int dy = -1; dy <= 1; ++dy) {
      const std::uint64_t y2 = (y + static_cast<std::uint64_t>(dy)) & mask;

      for (int dx = -1; dx <= 1; ++dx) {
        if (dx == 0 && dy == 0 && dz == 0) {
          continue; // skip the cell itself
        }

        const std::uint64_t x2 = (x + static_cast<std::uint64_t>(dx)) & mask;

        const std::size_t idx =
            (static_cast<std::size_t>(z2) * n_sz + static_cast<std::size_t>(y2)) * n_sz + static_cast<std::size_t>(x2);

        const unsigned char value = grid[idx];

        if (value != sim_detail::DEAD_CELL) {
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
std::uint64_t alive_neighbors_modular(std::uint64_t x, std::uint64_t y, std::uint64_t z, std::uint64_t grid_dimension,
                                      const std::vector<unsigned char>& grid,
                                      std::array<std::uint64_t, generator::N_SPECIES + 1>* species_counts) {
  std::uint64_t total                 = 0;
  const std::uint64_t n               = grid_dimension;
  constexpr std::uint64_t max_survive = 13;
  constexpr std::uint64_t max_birth   = 10;
  const std::uint64_t max_neighbors   = CountSpecies ? max_birth : max_survive;

  const std::uint64_t n_minus_1 = n - 1;
  const std::size_t n_sz        = static_cast<std::size_t>(n);

  for (int dz = -1; dz <= 1; ++dz) {
    std::uint64_t z2;
    if (dz == -1) {
      z2 = (z == 0) ? n_minus_1 : (z - 1);
    } else if (dz == 0) {
      z2 = z;
    } else { // dz == 1
      z2 = (z == n_minus_1) ? 0 : (z + 1);
    }

    for (int dy = -1; dy <= 1; ++dy) {
      std::uint64_t y2;
      if (dy == -1) {
        y2 = (y == 0) ? n_minus_1 : (y - 1);
      } else if (dy == 0) {
        y2 = y;
      } else { // dy == 1
        y2 = (y == n_minus_1) ? 0 : (y + 1);
      }

      for (int dx = -1; dx <= 1; ++dx) {
        if (dx == 0 && dy == 0 && dz == 0) {
          continue; // skip the cell itself
        }

        std::uint64_t x2;
        if (dx == -1) {
          x2 = (x == 0) ? n_minus_1 : (x - 1);
        } else if (dx == 0) {
          x2 = x;
        } else { // dx == 1
          x2 = (x == n_minus_1) ? 0 : (x + 1);
        }

        const std::size_t idx =
            (static_cast<std::size_t>(z2) * n_sz + static_cast<std::size_t>(y2)) * n_sz + static_cast<std::size_t>(x2);

        const unsigned char value = grid[idx];

        if (value != sim_detail::DEAD_CELL) {
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
std::uint64_t alive_neighbors_common(std::uint64_t x, std::uint64_t y, std::uint64_t z, std::uint64_t grid_dimension,
                                     bool use_bitmask_wrap, std::uint64_t wrap_mask,
                                     const std::vector<unsigned char>& grid,
                                     std::array<std::uint64_t, generator::N_SPECIES + 1>* species_counts) {
  if (grid_dimension == 0) {
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
std::uint64_t Simulation::alive_neighbors_total(std::uint64_t x, std::uint64_t y, std::uint64_t z) const {
  return sim_detail::alive_neighbors_common<false>(x, y, z, grid_dimension_, use_bitmask_wrap_, wrap_mask_, grid_,
                                                   nullptr);
}

// Dead-cell path: total + per-species counts, early-exit at >10 neighbors.
std::uint64_t
Simulation::alive_neighbors_with_species(std::uint64_t x, std::uint64_t y, std::uint64_t z,
                                         std::array<std::uint64_t, generator::N_SPECIES + 1>& species_counts) const {
  return sim_detail::alive_neighbors_common<true>(x, y, z, grid_dimension_, use_bitmask_wrap_, wrap_mask_, grid_,
                                                  &species_counts);
}
