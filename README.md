# SATSentinel

SATSentinel is an observer/debugger for CDCL SAT solvers: a solver reports notifications
(variable/clause creation, assignment, propagation, watch updates, ...) through the
`Sentinel-API.hpp` interface, and SATSentinel mirrors that state for interactive
inspection, invariant checking, and replay (`next()`/`back()`).

## Building

```sh
make all
```

Builds the CLI tool (`build/SATSentinel`) and static library (`build/SATSentinel.a`)
with no extra dependencies.

## Building with the GUI

```sh
make all GUI=1
```

Additionally compiles a Dear ImGui/GLFW/OpenGL3 frontend (vendored under
`third_party/imgui`) and links against the system GLFW + OpenGL libraries. Enable it at
runtime by setting `SentinelOptions::gui = true`; if `gui` is requested but the binary
wasn't built with `GUI=1`, SATSentinel logs a warning and falls back to the terminal
frontend.

Requires the GLFW and OpenGL development headers:

```sh
sudo apt-get install libglfw3-dev libgl1-mesa-dev
```

The GUI only ever reads `SentinelState` while rendering; it mutates the solver only
through the same command dispatch (`CommandParser`/`external_parser`) the terminal
frontend uses, triggered by typed commands or the Next/Back buttons.

**Note:** switching between `GUI=0` and `GUI=1` reuses stale `.o` files unless you
`make clean` first, since the build doesn't track the flag's value as a dependency.

## Tests

```sh
make install-test   # installs Catch2, if not already present
make tests
```
