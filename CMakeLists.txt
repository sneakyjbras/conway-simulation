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
# -march=native : use all ISA extensions available on the build host (AVX2/AVX-512)
# -funroll-loops: unroll counted loops (helps the 26-neighbour inner loop)
# -fno-math-errno: safe float relaxation (errno never set by sqrt/fabs)
add_compile_options(
  $<$<COMPILE_LANGUAGE:CXX>:-O3>
  $<$<COMPILE_LANGUAGE:CXX>:-march=native>
  $<$<COMPILE_LANGUAGE:CXX>:-funroll-loops>
  $<$<COMPILE_LANGUAGE:CXX>:-fno-math-errno>
)

# ── OpenMP ───────────────────────────────────────────────────────────────────
# Required for omp_get_wtime() timing and the parallel step loop.
find_package(OpenMP REQUIRED)

# ── Sources ──────────────────────────────────────────────────────────────────
# simulation_neighbors.cpp has been removed: neighbour logic is now a single
# inlined offset-table scan inside simulation.cpp (ghost-cell layout).
add_executable(life3d
  src/main.cpp
  src/parser.cpp
  src/simulation.cpp
  src/generator.cpp
  src/debug_printer.cpp
)

target_include_directories(life3d PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(life3d PRIVATE OpenMP::OpenMP_CXX)

# Export compile commands so clang-tidy / clangd work out of the box.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
