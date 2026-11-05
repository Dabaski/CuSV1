add_library(av1_resize STATIC resize.cpp)
target_include_directories(av1_resize PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(av1_resize PUBLIC av1_core)

add_executable(av1_l11_resize_tests tests/test_harness.cpp tests/test_resize.cpp)
target_include_directories(av1_l11_resize_tests PRIVATE ${PROJECT_SOURCE_DIR}/third_party/doctest)
target_link_libraries(av1_l11_resize_tests PRIVATE av1_resize)
av1_cuda_test(l11_resize av1_l11_resize_tests)
