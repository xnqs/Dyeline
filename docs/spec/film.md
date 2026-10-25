# Film Negative and Film Print: behavior spec

Written from published color science and film data only (see [CLEANROOM.md](../../CLEANROOM.md)).
The user-facing description is in [docs/tools.md](../tools.md); the model's numbers are in
`scripts/film/make_film_data.py`.

## Behavior

**Film Negative** takes scene-linear ACEScg and returns the scan of a color negative exposed to
it. Grey (0.18) comes back as grey. Shadows are held above black by the stock's toe, highlights
roll off along its shoulder, and each layer follows its own curve, so the tones a stock tints
are the ones its datasheet curves part in. A stop of Exposure is a stop more light on the
negative.

**Film Print** takes a negative's scan (or any image, read as one) and returns the projected
print. Grey prints as a neutral grey at 0.18, the clear base is white at 1.0, the print adds
contrast, most of all in the shadows, and saturated colors take on the print dyes' hues.
Printer points work as in a film lab: one point is 0.025 log exposure, and adding light to a
layer makes the print denser in that layer's dye.

## Model

1. Layer exposure: a 3x3 matrix from ACEScg, fitted by least squares to the exposures the
   stock's spectral sensitivities give for the 24 ColorChecker patches under a blackbody at the
   stock's rated color temperature (3200 K for tungsten stocks, 5500 K for daylight).
2. Negative density: each layer's characteristic curve, with a grey card at the ISO speed's
   exposure, log10(12.5 / ISO) lux-seconds.
3. Scan: 0.18 * 10^((D - D_grey) / 0.6), the density-to-log relation of a negative with the
   usual 0.6 mid-tone gamma.
4. Print exposure: log E = aim - (D_negative - D_grey), with the aim chosen so a grey card prints
   neutral at the stock's laboratory aim density.
5. Print density: each print layer's characteristic curve (Status A).
6. Display: the amounts of the three print dyes that measure those densities through Status A,
   their transmission under a 5500 K projector, seen with the CIE 1931 2-degree observer and
   adapted (CAT02) to ACES white; then scaled in stops around grey so the clear base is 1.0.

## Sources

- Film datasheets (Kodak, Fujifilm): spectral sensitivity, characteristic curves, dye spectral
  density and laboratory aim densities, as digitized by spectral_film_lut,
  https://github.com/JanLohse/spectral_film_lut (MIT).
- Status A and Status M densitometer responses: ISO 5-3, as tabulated in spectral_film_lut.
- CIE 1931 2-degree color matching functions and the ColorChecker reflectances (N. Ohta), as
  shipped in colour-science, https://github.com/colour-science/colour.
- ACEScg primaries and white point: the ACES specification (S-2014-004).
- The ISO speed's grey card exposure and the 0.6 negative gamma: published sensitometry
  practice, the same values spectral_film_lut uses.
