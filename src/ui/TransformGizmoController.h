#pragma once

#include "assets/AssetRegistry.h"
#include "core/Types.h"
#include "core/UndoRedoManager.h"
#include "document/DocumentCommandService.h"
#include "document/SceneTransformPreviewTransaction.h"
#include "modeling/ModelingSessionService.h"
#include "scene/SceneDocument.h"
#include "viewport/ViewportCameraController.h"
#include "viewport/ViewportInputRouter.h"
#include "viewport/ViewportLayout.h"
#include "viewport/ViewportSettings.h"

#include <imgui.h>

#include <optional>
#include <vector>

#if QUADER_HAS_MODELING_SDK
#include <quader_editor_core/tool/tool_identity.h>
#endif

namespace quader {

enum class TransformAxisHandle {
    None,
    X,
    Y,
    Z,
    XY,
    XZ,
    YZ,
    All,
};

struct TransformGizmoFrameRequest {
    SceneDocument& document;
    const AssetRegistry& assets;
    UndoRedoManager& undoRedo;
    DocumentCommandService& documentCommands;
    ModelingSessionService& modeling;
    ViewportInputRouter& inputRouter;
    const ViewportSettings& viewportSettings;
    ViewportCameraController& camera;
    ViewportPaneId paneId = ViewportPaneId::Perspective;
    ImVec2 itemMin = {};
    ImVec2 itemSize = {};
    bool suppressHoverWork = false;
    bool interactive = true;
};

class TransformGizmoController {
public:
    [[nodiscard]] bool draw(const TransformGizmoFrameRequest& request);
    [[nodiscard]] bool cancelActiveDrag(SceneDocument& document, ModelingSessionService& modeling);

    [[nodiscard]] bool dragging() const { return dragging_; }
    [[nodiscard]] bool dragLocksInput() const { return dragging_ && dragPaneActive_; }
    [[nodiscard]] ViewportPaneId dragPaneId() const { return dragPaneId_; }
    [[nodiscard]] bool supportsViewportTool(ViewportToolId tool) const;
    [[nodiscard]] bool suppressesHover(bool navigationGizmoConsumed, bool transformGizmoConsumed) const;

#if QUADER_HAS_MODELING_SDK
    [[nodiscard]] quader_editor_core::ViewportInteractionTool sdkToolFor(ViewportToolId tool) const;
#endif

private:
    struct StartTransform {
        EntityId entity = InvalidEntityId;
        TransformComponent transform;
        Vec3 localPivot = {};
    };

    void resetDragState();

    bool dragging_ = false;
    bool dragPaneActive_ = false;
    ViewportPaneId dragPaneId_ = ViewportPaneId::Perspective;
    bool modelingDragActive_ = false;
    bool modelingDragPreviewDriven_ = false;
    bool sceneOnlyDrag_ = false;
    std::optional<SceneTransformPreviewTransaction> sceneTransformPreview_;
    TransformAxisHandle activeAxis_ = TransformAxisHandle::None;
    Vec2 lastMouse_ = {};
    Vec3 startWorldPivot_ = {};
    Vec2 startMouse_ = {};
    std::vector<StartTransform> startTransforms_;
};

} // namespace quader
