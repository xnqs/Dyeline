#!/usr/bin/env python3
"""Renders test looks through Dyeline inside Natron and compares each with Dyeline's own CPU
reference (dyeline_render_frame), so the plugin is checked end to end in a real OFX host:
loading, parameters, regions of interest, tiles and the host's image buffers.

    run.py --natron <NatronRenderer> --plugin-dir <dir holding Dyeline.ofx.bundle>
           --reference <dyeline_render_frame> [--work <dir>]

Needs numpy and OpenEXR (pip install numpy OpenEXR).
"""

import argparse
import glob
import json
import os
import struct
import subprocess
import sys
import tempfile

import numpy as np
import OpenEXR

HERE = os.path.dirname(os.path.abspath(__file__))

# Natron's default project format; its first Read node may also set it from the image, and
# both give this frame.
WIDTH = 1920
HEIGHT = 1080

# Natron writes half-float EXRs, so compare to about half's precision.
TOLERANCE = 2e-3


def tool(name, **params):
    return {"tool": name, "params": params}


LOOKS = {
    "exposure": [tool("exposure", stops=1.0)],
    "grade": [
        tool("contrast", contrast=1.3),
        tool("three-way", **{"shadows-hue": 210, "shadows-amount": 0.4,
                             "highlights-hue": 40, "highlights-amount": 0.3}),
        tool("hue-saturation", saturation=1.2),
        tool("lens-vignette", amount=-1.0, **{"center-x": 0.1}),
    ],
    "spatial": [
        tool("diffusion", amount=0.5, size=2.0, style=1),
        tool("halation", amount=0.8, threshold=1.0),
        tool("local-contrast", amount=0.4),
        tool("streaks", amount=0.5, length=6.0),
    ],
    "lens": [
        tool("lens-distortion", amount=0.5, zoom=1.1, **{"center-x": 0.2}),
        tool("chromatic-aberration", red=0.4, blue=-0.3, fringe=0.1),
        tool("swing-tilt", blur=1.5, angle=15.0),
        tool("edge-softness", blur=1.0),
        tool("diffusion", amount=0.3, size=1.0),
    ],
}


def test_image():
    """RGBA, top row first: ramps, a grid of lines and bright spots, fully opaque."""
    y, x = np.mgrid[0:HEIGHT, 0:WIDTH].astype(np.float32)
    rgba = np.empty((HEIGHT, WIDTH, 4), np.float32)
    rgba[..., 0] = 0.02 + 0.6 * x / WIDTH
    rgba[..., 1] = 0.02 + 0.5 * y / HEIGHT
    rgba[..., 2] = 0.1 + 0.3 * np.abs(np.sin(x / 70.0 + y / 50.0))
    lines = (x.astype(int) % 120 == 60) | (y.astype(int) % 120 == 60)
    rgba[lines, :3] = 0.8
    spots = (x.astype(int) % 240 == 120) & (y.astype(int) % 240 == 120)
    rgba[spots, :3] = 50.0
    rgba[..., 3] = 1.0
    return rgba


def write_exr(path, rgba):
    header = {"compression": OpenEXR.ZIP_COMPRESSION, "type": OpenEXR.scanlineimage}
    with OpenEXR.File(header, {"RGBA": rgba}) as f:
        f.write(path)


def read_exr(path):
    with OpenEXR.File(path) as f:
        channels = f.channels()
        if "RGBA" in channels:
            return channels["RGBA"].pixels.astype(np.float32)
        return np.stack([channels[c].pixels.astype(np.float32) for c in "RGBA"], axis=-1)


def write_frame(path, rgba):
    """A Dyeline frame file (core/include/dyeline/frame_file.h): bottom row first."""
    with open(path, "wb") as f:
        f.write(b"DLFRAME1")
        f.write(struct.pack("<6i", WIDTH, HEIGHT, 0, 0, 0, 1))
        f.write(struct.pack("<6f", 1.0, 1.0, 0.0, 0.0, float(WIDTH), float(HEIGHT)))
        f.write(np.ascontiguousarray(rgba[::-1], dtype="<f4").tobytes())


def read_frame(path):
    with open(path, "rb") as f:
        data = f.read()
    width, height = struct.unpack_from("<2i", data, 8)
    pixels = np.frombuffer(data, "<f4", offset=8 + 6 * 4 + 6 * 4)
    return pixels.reshape(height, width, 4)[::-1]


def compare(name, got, want):
    if got.shape != want.shape:
        return "%s: Natron wrote %s, expected %s" % (name, got.shape, want.shape)
    error = np.abs(got - want) / np.maximum(1.0, np.abs(want))
    worst = np.unravel_index(np.argmax(error), error.shape)
    print("%-10s max relative error %.2e at row %d, column %d, channel %d (got %.5f, want %.5f)"
          % (name, error[worst], worst[0], worst[1], worst[2], got[worst], want[worst]))
    if error[worst] > TOLERANCE:
        bad = np.argwhere(error.max(axis=2) > TOLERANCE)
        print("  %d pixels differ, rows %d-%d, columns %d-%d; at the worst, got RGBA %s, want %s"
              % (len(bad), bad[:, 0].min(), bad[:, 0].max(), bad[:, 1].min(), bad[:, 1].max(),
                 np.round(got[worst[0], worst[1]], 4), np.round(want[worst[0], worst[1]], 4)))
        return "%s differs from the CPU reference" % name
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--natron", required=True)
    parser.add_argument("--plugin-dir", required=True)
    parser.add_argument("--reference", required=True)
    parser.add_argument("--work", default=None)
    args = parser.parse_args()

    work = os.path.abspath(args.work or tempfile.mkdtemp(prefix="dyeline-natron-"))
    os.makedirs(work, exist_ok=True)
    source = test_image()
    source_exr = os.path.join(work, "source.exr")
    source_frame = os.path.join(work, "source.frame")
    write_exr(source_exr, source)
    write_frame(source_frame, source)

    env = dict(os.environ)
    env["OFX_PLUGIN_PATH"] = os.path.abspath(args.plugin_dir)
    env.setdefault("QT_QPA_PLATFORM", "offscreen")

    failures = []
    for name, chain in LOOKS.items():
        look_path = os.path.join(work, name + ".dyeline")
        with open(look_path, "w") as f:
            json.dump({"formatVersion": 1, "meta": {"name": name}, "chain": chain}, f)
        reference = os.path.join(work, name + ".frame")
        subprocess.run([args.reference, look_path, source_frame, reference], check=True)
        want = read_frame(reference)

        for gpu in ("cpu", "own-gpu"):
            label = "%s/%s" % (name, gpu)
            out_pattern = os.path.join(work, "%s-%s.####.exr" % (name, gpu))
            run_env = dict(env)
            run_env.update(DYELINE_TEST_IN=source_exr, DYELINE_TEST_OUT=out_pattern,
                           DYELINE_TEST_LOOK=look_path)
            if gpu == "own-gpu":
                run_env["DYELINE_OPENCL_DEVICE"] = "any"  # PoCL on CI runners
            command = [args.natron, "-w", "TestWrite", "1-1", os.path.join(HERE, "graph.py")]
            result = subprocess.run(command, env=run_env, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, universal_newlines=True)
            outputs = sorted(glob.glob(out_pattern.replace("####", "*")))
            if result.returncode != 0 or not outputs:
                print(result.stdout)
                failures.append("%s: Natron failed (exit %d)" % (label, result.returncode))
                continue
            problem = compare(label, read_exr(outputs[-1]), want)
            if problem:
                print("\n".join("  " + line for line in result.stdout.splitlines()
                                 if "project format" in line or "rror" in line))
                failures.append(problem)

    if failures:
        print("\n".join(["", "FAILED:"] + failures))
        return 1
    print("\nAll looks match the CPU reference in Natron.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
