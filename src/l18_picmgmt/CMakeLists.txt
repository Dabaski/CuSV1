add_library(av1_picmgmt STATIC picmgmt.cpp)
target_include_directories(av1_picmgmt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(av1_picmgmt PUBLIC av1_core)

add_executable(av1_l18_picmgmt_tests tests/test_harness.cpp tests/test_picmgmt.cpp)
target_include_directories(av1_l18_picmgmt_tests PRIVATE ${PROJECT_SOURCE_DIR}/third_party/doctest)
target_compile_definitions(av1_l18_picmgmt_tests PRIVATE REPO_ROOT="${CMAKE_SOURCE_DIR}")
target_link_libraries(av1_l18_picmgmt_tests PRIVATE av1_picmgmt)
av1_cuda_test(l18_picmgmt av1_l18_picmgmt_tests)
