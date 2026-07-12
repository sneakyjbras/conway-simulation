// parser_test.cpp — library-level tests for CommandLineParser::parse.
//
// The parser is linked directly into the test target via life3d_core, so it is
// exercised as a plain function call — no subprocess, no I/O.

#include "parser.hpp"
#include "test_support.hpp"

#include <gtest/gtest.h>

#include <span>
#include <string>
#include <vector>

using life3d_test::make_argv;

TEST(Parser, AcceptsFourPositionalArgs)
{
  std::vector<std::string> a{"life3d", "10", "16", "0.25", "42"};
  auto                     argv = make_argv(a);
  auto parsed = CommandLineParser::parse(std::span<char* const>{argv.data(), argv.size()});
  ASSERT_TRUE(parsed.has_value()) << parsed.error();
  EXPECT_EQ(parsed->number_of_generations, 10u);
  EXPECT_EQ(parsed->grid_dimension, 16u);
  EXPECT_FLOAT_EQ(parsed->initial_density, 0.25f);
  EXPECT_EQ(parsed->random_seed, 42);
  EXPECT_EQ(parsed->runs, 1u);
  EXPECT_FALSE(parsed->annealing.enabled);
}

TEST(Parser, RejectsTooFewArgs)
{
  std::vector<std::string> a{"life3d", "10", "16"};
  auto                     argv = make_argv(a);
  auto parsed = CommandLineParser::parse(std::span<char* const>{argv.data(), argv.size()});
  EXPECT_FALSE(parsed.has_value());
}

TEST(Parser, RejectsOutOfRangeDensity)
{
  std::vector<std::string> a{"life3d", "10", "16", "1.5", "42"};
  auto                     argv = make_argv(a);
  auto parsed = CommandLineParser::parse(std::span<char* const>{argv.data(), argv.size()});
  EXPECT_FALSE(parsed.has_value());
}

TEST(Parser, RejectsZeroGenerations)
{
  std::vector<std::string> a{"life3d", "0", "16", "0.25", "42"};
  auto                     argv = make_argv(a);
  auto parsed = CommandLineParser::parse(std::span<char* const>{argv.data(), argv.size()});
  EXPECT_FALSE(parsed.has_value());
}

TEST(Parser, ParsesOptionalFlags)
{
  std::vector<std::string> a{"life3d", "10",   "16",      "0.25", "42",    "--runs",
                             "3",      "--sa", "--sa-t0", "0.5",  "--csv", "x.csv"};
  auto                     argv = make_argv(a);
  auto parsed = CommandLineParser::parse(std::span<char* const>{argv.data(), argv.size()});
  ASSERT_TRUE(parsed.has_value()) << parsed.error();
  EXPECT_EQ(parsed->runs, 3u);
  EXPECT_TRUE(parsed->annealing.enabled);
  EXPECT_FLOAT_EQ(parsed->annealing.t_initial, 0.5f);
  EXPECT_EQ(parsed->csv_path, "x.csv");
}

TEST(Parser, RejectsUnknownFlag)
{
  std::vector<std::string> a{"life3d", "10", "16", "0.25", "42", "--nope"};
  auto                     argv = make_argv(a);
  auto parsed = CommandLineParser::parse(std::span<char* const>{argv.data(), argv.size()});
  EXPECT_FALSE(parsed.has_value());
}
