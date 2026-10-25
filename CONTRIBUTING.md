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
| `core/include/dyeline/kernels/` | Per-pixel tool math shared by every backend (CPU, CUDA, Metal, OpenCL). |
| `core/src/gpu/` | Thin CUDA, Metal and OpenCL wrappers that launch the shared kernels. |
| `plugin/` | The OFX plugin: parameters, clips and the bridge to the core. |
| `tests/` | Unit tests for the core and a plugin load test. |
| `cmake/` | Build helpers: OpenFX SDK fetch and bundle layout. |

## Tests

Every tool needs unit tests in `tests/` that pin its math. GPU backends are tested against the
CPU result of the same shared kernel; those tests skip themselves on machines without that GPU.
CI runs the OpenCL test for real on Linux through PoCL, a CPU OpenCL driver.

## Writing kernels

Tool math lives in `core/include/dyeline/kernels/` and is compiled as C++, CUDA, Metal and OpenCL C. Use only
what `dl_kernel.h` provides plus basic C arithmetic: no standard library, templates or exceptions.
Mark functions `DL_DEVICE` and buffer pointers `DL_GLOBAL`. Metal and OpenCL build the kernels at
run time from a source string; list any new kernel header in `cmake/DyelineKernels.cmake`.

## Style

- C++17, 4-space indent, `snake_case` for functions and variables, `PascalCase` for types.
- Keep OFX types out of `core/`.
- Prefer plain, commented math over clever code; color code is read far more often than written.
