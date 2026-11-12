add_library(av1_rc STATIC rc_tables.cpp rc_arith.cpp)
target_include_directories(av1_rc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(av1_rc PUBLIC av1_core)

add_executable(av1_l15_rc_tests tests/test_harness.cpp tests/test_rc.cpp tests/test_rc_arith.cpp tests/test_rc_q.cpp
                               tests/test_rc_aq.cpp tests/test_rc_alloc.cpp tests/test_rc_rtc.cpp tests/test_rc_driver.cpp
                               tests/test_rc_recode.cpp)
target_include_directories(av1_l15_rc_tests PRIVATE ${PROJECT_SOURCE_DIR}/third_party/doctest)
target_compile_definitions(av1_l15_rc_tests PRIVATE REPO_ROOT="${CMAKE_SOURCE_DIR}")
target_link_libraries(av1_l15_rc_tests PRIVATE av1_rc)
av1_cuda_test(l15_rc av1_l15_rc_tests)
