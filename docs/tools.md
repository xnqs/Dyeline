# Tools

Every tool works on scene-linear RGB and leaves alpha alone. Each one has a **Strength** in the
look (0 to 1) that blends its result with its input. Tool ids are what `.dyeline` files store.

## Exposure (`exposure`)

| Parameter | Default | Range | Meaning |
| --- | --- | --- | --- |
| `stops` | 0 | -16 to 16 | Light added or taken away, in stops. +1 doubles it. |

## Contrast (`contrast`)

Steepens or flattens the image around a pivot grey, evenly in stops: with Contrast 2, one stop
above the pivot becomes two. Values at or below zero pass through.

| Parameter | Default | Range | Meaning |
| --- | --- | --- | --- |
| `contrast` | 1 | 0 to 4 | Slope in stops. 1 leaves the image unchanged, below 1 flattens it. |
| `pivot` | 0.18 | 0.001 to 10 | The scene-linear value that stays put (0.18 is middle grey). |

## S Curve (`s-curve`)

Adds contrast through the mid-tones and rolls off the shadows and highlights, like a print.
It works in stops around the pivot: values `range` stops above and below the pivot stay put,
the mid-tone slope rises from 1 (Amount 0) to 4 (Amount 1), and the curve flattens toward the
ends without ever reversing, so no detail is crushed to a flat line.

| Parameter | Default | Range | Meaning |
| --- | --- | --- | --- |
| `amount` | 0.5 | 0 to 1 | How strong the S is. |
| `pivot` | 0.18 | 0.001 to 10 | The scene-linear value at the curve's center. |
| `range` | 5 | 1 to 12 | Stops either side of the pivot where the curve meets the original. |

## Hue/Saturation (`hue-saturation`)

Turns every color around the grey axis, then pushes colors toward or away from grey while
keeping their luminance (ACEScg weights). Greys never change.

| Parameter | Default | Range | Meaning |
| --- | --- | --- | --- |
| `hue` | 0 | -180 to 180 | Degrees to turn. +120 takes red to green. |
| `saturation` | 1 | 0 to 4 | 0 is grey, 1 unchanged, 2 twice as colorful. |

## Black & White (`black-white`)

Makes grey from a weighted mix of the channels, like a color filter on black-and-white film.
The weights are scaled to add up to 1, so moving them shifts tones (a red filter darkens blue
skies) without changing overall brightness. All zero falls back to luminance.

| Parameter | Default | Range | Meaning |
| --- | --- | --- | --- |
| `red` | 0.27 | 0 to 2 | How much red contributes. |
| `green` | 0.67 | 0 to 2 | How much green contributes. |
| `blue` | 0.06 | 0 to 2 | How much blue contributes. |

## Channel Mixer (`channel-mixer`)

Builds each output channel from a mix of the three inputs. The defaults leave the image
unchanged.

| Parameter | Default | Range |
| --- | --- | --- |
| `red-from-red`, `green-from-green`, `blue-from-blue` | 1 | -2 to 2 |
| `red-from-green`, `red-from-blue`, `green-from-red`, `green-from-blue`, `blue-from-red`, `blue-from-green` | 0 | -2 to 2 |

## Shadows/Highlights (`shadows-highlights`)

Lifts or lowers the shadows and the highlights separately, in stops. Each fades in over six
stops from the pivot, follows the pixel's luminance and is applied as a gain, so hues don't
shift and tones never swap places.

| Parameter | Default | Range | Meaning |
| --- | --- | --- | --- |
| `shadows` | 0 | -2 to 2 | Stops added deep in the shadows. |
| `highlights` | 0 | -2 to 2 | Stops added high in the highlights. |
| `pivot` | 0.18 | 0.001 to 10 | Where neither changes. |

## Highlight Shoulder (`highlight-shoulder`)

Rolls values above Start smoothly toward a ceiling, the way film eases into its brightest
tones instead of clipping. The join at Start is seamless, and each channel rolls separately, so
very bright colors also lose some saturation, as on film.

| Parameter | Default | Range | Meaning |
| --- | --- | --- | --- |
| `start` | 1 | 0.05 to 16 | Scene-linear value where the roll-off begins. |
| `headroom` | 1.5 | 0.1 to 8 | Stops above Start that the ceiling sits at. |

## 3-Way Grade (`three-way`)

Tints the shadows, the midtones and the highlights separately, like the three color wheels of a
grading panel. Each range's tint is a hue and an amount; the ranges blend into each other over
six stops either side of the pivot, following the pixel's luminance. A tint shifts color
without brightening or darkening, and tones never swap places.

| Parameter | Default | Range | Meaning |
| --- | --- | --- | --- |
| `shadows-hue` | 220 | 0 to 360 | Shadows tint direction, in degrees: 0 red, 120 green, 240 blue. |
| `shadows-amount` | 0 | 0 to 1 | Shadows tint strength. At 1 the strongest channel moves by about a stop. |
| `midtones-hue` | 40 | 0 to 360 | Midtones tint direction. |
| `midtones-amount` | 0 | 0 to 1 | Midtones tint strength. |
| `highlights-hue` | 50 | 0 to 360 | Highlights tint direction. |
| `highlights-amount` | 0 | 0 to 1 | Highlights tint strength. |
| `pivot` | 0.18 | 0.001 to 10 | The center of the midtones. |

## LUT (`lut`)

Applies a `.cube` LUT file, 1D or 3D, like the ones other grading tools export. 3D tables use
tetrahedral interpolation. Values outside the table's input domain are held at its edges.

Most LUTs are built for display-encoded video, not the scene-linear light Dyeline works in. LUT
Gamma bridges that: each value is raised to 1/gamma before the lookup and the result to gamma
after, so a LUT built for gamma-2.4 video can be applied with LUT Gamma 2.4.

| Parameter | Default | Range | Meaning |
| --- | --- | --- | --- |
| `file` | none | path | The `.cube` file. A relative path is relative to the look file's folder; `~` is the home folder. |
| `gamma` | 1 | 0.2 to 5 | LUT Gamma: the encoding the LUT expects. 1 means scene-linear. |

```json
{ "tool": "lut", "params": { "file": "luts/film.cube", "gamma": 2.4 } }
```

If the file can't be read, the LUT is bypassed and the plugin shows a warning saying why.
Files with both a 1D and a 3D table, and 3D tables over 129 points, aren't supported yet. Like a
look file, a LUT changed on disk is picked up on the next render; EffectCraft needs Reload Look
File bumped to render again.

## Lens Vignette (`lens-vignette`)

Darkens the frame toward its edges, the way a lens loses light away from its center, or
brightens them with a positive Amount. The change is in stops, so it dims highlights and
shadows alike. Distances are measured so the frame's corners are at 1, whatever its shape and
render scale.

| Parameter | Default | Range | Meaning |
| --- | --- | --- | --- |
| `amount` | -1 | -4 to 4 | Stops at the edge of the falloff. Negative darkens. |
| `size` | 0.5 | 0 to 2 | Distance from the center where the falloff begins (1 is a corner). |
| `softness` | 0.6 | 0.01 to 2 | How far past Size the falloff reaches its full Amount. |
| `roundness` | 1 | 0 to 1 | 1 is a circle; 0 is an ellipse that follows the frame's shape. |
| `center-x` | 0 | -1 to 1 | Moves the center, as a fraction of half the frame's width. |
| `center-y` | 0 | -1 to 1 | Moves the center, as a fraction of half the frame's height (up is positive). |

## Grain (`grain`)

Film-like grain. Each pixel moves by a small random amount in stops, strongest in the midtones
and fading toward deep shadows and bright highlights, without shifting the image's overall
brightness. Grain is sized in full-resolution pixels, so it looks the same at any render scale,
and it changes every frame unless Animate is off.

| Parameter | Default | Range | Meaning |
| --- | --- | --- | --- |
| `amount` | 0.3 | 0 to 1 | Strength. At 1, midtones move by up to about a stop either way. |
| `size` | 1.5 | 0.5 to 8 | Grain size, in full-resolution pixels. |
| `color` | 0.3 | 0 to 1 | 0 is the same grain in every channel; 1 is separate grain per channel. |
| `animate` | 1 | 0 or 1 | 1 changes the grain every frame; 0 keeps it still. |
| `seed` | 0 | 0 to 9999 | Picks a different grain pattern. |
