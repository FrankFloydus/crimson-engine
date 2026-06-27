////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <string>
#include <utility>

namespace quader {

/**
 * Stores the Command Result data contract used by the shared core service layer.
 */
struct CommandResult {
    bool ok = false;
    bool changed = false;
    std::string message;
};

inline CommandResult commandChangedResult() {
    return {
            .ok = true,
            .changed = true,
    };
}

inline CommandResult commandUnchangedResult() {
    return {
            .ok = true,
            .changed = false,
    };
}

inline CommandResult commandInvalidResult(std::string message) {
    return {
            .ok = false,
            .changed = false,
            .message = std::move(message),
    };
}

using UndoRedoResult = CommandResult;

} // namespace quader
