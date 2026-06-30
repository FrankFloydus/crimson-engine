////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/asset_registry.hpp"
#include "api/quader_api_types.hpp"
#include "core/types.hpp"
#include "editor/modeling/modeling_types.hpp"
#include "editor/viewport/viewport_interaction_controller.hpp"
#include "render/scene_render_bridge.hpp"
#include "render/viewport_renderer.hpp"
#include "scene/scene_document.hpp"
#include "ui/navigation_gizmo.hpp"
#include "ui/modal_tool_overlay_renderer.hpp"
#include "ui/ui_panel.hpp"
#include "ui/viewport_context_menu_presenter.hpp"
#include "ui/viewport_focus_controller.hpp"
#include "ui/viewport_hover_controller.hpp"
#include "ui/viewport_input_frame_builder.hpp"
#include "ui/viewport_pane_overlay_cache.hpp"
#include "ui/viewport_shortcut_controller.hpp"
#include "viewport/viewport_camera_controller.hpp"
#include "viewport/viewport_input_router.hpp"
#include "api/quader_viewport_layout_types.hpp"
#include "viewport/viewport_workspace_service.hpp"

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
    bool activateToolFromMenu(
            ViewportToolId tool,
            QuaderEditorApi& editor,
            ModelingSessionService& modeling);
    const ViewportToolState& toolState() const { return inputRouter_.state(); }
    bool flyNavigationActive() const { return lastFlyNavigationActive_; }
    bool needsContinuousRendering() const {
        return lastFlyNavigationActive_ ||
                navigationGizmo_.inputLocked() ||
                interactionController_.dragging() ||
                splitHandleDragging_;
    }
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
            QVec2 capturedMouseDelta,
            bool* viewportCursorCaptureRequested,
            QVec2* viewportCursorCapturePoint,
            const UIContext& uiContext);
    void drawPaneContents(Pane& pane,
            SceneDocument& document,
            AssetRegistry& assets,
            ModelingSessionService& modeling,
            AssetId defaultMaterial,
            QVec2 capturedMouseDelta,
            bool* viewportCursorCaptureRequested,
            QVec2* viewportCursorCapturePoint,
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
    void syncActiveToolFromEditor(QuaderEditorApi& editor, ModelingSessionService& modeling);
    bool drawPaneChrome(Pane& pane, ImVec2 itemMin, ImVec2 itemSize, const LocalizationService& localization);
    void setPaneView(Pane& pane, ViewportView view);
    bool handleSplitInput(ImVec2 viewportMin, ImVec2 viewportSize, const std::vector<ViewportSplitHandle>& handles);
    const ViewportSettings& activeViewportSettings() const;

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
    ViewportInteractionController interactionController_;
    ViewportPaneId interactionPaneId_ = ViewportPaneId::Perspective;
    ModalToolOverlayRenderer modalToolOverlayRenderer_;
    ViewportPaneOverlayCache selectionOverlayCache_;
    QVec2 contextMenuRightPressScreen_ = {};
    QVec2 contextMenuRightDragDelta_ = {};
    bool contextMenuRightPressActive_ = false;
    NavigationGizmo navigationGizmo_;
    QVec2 flyCursorCaptureScreen_ = {};
    bool deleteObjectsPopupOpen_ = false;
    bool deleteObjectsPopupPositionPending_ = false;
    ImVec2 deleteObjectsPopupPosition_ = {};
    bool lastFlyNavigationActive_ = false;
    bool shutdownComplete_ = false;
};

} // namespace quader
