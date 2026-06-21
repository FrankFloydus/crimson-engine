#pragma once

#include "core/CommandResult.h"

#include <string>

namespace quader {

class IUndoableCommand {
public:
    virtual ~IUndoableCommand() = default;

    [[nodiscard]] virtual const std::string& label() const = 0;
    virtual CommandResult undo() = 0;
    virtual CommandResult redo() = 0;
};

} // namespace quader
