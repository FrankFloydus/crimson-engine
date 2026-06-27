////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace quader {

/**
 * Stores the QStr split-once result data contract used by Quader string token parsing.
 */
struct QStrSplitOnce {
    std::string_view before;
    std::string_view after;
};

/**
 * Owns ASCII string token behavior for Quader flat text files, config values, and search text.
 */
class QStr final {
public:
    QStr() = delete;

    [[nodiscard]] static std::string trimAsciiWhitespace(std::string_view value);
    [[nodiscard]] static std::string toAsciiLower(std::string_view value);
    [[nodiscard]] static bool equalsAsciiIgnoreCase(std::string_view left, std::string_view right);
    [[nodiscard]] static std::optional<bool> parseBoolToken(std::string_view value);
    [[nodiscard]] static std::optional<float> parseFiniteFloatToken(std::string_view value);
    [[nodiscard]] static std::optional<QStrSplitOnce> splitOnce(std::string_view value, char delimiter);
    [[nodiscard]] static std::vector<std::string_view> splitLines(std::string_view value);
};

} // namespace quader
