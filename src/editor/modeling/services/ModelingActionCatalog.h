////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace quader {

struct ModelingSelectionSummary;

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
    [[nodiscard]] static bool actionAvailable(
            const ModelingSelectionSummary& summary,
            std::string_view actionId);
};

} // namespace quader
