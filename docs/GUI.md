# GUI

SATSentinel has an optional Dear ImGui/GLFW/OpenGL3 frontend
(`src/gui/SentinelGUI.hpp`/`.cpp`) as an alternative to the terminal for interactive
sessions. It is strictly **read-only** over `SentinelState`: it renders the current
mirrored state and forwards user input (typed commands or button presses) through the
exact same command dispatch (`CommandParser` for navigation, `external_parser` for
user commands) that the terminal frontend uses — it never mutates solver state
directly. The one exception is a small set of "live" debugger settings it *can* toggle
directly (`interactive`, `check_only`, `crash_on_error` on the shared `Options`), which
mirrors how the terminal's own `set level` command already reaches into private
debugger state outside the formal `Sentinel-API.hpp` surface.

## Enabling it

Build with GUI support, then opt in at runtime:

```sh
make all GUI=1
```

```cpp
Options options;
options.gui = true;
```

or `./build/SATSentinel --gui`. If `gui` is requested without a `GUI=1` build, or GLFW
initialization fails (no display, etc.), SATSentinel logs a warning and transparently
falls back to the terminal frontend — see [Building](Building.md).

## Layout

The window is split into resizable panels (drag the splitters between them):

- **Trail panel** — the assignment trail in order, toggleable with an
  **implication graph** view of the same data. Left-click a node to open its detail in
  the bottom-right panel; right-click to cycle its ad-hoc highlight color
  (red → orange → blue → back to normal) while inspecting a conflict.
- **Variables panel** — filterable list of variables; click one to inspect it.
- **Clauses panel** — filterable list of clauses, with filters by clause text or by
  variable, and three independent toggles — **Conflicting**, **Implying**, and
  **Unit (not yet propagating)** — that narrow the list to their union (plus any
  marked clauses, which are always shown once any toggle is on). "Implying" means
  the clause is currently the reason for an assignment; "Unit" means the clause is
  unit but isn't (yet) anyone's reason.
- **Command panel** — two input boxes (navigation commands and user commands,
  matching whichever is currently active — see [Navigation & Debugging](Navigation-and-Debugging.md)),
  with history (back/next to browse previous entries), and, below that, a toggle between two
  views:
  - **Notifications** (the default) — a scrollable, numbered list of every notification
    the sentinel has replayed so far, both before and after the current one. The current
    notification's text is shown in blue; every other row stays in the default text
    color. Click a row to jump straight to it (forward or backward), via the `goto`
    navigation command — this bypasses the display level and any breakpoints, unlike
    `next`/`back`. Click a row's left gutter to toggle a breakpoint there (a filled red
    dot marks a set breakpoint), the same as VSCode's line gutter — equivalent to the
    `set breakpoint`/`remove breakpoint` navigation commands.
  - **Log** — a scrolling log of past commands/results (what used to be the panel's only
    view).
- **Bottom-right panel** — switches between three views:
  - **Options** — the enabled invariant checks (read-only) plus the live-editable
    debugger settings and display level.
  - **Detail** — the selected variable's or clause's full detail, including whatever
    your host application returned from `set_variable_detail_callback` /
    `set_clause_detail_callback` (only meaningful while synchronized with the live
    solver — see `is_real_time()` in [Navigation & Debugging](Navigation-and-Debugging.md)).
  - **Trace** — the C++ backtrace at the current notification (same restriction as the
    terminal's `backtrace` command), with a condensed mode that hides everything but
    each frame's resolved `file:line`.

## Interaction

- **Ctrl+scroll** zooms the whole UI (rescales fonts and widget sizes).
- Selecting a variable or clause automatically switches the bottom-right panel to
  **Detail**; the radio buttons let you flip back to **Options**/**Trace** without
  losing the selection.
- Next/Back buttons submit the same `"next"`/`"back"` commands as typing them into the
  navigation input box.
