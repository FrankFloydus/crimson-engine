////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <string>
#include <vector>

namespace quader {

/**
 * Exposes Quader Events API operations through the public Quader API surface.
 */
class QuaderEventsApi final {
public:
    [[nodiscard]] bool available() const { return false; }
    [[nodiscard]] std::vector<std::string> eventNames() const { return {}; }
};

} // namespace quader
