////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/asset_registry.hpp"
#include "api/quader_api_result.hpp"
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

namespace quader {

class RenderSystem;
class LocalizationService;
class QuaderEditorApi;
class EditorSelectionService;
class ModelingSessionService;
class PivotToolService;

/**
 * Owns Viewport Panel behavior within the ImGui editor presentation layer.
 */
class ViewportPanel final : public UIPanel {
public:
    ViewportPanel();
    ~ViewportPanel() override;

    UIPanelId id() const override { return UIPanelId::Viewport; }
    const char* title() const override {
      return ui_panel_key(UIPanelId::Viewport);
    }
    bool can_toggle() const override { return false; }
    void draw(UIContext& context) override;

    void initialize(RenderSystem &render_system);
    void shutdown();
    void prepare_viewports(ViewportWorkspaceService &workspace,
                           QuaderEditorApi &editor,
                           SceneRenderBridge &render_bridge,
                           const SceneDocument &document,
                           const AssetRegistry &assets,
                           const PivotToolService *pivot);
    void render_viewports(RenderSystem &render_system);
    QuaderApiVoidResult frame_all(ViewportWorkspaceService &workspace,
                                  const SceneDocument &document,
                                  const AssetRegistry &assets,
                                  ViewportPaneId pane);
    QuaderApiVoidResult frame_selection(ViewportWorkspaceService &workspace,
                                         const SceneDocument &document,
                                         const AssetRegistry &assets,
                                         const ModelingSessionService &modeling,
                                         ViewportPaneId pane);
    bool activate_tool_from_menu(ViewportToolId tool, QuaderEditorApi &editor,
                                 ModelingSessionService &modeling);
    const ViewportToolState &tool_state() const {
      return input_router_.state();
    }
    bool fly_navigation_active() const { return last_fly_navigation_active_; }
    bool needs_continuous_rendering() const {
      return last_fly_navigation_active_ ||
             active_viewport_settings().bool_value(
                 ViewportBoolSettingId::ShowInfo) ||
             navigation_gizmo_.input_locked() ||
             interaction_controller_.dragging() || split_handle_dragging_;
    }
    bool navigation_gizmo_visible() const {
      return navigation_gizmo_.visible();
    }
    void set_navigation_gizmo_visible(bool visible) {
      navigation_gizmo_.set_visible(visible);
    }

private:
    /**
     * Represents a Pane value used by the ImGui editor presentation layer.
     */
    struct Pane {
        ViewportPaneId id = ViewportPaneId::Perspective;
        ViewportRenderer renderer;
        bool initialized = false;
    };

    void draw(SceneRenderBridge &render_bridge, SceneDocument &document,
              AssetRegistry &assets, ModelingSessionService &modeling,
              AssetId default_material, QVec2 captured_mouse_delta,
              bool *viewport_cursor_capture_requested,
              QVec2 *viewport_cursor_capture_point,
              const UIContext &ui_context);
    void draw_pane_contents(
        Pane &pane, SceneDocument &document, AssetRegistry &assets,
        ModelingSessionService &modeling, AssetId default_material,
        QVec2 captured_mouse_delta, bool *viewport_cursor_capture_requested,
        QVec2 *viewport_cursor_capture_point, const UIContext &ui_context,
        ImVec2 item_min, ImVec2 item_size, bool hovered, bool focus_hovered);
    void draw_pane_decorations(Pane &pane, SceneDocument &document,
                               const AssetRegistry &assets,
                               ModelingSessionService &modeling,
                               EditorSelectionService &selection,
                               QuaderEditorApi &editor, ImVec2 item_min,
                               ImVec2 item_size);
    void draw_viewport_info_overlay(const SceneDocument &document,
                                    const AssetRegistry &assets,
                                    const UIContext &ui_context,
                                    ImVec2 item_min, ImVec2 item_size);
    void refresh_cached_selection_overlay_frame(
        ModelingSessionService &modeling, const SceneDocument &document,
        const AssetRegistry &assets);
    ViewportWorkspaceService& workspace();
    const ViewportWorkspaceService& workspace() const;
    Pane &pane(ViewportPaneId pane_id);
    const Pane &pane(ViewportPaneId pane_id) const;
    void sync_active_tool_from_editor(QuaderEditorApi &editor,
                                      ModelingSessionService &modeling);
    bool draw_pane_chrome(Pane &pane, ImVec2 item_min, ImVec2 item_size,
                          const LocalizationService &localization);
    void set_pane_view(Pane &pane, ViewportView view);
    bool handle_split_input(ImVec2 viewport_min, ImVec2 viewport_size,
                            const std::vector<ViewportSplitHandle> &handles);
    const ViewportSettings &active_viewport_settings() const;

    std::array<Pane, kViewportPaneCount> panes_;
    ViewportWorkspaceService* workspace_ = nullptr;
    ViewportSettings fallback_viewport_settings_ = ViewportSettings::defaults();
    const ViewportSettings *viewport_settings_ = nullptr;
    ViewportSplitHandle active_split_handle_;
    bool split_handle_dragging_ = false;
    std::vector<ViewportPaneId> active_render_panes_;
    ViewportInputRouter input_router_;
    ViewportInputFrameBuilder input_frame_builder_;
    ViewportFocusController focus_controller_;
    ViewportContextMenuPresenter context_menu_presenter_;
    ViewportShortcutController shortcut_controller_;
    ViewportHoverController hover_controller_;
    ViewportInteractionController interaction_controller_;
    ViewportPaneId interaction_pane_id_ = ViewportPaneId::Perspective;
    ModalToolOverlayRenderer modal_tool_overlay_renderer_;
    ViewportPaneOverlayCache selection_overlay_cache_;
    QVec2 context_menu_right_press_screen_ = {};
    QVec2 context_menu_right_drag_delta_ = {};
    bool context_menu_right_press_active_ = false;
    NavigationGizmo navigation_gizmo_;
    QVec2 fly_cursor_capture_screen_ = {};
    bool delete_objects_popup_open_ = false;
    bool delete_objects_popup_position_pending_ = false;
    ImVec2 delete_objects_popup_position_ = {};
    bool pivot_handle_dragging_ = false;
    bool last_fly_navigation_active_ = false;
    const SceneDocument *viewport_info_cached_document_ = nullptr;
    const AssetRegistry *viewport_info_cached_assets_ = nullptr;
    std::uint64_t viewport_info_cached_scene_revision_ = 0;
    std::uint64_t viewport_info_cached_asset_revision_ = 0;
    std::uint64_t viewport_info_cached_objects_ = 0;
    std::uint64_t viewport_info_cached_primitives_ = 0;
    std::uint64_t viewport_info_cached_draw_calls_ = 0;
    bool viewport_info_scene_stats_valid_ = false;
    bool shutdown_complete_ = false;
};

} // namespace quader
