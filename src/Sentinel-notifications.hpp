/*
 * This file is part of the source code of the software program
 * SATSentinel. It is protected by applicable copyright laws.
 *
 * This source code is protected by the terms of the MIT License.
 */
/**
 * @file src/Sentinel-notifications.hpp
 * @author Robin Coutelier
 *
 * @brief Declaration of the notification base class and all 26 concrete notification types that
 * represent solver events (variable creation, assignment, propagation, clause management, etc.).
 */
#pragma once

#include "Sentinel-types.hpp"
#include "Sentinel-state.hpp"

#include <vector>
#include <string>
#include <cassert>

namespace sentinel
{
  class SentinelMarker;
}

/**
 * Vocabulary:
 * - Variable: a variable is a literal without its sign.
 * - Literal: a literal is a variable with its sign.
 * - Clause: a clause is a disjunction of literals.
 * - Assignment: an assignment is a set of literals.
 * - A variable is assigned if it, or its negation, is in the assignment.
 * - A literal l is implied by an assignment and a clause if the clause is unit under the assignment and l is the only literal of the clause that is not falsified by the assignment.
 * - A literal l is propagated when it has been checked that it does not create a conflict and the literals implied by l have been added to the assignment.
 * - A literal is a decision literal if it is not implied by the current assignment but does not create a conflict when it is added to the assignment.
 */
namespace sentinel::notif
{
  const unsigned MAX_UNSIGNED = 0xFFFFFFFF;

  enum ENotifType
  {
    MESSAGE,

    VARIABLE_NEW,

    CLAUSE_NEW,
    CLAUSE_REMOVED,
    CLAUSE_SHRINKED,

    ASSIGNMENT,
    UNASSIGNMENT,

    PROPAGATION,
    UNPROPAGATION,
    LEVEL_UPDATE,
    REASON_UPDATE,

    WATCH,
    UNWATCH,
    BLOCKER,

    ALTERNATIVE_REASON_ADDED,
    ALTERNATIVE_REASON_REMOVED,

    LOCK,
    UNLOCK
  };
  std::string notification_type_to_string(ENotifType type);

  /**
   * @brief Virtual class that defines notifications that can be sent by the SAT solver to the observer.
   */
  class notification
  {
  public:
    /**
     * @brief Returns the event level of the notification. If the notification touches a marked object, the level will be 0. Otherwise, the level will be determine by the type of the notification. The level is used by the observer to determine when to display the state of the solver.
     */
    virtual unsigned get_event_level(SentinelMarker* marker) const noexcept = 0;

    /**
     * @brief Returns the type of the notification.
     */
    virtual ENotifType get_type() const noexcept = 0;

    /**
     * @brief Returns a short string describing the event.
     * @return const std::string A short string describing the event.
     */
    virtual const std::string get_message() const noexcept = 0;

    /**
     * @brief Applies the notification to the observer.
     * @details Also updates internal variables of the notification to allow rollback.
     * @param state The state of the solver that will be modified by the notification.
     */
    virtual bool apply(SentinelState* state) = 0;

    /**
     * @brief Rollbacks the notification from the observer.
     * @param state The state of the solver that will be modified by the notification.
     */
    virtual bool rollback(SentinelState* state) = 0;

    /**
     * @brief Destroy the notification object
     */
    virtual ~notification() = default;
  };

  /**
   * @brief Notification that a new variable was added.
   */
  class new_variable : public notification
  {
  private:
    /**
     * @brief The variable that was added.
     */
    Tvar var;

  public:
    static const unsigned DEFAULT_LEVEL = 4;
    static const ENotifType NTYPE = VARIABLE_NEW;

    unsigned get_event_level(SentinelMarker* marker) const noexcept override;
    ENotifType get_type() const noexcept override { return NTYPE; }
    const std::string get_message() const noexcept override { return "New variable " + var.to_string() + " added"; }

    explicit new_variable(Tvar var) : var(var) {}

    bool apply(SentinelState* state) override;
    bool rollback(SentinelState* state) override;
  };

  /**
   * @brief Notification that a literal was decided.
   * @details A literal is decided when it is added to the assignment arbitrarily, without being implied by the current assignment.
   */
  class assignment : public notification
  {
  private:
    /**
     * @brief The literal that was decided.
     */
    Tlit lit;

    Tclause reason;

  public:
    static const unsigned DEFAULT_LEVEL = 2;
    static const ENotifType NTYPE = ASSIGNMENT;

    unsigned get_event_level(SentinelMarker* marker) const noexcept override;
    ENotifType get_type() const noexcept override { return NTYPE; }
    const std::string get_message() const noexcept override { return "Assigned literal : " + lit.to_string() + " with reason " + reason.to_string(); }

    explicit assignment(Tlit lit, Tclause reason) : lit(lit), reason(reason) {}

    bool apply(SentinelState* state) override;
    bool rollback(SentinelState* state) override;
  };

  class update_level : public notification
  {
  private:
    /**
     * @brief The literal that was updated.
     */
    Tlit lit;

    /**
     * @brief The new level of the literal.
     */
    Tlevel level = LEVEL_UNDEF;
    Tlevel old_level = LEVEL_UNDEF;

  public:
    static const unsigned DEFAULT_LEVEL = 5;
    static const ENotifType NTYPE = LEVEL_UPDATE;

    unsigned get_event_level(SentinelMarker* marker) const noexcept override;
    ENotifType get_type() const noexcept override { return NTYPE; }
    const std::string get_message() const noexcept override { return "Update level : " + lit.to_string() + " updated to level " + level.to_string(); }

    explicit update_level(Tlit lit, Tlevel level) : lit(lit), level(level) {}

    bool apply(SentinelState* state) override;
    bool rollback(SentinelState* state) override;
  };

  class update_reason : public notification
  {
  private:
    /**
     * @brief The literal that was updated.
     */
    Tlit lit;
    /**
     * @brief The new reason of the literal.
     */
    Tclause reason = CLAUSE_UNDEF;
    Tclause old_reason = CLAUSE_UNDEF;
  public:
    static const unsigned DEFAULT_LEVEL = 5;
    static const ENotifType NTYPE = REASON_UPDATE;
    unsigned get_event_level(SentinelMarker* marker) const noexcept override;
    ENotifType get_type() const noexcept override { return NTYPE; }
    const std::string get_message() const noexcept override { return "Update reason : " + lit.to_string() + " updated to reason " + reason.to_string(); }

    explicit update_reason(Tlit lit, Tclause reason) : lit(lit), reason(reason) {}

    bool apply(SentinelState* state) override;
    bool rollback(SentinelState* state) override;
  };

  /**
   * @brief Notification that a literal was propagated.
   */
  class propagation : public notification
  {
  private:
    /**
     * @brief The literal that was propagated.
     */
    Tlit lit;

  public:
    static const unsigned DEFAULT_LEVEL = 6;
    static const ENotifType NTYPE = PROPAGATION;

    unsigned get_event_level(SentinelMarker* observer) const noexcept override;
    ENotifType get_type() const noexcept override { return NTYPE; }
    const std::string get_message() const noexcept override { return "Propagation : " + lit.to_string() + " propagated"; }

    explicit propagation(Tlit lit) : lit(lit) {}

    bool apply(SentinelState* state) override;
    bool rollback(SentinelState* state) override;
  };

  /**
   * @brief Notification that a literal was propagated.
   */
  class propagation_removed : public notification
  {
  private:
    /**
     * @brief The literal that was propagated.
     */
    Tlit lit;

  public:
    static const unsigned DEFAULT_LEVEL = 6;
    static const ENotifType NTYPE = UNPROPAGATION;

    unsigned get_event_level(SentinelMarker* marker) const noexcept override;
    ENotifType get_type() const noexcept override { return NTYPE; }
    const std::string get_message() const noexcept override { return "Propagation removed : " + lit.to_string(); }

    explicit propagation_removed(Tlit lit) : lit(lit) {}

    bool apply(SentinelState* state) override;
    bool rollback(SentinelState* state) override;
  };

  /**
   * @brief Notification that a literal was unassigned.
   */
  class unassignment : public notification
  {
  private:
    /**
     * @brief The literal that was unassigned.
     */
    Tlit lit;

    /**
     * @brief The state variable before it was unassigned. This is used to rollback the unassignment.
     */
    SentinelState::variable var;

  public:
    static const unsigned DEFAULT_LEVEL = 4;
    static const ENotifType NTYPE = UNASSIGNMENT;

    unsigned get_event_level(SentinelMarker* marker) const noexcept override;
    ENotifType get_type() const noexcept override { return NTYPE; }
    const std::string get_message() const noexcept override { return "Unassignment : " + lit.to_string() + " unassigned"; }

    explicit unassignment(Tlit lit) : lit(lit), var(lit.var()) {}

    bool apply(SentinelState* state) override;
    bool rollback(SentinelState* state) override;
  };

  /**
   * @brief Notification that a new clause was added.
   */
  class new_clause : public notification
  {
  private:
    /**
     * @brief The clause id that was added.
     */
    Tclause cl;

    /**
     * @brief The literals of the clause.
     */
    std::vector<Tlit> lits;

    /**
     * @brief True if the clause was added externally (by the user or from the problem statement).
     */
    bool external = false;

  public:
    static const unsigned DEFAULT_LEVEL = 3;
    static const ENotifType NTYPE = CLAUSE_NEW;

    unsigned get_event_level(SentinelMarker* marker) const noexcept override;
    ENotifType get_type() const noexcept override { return NTYPE; }
    const std::string get_message() const noexcept override;

    explicit new_clause(Tclause cl, std::vector<Tlit> lits, bool external);

    bool apply(SentinelState* state) override;
    bool rollback(SentinelState* state) override;
  };

  /**
   * @brief Notification that a clause was deleted.
   */
  class delete_clause : public notification
  {
  private:
    /**
     * @brief The clause id that was deleted.
     */
    Tclause cl;

    SentinelState::clause deleted_clause;

    /**
     * Id computed by the observer to identify the clause.
     */
    unsigned long hash = 0;

  public:
    static const unsigned DEFAULT_LEVEL = 3;
    static const ENotifType NTYPE = CLAUSE_REMOVED;

    unsigned get_event_level(SentinelMarker* marker) const noexcept override;
    ENotifType get_type() const noexcept override { return NTYPE; }
    const std::string get_message() const noexcept override { return "Delete clause : " + cl.to_string(); }

    explicit delete_clause(Tclause cl) : cl(cl) {}

    bool apply(SentinelState* state) override;
    bool rollback(SentinelState* state) override;
  };

  class watch : public notification
  {
  private:
    /**
     * @brief The clause id that was deleted.
     */
    Tclause cl;

    /**
     * @brief The literal that was watches.
     */
    Tlit lit;

    unsigned index = 0;

  public:
    static const unsigned DEFAULT_LEVEL = 9;
    static const ENotifType NTYPE = WATCH;

    unsigned get_event_level(SentinelMarker* marker) const noexcept override;
    ENotifType get_type() const noexcept override { return NTYPE; }
    const std::string get_message() const noexcept override { return "Watch literal : " + lit.to_string() + " in clause " + cl.to_string(); }

    explicit watch(Tclause cl, Tlit lit) : cl(cl), lit(lit) {}

    bool apply(SentinelState* state) override;
    bool rollback(SentinelState* state) override;
  };

  class unwatch : public notification
  {
  private:
    /**
     * @brief The clause id that was deleted.
     */
    Tclause cl;

    /**
     * @brief The literal that was watches.
     */
    Tlit lit;

    Tlit previous_blocker = LIT_UNDEF;

    unsigned index = 0;

  public:
    static const unsigned DEFAULT_LEVEL = 9;
    static const ENotifType NTYPE = UNWATCH;

    unsigned get_event_level(SentinelMarker* marker) const noexcept override;
    ENotifType get_type() const noexcept override { return NTYPE; }
    const std::string get_message() const noexcept override { return "Unwatch literal : " + lit.to_string() + " in clause " + cl.to_string(); }

    explicit unwatch(Tclause cl, Tlit lit) : cl(cl), lit(lit) {}

    bool apply(SentinelState* state) override;
    bool rollback(SentinelState* state) override;
  };

  /**
   * @brief Notification that a blocker was set to a clause.
  */
  class block : public notification
  {
  private:
    /**
     * @brief The clause id that was deleted.
     */
    Tclause cl;

    /**
     * @brief The  blocker that was set
     */
    Tlit blocker;

    /**
     * @brief The literal that is blocked in the watch list.
     */
    Tlit blocked_lit;

    /**
     * @brief The previous blocker of the clause (for rollback)
    */
    Tlit previous_blocker = LIT_UNDEF;

  public:
    static const unsigned DEFAULT_LEVEL = 9;
    static const ENotifType NTYPE = BLOCKER;

    unsigned get_event_level(SentinelMarker* marker) const noexcept override;
    ENotifType get_type() const noexcept override { return NTYPE; }
    const std::string get_message() const noexcept override { return "Block literal : " + blocker.to_string() + " in clause " + cl.to_string(); }

    explicit block(Tclause cl, Tlit blocker, Tlit blocked_lit) : cl(cl), blocker(blocker), blocked_lit(blocked_lit) {}

    bool apply(SentinelState* state) override;
    bool rollback(SentinelState* state) override;
  };

  /**
   * @brief Notification that a literal has be removed from a clause.
   * @pre The literal should be a level 0 literal.
   */
  class remove_literal : public notification
  {
  private:
    /**
     * @brief The clause id that was deleted.
     */
    Tclause cl;

    /**
     * @brief The literal that was removed.
     */
    Tlit lit;

  public:
    static const unsigned DEFAULT_LEVEL = 9;
    static const ENotifType NTYPE = CLAUSE_SHRINKED;

    unsigned get_event_level(SentinelMarker* marker) const noexcept override;
    ENotifType get_type() const noexcept override { return NTYPE; }
    const std::string get_message() const noexcept override { return "Remove literal : " + lit.to_string() + " from clause " + cl.to_string(); }

    explicit remove_literal(Tclause cl, Tlit lit) : cl(cl), lit(lit) {}

    bool apply(SentinelState* state) override;
    bool rollback(SentinelState* state) override;
  };

  // lock for assumptions
  class lock_assumption : public notification
  {
  private:
    Tlit lit;
  public:
    static const unsigned DEFAULT_LEVEL = 5;
    static const ENotifType NTYPE = LOCK;
    unsigned get_event_level(SentinelMarker* marker) const noexcept override;
    ENotifType get_type() const noexcept override { return NTYPE; }
    const std::string get_message() const noexcept override { return "Lock assumption: " + lit.to_string(); }

    explicit lock_assumption(Tlit lit) : lit(lit) {}

    bool apply(SentinelState* state) override;
    bool rollback(SentinelState* state) override;
  };

  // unlock for assumptions
  class unlock_assumption : public notification
  {
  private:
    Tlit lit;
  public:
    static const unsigned DEFAULT_LEVEL = 5;
    static const ENotifType NTYPE = UNLOCK;
    unsigned get_event_level(SentinelMarker* marker) const noexcept override;
    ENotifType get_type() const noexcept override { return NTYPE; }
    const std::string get_message() const noexcept override { return "Unlock assumption: " + lit.to_string(); }

    explicit unlock_assumption(Tlit lit) : lit(lit) {}

    bool apply(SentinelState* state) override;
    bool rollback(SentinelState* state) override;
  };

  class message : public notification
  {  private:
    std::string m;
    const unsigned notif_lvl;
  public:
    static const ENotifType NTYPE = MESSAGE;
    unsigned get_event_level(SentinelMarker* marker) const noexcept override { return notif_lvl; }
    ENotifType get_type() const noexcept override { return NTYPE; }
    const std::string get_message() const noexcept override { return "Message : " + m; }
    explicit message(std::string message, unsigned lvl = 0) : m(std::move(message)), notif_lvl(lvl) {}
    bool apply(SentinelState* state) override { return true; }
    bool rollback(SentinelState* state) override { return true; }
   };

}
