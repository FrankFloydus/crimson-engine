#pragma once

#include "core/CommandResult.h"

#include <string>
#include <utility>

namespace quader {

using DocumentCommandResult = CommandResult;

inline DocumentCommandResult documentCommandChangedResult() {
    return {
            .ok = true,
            .changed = true,
    };
}

inline DocumentCommandResult documentCommandUnchangedResult() {
    return {
            .ok = true,
            .changed = false,
    };
}

inline DocumentCommandResult documentCommandMissingEntityResult() {
    return {
            .ok = false,
            .changed = false,
            .message = "Entity does not exist",
    };
}

inline DocumentCommandResult documentCommandMissingCommandResult() {
    return {
            .ok = false,
            .changed = false,
            .message = "Document command is missing",
    };
}

inline DocumentCommandResult documentCommandInvalidRequestResult(std::string message) {
    return {
            .ok = false,
            .changed = false,
            .message = std::move(message),
    };
}

} // namespace quader
