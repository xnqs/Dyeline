// OpenCL backend kernels: thin wrappers around the shared kernel headers. At build time this
// file and the headers it includes are joined into one source string, which the plugin
// builds for the host's OpenCL device the first time it renders.

#include "dl_kernel.h"
#include "chain_kernel.h"

__kernel void dl_chain_kernel(__global const float* src, __global float* dst,
                              struct dl_image_args args, __constant struct dl_chain* chain,
                              __global const float* table) {
    const int x = (int)get_global_id(0);
    const int y = (int)get_global_id(1);
    if (x < args.width && y < args.height) {
        dl_chain_at(src, dst, x, y, args, chain, table);
    }
}
