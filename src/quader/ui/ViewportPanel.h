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
#include "quader/viewport/ViewportLayout.h"

#include <imgui.h>

#include <array>
#include <cstdint>
#include <vector>

namespace filament {
class Renderer;
}

namespace quader {

class RenderSystem;

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

    ViewportPanel();
    ~ViewportPanel() override;

    UIPanelId id() const override { return UIPanelId::Viewport; }
    const char* title() const override { return uiPanelKey(UIPanelId::Viewport); }
    bool canToggle() const override { return false; }
    void draw(UIContext& context) override;

    void initialize(RenderSystem& renderSystem);
    void shutdown();
    void prepareViewports(SceneRenderBridge& renderBridge);
    void renderViewports(filament::Renderer& renderer);
    void setViewportLayout(ViewportLayoutId layoutId);
    bool activateToolFromMenu(ViewportTool tool, ModelingSessionService& modeling);
    ViewportLayoutId viewportLayout() const { return layoutId_; }
    const ViewportToolState& toolState() const { return inputRouter_.state(); }
    bool flyNavigationActive() const { return lastFlyNavigationActive_; }

private:
    struct Pane {
        ViewportPaneId id = ViewportPaneId::Perspective;
        ViewportCameraController camera;
        ViewportRenderer renderer;
        Vec2 renderSize = {1.0f, 1.0f};
        bool initialized = false;
        bool visibleThisFrame = false;
    };

    struct GizmoStartTransform {
        EntityId entity = InvalidEntityId;
        TransformComponent transform;
        Vec3 localPivot = {};
    };

    struct HoverWorkKey {
        bool valid = false;
        ViewportPaneId paneId = ViewportPaneId::Perspective;
        ViewportTool activeTool = ViewportTool::Select;
        ViewportSelectionMode selectionMode = ViewportSelectionMode::Object;
        ModelingBoxDragState boxDragState = ModelingBoxDragState::Idle;
        int gridPreset = 0;
        Vec2 mouseLocal = {};
        Vec2 viewportSize = {};
        Vec3 cameraEye = {};
        Vec3 cameraForward = {0.0f, 0.0f, -1.0f};
        Vec3 cameraRight = {1.0f, 0.0f, 0.0f};
        Vec3 cameraUp = {0.0f, 1.0f, 0.0f};
        ViewportOverlayProjection cameraProjection = ViewportOverlayProjection::Perspective;
        float cameraOrthographicSize = 24.0f;
        float cameraFovDegrees = 60.0f;
        float cameraNearPlane = 0.05f;
        float cameraFarPlane = 1000.0f;
        bool primaryModifier = false;
        bool shiftModifier = false;
        bool altModifier = false;
        bool gizmoConsumed = false;
        std::uint64_t sceneRevision = 0;
        std::uint64_t assetRevision = 0;
        std::uint64_t contentRevision = 0;
        std::uint64_t selectionRevision = 0;

        static bool vec3Matches(Vec3 left, Vec3 right) {
            return left.x == right.x && left.y == right.y && left.z == right.z;
        }

        [[nodiscard]] bool matches(const HoverWorkKey& other) const {
            return valid &&
                    other.valid &&
                    paneId == other.paneId &&
                    activeTool == other.activeTool &&
                    selectionMode == other.selectionMode &&
                    boxDragState == other.boxDragState &&
                    gridPreset == other.gridPreset &&
                    mouseLocal.x == other.mouseLocal.x &&
                    mouseLocal.y == other.mouseLocal.y &&
                    viewportSize.x == other.viewportSize.x &&
                    viewportSize.y == other.viewportSize.y &&
                    vec3Matches(cameraEye, other.cameraEye) &&
                    vec3Matches(cameraForward, other.cameraForward) &&
                    vec3Matches(cameraRight, other.cameraRight) &&
                    vec3Matches(cameraUp, other.cameraUp) &&
                    cameraProjection == other.cameraProjection &&
                    cameraOrthographicSize == other.cameraOrthographicSize &&
                    cameraFovDegrees == other.cameraFovDegrees &&
                    cameraNearPlane == other.cameraNearPlane &&
                    cameraFarPlane == other.cameraFarPlane &&
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

    void draw(SceneRenderBridge& renderBridge,
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
    void drawPaneContents(Pane& pane,
            SceneDocument& document,
            AssetRegistry& assets,
            UndoRedoManager& undoRedo,
            DocumentCommandService& documentCommands,
            ModelingSessionService& modeling,
            AssetId defaultMaterial,
            Vec2 capturedMouseDelta,
            bool* viewportCursorCaptureRequested,
            Vec2* viewportCursorCapturePoint,
            const UIContext& uiContext,
            ImVec2 itemMin,
            ImVec2 itemSize,
            bool hovered,
            bool focusHovered);
    void drawPaneDecorations(Pane& pane,
            SceneDocument& document,
            const AssetRegistry& assets,
            UndoRedoManager& undoRedo,
            DocumentCommandService& documentCommands,
            ModelingSessionService& modeling,
            ImVec2 itemMin,
            ImVec2 itemSize);
    void refreshCachedSelectionOverlayFrame(const SceneDocument& document,
            const AssetRegistry& assets,
            ModelingSessionService& modeling);
    Pane& pane(ViewportPaneId paneId);
    const Pane& pane(ViewportPaneId paneId) const;
    std::vector<ViewportPanePlacement> activePanePlacements() const;
    std::vector<ViewportSplitHandle> activeSplitHandles() const;
    void resetPaneCamera(Pane& pane);
    void drawPaneChrome(const Pane& pane, ImVec2 itemMin, ImVec2 itemSize) const;
    bool handleSplitInput(ImVec2 viewportMin, ImVec2 viewportSize, const std::vector<ViewportSplitHandle>& handles);
    const ViewportSettings& activeViewportSettings() const;
    static ImVec2 paneRectMin(ImVec2 viewportMin, ImVec2 viewportSize, ViewportNormalizedRect normalizedRect);
    static ImVec2 paneRectSize(ImVec2 viewportSize, ViewportNormalizedRect normalizedRect);

    ModelingSelectionSummary selectFromViewport(SceneDocument& document,
            const AssetRegistry& assets,
            ModelingSessionService& modeling,
            const ViewportCameraController& camera,
            Vec2 localMouse,
            ViewportModifiers modifiers,
            int clickCount,
            bool preserveSelectionOnMiss = false,
            bool preserveSelectionOnSelectedHit = false);
    bool drawViewCube(ViewportCameraController& camera, ImVec2 itemMin, ImVec2 itemSize, bool interactive = true);
    bool drawTransformGizmo(SceneDocument& document,
            const AssetRegistry& assets,
            UndoRedoManager& undoRedo,
            DocumentCommandService& documentCommands,
            ModelingSessionService& modeling,
            ViewportCameraController& camera,
            ViewportPaneId paneId,
            ImVec2 itemMin,
            ImVec2 itemSize,
            bool suppressHoverWork,
            bool interactive = true);

    std::array<Pane, kViewportPaneCount> panes_;
    ViewportSettings fallbackViewportSettings_ = ViewportSettings::defaults();
    const ViewportSettings* viewportSettings_ = nullptr;
    ViewportLayoutState layoutState_;
    ViewportLayoutId layoutId_ = ViewportLayoutId::Single;
    ViewportPaneId focusedPaneId_ = ViewportPaneId::Perspective;
    ViewportSplitHandle activeSplitHandle_;
    bool splitHandleDragging_ = false;
    std::vector<ViewportPaneId> activeRenderPanes_;
    ViewportInputRouter inputRouter_;
    ModelingBoxPreview boxPreview_;
    bool insertEdgeLoopDragActive_ = false;
    bool cutDragActive_ = false;
    bool mirrorDragActive_ = false;
    bool gizmoDragging_ = false;
    bool gizmoDragPaneActive_ = false;
    ViewportPaneId gizmoDragPaneId_ = ViewportPaneId::Perspective;
    bool modelingTransformDragActive_ = false;
    bool modelingTransformDragPreviewDriven_ = false;
    bool sceneOnlyGizmoDrag_ = false;
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
    std::uint64_t cachedSelectionOverlayAssetRevision_ = 0;
    std::uint64_t cachedSelectionOverlayToolSignature_ = 0;
    ModelingSelectionMode cachedSelectionOverlayMode_ = ModelingSelectionMode::Object;
    Vec3 gizmoStartWorldPivot_ = {};
    Vec2 gizmoStartMouse_ = {};
    std::vector<GizmoStartTransform> gizmoStartTransforms_;
};

} // namespace quader
