#include "Sentinel-API.hpp"

#include "src/Sentinel-commands.hpp"

#include <fstream>

using namespace sentinel;

namespace
{
  std::vector<std::string> split_ws(const std::string& s)
  {
    std::vector<std::string> tokens;
    std::string::size_type start = 0;
    while (start < s.size()) {
      std::string::size_type end = s.find(' ', start);
      if (end == std::string::npos) {
        end = s.size();
      }
      std::string token = s.substr(start, end - start);
      if (!token.empty()) {
        tokens.push_back(token);
      }
      start = end + 1;
    }
    return tokens;
  }

  // Splits args into a leading token and the (untokenized) remainder, skipping any leading
  // spaces. Used by commands whose last argument is free text (ALIAS, MESSAGE).
  bool split_head(const std::string& args, std::string& head, std::string& rest)
  {
    std::string::size_type begin = args.find_first_not_of(' ');
    if (begin == std::string::npos) {
      return false;
    }
    std::string::size_type sep = args.find(' ', begin);
    if (sep == std::string::npos) {
      head = args.substr(begin);
      rest = "";
      return true;
    }
    head = args.substr(begin, sep - begin);
    std::string::size_type rest_begin = args.find_first_not_of(' ', sep);
    rest = (rest_begin == std::string::npos) ? "" : args.substr(rest_begin);
    return true;
  }

  bool parse_lit(const std::string& token, Tlit& out)
  {
    int value;
    try {
      value = std::stoi(token);
    } catch (std::exception const&) {
      return false;
    }
    if (value == 0) {
      return false;
    }
    out = Tlit(Tvar(abs(value)), value > 0);
    return true;
  }

  bool parse_reason(const std::string& token, Tclause& out)
  {
    if (token == "UNDEF") {
      out = CLAUSE_UNDEF;
    } else if (token == "ASSUMPTION") {
      out = CLAUSE_ASSUMPTION;
    } else if (token == "LAZY") {
      out = CLAUSE_LAZY;
    } else if (token == "ROOT") {
      out = CLAUSE_ROOT;
    } else if (token == "ERROR") {
      out = CLAUSE_ERROR;
    } else {
      try {
        out = Tclause((unsigned)std::stoi(token));
      } catch (std::exception const&) {
        return false;
      }
    }
    return true;
  }
}

void configure_command_parser(SATSentinel* sentinel, CommandParser& parser)
{
  /**
   * Supported commands (mirrors every operation the observer can perform, except checkpoint):
   * - help: print the list of commands
   * - NEW VAR <var>: add a variable to the solver state
   * - ALIAS <var> <text>: associate a human-readable alias with a variable
   * - NEW CLAUSE <clause> <lit1> [<lit2> ...] [EXTERNAL]: add a clause to the solver
   * - DELETE CLAUSE <clause>: delete a clause from the solver
   * - SHRINK CLAUSE <clause> <literal>: remove a literal from a clause
   * - ASSIGN <literal> [<reason>]: assign a literal to true, with an optional reason clause
   * - UNASSIGN <literal>: unassign a literal
   * - PROPAGATE <literal>: push a propagated literal onto the propagation queue
   * - UNPROPAGATE <literal>: remove a propagated literal from the propagation queue
   * - UPDATE LEVEL <literal> <level>: update the decision level of a literal
   * - UPDATE REASON <literal> <reason>: update the reason clause of a literal
   * - WATCH <clause> <literal>: watch a literal in a clause
   * - UNWATCH <clause> <literal>: unwatch a literal in a clause
   * - BLOCK <clause> <blocker> <watched-literal>: set the blocking literal of a watched clause
   * - LOCK <literal>: lock a literal as an assumption
   * - UNLOCK <literal>: unlock a literal as an assumption
   * - CHECK INVARIANTS: check the invariants of the solver state
   * - MESSAGE [<level>] <text>: send a diagnostic message to the solver state
   * - quit: quit the sentinel
   * (checkpoint is deliberately not exposed here: it is driven by the main loop, not replayed)
  */
  parser.add_command(Command(
    "quit",
    "Quit the sentinel",
    [](const std::string& args) {
      exit(0);
      return true;
    }));
  parser.add_command(CommandInteger(
    "NEW VAR",
    "Add a variable to the solver state",
    [sentinel](int var) {
      Tvar tvar(var);
      if (!add_variable(sentinel, tvar)) {
        std::cout << "Variable " << tvar << " already exists" << std::endl;
        return false;
      }
      return true;
    }));

  parser.add_command(Command(
    "ALIAS",
    "Associate a human-readable alias with a variable: <var> <alias-text>",
    [sentinel](const std::string& args) {
      std::string var_tok, alias_text;
      if (!split_head(args, var_tok, alias_text) || alias_text.empty()) {
        std::cout << "Invalid arguments (variable and alias text expected)" << std::endl;
        return false;
      }
      int var_value;
      try {
        var_value = std::stoi(var_tok);
      } catch (std::exception const&) {
        std::cout << "Invalid argument \"" << var_tok << "\" - integer expected" << std::endl;
        return false;
      }
      if (!set_variable_alias(sentinel, Tvar(var_value), alias_text)) {
        std::cout << "Failed to set alias for variable " << var_value << std::endl;
        return false;
      }
      return true;
    }));

  parser.add_command(Command(
    "NEW CLAUSE",
    "Add a clause to the solver state: <clause-id> <lit1> [<lit2> ...] [EXTERNAL]",
    [sentinel](const std::string& args) {
      std::vector<std::string> tokens = split_ws(args);
      bool external = false;
      if (!tokens.empty() && tokens.back() == "EXTERNAL") {
        external = true;
        tokens.pop_back();
      }
      if (tokens.size() < 2) {
        std::cout << "Invalid arguments (clause id and at least one literal expected)" << std::endl;
        return false;
      }
      int cl_value;
      try {
        cl_value = std::stoi(tokens[0]);
      } catch (std::exception const&) {
        std::cout << "Invalid argument \"" << tokens[0] << "\" - clause id (integer) expected" << std::endl;
        return false;
      }
      Tclause cl((unsigned)cl_value);

      std::vector<Tlit> lits;
      for (size_t i = 1; i < tokens.size(); ++i) {
        Tlit lit;
        if (!parse_lit(tokens[i], lit)) {
          std::cout << "Invalid argument \"" << tokens[i] << "\" - integer literal expected" << std::endl;
          return false;
        }
        lits.push_back(lit);
      }
      if (!add_clause(sentinel, cl, lits.data(), lits.size(), external)) {
        std::cout << "Clause " << cl << " failed to be added." << std::endl;
        return false;
      }
      return true;
    }));

  parser.add_command(CommandIntegers(
    "DELETE CLAUSE",
    "Delete a clause from the solver state",
    [sentinel](const std::vector<int>& args) {
      if (args.size() != 1) {
        std::cout << "Invalid arguments (exactly one argument expected)" << std::endl;
        return false;
      }
      Tclause clause(args[0]);
      if (!delete_clause(sentinel, clause)) {
        std::cout << "Clause " << clause << " failed to be deleted." << std::endl;
        return false;
      }
      return true;
    }));

  parser.add_command(CommandIntegers(
    "SHRINK CLAUSE",
    "Remove a literal from a clause",
    [sentinel](const std::vector<int>& args) {
      if (args.size() != 2) {
        std::cout << "Invalid arguments (exactly two arguments expected)" << std::endl;
        return false;
      }
      Tclause clause(args[0]);
      Tlit removed_lit(Tvar(abs(args[1])), args[1] > 0);
      if (!shrink_clause(sentinel, clause, removed_lit)) {
        std::cout << "Clause " << clause << " failed to be shrunk." << std::endl;
        return false;
      }
      return true;
    }));

  parser.add_command(Command(
    "ASSIGN",
    "Assign a literal to true, with an optional reason clause",
    [sentinel](const std::string& args) {
      std::vector<std::string> tokens = split_ws(args);
      if (tokens.size() < 1 || tokens.size() > 2) {
        std::cout << "Invalid arguments (one or two arguments expected)" << std::endl;
        return false;
      }
      Tlit lit;
      if (!parse_lit(tokens[0], lit)) {
        std::cout << "Invalid argument \"" << tokens[0] << "\" - integer literal expected" << std::endl;
        return false;
      }
      Tclause reason = CLAUSE_UNDEF;
      if (tokens.size() == 2 && !parse_reason(tokens[1], reason)) {
        std::cout << "Invalid argument \"" << tokens[1] << "\" - reason keyword or clause id expected" << std::endl;
        return false;
      }
      if (!assign(sentinel, lit, reason)) {
        std::cout << "Literal " << lit << " failed to be assigned." << std::endl;
        return false;
      }
      return true;
    }));

  parser.add_command(CommandInteger(
    "UNASSIGN",
    "Unassign a literal",
    [sentinel](int lit) {
      Tlit tlit(Tvar(abs(lit)), lit > 0);
      if (!unassign(sentinel, tlit)) {
        std::cout << "Literal " << tlit << " failed to be unassigned." << std::endl;
        return false;
      }
      return true;
    }));

  parser.add_command(CommandInteger(
    "PROPAGATE",
    "Push a propagated literal onto the propagation queue",
    [sentinel](int lit) {
      Tlit tlit(Tvar(abs(lit)), lit > 0);
      if (!propagate(sentinel, tlit)) {
        std::cout << "Literal " << tlit << " failed to be propagated." << std::endl;
        return false;
      }
      return true;
    }));

  parser.add_command(CommandInteger(
    "UNPROPAGATE",
    "Remove a propagated literal from the propagation queue",
    [sentinel](int lit) {
      Tlit tlit(Tvar(abs(lit)), lit > 0);
      if (!unpropagate(sentinel, tlit)) {
        std::cout << "Literal " << tlit << " failed to be unpropagated." << std::endl;
        return false;
      }
      return true;
    }));

  parser.add_command(CommandIntegers(
    "UPDATE LEVEL",
    "Update the decision level of a literal: <literal> <level>",
    [sentinel](const std::vector<int>& args) {
      if (args.size() != 2) {
        std::cout << "Invalid arguments (exactly two arguments expected)" << std::endl;
        return false;
      }
      if (args[1] < 0) {
        std::cout << "Invalid level (positive integer expected)" << std::endl;
        return false;
      }
      Tlit lit(Tvar(abs(args[0])), args[0] > 0);
      if (!update_level(sentinel, lit, Tlevel((unsigned)args[1]))) {
        std::cout << "Failed to update level of literal " << lit << std::endl;
        return false;
      }
      return true;
    }));

  parser.add_command(Command(
    "UPDATE REASON",
    "Update the reason clause of a literal: <literal> <reason>",
    [sentinel](const std::string& args) {
      std::vector<std::string> tokens = split_ws(args);
      if (tokens.size() != 2) {
        std::cout << "Invalid arguments (exactly two arguments expected)" << std::endl;
        return false;
      }
      Tlit lit;
      if (!parse_lit(tokens[0], lit)) {
        std::cout << "Invalid argument \"" << tokens[0] << "\" - integer literal expected" << std::endl;
        return false;
      }
      Tclause reason = CLAUSE_UNDEF;
      if (!parse_reason(tokens[1], reason)) {
        std::cout << "Invalid argument \"" << tokens[1] << "\" - reason keyword or clause id expected" << std::endl;
        return false;
      }
      if (!update_reason(sentinel, lit, reason)) {
        std::cout << "Failed to update reason of literal " << lit << std::endl;
        return false;
      }
      return true;
    }));

  parser.add_command(CommandIntegers(
    "WATCH",
    "Add a clause to the watch list of a literal: <clause> <literal>",
    [sentinel](const std::vector<int>& args) {
      if (args.size() != 2) {
        std::cout << "Invalid arguments (exactly two arguments expected)" << std::endl;
        return false;
      }
      Tclause clause(args[0]);
      Tlit lit(Tvar(abs(args[1])), args[1] > 0);
      if (!watch(sentinel, clause, lit)) {
        std::cout << "Failed to watch literal " << lit << " in clause " << clause << std::endl;
        return false;
      }
      return true;
    }));

  parser.add_command(CommandIntegers(
    "UNWATCH",
    "Remove a clause from the watch list of a literal: <clause> <literal>",
    [sentinel](const std::vector<int>& args) {
      if (args.size() != 2) {
        std::cout << "Invalid arguments (exactly two arguments expected)" << std::endl;
        return false;
      }
      Tclause clause(args[0]);
      Tlit lit(Tvar(abs(args[1])), args[1] > 0);
      if (!unwatch(sentinel, clause, lit)) {
        std::cout << "Failed to unwatch literal " << lit << " in clause " << clause << std::endl;
        return false;
      }
      return true;
    }));

  parser.add_command(CommandIntegers(
    "BLOCK",
    "Record the blocking literal of a watched clause: <clause> <blocker> <watched-literal>",
    [sentinel](const std::vector<int>& args) {
      if (args.size() != 3) {
        std::cout << "Invalid arguments (exactly three arguments expected)" << std::endl;
        return false;
      }
      Tclause clause(args[0]);
      Tlit blocker(Tvar(abs(args[1])), args[1] > 0);
      Tlit watched(Tvar(abs(args[2])), args[2] > 0);
      if (!block(sentinel, clause, blocker, watched)) {
        std::cout << "Failed to block literal " << blocker << " in clause " << clause << std::endl;
        return false;
      }
      return true;
    }));

  parser.add_command(CommandInteger(
    "LOCK",
    "Lock a literal as an assumption",
    [sentinel](int lit) {
      Tlit tlit(Tvar(abs(lit)), lit > 0);
      if (!lock_assumption(sentinel, tlit)) {
        std::cout << "Literal " << tlit << " failed to be locked." << std::endl;
        return false;
      }
      return true;
    }));

  parser.add_command(CommandInteger(
    "UNLOCK",
    "Unlock a literal as an assumption",
    [sentinel](int lit) {
      Tlit tlit(Tvar(abs(lit)), lit > 0);
      if (!unlock_assumption(sentinel, tlit)) {
        std::cout << "Literal " << tlit << " failed to be unlocked." << std::endl;
        return false;
      }
      return true;
    }));

  parser.add_command(Command(
    "CHECK INVARIANTS",
    "Check all registered invariants of the solver state",
    [sentinel](const std::string& args) {
      if (!check_invariants(sentinel)) {
        std::cout << "Invariant check failed." << std::endl;
        return false;
      }
      return true;
    }));

  parser.add_command(Command(
    "MESSAGE",
    "Emit a diagnostic message: [<level>] <text>",
    [sentinel](const std::string& args) {
      std::string head, rest;
      if (!split_head(args, head, rest)) {
        std::cout << "Invalid arguments (message text expected)" << std::endl;
        return false;
      }
      unsigned level = 0;
      std::string text = args.substr(args.find_first_not_of(' '));
      try {
        level = (unsigned)std::stoi(head);
        text = rest;
      } catch (std::exception const&) {
        // First token is not a level: treat the whole (trimmed) argument string as the message.
      }
      if (text.empty()) {
        std::cout << "Invalid arguments (message text expected)" << std::endl;
        return false;
      }
      if (!message(sentinel, text, level)) {
        std::cout << "Message failed to be sent." << std::endl;
        return false;
      }
      return true;
    }));
}

int main(int argc, char* argv[])
{
  std::vector<std::string> args(argv + 1, argv + argc);
  if (!args.empty() && (args[0] == "-h" || args[0] == "--help")) {
    std::cout << sentinel::Options::get_help_text();
    return 0;
  }

  // The first argument, if it does not start with '-', is a log file to replay: every line is
  // fed through the command parser (below) before the sentinel enters its checkpoint loop.
  std::string load_file;
  if (!args.empty() && !args[0].empty() && args[0][0] != '-') {
    load_file = args[0];
    args.erase(args.begin());
  }

  // Read the whole file up front (rather than streaming it) so that, if --save-file happens to
  // point at the same path, opening the save file for writing later does not truncate data we
  // still need to read here.
  std::vector<std::string> load_commands;
  if (!load_file.empty()) {
    std::ifstream file(load_file);
    if (!file.is_open()) {
      std::cout << "Could not open file to load: " << load_file << std::endl;
      return 1;
    }
    std::string line;
    while (std::getline(file, line)) {
      load_commands.push_back(line);
    }
  }

  sentinel::Options options(args);

  SATSentinel* sentinel = sentinel::create_sentinel(options);

  message(sentinel, "Sentinel started", 0);

  CommandParser parser;
  configure_command_parser(sentinel, parser);

  for (std::string line : load_commands) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty() || line[0] == '#') {
      continue;
    }
    bool continue_on_success = false;
    if (!parser.parse(line, continue_on_success)) {
      std::cout << "Failed to load command: \"" << line << "\"" << std::endl;
    }
  }

  Tparser* command_parser = new Tparser([&parser](std::string input) {
    bool continue_on_success = false;
    return parser.parse(input, continue_on_success);
  });
  set_command_parser(sentinel, command_parser);

  while (true) {
    checkpoint(sentinel);
  }
  return 0;
}
