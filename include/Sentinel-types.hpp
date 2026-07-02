/*
 * This file is part of the source code of the software program
 * SATSentinel. It is protected by applicable copyright laws.
 *
 * This source code is protected by the terms of the MIT License.
 */
/**
 * @file include/Sentinel-types.hpp
 * @author Robin Coutelier
 *
 * @brief Fundamental types used throughout SATSentinel: Tvar, Tlit, Tlevel, Tclause, Tval,
 * Tparser, and their arithmetic, comparison, and string-conversion operations.
 */
#pragma once

#include <string>
#include <ostream>
#include <cassert>
#include <functional>

namespace sentinel
{

  /**
   * @brief Type to denote the value of a variable.
   * @details The value can be VAR_TRUE, VAR_FALSE or VAR_UNDEF.
   */
  typedef struct Tval {
    unsigned int value;

    Tval(unsigned value) : value(value) {

    }
    inline bool operator==(const Tval& other) const { return value == other.value; }
    inline bool operator!=(const Tval& other) const { return value != other.value; }
    inline std::string to_string() const {
      switch (value) {
        case 0: return "FALSE";
        case 1: return "TRUE";
        case 2: return "UNDEF";
        default: return "ERROR";
      }
    }
    inline std::ostream& operator<<(std::ostream& os) const { os << to_string(); return os; }
  } Tval;

  /**
   * @brief Type to denote a variable.
   * @details The variable 0 is not used. The first variable is 1.
   */
  typedef struct Tvar{
    unsigned int value;

    Tvar() : value(0) {}
    Tvar(unsigned value) : value(value) {}
    inline bool operator==(const Tvar& other) const { return value == other.value; }
    inline bool operator!=(const Tvar& other) const { return value != other.value; }
    inline bool operator<(const Tvar& other) const { return value < other.value; }
    inline bool operator>(const Tvar& other) const { return value > other.value; }

    inline std::string to_string() const { return "v" + std::to_string(value); }
    inline std::ostream& operator<<(std::ostream& os) const { os << to_string(); return os; }
  } Tvar;

  /**
   * @brief Type to denote a propositional literal. The first bit is the polarity of the literal, the other bits are the variable.
   * @details The polarity is 0 for negative literals and 1 for positive literals.
   * @details The literals 0 and 1 are not used. The first literal is 2 for variable 1 and polarity 0.
   */
  typedef struct Tlit {
    unsigned int value;

    Tlit() : value(0) {}
    Tlit(unsigned value) : value(value) {}
    Tlit(Tvar var, unsigned pol) : value(var.value << 1 | pol) { assert(pol == 0 || pol == 1); }
    inline bool operator==(const Tlit& other) const { return value == other.value; }
    inline bool operator!=(const Tlit& other) const { return value != other.value; }
    inline bool operator<(const Tlit& other) const { return value < other.value; }
    inline Tlit operator~() const { return Tlit(value ^ 1); }
    inline Tvar var() const { return Tvar(value >> 1); }
    inline bool pol() const { return value & 1; }
    inline std::string to_string() const { return (pol() ? "" : "~") + var().to_string(); }
    inline std::ostream& operator<<(std::ostream& os) const { os << to_string(); return os; }

    inline bool satisfied(Tval val) const { return !(pol() ^ val.value); }
    inline bool falsified(Tval val) const { return !(pol() ^ val.value ^ 1); }
    inline bool undefined(Tval val) const { return val.value >> 1; }
  } Tlit;

  /**
   * @brief Type to denote a decision level.
   * @details The decision level 0 is the root level. The first decision level is 1.
   * @details A decision level of LEVEL_UNDEF = 0xFFFFFFFF means that the variable is unassigned.
   */
  typedef struct Tlevel {
    unsigned int value;

    Tlevel(unsigned value) : value(value) {}
    inline bool operator==(const Tlevel& other) const { return value == other.value; }
    inline bool operator!=(const Tlevel& other) const { return value != other.value; }
    inline bool operator<(const Tlevel& other) const { return value < other.value; }
    inline bool operator>(const Tlevel& other) const { return value > other.value; }
    inline bool operator<=(const Tlevel& other) const { return value <= other.value; }
    inline bool operator>=(const Tlevel& other) const { return value >= other.value; }
    inline void operator++() { value++; }
    inline void operator++(int) { value++; }
    inline void operator--() { value--; }
    inline void operator--(int) { value--; }
    inline std::ostream& operator<<(std::ostream& os) const { os << to_string(); return os; }

    inline std::string to_string() const {
      if (value == 0xFFFFFFFF) return "∞";
      if (value == 0xFFFFFFFE) return "ERROR";
      return std::to_string(value);
    }
  } Tlevel;

  /**
   * @brief Type to denote the ID a clause.
   * @details The first clause has the ID 0.
   */
  typedef struct Tclause {
    unsigned int value;

    Tclause(unsigned value) : value(value) {}
    inline bool operator==(const Tclause& other) const { return value == other.value; }
    inline bool operator!=(const Tclause& other) const { return value != other.value; }
    inline bool operator<(const Tclause& other) const { return value < other.value; }
    inline void operator++() { value++; }
    inline void operator++(int) { value++; }
    inline std::string to_string() const {
      if (value == 0xFFFFFFFF) return "UNDEF";
      if (value == 0xFFFFFFFE) return "LAZY";
      if (value == 0xFFFFFFFD) return "ASSUMPTION";
      return "C" + std::to_string(value);
    }
    inline std::ostream& operator<<(std::ostream& os) const { os << "C" << to_string(); return os; }
  } Tclause;

  using Tparser = std::function<bool(std::string)>;

  const Tlit LIT_UNDEF = 0;

  const Tval VAL_FALSE = 0;
  const Tval VAL_TRUE  = 1;
  const Tval VAL_UNDEF = 2;
  const Tval VAL_ERROR = 3;

  const Tlevel LEVEL_ROOT  = 0;
  const Tlevel LEVEL_UNDEF = 0xFFFFFFFF;
  const Tlevel LEVEL_ERROR = 0xFFFFFFFE;

  const Tclause CLAUSE_UNDEF      = 0xFFFFFFFF;
  const Tclause CLAUSE_LAZY       = 0xFFFFFFFE;
  const Tclause CLAUSE_ASSUMPTION = 0xFFFFFFFD;
  const Tclause CLAUSE_ERROR      = 0xFFFFFFFC;
}

namespace std {
  // define to to_string for Tvar, Tlit, Tlevel, Tclause
  inline std::string to_string(const sentinel::Tval& val) {
      return val.to_string();
  }
  inline std::string to_string(const sentinel::Tvar& var) {
      return var.to_string();
  }
  inline std::string to_string(const sentinel::Tlit& lit) {
      return lit.to_string();
  }
  inline std::string to_string(const sentinel::Tlevel& level) {
      return level.to_string();
  }
  inline std::string to_string(const sentinel::Tclause& cl) {
      return cl.to_string();
  }

  // define operator<< for Tvar, Tlit, Tlevel, Tclause
  inline std::ostream& operator<<(std::ostream& os, const sentinel::Tval& val) {
      return os << val.to_string();
  }
  inline std::ostream& operator<<(std::ostream& os, const sentinel::Tvar& var) {
      return os << var.to_string();
  }
  inline std::ostream& operator<<(std::ostream& os, const sentinel::Tlit& lit) {
      return os << lit.to_string();
  }
  inline std::ostream& operator<<(std::ostream& os, const sentinel::Tlevel& level) {
      return os << level.to_string();
  }
  inline std::ostream& operator<<(std::ostream& os, const sentinel::Tclause& cl) {
      return os << cl.to_string();
  }
}
