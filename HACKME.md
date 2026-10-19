# HACKME

Notes for people working on SATSentinel itself. For using it, start with the
[README](README.md) and the [wiki](docs/README.md); for the internal design, read
[Architecture](docs/Architecture.md) first. This file covers the practical side:
setting up, testing, and the usual kinds of change.

## Setup

```sh
git clone --recurse-submodules <repo-url>   # third_party/imgui is a submodule
make install-hooks                           # pre-commit hook: builds and runs the tests
make install-test                            # Catch2, needed for `make tests`
make debug                                   # -O0 -g build, easiest to debug
```

Only g++ with C++17 and `make` are needed for the CLI. The GUI needs GLFW and OpenGL
headers (`make install-gui`, then `make GUI=1`), see [Building](docs/Building.md).
Building with `GUI=0` and `GUI=1` are tracked by `build/.build-flags`; if a build ever
looks stale, `make clean`.

## Tests

```sh
make install-test   # installs Catch2, if not already present
make tests
./build/SATSentinel-tests
```

Tests live in `tests/` (Catch2) and are picked up automatically by the makefile
(`find tests -name "*.cpp"`). The pre-commit hook (`.githooks/pre-commit`) runs
both commands above and aborts the commit if either fails. Don't bypass it with
`--no-verify` to get a red test in; fix or update the test.

`tests/data/example.log` is the small execution log used in the README quick start. It
is a handy smoke test for anything touching the command language:

```sh
./build/SATSentinel tests/data/example.log
```

## Where things live

See the module map in [Architecture](docs/Architecture.md#module-map). The short
version:

- `include/` is the public surface (`Sentinel-API.hpp`, `Sentinel-types.hpp`,
  `Sentinel-options.hpp`). Anything a solver integrator can call goes through here.
- `src/Sentinel-notifications.*` is one class per event type, each with `apply()`,
  `rollback()`, `to_command()` and `get_event_level()`.
- `src/Sentinel-state.*` is the mirrored state and the built-in invariants.
- `src/SATSentinel.*` is the cursor over the notification history (`next()`/`back()`).
- `main.cpp` is the CLI and its text command language, which mirrors the API.

## Common changes

### Adding a notification type

The notification pattern is described in
[Architecture](docs/Architecture.md#the-notification-pattern). The checklist:

1. Add a value to `ENotifType` in `src/Sentinel-notifications.hpp` (and to
   `notification_type_to_string`).
2. Add the class there, with its `DEFAULT_LEVEL`, `NTYPE`, `get_event_level`,
   `get_type`, `get_message`, `to_command`, `apply` and `rollback`.
3. Implement them in `src/Sentinel-notifications.cpp`. `rollback()` must undo `apply()`
   exactly, using whatever `apply()` stashed in the object: stepping back relies on this,
   there are no state snapshots. Use `SOFT_ASSERT` for preconditions on `apply()`
   (a violation is reported as a notification failure instead of crashing).
   `get_event_level()` should return `0` when the notification touches a marked
   variable or clause.
4. Expose it in `include/Sentinel-API.hpp` / `src/Sentinel-API.cpp`.
5. Mirror it in the `main.cpp` command parser. `to_command()` output must parse back to
   the same notification, because `--save-file` logs are replayed through that parser.
6. Add tests in `tests/Sentinel-notification.test.cpp` (apply, then rollback, then check
   that the state is unchanged) and `tests/Sentinel-API.test.cpp`.
7. Update the docs: the API table in [Integrating a Solver](docs/Integrating-a-Solver.md),
   the command table in [Command Language & Replay](docs/Command-Language-and-Replay.md),
   and the level table in [Navigation & Debugging](docs/Navigation-and-Debugging.md#event-level-of-each-notification).

### Adding a built-in invariant

Invariants are implemented in `src/Sentinel-state.cpp`, enabled by a flag in `Options`
(`include/Sentinel-options.hpp`) and a CLI flag (`src/Sentinel-options.cpp`). Add the
flag in both places, add a row to the table in [Invariants](docs/Invariants.md), and
test both a state that satisfies it and one that violates it. Invariants are `O(|F|)`
or worse, so they must stay off by default.

### Adding a navigation command

Register it in `register_commands()` in `src/Sentinel-commands.cpp`, with a help
string (that string is what `help` prints). The GUI sends command strings through the
same parser, so it needs no change. If the command prints state, put the printing code
in `src/Sentinel-printer.cpp` or `src/Sentinel-state-printer.cpp`. Add it to the command
table in [Navigation & Debugging](docs/Navigation-and-Debugging.md). Mind the
registration order: "print" matches by prefix, so more specific `print ...` commands
must be registered before it.

### Adding a CLI option

Add the field to `Options` in `include/Sentinel-options.hpp`, then register it in
`src/Sentinel-options.cpp` (`p.add_string`, `add_bool`, ...) with a description and its
aliases. Add a row to the options table in [Building](docs/Building.md).

### LaTeX output

The renderers are `SentinelState::*_to_latex` in `src/Sentinel-state-printer.cpp`.
The macros the output expects are in `data/macros.tex`; keep the two in sync when
you change either. See [LaTeX Export](docs/LaTeX-Export.md).

### GUI

Everything GUI-specific is under `src/gui/` and behind `SENTINEL_GUI_ENABLED`. The GUI
only reads `SentinelState`/`SentinelMarker` and changes anything by sending command
strings back to `SATSentinel`. Keep it that way, so the same behavior is available from
the terminal. Remember to test with `GUI=1`, since the default build doesn't compile
it.

## Conventions

- Match the style of the surrounding code (naming, comment density, braces).
- Public API and notification classes carry Doxygen comments; keep them up to date.
- Source files start with the MIT license header.
- Keep the docs in `docs/` in sync with behavior. A change to a command, option,
  level or invariant is not done until its table is updated.
- Don't commit `build/`.
