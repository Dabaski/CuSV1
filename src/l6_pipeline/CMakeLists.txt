add_library(av1_pipeline STATIC pipeline.cpp picops.cpp src_ops.cpp coding_loop_helpers.cpp pd_helpers.cpp md_core.cpp)
target_include_directories(av1_pipeline PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# av1_rc joins under the LR3 court-order precedent (one line): the A6C
# lambda chain consumes l15's dcQuantQtx/acQuantQtx registry. av1_utility
# joins for the same reason: setup_mds reads the l14 geom table's d1/ns
# depth offsets (get_blk_geom_mds, the scs->blk_geom_mds surface).
target_link_libraries(av1_pipeline PUBLIC av1_pixels av1_transforms av1_intra av1_motion av1_entropy av1_rdo av1_analysis av1_rc av1_utility)

add_executable(av1_l6_pipeline_tests tests/test_harness.cpp tests/test_pipeline.cpp
                                 tests/test_fs3_emission.cpp tests/test_picops.cpp
                                 tests/test_src_ops.cpp tests/test_coding_loop_helpers.cpp
                                 tests/test_pd_helpers.cpp tests/test_md_core.cpp)
target_include_directories(av1_l6_pipeline_tests PRIVATE ${PROJECT_SOURCE_DIR}/third_party/doctest)
target_compile_definitions(av1_l6_pipeline_tests PRIVATE REPO_ROOT="${CMAKE_SOURCE_DIR}")
target_link_libraries(av1_l6_pipeline_tests PRIVATE av1_pipeline)
av1_cuda_test(pipeline av1_l6_pipeline_tests)
