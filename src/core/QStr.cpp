////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "core/QStr.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cctype>

namespace quader {
namespace {

bool isAsciiWhitespace(unsigned char value) {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

} // namespace

std::string QStr::trimAsciiWhitespace(std::string_view value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c) {
        return isAsciiWhitespace(c);
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) {
        return isAsciiWhitespace(c);
    }).base();
    if (first >= last) {
        return {};
    }
    return {first, last};
}

std::string QStr::toAsciiLower(std::string_view value) {
    std::string lowered(value);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return lowered;
}

bool QStr::equalsAsciiIgnoreCase(std::string_view left, std::string_view right) {
    return toAsciiLower(left) == toAsciiLower(right);
}

std::optional<bool> QStr::parseBoolToken(std::string_view value) {
    const std::string lowered = toAsciiLower(trimAsciiWhitespace(value));
    if (lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on") {
        return true;
    }
    if (lowered == "0" || lowered == "false" || lowered == "no" || lowered == "off") {
        return false;
    }
    return std::nullopt;
}

std::optional<float> QStr::parseFiniteFloatToken(std::string_view value) {
    const std::string trimmed = trimAsciiWhitespace(value);
    if (trimmed.empty()) {
        return std::nullopt;
    }

    float parsed = 0.0f;
    const char* begin = trimmed.data();
    const char* end = begin + trimmed.size();
    const std::from_chars_result result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end || !std::isfinite(parsed)) {
        return std::nullopt;
    }
    return parsed;
}

std::optional<QStrSplitOnce> QStr::splitOnce(std::string_view value, char delimiter) {
    const std::size_t position = value.find(delimiter);
    if (position == std::string_view::npos) {
        return std::nullopt;
    }
    return QStrSplitOnce{
            .before = value.substr(0, position),
            .after = value.substr(position + 1U),
    };
}

std::vector<std::string_view> QStr::splitLines(std::string_view value) {
    std::vector<std::string_view> lines;
    std::size_t start = 0U;
    while (start <= value.size()) {
        const std::size_t end = value.find('\n', start);
        if (end == std::string_view::npos) {
            lines.push_back(value.substr(start));
            break;
        }
        lines.push_back(value.substr(start, end - start));
        start = end + 1U;
    }
    return lines;
}

} // namespace quader
