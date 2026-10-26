# Changelog

## 0.6.0 beta

Milestone 3: lens tools and polish. Looks made with 0.1 and 0.5 load and render unchanged.

### New tools

- **Lens Distortion**: barrel and pincushion distortion from the radial Brown-Conrady model,
  with an Edges term for the corners, Zoom to fill the frame again, and a movable center.
- **Chromatic Aberration**: lateral (red and blue scaled against green toward the edges) and
  longitudinal (red and blue softened, for colored fringes on hard edges), with a movable
  center.
- **Edge Softness**: blur that grows toward the frame's edges, like an old lens.
- **Swing-Tilt**: a sharp band across the frame with blur growing away from it, for the
  tilt-shift "miniature" look. Both blurs grow smoothly through a light and a full level, and
  have on-image handles.
- **Streaks**: the long horizontal flares of an anamorphic lens, from light above a threshold.
- The **Diffusion Filter** gains a Style: **Pro-Mist** (a milky veil over the shadows) and
  **Black Pro-Mist** (highlights bloom, shadows stay deep). Existing looks keep the plain
  Diffusion style.
- Two bundled looks that use them: **Old Glass** and **Night Lights**.

### Plugin

- Tools can now move pixels, not only recolor them: the render plan renders what comes before
  a warp to an image and reads it back through the warp, on the CPU and every GPU backend.
  The plugin asks the host for exactly the source a warp reads.
- CI renders test looks in Natron 2.5 with the built plugin, on the CPU and on Dyeline's own
  OpenCL device, and compares them with the CPU reference. [docs/hosts.md](docs/hosts.md)
  lists what each host is checked against, and the hand checks for Resolve and Nuke.

### Performance

- `dyeline_bench` times each tool and bundled look on the CPU and Dyeline's own GPU device
  ([docs/performance.md](docs/performance.md)).
- Looks with spatial tools render 20 to 30% faster: the color before the first spatial tool
  is kept rather than worked out again by every later pass. Warps read fewer taps, and CPU
  blurs down columns use the cache better. Old Glass is 28% faster on the CPU.

### Known issues

- Warps leave transparent black where they read past the frame's edges (Lens Distortion's
  corners, for instance); use Zoom to fill the frame.
- The film stocks from 0.5 still await a colorist's check, and the installers aren't signed yet.
- Resolve and Nuke are checked by hand ([docs/hosts.md](docs/hosts.md)); only Natron renders on CI.

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
