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
  std::function<bool(const SentinelState*, std::string&)> invariant_checker;
  mutable std::string error_message;

  Invariant(const std::string name, std::function<bool(const SentinelState*, std::string&)> checker) :
    name(name),
    invariant_checker(checker)
  {}

  std::string get_error_message(std::string& err_msg) const {
    err_msg = error_message;
    error_message = "";
    return err_msg;
  }

  bool check(const SentinelState* state) const {
    return invariant_checker(state, error_message);
  }
};

struct WatchInvariant {
  const std::string name;
  std::function<bool(Tlit, Tlit, Tlit, std::string& err_msg)> watch_literal_invariant;
  mutable std::string error_message;

public:
  WatchInvariant(const std::string name, std::function<bool(Tlit, Tlit, Tlit, std::string&)> checker) :
    name(name),
    watch_literal_invariant(checker) {}


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
