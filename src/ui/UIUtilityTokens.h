////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/DebugPaletteView.h"

#include <filesystem>
#include <string>
#include <vector>

namespace quader {

/**
 * Names one flat design-system token that can be expanded into RCSS utility classes.
 */
struct UIUtilityToken {
    std::string name;
    std::string value;
};

std::filesystem::path appUtilityTokenPath(const std::filesystem::path& executableDir);
std::vector<UIUtilityToken> loadUIUtilityTokens(const std::filesystem::path& utilityFile);
std::string buildUIUtilityRcss(
        const std::vector<UIUtilityToken>& tokens,
        const std::vector<DebugPaletteColorToken>& paletteColors);

} // namespace quader
