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
  int neighbors_count(int x, int y, int z, unsigned char species) const;
  void initialize_from_generator();

  int generations_;
  int grid_dimension_;
  float density_;
  int seed_;

  std::vector<unsigned char> grid_;
  std::array<std::uint64_t, generator::N_SPECIES + 1> max_count_{};
  std::array<int, generator::N_SPECIES + 1> max_gen_{};
};
