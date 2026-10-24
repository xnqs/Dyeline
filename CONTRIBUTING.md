# Contributing to Dyeline

Thanks for helping. Please read [CLEANROOM.md](CLEANROOM.md) first: every contribution must follow
it.

## Sign-off (DCO)

Every commit needs a `Signed-off-by` line, which certifies the
[Developer Certificate of Origin](https://developercertificate.org/): you wrote the change or have
the right to submit it under the project's license. Add it with:

```sh
git commit -s
```

## Workflow

1. Open an issue or comment on one before starting larger work.
2. Branch from `main` and keep each pull request to one topic.
3. Build and run the tests locally (see the README).
4. Open a pull request and fill in the template, including the clean-room checklist.

## Code layout

| Folder | Contents |
| --- | --- |
| `core/` | The render core: tools and color math. Plain C++17, no OFX types. |
| `plugin/` | The OFX plugin: parameters, clips and the bridge to the core. |
| `tests/` | Unit tests for the core and a plugin load test. |
| `cmake/` | Build helpers: OpenFX SDK fetch and bundle layout. |

## Tests

Every tool needs unit tests in `tests/` that pin its math. GPU backends, when they land, are
tested against the CPU implementation.

## Style

- C++17, 4-space indent, `snake_case` for functions and variables, `PascalCase` for types.
- Keep OFX types out of `core/`.
- Prefer plain, commented math over clever code; color code is read far more often than written.
