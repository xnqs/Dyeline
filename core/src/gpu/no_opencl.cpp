// Stand-in used when Dyeline is built without the OpenCL backend.

#include "dyeline/gpu.h"

namespace dyeline::opencl {

bool available() {
    return false;
}

std::string render_plan(void*, const void*, void*, const RenderPlan&, const float*, std::size_t,
                        bool) {
    return "Dyeline was built without OpenCL support";
}

}  // namespace dyeline::opencl
