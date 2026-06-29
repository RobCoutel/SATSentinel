#pragma once

#include "Sentinel-types.hpp"
#include "Sentinel-options.hpp"
#include "Sentinel-invariants.hpp"

namespace sentinel
{
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

  class SATSentinel;
  /**
   * @brief Creates a new SAT sentinel instance.
   * @param options Configuration options for the sentinel.
   * @return A newly allocated sentinel instance.
   */
  SATSentinel* create_sentinel(const SentinelOptions& options);

  /**
   * @brief Destroys a sentinel instance and releases all associated resources.
   * @param sentinel Sentinel to destroy.
   */
  void delete_sentinel(SATSentinel* sentinel);

  /**
   * @brief Registers a new variable.
   * @param sentinel Sentinel instance.
   * @param v Variable to add.
   * @pre v ∉ V
   */
  bool add_variable(SATSentinel* sentinel, Tvar v);

  /**
   * @brief Associates a human-readable name with a variable.
   * @param sentinel Sentinel instance.
   * @param v Variable whose alias is updated.
   * @param alias New alias.
   * @pre v ∈ V
   */
  bool set_variable_alias(SATSentinel* sentinel, Tvar v, std::string alias);

  /**
   * @brief Adds a clause to the clause database.
   * @param sentinel Sentinel instance.
   * @param cl Clause identifier.
   * @param lits Literals of the clause.
   * @param size Number of literals.
   * @param external Whether the clause originates externally.
   * @pre cl ∉ F
   * @pre ∀ℓ ∈ lits. var(ℓ) ∈ V
   * @post cl ∈ F
   * @post cl = {lits[0], lits[1], ..., lits[size-1]}
   */
  bool add_clause(SATSentinel* sentinel,
                  Tclause cl,
                  const Tlit* lits,
                  unsigned int size,
                  bool external = false);

  /**
   * @brief Deletes a clause from the clause database.
   * @param sentinel Sentinel instance.
   * @param clause Clause to delete.
   * @pre cl ∈ F
   * @post cl ∉ F
   */
  bool delete_clause(SATSentinel* sentinel,
                     Tclause cl);

  /**
   * @brief Removes one literal from a clause.
   * @param sentinel Sentinel instance.
   * @param clause Clause to shrink.
   * @param removed_lit Literal to remove.
   * @pre cl ∈ F
   * @pre removed_lit ∈ clause
   */
  bool shrink_clause(SATSentinel* sentinel,
                     Tclause cl,
                     Tlit removed_lit);

  /**
   * @brief Assigns a literal.
   * @param sentinel Sentinel instance.
   * @param lit Literal ℓ to assign.
   * @param reason Reason clause R, or ■ for a decision.
   * @pre  ℓ ∉ π
   * @post ℓ ∈ π
   * @post R = ■ ⇒ ℓ ∈ ω ∧ ℓ ∈ πᵈ ∧ ℓ ∉ τ
   * @post R ≠ ■ ⇒ ℓ ∈ ω ∧ ℓ ∈ τ  ∧ ℓ ∉ πᵈ
   * @post ρ(ℓ) = R
   */
  bool assign(SATSentinel* sentinel,
              Tlit lit,
              Tclause reason = CLAUSE_UNDEF);

  /**
   * @brief Removes a literal from the assignment.
   * @param sentinel Sentinel instance.
   * @param ℓ Literal to unassign.
   * @pre  ℓ ∈ π
   * @post ℓ ∉ π
   */
  bool unassign(SATSentinel* sentinel, Tlit ℓ);

  /**
   * @brief Pushes a propagated literal onto the propagation queue.
   * @param sentinel Sentinel instance.
   * @param lit Literal ℓ to propagate.
   * @pre var(ℓ) ∈ V
   * @pre ℓ ∈ π
   * @pre ℓ ∈ ω
   * @pre ℓ ∉ τ
   * @post ℓ ∉ ω
   * @post ℓ ∈ τ
   */
  bool propagate(SATSentinel* sentinel, Tlit lit);

  /**
   * @brief Removes a propagated literal from the propagation queue.
   * @param sentinel Sentinel instance.
   * @param lit Literal ℓ to remove.
   * @pre ℓ ∈ τ
   * @pre ℓ ∉ ω
   * @post ℓ ∉ τ
   * @post ℓ ∈ ω
   */
  bool unpropagate(SATSentinel* sentinel, Tlit blocker);

  /**
   * @brief Updates the decision level of a literal.
   * @param sentinel Sentinel instance.
   * @param lit Literal ℓ to update.
   * @param level New decision level.
   * @pre ℓ ∈ π
   * @post δ(ℓ) = level
   */
  bool update_level(SATSentinel* sentinel,
                    Tlit lit,
                    Tlevel level);

  /**
   * @brief Updates the reason clause of a propagated literal.
   * @param sentinel Sentinel instance.
   * @param lit Literal ℓ to update.
   * @param reason New reason clause.
   * @pre ℓ ∈ π
   * @post ρ(ℓ) = reason
   */
  bool update_reason(SATSentinel* sentinel,
                     Tlit lit,
                     Tclause reason);

  /**
   * @brief Adds a clause to the watch list of a literal.
   * @param sentinel Sentinel instance.
   * @param clause Clause to watch.
   * @param lit New  watched literal.
   * @pre clause ∈ F
   * @post clause ∈ WL(ℓ)
   */
  bool watch(SATSentinel* sentinel,
             Tclause clause,
             Tlit lit);

  /**
   * @brief Removes a clause from the watch list of a literal.
   * @param sentinel Sentinel instance.
   * @param clause Clause to remove.
   * @param lit Previous watch lit ℓ.
   * @pre clause ∈ WL(ℓ)
   * @post clause ∉ WL(ℓ)
   */
  bool unwatch(SATSentinel* sentinel,
               Tclause clause,
               Tlit lit);

  /**
   * @brief Records the blocking literal of a watched clause.
   * @param sentinel Sentinel instance.
   * @param clause Clause to update.
   * @param blocker New blocking literal.
   * @param watch Optional watched literal.
   * @pre clause ∈ F
   * @pre blocker ∈ clause
   * @pre clause ∈ WL(watch)
   * @post blocker is the blocking literal of clause
   */
  bool block(SATSentinel* sentinel,
             Tclause clause,
             Tlit blocker,
             Tlit watch);

  /**
   * @brief Locks a literal as an assumption.
   * @param sentinel Sentinel instance.
   * @param lit Literal to lock.
   * @pre var(lit) ∈ V
   */
  bool lock_assumption(SATSentinel* sentinel, Tlit lit);

  bool unlock_assumption(SATSentinel* sentinel, Tlit lit);

  /**
   * @brief Verifies all maintained invariants.
   * @param sentinel Sentinel instance.
   * @return True iff all invariants hold.
   */
  bool check_invariants(SATSentinel* sentinel);

  /**
   * @brief Creates an execution checkpoint.
   * @param sentinel Sentinel instance.
   */
  bool checkpoint(SATSentinel* sentinel);

  /**
   * @brief Emits a diagnostic message.
   * @param sentinel Sentinel instance.
   * @param message Message text.
   * @param level Verbosity level.
   */
  bool message(SATSentinel* sentinel,
               const std::string& message,
               unsigned level = 0);

  /**
   * @brief Saves the recorded execution to a file.
   * @param sentinel Sentinel instance.
   * @param filename Output filename.
   */
  bool save_execution(SATSentinel* sentinel,
                      const std::string& filename);

  /**
   * @brief Loads a previously recorded execution.
   * @param sentinel Sentinel instance.
   * @param filename Input filename.
   */
  bool load_execution(SATSentinel* sentinel,
                      const std::string& filename);

  /**
   * @brief Sets the command parser used by the sentinel.
   * @param sentinel Sentinel instance.
   * @param parser Command parser.
   */
  void set_command_parser(SATSentinel* sentinel,
                          Tparser* parser);

  /**
   * @brief Registers a custom invariant checker.
   * @param sentinel Sentinel instance.
   * @param custom_checker Function that checks the invariant. It should return true if the invariant holds, and false otherwise. If the invariant fails, it should set the error message in the provided string reference.
   * @param name Name of the custom invariant for identification in error messages.
   */
  void add_invariant(SATSentinel* sentinel,
                     std::function<bool(std::string&)> custom_checker,
                     const std::string& name);

  void add_invariant(SATSentinel* sentinel,
                     Invariant* invariant);

  /**
   * @brief Registers a custom watch invariant checker.
   * @param sentinel Sentinel instance.
   * @param custom_checker Function that checks the watch invariant. It should return true if the invariant holds, and false otherwise. If the invariant fails, it should set the error message in the provided string reference. The arguments of the functions are the two watched literals and the blocking literal for the first watcher.
   * @param name Name of the custom watch invariant for identification in error messages.
   */
  void add_watch_invariant(SATSentinel* sentinel,
                           std::function<bool(Tlit, Tlit, Tlit, std::string&)> custom_checker,
                           const std::string& name);

  void add_watch_invariant(SATSentinel* sentinel,
                           WatchInvariant* invariant);
}
