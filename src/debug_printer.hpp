#pragma once
// debug_printer.hpp – C++23

#include <cstdint>
#include <vector>

class DebugPrinter
{
public:
  explicit DebugPrinter(bool enabled = false) noexcept
    : enabled_{enabled}
  {
  }

  void enable(bool enabled) noexcept
  {
    enabled_ = enabled;
  }
  [[nodiscard]] bool is_enabled() const noexcept
  {
    return enabled_;
  }

  // Print all z-layers of one generation.
  // The ghost-padded edge length (N+2) is derived internally from
  // grid_dimension; actual cells occupy indices [1..N] on every axis.
  void print_generation(std::uint64_t                     generation,
                        const std::vector<unsigned char>& grid,
                        std::uint64_t                     grid_dimension) const;

private:
  bool enabled_{false};
};
