// annealing_test.cpp — simulated-annealing diagnostics on stderr and the
// invariant that annealing never changes simulation output.
// (test.sh: "── Simulated annealing (Phase 3) ──")

#include "test_support.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

using life3d_test::chomp;
using life3d_test::run_life3d;
using life3d_test::sim_stdout;

TEST(SimulatedAnnealing, DisabledEmitsNoSaLine)
{
  const auto err = run_life3d({"100", "32", "0.08", "42"}).err;
  EXPECT_NE(err.find("dirty_thr=50%/65%"), std::string::npos) << err;
  // No line beginning with "sa:".
  EXPECT_TRUE(err.rfind("sa:", 0) != 0 && err.find("\nsa:") == std::string::npos) << err;
}

TEST(SimulatedAnnealing, EnabledEmitsSaLineAndShiftsThreshold)
{
  const auto err = run_life3d({"100", "32", "0.08", "42", "--sa", "--sa-t0", "0.5"}).err;
  const auto pos = err.find("sa:");
  ASSERT_NE(pos, std::string::npos) << err;
  const auto line_end = err.find('\n', pos);
  const auto line     = err.substr(pos, line_end - pos);
  const auto mn       = line.find("min=");
  const auto mx       = line.find("max=");
  ASSERT_NE(mn, std::string::npos) << line;
  ASSERT_NE(mx, std::string::npos) << line;
  const double min_v = std::strtod(line.c_str() + mn + 4, nullptr);
  const double max_v = std::strtod(line.c_str() + mx + 4, nullptr);
  EXPECT_GT(max_v, min_v) << line;
}

TEST(SimulatedAnnealing, PreservesCorrectness)
{
  // test.sh T5b: --sa output identical to non-SA output.
  const auto base = sim_stdout("100", "32", "0.08", "42");
  const auto sa   = chomp(
      run_life3d({"100", "32", "0.08", "42", "--sa", "--sa-t0", "3.0", "--sa-cool", "0.01"}).out);
  EXPECT_EQ(base, sa);
}
