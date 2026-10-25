// Grain: film-like noise, applied as a small random change in stops so it scales with the
// image instead of adding a flat haze. It is strongest in the midtones and fades, but never
// vanishes, toward deep shadows and bright highlights, as on film.
//
// The noise is value noise on a lattice Size canonical pixels apart, smoothly interpolated
// and evenly strong, so grain keeps its size and strength at any render scale. Lattice values come from an integer hash of
// the cell, the frame's seed and the channel, which every backend computes bit for bit alike.
#ifndef DYELINE_KERNELS_TOOLS_GRAIN_H
#define DYELINE_KERNELS_TOOLS_GRAIN_H

#include "../dl_kernel.h"

// A well-mixed 32-bit hash (the "lowbias32" finalizer).
DL_DEVICE unsigned int dl_hash(unsigned int x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

// A value in [-1, 1) for lattice point (ix, iy) of one noise layer.
DL_DEVICE float dl_lattice(int ix, int iy, unsigned int layer) {
    const unsigned int h =
        dl_hash((unsigned int)ix ^ dl_hash((unsigned int)iy ^ dl_hash(layer)));
    return (float)(h >> 8) * (2.0f / 16777216.0f) - 1.0f;
}

DL_DEVICE float dl_value_noise(float gx, float gy, unsigned int layer) {
    const float fx0 = dl_floor(gx);
    const float fy0 = dl_floor(gy);
    const int ix = (int)fx0;
    const int iy = (int)fy0;
    float tx = gx - fx0;
    float ty = gy - fy0;
    tx = tx * tx * (3.0f - 2.0f * tx);
    ty = ty * ty * (3.0f - 2.0f * ty);
    const float wa = (1.0f - tx) * (1.0f - ty);
    const float wb = tx * (1.0f - ty);
    const float wc = (1.0f - tx) * ty;
    const float wd = tx * ty;
    const float sum = wa * dl_lattice(ix, iy, layer) + wb * dl_lattice(ix + 1, iy, layer) +
                      wc * dl_lattice(ix, iy + 1, layer) + wd * dl_lattice(ix + 1, iy + 1, layer);
    // Dividing by the weights' length keeps the noise equally strong everywhere: plain
    // interpolation is weaker between lattice points, which shows as a grid in large grain
    // and halves fine grain. The result stays within [-2, 2].
    return sum / dl_sqrt(wa * wa + wb * wb + wc * wc + wd * wd);
}

// p[0]: stops of change at full noise in the midtones; p[1]: 1 / Size; p[2]: color, 0 for
// the same grain in every channel to 1 for separate grain per channel; p[3]: 1 to change the
// grain every frame; p[4]: the look's own seed; p[5..7]: the luminance weights.
DL_DEVICE dl_float4 dl_tool_grain(dl_float4 c, DL_CONSTANT const float* p, struct dl_place at) {
    const float y = p[5] * c.x + p[6] * c.y + p[7] * c.z;
    if (y <= 0.0f) {
        return c;
    }
    const float stops_from_grey = dl_log2(y / 0.18f) * 0.25f;
    const float response = 0.35f + 0.65f * dl_exp2(-stops_from_grey * stops_from_grey);
    const unsigned int seed =
        dl_hash((unsigned int)(int)p[4] ^ (p[3] != 0.0f ? dl_hash((unsigned int)at.seed) : 0U)) * 4U;
    const float gx = at.x * p[1];
    const float gy = at.y * p[1];
    const float mono = dl_value_noise(gx, gy, seed + 3U);
    const float k = p[0] * response;
    const float nr = mono + (dl_value_noise(gx, gy, seed) - mono) * p[2];
    const float ng = mono + (dl_value_noise(gx, gy, seed + 1U) - mono) * p[2];
    const float nb = mono + (dl_value_noise(gx, gy, seed + 2U) - mono) * p[2];
    return dl_make_float4(c.x * dl_exp2(k * nr), c.y * dl_exp2(k * ng), c.z * dl_exp2(k * nb), c.w);
}

#endif  // DYELINE_KERNELS_TOOLS_GRAIN_H
