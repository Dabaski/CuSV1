add_library(av1_palette STATIC palette.cpp)
target_include_directories(av1_palette PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(av1_palette PUBLIC av1_core)

add_executable(av1_l13_palette_tests tests/test_harness.cpp tests/test_palette.cpp)
target_include_directories(av1_l13_palette_tests PRIVATE ${PROJECT_SOURCE_DIR}/third_party/doctest)
target_link_libraries(av1_l13_palette_tests PRIVATE av1_palette)
av1_cuda_test(l13_palette av1_l13_palette_tests)
