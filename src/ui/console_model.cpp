////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/console_model.hpp"

#include "core/qstr.hpp"

namespace quader {

ConsoleLevelMask
ConsoleModel::level_mask(spdlog::level::level_enum level) noexcept {
  switch (level) {
  case spdlog::level::trace:
    return kConsoleTraceLevel;
  case spdlog::level::debug:
    return kConsoleDebugLevel;
  case spdlog::level::info:
    return kConsoleInfoLevel;
  case spdlog::level::warn:
    return kConsoleWarningLevel;
  case spdlog::level::err:
  case spdlog::level::critical:
    return kConsoleErrorLevel;
  default:
    return 0u;
  }
}

bool ConsoleModel::accepts(const LogEntry &entry, const ConsoleFilter &filter) {
  const std::string search = QStr::to_ascii_lower(filter.search_text);
  return accepts_normalized(entry, filter.level_mask, search);
}

bool ConsoleModel::accepts_normalized(const LogEntry &entry,
                                      ConsoleLevelMask selected_levels,
                                      std::string_view normalized_search) {
  return (level_mask(entry.level) & selected_levels) != 0u &&
         (normalized_search.empty() ||
          QStr::to_ascii_lower(entry.message).find(normalized_search) !=
              std::string::npos);
}

std::vector<LogEntry> ConsoleModel::filter(const std::vector<LogEntry> &entries,
                                           const ConsoleFilter &filter) {
  std::vector<LogEntry> result;
  const std::string search = QStr::to_ascii_lower(filter.search_text);

  for (const LogEntry &entry : entries) {
    if (accepts_normalized(entry, filter.level_mask, search)) {
      result.push_back(entry);
    }
  }

  return result;
}

} // namespace quader
