////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/modeling_types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace quader_host_contract {
enum class HostPreviewKind;
}

namespace quader {

/**
 * Enumerates ModelingActionSurface values used by the modeling layer.
 */
enum class ModelingActionSurface {
    ViewportContextMenu,
    OperationsPanel,
};

/**
 * Enumerates ModelingActionVisibility values used by the modeling layer.
 */
enum class ModelingActionVisibility {
    Hidden,
    Disabled,
    Enabled,
};

/**
 * Enumerates ModelingActionKind values used by the modeling layer.
 */
enum class ModelingActionKind {
    Operation,
    Tool,
};

/**
 * Enumerates ModelingActionPreviewKind values used by the modeling layer.
 */
enum class ModelingActionPreviewKind {
    None,
    Bridge,
    EdgeBevel,
    Thicken,
    SliceQuad,
    MergeByDistance,
};

/**
 * Enumerates selection requirement shapes for modeling action availability.
 */
enum class ModelingActionSelectionRequirement {
    AnySelection,
    SelectionMode,
    NonObjectSelection,
    ObjectOrFaceSelection,
    EdgeOrFaceSelection,
};

/**
 * Represents the app-side action availability rule evaluated for host and instant modeling actions.
 */
struct ModelingActionAvailabilityRule {
    ModelingActionSelectionRequirement requirement = ModelingActionSelectionRequirement::AnySelection;
    ModelingSelectionMode mode = ModelingSelectionMode::Object;
    std::uint32_t minimumSelectedVertices = 0;
    std::uint32_t minimumSelectedEdges = 0;
    std::uint32_t exactSelectedEdges = 0;
    std::uint32_t minimumSelectedFaces = 0;
    std::uint32_t minimumSelectedObjects = 0;
    ModelingActiveSelectionKind requiredActiveKind = ModelingActiveSelectionKind::None;
    bool requiresSingleObject = false;
};

/**
 * Represents an evaluated modeling action availability result.
 */
struct ModelingActionAvailabilityResult {
    ModelingActionVisibility visibility = ModelingActionVisibility::Hidden;
    std::string disabledReason;
    std::string disabledReasonKey;
    std::uint32_t disabledReasonCount = 0;
};

/**
 * Represents a Modeling Action Query value used by the modeling application service layer.
 */
struct ModelingActionQuery {
    ModelingActionSurface surface = ModelingActionSurface::OperationsPanel;
    bool modalToolOwnsViewport = false;
    bool suppressWhenModalToolOwnsViewport = true;
    bool includeDisabled = true;
    bool includeHidden = false;
};

/**
 * Represents a Modeling Action Item value used by the modeling application service layer.
 */
struct ModelingActionItem {
    std::string actionId;
    std::string operationId;
    std::string toolId;
    std::string label;
    ModelingActionKind kind = ModelingActionKind::Operation;
    ModelingActionPreviewKind previewKind = ModelingActionPreviewKind::None;
    ModelingActionVisibility visibility = ModelingActionVisibility::Hidden;
    std::string disabledReason;
    std::string disabledReasonKey;
    std::uint32_t disabledReasonCount = 0;
    bool rebuildsMeshOnSuccess = true;
};

/**
 * Represents a Modeling Action Panel value used by the modeling application service layer.
 */
struct ModelingActionPanel {
    std::string panelId;
    std::string label;
    std::string selectionModeId;
    ModelingActionVisibility visibility = ModelingActionVisibility::Hidden;
    std::vector<ModelingActionItem> actions;
};

/**
 * Represents a Modeling Action Set value used by the modeling application service layer.
 */
struct ModelingActionSet {
    ModelingActionSurface surface = ModelingActionSurface::OperationsPanel;
    std::string surfaceId;
    std::vector<ModelingActionPanel> panels;
};

/**
 * Owns Modeling Action Catalog behavior within the modeling application service layer.
 */
class ModelingActionCatalog {
public:
    [[nodiscard]] static ModelingActionSet actionSetFor(
            const ModelingSelectionSummary& summary,
            const ModelingActionQuery& query);
    [[nodiscard]] static std::vector<ModelingActionItem> actionsFor(
            const ModelingSelectionSummary& summary,
            const ModelingActionQuery& query);
    [[nodiscard]] static std::optional<ModelingActionItem> actionFor(
            const ModelingSelectionSummary& summary,
            std::string_view actionId);
    [[nodiscard]] static bool actionAvailable(
            const ModelingSelectionSummary& summary,
            std::string_view actionId);
    [[nodiscard]] static ModelingActionAvailabilityResult evaluateAvailability(
            const ModelingSelectionSummary& summary,
            const ModelingActionAvailabilityRule& rule);
    [[nodiscard]] static ModelingActionAvailabilityResult evaluateOperationAvailability(
            const ModelingSelectionSummary& summary,
            std::string_view operationId,
            const ModelingActionAvailabilityRule& rule);
    [[nodiscard]] static ModelingActionPreviewKind previewKindFromHost(
            quader_host_contract::HostPreviewKind kind);
};

} // namespace quader
