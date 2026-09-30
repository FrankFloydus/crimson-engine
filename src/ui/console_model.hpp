////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/logger.hpp"

#include <spdlog/common.h>

#include <string>
#include <string_view>
#include <vector>

namespace quader {

using ConsoleLevelMask = std::uint8_t;
inline constexpr ConsoleLevelMask kConsoleTraceLevel = 1u << 0u;
inline constexpr ConsoleLevelMask kConsoleDebugLevel = 1u << 1u;
inline constexpr ConsoleLevelMask kConsoleInfoLevel = 1u << 2u;
inline constexpr ConsoleLevelMask kConsoleWarningLevel = 1u << 3u;
inline constexpr ConsoleLevelMask kConsoleErrorLevel = 1u << 4u;
inline constexpr ConsoleLevelMask kAllConsoleLevels =
    kConsoleTraceLevel | kConsoleDebugLevel | kConsoleInfoLevel |
    kConsoleWarningLevel | kConsoleErrorLevel;

/**
 * Describes the active application-log filter.
 */
struct ConsoleFilter {
  ConsoleLevelMask level_mask = kAllConsoleLevels;
  std::string search_text;
};

/**
 * Owns toolkit-neutral console filtering behavior.
 */
class ConsoleModel {
public:
  [[nodiscard]] static ConsoleLevelMask
  level_mask(spdlog::level::level_enum level) noexcept;
  [[nodiscard]] static bool accepts(const LogEntry &entry,
                                    const ConsoleFilter &filter);
  [[nodiscard]] static bool
  accepts_normalized(const LogEntry &entry, ConsoleLevelMask level_mask,
                     std::string_view normalized_search);
  static std::vector<LogEntry> filter(const std::vector<LogEntry> &entries,
                                      const ConsoleFilter &filter);
};

} // namespace quader
