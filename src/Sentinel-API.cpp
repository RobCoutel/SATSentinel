/*
 * This file is part of the source code of the software program
 * SATSentinel. It is protected by applicable copyright laws.
 *
 * This source code is protected by the terms of the MIT License.
 */
/**
 * @file src/Sentinel-API.cpp
 * @author Robin Coutelier
 *
 * @brief Implementation of the public C-style API functions, each wrapping a notification
 * object dispatched through SATSentinel::notify().
 */
#include "Sentinel-API.hpp"

#include "SATSentinel.hpp"
#include "Sentinel-notifications.hpp"

#include <cassert>

namespace sentinel
{
  SATSentinel* create_sentinel(const SentinelOptions& options)
  {
    return new SATSentinel(new SentinelOptions(options));
  }

  void delete_sentinel(SATSentinel* sentinel)
  {
    assert(sentinel);
    delete sentinel;
  }

  bool add_variable(SATSentinel* sentinel, Tvar var)
  {
    assert(sentinel);
    return sentinel->notify(new notif::new_variable(var));
  }

  bool set_variable_alias(SATSentinel* sentinel, Tvar var, std::string alias)
  {
    assert(sentinel);
    sentinel->set_alias(var, alias);
    return true;
  }

  bool add_clause(SATSentinel* sentinel, Tclause cl, const Tlit* lits, unsigned int size, bool external)
  {
    std::vector<Tlit> lits_vec(lits, lits + size);
    return sentinel->notify(new notif::new_clause(cl, lits_vec, external));
  }

  bool delete_clause(SATSentinel* sentinel, Tclause clause)
  {
    return sentinel->notify(new notif::delete_clause(clause));
  }

  bool shrink_clause(SATSentinel* sentinel, Tclause clause, Tlit removed_lit)
  {
    return sentinel->notify(new notif::remove_literal(clause, removed_lit));
  }

  bool assign(SATSentinel* sentinel, Tlit lit, Tclause reason)
  {
    return sentinel->notify(new notif::assignment(lit, reason));
  }

  bool unassign(SATSentinel* sentinel, Tlit lit)
  {
    return sentinel->notify(new notif::unassignment(lit));
  }

  bool propagate(SATSentinel* sentinel, Tlit lit)
  {
    return sentinel->notify(new notif::propagation(lit));
  }

  bool unpropagate(SATSentinel* sentinel, Tlit lit)
  {
    return sentinel->notify(new notif::propagation_removed(lit));
  }

  bool update_level(SATSentinel* sentinel, Tlit lit, Tlevel level)
  {
    return sentinel->notify(new notif::update_level(lit, level));
  }

  bool update_reason(SATSentinel* sentinel, Tlit lit, Tclause reason)
  {
    return sentinel->notify(new notif::update_reason(lit, reason));
  }

  bool watch(SATSentinel* sentinel, Tclause clause, Tlit lit)
  {
    return sentinel->notify(new notif::watch(clause, lit));
  }

  bool unwatch(SATSentinel* sentinel, Tclause clause, Tlit lit)
  {
    return sentinel->notify(new notif::unwatch(clause, lit));
  }

  bool block(SATSentinel* sentinel, Tclause clause, Tlit lit, Tlit watch)
  {
    return sentinel->notify(new notif::block(clause, lit, watch));
  }

  bool lock_assumption(SATSentinel* sentinel, Tlit lit)
  {
    return sentinel->notify(new notif::lock_assumption(lit));
  }

  bool unlock_assumption(SATSentinel* sentinel, Tlit lit)
  {
    return sentinel->notify(new notif::unlock_assumption(lit));
  }

  bool check_invariants(SATSentinel* sentinel)
  {
    assert(sentinel);
    return sentinel->check_invariants();
  }

  bool checkpoint(SATSentinel* sentinel)
  {
    sentinel->get_navigation_commands();
    return sentinel->get_external_commands();
  }

  bool message(SATSentinel* sentinel, const std::string& message, unsigned level)
  {
    return sentinel->notify(new notif::message(message, level));
  }

  bool save_execution(SATSentinel* sentinel, const std::string& filename)
  {
    assert(sentinel);
    // TODO: Implement serialization logic
    return true;
  }

  bool load_execution(SATSentinel* sentinel, const std::string& filename)
  {
    assert(sentinel);
    // TODO: Implement deserialization logic
    return true;
  }

  void set_command_parser(SATSentinel* sentinel, Tparser* parser)
  {
    sentinel->set_command_parser(parser);
  }

  void add_invariant(SATSentinel* sentinel, std::function<bool(std::string&)> custom_checker, const std::string& name)
  {
    sentinel->add_invariant(new Invariant(name, custom_checker));
  }

  void add_invariant(SATSentinel* sentinel, Invariant* invariant)
  {
    sentinel->add_invariant(invariant);
  }

  void add_watch_invariant(SATSentinel* sentinel, std::function<bool(Tlit, Tlit, Tlit, std::string&)> custom_checker, const std::string& name)
  {
    sentinel->add_watch_invariant(new WatchInvariant(name, custom_checker));
  }

  void add_watch_invariant(SATSentinel* sentinel, WatchInvariant* invariant)
  {
    sentinel->add_watch_invariant(invariant);
  }

}
