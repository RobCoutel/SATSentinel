# Types Reference

All of these are defined in [`include/Sentinel-types.hpp`](../include/Sentinel-types.hpp)
and used throughout the API, the command language, and the GUI.

## Terminology used across the docs

| Symbol | Meaning |
|---|---|
| `V` | the set of variables |
| `F` | the set of clauses |
| `π` | the complete partial assignment |
| `πᵈ` | the set of decision literals |
| `τ` | the set of propagated literals |
| `ω` | the propagation queue |
| `δ(ℓ)` | the decision level of literal `ℓ` |
| `ρ(ℓ)` | the reason clause of literal `ℓ` |
| `WL(ℓ)` | the watch list of literal `ℓ` |
| `■` | the undefined clause (`CLAUSE_UNDEF`) |

- A literal `ℓ` is **assigned** if it is in `π`.
- A literal `ℓ` is **propagated** if it is in `τ`.
- A literal `ℓ` is a **decision** if it is in `πᵈ`.
- A literal `ℓ` is **implied** if it is assigned and not a decision.
- The **reason** of `ℓ` is the clause `ρ(ℓ)` that implies it, or `■` if `ℓ` is a decision.
- Invariant: `π = τ ⋅ ω`, `πᵈ ⊆ π`, and `∀ℓ ∈ π. ¬ℓ ∉ π`.

## `Tvar` — variable

A thin wrapper around `unsigned int`. The first variable is `0`. Prints as `v<id>`
(e.g. `v3`).

## `Tlit` — literal

A variable plus a polarity bit: `value = (var << 1) | pol`, where `pol` is `1` for
positive and `0` for negative. `~lit` negates it, `lit.var()` and `lit.pol()` extract
the parts.

**Text syntax** (used by the command language and execution logs, matching
`Tlit::to_string()`):

- `0` — the positive literal of variable 0.
- `~0` — the negative literal of variable 0.
- The `v` prefix is optional: `v0` and `0` parse the same.
- There is **no** plain `-` sign for polarity (`-0` would be indistinguishable from `0`
  under signed parsing) — always use `~`.

`LIT_UNDEF = 0xFFFFFFFF` is the sentinel "no literal" value.

## `Tval` — variable value

One of `VAL_TRUE`, `VAL_FALSE`, `VAL_UNDEF` (and an internal `VAL_ERROR`). Printed as
`TRUE` / `FALSE` / `UNDEF`.

## `Tlevel` — decision level

`LEVEL_ROOT = 0` is the root level; the first real decision level is `1`.
`LEVEL_UNDEF = 0xFFFFFFFF` (printed as `∞`) means "unassigned".

## `Tclause` — clause id

The first clause has id `0`. A handful of high values are reserved as special
"reasons" rather than real indices into the clause database (`Tclause::special()` is
true for any of these):

| Constant | Meaning | Text form |
|---|---|---|
| `CLAUSE_UNDEF` | no reason (the literal is a decision) | `UNDEF` |
| `CLAUSE_ROOT` | forced at the root level, outside any clause | `ROOT` |
| `CLAUSE_LAZY` | lazily-justified reason (see the *Lazy Reimplication* watch invariants in [Invariants](Invariants.md)) | `LAZY` |
| `CLAUSE_ASSUMPTION` | locked as an assumption (see `lock_assumption`) | `ASSUMPTION` |
| `CLAUSE_ERROR` | internal error marker | `ERROR` |

These same keywords are accepted wherever a *reason* argument is expected in the
command language (e.g. `ASSIGN 3 ASSUMPTION`), alongside a plain integer clause id.

## `Tparser`

```cpp
using Tparser = std::function<bool(std::string)>;
```

The signature of a command handler: takes one input line, returns whether it
succeeded. Used both internally (built-in navigation commands) and for the
application-supplied parser registered via `set_command_parser` — see
[Command Language & Replay](Command-Language-and-Replay.md).
