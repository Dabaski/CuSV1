cmake_minimum_required(VERSION 3.22)
project(golden_gen C CXX)
set(CMAKE_C_STANDARD 11)

# The verbatim SVT extracts carry inherent compiler warnings (C4244 narrowing
# inside untouched SVT code); when this directory is pulled into the ROOT
# build, the parent's /W4 /WX would fail them. Relax for these targets only:
# appended after the parent's add_compile_options, so /W3 + /WX- win.
if(MSVC)
  add_compile_options(/W3 /WX-)
endif()

# composition.c #includes svt_gen.c (the extracted verbatim TU); each main
# includes composition.c, so the mains are the only translation units.
# l7_ctx_shim.cpp + the l7 entropy.cpp twin are linked in for the TD2
# context-equality gate (SVT-extract-vs-l7 bit-exact assertion in-process).
add_executable(golden_primitives main_primitives.c l7_ctx_shim.cpp
                                 ../../src/l7_entropy/entropy.cpp)
target_include_directories(golden_primitives PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../../src/l7_entropy)

add_executable(golden_frame main_frame.c)
target_include_directories(golden_frame PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})