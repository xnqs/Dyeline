#pragma once

// Color management: the color spaces a look's footage can come in and go out in, and the
// transforms between them and the working space, ACEScg (scene-linear, AP1 primaries).
//
// A look's color mode decides how much is converted:
// - "tone" (1D) converts only the transfer curve. Footage keeps its own primaries, so a
//   camera's wide gamut stays as it was.
// - "full" converts the curve and the gamut, so every tool sees true ACEScg.

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "dyeline/kernels/chain_kernel.h"
#include "dyeline/look.h"

namespace dyeline {

using Matrix3 = std::array<double, 9>;  // row by row

struct ColorSpace {
    const char* id;     // saved in looks, so it never changes once released
    const char* label;
    int curve;          // a DL_CURVE_ code
    Matrix3 to_working; // linear RGB in this space's primaries to ACEScg
};

// Every built-in color space, in menu order.
const std::vector<ColorSpace>& all_color_spaces();

// The space with this id, or null.
const ColorSpace* find_color_space(std::string_view id);

// Fills the chain's input and output transforms for a look's color settings. An output of
// "same" means the input space. Fails, with `problem` naming the setting, on an unknown mode
// or space; the transforms are then left as they were.
bool encode_color(const LookColor& color, dl_transform& input, dl_transform& output,
                  std::string& problem);

// True when a transform changes nothing.
bool is_identity(const dl_transform& transform);

// Converts linear RGB in the primaries and white point given (as CIE xy: red, green, blue,
// white) to linear RGB in another set, adapting the white with Bradford as ACES does.
Matrix3 rgb_to_rgb(const std::array<double, 8>& from, const std::array<double, 8>& to);

}  // namespace dyeline
