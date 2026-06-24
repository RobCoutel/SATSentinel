#include "Sentinel-notifications.hpp"

#include "SATSentinel.hpp"

#include "Sentinel-types.hpp"

#include <iostream>
#include <algorithm>

using namespace std;

#define SOFT_ASSERT(cond) if (!(cond))                \
do {                                             \
  std::cerr << "Assertion failed: " << #cond << std::endl; \
  return false;                                  \
} while (0)

namespace sentinel
{
  std::string notif::notification_type_to_string(ENotifType type)
  {
    switch (type) {
    case MESSAGE: return "MESSAGE";
    case CHECK_INVARIANTS: return "CHECK_INVARIANTS";
    case VARIABLE_NEW: return "VARIABLE_NEW";
    case CLAUSE_NEW: return "CLAUSE_NEW";
    case CLAUSE_REMOVED: return "CLAUSE_REMOVED";
    case CLAUSE_SHRINKED: return "CLAUSE_SHRINKED";
    case ASSIGNMENT: return "ASSIGNMENT";
    case UNASSIGNMENT: return "UNASSIGNMENT";
    case PROPAGATION: return "PROPAGATION";
    case UNPROPAGATION: return "UNPROPAGATION";
    case LEVEL_UPDATE: return "LEVEL_UPDATE";
    case REASON_UPDATE: return "REASON_UPDATE";
    case WATCH: return "WATCH";
    case UNWATCH: return "UNWATCH";
    case BLOCKER: return "BLOCKER";
    case ALTERNATIVE_REASON_ADDED: return "ALTERNATIVE_REASON_ADDED";
    case ALTERNATIVE_REASON_REMOVED: return "ALTERNATIVE_REASON_REMOVED";
    case LOCK: return "LOCK";
    case UNLOCK: return "UNLOCK";
    }
    assert(false);
    return "";
  }

  /** NEW VARIABLE **/
  unsigned notif::new_variable::get_event_level(SentinelMarker* marker) const noexcept
  {
    assert(marker);
    if (marker->is_marked(var))
      return 0;
    return DEFAULT_LEVEL;
  }

  bool notif::new_variable::apply(SentinelState* state)
  {
    assert(state);
    if (var.value >= state->_variables.size()) {
      state->_variables.resize(var.value + 1);
    }
    SOFT_ASSERT(!state->_variables[var.value].active);
    state->_variables[var.value].var = var;
    state->_variables[var.value].active = true;
    return true;
  }

  bool notif::new_variable::rollback(SentinelState* state)
  {
    assert(state);
    assert (var.value < state->_variables.size());
    state->_variables[var.value].active = false;
    return true;
  }

  /** ASSIGNMENT **/
  unsigned notif::assignment::get_event_level(SentinelMarker* marker) const noexcept
  {
    assert(marker);
    if (marker->is_marked(lit.var()))
      return 0;
    return DEFAULT_LEVEL;
  }

  bool notif::assignment::apply(SentinelState* state)
  {
    assert(state);
    SOFT_ASSERT(lit.var().value < state->_variables.size());
    SOFT_ASSERT(state->_variables[lit.var().value].active);
    SOFT_ASSERT(state->lit_undef(lit));

    Tvar var = lit.var();
    state->value(var) = lit.pol() == 1 ? VAR_TRUE : VAR_FALSE;
    state->reason(var) = reason;
    if (reason == CLAUSE_UNDEF) {
      state->level(var) = state->_level_counters.size();
      state->_level_counters.push_back(1);
    } else {
      Tlevel reason_level = 0;
      for (const auto& lit : state->literals(reason)) {
        if (lit.var() == var) {
          continue;
        }
        SOFT_ASSERT(state->lit_false(lit));
        reason_level = std::max(reason_level, state->level(lit.var()));
      }
      state->level(var) = reason_level;
      state->_level_counters[reason_level.value]++;
    }

    state->_trail.push_back(lit);
    return true;
  }

  bool notif::assignment::rollback(SentinelState* state)
  {
    assert(state);
    assert(!state->_trail.empty());
    assert(state->_trail.back() == lit);
    state->_trail.pop_back();
    Tvar var = lit.var();
    state->value(var) = VAR_UNDEF;
    state->reason(var) = CLAUSE_UNDEF;
    return true;
  }

  /** UNASSIGNMENT **/
  unsigned notif::unassignment::get_event_level(SentinelMarker* marker) const noexcept
  {
    assert(marker);
    if (marker->is_marked(lit.var()))
      return 0;
    return DEFAULT_LEVEL;
  }

  bool notif::unassignment::apply(SentinelState* state)
  {
    assert(state);
    SOFT_ASSERT(lit.var().value < state->_variables.size());
    SOFT_ASSERT(state->active(lit.var()));
    SOFT_ASSERT(!state->lit_undef(lit));

    var = state->_variables[lit.var().value];

    state->value(lit.var()) = VAR_UNDEF;
    state->reason(lit.var()) = CLAUSE_UNDEF;
    state->propagated(lit.var()) = false;
    deleted_level = state->decrement_level_counter(var.level);

    // search it on the trail and remove it
    // we know it's location
    for (unsigned i = var.position; i < state->_trail.size()-1; i++) {
      state->_trail[i] = state->_trail[i+1];
      state->position(state->_trail[i].var()) = i;
    }
    state->_trail.pop_back();
    return true;
  }

  bool notif::unassignment::rollback(SentinelState* state)
  {
    assert(state);
    SOFT_ASSERT(lit.var().value < state->_variables.size());
    SOFT_ASSERT(state->active(lit.var()));
    SOFT_ASSERT(state->lit_undef(lit));

    state->_variables[lit.var().value] = var;

    state->increment_level_counter(var.level, deleted_level);

    // add it back to the trail
    state->_trail.resize(var.position + 1);
    for (unsigned i = state->_trail.size(); i > var.position; i--) {
      state->_trail[i] = state->_trail[i-1];
      state->position(state->_trail[i].var()) = i;
    }
    state->_trail[var.position] = lit;
    return true;
  }

  /** PROPAGATION **/
  unsigned notif::propagation::get_event_level(SentinelMarker* observer) const noexcept
  {
    assert(observer);
    if (observer->is_marked(lit.var()))
      return 0;
    return DEFAULT_LEVEL;
  }

  bool notif::propagation::apply(SentinelState* state)
  {
    assert(state);
    SOFT_ASSERT(lit.var().value < state->_variables.size());
    SOFT_ASSERT(state->active(lit.var()));
    SOFT_ASSERT(!state->lit_undef(lit));
    SOFT_ASSERT(!state->propagated(lit));

    state->propagated(lit.var()) = true;
    return true;
  }

  bool notif::propagation::rollback(SentinelState* state)
  {
    assert(state);
    assert(lit.var().value < state->_variables.size());
    assert(state->active(lit.var()));
    assert(!state->lit_undef(lit));
    assert(state->propagated(lit.var()) == true);

    state->propagated(lit.var()) = false;
    return true;
  }

  /** PROPAGATION REMOVED **/
  unsigned notif::propagation_removed::get_event_level(SentinelMarker* marker) const noexcept
  {
    assert(marker);
    if (marker->is_marked(lit.var()))
      return 0;
    return DEFAULT_LEVEL;
  }

  bool notif::propagation_removed::apply(SentinelState* state)
  {
    assert(state);
    SOFT_ASSERT(lit.var().value < state->_variables.size());
    SOFT_ASSERT(state->active(lit.var()));
    SOFT_ASSERT(!state->lit_undef(lit));
    SOFT_ASSERT(state->propagated(lit.var()) == true);

    state->propagated(lit.var()) = false;
    return true;
  }

  bool notif::propagation_removed::rollback(SentinelState* state)
  {
    assert(state);
    assert(lit.var().value < state->_variables.size());
    assert(state->active(lit.var()));
    assert(!state->lit_undef(lit));
    SOFT_ASSERT(state->propagated(lit.var()) == false);

    state->propagated(lit.var()) = true;
    return true;
  }

  /** UPDATE LEVEL **/
  unsigned notif::update_level::get_event_level(SentinelMarker* marker) const noexcept
  {
    assert(marker);
    if (marker->is_marked(lit.var()))
      return 0;
    return DEFAULT_LEVEL;
  }
  bool notif::update_level::apply(SentinelState* state)
  {
    assert(state);
    SOFT_ASSERT(lit.var().value < state->_variables.size());
    SOFT_ASSERT(state->_variables[lit.var().value].active);
    SOFT_ASSERT(!state->lit_undef(lit));
    Tvar var = lit.var();
    old_level = state->level(var);
    state->level(var) = level;

    assert(old_level.value < state->_level_counters.size());
    deleted_level = state->decrement_level_counter(old_level);
    return true;
  }
  bool notif::update_level::rollback(SentinelState* state)
  {
    assert(state);
    assert(lit.var().value < state->_variables.size());
    assert(state->_variables[lit.var().value].active);
    assert(!state->lit_undef(lit));
    Tvar var = lit.var();
    state->level(var) = old_level;

    state->increment_level_counter(old_level, deleted_level);
    return true;
  }

  /** UPDATE REASON **/
  unsigned notif::update_reason::get_event_level(SentinelMarker* marker) const noexcept
  {
    assert(marker);
    if (marker->is_marked(lit.var()))
      return 0;
    return DEFAULT_LEVEL;
  }
  bool notif::update_reason::apply(SentinelState* state)
  {
    assert(state);
    SOFT_ASSERT(lit.var().value < state->_variables.size());
    SOFT_ASSERT(state->_variables[lit.var().value].active);
    SOFT_ASSERT(!state->lit_undef(lit));
    Tvar var = lit.var();
    old_reason = state->reason(var);
    state->reason(var) = reason;
    return true;
  }
  bool notif::update_reason::rollback(SentinelState* state)
  {
    assert(state);
    assert(lit.var().value < state->_variables.size());
    assert(state->_variables[lit.var().value].active);
    assert(!state->lit_undef(lit));
    Tvar var = lit.var();
    state->reason(var) = old_reason;
    return true;
  }



  /** NEW CLAUSE **/
  notif::new_clause::new_clause(Tclause cl, std::vector<Tlit> lits, bool external) :
    cl(cl), lits(std::move(lits)), external(external)
  {
    std::sort(this->lits.begin(), this->lits.end(), [](const Tlit& a, const Tlit& b) {
      return a.value < b.value;
    });
  }
  unsigned notif::new_clause::get_event_level(SentinelMarker* marker) const noexcept
  {
    assert(marker);
    if (marker->is_marked(cl))
      return 0;
    return DEFAULT_LEVEL;
  }
  const std::string notif::new_clause::get_message() const noexcept
  {
    std::string str = "New clause : " + cl.to_string() + " with literals ";
    for (const auto& lit : lits) {
      str += lit.to_string() + " ";
    }
    str += external ? "(external) " : "";
    return str;
  }
  bool notif::new_clause::apply(SentinelState* state)
  {
    assert(state);
    if (cl.value >= state->_clauses.size()) {
      state->_clauses.resize(cl.value + 1);
    }
    SOFT_ASSERT(!state->_clauses[cl.value].active);
    state->_clauses[cl.value].literals = lits;
    state->_clauses[cl.value].active = true;
    state->_clauses[cl.value].external = external;
    return true;

  }
  bool notif::new_clause::rollback(SentinelState* state)
  {
    assert(state);
    assert(cl.value < state->_clauses.size());
    state->_clauses[cl.value].active = false;
    return true;
  }

  /** DELETE CLAUSE **/
  unsigned notif::delete_clause::get_event_level(SentinelMarker* marker) const noexcept
  {
    assert(marker);
    if (marker->is_marked(cl))
      return 0;
    return DEFAULT_LEVEL;
  }

  bool notif::delete_clause::apply(SentinelState* state)
  {
    assert(state);
    SOFT_ASSERT(cl.value < state->_clauses.size());
    SOFT_ASSERT(state->_clauses[cl.value].active);
    deleted_clause = state->_clauses[cl.value];
    state->_clauses[cl.value].active = false;
    return true;
  }

  bool notif::delete_clause::rollback(SentinelState* state)
  {
    assert(state);
    assert(cl.value < state->_clauses.size());
    state->_clauses[cl.value] = deleted_clause;
    return true;
  }

  /** WATCH **/
  unsigned notif::watch::get_event_level(SentinelMarker* marker) const noexcept
  {
    assert(marker);
    if (marker->is_marked(cl) || marker->is_marked(lit.var()))
      return 0;
    return DEFAULT_LEVEL;
  }

  bool notif::watch::apply(SentinelState* state)
  {
    assert(state);
    SOFT_ASSERT(cl.value < state->_clauses.size());
    SOFT_ASSERT(state->_clauses[cl.value].active);
    SOFT_ASSERT(lit.var().value < state->_variables.size());
    SOFT_ASSERT(state->active(lit.var()));
    SOFT_ASSERT(state->_clauses[cl.value].watches.size() < 2);
    SOFT_ASSERT(state->_clauses[cl.value].watches.size() == 0 || state->_clauses[cl.value].watches[0].first != lit);
    SOFT_ASSERT(find(state->_clauses[cl.value].literals.begin(), state->_clauses[cl.value].literals.end(), lit) != state->_clauses[cl.value].literals.end());

    state->_clauses[cl.value].watches.push_back({lit, LIT_UNDEF});

    return true;
  }
  bool notif::watch::rollback(SentinelState* state)
  {
    assert(state);
    assert(cl.value < state->_clauses.size());
    assert(state->_clauses[cl.value].active);
    assert(state->_clauses[cl.value].watches.size() > 0);
    assert(state->_clauses[cl.value].watches.size() <= 2);
    assert(state->_clauses[cl.value].watches[0].first == lit
      || (state->_clauses[cl.value].watches.size() > 1 && state->_clauses[cl.value].watches[1].first == lit));

    if (state->_clauses[cl.value].watches.size() == 1 || state->_clauses[cl.value].watches[0].first == lit) {
      state->_clauses[cl.value].watches.erase(state->_clauses[cl.value].watches.begin());
    } else {
      state->_clauses[cl.value].watches.pop_back();
    }

    return true;
  }

  /** UNWATCH **/
  unsigned notif::unwatch::get_event_level(SentinelMarker* marker) const noexcept
  {
    assert(marker);
    if (marker->is_marked(cl) || marker->is_marked(lit.var()))
      return 0;
    return DEFAULT_LEVEL;
  }
  bool notif::unwatch::apply(SentinelState* state)
  {
    assert(state);
    SOFT_ASSERT(cl.value < state->_clauses.size());
    SOFT_ASSERT(state->_clauses[cl.value].active);
    SOFT_ASSERT(lit.var().value < state->_variables.size());
    SOFT_ASSERT(state->active(lit.var()));
    SOFT_ASSERT(state->_clauses[cl.value].watches.size() > 0);
    SOFT_ASSERT(state->_clauses[cl.value].watches.size() <= 2);
    auto it = find_if(state->_clauses[cl.value].watches.begin(), state->_clauses[cl.value].watches.end(), [this](const std::pair<Tlit, Tlit>& p) {
      return p.first == lit;
    });
    SOFT_ASSERT(it != state->_clauses[cl.value].watches.end());

    previous_blocker = it->second;
    state->_clauses[cl.value].watches.erase(it);
      SOFT_ASSERT(state->propagated(lit.var()) == false);
    return true;
  }
  bool notif::unwatch::rollback(SentinelState* state)
  {
    assert(state);
    assert(cl.value < state->_clauses.size());
    assert(state->_clauses[cl.value].active);
    assert(lit.var().value < state->_variables.size());
    assert(state->active(lit.var()));
    assert(state->_clauses[cl.value].watches.size() < 2);
    assert(find(state->_clauses[cl.value].literals.begin(), state->_clauses[cl.value].literals.end(), lit) != state->_clauses[cl.value].literals.end());

    state->_clauses[cl.value].watches.push_back({lit, previous_blocker});

    return true;
  }

  /** BLOCK **/
  unsigned notif::block::get_event_level(SentinelMarker* marker) const noexcept
  {
    assert(marker);
    if (marker->is_marked(cl) || marker->is_marked(blocker.var()) || marker->is_marked(blocked_lit.var()))
      return 0;
    return DEFAULT_LEVEL;
  }
  bool notif::block::apply(SentinelState* state)
  {
    assert(state);
    SOFT_ASSERT(cl.value < state->_clauses.size());
    SOFT_ASSERT(state->_clauses[cl.value].active);
    SOFT_ASSERT(blocker.var().value < state->_variables.size());
    SOFT_ASSERT(state->active(blocker.var()));
    SOFT_ASSERT(blocked_lit.var().value < state->_variables.size());
    SOFT_ASSERT(state->active(blocked_lit.var()));
    SOFT_ASSERT(state->_clauses[cl.value].watches.size() > 0);
    SOFT_ASSERT(state->_clauses[cl.value].watches.size() <= 2);
    auto it = find_if(state->_clauses[cl.value].watches.begin(), state->_clauses[cl.value].watches.end(), [this](const std::pair<Tlit, Tlit>& p) {
      return p.first == blocked_lit;
    });
    SOFT_ASSERT(it != state->_clauses[cl.value].watches.end());

    previous_blocker = it->second;
    it->second = blocker;

    return true;
  }
  bool notif::block::rollback(SentinelState* state)
  {
    assert(state);
    assert(cl.value < state->_clauses.size());
    assert(state->_clauses[cl.value].active);
    assert(blocker.var().value < state->_variables.size());
    assert(state->active(blocker.var()));
    assert(blocked_lit.var().value < state->_variables.size());
    assert(state->active(blocked_lit.var()));
    assert(state->_clauses[cl.value].watches.size() > 0);
    assert(state->_clauses[cl.value].watches.size() <= 2);
    auto it = find_if(state->_clauses[cl.value].watches.begin(), state->_clauses[cl.value].watches.end(), [this](const std::pair<Tlit, Tlit>& p) {
      return p.first == blocked_lit;
    });
    SOFT_ASSERT(it != state->_clauses[cl.value].watches.end());

    it->second = previous_blocker;

    return true;
  }

  /** REMOVE LITERAL **/
  unsigned notif::remove_literal::get_event_level(SentinelMarker* marker) const noexcept
  {
    assert(marker);
    if (marker->is_marked(cl) || marker->is_marked(lit.var()))
      return 0;
    return DEFAULT_LEVEL;
  }
  bool notif::remove_literal::apply(SentinelState* state)
  {
    assert(state);
    SOFT_ASSERT(cl.value < state->_clauses.size());
    SOFT_ASSERT(state->_clauses[cl.value].active);
    SOFT_ASSERT(lit.var().value < state->_variables.size());
    SOFT_ASSERT(state->active(lit.var()));
    SOFT_ASSERT(find(state->_clauses[cl.value].literals.begin(), state->_clauses[cl.value].literals.end(), lit) != state->_clauses[cl.value].literals.end());

    // move the literal to the end of the literals vector
    // the literals vector is stored as l1 l2 l3 | l4 l5 where l4 and l5 are the removed literals. l1 - l3 are sorted and l4 - l5 are sorted.
    auto it = find(state->_clauses[cl.value].literals.begin(), state->_clauses[cl.value].literals.end(), lit);
    SOFT_ASSERT(it != state->_clauses[cl.value].literals.end());

    std::iter_swap(it, state->_clauses[cl.value].literals.end() - state->_clauses[cl.value].n_deleted_literals - 1);

    state->_clauses[cl.value].n_deleted_literals++;

    std::sort(state->_clauses[cl.value].literals.end() - state->_clauses[cl.value].n_deleted_literals, state->_clauses[cl.value].literals.end(), [](const Tlit& a, const Tlit& b) {
      return a.value < b.value;
    });
    std::sort(state->_clauses[cl.value].literals.begin(), state->_clauses[cl.value].literals.end() - state->_clauses[cl.value].n_deleted_literals, [](const Tlit& a, const Tlit& b) {
      return a.value < b.value;
    });

    return true;
  }
  bool notif::remove_literal::rollback(SentinelState* state)
  {
    assert(state);
    assert(cl.value < state->_clauses.size());
    assert(state->_clauses[cl.value].active);
    assert(lit.var().value < state->_variables.size());
    assert(state->active(lit.var()));
    assert(find(state->_clauses[cl.value].literals.end() - state->_clauses[cl.value].n_deleted_literals, state->_clauses[cl.value].literals.end(), lit) != state->_clauses[cl.value].literals.end());

    std::iter_swap(std::find(state->_clauses[cl.value].literals.end() - state->_clauses[cl.value].n_deleted_literals, state->_clauses[cl.value].literals.end(), lit), state->_clauses[cl.value].literals.end() - state->_clauses[cl.value].n_deleted_literals);

    state->_clauses[cl.value].n_deleted_literals--;

     std::sort(state->_clauses[cl.value].literals.end() - state->_clauses[cl.value].n_deleted_literals, state->_clauses[cl.value].literals.end(), [](const Tlit& a, const Tlit& b) {
      return a.value < b.value;
    });
    std::sort(state->_clauses[cl.value].literals.begin(), state->_clauses[cl.value].literals.end() - state->_clauses[cl.value].n_deleted_literals, [](const Tlit& a, const Tlit& b) {
      return a.value < b.value;
    });

    return true;
  }

  /** LOCK ASSUMPTION **/
  unsigned notif::lock_assumption::get_event_level(SentinelMarker* marker) const noexcept
  {
    assert(marker);
    if (marker->is_marked(lit.var()))
      return 0;
    return DEFAULT_LEVEL;
  }
  bool notif::lock_assumption::apply(SentinelState* state)
  {
    assert(state);
    SOFT_ASSERT(lit.var().value < state->_variables.size());
    SOFT_ASSERT(state->active(lit.var()));
    SOFT_ASSERT(state->lit_undef(lit));
    SOFT_ASSERT(!state->locked(lit.var()));

    state->locked(lit.var()) = true;
    return true;
  }
  bool notif::lock_assumption::rollback(SentinelState* state)
  {
    assert(state);
    assert(lit.var().value < state->_variables.size());
    assert(state->active(lit.var()));
    assert(state->lit_undef(lit));
    assert(state->locked(lit.var()));

    state->locked(lit.var()) = false;
    return true;
  }
  unsigned notif::unlock_assumption::get_event_level(SentinelMarker* marker) const noexcept
  {
    assert(marker);
    if (marker->is_marked(lit.var()))
      return 0;
    return DEFAULT_LEVEL;
  }
  bool notif::unlock_assumption::apply(SentinelState* state)
  {
    assert(state);
    SOFT_ASSERT(lit.var().value < state->_variables.size());
    SOFT_ASSERT(state->active(lit.var()));
    SOFT_ASSERT(state->lit_undef(lit));
    SOFT_ASSERT(state->locked(lit.var()));

    state->locked(lit.var()) = false;
    return true;
  }
  bool notif::unlock_assumption::rollback(SentinelState* state)
  {
    assert(state);
    assert(lit.var().value < state->_variables.size());
    assert(state->active(lit.var()));
    assert(state->lit_undef(lit));
    assert(!state->locked(lit.var()));

    state->locked(lit.var()) = true;
    return true;
  }
}
