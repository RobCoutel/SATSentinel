/*
 * This file is part of the source code of the software program
 * SATSentinel. It is protected by applicable copyright laws.
 *
 * This source code is protected by the terms of the MIT License.
 */
/**
 * @file src/utils/options.cpp
 * @author Robin Coutelier
 *
 * @brief Implementation of the generic Option/OptionParser system.
 */
#include "options.hpp"
#include "printer.hpp"

#include <sstream>

using namespace std;

namespace sentinel
{
  /**************************************************************************************************/
  /*                                          Option                                                */
  /**************************************************************************************************/
  Option::Option(string name, string brief, string category) :
    _name(std::move(name)),
    _brief(std::move(brief)),
    _category(std::move(category))
  {
    _aliases.push_back(_name);
  }

  Option& Option::alias(const string& a)
  {
    _aliases.push_back(a);
    return *this;
  }

  Option& Option::subsumes(Option& loser)
  {
    _subsumed.push_back(&loser);
    loser._subsumed_by.push_back(this);
    return *this;
  }

  Option& Option::require(Option& dep, bool expected)
  {
    _requirements.emplace_back(&dep, expected);
    return *this;
  }

  bool Option::matches(const string& token) const
  {
    for (const string& a : _aliases)
      if (a == token)
        return true;
    return false;
  }

  string Option::help_entry() const
  {
    ostringstream out;
    out << _aliases[0];
    for (size_t i = 1; i < _aliases.size(); i++)
      out << ", " << _aliases[i];
    out << " (" << type_name() << ", default " << default_to_string() << ")\n";
    if (!_brief.empty())
      out << justify_string(_brief, 80, ' ', "    ") << "\n";
    if (!_requirements.empty()) {
      out << "    requires: ";
      for (size_t i = 0; i < _requirements.size(); i++) {
        if (i)
          out << ", ";
        out << _requirements[i].first->get_name() << "=" << (_requirements[i].second ? "on" : "off");
      }
      out << "\n";
    }
    if (!_subsumed.empty()) {
      out << "    subsumes: ";
      for (size_t i = 0; i < _subsumed.size(); i++) {
        if (i)
          out << ", ";
        out << _subsumed[i]->get_name();
      }
      out << "\n";
    }
    return out.str();
  }

  /**************************************************************************************************/
  /*                                        BoolOption                                              */
  /**************************************************************************************************/
  BoolOption::BoolOption(string name, bool& storage, string brief, string category) :
    Option(std::move(name), std::move(brief), std::move(category)),
    _storage(storage),
    _default(storage)
  {
  }

  bool BoolOption::consume(const vector<string>& tokens, unsigned& i)
  {
    string next_token = (i + 1 < tokens.size()) ? tokens[i + 1] : "";
    if (!next_token.empty() && next_token[0] != '-') {
      if (next_token == "on") {
        _storage = true;
      }
      else if (next_token == "off") {
        _storage = false;
      }
      else {
        LOG_WARNING("option " << tokens[i] << " requires a boolean value (on/off).");
        LOG_WARNING("Default value " << (_storage ? "on" : "off") << " is used.");
        return false;
      }
      i++;
    }
    else {
      _storage = true;
    }
    return true;
  }

  /**************************************************************************************************/
  /*                                        IntOption                                               */
  /**************************************************************************************************/
  IntOption::IntOption(string name, int& storage, string brief, string category) :
    Option(std::move(name), std::move(brief), std::move(category)),
    _storage(storage),
    _default(storage)
  {
  }

  bool IntOption::consume(const vector<string>& tokens, unsigned& i)
  {
    string next_token = (i + 1 < tokens.size()) ? tokens[i + 1] : "";
    if (next_token.empty() || next_token[0] == '-') {
      LOG_WARNING("option " << tokens[i] << " requires a value (integer).");
      LOG_WARNING("Default value " << _storage << " is used.");
      return false;
    }
    try {
      _storage = stoi(next_token);
    }
    catch (const std::invalid_argument&) {
      LOG_WARNING("option " << tokens[i] << " requires an integer value.");
      LOG_WARNING("Default value " << _storage << " is used.");
      return false;
    }
    i++;
    return true;
  }

  /**************************************************************************************************/
  /*                                       StringOption                                             */
  /**************************************************************************************************/
  StringOption::StringOption(string name, string& storage, string brief, string category) :
    Option(std::move(name), std::move(brief), std::move(category)),
    _storage(storage),
    _default(storage)
  {
  }

  bool StringOption::consume(const vector<string>& tokens, unsigned& i)
  {
    string next_token = (i + 1 < tokens.size()) ? tokens[i + 1] : "";
    if (next_token.empty() || next_token[0] == '-') {
      LOG_WARNING("option " << tokens[i] << " requires a value (string of characters).");
      LOG_WARNING("The option is ignored.");
      return false;
    }
    _storage = next_token;
    i++;
    return true;
  }

  /**************************************************************************************************/
  /*                                       OptionParser                                             */
  /**************************************************************************************************/
  Option& OptionParser::register_option(unique_ptr<Option> option)
  {
    Option* raw = option.get();
    _options.push_back(std::move(option));
    _index_order.push_back(raw);
    return *raw;
  }

  BoolOption& OptionParser::add_bool(const string& name, bool& storage, const string& brief)
  {
    auto opt = make_unique<BoolOption>(name, storage, brief, _current_category);
    return static_cast<BoolOption&>(register_option(std::move(opt)));
  }

  IntOption& OptionParser::add_int(const string& name, int& storage, const string& brief)
  {
    auto opt = make_unique<IntOption>(name, storage, brief, _current_category);
    return static_cast<IntOption&>(register_option(std::move(opt)));
  }

  StringOption& OptionParser::add_string(const string& name, string& storage, const string& brief)
  {
    auto opt = make_unique<StringOption>(name, storage, brief, _current_category);
    return static_cast<StringOption&>(register_option(std::move(opt)));
  }

  void OptionParser::parse(vector<string>& tokens)
  {
    for (unsigned i = 0; i < tokens.size(); i++) {
      const string token = tokens[i];
      Option* match = nullptr;
      for (Option* opt : _index_order) {
        if (opt->matches(token)) {
          match = opt;
          break;
        }
      }
      if (!match) {
        LOG_WARNING("Unknown option " << token);
        continue;
      }
      if (match->was_set()) {
        LOG_WARNING("option " << token << " already set. The second occurrence is ignored.");
        continue;
      }
      if (match->consume(tokens, i))
        match->mark_set();
    }
  }

  void OptionParser::resolve()
  {
    bool changed = true;
    while (changed) {
      changed = false;
      for (Option* opt : _index_order) {
        if (!opt->truthy())
          continue;
        for (Option* loser : opt->get_subsumed()) {
          if (loser->truthy()) {
            LOG_WARNING(opt->get_name() << " subsumes " << loser->get_name() << ".");
            LOG_WARNING("The sentinel will run with " << opt->get_name() << ".");
            loser->reset_to_default();
            changed = true;
          }
        }
      }
      for (Option* opt : _index_order) {
        if (!opt->truthy())
          continue;
        bool ok = true;
        for (const auto& [dep, expected] : opt->get_requirements()) {
          if (dep->truthy() != expected) {
            ok = false;
            break;
          }
        }
        if (!ok) {
          LOG_WARNING(opt->get_name() << " requires an incompatible combination of options.");
          LOG_WARNING("The option is ignored.");
          opt->reset_to_default();
          changed = true;
        }
      }
    }
  }

  string OptionParser::help_text(const string& title, const string& header) const
  {
    ostringstream out;
    out << title << "\n";
    out << justify_string(header, 80, ' ', "") << "\n";
    string last_category;
    bool first = true;
    for (Option* opt : _index_order) {
      if (first || opt->get_category() != last_category) {
        last_category = opt->get_category();
        first = false;
        if (!last_category.empty())
          out << "\n*** " << last_category << " ***\n";
      }
      out << "  " << opt->help_entry() << "\n";
    }
    return out.str();
  }

} // namespace sentinel
