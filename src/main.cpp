// main.cpp – C++23
//
// Thin entry point: parse the command line, build the parameters, and hand
// off to the Runner.  All timing and Monte Carlo logic lives in Runner; all
// computation lives in Simulation.  main() owns neither.
//
// NOTE: std::print / std::println (P2093) requires GCC 14+ libstdc++.
// This file uses <cstdio> + std::format (available in GCC 13) instead.

#include "parser.hpp"
#include "runner.hpp"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <span>

int main(int argc, char* argv[])
{
  try
  {
    std::span<char* const> arguments{argv, static_cast<std::size_t>(argc)};

    auto parsed = CommandLineParser::parse(arguments);
    if (!parsed)
    {
      // usage_message() returns a std::string_view, which printf's %s cannot
      // safely consume via .data() alone (no null-termination guarantee).
      // %.*s with an explicit length is the correct way to print a
      // string_view through the printf family.
      const auto usage = CommandLineParser::usage_message();
      std::fprintf(stderr, "%.*s\nError: %s\n", static_cast<int>(usage.size()), usage.data(),
                   parsed.error().c_str());
      return EXIT_FAILURE;
    }

    Runner runner(parsed->to_params());
    (void)runner.execute();
    return EXIT_SUCCESS;
  }
  catch (const std::exception& e)
  {
    std::fprintf(stderr, "Unhandled exception: %s\n", e.what());
    return EXIT_FAILURE;
  }
}
