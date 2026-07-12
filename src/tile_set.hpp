#pragma once
// tile_set.hpp – C++23
//
// The tile active-set: a coarse-grained skip structure layered over the dirty
// set.  Interior cells are grouped into TILE_DIM^3 cubic tiles.  A tile is
// ACTIVE if any of its cells is dirty; dormant tiles contain only dead cells
// and are copied wholesale by the sparse step instead of being scanned.
//
// This class owns the per-tile active flags and alive counts but not the grid.
// Like DirtySet it holds geometry by value and takes the data it needs as call
// arguments, so it stays trivially movable.
//
// Note: tile_alive_count_ is currently maintained (initialised from the grid,
// updated incrementally from the change list) but not yet consulted for a
// dispatch decision — it is the foundation for a future count-based tile
// activity heuristic.  It is kept faithfully so that foundation stays intact.

#include "grid_types.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

class TileSet
{
public:
  TileSet(std::uint32_t grid_dimension, const GridGeometry& geom);

  // Recompute the active flags from the current dirty-cell index list: O(D).
  void rebuild(const std::vector<std::ptrdiff_t>& dirty_indices) noexcept;

  // Apply a change list delta to the per-tile alive counts: O(|changes|).
  void update_counts_from_changes(const ChangesVector& changes) noexcept;

  // Initialise per-tile alive counts by scanning the logical grid once.
  void init_counts_from_grid(const unsigned char* grid) noexcept;

  // ── Observers ─────────────────────────────────────────────────────────────
  [[nodiscard]] const std::vector<std::uint8_t>& active() const noexcept
  {
    return tile_active_;
  }

  [[nodiscard]] int tiles_per_axis() const noexcept
  {
    return tiles_per_axis_;
  }

private:
  GridGeometry geom_;
  int          tiles_per_axis_{0};

  std::vector<std::uint8_t>  tile_active_;
  std::vector<std::uint32_t> tile_alive_count_;
};
