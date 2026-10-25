#!/usr/bin/env python3
"""Copies film stock data from a checkout of spectral_film_lut into core/film_data.

spectral_film_lut (https://github.com/JanLohse/spectral_film_lut, MIT license, by Jan Lohse)
digitized the published datasheets of many film stocks. Its data files are Python modules
calling FilmData(...) with literal values. This script reads those literals without running
the modules and writes the parts Dyeline uses as JSON, one file per stock, plus the Status A
and Status M densitometer responses.

    scripts/film/import_spectral_film_lut.py <spectral_film_lut checkout>

Run it again only to add a stock or pick up corrected data; the JSON files are committed.
"""

import ast
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "..", "core", "film_data")

# Dyeline id -> the stock's data file in spectral_film_lut, below src/spectral_film_lut.
STOCKS = {
    "kodak-5203": "negative_film/kodak_5203.py",
    "kodak-5207": "negative_film/kodak_5207.py",
    "kodak-5213": "negative_film/kodak_5213.py",
    "kodak-5219": "negative_film/kodak_5219.py",
    "kodak-5277": "negative_film/kodak_5277.py",
    "fuji-eterna-500": "negative_film/fuji_eterna_500.py",
    "kodak-2383": "print_film/kodak_2383.py",
    "kodak-2393": "print_film/kodak_2393.py",
    "fuji-3513di": "print_film/fuji_3513di.py",
    "kodak-5384": "print_film/kodak_5384.py",
}

# The FilmData fields Dyeline uses.
FIELDS = ["name", "manufacturer", "year", "iso", "film_type", "stage", "density_measure",
          "exposure_kelvin", "exposure_base", "lad", "log_sensitivity", "sensiometric_curve",
          "spectral_density", "d_min_sd"]


def literal(node):
    value = ast.literal_eval(node)
    if isinstance(value, dict):
        return sorted([float(k), float(v)] for k, v in value.items())
    if isinstance(value, list) and value and isinstance(value[0], dict):
        return [sorted([float(k), float(v)] for k, v in d.items()) for d in value]
    return value


def film_data(path):
    tree = ast.parse(open(path, encoding="utf-8").read())
    for node in ast.walk(tree):
        if isinstance(node, ast.Call) and getattr(node.func, "id", "") == "FilmData":
            out = {}
            for keyword in node.keywords:
                if keyword.arg in FIELDS:
                    try:
                        out[keyword.arg] = literal(keyword.value)
                    except ValueError:
                        pass  # computed from another stock's data: not used
            return out
    raise SystemExit(f"{path}: no FilmData(...) found")


def status_responses(path):
    tree = ast.parse(open(path, encoding="utf-8").read())
    out = {}
    for node in tree.body:
        if isinstance(node, ast.Assign) and isinstance(node.targets[0], ast.Name):
            name = node.targets[0].id
            if name in ("STATUS_A", "STATUS_M") and name.lower() not in out:
                out[name.lower()] = literal(node.value)
    return out


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    src = os.path.join(sys.argv[1], "src", "spectral_film_lut")
    os.makedirs(OUT, exist_ok=True)
    for stock, file in STOCKS.items():
        data = film_data(os.path.join(src, file))
        data["source"] = "spectral_film_lut " + file
        with open(os.path.join(OUT, stock + ".json"), "w", encoding="utf-8") as out:
            json.dump(data, out, indent=1)
            out.write("\n")
    with open(os.path.join(OUT, "densitometry.json"), "w", encoding="utf-8") as out:
        json.dump(status_responses(os.path.join(src, "densiometry.py")), out, indent=1)
        out.write("\n")


if __name__ == "__main__":
    main()
