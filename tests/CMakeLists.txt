# tests/CMakeLists.txt — GoogleTest harness for life3d
#
# Fetches GoogleTest at a pinned release tag and defines the `life3d_tests`
# target.  This name is a contract: run it with
#   cmake --build build --target life3d_tests
#   ctest --test-dir build
#
# The suite is split by category into subfolders — every source below is
# compiled into the single `life3d_tests` executable so ctest discovers and
# runs the entire suite:
#   unit/        in-process library tests (parser)
#   integration/ subprocess-driven CLI behaviour (stdout/stderr/CSV, --runs, --sa)
#   regression/  golden-file and hand-verified reference-maxima regression
#   fixtures/    golden_*.txt data files (injected via LIFE3D_GOLDEN_DIR)
#   support/     shared test helpers (subprocess runner, output capture, paths)

include(FetchContent)

# Pinned to a specific release tag (not a moving branch) for reproducible
# builds.  Bump deliberately.
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.15.2
  GIT_SHALLOW    TRUE
)

# Do not let GoogleTest's own install/testing wiring leak into this project.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

# Single aggregate target over every per-category source.  Keeping one
# executable means one gtest_discover_tests call enumerates the whole suite.
add_executable(life3d_tests
  unit/parser_test.cpp
  integration/determinism_test.cpp
  integration/mode_dispatch_test.cpp
  integration/annealing_test.cpp
  integration/monte_carlo_test.cpp
  integration/csv_test.cpp
  integration/edge_cases_test.cpp
  regression/golden_regression_test.cpp
  regression/reference_regression_test.cpp
)

# Shared test-support header (subprocess runner, output capture, path helpers).
target_include_directories(life3d_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/support)

# GoogleTest is written for older standards; the -march=native / -O3 flags are
# inherited from the top-level add_compile_options and are harmless here.
target_link_libraries(life3d_tests PRIVATE life3d_core GTest::gtest_main)

# Compile-time knowledge of where the built binary and the golden fixtures live,
# so subprocess-driven tests need no runtime configuration.  The golden files
# now live in tests/fixtures/.
target_compile_definitions(life3d_tests PRIVATE
  LIFE3D_BINARY_PATH="$<TARGET_FILE:life3d>"
  LIFE3D_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

# Make sure the CLI binary exists before the subprocess tests run.
add_dependencies(life3d_tests life3d)

include(GoogleTest)
# PRE_TEST discovery defers enumeration to test time, when $<TARGET_FILE:life3d>
# is guaranteed built.
gtest_discover_tests(life3d_tests
  DISCOVERY_MODE PRE_TEST
)
