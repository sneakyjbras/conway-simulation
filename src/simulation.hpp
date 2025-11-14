// simulation.hpp
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

  // NEW: enable/disable debug printing
  void set_debug(bool enabled) noexcept {
    debug_printer_.enable(enabled);
  }

private:
  std::size_t index_3d(int x, int y, int z) const;
  void initialize_from_generator();
  // If you already have this signature, keep it exactly the same:
  int total_alive_neighbors(int x, int y, int z, std::array<int, generator::N_SPECIES + 1>* per_species) const;
  int total_alive_neighbors(int x, int y, int z) const;

  int generations_;
  int grid_dimension_;
  float density_;
  int seed_;

  std::vector<unsigned char> grid_;

  std::array<std::uint64_t, generator::N_SPECIES + 1> max_count_{};
  std::array<std::uint64_t, generator::N_SPECIES + 1> max_gen_{};

  // NEW:
  DebugPrinter debug_printer_{false};
};
