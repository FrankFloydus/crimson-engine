////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/command_result.hpp"

#include <string>
#include <string_view>

namespace quader {

/**
 * Applies I Undoable Command behavior as a command in the shared core service layer.
 */
class IUndoableCommand {
public:
    virtual ~IUndoableCommand() = default;

    [[nodiscard]] virtual const std::string& label() const = 0;
    virtual CommandResult undo() = 0;
    virtual CommandResult redo() = 0;
    virtual bool mergeWith(const IUndoableCommand& newerCommand, std::string_view mergeKey) {
        (void)newerCommand;
        (void)mergeKey;
        return false;
    }
};

} // namespace quader
