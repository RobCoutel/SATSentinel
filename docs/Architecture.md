# Architecture

Internal module map and design notes, for contributors working on SATSentinel itself
(as opposed to integrating it into a solver — see
[Integrating a Solver](Integrating-a-Solver.md) for that).

## Module map

| Path | Role |
|---|---|
| `include/Sentinel-API.hpp` | Public C++ API — the only header a solver integrator needs. |
| `include/Sentinel-types.hpp` | `Tvar`/`Tlit`/`Tlevel`/`Tclause`/`Tval` and their string forms. |
| `include/Sentinel-options.hpp` | `Options` struct + CLI parsing declaration. |
| `src/SATSentinel.hpp`/`.cpp` | `SATSentinel`: the main observer class — owns the notification history, the display-level/breakpoint/mark pause logic (`next()`/`back()`), and wires the GUI in when enabled. |
| `src/Sentinel-state.hpp`/`.cpp` | `SentinelState`: the mirrored solver state (variables, clauses, trail, watch lists) plus all built-in invariant checks. |
| `src/Sentinel-notifications.hpp`/`.cpp` | The notification class hierarchy — one class per reportable event type, each knowing how to `apply()`/`rollback()` itself against a `SentinelState` and render itself as replayable command text (`to_command()`). |
| `src/Sentinel-invariants.hpp` | `Invariant`/`WatchInvariant`: the plain-function wrapper types used both by the built-ins and by custom invariants registered via the API. |
| `src/Sentinel-commands.hpp`/`.cpp` | The generic `Command`/`CommandParser` framework, plus `SATSentinel`'s built-in navigation commands (`next`, `back`, `mark`, breakpoints, ...). |
| `src/Sentinel-context.hpp` | `SentinelContext`: small bag of per-session display settings. |
| `src/Sentinel-serialization.hpp`/`.cpp` | `ExecutionLogger`: appends `to_command()` text to `--save-file`, flushing after every write. |
| `src/gui/SentinelGUI.hpp`/`.cpp` | The optional Dear ImGui frontend, compiled only under `GUI=1`/`SENTINEL_GUI_ENABLED`. |
| `src/utils/options.hpp`/`.cpp` | Generic reusable CLI option-parsing framework (`Option`/`OptionParser`/`BoolOption`/etc.) that `Sentinel-options.cpp` builds on. |
| `main.cpp` | The standalone CLI: builds a `CommandParser` that mirrors the whole API as text commands (see [Command Language & Replay](Command-Language-and-Replay.md)), replays an optional load file, then loops on `checkpoint()`. |
| `tests/` | Catch2 unit tests. |
| `third_party/imgui` | Vendored Dear ImGui (git submodule), only built under `GUI=1`. |

## The notification pattern

Every state-changing API call (`assign`, `watch`, `add_clause`, ...) constructs one
`sentinel::notif::notification` subclass instance (`src/Sentinel-notifications.hpp`)
and hands it to `SATSentinel::notify()`. Each subclass implements:

- `apply(SentinelState*)` — mutate the mirror forward, and stash whatever it needs
  internally to undo itself later (e.g. `unassignment` stores the pre-unassignment
  `SentinelState::variable` snapshot).
- `rollback(SentinelState*)` — undo `apply()` using that stashed state.
- `to_command()` — render itself as the equivalent command-language text (see
  [Command Language & Replay](Command-Language-and-Replay.md)); this is what
  `ExecutionLogger` writes to `--save-file`.
- `get_event_level(SentinelMarker*)` — the notification's default pause-priority
  (`DEFAULT_LEVEL` per class, e.g. clause/variable creation is coarser than a
  watch-list update), forced to `0` if the notification touches a marked
  variable/clause.

`SATSentinel` keeps the full ordered history in `notifications` plus a
`current_notification_index` cursor. `next()` walks the cursor forward, `apply()`-ing
each notification and deciding (via event level / breakpoints / failure) whether to
stop and prompt for a navigation command. `back()` walks it backward the same way,
calling `rollback()`. This means the entire "step back through history" feature is
just cursor movement over an append-only log plus per-notification inverse functions —
there's no separate snapshotting or diffing system.

`is_real_time()` (`current_notification_index == notifications.size()`) tells whether
the cursor is caught up with the live solver — it gates things like `backtrace` (the
native call stack is only meaningful while the solver is actually blocked inside the
`notify()` call, not while browsing already-recorded history) and the
variable/clause detail callbacks (which reflect live host state).

## Two independent command surfaces

It's worth being explicit about a naming trap in the code: `SATSentinel` holds *two*
separate `CommandParser`-shaped things —

- `navigation_commands`, a `CommandParser` owned by `SATSentinel` itself, built once in
  `register_commands()` (`Sentinel-commands.cpp`) — this is the fixed set documented in
  [Navigation & Debugging](Navigation-and-Debugging.md).
- `external_parser`, a bare `Tparser*` the host application supplies via
  `set_command_parser()` — this is whatever the integrator wants (`main.cpp`'s is a
  full API mirror; a real solver might expose something much smaller, or nothing at
  all).

`get_navigation_commands()` drives the first; `get_external_commands()` drives the
second (falling back to a `--commands` file, then stdin, then — if `GUI=1` and enabled
— the GUI's command panel, for both).

## GUI contract

`SentinelGUI` is handed `const` pointers to `SentinelState`/`SentinelMarker` and reads
them directly for rendering, but the *only* way it can affect solver-observable state
is by calling back into `SATSentinel` through the same `GuiDispatch` callback
(`std::function<bool(const std::string&, bool&)>`) that wraps `navigation_commands`/
`external_parser` — i.e. it submits command strings exactly as if they'd been typed
into the terminal. This keeps "what can happen when a command runs" defined in exactly
one place regardless of frontend.

## Build system notes

- `SENTINEL_GUI_ENABLED` is the compile-time guard (`-DSENTINEL_GUI_ENABLED` under
  `GUI=1`) around every GUI-only code path outside `src/gui/`, so `SATSentinel`'s
  memory layout is otherwise identical between `GUI=0`/`GUI=1` builds (the `gui_view`
  member itself is declared unconditionally to keep `sizeof(SATSentinel)` stable).
- The makefile fingerprints `GUI`/`BUILD_MODE`/`CFLAGS` into `build/.build-flags` and
  makes it a prerequisite of every object file, so toggling those flags forces the
  affected files to rebuild — see [Building](Building.md).
