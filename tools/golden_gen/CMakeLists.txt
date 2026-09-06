cmake_minimum_required(VERSION 3.22)
project(golden_gen C)
set(CMAKE_C_STANDARD 11)

# composition.c #includes svt_gen.c (the extracted verbatim TU); each main
# includes composition.c, so the mains are the only translation units.
add_executable(golden_primitives main_primitives.c)
target_include_directories(golden_primitives PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(golden_frame main_frame.c)
target_include_directories(golden_frame PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})