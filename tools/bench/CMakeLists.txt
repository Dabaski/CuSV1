# tools/bench - console benchmark tool (own target, NOT a ctest).
# Measurement only; links the av1 libraries from the main build tree.
add_executable(av1_bench bench.cpp)
target_include_directories(av1_bench PRIVATE
  ${PROJECT_SOURCE_DIR}/src/l0_core
  ${PROJECT_SOURCE_DIR}/src/l1_pixels
  ${PROJECT_SOURCE_DIR}/src/l2_gpurt
  ${PROJECT_SOURCE_DIR}/src/l3_transforms
  ${PROJECT_SOURCE_DIR}/src/l4_intra
  ${PROJECT_SOURCE_DIR}/src/l5_motion
  ${PROJECT_SOURCE_DIR}/src/l6_pipeline)
target_link_libraries(av1_bench PRIVATE
  av1_pipeline av1_intra av1_transforms av1_motion av1_gpurt)
