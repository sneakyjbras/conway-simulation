# tests/CMakeLists.txt — GoogleTest harness for life3d
#
# Fetches GoogleTest at a pinned release tag and defines the `life3d_tests`
# target.  This name is a contract: run it with
#   cmake --build build --target life3d_tests
#   ctest --test-dir build

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

add_executable(life3d_tests
  life3d_tests.cpp
)

# GoogleTest is written for older standards; the -march=native / -O3 flags are
# inherited from the top-level add_compile_options and are harmless here.
target_link_libraries(life3d_tests PRIVATE life3d_core GTest::gtest_main)

# Compile-time knowledge of where the built binary and the golden fixtures live,
# so subprocess-driven tests need no runtime configuration.
target_compile_definitions(life3d_tests PRIVATE
  LIFE3D_BINARY_PATH="$<TARGET_FILE:life3d>"
  LIFE3D_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

# Make sure the CLI binary exists before the subprocess tests run.
add_dependencies(life3d_tests life3d)

include(GoogleTest)
# PRE_TEST discovery defers enumeration to test time, when $<TARGET_FILE:life3d>
# is guaranteed built.
gtest_discover_tests(life3d_tests
  DISCOVERY_MODE PRE_TEST
)
