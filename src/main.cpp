#include "simulation.hpp"
#include <cstdio>
#include <cstdlib>
#include <omp.h>
#include <print>

int main(int argc, char *argv[]) {
  if (argc != 5) {
    std::print(stderr, "Usage: {} <generations> <N> <density> <seed>\n",
               argv[0]);
    return 1;
  }
  const int generations = std::atoi(argv[1]);
  const int N = std::atoi(argv[2]);
  const float density = std::strtof(argv[3], nullptr);
  const int seed = std::atoi(argv[4]);

  if (generations < 0 || N <= 0 || density < 0.0f || density > 1.0f) {
    std::print(stderr, "Invalid arguments\n");
    return 1;
  }

  Simulation sim(generations, N, density, seed);

  double exec_time = -omp_get_wtime();
  sim.run(); // you will implement the serial algorithm
  exec_time += omp_get_wtime();
  std::print(stderr, "{:.1f}s\n", exec_time); // stderr per spec

  sim.print_results(); // stdout: 9 lines
  return 0;
}
