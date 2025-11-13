#pragma once
// simulation.hpp - C++23
// Keeps simulation logic only; initialization uses generator module.

#include "generator.hpp"

#include <array>
#include <cstddef>
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
  int total_alive_neighbors(int x, int y, int z) const;
  int total_alive_neighbors(int x, int y, int z, std::array<int, generator::N_SPECIES + 1>* species_counts) const;

  int generations_;
  int grid_dimension_;
  float density_;
  int seed_;

  std::vector<unsigned char> grid_;
  std::array<std::uint64_t, generator::N_SPECIES + 1> max_count_{};
  std::array<int, generator::N_SPECIES + 1> max_gen_{};
};
