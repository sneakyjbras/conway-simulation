#include "parser.hpp"

#include <charconv>
#include <cmath> // std::isfinite
#include <string>

namespace {

template <class T>
[[nodiscard]] auto parse_float_or_int(std::string_view text_to_parse, int base_if_int)
    -> std::expected<T, std::string> {
  T value{};
  const char* const first = text_to_parse.begin();
  const char* const last  = text_to_parse.end();

  if constexpr (std::is_floating_point_v<T>) {
    auto [ptr, ec] = std::from_chars(first, last, value, std::chars_format::general);
    if (ec != std::errc() || ptr != last) {
      return std::unexpected("not a valid floating-point number");
    }
  } else {
    auto [ptr, ec] = std::from_chars(first, last, value, base_if_int);
    if (ec != std::errc() || ptr != last) {
      return std::unexpected("not a valid integer");
    }
  }
  return value;
}

} // namespace

template <class NumericType>
auto CommandLineParser::parse_scalar(std::string_view text_to_parse, int integer_base)
    -> std::expected<NumericType, std::string> {
  return parse_float_or_int<NumericType>(text_to_parse, integer_base);
}

// Explicit instantiations for the types we use.
template auto CommandLineParser::parse_scalar<std::uint32_t>(std::string_view, int)
    -> std::expected<std::uint32_t, std::string>;
template auto CommandLineParser::parse_scalar<double>(std::string_view, int) -> std::expected<double, std::string>;

auto CommandLineParser::usage_message() -> std::string_view {
  return "Usage: <program> <generations> <grid_dimension> <density> <seed>";
}

auto CommandLineParser::parse(std::span<char* const> raw_arguments) -> std::expected<SimulationConfig, std::string> {
  if (static_cast<int>(raw_arguments.size()) != kExpectedArgumentCount) {
    return std::unexpected(std::string{usage_message()});
  }

  const auto generations_arg = std::string_view{raw_arguments[static_cast<std::size_t>(ArgumentIndex::Generations)]};
  const auto grid_dim_arg    = std::string_view{raw_arguments[static_cast<std::size_t>(ArgumentIndex::GridDimension)]};
  const auto density_arg     = std::string_view{raw_arguments[static_cast<std::size_t>(ArgumentIndex::Density)]};
  const auto seed_arg        = std::string_view{raw_arguments[static_cast<std::size_t>(ArgumentIndex::Seed)]};

  auto parsed_generations    = parse_scalar<std::uint32_t>(generations_arg);
  auto parsed_grid_dimension = parse_scalar<std::uint32_t>(grid_dim_arg);
  auto parsed_density        = parse_scalar<double>(density_arg);
  auto parsed_seed           = parse_scalar<std::uint32_t>(seed_arg);

  if (!parsed_generations) {
    return std::unexpected("Invalid <generations>: " + parsed_generations.error());
  }
  if (!parsed_grid_dimension) {
    return std::unexpected("Invalid <grid_dimension>: " + parsed_grid_dimension.error());
  }
  if (!parsed_density) {
    return std::unexpected("Invalid <density>: " + parsed_density.error());
  }
  if (!parsed_seed) {
    return std::unexpected("Invalid <seed>: " + parsed_seed.error());
  }

  const std::uint32_t number_of_generations = *parsed_generations;
  const std::uint32_t grid_dimension        = *parsed_grid_dimension;
  const double initial_density              = *parsed_density;
  const std::uint32_t random_seed           = *parsed_seed;

  if (number_of_generations < kMinimumPositive) {
    return std::unexpected("<generations> must be >= 1");
  }
  if (grid_dimension < kMinimumPositive) {
    return std::unexpected("<grid_dimension> must be >= 1");
  }
  if (!std::isfinite(initial_density) || initial_density < kMinimumDensityInclusive ||
      initial_density > kMaximumDensityInclusive) {
    return std::unexpected("<density> must be finite and in [0, 1]");
  }

  return SimulationConfig{
      .number_of_generations = number_of_generations,
      .grid_dimension        = grid_dimension,
      .initial_density       = initial_density,
      .random_seed           = random_seed,
  };
}
