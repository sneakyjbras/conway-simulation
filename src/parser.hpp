#pragma once
// parser.hpp – C++23
// Uses std::expected, std::span, std::from_chars: all available in GCC 13.

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>

struct SimulationConfig {
  std::uint64_t number_of_generations;
  std::uint64_t grid_dimension;
  float         initial_density;
  std::int32_t  random_seed;
};

class CommandLineParser {
public:
  [[nodiscard]] static auto usage_message() -> std::string_view;

  [[nodiscard]] static auto parse(std::span<char* const> raw_arguments)
      -> std::expected<SimulationConfig, std::string>;

private:
  static constexpr std::int32_t  kExpectedArgumentCount     = 5;
  static constexpr std::int32_t  kDefaultIntegerParsingBase = 10;
  static constexpr double        kMinimumDensityInclusive   = 0.0;
  static constexpr double        kMaximumDensityInclusive   = 1.0;
  static constexpr std::uint64_t kMinimumPositive           = 1;

  enum class ArgumentIndex : std::uint8_t {
    Program       = 0,
    Generations   = 1,
    GridDimension = 2,
    Density       = 3,
    Seed          = 4,
  };

  template <class NumericType>
  [[nodiscard]] static auto parse_scalar(std::string_view     text_to_parse,
                                         std::int32_t integer_base = kDefaultIntegerParsingBase)
      -> std::expected<NumericType, std::string>;
};
