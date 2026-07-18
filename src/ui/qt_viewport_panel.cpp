////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_viewport_panel.hpp"

#include "api/quader_api_type_conversions.hpp"
#include "api/quader_command_ids.hpp"
#include "api/quader_editor_api.hpp"
#include "assets/asset_registry.hpp"
#include "core/localization_service.hpp"
#include "core/logger.hpp"
#include "diagnostics/profile.hpp"
#include "editor/array/linear_array_tool_service.hpp"
#include "editor/array/radial_array_tool_service.hpp"
#include "editor/input/viewport_keybinding_resolver.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"
#include "editor/pivot/pivot_tool_service.hpp"
#include "editor/placement/viewport_point_placement_service.hpp"
#include "editor/viewport/viewport_scene_projection.hpp"
#include "editor/viewport/viewport_tool_overlay_projection.hpp"
#include "editor/render/quader_render_api_runtime_access.hpp"
#include "editor/tools/viewport_tool.hpp"
#include "editor/tools/viewport_tool_catalog.hpp"
#include "editor/tools/viewport_tool_ports.hpp"
#include "editor/viewport/viewport_native_tool_interaction.hpp"
#include "math/glm_bridge.hpp"
#include "render/overlay/viewport_overlay_style_resolver.hpp"
#include "render/render_layers.hpp"
#include "render/scene_render_bridge.hpp"
#include "render/viewport_info_stats.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_entity_naming.hpp"
#include "ui/qt_viewport_chrome_layout.hpp"
#include "ui/qt_viewport_fly_navigation_hud.hpp"
#include "ui/qt_viewport_pane_activation.hpp"
#include "ui/qt_viewport_render_submission_policy.hpp"
#include "ui/viewport_context_menu_policy.hpp"
#include "viewport/viewport_host_interaction_policy.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"
#include "viewport/viewport_camera_adapter.hpp"
#include "viewport/viewport_input_types.hpp"
#include "viewport/viewport_layout.hpp"

#include <QApplication>
#include <QAction>
#include <QActionGroup>
#include <QFrame>
#include <QFocusFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPoint>
#include <QPushButton>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quader {
namespace {

using ViewportProbeClock = std::chrono::steady_clock;

/// Accumulates opt-in viewport cadence and interaction timing diagnostics.
struct ViewportCadenceProbe {
  ViewportProbeClock::time_point window_start = ViewportProbeClock::now();
  std::uint64_t frames = 0;
  std::uint64_t motion_frames = 0;
  std::uint64_t mouse_events = 0;
  std::uint64_t submitted = 0;
  double input_ms = 0.0;
  double preview_sync_ms = 0.0;
  double prepare_ms = 0.0;
  double render_ms = 0.0;
};

ViewportCadenceProbe g_viewport_probe;

double elapsed_milliseconds(ViewportProbeClock::time_point start,
                            ViewportProbeClock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - start).count();
}

bool viewport_frame_diagnostics_enabled() {
  static const bool kEnabled =
      std::getenv("QUADER_FRAME_DIAGNOSTICS") != nullptr;
  return kEnabled;
}

double viewport_component_pick_radius_pixels(double preference_radius) {
  constexpr double kPixelsPerPreferenceUnit = 40.0;
  return std::clamp(preference_radius * kPixelsPerPreferenceUnit, 2.0, 32.0);
}

bool same_pane_placements(const std::vector<ViewportPanePlacement> &left,
                          const std::vector<ViewportPanePlacement> &right) {
  if (left.size() != right.size()) {
    return false;
  }
  for (std::size_t index = 0; index < left.size(); ++index) {
    const ViewportPanePlacement &a = left[index];
    const ViewportPanePlacement &b = right[index];
    if (a.pane_id != b.pane_id || a.normalized_rect.x != b.normalized_rect.x ||
        a.normalized_rect.y != b.normalized_rect.y ||
        a.normalized_rect.width != b.normalized_rect.width ||
        a.normalized_rect.height != b.normalized_rect.height) {
      return false;
    }
  }
  return true;
}

constexpr std::array<ViewportView, 7> kViewportViews{
    ViewportView::Perspective, ViewportView::Top,  ViewportView::Bottom,
    ViewportView::Front,       ViewportView::Back, ViewportView::Side,
    ViewportView::Left,
};

constexpr std::array<ViewportShadingMode, 3> kShadingModes{
    ViewportShadingMode::Wireframe,
    ViewportShadingMode::Shaded,
    ViewportShadingMode::Rendered,
};

std::optional<QuaderCommandId>
shading_command_id(ViewportShadingMode shading) {
  switch (shading) {
  case ViewportShadingMode::Wireframe:
    return quader_command::kViewportShadingWireframe;
  case ViewportShadingMode::Shaded:
    return quader_command::kViewportShadingShaded;
  case ViewportShadingMode::Rendered:
    return quader_command::kViewportShadingRendered;
  case ViewportShadingMode::DiffuseOnly:
  case ViewportShadingMode::SpecularOnly:
  case ViewportShadingMode::Albedo:
  case ViewportShadingMode::Roughness:
  case ViewportShadingMode::Metalness:
  case ViewportShadingMode::Normal:
    return std::nullopt;
  }
  return std::nullopt;
}

constexpr std::array<ViewportShadingMode, 6> kDebugViewModes{
    ViewportShadingMode::DiffuseOnly, ViewportShadingMode::SpecularOnly,
    ViewportShadingMode::Albedo,      ViewportShadingMode::Roughness,
    ViewportShadingMode::Metalness,   ViewportShadingMode::Normal,
};

QString localized_view_label(const LocalizationService *localization,
                             ViewportView view) {
  std::string key = "viewport.view.";
  key.append(viewport_view_stable_id(view));
  const std::string fallback(viewport_view_label(view));
  return QString::fromStdString(
      localization != nullptr ? localization->text(key, fallback) : fallback);
}

std::string_view shading_stable_id(ViewportShadingMode shading) {
  switch (shading) {
  case ViewportShadingMode::Wireframe:
    return "wireframe";
  case ViewportShadingMode::Shaded:
    return "shaded";
  case ViewportShadingMode::Rendered:
    return "rendered";
  case ViewportShadingMode::DiffuseOnly:
    return "diffuse_only";
  case ViewportShadingMode::SpecularOnly:
    return "specular_only";
  case ViewportShadingMode::Albedo:
    return "albedo";
  case ViewportShadingMode::Roughness:
    return "roughness";
  case ViewportShadingMode::Metalness:
    return "metalness";
  case ViewportShadingMode::Normal:
    return "normal";
  }
  return "shaded";
}

std::string_view shading_label(ViewportShadingMode shading) {
  switch (shading) {
  case ViewportShadingMode::Wireframe:
    return "Wireframe";
  case ViewportShadingMode::Shaded:
    return "Shaded";
  case ViewportShadingMode::Rendered:
    return "Rendered";
  case ViewportShadingMode::DiffuseOnly:
    return "Diffuse Only";
  case ViewportShadingMode::SpecularOnly:
    return "Specular Only";
  case ViewportShadingMode::Albedo:
    return "Albedo";
  case ViewportShadingMode::Roughness:
    return "Roughness";
  case ViewportShadingMode::Metalness:
    return "Metalness";
  case ViewportShadingMode::Normal:
    return "Normal";
  }
  return "Shaded";
}

QString localized_shading_label(const LocalizationService *localization,
                                ViewportShadingMode shading) {
  std::string key = viewport_shading_is_debug_view(shading)
                        ? "viewport.debug_view."
                        : "viewport.shading.";
  key.append(shading_stable_id(shading));
  const std::string fallback(shading_label(shading));
  return QString::fromStdString(
      localization != nullptr ? localization->text(key, fallback) : fallback);
}

QString localized_display_menu_label(const LocalizationService *localization,
                                     std::string_view key,
                                     std::string_view fallback) {
  return QString::fromStdString(
      localization != nullptr
          ? localization->text(std::string(key), std::string(fallback))
          : std::string(fallback));
}

ViewportMaterialDebugMode
viewport_material_debug_mode(ViewportShadingMode shading) {
  switch (shading) {
  case ViewportShadingMode::DiffuseOnly:
    return ViewportMaterialDebugMode::DiffuseOnly;
  case ViewportShadingMode::SpecularOnly:
    return ViewportMaterialDebugMode::SpecularOnly;
  case ViewportShadingMode::Albedo:
    return ViewportMaterialDebugMode::Albedo;
  case ViewportShadingMode::Roughness:
    return ViewportMaterialDebugMode::Roughness;
  case ViewportShadingMode::Metalness:
    return ViewportMaterialDebugMode::Metalness;
  case ViewportShadingMode::Normal:
    return ViewportMaterialDebugMode::Normal;
  case ViewportShadingMode::Wireframe:
  case ViewportShadingMode::Shaded:
  case ViewportShadingMode::Rendered:
    return ViewportMaterialDebugMode::None;
  }
  return ViewportMaterialDebugMode::None;
}

QString localized_viewport_info_label(const LocalizationService *localization,
                                      std::string_view key,
                                      std::string_view fallback) {
  return QString::fromStdString(
      localization != nullptr
          ? localization->text(std::string(key), std::string(fallback))
          : std::string(fallback));
}

QString format_viewport_info_milliseconds(double value) {
  if (!(value >= 0.0) || !std::isfinite(value)) {
    return QStringLiteral("--");
  }
  return QStringLiteral("%1 ms").arg(value, 0, 'f', 2);
}

QString format_viewport_info_frames_per_second(double value) {
  if (!(value >= 0.0) || !std::isfinite(value)) {
    return QStringLiteral("--");
  }
  return QString::number(value, 'f', 1);
}

bool same_viewport_info_camera(const ViewportOverlayCamera &left,
                               const ViewportOverlayCamera &right) {
  return left.eye.x == right.eye.x && left.eye.y == right.eye.y &&
         left.eye.z == right.eye.z && left.forward.x == right.forward.x &&
         left.forward.y == right.forward.y &&
         left.forward.z == right.forward.z && left.right.x == right.right.x &&
         left.right.y == right.right.y && left.right.z == right.right.z &&
         left.up.x == right.up.x && left.up.y == right.up.y &&
         left.up.z == right.up.z &&
         left.viewport_size.x == right.viewport_size.x &&
         left.viewport_size.y == right.viewport_size.y &&
         left.projection == right.projection &&
         left.orthographic_size == right.orthographic_size &&
         left.fov_degrees == right.fov_degrees &&
         left.near_plane == right.near_plane &&
         left.far_plane == right.far_plane;
}

void log_command_failure(std::string_view source,
                         const QuaderApiVoidResult &result) {
  if (!result.ok) {
    LOG_ERROR("{} failed: {}", source,
              result.message.empty() ? "Command failed." : result.message);
  }
}

bool has_input_activity(const QtViewportInputFrame &frame) {
  return frame.pointer_activity() ||
         !frame.input.pressed_input_chords.empty() ||
         !frame.input.released_input_chords.empty() ||
         !frame.input.wheel_input_edges.empty();
}

ViewportOverlaySelectionContext
overlay_selection_context(ModelingSelectionMode mode) {
  switch (mode) {
  case ModelingSelectionMode::Vertex:
    return ViewportOverlaySelectionContext::Vertex;
  case ModelingSelectionMode::Edge:
    return ViewportOverlaySelectionContext::Edge;
  case ModelingSelectionMode::Face:
    return ViewportOverlaySelectionContext::Face;
  case ModelingSelectionMode::Group:
    return ViewportOverlaySelectionContext::Group;
  case ModelingSelectionMode::Object:
    return ViewportOverlaySelectionContext::Object;
  }
  return ViewportOverlaySelectionContext::Any;
}


} // namespace

bool qt_event_target_belongs_to_tool_settings(const QObject *target) {
  for (const QObject *current = target; current != nullptr;
       current = current->parent()) {
    if (current->objectName() == QStringLiteral("floating.tool_settings")) {
      return true;
    }
  }

  // Top-level QWidget mouse events can be delivered to the separate
  // QWidgetWindow returned by windowHandle(). That native window is not a
  // QObject child of the widget, so QObject ancestry alone cannot associate
  // it with Tool Settings. Match only the exact window handle of a marked
  // top-level widget; other dialogs and windows remain outside interactions.
  const auto *target_window = qobject_cast<const QWindow *>(target);
  if (target_window == nullptr ||
      qobject_cast<QApplication *>(QCoreApplication::instance()) == nullptr) {
    return false;
  }
  for (const QWidget *top_level : QApplication::topLevelWidgets()) {
    if (top_level != nullptr && top_level->windowHandle() == target_window &&
        top_level->objectName() == QStringLiteral("floating.tool_settings")) {
      return true;
    }
  }
  return false;
}

QtViewportPanel::QtViewportPanel(QWidget *parent) : QWidget(parent) {
  setObjectName(QStringLiteral("viewportWorkspace"));
  setMinimumSize(300, 200);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  setAutoFillBackground(true);

  panes_[viewport_pane_index(ViewportPaneId::Perspective)].id =
      ViewportPaneId::Perspective;
  panes_[viewport_pane_index(ViewportPaneId::Top)].id = ViewportPaneId::Top;
  panes_[viewport_pane_index(ViewportPaneId::Front)].id = ViewportPaneId::Front;
  panes_[viewport_pane_index(ViewportPaneId::Side)].id = ViewportPaneId::Side;
  for (Pane &pane_state : panes_) {
    create_pane(pane_state);
  }
  focused_pane_frame_ = new QFocusFrame(this);
  focused_pane_frame_->setObjectName(
      QStringLiteral("viewportFocusedPaneFrame"));
  focused_pane_frame_->setAttribute(Qt::WA_NativeWindow);
  focused_pane_frame_->setAttribute(Qt::WA_TransparentForMouseEvents);
  focused_pane_frame_->hide();
  for (std::size_t index = 0; index < split_handles_.size(); ++index) {
    SplitHandleHost &handle = split_handles_[index];
    handle.widget = new QWidget(this);
    handle.widget->setObjectName(
        QStringLiteral("viewportSplitHandle_%1").arg(index));
    handle.widget->setMouseTracking(true);
    handle.widget->setAttribute(Qt::WA_NativeWindow);
    handle.widget->installEventFilter(this);
    handle.widget->hide();
  }
  if (qApp != nullptr) {
    qApp->installEventFilter(this);
  }
}

QtViewportPanel::~QtViewportPanel() {
  if (qApp != nullptr) {
    qApp->removeEventFilter(this);
  }
  shutdown();
}

bool QtViewportPanel::initialize(RenderSystem &render_system) {
  if (initialized_) {
    return true;
  }
  render_system_ = &render_system;
  native_surface_startup_ = {};
  bool success = true;
  for (Pane &pane_state : panes_) {
    pane_state.surface_container->hide();
    success =
        pane_state.surface->initialize(
            render_system,
            viewport_grid_layer_mask(viewport_pane_index(pane_state.id))) &&
        success;
  }
  initialized_ = success;
  refresh_workspace_layout();
  return success;
}

void QtViewportPanel::shutdown() {
  if (render_system_ == nullptr) {
    return;
  }
  for (Pane &pane_state : panes_) {
    pane_state.navigation_gizmo->set_camera(nullptr);
    pane_state.surface_container->hide();
    if (pane_state.surface != nullptr) {
      pane_state.surface->set_pointer_capture(false);
      pane_state.surface->shutdown();
    }
  }
  interaction_controller_.reset(interaction_editor_);
  interaction_editor_ = nullptr;
  input_router_.reset();
  hide_fly_navigation_huds();
  focused_pane_frame_->hide();
  last_fly_navigation_active_ = false;
  modal_tool_dragging_ = false;
  modal_tool_owns_viewport_snapshot_ = false;
  pending_external_pointer_event_ = false;
  pending_external_pointer_left_release_only_ = true;
  linear_array_wheel_remainder_ = 0.0;
  radial_array_wheel_remainder_ = 0.0;
  native_surface_startup_ = {};
  startup_presentation_blocked_ = true;
  initialized_ = false;
  render_system_ = nullptr;
  if (workspace_ != nullptr) {
    workspace_->clear_pane_visibility();
  }
}

void QtViewportPanel::attach_workspace(ViewportWorkspaceService &workspace) {
  if (workspace_ != &workspace) {
    interaction_controller_.reset(interaction_editor_);
    hover_controller_.invalidate();
    selection_overlay_cache_.mark_dirty();
    viewport_info_scene_stats_valid_ = false;
    displayed_placements_.clear();
    interaction_controller_.clear_modal_drag_state();
    interaction_controller_.clear_native_tool_drag_state();
    modal_tool_dragging_ = false;
    modal_tool_owns_viewport_snapshot_ = false;
  }
  workspace_ = &workspace;
  for (Pane &pane_state : panes_) {
    pane_state.navigation_gizmo->set_camera(
        &workspace.camera(pane_state.id));
  }
  refresh_workspace_layout();
}

void QtViewportPanel::set_viewport_settings(const ViewportSettings &settings) {
  viewport_settings_ = &settings;
}

void QtViewportPanel::set_render_performance_stats(
    RenderFramePerformanceStats stats) {
  render_performance_ = stats;
}

void QtViewportPanel::set_localization(LocalizationService &localization) {
  localization_ = &localization;
  for (Pane &pane_state : panes_) {
    refresh_pane_controls(pane_state);
  }
}

void QtViewportPanel::set_startup_presentation_blocked(bool blocked) {
  if (startup_presentation_blocked_ == blocked) {
    return;
  }
  startup_presentation_blocked_ = blocked;
  native_surface_startup_ = {};
  if (blocked) {
    for (Pane &pane_state : panes_) {
      pane_state.surface_container->hide();
      pane_state.frame->hide();
    }
    focused_pane_frame_->hide();
    for (SplitHandleHost &handle : split_handles_) {
      handle.widget->hide();
    }
    return;
  }
  displayed_placements_.clear();
  refresh_workspace_layout();
}

void QtViewportPanel::refresh_workspace_layout() {
  if (workspace_ == nullptr) {
    return;
  }
  const std::vector<ViewportPanePlacement> placements =
      workspace_->active_pane_placements();
  const bool geometry_changed =
      !same_pane_placements(displayed_placements_, placements);
  displayed_layout_ = workspace_->layout();
  if (geometry_changed) {
    qt_invalidate_startup_presentation_geometry(native_surface_startup_);
    displayed_placements_ = placements;
    update_pane_geometry();
  }
  for (Pane &pane_state : panes_) {
    sync_pane_control_selection(pane_state);
  }
  update_focused_pane_indicator();
}

void QtViewportPanel::process_input(
    QuaderEditorApi &editor, SceneDocument &document, AssetRegistry &assets,
    ModelingSessionService &modeling, AssetId default_material,
    PivotToolService *pivot, LinearArrayToolService *linear_array,
    RadialArrayToolService *radial_array,
    ViewportPointPlacementService *point_placement, double delta_seconds) {
  const bool diagnostics_enabled = viewport_frame_diagnostics_enabled();
  const ViewportProbeClock::time_point input_start =
      diagnostics_enabled ? ViewportProbeClock::now()
                          : ViewportProbeClock::time_point{};
  process_viewport_input(editor, document, assets, modeling, default_material,
                         pivot, linear_array, radial_array, point_placement,
                         delta_seconds);
  if (diagnostics_enabled) {
    g_viewport_probe.input_ms +=
        elapsed_milliseconds(input_start, ViewportProbeClock::now());
  }
}

void QtViewportPanel::prepare_viewports(
    QuaderEditorApi &editor, SceneRenderBridge &render_bridge,
    RenderOverlaySnapshotCache &snapshot_cache,
    SceneDocument &document, AssetRegistry &assets,
    ModelingSessionService &modeling, PivotToolService *pivot,
    LinearArrayToolService *linear_array,
    RadialArrayToolService *radial_array) {
  QDR_PROFILE_FUNCTION();
  const bool diagnostics_enabled = viewport_frame_diagnostics_enabled();
  const ViewportProbeClock::time_point prepare_start =
      diagnostics_enabled ? ViewportProbeClock::now()
                          : ViewportProbeClock::time_point{};
  if (workspace_ == nullptr) {
    return;
  }
  if (displayed_layout_ != workspace_->layout()) {
    refresh_workspace_layout();
  }
  update_focused_pane_indicator();

  const ViewportProbeClock::time_point preview_start =
      diagnostics_enabled ? ViewportProbeClock::now()
                          : ViewportProbeClock::time_point{};
  (void)QuaderRenderApiRuntimeAccess::sync_tool_previews(editor.render(),
                                                         render_bridge);
  if (diagnostics_enabled) {
    g_viewport_probe.preview_sync_ms +=
        elapsed_milliseconds(preview_start, ViewportProbeClock::now());
  }

  // Preserve the native viewport's semantic-overlay cache contract.  In
  // particular, object-mode face fills are owned by the persistent render
  // cache while authored selection wires remain in this transient frame.
  refresh_cached_selection_overlay_frame(modeling, document, assets);

  const ViewportSettings &settings =
      viewport_settings_ != nullptr ? *viewport_settings_ : fallback_settings_;
  const float grid_world_size =
      viewport_grid_world_size_for_preset(workspace_->grid_preset());
  const bool grid_visible =
      settings.bool_value(ViewportBoolSettingId::ShowGrid);
  const bool overlays_visible =
      settings.bool_value(ViewportBoolSettingId::ShowOverlays);
  const bool gizmos_visible =
      settings.bool_value(ViewportBoolSettingId::ShowGizmos);
  const bool viewport_info_visible =
      settings.bool_value(ViewportBoolSettingId::ShowInfo);
  const bool navigation_gizmo_visible =
      editor.preferences().navigation_gizmo_visible();
  const bool instance_edit_context_active =
      editor.modeling().instance_edit_state().active;
  const ViewportPaneId focused_pane = workspace_->focused_pane();
  for (Pane &pane_state : panes_) {
    pane_state.navigation_gizmo->set_camera(
        &workspace_->camera(pane_state.id));
    pane_state.navigation_gizmo->set_gizmo_visible(
        navigation_gizmo_visible);
    if (!viewport_info_visible || pane_state.id != focused_pane) {
      pane_state.info_overlay->setVisible(false);
    }
  }
  const PivotHandleState pivot_handle =
      pivot != nullptr ? pivot->handle_state() : PivotHandleState{};
  const LinearArrayHandleState linear_array_handle =
      linear_array != nullptr ? linear_array->handle_state()
                              : LinearArrayHandleState{};
  const RadialArrayCenterState radial_array_center =
      radial_array != nullptr ? radial_array->center_state()
                              : RadialArrayCenterState{};
  const bool pivot_handle_editing =
      pivot_handle.visible && pivot_handle.editing;
  render_bridge.update_mesh_surface_grid(grid_world_size, settings);

  const ModelingSelectionMode cached_overlay_mode =
      selection_overlay_cache_.valid()
          ? selection_overlay_cache_.key().selection_mode
          : viewport_selection_mode_to_app(
                input_router_.state().selection_mode);
  const ViewportPersistentOverlayStyleSnapshot persistent_overlay_style =
      ViewportOverlayStyleResolver::persistent_style_snapshot(settings);

  std::uint8_t wireframe_layer_mask = 0u;
  for (const ViewportPanePlacement &placement :
       workspace_->active_pane_placements()) {
    if (workspace_->shading_mode(placement.pane_id) ==
        ViewportShadingMode::Wireframe) {
      wireframe_layer_mask = static_cast<std::uint8_t>(
          wireframe_layer_mask |
          viewport_grid_layer_mask(viewport_pane_index(placement.pane_id)));
    }
  }
  (void)QuaderRenderApiRuntimeAccess::sync_scene_asset_overlays(
      editor.render(), render_bridge, snapshot_cache,
      QuaderRenderSceneOverlaySyncRequest{
          .selected_object_overlay_visible =
              overlays_visible &&
              cached_overlay_mode == ModelingSelectionMode::Object,
          .scene_wireframe_layer_mask = wireframe_layer_mask,
          .selected_scene_wire_visible =
              overlays_visible &&
              (cached_overlay_mode == ModelingSelectionMode::Object ||
               cached_overlay_mode == ModelingSelectionMode::Group),
          .style = persistent_overlay_style,
      });

  std::array<bool, kViewportPaneCount> scene_overlay_requests = {};
  for (const ViewportPanePlacement &placement :
       workspace_->active_pane_placements()) {
    const Pane &pane_state = pane(placement.pane_id);
    if (!pane_state.frame->isVisible()) {
      continue;
    }
    const ViewportShadingMode shading =
        workspace_->shading_mode(placement.pane_id);
    const bool point_light_gizmos_visible =
        gizmos_visible && viewport_shading_shows_light_gizmos(shading);
    scene_overlay_requests[viewport_pane_index(placement.pane_id)] =
        (overlays_visible && selection_overlay_cache_.valid()) ||
        point_light_gizmos_visible;
  }
  const RenderOverlaySelectionSnapshot *scene_overlay_snapshot = nullptr;
  if (viewport_scene_overlay_snapshot_extraction_count(
          scene_overlay_requests) != 0u) {
    scene_overlay_snapshot =
        &QuaderRenderApiRuntimeAccess::build_scene_overlay_snapshot(
            editor.render(), snapshot_cache);
  }

  workspace_->clear_pane_visibility();
  for (const ViewportPanePlacement &placement :
       workspace_->active_pane_placements()) {
    Pane &pane_state = pane(placement.pane_id);
    if (!pane_state.frame->isVisible()) {
      continue;
    }
    workspace_->set_pane_visible(pane_state.id, true);
    // The QWidget container owns native-child geometry, but Qt can defer the
    // corresponding QWindow resize event. Synchronize Filament's viewport
    // before camera projection and grid generation consume its dimensions.
    pane_state.surface->synchronize_render_size();
    const QVec2 render_size = pane_state.surface->render_size_pixels();
    workspace_->set_pane_render_size(pane_state.id, render_size);
    ViewportWorkspacePaneState &pane_workspace =
        workspace_->pane(pane_state.id);
    ViewportRenderer &renderer = pane_state.surface->renderer();
    renderer.set_instance_edit_context_active(
        instance_edit_context_active,
        settings.color(ViewportColorSettingId::InstanceEditBackground));
    renderer.set_material_debug_mode(
        viewport_material_debug_mode(pane_workspace.shading));

    if (const EntityId root = document.scene_root(); root != kInvalidEntityId) {
      if (const SceneSettingsComponent *scene_settings =
              document.scene_settings(root)) {
        pane_workspace.camera.set_clip_planes(scene_settings->camera.near_plane,
                                              scene_settings->camera.far_plane);
        renderer.apply_scene_settings(
            *scene_settings,
            viewport_shading_uses_scene_lighting(pane_workspace.shading));
      }
    }
    renderer.update_camera(pane_workspace.camera);
    renderer.update_ground_grid(pane_workspace.camera, grid_world_size,
                                settings, grid_visible);
    renderer.set_scene_layer_mask(
        viewport_shading_uses_rendered_materials(pane_workspace.shading)
                                      ? kViewportRenderedSceneLayerMask
                                      : kViewportShadedSceneLayerMask);
    renderer.set_shadowing_enabled(
        viewport_shading_uses_scene_lighting(pane_workspace.shading));
    renderer.set_scene_visible(pane_workspace.shading !=
                               ViewportShadingMode::Wireframe);
    const bool tool_overlay_visible =
        pane_state.id == interaction_pane_id_ && active_box_preview_.valid &&
        active_box_preview_.drag_state != ModelingBoxDragState::Height;
    const bool radial_array_center_visible =
        should_show_point_placement_overlay(
            radial_array_center.visible, radial_array_center.locked,
            pane_state.id == interaction_pane_id_);
    renderer.set_overlay_visible(should_enable_viewport_overlay_pass({
        .overlays_visible = overlays_visible,
        .gizmos_visible = gizmos_visible,
        .wireframe_pane =
            pane_workspace.shading == ViewportShadingMode::Wireframe,
        .pivot_handle_visible = pivot_handle.visible,
        .native_tool_overlay_visible =
            linear_array_handle.visible || radial_array_center_visible,
        .tool_overlay_visible = tool_overlay_visible,
    }));

    const ViewportOverlayCamera overlay_camera =
        ViewportCameraAdapter::overlay_camera(pane_workspace.camera,
                                              pane_workspace.render_size);
    if (viewport_info_visible && pane_state.id == focused_pane) {
      draw_viewport_info_overlay(pane_state, document, assets, overlay_camera);
    }
    ViewportOverlayFrame overlay_frame =
        overlays_visible && selection_overlay_cache_.valid()
            ? selection_overlay_cache_.frame()
            : ViewportOverlayFrame{};
    const bool point_light_gizmos_visible =
        gizmos_visible &&
        viewport_shading_shows_light_gizmos(pane_workspace.shading);
    if (scene_overlay_requests[viewport_pane_index(pane_state.id)]) {
      overlay_frame = QuaderRenderApiRuntimeAccess::build_scene_overlay_frame(
          render_bridge, std::move(overlay_frame), *scene_overlay_snapshot,
          ViewportSceneOverlayFrameRequest{
              .selection_mode = ViewportToolOverlayProjection::selection_mode(
                  cached_overlay_mode),
              .wireframe_pane =
                  pane_workspace.shading == ViewportShadingMode::Wireframe,
              .point_light_gizmos_visible = point_light_gizmos_visible,
          });
    }
    if (pane_state.id == interaction_pane_id_) {
      if (tool_overlay_visible) {
        ViewportToolOverlayProjection::append_box(
            overlay_frame, overlay_camera, active_box_preview_);
      }
      if (gizmos_visible && !pivot_handle_editing) {
        if (const quader_editor_core::TransformGizmoFrame *gizmo =
                interaction_controller_.transform_gizmo_frame()) {
          ViewportToolOverlayProjection::append_transform_gizmo(
              overlay_frame, overlay_camera, *gizmo);
        }
      }
      if (overlays_visible) {
        ViewportToolOverlayProjection::append_selection_marquee(
            overlay_frame, overlay_camera,
            interaction_controller_.selection_marquee());
      }
    }
    ViewportToolOverlayProjection::append_pivot_handle(
        overlay_frame, pane_workspace.camera, pivot_handle);
    ViewportToolOverlayProjection::append_linear_array_handle(
        overlay_frame, linear_array_handle);
    if (radial_array_center_visible) {
      ViewportToolOverlayProjection::append_radial_array_center(
          overlay_frame, radial_array_center);
    }
    ViewportOverlayStyleResolver::apply(
        overlay_frame, settings,
        ViewportOverlayStyleContext{
            .selection_mode = overlay_selection_context(cached_overlay_mode),
        });
    render_bridge.set_overlay_frame(
        viewport_pane_index(pane_state.id),
        viewport_grid_layer_mask(viewport_pane_index(pane_state.id)),
        std::move(overlay_frame), overlay_camera);
    sync_pane_control_selection(pane_state);
  }
  if (diagnostics_enabled) {
    g_viewport_probe.prepare_ms +=
        elapsed_milliseconds(prepare_start, ViewportProbeClock::now());
    ++g_viewport_probe.frames;
  }
}

void QtViewportPanel::refresh_cached_selection_overlay_frame(
    ModelingSessionService &modeling, const SceneDocument &document,
    const AssetRegistry &assets) {
  const ModelingSelectionSummary overlay_summary = modeling.selection_summary();
  const ModelingSelectionOverlayOptions overlay_options{
      .suppress_persistent_object_selection =
          overlay_summary.mode == ModelingSelectionMode::Object,
  };
  const ViewportLocalVisibilitySnapshot local_visibility =
      workspace_ != nullptr ? workspace_->local_visibility_snapshot()
                            : ViewportLocalVisibilitySnapshot{};
  const ViewportPaneOverlayCacheKey key{
      .content_revision = overlay_summary.content_revision,
      .selection_revision = overlay_summary.selection_revision,
      .scene_revision = document.revision(),
      .asset_revision = assets.revision(),
      .local_visibility_revision = local_visibility.revision(),
      .overlay_signature =
          modeling.selection_overlay_signature(overlay_options),
      .selection_mode = overlay_summary.mode,
  };
  if (!selection_overlay_cache_.needs_refresh(key)) {
    return;
  }

  selection_overlay_cache_.update(
      modeling.selection_overlay_frame(overlay_options), key);
}

std::size_t
QtViewportPanel::render_viewports(bool account_presentation_frame) {
  QDR_PROFILE_FUNCTION();
  const bool diagnostics_enabled = viewport_frame_diagnostics_enabled();
  const ViewportProbeClock::time_point render_start =
      diagnostics_enabled ? ViewportProbeClock::now()
                          : ViewportProbeClock::time_point{};
  if (!initialized_ || startup_presentation_blocked_ || workspace_ == nullptr ||
      render_system_ == nullptr) {
    return 0u;
  }
  const qreal pixel_scale = devicePixelRatioF();
  const int host_width_pixels =
      static_cast<int>(std::lround(width() * pixel_scale));
  const int host_height_pixels =
      static_cast<int>(std::lround(height() * pixel_scale));
  const QWindow *host_window =
      window() != nullptr ? window()->windowHandle() : nullptr;
  const std::uint64_t geometry_signature =
      (static_cast<std::uint64_t>(static_cast<std::uint32_t>(
           host_width_pixels))
       << 32u) |
      static_cast<std::uint32_t>(host_height_pixels);
  const QtStartupPresentationAction startup_action =
      qt_startup_presentation_action(
          native_surface_startup_,
          host_window != nullptr && host_window->isExposed(),
          geometry_signature);
  if (!qt_startup_native_child_should_be_visible(startup_action)) {
    for (Pane &pane_state : panes_) {
      pane_state.surface_container->hide();
    }
    return 0u;
  }
  if (startup_action == QtStartupPresentationAction::GeometryStabilized) {
    for (Pane &pane_state : panes_) {
      pane_state.surface_container->show();
      update_pane_overlay_geometry(pane_state);
    }
    update_focused_pane_indicator();
    return 0u;
  }
  if (account_presentation_frame) {
    render_system_->begin_presentation_frame();
  }
  const std::vector<ViewportPanePlacement> active_placements =
      workspace_->active_pane_placements();
  const std::size_t submitted = qt_submit_viewport_render_frames(
      active_placements, input_router_.navigating(), interaction_pane_id_,
      [this](const ViewportPanePlacement &placement) {
        Pane &pane_state = pane(placement.pane_id);
        return workspace_->pane_visible(pane_state.id) &&
               pane_state.surface->render_frame();
      });
  if (account_presentation_frame) {
    render_system_->complete_presentation_frame(submitted);
  }
  if (diagnostics_enabled) {
    const ViewportProbeClock::time_point now = ViewportProbeClock::now();
    g_viewport_probe.render_ms += elapsed_milliseconds(render_start, now);
    g_viewport_probe.submitted += submitted;
    const double window_seconds =
        std::chrono::duration<double>(now - g_viewport_probe.window_start)
            .count();
    if (window_seconds >= 2.0 && g_viewport_probe.frames > 0u) {
      const double frames = static_cast<double>(g_viewport_probe.frames);
      LOG_INFO("Qt viewport cadence fps={:.1f} mouse_events={} "
               "motion_frames={} submitted_per_frame={:.2f} "
               "avg_input_ms={:.3f} avg_preview_sync_ms={:.3f} "
               "avg_prepare_ms={:.3f} avg_render_ms={:.3f}",
               frames / window_seconds, g_viewport_probe.mouse_events,
               g_viewport_probe.motion_frames,
               static_cast<double>(g_viewport_probe.submitted) / frames,
               g_viewport_probe.input_ms / frames,
               g_viewport_probe.preview_sync_ms / frames,
               g_viewport_probe.prepare_ms / frames,
               g_viewport_probe.render_ms / frames);
      g_viewport_probe = ViewportCadenceProbe{};
    }
  }
  return submitted;
}

QuaderApiVoidResult QtViewportPanel::frame_all(const SceneDocument &document,
                                               const AssetRegistry &assets,
                                               ViewportPaneId pane_id) {
  if (workspace_ == nullptr) {
    return {.ok = false,
            .changed = false,
            .message = "Viewport workspace is not attached."};
  }
  const ViewportLocalVisibilitySnapshot local_visibility =
      workspace_->local_visibility_snapshot();
  (void)workspace_->camera(pane_id).focus_optional_bounds(
      ViewportSceneProjection::world_bounds(document, assets,
                                            local_visibility),
      ViewportEmptyFocusBehavior::FocusOrigin);
  return {.ok = true,
          .changed = true,
          .message = "Focus All",
          .mutation_kind = QuaderMutationKind::Runtime};
}

QuaderApiVoidResult
QtViewportPanel::frame_selection(const ModelingSessionService &modeling,
                                 ViewportPaneId pane_id) {
  if (workspace_ == nullptr) {
    return {.ok = false,
            .changed = false,
            .message = "Viewport workspace is not attached."};
  }
  const std::optional<ViewportFocusBounds> bounds =
      ViewportSceneProjection::selection_bounds(modeling);
  if (!bounds.has_value()) {
    return {
        .ok = false, .changed = false, .message = "Select something to focus."};
  }
  (void)workspace_->camera(pane_id).focus_optional_bounds(bounds);
  return {.ok = true,
          .changed = true,
          .message = "Focus Selection",
          .mutation_kind = QuaderMutationKind::Runtime};
}

bool QtViewportPanel::needs_continuous_rendering() const {
  // The Filament child swapchains are interactive editor surfaces, not static
  // document previews. Keep the app on its active timer while the workspace is
  // visible so the first pointer event and subsequent navigation are never
  // quantized by the 100 ms idle timer.
  return initialized_ && isVisible() &&
         (window() == nullptr || !window()->isMinimized());
}

bool QtViewportPanel::focus_active_pane() {
  if (workspace_ == nullptr) {
    return false;
  }
  request_pane_focus(workspace_->focused_pane(),
                     ViewportPaneFocusTrigger::Programmatic);
  return true;
}

QtViewportSurface *QtViewportPanel::surface(ViewportPaneId pane_id) {
  return pane(pane_id).surface;
}

const QtViewportSurface *
QtViewportPanel::surface(ViewportPaneId pane_id) const {
  return pane(pane_id).surface;
}

bool QtViewportPanel::eventFilter(QObject *watched, QEvent *event) {
  if (event != nullptr && modal_tool_owns_viewport_snapshot_ &&
      !is_viewport_internal_event_target(watched) &&
      (event->type() == QEvent::MouseButtonPress ||
       event->type() == QEvent::MouseButtonDblClick ||
       event->type() == QEvent::MouseButtonRelease)) {
    // Pane-owned chrome and its combo popups are internal by QObject ancestry.
    // Split handles, docks, menus and dialogs remain external. Active Qt
    // popups/modals are intentionally not blanket exemptions: their keyboard
    // scope is protected by QtCommandEventFilter, while an outside pointer
    // gesture still exits an already-active viewport modal tool.
    queue_external_pointer_event(*static_cast<QMouseEvent *>(event));
  }

  auto found = std::find_if(
      split_handles_.begin(), split_handles_.end(),
      [&](const SplitHandleHost &host) { return host.widget == watched; });
  if (found == split_handles_.end() || event == nullptr) {
    return QWidget::eventFilter(watched, event);
  }

  auto update_from_mouse = [&](const QMouseEvent &mouse) {
    if (workspace_ == nullptr) {
      return;
    }
    const QPoint local = mapFromGlobal(mouse.globalPosition().toPoint());
    const float normalized =
        found->descriptor.axis == ViewportSplitHandleAxis::Vertical
            ? static_cast<float>(local.x()) /
                  static_cast<float>(std::max(1, width()))
            : static_cast<float>(local.y()) /
                  static_cast<float>(std::max(1, height()));
    workspace_->set_split_handle_position(active_split_handle_, normalized);
    update_pane_geometry();
  };

  switch (event->type()) {
  case QEvent::MouseButtonPress: {
    auto &mouse = *static_cast<QMouseEvent *>(event);
    if (mouse.button() != Qt::LeftButton || !found->visible) {
      break;
    }
    active_split_handle_ = found->descriptor.id;
    split_handle_dragging_ = true;
    found->widget->grabMouse();
    update_from_mouse(mouse);
    mouse.accept();
    return true;
  }
  case QEvent::MouseMove: {
    if (!split_handle_dragging_) {
      break;
    }
    auto &mouse = *static_cast<QMouseEvent *>(event);
    update_from_mouse(mouse);
    mouse.accept();
    return true;
  }
  case QEvent::MouseButtonRelease: {
    auto &mouse = *static_cast<QMouseEvent *>(event);
    if (!split_handle_dragging_ || mouse.button() != Qt::LeftButton) {
      break;
    }
    update_from_mouse(mouse);
    split_handle_dragging_ = false;
    if (QWidget::mouseGrabber() == found->widget) {
      found->widget->releaseMouse();
    }
    mouse.accept();
    return true;
  }
  default:
    break;
  }
  return QWidget::eventFilter(watched, event);
}

void QtViewportPanel::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  qt_invalidate_startup_presentation_geometry(native_surface_startup_);
  update_pane_geometry();
}

void QtViewportPanel::create_pane(Pane &pane_state) {
  pane_state.frame = new QFrame(this);
  pane_state.frame->setObjectName(
      QStringLiteral("viewportPane_%1")
          .arg(QString::fromUtf8(
              viewport_pane_stable_id(pane_state.id).data())));
  pane_state.frame->setFrameShape(QFrame::StyledPanel);
  pane_state.frame->setSizePolicy(QSizePolicy::Expanding,
                                  QSizePolicy::Expanding);
  pane_state.frame->hide();

  pane_state.surface = new QtViewportSurface(pane_state.id);
  pane_state.surface_container =
      QWidget::createWindowContainer(pane_state.surface, pane_state.frame);
  pane_state.surface_container->setObjectName(
      QStringLiteral("viewportSurfaceContainer_%1")
          .arg(QString::fromUtf8(
              viewport_pane_stable_id(pane_state.id).data())));
  pane_state.surface_container->setFocusPolicy(Qt::StrongFocus);
  pane_state.surface_container->setSizePolicy(QSizePolicy::Expanding,
                                              QSizePolicy::Expanding);
  pane_state.surface_container->setGeometry(pane_state.frame->rect());
  pane_state.surface_container->hide();
  pane_state.surface_container->installEventFilter(pane_state.surface);
  pane_state.surface->set_focus_request_callback(
      [this](ViewportPaneId pane_id, ViewportPaneFocusTrigger trigger) {
        request_pane_focus(pane_id, trigger);
      });

  pane_state.toolbar = new QWidget(pane_state.frame);
  pane_state.toolbar->setObjectName(
      QStringLiteral("viewportToolbar_%1")
          .arg(QString::fromUtf8(
              viewport_pane_stable_id(pane_state.id).data())));
  pane_state.toolbar->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  pane_state.toolbar->setAttribute(Qt::WA_NativeWindow);
  auto *toolbar_layout = new QHBoxLayout(pane_state.toolbar);
  toolbar_layout->setContentsMargins(0, 0, 0, 0);

  pane_state.display_button = new QPushButton(pane_state.toolbar);
  pane_state.display_button->setObjectName(
      QStringLiteral("viewportShading_%1")
          .arg(QString::fromUtf8(
              viewport_pane_stable_id(pane_state.id).data())));
  pane_state.display_menu = new QMenu(pane_state.display_button);
  pane_state.display_button->setMenu(pane_state.display_menu);

  auto *display_group = new QActionGroup(pane_state.display_menu);
  display_group->setExclusive(true);
  for (std::size_t index = 0; index < kShadingModes.size(); ++index) {
    QAction *action = pane_state.display_menu->addAction(QString());
    action->setCheckable(true);
    display_group->addAction(action);
    pane_state.shading_actions[index] = action;
    const ViewportShadingMode shading = kShadingModes[index];
    connect(action, &QAction::triggered, this,
            [this, &pane_state, shading]() {
              if (workspace_ != nullptr) {
                (void)workspace_->focus_pane(pane_state.id);
                update_focused_pane_indicator();
              }
              const std::optional<QuaderCommandId> command_id =
                  shading_command_id(shading);
              if (interaction_editor_ != nullptr && command_id.has_value()) {
                (void)interaction_editor_->commands().execute(*command_id);
                sync_pane_control_selection(pane_state);
              } else {
                set_pane_shading(pane_state, shading);
              }
            });
  }

  pane_state.display_menu->addSeparator();
  pane_state.camera_menu = new QMenu(pane_state.display_menu);
  pane_state.display_menu->addMenu(pane_state.camera_menu);
  auto *camera_group = new QActionGroup(pane_state.camera_menu);
  camera_group->setExclusive(true);
  for (std::size_t index = 0; index < kViewportViews.size(); ++index) {
    QAction *action = pane_state.camera_menu->addAction(QString());
    action->setCheckable(true);
    camera_group->addAction(action);
    pane_state.camera_actions[index] = action;
    const ViewportView view = kViewportViews[index];
    connect(action, &QAction::triggered, this,
            [this, &pane_state, view]() {
              if (workspace_ != nullptr) {
                (void)workspace_->focus_pane(pane_state.id);
                update_focused_pane_indicator();
              }
              set_pane_view(pane_state, view);
            });
  }

  pane_state.debug_view_menu = new QMenu(pane_state.display_menu);
  pane_state.display_menu->addMenu(pane_state.debug_view_menu);
  for (std::size_t index = 0; index < kDebugViewModes.size(); ++index) {
    QAction *action = pane_state.debug_view_menu->addAction(QString());
    action->setCheckable(true);
    display_group->addAction(action);
    pane_state.debug_view_actions[index] = action;
    const ViewportShadingMode shading = kDebugViewModes[index];
    connect(action, &QAction::triggered, this,
            [this, &pane_state, shading]() {
              if (workspace_ != nullptr) {
                (void)workspace_->focus_pane(pane_state.id);
                update_focused_pane_indicator();
              }
              set_pane_shading(pane_state, shading);
            });
  }

  toolbar_layout->addWidget(pane_state.display_button);
  pane_state.toolbar->adjustSize();

  pane_state.info_overlay = new QLabel(pane_state.frame);
  pane_state.info_overlay->setObjectName(
      QStringLiteral("viewportInfo_%1")
          .arg(QString::fromUtf8(
              viewport_pane_stable_id(pane_state.id).data())));
  pane_state.info_overlay->setTextFormat(Qt::PlainText);
  pane_state.info_overlay->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  pane_state.info_overlay->setSizePolicy(QSizePolicy::Fixed,
                                         QSizePolicy::Fixed);
  pane_state.info_overlay->setAttribute(Qt::WA_NativeWindow);
  pane_state.info_overlay->setAttribute(Qt::WA_TransparentForMouseEvents);
  pane_state.info_overlay->hide();

  pane_state.fly_navigation_hud =
      new QtViewportFlyNavigationHud(*pane_state.frame, pane_state.id);

  pane_state.navigation_gizmo =
      new QtNavigationGizmo(pane_state.id, pane_state.frame);
  pane_state.navigation_gizmo->set_focus_pane_callback(
      [this, &pane_state](ViewportPaneId pane_id) {
        request_pane_focus(pane_id,
                           ViewportPaneFocusTrigger::PointerPress);
        interaction_pane_id_ = pane_id;
        hover_controller_.invalidate();
        pane_state.navigation_gizmo->raise();
      });
  update_pane_overlay_geometry(pane_state);
  pane_state.toolbar->raise();

  refresh_pane_controls(pane_state);
}

void QtViewportPanel::refresh_keybinding_presentation(
    QuaderEditorApi &editor) {
  interaction_editor_ = &editor;
  for (Pane &pane_state : panes_) {
    refresh_pane_controls(pane_state);
  }
}

void QtViewportPanel::refresh_pane_controls(Pane &pane_state) {
  for (std::size_t index = 0; index < kShadingModes.size(); ++index) {
    const ViewportShadingMode shading = kShadingModes[index];
    QString text = localized_shading_label(localization_, shading);
    if (interaction_editor_ != nullptr) {
      const std::optional<QuaderCommandId> command_id =
          shading_command_id(shading);
      if (command_id.has_value()) {
        const std::string shortcut =
            interaction_editor_->commands().shortcut_for(*command_id);
        if (!shortcut.empty()) {
          text.append(QLatin1Char('\t'));
          text.append(QString::fromStdString(shortcut));
        }
      }
    }
    pane_state.shading_actions[index]->setText(text);
  }
  pane_state.camera_menu->setTitle(localized_display_menu_label(
      localization_, "viewport.display.camera", "Camera"));
  for (std::size_t index = 0; index < kViewportViews.size(); ++index) {
    pane_state.camera_actions[index]->setText(
        localized_view_label(localization_, kViewportViews[index]));
  }
  pane_state.debug_view_menu->setTitle(localized_display_menu_label(
      localization_, "viewport.display.debug_view", "Debug View"));
  for (std::size_t index = 0; index < kDebugViewModes.size(); ++index) {
    pane_state.debug_view_actions[index]->setText(
        localized_shading_label(localization_, kDebugViewModes[index]));
  }
  sync_pane_control_selection(pane_state);
  pane_state.toolbar->adjustSize();
  update_pane_overlay_geometry(pane_state);
}

void QtViewportPanel::sync_pane_control_selection(Pane &pane_state) {
  if (workspace_ == nullptr) {
    return;
  }
  const ViewportView view = workspace_->view(pane_state.id);
  const ViewportShadingMode shading =
      workspace_->shading_mode(pane_state.id);
  for (std::size_t index = 0; index < kViewportViews.size(); ++index) {
    pane_state.camera_actions[index]->setChecked(kViewportViews[index] == view);
  }
  for (std::size_t index = 0; index < kShadingModes.size(); ++index) {
    pane_state.shading_actions[index]->setChecked(
        kShadingModes[index] == shading);
  }
  for (std::size_t index = 0; index < kDebugViewModes.size(); ++index) {
    pane_state.debug_view_actions[index]->setChecked(
        kDebugViewModes[index] == shading);
  }
  const QString display_label = localized_shading_label(localization_, shading);
  if (pane_state.display_button->text() != display_label) {
    pane_state.display_button->setText(display_label);
    pane_state.toolbar->adjustSize();
    update_pane_overlay_geometry(pane_state);
  }
}

void QtViewportPanel::update_pane_geometry() {
  if (workspace_ == nullptr) {
    return;
  }
  std::array<bool, kViewportPaneCount> active{};
  std::array<const ViewportPanePlacement *, kViewportPaneCount> placements{};
  const int full_width = std::max(1, width());
  const int full_height = std::max(1, height());
  const std::vector<ViewportPanePlacement> active_placements =
      workspace_->active_pane_placements();
  for (const ViewportPanePlacement &placement : active_placements) {
    const std::size_t index = viewport_pane_index(placement.pane_id);
    active[index] = true;
    placements[index] = &placement;
  }
  qt_apply_viewport_pane_activation(
      active,
      [this](std::size_t index, bool visible) {
        panes_[index].frame->setVisible(
            visible && !startup_presentation_blocked_);
      },
      [this, &placements, full_width, full_height](std::size_t index) {
        Pane &pane_state = panes_[index];
        const ViewportNormalizedRect &rect =
            placements[index]->normalized_rect;
        const int x = static_cast<int>(std::lround(rect.x * full_width));
        const int y = static_cast<int>(std::lround(rect.y * full_height));
        const int right = static_cast<int>(
            std::lround((rect.x + rect.width) * full_width));
        const int bottom = static_cast<int>(
            std::lround((rect.y + rect.height) * full_height));
        pane_state.frame->setGeometry(x, y, std::max(1, right - x),
                                      std::max(1, bottom - y));
        update_pane_overlay_geometry(pane_state);
      });
  update_split_handles();
  update_focused_pane_indicator();
}

void QtViewportPanel::update_pane_overlay_geometry(Pane &pane_state) {
  if (pane_state.frame == nullptr || pane_state.surface_container == nullptr ||
      pane_state.toolbar == nullptr || pane_state.display_button == nullptr ||
      pane_state.info_overlay == nullptr ||
      pane_state.navigation_gizmo == nullptr ||
      pane_state.fly_navigation_hud == nullptr) {
    return;
  }
  pane_state.surface_container->setGeometry(pane_state.frame->rect());
  constexpr int kOverlayInset = 6;
  pane_state.display_button->setMinimumWidth(0);
  pane_state.display_button->setMaximumWidth(QWIDGETSIZE_MAX);
  const QSize display_size = pane_state.display_button->sizeHint();
  const QtViewportChromeLayout chrome = qt_viewport_chrome_layout(
      pane_state.frame->width(), display_size.width(), kOverlayInset);
  pane_state.display_button->setFixedWidth(chrome.menu_width);
  pane_state.display_menu->setMinimumWidth(chrome.menu_width);
  const int toolbar_height = std::max(1, display_size.height());
  pane_state.toolbar->setGeometry(
      chrome.toolbar_x, kOverlayInset, chrome.toolbar_width, toolbar_height);
  pane_state.toolbar->raise();

  constexpr int kNavigationGizmoExtent = 96;
  const int navigation_top =
      pane_state.toolbar->geometry().bottom() + kOverlayInset;
  const int available_width =
      std::max(1, pane_state.frame->width() - kOverlayInset * 2);
  const int available_height =
      std::max(1, pane_state.frame->height() - navigation_top -
                      kOverlayInset);
  const int navigation_extent =
      std::min({kNavigationGizmoExtent, available_width, available_height});
  pane_state.navigation_gizmo->setGeometry(
      std::max(kOverlayInset,
               pane_state.frame->width() - navigation_extent -
                   kOverlayInset),
      navigation_top, navigation_extent, navigation_extent);
  pane_state.navigation_gizmo->raise();

  pane_state.fly_navigation_hud->set_bounds(pane_state.frame->rect());

  pane_state.info_overlay->adjustSize();
  const QSize info_size = pane_state.info_overlay->sizeHint();
  pane_state.info_overlay->setGeometry(
      std::max(kOverlayInset,
               pane_state.frame->width() - info_size.width() - kOverlayInset),
      std::max(kOverlayInset,
               pane_state.frame->height() - info_size.height() - kOverlayInset),
      info_size.width(), info_size.height());
  pane_state.info_overlay->raise();
}

void QtViewportPanel::update_focused_pane_indicator() {
  if (workspace_ == nullptr || focused_pane_frame_ == nullptr) {
    return;
  }
  if (startup_presentation_blocked_) {
    focused_pane_frame_->hide();
    return;
  }
  const std::vector<ViewportPanePlacement> placements =
      workspace_->active_pane_placements();
  if (placements.size() <= 1u) {
    focused_pane_frame_->hide();
    return;
  }
  const ViewportPaneId focused_pane = workspace_->focused_pane();
  const auto focused = std::find_if(
      placements.begin(), placements.end(),
      [focused_pane](const ViewportPanePlacement &placement) {
        return placement.pane_id == focused_pane;
      });
  if (focused == placements.end()) {
    focused_pane_frame_->hide();
    return;
  }

  QWidget *target = pane(focused_pane).frame;
  if (focused_pane_frame_->widget() != target) {
    focused_pane_frame_->setWidget(target);
  }
  focused_pane_frame_->show();
  focused_pane_frame_->raise();
}

void QtViewportPanel::request_pane_focus(
    ViewportPaneId pane_id, ViewportPaneFocusTrigger trigger) {
  if (workspace_ == nullptr) {
    return;
  }
  const ViewportPaneFocusDecision decision = viewport_pane_focus_decision({
      .trigger = trigger,
      .interaction_locked = pane_interaction_locked(),
      .requested_pane_is_interaction_owner =
          pane_id == interaction_pane_id_,
      .viewport_keyboard_focus = viewport_surface_has_keyboard_focus(),
  });
  if (!decision.focus_workspace_pane) {
    return;
  }

  (void)workspace_->focus_pane(pane_id);
  update_focused_pane_indicator();
  if (!decision.focus_native_surface) {
    return;
  }

  Pane &pane_state = pane(pane_id);
  const Qt::FocusReason reason =
      trigger == ViewportPaneFocusTrigger::Programmatic
          ? Qt::OtherFocusReason
          : Qt::MouseFocusReason;
  pane_state.surface_container->setFocus(reason);
  pane_state.surface->requestActivate();
}

bool QtViewportPanel::viewport_surface_has_keyboard_focus() const {
  const QObject *focus_object = QGuiApplication::focusObject();
  const QWindow *focus_window = QGuiApplication::focusWindow();
  const QWidget *focus_widget = QApplication::focusWidget();
  for (const Pane &pane_state : panes_) {
    if (focus_object == pane_state.surface ||
        focus_window == pane_state.surface ||
        focus_widget == pane_state.surface_container ||
        (focus_widget != nullptr && pane_state.surface_container != nullptr &&
         pane_state.surface_container->isAncestorOf(focus_widget))) {
      return true;
    }
  }
  return false;
}

bool QtViewportPanel::pane_interaction_locked() const {
  if (input_router_.navigating() || interaction_controller_.dragging() ||
      interaction_controller_.native_tool_dragging() ||
      modal_tool_dragging_) {
    return true;
  }
  return std::any_of(panes_.begin(), panes_.end(), [](const Pane &pane_state) {
    return pane_state.navigation_gizmo != nullptr &&
           pane_state.navigation_gizmo->input_locked();
  });
}

bool QtViewportPanel::is_viewport_internal_event_target(
    const QObject *target) const {
  if (qt_event_target_belongs_to_tool_settings(target)) {
    return true;
  }

  const auto pane_owns_object = [this](const QObject *object) {
    for (const QObject *current = object; current != nullptr;
         current = current->parent()) {
      for (const Pane &pane_state : panes_) {
        if (current == pane_state.frame || current == pane_state.surface ||
            current == pane_state.surface_container) {
          return true;
        }
      }
    }
    return false;
  };
  if (pane_owns_object(target)) {
    return true;
  }

  // A QMenu popup has its own QWidgetWindow. Associate both the popup widget
  // and that native window with the pane-owned display menu that created it.
  const QWidget *popup = QApplication::activePopupWidget();
  return popup != nullptr && pane_owns_object(popup) &&
         (target == popup || target == popup->windowHandle());
}

void QtViewportPanel::queue_external_pointer_event(const QMouseEvent &event) {
  if (event.button() != Qt::LeftButton &&
      event.button() != Qt::RightButton &&
      event.button() != Qt::MiddleButton) {
    return;
  }
  const bool left_button_release =
      event.type() == QEvent::MouseButtonRelease &&
      event.button() == Qt::LeftButton;
  if (!pending_external_pointer_event_) {
    pending_external_pointer_left_release_only_ = left_button_release;
  } else {
    pending_external_pointer_left_release_only_ =
        pending_external_pointer_left_release_only_ && left_button_release;
  }
  pending_external_pointer_event_ = true;
}

void QtViewportPanel::update_fly_navigation_hud(
    Pane &pane_state, bool active, bool speed_is_default, float speed) {
  pane_state.fly_navigation_hud->present(
      qt_viewport_fly_navigation_hud_presentation(
          active, speed_is_default, speed, localization_));
}

void QtViewportPanel::hide_fly_navigation_huds() {
  for (Pane &pane_state : panes_) {
    if (pane_state.fly_navigation_hud != nullptr) {
      pane_state.fly_navigation_hud->hide();
    }
  }
}

void QtViewportPanel::draw_viewport_info_overlay(
    Pane &pane_state, const SceneDocument &document,
    const AssetRegistry &assets, ViewportOverlayCamera camera) {
  const std::uint64_t scene_revision = document.revision();
  const std::uint64_t asset_revision = assets.revision();
  const ViewportLocalVisibilitySnapshot local_visibility =
      workspace_ != nullptr ? workspace_->local_visibility_snapshot()
                            : ViewportLocalVisibilitySnapshot{};
  const std::uint64_t local_visibility_revision =
      local_visibility.revision();
  const bool cache_hit =
      viewport_info_scene_stats_valid_ &&
      viewport_info_cached_document_ == &document &&
      viewport_info_cached_assets_ == &assets &&
      viewport_info_cached_scene_revision_ == scene_revision &&
      viewport_info_cached_asset_revision_ == asset_revision &&
      viewport_info_cached_local_visibility_revision_ ==
          local_visibility_revision &&
      same_viewport_info_camera(viewport_info_cached_camera_, camera);
  ViewportInfoSceneStats scene_stats;
  if (cache_hit) {
    scene_stats.objects = viewport_info_cached_objects_;
    scene_stats.primitives = viewport_info_cached_primitives_;
    scene_stats.draw_calls = viewport_info_cached_draw_calls_;
  } else {
    QDR_PROFILE_SCOPE("Viewport info collect scene stats");
    scene_stats = collect_viewport_info_scene_stats(
        document, assets, camera, &local_visibility);
    viewport_info_cached_document_ = &document;
    viewport_info_cached_assets_ = &assets;
    viewport_info_cached_scene_revision_ = scene_revision;
    viewport_info_cached_asset_revision_ = asset_revision;
    viewport_info_cached_local_visibility_revision_ =
        local_visibility_revision;
    viewport_info_cached_camera_ = camera;
    viewport_info_cached_objects_ = scene_stats.objects;
    viewport_info_cached_primitives_ = scene_stats.primitives;
    viewport_info_cached_draw_calls_ = scene_stats.draw_calls;
    viewport_info_scene_stats_valid_ = true;
  }

  const QString unavailable = localized_viewport_info_label(
      localization_, "ui.viewport.info.unavailable", "--");
  const QString fps_value = render_performance_.frames_per_second_available
                                ? format_viewport_info_frames_per_second(
                                      render_performance_.frames_per_second)
                                : unavailable;
  const QString cpu_value = render_performance_.cpu_frame_ms_available
                                ? format_viewport_info_milliseconds(
                                      render_performance_.cpu_frame_ms)
                                : unavailable;
  const QString gpu_value = render_performance_.gpu_frame_ms_available
                                ? format_viewport_info_milliseconds(
                                      render_performance_.gpu_frame_ms)
                                : unavailable;
  const auto line = [this](std::string_view key, std::string_view fallback,
                           const QString &value) {
    return QStringLiteral("%1: %2").arg(
        localized_viewport_info_label(localization_, key, fallback), value);
  };
  const QString text =
      line("ui.viewport.info.fps", "FPS", fps_value) + QLatin1Char('\n') +
      line("ui.viewport.info.cpu", "CPU", cpu_value) + QLatin1Char('\n') +
      line("ui.viewport.info.gpu", "GPU", gpu_value) + QLatin1Char('\n') +
      line("ui.viewport.info.objects", "Objects",
           QString::number(scene_stats.objects)) +
      QLatin1Char('\n') +
      line("ui.viewport.info.primitives", "Primitives",
           QString::number(scene_stats.primitives)) +
      QLatin1Char('\n') +
      line("ui.viewport.info.draw_calls", "Draw Calls",
           QString::number(scene_stats.draw_calls));
  if (pane_state.info_overlay->text() != text) {
    pane_state.info_overlay->setText(text);
  }
  pane_state.info_overlay->setVisible(true);
  update_pane_overlay_geometry(pane_state);
}

void QtViewportPanel::update_split_handles() {
  if (workspace_ == nullptr || startup_presentation_blocked_) {
    for (SplitHandleHost &host : split_handles_) {
      host.visible = false;
      host.widget->hide();
    }
    return;
  }
  const std::vector<ViewportSplitHandle> handles =
      workspace_->active_split_handles();
  const int full_width = std::max(1, width());
  const int full_height = std::max(1, height());
  constexpr int kHitThickness = 10;
  for (std::size_t index = 0; index < split_handles_.size(); ++index) {
    SplitHandleHost &host = split_handles_[index];
    if (index >= handles.size()) {
      host.visible = false;
      host.widget->hide();
      continue;
    }
    host.descriptor = handles[index];
    host.visible = true;
    if (host.descriptor.axis == ViewportSplitHandleAxis::Vertical) {
      const int x =
          static_cast<int>(std::lround(host.descriptor.position * full_width));
      const int start =
          static_cast<int>(std::lround(host.descriptor.start * full_height));
      const int end =
          static_cast<int>(std::lround(host.descriptor.end * full_height));
      host.widget->setGeometry(x - kHitThickness / 2, start, kHitThickness,
                               std::max(1, end - start));
      host.widget->setCursor(Qt::SplitHCursor);
    } else {
      const int y =
          static_cast<int>(std::lround(host.descriptor.position * full_height));
      const int start =
          static_cast<int>(std::lround(host.descriptor.start * full_width));
      const int end =
          static_cast<int>(std::lround(host.descriptor.end * full_width));
      host.widget->setGeometry(start, y - kHitThickness / 2,
                               std::max(1, end - start), kHitThickness);
      host.widget->setCursor(Qt::SplitVCursor);
    }
    host.widget->show();
    host.widget->raise();
  }
}

void QtViewportPanel::process_viewport_input(
    QuaderEditorApi &editor, SceneDocument &document, AssetRegistry &assets,
    ModelingSessionService &modeling, AssetId default_material,
    PivotToolService *pivot, LinearArrayToolService *linear_array,
    RadialArrayToolService *radial_array,
    ViewportPointPlacementService *point_placement,
    double frame_delta_seconds) {
  if (workspace_ == nullptr) {
    return;
  }
  if (interaction_editor_ != &editor) {
    refresh_keybinding_presentation(editor);
  }
  if (linear_array != nullptr) {
    const CommandResult refresh = linear_array->refresh();
    if (refresh.changed) {
      interaction_controller_.clear_native_tool_drag_state();
      hover_controller_.invalidate();
    }
  }
  if (radial_array != nullptr) {
    const CommandResult refresh = radial_array->refresh();
    if (refresh.changed) {
      hover_controller_.invalidate();
      selection_overlay_cache_.mark_dirty();
    }
  }
  sync_active_tool_from_editor(editor);

  const float delta_seconds = static_cast<float>(
      std::clamp(frame_delta_seconds, 0.0, 0.1));

  const ModelingToolStatus current_tool_status = modeling.tool_status();
  const bool modal_dragging = current_tool_status.insert_edge_loop_dragging ||
                              current_tool_status.cut_dragging ||
                              current_tool_status.mirror_dragging;
  modal_tool_dragging_ = modal_dragging;
  std::optional<ViewportPaneId> navigation_gizmo_locked_pane;
  bool navigation_gizmo_activity = false;
  for (Pane &pane_state : panes_) {
    navigation_gizmo_activity =
        pane_state.navigation_gizmo->take_interaction_activity() ||
        navigation_gizmo_activity;
    if (pane_state.navigation_gizmo->input_locked()) {
      navigation_gizmo_locked_pane = pane_state.id;
    }
  }
  if (navigation_gizmo_locked_pane.has_value()) {
    interaction_pane_id_ = *navigation_gizmo_locked_pane;
  }
  const bool navigation_gizmo_consumed =
      navigation_gizmo_locked_pane.has_value() || navigation_gizmo_activity;
  const bool interaction_locked =
      input_router_.navigating() || interaction_controller_.dragging() ||
      interaction_controller_.native_tool_dragging() ||
      modal_dragging || navigation_gizmo_consumed;
  std::array<QtViewportInputFrame, kViewportPaneCount> samples;
  for (Pane &pane_state : panes_) {
    const bool routes_locked_interaction =
        interaction_locked && pane_state.id == interaction_pane_id_;
    samples[viewport_pane_index(pane_state.id)] =
        pane_state.surface->take_input_frame(delta_seconds,
                                             routes_locked_interaction);
  }

  std::vector<ViewportPaneInputCandidate> pane_candidates;
  pane_candidates.reserve(workspace_->active_pane_placements().size());
  for (const ViewportPanePlacement &placement :
       workspace_->active_pane_placements()) {
    const QtViewportInputFrame &candidate =
        samples[viewport_pane_index(placement.pane_id)];
    pane_candidates.push_back({
        .pane_id = placement.pane_id,
        .has_input_activity = has_input_activity(candidate),
        .hovered = candidate.input.viewport_hovered,
        .focused = candidate.input.viewport_focused,
    });
  }
  const std::optional<ViewportPaneId> input_pane = resolve_viewport_input_pane({
      .active_panes = pane_candidates,
      .interaction_locked = interaction_locked,
      .interaction_owner = interaction_pane_id_,
      .focused_pane = workspace_->focused_pane(),
  });
  if (!input_pane.has_value()) {
    for (Pane &pane_state : panes_) {
      pane_state.surface->set_pointer_capture(false);
    }
    hide_fly_navigation_huds();
    last_fly_navigation_active_ = false;
    modal_tool_owns_viewport_snapshot_ =
        interaction_controller_.active_tool_owns_viewport(
            input_router_.state().active_tool) ||
        modeling.modal_tool_owns_viewport();
    return;
  }

  Pane &pane_state = pane(*input_pane);
  QtViewportInputFrame &sample = samples[viewport_pane_index(*input_pane)];
  if (viewport_frame_diagnostics_enabled()) {
    g_viewport_probe.mouse_events += sample.mouse_move_events;
  }
  const bool pointer_pressed = sample.left_pressed || sample.right_pressed ||
                               sample.input.middle_pressed;
  const bool wheel_activity = sample.input.mouse_wheel != 0.0f ||
                              !sample.input.wheel_input_edges.empty();
  if (!interaction_locked && sample.input.viewport_hovered) {
    request_pane_focus(*input_pane, ViewportPaneFocusTrigger::Hover);
  }
  if (pointer_pressed || wheel_activity) {
    request_pane_focus(
        *input_pane, pointer_pressed ? ViewportPaneFocusTrigger::PointerPress
                                     : ViewportPaneFocusTrigger::Wheel);
    sample.input.viewport_focused = true;
  }
  if (pointer_pressed) {
    interaction_pane_id_ = *input_pane;
  }

  ViewportInputFrame &input = sample.input;
  if (viewport_frame_diagnostics_enabled() &&
      (input.mouse_delta.x != 0.0f || input.mouse_delta.y != 0.0f)) {
    ++g_viewport_probe.motion_frames;
  }
  input.navigation_active = input_router_.navigating() ||
                            navigation_gizmo_locked_pane.has_value();
  input.fly_navigation_active = input_router_.fly_navigating();
  input.orbit_navigation_active = input_router_.orbit_navigating() ||
                                  navigation_gizmo_locked_pane.has_value();
  input.pan_navigation_active = input_router_.pan_navigating();
  ViewportModelingToolPortAdapter preview_port_adapter(modeling);
  const ViewportToolPorts preview_ports = preview_port_adapter.ports();
  const bool resolved_grid_snap_enabled = viewport_grid_snap_active(
      active_viewport_settings(), input.modifiers.primary);
  const ModelingToolSettingsTarget tool_settings =
      modeling.tool_settings_target();
  input.modal_tool_active =
      interaction_controller_.has_active_modal_preview(
          *preview_ports.preview, resolved_grid_snap_enabled) ||
      modeling.modal_tool_owns_viewport();
  input.active_tool_owns_viewport =
      interaction_controller_.active_tool_owns_viewport(
          input_router_.state().active_tool);
  input.active_tool_blocks_focus_shortcuts =
      interaction_controller_.active_tool_blocks_viewport_focus_shortcuts(
          input_router_.state().active_tool);
  input.active_transform_drag = interaction_controller_.dragging();
  modal_tool_owns_viewport_snapshot_ =
      input.active_tool_owns_viewport || input.modal_tool_active;
  input.operation_preview_active = tool_settings.operation_preview_active;
  input.grid_preset = workspace_->grid_preset();
  ViewportKeybindingResolver::resolve(
      editor, input_router_.state(), input, suppressed_keybinding_chords_,
      linear_array_wheel_remainder_, radial_array_wheel_remainder_);
  if (sync_active_tool_from_editor(editor)) {
    input.modal_tool_active =
        interaction_controller_.has_active_modal_preview(
            *preview_ports.preview, resolved_grid_snap_enabled) ||
        modeling.modal_tool_owns_viewport();
    input.active_tool_owns_viewport =
        interaction_controller_.active_tool_owns_viewport(
            input_router_.state().active_tool);
    input.active_tool_blocks_focus_shortcuts =
        interaction_controller_.active_tool_blocks_viewport_focus_shortcuts(
            input_router_.state().active_tool);
    modal_tool_owns_viewport_snapshot_ =
        input.active_tool_owns_viewport || input.modal_tool_active;
  }

  ViewportCameraController &camera = workspace_->camera(*input_pane);
  const ViewportInputResult result = input_router_.handle_frame(input, camera);
  for (Pane &candidate : panes_) {
    if (candidate.id != *input_pane) {
      candidate.fly_navigation_hud->hide();
    }
  }
  update_fly_navigation_hud(pane_state, result.fly_hud_active,
                            camera.fly_speed_is_default(), camera.fly_speed());
  if (result.maximized_toggle_requested) {
    (void)workspace_->focus_pane(*input_pane);
    (void)workspace_->toggle_focused_pane_maximized();
    refresh_workspace_layout();
  }
  if (result.grid_preset_changed) {
    workspace_->set_grid_preset(result.requested_grid_preset);
    input_router_.sync_grid_preset_snapshot(workspace_->grid_preset());
  }
  if (result.view_changed) {
    (void)workspace_->set_view(*input_pane, result.requested_view);
  }
  if (result.focus_document_requested) {
    const ViewportLocalVisibilitySnapshot local_visibility =
        workspace_->local_visibility_snapshot();
    (void)camera.focus_optional_bounds(
        ViewportSceneProjection::world_bounds(document, assets,
                                              local_visibility),
        ViewportEmptyFocusBehavior::FocusOrigin);
  }

  const bool modal_tool_owns_viewport_for_menu =
      input.active_tool_owns_viewport || input.modal_tool_active;
  if (!modal_tool_owns_viewport_for_menu && input.right_pressed &&
      input.viewport_hovered && !input.modifiers.primary &&
      !input.modifiers.shift && !input.modifiers.alt) {
    context_menu_right_press_active_ = true;
    context_menu_pane_id_ = *input_pane;
    context_menu_right_press_screen_ = input.mouse_screen;
    context_menu_right_press_local_ = sample.right_click_local;
    context_menu_right_drag_delta_ = {};
  }
  if (context_menu_right_press_active_ &&
      context_menu_pane_id_ == *input_pane && input.right_down &&
      !input.right_pressed) {
    context_menu_right_drag_delta_ =
        context_menu_right_drag_delta_ + input.mouse_delta;
  }

  bool pending_context_menu_click = false;
  QVec2 pending_context_menu_local_mouse = {};
  ViewportContextMenuOpenRequest pending_context_menu_request;
  if (input.right_released && context_menu_right_press_active_ &&
      context_menu_pane_id_ == *input_pane) {
    QVec2 release_screen = input.mouse_screen;
    QVec2 release_local = input.mouse_local;
    QVec2 accumulated_drag = context_menu_right_drag_delta_;
    if (pane_state.surface->pointer_captured()) {
      // Fly navigation recenters the native cursor. Reconstruct the release
      // point from relative motion so a short RMB click still targets and
      // opens at the original viewport position rather than the warp center.
      accumulated_drag = accumulated_drag + input.mouse_delta;
      release_screen = context_menu_right_press_screen_ + accumulated_drag;
      release_local = context_menu_right_press_local_ + accumulated_drag;
    }
    const bool release_inside_viewport =
        release_local.x >= 0.0f && release_local.y >= 0.0f &&
        release_local.x <= input.viewport_size.x &&
        release_local.y <= input.viewport_size.y;
    pending_context_menu_request = {
        .modal_tool_owns_viewport = modal_tool_owns_viewport_for_menu,
        .right_press_tracked = true,
        .right_released = true,
        .release_inside_viewport = release_inside_viewport,
        .press_screen = context_menu_right_press_screen_,
        .release_screen = release_screen,
        .accumulated_drag_delta = accumulated_drag,
    };
    if (is_viewport_context_menu_click_gesture(
            pending_context_menu_request)) {
      pending_context_menu_click = true;
      pending_context_menu_local_mouse = release_local;
    }
    context_menu_right_press_active_ = false;
    context_menu_right_drag_delta_ = {};
  }
  if (!input.right_down && !input.right_released) {
    context_menu_right_press_active_ = false;
    context_menu_right_drag_delta_ = {};
  }

  const ViewportShortcutControllerResult preview_shortcut =
      shortcut_controller_.handle_operation_preview_shortcut(
          result, tool_settings, editor);
  const bool preview_shortcut_consumed = preview_shortcut.consumed;
  if (preview_shortcut.invalidate_hover) {
    hover_controller_.invalidate();
  }
  if (preview_shortcut.mark_overlay_dirty) {
    selection_overlay_cache_.mark_dirty();
  }

  if (!preview_shortcut_consumed && result.shortcut_handled &&
      result.shortcut.kind == ViewportShortcutKind::SetSelectionMode) {
    log_command_failure(
        "Selection mode change",
        editor.selection().set_mode(to_api_selection_mode(
            viewport_selection_mode_to_app(result.shortcut.selection_mode))));
    input_router_.set_selection_mode(app_selection_mode_to_viewport(
        to_modeling_selection_mode(editor.selection().summary().mode)));
    interaction_controller_.clear_modal_drag_state();
    interaction_controller_.reset(&editor);
    hover_controller_.invalidate();
    selection_overlay_cache_.mark_dirty();
  }

  if (result.tool_activation_requested) {
    const ViewportTool *active_tool = interaction_controller_.active_tool(
        input_router_.state().active_tool);
    const bool editor_tool_matches_requested =
        active_tool != nullptr &&
        editor.tools().active_tool() == active_tool->id();
    if (should_dispatch_viewport_tool_activation({
            .activation_requested = true,
            .tool_changed = result.tool_changed,
            .editor_tool_matches_requested = editor_tool_matches_requested,
        })) {
      hover_controller_.invalidate();
      selection_overlay_cache_.mark_dirty();
      const QuaderApiVoidResult activation =
          active_tool != nullptr
              ? editor.tools().activate(input_router_.state().active_tool)
              : QuaderApiVoidResult{.ok = false,
                                    .changed = false,
                                    .message = "No active viewport tool."};
      if (!activation.ok) {
        log_command_failure("Viewport tool activation", activation);
        input_router_.set_active_tool(ViewportToolId::Select);
        (void)editor.tools().activate(ViewportToolId::Select);
      }
      interaction_controller_.clear_modal_tool_state();
      interaction_controller_.reset(&editor);
    }
  }

  if (!preview_shortcut_consumed) {
    const ViewportShortcutControllerResult modeling_shortcut =
        shortcut_controller_.handle_modeling_shortcut(result, editor);
    if (modeling_shortcut.invalidate_hover) {
      hover_controller_.invalidate();
    }
    if (modeling_shortcut.mark_overlay_dirty) {
      selection_overlay_cache_.mark_dirty();
    }
  }

  const bool show_delete_popup =
      !preview_shortcut_consumed && result.shortcut_handled &&
      result.shortcut.kind == ViewportShortcutKind::DeletePopup &&
      localization_ != nullptr;
  QPoint delete_popup_global_point;
  if (show_delete_popup) {
    const qreal scale =
        std::max<qreal>(pane_state.surface->devicePixelRatio(), 1.0);
    const QPoint local_point(
        static_cast<int>(std::lround(input.mouse_local.x /
                                     static_cast<float>(scale))),
        static_cast<int>(std::lround(input.mouse_local.y /
                                     static_cast<float>(scale))));
    delete_popup_global_point = pane_state.surface->mapToGlobal(local_point);
  }

  const bool point_lights_pickable =
      active_viewport_settings().bool_value(
          ViewportBoolSettingId::ShowGizmos) &&
      viewport_shading_shows_light_gizmos(
          workspace_->shading_mode(*input_pane));
  const ViewportLocalVisibilitySnapshot local_visibility =
      workspace_->local_visibility_snapshot();
  const std::vector<ViewportSceneBillboardPickCandidate> billboards =
      point_lights_pickable
          ? ViewportSceneProjection::point_light_pick_candidates(
                document, local_visibility)
          : std::vector<ViewportSceneBillboardPickCandidate>{};
  ViewportInteractionFrame interaction_frame{
      .input_router = input_router_,
      .camera = camera,
      .settings = active_viewport_settings(),
      .input = input,
      .input_result = result,
      .left_click_local =
          sample.left_pressed ? sample.left_click_local : input.mouse_local,
      .left_click_count = std::max(1, sample.left_click_count),
      .left_pressed = sample.left_pressed,
      .left_down = sample.left_down,
      .left_released = sample.left_released,
      .right_click_local =
          sample.right_pressed ? sample.right_click_local : input.mouse_local,
      .right_click_count = std::max(1, sample.right_click_count),
      .right_pressed = sample.right_pressed,
      .right_down = sample.right_down,
      .right_released = sample.right_released,
      .active_tool_allows_selection_fallback =
          interaction_controller_.active_tool_allows_selection_fallback(
              input_router_.state().active_tool),
      .scene_billboard_pick_candidates = billboards,
  };

  auto modal_tool_owns_viewport = [&]() {
    return interaction_controller_.active_tool_owns_viewport(
               input_router_.state().active_tool) ||
           interaction_controller_.has_active_modal_preview(
               *preview_ports.preview, resolved_grid_snap_enabled) ||
           modeling.modal_tool_owns_viewport();
  };
  auto cancel_active_modal_tool = [&](bool exit_tool) {
    const ViewportToolId active_tool = input_router_.state().active_tool;
    if (active_tool == ViewportToolId::CreatePrimitive) {
      log_command_failure("Primitive tool cancel",
                          editor.tools().cancel_active_tool_preview());
      sync_active_tool_from_editor(editor);
      interaction_controller_.reset(&editor);
      hover_controller_.invalidate();
      selection_overlay_cache_.mark_dirty();
      return;
    }
    if (input_router_.state().active_tool == ViewportToolId::LinearArray) {
      log_command_failure("Linear Array cancel",
                          editor.tools().cancel_active_tool_preview());
      interaction_controller_.clear_native_tool_drag_state();
      sync_active_tool_from_editor(editor);
      interaction_controller_.reset(&editor);
      hover_controller_.invalidate();
      selection_overlay_cache_.mark_dirty();
      return;
    }
    if (input_router_.state().active_tool == ViewportToolId::RadialArray) {
      log_command_failure("Radial Array cancel",
                          editor.tools().cancel_active_tool_preview());
      sync_active_tool_from_editor(editor);
      interaction_controller_.reset(&editor);
      hover_controller_.invalidate();
      selection_overlay_cache_.mark_dirty();
      return;
    }
    if (input_router_.state().active_tool == ViewportToolId::Pivot) {
      log_command_failure("Pivot cancel",
                          editor.tools().cancel_active_tool_preview());
      interaction_controller_.clear_native_tool_drag_state();
      sync_active_tool_from_editor(editor);
      interaction_controller_.reset(&editor);
      hover_controller_.invalidate();
      selection_overlay_cache_.mark_dirty();
      return;
    }
    ViewportModelingToolPortCallbacks callbacks;
    callbacks.invalidate_hover = [&]() { hover_controller_.invalidate(); };
    callbacks.mark_overlay_dirty = [&]() {
      selection_overlay_cache_.mark_dirty();
    };
    ViewportModelingToolPortAdapter cancel_ports(modeling,
                                                 std::move(callbacks));
    ViewportToolContext cancel_context{
        .state = input_router_.state(),
        .camera = camera,
        .ports = cancel_ports.ports(),
    };
    cancel_context.set_active_tool = [&](ViewportToolId tool) {
      input_router_.set_active_tool(tool);
    };
    (void)interaction_controller_.handle_active_tool_event(
        input_router_.state(), cancel_context,
        ViewportToolEvent{.kind = ViewportToolEventKind::Cancel,
                          .tool = input_router_.state().active_tool,
                          .exit_active_modal_tool = exit_tool});
    interaction_controller_.reset(&editor);
    hover_controller_.invalidate();
    selection_overlay_cache_.mark_dirty();
  };

  bool cancel_consumed = false;
  if (!preview_shortcut_consumed && result.cancel_requested) {
    if (interaction_controller_.dragging()) {
      (void)interaction_controller_.cancel_active_drag(editor);
      selection_overlay_cache_.mark_dirty();
      cancel_consumed = true;
    }
    if (modal_tool_owns_viewport()) {
      cancel_active_modal_tool(true);
      cancel_consumed = true;
    }
  }

  const bool external_pointer_event =
      std::exchange(pending_external_pointer_event_, false);
  const bool external_pointer_left_release_only =
      std::exchange(pending_external_pointer_left_release_only_, true);
  if (should_cancel_modal_tool_for_external_pointer({
          .external_pointer_event = external_pointer_event,
          .modal_tool_owns_viewport = modal_tool_owns_viewport(),
          .persistent_native_tool_active =
              input_router_.state().active_tool ==
                  ViewportToolId::CreatePrimitive ||
              input_router_.state().active_tool == ViewportToolId::LinearArray ||
              input_router_.state().active_tool == ViewportToolId::RadialArray ||
              input_router_.state().active_tool == ViewportToolId::Pivot,
          .navigation_active = result.navigation_active,
          .left_button_release_only = external_pointer_left_release_only,
          .insert_edge_loop_dragging =
              current_tool_status.insert_edge_loop_dragging,
          .cut_dragging = current_tool_status.cut_dragging,
          .mirror_dragging = current_tool_status.mirror_dragging,
      })) {
    cancel_active_modal_tool(true);
    cancel_consumed = true;
  }

  const bool suppress_hover_work =
      result.navigation_active || result.camera_changed ||
      navigation_gizmo_consumed;
  if (suppress_hover_work || result.grid_preset_changed) {
    hover_controller_.invalidate();
  }

  const ViewportNativeToolInteractionResult native_tool_result =
      interaction_controller_.handle_native_tools(
          editor, document, assets, pivot, linear_array, radial_array,
          point_placement, camera,
          ViewportNativeToolInteractionFrame{
              .input = input,
              .input_result = result,
              .navigation_gizmo_consumed = navigation_gizmo_consumed,
              .suppress_hover_work = suppress_hover_work,
              .left_pressed = sample.left_pressed,
              .left_down = sample.left_down,
              .left_released = sample.left_released,
              .left_click_local = sample.left_click_local,
              .grid_snap_enabled = resolved_grid_snap_enabled,
              .linear_array_wheel_remainder =
                  &linear_array_wheel_remainder_,
              .radial_array_wheel_remainder =
                  &radial_array_wheel_remainder_,
          });
  if (native_tool_result.sync_active_tool) {
    sync_active_tool_from_editor(editor);
  }
  if (native_tool_result.claim_interaction_pane) {
    interaction_pane_id_ = *input_pane;
  }
  if (native_tool_result.invalidate_hover) {
    hover_controller_.invalidate();
  }
  if (native_tool_result.mark_overlay_dirty) {
    selection_overlay_cache_.mark_dirty();
  }

  const bool radial_array_consumed =
      native_tool_result.radial_array_consumed;
  const bool linear_array_consumed =
      native_tool_result.linear_array_consumed;
  const bool pivot_consumed = native_tool_result.pivot_consumed;
  const bool pivot_blocks_gizmo = native_tool_result.pivot_blocks_gizmo;

  const QuaderSelectionSummary interaction_selection =
      editor.selection().summary();
  interaction_frame.selection_state = {
      .summary = interaction_selection,
      .scene_revision = document.revision(),
      .asset_revision = assets.revision(),
      .pivot_revision =
          pivot != nullptr ? pivot->selection_pivot_revision() : 0,
  };
  const ViewportBeforeToolInteractionResult before_tool =
      (cancel_consumed || navigation_gizmo_consumed ||
       radial_array_consumed || linear_array_consumed || pivot_blocks_gizmo ||
       pivot_consumed)
          ? ViewportBeforeToolInteractionResult{.gizmo_consumed = true}
          : interaction_controller_.handle_before_active_tool(
                editor, interaction_frame);

  const ModelingSelectionSummary hover_selection =
      to_modeling_selection_summary(interaction_selection);
  auto make_hover_key = [&](bool gizmo_consumed) {
    const ViewportSettings &settings = active_viewport_settings();
    return hover_controller_.make_key(
        camera,
        ViewportHoverWorkRequest{
            .pane_id = *input_pane,
            .active_tool = input_router_.state().active_tool,
            .selection_mode = input_router_.state().selection_mode,
            .box_drag_state =
                interaction_controller_
                    .box_preview(*preview_ports.preview,
                                 resolved_grid_snap_enabled)
                    .drag_state,
            .grid_preset = input_router_.state().grid_preset,
            .grid_snap_enabled = resolved_grid_snap_enabled,
            .mouse_local = input.mouse_local,
            .viewport_size = input.viewport_size,
            .modifiers = input.modifiers,
            .gizmo_consumed = gizmo_consumed,
            .vertex_radius =
                settings.float_value(ViewportFloatSettingId::PickVertexRadius),
            .edge_radius =
                settings.float_value(ViewportFloatSettingId::PickEdgeRadius),
            .scene_revision = document.revision(),
            .asset_revision = assets.revision(),
            .local_visibility_revision = local_visibility.revision(),
            .content_revision = hover_selection.content_revision,
            .selection_revision = hover_selection.selection_revision,
        });
  };
  auto make_tool_event_at = [&](QVec2 mouse_local, bool dragging = false) {
    const ViewportSettings &settings = active_viewport_settings();
    const double vertex_radius =
        settings.float_value(ViewportFloatSettingId::PickVertexRadius);
    const double edge_radius =
        settings.float_value(ViewportFloatSettingId::PickEdgeRadius);
    const ViewportRay ray = camera.ray_from_viewport_point(mouse_local);
    return ModelingToolRayEvent{
        .screen_position = mouse_local,
        .ray = {.origin = ray.origin, .direction = ray.direction},
        .camera =
            ViewportCameraAdapter::modeling_camera(camera, input.viewport_size),
        .grid_size =
            std::max(static_cast<double>(viewport_grid_world_size_for_preset(
                         input_router_.state().grid_preset)),
                     0.0001),
        .vertex_radius = vertex_radius,
        .edge_radius = edge_radius,
        .vertex_screen_radius =
            viewport_component_pick_radius_pixels(vertex_radius),
        .edge_screen_radius =
            viewport_component_pick_radius_pixels(edge_radius),
        .snap_enabled = resolved_grid_snap_enabled,
        .constrain_axis = input.modifiers.shift,
        .dragging = dragging,
    };
  };
  ViewportModelingToolPortCallbacks tool_callbacks;
  tool_callbacks.claim_hover = [&](ModelingBoxDragState) {
    return hover_controller_.claim(make_hover_key(before_tool.gizmo_consumed));
  };
  tool_callbacks.invalidate_hover = [&]() { hover_controller_.invalidate(); };
  tool_callbacks.mark_overlay_dirty = [&]() {
    selection_overlay_cache_.mark_dirty();
  };
  tool_callbacks.set_selection_mode = [&](ViewportSelectionMode mode) {
    input_router_.set_selection_mode(mode);
  };
  tool_callbacks.set_app_selection_mode = [&](ModelingSelectionMode mode) {
    log_command_failure(
        "Selection mode change",
        editor.selection().set_mode(to_api_selection_mode(mode)));
    input_router_.set_selection_mode(app_selection_mode_to_viewport(
        to_modeling_selection_mode(editor.selection().summary().mode)));
    selection_overlay_cache_.mark_dirty();
  };
  tool_callbacks.active_material = [&editor]() {
    const QuaderApiResult<QuaderProjectMaterialInfo> active =
        editor.project_assets().active_material();
    return active.ok ? MaterialReference::project(active.value().id)
                     : MaterialReference{};
  };
  tool_callbacks.handle_tool_command_result =
      [&](std::string_view label, const ModelingCommandResult &command) {
        if (!command.ok) {
          LOG_ERROR("{} failed: {}", label,
                    command.message.empty() ? "Command failed."
                                            : command.message);
        }
        // The claimed hover key also throttles deterministic failures. Retrying
        // an unchanged failed query every frame only floods the log and can
        // leave stale presentation oscillating. A changed result invalidates
        // naturally; a failed key becomes eligible again when pointer, camera,
        // tool, or document state changes.
        if (command.changed) {
          hover_controller_.invalidate();
          selection_overlay_cache_.mark_dirty();
        }
      };
  ViewportModelingToolPortAdapter tool_ports(modeling,
                                             std::move(tool_callbacks));
  ViewportBoxToolContext box_context{.default_material = default_material};
  box_context.make_event = [&]() {
    return ViewportNativeToolInteraction::modeling_box_event(
        camera, input,
        viewport_grid_world_size_for_preset(input_router_.state().grid_preset),
        resolved_grid_snap_enabled);
  };
  box_context.accept_preview = [&](std::string name, AssetId material,
                                   bool snap_enabled) {
    const QuaderApiVoidResult result =
        editor.tools().accept_active_primitive(std::move(name), material,
                                               snap_enabled);
    return ModelingCommandResult{
        .ok = result.ok,
        .changed = result.changed,
        .message = result.message,
    };
  };
  box_context.apply_session = [&]() {
    const QuaderApiVoidResult result =
        editor.tools().commit_active_tool_preview();
    return ModelingCommandResult{
        .ok = result.ok,
        .changed = result.changed,
        .message = result.message,
    };
  };
  box_context.cancel_session = [&]() {
    const QuaderApiVoidResult result =
        editor.tools().cancel_active_tool_preview();
    return ModelingCommandResult{
        .ok = result.ok,
        .changed = result.changed,
        .message = result.message,
    };
  };
  box_context.session_active = [&]() {
    return editor.tools().has_active_primitive_creation();
  };
  box_context.next_mesh_name = [&]() {
    const std::string active_name =
        editor.tools().active_primitive_default_name();
    const std::string_view base_name =
        active_name.empty() ? std::string_view{"Mesh"} : active_name;
    return SceneEntityNaming::next_name(document, base_name);
  };
  ViewportPointToolContext point_context;
  point_context.make_event_at = make_tool_event_at;
  point_context.hit_drawn_plane_captured_point =
      [&](const std::array<QVec3, 3> &points, int count) {
        return ViewportNativeToolInteraction::hit_drawn_plane_captured_point(
            points, count, sample.left_click_local, camera);
      };
  point_context.hit_drawn_plane_line = [&](const std::array<QVec3, 3> &points,
                                           int count, bool has_line) {
    return ViewportNativeToolInteraction::hit_drawn_plane_line(
        points, count, has_line, sample.left_click_local, camera);
  };
  ViewportToolContext tool_context{
      .state = input_router_.state(),
      .camera = camera,
      .ports = tool_ports.ports(),
      .document = &document,
      .settings = &active_viewport_settings(),
      .box = &box_context,
      .point = &point_context,
  };
  tool_context.set_active_tool = [&](ViewportToolId tool) {
    input_router_.set_active_tool(tool);
  };
  const bool gizmo_consumed =
      cancel_consumed || navigation_gizmo_consumed ||
      radial_array_consumed || linear_array_consumed || pivot_consumed ||
      before_tool.gizmo_consumed;
  const ViewportToolResult active_tool_result =
      interaction_controller_.handle_active_tool_event(
          input_router_.state(), tool_context,
          ViewportToolEvent{
              .kind = ViewportToolEventKind::Frame,
              .tool = input_router_.state().active_tool,
              .pointer =
                  {
                      .hovered = input.viewport_hovered,
                      .left_clicked = sample.left_pressed,
                      .left_down = sample.left_down,
                      .left_released = sample.left_released,
                      .can_interact = input.viewport_hovered &&
                                      !gizmo_consumed &&
                                      !result.navigation_active,
                      .finalize_pressed = input.modal_tool_finalize_pressed,
                      .mode_cycle_pressed = input.modal_tool_mode_cycle_pressed,
                      .suppress_hover_work = suppress_hover_work,
                      .gizmo_consumed = gizmo_consumed,
                      .navigation_active = result.navigation_active,
                      .text_input_active = input.text_input_active,
                      .mouse_local = input.mouse_local,
                      .left_click_local = sample.left_pressed
                                              ? sample.left_click_local
                                              : input.mouse_local,
                      .viewport_size = input.viewport_size,
                      .modifiers = input.modifiers,
                  },
          });
  active_box_preview_ = interaction_controller_.box_preview(
      *preview_ports.preview, resolved_grid_snap_enabled);
  interaction_controller_.handle_after_active_tool(
      editor, interaction_frame, active_tool_result, before_tool,
      tool_context.ports.hover);

  bool show_context_menu = false;
  QPoint context_menu_global_point;
  const bool selection_fallback_active =
      interaction_controller_.active_tool_allows_selection_fallback(
          input_router_.state().active_tool);
  if (!active_tool_result.handled && selection_fallback_active &&
      pending_context_menu_click && !gizmo_consumed) {
    ViewportInputFrame context_input = input;
    context_input.mouse_local = pending_context_menu_local_mouse;
    const ViewportInteractionFrame context_frame{
        .input_router = input_router_,
        .camera = camera,
        .settings = active_viewport_settings(),
        .input = context_input,
        .input_result = result,
        .active_tool_allows_selection_fallback = selection_fallback_active,
        .scene_billboard_pick_candidates = billboards,
    };
    const QuaderSelectionSummary summary =
        interaction_controller_.pick_context_menu_target(editor,
                                                         context_frame);
    const QuaderActionQuery query{
        .surface = QuaderActionSurface::ViewportContextMenu,
        .modal_tool_owns_viewport = modal_tool_owns_viewport(),
        .include_disabled = true,
        .include_hidden = false,
    };
    const std::vector<QuaderActionItem> actions =
        summary.has_selection ? editor.modeling().available_actions(query)
                              : std::vector<QuaderActionItem>{};
    pending_context_menu_request.target_hit = summary.has_selection;
    pending_context_menu_request.has_actions =
        summary.has_selection && viewport_context_menu_has_available_actions(
                                     !actions.empty(),
                                     editor.modeling().can_create_instance(),
                                     editor.modeling().can_flatten_instance(),
                                     editor.layers()
                                         .can_remove_selection_from_all_layers());
    show_context_menu =
        should_open_viewport_context_menu(pending_context_menu_request) &&
        localization_ != nullptr;
    if (show_context_menu) {
      const qreal scale =
          std::max<qreal>(pane_state.surface->devicePixelRatio(), 1.0);
      const QPoint local_point(
          static_cast<int>(std::lround(pending_context_menu_local_mouse.x /
                                       static_cast<float>(scale))),
          static_cast<int>(std::lround(pending_context_menu_local_mouse.y /
                                       static_cast<float>(scale))));
      context_menu_global_point = pane_state.surface->mapToGlobal(local_point);
    }
    hover_controller_.invalidate();
    selection_overlay_cache_.mark_dirty();
  }
  if (interaction_controller_.dragging() || before_tool.gizmo_consumed ||
      has_input_activity(sample)) {
    interaction_pane_id_ = *input_pane;
  }

  last_fly_navigation_active_ = result.fly_cursor_capture_active;
  for (Pane &candidate : panes_) {
    candidate.surface->set_pointer_capture(result.fly_cursor_capture_active &&
                                           candidate.id == *input_pane);
  }
  if (show_context_menu) {
    context_menu_presenter_.show(
        *pane_state.surface_container, context_menu_global_point, editor,
        input_router_, *localization_, modal_tool_owns_viewport());
  }
  if (show_delete_popup &&
      delete_popup_presenter_.show(
          *pane_state.surface_container, delete_popup_global_point, editor,
          modeling, input_router_, *localization_)) {
    hover_controller_.invalidate();
    selection_overlay_cache_.mark_dirty();
  }
  const ModelingToolStatus final_tool_status = modeling.tool_status();
  modal_tool_dragging_ = final_tool_status.insert_edge_loop_dragging ||
                         final_tool_status.cut_dragging ||
                         final_tool_status.mirror_dragging;
  modal_tool_owns_viewport_snapshot_ = modal_tool_owns_viewport();
}

bool QtViewportPanel::sync_active_tool_from_editor(QuaderEditorApi &editor) {
  const ViewportTool *editor_tool =
      find_viewport_tool(editor.tools().active_tool());
  const ViewportToolId active_tool = editor_tool != nullptr
                                         ? editor_tool->viewport_tool()
                                         : ViewportToolId::Select;
  const bool changed = input_router_.state().active_tool != active_tool;
  if (changed) {
    linear_array_wheel_remainder_ = 0.0;
    radial_array_wheel_remainder_ = 0.0;
    input_router_.set_active_tool(active_tool);
    interaction_controller_.clear_modal_tool_state();
    interaction_controller_.clear_native_tool_drag_state();
    const QuaderApiVoidResult cleared_hover = editor.selection().clear_hover();
    if (!cleared_hover.ok()) {
      LOG_ERROR("Viewport tool activation hover clear failed: {}",
                cleared_hover.error_message().empty()
                    ? "Hover clear failed."
                    : cleared_hover.error_message());
    }
    interaction_controller_.reset(&editor);
    hover_controller_.invalidate();
    active_box_preview_ = {};
    selection_overlay_cache_.mark_dirty();
  }
  input_router_.set_selection_mode(app_selection_mode_to_viewport(
      to_modeling_selection_mode(editor.selection().summary().mode)));
  input_router_.sync_grid_preset_snapshot(workspace_->grid_preset());
  return changed;
}

const ViewportSettings &QtViewportPanel::active_viewport_settings() const {
  return viewport_settings_ != nullptr ? *viewport_settings_
                                       : fallback_settings_;
}

void QtViewportPanel::set_pane_view(Pane &pane_state, ViewportView view) {
  if (workspace_ != nullptr) {
    (void)workspace_->set_view(pane_state.id, view);
    sync_pane_control_selection(pane_state);
  }
}

void QtViewportPanel::set_pane_shading(Pane &pane_state,
                                       ViewportShadingMode shading) {
  if (workspace_ != nullptr) {
    (void)workspace_->set_shading_mode(pane_state.id, shading);
    sync_pane_control_selection(pane_state);
  }
}

QtViewportPanel::Pane &QtViewportPanel::pane(ViewportPaneId pane_id) {
  return panes_[viewport_pane_index(pane_id)];
}

const QtViewportPanel::Pane &
QtViewportPanel::pane(ViewportPaneId pane_id) const {
  return panes_[viewport_pane_index(pane_id)];
}

} // namespace quader
