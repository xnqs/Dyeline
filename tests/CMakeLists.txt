include(FetchContent)
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
    GIT_SHALLOW TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
include(GoogleTest)

add_executable(dyeline_core_tests
    exposure_test.cpp
)
target_link_libraries(dyeline_core_tests PRIVATE Dyeline::core GTest::gtest_main)
dyeline_set_warnings(dyeline_core_tests)
gtest_discover_tests(dyeline_core_tests)

# GPU backends against the CPU reference. These skip themselves on machines without a GPU.
if(DYELINE_ENABLE_CUDA)
    add_executable(dyeline_cuda_tests gpu_cuda_test.cpp)
    target_link_libraries(dyeline_cuda_tests PRIVATE Dyeline::core CUDA::cudart_static GTest::gtest_main)
    dyeline_set_warnings(dyeline_cuda_tests)
    gtest_discover_tests(dyeline_cuda_tests DISCOVERY_MODE PRE_TEST)
endif()
if(DYELINE_ENABLE_METAL)
    add_executable(dyeline_metal_tests gpu_metal_test.mm)
    target_link_libraries(dyeline_metal_tests PRIVATE Dyeline::core GTest::gtest_main)
    target_compile_options(dyeline_metal_tests PRIVATE -fobjc-arc)
    gtest_discover_tests(dyeline_metal_tests DISCOVERY_MODE PRE_TEST)
endif()

# Loads the built plugin binary the way a host does and checks its OFX entry points.
add_executable(dyeline_plugin_load_test
    plugin_load_test.cpp
)
target_include_directories(dyeline_plugin_load_test SYSTEM PRIVATE ${openfx_SOURCE_DIR}/include)
target_compile_definitions(dyeline_plugin_load_test PRIVATE
    DYELINE_PLUGIN_PATH="$<TARGET_FILE:dyeline_ofx>")
target_link_libraries(dyeline_plugin_load_test PRIVATE GTest::gtest_main ${CMAKE_DL_LIBS})
dyeline_set_warnings(dyeline_plugin_load_test)
add_dependencies(dyeline_plugin_load_test dyeline_ofx)
gtest_discover_tests(dyeline_plugin_load_test)
