/*
 * This file is part of the source code of the software program
 * SATSentinel. It is protected by applicable copyright laws.
 *
 * This source code is protected by the terms of the MIT License.
 */
/**
 * @file src/Sentinel-context.hpp
 * @author Robin Coutelier
 *
 * @brief SentinelContext carrying per-session display settings, notably the display level that
 * controls which notification events trigger an interactive pause.
 */
#pragma once

namespace sentinel
{
  class SentinelContext
  {
  public:
    SentinelContext() = default;
    ~SentinelContext() = default;

    void set_display_level(unsigned level) { display_level = level; }
    unsigned get_display_level() const { return display_level; }

  private:
    unsigned display_level = 0;

    bool show_trail = true;
    unsigned trail_display_start = 0;

    bool show_variables = true;
    bool show_clauses = true;

  };
}
