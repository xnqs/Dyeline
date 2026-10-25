// Local Contrast: raises (or lowers) contrast between each pixel and its surroundings, leaving
// broad brightness alone, so detail and texture stand out without the image's overall tones
// changing. It compares the pixel's luminance with a blur of it:
//   extract:  e = (Y, Y, Y)
//   combine:  out = c * ((Y + f) / (blur(Y) + f))^amount, the gain held to 1/4..4
// where f, a small floor at the pivot / 64, keeps noise in the blacks from being amplified.
#ifndef DYELINE_KERNELS_TOOLS_LOCAL_CONTRAST_H
#define DYELINE_KERNELS_TOOLS_LOCAL_CONTRAST_H

#include "../dl_kernel.h"

// p[0]: amount, -1 to 1; p[1]: the floor f; p[2..4]: the luminance weights. p[15], used by
// the host only, holds the blur size.
DL_DEVICE dl_float4 dl_tool_local_contrast_extract(dl_float4 c, DL_CONSTANT const float* p) {
    const float y = p[2] * c.x + p[3] * c.y + p[4] * c.z;
    return dl_make_float4(y, y, y, c.w);
}

DL_DEVICE dl_float4 dl_tool_local_contrast_combine(dl_float4 c, DL_CONSTANT const float* p,
                                                   dl_float4 blurred) {
    const float y = p[2] * c.x + p[3] * c.y + p[4] * c.z;
    const float ratio = ((y > 0.0f ? y : 0.0f) + p[1]) /
                        ((blurred.x > 0.0f ? blurred.x : 0.0f) + p[1]);
    float gain = dl_exp2(p[0] * dl_log2(ratio));
    gain = gain < 0.25f ? 0.25f : (gain > 4.0f ? 4.0f : gain);
    return dl_make_float4(c.x * gain, c.y * gain, c.z * gain, c.w);
}

#endif  // DYELINE_KERNELS_TOOLS_LOCAL_CONTRAST_H
