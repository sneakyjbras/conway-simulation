// main.cpp – C++23
//
// NOTE: std::print / std::println (P2093) requires GCC 14+ libstdc++.
// This file uses <cstdio> + std::format (available in GCC 13) instead,
// which is fully C++23 compliant and avoids the GCC 13 header gap.
//
// Timing uses std::chrono::steady_clock (no OpenMP dependency).

#include "parser.hpp"
#include "simulation.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <format>
#include <span>

int main(int argc, char* argv[])
{
  try
  {
    std::span<char* const> arguments{argv, static_cast<std::size_t>(argc)};

    auto parsed = CommandLineParser::parse(arguments);
    if (!parsed)
    {
      std::fprintf(stderr, "%s\nError: %s\n", CommandLineParser::usage_message().data(),
                   parsed.error().c_str());
      return EXIT_FAILURE;
    }

    const auto& cfg = *parsed;

    Simulation simulation(
        static_cast<int>(cfg.number_of_generations), static_cast<int>(cfg.grid_dimension),
        static_cast<float>(cfg.initial_density), static_cast<int>(cfg.random_seed));

    // Debug printing is OFF by default: printing every cell of every
    // generation dominates runtime and is only useful on tiny grids.
    simulation.set_debug_enabled(false);

    const auto t0 = std::chrono::steady_clock::now();
    simulation.run();
    const auto t1      = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration<double>(t1 - t0).count();

    std::fprintf(stderr, "%.3fs\n", elapsed);
    simulation.print_results();
    return EXIT_SUCCESS;
  }
  catch (const std::exception& e)
  {
    std::fprintf(stderr, "Unhandled exception: %s\n", e.what());
    return EXIT_FAILURE;
  }
}
