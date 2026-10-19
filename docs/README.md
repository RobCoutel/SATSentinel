# SATSentinel Wiki

SATSentinel is an observer/debugger for CDCL SAT solvers. A solver reports every
state-changing event (variable/clause creation, assignment, propagation, watch-list
updates, ...) through a small C++ API (`Sentinel-API.hpp`); SATSentinel mirrors that
state internally, checks it against a library of correctness invariants, and lets you
pause, inspect, and step (`next` / `back`) through the solver's execution — either in a
terminal or in an optional Dear ImGui GUI.

This wiki covers building it, wiring it into a solver, and using it interactively.

## Pages

- **[Building](Building.md)** — build the CLI, the GUI, the tests, and the git hooks.
- **[Integrating a Solver](Integrating-a-Solver.md)** — the `Sentinel-API.hpp` C++ API:
  how to report events from your solver, register invariants, and drive checkpoints.
- **[Types Reference](Types-Reference.md)** — `Tvar`, `Tlit`, `Tclause`, `Tlevel`,
  `Tval`, and the text syntax used to write literals/clauses in commands and logs.
- **[Command Language & Replay](Command-Language-and-Replay.md)** — the text command
  language used by the standalone CLI, load files, and `--save-file` execution logs.
- **[Navigation & Debugging](Navigation-and-Debugging.md)** — the built-in
  `next`/`back`/breakpoint/mark commands used to pause and step through an execution.
- **[LaTeX Export](LaTeX-Export.md)** — `print ... latex` / `save` / `--save-latex`:
  render trails, clause sets and implication graphs as LaTeX/TikZ.
- **[Invariants](Invariants.md)** — the built-in correctness checks and how to add
  custom ones.
- **[GUI](GUI.md)** — the optional Dear ImGui frontend (`make GUI=1`).
- **[Architecture](Architecture.md)** — internal module map, the notification
  apply/rollback pattern, and how the pieces fit together (for contributors).

## Quick start

```sh
make all                 # build build/SATSentinel and build/SATSentinel.a
./build/SATSentinel -h   # list every CLI option
```

With no arguments, `SATSentinel` starts an empty session and waits for commands on
stdin — see [Command Language & Replay](Command-Language-and-Replay.md) to script a
problem by hand, or [Integrating a Solver](Integrating-a-Solver.md) to drive it from
real solver code instead.

## Acknowledgements

Portions of SATSentinel's source code and tests were written with the assistance of
Claude (Anthropic).
