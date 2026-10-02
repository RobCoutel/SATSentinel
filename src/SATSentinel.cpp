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

#ifdef SENTINEL_GUI_ENABLED
#include "gui/SentinelGUI.hpp"
#endif

#include <iostream>
#include <vector>
#include <string>

namespace sentinel
{

SATSentinel::SATSentinel(Options* options)
{
  if (options) {
    _options = options;
  } else {
    _options = new Options();
  }
  markers = new SentinelMarker();
  state = new SentinelState(options);
  display_level = _options->default_display_level;

  if (!_options->save_file.empty() && !execution_log.open(_options->save_file)) {
    std::cout << WARNING_HEAD << "Could not open save file for writing: " << _options->save_file << std::endl;
  }

  register_commands();

#ifdef SENTINEL_GUI_ENABLED
  if (_options->gui) {
    gui_view = new SentinelGUI(state, markers, _options, &display_level,
                                &_variable_detail_callback, &_clause_detail_callback,
                                &notifications, &current_notification_index, &breakpoints,
                                [this]() { return is_real_time(); });
    if (!gui_view->is_valid()) {
      std::cout << WARNING_HEAD << "Failed to initialize the GUI (no display / GLFW init failure?); falling back to the terminal frontend." << std::endl;
      delete gui_view;
      gui_view = nullptr;
      _options->gui = false;
    }
  }
#else
  if (_options->gui) {
    std::cout << WARNING_HEAD << "GUI requested but SATSentinel was built without GUI support (rebuild with `make GUI=1`); continuing without GUI." << std::endl;
    _options->gui = false;
  }
#endif
}

SATSentinel::~SATSentinel()
{
#ifdef SENTINEL_GUI_ENABLED
  delete gui_view;
#endif
  delete markers;
  delete state; // this will delete the options
  if (external_parser) {
    delete external_parser;
  }
}

bool SATSentinel::notify(notif::notification* notif)
{
  notifications.push_back(notif);
  if (execution_log.is_open()) {
    execution_log.record(*notif);
  }
  return next();
}

bool SATSentinel::next()
{
  bool success = true;
  bool display_state = false;
  while (current_notification_index < notifications.size()) {
    notif::notification* notif = notifications[current_notification_index++];
    success = notif->apply(state);
    display_state |= !success;
    display_state |= notif->get_event_level(markers) <= display_level;
    display_state |= breakpoints.find(current_notification_index) != breakpoints.end();
    if (!display_state) {
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
    // Nothing to roll back to; re-display the current (start-of-history) state
    // instead of silently returning, so the GUI/terminal prompt loop keeps
    // running rather than being torn down as if a stopping command ran.
    if (!_options->check_only) {
      get_navigation_commands();
    }
    return true;
  }
  bool success = true;
  while (current_notification_index > 0) {
    notif::notification* notif = notifications[--current_notification_index];
    bool step_success = notif->rollback(state);
    success = success && step_success;
    if (step_success && notif->get_event_level(markers) > display_level) {
      continue;
    }
    if (!step_success) {
      failed = true;
      std::cerr << "Notification rollback failed: " << notif->get_message() << std::endl;
    }
    break;
  }
  // The loop above always leaves current_notification_index strictly below
  // notifications.size() (real time), whether it stopped on a displayable
  // notification or ran all the way down to the start of history. Either way
  // we must re-enter the prompt loop rather than returning: letting this
  // "stop" propagate to the caller would release control back to the solver
  // while the sentinel is not on top of the notification stack.
  if (!_options->check_only) {
    get_navigation_commands();
  }
  return success;
}

bool SATSentinel::goto_notification(size_t target_index)
{
  if (target_index > notifications.size())
    target_index = notifications.size();

  bool success = true;
  while (current_notification_index < target_index) {
    notif::notification* notif = notifications[current_notification_index++];
    if (!notif->apply(state)) {
      success = false;
      failed = true;
      LOG_ERROR("Notification failed: " << notif->get_message());
      if (_options->crash_on_error) {
        LOG_ERROR("Crashing due to error...");
        abort();
      }
    }
  }
  while (current_notification_index > target_index) {
    notif::notification* notif = notifications[--current_notification_index];
    if (!notif->rollback(state)) {
      success = false;
      failed = true;
      std::cerr << "Notification rollback failed: " << notif->get_message() << std::endl;
    }
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

}
