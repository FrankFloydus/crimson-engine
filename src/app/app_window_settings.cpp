////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "app/app_window_settings.hpp"

#include "core/q_str.hpp"
#include "core/user_config_paths.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>

namespace quader {
namespace {

std::optional<int> parseIntToken(std::string_view value) {
    const std::string trimmed = QStr::trimAsciiWhitespace(value);
    if (trimmed.empty()) {
        return std::nullopt;
    }

    int parsed = 0;
    const char* begin = trimmed.data();
    const char* end = begin + trimmed.size();
    const std::from_chars_result result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::nullopt;
    }
    return parsed;
}

std::optional<bool> parseBoolToken(std::string_view value) {
    return QStr::parseBoolToken(value);
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

    std::optional<int> normalX;
    std::optional<int> normalY;
    std::optional<int> normalWidth;
    std::optional<int> normalHeight;
    bool maximized = false;

    std::string line;
    while (std::getline(file, line)) {
        line = QStr::trimAsciiWhitespace(line);
        if (line.empty() || line.front() == '#' || line.front() == '[') {
            continue;
        }

        const std::optional<QStrSplitOnce> split = QStr::splitOnce(line, '=');
        if (!split) {
            continue;
        }

        const std::string key = QStr::trimAsciiWhitespace(split->before);
        const std::string value = QStr::trimAsciiWhitespace(split->after);
        if (key == "NormalX") {
            normalX = parseIntToken(value);
        } else if (key == "NormalY") {
            normalY = parseIntToken(value);
        } else if (key == "NormalWidth") {
            normalWidth = parseIntToken(value);
        } else if (key == "NormalHeight") {
            normalHeight = parseIntToken(value);
        } else if (key == "Maximized") {
            maximized = parseBoolToken(value).value_or(maximized);
        }
    }

    if (normalX && normalY && normalWidth && normalHeight) {
        settings.setPlacement({
                .normalFrame = {
                        .x = *normalX,
                        .y = *normalY,
                        .width = *normalWidth,
                        .height = *normalHeight,
                },
                .maximized = maximized,
        });
    }

    return settings;
}

std::filesystem::path AppWindowSettings::resolvePath(const std::filesystem::path& executableDir) {
    return quaderUserConfigDirectory(executableDir) / "window_settings.ini";
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
        file << "NormalX=" << placement_->normalFrame.x << '\n';
        file << "NormalY=" << placement_->normalFrame.y << '\n';
        file << "NormalWidth=" << placement_->normalFrame.width << '\n';
        file << "NormalHeight=" << placement_->normalFrame.height << '\n';
        file << "Maximized=" << (placement_->maximized ? "true" : "false") << '\n';
    }
    return true;
}

void AppWindowSettings::setPlacement(PlatformWindowPlacement placement) {
    placement.normalFrame.width = std::max(kPlatformWindowMinimumWidth, placement.normalFrame.width);
    placement.normalFrame.height = std::max(kPlatformWindowMinimumHeight, placement.normalFrame.height);
    placement_ = placement;
}

} // namespace quader
