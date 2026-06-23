#pragma once


#include <string>
#include <functional>
#include <iostream>

namespace sentinel::sat
{
  class Command {
  public:
    Command(const std::string& name,
            const std::string& doc,
            std::function<bool(const std::string&)> action) :
      name(name),
      doc(doc),
      action(action)
    {}

    bool execute(const std::string& input) const {
      unsigned match_length = 0xFFFFFFFF;
      if (input == name
      || (input.size() > name.size()
       && input.compare(0, name.size(), name) == 0
       && input[name.size()] == ' ')) {
        match_length = name.size();
      } else {
        for (const std::string& alias : aliases) {
          if (input == alias
          || (input.size() > alias.size()
           && input.compare(0, alias.size(), alias) == 0
           && input[alias.size()] == ' ')) {
            match_length = alias.size();
            break;
          }
        }
      }
      if (match_length == 0xFFFFFFFF) {
        return false;
      }
      return action(input.substr(match_length));
    }

    const std::string& get_name() const {
      return name;
    }

    const std::string& get_doc() const {
      return doc;
    }

    void add_alias(const std::string& alias) {
      aliases.push_back(alias);
    }
  private:
    const std::string name;
    std::vector<std::string> aliases;
    const std::string doc;
    std::function<bool(const std::string&)> action;
  };

  class CommandInteger : public Command {
  public:
    CommandInteger(const std::string& name,
                   const std::string& doc,
                   std::function<bool(int)> action) :
      Command(name, doc, [action](const std::string& args) {
        try {
          int value = std::stoi(args);
          return action(value);
        } catch (std::invalid_argument const&) {
          std::cout << "Invalid argument (integer expected)" << std::endl;
          return false;
        }
      }) {}
  };

  class CommandIntegers : public Command {
  public:
    CommandIntegers(const std::string& name,
                    const std::string& doc,
                    std::function<bool(const std::vector<int>&)> action) :
      Command(name, doc, [action](const std::string& args) {
        std::vector<int> values;
        std::string::size_type start = 0;
        while (start < args.size()) {
          std::string::size_type end = args.find(' ', start);
          if (end == std::string::npos) {
            end = args.size();
          }
          std::string token = args.substr(start, end - start);
          if (token.empty()) {
            start = end + 1;
            continue;
          }
          try {
            int value = std::stoi(token);
            values.push_back(value);
          } catch (std::invalid_argument const&) {
            std::cout << "Invalid argument \"" + token + "\" - integer expected" << std::endl;
            return false;
          }
          start = end + 1;
        }
        return action(values);
      }) {}
    };

  class CommandParser {
  public:
    CommandParser(const CommandParser&) = delete;
    CommandParser& operator=(const CommandParser&) = delete;

    CommandParser() {
      add_command(Command(
        "help",
        "Print this help message",
        [this](const std::string& args) {
          std::cout << get_help() << std::endl;
          return true;
        }));
    }

    void add_command(const Command& command) {
      commands.push_back(command);
    }

    void add_command(const std::string& name,
                     const std::string& doc,
                     std::function<bool(const std::string&)> action) {
      commands.emplace_back(name, doc, action);
    }

    void add_alias(const std::string& command_name, const std::string& alias) {
      for (Command& command : commands) {
        if (command.get_name() == command_name) {
          command.add_alias(alias);
          return;
        }
      }
      std::cout << "Command not found: " << command_name << std::endl;
    }

    bool parse(const std::string& input) const {
      for (const Command& command : commands) {
        if (command.execute(input)) {
          return true;
        }
      }
      std::cout << "Command: \"" << input << "\" failed to execute. Type \"help\" for a list of commands." << std::endl;
      return false;
    }

    std::string get_help() const {
      std::string help;
      for (const Command& command : commands) {
        help += command.get_name() + ": " + command.get_doc() + "\n";
      }
      return help;
    }

  private:
    std::vector<Command> commands;
  };
}
