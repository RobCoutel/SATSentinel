/*
 * This file is part of the source code of the software program
 * SATSentinel. It is protected by applicable copyright laws.
 *
 * This source code is protected by the terms of the MIT License.
 */
/**
 * @file src/Sentinel-state.cpp
 * @author Robin Coutelier
 *
 * @brief Implementation of SentinelState: variable and clause tracking, trail management,
 * level counters, and the pretty-print routines for interactive inspection.
 */
#include "Sentinel-state.hpp"

#include "utils/printer.hpp"

#include <cassert>

namespace sentinel
{

SentinelState::SentinelState(Options* options)
{
  _level_counters.push_back(0);
  if (options) {
    _options = options;
  } else {
    _options = new Options();
  }

  register_invariants();
}

SentinelState::~SentinelState()
{
  if (_options) {
    delete _options;
  }
  for (Invariant* invariant : _invariants) {
    delete invariant;
  }
  for (WatchInvariant* watch_invariant : _watch_invariants) {
    delete watch_invariant;
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

bool SentinelState::implying(Tclause cl) const
{
  const clause& c = _clauses[cl];
  for (Tlit lit : c.literals) {
    if (reason(lit) == cl) {
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
    // if the level is top level, then we can remove all the empty levels from the top
    if (level.value == _level_counters.size() - 1) {
      // we do not remove level 0
      while (_level_counters.size() > 1 && _level_counters.back() == 0) {
        _level_counters.pop_back();
      }
    }
    return true;
  }
  return false;
}

void SentinelState::increment_level_counter(Tlevel level)
{
  if (level.value >= _level_counters.size()) {
    _level_counters.resize(level.value + 1, 0);
  }
  _level_counters[level.value]++;
}

void SentinelState::register_invariants()
{

  if (_options->check_no_conflicts) {
    _invariants.push_back(new Invariant("Trail Sanity", [this](std::string& err_msg) {
      return this->check_no_conflicts(err_msg);
    }));
  }
  if (_options->check_implied_levels) {
    _invariants.push_back(new Invariant("Implied Levels", [this](std::string& err_msg) {
      return this->check_implied_levels(err_msg);
    }));
  }
  if (_options->check_trail_monotonicity) {
    _invariants.push_back(new Invariant("Trail Monotonicity", [this](std::string& err_msg) {
      return this->check_trail_monotonicity(err_msg);
    }));
  }
  if (_options->check_no_missed_implications) {
    _invariants.push_back(new Invariant("No Missed Implications", [this](std::string& err_msg) {
      return this->check_no_missed_implications(err_msg);
    }));
  }
  if (_options->check_no_missed_lower_implications) {
    _invariants.push_back(new Invariant("No Missed Lower Implications", [this](std::string& err_msg) {
      return this->check_no_missed_lower_implications(err_msg);
    }));
  }
  if (_options->check_topological_order) {
    _invariants.push_back(new Invariant("Topological Order", [this](std::string& err_msg) {
      return this->check_topological_order(err_msg);
    }));
  }
  if (_options->check_correct_implications) {
    _invariants.push_back(new Invariant("Assignment Coherence", [this](std::string& err_msg) {
      return this->check_correct_implications(err_msg);
    }));
  }
  if (_options->check_repetition) {
    _invariants.push_back(new Invariant("Repetition", [this](std::string& err_msg) {
      return this->check_repetition(err_msg, this->_current_notification_index);
    }));
  }

  if (_options->check_weak_watched_literals) {
    _watch_invariants.push_back(new WatchInvariant("Weak Watched Literals", [this](Tlit c1, Tlit c2, Tlit blocker, std::string& err_msg) {
      return this->weak_watched_literals(c1, c2, blocker);
    }, "This invariant checks the conflict completeness of the watched literals",
       "¬c₁ ∈ τ ⇒ (¬c₂ ∉ τ ∨ b ∈ π)"));
  }
  if (_options->check_strong_watched_literals) {
    _watch_invariants.push_back(new WatchInvariant("Strong Watched Literals", [this](Tlit c1, Tlit c2, Tlit blocker, std::string& err_msg) {
      return this->strong_watched_literals(c1, c2, blocker);
    }, "This invariant checks the propagation completeness of the watched literals",
        "¬c₁ ∈ τ ⇒ (c₂ ∈ π ∨ b ∈ π)"));
  }
  if (_options->check_backtrack_compatible_watched_literals) {
    _watch_invariants.push_back(new WatchInvariant("Backtrack-Compatible Watched Literals", [this](Tlit c1, Tlit c2, Tlit blocker, std::string& err_msg) {
      return this->backtrack_compatible_watched_literals(c1, c2, blocker);
    }, "This invariant checks that backtracking will maintain the strong watched literal property",
     "¬c₁ ∈ τ ⇒ [(c₂ ∈ π ∧ δ(c₂) ≤ δ(c₁)) ∨ (b ∈ π ∧ δ(b) ≤ δ(c₁))]"));
  }
}
}
