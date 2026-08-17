/*
 * This file was mostly copied from the source code of the software program
 * NapSAT. It is protected by applicable copyright laws.
 *
 * This source code is protected by the terms of the MIT License.
 */
/**
 * @file src/utils/printer.hpp
 * @author Robin Coutelier
 * @brief This file is part of the NapSAT solver. It defines functions for string manipulation and pretty printing.
 */
#pragma once

#include <iostream>
#include <string>
#include <chrono>

namespace sentinel
{

const char ESC_CHAR = '\033'; // the decimal code for escape character is 27
const char ESC_END = 'm';

#define ORANGE "\033[0;33m"
#define GREEN "\033[0;32m"
#define RED "\033[0;31m"
#define GRAY "\033[0;37m"
#define UNDERLINE "\033[4m"

#define RESET "\033[0m"

#define COLORBLIND_MODE 1

void update_terminal_width();

unsigned get_terminal_width();

/**
 * @brief Returns the length of a string without the escape characters.
 */
unsigned string_length_escaped(std::string const str);

/**
 * @brief Adds spaces to the left of the number to make it have as many digits as the maximum number of digits in the given range.
 * @param n The number to pad.
 * @param max_int The maximum number in the range.
 * @pre max_int > 0
 * @return A string of spaces such that any n in [0, max_int] will have the same number of digits.
 */
std::string pad(unsigned n, unsigned max_int);


/**
 * @brief Returns a pretty string representation of an integer.
 * @param n The integer to convert.
 * @return A string representation of the integer, with commas every three digits.
 */
std::string pretty_integer(long long n);


/**
 * @brief Returns a pretty string representation of a float.
 * @param n The float to convert.
 * @return A string representation of the float, with commas every three digits.
 */
std::string pretty_float(double f, unsigned n = 2);

/**
 * @brief Returns a string representation of a time in microseconds.
 * @param time The time to convert.
 * @return A string representation of the time.
 */
std::string pretty_time(std::chrono::microseconds time);

/**
 * @brief Justifies a string to a given width by adding fill characters to the right.
 * @param str The string to justify.
 * @param width The width to justify to.
 * @param fill The character to fill with.
 * @return The justified string.
 */
std::string justify_string(const std::string& str, unsigned width, char fill = ' ', const std::string& prefix = "");

/**
 * @brief Captures the C++ call stack of the calling process (Linux/glibc <execinfo.h>).
 * @param max_frames Maximum number of stack frames to capture.
 * @param skip_frames Number of innermost frames to omit, in addition to this function's own
 * frame (which is always omitted): pass 1 (the default) to start the trace at your direct
 * caller, or more to also hide wrapper functions between you and the frame of interest.
 * @return A newline-terminated, "#N <module>(<demangled-function>+0xOFFSET) [0xADDRESS]"-per-line
 * string of the call stack, or an empty string if the stack could not be resolved. Function
 * names are demangled (via __cxa_demangle) whenever a symbol is found. In debug builds (NDEBUG
 * undefined, e.g. `make BUILD_MODE=debug`) each resolvable frame also gets " at file:line",
 * resolved by shelling out to the external `addr2line` tool against that DWARF info - a release
 * build has none, so the suffix is silently omitted there. Link with -rdynamic for symbols to
 * resolve at all (otherwise most frames show only module+address).
 */
std::string capture_backtrace(unsigned max_frames = 64, unsigned skip_frames = 1);

/**
 * @brief Condenses a capture_backtrace() trace down to just the resolved source locations.
 * @details On a line that got a resolved " at <file>:<line>" suffix, drops everything before
 * "at" (frame index, module, mangled/demangled function signature, raw address alike), keeping
 * only "at <file>:<line>". A line with no such suffix (no debug info available for that frame -
 * e.g. a release build, or an address libc/the loader didn't expose) is left untouched, since
 * there's nothing more useful to fall back on.
 * @param trace A string as returned by capture_backtrace().
 */
std::string condense_backtrace(const std::string& trace);

const std::string ERROR_HEAD = "\033[1;31mERROR: \033[0m";
const std::string WARNING_HEAD = "\033[0;33mWARNING: \033[0m";
const std::string INFO_HEAD = "\033[34mINFO: \033[0m";

#define LOG_ERROR(msg)   do { std::cerr << ERROR_HEAD << msg << std::endl; } while(0)
#define LOG_WARNING(msg) do { std::cout << WARNING_HEAD << msg << std::endl; } while(0)
#define LOG_INFO(msg)    do { std::cout << INFO_HEAD << msg << std::endl; } while(0)
}
