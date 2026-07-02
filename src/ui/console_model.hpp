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
 * Represents a Console Filter value used by the ImGui editor presentation layer.
 */
struct ConsoleFilter {
  spdlog::level::level_enum minimum_level = spdlog::level::trace;
  std::string search_text;
};

/**
 * Owns Console Model behavior within the ImGui editor presentation layer.
 */
class ConsoleModel {
public:
    static std::vector<LogEntry> filter(const std::vector<LogEntry>& entries, const ConsoleFilter& filter);
};

} // namespace quader
