cmake_minimum_required(VERSION 3.20)
project(life3d LANGUAGES CXX)

# ── Language standard ────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ── Optimisation flags ───────────────────────────────────────────────────────
# -O3           : full optimisation (inlining, vectorisation, loop opts)
# -march=native : use all ISA extensions on the build host (AVX2 / AVX-512)
# -funroll-loops: unroll counted loops (helps the 26-neighbour inner loop)
# -fno-math-errno: safe float relaxation (errno never set by sqrt/fabs)
add_compile_options(
  $<$<COMPILE_LANGUAGE:CXX>:-O3>
  $<$<COMPILE_LANGUAGE:CXX>:-march=native>
  $<$<COMPILE_LANGUAGE:CXX>:-funroll-loops>
  $<$<COMPILE_LANGUAGE:CXX>:-fno-math-errno>
)

# ── Core library ─────────────────────────────────────────────────────────────
# Every translation unit except main.cpp lives in a reusable static library so
# both the CLI executable and the GoogleTest harness can link the exact same
# object code and call the simulation's functions directly (no subprocess for
# library-level behaviour).
add_library(life3d_core STATIC
  src/parser.cpp
  src/simulation.cpp
  src/simulation_init.cpp
  src/simulation_step.cpp
  src/frame_exporter.cpp
  src/generator.cpp
  src/debug_printer.cpp
  src/annealing.cpp
  src/runner.cpp
  src/dirty_set.cpp
  src/tile_set.cpp
)

# PUBLIC so both the executable and the test target inherit the include path.
target_include_directories(life3d_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# ── Reproducible RNG ─────────────────────────────────────────────────────────
# The initial-grid RNG (generator.cpp) does `0.5f + scale * x` in float.  Under
# -O3 -march=native on an FMA-capable CPU the compiler contracts that into a
# single fused-multiply-add (one rounding step), which yields bit-different
# results from the separately-rounded form and, crucially, differs across ISAs
# (x86 vs ARM).  That perturbs the generated grid and makes results
# non-reproducible from machine to machine.  Pin contraction off for this one
# translation unit so the grid — and every downstream result and golden file —
# is identical on every platform.  (generator.cpp runs once at startup, so the
# lost FMA has no measurable performance impact.)
set_source_files_properties(src/generator.cpp PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")

# ── OpenMP (timing only) ─────────────────────────────────────────────────────
# Used solely for omp_get_wtime() in Runner.  Optional: when OpenMP is not
# found (e.g. minimal ARM toolchains) the code falls back to
# std::chrono::steady_clock via the _OPENMP guard, so the build still succeeds.
find_package(OpenMP QUIET COMPONENTS CXX)
if(OpenMP_CXX_FOUND)
  target_link_libraries(life3d_core PUBLIC OpenMP::OpenMP_CXX)
  message(STATUS "OpenMP found — using omp_get_wtime() for timing")
else()
  message(STATUS "OpenMP not found — falling back to std::chrono for timing")
endif()

# ── CLI executable ───────────────────────────────────────────────────────────
add_executable(life3d src/main.cpp)
target_link_libraries(life3d PRIVATE life3d_core)

# Export compile commands so clang-tidy / clangd work out of the box.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Tests (GoogleTest) ───────────────────────────────────────────────────────
# BUILD_TESTING is the standard CTest switch (default ON).  A normal
# `./build.sh` still produces the life3d binary; the test wiring only adds an
# extra target.  Pass -DBUILD_TESTING=OFF to skip the GoogleTest download and
# the test target entirely (e.g. for offline builds).
include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
