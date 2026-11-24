# dyeline-cube: bakes looks into .cube LUTs from the command line, built into the bundle next
# to the plugin and the designer so it ships with them.

set(cli_output_dir "${DYELINE_BUNDLE_DIR}/Contents/${DYELINE_OFX_ARCH}$<0:>")

# The command itself, apart from main(), so the tests can run it in-process.
add_library(dyeline_cube_cli STATIC cube_cli.cpp)
target_include_directories(dyeline_cube_cli PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dyeline_cube_cli PUBLIC Dyeline::core)
dyeline_set_warnings(dyeline_cube_cli)

add_executable(dyeline_cube main.cpp)
target_link_libraries(dyeline_cube PRIVATE dyeline_cube_cli)
dyeline_set_warnings(dyeline_cube)
set_target_properties(dyeline_cube PROPERTIES
    OUTPUT_NAME "dyeline-cube"
    RUNTIME_OUTPUT_DIRECTORY "${cli_output_dir}")
if(WIN32)
    target_link_libraries(dyeline_cube PRIVATE shell32)  # CommandLineToArgvW
endif()
