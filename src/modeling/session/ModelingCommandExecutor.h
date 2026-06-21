#pragma once

#include "modeling/ModelingSessionService.h"

#include <cstdint>

#if QUADER_HAS_MODELING_SDK
#include <quader_editor_core/operation_command.h>
#include <quader_editor_core/runtime_command.h>
#include <quader_editor_core/session.h>
#endif

namespace quader {

class ModelingCommandExecutor {
public:
    struct SyncPlan {
        bool syncMeshes = false;
        bool forceMeshSync = false;
        bool syncSelection = false;
        bool invalidateOverlay = false;
    };

    [[nodiscard]] static ModelingCommandResult sdkUnavailable();

#if QUADER_HAS_MODELING_SDK
    [[nodiscard]] static ModelingCommandResult fromSdk(const quader_editor_core::CommandResult& result);
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
#endif
};

} // namespace quader
