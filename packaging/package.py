#!/usr/bin/env python3
"""Packages a built Dyeline.ofx.bundle for release.

    python packaging/package.py --build build --out dist

Makes, for the OS it runs on:
  Linux    Dyeline-<version>-linux-<arch>.tar.gz, with install.sh
  macOS    Dyeline-<version>-macos.pkg (installs into /Library/OFX/Plugins), and a .zip
  Windows  Dyeline-<version>-windows-x64-setup.exe when NSIS (makensis) is found, and a .zip

Each holds the bundle, LICENSE, NOTICE and INSTALL.md. Uses only the Python standard library
and, on macOS and Windows, the system's own packaging tools.
"""

import argparse
import os
import platform
import re
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HERE = os.path.dirname(os.path.abspath(__file__))
BUNDLE = "Dyeline.ofx.bundle"
DOCS = ["LICENSE", "NOTICE"]


def project_version():
    with open(os.path.join(ROOT, "CMakeLists.txt"), encoding="utf-8") as f:
        match = re.search(r"project\(Dyeline VERSION ([0-9.]+)", f.read())
    if not match:
        sys.exit("package.py: no version in CMakeLists.txt")
    return match.group(1)


def check_bundle(bundle, arch_dir, names):
    for name in names:
        path = os.path.join(bundle, "Contents", arch_dir, name)
        if not os.path.isfile(path):
            sys.exit(f"package.py: {path} is missing; build first")
    if not os.path.isdir(os.path.join(bundle, "Contents", "Resources", "looks")):
        sys.exit("package.py: the bundled looks are missing; build first")


def stage(bundle, staging, extra=()):
    """Copies the bundle and the docs into `staging`, keeping file modes."""
    shutil.copytree(bundle, os.path.join(staging, BUNDLE), symlinks=True)
    for doc in DOCS:
        shutil.copy2(os.path.join(ROOT, doc), staging)
    shutil.copy2(os.path.join(HERE, "INSTALL.md"), staging)
    for path in extra:
        shutil.copy2(path, staging)


def make_zip(source_dir, top, out_path):
    with zipfile.ZipFile(out_path, "w", zipfile.ZIP_DEFLATED) as z:
        for folder, _, files in os.walk(source_dir):
            for name in sorted(files):
                full = os.path.join(folder, name)
                arc = os.path.join(top, os.path.relpath(full, source_dir))
                info = zipfile.ZipInfo.from_file(full, arc)
                info.compress_type = zipfile.ZIP_DEFLATED
                with open(full, "rb") as f:
                    z.writestr(info, f.read())


def linux(bundle, out, version):
    arch = "arm64" if platform.machine() in ("aarch64", "arm64") else "x86_64"
    arch_dir = "Linux-arm-64" if arch == "arm64" else "Linux-x86-64"
    check_bundle(bundle, arch_dir, ["Dyeline.ofx", "dyeline-designer"])
    name = f"Dyeline-{version}-linux-{arch}"
    with tempfile.TemporaryDirectory() as tmp:
        staging = os.path.join(tmp, name)
        os.makedirs(staging)
        stage(bundle, staging, [os.path.join(HERE, "install.sh")])
        os.chmod(os.path.join(staging, "install.sh"), 0o755)
        path = os.path.join(out, name + ".tar.gz")
        with tarfile.open(path, "w:gz") as tar:
            tar.add(staging, arcname=name, filter=normalize)
    return [path]


def normalize(info):
    # Owned by root in the archive, whoever built it, with plain read and execute bits.
    info.uid = info.gid = 0
    info.uname = info.gname = "root"
    info.mode = 0o755 if info.isdir() or info.mode & stat.S_IXUSR else 0o644
    return info


def macos(bundle, out, version):
    check_bundle(bundle, "MacOS", ["Dyeline.ofx", "dyeline-designer"])
    name = f"Dyeline-{version}-macos"
    results = []
    with tempfile.TemporaryDirectory() as tmp:
        # The package installs the bundle alone into /Library/OFX/Plugins.
        root = os.path.join(tmp, "root")
        os.makedirs(root)
        shutil.copytree(bundle, os.path.join(root, BUNDLE), symlinks=True)
        component = os.path.join(tmp, "Dyeline.pkg")
        subprocess.check_call(["pkgbuild", "--root", root, "--install-location", "/Library/OFX/Plugins",
                               "--identifier", "org.dyeline.ofx", "--version", version, component])
        resources = os.path.join(tmp, "resources")
        os.makedirs(resources)
        shutil.copy2(os.path.join(ROOT, "LICENSE"), os.path.join(resources, "LICENSE.txt"))
        distribution = os.path.join(tmp, "distribution.xml")
        with open(os.path.join(HERE, "macos-distribution.xml"), encoding="utf-8") as f:
            text = f.read().replace("@VERSION@", version)
        with open(distribution, "w", encoding="utf-8") as f:
            f.write(text)
        pkg = os.path.join(out, name + ".pkg")
        subprocess.check_call(["productbuild", "--distribution", distribution, "--package-path", tmp,
                               "--resources", resources, pkg])
        results.append(pkg)

        staging = os.path.join(tmp, name)
        os.makedirs(staging)
        stage(bundle, staging)
        archive = os.path.join(out, name + ".zip")
        # ditto keeps macOS metadata and permissions the way Finder expects.
        subprocess.check_call(["ditto", "-c", "-k", "--keepParent", staging, archive])
        results.append(archive)
    return results


def windows(bundle, out, version, require_installer):
    check_bundle(bundle, "Win64", ["Dyeline.ofx", "dyeline-designer.exe"])
    name = f"Dyeline-{version}-windows-x64"
    results = []
    with tempfile.TemporaryDirectory() as tmp:
        staging = os.path.join(tmp, name)
        os.makedirs(staging)
        stage(bundle, staging)
        archive = os.path.join(out, name + ".zip")
        make_zip(staging, name, archive)
        results.append(archive)

        makensis = shutil.which("makensis") or next(
            (p for p in (r"C:\Program Files (x86)\NSIS\makensis.exe", r"C:\Program Files\NSIS\makensis.exe")
             if os.path.isfile(p)), None)
        if makensis is None:
            if require_installer:
                sys.exit("package.py: NSIS (makensis) not found")
            print("package.py: NSIS not found, so no installer; the .zip is made", file=sys.stderr)
            return results
        setup = os.path.join(out, name + "-setup.exe")
        subprocess.check_call([makensis, "/V2", f"/DVERSION={version}", f"/DSTAGING={staging}",
                               f"/DLICENSE={os.path.join(ROOT, 'LICENSE')}", f"/DOUTFILE={setup}",
                               os.path.join(HERE, "windows-installer.nsi")])
        results.append(setup)
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build", default="build", help="the build folder holding Dyeline.ofx.bundle")
    parser.add_argument("--out", default="dist", help="where the packages go")
    parser.add_argument("--version", default=None, help="defaults to the version in CMakeLists.txt")
    parser.add_argument("--require-installer", action="store_true",
                        help="fail on Windows when NSIS is missing, instead of making only the .zip")
    args = parser.parse_args()

    bundle = os.path.join(os.path.abspath(args.build), BUNDLE)
    if not os.path.isdir(bundle):
        sys.exit(f"package.py: {bundle} not found; build first")
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    version = args.version or project_version()

    system = platform.system()
    if system == "Linux":
        made = linux(bundle, out, version)
    elif system == "Darwin":
        made = macos(bundle, out, version)
    elif system == "Windows":
        made = windows(bundle, out, version, args.require_installer)
    else:
        sys.exit(f"package.py: no packaging for {system}")
    for path in made:
        print(path)


if __name__ == "__main__":
    main()
