// Metal backend kernels: thin wrappers around the shared kernel headers. At build time this
// file and the headers it includes are joined into one source string, which the plugin
// compiles with the host's Metal device the first time it renders.

#include "dl_kernel.h"
#include "exposure_kernel.h"

kernel void dl_exposure_kernel(device const float* src [[buffer(0)]],
                               device float* dst [[buffer(1)]],
                               constant dl_exposure_args& args [[buffer(2)]],
                               uint2 gid [[thread_position_in_grid]]) {
    const int x = int(gid.x);
    const int y = int(gid.y);
    if (x < args.width && y < args.height) {
        dl_exposure_at(src, dst, x, y, args);
    }
}
