#pragma once

// The designer's scopes, drawn from the preview as it is shown on screen: an RGB parade
// (each channel's levels across the width of the frame, side by side) and a vectorscope
// (the Rec.709 chroma of every pixel, with the 75% color bar targets and the skin tone line).

#include "preview.h"

namespace dyeline::designer {

// Each channel's panel is kParadeColumns wide and 256 levels tall, with a small gap between.
constexpr int kParadeColumns = 256;
constexpr int kParadeGap = 8;
constexpr int kParadeWidth = kParadeColumns * 3 + kParadeGap * 2;
constexpr int kParadeHeight = 256;
constexpr int kVectorscopeSize = 256;

DisplayImage rgb_parade(const DisplayImage& image);
DisplayImage vectorscope(const DisplayImage& image);

}  // namespace dyeline::designer
