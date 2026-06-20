#pragma once

#include "quader/assets/AssetRegistry.h"
#include "quader/core/Types.h"
#include "quader/core/UndoRedoManager.h"
#include "quader/document/DocumentCommandService.h"
#include "quader/modeling/ModelingSessionService.h"
#include "quader/render/SceneRenderBridge.h"
#include "quader/render/ViewportRenderer.h"
#include "quader/scene/SceneDocument.h"
#include "quader/ui/UIPanel.h"
#include "quader/viewport/ViewportCameraController.h"
#include "quader/viewport/ViewportInputRouter.h"

#include <imgui.h>

#include <cstdint>
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

    const ViewportToolState& toolState() const { return inputRouter_.state(); }
    bool flyNavigationActive() const { return lastFlyNavigationActive_; }

private:
    struct GizmoStartTransform {
        EntityId entity = InvalidEntityId;
        TransformComponent transform;
        Vec3 localPivot = {};
    };

    struct HoverWorkKey {
        bool valid = false;
        ViewportTool activeTool = ViewportTool::Select;
        ViewportSelectionMode selectionMode = ViewportSelectionMode::Object;
        ModelingBoxDragState boxDragState = ModelingBoxDragState::Idle;
        int gridPreset = 0;
        Vec2 mouseLocal = {};
        Vec2 viewportSize = {};
        bool primaryModifier = false;
        bool shiftModifier = false;
        bool altModifier = false;
        bool gizmoConsumed = false;
        std::uint64_t sceneRevision = 0;
        std::uint64_t assetRevision = 0;
        std::uint64_t contentRevision = 0;
        std::uint64_t selectionRevision = 0;

        [[nodiscard]] bool matches(const HoverWorkKey& other) const {
            return valid &&
                    other.valid &&
                    activeTool == other.activeTool &&
                    selectionMode == other.selectionMode &&
                    boxDragState == other.boxDragState &&
                    gridPreset == other.gridPreset &&
                    mouseLocal.x == other.mouseLocal.x &&
                    mouseLocal.y == other.mouseLocal.y &&
                    viewportSize.x == other.viewportSize.x &&
                    viewportSize.y == other.viewportSize.y &&
                    primaryModifier == other.primaryModifier &&
                    shiftModifier == other.shiftModifier &&
                    altModifier == other.altModifier &&
                    gizmoConsumed == other.gizmoConsumed &&
                    sceneRevision == other.sceneRevision &&
                    assetRevision == other.assetRevision &&
                    contentRevision == other.contentRevision &&
                    selectionRevision == other.selectionRevision;
        }
    };

    void draw(ViewportRenderer& renderer,
            ViewportCameraController& camera,
            SceneRenderBridge& renderBridge,
            SceneDocument& document,
            AssetRegistry& assets,
            UndoRedoManager& undoRedo,
            DocumentCommandService& documentCommands,
            ModelingSessionService& modeling,
            AssetId defaultMaterial,
            Vec2 capturedMouseDelta,
            bool* viewportCursorCaptureRequested,
            Vec2* viewportCursorCapturePoint,
            const UIContext& uiContext);

    ModelingSelectionSummary selectFromViewport(SceneDocument& document,
            const AssetRegistry& assets,
            ModelingSessionService& modeling,
            const ViewportCameraController& camera,
            Vec2 localMouse,
            ViewportModifiers modifiers,
            int clickCount,
            bool preserveSelectionOnMiss = false,
            bool preserveSelectionOnSelectedHit = false);
    bool drawViewCube(ViewportCameraController& camera, ImVec2 itemMin, ImVec2 itemSize);
    bool drawTransformGizmo(SceneDocument& document,
            const AssetRegistry& assets,
            UndoRedoManager& undoRedo,
            DocumentCommandService& documentCommands,
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
    Vec2 contextMenuRightDragDelta_ = {};
    bool contextMenuRightPressActive_ = false;
    Vec2 flyCursorCaptureScreen_ = {};
    bool selectionHoverClearIssued_ = false;
    bool lastFlyNavigationActive_ = false;
    HoverWorkKey lastHoverWorkKey_;
    ViewportOverlayFrame cachedSelectionOverlayFrame_;
    bool cachedSelectionOverlayFrameValid_ = false;
    bool cachedSelectionOverlayFrameDirty_ = true;
    std::uint64_t cachedSelectionOverlayContentRevision_ = 0;
    std::uint64_t cachedSelectionOverlaySelectionRevision_ = 0;
    std::uint64_t cachedSelectionOverlaySceneRevision_ = 0;
    Vec3 gizmoStartWorldPivot_ = {};
    std::vector<GizmoStartTransform> gizmoStartTransforms_;
#if QUADER_HAS_MODELING_SDK
    quader_editor_core::MoveToolState sdkMoveDrag_ = {};
    quader_editor_core::RotateToolState sdkRotateDrag_ = {};
    quader_editor_core::ScaleToolState sdkScaleDrag_ = {};
#endif
};

} // namespace quader
