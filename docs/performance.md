# Performance

`dyeline_bench` times Dyeline's renders: each tool on its own at its default settings, then
each bundled look. It is built with the plugin:

```sh
cmake --build build --target dyeline_bench
./build/bench/dyeline_bench --sizes 1080,2160 --backends cpu,opencl,metal --runs 5
```

It prints a Markdown table of milliseconds per frame, the median of the runs after a warm-up.
The times include what the plugin does each frame: compiling the look and planning the render,
and on Dyeline's own GPU device the upload and read-back. `--only <text>` keeps the cases
whose names contain the text, and `--looks <dir>` times another folder of looks. A backend
with no device on the machine is left out.

## Where the time goes

A render is a list of passes, planned in `core/src/render.cpp`:

- **Color tools** cost one evaluation per pixel and are cheap on every backend.
- **Spatial tools** (Diffusion, Halation, Streaks, Local Contrast, Edge Softness, Swing-Tilt)
  add an extract pass and six box-blur passes, twelve for the two-level blurs. The blur's cost
  does not grow with its size.
- **Warps** (Lens Distortion, Chromatic Aberration) render what comes before them to an image
  and read it back with bilinear filtering. Chromatic Aberration with Fringe reads 15 taps
  a pixel, which makes it the most expensive tool on the CPU.

## What was optimized in 0.6

Measured on the CPU, 4 threads of a cloud Xeon, at 1080p (milliseconds per frame):

| Look | Before | After |
| --- | ---: | ---: |
| Old Glass | 640 | 461 |
| Candlelight | 427 | 290 |
| Daylight Premier | 214 | 155 |
| Neon Pop | 250 | 187 |
| Seventies | 277 | 211 |
| Sun Bleached | 282 | 223 |
| Golden Hour | 269 | 192 |

- **The color before the first spatial tool is kept.** The extract pass of the first spatial
  tool after the source (or after a warp) stores the color at its input. Later passes read it
  instead of running the tools before it again, so a look's grade runs once per pixel rather
  than once per pass. It costs one RGBA image of the extract region, and is not used for
  alpha-only images. The result is identical to rendering without it (a test checks this).
- **Warps read all channels with one tap when they can.** Lens Distortion moves every channel
  alike, so it reads RGBA once instead of once per channel. Chromatic Aberration's fringe
  offsets are constants instead of a sine and cosine per tap.
- **Blurs down columns run in blocks of neighboring columns on the CPU**, which share cache
  lines, with the same sums in the same order as the GPU kernels.

GPU backends get the first two; their times depend too much on the card to quote here. Run
`dyeline_bench` on your own machine to compare.
