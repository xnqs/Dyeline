// Stand-in used when Dyeline is built without the CUDA backend.

#include "dyeline/gpu.h"

namespace dyeline::cuda {

bool available() {
    return false;
}

std::string exposure(void*, const float*, float*, const dl_exposure_args&, bool) {
    return "Dyeline was built without CUDA support";
}

}  // namespace dyeline::cuda
