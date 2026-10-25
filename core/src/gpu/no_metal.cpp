// Stand-in used when Dyeline is built without the Metal backend.

#include "dyeline/gpu.h"

namespace dyeline::metal {

bool available() {
    return false;
}

std::string render_plan(void*, const void*, void*, const RenderPlan&, const float*, std::size_t,
                        bool) {
    return "Dyeline was built without Metal support";
}

}  // namespace dyeline::metal
