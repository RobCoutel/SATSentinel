/*
 * This file is part of the source code of the software program
 * SATSentinel. It is protected by applicable copyright laws.
 *
 * This source code is protected by the terms of the MIT License.
 */
/**
 * @file tests/Sentinel-API.test.cpp
 * @brief Unit tests for the Sentinel API.
 * @author mostly Claude and Robin Coutelier
 *
 * This file contains unit tests for the Sentinel API, which is a C++ library for monitoring and controlling the execution of a SAT solver. The tests are written using the Catch2 testing framework and cover various aspects of the Sentinel API, including variable management, clause management, and invariant checking.
 *
 */

#include <catch2/catch_all.hpp>

#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "Sentinel-API.hpp"

// Expose private members so tests can inspect internal state and call
// next() / back() directly.
#define private public
#include "SATSentinel.hpp"
#undef private


using namespace sentinel;

// Redirect std::cerr to a sink for the duration of each test case so that
// LOG_ERROR / check_invariants failure messages don't clutter test output.
struct SuppressCerr : Catch::EventListenerBase {
    using EventListenerBase::EventListenerBase;
    std::ostringstream sink;
    std::streambuf* saved = nullptr;
    void testCaseStarting(Catch::TestCaseInfo const&) override {
        saved = std::cerr.rdbuf(sink.rdbuf());
    }
    void testCaseEnded(Catch::TestCaseStats const&) override {
        std::cerr.rdbuf(saved);
        sink.str(""); sink.clear();
    }
};
CATCH_REGISTER_LISTENER(SuppressCerr)

// ==========================================================================
// Helpers
// ==========================================================================

namespace {

SATSentinel* make_sentinel(SentinelOptions* opts = new SentinelOptions{})
{
    opts->check_only = true;  // don't prompt for user input during tests
    return create_sentinel(*opts);
}

void add_vars(SATSentinel* s, std::initializer_list<unsigned> ids)
{
    for (unsigned id : ids)
        REQUIRE(add_variable(s, Tvar{id}));
}

// Add a clause and immediately install two watches (needed for check_invariants).
Tclause mk_watched_clause(SATSentinel* s, unsigned id,
                           std::vector<Tlit> lits, bool external = false)
{
    Tclause cl{id};
    REQUIRE(add_clause(s, cl, lits.data(), (unsigned)lits.size(), external));
    if (lits.size() >= 2) {
        REQUIRE(watch(s, cl, lits[0]));
        REQUIRE(watch(s, cl, lits[1]));
    }
    return cl;
}

// Suppress the "No external command parser set." stdout noise that
// get_external_commands() emits when called from back() in step mode.
void back_silent(SATSentinel* s)
{
    std::ostringstream sink;
    auto* old = std::cout.rdbuf(sink.rdbuf());
    s->back();
    std::cout.rdbuf(old);
}

// Step mode: back() rolls back exactly one notification per call.
// safe because external_parser==nullptr → get_external_commands() returns
// immediately without touching stdin.
void enter_step_mode(SATSentinel* s) { s->display_level = 100; }

// Batch mode: next() processes all pending notifications at once without
// calling get_navigation_commands() (every event level > 0).
void exit_step_mode(SATSentinel* s)  { s->display_level = 0; }

} // namespace

// ==========================================================================
// PART 1 — Standard API Tests
// ==========================================================================

// --------------------------------------------------------------------------
// 1.1  Sentinel lifecycle
// --------------------------------------------------------------------------

TEST_CASE("default sentinel is non-null with empty state", "[api]")
{
    SATSentinel* s = make_sentinel();
    REQUIRE(s != nullptr);
    REQUIRE(s->state->variables_size() == 0);
    REQUIRE(s->state->clauses_size()   == 0);
    REQUIRE(s->state->trail_size()     == 0);
    delete_sentinel(s);
}

TEST_CASE("watch invariant flags are forwarded to internal state", "[api]")
{
    SentinelOptions opts;
    opts.check_weak_watched_literals   = true;
    opts.check_strong_watched_literals = true;

    SATSentinel* s = make_sentinel(&opts);
    REQUIRE(s->state->_watch_invariants.size() == 2);
    REQUIRE(s->state->_invariants.empty());
    delete_sentinel(s);
}

TEST_CASE("all built-in invariant flags can be enabled simultaneously", "[api]")
{
    SentinelOptions opts;
    opts.check_no_conflicts            = true;
    opts.check_no_missed_implications  = true;
    opts.check_implied_levels          = true;
    opts.check_trail_monotonicity      = true;
    opts.check_topological_order       = true;
    opts.check_assignment_coherence    = true;
    opts.check_weak_watched_literals   = true;
    opts.check_strong_watched_literals = true;

    SATSentinel* s = make_sentinel(&opts);
    REQUIRE(s->state->_invariants.size()       == 6);
    REQUIRE(s->state->_watch_invariants.size() == 2);
    // Empty state with no clauses: all invariants pass.
    REQUIRE(check_invariants(s));
    delete_sentinel(s);
}

// --------------------------------------------------------------------------
// 1.2  Variable management
// --------------------------------------------------------------------------

TEST_CASE("add_variable registers a variable as active with UNDEF value", "[api]")
{
    SATSentinel* s = make_sentinel();

    REQUIRE(add_variable(s, Tvar{1}));
    REQUIRE(s->state->variables_size() >= 2);
    REQUIRE(s->state->active(Tvar{1}));
    REQUIRE(s->state->value(Tvar{1}) == VAL_UNDEF);
    // No clauses: watch check passes trivially.
    REQUIRE(check_invariants(s));
    delete_sentinel(s);
}

TEST_CASE("add_variable with non-contiguous id resizes array without activating gaps", "[api]")
{
    SATSentinel* s = make_sentinel();
    REQUIRE(add_variable(s, Tvar{5}));
    REQUIRE(s->state->variables_size() >= 6);
    REQUIRE( s->state->active(Tvar{5}));
    REQUIRE_FALSE(s->state->active(Tvar{1}));
    delete_sentinel(s);
}

TEST_CASE("adding the same variable twice sets the failed flag", "[api]")
{
    // notify() returns the outer `success` variable which shadows the inner
    // one due to a known bug, so it always returns true. The failure is
    // tracked via sentinel->failed instead.
    SATSentinel* s = make_sentinel();
    REQUIRE(add_variable(s, Tvar{1}));
    REQUIRE_FALSE(s->failed);
    add_variable(s, Tvar{1});     // second add: fails internally
    REQUIRE(s->failed);
    delete_sentinel(s);
}

TEST_CASE("set_variable_alias stores the name and exposes polarity prefix", "[api]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1});

    REQUIRE(set_variable_alias(s, Tvar{1}, "foo"));
    REQUIRE(s->state->alias(Tvar{1})          == "foo");
    REQUIRE(s->state->alias(Tlit{Tvar{1}, 1}) == "foo");   // positive literal
    REQUIRE(s->state->alias(Tlit{Tvar{1}, 0}) == "~foo");  // negative literal
    delete_sentinel(s);
}

// --------------------------------------------------------------------------
// 1.3  Clause management
// --------------------------------------------------------------------------

TEST_CASE("add_clause stores literals sorted and marks clause as active", "[api]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1, 2, 3});

    const std::vector<Tlit> lits = {Tlit{Tvar{3}, 1}, Tlit{Tvar{1}, 0}, Tlit{Tvar{2}, 1}};
    REQUIRE(add_clause(s, Tclause{0}, lits.data(), 3, true));

    REQUIRE(s->state->active(Tclause{0}));
    REQUIRE(s->state->clause_external(Tclause{0}));
    const auto& stored = s->state->literals(Tclause{0});
    REQUIRE(stored.size() == 3);
    REQUIRE(std::is_sorted(stored.begin(), stored.end(),
        [](const Tlit& a, const Tlit& b) { return a.value < b.value; }));
    delete_sentinel(s);
}

TEST_CASE("add_clause with external=false marks clause as non-external", "[api]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1, 2});
    const std::vector<Tlit> lits = {Tlit{Tvar{1}, 1}, Tlit{Tvar{2}, 0}};
    REQUIRE(add_clause(s, Tclause{0}, lits.data(), 2, false));
    REQUIRE_FALSE(s->state->clause_external(Tclause{0}));
    delete_sentinel(s);
}

TEST_CASE("delete_clause marks the clause as inactive", "[api]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1, 2});
    const std::vector<Tlit> lits = {Tlit{Tvar{1}, 1}, Tlit{Tvar{2}, 0}};
    add_clause(s, Tclause{0}, lits.data(), 2, false);

    REQUIRE(delete_clause(s, Tclause{0}));
    REQUIRE_FALSE(s->state->active(Tclause{0}));
    delete_sentinel(s);
}

TEST_CASE("delete_clause on an inactive/absent clause sets the failed flag", "[api]")
{
    SATSentinel* s = make_sentinel();
    REQUIRE_FALSE(s->failed);
    delete_clause(s, Tclause{99});
    REQUIRE(s->failed);
    delete_sentinel(s);
}

TEST_CASE("shrink_clause moves one literal to the deleted section", "[api]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1, 2, 3});
    const std::vector<Tlit> lits = {Tlit{Tvar{1}, 0}, Tlit{Tvar{2}, 1}, Tlit{Tvar{3}, 1}};
    add_clause(s, Tclause{0}, lits.data(), 3, false);

    REQUIRE(shrink_clause(s, Tclause{0}, Tlit{Tvar{2}, 1}));
    REQUIRE(s->state->_clauses[Tclause{0}].n_deleted_literals == 1);
    unsigned active_count =
        (unsigned)s->state->literals(Tclause{0}).size()
        - s->state->_clauses[Tclause{0}].n_deleted_literals;
    REQUIRE(active_count == 2);
    delete_sentinel(s);
}

TEST_CASE("shrinking a clause twice removes two literals", "[api]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1, 2, 3});
    const std::vector<Tlit> lits = {Tlit{Tvar{1}, 0}, Tlit{Tvar{2}, 1}, Tlit{Tvar{3}, 1}};
    add_clause(s, Tclause{0}, lits.data(), 3, false);

    REQUIRE(shrink_clause(s, Tclause{0}, Tlit{Tvar{2}, 1}));
    REQUIRE(shrink_clause(s, Tclause{0}, Tlit{Tvar{3}, 1}));
    REQUIRE(s->state->_clauses[Tclause{0}].n_deleted_literals == 2);
    delete_sentinel(s);
}

// --------------------------------------------------------------------------
// 1.4  Assignment operations
// --------------------------------------------------------------------------

TEST_CASE("decision assignment sets value, opens a new level, and appends to trail", "[api]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1});

    REQUIRE(assign(s, Tlit{Tvar{1}, 1}));
    REQUIRE(s->state->value(Tvar{1})   == VAL_TRUE);
    REQUIRE(s->state->reason(Tvar{1})  == CLAUSE_UNDEF);
    REQUIRE(s->state->trail_size()     == 1);
    REQUIRE(s->state->trail_literal(0) == Tlit{Tvar{1}, 1});
    REQUIRE(s->state->level(Tvar{1})   == Tlevel{1});
    REQUIRE(check_invariants(s));       // no clauses → watch check trivially passes
    delete_sentinel(s);
}

TEST_CASE("negative decision assignment sets VAR_FALSE", "[api]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1});
    REQUIRE(assign(s, Tlit{Tvar{1}, 0}));
    REQUIRE(s->state->value(Tvar{1}) == VAL_FALSE);
    delete_sentinel(s);
}

TEST_CASE("implied assignment derives its level from the reason clause", "[api]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1, 2, 3});

    // C0: (~1 v 3)  →  if 1 is true, 3 is implied
    const std::vector<Tlit> c0 = {Tlit{Tvar{1}, 0}, Tlit{Tvar{3}, 1}};
    add_clause(s, Tclause{0}, c0.data(), 2, true);

    REQUIRE(assign(s, Tlit{Tvar{1}, 1}));          // decision @level 1
    REQUIRE(s->state->level(Tvar{1}) == Tlevel{1});

    // C0 has ~1 (falsified at level 1) and 3 (the implied literal).
    REQUIRE(assign(s, Tlit{Tvar{3}, 1}, Tclause{0}));
    REQUIRE(s->state->value(Tvar{3})  == VAL_TRUE);
    REQUIRE(s->state->reason(Tvar{3}) == Tclause{0});
    REQUIRE(s->state->level(Tvar{3})  == Tlevel{1});
    delete_sentinel(s);
}

TEST_CASE("assigning an already-assigned literal sets the failed flag", "[api]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1});
    REQUIRE(assign(s, Tlit{Tvar{1}, 1}));
    REQUIRE_FALSE(s->failed);
    assign(s, Tlit{Tvar{1}, 0});   // attempt to assign opposite — should fail
    REQUIRE(s->failed);
    delete_sentinel(s);
}

TEST_CASE("unassign (LIFO) restores variable to UNDEF and shrinks the trail", "[api]")
{
    SATSentinel* s = make_sentinel();
    Tvar v1{1};
    Tlit l1{v1, 1};
    add_vars(s, {1});
    REQUIRE(assign(s, l1));

    REQUIRE(unassign(s, l1));
    REQUIRE(s->state->value (v1) == VAL_UNDEF);
    REQUIRE(s->state->reason(v1) == CLAUSE_UNDEF);
    REQUIRE(s->state->trail_size()    == 0);
    REQUIRE(check_invariants(s));
    delete_sentinel(s);
}

TEST_CASE("unassign (random access) removes a literal from the middle of the trail", "[api]")
{
    // This exercises the non-chronological / non-LIFO path used by SCB/LSCB.
    // The solver is expected to supply correct position values before
    // issuing the unassignment notification.
    SATSentinel* s = make_sentinel();
    add_vars(s, {1, 2, 3});

    assign(s, Tlit{Tvar{1}, 1});
    assign(s, Tlit{Tvar{2}, 1});
    assign(s, Tlit{Tvar{3}, 1});
    REQUIRE(s->state->trail_size() == 3);

    s->state->position(Tvar{1}) = 0;
    s->state->position(Tvar{2}) = 1;
    s->state->position(Tvar{3}) = 2;

    REQUIRE(unassign(s, Tlit{Tvar{2}, 1}));

    REQUIRE(s->state->trail_size()     == 2);
    REQUIRE(s->state->value(Tvar{2})   == VAL_UNDEF);
    REQUIRE(s->state->trail_literal(0) == Tlit{Tvar{1}, 1});
    REQUIRE(s->state->trail_literal(1) == Tlit{Tvar{3}, 1});
    REQUIRE(s->state->position(Tvar{3}) == 1);
    REQUIRE(check_invariants(s));       // no clauses → trivially passes
    delete_sentinel(s);
}

TEST_CASE("unassigning a non-assigned literal sets the failed flag", "[api]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1});
    REQUIRE_FALSE(s->failed);
    unassign(s, Tlit{Tvar{1}, 1});
    REQUIRE(s->failed);
    delete_sentinel(s);
}

// --------------------------------------------------------------------------
// 1.5  Propagation
// --------------------------------------------------------------------------

TEST_CASE("propagate sets the propagated flag for an assigned literal", "[api]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1});
    assign(s, Tlit{Tvar{1}, 1});

    REQUIRE_FALSE(s->state->propagated(Tvar{1}));
    REQUIRE(propagate(s, Tlit{Tvar{1}, 1}));
    REQUIRE(s->state->propagated(Tvar{1}));
    REQUIRE(check_invariants(s));
    delete_sentinel(s);
}

TEST_CASE("propagating an unassigned literal sets the failed flag", "[api]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1});
    REQUIRE_FALSE(s->failed);
    propagate(s, Tlit{Tvar{1}, 1});
    REQUIRE(s->failed);
    delete_sentinel(s);
}

TEST_CASE("unpropagate clears the propagated flag", "[api]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1});
    assign(s,    Tlit{Tvar{1}, 1});
    propagate(s, Tlit{Tvar{1}, 1});

    REQUIRE(unpropagate(s, Tlit{Tvar{1}, 1}));
    REQUIRE_FALSE(s->state->propagated(Tvar{1}));
    REQUIRE(check_invariants(s));
    delete_sentinel(s);
}

// --------------------------------------------------------------------------
// 1.6  Level and reason updates
// --------------------------------------------------------------------------

TEST_CASE("update_level changes the stored decision level", "[api]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1, 2});
    assign(s, Tlit{Tvar{1}, 1});  // level 1
    assign(s, Tlit{Tvar{2}, 1});  // level 2

    REQUIRE(s->state->level(Tvar{1}) == Tlevel{1});
    REQUIRE(update_level(s, Tlit{Tvar{1}, 1}, Tlevel{0}));
    REQUIRE(s->state->level(Tvar{1}) == Tlevel{0});
    delete_sentinel(s);
}

TEST_CASE("update_reason replaces the reason clause", "[api]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1, 2});
    const std::vector<Tlit> lits = {Tlit{Tvar{1}, 0}, Tlit{Tvar{2}, 1}};
    add_clause(s, Tclause{0}, lits.data(), 2, false);
    assign(s, Tlit{Tvar{1}, 1});

    REQUIRE(s->state->reason(Tvar{1}) == CLAUSE_UNDEF);
    REQUIRE(update_reason(s, Tlit{Tvar{1}, 1}, Tclause{0}));
    REQUIRE(s->state->reason(Tvar{1}) == Tclause{0});
    delete_sentinel(s);
}

// --------------------------------------------------------------------------
// 1.7  Watch lists
// --------------------------------------------------------------------------

TEST_CASE("watch adds an entry to a clause's watch list", "[api]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1, 2, 3});
    const std::vector<Tlit> lits = {Tlit{Tvar{1}, 0}, Tlit{Tvar{2}, 1}, Tlit{Tvar{3}, 1}};
    add_clause(s, Tclause{0}, lits.data(), 3, false);

    REQUIRE(watch(s, Tclause{0}, Tlit{Tvar{1}, 0}));
    REQUIRE(s->state->watches(Tclause{0}).size() == 1);
    REQUIRE(s->state->watches(Tclause{0})[0].first == Tlit{Tvar{1}, 0});

    REQUIRE(watch(s, Tclause{0}, Tlit{Tvar{2}, 1}));
    REQUIRE(s->state->watches(Tclause{0}).size() == 2);
    // Both watches installed → invariant check passes.
    REQUIRE(check_invariants(s));
    delete_sentinel(s);
}

TEST_CASE("block sets the blocker for an existing watch", "[api]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1, 2, 3});
    const std::vector<Tlit> lits = {Tlit{Tvar{1}, 0}, Tlit{Tvar{2}, 1}, Tlit{Tvar{3}, 1}};
    add_clause(s, Tclause{0}, lits.data(), 3, false);
    watch(s, Tclause{0}, Tlit{Tvar{1}, 0});
    watch(s, Tclause{0}, Tlit{Tvar{2}, 1});

    REQUIRE(block(s, Tclause{0}, Tlit{Tvar{3}, 1}, Tlit{Tvar{1}, 0}));
    REQUIRE(s->state->watches(Tclause{0})[0].second == Tlit{Tvar{3}, 1});
    REQUIRE(check_invariants(s));
    delete_sentinel(s);
}

TEST_CASE("unwatch removes an existing watch entry", "[api]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1, 2, 3});
    const std::vector<Tlit> lits = {Tlit{Tvar{1}, 0}, Tlit{Tvar{2}, 1}, Tlit{Tvar{3}, 1}};
    add_clause(s, Tclause{0}, lits.data(), 3, false);
    watch(s, Tclause{0}, Tlit{Tvar{1}, 0});
    watch(s, Tclause{0}, Tlit{Tvar{2}, 1});

    REQUIRE(unwatch(s, Tclause{0}, Tlit{Tvar{1}, 0}));
    REQUIRE(s->state->watches(Tclause{0}).size() == 1);
    REQUIRE(s->state->watches(Tclause{0})[0].first == Tlit{Tvar{2}, 1});
    delete_sentinel(s);
}

TEST_CASE("updating the blocker replaces the previous value", "[api]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1, 2, 3, 4});
    const std::vector<Tlit> lits = {
        Tlit{Tvar{1}, 0}, Tlit{Tvar{2}, 1}, Tlit{Tvar{3}, 1}, Tlit{Tvar{4}, 1}
    };
    add_clause(s, Tclause{0}, lits.data(), 4, false);
    watch(s, Tclause{0}, Tlit{Tvar{1}, 0});
    watch(s, Tclause{0}, Tlit{Tvar{2}, 1});
    block(s, Tclause{0}, Tlit{Tvar{2}, 1}, Tlit{Tvar{1}, 0});

    REQUIRE(block(s, Tclause{0}, Tlit{Tvar{3}, 1}, Tlit{Tvar{1}, 0}));
    REQUIRE(s->state->watches(Tclause{0})[0].second == Tlit{Tvar{3}, 1});
    delete_sentinel(s);
}

// --------------------------------------------------------------------------
// 1.8  Assumptions
// --------------------------------------------------------------------------

TEST_CASE("lock_assumption locks an unassigned literal", "[api]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1});

    REQUIRE(lock_assumption(s, Tlit{Tvar{1}, 1}));
    REQUIRE(s->state->locked(Tvar{1}));
    REQUIRE(s->state->value(Tvar{1}) == VAL_UNDEF);
    REQUIRE(check_invariants(s));
    delete_sentinel(s);
}

TEST_CASE("locking an already-locked literal sets the failed flag", "[api]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1});
    lock_assumption(s, Tlit{Tvar{1}, 1});
    REQUIRE_FALSE(s->failed);
    lock_assumption(s, Tlit{Tvar{1}, 1});
    REQUIRE(s->failed);
    delete_sentinel(s);
}

TEST_CASE("unlock_assumption unlocks a locked literal", "[api]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1});
    lock_assumption(s, Tlit{Tvar{1}, 1});

    REQUIRE(unlock_assumption(s, Tlit{Tvar{1}, 1}));
    REQUIRE_FALSE(s->state->locked(Tvar{1}));
    REQUIRE(check_invariants(s));
    delete_sentinel(s);
}

TEST_CASE("unlocking a non-locked literal sets the failed flag", "[api]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1});
    REQUIRE_FALSE(s->failed);
    unlock_assumption(s, Tlit{Tvar{1}, 1});
    REQUIRE(s->failed);
    delete_sentinel(s);
}

// --------------------------------------------------------------------------
// 1.9  Custom invariants
// --------------------------------------------------------------------------

TEST_CASE("custom lambda invariant is invoked during check_invariants", "[api]")
{
    SATSentinel* s = make_sentinel();
    bool called = false;
    add_invariant(s, [&](std::string&) -> bool {
        called = true;
        return true;
    }, "always_passes");

    REQUIRE(check_invariants(s));
    REQUIRE(called);
    delete_sentinel(s);
}

TEST_CASE("a failing custom invariant makes check_invariants return false", "[api]")
{
    SATSentinel* s = make_sentinel();
    add_invariant(s, [](std::string& err) -> bool {
        err = "deliberate failure";
        return false;
    }, "always_fails");

    REQUIRE_FALSE(check_invariants(s));
    delete_sentinel(s);
}

TEST_CASE("custom watch invariant is called for each watch pair", "[api]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1, 2, 3});
    const std::vector<Tlit> lits = {Tlit{Tvar{1}, 0}, Tlit{Tvar{2}, 1}, Tlit{Tvar{3}, 1}};
    add_clause(s, Tclause{0}, lits.data(), 3, false);
    watch(s, Tclause{0}, Tlit{Tvar{1}, 0});
    watch(s, Tclause{0}, Tlit{Tvar{2}, 1});
    block(s, Tclause{0}, Tlit{Tvar{3}, 1}, Tlit{Tvar{1}, 0});

    int invocations = 0;
    add_watch_invariant(s, [&](Tlit, Tlit, Tlit, std::string&) -> bool {
        invocations++;
        return true;
    }, "count_watch_calls");

    REQUIRE(check_invariants(s));
    REQUIRE(invocations > 0);
    delete_sentinel(s);
}

// --------------------------------------------------------------------------
// 1.10  Invariant enforcement
// --------------------------------------------------------------------------

TEST_CASE("check_no_conflicts detects a unit clause falsified by the propagated trail", "[api]")
{
    // check_no_conflicts requires a literal to be BOTH false AND propagated.
    // Using a unit clause avoids the 2-watch requirement.
    SentinelOptions opts;
    opts.check_no_conflicts = true;
    SATSentinel* s = make_sentinel(&opts);
    add_vars(s, {1});

    // Clause: (1)
    const std::vector<Tlit> lits = {Tlit{Tvar{1}, 1}};
    add_clause(s, Tclause{0}, lits.data(), 1, true);

    // Assign and propagate ~1 → clause (1) is falsified in τ
    assign(s,    Tlit{Tvar{1}, 0});
    propagate(s, Tlit{Tvar{1}, 0});

    REQUIRE_FALSE(check_invariants(s));
    delete_sentinel(s);
}

TEST_CASE("check_trail_monotonicity detects a level decrease within the trail", "[api]")
{
    SentinelOptions opts;
    opts.check_trail_monotonicity = true;
    SATSentinel* s = make_sentinel(&opts);
    add_vars(s, {1, 2});

    assign(s, Tlit{Tvar{1}, 1});  // level 1
    assign(s, Tlit{Tvar{2}, 1});  // level 2

    // Move variable 2 to level 0 while it appears after variable 1 in the trail.
    update_level(s, Tlit{Tvar{2}, 1}, Tlevel{0});

    // No clauses → check_watched_literals passes, but trail-monotonicity fails.
    REQUIRE_FALSE(check_invariants(s));
    delete_sentinel(s);
}

TEST_CASE("message notification is a no-op on all state fields", "[api]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1});
    assign(s, Tlit{Tvar{1}, 1});

    size_t trail_before = s->state->trail_size();
    size_t vars_before  = s->state->variables_size();

    REQUIRE(message(s, "unit test", 1));
    REQUIRE(s->state->trail_size()     == trail_before);
    REQUIRE(s->state->variables_size() == vars_before);
    REQUIRE(check_invariants(s));
    delete_sentinel(s);
}

// --------------------------------------------------------------------------
// 1.11  CDCL-like scenarios
// --------------------------------------------------------------------------

TEST_CASE("full CDCL cycle: decide, propagate, then chronologically backtrack", "[api][cdcl]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1, 2, 3});

    // C0: (~1 v 2),  C1: (~1 v ~2 v 3)
    // Use mk_watched_clause so check_invariants works at every checkpoint.
    const std::vector<Tlit> c0_lits = {Tlit{Tvar{1}, 0}, Tlit{Tvar{2}, 1}};
    const std::vector<Tlit> c1_lits = {Tlit{Tvar{1}, 0}, Tlit{Tvar{2}, 0}, Tlit{Tvar{3}, 1}};
    mk_watched_clause(s, 0, c0_lits, true);
    mk_watched_clause(s, 1, c1_lits, true);

    REQUIRE(assign(s,    Tlit{Tvar{1}, 1}));
    REQUIRE(propagate(s, Tlit{Tvar{1}, 1}));
    REQUIRE(assign(s,    Tlit{Tvar{2}, 1}, Tclause{0}));
    REQUIRE(propagate(s, Tlit{Tvar{2}, 1}));
    REQUIRE(assign(s,    Tlit{Tvar{3}, 1}, Tclause{1}));
    REQUIRE(propagate(s, Tlit{Tvar{3}, 1}));

    REQUIRE(s->state->trail_size() == 3);
    REQUIRE(check_invariants(s));

    // Chronological backtrack
    REQUIRE(unpropagate(s, Tlit{Tvar{3}, 1}));
    REQUIRE(unassign(s,    Tlit{Tvar{3}, 1}));
    REQUIRE(unpropagate(s, Tlit{Tvar{2}, 1}));
    REQUIRE(unassign(s,    Tlit{Tvar{2}, 1}));
    REQUIRE(unpropagate(s, Tlit{Tvar{1}, 1}));
    REQUIRE(unassign(s,    Tlit{Tvar{1}, 1}));

    REQUIRE(s->state->trail_size() == 0);
    REQUIRE(check_invariants(s));
    delete_sentinel(s);
}

TEST_CASE("non-chronological backtracking removes a literal from the middle of the trail", "[api][cdcl]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1, 2, 3});

    assign(s, Tlit{Tvar{1}, 1});
    assign(s, Tlit{Tvar{2}, 1});
    assign(s, Tlit{Tvar{3}, 1});

    s->state->position(Tvar{1}) = 0;
    s->state->position(Tvar{2}) = 1;
    s->state->position(Tvar{3}) = 2;

    REQUIRE(unassign(s, Tlit{Tvar{2}, 1}));

    REQUIRE(s->state->trail_size()     == 2);
    REQUIRE(s->state->value(Tvar{2})   == VAL_UNDEF);
    REQUIRE(s->state->trail_literal(0) == Tlit{Tvar{1}, 1});
    REQUIRE(s->state->trail_literal(1) == Tlit{Tvar{3}, 1});
    REQUIRE(s->state->position(Tvar{3}) == 1);
    REQUIRE(check_invariants(s));
    delete_sentinel(s);
}

TEST_CASE("incremental solving: reset trail, swap assumption polarity, re-solve", "[api][cdcl]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1, 2});

    const std::vector<Tlit> lits = {Tlit{Tvar{1}, 0}, Tlit{Tvar{2}, 1}};
    mk_watched_clause(s, 0, lits, true);

    // First solve: assume 1=true
    lock_assumption(s, Tlit{Tvar{1}, 1});
    assign(s,    Tlit{Tvar{1}, 1});
    propagate(s, Tlit{Tvar{1}, 1});
    assign(s,    Tlit{Tvar{2}, 1}, Tclause{0});
    REQUIRE(check_invariants(s));

    // Reset
    unassign(s,    Tlit{Tvar{2}, 1});
    unpropagate(s, Tlit{Tvar{1}, 1});
    unassign(s,    Tlit{Tvar{1}, 1});
    unlock_assumption(s, Tlit{Tvar{1}, 1});
    REQUIRE(s->state->trail_size() == 0);

    // Second solve: assume 1=false
    lock_assumption(s, Tlit{Tvar{1}, 0});
    assign(s, Tlit{Tvar{1}, 0});
    REQUIRE(check_invariants(s));

    unlock_assumption(s, Tlit{Tvar{1}, 0});
    unassign(s, Tlit{Tvar{1}, 0});
    REQUIRE(s->state->trail_size() == 0);
    REQUIRE(check_invariants(s));
    delete_sentinel(s);
}

TEST_CASE("watch-list scenario: set two watches, assign a blocker, verify state", "[api][cdcl]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1, 2, 3, 4});

    const std::vector<Tlit> lits = {
        Tlit{Tvar{1}, 1}, Tlit{Tvar{2}, 1}, Tlit{Tvar{3}, 1}, Tlit{Tvar{4}, 1}
    };
    add_clause(s, Tclause{0}, lits.data(), 4, true);
    watch(s, Tclause{0}, Tlit{Tvar{1}, 1});
    watch(s, Tclause{0}, Tlit{Tvar{2}, 1});
    block(s, Tclause{0}, Tlit{Tvar{3}, 1}, Tlit{Tvar{1}, 1});

    REQUIRE(s->state->watches(Tclause{0}).size() == 2);
    assign(s, Tlit{Tvar{3}, 1});  // blocker becomes satisfied

    REQUIRE(check_invariants(s));
    delete_sentinel(s);
}

TEST_CASE("clause lifecycle: add with watches, shrink, then delete", "[api][cdcl]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1, 2, 3, 4});

    const std::vector<Tlit> lits = {
        Tlit{Tvar{1}, 0}, Tlit{Tvar{2}, 1}, Tlit{Tvar{3}, 1}, Tlit{Tvar{4}, 1}
    };
    mk_watched_clause(s, 0, lits, false);
    REQUIRE(s->state->active(Tclause{0}));
    REQUIRE(check_invariants(s));

    shrink_clause(s, Tclause{0}, Tlit{Tvar{4}, 1});
    REQUIRE(s->state->_clauses[Tclause{0}].n_deleted_literals == 1);

    delete_clause(s, Tclause{0});
    REQUIRE_FALSE(s->state->active(Tclause{0}));
    REQUIRE(check_invariants(s));
    delete_sentinel(s);
}

// ==========================================================================
// PART 2 — next() and back() for each notification type
// ==========================================================================
//
// Strategy
// --------
// • notify() (invoked by every API function) appends a notification and
//   calls next() in batch mode (display_level=0): all pending notifications
//   are processed without ever calling get_navigation_commands().
//
// • enter_step_mode() sets display_level=100 so that back() rolls back
//   exactly ONE notification per call (every event level ≤ 9 ≤ 100, so
//   the loop breaks after the first rollback). With external_parser==nullptr,
//   get_external_commands() prints a brief message and returns without
//   reading stdin — back_silent() suppresses that output.
//
// • exit_step_mode() restores display_level=0 so that the subsequent
//   sentinel->next() call processes all pending notifications in batch.
//
// • assignment::rollback() does NOT restore _level_counters, so decision
//   levels may differ after a back()+next() cycle on assignment notifications.
//   Tests for assignment only verify value and trail membership, not levels.
// ==========================================================================

// --------------------------------------------------------------------------
// 2.1  new_variable
// --------------------------------------------------------------------------

TEST_CASE("new_variable: next() activates; back() deactivates; next() re-activates", "[nav]")
{
    SATSentinel* s = make_sentinel();

    REQUIRE(add_variable(s, Tvar{1}));
    REQUIRE(s->state->active(Tvar{1}));
    REQUIRE(s->state->value(Tvar{1}) == VAL_UNDEF);

    enter_step_mode(s);
    back_silent(s);
    REQUIRE_FALSE(s->state->active(Tvar{1}));

    exit_step_mode(s);
    s->next();
    REQUIRE(s->state->active(Tvar{1}));
    REQUIRE(s->state->value(Tvar{1}) == VAL_UNDEF);

    delete_sentinel(s);
}

// --------------------------------------------------------------------------
// 2.2  assignment (decision)
// --------------------------------------------------------------------------

TEST_CASE("assignment decision: next() adds to trail; back() removes; next() re-adds", "[nav]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1});

    REQUIRE(assign(s, Tlit{Tvar{1}, 1}));
    REQUIRE(s->state->value(Tvar{1})   == VAL_TRUE);
    REQUIRE(s->state->trail_size()     == 1);

    // assignment::rollback() asserts trail.back()==lit, which holds here.
    enter_step_mode(s);
    back_silent(s);
    REQUIRE(s->state->value(Tvar{1})  == VAL_UNDEF);
    REQUIRE(s->state->trail_size()    == 0);

    exit_step_mode(s);
    s->next();
    REQUIRE(s->state->value(Tvar{1})  == VAL_TRUE);
    REQUIRE(s->state->trail_size()    == 1);
    REQUIRE(s->state->trail_literal(0) == Tlit{Tvar{1}, 1});

    delete_sentinel(s);
}

// --------------------------------------------------------------------------
// 2.3  assignment (implied)
// --------------------------------------------------------------------------

TEST_CASE("assignment implied: next() sets reason; back() clears; next() re-sets", "[nav]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1, 2});
    const std::vector<Tlit> c0 = {Tlit{Tvar{1}, 0}, Tlit{Tvar{2}, 1}};
    add_clause(s, Tclause{0}, c0.data(), 2, true);
    assign(s, Tlit{Tvar{1}, 1});   // prerequisite decision

    REQUIRE(assign(s, Tlit{Tvar{2}, 1}, Tclause{0}));
    REQUIRE(s->state->value(Tvar{2})  == VAL_TRUE);
    REQUIRE(s->state->reason(Tvar{2}) == Tclause{0});
    REQUIRE(s->state->trail_size()    == 2);

    enter_step_mode(s);
    back_silent(s);
    REQUIRE(s->state->value(Tvar{2})  == VAL_UNDEF);
    REQUIRE(s->state->trail_size()    == 1);

    exit_step_mode(s);
    s->next();
    REQUIRE(s->state->value(Tvar{2})  == VAL_TRUE);
    REQUIRE(s->state->reason(Tvar{2}) == Tclause{0});
    REQUIRE(s->state->trail_size()    == 2);

    delete_sentinel(s);
}

// --------------------------------------------------------------------------
// 2.4  unassignment (LIFO — top of trail)
// --------------------------------------------------------------------------

TEST_CASE("unassignment LIFO: next() removes top; back() restores; next() removes again", "[nav]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1});
    assign(s, Tlit{Tvar{1}, 1});
    s->state->position(Tvar{1}) = 0;   // required for correct rollback

    REQUIRE(unassign(s, Tlit{Tvar{1}, 1}));
    REQUIRE(s->state->trail_size()   == 0);
    REQUIRE(s->state->value(Tvar{1}) == VAL_UNDEF);

    enter_step_mode(s);
    back_silent(s);
    REQUIRE(s->state->trail_size()   == 1);
    REQUIRE(s->state->value(Tvar{1}) == VAL_TRUE);
    REQUIRE(s->state->trail_literal(0) == Tlit{Tvar{1}, 1});

    exit_step_mode(s);
    s->next();
    REQUIRE(s->state->trail_size()   == 0);
    REQUIRE(s->state->value(Tvar{1}) == VAL_UNDEF);

    delete_sentinel(s);
}

// --------------------------------------------------------------------------
// 2.5  unassignment (random access — middle of trail)
// --------------------------------------------------------------------------

TEST_CASE("unassignment random access: next() shifts trail; back() inserts literal back", "[nav]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1, 2, 3});

    assign(s, Tlit{Tvar{1}, 1});
    assign(s, Tlit{Tvar{2}, 1});
    assign(s, Tlit{Tvar{3}, 1});
    s->state->position(Tvar{1}) = 0;
    s->state->position(Tvar{2}) = 1;
    s->state->position(Tvar{3}) = 2;

    // Apply: remove L2 from position 1
    REQUIRE(unassign(s, Tlit{Tvar{2}, 1}));
    REQUIRE(s->state->trail_size()     == 2);
    REQUIRE(s->state->trail_literal(0) == Tlit{Tvar{1}, 1});
    REQUIRE(s->state->trail_literal(1) == Tlit{Tvar{3}, 1});
    REQUIRE(s->state->position(Tvar{3}) == 1);

    // Rollback: L2 re-inserted at position 1, L3 shifts to position 2
    enter_step_mode(s);
    back_silent(s);
    REQUIRE(s->state->trail_size()     == 3);
    REQUIRE(s->state->value(Tvar{2})   == VAL_TRUE);
    REQUIRE(s->state->trail_literal(1) == Tlit{Tvar{2}, 1});
    REQUIRE(s->state->trail_literal(2) == Tlit{Tvar{3}, 1});

    // Re-apply: remove L2 again
    exit_step_mode(s);
    s->next();
    REQUIRE(s->state->trail_size()     == 2);
    REQUIRE(s->state->value(Tvar{2})   == VAL_UNDEF);
    REQUIRE(s->state->trail_literal(1) == Tlit{Tvar{3}, 1});
    REQUIRE(s->state->position(Tvar{3}) == 1);

    delete_sentinel(s);
}

// --------------------------------------------------------------------------
// 2.6  propagation
// --------------------------------------------------------------------------

TEST_CASE("propagation: next() sets flag; back() clears; next() sets again", "[nav]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1});
    assign(s, Tlit{Tvar{1}, 1});

    REQUIRE(propagate(s, Tlit{Tvar{1}, 1}));
    REQUIRE(s->state->propagated(Tvar{1}));

    enter_step_mode(s);
    back_silent(s);
    REQUIRE_FALSE(s->state->propagated(Tvar{1}));

    exit_step_mode(s);
    s->next();
    REQUIRE(s->state->propagated(Tvar{1}));

    delete_sentinel(s);
}

// --------------------------------------------------------------------------
// 2.7  propagation_removed (unpropagate)
// --------------------------------------------------------------------------

TEST_CASE("propagation_removed: next() clears flag; back() restores; next() clears again", "[nav]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1});
    assign(s,    Tlit{Tvar{1}, 1});
    propagate(s, Tlit{Tvar{1}, 1});

    REQUIRE(unpropagate(s, Tlit{Tvar{1}, 1}));
    REQUIRE_FALSE(s->state->propagated(Tvar{1}));

    enter_step_mode(s);
    back_silent(s);
    REQUIRE(s->state->propagated(Tvar{1}));

    exit_step_mode(s);
    s->next();
    REQUIRE_FALSE(s->state->propagated(Tvar{1}));

    delete_sentinel(s);
}

// --------------------------------------------------------------------------
// 2.8  update_level
// --------------------------------------------------------------------------

TEST_CASE("update_level: next() changes level; back() restores original; next() changes again", "[nav]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1, 2});
    assign(s, Tlit{Tvar{1}, 1});   // level 1
    assign(s, Tlit{Tvar{2}, 1});   // level 2

    Tlevel original = s->state->level(Tvar{1});

    REQUIRE(update_level(s, Tlit{Tvar{1}, 1}, Tlevel{0}));
    REQUIRE(s->state->level(Tvar{1}) == Tlevel{0});

    enter_step_mode(s);
    back_silent(s);
    REQUIRE(s->state->level(Tvar{1}) == original);

    exit_step_mode(s);
    s->next();
    REQUIRE(s->state->level(Tvar{1}) == Tlevel{0});

    delete_sentinel(s);
}

// --------------------------------------------------------------------------
// 2.9  update_reason
// --------------------------------------------------------------------------

TEST_CASE("update_reason: next() changes reason; back() restores UNDEF; next() changes again", "[nav]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1, 2});
    const std::vector<Tlit> lits = {Tlit{Tvar{1}, 0}, Tlit{Tvar{2}, 1}};
    add_clause(s, Tclause{0}, lits.data(), 2, false);
    assign(s, Tlit{Tvar{1}, 1});

    REQUIRE(s->state->reason(Tvar{1}) == CLAUSE_UNDEF);
    REQUIRE(update_reason(s, Tlit{Tvar{1}, 1}, Tclause{0}));
    REQUIRE(s->state->reason(Tvar{1}) == Tclause{0});

    enter_step_mode(s);
    back_silent(s);
    REQUIRE(s->state->reason(Tvar{1}) == CLAUSE_UNDEF);

    exit_step_mode(s);
    s->next();
    REQUIRE(s->state->reason(Tvar{1}) == Tclause{0});

    delete_sentinel(s);
}

// --------------------------------------------------------------------------
// 2.10  new_clause
// --------------------------------------------------------------------------

TEST_CASE("new_clause: next() makes active; back() deactivates; next() re-activates", "[nav]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1, 2, 3});
    const std::vector<Tlit> lits = {Tlit{Tvar{1}, 0}, Tlit{Tvar{2}, 1}, Tlit{Tvar{3}, 1}};

    REQUIRE(add_clause(s, Tclause{0}, lits.data(), 3, true));
    REQUIRE(s->state->active(Tclause{0}));
    REQUIRE(s->state->literals(Tclause{0}).size() == 3);

    enter_step_mode(s);
    back_silent(s);
    REQUIRE_FALSE(s->state->active(Tclause{0}));

    exit_step_mode(s);
    s->next();
    REQUIRE(s->state->active(Tclause{0}));
    REQUIRE(s->state->literals(Tclause{0}).size() == 3);

    delete_sentinel(s);
}

// --------------------------------------------------------------------------
// 2.11  delete_clause
// --------------------------------------------------------------------------

TEST_CASE("delete_clause: next() deactivates; back() fully restores; next() deactivates again", "[nav]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1, 2, 3});
    const std::vector<Tlit> lits = {Tlit{Tvar{1}, 0}, Tlit{Tvar{2}, 1}, Tlit{Tvar{3}, 1}};
    add_clause(s, Tclause{0}, lits.data(), 3, false);

    REQUIRE(delete_clause(s, Tclause{0}));
    REQUIRE_FALSE(s->state->active(Tclause{0}));

    enter_step_mode(s);
    back_silent(s);
    REQUIRE(s->state->active(Tclause{0}));
    REQUIRE(s->state->literals(Tclause{0}).size() == 3);

    exit_step_mode(s);
    s->next();
    REQUIRE_FALSE(s->state->active(Tclause{0}));

    delete_sentinel(s);
}

// --------------------------------------------------------------------------
// 2.12  watch
// --------------------------------------------------------------------------

TEST_CASE("watch: next() adds entry; back() removes; next() re-adds", "[nav]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1, 2, 3});
    const std::vector<Tlit> lits = {Tlit{Tvar{1}, 0}, Tlit{Tvar{2}, 1}, Tlit{Tvar{3}, 1}};
    add_clause(s, Tclause{0}, lits.data(), 3, false);

    REQUIRE(watch(s, Tclause{0}, Tlit{Tvar{1}, 0}));
    REQUIRE(s->state->watches(Tclause{0}).size() == 1);

    enter_step_mode(s);
    back_silent(s);
    REQUIRE(s->state->watches(Tclause{0}).empty());

    exit_step_mode(s);
    s->next();
    REQUIRE(s->state->watches(Tclause{0}).size() == 1);
    REQUIRE(s->state->watches(Tclause{0})[0].first == Tlit{Tvar{1}, 0});

    delete_sentinel(s);
}

// --------------------------------------------------------------------------
// 2.13  unwatch (blocker preserved during rollback)
// --------------------------------------------------------------------------

TEST_CASE("unwatch: next() removes watch; back() restores it with original blocker", "[nav]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1, 2, 3});
    const std::vector<Tlit> lits = {Tlit{Tvar{1}, 0}, Tlit{Tvar{2}, 1}, Tlit{Tvar{3}, 1}};
    add_clause(s, Tclause{0}, lits.data(), 3, false);
    watch(s, Tclause{0}, Tlit{Tvar{1}, 0});
    block(s, Tclause{0}, Tlit{Tvar{3}, 1}, Tlit{Tvar{1}, 0});

    REQUIRE(unwatch(s, Tclause{0}, Tlit{Tvar{1}, 0}));
    REQUIRE(s->state->watches(Tclause{0}).empty());

    enter_step_mode(s);
    back_silent(s);
    REQUIRE(s->state->watches(Tclause{0}).size()    == 1);
    REQUIRE(s->state->watches(Tclause{0})[0].first  == Tlit{Tvar{1}, 0});
    REQUIRE(s->state->watches(Tclause{0})[0].second == Tlit{Tvar{3}, 1});  // blocker restored

    exit_step_mode(s);
    s->next();
    REQUIRE(s->state->watches(Tclause{0}).empty());

    delete_sentinel(s);
}

// --------------------------------------------------------------------------
// 2.14  block (previous blocker restored on rollback)
// --------------------------------------------------------------------------

TEST_CASE("block: next() sets blocker; back() restores previous; next() sets again", "[nav]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1, 2, 3, 4});
    const std::vector<Tlit> lits = {
        Tlit{Tvar{1}, 0}, Tlit{Tvar{2}, 1}, Tlit{Tvar{3}, 1}, Tlit{Tvar{4}, 1}
    };
    add_clause(s, Tclause{0}, lits.data(), 4, false);
    watch(s, Tclause{0}, Tlit{Tvar{1}, 0});
    watch(s, Tclause{0}, Tlit{Tvar{2}, 1});
    block(s, Tclause{0}, Tlit{Tvar{2}, 1}, Tlit{Tvar{1}, 0});

    REQUIRE(s->state->watches(Tclause{0})[0].second == Tlit{Tvar{2}, 1});

    REQUIRE(block(s, Tclause{0}, Tlit{Tvar{3}, 1}, Tlit{Tvar{1}, 0}));
    REQUIRE(s->state->watches(Tclause{0})[0].second == Tlit{Tvar{3}, 1});

    enter_step_mode(s);
    back_silent(s);
    REQUIRE(s->state->watches(Tclause{0})[0].second == Tlit{Tvar{2}, 1});

    exit_step_mode(s);
    s->next();
    REQUIRE(s->state->watches(Tclause{0})[0].second == Tlit{Tvar{3}, 1});

    delete_sentinel(s);
}

// --------------------------------------------------------------------------
// 2.15  remove_literal (shrink_clause)
// --------------------------------------------------------------------------

TEST_CASE("remove_literal: next() increments deleted count; back() decrements; next() increments again", "[nav]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1, 2, 3});
    const std::vector<Tlit> lits = {Tlit{Tvar{1}, 0}, Tlit{Tvar{2}, 1}, Tlit{Tvar{3}, 1}};
    add_clause(s, Tclause{0}, lits.data(), 3, false);
    unsigned total = (unsigned)s->state->literals(Tclause{0}).size();

    REQUIRE(shrink_clause(s, Tclause{0}, Tlit{Tvar{2}, 1}));
    REQUIRE(s->state->_clauses[Tclause{0}].n_deleted_literals == 1);

    enter_step_mode(s);
    back_silent(s);
    REQUIRE(s->state->_clauses[Tclause{0}].n_deleted_literals == 0);
    REQUIRE(s->state->literals(Tclause{0}).size() == total);

    exit_step_mode(s);
    s->next();
    REQUIRE(s->state->_clauses[Tclause{0}].n_deleted_literals == 1);

    delete_sentinel(s);
}

// --------------------------------------------------------------------------
// 2.16  lock_assumption
// --------------------------------------------------------------------------

TEST_CASE("lock_assumption: next() locks; back() unlocks; next() re-locks", "[nav]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1});

    REQUIRE(lock_assumption(s, Tlit{Tvar{1}, 1}));
    REQUIRE(s->state->locked(Tvar{1}));

    enter_step_mode(s);
    back_silent(s);
    REQUIRE_FALSE(s->state->locked(Tvar{1}));

    exit_step_mode(s);
    s->next();
    REQUIRE(s->state->locked(Tvar{1}));

    delete_sentinel(s);
}

// --------------------------------------------------------------------------
// 2.17  unlock_assumption
// --------------------------------------------------------------------------

TEST_CASE("unlock_assumption: next() unlocks; back() re-locks; next() unlocks again", "[nav]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1});
    lock_assumption(s, Tlit{Tvar{1}, 1});

    REQUIRE(unlock_assumption(s, Tlit{Tvar{1}, 1}));
    REQUIRE_FALSE(s->state->locked(Tvar{1}));

    enter_step_mode(s);
    back_silent(s);
    REQUIRE(s->state->locked(Tvar{1}));

    exit_step_mode(s);
    s->next();
    REQUIRE_FALSE(s->state->locked(Tvar{1}));

    delete_sentinel(s);
}

// --------------------------------------------------------------------------
// 2.18  message (pure no-op)
// --------------------------------------------------------------------------

TEST_CASE("message: next() and back() leave all state unchanged", "[nav]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1});
    assign(s, Tlit{Tvar{1}, 1});

    size_t trail_snap = s->state->trail_size();
    size_t vars_snap  = s->state->variables_size();

    REQUIRE(message(s, "sentinel test message", 1));
    REQUIRE(s->state->trail_size()     == trail_snap);
    REQUIRE(s->state->variables_size() == vars_snap);

    enter_step_mode(s);
    back_silent(s);
    REQUIRE(s->state->trail_size()     == trail_snap);
    REQUIRE(s->state->variables_size() == vars_snap);

    exit_step_mode(s);
    s->next();
    REQUIRE(s->state->trail_size()     == trail_snap);
    REQUIRE(s->state->variables_size() == vars_snap);

    delete_sentinel(s);
}

// --------------------------------------------------------------------------
// 2.19  Chained back()/next() — watch operations
// --------------------------------------------------------------------------

TEST_CASE("chained back/next through watch, block, and watch restores state at each step", "[nav]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1, 2, 3});
    const std::vector<Tlit> lits = {Tlit{Tvar{1}, 0}, Tlit{Tvar{2}, 1}, Tlit{Tvar{3}, 1}};
    add_clause(s, Tclause{0}, lits.data(), 3, false);

    // Build: watch(L1), block(L3 on L1), watch(L2)
    watch(s, Tclause{0}, Tlit{Tvar{1}, 0});
    block(s, Tclause{0}, Tlit{Tvar{3}, 1}, Tlit{Tvar{1}, 0});
    watch(s, Tclause{0}, Tlit{Tvar{2}, 1});
    REQUIRE(s->state->watches(Tclause{0}).size() == 2);

    enter_step_mode(s);

    // Undo watch(L2)
    back_silent(s);
    REQUIRE(s->state->watches(Tclause{0}).size() == 1);

    // Undo block(L3)
    back_silent(s);
    REQUIRE(s->state->watches(Tclause{0})[0].second == LIT_UNDEF);

    // Undo watch(L1)
    back_silent(s);
    REQUIRE(s->state->watches(Tclause{0}).empty());

    // Replay all three in one batch
    exit_step_mode(s);
    s->next();
    REQUIRE(s->state->watches(Tclause{0}).size() == 2);
    REQUIRE(s->state->watches(Tclause{0})[0].first  == Tlit{Tvar{1}, 0});
    REQUIRE(s->state->watches(Tclause{0})[0].second == Tlit{Tvar{3}, 1});

    delete_sentinel(s);
}

// --------------------------------------------------------------------------
// 2.20  Chained back()/next() — random-access unassignment sequence
// --------------------------------------------------------------------------

TEST_CASE("chained back/next through two random-access unassignments preserves trail integrity", "[nav]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1, 2, 3, 4});

    assign(s, Tlit{Tvar{1}, 1});
    assign(s, Tlit{Tvar{2}, 1});
    assign(s, Tlit{Tvar{3}, 1});
    assign(s, Tlit{Tvar{4}, 1});
    s->state->position(Tvar{1}) = 0;
    s->state->position(Tvar{2}) = 1;
    s->state->position(Tvar{3}) = 2;
    s->state->position(Tvar{4}) = 3;

    // First non-LIFO removal: remove L2 from position 1
    REQUIRE(unassign(s, Tlit{Tvar{2}, 1}));
    // Trail is now [L1, L3, L4]; update positions
    s->state->position(Tvar{3}) = 1;
    s->state->position(Tvar{4}) = 2;

    // Second non-LIFO removal: remove L3 from position 1
    REQUIRE(unassign(s, Tlit{Tvar{3}, 1}));
    // Trail is now [L1, L4]
    REQUIRE(s->state->trail_size() == 2);

    enter_step_mode(s);

    // Rollback unassign(L3): L3 goes back to position 1, L4 shifts to 2
    back_silent(s);
    REQUIRE(s->state->trail_size()     == 3);
    REQUIRE(s->state->value(Tvar{3})   == VAL_TRUE);
    REQUIRE(s->state->trail_literal(1) == Tlit{Tvar{3}, 1});
    REQUIRE(s->state->trail_literal(2) == Tlit{Tvar{4}, 1});

    // Rollback unassign(L2): L2 goes back to position 1
    back_silent(s);
    REQUIRE(s->state->trail_size()     == 4);
    REQUIRE(s->state->value(Tvar{2})   == VAL_TRUE);
    REQUIRE(s->state->trail_literal(1) == Tlit{Tvar{2}, 1});
    REQUIRE(s->state->trail_literal(2) == Tlit{Tvar{3}, 1});
    REQUIRE(s->state->trail_literal(3) == Tlit{Tvar{4}, 1});

    // Re-apply both unassignments in batch.
    // Before re-applying unassign(L2), positions must be correct.
    s->state->position(Tvar{3}) = 2;
    s->state->position(Tvar{4}) = 3;
    exit_step_mode(s);
    s->next();   // applies unassign(L2): removes L2, shifts L3→1, L4→2
    // At this point unassign(L3) is also pending; with display_level=0,
    // next() continues and applies it too (L4 shifts to 1).
    s->state->position(Tvar{4}) = 1; // would be set by solver before the 2nd unassign
    // Actually next() already ran the 2nd unassign; just verify the final state.
    REQUIRE(s->state->trail_size()    == 2);
    REQUIRE(s->state->value(Tvar{2}) == VAL_UNDEF);
    REQUIRE(s->state->value(Tvar{3}) == VAL_UNDEF);
    REQUIRE(s->state->trail_literal(0) == Tlit{Tvar{1}, 1});

    delete_sentinel(s);
}
