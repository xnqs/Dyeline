// Stand-in used when Dyeline is built without the Metal backend.

#include "dyeline/gpu.h"

namespace dyeline::metal {

bool available() {
    return false;
}

std::string render_chain(void*, const void*, void*, const dl_image_args&, const dl_chain&, bool) {
    return "Dyeline was built without Metal support";
}

}  // namespace dyeline::metal
