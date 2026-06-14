#pragma once
// dirty_set.hpp – C++23
//
// The dirty set: the activity-tracking core of the sparse optimisation.
//
// A cell can only change state if it, or one of its 26 neighbours, is alive.
// dirty_[idx] = 1 marks exactly those cells; dirty_[idx] = 0 cells are provably
// dead with all-dead neighbourhoods and can be skipped.  Alongside the byte map
// we keep an explicit index list (the "activity list") so passes that need to
// visit only dirty cells are O(dirty), not O(N^3).
//
// This class owns the dirty state and the three rebuild strategies, but not the
// grid itself.  The current grid buffer is passed into each rebuild call, so the
// class holds no reference back to the Simulation and stays trivially movable.
//
// Rebuild strategies (in increasing specialisation):
//   * rebuild_full        — O(N^3) scan of the whole grid (dense mode / startup).
//   * rebuild_incremental — O(D_prev * 27) from the previous dirty set.
//   * rebuild_from_changes— O(D_prev + |changes| * 27) when churn is low.
//
// Toroidal boundary handling: neighbour arithmetic on a boundary cell can land
// on a ghost index.  Those are remapped to their logical wrap-around equivalent
// rather than dropped, so cells on the far face are correctly re-evaluated.

#include "grid_types.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

class DirtySet
{
public:
  // ghost_volume   = (N+2)^3, the size of the dirty byte map.
  // estimated_dirty= reserve hint for the index lists.
  DirtySet(std::size_t ghost_volume, std::size_t estimated_dirty, const GridGeometry& geom);

  // ── Rebuild strategies ────────────────────────────────────────────────────
  void rebuild_full(const unsigned char* grid) noexcept;
  void rebuild_incremental(const unsigned char* grid) noexcept;
  void rebuild_from_changes(const unsigned char* grid, const ChangesVector& changes) noexcept;

  // Move the current index list into prev_ (and clear current) ahead of an
  // incremental rebuild.  Mirrors the old dirty_indices_.swap(prev_) idiom.
  void swap_indices() noexcept
  {
    dirty_indices_.swap(prev_dirty_indices_);
  }

  // ── Observers ─────────────────────────────────────────────────────────────
  [[nodiscard]] float dirty_ratio() const noexcept;

  [[nodiscard]] std::size_t dirty_count() const noexcept
  {
    return dirty_count_;
  }

  [[nodiscard]] const std::vector<std::ptrdiff_t>& dirty_indices() const noexcept
  {
    return dirty_indices_;
  }

  // Raw pointer to the dirty byte map — read by the sparse step's hot loop.
  [[nodiscard]] const std::uint8_t* dirty_data() const noexcept
  {
    return dirty_.data();
  }

  // Size of the previous dirty set — drives the low-churn rebuild decision.
  [[nodiscard]] std::size_t prev_size() const noexcept
  {
    return prev_dirty_indices_.size();
  }

private:
  GridGeometry geom_;

  std::vector<std::uint8_t>   dirty_;
  std::size_t                 dirty_count_{0};
  std::vector<std::ptrdiff_t> dirty_indices_;
  std::vector<std::ptrdiff_t> prev_dirty_indices_;
};
