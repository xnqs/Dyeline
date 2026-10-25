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
    chain_test.cpp
    color_test.cpp
    cube_test.cpp
    look_test.cpp
    render_test.cpp
    tools_test.cpp
)
target_link_libraries(dyeline_core_tests PRIVATE Dyeline::core GTest::gtest_main)
dyeline_set_warnings(dyeline_core_tests)
gtest_discover_tests(dyeline_core_tests)

# The Look Designer round trip, run against the windowless stand-in designer.
add_executable(dyeline_designer_tests designer_link_test.cpp)
target_compile_definitions(dyeline_designer_tests PRIVATE
    DYELINE_DESIGNER_PATH="$<TARGET_FILE:dyeline_designer_stub>")
target_link_libraries(dyeline_designer_tests PRIVATE Dyeline::core GTest::gtest_main)
dyeline_set_warnings(dyeline_designer_tests)
add_dependencies(dyeline_designer_tests dyeline_designer_stub)
gtest_discover_tests(dyeline_designer_tests)

# The Look Designer's editing model.
add_executable(dyeline_look_editor_tests look_editor_test.cpp)
target_link_libraries(dyeline_look_editor_tests PRIVATE dyeline_designer_model GTest::gtest_main)
dyeline_set_warnings(dyeline_look_editor_tests)
gtest_discover_tests(dyeline_look_editor_tests)

# The real designer, drawn for a few frames on SDL's offscreen video driver, so its window
# code runs on machines without a display.
if(DYELINE_BUILD_DESIGNER)
    add_executable(dyeline_designer_app_tests designer_app_test.cpp)
    target_compile_definitions(dyeline_designer_app_tests PRIVATE
        DYELINE_DESIGNER_PATH="$<TARGET_FILE:dyeline_designer>")
    target_link_libraries(dyeline_designer_app_tests PRIVATE Dyeline::core GTest::gtest_main)
    dyeline_set_warnings(dyeline_designer_app_tests)
    add_dependencies(dyeline_designer_app_tests dyeline_designer)
    gtest_discover_tests(dyeline_designer_app_tests
        PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=offscreen;SDL_RENDER_DRIVER=software")
endif()

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
if(DYELINE_ENABLE_OPENCL)
    add_executable(dyeline_opencl_tests gpu_opencl_test.cpp)
    target_include_directories(dyeline_opencl_tests PRIVATE ${PROJECT_SOURCE_DIR}/core/src/gpu/opencl)
    target_link_libraries(dyeline_opencl_tests PRIVATE Dyeline::core OpenCL::Headers GTest::gtest_main)
    dyeline_set_warnings(dyeline_opencl_tests)
    gtest_discover_tests(dyeline_opencl_tests DISCOVERY_MODE PRE_TEST)
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
