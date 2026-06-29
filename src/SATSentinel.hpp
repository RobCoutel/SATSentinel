#pragma once

#include "Sentinel-types.hpp"
#include "Sentinel-state.hpp"
#include "Sentinel-notifications.hpp"
#include "Sentinel-context.hpp"
#include "Sentinel-commands.hpp"

#include <set>
#include <vector>
#include <string>

namespace sentinel
{

  namespace notif {
    class notification;
  }

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
    SATSentinel(SentinelOptions* options = nullptr);
    ~SATSentinel();

    bool notify(notif::notification* notif);

    void set_alias(Tvar var, std::string alias) { state->alias(var) = alias; }

    void set_command_parser(Tparser* parser);

    bool get_external_commands();

    bool get_navigation_commands();

    bool check_invariants() const;

    void add_invariant(Invariant* invariant);

    void add_watch_invariant(WatchInvariant* invariant);

  private:
    SentinelOptions* _options;

    std::vector<notif::notification*> notifications;

    SentinelMarker* markers;

    std::set<size_t> breakpoints;

    SentinelState* state;

    CommandParser navigation_commands;

    Tparser* external_parser = nullptr;

    SentinelContext context;

    unsigned current_notification_index = 0;

    unsigned display_level = 0;
    bool failed = false;

    bool next();

    bool back();

    std::string last_notification_message() const;

    bool is_real_time() const;


    const SentinelState* get_state() const { return state; }

    void print_state() const;

    void print_clauses() const;
    void print_variables() const;
    void print_trail() const;

    void register_commands();

  };
}
