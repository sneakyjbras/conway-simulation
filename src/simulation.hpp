#pragma once

#include "debug_printer.hpp"
#include "generator.hpp"

#include <array>
#include <cstdint>
#include <vector>

class Simulation {
public:
  Simulation(int number_of_generations, int grid_dimension, float initial_density, int random_seed);
  ~Simulation();

  void run();
  void print_results() const;

private:
  std::size_t index_3d(int x, int y, int z) const;
  void initialize_from_generator();

  // Wrapper that preserves the old API.
  int total_alive_neighbors(int x, int y, int z, std::array<int, generator::N_SPECIES + 1>* per_species) const;
  int total_alive_neighbors(int x, int y, int z) const;

  // Internal helpers for neighbor counting.
  int alive_neighbors_total(int x, int y, int z) const;
  int alive_neighbors_with_species(int x, int y, int z,
                                   std::array<int, generator::N_SPECIES + 1>& species_counts) const;

  int generations_;
  int grid_dimension_;
  float density_;
  std::int32_t seed_; // keep seed as 32-bit, as discussed earlier.

  std::vector<unsigned char> grid_;

  bool use_bitmask_wrap_{false};
  int wrap_mask_{0};

  // index 0 unused; 1..N_SPECIES are valid.
  std::array<std::uint64_t, generator::N_SPECIES + 1> max_count_{};
  std::array<std::uint64_t, generator::N_SPECIES + 1> max_gen_{};

  DebugPrinter debug_printer_;
};
