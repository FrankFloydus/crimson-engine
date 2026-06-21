#pragma once

#include "modeling/ModelingSessionService.h"

#include <optional>

#if QUADER_HAS_MODELING_SDK
#include <quader_editor_core/operation_command.h>
#include <quader_editor_core/session.h>
#endif

namespace quader {

class SdkOperationExecutor {
public:
#if QUADER_HAS_MODELING_SDK
    struct OperationCommand {
        quader_editor_core::EditorOperationId operation =
                quader_editor_core::EditorOperationId::delete_selection;
        quader_editor_core::EditorOperationCommandPhase phase =
                quader_editor_core::EditorOperationCommandPhase::execute;
        quader_editor_core::EditorOperationSettings settings =
                quader_editor_core::SelectionOperationSettings{};
    };

    [[nodiscard]] std::optional<OperationCommand> operationForInstantCommand(
            ModelingInstantOperationId operation,
            const ModelingInteractionSettings& interactionSettings) const;
    [[nodiscard]] static quader_editor_core::EditorOperationCommand makeCommand(
            const OperationCommand& operation);
    [[nodiscard]] static quader_editor_core::EditorOperationCommandResult execute(
            quader_editor_core::EditorSession& session,
            const OperationCommand& operation);
#endif
};

} // namespace quader
