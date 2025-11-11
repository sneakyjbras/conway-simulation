#pragma once
#include <cstdint>
#include <vector>

// Bindings to the C generator (verbatim from Appendix A).
extern "C" {
#define N_SPECIES 9
void init_r4uni(int input_seed);
float r4_uni(void);
char ***gen_initial_grid(int N, float density, int input_seed);
}

class Simulation {
public:
  Simulation(int generations, int N, float density, int seed);
  ~Simulation();

  // Run the G timesteps. You will implement the serial algorithm here.
  void run();

  // Print exactly 9 lines to stdout using std::print/std::println.
  void print_results() const;

private:
  int generations_;
  int N_;
  float density_;
  int seed_;

  // Flat 0..9 grid (size N^3). 0 = dead, 1..9 = species.
  std::vector<std::uint8_t> grid_;

  // Per-species maxima (index 1..9 used)
  std::uint64_t max_count_[N_SPECIES + 1]{};
  int max_gen_[N_SPECIES + 1]{};

  // Helper: convert the C generator’s char*** to flat vector, then free it.
  void init_from_generator();

  // Free the C 3D structure allocated by gen_initial_grid
  static void free_grid(char ***g, int N);
};
