# Dyeline

Dyeline is a free, open-source OpenFX (OFX) plugin for building film-style looks from a chain of
color, lens and film tools, with GPU acceleration. It is a clean-room project inspired by the
workflow of Magic Bullet Looks.

> Dyeline is not affiliated with, endorsed by or connected to Maxon or Red Giant. "Magic Bullet
> Looks" is their trademark and is named here only to describe what Dyeline is an alternative to.

**Status:** early scaffolding (Milestone 0). The plugin currently offers one tool, Exposure, rendered
on the CPU, or on the GPU through CUDA (Windows, Linux) or Metal (macOS) when the host provides one.
It is not useful for real work yet.

## Hosts

Dyeline targets OFX hosts on Windows, macOS and Linux. First targets:

- DaVinci Resolve
- Natron
- EffectCraft (OpenFX fork)

## Building

Requirements: CMake 3.24+, a C++17 compiler, and git (the OpenFX SDK and GoogleTest are fetched at
configure time). The CUDA backend is built when a CUDA toolkit (nvcc) is found; turn it off with
`-DDYELINE_ENABLE_CUDA=OFF`. The Metal backend is always built on macOS.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The plugin bundle is written to `build/Dyeline.ofx.bundle`. Copy that folder into your host's OFX
plugin folder, or run `cmake --install build` to copy it to the standard location:

| OS | OFX plugin folder |
| --- | --- |
| Windows | `C:\Program Files\Common Files\OFX\Plugins` |
| macOS | `/Library/OFX/Plugins` |
| Linux | `/usr/OFX/Plugins` |

## Contributing

Read [CONTRIBUTING.md](CONTRIBUTING.md) and [CLEANROOM.md](CLEANROOM.md) before your first pull
request. Every contribution must follow the clean-room rules.

## License

Code is licensed under the [Apache License 2.0](LICENSE). Film data, presets and documentation will
be licensed under CC-BY-4.0 as they are added; see [NOTICE](NOTICE).
