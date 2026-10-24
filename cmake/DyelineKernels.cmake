# Joins the shared kernel headers and a backend's wrapper file into one source string, for
# backends that compile kernels at run time (Metal, OpenCL). Writes:
#   <out_dir>/<name>        the joined kernel source, for build-time checks
#   <out_dir>/<name>.cpp    a C++ file defining `const char* const <symbol>`
# and stores the .cpp path in <out_var>. Quoted #include lines are dropped because the
# headers are inlined in order.
function(dyeline_embed_kernels out_var name symbol wrapper)
    set(inputs
        ${PROJECT_SOURCE_DIR}/core/include/dyeline/kernels/dl_kernel.h
        ${PROJECT_SOURCE_DIR}/core/include/dyeline/kernels/exposure_kernel.h
        ${wrapper}
    )
    set(joined "")
    foreach(input IN LISTS inputs)
        file(READ ${input} text)
        string(REGEX REPLACE "#include \"[^\"]*\"\n" "" text "${text}")
        string(APPEND joined "${text}\n")
    endforeach()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${inputs})

    set(source_file ${CMAKE_CURRENT_BINARY_DIR}/${name})
    set(cpp_file ${CMAKE_CURRENT_BINARY_DIR}/${name}.cpp)
    file(WRITE ${source_file} "${joined}")
    file(WRITE ${cpp_file}
        "// Generated from the shared kernel headers. Do not edit.\n"
        "extern const char* const ${symbol};\n"
        "const char* const ${symbol} = R\"DLKERNEL(\n${joined})DLKERNEL\";\n")
    set(${out_var} ${cpp_file} PARENT_SCOPE)
endfunction()
