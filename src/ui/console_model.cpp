////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/console_model.hpp"

#include "core/qstr.hpp"

namespace quader {

std::vector<LogEntry> ConsoleModel::filter(const std::vector<LogEntry> &entries,
                                           const ConsoleFilter &filter) {
  std::vector<LogEntry> result;
  const std::string search = QStr::to_ascii_lower(filter.search_text);

  for (const LogEntry &entry : entries) {
    if (entry.level < filter.minimum_level) {
      continue;
    }
    if (!search.empty() &&
        QStr::to_ascii_lower(entry.message).find(search) == std::string::npos) {
      continue;
    }
    result.push_back(entry);
  }

  return result;
}

} // namespace quader
