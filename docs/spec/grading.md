# Grading tools: behavior spec

4-Way Wheels, Blockbuster, Color Filter, Duotone, Local Contrast and the 3-Way Grade's HSL and
Sat EQ tabs, written from color grading and photographic practice and the tools' own
descriptions in [docs/tools.md](../tools.md), and nothing else (see
[CLEANROOM.md](../../CLEANROOM.md)).

- **4-Way Wheels.** Grading panels pair lift, gamma and gain wheels with an offset that moves
  every tone. Dyeline reuses the 3-Way Grade's op: each range's stops per channel are its tint
  plus its exposure, and the Offset's tint and exposure are added to all three ranges. The
  ranges' weights add up to 1, so the Offset reaches every tone alike.
- **HSL.** A hue-band qualifier picks colors by hue. Dyeline measures hue as the angle in the
  plane across grey, with axes a = (2R - G - B) / sqrt(6) and b = (G - B) / sqrt(2), which puts
  red at 0 and the six bands 60 degrees apart, and blends linearly between neighboring bands.
  Each band's turn rotates (a, b), its saturation scales them, and its lightness multiplies the
  pixel by 2^stops, weighted by smoothstep(0, mean / 3, chroma) so greys are untouched.
  Rotating and scaling (a, b) keeps the channels' mean.
- **Sat EQ.** Saturation by luminance, with bands at -4, -2, 0, 2 and 4 stops from the pivot,
  blended linearly and held beyond the ends; it multiplies the HSL saturation.
- **Blockbuster.** The orange and teal look pushes colors toward a warm-cool axis. Dyeline
  removes luminance (c - Y), scales the part along the axis by 1 + Amount / 2 and the rest by
  1 - Amount, and adds Y back; it is linear, so it is a matrix, and luminance never changes.
- **Duotone.** A duotone print inks shadows and highlights in two colors. Dyeline outputs
  luminance times a blend of two tints, each with a luminance-weighted sum of 1, blended by a
  smoothstep over three stops either side of the pivot.
- **Color Filter.** A colored lens filter passes its own hue and absorbs the opposite one.
  Dyeline scales each channel by 2^(-Density (1 - cos(hue - channel angle))), with red at 0,
  green at 120 and blue at 240, and with Preserve Brightness divides by the luminance of those
  gains so grey keeps its level.
- **Local Contrast.** Local contrast (or clarity) compares a pixel with its surroundings.
  Dyeline blurs luminance with the same box-blur passes as Diffusion Filter and scales the
  pixel by ((Y + f) / (blur(Y) + f))^Amount, with f = pivot / 64 and the gain held to 1/4..4.
