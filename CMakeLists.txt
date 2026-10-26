cmake_minimum_required(VERSION 3.24)

project(Dyeline VERSION 0.6.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(DYELINE_BUILD_TESTS "Build the unit tests" ON)

# GPU backends. CUDA is built when a CUDA compiler is found (Windows and Linux); Metal is
# always built on macOS.
include(CheckLanguage)
if(APPLE)
    set(DYELINE_ENABLE_CUDA OFF CACHE BOOL "Build the CUDA backend" FORCE)
else()
    check_language(CUDA)
    if(CMAKE_CUDA_COMPILER)
        set(cuda_default ON)
    else()
        set(cuda_default OFF)
    endif()
    option(DYELINE_ENABLE_CUDA "Build the CUDA backend" ${cuda_default})
endif()
option(DYELINE_ENABLE_METAL "Build the Metal backend" ${APPLE})
# OpenCL needs no SDK (only headers, fetched below), so it is on wherever hosts use it.
if(APPLE)
    set(opencl_default OFF)
else()
    set(opencl_default ON)
endif()
option(DYELINE_ENABLE_OPENCL "Build the OpenCL backend" ${opencl_default})

if(DYELINE_ENABLE_CUDA)
    # One binary per major GPU generation the toolkit supports; override on the command line.
    if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
        set(CMAKE_CUDA_ARCHITECTURES all-major)
    endif()
    enable_language(CUDA)
    find_package(CUDAToolkit REQUIRED)
    set(CMAKE_CUDA_STANDARD 17)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
endif()
if(DYELINE_ENABLE_METAL)
    enable_language(OBJCXX)
endif()
message(STATUS "Dyeline backends: CPU, CUDA=${DYELINE_ENABLE_CUDA}, Metal=${DYELINE_ENABLE_METAL}, OpenCL=${DYELINE_ENABLE_OPENCL}")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(DyelineWarnings)
include(OpenFX)
include(DyelineKernels)

# JSON for .dyeline looks (header-only, MIT).
include(FetchContent)
FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
)
set(JSON_Install OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json)

if(DYELINE_ENABLE_OPENCL)
    FetchContent_Declare(opencl_headers
        GIT_REPOSITORY https://github.com/KhronosGroup/OpenCL-Headers.git
        GIT_TAG v2025.07.22
        GIT_SHALLOW TRUE
        SOURCE_SUBDIR dyeline-no-cmake
    )
    FetchContent_MakeAvailable(opencl_headers)
    add_library(OpenCL::Headers INTERFACE IMPORTED)
    target_include_directories(OpenCL::Headers SYSTEM INTERFACE ${opencl_headers_SOURCE_DIR})
    target_compile_definitions(OpenCL::Headers INTERFACE CL_TARGET_OPENCL_VERSION=120)
endif()

add_subdirectory(core)
add_subdirectory(plugin)
add_subdirectory(bench)

if(DYELINE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
