/*
 * This file is part of the source code of the software program
 * SATSentinel. It is protected by applicable copyright laws.
 *
 * This source code is protected by the terms of the MIT License.
 */
/**
 * @file src/Sentinel-options.cpp
 * @author Robin Coutelier
 *
 * @brief Command-line parser for Options, built on the generic Option/OptionParser system
 * (src/utils/options.hpp).
 */
#include "Sentinel-options.hpp"
#include "utils/options.hpp"

using namespace std;

namespace sentinel
{
  static void build_option_parser(Options& t, OptionParser& p)
  {
    p.set_category("GENERAL");
    p.add_int("--default-display-level", t.default_display_level,
              "Default display level. Notifications with a level higher than or equal to this "
              "level will interrupt the solver, display the state, and prompt the user for a "
              "navigation command.")
      .alias("-dl");
    p.add_bool("--check-only", t.check_only,
               "If true, the sentinel will never interrupt the solver execution, and will not "
               "display the state. However, if a notification fails, the sentinel will display an "
               "error.")
      .alias("-co");
    p.add_bool("--crash-on-error", t.crash_on_error,
               "If true, the sentinel will crash the execution if a notification fails. Otherwise, "
               "the sentinel will display an error and continue the execution.");
    p.add_bool("--gui", t.gui,
               "If true, the sentinel will display a graphical interface instead of printing to the "
               "terminal. Has no effect unless SATSentinel was built with GUI support.")
      .alias("-gui")
      .alias("--sentinel-gui");
    p.add_string("--commands", t.commands_file,
                 "In interactive mode, the sentinel will read the commands in the given file before "
                 "asking the user for input.")
      .alias("-commands")
      .alias("--command-file");

    p.set_category("INVARIANT CHECKS");
    p.add_bool("--check-no-conflicts", t.check_no_conflicts,
               "Check that no clause is falsified by the trail.");
    p.add_bool("--check-no-missed-implications", t.check_no_missed_implications,
               "Check that no clause has exactly one undefined literal with the others falsified.");
    p.add_bool("--check-implied-levels", t.check_implied_levels,
               "Check that implied literals are assigned at the correct decision level.");
    p.add_bool("--check-trail-monotonicity", t.check_trail_monotonicity,
               "Check that the trail is monotonic with respect to decision levels.");
    p.add_bool("--check-topological-order", t.check_topological_order,
               "Check that the trail is a topological sort of the implication graph.");
    p.add_bool("--check-assignment-coherence", t.check_assignment_coherence,
               "Check that the reasons of implied literals are correct.");
    p.add_bool("--check-weak-watched-literals", t.check_weak_watched_literals,
               "Check the weak watched-literal-with-blocker invariant.");
    p.add_bool("--check-strong-watched-literals", t.check_strong_watched_literals,
               "Check the strong watched-literal-with-blocker invariant.");
  }

  Options::Options(vector<string>& tokens)
  {
    OptionParser parser;
    build_option_parser(*this, parser);
    parser.parse(tokens);
    parser.resolve();
  }

  string Options::get_help_text()
  {
    static const string title =
      "################################################################################\n"
      "#                                 SAT-Sentinel                                 #\n"
      "################################################################################";
    static const string header =
      "The SAT-Sentinel is a tool for observing and interacting with a SAT solver. It can be used to check invariants, display the solver state, and interactively navigate through the solver's execution.\n It supports both a command-line interface and a graphical interface (if built with GUI support).";
    Options dummy;
    OptionParser parser;
    build_option_parser(dummy, parser);
    return parser.help_text(title, header);
  }

} // namespace sentinel
