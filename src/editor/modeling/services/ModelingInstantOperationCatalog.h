////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/ModelingTypes.h"

#include <optional>
#include <span>
#include <string_view>

#include <editor/modeling/operations/operation_command.h>

namespace quader {

/**
 * Enumerates Modeling Instant Operation Enablement policies owned by the modeling operation catalog.
 */
enum class ModelingInstantOperationEnablement {
    AnySelection,
    ObjectSelection,
    MultipleObjects,
    FaceSelection,
    EdgeOrFaceSelection,
    ActiveVertexSelection,
    VertexSelection,
    MultipleVertices,
    ActiveEdgeSelection,
    EdgeSelection,
    NonObjectSelection,
    ObjectOrFaceSelection,
};

/**
 * Enumerates Runtime Operation Settings Kind values used by instant operation catalog entries.
 */
enum class RuntimeOperationSettingsKind {
    Selection,
    DuplicateMeshes,
    Bridge,
    BevelVertices,
    RecalculateFaceNormals,
};

/**
 * Stores one catalog-owned instant modeling operation contract shared by command routing and runtime execution.
 */
struct ModelingInstantOperationCatalogEntry {
    ModelingInstantOperationId operation = ModelingInstantOperationId::DeleteSelection;
    std::string_view id;
    std::string_view operationId;
    std::string_view commandLabel;
    std::string_view liveLabel;
    ModelingInstantOperationEnablement enablement = ModelingInstantOperationEnablement::AnySelection;
    std::optional<quader_editor_core::EditorOperationId> runtimeOperation;
    RuntimeOperationSettingsKind settingsKind = RuntimeOperationSettingsKind::Selection;
    bool rebuildsMeshOnSuccess = true;
};

[[nodiscard]] std::span<const ModelingInstantOperationCatalogEntry> modelingInstantOperationCatalog();
[[nodiscard]] const ModelingInstantOperationCatalogEntry* findModelingInstantOperationCatalogEntry(
        ModelingInstantOperationId operation);
[[nodiscard]] bool modelingInstantOperationEnabled(
        const ModelingInstantOperationCatalogEntry& entry,
        const ModelingSelectionSummary& summary);

} // namespace quader
