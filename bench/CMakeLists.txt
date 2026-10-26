# dyeline_bench: render timings for each tool and bundled look (see bench.cpp).
add_executable(dyeline_bench bench.cpp)
target_compile_definitions(dyeline_bench PRIVATE DYELINE_LOOKS_DIR="${PROJECT_SOURCE_DIR}/looks")
target_link_libraries(dyeline_bench PRIVATE Dyeline::core)
dyeline_set_warnings(dyeline_bench)
