#include "Sentinel-state.hpp"

#include "utils/printer.hpp"

#include <cassert>

namespace sentinel
{

SentinelState::SentinelState(SentinelOptions* options)
{
  _level_counters.push_back(0);
  if (options) {
    _options = options;
  } else {
    _options = new SentinelOptions();
  }

  if (_options->check_trail_sanity) {
    _invariants.push_back(trail_sanity);
  }
  if (_options->check_implied_levels) {
    _invariants.push_back(implied_levels);
  }
  if (_options->check_trail_monotonicity) {
    _invariants.push_back(trail_monotonicity);
  }
  if (_options->check_no_missed_implications) {
    _invariants.push_back(no_missed_implications);
  }
  if (_options->check_topological_order) {
    _invariants.push_back(topological_order);
  }
  if (_options->check_assignment_coherence) {
    _invariants.push_back(assignment_coherence);
  }

  if (_options->check_weak_watched_literals) {
    _watch_invariants.push_back(WatchInvariant("Weak Watched Literals", [this](Tlit c1, Tlit c2, Tlit blocker, std::string& err_msg) {
      return this->weak_watched_literals(c1, c2, blocker);
    }));
  }
  if (_options->check_strong_watched_literals) {
    _watch_invariants.push_back(WatchInvariant("Strong Watched Literals", [this](Tlit c1, Tlit c2, Tlit blocker, std::string& err_msg) {
      return this->strong_watched_literals(c1, c2, blocker);
    }));
  }
}

SentinelState::~SentinelState()
{
  if (_options) {
    delete _options;
  }
}

bool SentinelState::unit(Tclause cl) const
{
  const clause& c = _clauses[cl];
  unsigned n_false = 0;
  for (Tlit lit : c.literals) {
    if (lit_false(lit)) {
      n_false++;
    }
  }
  return n_false + 1 == c.literals.size();
}

bool SentinelState::conflicting(Tclause cl) const
{
  const clause& c = _clauses[cl];
  for (Tlit lit : c.literals) {
    if (!lit_false(lit)) {
      return false;
    }
  }
  return true;
}

bool SentinelState::clause_satisfied(Tclause cl) const
{
  const clause& c = _clauses[cl];
  for (Tlit lit : c.literals) {
    if (lit_true(lit)) {
      return true;
    }
  }
  return false;
}

bool SentinelState::decrement_level_counter(Tlevel level)
{
  assert(level.value < _level_counters.size());
  _level_counters[level.value]--;
  if (_level_counters[level.value] == 0) {
    for (unsigned i = level.value + 1; i < _level_counters.size(); i++) {
      _level_counters[i - 1] = _level_counters[i];
    }
    _level_counters.pop_back();
    return true;
  }
  return false;
}

void SentinelState::increment_level_counter(Tlevel level, bool create)
{
  if (!create) {
    assert(level.value < _level_counters.size());
    _level_counters[level.value]++;
  } else {
    _level_counters.insert(_level_counters.begin() + level.value, 1);
  }
}


std::string SentinelState::to_string(Tlit lit) const
{
  std::string s = "";
  Tvar var = lit.var();

  // styling
  if (lit_undef(lit))
    s += ORANGE;
  else if (lit_true(lit))
    s += GREEN;
  else
    s += RED;

  if (propagated(lit))
    s += UNDERLINE;

  // the literal
  if (alias(lit).empty())
    s += lit.to_string();
  else
    s += alias(lit);
  if (locked(var))
    s += "🔒";

  // reset the style
  s += RESET;
  return s;
}

std::string SentinelState::to_string(Tvar var) const
{
  std::string s = "";
  s += var.to_string() + ": ";
  s += pad(var.value, _variables.size());
  if (propagated(var))
    s += " (p)";
  else
    s += " (u)";
  if (!alias(var).empty())
    s += alias(var) + ": ";
  else
    s += var.to_string() + " ";
  if (active(var)) {
    if (value(var) == VAR_UNDEF) {
      s += ORANGE;
      s += "undef";
      s += RESET;
    } else if (value(var) == VAR_TRUE) {
      s += GREEN;
      s += "true";
      s += RESET;
    } else if (value(var) == VAR_FALSE) {
      s += RED;
      s += "false";
      s += RESET;
    } else
      s += "error";
    s += " @ ";
    s += level(var).to_string();
    s += " by ";
    if (decision(var))
      s += "decision";
    else if (reason(var) == CLAUSE_UNDEF)
      s += "undef";
    else if (lazy(var))
      s += "lazy";
    else
      s += reason(var).to_string();
  }
  else
    s += "deleted";
  return s;
}

std::string SentinelState::to_string(Tclause cl) const
{
  if (cl.value >= _clauses.size()) {
    return "C" + std::to_string(cl.value) + " (undefined)";
  }
  const clause& c = _clauses[cl];

  if (!c.active) {
    return "C" + std::to_string(cl.value) + " (inactive)";
  }

  std::string s = cl.to_string()+ ": ";
  std::string satisfied_lits = "";
  std::string undefined_lits = "";
  std::string falsified_lits = "";

  Tlit c1 = c.watches.size() > 0 ? c.watches[0].first : Tlit(0);
  Tlit c2 = c.watches.size() > 1 ? c.watches[1].first : Tlit(0);
  Tlit b1 = c.watches.size() > 0 ? c.watches[0].second : Tlit(0);
  Tlit b2 = c.watches.size() > 1 ? c.watches[1].second : Tlit(0);

  for (unsigned i = 0; i < c.literals.size() - c.n_deleted_literals; i++) {
    Tlit lit = c.literals[i];
    if (lit_true(lit)) {
      if (lit == c1 || lit == c2)
        satisfied_lits += "w";
      satisfied_lits += to_string(lit);
      if (lit == c1 && b1.value != 0)
        satisfied_lits += "(" + to_string(b1) + ")";
      else if (lit == c2 && b2.value != 0)
        satisfied_lits += "(" + to_string(b2) + ")";
      satisfied_lits += " ";
    } else if (lit_undef(lit)) {
      if (lit == c1 || lit == c2)
        undefined_lits += "w";
      undefined_lits += to_string(lit);
      if (lit == c1 && b1.value != 0)
        undefined_lits += "(" + to_string(b1) + ")";
      else if (lit == c2 && b2.value != 0)
        undefined_lits += "(" + to_string(b2) + ")";
      undefined_lits += " ";
    } else {
      if (lit == c1 || lit == c2)
        falsified_lits += "w";
      falsified_lits += to_string(lit);
      if (lit == c1 && b1.value != 0)
        falsified_lits += "(" + to_string(b1) + ")";
      else if (lit == c2 && b2.value != 0)
        falsified_lits += "(" + to_string(b2) + ")";
      falsified_lits += " ";
    }
  }
  s += satisfied_lits + undefined_lits + falsified_lits;
  if (c.n_deleted_literals > 0) {
    s += "| ";
    for (unsigned i = c.literals.size() - c.n_deleted_literals; i < c.literals.size(); i++) {
      Tlit lit = c.literals[i];
      s += to_string(lit) + " ";
    }
  }
  return s;
}
}
