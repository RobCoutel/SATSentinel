#include "SATSentinel.hpp"
#include "Sentinel-commands.hpp"

#include "Sentinel-types.hpp"
#include "utils/printer.hpp"

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
  external_parser = parser;
}

bool SATSentinel::get_external_commands()
{
  std::string input;
  if (!external_parser) {
    std::cout << "No external command parser set. Please set one using set_command_parser() before calling get_external_commands()." << std::endl;
    return false;
  }
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
  std::string input;
  print_state();
  std::cout << "Notification " << current_notification_index << ": " << last_notification_message() << std::endl;
  std::cout << "NAVIGATION COMMANDS\n";
  navigation_commands.get_command();
  return true;
}

bool SATSentinel::check_invariants() const
{
  std::string err_msg;
  bool success = state->check_invariants(err_msg);
  if (!success) {
    std::cerr << "Invariant check failed:\n" << err_msg << std::endl;
  }
  return success;
}


void sentinel::SATSentinel::register_commands() {
  navigation_commands.add_command(Command(
    "next",
    "Go to the next notification",
    [this](const std::string& args) {
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
        std::cout << "Variable " << tvar.to_string() << " does not exist" << std::endl;
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
        std::cout << "Clause " << tcl.to_string() << " does not exist" << std::endl;
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
