#include "Sentinel-API.hpp"

#include "src/Sentinel-commands.hpp"

using namespace sentinel;

static Tclause clause_number = 0;

void configure_command_parser(SATSentinel* sentinel, CommandParser& parser)
{
  /**
   * Supported commands:
   * - help: print the list of commands
   * - add variable <var>: add a variable to the solver state
   * - add clause <clause> [ℓ₁] [ℓ₂] [...]: add a clause to the solver
   * - delete clause <clause>: delete a clause from the solver
   * - shrink clause <clause> <literal>: remove a literal from a clause
   * - assign <literal> [<reason>]: assign a literal to true, with an optional reason clause
   * - unassign <literal>: unassign a literal
   * - propagate <literal>: propagate a literal
   * - unpropagate <literal>: unpropagate a literal
   * - update level <literal> <level>: update the level of a literal
   * - update reason <literal> <reason>: update the reason of a literal
   * - watch <clause> <literal>: watch a literal in a clause
   * - unwatch <clause> <literal>: unwatch a literal in a clause
   * - block <clause> <literal> [<watch>]: block a literal in a clause, with an optional watch literal
   * - check invariants: check the invariants of the solver state
   * - checkpoint: create a checkpoint of the solver state
   * - message <message>: send a message to the solver state
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
  parser.add_command(CommandIntegers(
    "NEW CLAUSE",
    "Add a clause to the solver state",
    [sentinel](const std::vector<int>& args) {
      if (args.size() < 1) {
        std::cout << "Invalid arguments (at least one argument expected)" << std::endl;
        return false;
      }
      Tclause cl = clause_number;
      clause_number++;

      std::vector<Tlit> lits;
      for (size_t i = 0; i < args.size(); ++i) {
        int lit = args[i];
        Tvar var(abs(lit));
        lits.push_back(Tlit(var, lit > 0));
      }
      if (!add_clause(sentinel, cl, lits.data(), lits.size())) {
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
      Tlit removed_lit(Tvar(abs(args[1])), args[1] < 0);
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
      std::vector<std::string> tokens;
      std::string::size_type start = 0;
      while (start < args.size()) {
        std::string::size_type end = args.find(' ', start);
        if (end == std::string::npos) {
          end = args.size();
        }
        std::string token = args.substr(start, end - start);
        if (!token.empty()) {
          tokens.push_back(token);
        }
        start = end + 1;
      }
      if (tokens.size() < 1 || tokens.size() > 2) {
        std::cout << "Invalid arguments (one or two arguments expected)" << std::endl;
        return false;
      }
      int lit_value;
      try {
        lit_value = std::stoi(tokens[0]);
      } catch (std::invalid_argument const&) {
        std::cout << "Invalid argument \"" << tokens[0] << "\" - integer expected" << std::endl;
        return false;
      }
      Tlit lit(Tvar(abs(lit_value)), lit_value > 0);
      Tclause reason = CLAUSE_UNDEF;
      if (tokens.size() == 2) {
        const std::string& r = tokens[1];
        if (r == "ASSUMPTION") {
          reason = CLAUSE_ASSUMPTION;
        } else if (r == "LAZY") {
          reason = CLAUSE_LAZY;
        } else if (r == "ROOT") {
          reason = CLAUSE_ROOT;
        } else if (r == "ERROR") {
          reason = CLAUSE_ERROR;
        } else {
          try {
            reason = Tclause((unsigned)std::stoi(r));
          } catch (std::invalid_argument const&) {
            std::cout << "Invalid argument \"" << r << "\" - reason keyword or clause id expected" << std::endl;
            return false;
          }
        }
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
}

int main(int argc, char* argv[])
{
  std::vector<std::string> args(argv + 1, argv + argc);
  if (!args.empty() && (args[0] == "-h" || args[0] == "--help")) {
    std::cout << sentinel::Options::get_help_text();
    return 0;
  }
  sentinel::Options options(args);

  SATSentinel* sentinel = sentinel::create_sentinel(options);

  CommandParser parser;
  configure_command_parser(sentinel, parser);


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
