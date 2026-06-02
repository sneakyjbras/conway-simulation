// debug_printer.cpp – C++23

#include "debug_printer.hpp"

#include <cstdio>
#include <string>

void DebugPrinter::print_generation(std::uint64_t                    generation,
                                    const std::vector<unsigned char>& grid,
                                    std::uint64_t                    grid_dimension,
                                    std::uint64_t                    ghost_dim) const {
  if (!enabled_) {
    return;
  }

  const std::size_t N   = static_cast<std::size_t>(grid_dimension);
  const std::size_t Ng  = static_cast<std::size_t>(ghost_dim); // N+2
  const std::size_t Ng2 = Ng * Ng;

  std::fprintf(stdout, "Generation %llu  ------------------------------\n",
               static_cast<unsigned long long>(generation));

  for (std::size_t z = 0; z < N; ++z) {
    std::fprintf(stdout, "Layer z = %zu\n", z);

    for (std::size_t y = 0; y < N; ++y) {
      std::string line;
      line.reserve(2 * N);

      for (std::size_t x = 0; x < N; ++x) {
        // Interior cell (x,y,z) lives at ghost position (x+1, y+1, z+1).
        const std::size_t idx = (z + 1) * Ng2 + (y + 1) * Ng + (x + 1);
        const unsigned char v = grid[idx];

        if (v == 0) {
          line += ". ";
        } else {
          line += static_cast<char>('0' + v);
          line += ' ';
        }
      }

      std::fprintf(stdout, "%s\n", line.c_str());
    }

    std::fputc('\n', stdout);
  }

  std::fputc('\n', stdout);
}
