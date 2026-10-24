# Clean-room rules

Dyeline matches what Magic Bullet Looks does as described in public material, never how its
software does it. These rules apply to every contributor and every contribution: code, shaders,
data, presets, documentation and artwork. Each pull request confirms them in its checklist.

## Allowed sources

- Maxon's public help pages, release notes, marketing pages and tutorial videos for Magic Bullet
  Looks.
- Published color science: papers, textbooks, the ACES and OpenColorIO specifications, and camera
  vendors' published log and gamut documents.
- Manufacturer film datasheets, used only to extract numbers. The datasheets themselves are never
  committed.
- Observing Magic Bullet Looks as a black box (watching it run and describing what a control does in
  your own words).

## Not allowed

- Disassembling, decompiling, debugging or tracing Magic Bullet Looks binaries, shaders, data files
  or network traffic.
- Copying shipped presets, LUTs, thumbnails, icons, UI artwork or exact preset parameter values.
- Reading or reusing `.MBLook` files. Import of that format is deferred until after v1 and needs a
  legal review and a separate process first.
- Writing Dyeline code if you have seen Red Giant or Maxon source code, or are under an NDA that
  covers it.
- Using Maxon tool brand names (Colorista, Mojo, Renoiser, Huenity, Cosmo, Magic Bullet) as Dyeline
  names.

## Process

1. Each tool gets a behavior spec in `docs/spec/`, written from allowed sources and citing each
   source URL.
2. Implementers work only from the spec and the color science literature.
3. Visual comparison with Magic Bullet Looks output is allowed for tuning, but parameter ranges and
   curves are derived, not copied. Comparison notes go in the pull request.
4. Every commit carries a DCO sign-off (see [CONTRIBUTING.md](CONTRIBUTING.md)) and every pull
   request ticks the clean-room box in its template.

If you are unsure whether a source is allowed, ask in an issue before using it.
