////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/modeling_types.hpp"

#include <optional>

#include <editor/modeling/operations/operation_command.hpp>
#include <editor/modeling/runtime/session.hpp>

namespace quader {

/**
 * Owns Runtime Operation Executor behavior within the modeling application service layer.
 */
class RuntimeOperationExecutor {
public:
    /**
     * Represents an Operation Command value used by the modeling application service layer.
     */
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
};

} // namespace quader
