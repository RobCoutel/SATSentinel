/*
 * This file is part of the source code of the software program
 * SATSentinel. It is protected by applicable copyright laws.
 *
 * This source code is protected by the terms of the MIT License.
 */
/**
 * @file tests/Sentinel-notification.test.cpp
 * @brief Unit tests for notification apply()/rollback() correctness under repeated
 * forward/backward navigation.
 * @author mostly Claude and Robin Coutelier
 *
 * Sentinel-API.test.cpp already checks that next()/back() touch the *expected* fields
 * for one notification at a time. This file goes further:
 *
 *  - PART 1 tests each notification type in isolation, directly against a bare
 *    SentinelState, for the "involution" property: apply() immediately followed by
 *    rollback() must restore the exact prior state (not just the couple of fields a
 *    given API caller happens to care about).
 *
 *  - PART 2 reproduces the same bugs through the real SATSentinel::next()/back()
 *    navigation engine, showing their user-visible effect (e.g. decision levels
 *    inflating every time the user steps back and forward over a decision).
 *
 *  - PART 3 builds longer notification chains combining many types and checks
 *    "path independence": the state reached at index i of the replay must be
 *    identical no matter which sequence of next()/back() calls was used to get
 *    there. This is the property an interactive step-debugger fundamentally
 *    relies on.
 *
 *  - PART 4 stress-tests specific notification families (watch/block/unwatch,
 *    remove_literal, non-LIFO unassignment) with repeated back-and-forth cycles.
 *
 * Tests whose name starts with "KNOWN BUG" are expected to currently FAIL: they
 * pin down real defects in Sentinel-notifications.cpp rather than paper over them.
 */

#include <catch2/catch_all.hpp>

#include <sstream>
#include <string>
#include <vector>

#include "Sentinel-API.hpp"
#include "Sentinel-state.hpp"
#include "Sentinel-notifications.hpp"
#include "Sentinel-options.hpp"

// Expose private members so tests can inspect internal state and call
// next() / back() directly on the real navigation engine.
#define private public
#include "SATSentinel.hpp"
#undef private

using namespace sentinel;

namespace {

// NOTE: no local SuppressCerr listener here. Catch2 listeners are registered
// once per *binary*, not per translation unit — Sentinel-API.test.cpp already
// registers one that redirects std::cerr for every test case in this
// executable. A second, independent listener doing the same save/restore
// dance used to live here; with two listeners interleaving their
// testCaseStarting/testCaseEnded calls, one would "save" the other's sink
// buffer instead of the real original streambuf, leaving std::cerr pointing
// into a SuppressCerr object that gets freed when that listener is torn down.
// The dangling access only surfaces in std::ios_base::Init::~Init() at real
// process exit (after Catch2 already reported "All tests passed"), which is
// exactly the "tests crash after success" symptom valgrind caught.

// ==========================================================================
// Snapshot infrastructure — full-state equality, not just a handful of fields.
// ==========================================================================

struct StateSnapshot {
    std::vector<SentinelState::variable> variables;
    std::vector<SentinelState::clause>   clauses;
    std::vector<Tlit>                    trail;
    std::vector<unsigned>                level_counters;
};

StateSnapshot snapshot(const SentinelState* state)
{
    StateSnapshot snap;
    snap.variables = state->_variables;
    snap.clauses.assign(state->_clauses.begin(), state->_clauses.end());
    snap.trail = state->_trail;
    snap.level_counters = state->_level_counters;
    return snap;
}

// Compares everything an accessor can observe. `.var` is intentionally
// excluded: it is write-only bookkeeping set by new_variable::apply() and
// never read back anywhere in the codebase (grep confirms the only write is
// Sentinel-notifications.cpp:76), so a stale value there has no observable
// effect and is not a correctness bug.
bool variables_equal(const std::vector<SentinelState::variable>& a,
                      const std::vector<SentinelState::variable>& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) {
        if (a[i].value      != b[i].value)      return false;
        if (a[i].level      != b[i].level)      return false;
        if (a[i].reason     != b[i].reason)     return false;
        if (a[i].active     != b[i].active)     return false;
        if (a[i].propagated != b[i].propagated) return false;
        if (a[i].position   != b[i].position)   return false;
        if (a[i].locked     != b[i].locked)     return false;
        if (a[i].alias      != b[i].alias)      return false;
    }
    return true;
}

bool clauses_equal(const std::vector<SentinelState::clause>& a,
                    const std::vector<SentinelState::clause>& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) {
        if (a[i].literals           != b[i].literals)           return false;
        if (a[i].n_deleted_literals != b[i].n_deleted_literals) return false;
        if (a[i].watches            != b[i].watches)            return false;
        if (a[i].active             != b[i].active)             return false;
        if (a[i].learnt              != b[i].learnt)             return false;
        if (a[i].external           != b[i].external)           return false;
    }
    return true;
}

bool snapshots_equal(const StateSnapshot& a, const StateSnapshot& b)
{
    return variables_equal(a.variables, b.variables)
        && clauses_equal(a.clauses, b.clauses)
        && a.trail == b.trail
        && a.level_counters == b.level_counters;
}

// Cross-checks _level_counters against the actual per-variable levels. This is
// a property the "level bookkeeping" notifications (assignment, unassignment,
// update_level) must all preserve for `state->level()` / the trail display to
// mean anything.
bool level_counters_consistent(const SentinelState* state)
{
    std::vector<unsigned> hist(state->_level_counters.size(), 0);
    for (const auto& v : state->_variables) {
        if (!v.active || v.value == VAL_UNDEF) continue;
        if (v.level.value >= hist.size()) return false;
        hist[v.level.value]++;
    }
    return hist == state->_level_counters;
}

// --------------------------------------------------------------------------
// SATSentinel-level helpers (mirrors Sentinel-API.test.cpp's conventions).
// --------------------------------------------------------------------------

SATSentinel* make_sentinel(SentinelOptions* opts = new SentinelOptions{})
{
    opts->check_only = true;
    return create_sentinel(*opts);
}

void add_vars(SATSentinel* s, std::initializer_list<unsigned> ids)
{
    for (unsigned id : ids)
        REQUIRE(add_variable(s, Tvar{id}));
}

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

// back() unconditionally calls get_external_commands(), which prints a notice
// to stdout when no external parser is installed. Swallow that noise.
void back_silent(SATSentinel* s)
{
    std::ostringstream sink;
    auto* old = std::cout.rdbuf(sink.rdbuf());
    s->back();
    std::cout.rdbuf(old);
}

// next() is a no-op on stdio as long as check_only == true (set by make_sentinel).
void next_silent(SATSentinel* s) { s->next(); }

void enter_step_mode(SATSentinel* s) { s->display_level = 100; }

// Walks the navigation engine, one notification at a time, until
// current_notification_index == target. Works regardless of the current
// position (goes backward or forward as needed).
void goto_index(SATSentinel* s, size_t target)
{
    enter_step_mode(s);
    while (s->current_notification_index > target) back_silent(s);
    while (s->current_notification_index < target) next_silent(s);
}

StateSnapshot snapshot(SATSentinel* s) { return snapshot(s->state); }

} // namespace

// ==========================================================================
// PART 1 — Per-notification-type involution: apply(); rollback(); == identity
// ==========================================================================
//
// Each test builds whatever prerequisite state a notification needs (added
// directly against a bare SentinelState, not through SATSentinel), snapshots
// right before the notification under test, applies it, rolls it back, and
// requires the state to be pixel-identical to the snapshot.
//
// Tests named "KNOWN BUG" are expected to fail on the current implementation;
// they exist to pin down exactly which notification's rollback is broken.
// --------------------------------------------------------------------------

TEST_CASE("involution: new_variable", "[notif][involution]")
{
    SentinelState state(nullptr);
    // Pre-grow the _variables vector via an unrelated, permanent variable so the
    // vector doesn't need to resize when we add the variable under test — resizing
    // is a one-way, allocate-only side effect by design, not something rollback is
    // expected to undo.
    notif::new_variable dummy(Tvar{10});
    REQUIRE(dummy.apply(&state));

    notif::new_variable nv(Tvar{3});
    StateSnapshot before = snapshot(&state);
    REQUIRE(nv.apply(&state));
    REQUIRE(state.active(Tvar{3}));
    REQUIRE(nv.rollback(&state));
    StateSnapshot after = snapshot(&state);
    REQUIRE(snapshots_equal(before, after));
}

TEST_CASE("involution: assignment (decision) restores decision level", "[notif][involution]")
{
    SentinelState state(nullptr);
    notif::new_variable v1(Tvar{1});
    REQUIRE(v1.apply(&state));

    notif::assignment a(Tlit{Tvar{1}, 1}, CLAUSE_UNDEF);
    StateSnapshot before = snapshot(&state);
    REQUIRE(a.apply(&state));
    REQUIRE(a.rollback(&state));
    StateSnapshot after = snapshot(&state);

    // value/reason/trail membership ARE correctly restored...
    REQUIRE(state.value(Tvar{1})  == VAL_UNDEF);
    REQUIRE(state.reason(Tvar{1}) == CLAUSE_UNDEF);
    REQUIRE(state.trail_size()    == 0);

    // ...but assignment::rollback() never touches level(var) or _level_counters,
    // so both are left stale from apply(). This documents the defect precisely:
    REQUIRE(state.level(Tvar{1}) == LEVEL_UNDEF);          // FAILS: still Tlevel{1}
    REQUIRE(state._level_counters.size() == 1);            // FAILS: still size 2
    REQUIRE(snapshots_equal(before, after));                // aggregate check
}

TEST_CASE("KNOWN BUG: involution: assignment (implied) does not restore decision level", "[notif][involution][bug]")
{
    SentinelState state(nullptr);
    notif::new_variable v1(Tvar{1});
    notif::new_variable v2(Tvar{2});
    REQUIRE(v1.apply(&state));
    REQUIRE(v2.apply(&state));
    notif::new_clause c0(Tclause{0}, {Tlit{Tvar{1}, 0}, Tlit{Tvar{2}, 1}}, true);
    REQUIRE(c0.apply(&state));

    notif::assignment dec(Tlit{Tvar{1}, 1}, CLAUSE_UNDEF);
    REQUIRE(dec.apply(&state));   // prerequisite decision, level 1, not measured

    notif::assignment impl(Tlit{Tvar{2}, 1}, Tclause{0});
    StateSnapshot before = snapshot(&state);
    REQUIRE(impl.apply(&state));
    REQUIRE(state.level(Tvar{2}) == Tlevel{1});
    REQUIRE(impl.rollback(&state));
    StateSnapshot after = snapshot(&state);

    REQUIRE(state.value(Tvar{2}) == VAL_UNDEF);
    REQUIRE(state.level(Tvar{2}) == LEVEL_UNDEF);           // FAILS: still Tlevel{1}
    REQUIRE(snapshots_equal(before, after));
}

TEST_CASE("involution: unassignment (LIFO, top of trail)", "[notif][involution]")
{
    SentinelState state(nullptr);
    notif::new_variable v1(Tvar{1});
    REQUIRE(v1.apply(&state));
    notif::assignment a(Tlit{Tvar{1}, 1}, CLAUSE_UNDEF);
    REQUIRE(a.apply(&state));
    state.position(Tvar{1}) = 0;

    notif::unassignment u(Tlit{Tvar{1}, 1});
    StateSnapshot before = snapshot(&state);
    REQUIRE(u.apply(&state));
    REQUIRE(state.trail_size() == 0);
    REQUIRE(u.rollback(&state));
    StateSnapshot after = snapshot(&state);
    REQUIRE(snapshots_equal(before, after));
}

TEST_CASE("involution: unassignment (non-LIFO, middle of trail, empties a non-top level)", "[notif][involution]")
{
    SentinelState state(nullptr);
    for (unsigned id : {1u, 2u, 3u}) {
        notif::new_variable v(Tvar{id});
        REQUIRE(v.apply(&state));
    }
    // Three decisions -> three distinct singleton levels, so removing the
    // middle one drives its level-counter entry to zero without it being the
    // top level (exercising decrement_level_counter()'s "hole in the middle"
    // path, not just the trailing-zero-pop path).
    notif::assignment a1(Tlit{Tvar{1}, 1}, CLAUSE_UNDEF);
    notif::assignment a2(Tlit{Tvar{2}, 1}, CLAUSE_UNDEF);
    notif::assignment a3(Tlit{Tvar{3}, 1}, CLAUSE_UNDEF);
    REQUIRE(a1.apply(&state));
    REQUIRE(a2.apply(&state));
    REQUIRE(a3.apply(&state));
    state.position(Tvar{1}) = 0;
    state.position(Tvar{2}) = 1;
    state.position(Tvar{3}) = 2;
    REQUIRE(state._level_counters == std::vector<unsigned>{0, 1, 1, 1});

    notif::unassignment u(Tlit{Tvar{2}, 1});
    StateSnapshot before = snapshot(&state);
    REQUIRE(u.apply(&state));
    REQUIRE(state.trail_size() == 2);
    REQUIRE(state.position(Tvar{3}) == 1);
    REQUIRE(u.rollback(&state));
    StateSnapshot after = snapshot(&state);

    REQUIRE(state.trail_literal(1) == Tlit{Tvar{2}, 1});
    REQUIRE(state.position(Tvar{3}) == 2);
    REQUIRE(snapshots_equal(before, after));
}

TEST_CASE("involution: propagation", "[notif][involution]")
{
    SentinelState state(nullptr);
    notif::new_variable v1(Tvar{1});
    REQUIRE(v1.apply(&state));
    notif::assignment a(Tlit{Tvar{1}, 1}, CLAUSE_UNDEF);
    REQUIRE(a.apply(&state));

    notif::propagation p(Tlit{Tvar{1}, 1});
    StateSnapshot before = snapshot(&state);
    REQUIRE(p.apply(&state));
    REQUIRE(state.propagated(Tvar{1}));
    REQUIRE(p.rollback(&state));
    StateSnapshot after = snapshot(&state);
    REQUIRE(snapshots_equal(before, after));
}

TEST_CASE("involution: propagation_removed", "[notif][involution]")
{
    SentinelState state(nullptr);
    notif::new_variable v1(Tvar{1});
    REQUIRE(v1.apply(&state));
    notif::assignment a(Tlit{Tvar{1}, 1}, CLAUSE_UNDEF);
    REQUIRE(a.apply(&state));
    notif::propagation p(Tlit{Tvar{1}, 1});
    REQUIRE(p.apply(&state));

    notif::propagation_removed pr(Tlit{Tvar{1}, 1});
    StateSnapshot before = snapshot(&state);
    REQUIRE(pr.apply(&state));
    REQUIRE_FALSE(state.propagated(Tvar{1}));
    REQUIRE(pr.rollback(&state));
    StateSnapshot after = snapshot(&state);
    REQUIRE(snapshots_equal(before, after));
}

TEST_CASE("involution: update_level (moving to an already-existing lower level)", "[notif][involution]")
{
    SentinelState state(nullptr);
    notif::new_variable v1(Tvar{1});
    notif::new_variable v2(Tvar{2});
    REQUIRE(v1.apply(&state));
    REQUIRE(v2.apply(&state));
    notif::assignment a1(Tlit{Tvar{1}, 1}, CLAUSE_UNDEF);  // level 1
    notif::assignment a2(Tlit{Tvar{2}, 1}, CLAUSE_UNDEF);  // level 2
    REQUIRE(a1.apply(&state));
    REQUIRE(a2.apply(&state));

    notif::update_level ul(Tlit{Tvar{1}, 1}, Tlevel{0});
    StateSnapshot before = snapshot(&state);
    REQUIRE(ul.apply(&state));
    REQUIRE(state.level(Tvar{1}) == Tlevel{0});
    REQUIRE(ul.rollback(&state));
    StateSnapshot after = snapshot(&state);
    REQUIRE(state.level(Tvar{1}) == Tlevel{1});
    REQUIRE(snapshots_equal(before, after));
}

TEST_CASE("update_level::apply() credits the destination level's counter", "[notif]")
{
    // update_level::apply() must both debit the source level and credit the
    // destination level in _level_counters, or state->level()/later decrements
    // silently operate on stale data. Guards against a regression of a defect
    // that used to exist here (apply() only ever decremented the old level).
    SentinelState state(nullptr);
    notif::new_variable v1(Tvar{1});
    notif::new_variable v2(Tvar{2});
    REQUIRE(v1.apply(&state));
    REQUIRE(v2.apply(&state));
    notif::assignment a1(Tlit{Tvar{1}, 1}, CLAUSE_UNDEF);  // level 1
    notif::assignment a2(Tlit{Tvar{2}, 1}, CLAUSE_UNDEF);  // level 2
    REQUIRE(a1.apply(&state));
    REQUIRE(a2.apply(&state));
    REQUIRE(level_counters_consistent(&state));

    notif::update_level ul(Tlit{Tvar{1}, 1}, Tlevel{0});   // move v1 down to level 0
    REQUIRE(ul.apply(&state));
    REQUIRE(level_counters_consistent(&state));

    REQUIRE(ul.rollback(&state));
    REQUIRE(level_counters_consistent(&state));
    REQUIRE(state.level(Tvar{1}) == Tlevel{1});
}

TEST_CASE("involution: update_reason", "[notif][involution]")
{
    SentinelState state(nullptr);
    notif::new_variable v1(Tvar{1});
    notif::new_variable v2(Tvar{2});
    REQUIRE(v1.apply(&state));
    REQUIRE(v2.apply(&state));
    notif::new_clause c0(Tclause{0}, {Tlit{Tvar{1}, 0}, Tlit{Tvar{2}, 1}}, false);
    REQUIRE(c0.apply(&state));
    notif::assignment a(Tlit{Tvar{1}, 1}, CLAUSE_UNDEF);
    REQUIRE(a.apply(&state));

    notif::update_reason ur(Tlit{Tvar{1}, 1}, Tclause{0});
    StateSnapshot before = snapshot(&state);
    REQUIRE(ur.apply(&state));
    REQUIRE(state.reason(Tvar{1}) == Tclause{0});
    REQUIRE(ur.rollback(&state));
    StateSnapshot after = snapshot(&state);
    REQUIRE(snapshots_equal(before, after));
}

TEST_CASE("involution: new_clause::rollback() leaks literals/external after deactivation", "[notif][involution]")
{
    SentinelState state(nullptr);
    notif::new_variable v1(Tvar{1});
    notif::new_variable v2(Tvar{2});
    notif::new_variable v3(Tvar{3});
    REQUIRE(v1.apply(&state));
    REQUIRE(v2.apply(&state));
    REQUIRE(v3.apply(&state));
    // Pre-grow _clauses via a permanent higher-id clause so the vector doesn't
    // need to resize for the clause under test.
    notif::new_clause dummy(Tclause{10}, {Tlit{Tvar{1}, 1}}, false);
    REQUIRE(dummy.apply(&state));

    notif::new_clause nc(Tclause{0},
                          {Tlit{Tvar{1}, 0}, Tlit{Tvar{2}, 1}, Tlit{Tvar{3}, 1}},
                          /*external=*/true);
    StateSnapshot before = snapshot(&state);
    REQUIRE(nc.apply(&state));
    REQUIRE(state.active(Tclause{0}));
    REQUIRE(nc.rollback(&state));
    StateSnapshot after = snapshot(&state);

    REQUIRE_FALSE(state.active(Tclause{0}));               // correctly deactivated
    REQUIRE(state.literals(Tclause{0}).empty());            // FAILS: still holds 3 literals
    REQUIRE_FALSE(state.clause_external(Tclause{0}));       // FAILS: still true
    REQUIRE(snapshots_equal(before, after));
}

TEST_CASE("involution: delete_clause fully restores the clause struct", "[notif][involution]")
{
    SentinelState state(nullptr);
    notif::new_variable v1(Tvar{1});
    notif::new_variable v2(Tvar{2});
    REQUIRE(v1.apply(&state));
    REQUIRE(v2.apply(&state));
    notif::new_clause c0(Tclause{0}, {Tlit{Tvar{1}, 0}, Tlit{Tvar{2}, 1}}, true);
    REQUIRE(c0.apply(&state));

    notif::delete_clause dc(Tclause{0});
    StateSnapshot before = snapshot(&state);
    REQUIRE(dc.apply(&state));
    REQUIRE_FALSE(state.active(Tclause{0}));
    REQUIRE(dc.rollback(&state));
    StateSnapshot after = snapshot(&state);

    REQUIRE(state.active(Tclause{0}));
    REQUIRE(state.literals(Tclause{0}).size() == 2);
    REQUIRE(snapshots_equal(before, after));
}

TEST_CASE("involution: watch", "[notif][involution]")
{
    SentinelState state(nullptr);
    notif::new_variable v1(Tvar{1});
    notif::new_variable v2(Tvar{2});
    REQUIRE(v1.apply(&state));
    REQUIRE(v2.apply(&state));
    notif::new_clause c0(Tclause{0}, {Tlit{Tvar{1}, 0}, Tlit{Tvar{2}, 1}}, false);
    REQUIRE(c0.apply(&state));

    notif::watch w(Tclause{0}, Tlit{Tvar{1}, 0});
    StateSnapshot before = snapshot(&state);
    REQUIRE(w.apply(&state));
    REQUIRE(state.watches(Tclause{0}).size() == 1);
    REQUIRE(w.rollback(&state));
    StateSnapshot after = snapshot(&state);
    REQUIRE(snapshots_equal(before, after));
}

TEST_CASE("involution: watch of the second literal (content-addressed removal)", "[notif][involution]")
{
    SentinelState state(nullptr);
    notif::new_variable v1(Tvar{1});
    notif::new_variable v2(Tvar{2});
    REQUIRE(v1.apply(&state));
    REQUIRE(v2.apply(&state));
    notif::new_clause c0(Tclause{0}, {Tlit{Tvar{1}, 0}, Tlit{Tvar{2}, 1}}, false);
    REQUIRE(c0.apply(&state));
    notif::watch w1(Tclause{0}, Tlit{Tvar{1}, 0});
    REQUIRE(w1.apply(&state));

    notif::watch w2(Tclause{0}, Tlit{Tvar{2}, 1});
    StateSnapshot before = snapshot(&state);
    REQUIRE(w2.apply(&state));
    REQUIRE(state.watches(Tclause{0}).size() == 2);
    REQUIRE(w2.rollback(&state));
    StateSnapshot after = snapshot(&state);
    REQUIRE(state.watches(Tclause{0}).size() == 1);
    REQUIRE(state.watches(Tclause{0})[0].first == Tlit{Tvar{1}, 0});
    REQUIRE(snapshots_equal(before, after));
}

TEST_CASE("involution: unwatch restores the blocker", "[notif][involution]")
{
    SentinelState state(nullptr);
    notif::new_variable v1(Tvar{1});
    notif::new_variable v2(Tvar{2});
    notif::new_variable v3(Tvar{3});
    REQUIRE(v1.apply(&state));
    REQUIRE(v2.apply(&state));
    REQUIRE(v3.apply(&state));
    notif::new_clause c0(Tclause{0}, {Tlit{Tvar{1}, 0}, Tlit{Tvar{2}, 1}, Tlit{Tvar{3}, 1}}, false);
    REQUIRE(c0.apply(&state));
    notif::watch w(Tclause{0}, Tlit{Tvar{1}, 0});
    REQUIRE(w.apply(&state));
    notif::block bl(Tclause{0}, Tlit{Tvar{3}, 1}, Tlit{Tvar{1}, 0});
    REQUIRE(bl.apply(&state));

    notif::unwatch uw(Tclause{0}, Tlit{Tvar{1}, 0});
    StateSnapshot before = snapshot(&state);
    REQUIRE(uw.apply(&state));
    REQUIRE(state.watches(Tclause{0}).empty());
    REQUIRE(uw.rollback(&state));
    StateSnapshot after = snapshot(&state);

    REQUIRE(state.watches(Tclause{0})[0].second == Tlit{Tvar{3}, 1});
    REQUIRE(snapshots_equal(before, after));
}

TEST_CASE("KNOWN BUG: involution: unwatch::rollback() loses slot order when the removed watch was the front one", "[notif][involution][bug]")
{
    // unwatch::apply() removes whichever entry matches `lit` (front or back);
    // unwatch::rollback() unconditionally push_back()s it, so if the removed
    // watch was at the front, rollback silently swaps watches[0] and
    // watches[1]. This matters: check_watched_literals()'s weak/strong
    // invariants are asymmetric between watches[0] (which carries the
    // blocker) and watches[1], so a slot swap changes what gets checked.
    SentinelState state(nullptr);
    for (unsigned id : {1u, 2u, 3u}) {
      notif::new_variable v(Tvar{id});
      REQUIRE(v.apply(&state));
    }
    Tvar v1 = Tvar{1}, v2 = Tvar{2}, v3 = Tvar{3};
    Tlit l1{v1, 0}, l2{v2, 1}, l3{v3, 1};
    Tclause cl = Tclause{0};

    notif::new_clause c0(cl, {l1, l2, l3}, false);

    REQUIRE(c0.apply(&state));
    notif::watch w1(cl, l1);   // becomes slot 0 (front)
    notif::watch w2(cl, l2);   // becomes slot 1 (back)
    REQUIRE(w1.apply(&state));
    REQUIRE(w2.apply(&state));
    REQUIRE(state.watches(cl)[0].first == l1);
    REQUIRE(state.watches(cl)[1].first == l2);

    notif::unwatch uw(cl, l1);   // removes the FRONT watch
    StateSnapshot before = snapshot(&state);
    REQUIRE(uw.apply(&state));
    REQUIRE(state.watches(cl).size() == 1);
    REQUIRE(uw.rollback(&state));
    StateSnapshot after = snapshot(&state);

    // Content is restored, but not in its original slot:
    REQUIRE(state.watches(cl).size() == 2);
    Tlit watch1 = state.watches(cl)[0].first;
    REQUIRE(watch1 == l1);
    REQUIRE(snapshots_equal(before, after));                          // FAILS: order differs
}

TEST_CASE("involution: block restores the previous blocker", "[notif][involution]")
{
    SentinelState state(nullptr);
    for (unsigned id : {1u, 2u, 3u, 4u}) {
        notif::new_variable v(Tvar{id});
        REQUIRE(v.apply(&state));
    }
    notif::new_clause c0(Tclause{0},
        {Tlit{Tvar{1}, 0}, Tlit{Tvar{2}, 1}, Tlit{Tvar{3}, 1}, Tlit{Tvar{4}, 1}}, false);
    REQUIRE(c0.apply(&state));
    notif::watch w1(Tclause{0}, Tlit{Tvar{1}, 0});
    notif::watch w2(Tclause{0}, Tlit{Tvar{2}, 1});
    REQUIRE(w1.apply(&state));
    REQUIRE(w2.apply(&state));
    notif::block b1(Tclause{0}, Tlit{Tvar{2}, 1}, Tlit{Tvar{1}, 0});
    REQUIRE(b1.apply(&state));

    notif::block b2(Tclause{0}, Tlit{Tvar{3}, 1}, Tlit{Tvar{1}, 0});
    StateSnapshot before = snapshot(&state);
    REQUIRE(b2.apply(&state));
    REQUIRE(state.watches(Tclause{0})[0].second == Tlit{Tvar{3}, 1});
    REQUIRE(b2.rollback(&state));
    StateSnapshot after = snapshot(&state);
    REQUIRE(state.watches(Tclause{0})[0].second == Tlit{Tvar{2}, 1});
    REQUIRE(snapshots_equal(before, after));
}

TEST_CASE("involution: remove_literal (single shrink)", "[notif][involution]")
{
    SentinelState state(nullptr);
    for (unsigned id : {1u, 2u, 3u}) {
        notif::new_variable v(Tvar{id});
        REQUIRE(v.apply(&state));
    }
    notif::new_clause c0(Tclause{0}, {Tlit{Tvar{1}, 0}, Tlit{Tvar{2}, 1}, Tlit{Tvar{3}, 1}}, false);
    REQUIRE(c0.apply(&state));

    notif::remove_literal rl(Tclause{0}, Tlit{Tvar{2}, 1});
    StateSnapshot before = snapshot(&state);
    REQUIRE(rl.apply(&state));
    REQUIRE(state._clauses[Tclause{0}].n_deleted_literals == 1);
    REQUIRE(rl.rollback(&state));
    StateSnapshot after = snapshot(&state);
    REQUIRE(snapshots_equal(before, after));
}

TEST_CASE("involution: remove_literal (two shrinks, undone out of order matched by content)", "[notif][involution]")
{
    SentinelState state(nullptr);
    for (unsigned id : {1u, 2u, 3u, 4u}) {
        notif::new_variable v(Tvar{id});
        REQUIRE(v.apply(&state));
    }
    notif::new_clause c0(Tclause{0},
        {Tlit{Tvar{1}, 0}, Tlit{Tvar{2}, 1}, Tlit{Tvar{3}, 1}, Tlit{Tvar{4}, 1}}, false);
    REQUIRE(c0.apply(&state));

    StateSnapshot origin = snapshot(&state);

    notif::remove_literal rl1(Tclause{0}, Tlit{Tvar{2}, 1});
    notif::remove_literal rl2(Tclause{0}, Tlit{Tvar{4}, 1});
    REQUIRE(rl1.apply(&state));
    REQUIRE(rl2.apply(&state));
    REQUIRE(state._clauses[Tclause{0}].n_deleted_literals == 2);

    // Undo strictly in LIFO order (as SATSentinel::back() would).
    REQUIRE(rl2.rollback(&state));
    REQUIRE(rl1.rollback(&state));
    StateSnapshot after = snapshot(&state);
    REQUIRE(snapshots_equal(origin, after));
}

TEST_CASE("involution: lock_assumption", "[notif][involution]")
{
    SentinelState state(nullptr);
    notif::new_variable v1(Tvar{1});
    REQUIRE(v1.apply(&state));

    notif::lock_assumption la(Tlit{Tvar{1}, 1});
    StateSnapshot before = snapshot(&state);
    REQUIRE(la.apply(&state));
    REQUIRE(state.locked(Tvar{1}));
    REQUIRE(la.rollback(&state));
    StateSnapshot after = snapshot(&state);
    REQUIRE(snapshots_equal(before, after));
}

TEST_CASE("involution: unlock_assumption", "[notif][involution]")
{
    SentinelState state(nullptr);
    notif::new_variable v1(Tvar{1});
    REQUIRE(v1.apply(&state));
    notif::lock_assumption la(Tlit{Tvar{1}, 1});
    REQUIRE(la.apply(&state));

    notif::unlock_assumption ua(Tlit{Tvar{1}, 1});
    StateSnapshot before = snapshot(&state);
    REQUIRE(ua.apply(&state));
    REQUIRE_FALSE(state.locked(Tvar{1}));
    REQUIRE(ua.rollback(&state));
    StateSnapshot after = snapshot(&state);
    REQUIRE(snapshots_equal(before, after));
}

TEST_CASE("involution: message is a strict no-op", "[notif][involution]")
{
    SentinelState state(nullptr);
    notif::new_variable v1(Tvar{1});
    REQUIRE(v1.apply(&state));
    StateSnapshot before = snapshot(&state);

    notif::message m("hello", 3);
    REQUIRE(m.apply(&state));
    REQUIRE(m.rollback(&state));
    StateSnapshot after = snapshot(&state);
    REQUIRE(snapshots_equal(before, after));
}

// ==========================================================================
// PART 2 — Same bugs, reproduced through the real SATSentinel navigation
// engine (next()/back()), to show their concrete, user-visible effect.
// ==========================================================================

TEST_CASE("stepping back then forward over a decision inflates its level", "[nav]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1});
    REQUIRE(assign(s, Tlit{Tvar{1}, 1}));
    REQUIRE(s->state->level(Tvar{1}) == Tlevel{1});

    enter_step_mode(s);
    back_silent(s);                       // rollback the decision
    REQUIRE(s->state->value(Tvar{1}) == VAL_UNDEF);

    next_silent(s);                       // re-apply the SAME decision
    REQUIRE(s->state->value(Tvar{1}) == VAL_TRUE);
    // A user stepping back and forward once over an unrelated decision should
    // see the exact same level they started with. Instead it silently grew:
    REQUIRE(s->state->level(Tvar{1}) == Tlevel{1});   // FAILS: actually Tlevel{2}
}

TEST_CASE("KNOWN BUG: repeated back/forward cycling makes the decision level grow without bound", "[nav][bug]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1});
    REQUIRE(assign(s, Tlit{Tvar{1}, 1}));
    enter_step_mode(s);

    for (int cycle = 0; cycle < 4; cycle++) {
        back_silent(s);
        next_silent(s);
    }
    // No solver operation ever asked for this literal's level to change; four
    // no-op round trips through the navigator should be indistinguishable from
    // zero round trips.
    REQUIRE(s->state->level(Tvar{1}) == Tlevel{1});   // FAILS: actually Tlevel{5}
    // The corruption is silent — the sentinel never flags a failure:
    REQUIRE_FALSE(s->failed);
}

TEST_CASE("KNOWN BUG: rolling back a clause addition leaves its literals and external flag behind", "[nav][bug]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1, 2, 3});
    const std::vector<Tlit> lits = {Tlit{Tvar{1}, 0}, Tlit{Tvar{2}, 1}, Tlit{Tvar{3}, 1}};
    REQUIRE(add_clause(s, Tclause{0}, lits.data(), 3, /*external=*/true));

    enter_step_mode(s);
    back_silent(s);

    REQUIRE_FALSE(s->state->active(Tclause{0}));
    REQUIRE(s->state->literals(Tclause{0}).empty());          // FAILS: still 3 literals
    REQUIRE_FALSE(s->state->clause_external(Tclause{0}));     // FAILS: still true
}

// ==========================================================================
// PART 3 — Path independence: the state at index i must not depend on which
// sequence of next()/back() calls was used to reach it.
// ==========================================================================

TEST_CASE("path independence holds for a scenario that never crosses a buggy notification's rollback", "[nav][path-independence]")
{
    // Deliberately excludes `assignment` and `new_clause` rollback from the
    // navigated range (clauses/decisions are set up once and never stepped
    // back past) so this test isolates and confirms the *other* fourteen
    // notification types compose correctly under arbitrary back-and-forth.
    SATSentinel* s = make_sentinel();
    add_vars(s, {1, 2, 3, 4});
    mk_watched_clause(s, 0, {Tlit{Tvar{1}, 0}, Tlit{Tvar{2}, 1}, Tlit{Tvar{3}, 1}}, true);
    mk_watched_clause(s, 1, {Tlit{Tvar{2}, 0}, Tlit{Tvar{3}, 0}, Tlit{Tvar{4}, 1}}, true);

    assign(s, Tlit{Tvar{1}, 1});
    size_t floor_index = s->notifications.size();   // never navigate before this point

    propagate(s, Tlit{Tvar{1}, 1});
    block(s, Tclause{0}, Tlit{Tvar{3}, 1}, Tlit{Tvar{2}, 1});
    update_reason(s, Tlit{Tvar{1}, 1}, CLAUSE_UNDEF);
    lock_assumption(s, Tlit{Tvar{2}, 1});
    shrink_clause(s, Tclause{1}, Tlit{Tvar{4}, 1});
    unlock_assumption(s, Tlit{Tvar{2}, 1});
    unwatch(s, Tclause{0}, Tlit{Tvar{2}, 1});
    watch(s, Tclause{0}, Tlit{Tvar{2}, 1});
    unpropagate(s, Tlit{Tvar{1}, 1});

    REQUIRE_FALSE(s->failed);
    size_t N = s->notifications.size();

    // Ground truth: snapshot every index by walking forward once from the floor.
    goto_index(s, floor_index);
    std::vector<StateSnapshot> truth;
    truth.push_back(snapshot(s));
    for (size_t i = floor_index; i < N; i++) {
        next_silent(s);
        truth.push_back(snapshot(s));
    }
    REQUIRE(s->current_notification_index == N);

    // Zig-zag across the range, re-checking against ground truth every time we
    // land on an index — including revisiting the same index from both
    // directions (a pure "back" landing vs. a pure "forward" landing).
    std::vector<size_t> path = {
        N, floor_index, N, (floor_index + N) / 2, floor_index,
        N - 1, floor_index + 1, N, floor_index, N
    };
    for (size_t target : path) {
        goto_index(s, target);
        REQUIRE(s->current_notification_index == target);
        REQUIRE(snapshots_equal(snapshot(s), truth[target - floor_index]));
    }

    delete_sentinel(s);
}

TEST_CASE("KNOWN BUG: path independence breaks once navigation crosses an assignment/new_clause boundary", "[nav][path-independence][bug]")
{
    // Same shape of scenario as above, but this time we deliberately navigate
    // all the way back past the initial add_clause/assign notifications and
    // forward again — the exact "back and forth" pattern an interactive user
    // performs when scrubbing through a solver run.
    SATSentinel* s = make_sentinel();
    add_vars(s, {1, 2});
    const std::vector<Tlit> lits = {Tlit{Tvar{1}, 0}, Tlit{Tvar{2}, 1}};
    REQUIRE(add_clause(s, Tclause{0}, lits.data(), 2, true));
    REQUIRE(assign(s, Tlit{Tvar{1}, 1}));
    REQUIRE(assign(s, Tlit{Tvar{2}, 1}, Tclause{0}));

    size_t N = s->notifications.size();
    StateSnapshot truth_at_N = snapshot(s);

    enter_step_mode(s);
    goto_index(s, 0);                 // walk all the way back to the beginning
    goto_index(s, N);                 // and all the way forward again

    REQUIRE(s->current_notification_index == N);
    // A full round trip to the start and back should reproduce the exact same
    // state we had before we ever navigated.
    REQUIRE(snapshots_equal(snapshot(s), truth_at_N));   // FAILS

    delete_sentinel(s);
}

// ==========================================================================
// PART 4 — Family-specific back-and-forth stress tests.
// ==========================================================================

TEST_CASE("KNOWN BUG: partially stepping back over an unwatch swaps the watch-list slot order", "[nav][path-independence][bug]")
{
    // Same underlying defect as the isolated unwatch involution test above,
    // exercised through the real navigation engine on a realistic chain of
    // watch-list mutations (the kind graph-backtracking / chunk-merging
    // produces): watch L1, block it, watch L2, unwatch L1, re-watch L1, block
    // again. A *full* round trip back to the start and forward again would
    // hide the bug (unwinding all the way to an empty watch list erases the
    // intermediate corruption, and replaying forward rebuilds it fresh) — so
    // this test instead stops the backward walk exactly where unwatch(L1)'s
    // rollback fires while a second watch is still present, which is where
    // unwatch::rollback() push_back()s instead of restoring the original slot.
    SATSentinel* s = make_sentinel();
    add_vars(s, {1, 2, 3});
    const std::vector<Tlit> lits = {Tlit{Tvar{1}, 0}, Tlit{Tvar{2}, 1}, Tlit{Tvar{3}, 1}};
    add_clause(s, Tclause{0}, lits.data(), 3, false);
    size_t floor_index = s->notifications.size();

    watch(s, Tclause{0}, Tlit{Tvar{1}, 0});                    // floor+1
    block(s, Tclause{0}, Tlit{Tvar{3}, 1}, Tlit{Tvar{1}, 0});  // floor+2
    watch(s, Tclause{0}, Tlit{Tvar{2}, 1});                    // floor+3
    unwatch(s, Tclause{0}, Tlit{Tvar{1}, 0});                  // floor+4
    watch(s, Tclause{0}, Tlit{Tvar{1}, 0});                    // floor+5
    block(s, Tclause{0}, Tlit{Tvar{2}, 1}, Tlit{Tvar{1}, 0});  // floor+6 == N

    size_t N = s->notifications.size();
    REQUIRE(N == floor_index + 6);

    enter_step_mode(s);

    // Ground truth: snapshot every index by walking forward once from the floor.
    goto_index(s, floor_index);
    std::vector<StateSnapshot> truth = {snapshot(s)};
    for (size_t i = floor_index; i < N; i++) {
        next_silent(s);
        truth.push_back(snapshot(s));
    }
    REQUIRE(truth[3].clauses[0].watches[0].first == Tlit{Tvar{1}, 0});   // L1 was watched first...
    REQUIRE(truth[3].clauses[0].watches[1].first == Tlit{Tvar{2}, 1});   // ...L2 second.

    // Step back from N to floor+3 (undoing block2, watch(L1)#2, and unwatch(L1)
    // in that order) instead of unwinding all the way to the floor.
    goto_index(s, N);
    goto_index(s, floor_index + 3);
    REQUIRE(s->current_notification_index == floor_index + 3);

    REQUIRE(snapshots_equal(snapshot(s), truth[3]));   // FAILS: L1/L2 slots are swapped

    delete_sentinel(s);
}

TEST_CASE("non-LIFO unassignment sequence survives repeated back-and-forth", "[nav][path-independence]")
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
    size_t floor_index = s->notifications.size();   // don't cross the decisions themselves

    unassign(s, Tlit{Tvar{2}, 1});   // trail: [1, 3, 4]
    s->state->position(Tvar{3}) = 1;
    s->state->position(Tvar{4}) = 2;
    unassign(s, Tlit{Tvar{3}, 1});   // trail: [1, 4]

    size_t N = s->notifications.size();
    REQUIRE(s->state->trail_size() == 2);

    enter_step_mode(s);
    goto_index(s, floor_index);
    std::vector<StateSnapshot> truth = {snapshot(s)};
    for (size_t i = floor_index; i < N; i++) {
        next_silent(s);
        truth.push_back(snapshot(s));
    }

    for (int cycle = 0; cycle < 3; cycle++) {
        goto_index(s, floor_index);
        REQUIRE(snapshots_equal(snapshot(s), truth[0]));
        goto_index(s, floor_index + 1);
        REQUIRE(snapshots_equal(snapshot(s), truth[1]));
        goto_index(s, N);
        REQUIRE(snapshots_equal(snapshot(s), truth[2]));
    }

    delete_sentinel(s);
}

TEST_CASE("shrink_clause chain: two shrinks survive an interleaved back-and-forth walk", "[nav][path-independence]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1, 2, 3, 4});
    const std::vector<Tlit> lits = {Tlit{Tvar{1}, 0}, Tlit{Tvar{2}, 1}, Tlit{Tvar{3}, 1}, Tlit{Tvar{4}, 1}};
    add_clause(s, Tclause{0}, lits.data(), 4, false);
    size_t floor_index = s->notifications.size();

    shrink_clause(s, Tclause{0}, Tlit{Tvar{2}, 1});
    shrink_clause(s, Tclause{0}, Tlit{Tvar{4}, 1});
    size_t N = s->notifications.size();

    enter_step_mode(s);
    goto_index(s, floor_index);
    REQUIRE(s->state->_clauses[Tclause{0}].n_deleted_literals == 0);

    next_silent(s);
    REQUIRE(s->state->_clauses[Tclause{0}].n_deleted_literals == 1);
    StateSnapshot mid = snapshot(s);

    next_silent(s);
    REQUIRE(s->state->_clauses[Tclause{0}].n_deleted_literals == 2);
    StateSnapshot end = snapshot(s);

    back_silent(s);
    REQUIRE(snapshots_equal(snapshot(s), mid));
    back_silent(s);
    REQUIRE(s->state->_clauses[Tclause{0}].n_deleted_literals == 0);

    next_silent(s);
    REQUIRE(snapshots_equal(snapshot(s), mid));
    next_silent(s);
    REQUIRE(snapshots_equal(snapshot(s), end));
    REQUIRE(s->current_notification_index == N);

    delete_sentinel(s);
}

TEST_CASE("lock/unlock assumption toggling is stable under many back-and-forth cycles", "[nav][path-independence]")
{
    SATSentinel* s = make_sentinel();
    add_vars(s, {1});
    size_t floor_index = s->notifications.size();

    lock_assumption(s, Tlit{Tvar{1}, 1});
    unlock_assumption(s, Tlit{Tvar{1}, 1});
    lock_assumption(s, Tlit{Tvar{1}, 1});
    size_t N = s->notifications.size();
    REQUIRE(s->state->locked(Tvar{1}));

    enter_step_mode(s);
    for (int cycle = 0; cycle < 5; cycle++) {
        goto_index(s, floor_index);
        REQUIRE_FALSE(s->state->locked(Tvar{1}));
        goto_index(s, N);
        REQUIRE(s->state->locked(Tvar{1}));
    }

    delete_sentinel(s);
}
