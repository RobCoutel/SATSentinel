#pragma once

namespace sentinel::sat
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
