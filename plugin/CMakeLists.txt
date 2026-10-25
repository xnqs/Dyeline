add_library(dyeline_ofx MODULE
    src/dyeline_plugin.cpp
    src/module_path.cpp
)
target_link_libraries(dyeline_ofx PRIVATE Dyeline::core OpenFX::Support)
# The version hosts see, from the project's.
target_compile_definitions(dyeline_ofx PRIVATE
    DYELINE_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    DYELINE_VERSION_MINOR=${PROJECT_VERSION_MINOR})
dyeline_set_warnings(dyeline_ofx)

# OFX bundle layout: Dyeline.ofx.bundle/Contents/<arch>/Dyeline.ofx
if(WIN32)
    set(DYELINE_OFX_ARCH "Win64")
    set(DYELINE_OFX_DEFAULT_INSTALL_DIR "C:/Program Files/Common Files/OFX/Plugins")
elseif(APPLE)
    set(DYELINE_OFX_ARCH "MacOS")
    set(DYELINE_OFX_DEFAULT_INSTALL_DIR "/Library/OFX/Plugins")
else()
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(DYELINE_OFX_ARCH "Linux-arm-64")
    else()
        set(DYELINE_OFX_ARCH "Linux-x86-64")
    endif()
    set(DYELINE_OFX_DEFAULT_INSTALL_DIR "/usr/OFX/Plugins")
endif()

set(DYELINE_OFX_INSTALL_DIR "${DYELINE_OFX_DEFAULT_INSTALL_DIR}" CACHE PATH
    "Folder the OFX bundle is installed into")

set(DYELINE_BUNDLE_DIR "${CMAKE_BINARY_DIR}/Dyeline.ofx.bundle")
set_target_properties(dyeline_ofx PROPERTIES
    OUTPUT_NAME "Dyeline"
    PREFIX ""
    SUFFIX ".ofx"
    # The empty generator expression stops multi-config generators adding a per-config subfolder.
    LIBRARY_OUTPUT_DIRECTORY "${DYELINE_BUNDLE_DIR}/Contents/${DYELINE_OFX_ARCH}$<0:>"
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

# Export only the two OFX entry points, so the plugin's own symbols never clash with the host's.
if(APPLE)
    configure_file(Info.plist.in "${DYELINE_BUNDLE_DIR}/Contents/Info.plist" @ONLY)
    target_link_options(dyeline_ofx PRIVATE
        "LINKER:-exported_symbols_list,${CMAKE_CURRENT_SOURCE_DIR}/exports.macos")
elseif(UNIX)
    target_link_options(dyeline_ofx PRIVATE
        "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/exports.linux")
endif()

# The bundled looks, in Contents/Resources/looks, where the designer's browser finds them.
# Copied at build time, so a build folder's bundle works without an install.
file(GLOB_RECURSE dyeline_bundled_looks CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/looks/*.dyeline")
set(dyeline_looks_stamp "${CMAKE_CURRENT_BINARY_DIR}/looks.stamp")
add_custom_command(
    OUTPUT "${dyeline_looks_stamp}"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${DYELINE_BUNDLE_DIR}/Contents/Resources/looks"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/looks"
            "${DYELINE_BUNDLE_DIR}/Contents/Resources/looks"
    COMMAND ${CMAKE_COMMAND} -E touch "${dyeline_looks_stamp}"
    DEPENDS ${dyeline_bundled_looks}
    COMMENT "Copying the bundled looks"
)
add_custom_target(dyeline_looks ALL DEPENDS "${dyeline_looks_stamp}")
add_dependencies(dyeline_ofx dyeline_looks)

install(DIRECTORY "${DYELINE_BUNDLE_DIR}" DESTINATION "${DYELINE_OFX_INSTALL_DIR}"
    USE_SOURCE_PERMISSIONS)

# The designer app lives in the bundle next to the plugin binary, so it is configured here,
# where the bundle layout is known.
add_subdirectory(${PROJECT_SOURCE_DIR}/designer ${CMAKE_BINARY_DIR}/designer)
add_dependencies(dyeline_ofx dyeline_designer)
