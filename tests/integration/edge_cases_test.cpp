// edge_cases_test.cpp — boundary inputs: zero density, tiny grids, output shape.
// (test.sh: "── Edge cases ──")

#include "test_support.hpp"

#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <vector>

using life3d_test::run_life3d;
using life3d_test::sim_stdout;

TEST(EdgeCases, ZeroDensityAllMaxZero)
{
  const auto         out = sim_stdout("5", "16", "0.0", "1");
  std::istringstream ss(out);
  std::string        id, count, rest;
  int                lines = 0;
  while (ss >> id >> count)
  {
    std::getline(ss, rest);
    EXPECT_EQ(count, "0") << out;
    ++lines;
  }
  EXPECT_EQ(lines, 9);
}

TEST(EdgeCases, ZeroDensitySeedIndependent)
{
  EXPECT_EQ(sim_stdout("5", "16", "0.0", "1"), sim_stdout("5", "16", "0.0", "999"));
}

TEST(EdgeCases, OutputFormatNineLinesIdsOneToNine)
{
  const auto               out = sim_stdout("5", "8", "0.3", "7");
  std::istringstream       ss(out);
  std::string              line;
  std::vector<std::string> first_tokens;
  while (std::getline(ss, line))
  {
    std::istringstream ls(line);
    std::string        id;
    ls >> id;
    first_tokens.push_back(id);
  }
  ASSERT_EQ(first_tokens.size(), 9u) << out;
  EXPECT_EQ(first_tokens.front(), "1");
  EXPECT_EQ(first_tokens.back(), "9");
}

TEST(EdgeCases, SmallGridsDoNotCrash)
{
  EXPECT_EQ(run_life3d({"1", "4", "0.5", "123"}).code, 0);
  EXPECT_EQ(run_life3d({"1", "64", "0.25", "7"}).code, 0);
}
