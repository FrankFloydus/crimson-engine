////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_result.hpp"
#include "api/quader_viewport_layout_types.hpp"
#include "editor/viewport/viewport_interaction_controller.hpp"
#include "render/render_system.hpp"
#include "ui/qt_navigation_gizmo.hpp"
#include "ui/qt_startup_presentation.hpp"
#include "ui/qt_viewport_context_menu.hpp"
#include "ui/qt_viewport_delete_popup.hpp"
#include "ui/qt_viewport_surface.hpp"
#include "ui/viewport_host_interaction_policy.hpp"
#include "ui/viewport_hover_controller.hpp"
#include "ui/viewport_pane_overlay_cache.hpp"
#include "ui/viewport_shortcut_controller.hpp"
#include "viewport/viewport_input_router.hpp"
#include "viewport/viewport_settings.hpp"
#include "viewport/viewport_workspace_service.hpp"

#include <QElapsedTimer>
#include <QWidget>

#include <array>
#include <cstdint>
#include <vector>

class QComboBox;
class QFrame;
class QFocusFrame;
class QLabel;
class QMouseEvent;

namespace quader {

class AssetRegistry;
class LinearArrayToolService;
class ModelingSessionService;
class LocalizationService;
class PivotToolService;
class QuaderEditorApi;
class RenderOverlaySnapshotCache;
class QtViewportFlyNavigationHud;
class SceneDocument;
class SceneRenderBridge;

/**
 * Qt Widgets host for Quader's embedded one-to-four pane viewport workspace.
 * ViewportWorkspaceService remains the sole owner of pane/layout state.
 */
class QtViewportPanel final : public QWidget {
public:
  explicit QtViewportPanel(QWidget *parent = nullptr);
  ~QtViewportPanel() override;

  QtViewportPanel(const QtViewportPanel &) = delete;
  QtViewportPanel &operator=(const QtViewportPanel &) = delete;

  bool initialize(RenderSystem &render_system);
  void shutdown();

  void attach_workspace(ViewportWorkspaceService &workspace);
  void set_viewport_settings(const ViewportSettings &settings);
  void set_render_performance_stats(RenderFramePerformanceStats stats);
  void set_localization(LocalizationService &localization);
  void set_startup_presentation_blocked(bool blocked);
  [[nodiscard]] bool startup_presentation_blocked() const {
    return startup_presentation_blocked_;
  }
  void refresh_workspace_layout();
  void process_input(QuaderEditorApi &editor, SceneDocument &document,
                     AssetRegistry &assets, ModelingSessionService &modeling,
                     AssetId default_material, PivotToolService *pivot,
                     LinearArrayToolService *linear_array);
  void prepare_viewports(QuaderEditorApi &editor,
                         SceneRenderBridge &render_bridge,
                         RenderOverlaySnapshotCache &snapshot_cache,
                         SceneDocument &document, AssetRegistry &assets,
                         ModelingSessionService &modeling,
                         PivotToolService *pivot,
                         LinearArrayToolService *linear_array);
  [[nodiscard]] QuaderApiVoidResult frame_all(const SceneDocument &document,
                                              const AssetRegistry &assets,
                                              ViewportPaneId pane_id);
  [[nodiscard]] QuaderApiVoidResult
  frame_selection(const ModelingSessionService &modeling,
                  ViewportPaneId pane_id);
  [[nodiscard]] std::size_t render_viewports();
  [[nodiscard]] bool needs_continuous_rendering() const;
  [[nodiscard]] bool focus_active_pane();

  [[nodiscard]] QtViewportSurface *surface(ViewportPaneId pane_id);
  [[nodiscard]] const QtViewportSurface *surface(ViewportPaneId pane_id) const;

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;

private:
  /// Owns the retained Qt presentation objects for one workspace pane.
  struct Pane {
    ViewportPaneId id = ViewportPaneId::Perspective;
    QFrame *frame = nullptr;
    QWidget *toolbar = nullptr;
    QComboBox *view_combo = nullptr;
    QComboBox *shading_combo = nullptr;
    QLabel *info_overlay = nullptr;
    QtNavigationGizmo *navigation_gizmo = nullptr;
    QtViewportFlyNavigationHud *fly_navigation_hud = nullptr;
    QtViewportSurface *surface = nullptr;
    QWidget *surface_container = nullptr;
  };

  void create_pane(Pane &pane);
  void refresh_pane_controls(Pane &pane);
  void sync_pane_control_selection(Pane &pane);
  void update_pane_geometry();
  void update_pane_overlay_geometry(Pane &pane);
  void update_focused_pane_indicator();
  void request_pane_focus(ViewportPaneId pane_id,
                          ViewportPaneFocusTrigger trigger);
  [[nodiscard]] bool viewport_surface_has_keyboard_focus() const;
  [[nodiscard]] bool pane_interaction_locked() const;
  [[nodiscard]] bool is_viewport_internal_event_target(
      const QObject *target) const;
  void queue_external_pointer_event(const QMouseEvent &event);
  void update_fly_navigation_hud(Pane &pane, bool active,
                                 bool speed_is_default, float speed);
  void hide_fly_navigation_huds();
  void draw_viewport_info_overlay(Pane &pane,
                                  const SceneDocument &document,
                                  const AssetRegistry &assets,
                                  ViewportOverlayCamera camera);
  void update_split_handles();
  void set_pane_view(Pane &pane, ViewportView view);
  void set_pane_shading(Pane &pane, ViewportShadingMode shading);
  void process_viewport_input(QuaderEditorApi &editor, SceneDocument &document,
                              AssetRegistry &assets,
                              ModelingSessionService &modeling,
                              AssetId default_material, PivotToolService *pivot,
                              LinearArrayToolService *linear_array);
  void refresh_cached_selection_overlay_frame(ModelingSessionService &modeling,
                                              const SceneDocument &document,
                                              const AssetRegistry &assets);
  void sync_active_tool_from_editor(QuaderEditorApi &editor);
  [[nodiscard]] const ViewportSettings &active_viewport_settings() const;
  [[nodiscard]] Pane &pane(ViewportPaneId pane_id);
  [[nodiscard]] const Pane &pane(ViewportPaneId pane_id) const;

  /// Associates a retained splitter widget with its workspace descriptor.
  struct SplitHandleHost {
    QWidget *widget = nullptr;
    ViewportSplitHandle descriptor = {};
    bool visible = false;
  };

  std::array<Pane, kViewportPaneCount> panes_;
  RenderSystem *render_system_ = nullptr;
  ViewportWorkspaceService *workspace_ = nullptr;
  const ViewportSettings *viewport_settings_ = nullptr;
  LocalizationService *localization_ = nullptr;
  RenderFramePerformanceStats render_performance_ = {};
  ViewportSettings fallback_settings_ = ViewportSettings::defaults();
  ViewportLayoutId displayed_layout_ = ViewportLayoutId::Single;
  std::vector<ViewportPanePlacement> displayed_placements_;
  ViewportInputRouter input_router_;
  ViewportShortcutController shortcut_controller_;
  ViewportHoverController hover_controller_;
  ViewportInteractionController interaction_controller_;
  QtViewportContextMenuPresenter context_menu_presenter_;
  QtViewportDeletePopupPresenter delete_popup_presenter_;
  ViewportPaneOverlayCache selection_overlay_cache_;
  ViewportPaneId interaction_pane_id_ = ViewportPaneId::Perspective;
  ViewportPaneId context_menu_pane_id_ = ViewportPaneId::Perspective;
  QVec2 context_menu_right_press_screen_ = {};
  QVec2 context_menu_right_press_local_ = {};
  QVec2 context_menu_right_drag_delta_ = {};
  bool context_menu_right_press_active_ = false;
  QElapsedTimer input_clock_;
  std::array<SplitHandleHost, 2> split_handles_;
  ViewportSplitHandleId active_split_handle_ =
      ViewportSplitHandleId::TwoHorizontal;
  bool split_handle_dragging_ = false;
  QFocusFrame *focused_pane_frame_ = nullptr;
  QuaderEditorApi *interaction_editor_ = nullptr;
  ModelingBoxPreview active_box_preview_ = {};
  bool pivot_handle_dragging_ = false;
  bool linear_array_handle_dragging_ = false;
  bool modal_tool_dragging_ = false;
  bool modal_tool_owns_viewport_snapshot_ = false;
  bool pending_external_pointer_event_ = false;
  bool pending_external_pointer_left_release_only_ = true;
  bool last_fly_navigation_active_ = false;
  const SceneDocument *viewport_info_cached_document_ = nullptr;
  const AssetRegistry *viewport_info_cached_assets_ = nullptr;
  std::uint64_t viewport_info_cached_scene_revision_ = 0;
  std::uint64_t viewport_info_cached_asset_revision_ = 0;
  ViewportOverlayCamera viewport_info_cached_camera_ = {};
  std::uint64_t viewport_info_cached_objects_ = 0;
  std::uint64_t viewport_info_cached_primitives_ = 0;
  std::uint64_t viewport_info_cached_draw_calls_ = 0;
  bool viewport_info_scene_stats_valid_ = false;
  QtStartupPresentationState native_surface_startup_;
  bool startup_presentation_blocked_ = true;
  bool initialized_ = false;
};

} // namespace quader
