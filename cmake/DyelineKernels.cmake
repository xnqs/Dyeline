# Joins the shared kernel headers and a backend's wrapper file into one source string, for
# backends that compile kernels at run time (Metal, OpenCL). Writes:
#   <out_dir>/<name>        the joined kernel source, for build-time checks
#   <out_dir>/<name>.cpp    a C++ file defining `const char* const <symbol>`
# and stores the .cpp path in <out_var>. Quoted #include lines are dropped because the
# headers are inlined in order.
# The order is the include order: the portability layer, every tool, then the chain pass.
function(dyeline_embed_kernels out_var name symbol wrapper)
    set(kernels ${PROJECT_SOURCE_DIR}/core/include/dyeline/kernels)
    file(GLOB tool_headers CONFIGURE_DEPENDS ${kernels}/tools/*.h)
    list(SORT tool_headers)
    set(inputs
        ${kernels}/dl_kernel.h
        ${tool_headers}
        ${kernels}/chain_kernel.h
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

    # The source goes in as a byte array rather than a string literal: MSVC caps a string
    # literal at 16 KB (64 KB when concatenated), and the kernels are already past the first.
    # Kernel sources must be ASCII: a byte above 0x7f would not fit a char initializer.
    file(READ ${source_file} hex HEX)
    string(LENGTH "${hex}" hex_length)
    set(bytes "")
    set(offset 0)
    while(offset LESS hex_length)
        string(SUBSTRING "${hex}" ${offset} 64 row)
        string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," row "${row}")
        string(APPEND bytes "    ${row}\n")
        math(EXPR offset "${offset} + 64")
    endwhile()
    file(WRITE ${cpp_file}
        "// Generated from the shared kernel headers. Do not edit.\n"
        "extern const char* const ${symbol};\n"
        "static const char ${symbol}_bytes[] = {\n${bytes}    0x00};\n"
        "const char* const ${symbol} = ${symbol}_bytes;\n")
    set(${out_var} ${cpp_file} PARENT_SCOPE)
endfunction()
