// mode_dispatch_test.cpp — dense/sparse dispatch coverage via the stderr mode
// line, plus the branchless-equivalence check.
// (test.sh: "── Mode coverage ──")

#include "test_support.hpp"

#include <gtest/gtest.h>

#include <string>

using life3d_test::chomp;
using life3d_test::extract_int;
using life3d_test::run_life3d;

TEST(ModeCoverage, SparseMixFiresBothPaths)
{
  // d=0.08 N=32 gen=100 exercises both dense and sparse dispatch.
  const auto err = run_life3d({"100", "32", "0.08", "42"}).err;
  EXPECT_GT(extract_int(err, "dense="), 0) << err;
  EXPECT_GT(extract_int(err, "sparse="), 0) << err;
}

TEST(ModeCoverage, HighDensityStaysDense)
{
  const auto err = run_life3d({"10", "16", "0.25", "42"}).err;
  EXPECT_GT(extract_int(err, "dense="), 0) << err;
}

TEST(ModeCoverage, EmptyGridUsesSparseOnly)
{
  // test.sh T1: d=0.0 -> dirty_ratio 0 -> sparse from gen 0 -> dense=0.
  const auto err = run_life3d({"10", "16", "0.0", "1"}).err;
  EXPECT_EQ(extract_int(err, "dense="), 0) << err;
}

TEST(ModeCoverage, HysteresisThresholdsInModeLine)
{
  // test.sh T3: spec threshold constants must appear verbatim.
  const auto err = run_life3d({"100", "32", "0.08", "42"}).err;
  EXPECT_NE(err.find("dirty_thr=50%/65%"), std::string::npos) << err;
  EXPECT_NE(err.find("churn_thr=5%/10%"), std::string::npos) << err;
}

TEST(ModeCoverage, BranchlessDenseEqualsSparseAtHighDensity)
{
  // test.sh T8: force all-dense vs all-sparse at d=0.4; output must match.
  const auto dense = chomp(
      run_life3d({"50", "32", "0.4", "7", "--enter-sparse", "0.0", "--enter-churn", "0.0"}).out);
  const auto sparse = chomp(
      run_life3d({"50", "32", "0.4", "7", "--exit-sparse", "1.1", "--exit-churn", "1.1"}).out);
  EXPECT_EQ(dense, sparse);
}
