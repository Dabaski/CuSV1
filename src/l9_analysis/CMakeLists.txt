add_library(av1_analysis STATIC analysis.cpp fft1d.cpp fft2d.cpp noise_model.cpp
               noise_util.cpp temporal_filter.cpp packunpack.cpp cdefault_extra.cpp)
target_include_directories(av1_analysis PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(av1_analysis PUBLIC av1_core)

add_executable(av1_l9_analysis_tests tests/test_harness.cpp tests/test_variance.cpp
               tests/test_pad.cpp tests/test_downsample_filtering.cpp
               tests/test_fft1d.cpp tests/test_fft2d.cpp
               tests/test_noise_strength.cpp tests/test_noise_model.cpp
               tests/test_noise_util.cpp tests/test_temporal.cpp
               tests/test_packunpack.cpp)
target_include_directories(av1_l9_analysis_tests PRIVATE ${PROJECT_SOURCE_DIR}/third_party/doctest)
target_link_libraries(av1_l9_analysis_tests PRIVATE av1_analysis)
av1_cuda_test(l9_analysis av1_l9_analysis_tests)
