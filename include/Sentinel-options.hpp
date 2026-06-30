/*
 * This file is part of the source code of the software program
 * SATSentinel. It is protected by applicable copyright laws.
 *
 * This source code is protected by the terms of the MIT License.
 */
/**
 * @file include/Sentinel-options.hpp
 * @author Robin Coutelier
 *
 * @brief SentinelOptions configuration struct controlling interactive mode, display level, and
 * which built-in invariant checks are enabled at runtime.
 */
#pragma once

#include <string>

namespace sentinel
{
  struct SentinelOptions
  {
    /**
     * @brief If true, when the user sends a checkpoint, the sentinel will interrupt the solver execution and pass the user command to the solver.
     */
    bool interactive = false;

    /**
     * @brief Default display level. Notifications with a level higher than or equal to this level will interrupt the solver, display the state, and prompt the user for a navigation command.
     */
    int default_display_level = 0;

    /**
     * @brief If true, the sentinel will never interrupt the solver execution, and will not display the state. However, if a notification fails, the sentinel will display an error.
     */
    bool check_only = false;

    /**
     * @brief If true, the sentinel will crash the execution if a notification fails. Otherwise, the sentinel will display an error and continue the execution.
     */
    bool crash_on_error = false;

  /**
   * Terminology:
   * we call: V the set of variables
   *          F the set of clauses
   *          π the complete partial assignment
   *          πᵈ the set of decision literals
   *          τ the set of propagated literals
   *          ω the propagation queue
   *          δ(ℓ) the decision level of ℓ
   *          ρ(ℓ) the reason of ℓ
   *          WL(ℓ) the watch list of ℓ
   *          ■ the undefined clause
   *
   * - A literal ℓ is "assigned" if it is in π.
   * - A literal ℓ is "propagated" if it is in τ.
   * - A literal ℓ is a "decision" if it is in πᵈ.
   * - A literal ℓ is "implied" if it is assigned and not a decision.
   * - The "reason" of a literal ℓ is the clause ρ(ℓ) that implies ℓ, or ■ if ℓ is a decision.
   *
   * Note : π = τ ⋅ ω
   *        πᵈ ⊆ π
   *        ∀ℓ ∈ π. ¬ℓ ∉ π
   */

    /**
     * @brief If true, the sentinel will check for the sanity of the trail. That is, it will check that no clause is falsified by the trail.
     * @details For each clause C ∈ F, check if
     *   |C \ {¬ℓ : ℓ ∈ τ}| > 0
     * @note Invariant Cost: O(|F| * |C|) where |F| is the number of clauses and |C| is the size of the largest clause.
     */
    bool check_no_conflicts = false;

    /**
     * @brief If true, the sentinel will check that there is not clause C such that only one literal is C is undefined, and the others are falsified.
     * @details For each clause C ∈ F, check if
     *   |C \ {¬ℓ : ℓ ∈ τ}| = 1 ⇒ C ∩ π ≠ ∅
     * @note Invariant Cost: O(|F| * |C|) where |F| is the number of clauses and |C| is the size of the largest clause.
     */
    bool check_no_missed_implications = false;

    /**
     * @brief If true, the sentinel will check that literals are implied at the correct decision level.
     * @details For each implied literal ℓ ∈ π \ πᵈ, check if
     *    δ(ℓ) = max{δ(ℓ') : ℓ' ∈ ρ(ℓ) \ {ℓ}}
     * @note Invariant Cost: O(|π| * |C|) where |π| is the number of literals on the trail and |C| is the size of the largest clause.
     */
    bool check_implied_levels = false;

    /**
     * @brief If true, the sentinel will check that the trail is monotonic with respect to the decision levels of the literals.
     * @details For each pair of consecutive literals ℓᵢ, ℓᵢ₊₁ ∈ π, check if
     *   δ(ℓᵢ) ≤ δ(ℓᵢ₊₁)
     * @note Invariant Cost: O(|π|) where |π| is the number of literals on the trail.
     */
    bool check_trail_monotonicity = false;

    /**
     * @brief If true, the sentinel will check that the trail is a topological sort of the implication graph.
     * @details For each implied literal ℓ with reason clause ρ(ℓ) ≠ ■, all the literals in the reason clause must be assigned before ℓ in the trail. That is,
     *   ∀ ℓ ∈ π. ρ(ℓ) ≠ ■ ⇒ ∀ ℓ' ∈ ρ(ℓ). p(ℓ') ≤ p(ℓ)
     * where p(ℓ) is the position of ℓ in the trail.
     * @note Invariant Cost: O(|π| * |C|) where |π| is the number of literals on the trail and |C| is the size of the largest clause.
     */
    bool check_topological_order = false;

    /**
     * @brief If true, the sentinel will check that the reasons of the implied literals are correct.
     * @details For each implied literal ℓ with reason clause ρ(ℓ) ≠ ■,
     * ∀ ℓ ∈ π. ρ(ℓ) ≠ ■ ⇒ [(ρ(ℓ) \ {ℓ} ∧ π ⊧ ⊥) ∧ (ℓ ∈ ρ(ℓ))]
     * @note Invariant Cost: O(|π| * |C|) where |π| is the number of literals on the trail and |C| is the size of the largest clause.
     */
    bool check_assignment_coherence = false;

    /**
     * @brief If true, the sentinel will check that the weak watched literal with blocker invariant holds
     * Invariant 1 of [Lazy Reimplication in Chronological Backtracking, 2024, Coutelier et al.]
     * @details For each clause C ∈ F with |C| > 2, watched by c₁ and c₂, and with a blocker b for c₁, the following property must hold:
     *   ¬c₁ ∈ τ ⇒ (¬c₂ ∉ τ ∨ b ∈ π)
     * @note Invariant Cost: O(|F|) where |F| is the number of clauses.
     */
    bool check_weak_watched_literals = false;

    /**
     * @brief If true, the sentinel will check that the weak watched literal with blocker invariant holds
     * Invariant 3 of [Lazy Reimplication in Chronological Backtracking, 2024, Coutelier et al.]
     * @details For each clause C ∈ F with |C| > 2, watched by c₁ and c₂, and with a blocker b for c₁, the following property must hold:
     *   ¬c₁ ∈ τ ⇒ (c₂ ∈ π ∨ b ∈ π)
     * @note Invariant Cost: O(|F|) where |F| is the number of clauses.
     */
    bool check_strong_watched_literals = false;
  };
}
