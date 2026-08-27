/*
 * This file is part of the source code of the software program
 * SATSentinel. It is protected by applicable copyright laws.
 *
 * This source code is protected by the terms of the MIT License.
 */
/**
 * @file src/SATSentinel.hpp
 * @author Robin Coutelier
 *
 * @brief Declaration of SATSentinel (the main observer class) and SentinelMarker (used to
 * highlight specific variables or clauses in interactive display).
 */
#pragma once

#include "Sentinel-types.hpp"
#include "Sentinel-state.hpp"
#include "Sentinel-notifications.hpp"
#include "Sentinel-context.hpp"
#include "Sentinel-commands.hpp"
#include "Sentinel-serialization.hpp"

#include <functional>
#include <set>
#include <vector>
#include <string>

namespace sentinel
{

  namespace notif {
    class notification;
  }

  class SentinelGUI;

  class SentinelMarker {
    public:
      SentinelMarker() = default;
      ~SentinelMarker() = default;

      void mark(Tvar var)              { marked_vars.insert(var); }
      void unmark(Tvar var)            { marked_vars.erase(var);  }
      bool is_marked(Tvar var) const   { return marked_vars.find(var) != marked_vars.end(); }

      void mark(Tclause cl)            { marked_clauses.insert(cl); }
      void unmark(Tclause cl)          { marked_clauses.erase(cl);  }
      bool is_marked(Tclause cl) const { return marked_clauses.find(cl) != marked_clauses.end(); }

    private:
      std::set<Tvar>     marked_vars;
      std::set<Tclause> marked_clauses;
  };

  class SATSentinel
  {
  public:
    SATSentinel(Options* options = nullptr);
    ~SATSentinel();

    bool notify(notif::notification* notif);

    void set_alias(Tvar var, std::string alias) { state->alias(var) = alias; }

    void set_variable_detail_callback(std::function<std::string(Tvar)> callback) { _variable_detail_callback = callback; }

    void set_clause_detail_callback(std::function<std::string(Tclause)> callback) { _clause_detail_callback = callback; }

    void set_command_parser(Tparser* parser);

    bool get_external_commands();

    bool get_navigation_commands();

    bool check_invariants() const;

    void add_invariant(Invariant* invariant);

    void add_watch_invariant(WatchInvariant* invariant);

  private:
    Options* _options;

    std::vector<notif::notification*> notifications;

    SentinelMarker* markers;

    std::set<size_t> breakpoints;

    SentinelState* state;

    std::vector<std::string> commands;

    // Only ever non-null when SATSentinel was built with `make GUI=1` and
    // SentinelOptions::gui is true. Declared unconditionally (rather than
    // behind an #ifdef) so that sizeof(SATSentinel) and member offsets never
    // differ between GUI=0/GUI=1 compiled translation units.
    SentinelGUI* gui_view = nullptr;

    CommandParser navigation_commands;

    Tparser* external_parser = nullptr;

    // Records every notification, as it is sent, to _options->save_file (when set). See
    // Sentinel-serialization.hpp.
    ExecutionLogger execution_log;

    SentinelContext context;

    unsigned current_notification_index = 0;

    unsigned display_level = 0;
    bool failed = false;

    // Optional user-supplied callback providing extra, application-specific
    // metadata about a variable (e.g. SMT-level details). Displayed by the
    // GUI's variable-detail popup; only meaningful in real time (see
    // is_real_time()) since the callback reflects the live host state, not
    // the replayed SentinelState.
    std::function<std::string(Tvar)> _variable_detail_callback;

    // Same idea as _variable_detail_callback, but for clauses.
    std::function<std::string(Tclause)> _clause_detail_callback;

    /**
     * @brief Continue the replay of the notifications until the next breakpoint, or the notification level is lower than the display level, or the end of the notifications is reached.
     * @return false if the last notification failed, true otherwise.
     */
    bool next();

    /**
     * @brief Go back to the previous notification, and rollback the state of the solver until we reach a checkpoint, or the notification level is lower than the display level, or the beginning of the notifications is reached.
     * @return false if the last notification failed, true otherwise.
     */
    bool back();

    /**
     * @brief Jump directly to a specific notification index, applying or rolling back
     * every notification in between as needed, regardless of display level or
     * breakpoints. Used by the GUI's notifications panel ("goto" navigation command) to
     * play the sentinel forward/backward until the notification the user clicked on.
     * @param target_index The 1-based notification index to land on (i.e. the value
     * current_notification_index will have afterwards), matching the numbering already
     * used by "Notification N: ..." headers and the breakpoints set. Clamped to
     * notifications.size() if out of range.
     * @return false if any notification along the way failed to apply/rollback.
     */
    bool goto_notification(size_t target_index);

    std::string last_notification_message() const;

    bool is_real_time() const;


    const SentinelState* get_state() const { return state; }

    void print_state() const;

    void print_clauses() const;
    void print_variables() const;
    void print_trail() const;

    // Prints the C++ stack trace of the host process at the current notification. Only
    // meaningful when is_real_time(): the sentinel is then still blocked inside the notify()
    // call the solver made to report the live notification, so that call chain is on the
    // stack. Once the user has navigated into history with "back", the displayed notification
    // no longer corresponds to any frame on the (unchanged) stack, so the trace is refused.
    void print_backtrace() const;

    void register_commands();

  };
}
