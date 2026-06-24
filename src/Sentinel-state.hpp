#pragma once

#include "Sentinel-types.hpp"
#include "Sentinel-options.hpp"

#include "Sentinel-invariants.hpp"

#include <vector>
#include <string>
#include <set>
#include <functional>

namespace sentinel
{
class SentinelState
{
public:
  SentinelState(SentinelOptions* options = nullptr);
  ~SentinelState();

  /** STATE ACCESSORS **/

  inline Tval& value(Tvar var)      { return _variables[var.value].value; }
  inline Tval value(Tvar var) const { return _variables[var.value].value; }
  inline Tval& value(Tlit lit)      { return value(lit.var()); }
  inline Tval value(Tlit lit) const { return value(lit.var()); }

  inline bool lit_true(Tlit lit) const  { return lit.satisfied(value(lit)); }
  inline bool lit_false(Tlit lit) const { return lit.falsified(value(lit)); }
  inline bool lit_undef(Tlit lit) const { return lit.undefined(value(lit)); }


  inline Tlevel& level(Tvar var)       { return _variables[var.value].level; }
  inline Tlevel  level(Tvar var) const { return _variables[var.value].level; }
  inline Tlevel& level(Tlit lit)       { return level(lit.var()); }
  inline Tlevel  level(Tlit lit) const { return level(lit.var()); }

  inline Tclause& reason(Tvar var)       { return _variables[var.value].reason; }
  inline Tclause  reason(Tvar var) const { return _variables[var.value].reason; }
  inline Tclause& reason(Tlit lit)       { return reason(lit.var()); }
  inline Tclause  reason(Tlit lit) const { return reason(lit.var()); }

  inline bool decision(Tvar var) const { return value(var) != VAR_UNDEF && reason(var) == CLAUSE_UNDEF; }
  inline bool decision(Tlit lit) const { return decision(lit.var()); }
  inline bool lazy(Tvar var) const     { return reason(var) == CLAUSE_LAZY; }
  inline bool lazy(Tlit lit) const     { return lazy(lit.var()); }
  inline bool justified(Tvar var) const { return !decision(var) && !lazy(var); }
  inline bool justified(Tlit lit) const { return !decision(lit) && !lazy(lit); }

  inline bool& propagated(Tvar var)       { return _variables[var.value].propagated; }
  inline bool  propagated(Tvar var) const { return _variables[var.value].propagated; }
  inline bool& propagated(Tlit lit)       { return propagated(lit.var()); }
  inline bool  propagated(Tlit lit) const { return propagated(lit.var()); }

  inline bool& active(Tvar var)       { return _variables[var.value].active; }
  inline bool  active(Tvar var) const { return _variables[var.value].active; }
  inline bool& active(Tlit lit)       { return active(lit.var()); }
  inline bool  active(Tlit lit) const { return active(lit.var()); }

  inline bool& locked(Tvar var)       { return _variables[var.value].locked; }
  inline bool  locked(Tvar var) const { return _variables[var.value].locked; }
  inline bool& locked(Tlit lit)       { return locked(lit.var()); }
  inline bool  locked(Tlit lit) const { return locked(lit.var()); }

  inline std::string& alias(Tvar var)       { return _variables[var.value].alias; }
  inline std::string  alias(Tvar var) const { return _variables[var.value].alias; }
  inline std::string  alias(Tlit lit) const {
    const std::string& var_alias = alias(lit.var());
    return var_alias.empty() ? "" : (lit.pol() ? "" : "~") + var_alias;
  }

  inline unsigned& position(Tvar var)       { return _variables[var.value].position; }
  inline unsigned  position(Tvar var) const { return _variables[var.value].position; }
  inline unsigned& position(Tlit lit)       { return position(lit.var()); }
  inline unsigned  position(Tlit lit) const { return position(lit.var()); }

  inline size_t trail_size() const { return _trail.size(); }
  inline Tlit trail_literal(size_t index) const { return _trail[index]; }

  inline size_t clauses_size() const { return _clauses.size(); }
  inline size_t variables_size() const { return _variables.size(); }

  inline bool& active(Tclause cl)       { return _clauses[cl.value].active; }
  inline bool  active(Tclause cl) const { return _clauses[cl.value].active; }
  inline bool& clause_learnt(Tclause cl)       { return _clauses[cl.value].learnt; }
  inline bool  clause_learnt(Tclause cl) const { return _clauses[cl.value].learnt; }
  inline bool& clause_external(Tclause cl)       { return _clauses[cl.value].external; }
  inline bool  clause_external(Tclause cl) const { return _clauses[cl.value].external; }

  bool unit(Tclause cl) const;
  bool conflicting(Tclause cl) const;
  bool clause_satisfied(Tclause cl) const;

  inline       std::vector<Tlit>& literals(Tclause cl)       { return _clauses[cl.value].literals; }
  inline const std::vector<Tlit>& literals(Tclause cl) const { return _clauses[cl.value].literals; }
  inline       std::vector<std::pair<Tlit, Tlit>>& watches(Tclause cl)       { return _clauses[cl.value].watches; }
  inline const std::vector<std::pair<Tlit, Tlit>>& watches(Tclause cl) const { return _clauses[cl.value].watches; }

  inline Tlevel level() const { return _level_counters.size() - 1; }

  bool decrement_level_counter(Tlevel level);
  void increment_level_counter(Tlevel level, bool create = false);

  /** INVARIANTS **/
  bool check_invariants(std::string &err_msg, bool check_watch_literals = true) const;

  bool check_watched_literals(std::string &err_msg) const;

  bool check_trail_sanity(std::string &err_msg) const;
  bool check_implied_levels(std::string &err_msg) const;
  bool check_trail_monotonicity(std::string &err_msg) const;
  bool check_no_missed_implications(std::string &err_msg) const;
  bool check_topological_order(std::string &err_msg) const;
  bool check_assignment_coherence(std::string &err_msg) const;

  bool weak_watched_literals(Tlit c1, Tlit c2, Tlit blocker) const;
  bool strong_watched_literals(Tlit c1, Tlit c2, Tlit blocker) const;

  std::vector<Invariant> _invariants;
  void add_custom_invariant(std::function<bool(const SentinelState*, std::string&)> custom_checker, const std::string& name = "Custom Invariant");
  void add_invariant(const Invariant& invariant) { _invariants.push_back(invariant); }
  std::vector<WatchInvariant> _watch_invariants;
  void add_custom_watch_invariant(std::function<bool(Tlit, Tlit, Tlit, std::string& err_msg)> custom_checker);

  /** PRINTING **/
  std::string to_string(Tlit lit) const;
  std::string to_string(Tvar var) const;
  std::string to_string(Tclause cl) const;

  struct variable
  {
    variable() = default;
    variable(Tvar var) : var(var) {}
    Tvar var;
    Tval value = VAR_UNDEF;
    Tlevel level = LEVEL_UNDEF;
    Tclause reason = CLAUSE_UNDEF;
    bool active = false;
    bool propagated = false;
    unsigned position = 0xFFFFFFFF;
    bool locked = false;
    std::string alias = "";
  };

  struct clause
  {
    clause() = default;
    clause(const std::vector<Tlit>& literals, bool learnt, bool external) :
      literals(literals),
      learnt(learnt),
      external(external) {}

    std::vector<Tlit> literals;
    unsigned n_deleted_literals = 0;
    std::vector<std::pair<Tlit, Tlit>> watches;
    bool active = false;
    bool learnt = false;
    bool external = false;

    std::string to_string() const {
      std::string str = "{ ";
      for (unsigned i = 0; i < literals.size(); i++) {
        Tlit lit = literals[i];
        if (i == literals.size() - n_deleted_literals) {
          str += " | ";
        }
        str += lit.to_string() + " ";
      }
      str += "}";
      return str;
    }
  };

  struct clause_set : public std::vector<clause> {

    clause& operator[](Tclause cl) { return std::vector<clause>::operator[](cl.value); }
    const clause& operator[](Tclause cl) const { return std::vector<clause>::operator[](cl.value); }
  };

  std::vector<variable> _variables;
  clause_set _clauses;
  std::vector<Tlit> _trail;

  const SentinelOptions* _options;

  // for each level, keeps track of the number of literals assigned at this level.
  std::vector<unsigned> _level_counters;
};

const Invariant trail_sanity("Trail Sanity", [](const SentinelState* state, std::string& err_msg) {
  return state->check_trail_sanity(err_msg);
});
const Invariant implied_levels("Implied Levels", [](const SentinelState* state, std::string& err_msg) {
  return state->check_implied_levels(err_msg);
});
const Invariant trail_monotonicity("Trail Monotonicity", [](const SentinelState* state, std::string& err_msg) {
  return state->check_trail_monotonicity(err_msg);
});
const Invariant no_missed_implications("No Missed Implications", [](const SentinelState* state, std::string& err_msg) {
  return state->check_no_missed_implications(err_msg);
});
const Invariant topological_order("Topological Order", [](const SentinelState* state, std::string& err_msg) {
  return state->check_topological_order(err_msg);
});
const Invariant assignment_coherence("Assignment Coherence", [](const SentinelState* state, std::string& err_msg) {
  return state->check_assignment_coherence(err_msg);
});

}
