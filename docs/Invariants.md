# Invariants

SATSentinel can continuously check the mirrored solver state against a library of
correctness properties, either every time you call `check_invariants()` /
`CHECK INVARIANTS` explicitly, or automatically whenever the CDCL literature's usual
invariants would be expected to hold. All are defined in `include/Sentinel-options.hpp`
(the enabling flags) and implemented in `src/Sentinel-state.cpp`.

Each is off by default (they range from `O(|F|)` to `O(|π| · |C|)` per check, so enable
only what you need for the run you're debugging).

## Built-in invariants

| Option flag | CLI flag | Checks |
|---|---|---|
| `check_no_conflicts` | `--check-no-conflicts` | No clause `C ∈ F` is falsified by the trail: `\|C \ {¬ℓ : ℓ ∈ τ}\| > 0`. |
| `check_no_missed_implications` | `--check-no-missed-implications` | No clause is unit-but-not-yet-propagated: `\|C \ {¬ℓ : ℓ ∈ τ}\| = 1 ⇒ C ∩ π ≠ ∅`. |
| `check_implied_levels` | `--check-implied-levels` | Every implied literal `ℓ ∈ π \ πᵈ` sits at `δ(ℓ) = max{δ(ℓ') : ℓ' ∈ ρ(ℓ) \ {ℓ}}`. |
| `check_trail_monotonicity` | `--check-trail-monotonicity` | Consecutive trail literals `ℓᵢ, ℓᵢ₊₁` satisfy `δ(ℓᵢ) ≤ δ(ℓᵢ₊₁)`. |
| `check_topological_order` | `--check-topological-order` | The trail is a topological sort of the implication graph: every literal in `ρ(ℓ)` is assigned before `ℓ`. |
| `check_assignment_coherence` | `--check-assignment-coherence` | Every implied literal's reason clause actually justifies it (`ρ(ℓ) \ {ℓ}` is falsified, and `ℓ ∈ ρ(ℓ)`). |
| `check_weak_watched_literals` | `--check-weak-watched-literals` | Weak watched-literal-with-blocker invariant (Invariant 1 of *Lazy Reimplication in Chronological Backtracking*, Coutelier et al. 2024): `¬c₁ ∈ τ ⇒ (¬c₂ ∉ τ ∨ b ∈ π)`. |
| `check_strong_watched_literals` | `--check-strong-watched-literals` | Strong variant (Invariant 3, same paper): `¬c₁ ∈ τ ⇒ (c₂ ∈ π ∨ b ∈ π)`. |
| `check_repetition` | `--check-repetition` | The current assignment `π` (as a set, independent of trail order) has not already occurred earlier in this execution: `π ∉ Π`, where `Π` is the set of assignments observed by previous checks. `π` is recorded into `Π` either way, so the next repeat is also caught. If `π` was last recorded at the same notification index as the current check, the check passes trivially (this is just the invariant having been invoked twice for the same notification, not a real repetition). |

The last two are checked per-clause via a `WatchInvariant` (they need both watched
literals and the blocker, not just global state — see below); the rest are plain
`Invariant`s checked once over the whole state.

## Running the checks

```cpp
bool check_invariants(SATSentinel* sentinel);   // from Sentinel-API.hpp
```

or, interactively/from a script, the `CHECK INVARIANTS` command (see
[Command Language & Replay](Command-Language-and-Replay.md)). A failure prints the
offending invariant's name and its error message; it does **not** stop the run unless
`Options::crash_on_error` is set.

## Custom invariants

Beyond the nine built-ins, you can register your own — useful for solver-specific
properties (e.g. heuristic bookkeeping consistency) that SATSentinel can't know about
generically.

```cpp
// Whole-state invariant: checked once per check_invariants() call.
void add_invariant(SATSentinel* sentinel,
                    std::function<bool(std::string&)> custom_checker,
                    const std::string& name);
void add_invariant(SATSentinel* sentinel, Invariant* invariant);

// Per-watched-clause invariant: checked once per (c1, c2, blocker) watch triple.
void add_watch_invariant(SATSentinel* sentinel,
                          std::function<bool(Tlit, Tlit, Tlit, std::string&)> custom_checker,
                          const std::string& name);
void add_watch_invariant(SATSentinel* sentinel, WatchInvariant* invariant);
```

The checker returns `true` if the invariant holds; on `false`, it should write a
human-readable explanation into the `std::string&` output parameter, which
`check_invariants()` then reports alongside the invariant's name.

```cpp
add_invariant(sentinel, [](std::string& err) {
  // ... inspect your own solver-side state ...
  if (/* violated */) {
    err = "heuristic score table out of sync with the trail";
    return false;
  }
  return true;
}, "Heuristic Score Sync");
```
