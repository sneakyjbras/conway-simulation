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
  return "Usage: <program> <generations> <grid_dimension> <density> <seed>\n"
         "  Optional flags (defaults in brackets):\n"
         "    --runs N             Monte Carlo trials [1]\n"
         "    --csv PATH           write timing CSV [none]\n"
         "    --dist uniform|gauss initial distribution [uniform]\n"
         "    --enter-sparse F     dirty-ratio enter threshold [0.50]\n"
         "    --exit-sparse F      dirty-ratio exit threshold [0.65]\n"
         "    --enter-churn F      change-ratio enter threshold [0.05]\n"
         "    --exit-churn F       change-ratio exit threshold [0.10]\n"
         "    --uniform-factor F   uniform scratch reserve factor [1.5]\n"
         "    --gauss-factor F     gaussian scratch reserve factor [3.0]\n"
         "    --sa                 enable simulated-annealing thresholds [off]\n"
         "    --sa-t0 F            annealing initial temperature [0.10]\n"
         "    --sa-tf F            annealing final temperature [0.001]\n"
         "    --sa-cool F          annealing cooling rate [0.05]\n"
         "    --sa-window N        annealing EMA window [10]";
}

auto CommandLineParser::parse(std::span<char* const> raw_arguments)
    -> std::expected<SimulationConfig, std::string>
{
  if (raw_arguments.size() < static_cast<std::size_t>(kMinimumArgumentCount))
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

  SimulationConfig config{};
  config.number_of_generations = number_of_generations;
  config.grid_dimension        = grid_dimension;
  config.initial_density       = initial_density;
  config.random_seed           = random_seed;

  // ── Optional flags ────────────────────────────────────────────────────────
  // Parsed positionally after the four required arguments.  A flag taking a
  // value consumes the next token; unknown flags are an error.
  const std::size_t first_optional = static_cast<std::size_t>(kMinimumArgumentCount);

  auto need_value = [&](std::size_t      i,
                        std::string_view flag) -> std::expected<std::string_view, std::string>
  {
    if (i + 1 >= raw_arguments.size())
      return std::unexpected(std::string{flag} + " requires a value");
    return std::string_view{raw_arguments[i + 1]};
  };

  for (std::size_t i = first_optional; i < raw_arguments.size(); ++i)
  {
    const std::string_view flag{raw_arguments[i]};

    auto take_float = [&](float& dst) -> std::expected<void, std::string>
    {
      auto v = need_value(i, flag);
      if (!v)
        return std::unexpected(v.error());
      auto parsed = parse_scalar<float>(*v);
      if (!parsed)
        return std::unexpected(std::string{flag} + ": " + parsed.error());
      dst = *parsed;
      ++i;
      return {};
    };

    auto take_uint = [&](std::uint32_t& dst) -> std::expected<void, std::string>
    {
      auto v = need_value(i, flag);
      if (!v)
        return std::unexpected(v.error());
      auto parsed = parse_scalar<std::uint64_t>(*v);
      if (!parsed)
        return std::unexpected(std::string{flag} + ": " + parsed.error());
      dst = static_cast<std::uint32_t>(*parsed);
      ++i;
      return {};
    };

    std::expected<void, std::string> result{};

    if (flag == "--runs")
      result = take_uint(config.runs);
    else if (flag == "--csv")
    {
      auto v = need_value(i, flag);
      if (!v)
        return std::unexpected(v.error());
      config.csv_path = std::string{*v};
      ++i;
    }
    else if (flag == "--dist")
    {
      auto v = need_value(i, flag);
      if (!v)
        return std::unexpected(v.error());
      if (*v == "uniform")
        config.distribution = DistributionType::kUniform;
      else if (*v == "gauss" || *v == "gaussian")
        config.distribution = DistributionType::kGaussian;
      else
        return std::unexpected(std::string{"--dist must be uniform or gauss"});
      ++i;
    }
    else if (flag == "--enter-sparse")
      result = take_float(config.thresholds.enter_sparse);
    else if (flag == "--exit-sparse")
      result = take_float(config.thresholds.exit_sparse);
    else if (flag == "--enter-churn")
      result = take_float(config.thresholds.enter_churn);
    else if (flag == "--exit-churn")
      result = take_float(config.thresholds.exit_churn);
    else if (flag == "--uniform-factor")
      result = take_float(config.alloc.uniform_reserve_factor);
    else if (flag == "--gauss-factor")
      result = take_float(config.alloc.gaussian_reserve_factor);
    else if (flag == "--sa")
      config.annealing.enabled = true;
    else if (flag == "--sa-t0")
      result = take_float(config.annealing.t_initial);
    else if (flag == "--sa-tf")
      result = take_float(config.annealing.t_final);
    else if (flag == "--sa-cool")
      result = take_float(config.annealing.cooling_rate);
    else if (flag == "--sa-window")
      result = take_uint(config.annealing.window_size);
    else
      return std::unexpected(std::string{"Unknown flag: "} + std::string{flag});

    if (!result)
      return std::unexpected(result.error());
  }

  return config;
}
