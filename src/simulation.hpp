#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// Number of species (replaces the old macro)
inline constexpr std::size_t N_SPECIES = 9;

auto r4_uni(std::uint32_t& state) -> float;

// Keep the legacy return type to avoid wider refactors, but modernize naming and style.
auto gen_initial_grid(int grid_dimension, float density, int input_seed) -> char***;

class Simulation {
public:
  Simulation(int number_of_generations, int grid_dimension, float initial_density, int random_seed);
  ~Simulation();

  Simulation(const Simulation&)                        = delete;
  auto operator=(const Simulation&) -> Simulation&     = delete;
  Simulation(Simulation&&) noexcept                    = default;
  auto operator=(Simulation&&) noexcept -> Simulation& = default;

  auto run() -> void;
  auto print_results() const -> void;

private:
  // Parameters
  int generations_{};
  int grid_dimension_{};
  float density_{};
  int seed_{};

  // Grid stored as a flat vector (no raw C arrays).
  // Values in [0, N_SPECIES]; 0 means empty.
  std::vector<unsigned char> grid_;

  // Stats
  std::array<std::uint64_t, N_SPECIES + 1> max_count_{};
  std::array<int, N_SPECIES + 1> max_gen_{};

  // Helpers
  static auto free_grid(char*** grid, int grid_dimension) -> void;

  [[nodiscard]] auto index_3d(int coordinate_x, int coordinate_y, int coordinate_z) const -> std::size_t;

  [[nodiscard]] auto neighbors_count(int coordinate_x, int coordinate_y, int coordinate_z, unsigned char species) const
      -> int;

  auto initialize_from_generator() -> void;
};
