add_library(av1_inter STATIC inter_prediction.cpp)
target_include_directories(av1_inter PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(av1_inter PUBLIC av1_core av1_convolve)

add_executable(av1_l17_inter_tests tests/test_harness.cpp tests/test_inter.cpp)
target_include_directories(av1_l17_inter_tests PRIVATE ${PROJECT_SOURCE_DIR}/third_party/doctest)
target_link_libraries(av1_l17_inter_tests PRIVATE av1_inter)
av1_cuda_test(l17_inter av1_l17_inter_tests)
