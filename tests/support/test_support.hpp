// test_support.hpp — shared helpers for the life3d GoogleTest suite.
//
// The suite spans two altitudes of testing:
//
//   1. Library-level: the sim's translation units are linked directly into the
//      test target (via life3d_core), so functions such as the command-line
//      parser are exercised as plain calls — no subprocess, no I/O.
//
//   2. CLI-level: behaviour that is only observable through the program's
//      stdout/stderr/CSV output is checked by driving the built `life3d` binary
//      as a subprocess, mirroring the correctness checks in test.sh.
//
// These helpers back both altitudes and are shared across every per-category
// test source.  They are `inline` free functions in the `life3d_test`
// namespace, so including this header from multiple translation units is
// ODR-safe and unused helpers raise no warnings.

#ifndef LIFE3D_TEST_SUPPORT_HPP
#define LIFE3D_TEST_SUPPORT_HPP

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

namespace life3d_test
{

// ── Subprocess result ────────────────────────────────────────────────────────
struct ProcResult
{
  std::string out;      // captured stdout
  std::string err;      // captured stderr
  int         code{-1}; // process exit code (127 on spawn failure)
};

// Read an entire file into a string.
inline std::string slurp(const std::string& path)
{
  std::ifstream      in(path, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// Trim trailing newlines so comparisons match bash's $(...) capture, which
// strips trailing newlines.
inline std::string chomp(std::string s)
{
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
    s.pop_back();
  return s;
}

inline std::string shell_quote(const std::string& arg)
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
inline ProcResult run_life3d(const std::vector<std::string>& args)
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
inline std::string sim_stdout(const std::string& gen,
                              const std::string& n,
                              const std::string& density,
                              const std::string& seed)
{
  return chomp(run_life3d({gen, n, density, seed}).out);
}

// Available RAM in KiB (used to gate the large reference-regression cases).
inline long mem_available_kb()
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

// Build an argv span from string storage for the in-process parser tests.
inline std::vector<char*> make_argv(std::vector<std::string>& storage)
{
  std::vector<char*> argv;
  argv.reserve(storage.size());
  for (auto& s : storage)
    argv.push_back(s.data());
  return argv;
}

// Extract the first integer that follows `key` in `hay`, or -1 if absent.
inline long extract_int(const std::string& hay, const std::string& key)
{
  const auto pos = hay.find(key);
  if (pos == std::string::npos)
    return -1;
  return std::strtol(hay.c_str() + pos + key.size(), nullptr, 10);
}

// Absolute path of a golden fixture inside the injected fixtures directory.
inline std::string golden_path(const std::string& file)
{
  return std::string(LIFE3D_GOLDEN_DIR) + "/" + file;
}

} // namespace life3d_test

#endif // LIFE3D_TEST_SUPPORT_HPP
