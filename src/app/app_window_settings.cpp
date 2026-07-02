////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "app/app_window_settings.hpp"

#include "core/qstr.hpp"
#include "core/user_config_paths.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>

namespace quader {
namespace {

std::optional<int> parse_int_token(std::string_view value) {
  const std::string trimmed = QStr::trim_ascii_whitespace(value);
  if (trimmed.empty()) {
    return std::nullopt;
  }

  int parsed = 0;
  const char *begin = trimmed.data();
  const char *end = begin + trimmed.size();
  const std::from_chars_result result = std::from_chars(begin, end, parsed);
  if (result.ec != std::errc{} || result.ptr != end) {
    return std::nullopt;
  }
  return parsed;
}

std::optional<bool> parse_bool_token(std::string_view value) {
  return QStr::parse_bool_token(value);
}

} // namespace

AppWindowSettings AppWindowSettings::defaults() {
    return {};
}

AppWindowSettings AppWindowSettings::load(const std::filesystem::path& path) {
    AppWindowSettings settings = defaults();
    std::ifstream file(path);
    if (!file.is_open()) {
        return settings;
    }

    std::optional<int> normal_x;
    std::optional<int> normal_y;
    std::optional<int> normal_width;
    std::optional<int> normal_height;
    bool maximized = false;

    std::string line;
    while (std::getline(file, line)) {
      line = QStr::trim_ascii_whitespace(line);
      if (line.empty() || line.front() == '#' || line.front() == '[') {
        continue;
      }

      const std::optional<QStrSplitOnce> split = QStr::split_once(line, '=');
      if (!split) {
        continue;
      }

      const std::string key = QStr::trim_ascii_whitespace(split->before);
      const std::string value = QStr::trim_ascii_whitespace(split->after);
      if (key == "NormalX") {
        normal_x = parse_int_token(value);
      } else if (key == "NormalY") {
        normal_y = parse_int_token(value);
      } else if (key == "NormalWidth") {
        normal_width = parse_int_token(value);
      } else if (key == "NormalHeight") {
        normal_height = parse_int_token(value);
      } else if (key == "Maximized") {
        maximized = parse_bool_token(value).value_or(maximized);
      }
    }

    if (normal_x && normal_y && normal_width && normal_height) {
      settings.set_placement({
          .normal_frame =
              {
                  .x = *normal_x,
                  .y = *normal_y,
                  .width = *normal_width,
                  .height = *normal_height,
              },
          .maximized = maximized,
      });
    }

    return settings;
}

std::filesystem::path
AppWindowSettings::resolve_path(const std::filesystem::path &executable_dir) {
  return quader_user_config_directory(executable_dir) / "window_settings.ini";
}

bool AppWindowSettings::save(const std::filesystem::path& path) const {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);

    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }

    file << "[Window]\n";
    if (placement_) {
      file << "NormalX=" << placement_->normal_frame.x << '\n';
      file << "NormalY=" << placement_->normal_frame.y << '\n';
      file << "NormalWidth=" << placement_->normal_frame.width << '\n';
      file << "NormalHeight=" << placement_->normal_frame.height << '\n';
      file << "Maximized=" << (placement_->maximized ? "true" : "false")
           << '\n';
    }
    return true;
}

void AppWindowSettings::set_placement(PlatformWindowPlacement placement) {
  placement.normal_frame.width =
      std::max(kPlatformWindowMinimumWidth, placement.normal_frame.width);
  placement.normal_frame.height =
      std::max(kPlatformWindowMinimumHeight, placement.normal_frame.height);
  placement_ = placement;
}

} // namespace quader
