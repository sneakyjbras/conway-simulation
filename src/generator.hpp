#pragma once
// generator.hpp – C++23
//
// Generates the initial 3-D grid as a flat std::vector<unsigned char>.
// The old char*** triple-pointer API has been replaced: callers no longer
// manage heap allocation, and the resulting buffer is cache-contiguous.

#include <cstdint>
#include <vector>

namespace generator
{

inline constexpr std::uint32_t N_SPECIES = 9;

// Allocate and fill a flat N×N×N grid (z-major, then y, then x).
// Cell encoding: 0 = dead, 1..N_SPECIES = species label.
// The returned vector has exactly N*N*N elements.
[[nodiscard]] std::vector<unsigned char>
gen_initial_grid(std::uint64_t N, float density, std::int32_t input_seed);

} // namespace generator
