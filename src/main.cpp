#include "parser.hpp"
#include "simulation.hpp"

#include <cstdlib>
#include <exception>
#include <omp.h>
#include <print>
#include <span>

int main(int argc, char* argv[]) {
  try {
    // Parser expects a span; wrap argc/argv minimally.
    std::span<char* const> arguments{argv, static_cast<std::size_t>(argc)};

    auto parsed = CommandLineParser::parse(arguments);
    if (!parsed) {
      std::print(stderr, "{}\nError: {}\n", CommandLineParser::usage_message(), parsed.error());
      return EXIT_FAILURE;
    }

    const auto& cfg = *parsed;

    Simulation simulation(static_cast<int>(cfg.number_of_generations), static_cast<int>(cfg.grid_dimension),
                          static_cast<float>(cfg.initial_density), static_cast<int>(cfg.random_seed));
    // simulation.set_debug_enabled(true);

    double wtime = -omp_get_wtime();
    simulation.run();
    wtime += omp_get_wtime();

    std::println(stderr, "{:.1f}s", wtime);
    simulation.print_results();
    return EXIT_SUCCESS;

  } catch (const std::exception& e) {
    std::println(stderr, "Unhandled exception in main: {}", e.what());
    return EXIT_FAILURE;
  }
}
