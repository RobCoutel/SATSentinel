# Command Language & Replay

SATSentinel has two independent command surfaces:

1. **Navigation commands** — always available, built into `SATSentinel` itself
   (`next`, `back`, `print`, breakpoints, ...). See
   [Navigation & Debugging](Navigation-and-Debugging.md).
2. **The external/"user" command parser** (`Tparser`) — whatever the host application
   registers via `set_command_parser`. This is entirely up to the integrator (see
   [Integrating a Solver](Integrating-a-Solver.md)).

This page documents the *specific* external command language implemented by the
standalone `SATSentinel` CLI (`main.cpp`'s `configure_command_parser`). It mirrors the
whole `Sentinel-API.hpp` surface as text commands, which makes it useful for three
things at once: manually scripting a fake solver run from the terminal, feeding a
`--commands` file, and replaying a `--save-file` execution log (the log format *is*
this command language).

For instance, most of my own slides are created using the `print trail latex` commands from an artificial `--commands` file, which is replayed through the CLI to produce a LaTeX tikz representation of the solver's trail at each step (see [LaTeX Export](LaTeX-Export.md)).

## Commands

| Command | Syntax | API call |
|---|---|---|
| `NEW VAR` | `<var>` | `add_variable` |
| `ALIAS` | `<var> <alias-text>` | `set_variable_alias` |
| `NEW CLAUSE` | `<clause-id> <lit1> [<lit2> ...] [EXTERNAL]` | `add_clause` |
| `DELETE CLAUSE` | `<clause-id>` | `delete_clause` |
| `SHRINK CLAUSE` | `<clause-id> <literal>` | `shrink_clause` |
| `ASSIGN` | `<literal> [<reason>]` | `assign` |
| `UNASSIGN` | `<literal>` | `unassign` |
| `PROPAGATE` | `<literal>` | `propagate` |
| `UNPROPAGATE` | `<literal>` | `unpropagate` |
| `UPDATE LEVEL` | `<literal> <level>` | `update_level` |
| `UPDATE REASON` | `<literal> <reason>` | `update_reason` |
| `WATCH` | `<clause-id> <literal>` | `watch` |
| `UNWATCH` | `<clause-id> <literal>` | `unwatch` |
| `BLOCK` | `<clause-id> <blocker> <watched-literal>` | `block` |
| `LOCK` | `<literal>` | `lock_assumption` |
| `UNLOCK` | `<literal>` | `unlock_assumption` |
| `CHECK INVARIANTS` | — | `check_invariants` |
| `MESSAGE` | `[<level>] <text>` | `message` |
| `quit` | — | exits the process |

Notes:

- `<literal>` and `<reason>` use the syntax described in
  [Types Reference](Types-Reference.md) — e.g. `~v0`/`~0` for a negative literal, and
  `UNDEF`/`ROOT`/`LAZY`/`ASSUMPTION`/`ERROR`/an integer clause id for a reason.
- `checkpoint` is deliberately **not** exposed as a command: it's driven by the CLI's
  main loop (`while (true) checkpoint(sentinel);`), not something you replay.
- Commands are case- and whitespace-sensitive; `help` (built into the navigation
  parser, not this one) is not part of this table.

## Replaying a log

```sh
./build/SATSentinel my_run.log [flags...]
```

If the first positional argument doesn't start with `-`, it's treated as a file of
commands (one per line) to replay through this parser *before* the sentinel enters its
checkpoint loop. Blank lines and lines starting with `#` are skipped. This is exactly
the `--commands`/`-commands` mechanism used mid-run, just applied up front.

## Recording a log

```sh
./build/SATSentinel --save-file my_run.log
```

With `--save-file` set, every notification sent to the sentinel — however it was
triggered (a real solver via the API, or this same command language) — is appended to
the file in this same human-readable command form, flushed immediately so a crash
doesn't lose earlier entries. Feed the resulting file back in as the positional
load-file argument above to reconstruct the exact same run.

If more than one `SATSentinel`/`ExecutionLogger` instance ever opens the same
`--save-file` path within the same process — built at once with the same options, or
one after another over the run, even after an earlier instance was destroyed — only
the very first to claim that path keeps the plain filename. Every other instance that
opens it gets its instance number appended before the extension instead (e.g.
`my_run.log`, `my_run_1.log`, `my_run_2.log`, ...), so it never truncates a log an
earlier instance left behind.

## Driving commands from a file mid-run

```sh
./build/SATSentinel --commands my_commands.txt
```

At each checkpoint, before prompting on stdin, the sentinel first drains any remaining
lines from `--commands`. Combine with `--save-file` to record while you replay a
scripted scenario.
