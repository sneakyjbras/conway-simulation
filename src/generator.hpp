#pragma once
// generator.hpp - C++23
// Simple 3D initial grid generator (ported from the original C code).
// No frills; straightforward API.

#include <cstddef>

namespace generator {

inline constexpr int N_SPECIES = 9;

// Initialize the RNG with an input seed.
void init_r4uni(int input_seed);

// Return a uniform float in [0,1).
// (Same algorithm as the original r4_uni; kept for reproducibility.)
float r4_uni();

// Allocate and fill a 3D grid with dimensions N x N x N.
// Each cell is 0 (empty) or a species label in [1, N_SPECIES].
// Caller owns the returned memory and must free it with free_grid().
char*** gen_initial_grid(int N, float density, int input_seed);

// Free a grid allocated by gen_initial_grid().
void free_grid(char*** grid, int N);

} // namespace generator
