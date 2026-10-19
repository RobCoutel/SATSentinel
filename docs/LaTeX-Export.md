# LaTeX Export

SATSentinel can render the mirrored solver state as LaTeX/TikZ, to put trails,
clause sets and implication graphs directly in slides or papers. The code lives in
`src/Sentinel-state-printer.cpp` (`SentinelState::*_to_latex`) and
`src/Sentinel-printer.cpp` (the commands). Like the other
[navigation commands](Navigation-and-Debugging.md), these are available at every pause.

## Commands

| Command | Output |
|---|---|
| `print trail latex` | TikZ drawing of the trail (and conflicting clauses) at the current notification, printed to stdout. |
| `print graph latex` | TikZ drawing of the implication graph, printed to stdout. |
| `print clauses latex` | A `tabular` listing every active clause, printed to stdout. |
| `save` | Writes the clause set and the trail to files (see below). Needs `--save-latex`. |

### Saving to files

Run the CLI with `--save-latex <prefix>` (`Options::save_latex_file`). Each `save`
then writes two files, where `<i>` counts the `save` commands issued this session,
starting at `0`:

- `<prefix>-clauses-<i>.tex`: same content as `print clauses latex`.
- `<prefix>-trail-<i>.tex`: same content as `print trail latex`.

Missing parent directories of `<prefix>` are created. Without `--save-latex`, `save`
only prints a warning. `save` does not write the implication graph; use
`print graph latex` for that.

A typical workflow is to script a run with a `--commands` file (see
[Command Language & Replay](Command-Language-and-Replay.md)) that alternates
state-changing commands with `save`, then `\input` the generated files into slides.

```sh
./my-solver --sat-sentinel "--save-latex out/run --commands steps.txt"
# produces out/run-clauses-0.tex, out/run-trail-0.tex, out/run-clauses-1.tex, ...
```

## What is rendered
You may find examples of generated LaTeX trails in my conference presentations available on [my website](https://robcoutel.github.io), for example [SAT 2024](https://robcoutel.github.io/presentations/Lazy_Reimplication_In_Chronological_Backtracking.pdf)

### Variable and literal names

A variable with an alias (`ALIAS` / `set_variable_alias`) is rendered with its trailing
digits as a subscript: `x1` becomes `x_{1}`. An alias without trailing digits is kept
as is, and a variable without alias is rendered as `v_{<id>}`. Negation is `\neg`.

Literals are colored according to their current value: false literals in `\red{...}`,
true literals in `\blue{...}` (`\green{...}` if `COLORBLIND_MODE` in
`src/utils/printer.hpp` is set to `0`), undefined literals uncolored.

### Trail (`print trail latex`)

Emits `\draw` commands, to be placed inside a `tikzpicture`:

- one segment per trail literal, drawn at the height of its decision level;
- the literal's reason clause (rotated by 270°) below the axis, and `\delta = <level>`
  under decisions;
- every conflicting clause appended in red, ending with `\bot`;
- a dotted vertical line marking the propagation queue head, labelled `\q` (queue, to the
  right) and `\trail` (propagated part, to the left);
- tick marks on the axis.

### Clause set (`print clauses latex`)

A `tabular` of the active clauses, written `C_{i} = l1 \lor ...` with 1-based
indices. Indices and literal subscripts are padded with `\phantom{...}` so that the
disjuncts line up between rows. Watched literals are underlined.

### Implication graph (`print graph latex`)

Emits the `\tikzstyle` definitions it needs, then one `\node` per trail literal (one
row per decision level, rectangles for decisions and circles for propagated literals),
and an `\draw ... edge[myarr]` from every other literal of the reason clause to the
implied literal.

## Using the output in a document

The output relies on a few macros and styles that you must define in your preamble:
`\red`, `\blue`, `\green`, `\q`, `\trail`, and the TikZ styles `vertex`, `implied`,
`decision`, `myarr`, `marked`, `conflict`. The repository provides a ready-made set in
[`data/macros.tex`](../data/macros.tex); `\input` it in your preamble. You also need
the `tikz` and `xcolor` packages.

```latex
\usepackage{tikz}
\usepackage{xcolor}
\input{macros}   % data/macros.tex
...
\begin{tikzpicture}
  \input{out/run-trail-0.tex}
\end{tikzpicture}
\input{out/run-clauses-0.tex}
```

The trail file is a bare list of `\draw` commands, so it needs the surrounding
`tikzpicture`; the clause file is a standalone `tabular`. The implication-graph
output defines its own `\tikzstyle`s, so it only needs a `tikzpicture` around it.
