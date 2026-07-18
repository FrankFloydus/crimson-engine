////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/modeling_result_types.hpp"

#include <cstdint>

#include <editor/modeling/operations/operation_command.hpp>
#include <editor/modeling/runtime/runtime_command.hpp>
#include <editor/modeling/runtime/session.hpp>

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
      bool sync_meshes = false;
      bool force_mesh_sync = false;
      bool sync_selection = false;
      bool invalidate_overlay = false;
    };

    [[nodiscard]] static ModelingCommandResult runtime_unavailable();

    [[nodiscard]] static ModelingCommandResult
    from_runtime(const quader_editor_core::CommandResult &result);
    [[nodiscard]] static SyncPlan
    runtime_sync_plan(bool may_change_content, std::uint64_t content_before,
                      std::uint64_t content_after,
                      const quader_editor_core::CommandResult &result);
};

} // namespace quader
