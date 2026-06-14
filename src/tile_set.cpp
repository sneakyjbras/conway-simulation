// tile_set.cpp – C++23
//
// Implementation of the tile active-set.  The rebuild and count-update
// algorithms are preserved exactly from the original Simulation members; only
// the data access changed (grid passed in, geometry held by value).

#include "tile_set.hpp"

#include <algorithm>

namespace
{

// Flat tile index from 3D tile coordinates.  Promotes to std::size_t before
// the multiply-add (rather than casting the already-computed int result), so
// the widening happens before any possible overflow in the index arithmetic.
// 3D tile-grid coordinate, bundled into a struct so it isn't three adjacent
// same-type int parameters (which clang-tidy flags as easily swapped).
struct TileCoord
{
  int tz;
  int ty;
  int tx;
};

[[nodiscard]] std::size_t tile_index(const TileCoord& coord, std::size_t tpa) noexcept
{
  return (static_cast<std::size_t>(coord.tz) * tpa + static_cast<std::size_t>(coord.ty)) * tpa
         + static_cast<std::size_t>(coord.tx);
}

} // namespace

TileSet::TileSet(std::uint32_t grid_dimension, const GridGeometry& geom)
  : geom_(geom)
  , tiles_per_axis_((static_cast<int>(grid_dimension) + TILE_DIM - 1) / TILE_DIM)
{
  const auto count = static_cast<std::size_t>(tiles_per_axis_)
                     * static_cast<std::size_t>(tiles_per_axis_)
                     * static_cast<std::size_t>(tiles_per_axis_);
  tile_active_.assign(count, std::uint8_t{0});
  tile_alive_count_.assign(count, std::uint32_t{0});
}

// ── rebuild ───────────────────────────────────────────────────────────────────
// O(D): mark the tile containing each dirty cell as active.

void TileSet::rebuild(const std::vector<std::ptrdiff_t>& dirty_indices) noexcept
{
  const int  tpa = tiles_per_axis_;
  const auto sy  = geom_.stride_y;
  const auto sz  = geom_.stride_z;

  std::fill(tile_active_.begin(), tile_active_.end(), std::uint8_t{0});

  for (std::ptrdiff_t idx : dirty_indices)
  {
    const std::ptrdiff_t gz  = idx / sz;
    const std::ptrdiff_t rem = idx - gz * sz;
    const std::ptrdiff_t gy  = rem / sy;
    const std::ptrdiff_t gx  = rem - gy * sy;
    const int            tz  = (static_cast<int>(gz) - 1) / TILE_DIM;
    const int            ty  = (static_cast<int>(gy) - 1) / TILE_DIM;
    const int            tx  = (static_cast<int>(gx) - 1) / TILE_DIM;
    tile_active_[tile_index({tz, ty, tx}, static_cast<std::size_t>(tpa))] = 1;
  }
}

// ── update_counts_from_changes ────────────────────────────────────────────────
// O(|changes|): apply the per-step change-list delta to tile alive counts.

void TileSet::update_counts_from_changes(const ChangesVector& changes) noexcept
{
  const int  tpa = tiles_per_axis_;
  const auto sy  = geom_.stride_y;
  const auto sz  = geom_.stride_z;

  for (const auto& entry : changes)
  {
    const std::ptrdiff_t idx = entry.first;
    const unsigned char  nv  = entry.second;

    const std::ptrdiff_t gz  = idx / sz;
    const std::ptrdiff_t rem = idx - gz * sz;
    const std::ptrdiff_t gy  = rem / sy;
    const std::ptrdiff_t gx  = rem - gy * sy;
    const int            tz  = (static_cast<int>(gz) - 1) / TILE_DIM;
    const int            ty  = (static_cast<int>(gy) - 1) / TILE_DIM;
    const int            tx  = (static_cast<int>(gx) - 1) / TILE_DIM;
    const auto           ti  = tile_index({tz, ty, tx}, static_cast<std::size_t>(tpa));

    if (nv == DEAD_CELL)
      --tile_alive_count_[ti]; // death
    else
      ++tile_alive_count_[ti]; // birth
  }
}

// ── init_counts_from_grid ─────────────────────────────────────────────────────
// Scan the logical grid once to seed per-tile alive counts.

void TileSet::init_counts_from_grid(const unsigned char* grid) noexcept
{
  const int            N   = static_cast<int>(geom_.n);
  const int            tpa = tiles_per_axis_;
  const std::ptrdiff_t sy  = geom_.stride_y;
  const std::ptrdiff_t sz  = geom_.stride_z;

  std::fill(tile_alive_count_.begin(), tile_alive_count_.end(), std::uint32_t{0});

  for (int z = 0; z < N; ++z)
  {
    const std::ptrdiff_t z_base = static_cast<std::ptrdiff_t>(z + 1) * sz;
    for (int y = 0; y < N; ++y)
    {
      const std::ptrdiff_t row = z_base + static_cast<std::ptrdiff_t>(y + 1) * sy + 1;
      for (int x = 0; x < N; ++x)
      {
        if (grid[static_cast<std::size_t>(row + x)] == DEAD_CELL)
          continue;
        const int tz = z / TILE_DIM;
        const int ty = y / TILE_DIM;
        const int tx = x / TILE_DIM;
        ++tile_alive_count_[tile_index({tz, ty, tx}, static_cast<std::size_t>(tpa))];
      }
    }
  }
}
