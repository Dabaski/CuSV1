add_library(av1_utility STATIC utility.cpp mathutil.cpp)
target_include_directories(av1_utility PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(av1_utility PUBLIC av1_core)

add_executable(av1_l14_utility_tests tests/test_harness.cpp tests/test_utility.cpp tests/test_mathutil.cpp)
target_include_directories(av1_l14_utility_tests PRIVATE ${PROJECT_SOURCE_DIR}/third_party/doctest)
target_link_libraries(av1_l14_utility_tests PRIVATE av1_utility)
av1_cuda_test(l14_utility av1_l14_utility_tests)
