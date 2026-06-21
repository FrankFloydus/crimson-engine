#pragma once

#include "assets/AssetRegistry.h"
#include "core/Types.h"
#include "core/UndoRedoManager.h"
#include "document/DocumentCommandService.h"
#include "modeling/ModelingSessionService.h"
#include "render/SceneRenderBridge.h"
#include "render/ViewportRenderer.h"
#include "scene/SceneDocument.h"
#include "ui/NavigationGizmo.h"
#include "ui/ModalToolOverlayRenderer.h"
#include "ui/TransformGizmoController.h"
#include "ui/UIPanel.h"
#include "ui/ViewportContextMenuPresenter.h"
#include "ui/ViewportFocusController.h"
#include "ui/ViewportHoverController.h"
#include "ui/ViewportInputFrameBuilder.h"
#include "ui/ViewportPaneController.h"
#include "ui/ViewportPaneOverlayCache.h"
#include "ui/ViewportSelectionController.h"
#include "ui/ViewportShortcutController.h"
#include "viewport/ViewportCameraController.h"
#include "viewport/ViewportInputRouter.h"
#include "viewport/ViewportLayout.h"

#include <imgui.h>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace filament {
class Renderer;
}

namespace quader {

class RenderSystem;

class ViewportPanel final : public UIPanel {
public:
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
    bool activateToolFromMenu(ViewportToolId tool, ModelingSessionService& modeling);
    ViewportLayoutId viewportLayout() const { return layoutId_; }
    const ViewportToolState& toolState() const { return inputRouter_.state(); }
    bool flyNavigationActive() const { return lastFlyNavigationActive_; }
    bool navigationGizmoVisible() const { return navigationGizmo_.visible(); }
    void setNavigationGizmoVisible(bool visible) { navigationGizmo_.setVisible(visible); }

private:
    struct Pane {
        ViewportPaneId id = ViewportPaneId::Perspective;
        ViewportCameraController camera;
        ViewportRenderer renderer;
        Vec2 renderSize = {1.0f, 1.0f};
        bool initialized = false;
        bool visibleThisFrame = false;
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
    ViewportInputFrameBuilder inputFrameBuilder_;
    ViewportFocusController focusController_;
    ViewportContextMenuPresenter contextMenuPresenter_;
    ViewportShortcutController shortcutController_;
    ViewportHoverController hoverController_;
    ViewportPaneController paneController_;
    ViewportSelectionController selectionController_;
    TransformGizmoController transformGizmoController_;
    ModalToolOverlayRenderer modalToolOverlayRenderer_;
    ViewportPaneOverlayCache selectionOverlayCache_;
    bool paintSelectionActive_ = false;
    Vec2 contextMenuRightPressScreen_ = {};
    Vec2 contextMenuRightDragDelta_ = {};
    bool contextMenuRightPressActive_ = false;
    NavigationGizmo navigationGizmo_;
    Vec2 flyCursorCaptureScreen_ = {};
    bool selectionHoverClearIssued_ = false;
    std::optional<ModelingHoverTarget> selectionHoverSuppressedTarget_;
    bool lastFlyNavigationActive_ = false;
};

} // namespace quader
