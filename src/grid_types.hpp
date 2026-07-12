#pragma once
// grid_types.hpp – C++23
//
// Shared low-level types used by the grid sub-objects (DirtySet, TileSet) and
// the Simulation orchestrator.  Keeping these in one header lets the sub-objects
// be compiled independently of simulation.hpp while still agreeing on the exact
// memory layout and change representation.

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

// The dead-cell sentinel.  Cell encoding: 0 = dead, 1..N_SPECIES = species.
inline constexpr unsigned char DEAD_CELL = 0;

// Edge length of each cubic tile (cells per axis).  8³ = 512 cells per tile,
// which fits comfortably in L1/L2 cache.  Shared by the sparse step (which
// iterates tiles) and the TileSet (which sizes the active/count arrays).
inline constexpr int TILE_DIM = 8;

// A single cell-state change recorded during a sparse step:
//   {flat ghost-array index, new cell value}
using CellChange    = std::pair<std::ptrdiff_t, unsigned char>;
using ChangesVector = std::vector<CellChange>;

// Read-only geometry of the ghost-padded grid.  These values are fixed once the
// grid dimension is known and are shared by every rebuild routine.  Holding
// them by value (rather than referencing the owning Simulation) keeps the
// sub-objects trivially movable.
struct GridGeometry
{
  std::ptrdiff_t                 stride_y{0}; // = ghost_dim (N+2)
  std::ptrdiff_t                 stride_z{0}; // = ghost_dim^2
  std::ptrdiff_t                 n{0};        // logical cells per axis (N)
  std::array<std::ptrdiff_t, 26> offsets{};   // 26 neighbour flat offsets
};
