// Stand-in used when Dyeline is built without the OpenCL backend.

#include "dyeline/gpu.h"

namespace dyeline::opencl {

bool available() {
    return false;
}

std::string exposure(void*, const void*, void*, const dl_exposure_args&, bool) {
    return "Dyeline was built without OpenCL support";
}

}  // namespace dyeline::opencl
