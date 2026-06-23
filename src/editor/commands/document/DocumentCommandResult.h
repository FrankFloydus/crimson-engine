////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/CommandResult.h"
#include "core/Types.h"

#include <string>
#include <utility>
#include <vector>

namespace quader {

using DocumentCommandResult = CommandResult;

/**
 * Stores Create Entity Command payload data for document command callers.
 */
struct DocumentCreateEntityResult : DocumentCommandResult {
    EntityId entity = InvalidEntityId;
};

/**
 * Stores Group Selection Command payload data for document command callers.
 */
struct DocumentGroupSelectionResult : DocumentCommandResult {
    EntityId groupEntity = InvalidEntityId;
};

/**
 * Stores Duplicate Entity Command payload data for document command callers.
 */
struct DocumentDuplicateEntityResult : DocumentCommandResult {
    EntityId duplicateEntity = InvalidEntityId;
};

/**
 * Stores Duplicate Selected Hierarchy Command payload data for document command callers.
 */
struct DocumentDuplicateHierarchyResult : DocumentCommandResult {
    std::vector<EntityId> duplicateRoots;
};

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
