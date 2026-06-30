/*
 * This file is part of the source code of the software program
 * SATSentinel. It is protected by applicable copyright laws.
 *
 * This source code is protected by the terms of the MIT License.
 */
/**
 * @file src/Sentinel-commands.hpp
 * @author Robin Coutelier
 *
 * @brief Command and CommandParser classes that implement the interactive debugging interface,
 * including command registration, aliasing, help generation, and line-by-line parsing.
 */
#pragma once

#include <string>
#include <functional>
#include <iostream>

namespace sentinel
{
  class Command {
  public:
    Command(const std::string& name,
            const std::string& doc,
            std::function<bool(const std::string&)> action,
            bool continue_on_success = true) :
      name(name),
      doc(doc),
      action(action),
      continue_on_success(continue_on_success)
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

    bool get_continue_on_success() const {
      return continue_on_success;
    }
  private:
    const std::string name;
    std::vector<std::string> aliases;
    const std::string doc;
    std::function<bool(const std::string&)> action;
    bool continue_on_success;
  };

  class CommandInteger : public Command {
  public:
    CommandInteger(const std::string& name,
                   const std::string& doc,
                   std::function<bool(int)> action,
                   bool continue_on_success = true) :
      Command(name, doc, [action](const std::string& args) {
        try {
          int value = std::stoi(args);
          return action(value);
        } catch (std::invalid_argument const&) {
          std::cout << "Invalid argument (integer expected)" << std::endl;
          return false;
        }
      }, continue_on_success) {}
  };

  class CommandIntegers : public Command {
  public:
    CommandIntegers(const std::string& name,
                    const std::string& doc,
                    std::function<bool(const std::vector<int>&)> action,
                    bool continue_on_success = true) :
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
      }, continue_on_success) {}
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
        }, false));
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

    void get_command() {
      bool continue_on_success = false;
      do {
        std::cout << "Enter a command: ";
        std::string input;
        std::getline(std::cin, input);
        bool success = parse(input, continue_on_success);
        if (!success) {
          std::cout << "Command failed to execute. Type \"help\" for a list of commands." << std::endl;
        }

      } while (!continue_on_success);
    }

    bool parse(const std::string& input, bool& continue_on_success) const {
      for (const Command& command : commands) {
        if (command.execute(input)) {
          continue_on_success = command.get_continue_on_success();
          return true;
        }
      }
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
