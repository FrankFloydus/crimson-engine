////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <modeling/operations/operation_command.h>
#include <modeling/runtime/repeat_last_action.h>
#include <modeling/runtime/runtime_command.h>
#include <modeling/runtime/session.h>
#include <modeling/interactions/transform_drag.h>

namespace quader {

/**
 * Owns Modeling History Bridge behavior within the modeling application service layer.
 */
class ModelingHistoryBridge {
public:
    [[nodiscard]] static quader_editor_core::EditorRuntimeCommandId undoCommand();
    [[nodiscard]] static quader_editor_core::EditorRuntimeCommandId redoCommand();

    [[nodiscard]] quader_editor_core::RepeatLastActionState& repeatLastAction();
    [[nodiscard]] const quader_editor_core::RepeatLastActionState& repeatLastAction() const;
    [[nodiscard]] quader_editor_core::CommandResult executeRepeatLastAction(
            quader_editor_core::EditorSession& session);
    void recordOperation(
            const quader_editor_core::EditorOperationCommand& command,
            const quader_editor_core::CommandResult& result);
    void recordTransform(
            const quader_editor_core::EditorSession& session,
            const quader_editor_core::TransformDragState& drag,
            bool duplicateSelected);

private:
    quader_editor_core::RepeatLastActionState repeatLastAction_;
};

} // namespace quader
