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

  // Simple toggle for debug printing.
  inline void set_debug_enabled(bool enabled) {
    debug_printer_.enable(enabled);
  }

private:
  [[nodiscard]] std::size_t index_3d(int x, int y, int z) const noexcept;

  void initialize_from_generator();

  // Internal helpers for neighbor counting.
  int alive_neighbors_total(int x, int y, int z) const;
  int alive_neighbors_with_species(int x, int y, int z,
                                   std::array<int, generator::N_SPECIES + 1>& species_counts) const;

  // Old API wrapper.
  int total_alive_neighbors(int x, int y, int z, std::array<int, generator::N_SPECIES + 1>* per_species) const;
  int total_alive_neighbors(int x, int y, int z) const;

  // Perform one generation step: read from grid_, write into 'next', and fill 'counts'
  // with per-species populations for the current generation.
  void step_generation(std::vector<unsigned char>& next, std::array<std::uint64_t, generator::N_SPECIES + 1>& counts);

  // Update maxima arrays using the counts for a given generation index.
  void update_maxima_for_generation(const std::array<std::uint64_t, generator::N_SPECIES + 1>& counts,
                                    std::uint64_t gen);

  int generations_;
  int grid_dimension_;
  float density_;
  std::int32_t seed_;

  std::vector<unsigned char> grid_;

  bool use_bitmask_wrap_{false};
  int wrap_mask_{0};

  // index 0 is unused; 1..N_SPECIES are valid
  std::array<std::uint64_t, generator::N_SPECIES + 1> max_count_{};
  std::array<std::uint64_t, generator::N_SPECIES + 1> max_gen_{};

  DebugPrinter debug_printer_;
};
