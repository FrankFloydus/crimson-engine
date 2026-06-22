////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "modeling/ModelingTypes.h"

#include <cstdint>

#include <modeling/operations/operation_command.h>
#include <modeling/runtime/runtime_command.h>
#include <modeling/runtime/session.h>

namespace quader {

/**
 * Owns Modeling Command Executor behavior within the modeling application service layer.
 */
class ModelingCommandExecutor {
public:
    /**
     * Represents a Sync Plan value used by the modeling application service layer.
     */
    struct SyncPlan {
        bool syncMeshes = false;
        bool forceMeshSync = false;
        bool syncSelection = false;
        bool invalidateOverlay = false;
    };

    [[nodiscard]] static ModelingCommandResult runtimeUnavailable();

    [[nodiscard]] static ModelingCommandResult fromRuntime(const quader_editor_core::CommandResult& result);
    [[nodiscard]] static SyncPlan runtimeSyncPlan(
            bool mayChangeContent,
            std::uint64_t contentBefore,
            std::uint64_t contentAfter,
            const quader_editor_core::CommandResult& result);
    [[nodiscard]] static SyncPlan operationSyncPlan(
            quader_editor_core::EditorOperationCommandPhase phase,
            const quader_editor_core::PreviewTransactionStatus& previewBefore,
            const quader_editor_core::EditorOperationCommandResult& result,
            std::uint64_t contentBefore,
            std::uint64_t contentAfter);
};

} // namespace quader
