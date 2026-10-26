# Host compatibility

Dyeline is one OFX image effect (`io.github.xnqs.Dyeline`, float images only, RGBA, RGB or
alpha). This page says what each host is checked against and how. A plugin that loads in one
host can still fail in another, because hosts differ in render scale, tiling, regions of
interest and GPU rendering, so each release is checked in every host below.

## What CI checks on every change

| Check | Hosts it stands in for | Where |
| --- | --- | --- |
| The bundle loads and reports one effect with the right version | every host | `tests/plugin_load_test.cpp` |
| Every look renders the same in tiles, at offset windows and from just the source region the plugin asks for | hosts that tile or crop (Natron, Nuke) | `tests/render_test.cpp` |
| CUDA, OpenCL and Metal match the CPU, at a non-square pixel aspect and half render scale | Resolve (CUDA, OpenCL, Metal), Nuke (CUDA) | `tests/gpu_*_test.*` |
| Test looks render in **Natron 2.5** and match the CPU reference, on the CPU and on Dyeline's own OpenCL device | Natron | `tests/hosts/natron/run.py` (Linux CI) |

The Natron check renders a 1920x1080 image through four looks (a plain exposure, a grade, the
spatial tools and the lens tools) with `NatronRenderer`, and compares each result with
`dyeline_render_frame`, the CPU reference. To run it yourself:

```sh
python3 -m venv venv && venv/bin/pip install numpy OpenEXR
venv/bin/python tests/hosts/natron/run.py --natron /path/to/NatronRenderer \
    --plugin-dir build --reference build/tests/dyeline_render_frame
```

## Checked by hand before each release

Resolve and Nuke can't run on CI runners, so they're checked by hand. Use the bundled look
**Old Glass** (it has a warp, a blur and position-dependent tools) on a 4K clip unless a step
says otherwise.

### DaVinci Resolve (Free and Studio, Windows, macOS and Linux)

1. Dyeline shows in the OpenFX library; drag it onto a node. No error in the node.
2. **Edit Look...** opens the designer with the current frame; **Apply** updates the viewer.
3. Apply **Old Glass**: the viewer matches the designer's preview, at full and at proxy
   resolution (Playback > Timeline Proxy Resolution), and on a timeline whose resolution differs
   from the clip's.
4. In Preferences > Memory and GPU, try each GPU processing mode the machine offers (CUDA,
   OpenCL, Metal): the frame looks the same in each.
5. Play back: no flicker between frames, no dropped tiles at the frame's edges.
6. Render the clip in the Deliver page and compare a frame with the viewer.
7. Set Color to the project's timeline space (DaVinci Intermediate, Full), and check a grey
   card stays grey with an empty look.

### Foundry Nuke (Windows, macOS, Linux)

1. Dyeline loads (it appears under OFX in the node menu).
2. **Old Glass** renders at full and at proxy scale (the proxy toggle): the two match apart from
   resolution.
3. Crop the input to a region and put a Reformat after Dyeline: no black or stretched edges
   inside the frame, which would mean the plugin asked for too little source.
4. Turn on GPU rendering in the node, if Nuke offers it, and compare with the CPU.
5. Render with `nuke -x` from the command line and compare a frame with the viewer.

### Natron (beyond CI)

1. **Edit Look...** opens the designer and **Apply** updates the viewer.
2. Proxy mode (render scale 1/2) matches full scale apart from resolution.

### EffectCraft

1. Set **Look File** and add one to **Open Designer**: the designer opens without freezing the
   host.
2. With `--control 9877`, each **Save** in the designer refreshes the effect.
3. **Use Dyeline's GPU** renders the same as the CPU.

## Reporting a host problem

Open an issue with the host and its version, the operating system and GPU, the look file, and
a screenshot of the viewer next to the designer's preview.
