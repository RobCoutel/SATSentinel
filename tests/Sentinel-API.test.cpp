#include <catch2/catch_all.hpp>

#include <set>
#include <string>
#include <vector>

#include "Sentinel-API.hpp"

#define private public
#include "SATSentinel.hpp"
#undef private

using namespace sentinel;

namespace
{
  SATSentinel* make_sentinel(const SentinelOptions& options = SentinelOptions{})
  {
    return create_sentinel(options);
  }

  void add_variables(SATSentinel* sentinel, std::initializer_list<unsigned> vars)
  {
    for (unsigned var : vars) {
      REQUIRE(add_variable(sentinel, Tvar{var}));
    }
  }

  void destroy_sentinel(SATSentinel* sentinel)
  {
    delete_sentinel(sentinel);
  }
}

TEST_CASE("create_sentinel forwards options into the solver state", "[api]")
{
  SentinelOptions options;
  options.check_trail_sanity = false;
  options.check_implied_levels = false;
  options.check_trail_monotonicity = false;
  options.check_no_missed_implications = false;
  options.check_topological_order = false;
  options.check_assignment_coherence = false;
  options.check_weak_watched_literals = true;
  options.check_strong_watched_literals = true;

  SATSentinel* sentinel = make_sentinel(options);

  REQUIRE(sentinel != nullptr);
  REQUIRE(sentinel->state->_invariants.empty());
  REQUIRE(sentinel->state->_watch_invariants.size() == 2);

  destroy_sentinel(sentinel);
}

TEST_CASE("variables and aliases are stored in the solver state", "[api]")
{
  SATSentinel* sentinel = make_sentinel();

  REQUIRE(sentinel->state->variables_size() == 0);
  REQUIRE(add_variable(sentinel, Tvar{1}));
  REQUIRE(sentinel->state->variables_size() == 2);
  REQUIRE(sentinel->state->active(Tvar{1}));
  REQUIRE(sentinel->state->value(Tvar{1}) == VAR_UNDEF);
  REQUIRE(sentinel->state->alias(Tvar{1}).empty());

  REQUIRE(set_variable_alias(sentinel, Tvar{1}, "x1"));
  REQUIRE(sentinel->state->alias(Tvar{1}) == "x1");
  REQUIRE(sentinel->state->alias(Tlit{Tvar{1}, 0}) == "~x1");
  REQUIRE(check_invariants(sentinel));

  destroy_sentinel(sentinel);
}

TEST_CASE("clauses are added, watched, shrunk, and deleted through the API", "[api]")
{
  SATSentinel* sentinel = make_sentinel();
  add_variables(sentinel, {1, 2, 3});

  const Tclause clause{0};
  const std::vector<Tlit> unsorted_lits{
    Tlit{Tvar{3}, 1},
    Tlit{Tvar{1}, 0},
    Tlit{Tvar{2}, 1}
  };

  REQUIRE(add_clause(sentinel, clause, unsorted_lits.data(), static_cast<unsigned>(unsorted_lits.size()), true));
  REQUIRE(sentinel->state->active(clause));
  REQUIRE(sentinel->state->clause_external(clause));
  REQUIRE(sentinel->state->literals(clause) == std::vector<Tlit>{Tlit{Tvar{1}, 0}, Tlit{Tvar{2}, 1}, Tlit{Tvar{3}, 1}});

  const Tlit watched_lit{Tvar{1}, 0};
  const Tlit blocker_lit{Tvar{2}, 1};
  REQUIRE(watch(sentinel, clause, watched_lit));
  REQUIRE(sentinel->state->watches(clause).size() == 1);
  REQUIRE(sentinel->state->watches(clause)[0].first == watched_lit);

  REQUIRE(block(sentinel, clause, blocker_lit, watched_lit));
  REQUIRE(sentinel->state->watches(clause)[0].second == blocker_lit);

  REQUIRE(unwatch(sentinel, clause, watched_lit));
  REQUIRE(sentinel->state->watches(clause).empty());

  REQUIRE(shrink_clause(sentinel, clause, Tlit{Tvar{2}, 1}));
  REQUIRE(sentinel->state->_clauses[clause.value].n_deleted_literals == 1);
  REQUIRE(sentinel->state->literals(clause).back() == Tlit{Tvar{2}, 1});
  REQUIRE(check_invariants(sentinel));

  REQUIRE(delete_clause(sentinel, clause));
  REQUIRE_FALSE(sentinel->state->active(clause));
  REQUIRE(check_invariants(sentinel));

  destroy_sentinel(sentinel);
}

TEST_CASE("assignments, propagation flags, and unassignment follow the API contract", "[api]")
{
  SATSentinel* sentinel = make_sentinel();
  add_variable(sentinel, Tvar{1});

  const Tlit literal{Tvar{1}, 1};

  REQUIRE(assign(sentinel, literal));
  REQUIRE(sentinel->state->value(Tvar{1}) == VAR_TRUE);
  REQUIRE(sentinel->state->reason(Tvar{1}) == CLAUSE_UNDEF);
  REQUIRE(sentinel->state->level(Tvar{1}) == Tlevel{1});
  REQUIRE(sentinel->state->trail_size() == 1);
  REQUIRE(sentinel->state->trail_literal(0) == literal);

  REQUIRE(unassign(sentinel, literal));
  REQUIRE(sentinel->state->value(Tvar{1}) == VAR_UNDEF);
  REQUIRE(sentinel->state->reason(Tvar{1}) == CLAUSE_UNDEF);
  REQUIRE(sentinel->state->trail_size() == 0);
  REQUIRE(check_invariants(sentinel));

  destroy_sentinel(sentinel);
}

// TEST_CASE("message notifications are no-ops on the solver state", "[api]")
// {
//   SATSentinel* sentinel = make_sentinel();
//   add_variable(sentinel, Tvar{1});
//   assign(sentinel, Tlit{Tvar{1}, 1});

//   const auto trail_before = sentinel->state->trail_size();
//   const auto variables_before = sentinel->state->variables_size();

//   REQUIRE(message(sentinel, "hello from tests", 1));
//   REQUIRE(sentinel->state->trail_size() == trail_before);
//   REQUIRE(sentinel->state->variables_size() == variables_before);
//   REQUIRE(check_invariants(sentinel));

//   destroy_sentinel(sentinel);
// }

// TEST_CASE("update_level changes the decision level of a variable", "[api]")
// {
//   SATSentinel* sentinel = make_sentinel();
//   add_variable(sentinel, Tvar{1});

//   const Tlit literal{Tvar{1}, 1};

//   REQUIRE(assign(sentinel, literal));
//   REQUIRE(sentinel->state->level(Tvar{1}) == Tlevel{1});

//   REQUIRE(update_level(sentinel, literal, Tlevel{2}));
//   REQUIRE(sentinel->state->level(Tvar{1}) == Tlevel{2});
//   REQUIRE(check_invariants(sentinel));

//   destroy_sentinel(sentinel);
// }

// TEST_CASE("update_reason changes the reason clause for a variable", "[api]")
// {
//   SATSentinel* sentinel = make_sentinel();
//   add_variables(sentinel, {1, 2, 3});

//   // Create a clause
//   const Tclause clause{0};
//   const std::vector<Tlit> lits{
//     Tlit{Tvar{1}, 0},
//     Tlit{Tvar{2}, 1},
//     Tlit{Tvar{3}, 1}
//   };

//   REQUIRE(add_clause(sentinel, clause, lits.data(), static_cast<unsigned>(lits.size()), true));

//   const Tlit literal{Tvar{1}, 1};

//   REQUIRE(assign(sentinel, literal));
//   REQUIRE(sentinel->state->reason(Tvar{1}) == CLAUSE_UNDEF);

//   REQUIRE(update_reason(sentinel, literal, clause));
//   REQUIRE(sentinel->state->reason(Tvar{1}) == clause);
//   REQUIRE(check_invariants(sentinel));

//   destroy_sentinel(sentinel);
// }

// TEST_CASE("checkpoint triggers external command processing", "[api]")
// {
//   SATSentinel* sentinel = make_sentinel();
//   add_variable(sentinel, Tvar{1});

//   // checkpoint should return true when there are no external commands
//   REQUIRE(checkpoint(sentinel));

//   destroy_sentinel(sentinel);
// }

// TEST_CASE("save_execution and load_execution preserve solver state", "[api]")
// {
//   SATSentinel* sentinel = make_sentinel();
//   add_variables(sentinel, {1, 2, 3});

//   // Add a clause
//   const Tclause clause{0};
//   const std::vector<Tlit> lits{
//     Tlit{Tvar{1}, 0},
//     Tlit{Tvar{2}, 1},
//     Tlit{Tvar{3}, 1}
//   };
//   REQUIRE(add_clause(sentinel, clause, lits.data(), static_cast<unsigned>(lits.size()), true));

//   // Make some assignments
//   REQUIRE(assign(sentinel, Tlit{Tvar{1}, 1}));
//   REQUIRE(propagate(sentinel, Tlit{Tvar{1}, 1}));

//   // Save execution state
//   REQUIRE(save_execution(sentinel, "/tmp/test_execution.dat"));

//   // Load execution state
//   REQUIRE(load_execution(sentinel, "/tmp/test_execution.dat"));

//   REQUIRE(check_invariants(sentinel));

//   destroy_sentinel(sentinel);
// }
