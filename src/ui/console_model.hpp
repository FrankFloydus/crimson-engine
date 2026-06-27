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
#include <vector>

namespace quader {

/**
 * Represents a Console Filter value used by the editor presentation layer.
 */
struct ConsoleFilter {
    spdlog::level::level_enum minimumLevel = spdlog::level::trace;
    std::string searchText;
};

/**
 * Owns Console Model behavior within the editor presentation layer.
 */
class ConsoleModel {
public:
    static std::vector<LogEntry> filter(const std::vector<LogEntry>& entries, const ConsoleFilter& filter);
};

} // namespace quader
