# Film stock data

The data behind Film Negative and Film Print (see [docs/tools.md](../../docs/tools.md)): one
JSON file per stock, plus the Status A and Status M densitometer responses in
`densitometry.json`.

The values are manufacturers' published datasheet curves, digitized by the
[spectral_film_lut](https://github.com/JanLohse/spectral_film_lut) project by Jan Lohse and used
under its MIT license ([LICENSE-spectral_film_lut](LICENSE-spectral_film_lut)). They were copied
from its revision `bcbcfc6861f49cc706dc5d00ea048e4853060aab` by
`scripts/film/import_spectral_film_lut.py`; each file's `source` field names the module it came
from. No datasheet is committed here.

| File | Stock | Kind |
| --- | --- | --- |
| `kodak-5219.json` | Kodak Vision3 500T 5219 | camera negative |
| `kodak-5213.json` | Kodak Vision3 200T 5213 | camera negative |
| `kodak-5207.json` | Kodak Vision3 250D 5207 | camera negative |
| `kodak-5203.json` | Kodak Vision3 50D 5203 | camera negative |
| `fuji-eterna-500.json` | Fuji Eterna 500T | camera negative |
| `kodak-5277.json` | Kodak Vision 320T 5277 | camera negative |
| `kodak-2383.json` | Kodak Vision 2383 | print |
| `kodak-2393.json` | Kodak Vision Premier 2393 | print |
| `fuji-3513di.json` | Fuji Eterna-CP 3513DI | print |
| `kodak-5384.json` | Kodak 5384 | print |

Fields, as spectral_film_lut names them: `log_sensitivity` is each layer's spectral sensitivity
(log10, red layer first) as [nanometers, value] pairs; `sensiometric_curve` is each layer's
characteristic curve as [log exposure, density] pairs, in log lux-seconds, or in stops around a
grey card when `exposure_base` is 2; `spectral_density` holds the dyes' spectra; `lad` is a
print's laboratory aim density; `density_measure` says whether densities are Status A or M.

The plugin doesn't read these files. `scripts/film/make_film_data.py` builds
`core/src/film_stocks.inc` from them, which is committed; it describes the model and needs
numpy and colour-science. After changing a file here, or adding a stock (to the end of its list
in that script, since looks store a stock by its position), run it again:

```sh
pip install colour-science
python3 scripts/film/make_film_data.py
```
