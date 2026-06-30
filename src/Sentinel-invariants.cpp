/*
 * This file is part of the source code of the software program
 * SATSentinel. It is protected by applicable copyright laws.
 *
 * This source code is protected by the terms of the MIT License.
 */
/**
 * @file src/Sentinel-invariants.cpp
 * @author Robin Coutelier
 *
 * @brief Implementation of built-in invariant checks (trail sanity, level ordering, watched
 * literals, topological order, assignment coherence) and the SATSentinel registration wrappers.
 */
#include "Sentinel-state.hpp"

#include "utils/printer.hpp"
#include "Sentinel-invariants.hpp"
#include "SATSentinel.hpp"

#include <iostream>
#include <fstream>
#include <algorithm>

using namespace std;

namespace sentinel {

bool SATSentinel::check_invariants() const
{
  std::string err_msg;
  bool success = state->check_invariants(err_msg);
  if (!success) {
    LOG_ERROR("Invariant check failed:\n" << err_msg);
  }
  return success;
}

void SATSentinel::add_invariant(Invariant* invariant)
{
  state->add_invariant(invariant);
}

void SATSentinel::add_watch_invariant(WatchInvariant* invariant)
{
  state->add_watch_invariant(invariant);
}

bool SentinelState::check_invariants(string &err_msg, bool check_watch) const
{
  bool success = true;
  for (const Invariant* invariant : _invariants) {
    if (!invariant->check()) {
      success = false;
      err_msg += "Invariant violation (" + invariant->name + "):\n" + invariant->error_message + "\n";
    }
  }
  if (check_watch) {
    success &= check_watched_literals(err_msg);
  }
  return success;
}

bool SentinelState::check_no_conflicts(string &err_msg) const
{
  const string error_header = ERROR_HEAD + "Invariant violation (trail sanity): ";
  bool success = true;
  for (Tclause cl = 0; cl.value < _clauses.size(); cl++) {
    clause c = _clauses[cl];
    if (!c.active)
      continue;
    unsigned i;
    for (i = 0; i < c.literals.size(); i++)
      if (!lit_false(c.literals[i]) || !propagated(c.literals[i]))
        break;
    if (i == c.literals.size()) {
      success = false;
      err_msg += error_header + "clause " + to_string(cl) + " is falsified by the trail.\n";
    }
  }
  return success;
}

bool SentinelState::check_implied_levels(string &err_msg) const
{
  const string error_header = ERROR_HEAD + "Invariant violation (level ordering): ";
  bool success = true;
  for (Tlit lit : _trail) {
    if (decision(lit) || lazy(lit))
      continue;
    clause c = _clauses[reason(lit)];
    if (!c.active) {
      success = false;
      err_msg += error_header + "clause " + to_string(reason(lit)) + " is not active.\n";
      continue;
    }
    for (Tlit lit2 : c.literals) {
      if (level(lit2) > level(lit)) {
        success = false;
        err_msg += error_header + "clause " + to_string(reason(lit)) + " has a literal " + to_string(lit2) + " with a higher level than " + to_string(lit) + ".\n";
      }
    }
  }
  return success;
}

bool SentinelState::check_trail_monotonicity(string &err_msg) const
{
  const string error_header = ERROR_HEAD + "Invariant violation (trail monotonicity): ";
  bool success = true;
  for (size_t i = 1; i < _trail.size(); i++) {
    Tlit lit = _trail[i];
    Tlit prev = _trail[i - 1];
    if (level(lit) < level(prev)) {
      success = false;
      err_msg += error_header + "literal " + to_string(lit) + " has a lower level than the previous literal " + to_string(prev) + ".\n";
    }
  }
  return success;
}

bool SentinelState::check_no_missed_implications(string &err_msg) const
{
  const string error_header = ERROR_HEAD + "Invariant violation (no missed implications): ";
  bool success = true;
  for (Tclause cl = 0; cl.value < _clauses.size(); cl++) {
    clause c = _clauses[cl.value];
    if (!c.active)
      continue;
    unsigned n_undef = 0;
    Tlit last_undef = LIT_UNDEF;
    for (Tlit lit : c.literals) {
      if (lit_true(lit) || (lit_false(lit) && !propagated(lit)))
        goto next_clause;
      if (lit_undef(lit)) {
        n_undef++;
        last_undef = lit;
      }
    }
    if (n_undef == 1) {
      success = false;
      err_msg += error_header + "clause " + to_string(cl) + " has only one undefined literal " + to_string(last_undef) + ".\n";
    }
  next_clause:;
  }
  return success;
}

bool SentinelState::check_topological_order(string &err_msg) const
{
  const string error_header = ERROR_HEAD + "Invariant violation (topological order): ";
  bool success = true;
  vector<bool> visited(_variables.size(), false);
  for (Tlit lit : _trail) {
    assert(lit.var().value < visited.size());
    visited[lit.var().value] = true;
    if (reason(lit) == CLAUSE_UNDEF || reason(lit) == CLAUSE_LAZY)
      continue;
    clause c = _clauses[reason(lit)];
    if (!c.active) {
      success = false;
      err_msg += error_header + "clause " + to_string(reason(lit)) + " is not active.\n";
      continue;
    }
    for (Tlit lit2 : c.literals) {
      if (!visited[lit2.var().value]) {
        success = false;
        err_msg += error_header + "the reason clause " + to_string(reason(lit)) + " for the implication of literal " + to_string(lit) + " has a literal " + to_string(lit2) + " that is not visited yet.\n";
      }
    }
  }
  return success;
}

bool SentinelState::check_watched_literals(string &err_msg) const
{
  const string error_header = ERROR_HEAD + "Invariant violation (watch literals): ";
  bool success = true;
  for (Tclause cl = 0; cl.value < _clauses.size(); cl++) {
    const clause c = _clauses[cl];
    if (!c.active || c.literals.size() - c.n_deleted_literals < 2)
      continue;
    if (c.watches.size() != 2) {
      err_msg += error_header + "clause " + to_string(cl) + " has " + std::to_string(c.watches.size()) + " watches literals.\n";
      err_msg += error_header + "watches literals: ";
      for (pair<Tlit, Tlit> p : c.watches)
        err_msg += to_string(p.first) + " ";
      err_msg += "\n";
      success = false;
      continue;
    }

    if (c.watches.size() != 2) {
      err_msg += error_header + "clause " + to_string(cl) + " has " + std::to_string(c.watches.size()) + " watches literals.\n";
      err_msg += error_header + "watches literals: ";
      for (pair<Tlit, Tlit> p : c.watches)
        err_msg += to_string(p.first) + " ";
      err_msg += "\n";
      success = false;
      continue;
    }

    // check that both watches are indeed in the clause
    for (pair<Tlit, Tlit> p : c.watches) {
      Tlit lit = p.first;
      if (std::find(c.literals.begin(), c.literals.end(), lit) == c.literals.end()) {
        err_msg += error_header + "clause " + to_string(cl) + " has a watch literal " + to_string(lit) + " that is not in the clause.\n";
        success = false;
      }
    }
    if (!success)
      continue;

    for (pair<Tlit, Tlit> p : c.watches) {
      Tlit lit = p.first;
      Tlit blocker = p.second;
      Tlit other = LIT_UNDEF;
      for (pair<Tlit, Tlit> p2 : c.watches) {
        if (p2.first != lit) {
          other = p2.first;
          break;
        }
      }
      assert(other != LIT_UNDEF);
      bool last_failed = false;

      for (const WatchInvariant* invariant : _watch_invariants) {
        if (!invariant->check(lit, other, blocker)) {
          success = false;
          last_failed = true;
          err_msg += ERROR_HEAD + "Invariant violation (" + invariant->name + "): " + invariant->geterr_msg() + "\n";
        }
      }


      if (last_failed) {
        err_msg += ERROR_HEAD + "clause " + to_string(cl) + " does not satisfy the invariant.\n";
        err_msg += ERROR_HEAD + " c₁ = " + to_string(lit) + ", ";
        err_msg +=              " δ(c₁) = " + level(lit).to_string() + "\n";
        err_msg += ERROR_HEAD + " c₂ = " + to_string(other) + ", ";
        err_msg +=              " δ(c₂) = " + level(other).to_string() + "\n";
        err_msg += ERROR_HEAD + " b  = " + to_string(blocker) + ", ";
        err_msg +=              " δ(b)  = " + level(blocker).to_string() + "\n";
      }
    }
  }
  return success;
}

bool SentinelState::weak_watched_literals(Tlit c1, Tlit c2, Tlit blocker) const
{
  // ¬c₁ ∈ τ ⇒ [¬c₂ ∉ τ ∨ b ∈ π]
  bool success  = !(propagated(c1) && lit_false(c1));
       success |= !(propagated(c2) && lit_false(c2));
       success |= lit_true(blocker);
  return success;
}

bool SentinelState::strong_watched_literals(Tlit c1, Tlit c2, Tlit blocker) const
{
  // ¬c₁ ∈ τ ⇒ [c₂ ∈ π ∨ b ∈ π]
  bool success  = !propagated(c1) || !lit_false(c1);
       success |= lit_true(c2);
       success |= lit_true(blocker);
  return success;
}

bool SentinelState::check_assignment_coherence(std::string& err_msg) const
{
  const string error_header = ERROR_HEAD + "Invariant violation (assignment coherence): ";
  bool success = true;
  vector<bool> visited(_clauses.size(), false);
  for (Tlit lit : _trail) {
    if (visited[lit.var().value]) {
      success = false;
      err_msg += error_header + "variable " + lit.var().to_string() + " is present more than once in the trail.\n";
    }
    if (lit_undef(lit)) {
      success = false;
      err_msg += error_header + "variable " + lit.var().to_string() + " is undefined in the trail.\n";
    }
    if (lit_false(lit)) {
      success = false;
      err_msg += error_header + "variable " + lit.var().to_string() + " is false in the assignment.\n";
    }
    visited[lit.var().value] = true;

    if (!justified(lit))
      continue;
    Tclause r = reason(lit);
    const clause c = _clauses[r];
    if (!c.active) {
      success = false;
      err_msg += error_header + "clause " + to_string(r) + " is not active.\n";
      continue;
    }
    // check that the reason is only satisfied by one literal, that is "lit"
    bool reason_satisfied = false;
    for (Tlit l : c.literals) {
      if (l == lit) {
        reason_satisfied = true;
        continue;
      }
      if (!lit_false(l)) {
        success = false;
        err_msg += error_header + "clause " + to_string(r) + " does not imply " + to_string(lit) + " correctly.\n";
      }
    }
    if (!reason_satisfied) {
      success = false;
      err_msg += error_header + "clause " + to_string(r) + " does not contain " + to_string(lit) + ".\n";
    }
  }
  return success;
}
}
