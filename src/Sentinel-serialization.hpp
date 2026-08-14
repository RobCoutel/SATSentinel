/*
 * This file is part of the source code of the software program
 * SATSentinel. It is protected by applicable copyright laws.
 *
 * This source code is protected by the terms of the MIT License.
 */
/**
 * @file src/Sentinel-serialization.hpp
 * @author Robin Coutelier
 *
 * @brief Live, human-readable recording of the notifications sent to the sentinel. Every recorded
 * line is the exact command text that main.cpp's command parser accepts, so a log file can be fed
 * straight back into the sentinel to reconstruct the state it describes.
 */
#pragma once

#include <fstream>
#include <string>

namespace sentinel::notif
{
  class notification;
}

namespace sentinel
{
  /**
   * @brief Appends notifications to a log file as human-readable commands, flushing after every
   * write so that a crash never loses previously recorded entries.
   */
  class ExecutionLogger
  {
  public:
    ExecutionLogger() = default;
    ~ExecutionLogger() = default;

    /**
     * @brief Opens (truncating) the given file for recording.
     * @return true on success, false if the file could not be opened.
     */
    bool open(const std::string& filename);

    bool is_open() const { return _stream.is_open(); }

    /**
     * @brief Records one notification as its command-text form, then flushes immediately.
     */
    void record(const notif::notification& notification);

  private:
    std::ofstream _stream;
  };
}
