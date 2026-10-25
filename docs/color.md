# Color management

Every tool works on scene-linear light in one working space, ACEScg (linear, with the ACES AP1
primaries). A look's `color` settings say what the footage is and what to output, and Dyeline
converts at both ends of the chain:

```json
"color": {"mode": "full", "input": "logc3", "output": "rec709"}
```

| Setting | Values | Meaning |
| --- | --- | --- |
| `mode` | `tone` (default), `full` | How much to convert (below). |
| `input` | a space id below; default `linear` | The space the host hands Dyeline. |
| `output` | a space id below, or `same` (default) | The space Dyeline hands back. `same` is the input space. |

- **Tone only** (`tone`) converts just the transfer curve, channel by channel. The footage keeps
  its own primaries, so a camera's wide gamut passes through untouched, and tools see linear light
  in the camera's gamut.
- **Full** (`full`) converts the curve and the gamut, so every tool sees true ACEScg, and the
  output gets the output space's primaries. White is adapted from D65 to the ACES white with the
  Bradford transform, as ACES' own conversions do.

The defaults (`tone`, `linear`, `same`) change nothing. They suit hosts that already hand plugins
scene-linear images, such as Natron and Nuke. In an editor or a grading app, set `input` to the
space of your timeline or footage, such as `rec709` for a Rec.709 timeline or `logc3` for ARRI
footage on a log timeline.

The **Strength** slider blends the tools' result with the untouched image in the working space,
so the input and output conversions always apply, even at 0.

If a setting names a space this build doesn't know, the conversions are left out and the
plugin shows a warning.

## Color spaces

| Id | Space | Curve | Primaries |
| --- | --- | --- | --- |
| `linear` | Linear | none | Rec.709 / sRGB |
| `rec709` | Rec.709 video | 2.4 power (as BT.1886 displays it) | Rec.709 |
| `srgb` | sRGB | IEC 61966-2-1 piecewise | Rec.709 / sRGB |
| `acescg` | ACEScg | none | ACES AP1 |
| `acescct` | ACEScct | ACEScct (S-2016-001) | ACES AP1 |
| `logc3` | ARRI LogC3 (EI 800) | LogC3 | ARRI Wide Gamut 3 |
| `logc4` | ARRI LogC4 | LogC4 | ARRI Wide Gamut 4 |
| `slog3` | Sony S-Log3 | S-Log3 | S-Gamut3.Cine |
| `slog3-sgamut3` | Sony S-Log3 | S-Log3 | S-Gamut3 |
| `vlog` | Panasonic V-Log | V-Log | V-Gamut |
| `canon-log3` | Canon Log 3 | Canon Log 3 | Cinema Gamut |
| `log3g10` | RED Log3G10 | Log3G10 | REDWideGamutRGB |
| `davinci-intermediate` | DaVinci Intermediate | DaVinci Intermediate | DaVinci Wide Gamut |

The curves and primaries come from each vendor's published specification and are built in as
small formulas, so Dyeline doesn't need OpenColorIO. Custom OCIO configs are planned for a later
milestone.
