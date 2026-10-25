# Tools

Every tool works on scene-linear RGB in the working space, ACEScg (see [color.md](color.md)),
and leaves alpha alone. Each one has a **Strength** in the
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

In the Look Designer these are the **Wheels** tab. Two more tabs work on hue and saturation,
after the wheels, in the plane across grey (so saturation keeps the channels' average):

**HSL** has a hue, a saturation and a lightness for each of six hue bands, 60 degrees apart:
`red`, `yellow`, `green`, `cyan`, `blue` and `magenta`. A color between two bands takes a share
of each, so the changes blend smoothly around the hue circle. Lightness reaches colors, not
greys: its effect grows with saturation, fully from about a third of the color's level.

| Parameter | Default | Range | Meaning |
| --- | --- | --- | --- |
| `red-hue` (and `yellow-hue` and so on) | 0 | -60 to 60 | Turns the band's colors, in degrees: 60 takes red to yellow. |
| `red-saturation` (and so on) | 1 | 0 to 2 | Scales the band's saturation: 0 is grey, 2 twice as saturated. |
| `red-lightness` (and so on) | 0 | -1 to 1 | Stops added to the band's colors. |

**Sat EQ** scales saturation by luminance, with five bands 4 and 2 stops under the pivot, at
it, and 2 and 4 stops over; beyond the outer bands their value holds.

| Parameter | Default | Range | Meaning |
| --- | --- | --- | --- |
| `sat-deep-shadows` | 1 | 0 to 2 | Saturation 4 stops under the pivot and below. |
| `sat-shadows` | 1 | 0 to 2 | Saturation 2 stops under. |
| `sat-midtones` | 1 | 0 to 2 | Saturation at the pivot. |
| `sat-highlights` | 1 | 0 to 2 | Saturation 2 stops over. |
| `sat-bright-highlights` | 1 | 0 to 2 | Saturation 4 stops over and above. |

Looks saved before these tabs (version 1) load unchanged, since every default leaves the image
alone.

## 4-Way Wheels (`four-way`)

The 3-Way Grade's three wheels, each with its own exposure, plus an **Offset** wheel that
reaches every tone alike, like the Offset ball of a grading panel. The ranges blend as in the
3-Way Grade, and their weights add up to 1, so the Offset's tint and exposure apply evenly from
the deepest shadows to the brightest highlights.

| Parameter | Default | Range | Meaning |
| --- | --- | --- | --- |
| `shadows-hue`, `shadows-amount` | 220, 0 | 0 to 360, 0 to 1 | Shadows tint, as in the 3-Way Grade. |
| `shadows-exposure` | 0 | -2 to 2 | Stops for the shadows. |
| `midtones-hue`, `midtones-amount`, `midtones-exposure` | 40, 0, 0 | as above | The midtones' wheel. |
| `highlights-hue`, `highlights-amount`, `highlights-exposure` | 50, 0, 0 | as above | The highlights' wheel. |
| `offset-hue`, `offset-amount` | 40, 0 | 0 to 360, 0 to 1 | A tint for every tone. |
| `offset-exposure` | 0 | -4 to 4 | Stops for every tone. |
| `pivot` | 0.18 | 0.001 to 10 | The center of the midtones. |

## Blockbuster (`blockbuster`)

The orange and teal look of many action films: every color is pulled toward one warm-cool axis
through grey and spread along it, so skin goes warmer and backgrounds cooler while greens,
magentas and the like lose saturation. Luminance never changes. With the luminance taken out,
the part of a color along the axis grows by half of Amount and the part across it shrinks by
Amount.

| Parameter | Default | Range | Meaning |
| --- | --- | --- | --- |
| `amount` | 0.6 | 0 to 1 | How strongly colors are pulled to the axis; at 1 only the axis is left. |
| `warm-hue` | 30 | 0 to 360 | The warm end of the axis, in degrees; the cool end is opposite. |

## Duotone (`duotone`)

Replaces the image's color with two tints, one for the shadows and one for the highlights,
blending across three stops either side of the pivot, as in a duotone print. Luminance is kept,
so the result is the black and white image, tinted.

| Parameter | Default | Range | Meaning |
| --- | --- | --- | --- |
| `shadows-hue` | 220 | 0 to 360 | The shadows' tint, in degrees. |
| `highlights-hue` | 40 | 0 to 360 | The highlights' tint. |
| `saturation` | 0.4 | 0 to 1 | How colored both tints are; 0 gives black and white. |
| `pivot` | 0.18 | 0.001 to 10 | Where the two tints meet. |

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

## Film Negative (`film-negative`)

Exposes the image onto a color negative stock and returns its scan. Each of the stock's three
layers sees the image through its own spectral sensitivity (a matrix fitted to the stock's
datasheet, with the camera balanced for the light the stock is made for), and develops along
its own characteristic curve. The scan reads the negative so a grey card stays at 0.18 and a
stock's 0.6 mid-tone gamma keeps the image's contrast: what changes is the toe, which holds
deep shadows above black, the shoulder, which rolls off highlights many stops over, and each
layer's slightly different curve, which tints shadows and highlights the way the stock does.

| Parameter | Default | Range | Meaning |
| --- | --- | --- | --- |
| `stock` | 0 | list | The negative (below). |
| `exposure` | 0 | -6 to 6 | Stops of exposure on the negative. Over-exposing moves the image up the curve. |

| `stock` | Negative |
| --- | --- |
| 0 | Kodak Vision3 500T 5219 |
| 1 | Kodak Vision3 200T 5213 |
| 2 | Kodak Vision3 250D 5207 |
| 3 | Kodak Vision3 50D 5203 |
| 4 | Fuji Eterna 500T |
| 5 | Kodak Vision 320T 5277 |

## Film Print (`film-print`)

Prints the image onto a print stock and shows the print as a projector would. The input is
read as a negative's scan, so Film Print after Film Negative is a negative printed onto the
print stock, and Film Print alone prints an ideal negative of the image. Printer light is set so
a grey card prints as a neutral grey at the stock's laboratory aim. Each print layer's curve
gives the print's dye densities, and the dyes' spectra give the color on screen, so saturated
colors shift and darken the way the print's dyes make them. Grey stays at 0.18 and the
print's clear base, its brightest white, comes out at 1.0. A print adds contrast, most of all
in the shadows: on Kodak 2383, two stops under grey prints about three and a half stops under.

| Parameter | Default | Range | Meaning |
| --- | --- | --- | --- |
| `stock` | 0 | list | The print stock (below). |
| `exposure` | 0 | -4 to 4 | Brightens the print, in stops of the image, by giving it less printer light. |
| `red`, `green`, `blue` | 0 | -24 to 24 | Printer points, as in a film lab: one point is 0.025 log exposure. Raising one makes the print denser in that layer's dye, so +Red is more cyan, +Green more magenta, +Blue more yellow. |
| `contrast` | 1 | 0.5 to 2 | Scales the print's tones in stops around grey. 1 is the print as projected. |

| `stock` | Print |
| --- | --- |
| 0 | Kodak Vision 2383 |
| 1 | Kodak Vision Premier 2393 |
| 2 | Fuji Eterna-CP 3513DI |
| 3 | Kodak 5384 |

The stocks' curves and spectra are datasheet values digitized by the spectral_film_lut project;
[core/film_data](../core/film_data/README.md) says where each comes from and how the tables are
built. Both tools depend only on a pixel's color, so `.cube` export keeps them.

## Bleach Bypass (`bleach-bypass`)

Skipping the bleach step in the lab leaves the film's silver in place beside its dyes. The silver
is a black-and-white copy of the image laid over the color one, so the image loses color and
gains contrast, with denser blacks. Every channel of a pixel darkens or brightens by the same
factor, set by its luminance, so grey at the pivot stays put; at Amount 1, one stop over the
pivot comes out a stop and a half over, and colors keep 35% of their difference from grey.

| Parameter | Default | Range | Meaning |
| --- | --- | --- | --- |
| `amount` | 0.7 | 0 to 1 | How much silver is kept. |
| `pivot` | 0.18 | 0.001 to 10 | The scene-linear value the contrast turns around. |

## 2-Strip (`two-strip`)

The early two-color process, which recorded only a red and a green-blue record: red stays and
green and blue both become their average, so the image holds only red-oranges and
cyan-greens, with no true blues, yellows or purples.

| Parameter | Default | Range | Meaning |
| --- | --- | --- | --- |
| `amount` | 1 | 0 to 1 | Mix from the original (0) to the full process (1). |

## Gradient Tint (`gradient-tint`)

Tints one side of a line across the frame, fading across a soft band, like a graduated color
filter on the lens: a blue sky over a neutral foreground, say. The tint shifts color without
brightening or darkening (the 3-Way Grade's tint). Positions are measured as for Lens Vignette:
the center is a fraction of the frame's half width and half height from its middle, and
Softness is a share of the frame's half diagonal. The designer shows handles for the center
and the angle.

| Parameter | Default | Range | Meaning |
| --- | --- | --- | --- |
| `hue` | 210 | 0 to 360 | The tint's hue, in degrees. |
| `amount` | 0.5 | 0 to 1 | Tint strength. |
| `center-x`, `center-y` | 0 | -1 to 1 | A point on the line (y is up). |
| `angle` | 0 | -180 to 180 | Which side is tinted, in degrees clockwise from up: 0 tints the top. |
| `softness` | 0.8 | 0.01 to 2 | How wide the fade across the line is. |

## Grad Exposure (`grad-exposure`)

Like Gradient Tint, but adds or takes away light: a graduated neutral density filter.

| Parameter | Default | Range | Meaning |
| --- | --- | --- | --- |
| `stops` | -1 | -4 to 4 | Stops on the changed side. Negative darkens. |
| `center-x`, `center-y`, `angle`, `softness` | | | As for Gradient Tint. |

## Spot Exposure (`spot-exposure`)

Brightens or darkens a round spot, fading out beyond it: lifting a face, or burning down a
corner. The spot is a circle whatever the frame's shape. The designer shows handles for the
center and the size.

| Parameter | Default | Range | Meaning |
| --- | --- | --- | --- |
| `stops` | 1 | -4 to 4 | Stops inside the spot. |
| `center-x`, `center-y` | 0 | -1 to 1 | The spot's center (y is up). |
| `size` | 0.3 | 0 to 2 | The spot's radius, 1 being the distance to a corner. |
| `softness` | 0.3 | 0.01 to 2 | How far beyond Size it fades out. |

## Fill Light (`fill-light`)

Adds light to the shadows, as a fill light would: full Amount three stops under the pivot and
below, fading to nothing a stop over it, so highlights stay as they are. A brighter pixel
always stays brighter. The tint leans the fill's color, warm or cool, without changing how
much it brightens.

| Parameter | Default | Range | Meaning |
| --- | --- | --- | --- |
| `amount` | 0.5 | 0 to 2 | Stops added to the deep shadows. |
| `pivot` | 0.18 | 0.001 to 10 | Middle grey, where the fill has faded to about a sixth. |
| `tint-hue` | 40 | 0 to 360 | The fill's hue. |
| `tint-amount` | 0 | 0 to 0.5 | How colored the fill is. |

## Spot Fill (`spot-fill`)

Fill Light inside a round spot only, as Spot Exposure places it.

| Parameter | Default | Range | Meaning |
| --- | --- | --- | --- |
| `amount`, `pivot`, `tint-hue`, `tint-amount` | 0.75, 0.18, 40, 0 | | As for Fill Light. |
| `center-x`, `center-y`, `size`, `softness` | 0, 0, 0.3, 0.3 | | As for Spot Exposure. |

## Color Filter (`color-filter`)

A colored glass filter on the lens, such as an 85 (amber) or 80A (blue). It passes its own hue
and holds back the opposite one, by up to twice Density stops. With Preserve Brightness on,
greys keep their brightness, as the camera's exposure would make up for the filter's loss.

| Parameter | Default | Range | Meaning |
| --- | --- | --- | --- |
| `hue` | 30 | 0 to 360 | The filter's color, in degrees. |
| `density` | 0.3 | 0 to 2 | How strong the filter is: the opposite hue loses twice this many stops. |
| `preserve` | 1 | 0 or 1 | Preserve Brightness: 1 keeps grey's brightness; 0 lets the filter darken the image. |

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

## Diffusion Filter (`diffusion`)

Softens the image the way a diffusion filter in front of the lens does: a blurred copy is mixed
back in, so edges and fine detail bloom while flat areas stay as they are. Highlights spread
more than shadows. Size is a fraction of the frame's width, so the look is the same at any
resolution and render scale.

| Parameter | Default | Range | Meaning |
| --- | --- | --- | --- |
| `amount` | 0.5 | 0 to 1 | How much of the blurred copy is mixed in. |
| `size` | 1 | 0.05 to 10 | Blur size, in percent of the frame's width. |
| `highlights` | 0.5 | 0 to 1 | At 0 every tone spreads alike; at 1 mostly highlights spread. |

## Local Contrast (`local-contrast`)

Raises contrast between each pixel and its surroundings while leaving broad brightness alone,
so texture and detail stand out without the image's overall tones changing; below zero it
softens them instead. Each pixel's luminance is compared with a blur of it, and the pixel is
scaled by their ratio to the power of Amount, held within two stops either way. A small floor at
the pivot over 64 keeps noise in the blacks from being amplified. Size is the blur's, a
fraction of the frame's width: small sizes bring out fine texture, large ones shape the
light across whole areas.

| Parameter | Default | Range | Meaning |
| --- | --- | --- | --- |
| `amount` | 0.3 | -1 to 1 | How much local contrast is added (or, below 0, taken away). |
| `size` | 1.5 | 0.1 to 10 | The size of the surroundings, in percent of the frame's width. |

## Telecine Net (`telecine-net`)

A fine net stretched in the light path, behind the lens or in the telecine, scatters a share of
every pixel's light evenly over a wide area. Shadows beside bright areas lift and contrast
softens across the frame, while the frame's total light stays the same. It works like
Diffusion Filter with every tone scattering alike. Size is a fraction of the frame's width.

| Parameter | Default | Range | Meaning |
| --- | --- | --- | --- |
| `amount` | 0.3 | 0 to 1 | The share of light the net scatters. |
| `size` | 4 | 0.5 to 10 | How far it scatters, in percent of the frame's width. |

## Halation (`halation`)

The warm glow film shows around bright highlights, from light that scatters back through the
emulsion. Only light above Threshold glows, so the rest of the image is left alone. The glow is
added, tinted, on top of the image. Size is a fraction of the frame's width.

| Parameter | Default | Range | Meaning |
| --- | --- | --- | --- |
| `amount` | 0.5 | 0 to 2 | Strength of the glow. |
| `threshold` | 1 | 0.05 to 16 | Scene-linear level above which light glows (0.18 is mid grey). |
| `size` | 0.6 | 0.05 to 5 | Glow size, in percent of the frame's width. |
| `tint-red`, `tint-green`, `tint-blue` | 1, 0.25, 0.05 | 0 to 1 | Color of the glow. |
