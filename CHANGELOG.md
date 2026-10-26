# Changelog

## Unreleased

Milestone 3: lens tools and polish.

### New tools

- **Lens Distortion**: barrel and pincushion distortion from the radial Brown-Conrady model,
  with an Edges term for the corners, Zoom to fill the frame again, and a movable center.
- **Chromatic Aberration**: lateral (red and blue scaled against green toward the edges) and
  longitudinal (red and blue softened, for colored fringes on hard edges), with a movable
  center.
- A bundled look that uses them, **Old Glass**.

### Plugin

- Tools can now move pixels, not only recolor them: the render plan renders what comes before
  a warp to an image and reads it back through the warp, on the CPU and every GPU backend.
  The plugin asks the host for exactly the source a warp reads.

## 0.5.0 beta

Milestone 2: film and selective tools. Looks made with 0.1 load unchanged.

### New tools

- **Film Negative** and **Film Print**: a film stock's response, built from its datasheet
  (spectral sensitivities, characteristic curves and dye densities). Negatives: Kodak Vision3
  500T 5219, 200T 5213, 250D 5207 and 50D 5203, Fuji Eterna 500T and Kodak Vision 320T 5277.
  Prints: Kodak Vision 2383, Vision Premier 2393, Fuji Eterna-CP 3513DI and Kodak 5384. Printer
  lights, exposure and contrast on the print.
- **Bleach Bypass**, **2-Strip** and **Telecine Net**.
- Selective tools: **Gradient Tint**, **Grad Exposure**, **Spot Exposure**, **Fill Light** and
  **Spot Fill**.
- **4-Way Wheels** (shadows, midtones, highlights and an offset, each with exposure),
  **Blockbuster** (orange and teal), **Duotone**, **Color Filter** and **Local Contrast**.
- The **3-Way Grade** gains **HSL** and **Sat EQ** tabs: hue, saturation and lightness for six
  hue bands, and saturation by brightness.

### Look Designer

- On-image handles: drag the center of a spot, gradient or Lens Vignette, a spot's size, and a
  gradient's angle over the preview. Each drag is one undo step.
- Tools with many controls split them into tabs.
- A user guide, [docs/user-guide.md](docs/user-guide.md).

### Plugin

- When the host renders on the CPU, **Use Dyeline's GPU** renders on Dyeline's own OpenCL
  (Windows, Linux) or Metal (macOS) device, falling back to the CPU.
- EffectCraft: **Open Designer** opens the designer on the Look File without the host waiting,
  and each save in the designer reloads matching effects over EffectCraft's control port.
- Started with no arguments, the designer opens a new look in your looks; the Windows and Linux
  installers add it to the app menus.

### Known issues

- The film stocks are fitted from published datasheets and still need checking by colorists:
  5219's negative alone leans cyan in the highlights, and the print darkens the shadows more
  than some will expect.
- The installers aren't signed yet.

## 0.1.0 alpha

The first release: the look format, the per-pixel and glow tools, CUDA, OpenCL and Metal
rendering, the Look Designer with scopes and a looks browser, .cube export, and installers for
Windows, macOS and Linux.
