// reference_regression_test.cpp — regression against hand-verified reference
// maxima for larger runs.  The two biggest cases are gated on available RAM and
// GTEST_SKIP when memory is tight (matching test.sh's gate).
// (test.sh: "── Reference ──")

#include "test_support.hpp"

#include <gtest/gtest.h>

#include <string>

using life3d_test::mem_available_kb;
using life3d_test::sim_stdout;

TEST(ReferenceRegression, N64_1000gens_d040_s0)
{
  const std::string expected = "1 81443 62\n2 24563 24\n3 20080 1\n4 19016 1\n5 17576 1\n"
                               "6 16905 1\n7 15793 1\n8 15174 1\n9 14807 1";
  EXPECT_EQ(sim_stdout("1000", "64", "0.4", "0"), expected);
}

TEST(ReferenceRegression, N128_200gens_d050_s1000)
{
  const std::string expected = "1 585667 87\n2 198117 20\n3 123360 6\n4 117152 0\n5 116181 0\n"
                               "6 116832 0\n7 116421 0\n8 116559 0\n9 116344 0";
  EXPECT_EQ(sim_stdout("200", "128", "0.5", "1000"), expected);
}

TEST(ReferenceRegression, N512_10gens_d040_s0)
{
  // ~700 MB working set; skip when free RAM is tight (matches test.sh gate).
  if (mem_available_kb() < 1048576)
    GTEST_SKIP() << "insufficient RAM (need >= 1 GB free)";
  const std::string expected =
      "1 19159668 9\n2 11835001 9\n3 10390082 1\n4 9659953 1\n5 9049805 1\n"
      "6 8563670 1\n7 8146485 1\n8 7824573 1\n9 7579768 1";
  EXPECT_EQ(sim_stdout("10", "512", "0.4", "0"), expected);
}

TEST(ReferenceRegression, N1024_3gens_d040_s100)
{
  // ~5.4 GB working set; skip when free RAM is tight (matches test.sh gate).
  if (mem_available_kb() < 6291456)
    GTEST_SKIP() << "insufficient RAM (need >= 6 GB free)";
  const std::string expected =
      "1 99924176 1\n2 90413216 1\n3 83138735 1\n4 77289277 1\n5 72447849 1\n"
      "6 68444034 1\n7 65197453 1\n8 62633193 1\n9 60610897 1";
  EXPECT_EQ(sim_stdout("3", "1024", "0.4", "100"), expected);
}
