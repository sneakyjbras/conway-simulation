// parser.cpp – C++23

#include "parser.hpp"

#include <charconv>
#include <cmath>
#include <string>
#include <type_traits>

namespace
{

template <class T>
[[nodiscard]] auto parse_float_or_int(std::string_view text_to_parse, int base_if_int)
    -> std::expected<T, std::string>
{
  T           value{};
  const char* first = text_to_parse.data();
  const char* last  = first + text_to_parse.size();

  if constexpr (std::is_floating_point_v<T>)
  {
    auto [ptr, ec] = std::from_chars(first, last, value);
    if (ec != std::errc{} || ptr != last)
    {
      return std::unexpected(std::string{"could not parse floating-point value '"}
                             + std::string{text_to_parse} + "'");
    }
  }
  else
  {
    static_assert(std::is_integral_v<T>);
    auto [ptr, ec] = std::from_chars(first, last, value, base_if_int);
    if (ec != std::errc{} || ptr != last)
    {
      return std::unexpected(std::string{"could not parse integer value '"}
                             + std::string{text_to_parse} + "'");
    }
  }

  return value;
}

} // namespace

template <class NumericType>
auto CommandLineParser::parse_scalar(std::string_view text_to_parse, std::int32_t integer_base)
    -> std::expected<NumericType, std::string>
{
  return parse_float_or_int<NumericType>(text_to_parse, static_cast<int>(integer_base));
}

template auto CommandLineParser::parse_scalar<std::uint64_t>(std::string_view, std::int32_t)
    -> std::expected<std::uint64_t, std::string>;
template auto CommandLineParser::parse_scalar<float>(std::string_view, std::int32_t)
    -> std::expected<float, std::string>;
template auto CommandLineParser::parse_scalar<std::int32_t>(std::string_view, std::int32_t)
    -> std::expected<std::int32_t, std::string>;

auto CommandLineParser::usage_message() -> std::string_view
{
  return "Usage: <program> <generations> <grid_dimension> <density> <seed>";
}

auto CommandLineParser::parse(std::span<char* const> raw_arguments)
    -> std::expected<SimulationConfig, std::string>
{
  if (raw_arguments.size() != static_cast<std::size_t>(kExpectedArgumentCount))
  {
    return std::unexpected(std::string{usage_message()});
  }

  const auto generations_arg =
      std::string_view{raw_arguments[static_cast<std::size_t>(ArgumentIndex::Generations)]};
  const auto grid_dim_arg =
      std::string_view{raw_arguments[static_cast<std::size_t>(ArgumentIndex::GridDimension)]};
  const auto density_arg =
      std::string_view{raw_arguments[static_cast<std::size_t>(ArgumentIndex::Density)]};
  const auto seed_arg =
      std::string_view{raw_arguments[static_cast<std::size_t>(ArgumentIndex::Seed)]};

  auto parsed_generations    = parse_scalar<std::uint64_t>(generations_arg);
  auto parsed_grid_dimension = parse_scalar<std::uint64_t>(grid_dim_arg);
  auto parsed_density        = parse_scalar<float>(density_arg);
  auto parsed_seed           = parse_scalar<std::int32_t>(seed_arg);

  if (!parsed_generations)
  {
    return std::unexpected("Invalid <generations>: " + parsed_generations.error());
  }
  if (!parsed_grid_dimension)
  {
    return std::unexpected("Invalid <grid_dimension>: " + parsed_grid_dimension.error());
  }
  if (!parsed_density)
  {
    return std::unexpected("Invalid <density>: " + parsed_density.error());
  }
  if (!parsed_seed)
  {
    return std::unexpected("Invalid <seed>: " + parsed_seed.error());
  }

  const std::uint64_t number_of_generations = *parsed_generations;
  const std::uint64_t grid_dimension        = *parsed_grid_dimension;
  const float         initial_density       = *parsed_density;
  const std::int32_t  random_seed           = *parsed_seed;

  if (number_of_generations < kMinimumPositive)
  {
    return std::unexpected("<generations> must be >= 1");
  }
  if (grid_dimension < kMinimumPositive)
  {
    return std::unexpected("<grid_dimension> must be >= 1");
  }
  if (!std::isfinite(initial_density) || initial_density < kMinimumDensityInclusive
      || initial_density > kMaximumDensityInclusive)
  {
    return std::unexpected("<density> must be finite and in [0, 1]");
  }

  return SimulationConfig{
      .number_of_generations = number_of_generations,
      .grid_dimension        = grid_dimension,
      .initial_density       = initial_density,
      .random_seed           = random_seed,
  };
}
