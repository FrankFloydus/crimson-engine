#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace quader {

struct ModelingSelectionSummary;

enum class ModelingActionSurface {
    ViewportContextMenu,
    OperationsPanel,
};

enum class ModelingActionVisibility {
    Hidden,
    Disabled,
    Enabled,
};

enum class ModelingActionKind {
    Operation,
    Tool,
};

enum class ModelingActionPreviewKind {
    None,
    Bridge,
    EdgeBevel,
    Thicken,
    SliceQuad,
    MergeByDistance,
};

struct ModelingActionQuery {
    ModelingActionSurface surface = ModelingActionSurface::OperationsPanel;
    bool modalToolOwnsViewport = false;
    bool includeDisabled = true;
    bool includeHidden = false;
};

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

struct ModelingActionPanel {
    std::string panelId;
    std::string label;
    std::string selectionModeId;
    ModelingActionVisibility visibility = ModelingActionVisibility::Hidden;
    std::vector<ModelingActionItem> actions;
};

struct ModelingActionSet {
    ModelingActionSurface surface = ModelingActionSurface::OperationsPanel;
    std::string surfaceId;
    std::vector<ModelingActionPanel> panels;
};

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
