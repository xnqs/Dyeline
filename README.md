# Dyeline

Dyeline is a free, open-source OpenFX (OFX) plugin for building film-style looks from a chain of
color, lens and film tools, with GPU acceleration. It is a clean-room project inspired by the
workflow of Magic Bullet Looks.

> Dyeline is not affiliated with, endorsed by or connected to Maxon or Red Giant. "Magic Bullet
> Looks" is their trademark and is named here only to describe what Dyeline is an alternative to.

**Status:** Milestone 1 in progress. The plugin renders a look, a chain of tools, on the CPU or on
the GPU through CUDA (Windows, Linux), OpenCL (Windows, Linux) or Metal (macOS) when the host
provides one. Exposure is the only tool so far, and the Look Designer is still a stand-in (see
below). It is not useful for real work yet.

## Looks

A look is a chain of tools plus its color settings, saved as a `.dyeline` file: UTF-8 JSON
described by [schema/dyeline-look-1.schema.json](schema/dyeline-look-1.schema.json). The plugin
keeps the same JSON in a hidden parameter, so a preset and a clip's grade are one thing.

```json
{
  "formatVersion": 1,
  "meta": {"name": "Brighter", "tags": [], "author": "", "license": "CC-BY-4.0"},
  "color": {"mode": "tone", "input": "rec709", "output": "same"},
  "chain": [
    {"tool": "exposure", "toolVersion": 1, "enabled": true, "pinned": false, "strength": 1.0,
     "params": {"stops": 1.0}}
  ]
}
```

Each tool versions its own parameters, so old looks keep loading as tools change. A tool this
build doesn't know (or a newer version of one) is kept in the look and bypassed, with a warning,
never dropped. In the host, **Strength** blends the whole look with the untouched image.

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

The bundle ships `dyeline-designer` next to the plugin binary. For now it is a stub with no
window: it raises the look's Exposure tool by one stop (adding one if the look has none) and
hands the look back. There are two ways to link it to the plugin, depending on the host.

**Edit Look (Natron, Resolve).** Click **Edit Look...** in the plugin's controls. The host
freezes while the designer runs (two seconds), then the image gets a stop brighter and the
**Designer** line reports how long the round trip took. Undo restores the old look in one step.
To check how a host copes with a long edit, start it with `DYELINE_DESIGNER_DELAY` set to the
number of seconds the stub should wait, for example `DYELINE_DESIGNER_DELAY=60 natron`.

**Look File (EffectCraft, and any host).** Some hosts never take parameter values back from a
plugin, so Edit Look does nothing there. Instead, set **Look File** to a `.dyeline` file and
edit that file with the designer:

```sh
dyeline-designer --file ~/looks/test.dyeline   # creates it, or adds a stop each run
```

While Look File is set, the file's look is rendered instead of the one saved with the project,
and the plugin re-reads the file whenever it changes. Hosts that cache frames by parameter
values alone, such as EffectCraft, keep showing the old frame after the file changes: add one to
**Reload Look File** to make them render again.

## Contributing

Read [CONTRIBUTING.md](CONTRIBUTING.md) and [CLEANROOM.md](CLEANROOM.md) before your first pull
request. Every contribution must follow the clean-room rules.

## License

Code is licensed under the [Apache License 2.0](LICENSE). Film data, presets and documentation will
be licensed under CC-BY-4.0 as they are added; see [NOTICE](NOTICE).
