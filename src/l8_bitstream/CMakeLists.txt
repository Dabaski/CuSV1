add_library(av1_bitstream STATIC bitstream.cpp packet_helpers.cpp)
target_include_directories(av1_bitstream PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(av1_bitstream PUBLIC av1_entropy)

add_executable(av1_l8_bitstream_tests tests/test_harness.cpp tests/test_bitstream.cpp
               tests/test_packet_helpers.cpp)
target_include_directories(av1_l8_bitstream_tests PRIVATE ${PROJECT_SOURCE_DIR}/third_party/doctest)
target_link_libraries(av1_l8_bitstream_tests PRIVATE av1_bitstream av1_entropy av1_pipeline)
av1_cuda_test(l8_bitstream av1_l8_bitstream_tests)
