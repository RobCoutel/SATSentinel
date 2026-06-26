#pragma once

#include "Sentinel-types.hpp"
#include "Sentinel-options.hpp"

namespace sentinel
{
  class SATSentinel;

  SATSentinel* create_sentinel(const SentinelOptions& options);

  void delete_sentinel(SATSentinel* sentinel);

  bool add_variable(SATSentinel* sentinel, Tvar var);
  bool set_variable_alias(SATSentinel* sentinel, Tvar var, std::string alias);

  bool add_clause(SATSentinel* sentinel, Tclause cl, const Tlit* lits, unsigned int size, bool external = false);
  bool delete_clause(SATSentinel* sentinel, Tclause clause);
  bool shrink_clause(SATSentinel* sentinel, Tclause clause, Tlit removed_lit);

  bool assign  (SATSentinel* sentinel, Tlit blocker, Tclause reason = CLAUSE_UNDEF);
  bool unassign(SATSentinel* sentinel, Tlit blocker);

  bool propagate  (SATSentinel* sentinel, Tlit blocker);
  bool unpropagate(SATSentinel* sentinel, Tlit blocker);

  bool update_level(SATSentinel* sentinel, Tlit blocker, Tlevel level);
  bool update_reason(SATSentinel* sentinel, Tlit blocker, Tclause reason);

  bool watch(SATSentinel* sentinel, Tclause clause, Tlit blocker);
  bool unwatch(SATSentinel* sentinel, Tclause clause, Tlit blocker);
  bool block(SATSentinel* sentinel, Tclause clause, Tlit blocker, Tlit watch = LIT_UNDEF);

  bool check_invariants(SATSentinel* sentinel);
  bool checkpoint(SATSentinel* sentinel);
  bool message(SATSentinel* sentinel, const std::string& message, unsigned level = 0);

  bool save_execution(SATSentinel* sentinel, const std::string& filename);
  bool load_execution(SATSentinel* sentinel, const std::string& filename);

  void set_command_parser(SATSentinel* sentinel, Tparser* parser);
}
