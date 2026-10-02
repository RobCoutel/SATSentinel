/*
 * This file is part of the source code of the software program
 * SATSentinel. It is protected by applicable copyright laws.
 *
 * This source code is protected by the terms of the MIT License.
 */
/**
 * @file src/Sentinel-state-printer.cpp
 * @author Robin Coutelier
 *
 * @brief Implementation of the pretty-print routines for interactive inspection of the
 * SentinelState.
 */
#include "Sentinel-state.hpp"

#include "utils/printer.hpp"

#include <cassert>
#include <cmath>
#include <utility>

namespace sentinel
{

std::string SentinelState::to_string(Tlit lit) const
{
  std::string s = "";
  Tvar var = lit.var();

  // styling
  if (lit_undef(lit))
    s += ORANGE;
  else if (lit_true(lit))
    s += GREEN;
  else
    s += RED;

  if (propagated(lit))
    s += UNDERLINE;

  // the literal
  if (alias(lit).empty())
    s += lit.to_string();
  else
    s += alias(lit);
  if (locked(var))
    s += "🔒";

  // reset the style
  s += RESET;
  return s;
}

std::string SentinelState::to_string(Tvar var) const
{
  std::string s = "";
  s += var.to_string() + ": ";
  s += pad(var.value, _variables.size());
  if (propagated(var))
    s += " (p)";
  else
    s += " (u)";
  if (!alias(var).empty())
    s += alias(var) + ": ";
  else
    s += var.to_string() + " ";
  if (active(var)) {
    if (value(var) == VAL_UNDEF) {
      s += ORANGE;
      s += "undef";
      s += RESET;
    } else if (value(var) == VAL_TRUE) {
      s += GREEN;
      s += "true";
      s += RESET;
    } else if (value(var) == VAL_FALSE) {
      s += RED;
      s += "false";
      s += RESET;
    } else
      s += "error";
    s += " @ ";
    s += level(var).to_string();
    s += " by ";
    if (decision(var))
      s += "decision";
    else if (reason(var) == CLAUSE_UNDEF)
      s += "undef";
    else if (lazy(var))
      s += "lazy";
    else
      s += reason(var).to_string();
  }
  else
    s += "deleted";
  return s;
}

std::string SentinelState::to_string(Tclause cl, bool show_blocker) const
{
  if (cl.value >= _clauses.size()) {
    return "C" + std::to_string(cl.value) + " (undefined)";
  }
  const clause& c = _clauses[cl];

  if (!c.active) {
    return "C" + std::to_string(cl.value) + " (inactive)";
  }

  std::string satisfied_lits = "";
  std::string undefined_lits = "";
  std::string falsified_lits = "";

  Tlit c1 = c.watches.size() > 0 ? c.watches[0].first : LIT_UNDEF;
  Tlit c2 = c.watches.size() > 1 ? c.watches[1].first : LIT_UNDEF;
  Tlit b1 = c.watches.size() > 0 ? c.watches[0].second : LIT_UNDEF;
  Tlit b2 = c.watches.size() > 1 ? c.watches[1].second : LIT_UNDEF;

  unsigned n_active = n_active_literals(cl);
  for (unsigned i = 0; i < n_active; i++) {
    Tlit lit = c.literals[i];
    if (lit_true(lit)) {
      satisfied_lits += to_string(lit);
      if (show_blocker && lit == c1 && b1 != LIT_UNDEF)
        satisfied_lits += "(" + to_string(b1) + ")";
      else if (show_blocker && lit == c2 && b2 != LIT_UNDEF)
        satisfied_lits += "(" + to_string(b2) + ")";
      satisfied_lits += " ";
    } else if (lit_undef(lit)) {
      undefined_lits += to_string(lit);
      if (show_blocker && lit == c1 && b1 != LIT_UNDEF)
        undefined_lits += "(" + to_string(b1) + ")";
      else if (show_blocker && lit == c2 && b2 != LIT_UNDEF)
        undefined_lits += "(" + to_string(b2) + ")";
      undefined_lits += " ";
    } else {
      falsified_lits += to_string(lit);
      if (show_blocker && lit == c1 && b1 != LIT_UNDEF)
        falsified_lits += "(" + to_string(b1) + ")";
      else if (show_blocker && lit == c2 && b2 != LIT_UNDEF)
        falsified_lits += "(" + to_string(b2) + ")";
      falsified_lits += " ";
    }
  }

  std::string s = "";
  if (!satisfied_lits.empty()) {
    s += GREEN;
  } else if (!undefined_lits.empty()) {
    s += ORANGE;
  } else {
    s += RED;
  }
  s += cl.to_string() + RESET + ": ";


  s += satisfied_lits + undefined_lits + falsified_lits;
  if (n_active < c.literals.size()) {
    s += "| ";
    for (unsigned i = n_active; i < c.literals.size(); i++) {
      Tlit lit = c.literals[i];
      s += to_string(lit) + " ";
    }
  }
  return s;
}

// Splits an alias into a name prefix and a trailing run of digits (e.g. "x1" -> ("x", "1")).
// digits is empty when the alias has no trailing digits, or consists only of digits.
static void split_alias(const std::string& alias, std::string& prefix, std::string& digits)
{
  size_t digits_start = alias.size();
  while (digits_start > 0 && alias[digits_start - 1] >= '0' && alias[digits_start - 1] <= '9')
    digits_start--;
  if (digits_start == 0 || digits_start == alias.size()) {
    prefix = alias;
    digits = "";
    return;
  }
  prefix = alias.substr(0, digits_start);
  digits = alias.substr(digits_start);
}

// The digit string that would appear in this variable's LaTeX subscript: its alias's trailing
// digit run, or its raw id when unaliased. Empty when it has no alias digits (e.g. alias "flag").
static std::string latex_subscript_digits(const std::string& alias, unsigned id)
{
  if (alias.empty())
    return std::to_string(id);
  std::string prefix, digits;
  split_alias(alias, prefix, digits);
  return digits;
}

// Renders a variable's LaTeX display name: its alias with any trailing run of digits turned
// into a subscript (e.g. "x1" -> "x_{1}", "h2" -> "h_{2}"), or "v_{id}" if it has no alias.
// An alias with no trailing digits (or made up only of digits) is emitted as-is.
static std::string latex_var_name(const std::string& alias, unsigned id)
{
  if (alias.empty())
    return "v_{" + std::to_string(id) + "}";
  std::string prefix, digits;
  split_alias(alias, prefix, digits);
  if (digits.empty())
    return alias;
  return prefix + "_{" + digits + "}";
}

std::string SentinelState::to_latex(Tlit lit) const
{
  std::string s = "";
  if (lit_false(lit))
    s += "\\red{";
  else if (lit_true(lit))
#if COLORBLIND_MODE
    s += "\\blue{";
#else
    s += "\\green{";
#endif
  if (!lit.pol())
    s += "\\neg ";
  s += latex_var_name(alias(lit.var()), lit.var().value);
  if (!lit_undef(lit))
    s += "}";
  return s;
}

// Pads a LaTeX subscript/index so that every value in [0, max_id] renders with the same
// number of digits (via \phantom{0..0}), keeping stacked clause/variable lists aligned.
static std::string latex_index_padding(unsigned id, unsigned max_id)
{
  unsigned n_digits = (unsigned) std::floor(std::log10(id + 1));
  unsigned max_digits = (unsigned) std::floor(std::log10(max_id + 1));
  if (n_digits >= max_digits)
    return "";
  std::string zeroes = "";
  while (n_digits < max_digits) {
    zeroes += "0";
    n_digits++;
  }
  return "\\phantom{" + zeroes + "}";
}

std::string SentinelState::literal_to_aligned_latex(Tlit lit, bool watched) const
{
  std::string s = "";
  if (lit_false(lit))
    s += "\\red{";
  else if (lit_true(lit))
#if COLORBLIND_MODE
    s += "\\blue{";
#else
    s += "\\green{";
#endif

  // Widest subscript (an alias's digit suffix, or the raw id when unaliased) across all
  // variables, so that every literal's name - whatever its own subscript width, or even if it
  // has none at all - takes up the same horizontal space and the disjuncts line up.
  unsigned max_digits = 0;
  for (const variable& v : _variables)
    max_digits = std::max(max_digits, (unsigned) latex_subscript_digits(v.alias, v.var.value).size());

  const std::string& var_alias = alias(lit.var());
  std::string prefix, digits;
  if (var_alias.empty()) {
    prefix = "v";
    digits = std::to_string(lit.var().value);
  } else {
    split_alias(var_alias, prefix, digits);
  }
  std::string padding = digits.size() < max_digits ? std::string(max_digits - digits.size(), '0') : "";

  // The underline (when watched) covers the real negation symbol and the variable's name, but
  // never a phantom negation placeholder or the phantom padding used to align subscripts of
  // different widths - those stay outside it while still reserving their space.
  if (lit.pol())
    s += "\\phantom{\\neg} ";
  std::string head = (lit.pol() ? "" : "\\neg ") + prefix;
  s += watched ? "\\underline{" + head : head;

  if (!digits.empty() || !padding.empty()) {
    if (digits.empty() && watched)
      s += "}";
    s += "_{";
    if (!digits.empty()) {
      s += digits;
      if (watched)
        s += "}";
    }
    else if (!padding.empty())
      s += "\\phantom{" + padding + "}";
    s += "}";
  }

  if (lit_false(lit) || lit_true(lit))
    s += "}";
  return s;

}

static const unsigned MAX_LITS_PER_LINE = 3;
std::string SentinelState::to_latex(Tclause cl) const
{
  if (cl == CLAUSE_UNDEF)
    return "decision";
  if (cl == CLAUSE_ROOT)
    return "root";
  if (cl == CLAUSE_LAZY)
    return "lazy";
  if (cl == CLAUSE_ASSUMPTION)
    return "assumption";

  const clause& c = _clauses[cl];
  std::string s = "$";
  bool printed = false;

  auto is_watched = [&](Tlit lit) {
    for (const auto& watch : c.watches)
      if (watch.first == lit)
        return true;
    return false;
  };

  // If there are some watched literals, print those first
  if (!c.watches.empty()) {
    assert(c.watches.size() == 2);
    std::pair<Tlit, Tlit> first_watched = c.watches.front();
    std::pair<Tlit, Tlit> second_watched = c.watches.back();
    if (lit_false(first_watched.first))
      std::swap(first_watched, second_watched);
    s += to_latex(first_watched.first);
    s += " \\lor ";
    s += to_latex(second_watched.first);
    printed = true;
  }

  for (unsigned i = 0; i < c.literals.size(); i++) {
    Tlit lit = c.literals[i];
    // skip the watched literals, already printed above
    if (is_watched(lit))
      continue;
    if (printed)
      s += " \\lor ";
    if (i == MAX_LITS_PER_LINE - 1 && i < c.literals.size() - 1) {
      s += "\\red{\\dots}";
      break;
    }
    s += to_latex(lit);
    printed = true;
  }
  s += "$";
  return s;
}

std::string SentinelState::clause_to_aligned_latex(Tclause cl) const
{
  if (cl == CLAUSE_UNDEF)
    return "decision";
  if (cl == CLAUSE_ROOT)
    return "root";
  if (cl == CLAUSE_LAZY)
    return "lazy";
  if (cl == CLAUSE_ASSUMPTION)
    return "assumption";

  const clause& c = _clauses[cl];
  assert(c.watches.size() == 0 || c.watches.size() == 2);
  std::string s = "";
  bool printed = false;

  auto is_watched = [&](Tlit lit) {
    for (const auto& watch : c.watches)
      if (watch.first == lit)
        return true;
    return false;
  };

  if (!c.watches.empty()) {
    std::pair<Tlit, Tlit> first_watched = c.watches.front();
    std::pair<Tlit, Tlit> second_watched = c.watches.back();
    if (lit_false(first_watched.first))
      std::swap(first_watched, second_watched);
    s += literal_to_aligned_latex(first_watched.first, true);
    s += " \\lor ";
    s += literal_to_aligned_latex(second_watched.first, true);
    printed = true;
  }

  for (Tlit lit : c.literals) {
    if (is_watched(lit))
      continue;
    if (printed)
      s += " \\lor ";
    s += literal_to_aligned_latex(lit, false);
    printed = true;
  }
  return s;
}

static inline std::string pair_to_latex(int a, double b)
{
  return "(" + std::to_string(a) + ", " + std::to_string(b) + ")";
}

std::string SentinelState::trail_to_latex() const
{
  double spacing = 0.75;
  std::string s = "";

  unsigned x = 0;
  Tlevel y = 0;

  // number of trail literals already dequeued by unit propagation, i.e. the position of the
  // propagation queue head on the trail.
  unsigned n_propagated = 0;
  while (n_propagated < _trail.size() && propagated(_trail[n_propagated]))
    n_propagated++;

  // print the literals
  for (Tlit lit : _trail) {
    Tlevel lvl = level(lit);
    s += "\\draw[thick] (" + std::to_string(x) + ", 0) -- node[below, yshift = -0.1cm] {";
    s += "\\rotatebox{270}{";
    s += to_latex(reason(lit));
    s += "}} (" + std::to_string(x + 1) + ", 0);\n";
    if (y != lvl) {
      s += "\\draw[thick] " + pair_to_latex(x, y.value * spacing) + " -- " + pair_to_latex(x, lvl.value * spacing) + ";\n";
      y = lvl;
    }
    s += "\\draw[thick] " + pair_to_latex(x, y.value * spacing) + " -- ";
    if (decision(lit))
      s += "node[below] {$\\delta = " + lvl.to_string() + "$} ";
    s += "node[above] {$";
    s += to_latex(lit);
    s += "$} " + pair_to_latex(x + 1, y.value * spacing) + ";\n";
    x++;
  }

  // print the conflicts
  for (Tclause cl = 0; cl.value < _clauses.size(); cl++) {
    if (!active(cl) || !conflicting(cl))
      continue;
    Tlevel lvl = 0;
    for (Tlit lit : literals(cl)) {
      if (level(lit) > lvl)
        lvl = level(lit);
    }
    // print in red
    s += "\\draw[thick, red] (" + std::to_string(x) + ", 0) -- node[below, yshift = -0.1cm] {";
    s += "\\rotatebox{270}{";
    s += to_latex(cl);
    s += "}} (" + std::to_string(x + 1) + ", 0);\n";
    if (y != lvl) {
      s += "\\draw[thick, red] " + pair_to_latex(x, y.value * spacing) + " -- " + pair_to_latex(x, lvl.value * spacing) + ";\n";
      y = lvl;
    }
    s += "\\draw[thick, red] " + pair_to_latex(x, y.value * spacing) + " -- ";
    s += "node[above, red] {$\\bot$} " + pair_to_latex(x + 1, y.value * spacing) + ";\n";
    x++;
  }

  s += "\n\\draw[thick, dotted] ";
  s += pair_to_latex(n_propagated, -3) + " -- ";
  s += pair_to_latex(n_propagated, (level().value + 1) * spacing + 0.5);
  s += " node[right, yshift=-0.2cm] {$\\q \\rightarrow$}";
  s += " node[left, yshift=-0.2cm] {$\\leftarrow \\trail$};\n\n";

  // print the ticks
  if (x > 0) {
    s += "\\foreach \\x in {0,1,...," + std::to_string(x) + "}\n";
    s += "  \\draw[thick] (\\x,3pt)--(\\x,-3pt);\n";
  }
  else {
    s += "\\draw[thick] (0,3pt)--(0,-3pt);\n";
  }
  return s;
}

std::string SentinelState::clause_set_to_latex() const
{
  std::string s = "\\begin{tabular}{l}\n";
  for (Tclause cl = 0; cl.value < _clauses.size(); cl++) {
    if (!active(cl))
      continue;
    s += "  $C_{" + std::to_string(cl.value + 1);
    s += latex_index_padding(cl.value, _clauses.size() - 1);
    s += "} = ";
    s += clause_to_aligned_latex(cl) + "$\\\\\n";
  }
  s += "\\end{tabular}\n";
  return s;
}

std::string SentinelState::used_clauses_to_latex() const
{
  std::string s = "\\begin{tabular}{l}\n";
  for (Tclause cl = 0; cl.value < _clauses.size(); cl++) {
    if (!active(cl))
      continue;
    if (!conflicting(cl) && !implying(cl))
      continue;
    s += "  $C_{" + std::to_string(cl.value + 1);
    s += latex_index_padding(cl.value, _clauses.size() - 1);
    s += "} = ";
    s += clause_to_aligned_latex(cl) + "$\\\\\n";
  }
  s += "\\end{tabular}\n";
  return s;
}

std::string SentinelState::implication_graph_to_latex() const
{
  std::string s = "";
  s += "\\tikzstyle{vertex}=[draw,minimum size=24pt,inner sep=1pt]\n";
  s += "\\tikzstyle{propagated}=[circle]\n";
  s += "\\tikzstyle{decision}=[rectangle]\n";
  s += "\\tikzstyle{myarr}=[shorten >=1pt,->,>=stealth]\n";
  s += "\\tikzstyle{currentclause}=[fill=blue!15]\n";

  for (Tlevel lvl = 0; lvl <= level(); lvl++) {
    unsigned x = 0;
    for (Tlit lit : _trail) {
      if (level(lit) != lvl)
        continue;
      s += "\\node[vertex";
      s += decision(lit) ? ", decision]" : ", propagated]";
      s += "(v" + std::to_string(lit.var().value) + ") at (";
      s += std::to_string(x) + ", " + std::to_string(-2 * (int) lvl.value) + ") {$";
      s += to_latex(lit);
      s += "$};\n";
      x += 2;
    }
  }
  s += "\n";

  // Draw an edge from every literal that justifies lit's antecedent clause to lit itself.
  // Decisions and literals with no recorded antecedent (root/lazy/assumption) have nothing to
  // draw edges from.
  for (unsigned i = 1; i < _trail.size(); i++) {
    Tlit lit = _trail[i];
    if (!justified(lit))
      continue;
    Tclause cl = reason(lit);
    for (Tlit lit2 : literals(cl)) {
      if (lit2 == lit)
        continue;
      if (level(lit2) != level(lit) || lit2.var() == _trail[i - 1].var())
        s += "\\draw (v" + std::to_string(lit2.var().value) + ") edge[myarr] (v" + std::to_string(lit.var().value) + ");";
      else
        s += "\\draw (v" + std::to_string(lit2.var().value) + ") edge[myarr, bend right=30] (v" + std::to_string(lit.var().value) + ");";
      s += "\n";
    }
    s += "\n";
  }
  return s;
}
}
