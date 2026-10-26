# Lens tools: behavior spec

Lens Distortion, Chromatic Aberration, Edge Softness and Swing-Tilt, written from optics references and the tools' own
descriptions in [docs/tools.md](../tools.md), and nothing else (see
[CLEANROOM.md](../../CLEANROOM.md)).

- **Measure.** Positions follow Lens Vignette: the lens's center is a fraction of the frame's
  half width and half height from its middle, and a distance r is 1 at the frame's corners.
- **Radial distortion.** Brown's model describes a lens's radial distortion as the ideal image
  position scaled by a polynomial in r^2. Dyeline maps each output pixel at distance r to the
  input at r * (1 + k1 r^2 + k2 r^4) / zoom, along the same direction. k1 > 0 reads from
  farther out as r grows, so straight lines bow outward (barrel); k1 < 0 gives pincushion; k2
  of the opposite sign gives a "mustache" shape. Amount and Edges scale k1 by 0.25 and k2 by
  0.1, which keeps the map in order along each ray across a centered frame except at the
  strongest pincushion settings, where the corners fold back as a real lens's would.
- **Lateral chromatic aberration.** A lens's magnification differs slightly by wavelength, so
  the red and blue images are scaled about the optical axis against green; the split grows
  in proportion to the distance from the axis. Dyeline scales red and blue so they move by
  their shift (a fraction of the frame's width) at the corners. Green stays put.
- **Longitudinal (axial) chromatic aberration.** Red and blue focus in front of or behind
  green, so in focus they are slightly blurred everywhere, which shows as colored fringes along
  hard edges. Dyeline averages red and blue over a small disc (the center and six points on a
  hexagon) and leaves green sharp.
- **Outside the frame.** Reading past the frame's edges gives transparent black, so a warp
  never invents image from stretched edge pixels; Zoom brings the frame back.
- **Strength.** Scales how far pixels move (and the disc's radius) rather than mixing two
  images, so half strength is half the effect with no double image.
- **Off-axis softness.** Simple lenses resolve less away from the axis (field curvature and
  off-axis aberrations), so the image softens toward the edges. Edge Softness blurs by a
  weight that is 0 inside Size and rises with a smoothstep over Softness, measured like a
  spot.
- **Tilt.** Tilting the lens against the film plane (the Scheimpflug principle) puts the plane
  of focus at an angle, so on a flat scene a band stays sharp and the blur grows with distance
  from it. Swing-Tilt weights the blur by a smoothstep of the distance from a line through the
  center, beyond Focus.
- **Growing blur.** A blur that grows across the frame is drawn from three images: the sharp
  pixel, a light Gaussian blur (a third of the size) and the full one, made by blurring the
  light blur again (Gaussian variances add). The weight runs from sharp to light to full, so
  partial blur looks like a smaller blur rather than a soft glow over a sharp image.

## Sources

- D. C. Brown, "Decentering Distortion of Lenses", Photogrammetric Engineering 32 (3), 1966,
  for the radial polynomial model.
- E. Hecht, *Optics*, chapter 6 (aberrations), for lateral and longitudinal chromatic
  aberration.
- Wikipedia, "Distortion (optics)", https://en.wikipedia.org/wiki/Distortion_(optics), and
  "Chromatic aberration", https://en.wikipedia.org/wiki/Chromatic_aberration, for the barrel,
  pincushion and mustache shapes and the two kinds of chromatic aberration.
- Wikipedia, "Scheimpflug principle", https://en.wikipedia.org/wiki/Scheimpflug_principle,
  and "Tilt–shift photography", https://en.wikipedia.org/wiki/Tilt%E2%80%93shift_photography,
  for tilted planes of focus and the miniature effect.
