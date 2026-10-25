# Dyeline

Dyeline is a free, open-source OpenFX (OFX) plugin for building film-style looks from a chain of
color, lens and film tools, with GPU acceleration. It is a clean-room project inspired by the
workflow of Magic Bullet Looks.

> Dyeline is not affiliated with, endorsed by or connected to Maxon or Red Giant. "Magic Bullet
> Looks" is their trademark and is named here only to describe what Dyeline is an alternative to.

**Status:** Milestone 2 in progress, after the 0.1 alpha. The plugin renders a look, a chain of
tools, on the GPU through CUDA (Windows, Linux), OpenCL (Windows, Linux) or Metal (macOS) when
the host provides one, else on its own OpenCL or Metal device, else on the CPU. The tools so far are Exposure, Contrast, S Curve, Hue/Saturation, Black & White, Channel
Mixer, Shadows/Highlights, Highlight Shoulder, 3-Way Grade (with HSL and Sat EQ), 4-Way Wheels,
Blockbuster, Duotone, LUT, Film Negative, Film Print, Bleach Bypass, 2-Strip, Gradient Tint,
Grad Exposure, Spot Exposure, Fill Light, Spot Fill, Color Filter, Lens Vignette, Grain,
Diffusion Filter, Local Contrast, Telecine Net and Halation (see
[docs/tools.md](docs/tools.md)). The Look Designer edits the chain, the tools and the color
settings, with a preview of the host's current frame, its scopes, and a looks browser with 22
bundled looks. It is not useful for real work yet.

## Looks

A look is a chain of tools plus its color settings, saved as a `.dyeline` file: UTF-8 JSON
described by [schema/dyeline-look-1.schema.json](schema/dyeline-look-1.schema.json). The plugin
keeps the same JSON in a hidden parameter, so a preset and a clip's grade are one thing.

```json
{
  "formatVersion": 1,
  "meta": {"name": "Brighter", "tags": [], "author": "", "license": "CC-BY-4.0"},
  "color": {"mode": "tone", "input": "linear", "output": "same"},
  "chain": [
    {"tool": "exposure", "toolVersion": 1, "enabled": true, "pinned": false, "strength": 1.0,
     "params": {"stops": 1.0}}
  ]
}
```

Each tool versions its own parameters, so old looks keep loading as tools change. A tool this
build doesn't know (or a newer version of one) is kept in the look and bypassed, with a warning,
never dropped. In the host, **Strength** blends the whole look with the untouched image.

The `color` settings say what space the footage is in and what to output; tools always work on
scene-linear ACEScg. The defaults change nothing, which suits Natron and Nuke. See
[docs/color.md](docs/color.md) for the modes and the built-in camera spaces.

## Hosts

Dyeline targets OFX hosts on Windows, macOS and Linux. First targets:

- DaVinci Resolve
- Natron
- EffectCraft (OpenFX fork)

New to Dyeline? Start with the [user guide](docs/user-guide.md).

## Installing

Each release on GitHub has an installer for Windows (`-setup.exe`), a package for macOS
(`.pkg`) and an archive with `install.sh` for Linux, each also as a plain .zip of the bundle.
[packaging/INSTALL.md](packaging/INSTALL.md) has the steps, and where your own looks are kept.

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

`python3 packaging/package.py --build build` makes the same packages a release has, for the OS
it runs on, in `dist/`. Pushing a tag such as `v0.2.0` builds them on all three in CI and opens a
draft GitHub release with them.

## The Look Designer

The bundle ships `dyeline-designer` next to the plugin binary: a window for editing a look.

- **Chain** (left): the look's tools, in the order they run. The checkbox bypasses a tool, drag
  a tool to reorder it, and right-click it to move, duplicate or remove it. **Add tool** puts a
  new tool after the selected one.
- **Preview** (top right): the host's current frame with the look applied, updated as you
  edit. While you drag a slider it follows at a smaller size, and the full-size render comes
  when you let go. **Original** (or the B key) shows the frame without the look. Linear looks are shown
  through the sRGB curve, display and log spaces as they are; **View** overrides that. The
  plugin hands over the frame when you click Edit Look, shrunk to fit 1920 x 1080; a designer
  opened any other way has no frame to show.
- **Scopes** (beside the preview, toggled with the checkbox or the S key): an RGB parade, and
  a vectorscope with the 75% color bar targets and the skin tone line. Both read the preview
  as it is shown on screen.
- **Looks** (the left pane's second tab): the bundled looks, grouped by mood, and your own,
  each as a thumbnail of the current frame. Click one to apply it; it replaces the chain and
  name but keeps your color settings, and Undo takes it back. Search matches names, tags and
  folders. Right-click a look to add it to your favorites. **Save to my looks** keeps the
  look being edited in your own folder (Linux `~/.local/share/dyeline/looks`, macOS
  `~/Library/Application Support/Dyeline/looks`, Windows `%APPDATA%\Dyeline\looks`), where
  subfolders show as groups. See [looks/README.md](looks/README.md) for the bundled set.
- **Tool** (bottom right): the selected tool's Strength and parameters. Double-click a control,
  or right-click it, to put it back to its default.
- **Top bar**: the look's name and its color settings (see [docs/color.md](docs/color.md)).
- **Export .cube** (bottom bar): bakes the look into a 3D LUT (17, 33 or 65 points) for
  software and cameras that can't run Dyeline. The LUT takes values in the look's input space
  and gives values in its output space, so set the input to the footage's log space: a
  scene-linear input clips above 1. Tools that depend on where a pixel is, or on its
  neighbors (the gradient and spot tools, Lens Vignette, Grain, Diffusion Filter, Local Contrast, Telecine Net, Halation), can't be held by a LUT and are
  left out; the dialog lists them and the file's comments name them.
- Ctrl+Z and Ctrl+Shift+Z (Cmd on macOS) undo and redo.

A tool this build doesn't know stays in the look, shown as missing, and is bypassed.

There are two ways to link the designer to the plugin, depending on the host.

**Edit Look (Natron, Resolve).** Click **Edit Look...** in the plugin's controls. The host waits
while the designer is open; **Apply** hands the edited look back and **Cancel** leaves it as it
was. Undo in the host restores the old look in one step.

**Look File (EffectCraft, and any host).** Some hosts never take parameter values back from a
plugin, so Edit Look does nothing there. Instead, set **Look File** to a `.dyeline` file and
edit that file with the designer:

```sh
dyeline-designer --file ~/looks/test.dyeline   # creates it if it doesn't exist
```

**Save** (Ctrl+S) writes the file. While Look File is set, the file's look is rendered instead of
the one saved with the project, and the plugin re-reads the file whenever it changes. Hosts that
cache frames by parameter values alone, such as EffectCraft, keep showing the old frame after the
file changes: add one to **Reload Look File** to make them render again.

The designer is built with Dear ImGui and SDL3, fetched at configure time. On Linux, SDL needs
the X11 and Wayland development packages (on Debian/Ubuntu: `libx11-dev libxext-dev
libxcursor-dev libxi-dev libxrandr-dev libxss-dev libxtst-dev libxfixes-dev libxkbcommon-dev
libwayland-dev wayland-protocols libdecor-0-dev libegl-dev libgl-dev libdbus-1-dev
libudev-dev`). `-DDYELINE_BUILD_DESIGNER=OFF` builds a windowless stand-in instead.

## Contributing

Read [CONTRIBUTING.md](CONTRIBUTING.md) and [CLEANROOM.md](CLEANROOM.md) before your first pull
request. Every contribution must follow the clean-room rules.

## License

Code is licensed under the [Apache License 2.0](LICENSE). Film data, presets and documentation will
be licensed under CC-BY-4.0 as they are added; see [NOTICE](NOTICE).
