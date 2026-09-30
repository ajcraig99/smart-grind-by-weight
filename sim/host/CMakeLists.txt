# Native (host) build of the digital twin: unmodified firmware + shims + plant + runtime.
#
#   cmake -S sim/host -B sim/out/host -G Ninja && cmake --build sim/out/host
#
# Builds ILP32 (-m32) so `long` and pointers are 32-bit as on the ESP32 and in wasm32.
cmake_minimum_required(VERSION 3.20)
project(grind_twin_host LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

get_filename_component(SIM_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
get_filename_component(REPO_DIR "${SIM_DIR}/.." ABSOLUTE)

include("${SIM_DIR}/cmake/twin_sources.cmake")

set(TWIN_ARCH_FLAGS -m32 -msse2 -mfpmath=sse)
add_compile_options(${TWIN_ARCH_FLAGS} -fno-gnu-unique)
add_link_options(${TWIN_ARCH_FLAGS})
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

twin_add_targets()

enable_testing()
include("${SIM_DIR}/cmake/twin_tests.cmake" OPTIONAL)
