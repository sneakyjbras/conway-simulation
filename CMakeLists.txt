cmake_minimum_required(VERSION 3.20)
project(life3d LANGUAGES C CXX)

# C++23 and -O2
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-O2> $<$<COMPILE_LANGUAGE:C>:-O2>)

# OpenMP for omp_get_wtime timing (even for serial implementation)
find_package(OpenMP REQUIRED)

add_executable(life3d
  src/main.cpp
  src/parser.cpp
  src/simulation.cpp
  src/generator.cpp
)
target_include_directories(life3d PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(life3d PRIVATE OpenMP::OpenMP_CXX)
