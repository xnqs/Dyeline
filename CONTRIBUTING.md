# Contributing to Dyeline

Thanks for helping. Please read [CLEANROOM.md](CLEANROOM.md) first: every contribution must follow
it.

## Sign-off (DCO)

Every commit needs a `Signed-off-by` line, which certifies the
[Developer Certificate of Origin](https://developercertificate.org/): you wrote the change or have
the right to submit it under the project's license. Add it with:

```sh
git commit -s
```

## Workflow

1. Open an issue or comment on one before starting larger work.
2. Branch from `main` and keep each pull request to one topic.
3. Build and run the tests locally (see the README).
4. Open a pull request and fill in the template, including the clean-room checklist.

## Code layout

| Folder | Contents |
| --- | --- |
| `core/` | The render core: tools and color math. Plain C++17, no OFX types. |
| `core/include/dyeline/kernels/` | The fused per-pixel pass, shared by every backend (CPU, CUDA, Metal, OpenCL). |
| `core/include/dyeline/kernels/tools/` | One header per tool with its per-pixel math, plus `color_space.h`, the camera curves. |
| `core/src/gpu/` | Thin CUDA, Metal and OpenCL wrappers that launch the shared kernels. |
| `plugin/` | The OFX plugin: parameters, clips and the bridge to the core. |
| `designer/` | The Look Designer app (Dear ImGui on SDL3), launched by the plugin. `src/look_editor.*` is its editing model, tested without a window. |
| `tests/` | Unit tests for the core and a plugin load test. |
| `schema/` | The JSON Schema of the `.dyeline` look format. |
| `cmake/` | Build helpers: OpenFX SDK fetch and bundle layout. |

## Tests

Every tool needs unit tests in `tests/` that pin its math. GPU backends are tested against the
CPU result of the same shared kernel; those tests skip themselves on machines without that GPU.
CI runs the OpenCL test for real on Linux through PoCL, a CPU OpenCL driver.

## Writing kernels

Tool math lives in `core/include/dyeline/kernels/` and is compiled as C++, CUDA, Metal and OpenCL
C. Use only what `dl_kernel.h` provides plus basic C arithmetic: no standard library, templates or
exceptions. Mark functions `DL_DEVICE`, image buffer pointers `DL_GLOBAL` and pointers into the
chain `DL_CONSTANT`. Metal and OpenCL build the kernels at run time from a source string that
CMake assembles from every header in `kernels/tools/`.

## Adding a per-pixel tool

1. Write `core/include/dyeline/kernels/tools/<tool>.h` with a `dl_tool_<tool>(color, p)` function.
   It gets straight (unpremultiplied) RGB and up to `DL_OP_PARAMS` floats, and must leave alpha
   alone.
2. Give it an op code in `chain_kernel.h` and a `case` in `dl_run_op`.
3. Register it in `core/src/tools.cpp`: its id (saved in looks, so it never changes), label,
   category, version, parameters with defaults and ranges, an `encode` that turns parameter
   values into the op's floats (work out anything that is constant per frame here, not per
   pixel), and an `is_identity` check. A tool that reads a file or needs more data than its
   op's floats, like LUT, sets `prepare` instead: it appends its data to the chain's table and
   stores the offset in its op, and the kernel reads it from the `table` argument.
4. Pin its math in `tests/tools_test.cpp`. When you later change its parameters, raise its
   version and add a `migrate` step so older looks still load.

A tool that blurs, like Diffusion Filter or Halation, is a spatial op: give it a
`dl_tool_<tool>_extract` that picks the light to blur and a `dl_tool_<tool>_combine` that mixes
the blurred image back into the pixel, add a `case` for each in `dl_extract_op` and
`dl_run_op`, list its op code in `dl_is_spatial`, and store its blur sigma (as a
fraction of the frame's width) in `p[DL_OP_BLUR_SIZE]`. `plan_render` in `core/src/render.cpp`
adds the extract and blur passes and the margin the host must supply; test it in
`tests/render_test.cpp`, including that tiled renders match a whole-frame one.

## Style

- C++17, 4-space indent, `snake_case` for functions and variables, `PascalCase` for types.
- Keep OFX types out of `core/`.
- Prefer plain, commented math over clever code; color code is read far more often than written.
