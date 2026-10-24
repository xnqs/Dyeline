# Fetches the OpenFX SDK and builds its C++ support library as the static target
# OpenFX::Support. Only the headers and the support library are used; the SDK's own
# CMake project (examples, conan dependencies) is not configured.
include(FetchContent)

set(DYELINE_OPENFX_TAG "OFX_Release_1.5.1" CACHE STRING "OpenFX SDK git tag")

FetchContent_Declare(openfx
    GIT_REPOSITORY https://github.com/AcademySoftwareFoundation/openfx.git
    GIT_TAG ${DYELINE_OPENFX_TAG}
    GIT_SHALLOW TRUE
    # Points at a folder with no CMakeLists.txt so MakeAvailable only downloads the SDK.
    SOURCE_SUBDIR dyeline-no-cmake
)
FetchContent_MakeAvailable(openfx)

set(OPENFX_SOURCES
    ${openfx_SOURCE_DIR}/Support/Library/ofxsCore.cpp
    ${openfx_SOURCE_DIR}/Support/Library/ofxsImageEffect.cpp
    ${openfx_SOURCE_DIR}/Support/Library/ofxsInteract.cpp
    ${openfx_SOURCE_DIR}/Support/Library/ofxsLog.cpp
    ${openfx_SOURCE_DIR}/Support/Library/ofxsMultiThread.cpp
    ${openfx_SOURCE_DIR}/Support/Library/ofxsParams.cpp
    ${openfx_SOURCE_DIR}/Support/Library/ofxsProperty.cpp
    ${openfx_SOURCE_DIR}/Support/Library/ofxsPropertyValidation.cpp
)

add_library(OpenFXSupport STATIC ${OPENFX_SOURCES})
add_library(OpenFX::Support ALIAS OpenFXSupport)
target_include_directories(OpenFXSupport SYSTEM PUBLIC
    ${openfx_SOURCE_DIR}/include
    ${openfx_SOURCE_DIR}/Support/include
)
if(WIN32)
    target_compile_definitions(OpenFXSupport PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
if(MSVC)
    target_compile_options(OpenFXSupport PRIVATE /W0)
else()
    target_compile_options(OpenFXSupport PRIVATE -w)
endif()
