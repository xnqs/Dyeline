# Selective tools: behavior spec

Gradient Tint, Grad Exposure, Spot Exposure, Fill Light and Spot Fill, written from
photographic practice and the tools' own descriptions in [docs/tools.md](../tools.md), and
nothing else (see [CLEANROOM.md](../../CLEANROOM.md)).

- **Graduated filters.** A graduated color or neutral density filter changes one side of a line
  across the frame with a soft transition. Dyeline scales each channel by stops times a weight
  that rises with a smoothstep across a band centered on the line. Color tints use the 3-Way
  Grade's luminance-neutral directions; exposure uses equal stops in every channel.
- **Spots.** A spot is a circle in the frame's canonical coordinates, its radius measured in
  half diagonals as Lens Vignette measures distance, with a smoothstep falloff beyond it.
- **Fill light.** A fill light adds light mostly where the key leaves shadow. Dyeline adds stops
  weighted by 1 - smoothstep(-3, 1, log2(Y / pivot)), whose steepest slope (0.375 per stop)
  times the largest Amount (2) stays under 1, so tones never swap places.
- **Handles.** Each tool that depends on position lists its center, radius or angle
  parameters as on-image handles, and the Look Designer draws them over the preview.
