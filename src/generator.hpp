#pragma once
// generator.hpp - C++23
// Simple 3D initial grid generator (ported from the original C code).
// No frills; straightforward API.

#include <cstddef>
#include <cstdint>

namespace generator {

inline constexpr std::uint32_t N_SPECIES = 9;

// Initialize the RNG with an input seed (32-bit signed).
void init_r4uni(std::int32_t input_seed);

// Return a uniform float in [0,1).
// (Simple, deterministic RNG; good enough for initial conditions.)
float r4_uni();

// Allocate and fill a 3D grid with dimensions N x N x N.
// Each cell is 0 (empty) or a species label in [1, N_SPECIES].
// Caller owns the returned memory and must free it with free_grid().
unsigned char*** gen_initial_grid(std::uint64_t N, float density, std::int32_t input_seed);

// Free a grid allocated by gen_initial_grid().
void free_grid(unsigned char*** grid, std::uint64_t N);

} // namespace generator
