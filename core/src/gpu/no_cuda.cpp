// Stand-in used when Dyeline is built without the CUDA backend.

#include "dyeline/gpu.h"

namespace dyeline::cuda {

bool available() {
    return false;
}

std::string render_plan(void*, const float*, float*, const RenderPlan&, const float*, std::size_t,
                        bool) {
    return "Dyeline was built without CUDA support";
}

}  // namespace dyeline::cuda
