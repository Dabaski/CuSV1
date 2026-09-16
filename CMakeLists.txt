cmake_minimum_required(VERSION 3.22)
project(av1_gpu_pascal LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /WX /permissive- /EHsc)
endif()

include(CTest)
find_package(CUDAToolkit QUIET)
function(av1_cuda_test name target)
  add_test(NAME ${name} COMMAND ${target})
  if(CUDAToolkit_FOUND AND DEFINED CUDAToolkit_BIN_DIR)
    set_tests_properties(${name} PROPERTIES ENVIRONMENT_MODIFICATION
      "PATH=path_list_prepend:${CUDAToolkit_BIN_DIR}")
  endif()
endfunction()
if(BUILD_TESTING)
  add_subdirectory(src/l0_core)
  add_subdirectory(src/l1_pixels)
  add_subdirectory(src/l2_gpurt)
  add_subdirectory(src/l3_transforms)
  add_subdirectory(src/l4_intra)
  add_subdirectory(src/l5_motion)
  add_subdirectory(src/l7_entropy)
  add_subdirectory(src/l8_bitstream)
  add_subdirectory(src/l6_pipeline)
endif()

# benchmark tool: own target, not a ctest (tools/bench/README notes inside
# AGENTS.md). Built alongside the libs; run its exe manually.
add_subdirectory(tools/bench)

# committed golden-gate infrastructure — regenerates
# expected_primitives.txt; see tools/golden_gen/README.md
add_subdirectory(tools/golden_gen)
