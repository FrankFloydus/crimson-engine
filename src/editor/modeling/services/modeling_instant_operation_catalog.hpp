////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/modeling_types.hpp"
#include "editor/modeling/services/modeling_action_catalog.hpp"

#include <optional>
#include <span>
#include <string_view>

#include <editor/modeling/operations/operation_command.hpp>

namespace quader {

/**
 * Stores one catalog-owned instant modeling operation contract shared by command routing and runtime execution.
 */
struct ModelingInstantOperationCatalogEntry {
    ModelingInstantOperationId operation = ModelingInstantOperationId::DuplicateMeshes;
    std::string_view id;
    std::string_view operation_id;
    std::string_view command_label;
    std::string_view live_label;
    ModelingActionAvailabilityRule availability;
    std::optional<quader_editor_core::EditorOperationId> runtime_operation;
    bool rebuilds_mesh_on_success = true;
    std::string_view public_command_id;
    bool registers_public_command = true;
};

[[nodiscard]] std::span<const ModelingInstantOperationCatalogEntry>
modeling_instant_operation_catalog();
[[nodiscard]] const ModelingInstantOperationCatalogEntry *
find_modeling_instant_operation_catalog_entry(
    ModelingInstantOperationId operation);
[[nodiscard]] std::optional<quader_editor_core::EditorOperationCommand>
modeling_instant_operation_command(
    ModelingInstantOperationId operation,
    const ModelingInteractionSettings &interaction_settings);
[[nodiscard]] bool modeling_instant_operation_enabled(
    const ModelingInstantOperationCatalogEntry &entry,
    const ModelingSelectionSummary &summary);

} // namespace quader
