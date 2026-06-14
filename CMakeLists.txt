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

# ── Sources ───────────────────────────────────────────────────────────────────
add_executable(life3d
  src/main.cpp
  src/parser.cpp
  src/simulation.cpp
  src/generator.cpp
  src/debug_printer.cpp
  src/annealing.cpp
  src/runner.cpp
  src/dirty_set.cpp
  src/tile_set.cpp
)

target_include_directories(life3d PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# ── OpenMP (timing only) ─────────────────────────────────────────────────────
# Used solely for omp_get_wtime() in Runner.  Optional: when OpenMP is not
# found (e.g. minimal ARM toolchains) the code falls back to
# std::chrono::steady_clock via the _OPENMP guard, so the build still succeeds.
find_package(OpenMP QUIET COMPONENTS CXX)
if(OpenMP_CXX_FOUND)
  target_link_libraries(life3d PRIVATE OpenMP::OpenMP_CXX)
  message(STATUS "OpenMP found — using omp_get_wtime() for timing")
else()
  message(STATUS "OpenMP not found — falling back to std::chrono for timing")
endif()

# Export compile commands so clang-tidy / clangd work out of the box.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
