# The Look Designer app. Milestone 0 ships a stub that exercises the plugin round trip; it is
# built next to the plugin binary inside the bundle, where the plugin looks for it.
add_executable(dyeline_designer stub_main.cpp)
target_link_libraries(dyeline_designer PRIVATE Dyeline::core)
dyeline_set_warnings(dyeline_designer)
set_target_properties(dyeline_designer PROPERTIES
    OUTPUT_NAME "dyeline-designer"
    RUNTIME_OUTPUT_DIRECTORY "${DYELINE_BUNDLE_DIR}/Contents/${DYELINE_OFX_ARCH}$<0:>"
)
