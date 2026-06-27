////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/ModelingTypes.h"
#include "editor/modeling/services/ModelingActionCatalog.h"

#include <optional>
#include <span>
#include <string_view>

#include <editor/modeling/operations/operation_command.h>

namespace quader {

/**
 * Stores one catalog-owned instant modeling operation contract shared by command routing and runtime execution.
 */
struct ModelingInstantOperationCatalogEntry {
    ModelingInstantOperationId operation = ModelingInstantOperationId::DuplicateMeshes;
    std::string_view id;
    std::string_view operationId;
    std::string_view commandLabel;
    std::string_view liveLabel;
    ModelingActionAvailabilityRule availability;
    std::optional<quader_editor_core::EditorOperationId> runtimeOperation;
    bool rebuildsMeshOnSuccess = true;
    std::string_view publicCommandId;
    bool registersPublicCommand = true;
};

[[nodiscard]] std::span<const ModelingInstantOperationCatalogEntry> modelingInstantOperationCatalog();
[[nodiscard]] const ModelingInstantOperationCatalogEntry* findModelingInstantOperationCatalogEntry(
        ModelingInstantOperationId operation);
[[nodiscard]] bool modelingInstantOperationEnabled(
        const ModelingInstantOperationCatalogEntry& entry,
        const ModelingSelectionSummary& summary);

} // namespace quader
