// Stand-in used when Dyeline is built without the OpenCL backend.

#include "dyeline/gpu.h"

namespace dyeline::opencl {

bool available() {
    return false;
}

std::string render_chain(void*, const void*, void*, const dl_image_args&, const dl_chain&, bool) {
    return "Dyeline was built without OpenCL support";
}

}  // namespace dyeline::opencl
