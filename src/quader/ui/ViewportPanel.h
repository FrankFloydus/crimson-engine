#pragma once

#include "quader/core/Types.h"
#include "quader/assets/AssetRegistry.h"
#include "quader/modeling/ModelingSessionService.h"
#include "quader/render/SceneRenderBridge.h"
#include "quader/render/ViewportRenderer.h"
#include "quader/scene/SceneDocument.h"
#include "quader/ui/UIPanel.h"
#include "quader/viewport/ViewportCameraController.h"
#include "quader/viewport/ViewportInputRouter.h"

#include <imgui.h>

#include <vector>

#if QUADER_HAS_MODELING_SDK
#include <quader_editor_core/tool/move_tool.h>
#include <quader_editor_core/tool/rotate_tool.h>
#include <quader_editor_core/tool/scale_tool.h>
#endif

namespace quader {

class ViewportPanel final : public UIPanel {
public:
    enum class GizmoAxis {
        None,
        X,
        Y,
        Z,
        XY,
        XZ,
        YZ,
        All,
    };

    UIPanelId id() const override { return UIPanelId::Viewport; }
    const char* title() const override { return uiPanelKey(UIPanelId::Viewport); }
    bool canToggle() const override { return false; }
    void draw(UIContext& context) override;

    void draw(ViewportRenderer& renderer,
            ViewportCameraController& camera,
            SceneRenderBridge& renderBridge,
            SceneDocument& document,
            AssetRegistry& assets,
            ModelingSessionService& modeling,
            AssetId defaultMaterial);
    const ViewportToolState& toolState() const { return inputRouter_.state(); }

private:
    struct GizmoStartTransform {
        EntityId entity = InvalidEntityId;
        TransformComponent transform;
        Vec3 localPivot = {};
    };

    void selectFromViewport(SceneDocument& document,
            const AssetRegistry& assets,
            ModelingSessionService& modeling,
            const ViewportCameraController& camera,
            Vec2 localMouse,
            ViewportModifiers modifiers,
            int clickCount);
    bool drawViewCube(ViewportCameraController& camera, ImVec2 itemMin, ImVec2 itemSize);
    bool drawTransformGizmo(SceneDocument& document,
            const AssetRegistry& assets,
            ModelingSessionService& modeling,
            ViewportCameraController& camera,
            ImVec2 itemMin,
            ImVec2 itemSize,
            bool suppressHoverWork);

    ViewportInputRouter inputRouter_;
    ModelingBoxPreview boxPreview_;
    bool gizmoDragging_ = false;
    bool sdkTransformPreviewActive_ = false;
    bool sdkTransformPreviewDuplicateSelected_ = false;
    bool sdkTransformPreviewExtrude_ = false;
    bool sdkTransformPreviewInset_ = false;
    bool paintSelectionActive_ = false;
    GizmoAxis activeGizmoAxis_ = GizmoAxis::None;
    Vec2 lastGizmoMouse_ = {};
    Vec2 contextMenuRightPressScreen_ = {};
    bool contextMenuRightPressActive_ = false;
    bool selectionHoverClearIssued_ = false;
    Vec3 gizmoStartWorldPivot_ = {};
    std::vector<GizmoStartTransform> gizmoStartTransforms_;
#if QUADER_HAS_MODELING_SDK
    quader_editor_core::MoveToolState sdkMoveDrag_ = {};
    quader_editor_core::RotateToolState sdkRotateDrag_ = {};
    quader_editor_core::ScaleToolState sdkScaleDrag_ = {};
#endif
};

} // namespace quader
