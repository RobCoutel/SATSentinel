/*
 * This file is part of the source code of the software program
 * SATSentinel. It is protected by applicable copyright laws.
 *
 * This source code is protected by the terms of the MIT License.
 */
/**
 * @file src/Sentinel-commands.cpp
 * @author Robin Coutelier
 *
 * @brief Implementation of the CommandParser interactive loop and the built-in navigation
 * commands (next, back, print state/clauses/variables/trail) registered on SATSentinel.
 */
#include "SATSentinel.hpp"
#include "Sentinel-commands.hpp"

#include "Sentinel-types.hpp"
#include "utils/printer.hpp"

#ifdef SENTINEL_GUI_ENABLED
#include "gui/SentinelGUI.hpp"
#endif

#include <cassert>
#include <iostream>
#include <fstream>
#include <string>

namespace sentinel
{
bool SATSentinel::is_real_time() const
{
  return current_notification_index == notifications.size();
}

void SATSentinel::print_backtrace() const
{
  // The stack only reflects the live notification while the sentinel sits on top of the
  // notification stack: "back" rewinds the *replayed state*, not the process call stack, so a
  // trace taken while browsing history would silently point at the wrong event.
  if (!is_real_time()) {
    std::cout << WARNING_HEAD << "Cannot print stack trace: the sentinel is browsing history "
                  "(not synchronized with the solver). Use \"next\" to return to the live "
                  "notification first." << std::endl;
    return;
  }

  // skip_frames=1: also hide this function's own frame, so the trace starts at our caller.
  std::string trace = capture_backtrace(64, 1);
  if (trace.empty()) {
    std::cout << WARNING_HEAD << "Failed to resolve the stack trace." << std::endl;
    return;
  }

  std::cout << "Stack trace at notification " << current_notification_index
            << " (" << last_notification_message() << "):" << std::endl;
  std::cout << trace;
}

void SATSentinel::set_command_parser(Tparser* parser)
{
  // if the commands file is setup, we read the commands from that file first, and then we read from the user input
  if (_options && !_options->commands_file.empty()) {
    std::ifstream commands_file(_options->commands_file);
    if (!commands_file.is_open()) {
      std::cout << "Could not open commands file: " << _options->commands_file << std::endl;
      return;
    }
    std::string line;
    while (std::getline(commands_file, line)) {
      commands.push_back(line);
    }
    // reverse the commands so that we can pop them from the back
    // for some reason, std::reverse does not compile on the zebra nodes.
    for (size_t i = 0; i < commands.size() / 2; i++) {
      std::swap(commands[i], commands[commands.size() - 1 - i]);
    }
  }

  external_parser = parser;
}

bool SATSentinel::get_external_commands()
{
  std::string input;
  if (!external_parser) {
    std::cout << "No external command parser set. Please set one using set_command_parser() before calling get_external_commands()." << std::endl;
    return false;
  }
  if (commands.size() > 0) {
    input = commands.back();
    commands.pop_back();
    bool success = external_parser->operator()(input);
    if (!success) {
      std::cout << "Command failed to execute. Type \"help\" for a list of commands." << std::endl;
    }
    return success;
  }
#ifdef SENTINEL_GUI_ENABLED
  if (gui_view) {
    // GUI is the sole interface: no terminal printing, no std::cin prompting.
    SentinelGUI::GuiDispatch dispatch = [this](const std::string& cmd_input, bool& should_stop_prompting) {
      bool cmd_success = external_parser->operator()(cmd_input);
      should_stop_prompting = cmd_success;
      return cmd_success;
    };
    gui_view->pump_until_command(dispatch, "Last notification: " + last_notification_message(), "USER COMMANDS");
    return true;
  }
#endif
  bool success = false;
  print_state();
  std::cout << "Last notification: " << last_notification_message() << std::endl;
  std::cout << "USER COMMANDS\n";
  do {
    std::cout << "Enter a command: ";
    std::getline(std::cin, input);
    success = external_parser->operator()(input);
  } while (!success);
  return true;
}

bool SATSentinel::get_navigation_commands()
{
#ifdef SENTINEL_GUI_ENABLED
  if (gui_view) {
    // GUI is the sole interface: no terminal printing, no std::cin prompting.
    SentinelGUI::GuiDispatch dispatch = [this](const std::string& cmd_input, bool& should_stop_prompting) {
      return navigation_commands.parse(cmd_input, should_stop_prompting);
    };
    gui_view->pump_until_command(dispatch, "Notification " + std::to_string(current_notification_index) + ": " + last_notification_message(), "NAVIGATION COMMANDS");
    return true;
  }
#endif
  print_state();
  std::cout << "Notification " << current_notification_index << ": " << last_notification_message() << std::endl;
  std::cout << "NAVIGATION COMMANDS\n";
  navigation_commands.get_command();
  return true;
}

void sentinel::SATSentinel::register_commands() {
  navigation_commands.add_command(Command(
    "next",
    "Go to the next notification",
    [this](const std::string& args) {
      // If we are behind the notification stack (the user navigated into
      // history with "back"), "next" must replay one recorded step forward
      // instead of releasing control back to the solver: the sentinel stays
      // locked until it is back on top of the stack (is_real_time()).
      if (!is_real_time())
        next();
      return true;
    }));
  navigation_commands.add_alias("next", "");

  navigation_commands.add_command(Command(
    "back",
    "Go to the previous notification",
    [this](const std::string& args) {
      back();
      return true;
    }));
  navigation_commands.add_alias("back", "b");

  navigation_commands.add_command(CommandInteger(
    "goto",
    "Jump directly to a specific (1-based) notification index, bypassing display level and "
    "breakpoints - used by the GUI's notifications panel to play the sentinel until a clicked "
    "notification.",
    [this](int index) {
      if (index < 0) {
        std::cout << "Invalid notification index (positive integer expected)" << std::endl;
        return false;
      }
      goto_notification((size_t)index);
      // Same reasoning as "back": goto_notification() may leave the sentinel off the top
      // of the notification stack, so control must re-enter the navigation prompt rather
      // than being released back to the (possibly out-of-sync) caller.
      if (!_options->check_only)
        get_navigation_commands();
      return true;
    }));

  navigation_commands.add_command(Command(
    "quit",
    "Quit the sentinel",
    [this](const std::string& args) {
      // TODO exit gracefully
      exit(0);
      return true;
    }));
  navigation_commands.add_alias("quit", "q");

  navigation_commands.add_command(Command(
    "print",
    "Print the current state of the solver",
    [this](const std::string& args) {
      print_state();
      return true;
    }, false));
  navigation_commands.add_alias("print", "p");

  navigation_commands.add_command(Command(
    "backtrace",
    "Print the C++ stack trace of the solver at the current notification. Only available while "
    "synchronized with the solver (i.e. not after \"back\" has moved away from the live notification).",
    [this](const std::string& args) {
      print_backtrace();
      return true;
    }, false));
  navigation_commands.add_alias("backtrace", "bt");

  navigation_commands.add_command(CommandInteger(
    "set level",
    "Set the display level (notifications with event level higher than the display level will be ignored)",
    [this](int level) {
      if (level < 0) {
        std::cout << "Invalid level (positive integer expected)" << std::endl;
        return false;
      }
      display_level = level;
      return true;
    }, false));

  navigation_commands.add_command(CommandInteger(
    "mark var",
    "Mark a variable. The Sentinel will stop at notifications that involve this variable.",
    [this](int var) {
      Tvar tvar(var);
      if (tvar.value >= state->variables_size()) {
        std::cout << "Variable " << tvar << " does not exist" << std::endl;
        return false;
      }
      markers->mark(tvar);
      return true;
    }, false));

  navigation_commands.add_command(CommandInteger(
    "unmark var",
    "Unmark a variable (cfr. \"mark var\").",
    [this](int var) {
      Tvar tvar(var);
      markers->unmark(tvar);
      return true;
    }, false));

  navigation_commands.add_command(CommandInteger(
    "mark clause",
    "Mark a clause. The Sentinel will stop at notifications that involve this clause.",
    [this](int cl) {
      Tclause tcl(cl);
      markers->mark(tcl);
      return true;
    }, false));

  navigation_commands.add_command(CommandInteger(
    "unmark clause",
    "Unmark a clause (cfr. \"mark clause\").",
    [this](int cl) {
      Tclause tcl(cl);
      markers->unmark(tcl);
      return true;
    }, false));

  navigation_commands.add_command(CommandInteger(
    "set breakpoint",
    "Set a breakpoint at a given level. The Sentinel will stop when it reaches a notification with the given level.",
    [this](int level) {
      if (level < 0) {
        std::cout << "Invalid breakpoint (positive integer expected)" << std::endl;
        return false;
      }
      breakpoints.insert(level);
      return true;
    }, false));

  navigation_commands.add_command(CommandInteger(
    "remove breakpoint",
    "Remove a breakpoint (cfr. \"set breakpoint\").",
    [this](int level) {
      if (level < 0) {
        std::cout << "Invalid breakpoint (positive integer expected)" << std::endl;
        return false;
      }
      breakpoints.erase(level);
      return true;
    }, false));
  }
}
