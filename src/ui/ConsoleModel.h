#pragma once

#include "core/Logger.h"

#include <spdlog/common.h>

#include <string>
#include <vector>

namespace quader {

struct ConsoleFilter {
    spdlog::level::level_enum minimumLevel = spdlog::level::trace;
    std::string searchText;
};

class ConsoleModel {
public:
    static std::vector<LogEntry> filter(const std::vector<LogEntry>& entries, const ConsoleFilter& filter);
};

} // namespace quader
