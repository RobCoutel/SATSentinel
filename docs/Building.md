# Building

SATSentinel builds with plain `make` and g++ (C++17), no package manager or CMake
involved.

## CLI + static library

```sh
make all
```

Produces:

- `build/SATSentinel` — the standalone CLI tool (`main.cpp`).
- `build/SATSentinel.a` — the static library other code links against to embed the
  observer (see [Integrating a Solver](Integrating-a-Solver.md)).

## GUI build

```sh
make all GUI=1
```

Additionally compiles a Dear ImGui/GLFW/OpenGL3 frontend (vendored under
`third_party/imgui`, a git submodule) and links against the system GLFW + OpenGL
libraries:

```sh
make install-gui     # apt-get install libglfw3-dev libgl1-mesa-dev; git submodule update --init
```

The GUI is only *compiled in* by `GUI=1`; it is only *enabled at runtime* when the host
sets `Options::gui = true` (or passes `--gui` to the CLI). If `gui` is requested but the
binary wasn't built with `GUI=1`, SATSentinel logs a warning and falls back to the
terminal frontend instead of failing. See [GUI](GUI.md) for what it looks like and how
to use it.

> **Stale objects warning:** switching between `GUI=0` and `GUI=1` (or between
> `BUILD_MODE=release`/`debug`) is tracked by a build-flags fingerprint
> (`build/.build-flags`), so a plain `make` after flipping the flag *does* rebuild the
> affected `.o` files. If you ever see stale-looking behavior after changing build
> flags, `make clean` first.

## Debug build

```sh
make debug            # -O0 -g -g3 -gdwarf-2 -ftrapv, no optimizations
```

## Tests

Tests use [Catch2](https://github.com/catchorg/Catch2) and live under `tests/`.

```sh
make install-test     # apt-get install catch2, if not already present
make tests            # builds build/SATSentinel-tests
./build/SATSentinel-tests
```

## Git hooks

```sh
make install-hooks    # git config core.hooksPath .githooks
```

Installs the repo's pre-commit hook (`.githooks/pre-commit`), which runs the test
suite before each commit.

## Command-line options

`./build/SATSentinel -h` (or `--help`) prints the full, always-up-to-date option list
generated from `Options::get_help_text()`. As of this writing:

### General

| Flag | Alias | Type | Default | Description |
|---|---|---|---|---|
| `--default-display-level` | `-dl` | int | `0` | Notifications with a level ≥ this interrupt the solver, display state, and prompt for a navigation command. |
| `--check-only` | `-co` | bool | `false` | Never interrupt or display state; only report notification/invariant failures. |
| `--crash-on-error` | — | bool | `false` | `abort()` immediately when a notification fails, instead of reporting and continuing. |
| `--gui` | `-gui`, `--sentinel-gui` | bool | `false` | Use the graphical frontend (needs `GUI=1` at build time; falls back to terminal otherwise). |
| `--commands` | `-commands`, `--command-file` | string | `""` | File of commands read (interactively, at each checkpoint) before falling back to stdin. |
| `--save-file` | `-save-file`, `--save` | string | `""` | Record every notification, as it happens, in replayable command form (see [Command Language & Replay](Command-Language-and-Replay.md)). |

### Invariant checks

See [Invariants](Invariants.md) for the semantics and cost of each of these:

`--check-no-conflicts`, `--check-no-missed-implications`, `--check-implied-levels`,
`--check-trail-monotonicity`, `--check-topological-order`,
`--check-assignment-coherence`, `--check-weak-watched-literals`,
`--check-strong-watched-literals` — all bool, default `false`.

Boolean flags accept an optional `on`/`off` argument (e.g. `--gui off`); with no
argument they default to `on`.

The standalone CLI (`main.cpp`) also accepts one optional **positional** argument
before any flags: a path to a log file to replay (see
[Command Language & Replay](Command-Language-and-Replay.md#replaying-a-log)).
