////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/AssetRegistry.h"
#include "core/Types.h"
#include "modeling/ModelingTypes.h"
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
#include "ui/ViewportPaneOverlayCache.h"
#include "ui/ViewportSelectionController.h"
#include "ui/ViewportShortcutController.h"
#include "viewport/ViewportCameraController.h"
#include "viewport/ViewportInputRouter.h"
#include "viewport/ViewportLayout.h"
#include "viewport/ViewportWorkspaceService.h"

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
class LocalizationService;
class QuaderEditorApi;
class EditorSelectionService;
class ModelingSessionService;
enum class QuaderShadingMode;

/**
 * Owns Viewport Panel behavior within the ImGui editor presentation layer.
 */
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
    void prepareViewports(
            ViewportWorkspaceService& workspace,
            SceneRenderBridge& renderBridge,
            const SceneDocument& document,
            const AssetRegistry& assets);
    void renderViewports(filament::Renderer& renderer);
    void setViewportLayout(ViewportLayoutId layoutId);
    int gridPreset() const;
    void setGridPreset(int preset);
    QuaderShadingMode paneShadingMode(ViewportPaneId paneId) const;
    void setPaneShadingMode(ViewportPaneId paneId, QuaderShadingMode mode);
    ViewportView paneView(ViewportPaneId paneId) const;
    void setPaneView(ViewportPaneId paneId, ViewportView view);
    bool activateToolFromMenu(
            ViewportToolId tool,
            QuaderEditorApi& editor,
            SceneDocument& document,
            ModelingSessionService& modeling);
    ViewportLayoutId viewportLayout() const;
    const ViewportToolState& toolState() const { return inputRouter_.state(); }
    bool flyNavigationActive() const { return lastFlyNavigationActive_; }
    bool navigationGizmoVisible() const { return navigationGizmo_.visible(); }
    void setNavigationGizmoVisible(bool visible) { navigationGizmo_.setVisible(visible); }

private:
    /**
     * Represents a Pane value used by the ImGui editor presentation layer.
     */
    struct Pane {
        ViewportPaneId id = ViewportPaneId::Perspective;
        ViewportRenderer renderer;
        bool initialized = false;
    };

    void draw(SceneRenderBridge& renderBridge,
            SceneDocument& document,
            AssetRegistry& assets,
            ModelingSessionService& modeling,
            AssetId defaultMaterial,
            Vec2 capturedMouseDelta,
            bool* viewportCursorCaptureRequested,
            Vec2* viewportCursorCapturePoint,
            const UIContext& uiContext);
    void drawPaneContents(Pane& pane,
            SceneDocument& document,
            AssetRegistry& assets,
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
            ModelingSessionService& modeling,
            EditorSelectionService& selection,
            QuaderEditorApi& editor,
            ImVec2 itemMin,
            ImVec2 itemSize);
    void refreshCachedSelectionOverlayFrame(const SceneDocument& document,
            const AssetRegistry& assets,
            ModelingSessionService& modeling);
    ViewportWorkspaceService& workspace();
    const ViewportWorkspaceService& workspace() const;
    Pane& pane(ViewportPaneId paneId);
    const Pane& pane(ViewportPaneId paneId) const;
    std::vector<ViewportPanePlacement> activePanePlacements() const;
    std::vector<ViewportSplitHandle> activeSplitHandles() const;
    bool drawPaneChrome(Pane& pane, ImVec2 itemMin, ImVec2 itemSize, const LocalizationService& localization);
    void setPaneView(Pane& pane, ViewportView view);
    bool handleSplitInput(ImVec2 viewportMin, ImVec2 viewportSize, const std::vector<ViewportSplitHandle>& handles);
    const ViewportSettings& activeViewportSettings() const;
    static ImVec2 paneRectMin(ImVec2 viewportMin, ImVec2 viewportSize, ViewportNormalizedRect normalizedRect);
    static ImVec2 paneRectSize(ImVec2 viewportSize, ViewportNormalizedRect normalizedRect);

    std::array<Pane, kViewportPaneCount> panes_;
    ViewportWorkspaceService* workspace_ = nullptr;
    ViewportSettings fallbackViewportSettings_ = ViewportSettings::defaults();
    const ViewportSettings* viewportSettings_ = nullptr;
    ViewportSplitHandle activeSplitHandle_;
    bool splitHandleDragging_ = false;
    std::vector<ViewportPaneId> activeRenderPanes_;
    ViewportInputRouter inputRouter_;
    ViewportInputFrameBuilder inputFrameBuilder_;
    ViewportFocusController focusController_;
    ViewportContextMenuPresenter contextMenuPresenter_;
    ViewportShortcutController shortcutController_;
    ViewportHoverController hoverController_;
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
    bool deleteObjectsPopupOpen_ = false;
    bool deleteObjectsPopupPositionPending_ = false;
    ImVec2 deleteObjectsPopupPosition_ = {};
    bool lastFlyNavigationActive_ = false;
    bool shutdownComplete_ = false;
};

} // namespace quader
