#!/bin/sh
# Installs Dyeline into the OFX plugin folder, replacing an earlier version.
#
#   ./install.sh                 into /usr/OFX/Plugins, the folder OFX hosts read
#   ./install.sh <folder>        into another folder, such as one set in OFX_PLUGIN_PATH
#   ./install.sh --uninstall [<folder>]
#
# Uses sudo when the folder isn't writable.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
uninstall=0
if [ "${1:-}" = "--uninstall" ]; then
    uninstall=1
    shift
fi
target=${1:-/usr/OFX/Plugins}

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
        echo "Removed Dyeline from $target. Your own looks in ~/.local/share/dyeline are kept."
    else
        echo "Dyeline is not installed in $target."
    fi
    exit 0
fi

run mkdir -p "$target"
run rm -rf "$target/Dyeline.ofx.bundle"
run cp -R "$here/Dyeline.ofx.bundle" "$target/"
echo "Installed Dyeline into $target. Restart your host to load it."
