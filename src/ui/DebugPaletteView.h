////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace quader {

/**
 * Names one app palette color for debug inspection.
 */
struct DebugPaletteColorToken {
    std::string name;
    std::string hex;
};

std::filesystem::path appPaletteTokenPath(const std::filesystem::path& executableDir);
std::vector<DebugPaletteColorToken> loadDebugPaletteColorTokens(const std::filesystem::path& paletteFile);
std::string debugPaletteUtilityClassSuffix(std::string_view tokenName);
std::string buildDebugPalettePageRml(const std::vector<DebugPaletteColorToken>& colors);

} // namespace quader
