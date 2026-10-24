// Stand-in used when Dyeline is built without the Metal backend.

#include "dyeline/gpu.h"

namespace dyeline::metal {

bool available() {
    return false;
}

std::string exposure(void*, const void*, void*, const dl_exposure_args&, bool) {
    return "Dyeline was built without Metal support";
}

}  // namespace dyeline::metal
