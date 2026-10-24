# Warning flags for Dyeline's own C++ code (not third-party code, and not CUDA or Metal
# sources, whose compilers take different flags).
function(dyeline_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/permissive->")
    else()
        target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic>")
    endif()
endfunction()
