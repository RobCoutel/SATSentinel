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

#include <execinfo.h>
#include <unistd.h>

namespace sentinel
{
bool SATSentinel::is_real_time() const
{
  return current_notification_index == notifications.size();
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
    std::reverse(commands.begin(), commands.end());
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
      if (tcl.value >= state->clauses_size()) {
        std::cout << "Clause " << tcl << " does not exist" << std::endl;
        return false;
      }
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
