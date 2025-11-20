// debug_printer.cpp
#include "debug_printer.hpp"

#include <print>
#include <string>

void DebugPrinter::print_generation(std::uint64_t generation, const std::vector<unsigned char>& grid,
                                    std::uint64_t grid_dimension) const {
  if (!enabled_) {
    return;
  }

  const std::size_t n  = static_cast<std::size_t>(grid_dimension);
  const std::size_t n2 = n * n;

  std::println("Generation {}  ------------------------------", generation);

  for (std::size_t z = 0; z < n; ++z) {
    std::println("Layer z = {}", z);

    for (std::size_t y = 0; y < n; ++y) {
      std::string line;
      line.reserve(2 * n); // rough reserve: "v " per cell

      for (std::size_t x = 0; x < n; ++x) {
        const std::size_t idx = (z * n2) + (y * n) + x;
        const unsigned char v = grid[idx];

        if (v == 0) {
          // Use '.' for dead cells.
          line += ". ";
        } else {
          // Species are 1..9 so this matches your screenshot.
          line += static_cast<char>('0' + v);
          line += ' ';
        }
      }

      std::println("{}", line);
    }

    std::println(""); // blank line between layers
  }

  std::println(""); // blank line between generations
}
