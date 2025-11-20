#pragma once

#include "debug_printer.hpp"
#include "generator.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace sim_detail {
inline constexpr unsigned char DEAD_CELL = 0;
} // namespace sim_detail

class Simulation {
public:
  Simulation(std::uint64_t number_of_generations, std::uint64_t grid_dimension, float initial_density,
             std::int32_t random_seed);
  ~Simulation();

  void run();
  void print_results() const;

  // Inline wrapper for enabling/disabling debug prints.
  inline void set_debug_enabled(bool enabled) {
    debug_printer_.enable(enabled);
  }

private:
  [[nodiscard]] std::size_t index_3d(std::uint64_t x, std::uint64_t y, std::uint64_t z) const noexcept;

  void initialize_from_generator();

  // Internal helpers for neighbor counting.
  std::uint64_t alive_neighbors_total(std::uint64_t x, std::uint64_t y, std::uint64_t z) const;
  std::uint64_t alive_neighbors_with_species(std::uint64_t x, std::uint64_t y, std::uint64_t z,
                                             std::array<std::uint64_t, generator::N_SPECIES + 1>& species_counts) const;

  // Wrapper that preserves the old API shape, but now with 64-bit quantities.
  std::uint64_t total_alive_neighbors(std::uint64_t x, std::uint64_t y, std::uint64_t z,
                                      std::array<std::uint64_t, generator::N_SPECIES + 1>* per_species) const;
  std::uint64_t total_alive_neighbors(std::uint64_t x, std::uint64_t y, std::uint64_t z) const;

  // Perform one generation step: read from grid_, write into 'next', and fill 'counts'
  // with per-species populations for the current generation.
  void step_generation(std::vector<unsigned char>& next, std::array<std::uint64_t, generator::N_SPECIES + 1>& counts);

  // Update maxima arrays using the counts for a given generation index.
  void update_maxima_for_generation(const std::array<std::uint64_t, generator::N_SPECIES + 1>& counts,
                                    std::uint64_t gen);

  std::uint64_t generations_;
  std::uint64_t grid_dimension_;
  float density_;
  std::int32_t seed_;

  std::vector<unsigned char> grid_;

  bool use_bitmask_wrap_{false};
  std::uint64_t wrap_mask_{0};

  // index 0 is unused; 1..N_SPECIES are valid
  std::array<std::uint64_t, generator::N_SPECIES + 1> max_count_{};
  std::array<std::uint64_t, generator::N_SPECIES + 1> max_gen_{};

  DebugPrinter debug_printer_;
};
