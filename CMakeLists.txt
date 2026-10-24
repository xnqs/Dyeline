cmake_minimum_required(VERSION 3.24)

project(Dyeline VERSION 0.1.0 LANGUAGES CXX)

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
message(STATUS "Dyeline backends: CPU, CUDA=${DYELINE_ENABLE_CUDA}, Metal=${DYELINE_ENABLE_METAL}")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(DyelineWarnings)
include(OpenFX)

add_subdirectory(core)
add_subdirectory(plugin)

if(DYELINE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
