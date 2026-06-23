#pragma once

namespace sentinel::sat
{
  struct SentinelOptions
  {
    bool interactive = false;
    int default_display_level = 0;
    bool check_only = false;

    bool check_trail_sanity = true;
    bool check_implied_levels = true;
    bool check_trail_monotonicity = true;
    bool check_no_missed_implications = true;
    bool check_topological_order = true;
    bool check_assignment_coherence = true;

    bool check_weak_watched_literals = false;
    bool check_strong_watched_literals = false;
  };
}
