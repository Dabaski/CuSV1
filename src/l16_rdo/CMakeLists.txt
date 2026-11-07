add_library(av1_rdo STATIC rate_est.cpp)
target_include_directories(av1_rdo PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(av1_rdo PUBLIC av1_core)

add_executable(av1_l16_rdo_tests tests/test_harness.cpp tests/test_rate_est.cpp)
target_include_directories(av1_l16_rdo_tests PRIVATE ${PROJECT_SOURCE_DIR}/third_party/doctest)
target_link_libraries(av1_l16_rdo_tests PRIVATE av1_rdo)
av1_cuda_test(l16_rdo av1_l16_rdo_tests)
