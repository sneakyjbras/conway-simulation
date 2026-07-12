// monte_carlo_test.cpp — the --runs Monte Carlo runner.
// (test.sh: "── Monte Carlo runner & CSV (Phase 4) ──")

#include "test_support.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

using life3d_test::chomp;
using life3d_test::run_life3d;
using life3d_test::sim_stdout;

TEST(MonteCarlo, RunsThreeMatchesSingleRun)
{
  const auto one   = sim_stdout("10", "16", "0.25", "42");
  const auto three = chomp(run_life3d({"10", "16", "0.25", "42", "--runs", "3"}).out);
  EXPECT_EQ(one, three);
}

TEST(MonteCarlo, PrintsNineResultLines)
{
  const auto three = chomp(run_life3d({"10", "16", "0.25", "42", "--runs", "3"}).out);
  const long lines = std::count(three.begin(), three.end(), '\n') + 1;
  EXPECT_EQ(lines, 9);
}

TEST(MonteCarlo, TimingLineReportsRunCount)
{
  const auto err = run_life3d({"10", "16", "0.25", "42", "--runs", "3"}).err;
  EXPECT_NE(err.find("runs=3"), std::string::npos) << err;
}
