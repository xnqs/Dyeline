add_library(dyeline_core STATIC
    src/chain.cpp
    src/color.cpp
    src/cube.cpp
    src/designer_link.cpp
    src/frame_file.cpp
    src/look.cpp
    src/paths.cpp
    src/render.cpp
    src/tools.cpp
)
add_library(Dyeline::core ALIAS dyeline_core)
target_include_directories(dyeline_core PUBLIC include)
target_link_libraries(dyeline_core PUBLIC nlohmann_json::nlohmann_json)
dyeline_set_warnings(dyeline_core)

# CUDA backend: kernels compiled by nvcc, linked against the static CUDA runtime so the
# plugin ships no extra libraries.
if(DYELINE_ENABLE_CUDA)
    target_sources(dyeline_core PRIVATE src/gpu/cuda/cuda_backend.cu)
    target_link_libraries(dyeline_core PRIVATE CUDA::cudart_static)
else()
    target_sources(dyeline_core PRIVATE src/gpu/no_cuda.cpp)
endif()

# Metal backend: the shared kernel headers and kernels.metal are joined into one source
# string, compiled at run time for the host's GPU.
if(DYELINE_ENABLE_METAL)
    dyeline_embed_kernels(metal_source_cpp dyeline_kernels.metal kDyelineMetalSource
        ${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/metal/kernels.metal)
    target_sources(dyeline_core PRIVATE src/gpu/metal/metal_backend.mm ${metal_source_cpp})
    set_source_files_properties(src/gpu/metal/metal_backend.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(dyeline_core PUBLIC "-framework Metal" "-framework Foundation")

    # Compile the joined source once at build time too, so a kernel error fails the build
    # instead of the first render. Skipped when the Metal compiler is not installed.
    set(metal_file ${CMAKE_CURRENT_BINARY_DIR}/dyeline_kernels.metal)
    execute_process(COMMAND xcrun -sdk macosx -f metal
        OUTPUT_VARIABLE metal_compiler OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE metal_found ERROR_QUIET)
    if(metal_found EQUAL 0)
        add_custom_command(
            OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/dyeline_kernels.air
            COMMAND xcrun -sdk macosx metal -c ${metal_file} -o ${CMAKE_CURRENT_BINARY_DIR}/dyeline_kernels.air
            DEPENDS ${metal_file}
            COMMENT "Checking Metal kernels compile"
        )
        add_custom_target(dyeline_metal_check ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/dyeline_kernels.air)
    else()
        message(STATUS "Metal compiler not found; Metal kernels are only checked at run time")
    endif()
else()
    target_sources(dyeline_core PRIVATE src/gpu/no_metal.cpp)
endif()

# OpenCL backend: the shared kernel headers and kernels.cl are joined into one source string,
# built at run time for the host's device. Only the Khronos headers are needed to build; the
# OpenCL library is loaded at run time.
if(DYELINE_ENABLE_OPENCL)
    dyeline_embed_kernels(opencl_source_cpp dyeline_kernels.cl kDyelineOpenCLSource
        ${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/opencl/kernels.cl)
    target_sources(dyeline_core PRIVATE src/gpu/opencl/opencl_backend.cpp ${opencl_source_cpp})
    target_link_libraries(dyeline_core PRIVATE OpenCL::Headers ${CMAKE_DL_LIBS})
else()
    target_sources(dyeline_core PRIVATE src/gpu/no_opencl.cpp)
endif()
