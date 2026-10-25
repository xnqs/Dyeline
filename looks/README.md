# Bundled looks

The looks that ship with Dyeline, one `.dyeline` file each, grouped in folders by mood. The
Look Designer's browser shows them next to your own looks. They are made from scratch by
Dyeline contributors and released under [CC-BY-4.0](https://creativecommons.org/licenses/by/4.0/),
like all of Dyeline's presets (the code is Apache-2.0).

Looks here leave the color settings alone: they are graded in the working space, so they
suit any footage once its input space is set. A new look should:

- use only tools that ship in the same release, with no LUT files;
- set `meta.name`, a few lowercase `meta.tags`, `meta.author` and `"license": "CC-BY-4.0"`;
- pass `ctest -R BundledLooks`, which renders every look here.
