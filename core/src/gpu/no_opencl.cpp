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

std::string own_device_name(std::string& error) {
    error = "Dyeline was built without OpenCL support";
    return {};
}

std::string render_plan_own_device(const float*, std::size_t, float*, std::size_t,
                                   const RenderPlan&, const float*, std::size_t) {
    return "Dyeline was built without OpenCL support";
}

}  // namespace dyeline::opencl
