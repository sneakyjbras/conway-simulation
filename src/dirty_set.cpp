// dirty_set.cpp – C++23
//
// Implementation of the three dirty-set rebuild strategies.  The algorithms are
// preserved exactly from the original Simulation members, including the
// toroidal boundary remap; only the data access changed (grid passed in,
// geometry held by value).

#include "dirty_set.hpp"

#include <algorithm>

DirtySet::DirtySet(std::size_t ghost_volume, std::size_t estimated_dirty, const GridGeometry& geom)
  : geom_(geom)
  , dirty_(ghost_volume, std::uint8_t{0})
{
  dirty_indices_.reserve(estimated_dirty);
  prev_dirty_indices_.reserve(estimated_dirty);
}

float DirtySet::dirty_ratio() const noexcept
{
  const std::size_t n3 = static_cast<std::size_t>(geom_.n) * static_cast<std::size_t>(geom_.n)
                         * static_cast<std::size_t>(geom_.n);
  if (n3 == 0)
    return 0.0f;
  return static_cast<float>(dirty_count_) / static_cast<float>(n3);
}

// ── Full rebuild ──────────────────────────────────────────────────────────────
// O(N^3) scan of the logical grid.  dirty_[idx] = 1 iff the cell is alive or has
// at least one alive neighbour.

void DirtySet::rebuild_full(const unsigned char* grid) noexcept
{
  const auto           N        = static_cast<int>(geom_.n);
  const auto*          g        = grid;
  const auto*          offs     = geom_.offsets.data();
  const std::ptrdiff_t stride_y = geom_.stride_y;
  const std::ptrdiff_t stride_z = geom_.stride_z;

  std::fill(dirty_.begin(), dirty_.end(), std::uint8_t{0});
  dirty_count_ = 0;
  dirty_indices_.clear();

  for (int z = 0; z < N; ++z)
  {
    const std::ptrdiff_t z_base = static_cast<std::ptrdiff_t>(z + 1) * stride_z;
    for (int y = 0; y < N; ++y)
    {
      const std::ptrdiff_t row = z_base + static_cast<std::ptrdiff_t>(y + 1) * stride_y + 1;
      for (int x = 0; x < N; ++x)
      {
        const std::ptrdiff_t idx = row + static_cast<std::ptrdiff_t>(x);

        bool d = (g[idx] != DEAD_CELL);
        if (!d)
          for (int k = 0; k < 26 && !d; ++k)
            d = (g[idx + offs[k]] != DEAD_CELL);

        const std::uint8_t flag               = d ? std::uint8_t{1} : std::uint8_t{0};
        dirty_[static_cast<std::size_t>(idx)] = flag;
        dirty_count_ += flag;
        if (flag)
          dirty_indices_.push_back(idx);
      }
    }
  }
}

// ── Incremental rebuild ─────────────────────────────────────────────────────
// O(D_prev * 27).  A cell can only enter/leave the dirty set if it or one of its
// 26 neighbours changed; we therefore re-evaluate prev_dirty_indices_ and their
// shells.  Ghost indices are remapped to their toroidal logical equivalent.

void DirtySet::rebuild_incremental(const unsigned char* grid) noexcept
{
  const auto* g    = grid;
  const auto* offs = geom_.offsets.data();
  const auto  N_   = geom_.n;
  const auto  sy   = geom_.stride_y;
  const auto  sz   = geom_.stride_z;

  for (std::ptrdiff_t idx : prev_dirty_indices_)
    dirty_[static_cast<std::size_t>(idx)] = 0;

  dirty_count_ = 0;
  dirty_indices_.clear();

  auto mark_if_dirty = [&](std::ptrdiff_t idx)
  {
    if (dirty_[static_cast<std::size_t>(idx)])
      return; // already marked this pass

    const std::ptrdiff_t gz  = idx / sz;
    const std::ptrdiff_t rem = idx - gz * sz;
    const std::ptrdiff_t gy  = rem / sy;
    const std::ptrdiff_t gx  = rem - gy * sy;

    // Ghost cell → redirect to the toroidal logical equivalent.
    if (gz < 1 || gz > N_ || gy < 1 || gy > N_ || gx < 1 || gx > N_)
    {
      const std::ptrdiff_t lgz = (gz < 1) ? N_ : (gz > N_) ? std::ptrdiff_t(1) : gz;
      const std::ptrdiff_t lgy = (gy < 1) ? N_ : (gy > N_) ? std::ptrdiff_t(1) : gy;
      const std::ptrdiff_t lgx = (gx < 1) ? N_ : (gx > N_) ? std::ptrdiff_t(1) : gx;
      idx                      = lgz * sz + lgy * sy + lgx;
      if (dirty_[static_cast<std::size_t>(idx)])
        return;
    }

    bool d = (g[idx] != DEAD_CELL);
    if (!d)
      for (int k = 0; k < 26 && !d; ++k)
        d = (g[idx + offs[k]] != DEAD_CELL);

    if (d)
    {
      dirty_[static_cast<std::size_t>(idx)] = 1;
      ++dirty_count_;
      dirty_indices_.push_back(idx);
    }
  };

  for (std::ptrdiff_t idx : prev_dirty_indices_)
  {
    mark_if_dirty(idx);
    for (int k = 0; k < 26; ++k)
      mark_if_dirty(idx + offs[k]);
  }
}

// ── Rebuild from changes ──────────────────────────────────────────────────────
// O(D_prev + |changes| * 27) — cheaper than the incremental rebuild when churn
// is low.  Uses a 0/1/2 dirty encoding while processing:
//   0 = not dirty, 1 = was dirty in D_prev and still dirty, 2 = newly dirty.

void DirtySet::rebuild_from_changes(const unsigned char* grid,
                                    const ChangesVector& changes) noexcept
{
  const auto* g    = grid;
  const auto* offs = geom_.offsets.data();
  const auto  N_   = geom_.n;
  const auto  sy   = geom_.stride_y;
  const auto  sz   = geom_.stride_z;

  dirty_count_ = 0;
  dirty_indices_.clear();

  auto re_evaluate = [&](std::ptrdiff_t idx)
  {
    const std::ptrdiff_t gz  = idx / sz;
    const std::ptrdiff_t rem = idx - gz * sz;
    const std::ptrdiff_t gy  = rem / sy;
    const std::ptrdiff_t gx  = rem - gy * sy;

    // Ghost cell → redirect to the toroidal logical equivalent.
    if (gz < 1 || gz > N_ || gy < 1 || gy > N_ || gx < 1 || gx > N_)
    {
      const std::ptrdiff_t lgz = (gz < 1) ? N_ : (gz > N_) ? std::ptrdiff_t(1) : gz;
      const std::ptrdiff_t lgy = (gy < 1) ? N_ : (gy > N_) ? std::ptrdiff_t(1) : gy;
      const std::ptrdiff_t lgx = (gx < 1) ? N_ : (gx > N_) ? std::ptrdiff_t(1) : gx;
      idx                      = lgz * sz + lgy * sy + lgx;
    }

    const auto uidx     = static_cast<std::size_t>(idx);
    const auto cur_flag = dirty_[uidx];
    if (cur_flag == 2)
      return; // already marked newly-dirty this pass

    bool d = (g[idx] != DEAD_CELL);
    if (!d)
      for (int k = 0; k < 26 && !d; ++k)
        d = (g[idx + offs[k]] != DEAD_CELL);

    if (d)
    {
      if (cur_flag == 0)
        dirty_[uidx] = 2; // newly dirty (was not in D_prev)
      // else cur_flag == 1: was dirty, still dirty — leave as 1
    }
    else
    {
      dirty_[uidx] = 0; // not dirty (removes if was 1)
    }
  };

  for (const auto& entry : changes)
  {
    re_evaluate(entry.first);
    for (int k = 0; k < 26; ++k)
      re_evaluate(entry.first + offs[k]);
  }

  // Part 1: D_prev cells that are still dirty (flag == 1).
  for (std::ptrdiff_t idx : prev_dirty_indices_)
  {
    if (dirty_[static_cast<std::size_t>(idx)] == 1)
    {
      dirty_indices_.push_back(idx);
      ++dirty_count_;
    }
  }

  // Part 2: newly-dirty cells (flag == 2); normalise 2→1 while scanning.
  for (const auto& entry : changes)
  {
    auto add_if_new = [&](std::ptrdiff_t candidate)
    {
      const auto uidx = static_cast<std::size_t>(candidate);
      if (dirty_[uidx] == 2)
      {
        dirty_[uidx] = 1;
        dirty_indices_.push_back(candidate);
        ++dirty_count_;
      }
    };
    add_if_new(entry.first);
    for (int k = 0; k < 26; ++k)
      add_if_new(entry.first + offs[k]);
  }
}
