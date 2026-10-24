add_library(dyeline_core STATIC
    src/exposure.cpp
)
add_library(Dyeline::core ALIAS dyeline_core)
target_include_directories(dyeline_core PUBLIC include)
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
    set(metal_inputs
        ${CMAKE_CURRENT_SOURCE_DIR}/include/dyeline/kernels/dl_kernel.h
        ${CMAKE_CURRENT_SOURCE_DIR}/include/dyeline/kernels/exposure_kernel.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/metal/kernels.metal
    )
    set(metal_source "")
    foreach(input IN LISTS metal_inputs)
        file(READ ${input} text)
        # Our own headers are inlined in order, so drop the quoted includes between them.
        string(REGEX REPLACE "#include \"[^\"]*\"\n" "" text "${text}")
        string(APPEND metal_source "${text}\n")
    endforeach()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${metal_inputs})

    set(metal_file ${CMAKE_CURRENT_BINARY_DIR}/dyeline_kernels.metal)
    file(WRITE ${metal_file} "${metal_source}")
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/metal_source.cpp
        "// Generated from the shared kernel headers. Do not edit.\n"
        "extern const char* const kDyelineMetalSource;\n"
        "const char* const kDyelineMetalSource = R\"DLMETAL(\n${metal_source})DLMETAL\";\n")

    target_sources(dyeline_core PRIVATE
        src/gpu/metal/metal_backend.mm
        ${CMAKE_CURRENT_BINARY_DIR}/metal_source.cpp
    )
    set_source_files_properties(src/gpu/metal/metal_backend.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(dyeline_core PUBLIC "-framework Metal" "-framework Foundation")

    # Compile the joined source once at build time too, so a kernel error fails the build
    # instead of the first render. Skipped when the Metal compiler is not installed.
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
