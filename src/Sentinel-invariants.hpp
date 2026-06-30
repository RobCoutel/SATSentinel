/*
 * This file is part of the source code of the software program
 * SATSentinel. It is protected by applicable copyright laws.
 *
 * This source code is protected by the terms of the MIT License.
 */
/**
 * @file src/Sentinel-invariants.hpp
 * @author Robin Coutelier
 *
 * @brief Invariant and WatchInvariant structs for registering custom solver correctness checks
 * that are evaluated at each checkpoint.
 */
#pragma once

#include "Sentinel-types.hpp"
#include "Sentinel-options.hpp"

#include <vector>
#include <string>
#include <functional>

namespace sentinel
{
class SentinelState;

struct Invariant {
  const std::string name;
  std::function<bool(std::string&)> invariant_checker;
  mutable std::string error_message;

  Invariant(const std::string name, std::function<bool(std::string&)> checker) :
    name(name),
    invariant_checker(checker)
  {}

  std::string get_error_message(std::string& err_msg) const {
    err_msg = error_message;
    error_message = "";
    return err_msg;
  }

  bool check() const {
    return invariant_checker( error_message);
  }
};

struct WatchInvariant {
  const std::string name;
  std::function<bool(Tlit, Tlit, Tlit, std::string& err_msg)> watch_literal_invariant;
  mutable std::string error_message;
  const std::string description;

public:
  WatchInvariant(const std::string name,
                 std::function<bool(Tlit, Tlit, Tlit, std::string&)> checker,
                 const std::string& description = "") :
    name(name),
    watch_literal_invariant(checker),
    description(description) {}


  bool check(Tlit c1, Tlit c2, Tlit blocker) const {
    if (watch_literal_invariant) {
      return watch_literal_invariant(c1, c2, blocker, error_message);
    }
    return true;
  }

  std::string geterr_msg() const {
    std::string err_msg = error_message;
    error_message = "";
    return err_msg;
  }
};
}
