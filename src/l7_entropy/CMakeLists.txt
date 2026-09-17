add_library(av1_entropy STATIC entropy.cpp)
target_include_directories(av1_entropy PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(av1_l7_entropy_tests tests/test_harness.cpp tests/test_entropy.cpp)
target_include_directories(av1_l7_entropy_tests PRIVATE ${PROJECT_SOURCE_DIR}/third_party/doctest)
target_link_libraries(av1_l7_entropy_tests PRIVATE av1_entropy av1_pixels av1_transforms)
av1_cuda_test(l7_entropy av1_l7_entropy_tests)