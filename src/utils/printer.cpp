/*
 * This file was mostly copied from the source code of the software program
 * NapSAT. It is protected by applicable copyright laws.
 *
 * This source code is protected by the terms of the MIT License.
 */
/**
 * @file src/utils/printer.cpp
 * @author Robin Coutelier
 * @brief This file is part of the NapSAT solver. It implements functions for string manipulation and pretty printing.
 */
#include "printer.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

using namespace std;

#ifdef __unix__
#include <sys/ioctl.h> //ioctl() and TIOCGWINSZ
#include <sys/wait.h> // waitpid(), for run_addr2line_batch()
#include <unistd.h> // for STDOUT_FILENO, pipe(), ...
#include <fcntl.h> // open(), for run_addr2line_batch()'s /dev/null redirect
#include <spawn.h> // posix_spawnp(), for run_addr2line_batch()
#include <execinfo.h> // backtrace()/backtrace_symbols()
#include <dlfcn.h> // dladdr()
#include <cxxabi.h> // abi::__cxa_demangle()
#include <map>

extern char** environ;
#endif
#ifdef _WIN32
#include <windows.h>
#endif

namespace sentinel
{

static unsigned TERMINAL_WIDTH = 100;

const char ESC_LOCK_START = "🔒"[0];
const char ESC_INFINITY_START = "∞"[0];

void update_terminal_width() {
#ifdef __unix__
  struct winsize size;
  ioctl(STDOUT_FILENO, TIOCGWINSZ, &size);
  short int width = size.ws_col;
  if (width > 0)
    TERMINAL_WIDTH = size.ws_col;
#endif
#ifdef _WIN32
  CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    TERMINAL_WIDTH = csbi.srWindow.Right - csbi.srWindow.Left + 1;
#endif
}

unsigned get_terminal_width()
{
  return TERMINAL_WIDTH;
}

unsigned string_length_escaped(string const str)
{
  unsigned n_escaped = 0;
  bool escaping = false;
  for (size_t i = 0; i < str.length(); i++) {
    char c = str[i];
    escaping |= c == ESC_CHAR;
    n_escaped += escaping;
    escaping &= c != ESC_END;

    if (c == ESC_LOCK_START && str.substr(i, 4) == "🔒") {
      n_escaped += 2; // the lock emoji is 4 bytes in UTF-8 but we want to count it as 1 character
    }
    if (c == ESC_INFINITY_START && str.substr(i, 3) == "∞") {
      n_escaped += 2; // the infinity emoji is 3 bytes in UTF-8 but we want to count it as 1 character
    }


  }
  return str.length() - n_escaped;
}

static inline unsigned log10(int n)
{
  assert(n > 0);
  unsigned digits = 0;
  while (n > 0) {
    n /= 10;
    digits++;
  }
  return digits;
}

string pad(unsigned n, unsigned max_int)
{
  n = max(n, 1u);
  int max_digits = log10(max_int);
  int digits = log10(n);
  string s = "";
  for (int i = digits; i < max_digits; i++)
    s += " ";
  return s;
}

string pretty_integer(long long n)
{
  string s = "";
  if (n == 0) return "0";
  while (n > 0) {
    s = to_string(n % 1000) + "," + s;
    n /= 1000;
    if (s.size() % 4 != 0 && n > 0)
      s = string(4 - s.size() % 4, '0') + s;
  }
  if (s.size() > 0)
    s = s.substr(0, s.size() - 1);
  return s;
}

string pretty_float(double f, unsigned n)
{
  string s = pretty_integer((long long)f);
  if (n)
    s += ".";
  while (n--) {
    f *= 10;
    s += to_string((long long)f % 10);
  }
  return s;
}

string pretty_time(chrono::microseconds time)
{
  string str = "";
  const long long ms = time.count() / 1000;
  const long long hours = ms / 3600000;
  const long long minutes = (ms % 3600000) / 60000;
  const long long seconds = (ms % 60000) / 1000;
  const long long microseconds = ms % 1000;
  if (hours > 0)
    str += to_string(hours) + "h ";
  if (minutes > 0)
    str += to_string(minutes) + "m ";
  if (seconds > 0)
    str += to_string(seconds) + "s ";
  str += to_string(microseconds) + "ms";
  return str;
}

std::string justify_string(const std::string& str, unsigned width, char fill, const std::string& prefix)
{
  assert(width > prefix.length());
  width -= prefix.length();
  string justified_str = "";
  // separate the string into words
  vector<string> words;
  string word = "";
  for (char c : str) {
    if (c == ' ' || c == '\n') {
      if (c == '\n') {
        word += c;
      }
      if (!word.empty()) {
        words.push_back(word);
        word = "";
      }
    } else {
      word += c;
    }
  }
  if (!word.empty()) {
    words.push_back(word);
  }

  // reverse the words to process them in reverse order
  std::reverse(words.begin(), words.end());

  // check how many words can fit in the given width
  while (!words.empty()) {
    unsigned line_length = 0;
    vector<string> line_words;
    bool eol = false;
    do  {
      line_length += words.back().length() + (line_words.empty() ? 0 : 1);
      line_words.push_back(words.back());
      words.pop_back();
      string& last_word = line_words.back();
      eol = last_word.back() == '\n' || words.empty();
    } while (!eol && line_length + words.back().length() < width);

    unsigned extra_spaces = width - line_length;

    // if this is the last line, or there is a line break, don't justify it
    if (words.empty() || eol || line_words.size() < extra_spaces) {
      extra_spaces = 0;
    }

    if (extra_spaces > 0) {
      // we might have some extra spaces to fill. Fill the line
      // 1. If the word ends with a punctuation mark, we don't add extra spaces after it.
      // 2. Pick the longest words first to add extra spaces after them.
      std::vector<unsigned> line_words_sorted;
      for (unsigned i = 0; i < line_words.size() - 1; i++) {
        // we cannot add a space after the last word, so we skip it
        line_words_sorted.push_back(i);
      }
      std::sort(line_words_sorted.begin(), line_words_sorted.end(), [&line_words](unsigned a, unsigned b) {
        bool a_punct = ispunct(line_words[a].back());
        bool b_punct = ispunct(line_words[b].back());
        if (a_punct && !b_punct) return false;
        if (!a_punct && b_punct) return true;
        return line_words[a].length() > line_words[b].length();
      });

      for (unsigned w_idx : line_words_sorted) {
        if (extra_spaces == 0)
          break;
        string& w = line_words[w_idx];
        w += fill;
        extra_spaces--;
      }
    }

    justified_str += prefix;
    for (size_t i = 0; i < line_words.size(); i++) {
      if (i)
        justified_str += " ";
      justified_str += line_words[i];
    }
    if (!eol)
      justified_str += "\n";
  }
  return justified_str;
}

// Only used (and only compiled) in debug builds - see the NDEBUG check in capture_backtrace()
// below - so it doesn't trip -Wunused-function in a release build.
#if defined(__unix__) && !defined(NDEBUG)
namespace
{
  // Resolves a whole batch of addresses within a single module in one `addr2line -f -C -e
  // <module> <addr>...` invocation, with no shell involved (posix_spawnp(), not system()/popen(),
  // so the module path or an address never gets shell-interpreted).
  //
  // This exists instead of one addr2line call per address for two reasons, both of which matter
  // once this runs inside a real solver process rather than a small test binary:
  //  - addr2line reloads and re-parses the module's entire DWARF line-number program from scratch
  //    on every invocation. A depth-N backtrace calling it N times pays that parse cost N times
  //    instead of once; batching collapses it to one call per distinct module (almost always a
  //    single call, since all frames of interest are normally in the same executable).
  //  - spawning is done via posix_spawnp(), not fork(). Plain fork() duplicates the caller's page
  //    tables, which costs time proportional to the size of the process's address space; for a
  //    solver holding a multi-GB clause database, doing that repeatedly (once per frame) is both
  //    slow and prone to tripping the kernel's memory-overcommit accounting - observed in practice
  //    as sporadic crashes/OOM kills, not just slowness. posix_spawnp() avoids that page-table copy.
  //
  // Returns one file:line string per address in rel_addrs, in the same order ("" for any address
  // that couldn't be resolved - no DWARF line info, i.e. a release build; the tool missing; or an
  // address with no matching line).
  vector<string> run_addr2line_batch(const string& module, const vector<uintptr_t>& rel_addrs)
  {
    vector<string> results(rel_addrs.size());
    if (rel_addrs.empty())
      return results;

    vector<string> addr_strs(rel_addrs.size());
    for (size_t i = 0; i < rel_addrs.size(); i++) {
      char buf[2 + sizeof(uintptr_t) * 2 + 1]; // "0x" + hex digits + '\0'
      snprintf(buf, sizeof(buf), "0x%lx", (unsigned long)rel_addrs[i]);
      addr_strs[i] = buf;
    }

    int out_pipe[2];
    if (pipe(out_pipe) != 0)
      return results;
    int devnull = open("/dev/null", O_WRONLY);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, out_pipe[1], STDOUT_FILENO);
    if (devnull >= 0)
      posix_spawn_file_actions_adddup2(&actions, devnull, STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, out_pipe[0]);
    posix_spawn_file_actions_addclose(&actions, out_pipe[1]);
    if (devnull >= 0)
      posix_spawn_file_actions_addclose(&actions, devnull);

    vector<char*> argv;
    argv.push_back((char*)"addr2line");
    argv.push_back((char*)"-f");
    argv.push_back((char*)"-C");
    argv.push_back((char*)"-e");
    argv.push_back((char*)module.c_str());
    for (auto& s : addr_strs)
      argv.push_back((char*)s.c_str());
    argv.push_back(nullptr);

    pid_t pid = 0;
    int rc = posix_spawnp(&pid, "addr2line", &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(out_pipe[1]);
    if (devnull >= 0)
      close(devnull);
    if (rc != 0) {
      close(out_pipe[0]);
      return results;
    }

    string output;
    char buf[4096];
    ssize_t n;
    while ((n = read(out_pipe[0], buf, sizeof(buf))) > 0)
      output.append(buf, (size_t)n);
    close(out_pipe[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
      return results;

    // Output is two lines per address: the (demangled) function name (unused - we already have
    // it via dladdr()/__cxa_demangle() above), then "file:line", in the same order as argv.
    size_t pos = 0;
    for (size_t i = 0; i < rel_addrs.size(); i++) {
      size_t nl1 = output.find('\n', pos);
      if (nl1 == string::npos)
        break;
      size_t nl2 = output.find('\n', nl1 + 1);
      string file_line = (nl2 == string::npos) ? output.substr(nl1 + 1) : output.substr(nl1 + 1, nl2 - nl1 - 1);
      if (!file_line.empty() && file_line.compare(0, 2, "??") != 0)
        results[i] = file_line;
      pos = (nl2 == string::npos) ? output.size() : nl2 + 1;
    }
    return results;
  }
}
#endif

std::string capture_backtrace(unsigned max_frames, unsigned skip_frames)
{
#ifdef __unix__
  // +1: this function's own frame is always skipped, on top of the caller-requested skip_frames.
  std::vector<void*> frames(max_frames + skip_frames + 1);
  int n_frames = backtrace(frames.data(), (int)frames.size());
  int first = std::min((int)(skip_frames + 1), n_frames);

  std::vector<string> lines(n_frames > first ? n_frames - first : 0);
  // Per-line index -> relative address still needing " at file:line", grouped by module so each
  // module needs only one addr2line invocation for the whole backtrace (see run_addr2line_batch).
#ifndef NDEBUG
  std::map<string, std::vector<std::pair<uintptr_t, size_t>>> pending_by_module;
#endif

  for (int i = first; i < n_frames; i++) {
    void* addr = frames[i];
    size_t line_idx = i - first;
    Dl_info info{};
    bool have_info = dladdr(addr, &info) != 0 && info.dli_fname != nullptr;

    string module = have_info ? info.dli_fname : "??";
    string func = "??";
    if (have_info && info.dli_sname) {
      int status = 0;
      char* demangled = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status);
      func = (status == 0 && demangled) ? demangled : info.dli_sname;
      free(demangled);
      if (info.dli_saddr) {
        uintptr_t offset = (uintptr_t)addr - (uintptr_t)info.dli_saddr;
        if (offset != 0) {
          char off_buf[3 + sizeof(uintptr_t) * 2 + 1]; // "+0x" + hex digits + '\0'
          snprintf(off_buf, sizeof(off_buf), "+0x%lx", (unsigned long)offset);
          func += off_buf;
        }
      }
    }

    lines[line_idx] = "#" + to_string(line_idx) + " " + module + "(" + func + ") [" + [&] {
      char addr_buf[2 + sizeof(uintptr_t) * 2 + 1];
      snprintf(addr_buf, sizeof(addr_buf), "0x%lx", (unsigned long)(uintptr_t)addr);
      return string(addr_buf);
    }() + "]";

    // Only meaningful for debug builds (NDEBUG undefined, e.g. `make BUILD_MODE=debug`), which
    // embed the DWARF line info addr2line needs; a release build (-DNDEBUG, no -g) has none.
#ifndef NDEBUG
    if (have_info) {
      uintptr_t rel_addr = (uintptr_t)addr - (uintptr_t)info.dli_fbase;
      pending_by_module[module].push_back({rel_addr, line_idx});
    }
#endif
  }

#ifndef NDEBUG
  for (const auto& [module, entries] : pending_by_module) {
    std::vector<uintptr_t> addrs(entries.size());
    for (size_t k = 0; k < entries.size(); k++)
      addrs[k] = entries[k].first;
    std::vector<string> locs = run_addr2line_batch(module, addrs);
    for (size_t k = 0; k < entries.size(); k++)
      if (!locs[k].empty())
        lines[entries[k].second] += " at " + locs[k];
  }
#endif

  string result;
  for (auto& line : lines)
    result += line + "\n";
  return result;
#else
  return "";
#endif
}

std::string condense_backtrace(const std::string& trace)
{
  string result;
  size_t line_start = 0;
  while (line_start <= trace.size()) {
    size_t line_end = trace.find('\n', line_start);
    bool has_newline = line_end != string::npos;
    string line = trace.substr(line_start, has_newline ? line_end - line_start : string::npos);

    // capture_backtrace() appends " at <file>:<line>" as the last thing on a resolved line, so
    // rfind() lands on that marker even if a demangled signature happens to contain "at" (e.g.
    // as part of a type name) earlier in the line.
    size_t at_pos = line.rfind(" at ");
    if (at_pos != string::npos)
      line = line.substr(at_pos + 1); // + 1: drop the leading space, keep "at ..." itself

    result += line;
    if (!has_newline)
      break;
    result += "\n";
    line_start = line_end + 1;
  }
  return result;
}
}
