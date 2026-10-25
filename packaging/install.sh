#!/bin/sh
# Installs Dyeline into the OFX plugin folder, replacing an earlier version.
#
#   ./install.sh                 into /usr/OFX/Plugins, the folder OFX hosts read
#   ./install.sh <folder>        into another folder, such as one set in OFX_PLUGIN_PATH
#   ./install.sh --uninstall [<folder>]
#
# Uses sudo when the folder isn't writable. Also adds the Look Designer to your desktop's
# application menu, for your user only.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
uninstall=0
if [ "${1:-}" = "--uninstall" ]; then
    uninstall=1
    shift
fi
target=${1:-/usr/OFX/Plugins}
menu_entry="${XDG_DATA_HOME:-$HOME/.local/share}/applications/dyeline-designer.desktop"

run() {
    if [ -w "$target" ] || { [ ! -e "$target" ] && [ -w "$(dirname "$target")" ]; }; then
        "$@"
    else
        sudo "$@"
    fi
}

if [ "$uninstall" -eq 1 ]; then
    if [ -d "$target/Dyeline.ofx.bundle" ]; then
        run rm -rf "$target/Dyeline.ofx.bundle"
        rm -f "$menu_entry"
        echo "Removed Dyeline from $target. Your own looks in ~/.local/share/dyeline are kept."
    else
        echo "Dyeline is not installed in $target."
    fi
    exit 0
fi

run mkdir -p "$target"
run rm -rf "$target/Dyeline.ofx.bundle"
run cp -R "$here/Dyeline.ofx.bundle" "$target/"

# The menu entry starts the designer on a new look in ~/.local/share/dyeline/looks.
designer=$(ls "$target"/Dyeline.ofx.bundle/Contents/Linux-*/dyeline-designer 2>/dev/null | head -n 1)
if [ -n "$designer" ]; then
    mkdir -p "$(dirname "$menu_entry")"
    cat > "$menu_entry" <<EOF
[Desktop Entry]
Type=Application
Name=Dyeline Look Designer
Comment=Make a look for the Dyeline OFX plugin
Exec="$designer"
Terminal=false
Categories=Graphics;AudioVideo;Video;
EOF
fi
echo "Installed Dyeline into $target. Restart your host to load it."
