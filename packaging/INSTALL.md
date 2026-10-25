# Installing Dyeline

Dyeline is one OFX plugin bundle, `Dyeline.ofx.bundle`, with the Look Designer and the
bundled looks inside it. Install it into your OFX plugin folder and restart your host
(DaVinci Resolve, Natron, Nuke, ...). It appears as **Dyeline** in the effects list. The
[user guide](https://github.com/xnqs/Dyeline/blob/main/docs/user-guide.md) explains how to use it.

## Windows

Run `Dyeline-<version>-windows-x64-setup.exe`. It installs into
`C:\Program Files\Common Files\OFX\Plugins`, puts **Dyeline Look Designer** in the Start
menu, and adds Dyeline to Apps > Installed apps, where you can uninstall it. From the .zip instead, copy `Dyeline.ofx.bundle` into that folder.

## macOS

Open `Dyeline-<version>-macos.pkg`. It installs into `/Library/OFX/Plugins`. The package isn't
signed yet, so macOS may refuse to open it at first: right-click it, choose Open, and confirm.
To uninstall, delete `/Library/OFX/Plugins/Dyeline.ofx.bundle`.

## Linux

Unpack the archive and run its installer:

```sh
tar xzf Dyeline-<version>-linux-x86_64.tar.gz
cd Dyeline-<version>-linux-x86_64
./install.sh                    # into /usr/OFX/Plugins, using sudo if needed
./install.sh ~/ofx-plugins      # or into a folder of your own (add it to OFX_PLUGIN_PATH)
./install.sh --uninstall        # removes it again
```

The installer also adds **Dyeline Look Designer** to your desktop's application menu, for your
user only.

The Look Designer needs a desktop session (X11 or Wayland). The GPU backends need the
vendor's driver: CUDA for NVIDIA, or an OpenCL driver (for AMD and Intel, Mesa's rusticl:
`sudo apt install mesa-opencl-icd`). Without them Dyeline renders on the CPU.

## Your looks

Looks you save in the Look Designer, and your favorites, live outside the plugin, so
reinstalling or uninstalling keeps them:

| OS | Folder |
| --- | --- |
| Windows | `%APPDATA%\Dyeline` |
| macOS | `~/Library/Application Support/Dyeline` |
| Linux | `~/.local/share/dyeline` |
