// debug_printer.hpp
#pragma once

#include <cstdint>
#include <vector>

class DebugPrinter {
public:
  explicit DebugPrinter(bool enabled = false) noexcept : enabled_{enabled} {}

  // Preferred API
  void set_enable(bool enabled) noexcept {
    enabled_ = enabled;
  }
  [[nodiscard]] bool is_enabled() const noexcept {
    return enabled_;
  }

  // Backwards-compatible aliases
  void enable(bool enabled) noexcept {
    set_enable(enabled);
  }
  [[nodiscard]] bool enabled() const noexcept {
    return is_enabled();
  }

  // Prints one full generation (all layers) in the style of your screenshot.
  void print_generation(std::uint64_t generation, const std::vector<unsigned char>& grid,
                        std::uint64_t grid_dimension) const;

private:
  bool enabled_{false};
};
