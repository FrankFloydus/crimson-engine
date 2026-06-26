////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/QuaderInputTypes.h"

#include <cstddef>
#include <string>
#include <vector>

namespace quader {

/**
 * Stores a keybinding rule exposed by the public command API.
 */
struct QuaderKeybindingRule {
    std::string targetId;
    std::vector<QuaderInputChord> sequence;
    std::string shortcut;
    std::string when;
    std::string source = "default";
    int weight = 0;
    bool removal = false;
    std::size_t order = 0u;
};

/**
 * Stores keybinding conflict information for rebinding UI and tests.
 */
struct QuaderKeybindingConflict {
    std::string targetId;
    std::string shortcut;
    std::string when;
    std::string source;
};

} // namespace quader
