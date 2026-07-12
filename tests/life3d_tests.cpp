// life3d_tests.cpp — GoogleTest harness for the life3d simulation.
//
// Two altitudes of testing:
//
//   1. Library-level: the sim's translation units are linked directly into this
//      target (via life3d_core), so the command-line parser is exercised as a
//      plain function call — no subprocess, no I/O.
//
//   2. CLI-level: behaviour that is only observable through the program's
//      stdout/stderr/CSV output is checked by driving the built `life3d` binary
//      as a subprocess, mirroring the correctness checks in test.sh.
//
// The performance benchmarks (test.sh --bench) are intentionally NOT ported —
// those stay in the shell script.

#include "parser.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

#ifndef LIFE3D_BINARY_PATH
#error "LIFE3D_BINARY_PATH must be defined by the build system"
#endif
#ifndef LIFE3D_GOLDEN_DIR
#error "LIFE3D_GOLDEN_DIR must be defined by the build system"
#endif

namespace
{

// ── Subprocess helper ────────────────────────────────────────────────────────
struct ProcResult
{
  std::string out;      // captured stdout
  std::string err;      // captured stderr
  int         code{-1}; // process exit code (127 on spawn failure)
};

std::string slurp(const std::string& path)
{
  std::ifstream in(path, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// Trim a single trailing newline so comparisons match bash's $(...) capture,
// which strips trailing newlines.
std::string chomp(std::string s)
{
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
    s.pop_back();
  return s;
}

std::string shell_quote(const std::string& arg)
{
  std::string q = "'";
  for (char c : arg)
  {
    if (c == '\'')
      q += "'\\''";
    else
      q += c;
  }
  q += "'";
  return q;
}

// Run the life3d binary with the given argument list; capture stdout and
// stderr separately via temp files.
ProcResult run_life3d(const std::vector<std::string>& args)
{
  const std::string out_path = std::string("/tmp/life3d_gtest_out_") + std::to_string(getpid())
                               + "_" + std::to_string(rand()) + ".txt";
  const std::string err_path = std::string("/tmp/life3d_gtest_err_") + std::to_string(getpid())
                               + "_" + std::to_string(rand()) + ".txt";

  std::string cmd = shell_quote(LIFE3D_BINARY_PATH);
  for (const auto& a : args)
    cmd += " " + shell_quote(a);
  cmd += " >" + shell_quote(out_path) + " 2>" + shell_quote(err_path);

  ProcResult r;
  const int  rc = std::system(cmd.c_str());
  r.code        = WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
  r.out         = slurp(out_path);
  r.err         = slurp(err_path);
  std::remove(out_path.c_str());
  std::remove(err_path.c_str());
  return r;
}

// Convenience: the four-positional-argument form, stdout only (chomped).
std::string sim_stdout(const std::string& gen, const std::string& n, const std::string& density,
                       const std::string& seed)
{
  return chomp(run_life3d({gen, n, density, seed}).out);
}

long mem_available_kb()
{
  std::ifstream in("/proc/meminfo");
  std::string   key;
  long          value = 0;
  std::string   unit;
  while (in >> key >> value >> unit)
  {
    if (key == "MemAvailable:")
      return value;
  }
  return 0;
}

// Build an argv span from string literals for the in-process parser tests.
std::vector<char*> make_argv(std::vector<std::string>& storage)
{
  std::vector<char*> argv;
  argv.reserve(storage.size());
  for (auto& s : storage)
    argv.push_back(s.data());
  return argv;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Library-level: command-line parser (called directly, no subprocess).
// ─────────────────────────────────────────────────────────────────────────────
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
  std::vector<std::string> a{"life3d", "10",  "16",   "0.25",   "42",
                             "--runs", "3",   "--sa", "--sa-t0", "0.5",
                             "--csv",  "x.csv"};
  auto argv   = make_argv(a);
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

// ─────────────────────────────────────────────────────────────────────────────
// Determinism (dense & sparse paths).  (test.sh: "── Determinism ──")
// ─────────────────────────────────────────────────────────────────────────────
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

// ─────────────────────────────────────────────────────────────────────────────
// Golden-file regression (dense d=0.25 and sparse d=0.08).
// ─────────────────────────────────────────────────────────────────────────────
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

// Give GoogleTest a readable rendering so the CTest display name (derived from
// the "# GetParam()" comment) is meaningful rather than "40-byte object <...>".
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
  const auto& c        = GetParam();
  const auto  golden   = chomp(slurp(std::string(LIFE3D_GOLDEN_DIR) + "/" + c.file));
  ASSERT_FALSE(golden.empty()) << "golden fixture missing/empty: " << c.file;
  const auto actual = sim_stdout(c.gen, c.n, c.density, c.seed);
  EXPECT_EQ(golden, actual);
}

INSTANTIATE_TEST_SUITE_P(
    Dense, GoldenRegression,
    ::testing::Values(GoldenCase{"10", "16", "0.25", "42", "golden_gen10_N16_d0.25_s42.txt"},
                      GoldenCase{"50", "64", "0.25", "1", "golden_gen50_N64_d0.25_s1.txt"},
                      GoldenCase{"100", "32", "0.25", "99", "golden_gen100_N32_d0.25_s99.txt"}),
    GoldenName{});

INSTANTIATE_TEST_SUITE_P(
    Sparse, GoldenRegression,
    ::testing::Values(GoldenCase{"50", "32", "0.08", "42", "golden_gen50_N32_d0.08_s42.txt"},
                      GoldenCase{"100", "64", "0.08", "7", "golden_gen100_N64_d0.08_s7.txt"}),
    GoldenName{});

// ─────────────────────────────────────────────────────────────────────────────
// Dense / sparse mode coverage (stderr mode line).  (test.sh "── Mode coverage")
// ─────────────────────────────────────────────────────────────────────────────
namespace
{
long extract_int(const std::string& hay, const std::string& key)
{
  const auto pos = hay.find(key);
  if (pos == std::string::npos)
    return -1;
  return std::strtol(hay.c_str() + pos + key.size(), nullptr, 10);
}
} // namespace

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
  const auto dense =
      chomp(run_life3d({"50", "32", "0.4", "7", "--enter-sparse", "0.0", "--enter-churn", "0.0"})
                .out);
  const auto sparse =
      chomp(run_life3d({"50", "32", "0.4", "7", "--exit-sparse", "1.1", "--exit-churn", "1.1"})
                .out);
  EXPECT_EQ(dense, sparse);
}

// ─────────────────────────────────────────────────────────────────────────────
// Simulated annealing.  (test.sh "── Simulated annealing (Phase 3) ──")
// ─────────────────────────────────────────────────────────────────────────────
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

// ─────────────────────────────────────────────────────────────────────────────
// Monte Carlo runner.  (test.sh "── Monte Carlo runner & CSV (Phase 4) ──")
// ─────────────────────────────────────────────────────────────────────────────
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

// ─────────────────────────────────────────────────────────────────────────────
// CSV output.  (test.sh T7)
// ─────────────────────────────────────────────────────────────────────────────
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

// ─────────────────────────────────────────────────────────────────────────────
// Edge cases.  (test.sh "── Edge cases ──")
// ─────────────────────────────────────────────────────────────────────────────
TEST(EdgeCases, ZeroDensityAllMaxZero)
{
  const auto        out = sim_stdout("5", "16", "0.0", "1");
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
  const auto        out = sim_stdout("5", "8", "0.3", "7");
  std::istringstream ss(out);
  std::string        line;
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

// ─────────────────────────────────────────────────────────────────────────────
// Reference regression (hardcoded verified maxima).  (test.sh "── Reference ──")
// ─────────────────────────────────────────────────────────────────────────────
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
  const std::string expected = "1 19159668 9\n2 11835001 9\n3 10390082 1\n4 9659953 1\n5 9049805 1\n"
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
