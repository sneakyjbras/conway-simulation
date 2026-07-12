// determinism_test.cpp — the simulation is bit-for-bit reproducible for a fixed
// (gen, N, density, seed), across both the dense and sparse dispatch paths.
// (test.sh: "── Determinism ──")

#include "test_support.hpp"

#include <gtest/gtest.h>

using life3d_test::sim_stdout;

TEST(Determinism, DensePathIdentical)
{
  EXPECT_EQ(sim_stdout("10", "16", "0.25", "42"), sim_stdout("10", "16", "0.25", "42"));
}

TEST(Determinism, SparsePathIdentical)
{
  EXPECT_EQ(sim_stdout("50", "32", "0.08", "42"), sim_stdout("50", "32", "0.08", "42"));
}

TEST(Determinism, DifferentSeedsDiffer)
{
  EXPECT_NE(sim_stdout("10", "16", "0.25", "42"), sim_stdout("10", "16", "0.25", "99"));
}

TEST(Determinism, IncrementalCountsDeterministic)
{
  // test.sh T2: sparse path with incremental species counts stays deterministic.
  EXPECT_EQ(sim_stdout("50", "32", "0.08", "77"), sim_stdout("50", "32", "0.08", "77"));
}
