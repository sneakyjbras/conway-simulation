// debug_printer.cpp
#include "debug_printer.hpp"

#include <print>
#include <string>

void DebugPrinter::print_generation(int generation, const std::vector<unsigned char>& grid, int grid_dimension) const {
  if (!enabled_) {
    return;
  }

  const int n         = grid_dimension;
  const std::size_t N = static_cast<std::size_t>(n);

  std::println("Generation {}  ------------------------------", generation);

  for (int z = 0; z < n; ++z) {
    std::println("Layer {}:", z);

    for (int y = 0; y < n; ++y) {
      std::string line;
      line.reserve(static_cast<std::size_t>(2 * n));

      for (int x = 0; x < n; ++x) {
        const std::size_t idx =
            (static_cast<std::size_t>(z) * N + static_cast<std::size_t>(y)) * N + static_cast<std::size_t>(x);

        const unsigned char v = grid[idx];

        if (v == static_cast<unsigned char>(0)) {
          // Dead cell -> just spaces, to keep alignment.
          line += ' ';
          line += ' ';
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
