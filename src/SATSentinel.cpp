/*
 * This file is part of the source code of the software program
 * SATSentinel. It is protected by applicable copyright laws.
 *
 * This source code is protected by the terms of the MIT License.
 */
/**
 * @file src/SATSentinel.cpp
 * @author Robin Coutelier
 *
 * @brief Implementation of SATSentinel: notification replay (next/back), invariant registration,
 * interactive state display, and built-in navigation command setup.
 */
#include "SATSentinel.hpp"

#include "Sentinel-types.hpp"
#include "Sentinel-state.hpp"
#include "Sentinel-notifications.hpp"
#include "utils/printer.hpp"

#include <iostream>
#include <vector>
#include <string>

namespace sentinel
{

SATSentinel::SATSentinel(SentinelOptions* options)
{
  if (options) {
    _options = options;
  } else {
    _options = new SentinelOptions();
  }
  markers = new SentinelMarker();
  state = new SentinelState(options);
  display_level = _options->default_display_level;

  register_commands();
}

SATSentinel::~SATSentinel()
{
  delete markers;
  delete state; // this will delete the options
  if (external_parser) {
    delete external_parser;
  }
}

bool SATSentinel::notify(notif::notification* notif)
{
  notifications.push_back(notif);
  return next();
}

bool SATSentinel::next()
{
  bool success = true;
  bool display_state = false;
  while (current_notification_index < notifications.size()) {
    notif::notification* notif = notifications[current_notification_index++];
    bool success = notif->apply(state);
    if (success && notif->get_event_level(markers) > display_level) {
      continue;
    }

    if (!success) {
      failed = true;
      LOG_ERROR("Notification failed: " << notif->get_message());
      if (_options->crash_on_error) {
        LOG_ERROR("Crashing due to error...");
        abort();
      }
    }
    display_state = true;
    break;
  }
  if (display_state && !_options->check_only) {
    get_navigation_commands();
  }
  return success;
}

bool SATSentinel::back()
{
  if (current_notification_index == 0) {
    return true;
  }
  bool success = true;
  bool display_state = false;
  while (current_notification_index > 0) {
    notif::notification* notif = notifications[--current_notification_index];
    bool success = notif->rollback(state);
    if (success && notif->get_event_level(markers) > display_level) {
      continue;
    }
    if (!success) {
      failed = true;
      std::cerr << "Notification rollback failed: " << notif->get_message() << std::endl;
    }
    display_state = true;
    break;
  }
  if (display_state) {
    get_external_commands();
  }
  return success;
}

std::string SATSentinel::last_notification_message() const
{
  if (current_notification_index == 0) {
    return "No notifications yet";
  }
  notif::notification* notif = notifications[current_notification_index - 1];
  return notif->get_message();
}

void SATSentinel::print_state() const
{
  if (failed) {
    std::cout << ERROR_HEAD << "FAILED STATE: " << notifications[current_notification_index-1]->get_message() << std::endl;
  }
  std::cout << "Current state of the solver:" << std::endl;
  print_variables();
  print_clauses();
  print_trail();
}

void SATSentinel::print_clauses() const
{
  update_terminal_width();

  // for every 100 clauses, check for the size. We do not want that the last clauses, that are super long
  // destroy the display of the shorter clauses we started with

  size_t start = 0;
  size_t end = 144;

  size_t n_clauses = state->clauses_size();

  while (start < n_clauses) {
    if (end > n_clauses)
      end = n_clauses;

    // generate and store the clauses' strings
    std::vector<std::string> clauses_str;
    unsigned max_clause_str_length = 0;
    unsigned ellipsed_clauses = 0;
    for (Tclause cl = start; cl.value < end; cl++) {
      if (!state->active(cl)) {
        end = std::min(end + 1, n_clauses);
        continue;
      }
      if (n_clauses > 300) {
        // check if the clause is relevant: only keep clauses that are unit, conflicting, or marked
        if (!markers->is_marked(cl)
         && !state->unit(cl)
         && !state->conflicting(cl)) {
          ellipsed_clauses++;
          end = std::min(end + 1, n_clauses);
          continue;
        }
      }
      if (ellipsed_clauses > 0) {
        clauses_str.push_back("... " + std::to_string(ellipsed_clauses) + " clauses ellipsed ...");
        max_clause_str_length = std::max(max_clause_str_length, string_length_escaped(clauses_str.back()));
        ellipsed_clauses = 0;
      }

      std::string clause_str = state->to_string(cl);
      max_clause_str_length = std::max(max_clause_str_length, string_length_escaped(clause_str));
      clauses_str.push_back(clause_str);
    }

    // check if we can push more clauses that are smaller than the current max length
    while (end < n_clauses) {
      if (!state->active(Tclause(end))) {
        end++;
        continue;
      }

      if (n_clauses > 300) {
        // check if the clause is relevant: only keep clauses that are unit, conflicting, or marked
        Tclause cl = Tclause(end);
        if (!markers->is_marked(cl)
         && !state->unit(cl)
         && !state->conflicting(cl)) {
          ellipsed_clauses++;
          end = std::min(end + 1, n_clauses);
          continue;
        }
      }

      std::string clause_str = state->to_string(Tclause(end));
      unsigned clause_str_length = string_length_escaped(clause_str);
      if (clause_str_length > max_clause_str_length)
        break;
      max_clause_str_length = std::max(max_clause_str_length, clause_str_length);
      clauses_str.push_back(clause_str);
      end++;
    }

    if (clauses_str.empty()) {
      std::cout << "No clauses to print" << std::endl;
      return;
    }

    // add 3 spaces for the clauses to be separated
    max_clause_str_length += 3;

    // compute the number of columns to print
    unsigned n_columns = get_terminal_width() / max_clause_str_length;
    n_columns = std::max(1u, n_columns);
    unsigned n_lines = clauses_str.size() / n_columns + 1;
    n_lines -= clauses_str.size() % n_columns == 0;

    // adjust the max_clause_str_length to fit as well as possible
    max_clause_str_length += (get_terminal_width() - (n_columns * max_clause_str_length)) / n_columns;

    // pad the clauses with spaces
    for (unsigned i = 0; i < clauses_str.size(); i++) {
      std::string clause_str = clauses_str[i];
      while (string_length_escaped(clause_str) < max_clause_str_length)
        clause_str += " ";
      clauses_str[i] = clause_str;
    }

    // print the clauses
    for (unsigned i = 0; i < n_lines; i++) {
      for (unsigned j = 0; j < n_columns; j++) {
        unsigned k = i * n_columns + j;
        if (k >= clauses_str.size())
          break;
        std::cout << clauses_str[k];
      }
      std::cout << "\n";
    }

    start = end;
    end = start + 144;
  }
  std::cout << "\n";
  for (unsigned i = 0; i < get_terminal_width(); i++)
    std::cout << "*";
  std::cout << std::endl;
}

void SATSentinel::print_variables() const
{
  update_terminal_width();

  std::vector<std::string> variables_str;
  unsigned max_var_str_length = 0;
  for (Tvar var = 0; var.value < state->variables_size(); var.value++) {
    std::string variable_str = state->to_string(var);
    max_var_str_length = std::max(max_var_str_length, string_length_escaped(variable_str));
    variables_str.push_back(variable_str);
  }

  if (variables_str.empty()) {
    std::cout << "No variables to print" << std::endl;
    return;
  }

  // add 3 spaces for the variables to be separated
  max_var_str_length += 3;

  // pad the variables with spaces
  for (size_t i = 0; i < variables_str.size(); i++) {
    std::string variable_str = variables_str[i];
    while (string_length_escaped(variable_str) < max_var_str_length)
      variable_str += " ";
    variables_str[i] = variable_str;
  }

  // compute the number of columns to print
  unsigned n_columns = get_terminal_width() / max_var_str_length;
  unsigned n_lines = variables_str.size() / n_columns + 1;
  n_lines -= variables_str.size() % n_columns == 0;
  // print the variables
  for (unsigned i = 0; i < n_lines; i++) {
    for (unsigned j = 0; j < n_columns; j++) {
      size_t idx = i + j * n_lines;
      if (idx >= variables_str.size())
        break;
      std::cout << variables_str[idx];
    }
    std::cout << "\n";
  }

  std::cout << "\n";
  for (unsigned i = 0; i < get_terminal_width(); i++)
    std::cout << "*";
  std::cout << std::endl;
}

void SATSentinel::print_trail() const
{
  update_terminal_width();

  std::cout << "trail :\n";

  unsigned max_n_digits_level = 1;
  Tlevel max_level = state->level();
  while (max_level > 0) {
    max_level.value /= 10;
    max_n_digits_level++;
  }

  for (Tlevel lvl = state->level(); lvl <= state->level(); lvl--) {
    bool propagation_marked = false;
    // pad the level with spaces
    std::string level_str = lvl.to_string();
    while(level_str.size() < max_n_digits_level)
      level_str = " " + level_str;
    std::cout << level_str + ": ";
    for (unsigned i = 0; i < state->trail_size(); i++) {
      Tlit lit = state->trail_literal(i);
      if (!propagation_marked && !state->propagated(lit)) {
        std::cout << "| ";
        propagation_marked = true;
      }
      if (state->level(lit) == lvl)
        std::cout << state->to_string(lit) << " ";
      else {
        unsigned len_str = string_length_escaped(state->to_string(lit));
        std::cout << " ";
        for (unsigned j = 0; j < len_str; j++)
          std::cout << " ";
      }
    }
    if (!propagation_marked)
      std::cout << "| ";
    std::cout << "\n";
  }
  for (unsigned i = 0; i < get_terminal_width(); i++)
    std::cout << "*";
  std::cout << std::endl;
}
}
