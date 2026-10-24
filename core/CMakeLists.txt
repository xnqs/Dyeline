add_library(dyeline_core STATIC
    src/exposure.cpp
)
add_library(Dyeline::core ALIAS dyeline_core)
target_include_directories(dyeline_core PUBLIC include)
dyeline_set_warnings(dyeline_core)
