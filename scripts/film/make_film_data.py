#!/usr/bin/env python3
"""Builds core/src/film_stocks.inc, the tables behind Film Negative and Film Print.

    python3 scripts/film/make_film_data.py

Reads the datasheet data in core/film_data (see its README) and needs numpy and colour-science
(pip install colour-science). The generated file is committed, so building Dyeline needs
neither; run this again after changing core/film_data or the model below.

The model, per stock (docs/tools.md describes what it means for a look):

Negative. A 3x3 matrix takes ACEScg to the exposure of each of the film's three layers. It is
fitted, by least squares, to the layer exposures the stock's spectral sensitivities give for
the 24 ColorChecker patches lit by a blackbody at the stock's rated color temperature, with
the camera white balanced to that light. Its rows are scaled so grey exposes the layers
equally. Each layer's characteristic curve (density over log exposure, base density taken
away) then gives its density, sampled in stops around the exposure of an 18% grey card at the
stock's speed (log H = log10(12.5 / ISO) lux-seconds). Above the datasheet's last point the
curve keeps its final slope and rolls off over half a log exposure, as a shoulder would. The
result is stored as a scan: 0.18 * 10^((D - D_grey) / 0.6), in stops, so a grey card comes
back at 0.18 and a negative with a gamma of 0.6 keeps the image's mid-tone contrast.

Print. Its input is read as that scan, so a density difference from grey on the negative is
0.6 * log10(v / 0.18), and a denser negative passes less printer light. Printer light aims a
grey card at the stock's laboratory aim densities (LAD, Status A). Each print layer's curve
gives Status A densities, and a 3D table takes those densities to ACEScg: it finds the amounts
of the print's three dyes that measure those densities, shines a 5500 K projector through
them, sees the result with the CIE 1931 observer adapted to ACES white, and scales each
channel so the aim grey comes out at 0.18. The table holds log2 of the result.
"""

import json
import math
import os
import sys

import numpy as np
import colour

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
DATA = os.path.join(ROOT, "core", "film_data")
OUT = os.path.join(ROOT, "core", "src", "film_stocks.inc")

# In tool order. Looks store a stock as its index, so only ever add to the end.
NEGATIVES = [
    ("kodak-5219", "Kodak Vision3 500T 5219"),
    ("kodak-5213", "Kodak Vision3 200T 5213"),
    ("kodak-5207", "Kodak Vision3 250D 5207"),
    ("kodak-5203", "Kodak Vision3 50D 5203"),
    ("fuji-eterna-500", "Fuji Eterna 500T"),
    ("kodak-5277", "Kodak Vision 320T 5277"),
]
PRINTS = [
    ("kodak-2383", "Kodak Vision 2383"),
    ("kodak-2393", "Kodak Vision Premier 2393"),
    ("fuji-3513di", "Fuji Eterna-CP 3513DI"),
    ("kodak-5384", "Kodak 5384"),
]

WAVELENGTHS = np.arange(380.0, 781.0, 5.0)
DAYLIGHT_KELVIN = 5500.0
PROJECTOR_KELVIN = 5500.0
SCAN_GAMMA = 0.6
LOG10_2 = math.log10(2.0)
DEFAULT_LAD = [1.09, 1.06, 1.03]

# Negative curves: stops around grey.
NEGATIVE_STOPS = (-12.0, 12.0)
NEGATIVE_POINTS = 193
SHOULDER_WIDTH = 0.5  # log exposure
# Print curves and the density table.
PRINT_POINTS = 192
CUBE_POINTS = 17


def load(name):
    with open(os.path.join(DATA, name + ".json"), encoding="utf-8") as f:
        return json.load(f)


def sampled(pairs, wavelengths, outside=None):
    """A spectral curve given as [wavelength, value] pairs, at `wavelengths`. Past its ends it
    is `outside`, or holds its end values when that is None."""
    pairs = sorted(pairs)
    x = np.array([p[0] for p in pairs])
    y = np.array([p[1] for p in pairs])
    return np.interp(wavelengths, x, y, left=outside, right=outside)


def status_responses(name):
    """Status A or M responses (red, green, blue), linear, each summing to 1."""
    responses = []
    for pairs in load("densitometry")[name]:
        log_response = sampled(pairs, WAVELENGTHS, outside=-np.inf)
        response = np.where(np.isfinite(log_response), 10.0 ** log_response, 0.0)
        responses.append(response / response.sum())
    return np.array(responses)


def blackbody(kelvin):
    shape = colour.SpectralShape(WAVELENGTHS[0], WAVELENGTHS[-1], WAVELENGTHS[1] - WAVELENGTHS[0])
    return np.asarray(colour.sd_blackbody(kelvin, shape).values)


def cmfs():
    observer = colour.MSDS_CMFS["CIE 1931 2 Degree Standard Observer"]
    return np.stack([np.interp(WAVELENGTHS, observer.wavelengths, observer.values[:, k])
                     for k in range(3)])


ACESCG = colour.RGB_COLOURSPACES["ACEScg"]


def to_acescg(xyz, white_xyz):
    """XYZ seen under a light whose white is `white_xyz`, adapted to ACES white, as ACEScg."""
    target = colour.xy_to_XYZ(ACESCG.whitepoint)
    adapted = colour.adaptation.chromatic_adaptation_VonKries(
        xyz, white_xyz / white_xyz[1], target, transform="CAT02")
    return adapted @ ACESCG.matrix_XYZ_to_RGB.T


def luminance(rgb):
    return float(np.dot(ACESCG.matrix_RGB_to_XYZ[1], rgb))


def curve(points):
    """A characteristic curve's [log exposure, density] points, sorted, base density removed.
    Digitizing leaves small wiggles, so density is made to never fall as exposure rises."""
    points = sorted(points)
    x = np.array([p[0] for p in points])
    d = np.maximum.accumulate(np.array([p[1] for p in points]))
    return x, d - d[0]


def extended(x, d, at):
    """The curve at log exposures `at`: flat below its first point, a shoulder past its last."""
    out = np.interp(at, x, d)
    tail = max(1, len(x) // 6)
    slope = max(0.0, (d[-1] - d[-1 - tail]) / (x[-1] - x[-1 - tail]))
    past = at > x[-1]
    delta = at[past] - x[-1]
    out[past] = d[-1] + slope * SHOULDER_WIDTH * (1.0 - np.exp(-delta / SHOULDER_WIDTH))
    return out


def negative(stock_id):
    film = load(stock_id)
    kelvin = film.get("exposure_kelvin") or DAYLIGHT_KELVIN
    light = blackbody(kelvin)
    sensitivity = np.array([10.0 ** sampled(s, WAVELENGTHS, outside=-np.inf)
                            for s in film["log_sensitivity"]])
    patches = colour.SDS_COLOURCHECKERS["ColorChecker N Ohta"]
    reflectance = np.array([np.interp(WAVELENGTHS, sd.wavelengths, sd.values)
                            for sd in patches.values()])
    observer = cmfs()
    lit = reflectance * light
    exposures = lit @ sensitivity.T
    white = observer @ light
    xyz = (lit @ observer.T) / white[1]
    rgb = to_acescg(xyz, white)
    matrix = np.linalg.lstsq(rgb, exposures, rcond=None)[0].T
    matrix /= matrix.sum(axis=1, keepdims=True)

    grey_log_h = math.log10(12.5 / film["iso"])
    stops = np.linspace(NEGATIVE_STOPS[0], NEGATIVE_STOPS[1], NEGATIVE_POINTS)
    scan = []
    for points in film["sensiometric_curve"]:
        x, d = curve(points)
        if film.get("exposure_base", 10) == 2:
            x = grey_log_h + x * LOG10_2  # stops around grey to log H
        density = extended(x, d, grey_log_h + stops * LOG10_2)
        grey = extended(x, d, np.array([grey_log_h]))[0]
        scan.append((density - grey) / (SCAN_GAMMA * LOG10_2))
    return matrix, stops, np.array(scan).T


def solve_dyes(target, dyes, response):
    """Dye amounts (>= 0) whose transmission measures `target` densities through `response`."""
    amounts = np.maximum(target, 0.0).copy()
    for _ in range(60):
        transmission = 10.0 ** -(amounts @ dyes)
        passed = np.maximum(response @ transmission, 1e-30)
        measured = -np.log10(passed)
        error = measured - target
        if np.max(np.abs(error)) < 1e-6:
            break
        jacobian = (response * transmission) @ dyes.T / passed[:, None]
        # Least squares, so a density the dyes can't reach is approached instead of failing.
        step = np.linalg.lstsq(jacobian, error, rcond=None)[0]
        amounts = np.clip(amounts - step, 0.0, 20.0)
    return amounts


def print_stock(stock_id, status_a):
    film = load(stock_id)
    curves = [curve(points) for points in film["sensiometric_curve"]]
    lo = min(x[0] for x, _ in curves)
    hi = max(x[-1] for x, _ in curves)
    log_e = np.linspace(lo, hi, PRINT_POINTS)
    densities = np.array([np.interp(log_e, x, d) for x, d in curves]).T
    base = [sorted(points)[0][1] for points in film["sensiometric_curve"]]
    lad = film.get("lad") or DEFAULT_LAD
    lad_density = np.array([max(a - b, 0.05) for a, b in zip(lad, base)])
    d_max = densities.max(axis=0)

    # Dyes, each scaled to measure a density of 1 through its own channel.
    dyes = np.array([sampled(s, WAVELENGTHS) for s in film["spectral_density"]])
    for k in range(3):
        dyes[k] /= -np.log10(status_a[k] @ 10.0 ** -dyes[k])
    projector = blackbody(PROJECTOR_KELVIN)
    observer = cmfs()
    white = observer @ projector

    def display(target):
        transmission = 10.0 ** -(solve_dyes(target, dyes, status_a) @ dyes)
        xyz = observer @ (transmission * projector) / white[1]
        return to_acescg(xyz, white)

    # The clear base is white. Grey is the LAD's brightness, made neutral: the lab's aim is a
    # neutral grey, which these approximations miss by a little.
    target = luminance(display(lad_density))
    aim_density = lad_density.copy()
    for _ in range(50):
        rgb = display(aim_density)
        aim_density = aim_density + np.log10(np.maximum(rgb, 1e-9) / target)
    aim = np.array([np.interp(t, d, x) for t, (x, d) in zip(aim_density, curves)])
    # Scaling in stops around grey so the clear base lands at 1.0.
    scale = math.log2(1.0 / 0.18) / math.log2(1.0 / target)

    cube = np.zeros((CUBE_POINTS, CUBE_POINTS, CUBE_POINTS, 3))
    axes = [np.linspace(0.0, m, CUBE_POINTS) for m in d_max]
    for b in range(CUBE_POINTS):
        for g in range(CUBE_POINTS):
            for r in range(CUBE_POINTS):
                rgb = display(np.array([axes[0][r], axes[1][g], axes[2][b]]))
                cube[b, g, r] = np.log2(np.maximum(rgb, 2.0 ** -30) / target) * scale
    return log_e, densities, aim, d_max, cube


def c_float(value, digits=6):
    """`value` as a C++ float literal."""
    text = f"{value:.{digits}g}"
    if not any(c in text for c in ".en"):
        text += ".0"
    return text + "f"


def floats(values, indent="    ", per_line=8):
    flat = [c_float(v, 5) for v in np.asarray(values, dtype=float).ravel()]
    return "\n".join(indent + ", ".join(flat[i:i + per_line]) + ","
                     for i in range(0, len(flat), per_line))


def c_name(stock_id):
    return stock_id.replace("-", "_")


def main():
    status_a = status_responses("status_a")
    out = ["// Generated by scripts/film/make_film_data.py from core/film_data. Do not edit.",
           "// Datasheet data digitized by spectral_film_lut (MIT license, Jan Lohse); see",
           "// core/film_data/README.md.", ""]
    negatives = []
    for stock_id, label in NEGATIVES:
        matrix, stops, scan = negative(stock_id)
        name = c_name(stock_id)
        out.append(f"const float k_{name}_scan[] = {{")
        out.append(floats(scan))
        out.append("};")
        negatives.append(
            f"    {{\"{stock_id}\", \"{label}\",\n"
            f"     {{{', '.join(c_float(v) for v in matrix.ravel())}}},\n"
            f"     {c_float(stops[0])}, {c_float((len(stops) - 1) / (stops[-1] - stops[0]))}, "
            f"{len(stops)}, k_{name}_scan}},")
        print(stock_id, "matrix", np.round(matrix, 3).tolist(), file=sys.stderr)
    prints = []
    for stock_id, label in PRINTS:
        log_e, densities, aim, d_max, cube = print_stock(stock_id, status_a)
        name = c_name(stock_id)
        out.append(f"const float k_{name}_curves[] = {{")
        out.append(floats(densities))
        out.append("};")
        out.append(f"const float k_{name}_display[] = {{")
        out.append(floats(cube))
        out.append("};")
        prints.append(
            f"    {{\"{stock_id}\", \"{label}\",\n"
            f"     {c_float(log_e[0])}, {c_float((len(log_e) - 1) / (log_e[-1] - log_e[0]))}, "
            f"{len(log_e)}, k_{name}_curves,\n"
            f"     {{{', '.join(c_float(v) for v in aim)}}},\n"
            f"     {{{', '.join(c_float(v) for v in d_max)}}},\n"
            f"     {CUBE_POINTS}, k_{name}_display}},")
        print(stock_id, "aim", np.round(aim, 3).tolist(), "d_max", np.round(d_max, 2).tolist(),
              file=sys.stderr)
    out.append("")
    out.append("const NegativeStock kNegativeStocks[] = {")
    out.extend(negatives)
    out.append("};")
    out.append("")
    out.append("const PrintStock kPrintStocks[] = {")
    out.extend(prints)
    out.append("};")
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
