#pragma once
// parser.hpp – C++23
// Uses std::expected, std::span, std::from_chars: all available in GCC 13.

#include "params.hpp"

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>

// Parsed command line: the four required positional arguments plus any
// optional tuning flags.  Optional fields default to the values in params.hpp
// so a bare four-argument invocation is unchanged.
struct SimulationConfig
{
  std::uint64_t number_of_generations{0};
  std::uint64_t grid_dimension{0};
  float         initial_density{0.0f};
  std::int32_t  random_seed{0};

  // Optional tuning knobs (all defaulted).
  DistributionType   distribution{DistributionType::kUniform};
  AllocConfig        alloc{};
  DispatchThresholds thresholds{};
  AnnealingParams    annealing{};
  std::uint32_t      runs{1};
  std::string        csv_path{};
  std::string        export_frames_path{};

  // Assemble the full SimulationParams from this parsed config.
  [[nodiscard]] SimulationParams to_params() const
  {
    SimulationParams p;
    p.problem.grid_dimension = static_cast<std::uint32_t>(grid_dimension);
    p.problem.generations    = static_cast<std::uint32_t>(number_of_generations);
    p.density                = initial_density;
    p.seed                   = random_seed;
    p.distribution           = distribution;
    p.alloc                  = alloc;
    p.thresholds             = thresholds;
    p.annealing              = annealing;
    p.runs                   = runs;
    p.csv_path               = csv_path;
    p.export_frames_path     = export_frames_path;
    return p;
  }
};

class CommandLineParser
{
public:
  [[nodiscard]] static auto usage_message() -> std::string_view;

  [[nodiscard]] static auto parse(std::span<char* const> raw_arguments)
      -> std::expected<SimulationConfig, std::string>;

private:
  static constexpr std::int32_t  kMinimumArgumentCount      = 5;
  static constexpr std::int32_t  kDefaultIntegerParsingBase = 10;
  static constexpr double        kMinimumDensityInclusive   = 0.0;
  static constexpr double        kMaximumDensityInclusive   = 1.0;
  static constexpr std::uint64_t kMinimumPositive           = 1;

  enum class ArgumentIndex : std::uint8_t
  {
    Program       = 0,
    Generations   = 1,
    GridDimension = 2,
    Density       = 3,
    Seed          = 4,
  };

  template <class NumericType>
  [[nodiscard]] static auto parse_scalar(std::string_view text_to_parse,
                                         std::int32_t     integer_base = kDefaultIntegerParsingBase)
      -> std::expected<NumericType, std::string>;
};
