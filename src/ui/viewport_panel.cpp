////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/viewport_panel.hpp"

#include "api/quader_api_type_conversions.hpp"
#include "api/quader_command_ids.hpp"
#include "api/quader_editor_api.hpp"
#include "assets/mesh_asset.hpp"
#include "core/localization_service.hpp"
#include "core/logger.hpp"
#include "diagnostics/profile.hpp"
#include "editor/commands/modeling/scene_aware_delete_selection_command.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"
#include "editor/pivot/pivot_tool_service.hpp"
#include "editor/render/quader_render_api_runtime_access.hpp"
#include "editor/viewport/quader_viewport_api_runtime_access.hpp"
#include "math/glm_bridge.hpp"
#include "render/overlay/viewport_overlay_style_resolver.hpp"
#include "render/overlay/viewport_selection_marquee_overlay_builder.hpp"
#include "render/overlay/viewport_transform_gizmo_overlay_builder.hpp"
#include "scene/scene_bounds.hpp"
#include "scene/scene_entity_naming.hpp"
#include "render/render_layers.hpp"
#include "render/render_system.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "ui/imgui_popup_utils.hpp"
#include "ui/ui_context.hpp"
#include "ui/viewport_context_menu_policy.hpp"
#include "ui/viewport_pane_controller.hpp"
#include "viewport/viewport_shading_mode_conversions.hpp"
#include "ui/viewport_screen_geometry.hpp"
#include "viewport/viewport_camera_adapter.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"
#include "viewport/viewport_settings.hpp"
#include "editor/tools/viewport_tool_catalog.hpp"
#include "editor/tools/viewport_tool_ports.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <imgui_internal.h>

namespace quader {
namespace {

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

bool should_defer_pane_render_resize(const UIContext &ui_context,
                                     const ViewportRenderer &renderer) {
  return ui_context.viewport != nullptr &&
         ui_context.viewport->render_resize_suspended &&
         renderer.texture_handle() != kInvalidRenderTextureHandle &&
         renderer.overlay_texture_handle() != kInvalidRenderTextureHandle;
}

bool viewport_shading_shows_point_light_gizmos(ViewportShadingMode shading) {
  return shading == ViewportShadingMode::Shaded ||
         shading == ViewportShadingMode::Rendered;
}

bool point_light_gizmos_visible_for_pane(bool gizmos_visible,
                                         ViewportShadingMode shading) {
  return gizmos_visible && viewport_shading_shows_point_light_gizmos(shading);
}

std::vector<ViewportSceneBillboardPickCandidate>
point_light_billboard_pick_candidates(const SceneDocument &document) {
  std::vector<ViewportSceneBillboardPickCandidate> candidates;
  const std::vector<EntityId> entities = document.entities();
  candidates.reserve(entities.size());
  for (const EntityId entity : entities) {
    const LightComponent *light = document.light(entity);
    if (light == nullptr || light->type != LightType::Point ||
        !document.is_effectively_visible(entity)) {
      continue;
    }
    candidates.push_back(ViewportSceneBillboardPickCandidate{
        .entity = entity,
        .position = transform_point(document.world_transform_matrix(entity), {}),
        .size_pixels = kOverlayPointLightGizmoSizePixels,
    });
  }
  return candidates;
}

float screen_distance_squared(QVec2 left, QVec2 right) {
  const QVec2 delta = left - right;
  return delta.x * delta.x + delta.y * delta.y;
}

std::optional<QVec3> ray_plane_y_intersection(const ViewportRay &ray,
                                              float y) {
  if (std::abs(ray.direction.y) <= 0.000001f) {
    return std::nullopt;
  }
  const float t = (y - ray.origin.y) / ray.direction.y;
  if (t < 0.0f) {
    return std::nullopt;
  }
  return ray.origin + ray.direction * t;
}

std::optional<QVec3> grid_snap_point(const ViewportCameraController &camera,
                                     QVec2 mouse_local, float grid_size) {
  const std::optional<QVec3> hit =
      ray_plane_y_intersection(camera.ray_from_viewport_point(mouse_local),
                               0.0f);
  if (!hit.has_value()) {
    return std::nullopt;
  }
  const float size = std::max(grid_size, 0.0001f);
  return QVec3{
      std::round(hit->x / size) * size,
      0.0f,
      std::round(hit->z / size) * size,
  };
}

QVec3 free_pivot_drag_point(const ViewportCameraController &camera,
                            QVec2 mouse_local, QVec3 current_position) {
  return ray_plane_y_intersection(camera.ray_from_viewport_point(mouse_local),
                                  current_position.y)
      .value_or(current_position);
}

int pivot_snap_priority(PivotSnapKind kind) {
  switch (kind) {
  case PivotSnapKind::Vertex:
    return 0;
  case PivotSnapKind::EdgeMidpoint:
    return 1;
  case PivotSnapKind::FaceCenter:
    return 2;
  case PivotSnapKind::Grid:
    return 3;
  case PivotSnapKind::None:
    break;
  }
  return 4;
}

/**
 * Stores one projected snap target candidate during pivot-handle dragging.
 */
struct PivotSnapCandidate {
    QVec3 world_position = {};
    PivotSnapKind kind = PivotSnapKind::None;
    float screen_distance_sq = 0.0f;
};

void consider_pivot_candidate(const ViewportCameraController &camera,
                              QVec2 mouse_local, float radius_sq,
                              QVec3 world_position, PivotSnapKind kind,
                              std::optional<PivotSnapCandidate> &best) {
  const ViewportScreenPoint screen =
      camera.project_world_point(world_position);
  if (!screen.visible) {
    return;
  }
  const float distance_sq =
      screen_distance_squared(screen.position, mouse_local);
  if (distance_sq > radius_sq) {
    return;
  }
  if (!best.has_value() ||
      distance_sq + 0.001f < best->screen_distance_sq ||
      (std::abs(distance_sq - best->screen_distance_sq) <= 0.001f &&
       pivot_snap_priority(kind) < pivot_snap_priority(best->kind))) {
    best = PivotSnapCandidate{
        .world_position = world_position,
        .kind = kind,
        .screen_distance_sq = distance_sq,
    };
  }
}

std::uint64_t edge_key(std::uint32_t a, std::uint32_t b) {
  const std::uint32_t low = std::min(a, b);
  const std::uint32_t high = std::max(a, b);
  return (static_cast<std::uint64_t>(low) << 32u) |
         static_cast<std::uint64_t>(high);
}

void consider_mesh_pivot_candidates(
    const ViewportCameraController &camera, QVec2 mouse_local, float radius_sq,
    const SceneDocument &document, const AssetRegistry &assets, EntityId entity,
    PivotSnapSettings settings, std::optional<PivotSnapCandidate> &best) {
  if (!document.is_effectively_visible(entity)) {
    return;
  }
  const VisibilityComponent *visibility = document.visibility(entity);
  if (visibility != nullptr && !visibility->selectable) {
    return;
  }
  const MeshComponent *component = document.mesh(entity);
  if (component == nullptr) {
    return;
  }
  const MeshAsset *mesh = assets.mesh(component->mesh);
  if (mesh == nullptr || mesh->vertices.empty()) {
    return;
  }
  const QMat4 world = document.world_transform_matrix(entity);
  if (settings.vertex) {
    for (const MeshVertex &vertex : mesh->vertices) {
      consider_pivot_candidate(camera, mouse_local, radius_sq,
                               transform_point(world, vertex.position),
                               PivotSnapKind::Vertex, best);
    }
  }
  if (!settings.edge_midpoint && !settings.face_center) {
    return;
  }
  std::unordered_set<std::uint64_t> edges;
  for (std::size_t index = 0; index + 2u < mesh->indices.size();
       index += 3u) {
    const std::uint32_t ia = mesh->indices[index];
    const std::uint32_t ib = mesh->indices[index + 1u];
    const std::uint32_t ic = mesh->indices[index + 2u];
    if (ia >= mesh->vertices.size() || ib >= mesh->vertices.size() ||
        ic >= mesh->vertices.size()) {
      continue;
    }
    const QVec3 a = transform_point(world, mesh->vertices[ia].position);
    const QVec3 b = transform_point(world, mesh->vertices[ib].position);
    const QVec3 c = transform_point(world, mesh->vertices[ic].position);
    if (settings.face_center) {
      consider_pivot_candidate(camera, mouse_local, radius_sq,
                               (a + b + c) / 3.0f,
                               PivotSnapKind::FaceCenter, best);
    }
    if (settings.edge_midpoint) {
      const std::array<std::pair<std::uint32_t, std::uint32_t>, 3> face_edges{{
          {ia, ib},
          {ib, ic},
          {ic, ia},
      }};
      for (const auto &[left, right] : face_edges) {
        if (!edges.insert(edge_key(left, right)).second) {
          continue;
        }
        const QVec3 midpoint =
            (transform_point(world, mesh->vertices[left].position) +
             transform_point(world, mesh->vertices[right].position)) *
            0.5f;
        consider_pivot_candidate(camera, mouse_local, radius_sq, midpoint,
                                 PivotSnapKind::EdgeMidpoint, best);
      }
    }
  }
}

QVec3 resolved_pivot_drag_position(
    const ViewportCameraController &camera, QVec2 mouse_local,
    QVec3 current_position, const SceneDocument &document,
    const AssetRegistry &assets, PivotSnapSettings settings, float grid_size) {
  constexpr float kPivotSnapRadiusPixels = 14.0f;
  const float radius_sq = kPivotSnapRadiusPixels * kPivotSnapRadiusPixels;
  std::optional<PivotSnapCandidate> best;
  for (const EntityId entity : document.entities()) {
    consider_mesh_pivot_candidates(camera, mouse_local, radius_sq, document,
                                   assets, entity, settings, best);
  }
  if (settings.grid) {
    if (const std::optional<QVec3> grid =
            grid_snap_point(camera, mouse_local, grid_size)) {
      consider_pivot_candidate(camera, mouse_local, radius_sq, *grid,
                               PivotSnapKind::Grid, best);
    }
  }
  return best.has_value() ? best->world_position
                          : free_pivot_drag_point(camera, mouse_local,
                                                  current_position);
}

bool pivot_handle_hit(const ViewportCameraController &camera,
                      const PivotHandleState &handle, QVec2 mouse_local) {
  if (!handle.visible) {
    return false;
  }
  const ViewportScreenPoint screen =
      camera.project_world_point(handle.world_position);
  if (!screen.visible) {
    return false;
  }
  constexpr float kPivotHandlePickRadiusPixels = 16.0f;
  return screen_distance_squared(screen.position, mouse_local) <=
         kPivotHandlePickRadiusPixels * kPivotHandlePickRadiusPixels;
}

std::optional<QVec3> intersect_ray_plane(const ViewportRay &ray,
                                         QVec3 plane_point,
                                         QVec3 plane_normal) {
  const float denom = dot(plane_normal, ray.direction);
  if (std::abs(denom) <= 0.000001f) {
    return std::nullopt;
  }
  const float t = dot(plane_point - ray.origin, plane_normal) / denom;
  if (!std::isfinite(t)) {
    return std::nullopt;
  }
  return ray.origin + ray.direction * t;
}

float pivot_handle_world_radius(const ViewportCameraController &camera,
                                QVec3 world_position) {
  constexpr float kPivotHandleRadiusPixels = 7.0f;
  const ViewportScreenPoint screen =
      camera.project_world_point(world_position);
  if (!screen.visible) {
    return 0.08f;
  }

  const ViewportCameraPose pose = camera.pose();
  const QVec3 plane_normal = normalize_or_zero(pose.target - pose.eye);
  if (length_squared(plane_normal) <= 0.000001f) {
    return 0.08f;
  }

  const std::optional<QVec3> center =
      intersect_ray_plane(camera.ray_from_viewport_point(screen.position),
                          world_position, plane_normal);
  const std::optional<QVec3> edge = intersect_ray_plane(
      camera.ray_from_viewport_point(
          {screen.position.x + kPivotHandleRadiusPixels, screen.position.y}),
      world_position, plane_normal);
  if (!center.has_value() || !edge.has_value()) {
    return 0.08f;
  }
  return std::clamp(length(*edge - *center), 0.025f, 100.0f);
}

void append_pivot_ring(ViewportOverlayFrame &frame, QVec3 center, QVec3 axis_a,
                       QVec3 axis_b, float radius) {
  constexpr int kSegments = 32;
  constexpr float kTau = 6.2831853071795864769f;
  for (int segment = 0; segment < kSegments; ++segment) {
    const float angle0 =
        kTau * static_cast<float>(segment) / static_cast<float>(kSegments);
    const float angle1 =
        kTau * static_cast<float>(segment + 1) /
        static_cast<float>(kSegments);
    const QVec3 start = center + axis_a * (std::cos(angle0) * radius) +
                        axis_b * (std::sin(angle0) * radius);
    const QVec3 end = center + axis_a * (std::cos(angle1) * radius) +
                      axis_b * (std::sin(angle1) * radius);
    frame.lines.push_back(ViewportOverlayLine{
        .layer = ViewportOverlayLayer::ToolHandle,
        .start = start,
        .end = end,
        .color = kOverlayPivotHandleColor,
        .width_pixels = 2.0f,
        .style_role = ViewportOverlayStyleRole::ExplicitColor,
        .source = ViewportOverlaySource::Tool,
    });
  }
}

void append_pivot_handle_overlay(ViewportOverlayFrame &frame,
                                 const ViewportCameraController &camera,
                                 const PivotHandleState &handle) {
  if (!handle.visible) {
    return;
  }
  const float radius = pivot_handle_world_radius(camera, handle.world_position);
  append_pivot_ring(frame, handle.world_position, {1.0f, 0.0f, 0.0f},
                    {0.0f, 1.0f, 0.0f}, radius);
  append_pivot_ring(frame, handle.world_position, {1.0f, 0.0f, 0.0f},
                    {0.0f, 0.0f, 1.0f}, radius);
  append_pivot_ring(frame, handle.world_position, {0.0f, 1.0f, 0.0f},
                    {0.0f, 0.0f, 1.0f}, radius);
}

float grid_size_for_preset(int preset) {
  return viewport_grid_world_size_for_preset(preset);
}

std::string viewport_view_localization_key(ViewportView view) {
  std::string key = "viewport.view.";
  key.append(viewport_view_stable_id(view));
  return key;
}

std::string
localized_viewport_view_label(const LocalizationService &localization,
                              ViewportView view) {
  return localization.text(viewport_view_localization_key(view),
                           std::string(viewport_view_label(view)));
}

constexpr std::array<ViewportView, 7> kViewportChromeViews{{
        ViewportView::Perspective,
        ViewportView::Top,
        ViewportView::Bottom,
        ViewportView::Front,
        ViewportView::Back,
        ViewportView::Side,
        ViewportView::Left,
}};

std::string_view viewport_shading_mode_stable_id(ViewportShadingMode mode) {
  switch (mode) {
  case ViewportShadingMode::Wireframe:
    return "wireframe";
  case ViewportShadingMode::Shaded:
    return "shaded";
  case ViewportShadingMode::Rendered:
    return "rendered";
  }
  return "shaded";
}

std::string_view viewport_shading_mode_label(ViewportShadingMode mode) {
  switch (mode) {
  case ViewportShadingMode::Wireframe:
    return "Wireframe";
  case ViewportShadingMode::Shaded:
    return "Shaded";
  case ViewportShadingMode::Rendered:
    return "Rendered";
  }
  return "Shaded";
}

std::string viewport_shading_mode_localization_key(ViewportShadingMode mode) {
  std::string key = "viewport.shading.";
  key.append(viewport_shading_mode_stable_id(mode));
  return key;
}

std::string
localized_viewport_shading_mode_label(const LocalizationService &localization,
                                      ViewportShadingMode mode) {
  return localization.text(viewport_shading_mode_localization_key(mode),
                           std::string(viewport_shading_mode_label(mode)));
}

constexpr std::array<ViewportShadingMode, 3> kViewportChromeShadingModes{{
        ViewportShadingMode::Wireframe,
        ViewportShadingMode::Shaded,
        ViewportShadingMode::Rendered,
}};

float viewport_chrome_combo_width_for_text(const std::string &label) {
  const ImGuiStyle &style = ImGui::GetStyle();
  const ImVec2 label_size =
      ImGui::CalcTextSize(label.data(), label.data() + label.size());
  return std::ceil(label_size.x + ImGui::GetFrameHeight() +
                   style.FramePadding.x * 2.0f + 10.0f);
}

float viewport_view_combo_width(const LocalizationService &localization) {
  float width = 0.0f;
  for (const ViewportView view : kViewportChromeViews) {
    width =
        std::max(width, viewport_chrome_combo_width_for_text(
                            localized_viewport_view_label(localization, view)));
  }
  return width;
}

float viewport_shading_combo_width(const LocalizationService &localization) {
  float width = 0.0f;
  for (const ViewportShadingMode mode : kViewportChromeShadingModes) {
    width = std::max(
        width, viewport_chrome_combo_width_for_text(
                   localized_viewport_shading_mode_label(localization, mode)));
  }
  return width;
}

/**
 * Stores the scene-visible counters presented by the View > Show Info viewport overlay.
 */
struct ViewportInfoSceneStats {
    std::uint64_t objects = 0;
    std::uint64_t primitives = 0;
    std::uint64_t draw_calls = 0;
};

ViewportInfoSceneStats
collect_viewport_info_scene_stats(const SceneDocument &document,
                                  const AssetRegistry &assets) {
  ViewportInfoSceneStats stats;
  for (const EntityId entity_id : document.entities()) {
    if (!document.is_effectively_visible(entity_id)) {
      continue;
    }

    const MeshComponent *mesh_component = document.mesh(entity_id);
    if (mesh_component == nullptr) {
      continue;
    }

    const MeshAsset *mesh = assets.mesh(mesh_component->mesh);
    if (mesh == nullptr || mesh->vertices.empty() || mesh->indices.empty()) {
      continue;
    }

    const std::vector<MeshPrimitiveSpan> spans =
        coalesced_mesh_primitive_spans(*mesh);
    if (spans.empty()) {
      continue;
    }

    ++stats.objects;
    stats.draw_calls += spans.size();
    for (const MeshPrimitiveSpan &span : spans) {
      stats.primitives += span.index_count / 3u;
    }
  }
  return stats;
}

std::string format_viewport_info_number(std::uint64_t value) {
  return std::to_string(value);
}

std::string format_viewport_info_milliseconds(double value) {
  if (!(value >= 0.0) || !std::isfinite(value)) {
    return "--";
  }
  char buffer[32] = {};
  std::snprintf(buffer, sizeof(buffer), "%.2f ms", value);
  return buffer;
}

std::string format_viewport_info_frames_per_second(double value) {
  if (!(value >= 0.0) || !std::isfinite(value)) {
    return "--";
  }
  char buffer[32] = {};
  std::snprintf(buffer, sizeof(buffer), "%.1f", value);
  return buffer;
}

std::string viewport_info_line(std::string_view label,
                               const std::string &value) {
  std::string line{label};
  line.append(": ");
  line.append(value);
  return line;
}

void draw_viewport_info_text(ImDrawList &draw_list, ImVec2 item_min,
                             ImVec2 item_size,
                             std::span<const std::string> lines) {
  if (lines.empty() || item_size.x <= 32.0f || item_size.y <= 32.0f) {
    return;
  }

  const float padding = 10.0f;
  const float line_advance = ImGui::GetTextLineHeightWithSpacing();
  float width = 0.0f;
  for (const std::string &line : lines) {
    width =
        std::max(width, ImGui::CalcTextSize(line.c_str(), nullptr, false).x);
  }
  const float height = ImGui::GetTextLineHeight() +
                       line_advance * static_cast<float>(lines.size() - 1u);
  const float x = std::max(item_min.x + padding,
                           item_min.x + item_size.x - width - padding);
  const float y = std::max(item_min.y + padding,
                           item_min.y + item_size.y - height - padding);
  const ImU32 shadow = IM_COL32(0, 0, 0, 180);
  const ImU32 foreground = IM_COL32(235, 238, 244, 230);

  for (std::size_t index = 0; index < lines.size(); ++index) {
    const std::string &line = lines[index];
    const ImVec2 position{x, y + line_advance * static_cast<float>(index)};
    draw_list.AddText({position.x + 1.0f, position.y + 1.0f}, shadow,
                      line.c_str());
    draw_list.AddText(position, foreground, line.c_str());
  }
}

std::optional<int>
hit_drawn_plane_captured_point(const std::array<QVec3, 3> &points,
                               int captured_point_count, QVec2 mouse_local,
                               const ViewportCameraController &camera,
                               ImVec2 item_min, ImVec2 item_size) {
  constexpr float kPointHitRadiusPixels = 10.0f;
  const ImVec2 mouse{item_min.x + mouse_local.x, item_min.y + mouse_local.y};
  const int count = std::min(captured_point_count, 2);
  for (int index = count - 1; index >= 0; --index) {
    const std::optional<ImVec2> screen = project_world_to_viewport(
        points[static_cast<std::size_t>(index)], camera, item_min, item_size);
    if (!screen) {
      continue;
    }
    if (viewport_distance_squared(mouse, *screen) <=
        kPointHitRadiusPixels * kPointHitRadiusPixels) {
      return index;
    }
  }
  return std::nullopt;
}

bool hit_drawn_plane_line(const std::array<QVec3, 3> &points,
                          int captured_point_count, bool has_line,
                          QVec2 mouse_local,
                          const ViewportCameraController &camera,
                          ImVec2 item_min, ImVec2 item_size) {
  constexpr float kLineHitRadiusPixels = 10.0f;
  if (!has_line || captured_point_count < 2) {
    return false;
  }

  const std::optional<ImVec2> start =
      project_world_to_viewport(points[0], camera, item_min, item_size);
  const std::optional<ImVec2> end =
      project_world_to_viewport(points[1], camera, item_min, item_size);
  if (!start || !end) {
    return false;
  }

  const ImVec2 mouse{item_min.x + mouse_local.x, item_min.y + mouse_local.y};
  return viewport_distance_to_segment_squared(mouse, *start, *end) <=
         kLineHitRadiusPixels * kLineHitRadiusPixels;
}

ModelingBoxToolEvent modeling_box_event(const ViewportCameraController &camera,
                                        const ViewportInputFrame &input,
                                        float grid_size, bool snap_enabled) {
  const ViewportRay ray = camera.ray_from_viewport_point(input.mouse_local);
  return {
      .screen_position = input.mouse_local,
      .ray =
          {
              .origin = ray.origin,
              .direction = ray.direction,
          },
      .grid_size = grid_size,
      .has_camera = true,
      .camera =
          ViewportCameraAdapter::modeling_camera(camera, input.viewport_size),
      .snap_enabled = snap_enabled,
  };
}

void focus_bounds_or_origin(std::optional<SceneBounds> bounds,
                            ViewportCameraController &camera) {
  if (bounds) {
    camera.focus_bounds(bounds->min, bounds->max);
  } else {
    camera.focus_origin();
  }
}

void focus_all(const SceneDocument &document, const AssetRegistry &assets,
               ViewportCameraController &camera) {
  focus_bounds_or_origin(document_world_bounds(document, assets), camera);
}

bool focus_selection(const ModelingSessionService &modeling,
                     ViewportCameraController &camera) {
  const std::optional<SceneBounds> bounds = modeling.selection_bounds();
  if (!bounds) {
    return false;
  }
  camera.focus_bounds(bounds->min, bounds->max);
  return true;
}

void log_command_failure(std::string_view source,
                         const QuaderApiVoidResult &result) {
  if (result.ok) {
    return;
  }
  LOG_ERROR("{} failed: {}", source,
            result.message.empty() ? "Command failed." : result.message);
}

void mark_navigation_pressed(ViewportNavigationInput &navigation,
                             ViewportInputActionKind action) {
  switch (action) {
  case ViewportInputActionKind::Orbit:
    navigation.orbit_pressed = true;
    navigation.orbit_down = true;
    break;
  case ViewportInputActionKind::OrbitSnap:
    navigation.orbit_snap_pressed = true;
    navigation.orbit_snap_down = true;
    break;
  case ViewportInputActionKind::Pan:
    navigation.pan_pressed = true;
    navigation.pan_down = true;
    break;
  case ViewportInputActionKind::Fly:
    navigation.fly_pressed = true;
    navigation.fly_down = true;
    break;
  case ViewportInputActionKind::ResetFlySpeed:
    navigation.reset_fly_speed_pressed = true;
    break;
  case ViewportInputActionKind::CancelNavigation:
    navigation.cancel_navigation_pressed = true;
    break;
  case ViewportInputActionKind::FlyForward:
    navigation.fly_forward_down = true;
    break;
  case ViewportInputActionKind::FlyBackward:
    navigation.fly_backward_down = true;
    break;
  case ViewportInputActionKind::FlyRight:
    navigation.fly_right_down = true;
    break;
  case ViewportInputActionKind::FlyLeft:
    navigation.fly_left_down = true;
    break;
  default:
    break;
  }
}

void mark_resolved_input_action_pressed(ViewportInputFrame &input,
                                        ViewportInputActionKind action) {
  switch (action) {
  case ViewportInputActionKind::ModalToolFinalize:
    input.modal_tool_finalize_pressed = true;
    break;
  case ViewportInputActionKind::ModalToolModeCycle:
    input.modal_tool_mode_cycle_pressed = true;
    break;
  case ViewportInputActionKind::QuickPivot:
    input.quick_pivot_pressed = true;
    input.quick_pivot_down = true;
    break;
  default:
    mark_navigation_pressed(input.navigation_bindings, action);
    break;
  }
}

void mark_navigation_down(ViewportNavigationInput &navigation,
                          ViewportInputActionKind action) {
  switch (action) {
  case ViewportInputActionKind::Orbit:
    navigation.orbit_down = true;
    break;
  case ViewportInputActionKind::OrbitSnap:
    navigation.orbit_snap_down = true;
    break;
  case ViewportInputActionKind::Pan:
    navigation.pan_down = true;
    break;
  case ViewportInputActionKind::Fly:
    navigation.fly_down = true;
    break;
  case ViewportInputActionKind::Zoom:
    navigation.zoom_wheel = true;
    break;
  case ViewportInputActionKind::ZoomToCursor:
    navigation.zoom_to_cursor_wheel = true;
    break;
  case ViewportInputActionKind::FlyForward:
    navigation.fly_forward_down = true;
    break;
  case ViewportInputActionKind::FlyBackward:
    navigation.fly_backward_down = true;
    break;
  case ViewportInputActionKind::FlyRight:
    navigation.fly_right_down = true;
    break;
  case ViewportInputActionKind::FlyLeft:
    navigation.fly_left_down = true;
    break;
  default:
    break;
  }
}

void mark_resolved_input_action_down(ViewportInputFrame &input,
                                     ViewportInputActionKind action) {
  switch (action) {
  case ViewportInputActionKind::QuickPivot:
    input.quick_pivot_down = true;
    break;
  default:
    mark_navigation_down(input.navigation_bindings, action);
    break;
  }
}

void mark_navigation_released(ViewportNavigationInput &navigation,
                              ViewportInputActionKind action) {
  switch (action) {
  case ViewportInputActionKind::Orbit:
    navigation.orbit_released = true;
    break;
  case ViewportInputActionKind::OrbitSnap:
    navigation.orbit_snap_released = true;
    break;
  case ViewportInputActionKind::Pan:
    navigation.pan_released = true;
    break;
  case ViewportInputActionKind::Fly:
    navigation.fly_released = true;
    break;
  default:
    break;
  }
}

void mark_resolved_input_action_released(ViewportInputFrame &input,
                                         ViewportInputActionKind action) {
  switch (action) {
  case ViewportInputActionKind::QuickPivot:
    input.quick_pivot_released = true;
    break;
  default:
    mark_navigation_released(input.navigation_bindings, action);
    break;
  }
}

void sync_fly_direction_from_navigation_bindings(ViewportInputFrame &input) {
  input.fly_local_direction = {};
  if (input.navigation_bindings.fly_forward_down) {
    input.fly_local_direction.z += 1.0f;
  }
  if (input.navigation_bindings.fly_backward_down) {
    input.fly_local_direction.z -= 1.0f;
  }
  if (input.navigation_bindings.fly_right_down) {
    input.fly_local_direction.x += 1.0f;
  }
  if (input.navigation_bindings.fly_left_down) {
    input.fly_local_direction.x -= 1.0f;
  }
}

QuaderViewportKeybindingContext
keybinding_context_from_input(const ViewportInputFrame &input) {
  return {
      .viewport_hovered = input.viewport_hovered,
      .navigation_active = input.navigation_active,
      .fly_navigation_active = input.fly_navigation_active,
      .orbit_navigation_active = input.orbit_navigation_active,
      .pan_navigation_active = input.pan_navigation_active,
      .modal_tool_active = input.modal_tool_active,
      .active_tool_owns_viewport = input.active_tool_owns_viewport,
      .operation_preview_active = input.operation_preview_active,
  };
}

bool resolve_viewport_input_action_for_chord(QuaderEditorApi &editor,
                                             const ViewportToolState &state,
                                             const ViewportInputFrame &input,
                                             const QuaderInputChord &chord,
                                             ViewportInputActionKind &action) {
  return editor.commands().resolve_viewport_input_action(
      chord, state, input.viewport_focused, input.text_input_active, action,
      keybinding_context_from_input(input));
}

bool apply_viewport_keybinding_dispatch(
    QuaderEditorApi &editor, const QuaderViewportKeybindingDispatch &dispatch,
    ViewportInputFrame &input, bool *command_shortcut_consumed) {
  switch (dispatch.kind) {
  case QuaderViewportKeybindingDispatch::Kind::PendingChord:
    input.keybinding_input_consumed = true;
    return true;
  case QuaderViewportKeybindingDispatch::Kind::ViewportShortcut:
    input.resolved_shortcuts.push_back(dispatch.shortcut);
    return true;
  case QuaderViewportKeybindingDispatch::Kind::Command:
    log_command_failure("Keybinding command",
                        editor.commands().execute(dispatch.command_id));
    if (command_shortcut_consumed != nullptr) {
      *command_shortcut_consumed = true;
    }
    input.keybinding_input_consumed = true;
    return true;
  case QuaderViewportKeybindingDispatch::Kind::NoMatch:
    return false;
  }
  return false;
}

void resolve_keybindings_for_frame(QuaderEditorApi &editor,
                                   const ViewportToolState &state,
                                   ViewportInputFrame &input,
                                   bool *command_shortcut_consumed) {
  input.resolved_shortcuts.clear();
  input.navigation_bindings = {};
  if (!input.viewport_focused || input.text_input_active) {
    editor.commands().reset_pending_keybinding_chord();
    return;
  }

  for (const QuaderInputChord &chord : input.active_input_chords) {
    ViewportInputActionKind action = ViewportInputActionKind::None;
    if (resolve_viewport_input_action_for_chord(editor, state, input, chord,
                                                action)) {
      mark_resolved_input_action_down(input, action);
    }
  }
  for (const QuaderInputChord &chord : input.released_input_chords) {
    ViewportInputActionKind action = ViewportInputActionKind::None;
    if (resolve_viewport_input_action_for_chord(editor, state, input, chord,
                                                action)) {
      mark_resolved_input_action_released(input, action);
    }
  }
  for (const QuaderInputChord &chord : input.wheel_input_chords) {
    ViewportInputActionKind action = ViewportInputActionKind::None;
    if (resolve_viewport_input_action_for_chord(editor, state, input, chord,
                                                action)) {
      mark_navigation_down(input.navigation_bindings, action);
    }
  }

  for (const QuaderInputChord &chord : input.pressed_input_chords) {
    ViewportInputActionKind action = ViewportInputActionKind::None;
    if (resolve_viewport_input_action_for_chord(editor, state, input, chord,
                                                action)) {
      mark_resolved_input_action_pressed(input, action);
      continue;
    }

    const QuaderViewportKeybindingDispatch dispatch =
        editor.commands().dispatch_viewport_input_binding(
            chord, state, input.viewport_focused, input.text_input_active,
            keybinding_context_from_input(input));
    if (apply_viewport_keybinding_dispatch(editor, dispatch, input,
                                           command_shortcut_consumed)) {
      return;
    }
  }

  sync_fly_direction_from_navigation_bindings(input);
}

QuaderApiVoidResult
execute_delete_popup_command(QuaderEditorApi &editor,
                             ViewportInputRouter &input_router,
                             QuaderCommandId command_id) {
  const QuaderApiVoidResult result = editor.commands().execute(command_id);
  if (!result.ok) {
    LOG_ERROR("Delete popup command '{}' failed: {}", command_id.string(),
              result.message.empty() ? "Operation failed." : result.message);
  }
  input_router.set_selection_mode(app_selection_mode_to_viewport(
      to_modeling_selection_mode(editor.selection().summary().mode)));
  return result;
}

bool draw_viewport_delete_popup(bool &object_popup_open,
                                bool &object_popup_position_pending,
                                ImVec2 &object_popup_position,
                                QuaderEditorApi &editor,
                                ViewportInputRouter &input_router,
                                LocalizationService &localization) {
  bool changed = false;

  if (object_popup_open) {
    if (object_popup_position_pending) {
      ImGui::SetNextWindowPos(object_popup_position, ImGuiCond_Always,
                              ImVec2(0.5f, 0.0f));
      object_popup_position_pending = false;
    }
    const std::string window_title =
        localization.text("ui.viewport.delete_popup.window_title", "Delete") +
        "##ViewportDeleteObjectsWindow";
    bool open = object_popup_open;
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin(window_title.c_str(), &open, flags)) {
      const std::string title = localization.text(
          "ui.viewport.delete_popup.title", "Delete Selected Objects");
      ImGui::TextUnformatted(title.c_str());
      const std::string delete_label =
          localization.text("ui.viewport.delete_popup.delete", "Delete");
      if (ImGui::Button(delete_label.c_str())) {
        const QuaderApiVoidResult result = execute_delete_popup_command(
            editor, input_router, quader_command::kDeleteSelection);
        changed = result.changed;
        open = false;
      }
      ImGui::SameLine();
      const std::string cancel_label =
          localization.text("ui.viewport.delete_popup.cancel", "Cancel");
      if (ImGui::Button(cancel_label.c_str())) {
        open = false;
      }
    }
    ImGui::End();
    object_popup_open = open;
  }

  if (ImGui::BeginPopup("ViewportDeleteElementsPopup")) {
    const ModelingSelectionSummary summary =
        to_modeling_selection_summary(editor.selection().summary());
    /**
     * Represents a Popup Action value used by the ImGui editor presentation
     * layer.
     */
    struct PopupAction {
      const char *label_key;
      const char *fallback;
      QuaderCommandId command_id;
      bool enabled;
    };
    const PopupAction actions[] = {
        {"ui.viewport.delete_vertices", "Delete Vertices",
         quader_command::kDeleteSelection,
         summary.mode == ModelingSelectionMode::Vertex &&
             summary.selected_vertex_count > 0u},
        {"ui.viewport.delete_face", "Delete Face",
         quader_command::kDeleteSelection,
         summary.mode == ModelingSelectionMode::Face &&
             summary.selected_face_count > 0u},
        {"ui.viewport.delete_edges", "Delete Edges",
         quader_command::kDeleteSelection,
         summary.mode == ModelingSelectionMode::Edge &&
             summary.selected_edge_count > 0u},
        {"ui.viewport.dissolve_edges", "Dissolve Edges",
         quader_command::kModelingDissolveEdges,
         summary.mode == ModelingSelectionMode::Edge &&
             summary.selected_edge_count > 0u},
        {"ui.viewport.dissolve_vertex", "Dissolve Vertex",
         quader_command::kModelingDissolveVertices,
         summary.mode == ModelingSelectionMode::Vertex &&
             summary.selected_vertex_count > 0u},
    };
    for (const PopupAction &action : actions) {
      const std::string label =
          localization.text(action.label_key, action.fallback);
      if (ImGui::MenuItem(label.c_str(), nullptr, false, action.enabled)) {
        const QuaderApiVoidResult result = execute_delete_popup_command(
            editor, input_router, action.command_id);
        changed = changed || result.changed;
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::EndPopup();
  }

  return changed;
}

} // namespace

ViewportPanel::ViewportPanel() {
  panes_[viewport_pane_index(ViewportPaneId::Perspective)].id =
      ViewportPaneId::Perspective;
  panes_[viewport_pane_index(ViewportPaneId::Top)].id = ViewportPaneId::Top;
  panes_[viewport_pane_index(ViewportPaneId::Front)].id = ViewportPaneId::Front;
  panes_[viewport_pane_index(ViewportPaneId::Side)].id = ViewportPaneId::Side;
}

ViewportPanel::~ViewportPanel() {
    shutdown();
}

void ViewportPanel::initialize(RenderSystem &render_system) {
  shutdown_complete_ = false;
  for (Pane &pane_state : panes_) {
    if (pane_state.initialized) {
      continue;
    }
    render_system.initialize_viewport_renderer(
        pane_state.renderer,
        viewport_grid_layer_mask(viewport_pane_index(pane_state.id)));
    pane_state.initialized = true;
  }
}

void ViewportPanel::shutdown() {
  if (shutdown_complete_) {
    return;
  }

  shutdown_complete_ = true;
  viewport_info_cached_document_ = nullptr;
  viewport_info_cached_assets_ = nullptr;
  viewport_info_scene_stats_valid_ = false;
  for (Pane &pane_state : panes_) {
    pane_state.renderer.shutdown();
    pane_state.initialized = false;
  }
    if (workspace_ != nullptr) {
      workspace_->clear_pane_visibility();
      workspace_ = nullptr;
    }
    active_render_panes_.clear();
}

void ViewportPanel::prepare_viewports(ViewportWorkspaceService &workspace,
                                      QuaderEditorApi &editor,
                                      SceneRenderBridge &render_bridge,
                                      const SceneDocument &document,
                                      const AssetRegistry &assets,
                                      const PivotToolService *pivot) {
  QDR_PROFILE_FUNCTION();
  workspace_ = &workspace;
  const float grid_world_size =
      viewport_grid_world_size_for_preset(workspace.grid_preset());
  const ViewportSettings &settings = active_viewport_settings();
  const bool grid_visible =
      settings.bool_value(ViewportBoolSettingId::ShowGrid);
  const bool gizmos_visible =
      settings.bool_value(ViewportBoolSettingId::ShowGizmos);
  const bool overlays_visible =
      settings.bool_value(ViewportBoolSettingId::ShowOverlays);
  const PivotHandleState pivot_handle =
      pivot != nullptr ? pivot->handle_state() : PivotHandleState{};
  const bool pivot_handle_editing =
      pivot_handle.visible && pivot_handle.editing;
  render_bridge.update_mesh_surface_grid(grid_world_size, settings);
  const ModelingSelectionMode cached_overlay_mode =
      selection_overlay_cache_.valid()
          ? selection_overlay_cache_.key().selection_mode
          : viewport_selection_mode_to_app(
                input_router_.state().selection_mode);
  std::uint8_t wireframe_layer_mask = 0u;
  for (ViewportPaneId pane_id : active_render_panes_) {
    const ViewportWorkspacePaneState &pane_workspace = workspace.pane(pane_id);
    if (pane(pane_id).initialized && pane_workspace.visible_this_frame &&
        pane_workspace.shading == ViewportShadingMode::Wireframe) {
      wireframe_layer_mask = static_cast<std::uint8_t>(
          wireframe_layer_mask |
          viewport_grid_layer_mask(viewport_pane_index(pane_id)));
    }
  }
  (void)QuaderRenderApiRuntimeAccess::sync_scene_asset_overlays(
      editor.render(), render_bridge,
      QuaderRenderSceneOverlaySyncRequest{
          .selected_object_overlay_visible =
              overlays_visible &&
              cached_overlay_mode == ModelingSelectionMode::Object,
          .scene_wireframe_layer_mask = wireframe_layer_mask,
      });
  for (ViewportPaneId pane_id : active_render_panes_) {
    QDR_PROFILE_SCOPE("Viewport prepare pane");
    Pane &pane_state = pane(pane_id);
    ViewportWorkspacePaneState &pane_workspace = workspace.pane(pane_id);
    if (!pane_state.initialized || !pane_workspace.visible_this_frame) {
      continue;
    }
    if (const EntityId root = document.scene_root(); root != kInvalidEntityId) {
      if (const SceneSettingsComponent *scene_settings =
              document.scene_settings(root)) {
        pane_workspace.camera.set_clip_planes(
            scene_settings->camera.near_plane,
            scene_settings->camera.far_plane);
        pane_state.renderer.apply_scene_settings(
            *scene_settings,
            pane_workspace.shading == ViewportShadingMode::Rendered);
      }
    }
    pane_state.renderer.update_camera(pane_workspace.camera);
    pane_state.renderer.update_ground_grid(
        pane_workspace.camera, grid_world_size, settings, grid_visible);
    pane_state.renderer.set_scene_layer_mask(
        pane_workspace.shading == ViewportShadingMode::Rendered
            ? kViewportRenderedSceneLayerMask
            : kViewportShadedSceneLayerMask);
    pane_state.renderer.set_shadowing_enabled(pane_workspace.shading ==
                                              ViewportShadingMode::Rendered);
    pane_state.renderer.set_scene_visible(pane_workspace.shading !=
                                          ViewportShadingMode::Wireframe);
    pane_state.renderer.set_overlay_visible(
        overlays_visible || gizmos_visible ||
        pane_workspace.shading == ViewportShadingMode::Wireframe ||
        pivot_handle.visible);
    const bool point_light_gizmos_visible =
        point_light_gizmos_visible_for_pane(gizmos_visible,
                                            pane_workspace.shading);
    ViewportOverlayFrame overlay_frame =
        overlays_visible && selection_overlay_cache_.valid()
            ? selection_overlay_cache_.frame()
            : ViewportOverlayFrame{};
    if ((overlays_visible && selection_overlay_cache_.valid()) ||
        point_light_gizmos_visible) {
      QDR_PROFILE_SCOPE("Viewport build render-owned scene overlay frame");
      overlay_frame = QuaderRenderApiRuntimeAccess::build_scene_overlay_frame(
          editor.render(), render_bridge, std::move(overlay_frame),
          ViewportSceneOverlayFrameRequest{
              .selection_mode = cached_overlay_mode,
              .wireframe_pane =
                  pane_workspace.shading == ViewportShadingMode::Wireframe,
              .point_light_gizmos_visible = point_light_gizmos_visible,
          });
    }
    if (pane_id == interaction_pane_id_) {
      const ViewportOverlayCamera overlay_camera =
          ViewportCameraAdapter::overlay_camera(pane_workspace.camera,
                                                pane_workspace.render_size);
      if (const quader_editor_core::TransformGizmoFrame *gizmo_frame =
              interaction_controller_.transform_gizmo_frame()) {
        if (gizmos_visible && !pivot_handle_editing) {
          ViewportTransformGizmoOverlayBuilder::append(
              overlay_frame, *gizmo_frame, overlay_camera);
        }
      }
      if (overlays_visible) {
        ViewportSelectionMarqueeOverlayBuilder::append(
            overlay_frame, overlay_camera,
            interaction_controller_.selection_marquee());
      }
    }
    append_pivot_handle_overlay(overlay_frame, pane_workspace.camera,
                                pivot_handle);
    ViewportOverlayStyleResolver::apply(
        overlay_frame, settings,
        ViewportOverlayStyleContext{
            .selection_mode = overlay_selection_context(cached_overlay_mode)});
    {
      QDR_PROFILE_SCOPE("Viewport submit overlay frame");
      render_bridge.set_overlay_frame(
          viewport_pane_index(pane_id),
          viewport_grid_layer_mask(viewport_pane_index(pane_id)),
          std::move(overlay_frame),
          ViewportCameraAdapter::overlay_camera(pane_workspace.camera,
                                                pane_workspace.render_size));
    }
  }
}

void ViewportPanel::render_viewports(RenderSystem &render_system) {
  QDR_PROFILE_FUNCTION();
  for (ViewportPaneId pane_id : active_render_panes_) {
    QDR_PROFILE_SCOPE("Viewport render pane");
    Pane &pane_state = pane(pane_id);
    if (!pane_state.initialized || workspace_ == nullptr ||
        !workspace_->pane_visible(pane_id)) {
      continue;
    }
    render_system.render_viewport(pane_state.renderer);
  }
}

QuaderApiVoidResult
ViewportPanel::frame_all(ViewportWorkspaceService &workspace,
                         const SceneDocument &document,
                         const AssetRegistry &assets,
                         ViewportPaneId pane_id) {
  focus_all(document, assets, workspace.camera(pane_id));
  return {
      .ok = true,
      .changed = true,
      .message = "Focus All",
      .mutation_kind = QuaderMutationKind::Runtime,
  };
}

QuaderApiVoidResult
ViewportPanel::frame_selection(ViewportWorkspaceService &workspace,
                               const SceneDocument &document,
                               const AssetRegistry &assets,
                               const ModelingSessionService &modeling,
                               ViewportPaneId pane_id) {
  (void)document;
  (void)assets;
  if (!focus_selection(modeling, workspace.camera(pane_id))) {
    return {
        .ok = false,
        .changed = false,
        .message = "Select something to focus.",
        .mutation_kind = QuaderMutationKind::None,
    };
  }
  return {
      .ok = true,
      .changed = true,
      .message = "Focus Selection",
      .mutation_kind = QuaderMutationKind::Runtime,
  };
}

bool ViewportPanel::activate_tool_from_menu(ViewportToolId tool,
                                            QuaderEditorApi &editor,
                                            ModelingSessionService &modeling) {
  LOG_INFO("Tools menu requested viewport tool {}", viewport_tool_id(tool));
  const ViewportTool *tool_definition = find_viewport_tool(tool);
  const QuaderApiVoidResult command =
      tool_definition != nullptr ? editor.tools().activate(tool)
                                 : QuaderApiVoidResult{
                                       .ok = false,
                                       .changed = false,
                                       .message = "Unknown viewport tool.",
                                   };
  if (!command.ok) {
    LOG_ERROR("Viewport tool activation failed: {}",
              command.message.empty() ? "Command failed." : command.message);
    return false;
  }
  const ViewportToolId active_tool =
      viewport_tool_for_modeling(modeling.tool_status().active_tool);
  LOG_INFO("Viewport tool activated from menu: requested {}, active {}",
           viewport_tool_id(tool), viewport_tool_id(active_tool));
  input_router_.set_active_tool(active_tool);
  input_router_.set_selection_mode(app_selection_mode_to_viewport(
      to_modeling_selection_mode(editor.selection().summary().mode)));
  input_router_.clear_modal_tool_state();
  interaction_controller_.reset(&editor);
  hover_controller_.invalidate();
  selection_overlay_cache_.mark_dirty();
  return true;
}

ViewportPanel::Pane &ViewportPanel::pane(ViewportPaneId pane_id) {
  return panes_[viewport_pane_index(pane_id)];
}

const ViewportPanel::Pane &ViewportPanel::pane(ViewportPaneId pane_id) const {
  return panes_[viewport_pane_index(pane_id)];
}

void ViewportPanel::sync_active_tool_from_editor(
    QuaderEditorApi &editor, ModelingSessionService &modeling) {
  const ViewportToolId active_tool =
      viewport_tool_for_modeling(modeling.tool_status().active_tool);
  if (input_router_.state().active_tool == active_tool) {
    return;
  }

  LOG_INFO("Viewport tool synchronized from editor: {}",
           viewport_tool_id(active_tool));
  input_router_.set_active_tool(active_tool);
  input_router_.clear_modal_tool_state();
  interaction_controller_.reset(&editor);
  hover_controller_.invalidate();
  selection_overlay_cache_.mark_dirty();
}

bool ViewportPanel::draw_pane_chrome(Pane &pane_state, ImVec2 item_min,
                                     ImVec2 item_size,
                                     const LocalizationService &localization) {
  ViewportWorkspaceService &viewport_workspace = workspace();
  ViewportWorkspacePaneState &pane_workspace =
      viewport_workspace.pane(pane_state.id);
  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  const ImVec2 item_max{item_min.x + item_size.x, item_min.y + item_size.y};
  const ImU32 divider_color = IM_COL32(19, 19, 20, 255);
  const ImU32 focused_color = IM_COL32(46, 46, 48, 242);
  draw_list->AddRect(item_min, item_max,
                     pane_state.id == viewport_workspace.focused_pane()
                         ? focused_color
                         : divider_color,
                     0.0f, 0, 1.0f);

  constexpr float kComboHeight = 28.0f;
  constexpr float kComboGap = 6.0f;
  ImGui::PushID(static_cast<int>(viewport_pane_index(pane_state.id)));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 4.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
  ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(16, 18, 21, 210));

  const float view_desired_width = viewport_view_combo_width(localization);
  const float shading_desired_width =
      viewport_shading_combo_width(localization);
  const float available_width = std::max(0.0f, item_size.x - 16.0f);
  const bool compact =
      available_width < view_desired_width + shading_desired_width + kComboGap;
  const float shading_width =
      compact ? std::min(shading_desired_width, available_width)
              : shading_desired_width;
  const float view_width =
      compact ? std::max(0.0f, available_width - shading_width - kComboGap)
              : view_desired_width;
  const float combo_gap = view_width >= 44.0f ? kComboGap : 0.0f;
  const ImVec2 combo_pos{
      item_max.x - 8.0f - view_width - combo_gap - shading_width,
      item_min.y + 7.0f,
  };
  const ImRect chrome_rect(
      combo_pos, ImVec2(item_max.x - 8.0f, combo_pos.y + kComboHeight));
  bool consumes_input = chrome_rect.Contains(ImGui::GetMousePos());
  (void)imgui_close_combo_popup_for_click_inside(
      item_min, item_max, chrome_rect.Min, chrome_rect.Max);

  ImGui::SetCursorScreenPos(combo_pos);
  if (view_width >= 44.0f) {
    ImGui::SetNextItemWidth(view_width);
    const ViewportView current_view = viewport_workspace.view(pane_state.id);
    const std::string preview =
        localized_viewport_view_label(localization, current_view);
    if (ImGui::BeginCombo("##ViewportView", preview.c_str(),
                          ImGuiComboFlags_HeightSmall)) {
      consumes_input = true;
      for (const ViewportView view : kViewportChromeViews) {
        const bool selected = current_view == view;
        const std::string label =
            localized_viewport_view_label(localization, view);
        if (ImGui::Selectable(label.c_str(), selected)) {
          set_pane_view(pane_state, view);
        }
        if (selected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }
    ImGui::SameLine(0.0f, combo_gap);
  }
  ImGui::SetNextItemWidth(shading_width);
  const std::string shading_preview = localized_viewport_shading_mode_label(
      localization, pane_workspace.shading);
  if (ImGui::BeginCombo("##ViewportShading", shading_preview.c_str(),
                        ImGuiComboFlags_HeightSmall)) {
    consumes_input = true;
    for (const ViewportShadingMode mode : kViewportChromeShadingModes) {
      const bool selected = pane_workspace.shading == mode;
      const std::string label =
          localized_viewport_shading_mode_label(localization, mode);
      if (ImGui::Selectable(label.c_str(), selected)) {
        if (viewport_workspace.set_shading_mode(pane_state.id, mode)) {
          selection_overlay_cache_.mark_dirty();
        }
      }
      if (selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }
  ImGui::PopStyleColor();
  ImGui::PopStyleVar(2);
  ImGui::PopID();
  return consumes_input;
}

void ViewportPanel::set_pane_view(Pane &pane_state, ViewportView view) {
  if (!workspace().set_view(pane_state.id, view)) {
    return;
  }
  hover_controller_.invalidate();
  selection_overlay_cache_.mark_dirty();
}

bool ViewportPanel::handle_split_input(
    ImVec2 viewport_min, ImVec2 viewport_size,
    const std::vector<ViewportSplitHandle> &handles) {
  constexpr float kHitThickness = 7.0f;
  bool consumed = false;
  auto handle_rect = [&](const ViewportSplitHandle &handle, float thickness) {
    const float half = thickness * 0.5f;
    if (handle.axis == ViewportSplitHandleAxis::Vertical) {
      const float x = viewport_min.x + viewport_size.x * handle.position;
      const float y = viewport_min.y + viewport_size.y * handle.start;
      const float height =
          viewport_size.y * std::max(0.0f, handle.end - handle.start);
      return ImRect{{x - half, y}, {x + half, y + height}};
    }
    const float y = viewport_min.y + viewport_size.y * handle.position;
    const float x = viewport_min.x + viewport_size.x * handle.start;
    const float width =
        viewport_size.x * std::max(0.0f, handle.end - handle.start);
    return ImRect{{x, y - half}, {x + width, y + half}};
  };

  for (const ViewportSplitHandle &handle : handles) {
    const ImRect hit = handle_rect(handle, kHitThickness);
    if (hit.Contains(ImGui::GetMousePos())) {
      ImGui::SetMouseCursor(handle.axis == ViewportSplitHandleAxis::Vertical
                                ? ImGuiMouseCursor_ResizeEW
                                : ImGuiMouseCursor_ResizeNS);
    }
  }

  if (!split_handle_dragging_ && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    for (auto it = handles.rbegin(); it != handles.rend(); ++it) {
      if (handle_rect(*it, kHitThickness).Contains(ImGui::GetMousePos())) {
        active_split_handle_ = *it;
        split_handle_dragging_ = true;
        consumed = true;
        break;
      }
    }
  }

  if (split_handle_dragging_ && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    const ImVec2 mouse = ImGui::GetMousePos();
    const float normalized_position =
        active_split_handle_.axis == ViewportSplitHandleAxis::Vertical
            ? std::clamp((mouse.x - viewport_min.x) /
                             std::max(viewport_size.x, 1.0f),
                         0.0f, 1.0f)
            : std::clamp((mouse.y - viewport_min.y) /
                             std::max(viewport_size.y, 1.0f),
                         0.0f, 1.0f);
    workspace().set_split_handle_position(active_split_handle_.id,
                                          normalized_position);
    consumed = true;
  }
  if (split_handle_dragging_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
    split_handle_dragging_ = false;
    consumed = true;
  }

  if (ImGui::GetCurrentWindow()->DrawList != nullptr) {
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    for (const ViewportSplitHandle &handle : handles) {
      const ImRect line = handle_rect(handle, 1.0f);
      draw_list->AddRectFilled(line.Min, line.Max, IM_COL32(31, 31, 33, 255));
    }
  }
  return consumed || split_handle_dragging_;
}

const ViewportSettings &ViewportPanel::active_viewport_settings() const {
  return viewport_settings_ != nullptr ? *viewport_settings_
                                       : fallback_viewport_settings_;
}

void ViewportPanel::draw(UIContext& context) {
  if (context.viewport == nullptr) {
    return;
  }
  UIViewportSurfaceContext &viewport = *context.viewport;
  viewport_settings_ = &context.preferences.viewport;
  draw(viewport.render_bridge, viewport.document, viewport.assets,
       viewport.modeling, viewport.default_material,
       viewport.relative_mouse_delta,
       viewport.cursor_capture_requested,
       viewport.cursor_capture_point, context);
}

void ViewportPanel::draw(SceneRenderBridge &render_bridge,
                         SceneDocument &document, AssetRegistry &assets,
                         ModelingSessionService &modeling,
                         AssetId default_material, QVec2 captured_mouse_delta,
                         bool *viewport_cursor_capture_requested,
                         QVec2 *viewport_cursor_capture_point,
                         const UIContext &ui_context) {
  QDR_PROFILE_FUNCTION();
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  const std::string window_title = ui_context.localization.imgui_label(
      "ui.panel.viewport", ui_panel_key(UIPanelId::Viewport), "Viewport");
  const bool visible = ImGui::Begin(window_title.c_str(), nullptr,
                                    ImGuiWindowFlags_NoScrollbar |
                                        ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar(2);
  ViewportWorkspaceService &viewport_workspace =
      QuaderViewportApiRuntimeAccess::workspace(
          ui_context.intent.editor.viewports());
  workspace_ = &viewport_workspace;
  input_router_.sync_grid_preset_snapshot(viewport_workspace.grid_preset());
  sync_active_tool_from_editor(ui_context.intent.editor, modeling);
  if (!visible) {
    for (Pane &pane_state : panes_) {
      render_bridge.set_overlay_frame(
          viewport_pane_index(pane_state.id),
          viewport_grid_layer_mask(viewport_pane_index(pane_state.id)), {},
          ViewportCameraAdapter::overlay_camera(
              viewport_workspace.camera(pane_state.id), {1.0f, 1.0f}));
    }
    (void)QuaderRenderApiRuntimeAccess::sync_scene_asset_overlays(
        ui_context.intent.editor.render(), render_bridge,
        QuaderRenderSceneOverlaySyncRequest{});
    last_fly_navigation_active_ = false;
    viewport_workspace.clear_pane_visibility();
    active_render_panes_.clear();
    ImGui::End();
    return;
  }

  ImVec2 available = ImGui::GetContentRegionAvail();
  available.x = std::max(available.x, 1.0f);
  available.y = std::max(available.y, 1.0f);
  const ImVec2 canvas_min = ImGui::GetCursorScreenPos();
  ImGui::Dummy(available);

  viewport_workspace.clear_pane_visibility();
  active_render_panes_.clear();

  /**
   * Represents a Draw Pane value used by the ImGui editor presentation layer.
   */
  struct DrawPane {
    Pane *pane = nullptr;
    ImVec2 item_min = {};
    ImVec2 item_size = {};
    bool hovered = false;
    bool focus_hovered = false;
    bool chrome_consumes_input = false;
  };
  std::vector<DrawPane> draw_panes;
  const std::vector<ViewportPanePlacement> placements =
      viewport_workspace.active_pane_placements();
  draw_panes.reserve(placements.size());
  for (const ViewportPanePlacement &placement : placements) {
    QDR_PROFILE_SCOPE("Viewport draw pane image");
    Pane &pane_state = pane(placement.pane_id);
    const ImVec2 item_min = ViewportPaneController::pane_rect_min(
        canvas_min, available, placement.normalized_rect);
    const ImVec2 item_size = ViewportPaneController::pane_rect_size(
        available, placement.normalized_rect);
    const QVec2 render_size = {std::max(item_size.x, 1.0f),
                               std::max(item_size.y, 1.0f)};
    const std::uint32_t render_width =
        static_cast<std::uint32_t>(render_size.x);
    const std::uint32_t render_height =
        static_cast<std::uint32_t>(render_size.y);
    viewport_workspace.set_pane_render_size(placement.pane_id, render_size);
    viewport_workspace.set_pane_visible(placement.pane_id, true);
    if (!should_defer_pane_render_resize(ui_context, pane_state.renderer)) {
      pane_state.renderer.resize(render_width, render_height);
    }
    active_render_panes_.push_back(placement.pane_id);

    const ImVec2 texture_uv_min{0.0f, pane_state.renderer.texture_max_v()};
    const ImVec2 texture_uv_max{pane_state.renderer.texture_max_u(), 0.0f};
    ImGui::SetCursorScreenPos(item_min);
    ImGui::PushID(static_cast<int>(viewport_pane_index(placement.pane_id)));
    ImGui::SetNextItemAllowOverlap();
    ImGui::Image(static_cast<ImTextureID>(pane_state.renderer.texture_handle()),
                 item_size, texture_uv_min, texture_uv_max);
    ImGui::GetWindowDrawList()->AddImage(
        static_cast<ImTextureID>(pane_state.renderer.overlay_texture_handle()),
        item_min, {item_min.x + item_size.x, item_min.y + item_size.y},
        texture_uv_min, texture_uv_max);
    DrawPane draw_pane{
        .pane = &pane_state,
        .item_min = ImGui::GetItemRectMin(),
        .item_size = {ImGui::GetItemRectSize().x, ImGui::GetItemRectSize().y},
        .hovered = ImGui::IsItemHovered(),
        .focus_hovered = ImGui::IsItemHovered(),
    };
    draw_panes.push_back(draw_pane);
    ImGui::PopID();
  }

  const std::vector<ViewportSplitHandle> split_handles =
      viewport_workspace.active_split_handles();
  const bool split_consumed =
      handle_split_input(canvas_min, available, split_handles);
  DrawPane *input_pane = nullptr;
  const bool transform_drag_locks_input = interaction_controller_.dragging();
  const bool navigation_gizmo_drag_locks_input =
      navigation_gizmo_.input_locked();
  for (DrawPane &draw_pane : draw_panes) {
    draw_pane.chrome_consumes_input =
        draw_pane_chrome(*draw_pane.pane, draw_pane.item_min,
                         draw_pane.item_size, ui_context.localization);
  }
  for (DrawPane &draw_pane : draw_panes) {
    if (!transform_drag_locks_input && !navigation_gizmo_drag_locks_input) {
      if (draw_pane.focus_hovered && !draw_pane.chrome_consumes_input &&
          !split_consumed) {
        viewport_workspace.focus_pane(draw_pane.pane->id);
      } else if (draw_pane.focus_hovered && !draw_pane.chrome_consumes_input &&
                 focus_controller_.input_active()) {
        viewport_workspace.focus_pane(draw_pane.pane->id);
      }
    }
    if (transform_drag_locks_input &&
        draw_pane.pane->id == interaction_pane_id_) {
      input_pane = &draw_pane;
    } else if (navigation_gizmo_drag_locks_input &&
               draw_pane.pane->id == navigation_gizmo_.locked_pane_id()) {
      input_pane = &draw_pane;
    } else if (!transform_drag_locks_input &&
               !navigation_gizmo_drag_locks_input && draw_pane.hovered &&
               !draw_pane.chrome_consumes_input && !split_consumed) {
      input_pane = &draw_pane;
    }
  }
  if (input_pane == nullptr) {
    for (DrawPane &draw_pane : draw_panes) {
      if (draw_pane.pane->id == viewport_workspace.focused_pane()) {
        input_pane = &draw_pane;
        break;
      }
    }
  }
  if (input_pane != nullptr) {
    QDR_PROFILE_SCOPE("Viewport draw input pane contents");
    const bool input_owns_active_gizmo_drag =
        transform_drag_locks_input &&
        input_pane->pane->id == interaction_pane_id_;
    const bool input_owns_active_navigation_gizmo_drag =
        navigation_gizmo_drag_locks_input &&
        input_pane->pane->id == navigation_gizmo_.locked_pane_id();
    draw_pane_contents(
        *input_pane->pane, document, assets, modeling, default_material,
        captured_mouse_delta, viewport_cursor_capture_requested,
        viewport_cursor_capture_point, ui_context, input_pane->item_min,
        input_pane->item_size,
        (input_pane->hovered && !input_pane->chrome_consumes_input &&
         !split_consumed) ||
            input_owns_active_gizmo_drag ||
            input_owns_active_navigation_gizmo_drag,
        (input_pane->focus_hovered && !input_pane->chrome_consumes_input &&
         !split_consumed) ||
            input_owns_active_gizmo_drag ||
            input_owns_active_navigation_gizmo_drag);
  }
  for (DrawPane &draw_pane : draw_panes) {
    if (&draw_pane == input_pane) {
      continue;
    }
    draw_pane_decorations(*draw_pane.pane, document, assets, modeling,
                          ui_context.viewport->selection,
                          ui_context.intent.editor, draw_pane.item_min,
                          draw_pane.item_size);
  }
  if (active_viewport_settings().bool_value(ViewportBoolSettingId::ShowInfo)) {
    for (const DrawPane &draw_pane : draw_panes) {
      if (draw_pane.pane->id != viewport_workspace.focused_pane()) {
        continue;
      }
      draw_viewport_info_overlay(document, assets, ui_context,
                                 draw_pane.item_min, draw_pane.item_size);
      break;
    }
  }
  refresh_cached_selection_overlay_frame(modeling, document, assets);
  ImGui::SetCursorScreenPos({canvas_min.x, canvas_min.y + available.y});
  ImGui::End();
}

void ViewportPanel::draw_pane_decorations(
    Pane &pane_state, SceneDocument &document, const AssetRegistry &assets,
    ModelingSessionService &modeling, EditorSelectionService &selection,
    QuaderEditorApi &editor, ImVec2 item_min, ImVec2 item_size) {
  ViewportCameraController &camera = workspace().camera(pane_state.id);
  navigation_gizmo_.draw(camera, pane_state.id, item_min, item_size, {},
                         nullptr, nullptr, false);
  (void)document;
  (void)assets;
  (void)modeling;
  (void)selection;
  (void)editor;
}

void ViewportPanel::draw_viewport_info_overlay(const SceneDocument &document,
                                               const AssetRegistry &assets,
                                               const UIContext &ui_context,
                                               ImVec2 item_min,
                                               ImVec2 item_size) {
  const std::uint64_t scene_revision = document.revision();
  const std::uint64_t asset_revision = assets.revision();
  const bool cache_hit =
      viewport_info_scene_stats_valid_ &&
      viewport_info_cached_document_ == &document &&
      viewport_info_cached_assets_ == &assets &&
      viewport_info_cached_scene_revision_ == scene_revision &&
      viewport_info_cached_asset_revision_ == asset_revision;
  ViewportInfoSceneStats scene_stats;
  if (cache_hit) {
    scene_stats.objects = viewport_info_cached_objects_;
    scene_stats.primitives = viewport_info_cached_primitives_;
    scene_stats.draw_calls = viewport_info_cached_draw_calls_;
  } else {
    QDR_PROFILE_SCOPE("Viewport info collect scene stats");
    scene_stats = collect_viewport_info_scene_stats(document, assets);
    viewport_info_cached_document_ = &document;
    viewport_info_cached_assets_ = &assets;
    viewport_info_cached_scene_revision_ = scene_revision;
    viewport_info_cached_asset_revision_ = asset_revision;
    viewport_info_cached_objects_ = scene_stats.objects;
    viewport_info_cached_primitives_ = scene_stats.primitives;
    viewport_info_cached_draw_calls_ = scene_stats.draw_calls;
    viewport_info_scene_stats_valid_ = true;
  }
  const RenderFramePerformanceStats &render_stats =
      ui_context.viewport->render_performance;

  const bool fps_available =
      ui_context.viewport->frame_timing.raw_delta_seconds > 0.0001;
  const double fps =
      fps_available ? 1.0 /
                          ui_context.viewport->frame_timing.raw_delta_seconds
                    : 0.0;

  const std::string unavailable =
      ui_context.localization.text("ui.viewport.info.unavailable", "--");
  const std::string cpu_value =
      render_stats.cpu_frame_ms_available
          ? format_viewport_info_milliseconds(render_stats.cpu_frame_ms)
          : unavailable;
  const std::string gpu_value =
      render_stats.gpu_frame_ms_available
          ? format_viewport_info_milliseconds(render_stats.gpu_frame_ms)
          : unavailable;
  const std::string fps_value =
      fps_available ? format_viewport_info_frames_per_second(fps) : unavailable;

  const std::array<std::string, 6> lines{{
      viewport_info_line(
          ui_context.localization.text("ui.viewport.info.fps", "FPS"),
          fps_value),
      viewport_info_line(
          ui_context.localization.text("ui.viewport.info.cpu", "CPU"),
          cpu_value),
      viewport_info_line(
          ui_context.localization.text("ui.viewport.info.gpu", "GPU"),
          gpu_value),
      viewport_info_line(
          ui_context.localization.text("ui.viewport.info.objects", "Objects"),
          format_viewport_info_number(scene_stats.objects)),
      viewport_info_line(ui_context.localization.text(
                             "ui.viewport.info.primitives", "Primitives"),
                         format_viewport_info_number(scene_stats.primitives)),
      viewport_info_line(ui_context.localization.text(
                             "ui.viewport.info.draw_calls", "Draw Calls"),
                         format_viewport_info_number(scene_stats.draw_calls)),
  }};
  draw_viewport_info_text(
      *ImGui::GetWindowDrawList(), item_min, item_size,
      std::span<const std::string>(lines.data(), lines.size()));
}

void ViewportPanel::refresh_cached_selection_overlay_frame(
    ModelingSessionService &modeling, const SceneDocument &document,
    const AssetRegistry &assets) {
  const ModelingSelectionSummary overlay_summary = [&]() {
    QDR_PROFILE_SCOPE("Viewport selection overlay summary");
    return modeling.selection_summary();
  }();
  const ModelingSelectionOverlayOptions overlay_options{
      .suppress_persistent_object_selection =
          overlay_summary.mode == ModelingSelectionMode::Object,
  };
  const std::uint64_t overlay_signature = [&]() {
    QDR_PROFILE_SCOPE("Viewport selection overlay signature");
    return modeling.selection_overlay_signature(overlay_options);
  }();
  const ViewportPaneOverlayCacheKey key{
      .content_revision = overlay_summary.content_revision,
      .selection_revision = overlay_summary.selection_revision,
      .scene_revision = document.revision(),
      .asset_revision = assets.revision(),
      .overlay_signature = overlay_signature,
      .selection_mode = overlay_summary.mode,
  };
  const ViewportPaneOverlayCacheRefreshReason refresh_reason = [&]() {
    QDR_PROFILE_SCOPE("Viewport selection overlay cache check");
    return selection_overlay_cache_.refresh_reason(key);
  }();
  if (refresh_reason == ViewportPaneOverlayCacheRefreshReason::None) {
    return;
  }

  auto rebuild_cache =
      [&]() {
        ViewportOverlayFrame source_frame = [&]() {
          QDR_PROFILE_SCOPE("Viewport selection overlay source frame");
          return modeling.selection_overlay_frame(overlay_options);
        }();
        selection_overlay_cache_.update(std::move(source_frame), key);
      };

  switch (refresh_reason) {
  case ViewportPaneOverlayCacheRefreshReason::Dirty: {
    QDR_PROFILE_SCOPE("Viewport selection overlay cache refresh dirty");
    rebuild_cache();
    break;
  }
  case ViewportPaneOverlayCacheRefreshReason::Invalid: {
    QDR_PROFILE_SCOPE("Viewport selection overlay cache refresh invalid");
    rebuild_cache();
    break;
  }
  case ViewportPaneOverlayCacheRefreshReason::ContentRevision: {
    QDR_PROFILE_SCOPE(
        "Viewport selection overlay cache refresh content revision");
    rebuild_cache();
    break;
  }
  case ViewportPaneOverlayCacheRefreshReason::SelectionRevision: {
    QDR_PROFILE_SCOPE(
        "Viewport selection overlay cache refresh selection revision");
    rebuild_cache();
    break;
  }
  case ViewportPaneOverlayCacheRefreshReason::SceneRevision: {
    QDR_PROFILE_SCOPE(
        "Viewport selection overlay cache refresh scene revision");
    rebuild_cache();
    break;
  }
  case ViewportPaneOverlayCacheRefreshReason::AssetRevision: {
    QDR_PROFILE_SCOPE(
        "Viewport selection overlay cache refresh asset revision");
    rebuild_cache();
    break;
  }
  case ViewportPaneOverlayCacheRefreshReason::OverlaySignature: {
    QDR_PROFILE_SCOPE(
        "Viewport selection overlay cache refresh overlay signature");
    rebuild_cache();
    break;
  }
  case ViewportPaneOverlayCacheRefreshReason::SelectionMode: {
    QDR_PROFILE_SCOPE(
        "Viewport selection overlay cache refresh selection mode");
    rebuild_cache();
    break;
  }
  case ViewportPaneOverlayCacheRefreshReason::None:
    break;
  }
}

ViewportWorkspaceService& ViewportPanel::workspace() {
    if (workspace_ == nullptr) {
        throw std::logic_error("ViewportPanel requires an attached ViewportWorkspaceService.");
    }
    return *workspace_;
}

const ViewportWorkspaceService& ViewportPanel::workspace() const {
    if (workspace_ == nullptr) {
        throw std::logic_error("ViewportPanel requires an attached ViewportWorkspaceService.");
    }
    return *workspace_;
}

void ViewportPanel::draw_pane_contents(
    Pane &pane_state, SceneDocument &document, AssetRegistry &assets,
    ModelingSessionService &modeling, AssetId default_material,
    QVec2 captured_mouse_delta, bool *viewport_cursor_capture_requested,
    QVec2 *viewport_cursor_capture_point, const UIContext &ui_context,
    ImVec2 item_min, ImVec2 item_size, bool hovered, bool focus_hovered) {
  QDR_PROFILE_FUNCTION();
  ViewportWorkspaceService &viewport_workspace = workspace();
  ViewportCameraController &camera = viewport_workspace.camera(pane_state.id);
  ViewportModelingToolPortAdapter preview_port_adapter(modeling);
  ViewportToolPorts preview_ports = preview_port_adapter.ports();
  const ImVec2 item_max{item_min.x + item_size.x, item_min.y + item_size.y};
  const ViewportFocusState focus_state =
      focus_controller_.resolve(focus_hovered, ui_context, UIPanelId::Viewport);
  auto modal_tool_owns_viewport = [&]() {
    return input_router_.active_tool_owns_viewport() ||
           input_router_.has_active_modal_preview(*preview_ports.preview) ||
           modeling.modal_tool_owns_viewport();
  };

  const bool use_captured_mouse_delta =
      input_router_.fly_cursor_capture_active();
  ViewportInputFrame input;
  {
    QDR_PROFILE_SCOPE("Viewport build input frame");
    input = input_frame_builder_.build({
        .hovered = hovered,
        .focused = focus_state.focused,
        .item_min = item_min,
        .item_size = item_size,
        .route_active_navigation_input = input_router_.navigating(),
        .use_captured_mouse_delta = use_captured_mouse_delta,
        .captured_mouse_delta = captured_mouse_delta,
        .pressed_input_chords = ui_context.viewport->pressed_input_chords,
        .delta_seconds = static_cast<float>(
            std::max(ui_context.viewport->frame_timing.delta_seconds, 0.0)),
    });
  }
  const ModelingToolSettingsTarget tool_settings_target =
      modeling.tool_settings_target();
  input.navigation_active = input_router_.navigating();
  input.fly_navigation_active = input_router_.fly_navigating();
  input.orbit_navigation_active = input_router_.orbit_navigating();
  input.pan_navigation_active = input_router_.pan_navigating();
  input.modal_tool_active = input_router_.has_active_modal_preview(
                                *preview_ports.preview,
                                viewport_grid_snap_active(
                                    active_viewport_settings(),
                                    input.modifiers.primary)) ||
                            modeling.modal_tool_owns_viewport();
  input.active_tool_owns_viewport = input_router_.active_tool_owns_viewport();
  input.operation_preview_active =
      tool_settings_target.operation_preview_active;
  input.grid_preset = viewport_workspace.grid_preset();
  {
    QDR_PROFILE_SCOPE("Viewport resolve keybindings");
    resolve_keybindings_for_frame(ui_context.intent.editor,
                                  input_router_.state(), input,
                                  ui_context.command_shortcut_consumed);
  }
  const ViewportInputResult result = [&]() {
    QDR_PROFILE_SCOPE("Viewport route input frame");
    return input_router_.handle_frame(input, camera);
  }();
  const ViewportShortcutControllerResult operation_preview_shortcut =
      shortcut_controller_.handle_operation_preview_shortcut(
          result, tool_settings_target, ui_context.intent.editor);
  const bool operation_preview_shortcut_consumed =
      operation_preview_shortcut.consumed;
  if (operation_preview_shortcut.invalidate_hover) {
    hover_controller_.invalidate();
  }
  if (operation_preview_shortcut.mark_overlay_dirty) {
    selection_overlay_cache_.mark_dirty();
  }
  if (result.maximized_toggle_requested &&
      !operation_preview_shortcut_consumed) {
    if (hovered) {
      viewport_workspace.focus_pane(pane_state.id);
    }
    viewport_workspace.toggle_focused_pane_maximized();
  }
  if (result.grid_preset_changed) {
    viewport_workspace.set_grid_preset(result.requested_grid_preset);
    input_router_.sync_grid_preset_snapshot(viewport_workspace.grid_preset());
  }
  if (result.view_changed) {
    (void)viewport_workspace.set_view(pane_state.id, result.requested_view);
  }
  if (input.right_pressed && hovered && !input.modifiers.primary &&
      !input.modifiers.shift && !input.modifiers.alt) {
    fly_cursor_capture_screen_ = input.mouse_screen;
  }
  last_fly_navigation_active_ = result.fly_cursor_capture_active;
  if (result.fly_cursor_capture_active) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    if (viewport_cursor_capture_requested != nullptr) {
      *viewport_cursor_capture_requested = true;
    }
    if (viewport_cursor_capture_point != nullptr) {
      const ImGuiViewport *main_viewport = ImGui::GetMainViewport();
      *viewport_cursor_capture_point = {
          fly_cursor_capture_screen_.x - main_viewport->Pos.x,
          fly_cursor_capture_screen_.y - main_viewport->Pos.y,
      };
    }
  }
  const bool modal_tool_owns_viewport_for_menu = modal_tool_owns_viewport();
  if (!modal_tool_owns_viewport_for_menu && input.right_pressed && hovered &&
      !input.modifiers.primary && !input.modifiers.shift &&
      !input.modifiers.alt) {
    context_menu_right_press_active_ = true;
    context_menu_right_press_screen_ = input.mouse_screen;
    context_menu_right_drag_delta_ = {};
  }
  if (context_menu_right_press_active_ && input.right_down &&
      !input.right_pressed) {
    context_menu_right_drag_delta_ =
        context_menu_right_drag_delta_ + input.mouse_delta;
  }
  bool pending_context_menu_click = false;
  QVec2 pending_context_menu_local_mouse = {};
  ViewportContextMenuOpenRequest pending_context_menu_request;
  if (input.right_released && context_menu_right_press_active_) {
    const bool release_inside_viewport = input.mouse_screen.x >= item_min.x &&
                                         input.mouse_screen.x <= item_max.x &&
                                         input.mouse_screen.y >= item_min.y &&
                                         input.mouse_screen.y <= item_max.y;
    pending_context_menu_request = {
        .modal_tool_owns_viewport = modal_tool_owns_viewport(),
        .right_press_tracked = context_menu_right_press_active_,
        .right_released = input.right_released,
        .release_inside_viewport = release_inside_viewport,
        .press_screen = context_menu_right_press_screen_,
        .release_screen = input.mouse_screen,
        .accumulated_drag_delta = context_menu_right_drag_delta_,
    };
    if (is_viewport_context_menu_click_gesture(pending_context_menu_request)) {
      pending_context_menu_click = true;
      pending_context_menu_local_mouse = input.mouse_local;
    }
    context_menu_right_press_active_ = false;
    context_menu_right_drag_delta_ = {};
  }
  if (!input.right_down && !input.right_released) {
    context_menu_right_press_active_ = false;
    context_menu_right_drag_delta_ = {};
  }
  if (result.focus_document_requested) {
    focus_all(document, assets, camera);
  }
  const bool selection_mode_shortcut_handled =
      result.shortcut_handled &&
      result.shortcut.kind == ViewportShortcutKind::SetSelectionMode;
  if (selection_mode_shortcut_handled) {
    const QuaderApiVoidResult command =
        ui_context.intent.editor.selection().set_mode(to_api_selection_mode(
            viewport_selection_mode_to_app(result.shortcut.selection_mode)));
    if (!command.ok) {
      LOG_ERROR("Selection mode change failed: {}", command.message);
    }
    input_router_.set_selection_mode(
        app_selection_mode_to_viewport(to_modeling_selection_mode(
            ui_context.intent.editor.selection().summary().mode)));
    input_router_.clear_modal_drag_state();
    interaction_controller_.reset(&ui_context.intent.editor);
    hover_controller_.invalidate();
    selection_overlay_cache_.mark_dirty();
  }
  if (result.tool_changed) {
    LOG_INFO("Viewport tool changed to {}",
             viewport_tool_id(input_router_.state().active_tool));
    input_router_.clear_modal_drag_state();
    interaction_controller_.reset(&ui_context.intent.editor);
    hover_controller_.invalidate();
    selection_overlay_cache_.mark_dirty();
    const ViewportTool *active_tool = input_router_.active_tool();
    const QuaderApiVoidResult command =
        active_tool != nullptr ? ui_context.intent.editor.tools().activate(
                                     input_router_.state().active_tool)
                               : QuaderApiVoidResult{
                                     .ok = false,
                                     .changed = false,
                                     .message = "No active viewport tool.",
                                 };
    if (!command.ok) {
      LOG_ERROR("Viewport tool activation failed: {}", command.message);
      input_router_.set_active_tool(ViewportToolId::Select);
      (void)ui_context.intent.editor.tools().activate(ViewportToolId::Select);
    } else {
      input_router_.set_selection_mode(
          app_selection_mode_to_viewport(to_modeling_selection_mode(
              ui_context.intent.editor.selection().summary().mode)));
    }
    input_router_.clear_modal_tool_state();
  }

  if (!operation_preview_shortcut_consumed) {
    const ViewportShortcutControllerResult shortcut =
        shortcut_controller_.handle_modeling_shortcut(result,
                                                      ui_context.intent.editor);
    if (shortcut.command_executed) {
      input_router_.set_selection_mode(
          app_selection_mode_to_viewport(to_modeling_selection_mode(
              ui_context.intent.editor.selection().summary().mode)));
    }
    if (shortcut.invalidate_hover) {
      hover_controller_.invalidate();
    }
    if (shortcut.mark_overlay_dirty) {
      selection_overlay_cache_.mark_dirty();
    }
  }
  if (!operation_preview_shortcut_consumed && result.shortcut_handled &&
      result.shortcut.kind == ViewportShortcutKind::DeletePopup) {
    const ModelingSelectionSummary delete_summary =
        to_modeling_selection_summary(
            ui_context.intent.editor.selection().summary());
    if (delete_summary.mode == ModelingSelectionMode::Object ||
        delete_summary.mode == ModelingSelectionMode::Group ||
        scene_aware_delete_has_selected_root(document)) {
      delete_objects_popup_open_ = true;
      delete_objects_popup_position_pending_ = true;
      delete_objects_popup_position_ = ImVec2(item_min.x + item_size.x * 0.5f,
                                              item_min.y + item_size.y * 0.25f);
    } else {
      ImGui::OpenPopup("ViewportDeleteElementsPopup");
    }
  }

  auto cancel_active_modal_tool = [&](bool exit_tool) {
    if (input_router_.state().active_tool == ViewportToolId::Pivot) {
      const QuaderApiVoidResult cancel =
          ui_context.intent.editor.tools().cancel_active_tool_preview();
      if (!cancel.ok) {
        LOG_ERROR("Pivot cancel failed: {}",
                  cancel.message.empty() ? "Command failed."
                                         : cancel.message);
      }
      pivot_handle_dragging_ = false;
      sync_active_tool_from_editor(ui_context.intent.editor, modeling);
      interaction_controller_.reset(&ui_context.intent.editor);
      hover_controller_.invalidate();
      selection_overlay_cache_.mark_dirty();
      (void)exit_tool;
      return;
    }
    ViewportModelingToolPortCallbacks cancel_callbacks;
    cancel_callbacks.invalidate_hover = [&]() { hover_controller_.invalidate(); };
    cancel_callbacks.mark_overlay_dirty = [&]() {
      selection_overlay_cache_.mark_dirty();
    };
    ViewportModelingToolPortAdapter cancel_ports(modeling,
                                                 std::move(cancel_callbacks));
    ViewportToolContext tool_context{
        .state = const_cast<ViewportToolState &>(input_router_.state()),
        .camera = camera,
        .ports = cancel_ports.ports(),
    };
    tool_context.set_active_tool = [&](ViewportToolId tool) {
      input_router_.set_active_tool(tool);
    };
    (void)input_router_.handle_active_tool_event(
        tool_context, ViewportToolEvent{
                          .kind = ViewportToolEventKind::Cancel,
                          .tool = input_router_.state().active_tool,
                          .exit_active_modal_tool = exit_tool,
                      });
    interaction_controller_.reset(&ui_context.intent.editor);
    hover_controller_.invalidate();
    selection_overlay_cache_.mark_dirty();
  };

  bool cancel_consumed = false;
  if (!operation_preview_shortcut_consumed && result.cancel_requested) {
    if (interaction_controller_.dragging()) {
      (void)interaction_controller_.cancel_active_drag(
          ui_context.intent.editor);
      selection_overlay_cache_.mark_dirty();
      cancel_consumed = true;
    }

    if (modal_tool_owns_viewport()) {
      cancel_active_modal_tool(true);
      cancel_consumed = true;
    }
  }

  const bool suppress_hover_work =
      result.navigation_active || result.camera_changed;
  const bool navigation_gizmo_consumed = [&]() {
    QDR_PROFILE_SCOPE("Viewport draw navigation gizmo");
    return navigation_gizmo_.draw(
        camera, pane_state.id, item_min, item_size, captured_mouse_delta,
        viewport_cursor_capture_requested, viewport_cursor_capture_point);
  }();
  if (suppress_hover_work || navigation_gizmo_consumed ||
      result.grid_preset_changed) {
    hover_controller_.invalidate();
  }
  const ImGuiIO &io = ImGui::GetIO();
  const bool left_clicked =
      hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
  const bool left_down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
  const bool left_released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
  const ImVec2 left_click_screen = io.MouseClickedPos[ImGuiMouseButton_Left];
  const QVec2 left_click_local = left_clicked
                                     ? QVec2{left_click_screen.x - item_min.x,
                                             left_click_screen.y - item_min.y}
                                     : input.mouse_local;
  const int left_click_count =
      std::max(1, ImGui::GetMouseClickedCount(ImGuiMouseButton_Left));
  const bool point_light_gizmos_visible =
      point_light_gizmos_visible_for_pane(
          active_viewport_settings().bool_value(ViewportBoolSettingId::ShowGizmos),
          viewport_workspace.pane(pane_state.id).shading);
  const std::vector<ViewportSceneBillboardPickCandidate>
      scene_billboard_pick_candidates =
          point_light_gizmos_visible
              ? point_light_billboard_pick_candidates(document)
              : std::vector<ViewportSceneBillboardPickCandidate>{};
  const ViewportInteractionFrame interaction_frame{
      .input_router = input_router_,
      .camera = camera,
      .settings = active_viewport_settings(),
      .input = input,
      .input_result = result,
      .left_click_local = left_click_local,
      .left_click_count = left_click_count,
      .left_pressed = left_clicked,
      .left_down = left_down,
      .left_released = left_released,
      .scene_billboard_pick_candidates = scene_billboard_pick_candidates,
  };
  PivotToolService *pivot_service =
      ui_context.viewport != nullptr ? ui_context.viewport->pivot : nullptr;
  bool pivot_consumed = false;
  if (pivot_service != nullptr) {
    if (input.quick_pivot_released) {
      pivot_service->end_quick_edit();
      pivot_handle_dragging_ = false;
      pivot_consumed = true;
    }
    if (input.quick_pivot_down && !pivot_service->quick_edit_active()) {
      if (const std::optional<QVec3> initial =
              ui_context.intent.editor.query().selection_transform_pivot()) {
        pivot_service->begin_quick(*initial);
        pivot_consumed = true;
      }
    }
    if (pivot_service->persistent_active() &&
        input.modal_tool_finalize_pressed) {
      const QuaderApiVoidResult apply =
          ui_context.intent.editor.tools().commit_active_tool_preview();
      if (!apply.ok) {
        LOG_ERROR("Pivot apply failed: {}",
                  apply.message.empty() ? "Command failed." : apply.message);
      }
      pivot_handle_dragging_ = false;
      sync_active_tool_from_editor(ui_context.intent.editor, modeling);
      hover_controller_.invalidate();
      selection_overlay_cache_.mark_dirty();
      pivot_consumed = true;
    }

    const PivotHandleState handle = pivot_service->handle_state();
    const bool pivot_can_interact =
        handle.visible && hovered && !result.navigation_active &&
        !input.text_input_active && !navigation_gizmo_consumed;
    if (pivot_can_interact && left_clicked &&
        pivot_handle_hit(camera, handle, left_click_local)) {
      pivot_handle_dragging_ = true;
      interaction_pane_id_ = pane_state.id;
      pivot_consumed = true;
    }
    if (pivot_handle_dragging_) {
      pivot_consumed = true;
      if (left_down && handle.visible) {
        PivotSnapSettings snap_settings =
            handle.persistent ? pivot_service->snap_settings()
                              : PivotSnapSettings{
                                    .vertex = true,
                                    .edge_midpoint = false,
                                    .face_center = false,
                                    .grid = true,
                                };
        const QVec3 position = resolved_pivot_drag_position(
            camera, input.mouse_local, handle.world_position, document, assets,
            snap_settings, grid_size_for_preset(input.grid_preset));
        if (handle.persistent) {
          pivot_service->set_persistent_preview_position(position);
        } else {
          pivot_service->set_quick_preview_position(position);
        }
        selection_overlay_cache_.mark_dirty();
      }
      if (left_released || !left_down || !handle.visible) {
        pivot_handle_dragging_ = false;
      }
    }
  }
  const PivotHandleState pivot_handle_after_input =
      pivot_service != nullptr ? pivot_service->handle_state()
                               : PivotHandleState{};
  const bool pivot_blocks_gizmo =
      pivot_handle_after_input.visible && pivot_handle_after_input.editing;
  const ViewportBeforeToolInteractionResult before_tool = [&]() {
    QDR_PROFILE_SCOPE("Viewport before tool interaction");
    return (cancel_consumed || pivot_blocks_gizmo || pivot_consumed)
               ? ViewportBeforeToolInteractionResult{.gizmo_consumed = true}
               : interaction_controller_.handle_before_active_tool(
                     ui_context.intent.editor, interaction_frame);
  }();
  if (interaction_controller_.dragging() || before_tool.gizmo_consumed) {
    interaction_pane_id_ = pane_state.id;
  }
  const bool gizmo_consumed = cancel_consumed || navigation_gizmo_consumed ||
                              pivot_consumed || before_tool.gizmo_consumed;
  const ModelingToolStatus release_tool_status = modeling.tool_status();
  const bool insert_edge_loop_drag_release =
      release_tool_status.insert_edge_loop_dragging && left_released;
  const bool drawn_plane_drag_release = (release_tool_status.cut_dragging ||
                                         release_tool_status.mirror_dragging) &&
                                        left_released;

  const bool mouse_clicked_outside_viewport =
      modal_tool_owns_viewport() && !io.WantCaptureMouse &&
      !insert_edge_loop_drag_release && !drawn_plane_drag_release && !hovered &&
      !result.navigation_active &&
      ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) &&
      (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
       ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
       ImGui::IsMouseClicked(ImGuiMouseButton_Middle) ||
       ImGui::IsMouseReleased(ImGuiMouseButton_Left) ||
       ImGui::IsMouseReleased(ImGuiMouseButton_Right) ||
       ImGui::IsMouseReleased(ImGuiMouseButton_Middle));
  if (mouse_clicked_outside_viewport) {
    cancel_active_modal_tool(true);
    cancel_consumed = true;
  }

  const ModelingSelectionSummary hover_selection_summary =
      to_modeling_selection_summary(
          ui_context.intent.editor.selection().summary());
  auto make_viewport_hover_work_key = [&](bool consumed_by_gizmo) {
    const ViewportSettings &settings = active_viewport_settings();
    return hover_controller_.make_key(
        camera,
        ViewportHoverWorkRequest{
            .pane_id = pane_state.id,
            .active_tool = input_router_.state().active_tool,
            .selection_mode = input_router_.state().selection_mode,
            .box_drag_state =
                input_router_
                    .box_preview(
                        *preview_ports.preview,
                        viewport_grid_snap_active(settings,
                                                  input.modifiers.primary))
                    .drag_state,
            .grid_preset = input_router_.state().grid_preset,
            .mouse_local = input.mouse_local,
            .viewport_size = input.viewport_size,
            .modifiers = input.modifiers,
            .gizmo_consumed = consumed_by_gizmo,
            .vertex_radius =
                settings.float_value(ViewportFloatSettingId::PickVertexRadius),
            .edge_radius =
                settings.float_value(ViewportFloatSettingId::PickEdgeRadius),
            .scene_revision = document.revision(),
            .asset_revision = assets.revision(),
            .content_revision = hover_selection_summary.content_revision,
            .selection_revision = hover_selection_summary.selection_revision,
        });
  };
  auto make_modeling_tool_event_at = [&](QVec2 mouse_local,
                                         bool dragging = false) {
    const ViewportSettings &settings = active_viewport_settings();
    const ViewportRay world_ray = camera.ray_from_viewport_point(mouse_local);
    return ModelingToolRayEvent{
        .screen_position = mouse_local,
        .ray =
            {
                .origin = world_ray.origin,
                .direction = world_ray.direction,
            },
        .camera = ViewportCameraAdapter::modeling_camera(
            camera, {item_size.x, item_size.y}),
        .grid_size = std::max(static_cast<double>(grid_size_for_preset(
                                  input_router_.state().grid_preset)),
                              0.0001),
        .vertex_radius =
            settings.float_value(ViewportFloatSettingId::PickVertexRadius),
        .edge_radius =
            settings.float_value(ViewportFloatSettingId::PickEdgeRadius),
        .snap_enabled =
            viewport_grid_snap_active(settings, input.modifiers.primary),
        .constrain_axis = input.modifiers.shift,
        .dragging = dragging,
    };
  };
  auto claim_tool_hover = [&](ModelingBoxDragState) {
    const ViewportHoverWorkKey hover_work_key =
        make_viewport_hover_work_key(gizmo_consumed);
    return hover_controller_.claim(hover_work_key);
  };
  auto handle_tool_command_result = [&](std::string_view label,
                                        const ModelingCommandResult &command) {
    if (!command.ok) {
      LOG_ERROR("{} failed: {}", label,
                command.message.empty() ? "Command failed." : command.message);
    }
    if (command.changed) {
      hover_controller_.invalidate();
      selection_overlay_cache_.mark_dirty();
    }
  };
  const bool finalize_pressed = input.modal_tool_finalize_pressed;
  const bool mode_cycle_pressed = input.modal_tool_mode_cycle_pressed;

  ViewportBoxToolContext box_tool_context{
      .default_material = default_material,
  };
  box_tool_context.make_event = [&]() {
    const float grid_size =
        grid_size_for_preset(input_router_.state().grid_preset);
    return modeling_box_event(
        camera, input, grid_size,
        viewport_grid_snap_active(active_viewport_settings(),
                                  input.modifiers.primary));
  };
  box_tool_context.commit_preview =
      [&](std::string name, AssetId material) {
        const QuaderApiResult<QuaderModelingObjectRef> result =
            ui_context.intent.editor.modeling().commit_box_preview(
                std::move(name), material);
        ModelingCreateBoxResult committed{
                .ok = result.ok,
                .changed = result.changed,
                .message = result.message,
        };
        if (result.ok && result.changed) {
          committed.object = {
              .modeling_object_id = result.value.modeling_object_id,
              .entity = result.value.entity,
              .mesh = result.value.mesh,
          };
        }
        return committed;
      };
  box_tool_context.next_mesh_name = [&]() {
    return SceneEntityNaming::next_name_for_kind(document,
                                                 SceneEntityKind::Mesh);
  };

  ViewportPointToolContext point_tool_context;
  point_tool_context.make_event_at = make_modeling_tool_event_at;
  point_tool_context.hit_drawn_plane_captured_point =
      [&](const std::array<QVec3, 3> &points, int captured_point_count) {
        return hit_drawn_plane_captured_point(points, captured_point_count,
                                              left_click_local, camera,
                                              item_min, item_size);
      };
  point_tool_context.hit_drawn_plane_line =
      [&](const std::array<QVec3, 3> &points, int captured_point_count,
          bool has_line) {
        return hit_drawn_plane_line(points, captured_point_count, has_line,
                                    left_click_local, camera, item_min,
                                    item_size);
      };

  ViewportModelingToolPortCallbacks tool_port_callbacks;
  tool_port_callbacks.claim_hover = claim_tool_hover;
  tool_port_callbacks.invalidate_hover = [&]() { hover_controller_.invalidate(); };
  tool_port_callbacks.mark_overlay_dirty = [&]() {
    selection_overlay_cache_.mark_dirty();
  };
  tool_port_callbacks.set_selection_mode = [&](ViewportSelectionMode mode) {
    input_router_.set_selection_mode(mode);
  };
  tool_port_callbacks.set_app_selection_mode = [&](ModelingSelectionMode mode) {
    const QuaderApiVoidResult command =
        ui_context.intent.editor.selection().set_mode(
            to_api_selection_mode(mode));
    if (!command.ok) {
      LOG_ERROR("Selection mode change failed: {}",
                command.message.empty() ? "Command failed." : command.message);
    }
    input_router_.set_selection_mode(
        app_selection_mode_to_viewport(to_modeling_selection_mode(
            ui_context.intent.editor.selection().summary().mode)));
  };
  tool_port_callbacks.handle_tool_command_result = handle_tool_command_result;
  ViewportModelingToolPortAdapter tool_ports(modeling,
                                             std::move(tool_port_callbacks));
  ViewportToolContext tool_context{
      .state = const_cast<ViewportToolState &>(input_router_.state()),
      .camera = camera,
      .ports = tool_ports.ports(),
      .document = &document,
      .settings = &active_viewport_settings(),
      .box = &box_tool_context,
      .point = &point_tool_context,
  };
  tool_context.set_active_tool = [&](ViewportToolId tool) {
    input_router_.set_active_tool(tool);
  };

  const bool can_interact_with_active_tool =
      hovered && !gizmo_consumed && !result.navigation_active;
  const ViewportToolResult active_tool_frame_result = [&]() {
    QDR_PROFILE_SCOPE("Viewport active tool frame");
    return input_router_.handle_active_tool_event(
        tool_context, ViewportToolEvent{
                          .kind = ViewportToolEventKind::Frame,
                          .tool = input_router_.state().active_tool,
                          .pointer =
                              {
                                  .hovered = hovered,
                                  .left_clicked = left_clicked,
                                  .left_down = left_down,
                                  .left_released = left_released,
                                  .can_interact = can_interact_with_active_tool,
                                  .finalize_pressed = finalize_pressed,
                                  .mode_cycle_pressed = mode_cycle_pressed,
                                  .suppress_hover_work = suppress_hover_work,
                                  .gizmo_consumed = gizmo_consumed,
                                  .navigation_active = result.navigation_active,
                                  .text_input_active = input.text_input_active,
                                  .mouse_local = input.mouse_local,
                                  .left_click_local = left_click_local,
                                  .viewport_size = input.viewport_size,
                                  .modifiers = input.modifiers,
                              },
                      });
  }();
  const ModelingBoxPreview box_preview = input_router_.box_preview(
      *preview_ports.preview,
      viewport_grid_snap_active(active_viewport_settings(),
                                input.modifiers.primary));
  if (box_preview.drag_state != ModelingBoxDragState::Height) {
    modal_tool_overlay_renderer_.draw_box_preview(
        ImGui::GetWindowDrawList(), box_preview, camera, item_min, item_size);
  }

  {
    QDR_PROFILE_SCOPE("Viewport after tool interaction");
    interaction_controller_.handle_after_active_tool(
        ui_context.intent.editor, interaction_frame, active_tool_frame_result,
        before_tool);
  }

  const bool selection_fallback_active =
      input_router_.active_tool_allows_selection_fallback();
  if (!active_tool_frame_result.handled && selection_fallback_active &&
      pending_context_menu_click && !gizmo_consumed) {
    ViewportInputFrame context_input = input;
    context_input.mouse_local = pending_context_menu_local_mouse;
    const ViewportInteractionFrame context_frame{
        .input_router = input_router_,
        .camera = camera,
        .settings = active_viewport_settings(),
        .input = context_input,
        .input_result = result,
        .scene_billboard_pick_candidates = scene_billboard_pick_candidates,
    };
    const QuaderSelectionSummary summary =
        interaction_controller_.pick_context_menu_target(
            ui_context.intent.editor, context_frame);
    const ModelingActionQuery query{
        .surface = ModelingActionSurface::ViewportContextMenu,
        .modal_tool_owns_viewport = modal_tool_owns_viewport(),
        .include_disabled = true,
        .include_hidden = false,
    };
    const std::vector<ModelingActionItem> actions =
        summary.has_selection ? modeling.context_actions(query)
                              : std::vector<ModelingActionItem>{};
    pending_context_menu_request.target_hit = summary.has_selection;
    pending_context_menu_request.has_actions = !actions.empty();
    if (should_open_viewport_context_menu(pending_context_menu_request)) {
      ImGui::OpenPopup("ViewportOperationsContext");
    }
    hover_controller_.invalidate();
    selection_overlay_cache_.mark_dirty();
  }

  {
    QDR_PROFILE_SCOPE("Viewport refresh selection overlay");
    refresh_cached_selection_overlay_frame(modeling, document, assets);
  }
  modal_tool_overlay_renderer_.draw_fly_navigation_overlay(
      ImGui::GetWindowDrawList(), item_min, item_size, result.fly_hud_active,
      result.fly_hud_active && !camera.fly_speed_is_default(),
      result.fly_hud_active, camera.fly_speed(), ui_context.localization);
  context_menu_presenter_.draw_operations_popup(
      ui_context.intent.editor, modeling, input_router_, ui_context.localization,
      modal_tool_owns_viewport());
  if (draw_viewport_delete_popup(
          delete_objects_popup_open_, delete_objects_popup_position_pending_,
          delete_objects_popup_position_, ui_context.intent.editor,
          input_router_, ui_context.localization)) {
    hover_controller_.invalidate();
    selection_overlay_cache_.mark_dirty();
  }
}

} // namespace quader
