/*
 * This file is part of the source code of the software program
 * SATSentinel. It is protected by applicable copyright laws.
 *
 * This source code is protected by the terms of the MIT License.
 */
/**
 * @file src/Sentinel-serialization.cpp
 * @author Robin Coutelier
 *
 * @brief Implementation of ExecutionLogger and of every notification's to_command(), which
 * together turn a live sentinel run into a human-readable, replayable command log.
 */
#include "Sentinel-serialization.hpp"

#include "Sentinel-notifications.hpp"
#include "Sentinel-types.hpp"

namespace sentinel
{
  namespace
  {
    // Literal syntax accepted by the command parser: Tlit::to_string() (e.g. "~v0"). A plain
    // sign (e.g. "-0") is not used, since it can't distinguish the negative literal of variable
    // 0 from the positive one.
    std::string lit_to_command(Tlit lit)
    {
      return lit.to_string();
    }

    // Reason/clause-id token accepted by the command parser: a keyword for the special reasons,
    // or the bare clause index otherwise (no "C" prefix, unlike Tclause::to_string()).
    std::string reason_to_command(Tclause reason)
    {
      if (reason == CLAUSE_UNDEF) return "UNDEF";
      if (reason == CLAUSE_ROOT) return "ROOT";
      if (reason == CLAUSE_LAZY) return "LAZY";
      if (reason == CLAUSE_ASSUMPTION) return "ASSUMPTION";
      if (reason == CLAUSE_ERROR) return "ERROR";
      return std::to_string(reason.value);
    }
  }

  bool ExecutionLogger::open(const std::string& filename)
  {
    _stream.open(filename, std::ios::out | std::ios::trunc);
    return _stream.is_open();
  }

  void ExecutionLogger::record(const notif::notification& notification)
  {
    _stream << notification.to_command() << std::endl;
  }
}

namespace sentinel::notif
{
  std::string new_variable::to_command() const
  {
    return "NEW VAR " + std::to_string(var.value);
  }

  std::string assignment::to_command() const
  {
    return "ASSIGN " + lit_to_command(lit) + " " + reason_to_command(reason);
  }

  std::string update_level::to_command() const
  {
    return "UPDATE LEVEL " + lit_to_command(lit) + " " + std::to_string(level.value);
  }

  std::string update_reason::to_command() const
  {
    return "UPDATE REASON " + lit_to_command(lit) + " " + reason_to_command(reason);
  }

  std::string propagation::to_command() const
  {
    return "PROPAGATE " + lit_to_command(lit);
  }

  std::string propagation_removed::to_command() const
  {
    return "UNPROPAGATE " + lit_to_command(lit);
  }

  std::string unassignment::to_command() const
  {
    return "UNASSIGN " + lit_to_command(lit);
  }

  std::string new_clause::to_command() const
  {
    std::string str = "NEW CLAUSE " + std::to_string(cl.value);
    for (const Tlit& l : lits) {
      str += " " + lit_to_command(l);
    }
    if (external) {
      str += " EXTERNAL";
    }
    return str;
  }

  std::string delete_clause::to_command() const
  {
    return "DELETE CLAUSE " + std::to_string(cl.value);
  }

  std::string watch::to_command() const
  {
    return "WATCH " + std::to_string(cl.value) + " " + lit_to_command(lit);
  }

  std::string unwatch::to_command() const
  {
    return "UNWATCH " + std::to_string(cl.value) + " " + lit_to_command(lit);
  }

  std::string block::to_command() const
  {
    return "BLOCK " + std::to_string(cl.value) + " " + lit_to_command(blocker) + " " + lit_to_command(blocked_lit);
  }

  std::string remove_literal::to_command() const
  {
    return "SHRINK CLAUSE " + std::to_string(cl.value) + " " + lit_to_command(lit);
  }

  std::string lock_assumption::to_command() const
  {
    return "LOCK " + lit_to_command(lit);
  }

  std::string unlock_assumption::to_command() const
  {
    return "UNLOCK " + lit_to_command(lit);
  }

  std::string message::to_command() const
  {
    return "MESSAGE " + std::to_string(notif_lvl) + " " + m;
  }
}
