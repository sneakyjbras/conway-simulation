#include "simulation.hpp"
#include <cassert>
#include <cstdlib>
#include <print>

Simulation::Simulation(int generations, int N, float density, int seed)
  : generations_(generations), N_(N), density_(density), seed_(seed) {
  grid_.resize(static_cast<size_t>(N_) * N_ * N_, 0);
  for (int s = 1; s <= N_SPECIES; ++s) { max_count_[s] = 0; max_gen_[s] = 0; }
  init_from_generator();    // fills grid_[...] from Appendix A generator
}

Simulation::~Simulation() = default;

void Simulation::init_from_generator() {
  // Get initial grid from the provided routine (ensures reproducibility).
  char*** g = gen_initial_grid(N_, density_, seed_);
  // Copy to flat vector: idx = (x*N + y)*N + z
  size_t idx = 0;
  for (int x = 0; x < N_; ++x)
    for (int y = 0; y < N_; ++y)
      for (int z = 0; z < N_; ++z, ++idx)
        grid_[idx] = static_cast<std::uint8_t>(g[x][y][z]);
  // Free generator allocation
  free_grid(g, N_);
}

void Simulation::free_grid(char*** g, int N) {
  if (!g) return;
  for (int x = 0; x < N; ++x) {
    if (g[x]) {
      if (g[x][0]) std::free(g[x][0]);   // contiguous N*N block
      std::free(g[x]);                   // row pointer array
    }
  }
  std::free(g);
}

void Simulation::run() {
  // TODO: implement your serial stepper here.
  // - Maintain two buffers (curr/next) in grid_ or allocate a second vector.
  // - Apply 26-neighbor rules with wrap-around (3D torus).
  // - Track per-species counts each generation to update max_count_/max_gen_.
  // - On ties for maxima, keep the lowest generation (update only on strictly greater).

  // Placeholder: count initial as generation 0 only (so you can compile/run now).
  const size_t n3 = static_cast<size_t>(N_) * N_ * N_;
  for (size_t i = 0; i < n3; ++i) {
    auto v = grid_[i];
    if (v) { ++max_count_[v]; }
  }
  for (int s = 1; s <= N_SPECIES; ++s) max_gen_[s] = 0;
}

void Simulation::print_results() const {
  // Exactly 9 lines, species 1..9, printed to stdout.
  for (int s = 1; s <= N_SPECIES; ++s) {
    std::println("{} {} {}", s, max_count_[s], max_gen_[s]);
  }
}
