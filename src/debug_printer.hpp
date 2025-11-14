// debug_printer.hpp
#pragma once

#include <vector>

class DebugPrinter {
public:
  explicit DebugPrinter(bool enabled = false) noexcept : enabled_{enabled} {}

  void enable(bool enabled) noexcept {
    enabled_ = enabled;
  }
  [[nodiscard]] bool enabled() const noexcept {
    return enabled_;
  }

  // Prints one full generation (all layers) in the style of your screenshot.
  void print_generation(int generation, const std::vector<unsigned char>& grid, int grid_dimension) const;

private:
  bool enabled_{false};
};
