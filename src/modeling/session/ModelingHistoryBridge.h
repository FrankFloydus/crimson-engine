#pragma once

#if QUADER_HAS_MODELING_SDK
#include <quader_editor_core/operation_command.h>
#include <quader_editor_core/repeat_last_action.h>
#include <quader_editor_core/runtime_command.h>
#include <quader_editor_core/session.h>
#include <quader_editor_core/tool/transform_drag.h>
#endif

namespace quader {

class ModelingHistoryBridge {
public:
#if QUADER_HAS_MODELING_SDK
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
#endif
};

} // namespace quader
