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
