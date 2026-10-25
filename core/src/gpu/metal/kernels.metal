// Metal backend kernels: thin wrappers around the shared kernel headers. At build time this
// file and the headers it includes are joined into one source string, which the plugin
// compiles with the host's Metal device the first time it renders.

#include "dl_kernel.h"
#include "chain_kernel.h"

kernel void dl_chain_kernel(device const float* src [[buffer(0)]],
                            device float* dst [[buffer(1)]],
                            constant dl_image_args& args [[buffer(2)]],
                            constant dl_chain& chain [[buffer(3)]],
                            uint2 gid [[thread_position_in_grid]]) {
    const int x = int(gid.x);
    const int y = int(gid.y);
    if (x < args.width && y < args.height) {
        dl_chain_at(src, dst, x, y, args, &chain);
    }
}
