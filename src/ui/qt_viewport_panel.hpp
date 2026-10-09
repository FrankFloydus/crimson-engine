////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_result.hpp"
#include "api/quader_api_types.hpp"
#include "api/quader_commands_api.hpp"
#include "api/quader_operation_input_hint_types.hpp"
#include "api/quader_viewport_layout_types.hpp"
#include "viewport/viewport_runtime_binding_token.hpp"
#include "editor/viewport/viewport_interaction_controller.hpp"
#include "editor/viewport/viewport_curve_picking.hpp"
#include "editor/viewport/transform_gizmo_interaction.hpp"
#include "render/render_system.hpp"
#include "render/viewport_info_stats.hpp"
#include "ui/qt_startup_presentation.hpp"
#include "ui/qt_ui_scale_metrics.hpp"
#include "ui/qt_viewport_context_menu.hpp"
#include "ui/qt_viewport_delete_popup.hpp"
#include "ui/qt_viewport_marking_menu.hpp"
#include "ui/qt_viewport_quick_menu.hpp"
#include "ui/qt_viewport_surface.hpp"
#include "viewport/viewport_host_interaction_policy.hpp"
#include "ui/viewport_hover_controller.hpp"
#include "ui/viewport_shortcut_controller.hpp"
#include "viewport/viewport_input_router.hpp"
#include "viewport/viewport_pointer_gesture.hpp"
#include "viewport/viewport_settings.hpp"
#include "viewport/viewport_workspace_service.hpp"

#include <QPointer>
#include <QPoint>
#include <QWidget>

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

class QAction;
class QFrame;
class QLabel;
class QMenu;
class QMouseEvent;
class QPushButton;
class QToolButton;

namespace quader {

class AssetRegistry;
class ModelingSessionService;
class LocalizationService;
class NativeViewportToolCoordinator;
class QuaderEditorApi;
class QtToolSettingsPanel;
class QtEnvironmentSettingsPopup;
class QtViewportControlStrip;
class QtViewportCameraSettingsPopup;
struct QuaderViewportKeybindingContext;
class RenderOverlaySnapshotCache;
class QtViewportEdgeLengthHud;
class QtViewportFlyNavigationHud;
class SceneDocument;
class SceneRenderBridge;
class ViewportPointPlacementService;

/**
 * Returns whether an application event target belongs to retained settings
 * controls, including the viewport overlay's native child window. The controls are
 * part of an active viewport tool rather than an outside interaction that
 * should cancel that tool.
 */
[[nodiscard]] bool
qt_event_target_belongs_to_tool_settings(const QObject *target);
[[nodiscard]] bool qt_event_target_belongs_to_tool_settings(
    const QObject *target, const QPoint &global_position);

/**
 * Qt Widgets host for Quader's embedded one-to-four pane viewport workspace.
 * ViewportWorkspaceService remains the sole owner of pane/layout state.
 */
class QtViewportPanel final : public QWidget {
public:
  explicit QtViewportPanel(QWidget *parent = nullptr, bool composed = false);
  ~QtViewportPanel() override;

  QtViewportPanel(const QtViewportPanel &) = delete;
  QtViewportPanel &operator=(const QtViewportPanel &) = delete;

  bool initialize(RenderSystem &render_system);
  void shutdown();

  void reset_interactions(QuaderEditorApi *editor,
                          NativeViewportToolCoordinator *native_tools);
  // Drops Qt-owned interaction state without touching the previous non-owning
  // runtime binding, which may already have been destroyed during a rebind.
  void abandon_interactions();

  void attach_workspace(ViewportWorkspaceService &workspace);
  /// Copies action presentation into independent controls for each pane.
  void set_control_actions(const std::vector<QAction *> &actions,
                           QAction *lasso_action = nullptr,
                           QAction *paint_action = nullptr);
  void set_control_action_requested_callback(
      std::function<void(ViewportPaneId, QAction &)> callback);
  /// Requests the owning app's frame flow after native pane geometry/exposure changes.
  void set_presentation_requested_callback(std::function<void()> callback);
  /// Prepares the existing scene projection before a resized composed paint.
  void set_composed_frame_prepare_callback(std::function<void()> callback);
  /// Returns false while a nested paint must wait for the shared projection.
  [[nodiscard]] bool prepare_composed_frame();
  void refresh_control_actions(QuaderEditorApi &editor);
  /// Adopts one retained settings overlay; null detaches the host callbacks.
  void set_settings_panel(QtToolSettingsPanel *panel);
  /// Samples cheap operation metadata; unchanged frames do no widget work.
  void refresh_settings_overlay(QuaderEditorApi &editor);
  [[nodiscard]] std::uint64_t settings_overlay_update_count() const {
    return settings_overlay_update_count_;
  }
  void set_viewport_settings(const ViewportSettings &settings);
  void apply_ui_scale(const QtUiScaleMetrics &metrics);
  [[nodiscard]] float
  viewport_raster_scale(ViewportPaneId pane_id) const;
  void set_render_performance_stats(RenderFramePerformanceStats stats);
  void set_localization(LocalizationService &localization);
  // Sample UI material selection only when a primitive footprint is accepted.
  void set_primitive_creation_material_provider(
      std::function<QuaderMaterialReference()> provider);
  void refresh_keybinding_presentation(QuaderEditorApi &editor);
  void prewarm_retained_dialogs();
  void set_startup_presentation_blocked(bool blocked);
  [[nodiscard]] bool startup_presentation_blocked() const {
    return startup_presentation_blocked_;
  }
  /// Rejects old textures after a load, even if the pane geometry is unchanged.
  void reset_loading_frame_fence();
  [[nodiscard]] bool loading_frame_presented() const;
  /// Keeps existing pane textures visible while synchronous loading is covered.
  void set_loading_presentation_blocked(bool blocked) {
    loading_presentation_blocked_ = blocked;
  }
  [[nodiscard]] bool loading_presentation_blocked() const {
    return loading_presentation_blocked_;
  }
  void refresh_workspace_layout();
  void process_input(QuaderEditorApi &editor, SceneDocument &document,
                     AssetRegistry &assets, ModelingSessionService &modeling,
                     AssetId default_material,
                     NativeViewportToolCoordinator &native_tools,
                     ViewportPointPlacementService *point_placement,
                     double delta_seconds);
  void prepare_viewports(QuaderEditorApi &editor,
                         SceneRenderBridge &render_bridge,
                         RenderOverlaySnapshotCache &snapshot_cache,
                         SceneDocument &document, AssetRegistry &assets,
                         ModelingSessionService &modeling,
                         NativeViewportToolCoordinator &native_tools);
  [[nodiscard]] QuaderApiVoidResult frame_all(const SceneDocument &document,
                                              const AssetRegistry &assets,
                                              ViewportPaneId pane_id);
  [[nodiscard]] QuaderApiVoidResult
  frame_selection(const ModelingSessionService &modeling,
                  ViewportPaneId pane_id);
  [[nodiscard]] std::size_t
  render_viewports(bool account_presentation_frame = true);
  [[nodiscard]] bool needs_continuous_rendering() const;
  [[nodiscard]] bool focus_active_pane();
  [[nodiscard]] const ViewportToolState &tool_state() const {
    return input_router_.state();
  }
  /** Shares current interaction guards with commands dispatched by other panels. */
  [[nodiscard]] QuaderViewportKeybindingContext command_input_context();
  /// Retained semantic projection from the last accepted input frame.
  [[nodiscard]] const QuaderOperationInputContext &operation_input_context() const {
    return operation_input_context_;
  }
  /// Counts retained primitive-feedback updates after revision/geometry gates.
  [[nodiscard]] std::uint64_t primitive_feedback_update_count() const {
    return primitive_feedback_update_count_;
  }

  [[nodiscard]] QtViewportSurface *surface(ViewportPaneId pane_id);
  [[nodiscard]] const QtViewportSurface *surface(ViewportPaneId pane_id) const;

protected:
  bool event(QEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;

private:
  /// Owns the retained Qt presentation objects for one workspace pane.
  struct Pane {
    ViewportPaneId id = ViewportPaneId::Perspective;
    QFrame *frame = nullptr;
    QtViewportControlStrip *toolbar = nullptr;
    QPushButton *display_button = nullptr;
    QPushButton *camera_button = nullptr;
    QToolButton *layout_button = nullptr;
    QMenu *layout_menu = nullptr;
    std::array<QAction *, 8> layout_actions = {};
    QToolButton *camera_settings_button = nullptr;
    QtViewportCameraSettingsPopup *camera_settings_popup = nullptr;
    std::uint64_t camera_settings_generation = 0u;
    std::uint64_t layout_menu_generation = 0u;
    QPushButton *environment_button = nullptr;
    QAction *environment_overflow_action = nullptr;
    QMenu *display_menu = nullptr;
    QMenu *camera_menu = nullptr;
    QMenu *debug_view_menu = nullptr;
    std::array<QAction *, 4> shading_actions = {};
    std::array<QAction *, 7> camera_actions = {};
    std::array<QAction *, 7> debug_view_actions = {};
    QLabel *info_overlay = nullptr;
    QLabel *primitive_feedback = nullptr;
    std::uint64_t primitive_feedback_revision = 0u;
    std::uint64_t primitive_feedback_locale_revision = 0u;
    QRect primitive_feedback_bounds;
    qreal primitive_feedback_dpr = 0.0;
    bool primitive_feedback_valid = false;
    QtViewportEdgeLengthHud *edge_length_hud = nullptr;
    QtViewportFlyNavigationHud *fly_navigation_hud = nullptr;
    QtViewportSurface *surface = nullptr;
    QWidget *surface_container = nullptr;
    QWidget *surface_backing_container = nullptr;
    ViewportView presented_view = ViewportView::Perspective;
    ViewportShadingMode presented_shading = ViewportShadingMode::Shaded;
    ViewportLayoutId presented_layout = ViewportLayoutId::Single;
    bool control_selection_valid = false;
    bool frame_presented = false;
    bool surface_presented = false;
    bool overlay_stack_valid = false;
    std::uint64_t loading_rendered_generation = 0u;
    std::uint64_t loading_rendered_geometry = 0u;
    std::uint64_t loading_submitted_generation = 0u;
    std::uint64_t loading_submitted_geometry = 0u;
    /// Associates a pane-owned presentation action with its metadata source.
    struct ControlAction {
      QPointer<QAction> source;
      QAction *action = nullptr;
    };
    std::vector<ControlAction> control_actions;
    QAction *selection_action = nullptr;
    std::array<ControlAction, 3> selection_choices{};
    std::uint64_t controls_revision = 0u;
    bool controls_valid = false;
    TransformGizmoPresentation idle_gizmo;
    PrimitiveCreationInteraction primitive_presentation;
  };

  void create_pane(Pane &pane);
  void refresh_native_geometry_ancestors();
  void synchronize_native_ancestor_position();
  void refresh_pane_controls(Pane &pane);
  void sync_pane_control_selection(Pane &pane);
  void sync_pane_actions(Pane &pane);
  void dispatch_control_action(Pane &pane, QAction &action);
  void refresh_selection_tool_actions(Pane &pane);
  void scale_pane_controls(Pane &pane);
  [[nodiscard]] QRect pane_content_rect(const Pane &pane) const;
  void update_pane_geometry();
  void update_pane_overlay_geometry(Pane &pane);
  void update_settings_overlay_geometry();
  void abandon_settings_overlay();
  void update_focused_pane_indicator();
  void cancel_navigation();
  void synchronize_pointer_capture(std::optional<ViewportPaneId> owner);
  void request_pane_focus(ViewportPaneId pane_id,
                          ViewportPaneFocusTrigger trigger);
  [[nodiscard]] bool viewport_surface_has_keyboard_focus() const;
  [[nodiscard]] bool pane_surface_has_keyboard_focus(const Pane &pane) const;
  [[nodiscard]] bool external_widget_has_keyboard_focus() const;
  [[nodiscard]] bool pane_controls_have_presentation_focus(const Pane &pane) const;
  void synchronize_control_focus(const QObject *object);
  void refresh_control_hint_context();
  [[nodiscard]] bool pane_interaction_locked() const;
  void set_modal_tool_owns_viewport_snapshot(bool owns_viewport);
  void update_fly_navigation_hud(Pane &pane, bool active,
                                 bool speed_is_default, float speed);
  void hide_fly_navigation_huds();
  void hide_primitive_feedback();
  void present_primitive_feedback(Pane &pane,
                                  const PrimitiveCreationControlsFrame &controls);
  void update_pointer_cursor_presentation();
  void draw_viewport_info_overlay(Pane &pane,
                                  const SceneDocument &document,
                                  const AssetRegistry &assets,
                                  ViewportOverlayCamera camera);
  void update_split_handles();
  void set_pane_view(Pane &pane, ViewportView view);
  void set_pane_shading(Pane &pane, ViewportShadingMode shading);
  [[nodiscard]] std::optional<QPoint> resolve_marking_menu_gesture(
      QuaderEditorApi &editor, Pane &pane, ViewportInputFrame &input,
      bool interaction_locked);
  void process_viewport_input(QuaderEditorApi &editor, SceneDocument &document,
                              AssetRegistry &assets,
                              ModelingSessionService &modeling,
                              AssetId default_material,
                              NativeViewportToolCoordinator &native_tools,
                              ViewportPointPlacementService *point_placement,
                              double delta_seconds);
  bool sync_active_tool_from_editor(QuaderEditorApi &editor,
                                    ModelingSessionService &modeling,
                                    NativeViewportToolCoordinator &native_tools,
                                    ViewportRuntimeBindingToken runtime_binding = {});
  [[nodiscard]] Pane &pane(ViewportPaneId pane_id);
  [[nodiscard]] const Pane &pane(ViewportPaneId pane_id) const;

  /// Associates a retained splitter widget with its workspace descriptor.
  struct SplitHandleHost {
    QWidget *widget = nullptr;
    ViewportSplitHandle descriptor = {};
    bool visible = false;
  };

  std::array<Pane, kViewportPaneCount> panes_;
  std::vector<QPointer<QWidget>> native_geometry_ancestors_;
  QtUiScaleMetrics ui_scale_metrics_;
  RenderSystem *render_system_ = nullptr;
  ViewportWorkspaceService *workspace_ = nullptr;
  QPointer<QtToolSettingsPanel> settings_panel_;
  QtEnvironmentSettingsPopup *environment_popup_ = nullptr;
  std::string settings_target_id_;
  std::uint64_t settings_origin_serial_ = 0;
  std::optional<ViewportPaneId> settings_origin_pane_;
  std::optional<ViewportPaneId> settings_host_pane_;
  bool settings_overlay_geometry_dirty_ = true;
  std::uint64_t settings_overlay_update_count_ = 0;
  const ViewportSettings *viewport_settings_ = nullptr;
  LocalizationService *localization_ = nullptr;
  std::function<QuaderMaterialReference()> primitive_creation_material_provider_;
  std::function<void(ViewportPaneId, QAction &)> control_action_requested_;
  std::function<void()> presentation_requested_;
  std::function<void()> composed_frame_prepare_;
  std::uint64_t composed_geometry_revision_ = 1u;
  std::uint64_t composed_prepared_geometry_revision_ = 0u;
  std::uint64_t loading_frame_generation_ = 1u;
  bool composed_preparing_ = false;
  bool composed_ = false;
  RenderFramePerformanceStats render_performance_ = {};
  ViewportSettings fallback_settings_ = ViewportSettings::defaults();
  ViewportLayoutId displayed_layout_ = ViewportLayoutId::Single;
  std::vector<ViewportPanePlacement> displayed_placements_;
  ViewportInputRouter input_router_;
  // The retained router is shared, but an active gesture always belongs to
  // exactly one workspace camera until release or explicit cancellation.
  std::optional<ViewportPaneId> navigation_camera_owner_;
  ViewportShortcutController shortcut_controller_;
  ViewportHoverController hover_controller_;
  ViewportInteractionController interaction_controller_;
  QuaderOperationInputContext operation_input_context_{.viewport_available = false};
  QtViewportContextMenuPresenter context_menu_presenter_;
  QtViewportDeletePopupPresenter delete_popup_presenter_;
  QtViewportQuickMenuPresenter quick_menu_presenter_;
  QtViewportMarkingMenuPresenter marking_menu_presenter_;
  /// Freezes the semantic launcher's pane and origin until tap or hold resolves.
  struct PendingMarkingMenuGesture {
    ViewportPaneId pane_id = ViewportPaneId::Perspective;
    QuaderInputChord initiating_chord;
    QPoint global_origin;
    std::chrono::steady_clock::time_point started;
    std::uint64_t tool_revision = 0;
    std::uint64_t keybindings_revision = 0;
  };
  std::optional<PendingMarkingMenuGesture> pending_marking_menu_gesture_;
  const SceneDocument *point_light_pick_cached_document_ = nullptr;
  std::uint64_t point_light_pick_cached_scene_revision_ = 0u;
  std::uint64_t point_light_pick_cached_selection_revision_ = 0u;
  std::uint64_t point_light_pick_cached_local_visibility_revision_ = 0u;
  float point_light_pick_cached_viewport_pixel_scale_ = 1.0F;
  std::vector<ViewportSceneBillboardPickCandidate>
      point_light_pick_cached_candidates_;
  bool point_light_pick_cache_valid_ = false;
  std::uint64_t point_light_pick_generation_ = 0;
  ViewportPaneId interaction_pane_id_ = ViewportPaneId::Perspective;
  std::optional<ViewportPaneId> pointer_cursor_pane_;
  std::optional<ViewportPaneId> gizmo_input_pane_;
  std::array<SplitHandleHost, 2> split_handles_;
  ViewportSplitHandleId active_split_handle_ =
      ViewportSplitHandleId::TwoHorizontal;
  bool split_handle_dragging_ = false;
  QWidget *focused_pane_frame_ = nullptr;
  int focused_pane_border_width_ = 0;
  // Non-owning runtime pair. UISystem abandons it without dereferencing it
  // before a runtime rebind because the previous scene services may already
  // be destroyed. Shutdown resets may use it only while the binding is live.
  QuaderEditorApi *interaction_editor_ = nullptr;
  NativeViewportToolCoordinator *interaction_native_tools_ = nullptr;
  // Changes before a synchronous runtime rebind drops the old scene owners.
  // Long input frames compare their captured value after command dispatch.
  std::uint64_t runtime_binding_generation_ = 0;
  std::uint64_t active_tool_revision_ = 0;
  ModelingBoxPreview active_box_preview_ = {};
  bool modal_tool_dragging_ = false;
  bool modal_tool_owns_viewport_snapshot_ = false;
  bool active_tool_revision_valid_ = false;
  std::vector<QuaderInputChord> suppressed_keybinding_chords_;
  std::vector<QuaderViewportKeybindingRepeat> repeated_keybinding_commands_;
  std::array<ViewportPointerReleaseSuppression, 3>
      command_pointer_release_suppression_;
  double linear_array_wheel_remainder_ = 0.0;
  double radial_array_wheel_remainder_ = 0.0;
  ViewportCurvePickCache curve_pick_cache_;
  bool last_fly_navigation_active_ = false;
  bool edge_length_hud_enabled_ = false;
  bool primitive_feedback_input_eligible_ = false;
  std::uint64_t primitive_feedback_update_count_ = 0u;
  const SceneDocument *viewport_info_cached_document_ = nullptr;
  const AssetRegistry *viewport_info_cached_assets_ = nullptr;
  std::uint64_t viewport_info_cached_visibility_revision_ = 0;
  std::uint64_t viewport_info_cached_transform_revision_ = 0;
  std::uint64_t viewport_info_cached_mesh_binding_revision_ = 0;
  std::uint64_t viewport_info_cached_render_component_revision_ = 0;
  std::uint64_t viewport_info_cached_layer_revision_ = 0;
  std::uint64_t viewport_info_cached_asset_revision_ = 0;
  std::uint64_t viewport_info_cached_local_visibility_revision_ = 0;
  ViewportInfoSceneSnapshot viewport_info_cached_scene_snapshot_;
  ViewportPaneId viewport_info_cached_pane_id_ =
      ViewportPaneId::Perspective;
  ViewportOverlayCamera viewport_info_cached_camera_ = {};
  ViewportInfoSceneStats viewport_info_cached_scene_stats_ = {};
  std::chrono::steady_clock::time_point
      viewport_info_last_scene_stats_refresh_{};
  std::chrono::steady_clock::time_point viewport_info_last_text_refresh_{};
  bool viewport_info_scene_snapshot_valid_ = false;
  bool viewport_info_scene_stats_valid_ = false;
  QtStartupPresentationState native_surface_startup_;
  bool startup_presentation_blocked_ = true;
  bool loading_presentation_blocked_ = false;
  bool initialized_ = false;
};

} // namespace quader
