add_library(av1_motion STATIC motion.cpp hash_motion.cpp global_motion.cpp amvp.cpp)
target_include_directories(av1_motion PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(av1_motion PUBLIC av1_core av1_gpurt)

add_executable(av1_l5_motion_tests tests/test_harness.cpp tests/test_motion.cpp tests/test_amvp.cpp)
target_include_directories(av1_l5_motion_tests PRIVATE ${PROJECT_SOURCE_DIR}/third_party/doctest)
target_link_libraries(av1_l5_motion_tests PRIVATE av1_motion)
av1_cuda_test(l5_motion av1_l5_motion_tests)