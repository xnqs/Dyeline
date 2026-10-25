# The Look Designer app, built next to the plugin binary inside the bundle, where the plugin
# looks for it. It is a Dear ImGui window on SDL3. DYELINE_BUILD_DESIGNER=OFF builds the
# windowless stand-in in its place, for machines without a desktop; the tests always use the
# stand-in, since it needs no window.

option(DYELINE_BUILD_DESIGNER "Build the Look Designer app (needs SDL3 and Dear ImGui, fetched)" ON)

set(designer_output_dir "${DYELINE_BUNDLE_DIR}/Contents/${DYELINE_OFX_ARCH}$<0:>")

# The editing model, shared by the app and its tests.
add_library(dyeline_designer_model STATIC src/look_editor.cpp src/preview.cpp src/scopes.cpp)
target_include_directories(dyeline_designer_model PUBLIC src)
target_link_libraries(dyeline_designer_model PUBLIC Dyeline::core)
dyeline_set_warnings(dyeline_designer_model)

# The stand-in: no window, and a scripted edit (see stub_main.cpp).
add_executable(dyeline_designer_stub stub_main.cpp)
target_link_libraries(dyeline_designer_stub PRIVATE Dyeline::core)
dyeline_set_warnings(dyeline_designer_stub)

if(NOT DYELINE_BUILD_DESIGNER)
    set_target_properties(dyeline_designer_stub PROPERTIES
        OUTPUT_NAME "dyeline-designer"
        RUNTIME_OUTPUT_DIRECTORY "${designer_output_dir}")
    add_custom_target(dyeline_designer)
    add_dependencies(dyeline_designer dyeline_designer_stub)
    return()
endif()

include(FetchContent)

# SDL3 (zlib license), linked statically so the bundle has nothing else to ship. Only the
# window, input and 2D rendering parts are needed.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_AUDIO OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_HAPTIC OFF CACHE BOOL "" FORCE)
set(SDL_HIDAPI OFF CACHE BOOL "" FORCE)
set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
set(SDL_POWER OFF CACHE BOOL "" FORCE)
if(APPLE)
    # macOS renders through Metal and needs no GLES. Without it, SDL's offscreen video driver
    # (used by the headless tests) no longer insists on loading an EGL library to make a window.
    set(SDL_OPENGLES OFF CACHE BOOL "" FORCE)
endif()
FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.16
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(SDL3)

# Dear ImGui (MIT), with its SDL3 platform and SDL_Renderer backends.
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.9b
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui)
add_library(dyeline_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
)
target_include_directories(dyeline_imgui SYSTEM PUBLIC
    ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${imgui_SOURCE_DIR}/misc/cpp)
target_compile_definitions(dyeline_imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
target_link_libraries(dyeline_imgui PUBLIC SDL3::SDL3-static)

add_executable(dyeline_designer src/main.cpp src/app.cpp)
target_link_libraries(dyeline_designer PRIVATE dyeline_designer_model dyeline_imgui)
dyeline_set_warnings(dyeline_designer)
if(WIN32)
    # A windowed app: no console window when the plugin starts it.
    set_target_properties(dyeline_designer PROPERTIES WIN32_EXECUTABLE ON)
endif()
set_target_properties(dyeline_designer PROPERTIES
    OUTPUT_NAME "dyeline-designer"
    RUNTIME_OUTPUT_DIRECTORY "${designer_output_dir}")
