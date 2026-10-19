# Navigation & Debugging

These commands are built into `SATSentinel` itself (`src/Sentinel-commands.cpp`) and
are always available at every pause, regardless of what external command language
(if any) the host application registered — see
[Command Language & Replay](Command-Language-and-Replay.md) for that separate,
application-defined surface.

## When does the sentinel pause?

Every reported event (see [Integrating a Solver](Integrating-a-Solver.md)) has an
*event level* — roughly, how noisy/fine-grained it is (e.g. a new variable is level
4, a watch-list update is level 9). The sentinel pauses and prompts for a navigation
command whenever:

- the event's level is `≤` the current **display level** (`--default-display-level` /
  `set level`, default `0` — so by default only the coarsest events pause), or
- the event touches a variable/clause you've `mark`ed (forced down to level `0`), or
- `current_notification_index` lands on a notification you've set a **breakpoint** on,
  or
- the notification itself failed (a precondition was violated).

### Event level of each notification

Notifications pause when their level is `≤` the display level, so the lower the level,
the more often it pauses. Defined by `DEFAULT_LEVEL` in
`src/Sentinel-notifications.hpp`, ordered from coarsest to finest:

| Level | Notification | Notes |
|---|---|---|
| 0 | any notification touching a `mark`ed variable/clause | Overrides the default level of every type below. |
| 0 | `message` | Default; the host may pass any level (`message(s, text, level)`). |
| 2 | `assignment` (decision) | Only when the reason is `CLAUSE_UNDEF`, i.e. a real decision. |
| 3 | `new_clause` | |
| 3 | `delete_clause` | |
| 4 | `new_variable` | |
| 4 | `unassignment` | |
| 5 | `assignment` (implied) | With a reason clause. |
| 5 | `update_level` | |
| 5 | `update_reason` | |
| 5 | `lock_assumption` | |
| 5 | `unlock_assumption` | |
| 6 | `propagation` | |
| 6 | `propagation_removed` | |
| 9 | `watch` | |
| 9 | `unwatch` | |
| 9 | `block` | |
| 9 | `remove_literal` | |

**Note:** Some display levels are intentionally skipped (e.g. 1) which can be used for custom message notifications from the host. For example, [NapSAT](https://github.com/RobCoutel/NapSAT) uses level 1 for every solve() call, so the sentinel pauses at every incremental solve step.

With `Options::check_only = true` (`--check-only`), none of this ever pauses or prints
state — notifications are still applied and invariant-checked, but the run is silent
except for failures. `Options::crash_on_error` makes any notification failure
`abort()` immediately instead of just reporting it.

## Commands

| Command | Alias | Args | Effect |
|---|---|---|---|
| `next` | *(empty input)* | — | Advance to the next notification (or resume the live solver if already at the head of history). |
| `back` | `b` | — | Roll the mirrored state back by one displayed step. |
| `goto` | — | `<index>` | Jump directly to a notification index, applying/rolling back everything in between, bypassing display level and breakpoints. Used by the GUI's [notifications panel](GUI.md) to play until a clicked notification. |
| `print` | `p` | — | Reprint the current trail/variables/clauses. |
| `print trail latex` / `print graph latex` / `print clauses latex` | — | — | Print a LaTeX/TikZ rendering of the trail, implication graph or clauses. See [LaTeX Export](LaTeX-Export.md). |
| `save` | — | — | Write the LaTeX clauses and trail to files (requires `--save-latex <prefix>`). See [LaTeX Export](LaTeX-Export.md). |
| `backtrace` | `bt` | — | Print the C++ call stack of the host process *at the current notification* — only while `is_real_time()` (see below). |
| `set level` | — | `<level>` | Set the display level (see above). |
| `mark var` | — | `<var>` | Mark a variable; any notification touching it will pause regardless of level. |
| `unmark var` | — | `<var>` | Undo `mark var`. |
| `mark clause` | — | `<clause>` | Same, for a clause. |
| `unmark clause` | — | `<clause>` | Undo `mark clause`. |
| `set breakpoint` | — | `<index>` | Force a pause once notification history reaches this index. |
| `remove breakpoint` | — | `<index>` | Undo `set breakpoint`. |
| `quit` | `q` | — | Exit the process immediately. |

## Stepping backward and forward (`back` / `next`)

`back` rolls the mirrored `SentinelState` back one displayed notification at a time,
by calling each notification's `rollback()` in reverse. Once you've stepped
backward, the sentinel is no longer synchronized with the live solver
(`is_real_time()` returns false): `next` in that state replays a step *forward through
already-recorded history* rather than releasing control back to the solver, so you
can freely scrub back and forth over what's already happened before returning to live
execution.

`goto <index>` behaves like a multi-step `back`/`next`: it always lands exactly on
`index` and re-displays state there, regardless of that notification's own event level
or whether a breakpoint is set on it — unlike `next`, which only ever stops at
notifications that would normally pause.

`backtrace` is refused while browsing history (i.e. after `back`, before catching back
up with `next`): the native call stack only reflects the live notification while the
sentinel is still blocked inside the `notify()` call the solver made to report it — a
trace taken while browsing history would silently point at the wrong event. Backtraces
are resolved from the host's dynamic symbol table (the CLI links with `-rdynamic` for
this).

**Note:** Backtrace gas been quite slow in applications with large traces (e.g., cvc5). In such applications, a debugger like gdb or lldb may be more appropriate for the task. You can still use the sentinel to guide the breakpoints and use the debugger to inspect the state at those breakpoints.

## Marks and breakpoints

`mark var` / `mark clause` are a way to say "surface everything that touches this
object, no matter the configured display level" — handy for following one variable's
life cycle through a long run without turning on level-9 tracing for everything else.
`set breakpoint <index>` instead targets a specific point in the notification
*sequence* (its index in `notifications`), regardless of what the event is.

## The GUI

Everything above applies identically when using the [GUI](GUI.md) — the same
`next`/`back`/mark/breakpoint commands are dispatched through the graphical command
panel instead of a terminal prompt; the GUI is a read-only view over the same
`SentinelState` plus the same command dispatch mechanism.
