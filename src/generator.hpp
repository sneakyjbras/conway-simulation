#pragma once
// generator.hpp – C++23
//
// Generates the initial 3-D grid as a flat std::vector<unsigned char>.
// The old char*** triple-pointer API has been replaced: callers no longer
// manage heap allocation, and the resulting buffer is cache-contiguous.

#include "params.hpp"

#include <cstdint>
#include <vector>

namespace generator
{

inline constexpr std::uint32_t N_SPECIES = 9;

// Inputs to the initial-grid generator, bundled into one struct.
//
// Bundling avoids adjacent scalar parameters of mutually-convertible types
// (uint64_t / float / int32_t are all implicitly inter-convertible, which
// clang-tidy's bugprone-easily-swappable-parameters flags as a same-position
// mistake risk).  It also matches the SimulationParams aggregate pattern used
// throughout the rest of the project.
struct GridSpec
{
  std::uint64_t    grid_dimension; // N — logical cells per axis
  float            density;        // target alive fraction in [0, 1]
  std::int32_t     seed;           // RNG seed
  DistributionType distribution{DistributionType::kUniform};
};

// Allocate and fill a flat N×N×N grid (z-major, then y, then x).
// Cell encoding: 0 = dead, 1..N_SPECIES = species label.
// The returned vector has exactly N*N*N elements.
//
// spec.distribution selects the spatial distribution of alive cells:
//   kUniform  — each cell independently alive with probability `density`
//               (bit-for-bit identical to the original xorshift sequence).
//   kGaussian — alive cells cluster toward the grid centre via a Box-Muller
//               radial falloff; `density` scales the overall alive fraction.
[[nodiscard]] std::vector<unsigned char> gen_initial_grid(const GridSpec& spec);

} // namespace generator
