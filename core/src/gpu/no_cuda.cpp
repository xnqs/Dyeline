// Stand-in used when Dyeline is built without the CUDA backend.

#include "dyeline/gpu.h"

namespace dyeline::cuda {

bool available() {
    return false;
}

std::string render_chain(void*, const float*, float*, const dl_image_args&, const dl_chain&, bool) {
    return "Dyeline was built without CUDA support";
}

}  // namespace dyeline::cuda
