# Dyeline

Dyeline is a free, open-source OpenFX (OFX) plugin for building film-style looks from a chain of
color, lens and film tools, with GPU acceleration. It is a clean-room project inspired by the
workflow of Magic Bullet Looks.

> Dyeline is not affiliated with, endorsed by or connected to Maxon or Red Giant. "Magic Bullet
> Looks" is their trademark and is named here only to describe what Dyeline is an alternative to.

**Status:** early scaffolding (Milestone 0). The plugin currently offers one tool, Exposure, rendered
on the CPU, or on the GPU through CUDA (Windows, Linux), OpenCL (Windows, Linux) or Metal (macOS)
when the host provides one.
It also carries a spike of the Look Designer round trip (see below). It is not useful for real
work yet.

## Hosts

Dyeline targets OFX hosts on Windows, macOS and Linux. First targets:

- DaVinci Resolve
- Natron
- EffectCraft (OpenFX fork)

## Building

Requirements: CMake 3.24+, a C++17 compiler, and git (the OpenFX SDK and GoogleTest are fetched at
configure time). The CUDA backend is built when a CUDA toolkit (nvcc) is found; turn it off with
`-DDYELINE_ENABLE_CUDA=OFF`. The OpenCL backend is built on Windows and Linux and needs no SDK: the
headers are fetched at configure time and the OpenCL library is loaded at run time, so the plugin
still loads on machines without it (`-DDYELINE_ENABLE_OPENCL=OFF` turns it off). The Metal backend
is always built on macOS.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The OpenCL test runs on the first OpenCL device it finds and skips itself when there is none. On
Linux with an AMD GPU, Mesa's rusticl driver runs it on the GPU itself, no host needed:

```sh
sudo apt install mesa-opencl-icd   # Debian/Ubuntu; Fedora: mesa-libOpenCL
RUSTICL_ENABLE=radeonsi ctest --test-dir build -R OpenCL --output-on-failure
```

The plugin bundle is written to `build/Dyeline.ofx.bundle`. Copy that folder into your host's OFX
plugin folder, or run `cmake --install build` to copy it to the standard location:

| OS | OFX plugin folder |
| --- | --- |
| Windows | `C:\Program Files\Common Files\OFX\Plugins` |
| macOS | `/Library/OFX/Plugins` |
| Linux | `/usr/OFX/Plugins` |

## Trying the designer round trip

The bundle ships `dyeline-designer` next to the plugin binary. In Milestone 0 it is a stub with
no window: it raises Exposure by one stop and hands the look back. There are two ways to link it
to the plugin, depending on the host.

**Edit Look (Natron, Resolve).** Click **Edit Look...** in the plugin's controls. The host
freezes while the designer runs (two seconds), then Exposure goes up by 1 and the **Designer**
line reports how long the round trip took. Undo restores the old values in one step. To check
how a host copes with a long edit, start it with `DYELINE_DESIGNER_DELAY` set to the number of
seconds the stub should wait, for example `DYELINE_DESIGNER_DELAY=60 natron`.

**Look File (EffectCraft, and any host).** Some hosts never take parameter values back from a
plugin, so Edit Look does nothing there. Instead, set **Look File** to a `.dyeline` file and
edit that file with the designer:

```sh
dyeline-designer --file ~/looks/test.dyeline   # creates it, or adds a stop each run
```

While Look File is set, the file's look replaces the Exposure and Strength sliders, and the
plugin re-reads the file whenever it changes. Clicking Edit Look with a look file set edits the
file too.

## Contributing

Read [CONTRIBUTING.md](CONTRIBUTING.md) and [CLEANROOM.md](CLEANROOM.md) before your first pull
request. Every contribution must follow the clean-room rules.

## License

Code is licensed under the [Apache License 2.0](LICENSE). Film data, presets and documentation will
be licensed under CC-BY-4.0 as they are added; see [NOTICE](NOTICE).
