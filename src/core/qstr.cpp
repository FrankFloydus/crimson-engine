////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "core/qstr.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cctype>

namespace quader {
namespace {

bool is_ascii_whitespace(unsigned char value) {
  return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

} // namespace

std::string QStr::trim_ascii_whitespace(std::string_view value) {
  const auto first =
      std::find_if_not(value.begin(), value.end(),
                       [](unsigned char c) { return is_ascii_whitespace(c); });
  const auto last =
      std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) {
        return is_ascii_whitespace(c);
      }).base();
  if (first >= last) {
    return {};
  }
  return {first, last};
}

std::string QStr::to_ascii_lower(std::string_view value) {
  std::string lowered(value);
  std::transform(
      lowered.begin(), lowered.end(), lowered.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return lowered;
}

bool QStr::equals_ascii_ignore_case(std::string_view left,
                                    std::string_view right) {
  return to_ascii_lower(left) == to_ascii_lower(right);
}

std::optional<bool> QStr::parse_bool_token(std::string_view value) {
  const std::string lowered = to_ascii_lower(trim_ascii_whitespace(value));
  if (lowered == "1" || lowered == "true" || lowered == "yes" ||
      lowered == "on") {
    return true;
  }
  if (lowered == "0" || lowered == "false" || lowered == "no" ||
      lowered == "off") {
    return false;
  }
  return std::nullopt;
}

std::optional<float> QStr::parse_finite_float_token(std::string_view value) {
  const std::string trimmed = trim_ascii_whitespace(value);
  if (trimmed.empty()) {
    return std::nullopt;
  }

  float parsed = 0.0f;
  const char *begin = trimmed.data();
  const char *end = begin + trimmed.size();
  const std::from_chars_result result = std::from_chars(begin, end, parsed);
  if (result.ec != std::errc{} || result.ptr != end || !std::isfinite(parsed)) {
    return std::nullopt;
  }
  return parsed;
}

std::optional<QStrSplitOnce> QStr::split_once(std::string_view value,
                                              char delimiter) {
  const std::size_t position = value.find(delimiter);
  if (position == std::string_view::npos) {
    return std::nullopt;
  }
  return QStrSplitOnce{
      .before = value.substr(0, position),
      .after = value.substr(position + 1U),
  };
}

std::vector<std::string_view> QStr::split_lines(std::string_view value) {
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
