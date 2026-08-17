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
#include <sys/wait.h> // waitpid(), for run_addr2line()
#include <unistd.h> // for STDOUT_FILENO, fork(), pipe(), ...
#include <fcntl.h> // open(), for run_addr2line()'s /dev/null redirect
#include <execinfo.h> // backtrace()/backtrace_symbols()
#include <dlfcn.h> // dladdr()
#include <cxxabi.h> // abi::__cxa_demangle()
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
  // Runs `addr2line -f -C -e <module> <addr>` with no shell involved (execlp(), not
  // system()/popen(), so a module path or address never gets shell-interpreted) and returns
  // its file:line line of output, or "" if it can't be resolved (no DWARF line info - i.e. a
  // release build - the tool is missing, or the address doesn't map to a known line).
  string run_addr2line(const string& module, uintptr_t rel_addr)
  {
    char addr_buf[2 + sizeof(uintptr_t) * 2 + 1]; // "0x" + hex digits + '\0'
    snprintf(addr_buf, sizeof(addr_buf), "0x%lx", (unsigned long)rel_addr);

    int out_pipe[2];
    if (pipe(out_pipe) != 0)
      return "";

    pid_t pid = fork();
    if (pid < 0) {
      close(out_pipe[0]);
      close(out_pipe[1]);
      return "";
    }
    if (pid == 0) {
      close(out_pipe[0]);
      dup2(out_pipe[1], STDOUT_FILENO);
      int devnull = open("/dev/null", O_WRONLY);
      if (devnull >= 0)
        dup2(devnull, STDERR_FILENO);
      close(out_pipe[1]);
      execlp("addr2line", "addr2line", "-f", "-C", "-e", module.c_str(), addr_buf, (char*)nullptr);
      _exit(127); // exec failed (addr2line not installed, ...)
    }
    close(out_pipe[1]);

    string output;
    char buf[512];
    ssize_t n;
    while ((n = read(out_pipe[0], buf, sizeof(buf))) > 0)
      output.append(buf, (size_t)n);
    close(out_pipe[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
      return "";

    // Output is two lines: the (demangled) function name, then "file:line". We already have
    // the function name via dladdr()/__cxa_demangle() above, so only the second line matters.
    size_t nl = output.find('\n');
    if (nl == string::npos)
      return "";
    string file_line = output.substr(nl + 1);
    size_t nl2 = file_line.find('\n');
    if (nl2 != string::npos)
      file_line = file_line.substr(0, nl2);
    if (file_line.empty() || file_line.compare(0, 2, "??") == 0)
      return "";
    return file_line;
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

  string result;
  for (int i = first; i < n_frames; i++) {
    void* addr = frames[i];
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

    result += "#" + to_string(i - first) + " " + module + "(" + func + ") [" + [&] {
      char addr_buf[2 + sizeof(uintptr_t) * 2 + 1];
      snprintf(addr_buf, sizeof(addr_buf), "0x%lx", (unsigned long)(uintptr_t)addr);
      return string(addr_buf);
    }() + "]";

    // Only meaningful for debug builds (NDEBUG undefined, e.g. `make BUILD_MODE=debug`), which
    // embed the DWARF line info addr2line needs; a release build (-DNDEBUG, no -g) has none.
#ifndef NDEBUG
    if (have_info) {
      uintptr_t rel_addr = (uintptr_t)addr - (uintptr_t)info.dli_fbase;
      string loc = run_addr2line(module, rel_addr);
      if (!loc.empty())
        result += " at " + loc;
    }
#endif
    result += "\n";
  }
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
