# Integrating a Solver

This page is for solver authors instrumenting their own CDCL solver with SATSentinel.
Everything here is declared in [`include/Sentinel-API.hpp`](../include/Sentinel-API.hpp);
see [Types Reference](Types-Reference.md) for the terminology (`V`, `F`, `π`, `δ`, `ρ`,
`WL`, ...) and literal/clause syntax used below and throughout the codebase.

## Model

SATSentinel does **not** run or drive your solver. Your solver calls into the API to
*report* every state-changing event as it happens; SATSentinel mirrors that state in a
private `SentinelState`, and — depending on configuration — pauses to let a user
inspect it. Every reporting call also doubles as a correctness assertion: it has
preconditions (documented per-function below) that, if violated, make the call return
`false` rather than silently corrupting the mirrored state.

## Lifecycle

```cpp
#include "Sentinel-API.hpp"
using namespace sentinel;

Options options;                         // see Building.md / CLI reference for fields
options.check_no_conflicts = true;       // enable whichever invariants you want
SATSentinel* sentinel = create_sentinel(options);

// ... run your solver, calling the reporting functions below ...

delete_sentinel(sentinel);
```

## Reporting state changes

Each call below corresponds 1:1 to a notification type and returns `false` if its
precondition is violated (it does not throw or abort by default — pair with
`Options::crash_on_error` if you want a hard stop on the first violation).

| Function | Effect | Precondition |
|---|---|---|
| `add_variable(s, v)` | registers variable `v` | `v ∉ V` |
| `set_variable_alias(s, v, alias)` | attaches a human-readable name, shown in the terminal/GUI | `v ∈ V` |
| `add_clause(s, cl, lits, size, external=false)` | registers clause `cl` | `cl ∉ F`; all literals' variables `∈ V` |
| `delete_clause(s, cl)` | removes clause `cl` | `cl ∈ F` |
| `shrink_clause(s, cl, lit)` | removes one literal from a clause | `cl ∈ F`; `lit ∈ cl` |
| `assign(s, lit, reason=CLAUSE_UNDEF)` | adds `lit` to `π` (and to `ω`); a decision if `reason == CLAUSE_UNDEF`, otherwise implied with that reason | `lit ∉ π` |
| `unassign(s, lit)` | removes `lit` from `π` | `lit ∈ π` |
| `propagate(s, lit)` | moves `lit` from `ω` to `τ` | `lit ∈ π ∩ ω`, `lit ∉ τ` |
| `unpropagate(s, lit)` | moves `lit` from `τ` back to `ω` | `lit ∈ τ` |
| `update_level(s, lit, level)` | changes `δ(lit)` | `lit ∈ π` |
| `update_reason(s, lit, reason)` | changes `ρ(lit)` | `lit ∈ π` |
| `watch(s, cl, lit)` | adds `cl` to `WL(lit)` | — |
| `unwatch(s, cl, lit)` | removes `cl` from `WL(lit)` | `cl ∈ WL(lit)` |
| `block(s, cl, blocker, watch)` | records `blocker` as the blocking literal for `cl`'s watch on `watch` | `blocker ∈ cl`; `cl ∈ WL(watch)` |
| `lock_assumption(s, lit)` | marks `lit` as a locked assumption | not already locked |
| `unlock_assumption(s, lit)` | unlocks it | currently locked |
| `message(s, text, level=0)` | emits a free-text diagnostic, subject to the same display-level gating as every other notification | — |

Every one of these calls internally goes through `SATSentinel::notify()`, which:

1. records the notification (and appends it to `--save-file` if one is open — see
   [Command Language & Replay](Command-Language-and-Replay.md)),
2. applies it to the mirrored `SentinelState`,
3. and, depending on the notification's *event level* vs. the current display level
   (and breakpoints/marked objects), may pause for interactive navigation — see
   [Navigation & Debugging](Navigation-and-Debugging.md).

## Checkpoints

```cpp
bool checkpoint(SATSentinel* sentinel);
```

Call `checkpoint()` at safe points in your solver's main loop (e.g. once per decision,
or once per conflict) where it's safe to pause and let the sentinel prompt for
navigation commands and then for "user" commands via whatever parser you registered
with `set_command_parser` (see below). It is deliberately *not* itself a reportable
notification/replayable command — it's driven by your main loop, not replayed from a
log.

## Invariant checking

```cpp
bool check_invariants(SATSentinel* sentinel);
```

Runs every enabled built-in invariant (from `Options`) plus any you registered — see
[Invariants](Invariants.md) for the full list, the math behind each one, and how to add
custom checks with `add_invariant` / `add_watch_invariant`.

You may check how [NapSAT](https://github.com/RobCoutel/NapSAT/blob/main/src/solver/NapSAT-checker.cpp) employs configures them in the `napsat::NapSAT::load_invariant_configuration` function.

## Optional callbacks

```cpp
void set_variable_detail_callback(SATSentinel*, std::function<std::string(Tvar)>);
void set_clause_detail_callback(SATSentinel*, std::function<std::string(Tclause)>);
```

Provide extra, application-specific detail strings (e.g. solver heuristic scores) shown
in the terminal/GUI detail views when the user inspects a variable or clause. These
reflect **live** host state, so they're only meaningful while the sentinel is
synchronized with the solver (`is_real_time()` — i.e. before the user has stepped
backward with `back`).

```cpp
void set_command_parser(SATSentinel*, Tparser* parser);
```

Registers the `Tparser` (`std::function<bool(std::string)>`) used for "user commands"
at each checkpoint pause — i.e. whatever domain-specific debug commands make sense for
*your* solver. The standalone `SATSentinel` CLI (`main.cpp`) registers a parser that
mirrors the whole reporting API as text commands (`NEW VAR`, `ASSIGN`, ...); see
[Command Language & Replay](Command-Language-and-Replay.md) for that concrete example,
which you can reuse verbatim, extend, or replace entirely.

## Minimal example

```cpp
#include "Sentinel-API.hpp"
using namespace sentinel;

Options options;
options.default_display_level = 5;   // pause on assignments/propagations, see Navigation-and-Debugging.md
SATSentinel* s = create_sentinel(options);

add_variable(s, Tvar(0));
add_variable(s, Tvar(1));

Tlit lits[] = { Tlit(Tvar(0), 1), Tlit(Tvar(1), 1) };  // clause (v0 OR v1)
add_clause(s, Tclause(0), lits, 2, /*external=*/true);
watch(s, Tclause(0), lits[0]);
watch(s, Tclause(0), lits[1]);

assign(s, Tlit(Tvar(0), 0));                            // decide ~v0
checkpoint(s);
assign(s, Tlit(Tvar(1), 1), Tclause(0));                 // v1 implied by clause 0
propagate(s, Tlit(Tvar(1), 1));
checkpoint(s);

check_invariants(s);
delete_sentinel(s);
```
