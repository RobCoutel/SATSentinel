# SATSentinel

SATSentinel is an observer/debugger for CDCL SAT solvers.
It is designed to either receive notifications from a solver, through the `Sentinel-API.hpp` interface, or to parse a log of notifications produced by a solver, and replay the execution step by step, while checking invariants and allowing the user to inspect the solver's state at any point.

See the **[wiki](docs/README.md)** for the full documentation: building, integrating
a solver, the command language, invariants, the GUI, and internal architecture.

## Quick start

The simplest way to try it out is to build the CLI tool and run it on a solver log. We provide an example log file in `tests/data/example.log`:

### Building

```sh
make
```

Builds the CLI tool (`build/SATSentinel`).
If you wish to enable the optional GUI, you will need to first install the required dependencies (GLFW + OpenGL development headers). The following command will require `sudo` privileges. You are invited to check the installed packages in the [Makefile](Makefile) and install them manually if you prefer.
```sh
make install
```

Then, the option `GUI=1` can be passed to the `make` command to build the GUI frontend:
```sh
make GUI=1
```

**Note:** switching between `GUI=0` and `GUI=1` reuses stale `.o` files unless you
`make clean` first, since the build doesn't track the flag's value as a dependency.

### Running
While the Sentinel is meant to be integrated into a solver using the API, it can also be used as a standalone tool. You may use the default parser whose commands are detailed in [Command Language & Replay](docs/Command-Language-and-Replay.md), or write your own parser to fit your solver outputs by overriding the parser defined in the [main.cpp](main.cpp) file.

For now, let's just run the example log file with the default parser:

```sh
./build/SATSentinel tests/data/example.log
```

### How to use SATSentinel
The sentinel makes the distinction between two types of commands:
- navigation commands, which are used to navigate through the execution of the solver, and
- user commands, which are used to interact with the solver and the sentinel.
The CLI will clearly display which type of command is expected at any point. The navigation commands are detailed in [Navigation & Debugging](docs/Navigation-and-Debugging.md), while the user commands are detailed in [Command Language & Replay](docs/Command-Language-and-Replay.md).

Try running the example log file and use the empty navigation command to step forward by typing the `enter` key, or typing `next` and pressing `enter`. You can also type `help` to see the list of available navigation commands. You may type `b` or `back` to step backward in the execution.

Right now, the sentinel will only skip though all the notifications. And after a few steps, it will ask for a user command. You may type `help` to see the list of available user commands. Try to add a new clause with the `NEW CLAUSE 1 v1 ~v2` command. The first number is the clause index, and the rest are the literals of the clause.

#### Display level
An important and powerful tool in the SATSentinel is the display level. The display level controls the level of granularity of the information displayed by the sentinel. The display level can be set using the `set level <l>` command, where `<l>` is an integer between 0 and 10. The higher the level, the more information is displayed. The display level can be changed at any time as a navigation command. The information displayed at each level is detailed in [Navigation & Debugging](docs/Navigation-and-Debugging.md).

Use the `set level 5` command to set the display level to 5, which will show more details about the notifications. You can now go through the notifications forwards and backwards with more details.

#### Breakpoints and markers
The sentinel allows the user to set breakpoints at specific notification numbers, which will override the display level of that notification to 0. Markers do a similar thing, but for variables and clauses. When a marker is set on a variable or clause, any notification that touches that variable or clause will have its display level overridden to 0. Breakpoints and markers can be set and removed at any time as navigation commands. The commands to set and remove breakpoints and markers are detailed in [Navigation & Debugging](docs/Navigation-and-Debugging.md).

Set the level back to 0 with the `set level 0` command. Let's now mark variable 2 with the `mark var 2` command. You can now go through the notifications forwards and backwards, and the sentinel will stop at any notification that touches variable 2.

## Acknowledgements
The first version of this project was developed at the TU Wien to support the [NapSAT](https://github.com/RobCoutel/NapSAT) SAT Solver. The project was later severed from NapSAT and is now independent.

The code for the GUI was written with the help of Claude Code. The core was taken from NapSAT and modified by Robin Coutelier, who still maintains the project.
