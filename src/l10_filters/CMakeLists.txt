add_library(av1_filters STATIC filters.cpp cdef.cpp restoration.cpp grain.cpp deblock_driver.cpp deblock_hbd.cpp)
target_include_directories(av1_filters PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(av1_filters PUBLIC av1_core av1_convolve)

add_executable(av1_l10_filters_tests tests/test_harness.cpp tests/test_filters.cpp tests/test_cdef.cpp
                                     tests/test_cdef2.cpp tests/test_restoration.cpp tests/test_cdef3.cpp
                                     tests/test_grain.cpp tests/test_deblock_driver.cpp
                                     tests/test_deblock_hbd.cpp tests/test_deblock_walls.cpp)
target_include_directories(av1_l10_filters_tests PRIVATE ${PROJECT_SOURCE_DIR}/third_party/doctest)
target_link_libraries(av1_l10_filters_tests PRIVATE av1_filters)
av1_cuda_test(l10_filters av1_l10_filters_tests)