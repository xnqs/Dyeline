// Color management: the transforms at the ends of every chain. Tools work in scene-linear
// light in the ACEScg working space; the input transform brings the footage there and the
// output transform takes the result to the output space (see core/src/color.cpp).
//
// The camera curves are the vendors' published formulas, with their constants as published.
#ifndef DYELINE_KERNELS_TOOLS_COLOR_SPACE_H
#define DYELINE_KERNELS_TOOLS_COLOR_SPACE_H

#include "../dl_kernel.h"

// Transfer curves. DL_CURVE_LINEAR leaves values alone.
#define DL_CURVE_LINEAR 0
#define DL_CURVE_SRGB 1          // IEC 61966-2-1 piecewise sRGB
#define DL_CURVE_GAMMA_2_4 2     // Rec.709 video as displayed (BT.1886, pure 2.4 power)
#define DL_CURVE_ACESCCT 3       // ACES S-2016-001
#define DL_CURVE_LOGC3 4         // ARRI LogC3, EI 800
#define DL_CURVE_LOGC4 5         // ARRI LogC4
#define DL_CURVE_SLOG3 6         // Sony S-Log3
#define DL_CURVE_VLOG 7          // Panasonic V-Log
#define DL_CURVE_CLOG3 8         // Canon Log 3
#define DL_CURVE_LOG3G10 9       // RED Log3G10
#define DL_CURVE_DAVINCI_INTERMEDIATE 10

// One end of the chain: decode a curve to linear, apply a 3x3 matrix, then encode a curve.
// The input transform decodes and converts to the working space; the output transform
// converts from it and encodes. `matrix` is 0 when there is no matrix to apply.
struct dl_transform {
    int decode;
    int encode;
    int matrix;
    int unused;
    float m[12];  // row by row; the last 3 are padding
};

DL_DEVICE float dl_log10(float x) {
    return dl_log2(x) * 0.30102999566f;
}

DL_DEVICE float dl_exp10(float x) {
    return dl_exp2(x * 3.32192809489f);
}

// A power curve that mirrors negative values, so they survive a round trip.
DL_DEVICE float dl_signed_pow(float x, float e) {
    return x < 0.0f ? -dl_pow(-x, e) : dl_pow(x, e);
}

// LogC4's constants, from ARRI's "LogC4 Logarithmic Color Space" specification.
#define DL_LOGC4_A 2231.82630906f  // (2^18 - 16) / 117.45
#define DL_LOGC4_B 0.90713587488f  // (1023 - 95) / 1023
#define DL_LOGC4_C 0.09286412512f  // 95 / 1023
#define DL_LOGC4_S 0.11359720861f  // 7 ln(2) 2^(7 - 14 c / b) / (a b)
#define DL_LOGC4_T -0.01805699611f // (2^(14 (-c / b) + 6) - 64) / a

// Curve code value to linear light.
DL_DEVICE float dl_decode_curve(int curve, float x) {
    switch (curve) {
        case DL_CURVE_SRGB: {
            const float a = dl_fabs(x);
            const float y = a <= 0.04045f ? a / 12.92f : dl_pow((a + 0.055f) / 1.055f, 2.4f);
            return x < 0.0f ? -y : y;
        }
        case DL_CURVE_GAMMA_2_4:
            return dl_signed_pow(x, 2.4f);
        case DL_CURVE_ACESCCT:
            return x <= 0.155251141552511f ? (x - 0.0729055341958355f) / 10.5402377416545f
                                            : dl_exp2(x * 17.52f - 9.72f);
        case DL_CURVE_LOGC3:
            return x > 0.149658f ? (dl_exp10((x - 0.385537f) / 0.247190f) - 0.052272f) / 5.555556f
                                 : (x - 0.092809f) / 5.367655f;
        case DL_CURVE_LOGC4:
            return x >= 0.0f
                       ? (dl_exp2(14.0f * (x - DL_LOGC4_C) / DL_LOGC4_B + 6.0f) - 64.0f) / DL_LOGC4_A
                       : x * DL_LOGC4_S + DL_LOGC4_T;
        case DL_CURVE_SLOG3:
            return x >= 171.2102946929f / 1023.0f
                       ? dl_exp10((x * 1023.0f - 420.0f) / 261.5f) * 0.19f - 0.01f
                       : (x * 1023.0f - 95.0f) * 0.01125f / (171.2102946929f - 95.0f);
        case DL_CURVE_VLOG:
            return x < 0.181f ? (x - 0.125f) / 5.6f : dl_exp10((x - 0.598206f) / 0.241514f) - 0.00873f;
        case DL_CURVE_CLOG3: {
            float y;
            if (x < 0.097465473f) {
                y = -(dl_exp10((0.12783901f - x) / 0.36726845f) - 1.0f) / 14.98325f;
            } else if (x <= 0.15277891f) {
                y = (x - 0.12512219f) / 1.9754798f;
            } else {
                y = (dl_exp10((x - 0.12240537f) / 0.36726845f) - 1.0f) / 14.98325f;
            }
            return y * 0.9f;  // Canon's scale: 18% grey sits at code 0.343
        }
        case DL_CURVE_LOG3G10:
            return x < 0.0f ? x / 15.1927f - 0.01f : (dl_exp10(x / 0.224282f) - 1.0f) / 155.975327f - 0.01f;
        case DL_CURVE_DAVINCI_INTERMEDIATE:
            return x <= 0.02740668f ? x / 10.44426855f : dl_exp2(x / 0.07329248f - 7.0f) - 0.0075f;
        default:
            return x;
    }
}

// Linear light to the curve's code value; the inverse of dl_decode_curve.
DL_DEVICE float dl_encode_curve(int curve, float x) {
    switch (curve) {
        case DL_CURVE_SRGB: {
            const float a = dl_fabs(x);
            const float y = a <= 0.0031308f ? a * 12.92f : 1.055f * dl_pow(a, 1.0f / 2.4f) - 0.055f;
            return x < 0.0f ? -y : y;
        }
        case DL_CURVE_GAMMA_2_4:
            return dl_signed_pow(x, 1.0f / 2.4f);
        case DL_CURVE_ACESCCT:
            return x <= 0.0078125f ? 10.5402377416545f * x + 0.0729055341958355f
                                   : (dl_log2(x) + 9.72f) / 17.52f;
        case DL_CURVE_LOGC3:
            return x > 0.010591f ? 0.247190f * dl_log10(5.555556f * x + 0.052272f) + 0.385537f
                                 : 5.367655f * x + 0.092809f;
        case DL_CURVE_LOGC4:
            return x >= DL_LOGC4_T
                       ? (dl_log2(DL_LOGC4_A * x + 64.0f) - 6.0f) / 14.0f * DL_LOGC4_B + DL_LOGC4_C
                       : (x - DL_LOGC4_T) / DL_LOGC4_S;
        case DL_CURVE_SLOG3:
            return x >= 0.01125f
                       ? (420.0f + dl_log10((x + 0.01f) / 0.19f) * 261.5f) / 1023.0f
                       : (x * (171.2102946929f - 95.0f) / 0.01125f + 95.0f) / 1023.0f;
        case DL_CURVE_VLOG:
            return x < 0.01f ? 5.6f * x + 0.125f : 0.241514f * dl_log10(x + 0.00873f) + 0.598206f;
        case DL_CURVE_CLOG3: {
            const float y = x / 0.9f;
            if (y < -0.014f) {
                return -0.36726845f * dl_log10(1.0f - y * 14.98325f) + 0.12783901f;
            }
            if (y <= 0.014f) {
                return 1.9754798f * y + 0.12512219f;
            }
            return 0.36726845f * dl_log10(y * 14.98325f + 1.0f) + 0.12240537f;
        }
        case DL_CURVE_LOG3G10: {
            const float y = x + 0.01f;
            return y < 0.0f ? y * 15.1927f : 0.224282f * dl_log10(y * 155.975327f + 1.0f);
        }
        case DL_CURVE_DAVINCI_INTERMEDIATE:
            return x <= 0.00262409f ? x * 10.44426855f : (dl_log2(x + 0.0075f) + 7.0f) * 0.07329248f;
        default:
            return x;
    }
}

DL_DEVICE dl_float4 dl_apply_transform(dl_float4 c, DL_CONSTANT const struct dl_transform* t) {
    if (t->decode != DL_CURVE_LINEAR) {
        c = dl_make_float4(dl_decode_curve(t->decode, c.x), dl_decode_curve(t->decode, c.y),
                           dl_decode_curve(t->decode, c.z), c.w);
    }
    if (t->matrix) {
        c = dl_make_float4(t->m[0] * c.x + t->m[1] * c.y + t->m[2] * c.z,
                           t->m[3] * c.x + t->m[4] * c.y + t->m[5] * c.z,
                           t->m[6] * c.x + t->m[7] * c.y + t->m[8] * c.z, c.w);
    }
    if (t->encode != DL_CURVE_LINEAR) {
        c = dl_make_float4(dl_encode_curve(t->encode, c.x), dl_encode_curve(t->encode, c.y),
                           dl_encode_curve(t->encode, c.z), c.w);
    }
    return c;
}

#endif  // DYELINE_KERNELS_TOOLS_COLOR_SPACE_H
