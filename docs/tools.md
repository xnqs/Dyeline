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
