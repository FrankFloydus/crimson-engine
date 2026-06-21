#pragma once

#include <string>
#include <utility>

namespace quader {

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
