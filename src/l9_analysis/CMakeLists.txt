add_library(av1_analysis STATIC analysis.cpp)
target_include_directories(av1_analysis PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(av1_analysis PUBLIC av1_core)

add_executable(av1_l9_analysis_tests tests/test_harness.cpp tests/test_variance.cpp
               tests/test_pad.cpp tests/test_downsample_filtering.cpp)
target_include_directories(av1_l9_analysis_tests PRIVATE ${PROJECT_SOURCE_DIR}/third_party/doctest)
target_link_libraries(av1_l9_analysis_tests PRIVATE av1_analysis)
av1_cuda_test(l9_analysis av1_l9_analysis_tests)
