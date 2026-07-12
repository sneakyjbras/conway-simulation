// csv_test.cpp — the --csv output: header, per-run data rows, summary footer.
// (test.sh T7)

#include "test_support.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <sstream>
#include <string>

using life3d_test::run_life3d;
using life3d_test::slurp;

TEST(Csv, WritesHeaderRowsAndSummary)
{
  const std::string csv = std::string("/tmp/life3d_gtest_csv_") + std::to_string(getpid()) + ".csv";
  std::remove(csv.c_str());
  const auto r = run_life3d({"10", "16", "0.25", "42", "--runs", "3", "--csv", csv});
  ASSERT_EQ(r.code, 0);

  const auto contents = slurp(csv);
  std::remove(csv.c_str());
  ASSERT_FALSE(contents.empty());

  std::istringstream ss(contents);
  std::string        line;
  std::getline(ss, line);
  EXPECT_EQ(line, "run,gen,N,density,seed,dist,elapsed_s,arch");

  int  data_rows = 0;
  bool summary   = false;
  while (std::getline(ss, line))
  {
    if (line.rfind("# mean=", 0) == 0)
      summary = true;
    else if (line.rfind("0,10,16,", 0) == 0 || line.rfind("1,10,16,", 0) == 0
             || line.rfind("2,10,16,", 0) == 0)
      ++data_rows;
  }
  EXPECT_EQ(data_rows, 3);
  EXPECT_TRUE(summary) << contents;
}
