// golden_regression_test.cpp — output regression against committed golden
// fixtures for both the dense (d=0.25) and sparse (d=0.08) paths.  The golden
// files live in tests/fixtures/, injected via LIFE3D_GOLDEN_DIR.

#include "test_support.hpp"

#include <gtest/gtest.h>

#include <ostream>
#include <string>

using life3d_test::chomp;
using life3d_test::golden_path;
using life3d_test::sim_stdout;
using life3d_test::slurp;

namespace
{
struct GoldenCase
{
  const char* gen;
  const char* n;
  const char* density;
  const char* seed;
  const char* file;
};

// Give GoogleTest a readable rendering of the parameter for diagnostics.
void PrintTo(const GoldenCase& c, std::ostream* os)
{
  *os << "gen" << c.gen << "_N" << c.n << "_d" << c.density << "_s" << c.seed;
}
} // namespace

class GoldenRegression : public ::testing::TestWithParam<GoldenCase>
{
};

struct GoldenName
{
  std::string operator()(const ::testing::TestParamInfo<GoldenCase>& info) const
  {
    const auto& c = info.param;
    return std::string("gen") + c.gen + "_N" + c.n + "_s" + c.seed;
  }
};

TEST_P(GoldenRegression, MatchesGoldenFile)
{
  const auto& c      = GetParam();
  const auto  golden = chomp(slurp(golden_path(c.file)));
  ASSERT_FALSE(golden.empty()) << "golden fixture missing/empty: " << c.file;
  const auto actual = sim_stdout(c.gen, c.n, c.density, c.seed);
  EXPECT_EQ(golden, actual);
}

INSTANTIATE_TEST_SUITE_P(
    Dense,
    GoldenRegression,
    ::testing::Values(GoldenCase{"10", "16", "0.25", "42", "golden_gen10_N16_d0.25_s42.txt"},
                      GoldenCase{"50", "64", "0.25", "1", "golden_gen50_N64_d0.25_s1.txt"},
                      GoldenCase{"100", "32", "0.25", "99", "golden_gen100_N32_d0.25_s99.txt"}),
    GoldenName{});

INSTANTIATE_TEST_SUITE_P(
    Sparse,
    GoldenRegression,
    ::testing::Values(GoldenCase{"50", "32", "0.08", "42", "golden_gen50_N32_d0.08_s42.txt"},
                      GoldenCase{"100", "64", "0.08", "7", "golden_gen100_N64_d0.08_s7.txt"}),
    GoldenName{});
