////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"
#include "api/quader_command_ids.hpp"
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_polygon_document_internal_access.hpp"
#include "assets/default_material_catalog.hpp"
#include "assets/curve_asset_payload.hpp"
#include "assets/mesh_asset.hpp"
#include "editor/array/path_array_tool_service.hpp"
#include "editor/curve/curve_tool_service.hpp"
#include "editor/modeling/modeling_viewport_camera_adapter.hpp"
#include "editor/modeling/runtime/tool_constants.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/input/viewport_keybinding_resolver.hpp"
#include "editor/input/quader_input_binding.hpp"
#include "editor/interactions/viewport_projection.hpp"
#include "editor/interactions/transform_gizmo.hpp"
#include "editor/pivot/pivot_tool_service.hpp"
#include "editor/placement/scene_surface_query.hpp"
#include "editor/viewport/viewport_tool_overlay_projection.hpp"
#include "editor/tools/viewport_tool.hpp"
#include "editor/tools/viewport_tool_ports.hpp"
#include "editor/viewport/viewport_interaction_controller.hpp"
#include "editor/viewport/viewport_curve_picking.hpp"
#include "editor/viewport/viewport_scene_projection.hpp"
#include "editor/viewport/viewport_selection_interaction.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "render/scene_render_extraction.hpp"
#include "render/scene_render_light_refresh.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/session/viewport_local_visibility_service.hpp"
#include "editor/session/project_session.hpp"
#include "scene/scene_bounds.hpp"
#include "scene/components.hpp"
#include "scene/scene_transform.hpp"
#include "support/viewport_overlay_test_helpers.hpp"
#include "ui/viewport_hover_controller.hpp"
#include "viewport/viewport_host_interaction_policy.hpp"
#include "viewport/viewport_camera_adapter.hpp"
#include "viewport/viewport_camera_controller.hpp"
#include "viewport/viewport_input_router.hpp"
#include "viewport/viewport_pixel_scale.hpp"
#include "viewport/viewport_settings.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

/// Owns a complete viewport interaction harness for one synthetic controller scenario.
struct ViewportInteractionHarness {
    quader::SceneSession session;
    quader::QuaderEditorApi editor;
    quader::ViewportInputRouter router;
    quader::ViewportCameraController camera;
    quader::ViewportSettings settings;
    quader::ViewportInteractionController controller;
    quader::ViewportInputFrame input;
    quader::ViewportInputResult input_result;
    double monotonic_time_seconds = 0.0;
    std::vector<quader::ViewportSceneBillboardPickCandidate>
        billboard_pick_candidates;
    quader::ViewportCurvePickCache curve_pick_cache;
    std::deque<quader::SceneSurfaceQuerySnapshot> scene_surface_snapshots;

    ViewportInteractionHarness()
        : editor(quader::QuaderApiSessionInternalAccess::from_scene_session(
              session)) {
      camera.set_viewport_size(800.0f, 600.0f);
      camera.focus_origin(8.0f);
      camera.set_view_direction({0.0f, 0.0f, -1.0f});

      input.viewport_hovered = true;
      input.viewport_focused = true;
      input.viewport_size = {800.0f, 600.0f};
      input.mouse_screen = {400.0f, 300.0f};
      input.mouse_local = {400.0f, 300.0f};
    }

    [[nodiscard]] quader::ViewportInteractionFrame
    frame(bool left_pressed = false, bool left_down = false,
          bool left_released = false, quader::QVec2 left_click_local = {},
          bool right_pressed = false, bool right_down = false,
          bool right_released = false, quader::QVec2 right_click_local = {}) {
      scene_surface_snapshots.push_back(
          session.scene_surface_query().snapshot());
      return {
          .input_router = router,
          .camera = camera,
          .settings = settings,
          .input = input,
          .input_result = input_result,
          .left_click_local = left_click_local,
          .left_click_count = 1,
          .left_pressed = left_pressed,
          .left_down = left_down,
          .left_released = left_released,
          .right_click_local = right_click_local,
          .right_click_count = 1,
          .right_pressed = right_pressed,
          .right_down = right_down,
          .right_released = right_released,
          .monotonic_time_seconds = monotonic_time_seconds,
          .active_tool_allows_selection_fallback =
              controller.active_tool_allows_selection_fallback(
                  router.state().active_tool),
          .scene_billboard_pick_candidates = billboard_pick_candidates,
          .scene_curve_pick_candidates = curve_pick_cache.candidates(
              session.scene(), session.assets(),
              session.viewport_workspace().local_visibility_snapshot()),
          .scene_surfaces = &scene_surface_snapshots.back(),
          .selection_state =
              {
                  .summary = editor.selection().summary(),
                  .scene_revision = session.scene().revision(),
                  .asset_revision = session.assets().revision(),
                  .pivot_revision =
                      session.pivot().selection_pivot_revision(),
              },
      };
    }

    void click_selection(quader::QVec2 hit, int click_count = 1,
                         bool cycle_depth = false,
                         quader::ViewportModifiers cycle_modifiers = {}) {
      input.mouse_local = hit;
      input.mouse_screen = hit;
      input.cycle_selection_depth_pressed = cycle_depth;
      input.cycle_selection_depth_modifiers = cycle_modifiers;
      quader::ViewportInteractionFrame press = frame(true, true, false, hit);
      press.left_click_count = click_count;
      const quader::ViewportBeforeToolInteractionResult before_press =
          controller.handle_before_active_tool(editor, press);
      controller.handle_after_active_tool(editor, press, {}, before_press);

      // Native double-click intent is an edge on press, not a held flag.
      input.cycle_selection_depth_pressed = false;
      input.cycle_selection_depth_modifiers = {};
      quader::ViewportInteractionFrame release = frame(false, false, true, hit);
      release.left_click_count = click_count;
      const quader::ViewportBeforeToolInteractionResult before_release =
          controller.handle_before_active_tool(editor, release);
      controller.handle_after_active_tool(editor, release, {}, before_release);
    }

    quader::ViewportBeforeToolInteractionResult
    dispatch_frame(const quader::ViewportInteractionFrame &frame) {
      const auto before = controller.handle_before_active_tool(editor, frame);
      controller.handle_after_active_tool(editor, frame, {}, before);
      return before;
    }
};

constexpr std::string_view kGestureProjectMaterialId =
    "903120c7-2f1c-46c8-9c69-1ebc41624eae";

/// Owns a disposable real project for viewport material gesture scenarios.
class ProjectViewportInteractionHarness final {
private:
  std::filesystem::path root_;

public:
  ProjectViewportInteractionHarness()
      : root_(std::filesystem::temp_directory_path() /
              ("quader-viewport-material-" +
               std::to_string(static_cast<std::uint64_t>(
                   std::chrono::steady_clock::now()
                       .time_since_epoch()
                       .count())))),
        editor(project) {
    std::error_code error;
    std::filesystem::create_directories(root_, error);
    EXPECT_FALSE(error) << error.message();
    project.set_resource_root(std::filesystem::path(QUADER_SOURCE_DIR) /
                              "resources" / "modeling_assets");
    camera.set_viewport_size(800.0f, 600.0f);
    camera.focus_origin(8.0f);
    camera.set_view_direction({0.0f, 0.0f, -1.0f});
    input.viewport_hovered = true;
    input.viewport_focused = true;
    input.viewport_size = {800.0f, 600.0f};
    input.mouse_screen = {400.0f, 300.0f};
    input.mouse_local = input.mouse_screen;
  }

  ~ProjectViewportInteractionHarness() {
    (void)project.close();
    std::error_code ignored;
    std::filesystem::remove_all(root_, ignored);
  }

  void initialize_with_second_material() {
    const quader::QuaderApiVoidResult created = editor.project().create(root_);
    ASSERT_TRUE(created.ok()) << created.error_message();
    const std::filesystem::path manifest_path =
        editor.project().manifest_path();
    ASSERT_TRUE(editor.project().close().ok());

    const std::filesystem::path default_qmat =
        root_ / std::filesystem::path(
                    std::string(quader::kDefaultProjectMaterialPath));
    std::ifstream input_stream(default_qmat, std::ios::binary);
    ASSERT_TRUE(input_stream.is_open()) << default_qmat.string();
    std::string qmat((std::istreambuf_iterator<char>(input_stream)),
                     std::istreambuf_iterator<char>());
    replace_all(qmat, quader::kDefaultProjectMaterialId,
                kGestureProjectMaterialId);
    replace_all(qmat, quader::kDefaultProjectMaterialName,
                "Viewport Gesture Material");
    const std::filesystem::path second_qmat =
        root_ / "Materials" / "viewport_gesture_material.qmat";
    std::ofstream output_stream(second_qmat,
                                std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output_stream.is_open()) << second_qmat.string();
    output_stream.write(qmat.data(),
                        static_cast<std::streamsize>(qmat.size()));
    output_stream.close();
    ASSERT_TRUE(output_stream.good());

    ASSERT_TRUE(editor.project().open(manifest_path).ok());
    if (!editor.scene_documents().has_active_scene()) {
      ASSERT_TRUE(editor.scene_documents().new_scene().ok());
    }
    ASSERT_NE(project.active_scene(), nullptr);
    ASSERT_NE(project.active_scene()->session, nullptr);
  }

  [[nodiscard]] quader::SceneSession &scene_session() {
    return *project.active_scene()->session;
  }

  [[nodiscard]] quader::ProjectAssetId second_material() const {
    return quader::ProjectAssetId::parse(kGestureProjectMaterialId).value();
  }

  [[nodiscard]] quader::ViewportInteractionFrame frame(
      quader::QVec2 right_click_local = {400.0f, 300.0f}) {
    quader::SceneSession &scene = scene_session();
    return {
        .input_router = router,
        .camera = camera,
        .settings = settings,
        .input = input,
        .input_result = input_result,
        .right_click_local = right_click_local,
        .double_click_interval_seconds = 0.3,
        .monotonic_time_seconds = monotonic_time_seconds,
        .active_tool_allows_selection_fallback =
            controller.active_tool_allows_selection_fallback(
                router.state().active_tool),
        .selection_state =
            {
                .summary = editor.selection().summary(),
                .scene_revision = scene.scene().revision(),
                .asset_revision = scene.assets().revision(),
                .pivot_revision = scene.pivot().selection_pivot_revision(),
            },
    };
  }

  [[nodiscard]] quader::ViewportInteractionFrame left_frame(
      quader::QVec2 click_local, bool pressed, bool down, bool released) {
    quader::ViewportInteractionFrame result = frame();
    result.left_click_local = click_local;
    result.left_pressed = pressed;
    result.left_down = down;
    result.left_released = released;
    return result;
  }

  quader::ViewportBeforeToolInteractionResult dispatch_frame(
      const quader::ViewportInteractionFrame &interaction_frame) {
    const auto before =
        controller.handle_before_active_tool(editor, interaction_frame);
    controller.handle_after_active_tool(editor, interaction_frame, {}, before);
    return before;
  }

  quader::ProjectSession project;
  quader::QuaderEditorApi editor;
  quader::ViewportInputRouter router;
  quader::ViewportCameraController camera;
  quader::ViewportSettings settings;
  quader::ViewportInteractionController controller;
  quader::ViewportInputFrame input;
  quader::ViewportInputResult input_result;
  double monotonic_time_seconds = 0.0;

private:
  static void replace_all(std::string &text, std::string_view from,
                          std::string_view to) {
    std::size_t position = 0;
    while ((position = text.find(from, position)) != std::string::npos) {
      text.replace(position, from.size(), to);
      position += to.size();
    }
  }

};

/// Backs the selection interaction's narrow hover port with the same work key
/// controller and live selection revisions used by the Qt viewport host.
struct ViewportSelectionWorkGateHarness {
  ViewportInteractionHarness &harness;
  quader::ViewportHoverController controller;
  std::uint64_t local_visibility_revision = 0;
  float viewport_pixel_scale = 1.0f;
  quader::ViewportModelingToolPortAdapter adapter;

  explicit ViewportSelectionWorkGateHarness(
      ViewportInteractionHarness &interaction_harness)
      : harness(interaction_harness),
        adapter(harness.session.modeling(), callbacks()) {}

  [[nodiscard]] quader::ViewportToolHoverPort *hover_port() {
    return &adapter;
  }

private:
  [[nodiscard]] quader::ViewportModelingToolPortCallbacks callbacks() {
    quader::ViewportModelingToolPortCallbacks result;
    result.claim_hover = [this](quader::ModelingBoxDragState drag_state) {
      const quader::QuaderSelectionSummary selection =
          harness.editor.selection().summary();
      const quader::ViewportSettings &settings = harness.settings;
      const double vertex_radius = settings.float_value(
          quader::ViewportFloatSettingId::PickVertexRadius);
      const double edge_radius = settings.float_value(
          quader::ViewportFloatSettingId::PickEdgeRadius);
      const quader::ViewportHoverWorkKey key = controller.make_key(
          harness.camera,
          quader::ViewportHoverWorkRequest{
              .active_tool = harness.router.state().active_tool,
              .selection_mode = harness.router.state().selection_mode,
              .box_drag_state = drag_state,
              .grid_preset = harness.router.state().grid_preset,
              .grid_snap_enabled = true,
              .mouse_local = harness.input.mouse_local,
              .viewport_size = harness.input.viewport_size,
              .modifiers = harness.input.modifiers,
              .viewport_pixel_scale = viewport_pixel_scale,
              .vertex_radius = vertex_radius,
              .edge_radius = edge_radius,
              .vertex_world_radius = quader::viewport_scaled_world_pick_radius(
                  vertex_radius,
                  viewport_pixel_scale, harness.input.device_pixel_ratio),
              .edge_world_radius = quader::viewport_scaled_world_pick_radius(
                  edge_radius,
                  viewport_pixel_scale, harness.input.device_pixel_ratio),
              .select_backfaces = settings.bool_value(
                  quader::ViewportBoolSettingId::BackfaceSelection),
              .scene_revision = harness.session.scene().revision(),
              .asset_revision = harness.session.assets().revision(),
              .local_visibility_revision = local_visibility_revision,
              .content_revision = selection.content_revision,
              .selection_revision = selection.selection_revision,
          });
      return controller.claim(key);
    };
    result.invalidate_hover = [this]() { controller.invalidate(); };
    return result;
  }
};

quader::QuaderModelingObjectRef
create_unit_box(ViewportInteractionHarness &harness) {
  const quader::QuaderApiResult<quader::QuaderModelingObjectRef> box =
      harness.editor.modeling().create_box(
          quader::QuaderBounds{
              .min = {-1.0f, -1.0f, -1.0f},
              .max = {1.0f, 1.0f, 1.0f},
          },
          "Viewport Interaction Box");
  EXPECT_TRUE(box.ok) << box.message;
  return box.value();
}

/// Exercises native picking against a capped cylinder's authored edge IDs.
struct CylinderEdgeSelectionScenario final {
  using EdgeId = std::pair<quader::QuaderElementId, quader::QuaderElementId>;

  ViewportInteractionHarness harness;
  quader_poly::Document document;
  quader::QuaderModelingObjectRef object;

  void initialize(int side_count) {
    document = quader_poly::make_cylinder({.side_count = side_count});
    const auto created = harness.editor.modeling().create_mesh(
        quader::QuaderPolygonDocumentInternalAccess::from_backend(document),
        {.name = "Viewport Cylinder Edge Selection"});
    ASSERT_TRUE(created.ok()) << created.error_message();
    object = created.value();
    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Edge)
                    .ok());
    ASSERT_TRUE(harness.editor.selection().clear().ok());
    harness.router.set_selection_mode(quader::ViewportSelectionMode::Edge);
    harness.camera.set_view_direction({-0.15f, -0.4f, -1.0f});
    harness.session.history().clear();
  }

  static EdgeId edge_id(quader::QuaderElementId a,
                        quader::QuaderElementId b) {
    return a < b ? EdgeId{a, b} : EdgeId{b, a};
  }

  static std::vector<EdgeId> perimeter(const quader_poly::Face &face) {
    std::vector<EdgeId> edges;
    for (std::size_t index = 0; index < face.vertices.size(); ++index) {
      edges.push_back(edge_id(face.vertices[index],
                             face.vertices[(index + 1u) % face.vertices.size()]));
    }
    std::sort(edges.begin(), edges.end());
    return edges;
  }

  [[nodiscard]] quader::QVec2 midpoint(quader::QuaderElementId a,
                                      quader::QuaderElementId b) const {
    const auto vertex_position = [&](quader::QuaderElementId id) {
      const auto vertex = std::find_if(
          document.vertices.begin(), document.vertices.end(),
          [&](const auto &candidate) { return candidate.id == id; });
      EXPECT_NE(vertex, document.vertices.end());
      return vertex == document.vertices.end() ? quader::QVec3{}
                                               : vertex->position;
    };
    const auto projected = harness.camera.project_world_point(
        (vertex_position(a) + vertex_position(b)) * 0.5f);
    EXPECT_TRUE(projected.visible);
    return projected.position;
  }

  [[nodiscard]] std::vector<EdgeId> selected_edges() const {
    const auto selection = harness.session.modeling().export_selection();
    std::vector<EdgeId> edges;
    for (const auto &selected_object : selection.objects) {
      if (selected_object.modeling_object_id != object.modeling_object_id) {
        continue;
      }
      for (const auto &edge : selected_object.selected_edges) {
        edges.push_back(edge_id(edge.a, edge.b));
      }
    }
    std::sort(edges.begin(), edges.end());
    return edges;
  }

  void expect_edge_hover_feedback(EdgeId edge, bool remove) const {
    const quader::ViewportOverlayFrame overlay =
        quader::test_support::visible_modeling_overlay_frame(
            harness.session.modeling());
    EXPECT_EQ(overlay.hover_target_selected, remove);
    const auto hovered = std::find_if(
        overlay.lines.begin(), overlay.lines.end(),
        [&](const quader::ViewportOverlayLine &line) {
          return line.layer == quader::ViewportOverlayLayer::HoverEdge &&
                 line.element.object_id == object.modeling_object_id &&
                 edge_id(line.element.edge_a, line.element.edge_b) == edge;
        });
    ASSERT_NE(hovered, overlay.lines.end());
    EXPECT_EQ(hovered->color, remove ? quader::kOverlayRemoveWireColor
                                     : quader::kOverlayHoverWireColor);
  }
};

quader::QuaderModelingObjectRef
create_box_at(ViewportInteractionHarness &harness, quader::QVec3 center) {
  const quader::QVec3 half{1.0f, 1.0f, 1.0f};
  const quader::QuaderApiResult<quader::QuaderModelingObjectRef> box =
      harness.editor.modeling().create_box(
          quader::QuaderBounds{
              .min = center - half,
              .max = center + half,
          },
          "Viewport Interaction Box");
  EXPECT_TRUE(box.ok) << box.message;
  return box.value();
}

void set_curve_solidify(ViewportInteractionHarness &harness,
                        quader::EntityId entity, float radius,
                        int resolution = 8) {
  const quader::CurveComponent *current =
      harness.session.scene().curve(entity);
  ASSERT_NE(current, nullptr);
  if (current == nullptr) {
    return;
  }
  quader::CurveComponent updated = *current;
  updated.solidify = true;
  updated.solidify_radius = radius;
  updated.solidify_resolution = resolution;
  ASSERT_TRUE(harness.session.scene().set_curve(entity, updated));
}

quader::QuaderModelingObjectRef create_project_box_at(
    ProjectViewportInteractionHarness &harness, quader::QVec3 center,
    std::string name = "Viewport Material Box") {
  const quader::QVec3 half{1.0f, 1.0f, 1.0f};
  const quader::QuaderApiResult<quader::QuaderModelingObjectRef> box =
      harness.editor.modeling().create_box(
          quader::QuaderBounds{.min = center - half, .max = center + half},
          std::move(name));
  EXPECT_TRUE(box.ok()) << box.error_message();
  return box.value();
}

std::vector<quader::QuaderElementId> authored_face_ids(
    ProjectViewportInteractionHarness &harness,
    quader::QuaderModelingObjectRef object) {
  const quader::MeshComponent *component =
      harness.scene_session().scene().mesh(object.entity);
  EXPECT_NE(component, nullptr);
  if (component == nullptr) {
    return {};
  }
  const quader::MeshAsset *mesh =
      harness.scene_session().assets().mesh(component->mesh);
  EXPECT_NE(mesh, nullptr);
  if (mesh == nullptr || !mesh->has_authored_polygon_document ||
      mesh->authored_polygon_document == nullptr) {
    return {};
  }
  std::vector<quader::QuaderElementId> result;
  result.reserve(mesh->authored_polygon_document->faces.size());
  for (const quader_poly::Face &face :
       mesh->authored_polygon_document->faces) {
    result.push_back(static_cast<quader::QuaderElementId>(face.id));
  }
  return result;
}

quader::ProjectAssetId default_project_material_id() {
  return quader::ProjectAssetId::parse(
             quader::kDefaultProjectMaterialId)
      .value();
}

/// Prepares a selected textured source and an unselected face in another mesh.
struct FaceTextureTransferScenario final {
  ProjectViewportInteractionHarness harness;
  quader::QuaderModelingObjectRef source;
  quader::QuaderModelingObjectRef target;
  quader::QuaderElementId source_face = 0;
  quader::QuaderElementId target_face = 0;
  quader::QVec2 source_screen = {};
  quader::QVec2 target_screen = {};

  void initialize() {
    harness.initialize_with_second_material();
    source = create_project_box_at(harness, {-3.0f, 0.0f, 0.0f}, "Source");
    target = create_project_box_at(harness, {1.0f, 0.0f, 0.0f}, "Target");
    const auto pick_face = [&](quader::QVec3 center, quader::QVec2 &screen) {
      const auto projected = harness.camera.project_world_point(center);
      EXPECT_TRUE(projected.visible);
      screen = projected.position;
      const auto ray = harness.camera.ray_from_viewport_point(screen);
      return harness.editor.selection().preview_hover({
          .ray = {.origin = ray.origin, .direction = ray.direction},
          .face_plane_hover = true,
      });
    };
    const auto source_hit = pick_face({-3.0f, 0.0f, 1.0f}, source_screen);
    const auto target_hit = pick_face({1.0f, 0.0f, 1.0f}, target_screen);
    ASSERT_EQ(source_hit.target.kind, quader::QuaderActiveSelectionKind::Face);
    ASSERT_EQ(target_hit.target.kind, quader::QuaderActiveSelectionKind::Face);
    ASSERT_EQ(source_hit.target.object_id, source.modeling_object_id);
    ASSERT_EQ(target_hit.target.object_id, target.modeling_object_id);
    source_face = source_hit.target.face_id;
    target_face = target_hit.target.face_id;
    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Face)
                    .ok());
    harness.router.set_selection_mode(quader::ViewportSelectionMode::Face);
    ASSERT_TRUE(harness.editor.selection().select_face(source, source_face).ok());
    ASSERT_TRUE(harness.editor.modeling()
                    .apply_material(quader::QuaderMaterialReference::project(
                        harness.second_material()))
                    .ok());
    harness.scene_session().history().clear();
    harness.input.mouse_local = target_screen;
    harness.input.mouse_screen = target_screen;
  }

  void expect_source_selection() const {
    const auto selection = harness.editor.selection().summary();
    EXPECT_EQ(selection.mode, quader::QuaderSelectionMode::Face);
    EXPECT_EQ(selection.selected_face_count, 1u);
    EXPECT_EQ(selection.active_object_id, source.modeling_object_id);
    EXPECT_EQ(selection.active_face_id, source_face);
  }

  void expect_target_material(quader::ProjectAssetId expected) const {
    const auto material = harness.editor.modeling().project_material_for_face(
        target.modeling_object_id, target_face);
    ASSERT_TRUE(material.ok()) << material.error_message();
    EXPECT_EQ(material.value().id, expected);
  }
};

quader::ModelingToolRayEvent
knife_event_at(ViewportInteractionHarness &harness,
               quader::QVec3 world_position) {
  const quader::ViewportScreenPoint screen =
      harness.camera.project_world_point(world_position);
  EXPECT_TRUE(screen.visible);
  const quader::ViewportRay ray =
      harness.camera.ray_from_viewport_point(screen.position);
  return {
      .screen_position = screen.position,
      .ray = {.origin = ray.origin, .direction = ray.direction},
      .camera = quader::ViewportCameraAdapter::modeling_camera(
          harness.camera, harness.input.viewport_size),
      .grid_size = 1.0,
      .vertex_radius = 0.1,
      .edge_radius = 0.1,
      .vertex_screen_radius = 12.0,
      .edge_screen_radius = 12.0,
      .snap_enabled = false,
  };
}

const quader_editor_core::TransformGizmoAxisPrimitive *
find_axis(const quader_editor_core::TransformGizmoFrame &frame,
          quader_editor_core::TransformAxis axis) {
  const auto found = std::find_if(
      frame.axes.begin(), frame.axes.end(),
      [axis](const auto &primitive) { return primitive.axis == axis; });
  return found == frame.axes.end() ? nullptr : &*found;
}

const quader_editor_core::TransformGizmoRingSegmentPrimitive *
find_ring(const quader_editor_core::TransformGizmoFrame &frame,
          quader_editor_core::TransformAxis axis) {
  const auto found =
      std::find_if(frame.ring_segments.begin(), frame.ring_segments.end(),
                   [axis](const auto &primitive) {
                     return primitive.axis == axis && primitive.front_facing;
                   });
  return found == frame.ring_segments.end() ? nullptr : &*found;
}

quader::QVec2 rotated_point(quader::QVec2 pivot, quader::QVec2 point,
                            float radians) {
  const float c = std::cos(radians);
  const float s = std::sin(radians);
  const quader::QVec2 delta = point - pivot;
  return {
      pivot.x + delta.x * c - delta.y * s,
      pivot.y + delta.x * s + delta.y * c,
  };
}

void expect_transform_near(const quader::TransformComponent &actual,
                           const quader::TransformComponent &expected) {
  EXPECT_NEAR(actual.position.x, expected.position.x, 0.001f);
  EXPECT_NEAR(actual.position.y, expected.position.y, 0.001f);
  EXPECT_NEAR(actual.position.z, expected.position.z, 0.001f);
  EXPECT_NEAR(actual.rotation_euler.x, expected.rotation_euler.x, 0.001f);
  EXPECT_NEAR(actual.rotation_euler.y, expected.rotation_euler.y, 0.001f);
  EXPECT_NEAR(actual.rotation_euler.z, expected.rotation_euler.z, 0.001f);
  EXPECT_NEAR(actual.scale.x, expected.scale.x, 0.001f);
  EXPECT_NEAR(actual.scale.y, expected.scale.y, 0.001f);
  EXPECT_NEAR(actual.scale.z, expected.scale.z, 0.001f);
}

void expect_vec3_near(quader::QVec3 actual, quader::QVec3 expected,
                      float epsilon = 0.001f) {
  EXPECT_NEAR(actual.x, expected.x, epsilon);
  EXPECT_NEAR(actual.y, expected.y, epsilon);
  EXPECT_NEAR(actual.z, expected.z, epsilon);
}

quader::QQuat canonical_quaternion(quader::QQuat value) {
  value = quader::normalize(value);
  if (value.w < 0.0f) {
    value.x = -value.x;
    value.y = -value.y;
    value.z = -value.z;
    value.w = -value.w;
  }
  return value;
}

quader::QQuat local_rotation_delta(const quader::TransformComponent &before,
                                   const quader::TransformComponent &after) {
  const quader::QQuat before_rotation =
      quader::normalize(quader::quat_from_euler_radians(before.rotation_euler));
  const quader::QQuat after_rotation =
      quader::normalize(quader::quat_from_euler_radians(after.rotation_euler));
  const quader::QQuat inverse_before{-before_rotation.x, -before_rotation.y,
                                     -before_rotation.z, before_rotation.w};
  return canonical_quaternion(inverse_before * after_rotation);
}

void expect_quaternion_near(quader::QQuat actual, quader::QQuat expected,
                            float epsilon = 0.001f) {
  actual = canonical_quaternion(actual);
  expected = canonical_quaternion(expected);
  EXPECT_NEAR(actual.x, expected.x, epsilon);
  EXPECT_NEAR(actual.y, expected.y, epsilon);
  EXPECT_NEAR(actual.z, expected.z, epsilon);
  EXPECT_NEAR(actual.w, expected.w, epsilon);
}

quader::QVec3 gizmo_axis_world_direction(
    const quader_editor_core::TransformGizmoAxisPrimitive &axis) {
  return quader::normalize_or_zero(axis.world_tip - axis.world_start);
}

quader::QuaderViewportCamera transform_drag_test_camera() {
  return {
      .orthographic = true,
      .origin = {0.0f, 0.0f, 10.0f},
      .x_axis = {1.0f, 0.0f, 0.0f},
      .y_axis = {0.0f, 1.0f, 0.0f},
      .z_axis = {0.0f, 0.0f, 1.0f},
      .viewport_size = {800.0f, 600.0f},
      .orthographic_size = 12.0,
      .fov_degrees = 60.0,
  };
}

quader::QuaderRay transform_drag_test_ray() {
  return {
      .origin = {0.0f, 0.0f, 10.0f},
      .direction = {0.0f, 0.0f, -1.0f},
  };
}

quader::QuaderTransformDragPreview perform_local_x_scene_drag(
    ViewportInteractionHarness &harness, quader::QuaderTransformTool tool,
    quader::QVec2 update_position) {
  const std::optional<quader::QuaderTransform3> orientation =
      harness.editor.query().selection_transform_orientation();
  const std::optional<quader::QVec3> pivot =
      harness.editor.query().selection_transform_pivot();
  EXPECT_TRUE(orientation.has_value());
  EXPECT_TRUE(pivot.has_value());
  if (!orientation.has_value() || !pivot.has_value()) {
    return {};
  }

  const bool scale = tool == quader::QuaderTransformTool::Scale;
  const quader::QVec2 screen_pivot{100.0f, 100.0f};
  const quader::QVec2 screen_position =
      scale ? screen_pivot : quader::QVec2{120.0f, 100.0f};
  const quader::QVec2 screen_tip =
      scale ? quader::QVec2{160.0f, 100.0f}
            : quader::QVec2{120.0f, 100.0f};
  const quader::QuaderViewportCamera camera = transform_drag_test_camera();
  const quader::QuaderRay ray = transform_drag_test_ray();
  const quader::QuaderApiResult<quader::QuaderTransformDragPreview> begin =
      harness.editor.modeling().begin_transform_drag({
          .tool = tool,
          .operation = quader::QuaderTransformOperation::Transform,
          .axis = quader::QuaderTransformAxis::X,
          .space = quader::QuaderTransformSpace::Local,
          .orientation = *orientation,
          .pivot = *pivot,
          .screen_position = screen_position,
          .screen_pivot = screen_pivot,
          .screen_tip = screen_tip,
          .has_world_direction = true,
          .world_direction = orientation->x_axis,
          .ray = ray,
          .camera = camera,
          .grid_size = 1.0,
          .vertex_snap_radius = 0.0,
          .grid_snap_enabled = false,
          .rotation_snap_degrees = 0.0,
      });
  EXPECT_TRUE(begin.ok()) << begin.error_message();
  if (!begin.ok()) {
    return {};
  }

  const quader::QuaderApiResult<quader::QuaderTransformDragPreview> update =
      harness.editor.modeling().update_transform_drag({
          .screen_position = update_position,
          .ray = ray,
          .camera = camera,
          .grid_size = 1.0,
          .vertex_snap_radius = 0.0,
          .grid_snap_enabled = false,
          .rotation_snap_degrees = 0.0,
      });
  EXPECT_TRUE(update.ok()) << update.error_message();
  if (!update.ok()) {
    return {};
  }
  const quader::QuaderApiResult<quader::QuaderTransformDragPreview> commit =
      harness.editor.modeling().commit_transform_drag();
  EXPECT_TRUE(commit.ok()) << commit.error_message();
  EXPECT_TRUE(commit.changed());
  return update.value();
}

quader_editor_core::TransformGizmoFrameInput
test_gizmo_input(quader_editor_core::ViewportInteractionTool tool,
                 const ViewportInteractionHarness &harness) {
  quader_editor_core::TransformGizmoFrameInput input;
  input.tool = tool;
  input.has_selection = true;
  input.has_pivot = true;
  input.pivot = {};
  input.camera = quader::ViewportCameraAdapter::runtime_camera(
      harness.camera, harness.input.viewport_size);
  input.gizmo_size_pixels = quader::kOverlayTransformGizmoSizePixels;
  input.pick_radius_pixels = quader::kOverlayTransformGizmoPickRadiusPixels;
  input.move_center_radius_pixels =
      quader::kOverlayTransformGizmoMoveCenterRadiusPixels;
  input.ring_segments = 32;
  return input;
}

quader::ViewportOverlayFrame
build_gizmo_overlay(const quader_editor_core::TransformGizmoFrame &frame,
                    const ViewportInteractionHarness &harness) {
  quader::ViewportOverlayFrame overlay;
  quader::ViewportToolOverlayProjection::append_transform_gizmo(
      overlay,
      quader::ViewportCameraAdapter::overlay_camera(
          harness.camera, harness.input.viewport_size),
      frame);
  return overlay;
}

quader::QVec2
axis_screen_point(const quader_editor_core::TransformGizmoAxisPrimitive &axis,
                  float factor) {
  return axis.screen_start + (axis.screen_tip - axis.screen_start) * factor;
}

void select_single_component(ViewportInteractionHarness &harness,
                             quader::QuaderModelingObjectRef object,
                             quader::QuaderSelectionMode mode) {
  ASSERT_TRUE(harness.editor.selection().select_object(object).ok);
  ASSERT_TRUE(harness.editor.selection().set_mode(mode).ok());
  switch (mode) {
  case quader::QuaderSelectionMode::Vertex:
    ASSERT_TRUE(harness.editor.selection().select_vertex(object, 1).ok());
    break;
  case quader::QuaderSelectionMode::Edge:
    ASSERT_TRUE(harness.editor.selection().select_edge(object, 1, 2).ok());
    break;
  case quader::QuaderSelectionMode::Face:
    ASSERT_TRUE(harness.editor.selection().select_face(object, 1).ok());
    break;
  default:
    FAIL() << "Unsupported component mode";
    break;
  }
}

void press_update_release_gizmo(ViewportInteractionHarness &harness,
                                quader::QVec2 start, quader::QVec2 update) {
  harness.input.mouse_local = start;
  harness.input.mouse_screen = start;
  (void)harness.controller.handle_before_active_tool(harness.editor,
                                                     harness.frame());
  EXPECT_EQ(harness.controller.active_tool_pointer_cursor(harness.router.state().active_tool),
            quader::ViewportPointerCursor::Default);
  const quader::ViewportBeforeToolInteractionResult press =
      harness.controller.handle_before_active_tool(
          harness.editor, harness.frame(true, true, false, start));
  ASSERT_TRUE(press.gizmo_consumed);
  EXPECT_EQ(harness.controller.active_tool_pointer_cursor(harness.router.state().active_tool),
            quader::ViewportPointerCursor::Default);

  harness.input.mouse_local = update;
  harness.input.mouse_screen = update;
  const quader::ViewportBeforeToolInteractionResult drag =
      harness.controller.handle_before_active_tool(
          harness.editor, harness.frame(false, true, false, start));
  EXPECT_TRUE(drag.gizmo_consumed);
  EXPECT_EQ(harness.controller.active_tool_pointer_cursor(harness.router.state().active_tool),
            quader::ViewportPointerCursor::Default);

  const quader::ViewportBeforeToolInteractionResult release =
      harness.controller.handle_before_active_tool(
          harness.editor, harness.frame(false, false, true, start));
  EXPECT_TRUE(release.gizmo_consumed);
  EXPECT_EQ(harness.controller.active_tool_pointer_cursor(harness.router.state().active_tool),
            quader::ViewportPointerCursor::Default);
}

std::vector<quader::EntityId>
selected_entities(const ViewportInteractionHarness &harness) {
  return harness.session.scene().selected_entities();
}

void drag_selection_marquee(ViewportInteractionHarness &harness,
                            quader::QVec2 start, quader::QVec2 end) {
  harness.input.mouse_local = start;
  harness.input.mouse_screen = start;
  const quader::ViewportInteractionFrame press =
      harness.frame(true, true, false, start);
  const quader::ViewportBeforeToolInteractionResult before =
      harness.controller.handle_before_active_tool(harness.editor, press);
  harness.controller.handle_after_active_tool(harness.editor, press, {},
                                              before);
  harness.input.mouse_local = end;
  harness.input.mouse_screen = end;
  harness.controller.handle_after_active_tool(
      harness.editor, harness.frame(false, true, false, start), {}, {});
  harness.controller.handle_after_active_tool(
      harness.editor, harness.frame(false, false, true, start), {}, {});
  harness.controller.handle_after_active_tool(harness.editor, harness.frame(),
                                              {}, {});
}

quader::QuaderBoxSelectRequest curve_marquee_request(quader::QVec2 start,
                                                    quader::QVec2 end) {
  return {
      .camera = {.orthographic = true,
                 .origin = {0.0f, 0.0f, 8.0f},
                 .viewport_size = {800.0f, 600.0f},
                 .orthographic_size = 12.0},
      .start = start,
      .end = end,
      .rule = quader::QuaderBoxSelectionRule::Contain,
  };
}

bool has_snap_target_vertex_point(const quader::QuaderOverlayFrame &frame) {
  return std::any_of(frame.points.begin(), frame.points.end(),
                     [](const quader::QuaderOverlayPoint &point) {
                       return point.layer ==
                              quader::QuaderOverlayLayer::SnapTargetVertex;
                     });
}

std::optional<quader::QuaderMeshVertexSnapTarget>
vertex_snap_target_at(ViewportInteractionHarness &harness,
                      quader::QVec3 world_position) {
  const quader::ViewportScreenPoint projected =
      harness.camera.project_world_point(world_position);
  EXPECT_TRUE(projected.visible);
  if (!projected.visible) {
    return std::nullopt;
  }
  const quader::ViewportRay ray =
      harness.camera.ray_from_viewport_point(projected.position);
  return harness.editor.query().mesh_vertex_snap_target(
      {.origin = ray.origin, .direction = ray.direction},
      harness.settings.float_value(
          quader::ViewportFloatSettingId::PickVertexRadius),
      false);
}

bool has_triangle_style(const quader::QuaderOverlayFrame &frame,
                        quader::QuaderOverlayStyleRole role) {
  return std::any_of(frame.triangles.begin(), frame.triangles.end(),
                     [role](const quader::QuaderOverlayTriangle &triangle) {
                       return triangle.style_role == role;
                     });
}

bool has_line_style(const quader::QuaderOverlayFrame &frame,
                    quader::QuaderOverlayStyleRole role) {
  return std::any_of(frame.lines.begin(), frame.lines.end(),
                     [role](const quader::QuaderOverlayLine &line) {
                       return line.style_role == role;
                     });
}

bool has_triangle_style(const quader::ViewportOverlayFrame &frame,
                        quader::ViewportOverlayStyleRole role) {
  return std::any_of(frame.triangles.begin(), frame.triangles.end(),
                     [role](const quader::ViewportOverlayTriangle &triangle) {
                       return triangle.style_role == role;
                     });
}

bool has_line_style(const quader::ViewportOverlayFrame &frame,
                    quader::ViewportOverlayStyleRole role) {
  return std::any_of(frame.lines.begin(), frame.lines.end(),
                     [role](const quader::ViewportOverlayLine &line) {
                       return line.style_role == role;
                     });
}

} // namespace

TEST(ViewportTransformGizmoOverlayBuilderTests, EmitsGodotStyleTransformGizmoVisuals) {
    ViewportInteractionHarness harness;

    EXPECT_EQ(quader::kOverlayTransformGizmoMoveCenterColor,
              quader::rgba8(253, 255, 136, 255));
    EXPECT_FLOAT_EQ(quader::kOverlayTransformGizmoMoveCenterRadiusPixels,
                    10.0f);
    EXPECT_FLOAT_EQ(
        quader::kOverlayTransformGizmoMoveCenterBorderWidthPixels, 1.0f);
    EXPECT_FLOAT_EQ(quader::kOverlayTransformGizmoMoveCenterDotSizePixels,
                    4.0f);

    const quader_editor_core::TransformGizmoFrame move_frame =
        quader_editor_core::build_transform_gizmo_frame(test_gizmo_input(
            quader_editor_core::ViewportInteractionTool::Move, harness));
    ASSERT_TRUE(move_frame.info.ok);
    const quader::ViewportOverlayFrame move_overlay =
        build_gizmo_overlay(move_frame, harness);

    const auto center_plane = std::find_if(
        move_frame.planes.begin(), move_frame.planes.end(),
        [](const quader_editor_core::TransformGizmoPlanePrimitive &plane) {
          return plane.axis == quader_editor_core::TransformAxis::All;
    });
    ASSERT_NE(center_plane, move_frame.planes.end());
    const auto screen_distance = [](quader::QVec2 a, quader::QVec2 b) {
      return std::hypot(a.x - b.x, a.y - b.y);
    };
    EXPECT_NEAR(screen_distance(center_plane->screen0, center_plane->screen1),
                20.0f, 0.5f);
    EXPECT_NEAR(screen_distance(center_plane->screen1, center_plane->screen2),
                20.0f, 0.5f);

    EXPECT_EQ(
        std::count_if(
            move_overlay.lines.begin(), move_overlay.lines.end(),
            [](const quader::ViewportOverlayLine &line) {
              return line.layer == quader::ViewportOverlayLayer::ToolHandle &&
                     line.color == quader::kOverlayToolAxisXColor &&
                     line.width_pixels ==
                         quader::kOverlayTransformGizmoAxisLineWidthPixels;
            }),
        1);
    EXPECT_EQ(std::count_if(
                  move_overlay.triangles.begin(), move_overlay.triangles.end(),
                  [](const quader::ViewportOverlayTriangle &triangle) {
                    return triangle.layer ==
                               quader::ViewportOverlayLayer::ToolPlane &&
                           triangle.color == quader::kOverlayToolPlaneXYColor;
                  }),
              2);
    const auto same_position = [](quader::QVec3 a, quader::QVec3 b) {
      return length(a - b) <= 0.0001f;
    };
    const auto matches_edge = [&same_position](
                                  const quader::ViewportOverlayLine &line,
                                  quader::QVec3 a, quader::QVec3 b) {
      return (same_position(line.start, a) && same_position(line.end, b)) ||
             (same_position(line.start, b) && same_position(line.end, a));
    };
    const std::array<std::pair<quader::QVec3, quader::QVec3>, 4>
        center_edges{{
            {center_plane->world0, center_plane->world1},
            {center_plane->world1, center_plane->world2},
            {center_plane->world2, center_plane->world3},
            {center_plane->world3, center_plane->world0},
        }};
    for (const auto &[edge_start, edge_end] : center_edges) {
      EXPECT_EQ(
          std::count_if(
              move_overlay.lines.begin(), move_overlay.lines.end(),
              [&](const quader::ViewportOverlayLine &line) {
                return line.layer ==
                           quader::ViewportOverlayLayer::ToolHandle &&
                       line.color ==
                           quader::kOverlayTransformGizmoMoveCenterColor &&
                       std::abs(
                           line.width_pixels -
                           quader::kOverlayTransformGizmoMoveCenterBorderWidthPixels) <=
                           0.001f &&
                       line.style_role ==
                           quader::ViewportOverlayStyleRole::ExplicitColor &&
                       line.source == quader::ViewportOverlaySource::Tool &&
                       matches_edge(line, edge_start, edge_end);
              }),
          1);
    }
    EXPECT_EQ(
        std::count_if(
            move_overlay.points.begin(), move_overlay.points.end(),
            [&move_frame](const quader::ViewportOverlayPoint &point) {
              return point.layer == quader::ViewportOverlayLayer::ToolHandle &&
                     point.color ==
                         quader::kOverlayTransformGizmoMoveCenterColor &&
                     length(point.position - move_frame.info.pivot) <=
                         0.0001f &&
                     std::abs(
                         point.size_pixels -
                         quader::kOverlayTransformGizmoMoveCenterDotSizePixels) <=
                         0.001f &&
                     !point.outline_growth_pixels.has_value();
            }),
        1);
    EXPECT_TRUE(std::none_of(
        move_overlay.triangles.begin(), move_overlay.triangles.end(),
        [](const quader::ViewportOverlayTriangle &triangle) {
          return triangle.color ==
                 quader::kOverlayTransformGizmoMoveCenterColor;
        })) << "the Maya-style center square must remain hollow";
    EXPECT_TRUE(
        std::none_of(move_overlay.lines.begin(), move_overlay.lines.end(),
                     [](const quader::ViewportOverlayLine &line) {
                       return line.color == quader::kOverlayToolLineShadowColor;
                     }));

    quader_editor_core::TransformGizmoFrameInput hovered_move_input =
        test_gizmo_input(quader_editor_core::ViewportInteractionTool::Move,
                         harness);
    hovered_move_input.hover_axis =
        quader_editor_core::TransformAxis::All;
    const quader::ViewportOverlayFrame hovered_move_overlay =
        build_gizmo_overlay(
            quader_editor_core::build_transform_gizmo_frame(
                hovered_move_input),
            harness);
    EXPECT_EQ(
        std::count_if(
            hovered_move_overlay.lines.begin(),
            hovered_move_overlay.lines.end(),
            [](const quader::ViewportOverlayLine &line) {
              return line.layer == quader::ViewportOverlayLayer::ToolHandle &&
                     line.color == quader::kOverlayToolAxisHighlightColor &&
                     std::abs(
                         line.width_pixels -
                         quader::kOverlayTransformGizmoMoveCenterBorderWidthPixels) <=
                         0.001f;
            }),
        4);
    EXPECT_EQ(
        std::count_if(
            hovered_move_overlay.points.begin(),
            hovered_move_overlay.points.end(),
            [](const quader::ViewportOverlayPoint &point) {
              return point.layer == quader::ViewportOverlayLayer::ToolHandle &&
                     point.color == quader::kOverlayToolAxisHighlightColor &&
                     std::abs(
                         point.size_pixels -
                         quader::kOverlayTransformGizmoMoveCenterDotSizePixels) <=
                         0.001f &&
                     !point.outline_growth_pixels.has_value();
            }),
        1);
    EXPECT_TRUE(std::none_of(
        hovered_move_overlay.triangles.begin(),
        hovered_move_overlay.triangles.end(),
        [](const quader::ViewportOverlayTriangle &triangle) {
          return triangle.color == quader::kOverlayToolAxisHighlightColor;
        })) << "hover must not fill the Maya-style center square";

    const quader_editor_core::TransformGizmoFrame scale_frame =
        quader_editor_core::build_transform_gizmo_frame(test_gizmo_input(
            quader_editor_core::ViewportInteractionTool::Scale, harness));
    ASSERT_TRUE(scale_frame.info.ok);
    const quader::ViewportOverlayFrame scale_overlay =
        build_gizmo_overlay(scale_frame, harness);
    EXPECT_EQ(
        std::count_if(scale_overlay.triangles.begin(),
                      scale_overlay.triangles.end(),
                      [](const quader::ViewportOverlayTriangle &triangle) {
                        return triangle.layer ==
                                   quader::ViewportOverlayLayer::ToolHandle &&
                               triangle.color == quader::kOverlayToolAxisXColor;
                      }),
        12);

    const quader_editor_core::TransformGizmoFrame rotate_frame =
        quader_editor_core::build_transform_gizmo_frame(test_gizmo_input(
            quader_editor_core::ViewportInteractionTool::Rotate, harness));
    ASSERT_TRUE(rotate_frame.info.ok);
    const quader::ViewportOverlayFrame rotate_overlay =
        build_gizmo_overlay(rotate_frame, harness);
    const auto axis_ring_lines = std::count_if(
        rotate_overlay.lines.begin(), rotate_overlay.lines.end(),
        [](const quader::ViewportOverlayLine &line) {
          return line.layer == quader::ViewportOverlayLayer::ToolHandle &&
                 line.width_pixels == 3.0f;
        });
    const auto front_facing_ring_segments = std::count_if(
        rotate_frame.ring_segments.begin(), rotate_frame.ring_segments.end(),
        [](const auto &segment) { return segment.front_facing; });
    EXPECT_EQ(axis_ring_lines, front_facing_ring_segments);
    EXPECT_TRUE(std::any_of(
        rotate_frame.ring_segments.begin(), rotate_frame.ring_segments.end(),
        [](const auto &segment) { return !segment.front_facing; }));
    EXPECT_TRUE(std::none_of(
        rotate_overlay.lines.begin(), rotate_overlay.lines.end(),
        [](const quader::ViewportOverlayLine &line) {
          return line.layer == quader::ViewportOverlayLayer::ToolHandle &&
                 ((line.color >> 24u) & 0xffu) < 100u;
        }));

    quader_editor_core::TransformGizmoFrame rotate_indicator_frame =
        rotate_frame;
    rotate_indicator_frame.info.rotate_indicator = {
        .active = true,
        .axis = quader_editor_core::TransformAxis::Z,
        .world_axis = {0.0f, 0.0f, 1.0f},
        .start_direction = {1.0f, 0.0f, 0.0f},
        .has_mouse_position = true,
        .mouse_position = {0.0f, 2.0f, 0.0f},
        .angle_degrees = 67.5,
    };
    const quader::ViewportOverlayFrame rotate_indicator_overlay =
        build_gizmo_overlay(rotate_indicator_frame, harness);
    EXPECT_GT(std::count_if(
                  rotate_indicator_overlay.triangles.begin(),
                  rotate_indicator_overlay.triangles.end(),
                  [](const quader::ViewportOverlayTriangle &triangle) {
                    return triangle.layer ==
                               quader::ViewportOverlayLayer::ToolPreviewFill &&
                           triangle.color ==
                               quader::kOverlayTransformGizmoRotatePieFillColor;
                  }),
              0);
    EXPECT_EQ(std::count_if(
                  rotate_indicator_overlay.lines.begin(),
                  rotate_indicator_overlay.lines.end(),
                  [](const quader::ViewportOverlayLine &line) {
                    return line.layer ==
                               quader::ViewportOverlayLayer::ToolPreviewLine &&
                           line.color ==
                               quader::kOverlayTransformGizmoRotatePieLineColor;
                  }),
              2);
    EXPECT_EQ(
        std::count_if(
            rotate_indicator_overlay.lines.begin(),
            rotate_indicator_overlay.lines.end(),
            [](const quader::ViewportOverlayLine &line) {
              return line.layer ==
                         quader::ViewportOverlayLayer::ToolPreviewLine &&
                     line.color ==
                         quader::kOverlayTransformGizmoRotatePieLineColor &&
                     std::abs(line.width_pixels - 1.55f) <= 0.001f &&
                     length(line.end - quader::QVec3{0.0f, 2.0f, 0.0f}) <=
                         0.001f;
            }),
        1);
}

TEST(ViewportInteractionControllerTests,
     NativeToolPipelineUsesOneCommonGizmoBlockingOutcome) {
  ViewportInteractionHarness harness;
  const quader::QuaderModelingObjectRef box = create_unit_box(harness);
  ASSERT_TRUE(harness.editor.selection().select_object(box).ok());
  ASSERT_TRUE(
      harness.editor.tools().activate(quader::ViewportToolId::LinearArray).ok());
  harness.router.set_active_tool(quader::ViewportToolId::LinearArray);
  harness.input.active_tool_owns_viewport = true;

  const quader::ViewportInteractionFrame interaction = harness.frame();
  const quader::ViewportNativeToolInteractionFrame native{
      .input = harness.input,
      .input_result = harness.input_result,
  };
  const quader::ViewportToolPointerLifecycleResult result =
      harness.controller.handle_tool_pointer_lifecycle(
          harness.editor, harness.session.scene(), harness.session.assets(),
          harness.session.native_tools(), &harness.session.point_placement(),
          harness.camera,
          {.interaction = interaction, .native_tool = native});

  EXPECT_FALSE(result.curve_gizmo_prepass);
  EXPECT_TRUE(result.native_tool.linear_array_consumed);
  EXPECT_TRUE(result.native_tool.consumed);
  EXPECT_TRUE(result.native_tool.blocks_transform_gizmo);
  EXPECT_TRUE(result.before_tool.gizmo_consumed);
  EXPECT_EQ(result.interaction_selection.summary.selection_revision,
            harness.editor.selection().summary().selection_revision);
}

TEST(ViewportInteractionControllerTests,
     LightCreationCommandsPlaceAtFrozenViewportPressAndExitOneShot) {
  struct LightCase final {
    quader::QuaderCommandId command;
    quader::QuaderLightType type;
    std::string_view tool_id;
  };

  for (const LightCase light_case : {
           LightCase{quader::quader_command::kSceneCreatePointLight,
                     quader::QuaderLightType::Point,
                     "create_point_light"},
           LightCase{quader::quader_command::kSceneCreateDirectionalLight,
                     quader::QuaderLightType::Directional,
                     "create_directional_light"},
       }) {
    SCOPED_TRACE(light_case.tool_id);
    int dirty_changed_events = 0;
    std::optional<bool> last_dirty_state;
    ViewportInteractionHarness harness;
    harness.camera.view_axis(quader::ViewportViewAxis::Y, 1);
    harness.input.orthographic_view = true;
    const quader::EntityId root = harness.session.scene().scene_root();
    ASSERT_NE(root, quader::kInvalidEntityId);
    ASSERT_TRUE(harness.session.scene().set_transform(
        root, quader::TransformComponent{
                  .position = {10.0f, 2.0f, -4.0f},
                  .rotation_euler = {0.0f, 0.25f, 0.0f},
                  .scale = {1.5f, 0.75f, 2.0f},
              }));
    harness.session.mark_clean();
    const quader::QuaderEventSubscription dirty_subscription =
        harness.editor.events().subscribe(
            quader::quader_event::kSceneDirtyChanged,
            [&](const quader::QuaderEvent &event) {
              ++dirty_changed_events;
              last_dirty_state = event.scene_dirty;
            });
    ASSERT_TRUE(dirty_subscription.valid());

    const std::size_t entity_count_before =
        harness.editor.query().entities().size();
    const std::size_t undo_count_before =
        harness.session.history().undo_count();
    const quader::QuaderApiVoidResult armed =
        harness.editor.commands().execute(light_case.command);
    ASSERT_TRUE(armed.ok()) << armed.error_message();
    ASSERT_TRUE(armed.changed());
    ASSERT_EQ(harness.editor.tools().active_tool(), light_case.tool_id);
    EXPECT_EQ(dirty_changed_events, 0);

    const auto dispatch = [&](bool pressed, bool down, bool released,
                              quader::QVec2 click_local,
                              quader::QVec2 current_local) {
      harness.input.mouse_local = current_local;
      harness.input.mouse_screen = current_local;
      harness.input.modal_tool_active =
          harness.session.native_tools().light_creation_active();
      const quader::ViewportInteractionFrame interaction =
          harness.frame(pressed, down, released, click_local);
      return harness.controller.handle_tool_pointer_lifecycle(
          harness.editor, harness.session.scene(), harness.session.assets(),
          harness.session.native_tools(), &harness.session.point_placement(),
          harness.camera,
          quader::ViewportToolPointerLifecycleFrame{
              .interaction = interaction,
              .native_tool =
                  {
                      .input = harness.input,
                      .input_result = harness.input_result,
                      .left_pressed = pressed,
                      .left_down = down,
                      .left_released = released,
                      .left_click_local = click_local,
                      .scene_snap_enabled = false,
                      .grid_snap_enabled = false,
                  },
          });
    };

    const auto idle = dispatch(false, false, false, {},
                               harness.input.mouse_local);
    EXPECT_TRUE(idle.native_tool.consumed);
    EXPECT_TRUE(idle.native_tool.blocks_transform_gizmo);
    EXPECT_EQ(harness.controller.active_tool_pointer_cursor(
                  harness.router.state().active_tool),
              quader::ViewportPointerCursor::PrecisionCross);
    EXPECT_EQ(dirty_changed_events, 0);

    constexpr quader::QVec3 kExpectedWorld{1.375f, 0.0f, -2.625f};
    const quader::ViewportScreenPoint expected_screen =
        harness.camera.project_world_point(kExpectedWorld);
    ASSERT_TRUE(expected_screen.visible);
    const quader::QVec2 later_hover =
        expected_screen.position + quader::QVec2{75.0f, -45.0f};
    const auto press = dispatch(true, true, false, expected_screen.position,
                                later_hover);
    EXPECT_TRUE(press.native_tool.consumed);
    EXPECT_TRUE(press.native_tool.blocks_transform_gizmo);
    EXPECT_TRUE(press.native_tool.claim_interaction_pane);
    ASSERT_TRUE(press.native_tool.command_attempted);
    ASSERT_TRUE(press.native_tool.command_result.ok)
        << press.native_tool.command_result.message;
    EXPECT_TRUE(press.native_tool.command_result.changed);
    EXPECT_TRUE(press.native_tool.sync_active_tool);
    EXPECT_EQ(dirty_changed_events, 1);
    ASSERT_TRUE(last_dirty_state.has_value());
    EXPECT_TRUE(*last_dirty_state);

    const std::vector<quader::QuaderEntitySnapshot> entities =
        harness.editor.query().entities();
    ASSERT_EQ(entities.size(), entity_count_before + 1u);
    const auto light = std::find_if(
        entities.begin(), entities.end(),
        [](const quader::QuaderEntitySnapshot &entity) {
          return entity.has_light;
        });
    ASSERT_NE(light, entities.end());
    EXPECT_EQ(light->light.type, light_case.type);
    EXPECT_TRUE(light->selected);
    EXPECT_TRUE(light->active);
    const quader::QVec3 created_world = quader::transform_point(
        harness.session.scene().world_transform_matrix(light->id), {});
    EXPECT_NEAR(created_world.x, kExpectedWorld.x, 0.0001f);
    EXPECT_NEAR(created_world.y, kExpectedWorld.y, 0.0001f);
    EXPECT_NEAR(created_world.z, kExpectedWorld.z, 0.0001f);
    EXPECT_EQ(harness.session.history().undo_count(), undo_count_before + 1u);
    EXPECT_FALSE(harness.session.native_tools().light_creation_active());
    EXPECT_EQ(harness.editor.tools().active_tool(), "select");

    const auto release = dispatch(false, false, true, later_hover, later_hover);
    EXPECT_TRUE(release.native_tool.consumed);
    EXPECT_EQ(harness.editor.query().entities().size(),
              entity_count_before + 1u);
    EXPECT_EQ(harness.session.history().undo_count(), undo_count_before + 1u);
    EXPECT_EQ(dirty_changed_events, 1);
    EXPECT_NE(harness.controller.active_tool_pointer_cursor(
                  harness.router.state().active_tool),
              quader::ViewportPointerCursor::PrecisionCross);

    ASSERT_TRUE(harness.editor.history().undo().ok());
    EXPECT_FALSE(harness.editor.query().entity(light->id).has_value());
    EXPECT_TRUE(selected_entities(harness).empty());
    EXPECT_FALSE(harness.editor.selection().summary().has_selection);
    ASSERT_TRUE(harness.editor.history().redo().ok());
    const std::optional<quader::QuaderEntitySnapshot> restored =
        harness.editor.query().entity(light->id);
    ASSERT_TRUE(restored.has_value());
    EXPECT_EQ(restored->light.type, light_case.type);
    EXPECT_TRUE(restored->selected);
    EXPECT_TRUE(restored->active);
    EXPECT_EQ(selected_entities(harness),
              std::vector<quader::EntityId>{light->id});
    const quader::QVec3 restored_world = quader::transform_point(
        harness.session.scene().world_transform_matrix(restored->id), {});
    EXPECT_NEAR(restored_world.x, kExpectedWorld.x, 0.0001f);
    EXPECT_NEAR(restored_world.y, kExpectedWorld.y, 0.0001f);
    EXPECT_NEAR(restored_world.z, kExpectedWorld.z, 0.0001f);
    EXPECT_EQ(dirty_changed_events, 1);
    EXPECT_TRUE(harness.editor.events().unsubscribe(dirty_subscription));
  }
}

TEST(ViewportInteractionControllerTests,
     LightCreationCompletionDoesNotClearReentrantSameKindRearm) {
  ViewportInteractionHarness harness;
  harness.camera.view_axis(quader::ViewportViewAxis::Y, 1);
  harness.input.orthographic_view = true;
  harness.session.mark_clean();

  bool dirty_callback_entered = false;
  bool cancel_succeeded = false;
  bool rearm_succeeded = false;
  const quader::QuaderEventSubscription dirty_subscription =
      harness.editor.events().subscribe(
          quader::quader_event::kSceneDirtyChanged,
          [&](const quader::QuaderEvent &event) {
            if (dirty_callback_entered || !event.scene_dirty.value_or(false)) {
              return;
            }
            dirty_callback_entered = true;
            const quader::QuaderApiVoidResult canceled =
                harness.editor.tools().cancel_active_tool_preview();
            cancel_succeeded = canceled.ok() && canceled.changed();
            const quader::QuaderApiVoidResult rearmed =
                harness.editor.commands().execute(
                    quader::quader_command::kSceneCreatePointLight);
            rearm_succeeded = rearmed.ok() && rearmed.changed();
          });
  ASSERT_TRUE(dirty_subscription.valid());

  const std::size_t entity_count_before =
      harness.editor.query().entities().size();
  const std::size_t undo_count_before = harness.session.history().undo_count();
  ASSERT_TRUE(harness.editor.commands()
                  .execute(quader::quader_command::kSceneCreatePointLight)
                  .ok());

  constexpr quader::QVec3 kExpectedWorld{0.625f, 0.0f, -1.875f};
  const quader::ViewportScreenPoint screen =
      harness.camera.project_world_point(kExpectedWorld);
  ASSERT_TRUE(screen.visible);
  harness.input.mouse_local = screen.position;
  harness.input.mouse_screen = screen.position;
  harness.input.modal_tool_active = true;
  const quader::ViewportInteractionFrame interaction =
      harness.frame(true, true, false, screen.position);
  const quader::ViewportToolPointerLifecycleResult placed =
      harness.controller.handle_tool_pointer_lifecycle(
          harness.editor, harness.session.scene(), harness.session.assets(),
          harness.session.native_tools(), &harness.session.point_placement(),
          harness.camera,
          quader::ViewportToolPointerLifecycleFrame{
              .interaction = interaction,
              .native_tool =
                  {
                      .input = harness.input,
                      .input_result = harness.input_result,
                      .left_pressed = true,
                      .left_down = true,
                      .left_click_local = screen.position,
                      .scene_snap_enabled = false,
                      .grid_snap_enabled = false,
                  },
          });

  ASSERT_TRUE(placed.native_tool.command_attempted);
  ASSERT_TRUE(placed.native_tool.command_result.ok)
      << placed.native_tool.command_result.message;
  EXPECT_TRUE(placed.native_tool.command_result.changed);
  EXPECT_TRUE(dirty_callback_entered);
  EXPECT_TRUE(cancel_succeeded);
  EXPECT_TRUE(rearm_succeeded);
  EXPECT_EQ(harness.editor.query().entities().size(),
            entity_count_before + 1u);
  EXPECT_EQ(harness.session.history().undo_count(), undo_count_before + 1u);
  EXPECT_TRUE(harness.session.native_tools().light_creation_active());
  EXPECT_EQ(harness.editor.tools().active_tool(), "create_point_light");
  EXPECT_EQ(harness.controller.active_tool_pointer_cursor(
                harness.router.state().active_tool),
            quader::ViewportPointerCursor::PrecisionCross);

  EXPECT_TRUE(harness.editor.events().unsubscribe(dirty_subscription));
  EXPECT_TRUE(harness.editor.tools().cancel_active_tool_preview().ok());
  EXPECT_FALSE(harness.session.native_tools().light_creation_active());
}

TEST(ViewportInteractionControllerTests,
     LightCreationUsesRawPerspectiveSurfaceHitWithoutGridSnap) {
  ViewportInteractionHarness harness;
  ASSERT_EQ(harness.camera.state().projection,
            quader::ViewportProjection::Perspective);
  const quader::QuaderModelingObjectRef box = create_unit_box(harness);
  ASSERT_NE(box.entity, quader::kInvalidEntityId);
  const std::size_t undo_count_before = harness.session.history().undo_count();

  ASSERT_TRUE(harness.editor.commands()
                  .execute(quader::quader_command::kSceneCreatePointLight)
                  .ok());
  constexpr quader::QVec3 kExpectedSurfaceWorld{0.37f, -0.22f, 1.0f};
  const quader::ViewportScreenPoint surface_screen =
      harness.camera.project_world_point(kExpectedSurfaceWorld);
  ASSERT_TRUE(surface_screen.visible);
  harness.input.mouse_local = surface_screen.position;
  harness.input.mouse_screen = surface_screen.position;
  harness.input.modal_tool_active = true;
  const quader::ViewportInteractionFrame interaction =
      harness.frame(true, true, false, surface_screen.position);
  const quader::ViewportToolPointerLifecycleResult placed =
      harness.controller.handle_tool_pointer_lifecycle(
          harness.editor, harness.session.scene(), harness.session.assets(),
          harness.session.native_tools(), &harness.session.point_placement(),
          harness.camera,
          quader::ViewportToolPointerLifecycleFrame{
              .interaction = interaction,
              .native_tool =
                  {
                      .input = harness.input,
                      .input_result = harness.input_result,
                      .left_pressed = true,
                      .left_down = true,
                      .left_click_local = surface_screen.position,
                      .scene_snap_enabled = false,
                      .grid_snap_enabled = false,
                  },
          });
  ASSERT_TRUE(placed.native_tool.command_attempted);
  ASSERT_TRUE(placed.native_tool.command_result.ok)
      << placed.native_tool.command_result.message;

  const std::vector<quader::QuaderEntitySnapshot> entities =
      harness.editor.query().entities();
  const auto light = std::find_if(
      entities.begin(), entities.end(),
      [](const quader::QuaderEntitySnapshot &entity) {
        return entity.has_light;
      });
  ASSERT_NE(light, entities.end());
  const quader::QVec3 placed_world = quader::transform_point(
      harness.session.scene().world_transform_matrix(light->id), {});
  EXPECT_NEAR(placed_world.x, kExpectedSurfaceWorld.x, 0.001f);
  EXPECT_NEAR(placed_world.y, kExpectedSurfaceWorld.y, 0.001f);
  EXPECT_NEAR(placed_world.z, kExpectedSurfaceWorld.z, 0.001f);
  EXPECT_EQ(harness.session.history().undo_count(), undo_count_before + 1u);
}

TEST(ViewportInteractionControllerTests,
     LightCreationRejectsNonInvertibleSceneRootWithoutMutation) {
  int dirty_changed_events = 0;
  ViewportInteractionHarness harness;
  harness.camera.view_axis(quader::ViewportViewAxis::Y, 1);
  harness.input.orthographic_view = true;
  const quader::EntityId root = harness.session.scene().scene_root();
  ASSERT_NE(root, quader::kInvalidEntityId);
  ASSERT_TRUE(harness.session.scene().set_transform(
      root, quader::TransformComponent{.scale = {0.0f, 1.0f, 1.0f}}));
  harness.session.mark_clean();
  const quader::QuaderEventSubscription dirty_subscription =
      harness.editor.events().subscribe(
          quader::quader_event::kSceneDirtyChanged,
          [&](const quader::QuaderEvent &) { ++dirty_changed_events; });
  ASSERT_TRUE(dirty_subscription.valid());
  const std::size_t entity_count_before =
      harness.editor.query().entities().size();
  const std::size_t undo_count_before = harness.session.history().undo_count();

  ASSERT_TRUE(harness.editor.commands()
                  .execute(quader::quader_command::kSceneCreatePointLight)
                  .ok());
  constexpr quader::QVec3 kExpectedWorld{1.0f, 0.0f, -2.0f};
  const quader::ViewportScreenPoint expected_screen =
      harness.camera.project_world_point(kExpectedWorld);
  ASSERT_TRUE(expected_screen.visible);
  harness.input.mouse_local = expected_screen.position;
  harness.input.mouse_screen = expected_screen.position;
  harness.input.modal_tool_active = true;
  const quader::ViewportInteractionFrame interaction =
      harness.frame(true, true, false, expected_screen.position);
  const quader::ViewportToolPointerLifecycleResult rejected =
      harness.controller.handle_tool_pointer_lifecycle(
          harness.editor, harness.session.scene(), harness.session.assets(),
          harness.session.native_tools(), &harness.session.point_placement(),
          harness.camera,
          quader::ViewportToolPointerLifecycleFrame{
              .interaction = interaction,
              .native_tool =
                  {
                      .input = harness.input,
                      .input_result = harness.input_result,
                      .left_pressed = true,
                      .left_down = true,
                      .left_click_local = expected_screen.position,
                      .scene_snap_enabled = false,
                      .grid_snap_enabled = false,
                  },
          });
  ASSERT_TRUE(rejected.native_tool.command_attempted);
  EXPECT_FALSE(rejected.native_tool.command_result.ok);
  EXPECT_EQ(rejected.native_tool.command_result.message,
            "Scene root transform is not invertible.");
  EXPECT_EQ(harness.editor.query().entities().size(), entity_count_before);
  EXPECT_EQ(harness.session.history().undo_count(), undo_count_before);
  EXPECT_TRUE(harness.session.native_tools().light_creation_active());
  EXPECT_FALSE(harness.session.dirty());
  EXPECT_EQ(dirty_changed_events, 0);
  EXPECT_TRUE(harness.editor.events().unsubscribe(dirty_subscription));
}

TEST(ViewportInteractionControllerTests,
     LightCreationModeCancelsAndToolSwitchesWithoutCreating) {
  ViewportInteractionHarness harness;
  const std::size_t entity_count_before =
      harness.editor.query().entities().size();
  const std::size_t undo_count_before = harness.session.history().undo_count();

  ASSERT_TRUE(harness.editor.commands()
                  .execute(quader::quader_command::kSceneCreatePointLight)
                  .ok());
  EXPECT_TRUE(harness.session.native_tools().light_creation_active());
  ASSERT_TRUE(harness.editor.tools().cancel_active_tool_preview().ok());
  EXPECT_FALSE(harness.session.native_tools().light_creation_active());
  EXPECT_EQ(harness.editor.tools().active_tool(), "select");

  ASSERT_TRUE(harness.editor.commands()
                  .execute(
                      quader::quader_command::kSceneCreateDirectionalLight)
                  .ok());
  EXPECT_TRUE(harness.session.native_tools().light_creation_active());
  ASSERT_TRUE(
      harness.editor.tools().activate(quader::ViewportToolId::Move).ok());
  EXPECT_FALSE(harness.session.native_tools().light_creation_active());
  EXPECT_EQ(harness.editor.tools().active_tool(), "move");
  EXPECT_EQ(harness.editor.query().entities().size(), entity_count_before);
  EXPECT_EQ(harness.session.history().undo_count(), undo_count_before);
}

TEST(ViewportInteractionControllerTests,
     SelectionModifierCursorCoversModesAndFreezesMarqueeIntent) {
  using Cursor = quader::ViewportPointerCursor;
  ViewportInteractionHarness harness;
  harness.input.viewport_focused = false;
  const auto present_cursor = [&] {
    (void)harness.controller.handle_before_active_tool(harness.editor,
                                                       harness.frame());
    return harness.controller.active_tool_pointer_cursor(
        harness.router.state().active_tool);
  };

  for (const auto mode : {quader::ViewportSelectionMode::Object,
                          quader::ViewportSelectionMode::Group,
                          quader::ViewportSelectionMode::Vertex,
                          quader::ViewportSelectionMode::Edge,
                          quader::ViewportSelectionMode::Face}) {
    SCOPED_TRACE(static_cast<int>(mode));
    harness.router.set_selection_mode(mode);
    harness.input.modifiers = {};
    EXPECT_EQ(present_cursor(), Cursor::SelectionDefault);
    harness.input.modifiers = {.shift = true};
    EXPECT_EQ(present_cursor(), Cursor::SelectionAdd);
    harness.input.modifiers = {.primary = true};
    EXPECT_EQ(present_cursor(), Cursor::SelectionSubtract);
    harness.input.modifiers = {.primary = true, .shift = true};
    EXPECT_EQ(present_cursor(), Cursor::SelectionSubtract);
  }

  harness.router.set_selection_mode(quader::ViewportSelectionMode::Edge);
  harness.input.modifiers = {.primary = true, .alt = true};
  EXPECT_EQ(present_cursor(), Cursor::Default);
  harness.input.modifiers = {.shift = true, .alt = true};
  EXPECT_EQ(present_cursor(), Cursor::Default);

  harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
  harness.input.modifiers = {.primary = true};
  harness.input.cycle_selection_depth_pressed = true;
  harness.input.cycle_selection_depth_modifiers = {.shift = true};
  EXPECT_EQ(present_cursor(), Cursor::SelectionAdd);
  harness.input.cycle_selection_depth_pressed = false;
  harness.input.cycle_selection_depth_modifiers = {};

  harness.input.viewport_focused = true;
  harness.input.modifiers = {.shift = true};
  const quader::QVec2 start{120.0f, 140.0f};
  harness.input.mouse_local = start;
  harness.input.mouse_screen = start;
  const auto press = harness.frame(true, true, false, start);
  const auto before = harness.controller.handle_before_active_tool(
      harness.editor, press);
  harness.controller.handle_after_active_tool(harness.editor, press, {},
                                              before);
  EXPECT_EQ(harness.controller.active_tool_pointer_cursor(
                harness.router.state().active_tool),
            Cursor::SelectionAdd);
  harness.input.modifiers = {};
  harness.input.mouse_local = {180.0f, 200.0f};
  harness.input.mouse_screen = harness.input.mouse_local;
  EXPECT_EQ(present_cursor(), Cursor::SelectionAdd);

  harness.input.viewport_focused = false;
  EXPECT_EQ(present_cursor(), Cursor::SelectionAdd);
  harness.input.viewport_hovered = false;
  EXPECT_EQ(present_cursor(), Cursor::Default);
  harness.input.viewport_hovered = true;
  harness.input.text_input_active = true;
  EXPECT_EQ(present_cursor(), Cursor::Default);
}

TEST(ViewportInteractionControllerTests,
     CtrlPublishesBlueRemovePreviewForTheSelectedFaceUnderThePointer) {
  ViewportInteractionHarness harness;
  const quader::QuaderModelingObjectRef box = create_unit_box(harness);
  ASSERT_TRUE(harness.editor.selection().select_object(box).ok());
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Face)
                  .ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Face);
  const quader::ViewportScreenPoint front_face =
      harness.camera.project_world_point({0.0f, 0.0f, 1.0f});
  ASSERT_TRUE(front_face.visible);
  harness.input.mouse_local = front_face.position;
  harness.input.mouse_screen = front_face.position;
  ViewportSelectionWorkGateHarness work_gate(harness);

  const auto dispatch = [&]() {
    const quader::ViewportInteractionFrame frame = harness.frame();
    const auto before =
        harness.controller.handle_before_active_tool(harness.editor, frame);
    harness.controller.handle_after_active_tool(
        harness.editor, frame, {}, before, work_gate.hover_port());
  };
  dispatch();
  const std::optional<quader::ResolvedSelectionTarget> hovered =
      harness.session.selection().resolved_hover_target();
  ASSERT_TRUE(hovered.has_value());
  ASSERT_EQ(hovered->domain,
            quader::ResolvedSelectionDomain::ModelingComponent);
  ASSERT_EQ(hovered->component_kind,
            quader::ModelingActiveSelectionKind::Face);
  ASSERT_EQ(hovered->modeling_object_id, box.modeling_object_id);
  ASSERT_GE(hovered->face, 0);
  ASSERT_TRUE(harness.editor.selection()
                  .select_face(box, hovered->face)
                  .ok());

  harness.input.modifiers = {.primary = true};
  dispatch();

  const auto has_blue_remove_preview = [&]() {
    const quader::ViewportOverlayFrame overlay =
        quader::test_support::visible_modeling_overlay_frame(
            harness.session.modeling());
    if (!overlay.hover_target_selected) {
      return false;
    }
    return std::any_of(
        overlay.triangles.begin(), overlay.triangles.end(),
        [&](const quader::ViewportOverlayTriangle &triangle) {
          return triangle.layer == quader::ViewportOverlayLayer::HoverFace &&
                 triangle.element.object_id == box.modeling_object_id &&
                 triangle.element.face_id == hovered->face &&
                 triangle.color == quader::kOverlayRemoveFaceColor;
        });
  };

  EXPECT_TRUE(has_blue_remove_preview());
  EXPECT_EQ(harness.controller.active_tool_pointer_cursor(
                harness.router.state().active_tool),
            quader::ViewportPointerCursor::SelectionSubtract);
}

TEST(ViewportPointerGestureTests,
     CommonPrimitiveOwnsPressIdentityThresholdDeadlineAndCanceledRelease) {
  const quader::ViewportPointerGesture gesture{
      .press =
          {
              .button = quader::QuaderMouseButton::Left,
              .local = {10.0f, 20.0f},
              .sequence = 41u,
          },
      .deadline_seconds = 5.0,
      .drag_threshold_pixels = 4.0f,
  };

  EXPECT_FALSE(quader::viewport_pointer_drag_threshold_reached(
      gesture, {13.99f, 20.0f}));
  EXPECT_TRUE(quader::viewport_pointer_drag_threshold_reached(
      gesture, {14.0f, 20.0f}));
  EXPECT_FALSE(quader::viewport_pointer_deadline_reached(gesture, 4.99));
  EXPECT_TRUE(quader::viewport_pointer_deadline_reached(gesture, 5.0));

  const std::array edges{
      quader::ViewportPointerButtonEdge{
          .button = quader::QuaderMouseButton::Right,
          .sequence = 50u,
      },
      quader::ViewportPointerButtonEdge{
          .button = quader::QuaderMouseButton::Left,
          .sequence = 40u,
      },
      quader::ViewportPointerButtonEdge{
          .button = quader::QuaderMouseButton::Left,
          .sequence = 42u,
      },
  };
  const quader::ViewportPointerButtonEdge *release =
      quader::viewport_pointer_edge_after(edges, gesture.press);
  ASSERT_NE(release, nullptr);
  EXPECT_EQ(release->sequence, 42u);

  quader::ViewportPointerReleaseSuppression suppression;
  suppression.begin(quader::QuaderMouseButton::Left, true);
  EXPECT_TRUE(suppression.consume(quader::QuaderMouseButton::Left, false,
                                  true, false));
  EXPECT_TRUE(suppression.consume(quader::QuaderMouseButton::Left, false,
                                  false, true));
  EXPECT_FALSE(suppression.active());
  suppression.begin(quader::QuaderMouseButton::Left, true);
  EXPECT_FALSE(suppression.consume(quader::QuaderMouseButton::Left, true,
                                   true, false));
  EXPECT_FALSE(suppression.active());
}

TEST(ViewportPointerGestureTests,
     ContextMenuOwnsExactRightPressAndSuppressesForeignOrCanceledRelease) {
  ViewportInteractionHarness harness;
  const quader::QVec2 press_local{120.0f, 140.0f};
  const quader::QVec2 press_screen{320.0f, 340.0f};
  harness.input.mouse_local = press_local;
  harness.input.mouse_screen = press_screen;
  harness.input.pointer_pressed_edges = {{
      .button = quader::QuaderMouseButton::Right,
      .local = press_local,
      .sequence = 41u,
  }};
  auto press = harness.frame(false, false, false, {}, true, true, false,
                             press_local);
  press.pointer_drag_threshold_pixels = 7.0f;
  const auto pressed =
      harness.controller.handle_context_menu_pointer(press, false);
  EXPECT_FALSE(pressed.completed);
  EXPECT_TRUE(harness.controller.context_menu_pointer_active());

  harness.input.pointer_pressed_edges.clear();
  harness.input.pointer_released_edges = {{
      .button = quader::QuaderMouseButton::Left,
      .local = {600.0f, 500.0f},
      .sequence = 42u,
  }};
  const auto foreign = harness.controller.handle_context_menu_pointer(
      harness.frame(false, false, false, {}, false, true, false), false);
  EXPECT_FALSE(foreign.completed);
  EXPECT_TRUE(harness.controller.context_menu_pointer_active());

  harness.input.pointer_released_edges = {{
      .button = quader::QuaderMouseButton::Right,
      .local = press_local,
      .sequence = 40u,
  }};
  const auto stale = harness.controller.handle_context_menu_pointer(
      harness.frame(false, false, false, {}, false, false, true,
                    press_local),
      false);
  EXPECT_FALSE(stale.completed);
  EXPECT_FALSE(harness.controller.context_menu_pointer_active());

  harness.input.pointer_released_edges = {{
      .button = quader::QuaderMouseButton::Right,
      .local = press_local,
      .sequence = 43u,
  }};
  const auto orphan = harness.controller.handle_context_menu_pointer(
      harness.frame(false, false, false, {}, false, false, true,
                    press_local),
      false);
  EXPECT_FALSE(orphan.completed);

  harness.input.pointer_released_edges.clear();
  harness.input.pointer_pressed_edges = {{
      .button = quader::QuaderMouseButton::Right,
      .local = press_local,
      .sequence = 50u,
  }};
  auto matching_press = harness.frame(false, false, false, {}, true, true,
                                      false, press_local);
  matching_press.pointer_drag_threshold_pixels = 7.0f;
  (void)harness.controller.handle_context_menu_pointer(
      matching_press, false);
  harness.input.pointer_pressed_edges.clear();
  harness.input.mouse_delta = {3.0f, 4.0f};
  harness.input.mouse_screen = press_screen + harness.input.mouse_delta;
  (void)harness.controller.handle_context_menu_pointer(
      harness.frame(false, false, false, {}, false, true, false), false);
  const quader::QVec2 release_local{123.0f, 144.0f};
  harness.input.mouse_local = release_local;
  harness.input.mouse_delta = {};
  harness.input.pointer_released_edges = {{
      .button = quader::QuaderMouseButton::Right,
      .local = release_local,
      .sequence = 51u,
  }};
  const auto completed = harness.controller.handle_context_menu_pointer(
      harness.frame(false, false, false, {}, false, false, true,
                    release_local),
      false);
  EXPECT_TRUE(completed.completed);
  EXPECT_TRUE(completed.release_inside_viewport);
  EXPECT_FLOAT_EQ(completed.release_local.x, release_local.x);
  EXPECT_FLOAT_EQ(completed.release_local.y, release_local.y);
  EXPECT_FLOAT_EQ(completed.accumulated_drag_delta.x, 3.0f);
  EXPECT_FLOAT_EQ(completed.accumulated_drag_delta.y, 4.0f);
  EXPECT_FLOAT_EQ(completed.drag_threshold_pixels, 7.0f);

  harness.input.pointer_released_edges.clear();
  harness.input.pointer_pressed_edges = {{
      .button = quader::QuaderMouseButton::Right,
      .local = press_local,
      .sequence = 60u,
  }};
  (void)harness.controller.handle_context_menu_pointer(
      harness.frame(false, false, false, {}, true, true, false, press_local),
      false);
  ASSERT_TRUE(harness.controller.context_menu_pointer_active());
  harness.controller.cancel_context_menu_pointer();
  harness.input.pointer_pressed_edges.clear();
  harness.input.pointer_released_edges = {{
      .button = quader::QuaderMouseButton::Right,
      .local = press_local,
      .sequence = 61u,
  }};
  const auto canceled_release =
      harness.controller.handle_context_menu_pointer(
          harness.frame(false, false, false, {}, false, false, true,
                        press_local),
          false);
  EXPECT_FALSE(canceled_release.completed);
  EXPECT_FALSE(harness.controller.context_menu_pointer_active());
}

/// Routes real fly-camera input before resolving a synthetic right-click gesture.
struct ContextMenuNavigationHarness final : ViewportInteractionHarness {
  std::uint64_t pointer_sequence = 1u;

  quader::ViewportContextMenuPointerResult right_frame(bool pressed, bool down,
                                                       bool released) {
    input.fly_navigation_active = router.fly_navigating();
    input.navigation_active = router.navigating();
    input.right_pressed = pressed;
    input.right_down = down;
    input.right_released = released;
    input.navigation_bindings.fly_pressed = pressed;
    input.navigation_bindings.fly_down = down;
    input.navigation_bindings.fly_released = released;
    input.pointer_pressed_edges.clear();
    input.pointer_released_edges.clear();
    const quader::ViewportPointerButtonEdge edge{
        .button = quader::QuaderMouseButton::Right,
        .local = input.mouse_local,
        .sequence = pointer_sequence++,
    };
    if (pressed) {
      input.pointer_pressed_edges.push_back(edge);
    }
    if (released) {
      input.pointer_released_edges.push_back(edge);
    }
    input_result = router.handle_frame(input, camera);
    auto interaction = frame(false, false, false, {}, pressed, down, released,
                             input.mouse_local);
    interaction.pointer_drag_threshold_pixels = 7.0f;
    interaction.pointer_capture_recentered = true;
    return controller.handle_context_menu_pointer(interaction, false);
  }
};

TEST(ViewportPointerGestureTests,
     FlyMovementPermanentlyConsumesRightClickUntilTheNextPress) {
  const std::array directions{
      quader::QVec3{1.0f, 0.0f, 0.0f}, quader::QVec3{-1.0f, 0.0f, 0.0f},
      quader::QVec3{0.0f, 1.0f, 0.0f}, quader::QVec3{0.0f, -1.0f, 0.0f},
      quader::QVec3{0.0f, 0.0f, 1.0f}, quader::QVec3{0.0f, 0.0f, -1.0f},
  };
  for (const quader::QVec3 direction : directions) {
    for (const bool stop_before_release : {false, true}) {
      SCOPED_TRACE(::testing::Message()
                   << "direction " << direction.x << "," << direction.y
                   << "," << direction.z
                   << "; stop before release " << stop_before_release);
      ContextMenuNavigationHarness harness;
      const auto box = create_unit_box(harness);
      ASSERT_TRUE(harness.editor.selection()
                      .set_mode(quader::QuaderSelectionMode::Object)
                      .ok());
      harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
      ASSERT_TRUE(harness.editor.selection().select_object(box).ok());
      harness.session.history().clear();
      const auto selected_before = selected_entities(harness);
      const auto eye_before = harness.camera.pose().eye;
      harness.input.delta_seconds = 1.0f / 600.0f;

      EXPECT_FALSE(harness.right_frame(true, true, false).completed);
      ASSERT_TRUE(harness.controller.context_menu_pointer_active());
      harness.input.fly_local_direction = direction;
      EXPECT_FALSE(harness.right_frame(false, true, false).completed);
      EXPECT_GT(quader::length(harness.camera.pose().eye - eye_before),
                0.00001f);
      if (stop_before_release) {
        harness.input.fly_local_direction = {};
        EXPECT_FALSE(harness.right_frame(false, true, false).completed);
      }
      EXPECT_FALSE(harness.right_frame(false, false, true).completed);
      EXPECT_EQ(selected_entities(harness), selected_before);
      EXPECT_FALSE(harness.editor.history().can_undo());
      EXPECT_FALSE(harness.editor.history().can_redo());

      harness.input.fly_local_direction = {};
      EXPECT_FALSE(harness.right_frame(true, true, false).completed);
      const auto click = harness.right_frame(false, false, true);
      ASSERT_TRUE(click.completed);
      ASSERT_TRUE(click.release_inside_viewport);
      const auto target = harness.controller.pick_context_menu_target(
          harness.editor, harness.frame());
      EXPECT_TRUE(target.has_selection);
      EXPECT_EQ(selected_entities(harness), selected_before)
          << "the pointer still rests on the same mesh after navigation";
    }
  }
}

TEST(ViewportPointerGestureTests,
     FlyLookExcursionCannotBecomeAClickByReturningToThePressPosition) {
  for (const quader::QVec2 excursion :
       {quader::QVec2{}, quader::QVec2{3.0f, 4.0f},
        quader::QVec2{8.0f, 0.0f}}) {
    SCOPED_TRACE(::testing::Message()
                 << "excursion " << excursion.x << "," << excursion.y);
    ContextMenuNavigationHarness harness;
    EXPECT_FALSE(harness.right_frame(true, true, false).completed);
    for (int idle_frame = 0; idle_frame < 3; ++idle_frame) {
      EXPECT_FALSE(harness.right_frame(false, true, false).completed);
    }
    const float yaw_before = harness.camera.state().yaw_radians;
    harness.input.mouse_delta = excursion;
    EXPECT_FALSE(harness.right_frame(false, true, false).completed);
    if (excursion.x != 0.0f) {
      EXPECT_NE(harness.camera.state().yaw_radians, yaw_before);
    }
    harness.input.mouse_delta = excursion * -1.0f;
    EXPECT_FALSE(harness.right_frame(false, true, false).completed);
    harness.input.mouse_delta = {};
    const auto release = harness.right_frame(false, false, true);
    EXPECT_EQ(release.completed, std::hypot(excursion.x, excursion.y) <= 7.0f);
    EXPECT_FALSE(harness.right_frame(true, true, false).completed);
    EXPECT_TRUE(harness.right_frame(false, false, true).completed);
  }
}

TEST(ViewportPointerGestureTests,
     FlySpeedEditConsumesRightClickAfterWheelOrResetHasStopped) {
  for (const bool reset_speed : {false, true}) {
    SCOPED_TRACE(::testing::Message() << "reset speed " << reset_speed);
    ContextMenuNavigationHarness harness;
    if (reset_speed) {
      harness.camera.adjust_fly_speed(1.0f);
    }
    const float speed_before = harness.camera.fly_speed();
    EXPECT_FALSE(harness.right_frame(true, true, false).completed);
    harness.input.navigation_bindings.reset_fly_speed_pressed = reset_speed;
    harness.input.navigation_bindings.zoom_wheel = !reset_speed;
    harness.input.mouse_wheel = reset_speed ? 0.0f : 1.0f;
    EXPECT_FALSE(harness.right_frame(false, true, false).completed);
    EXPECT_NE(harness.camera.fly_speed(), speed_before);
    harness.input.navigation_bindings.reset_fly_speed_pressed = false;
    harness.input.navigation_bindings.zoom_wheel = false;
    harness.input.mouse_wheel = 0.0f;
    EXPECT_FALSE(harness.right_frame(false, true, false).completed);
    EXPECT_FALSE(harness.right_frame(false, false, true).completed);
    EXPECT_FALSE(harness.right_frame(true, true, false).completed);
    EXPECT_TRUE(harness.right_frame(false, false, true).completed);
  }
}

TEST(ViewportPointerGestureTests,
     DeferredSelectionClickRetainsPointerOwnershipUntilResolution) {
  ViewportInteractionHarness harness;
  (void)create_unit_box(harness);
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Object)
                  .ok());
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
  harness.camera.view_axis(quader::ViewportViewAxis::Z, 1);
  harness.input.selection_click_delay_for_double_click = true;
  harness.monotonic_time_seconds = 10.0;
  const quader::QVec2 hit{400.0f, 300.0f};
  harness.input.mouse_local = hit;
  harness.input.mouse_screen = hit;

  quader::ViewportSelectionInteraction selection;
  const quader::ViewportInteractionFrame press =
      harness.frame(true, true, false, hit);
  EXPECT_FALSE(selection.prepare_frame(harness.editor, press).has_value());
  selection.handle_frame(harness.editor, press, {}, {}, nullptr);
  EXPECT_TRUE(selection.pointer_interaction_active());

  harness.monotonic_time_seconds = 10.1;
  const quader::ViewportInteractionFrame release =
      harness.frame(false, false, true, hit);
  EXPECT_FALSE(selection.prepare_frame(harness.editor, release).has_value());
  selection.handle_frame(harness.editor, release, {}, {}, nullptr);
  EXPECT_TRUE(selection.pointer_interaction_active())
      << "the accepted single remains owned while double-click intent is "
         "pending";
  EXPECT_FALSE(harness.editor.selection().summary().has_selection);

  harness.monotonic_time_seconds = 10.6;
  const quader::ViewportInteractionFrame idle = harness.frame();
  EXPECT_FALSE(selection.prepare_frame(harness.editor, idle).has_value());
  selection.handle_frame(harness.editor, idle, {}, {}, nullptr);
  EXPECT_FALSE(selection.pointer_interaction_active());
  EXPECT_TRUE(harness.editor.selection().summary().has_selection);
}

TEST(ViewportInteractionControllerTests, RotateGizmoDragEmitsPieAngleIndicator) {
    ViewportInteractionHarness harness;
    const quader::QuaderModelingObjectRef box = create_unit_box(harness);
    ASSERT_TRUE(harness.editor.selection().select_object(box).ok);
    ASSERT_TRUE(harness.editor.tools().activate(quader::ViewportToolId::Rotate).ok);
    harness.router.set_active_tool(quader::ViewportToolId::Rotate);

    const quader::ViewportBeforeToolInteractionResult warmup =
        harness.controller.handle_before_active_tool(harness.editor,
                                                     harness.frame());
    ASSERT_FALSE(warmup.gizmo_consumed);
    const quader_editor_core::TransformGizmoFrame *frame =
        harness.controller.transform_gizmo_frame();
    ASSERT_NE(frame, nullptr);
    const quader_editor_core::TransformGizmoRingSegmentPrimitive *ring =
        find_ring(*frame, quader_editor_core::TransformAxis::Z);
    ASSERT_NE(ring, nullptr);

    const quader::QVec2 start = ring->screen_start;
    const quader::QVec2 update =
        rotated_point(frame->info.screen_pivot, start, 0.6f);
    harness.input.mouse_local = start;
    harness.input.mouse_screen = start;
    const quader::ViewportBeforeToolInteractionResult press =
        harness.controller.handle_before_active_tool(
            harness.editor, harness.frame(true, true, false, start));
    ASSERT_TRUE(press.gizmo_consumed);

    harness.input.mouse_local = update;
    harness.input.mouse_screen = update;
    const quader::ViewportBeforeToolInteractionResult drag =
        harness.controller.handle_before_active_tool(
            harness.editor, harness.frame(false, true, false, start));
    ASSERT_TRUE(drag.gizmo_consumed);

    frame = harness.controller.transform_gizmo_frame();
    ASSERT_NE(frame, nullptr);
    EXPECT_TRUE(frame->info.rotate_indicator.active);
    EXPECT_NE(frame->info.rotate_indicator.axis,
              quader_editor_core::TransformAxis::None);
    EXPECT_NE(frame->info.rotate_indicator.axis,
              quader_editor_core::TransformAxis::All);
    EXPECT_NEAR(length(frame->info.rotate_indicator.world_axis), 1.0f, 0.001f);
    ASSERT_TRUE(frame->info.rotate_indicator.has_mouse_position);
    const quader::ViewportScreenPoint projected_mouse =
        harness.camera.project_world_point(
            frame->info.rotate_indicator.mouse_position);
    ASSERT_TRUE(projected_mouse.visible);
    EXPECT_NEAR(projected_mouse.position.x, update.x, 1.0f);
    EXPECT_NEAR(projected_mouse.position.y, update.y, 1.0f);
    EXPECT_GT(std::abs(frame->info.rotate_indicator.angle_degrees), 1.0);
    const double drag_angle = frame->info.rotate_indicator.angle_degrees;

    const quader::ViewportBeforeToolInteractionResult stationary =
        harness.controller.handle_before_active_tool(
            harness.editor, harness.frame(false, true, false, start));
    ASSERT_TRUE(stationary.gizmo_consumed);
    frame = harness.controller.transform_gizmo_frame();
    ASSERT_NE(frame, nullptr);
    EXPECT_TRUE(frame->info.rotate_indicator.active);
    EXPECT_NEAR(frame->info.rotate_indicator.angle_degrees, drag_angle, 0.001);

    const quader::ViewportOverlayFrame overlay =
        build_gizmo_overlay(*frame, harness);
    EXPECT_GT(std::count_if(overlay.triangles.begin(), overlay.triangles.end(), [](const quader::ViewportOverlayTriangle& triangle) {
        return triangle.layer == quader::ViewportOverlayLayer::ToolPreviewFill &&
                triangle.color == quader::kOverlayTransformGizmoRotatePieFillColor;
    }), 0);

    const quader::ViewportBeforeToolInteractionResult release =
        harness.controller.handle_before_active_tool(
            harness.editor, harness.frame(false, false, true, start));
    EXPECT_TRUE(release.gizmo_consumed);
}

TEST(ViewportInteractionControllerTests,
     RotateTrackballDragCommitsOnceAndRetainsStationaryPreview) {
  ViewportInteractionHarness harness;
  const auto cursor = [&] {
    return harness.controller.active_tool_pointer_cursor(harness.router.state().active_tool);
  };
  const auto box = create_unit_box(harness);
  ASSERT_TRUE(harness.editor.selection().select_object(box).ok());
  ASSERT_TRUE(
      harness.editor.tools().activate(quader::ViewportToolId::Rotate).ok());
  harness.router.set_active_tool(quader::ViewportToolId::Rotate);
  harness.session.history().clear();
  (void)harness.dispatch_frame(harness.frame());
  const auto *gizmo = harness.controller.transform_gizmo_frame();
  ASSERT_NE(gizmo, nullptr);
  const quader::QVec2 start =
      gizmo->info.screen_pivot + quader::QVec2{25.0f, 25.0f};
  harness.input.mouse_local = start;
  harness.input.mouse_screen = start;
  (void)harness.dispatch_frame(harness.frame());
  gizmo = harness.controller.transform_gizmo_frame();
  ASSERT_NE(gizmo, nullptr);
  ASSERT_EQ(gizmo->info.hover_axis, quader_editor_core::TransformAxis::All);
  EXPECT_EQ(cursor(), quader::ViewportPointerCursor::OpenHand);
  const quader::TransformComponent before =
      *harness.session.scene().transform(box.entity);
  const quader::QMat4 before_matrix =
      harness.session.scene().world_transform_matrix(box.entity);

  const auto press =
      harness.dispatch_frame(harness.frame(true, true, false, start));
  ASSERT_TRUE(press.gizmo_consumed);
  EXPECT_FALSE(press.selection_click_requested);
  EXPECT_FALSE(harness.controller.dragging());
  EXPECT_EQ(cursor(), quader::ViewportPointerCursor::OpenHand);
  harness.input.mouse_local = start + quader::QVec2{1.0f, 1.0f};
  harness.input.mouse_screen = harness.input.mouse_local;
  EXPECT_TRUE(harness.dispatch_frame(harness.frame(false, true, false, start))
                  .gizmo_consumed);
  EXPECT_FALSE(harness.controller.dragging());
  EXPECT_EQ(cursor(), quader::ViewportPointerCursor::OpenHand);
  EXPECT_EQ(harness.session.scene().world_transform_matrix(box.entity).values,
            before_matrix.values);

  harness.input.mouse_local = start + quader::QVec2{70.0f, -40.0f};
  harness.input.mouse_screen = harness.input.mouse_local;
  ASSERT_TRUE(harness.dispatch_frame(harness.frame(false, true, false, start))
                  .gizmo_consumed);
  ASSERT_TRUE(harness.controller.dragging());
  gizmo = harness.controller.transform_gizmo_frame();
  ASSERT_NE(gizmo, nullptr);
  EXPECT_EQ(gizmo->info.active_axis, quader_editor_core::TransformAxis::All);
  EXPECT_EQ(cursor(), quader::ViewportPointerCursor::ClosedHand);
  EXPECT_FALSE(gizmo->info.rotate_indicator.active);
  const quader::QMat4 preview_matrix =
      harness.session.scene().world_transform_matrix(box.entity);
  EXPECT_NE(preview_matrix.values, before_matrix.values);
  const quader::TransformComponent preview =
      *harness.session.scene().transform(box.entity);
  const auto content_revision = harness.session.modeling().content_revision();
  const auto scene_revision = harness.session.scene().revision();
  const auto asset_revision = harness.session.assets().revision();
  for (int frame_index = 0; frame_index < 3; ++frame_index) {
    SCOPED_TRACE(frame_index);
    EXPECT_TRUE(harness.dispatch_frame(harness.frame(false, true, false, start))
                    .gizmo_consumed);
    EXPECT_EQ(cursor(), quader::ViewportPointerCursor::ClosedHand);
    EXPECT_EQ(harness.session.modeling().content_revision(), content_revision);
    EXPECT_EQ(harness.session.scene().revision(), scene_revision);
    EXPECT_EQ(harness.session.assets().revision(), asset_revision);
    EXPECT_EQ(harness.session.scene().world_transform_matrix(box.entity).values,
              preview_matrix.values);
  }

  const auto release =
      harness.dispatch_frame(harness.frame(false, false, true, start));
  EXPECT_TRUE(release.gizmo_consumed);
  EXPECT_FALSE(release.selection_click_requested);
  EXPECT_FALSE(harness.controller.dragging());
  EXPECT_NE(cursor(), quader::ViewportPointerCursor::ClosedHand);
  expect_transform_near(*harness.session.scene().transform(box.entity), preview);
  EXPECT_EQ(selected_entities(harness),
            std::vector<quader::EntityId>{box.entity});
  ASSERT_TRUE(harness.editor.history().undo().ok());
  EXPECT_FALSE(harness.editor.history().can_undo());
  expect_transform_near(*harness.session.scene().transform(box.entity), before);
  ASSERT_TRUE(harness.editor.history().redo().ok());
  expect_transform_near(*harness.session.scene().transform(box.entity), preview);
}

TEST(ViewportInteractionControllerTests,
     RotateTrackballEscapeRestoresTransformWithoutSelectionFallback) {
  ViewportInteractionHarness harness;
  const auto box = create_unit_box(harness);
  ASSERT_TRUE(harness.editor.selection().select_object(box).ok());
  ASSERT_TRUE(
      harness.editor.tools().activate(quader::ViewportToolId::Rotate).ok());
  harness.router.set_active_tool(quader::ViewportToolId::Rotate);
  harness.session.history().clear();
  (void)harness.dispatch_frame(harness.frame());
  const auto *gizmo = harness.controller.transform_gizmo_frame();
  ASSERT_NE(gizmo, nullptr);
  const quader::QVec2 start =
      gizmo->info.screen_pivot + quader::QVec2{25.0f, 25.0f};
  const quader::TransformComponent before =
      *harness.session.scene().transform(box.entity);
  harness.input.mouse_local = start;
  harness.input.mouse_screen = start;
  ASSERT_TRUE(harness.dispatch_frame(harness.frame(true, true, false, start))
                  .gizmo_consumed);
  harness.input.mouse_local = start + quader::QVec2{70.0f, -40.0f};
  harness.input.mouse_screen = harness.input.mouse_local;
  ASSERT_TRUE(harness.dispatch_frame(harness.frame(false, true, false, start))
                  .gizmo_consumed);
  ASSERT_TRUE(harness.controller.dragging());
  gizmo = harness.controller.transform_gizmo_frame();
  ASSERT_NE(gizmo, nullptr);
  ASSERT_EQ(gizmo->info.active_axis, quader_editor_core::TransformAxis::All);
  EXPECT_GT(length(harness.session.scene().transform(box.entity)->rotation_euler -
                   before.rotation_euler),
            0.001f);

  harness.input_result.cancel_requested = true;
  (void)harness.dispatch_frame(harness.frame(false, true, false, start));
  harness.input_result.cancel_requested = false;
  EXPECT_FALSE(harness.controller.dragging());
  EXPECT_FALSE(harness.session.modeling().transform_drag_active());
  expect_transform_near(*harness.session.scene().transform(box.entity), before);
  const auto release =
      harness.dispatch_frame(harness.frame(false, false, true, start));
  EXPECT_FALSE(release.selection_click_requested);
  EXPECT_EQ(selected_entities(harness),
            std::vector<quader::EntityId>{box.entity});
  EXPECT_FALSE(harness.editor.history().can_undo());
}

TEST(ViewportInteractionControllerTests,
     ShortRotateTrackballClickSelectsObjectBehindHandleWithoutRotation) {
  ViewportInteractionHarness harness;
  const auto back = create_box_at(harness, {0.0f, 0.0f, -2.0f});
  const auto front = create_box_at(harness, {0.0f, 0.0f, 2.0f});
  ASSERT_TRUE(harness.editor.selection().select_object(back).ok());
  ASSERT_TRUE(
      harness.editor.tools().activate(quader::ViewportToolId::Rotate).ok());
  harness.router.set_active_tool(quader::ViewportToolId::Rotate);
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
  harness.session.history().clear();
  (void)harness.dispatch_frame(harness.frame());
  const auto *gizmo = harness.controller.transform_gizmo_frame();
  ASSERT_NE(gizmo, nullptr);
  const quader::QVec2 click =
      gizmo->info.screen_pivot + quader::QVec2{25.0f, 25.0f};
  const quader::QMat4 back_before =
      harness.session.scene().world_transform_matrix(back.entity);
  const quader::QMat4 front_before =
      harness.session.scene().world_transform_matrix(front.entity);
  harness.input.mouse_local = click;
  harness.input.mouse_screen = click;
  (void)harness.dispatch_frame(harness.frame());
  gizmo = harness.controller.transform_gizmo_frame();
  ASSERT_NE(gizmo, nullptr);
  ASSERT_EQ(gizmo->info.hover_axis, quader_editor_core::TransformAxis::All);
  const auto press =
      harness.dispatch_frame(harness.frame(true, true, false, click));
  ASSERT_TRUE(press.gizmo_consumed);
  EXPECT_FALSE(press.selection_click_requested);
  EXPECT_FALSE(harness.controller.dragging());
  EXPECT_EQ(selected_entities(harness),
            std::vector<quader::EntityId>{back.entity});

  const auto release =
      harness.dispatch_frame(harness.frame(false, false, true, click));
  EXPECT_FALSE(release.gizmo_consumed);
  EXPECT_TRUE(release.selection_click_requested);
  EXPECT_FALSE(harness.controller.dragging());
  EXPECT_FALSE(harness.session.modeling().transform_drag_active());
  EXPECT_EQ(selected_entities(harness),
            std::vector<quader::EntityId>{front.entity});
  EXPECT_EQ(harness.session.scene().world_transform_matrix(back.entity).values,
            back_before.values);
  EXPECT_EQ(harness.session.scene().world_transform_matrix(front.entity).values,
            front_before.values);
}

TEST(ViewportInteractionControllerTests, ClickFrameSelectsComponentThroughViewportFallback) {
    ViewportInteractionHarness harness;
    (void)create_unit_box(harness);

    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Face)
                    .ok);
    ASSERT_TRUE(harness.editor.selection().clear().ok);
    harness.router.set_selection_mode(quader::ViewportSelectionMode::Face);

    const quader::ViewportBeforeToolInteractionResult before_tool =
        harness.controller.handle_before_active_tool(
            harness.editor, harness.frame(true, true, false, {400.0f, 300.0f}));
    EXPECT_FALSE(before_tool.gizmo_consumed);
    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(true, true, false, {400.0f, 300.0f}),
        quader::ViewportToolResult{}, before_tool);
    EXPECT_EQ(harness.editor.selection().summary().selected_face_count, 0u);

    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(false, false, true, {400.0f, 300.0f}),
        quader::ViewportToolResult{},
        quader::ViewportBeforeToolInteractionResult{});

    const quader::QuaderSelectionSummary selection = harness.editor.selection().summary();
    EXPECT_EQ(selection.mode, quader::QuaderSelectionMode::Face);
    EXPECT_EQ(selection.selected_face_count, 1u);
    EXPECT_EQ(selection.active_kind, quader::QuaderActiveSelectionKind::Face);
    EXPECT_EQ(harness.router.state().selection_mode,
              quader::ViewportSelectionMode::Face);
    EXPECT_EQ(harness.editor.selection().hover_summary().target.kind,
              quader::QuaderActiveSelectionKind::None);

    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(), quader::ViewportToolResult{},
        quader::ViewportBeforeToolInteractionResult{});
    EXPECT_EQ(harness.editor.selection().hover_summary().target.kind,
              quader::QuaderActiveSelectionKind::None);
}

TEST(ViewportInteractionControllerTests,
     ClickFrameSelectsPointLightBillboardThroughViewportFallback) {
    ViewportInteractionHarness harness;
    quader::QuaderCreatePointLightArgs args;
    args.name = "Viewport Billboard Point Light";
    const quader::QuaderApiResult<quader::QuaderEntityRef> light =
        harness.editor.scene().create_point_light(args);
    ASSERT_TRUE(light.ok()) << light.error_message();

    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Object)
                    .ok);
    ASSERT_TRUE(harness.editor.selection().clear().ok);
    harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);

    const quader::ViewportScreenPoint projected =
        harness.camera.project_world_point({});
    ASSERT_TRUE(projected.visible);
    const quader::QVec2 hit =
        projected.position +
        quader::QVec2{quader::kOverlayPointLightGizmoSizePixels * 0.45f, 0.0f};
    harness.input.mouse_local = hit;
    harness.input.mouse_screen = hit;
    harness.billboard_pick_candidates.push_back({
        .entity = light.value().id,
        .position = {},
        .size_pixels = quader::kOverlayPointLightGizmoSizePixels,
    });

    const quader::ViewportBeforeToolInteractionResult before_tool =
        harness.controller.handle_before_active_tool(
            harness.editor, harness.frame(true, true, false, hit));
    EXPECT_FALSE(before_tool.gizmo_consumed);
    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(true, true, false, hit),
        quader::ViewportToolResult{}, before_tool);
    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(false, false, true, hit),
        quader::ViewportToolResult{},
        quader::ViewportBeforeToolInteractionResult{});

    const std::vector<quader::EntityId> selected = selected_entities(harness);
    ASSERT_EQ(selected.size(), 1u);
    EXPECT_EQ(selected.front(), light.value().id);
    EXPECT_EQ(harness.editor.selection().summary().mode,
              quader::QuaderSelectionMode::Object);
  EXPECT_EQ(harness.router.state().selection_mode,
              quader::ViewportSelectionMode::Object);
}

TEST(ViewportInteractionControllerTests,
     SceneHitPolicyArbitratesBillboardAndMeshByDepthBeforeProviderKind) {
  ViewportInteractionHarness harness;
  const quader::QuaderModelingObjectRef box = create_unit_box(harness);
  const auto light = harness.editor.scene().create_point_light({
      .name = "Depth Arbitrated Light",
  });
  ASSERT_TRUE(light.ok()) << light.error_message();
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Object)
                  .ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
  const quader::QVec2 hit{400.0f, 300.0f};

  harness.billboard_pick_candidates = {{
      .entity = light.value().id,
      .position = {0.0f, 0.0f, -2.0f},
      .size_pixels = quader::kOverlayPointLightGizmoSizePixels,
  }};
  harness.click_selection(hit);
  EXPECT_EQ(selected_entities(harness),
            std::vector<quader::EntityId>{box.entity});

  harness.billboard_pick_candidates.front().position.z = 2.0f;
  harness.click_selection(hit);
  EXPECT_EQ(selected_entities(harness),
            std::vector<quader::EntityId>{light.value().id});

  // Exact surface ties resolve by the documented semantic tie priority.
  harness.billboard_pick_candidates.front().position.z = 1.0f;
  harness.click_selection(hit);
  EXPECT_EQ(selected_entities(harness),
            std::vector<quader::EntityId>{light.value().id});
}

TEST(ViewportInteractionControllerTests,
     LockedAndNonselectableForegroundMeshesOccludeAllSceneHitProviders) {
  ViewportInteractionHarness harness;
  const quader::QuaderModelingObjectRef back =
      create_box_at(harness, {0.0f, 0.0f, -3.0f});
  const quader::QuaderModelingObjectRef front =
      create_box_at(harness, {0.0f, 0.0f, 2.0f});
  const auto light = harness.editor.scene().create_point_light({
      .name = "Occluded Light",
  });
  ASSERT_TRUE(light.ok()) << light.error_message();
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Object)
                  .ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
  harness.billboard_pick_candidates = {{
      .entity = light.value().id,
      .position = {0.0f, 0.0f, -1.0f},
      .size_pixels = quader::kOverlayPointLightGizmoSizePixels,
  }};
  const quader::QVec2 hit{400.0f, 300.0f};

  ASSERT_TRUE(harness.editor.scene().set_locked(front.entity, true).ok());
  harness.click_selection(hit);
  EXPECT_TRUE(selected_entities(harness).empty());

  ASSERT_TRUE(harness.editor.scene().set_locked(front.entity, false).ok());
  ASSERT_TRUE(harness.session.scene().set_selectable(front.entity, false));
  harness.click_selection(hit);
  EXPECT_TRUE(selected_entities(harness).empty());

  ASSERT_TRUE(harness.editor.scene().set_visible(front.entity, false).ok());
  harness.billboard_pick_candidates.clear();
  harness.click_selection(hit);
  EXPECT_EQ(selected_entities(harness),
            std::vector<quader::EntityId>{back.entity});
}

TEST(ViewportInteractionControllerTests,
     SceneHitPolicyReturnsFormalOccurrenceIdentityBeforeSelectionPromotion) {
  ViewportInteractionHarness harness;
  const quader::QuaderModelingObjectRef source = create_unit_box(harness);
  ASSERT_TRUE(harness.editor.selection().select_object(source).ok());
  const auto instance =
      harness.editor.modeling().create_instance_from_selection();
  ASSERT_TRUE(instance.ok()) << instance.error_message();
  const quader::EntityId occurrence = instance.value().occurrence.root.id;
  ASSERT_NE(occurrence, quader::kInvalidEntityId);
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Object)
                  .ok());
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);

  harness.click_selection({400.0f, 300.0f});
  EXPECT_EQ(selected_entities(harness),
            std::vector<quader::EntityId>{occurrence});
}

TEST(ViewportInteractionControllerTests,
     SceneHitPolicyUsesTheSameLocalVisibilitySnapshotAsRendering) {
  ViewportInteractionHarness harness;
  const quader::QuaderModelingObjectRef local =
      create_box_at(harness, {-3.0f, 0.0f, 0.0f});
  const quader::QuaderModelingObjectRef hidden =
      create_box_at(harness, {3.0f, 0.0f, 0.0f});
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Object)
                  .ok());
  ASSERT_TRUE(harness.editor.selection().select_object(local).ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
  const auto entered = harness.editor.viewports().toggle_local_visibility();
  ASSERT_TRUE(entered.ok()) << entered.error_message();

  const quader::ViewportScreenPoint hidden_screen =
      harness.camera.project_world_point({3.0f, 0.0f, 0.0f});
  ASSERT_TRUE(hidden_screen.visible);
  harness.click_selection(hidden_screen.position);
  EXPECT_TRUE(selected_entities(harness).empty());
  EXPECT_FALSE(harness.editor.query().entity(hidden.entity)->selected);

  const quader::ViewportScreenPoint local_screen =
      harness.camera.project_world_point({-3.0f, 0.0f, 0.0f});
  ASSERT_TRUE(local_screen.visible);
  harness.click_selection(local_screen.position);
  EXPECT_EQ(selected_entities(harness),
            std::vector<quader::EntityId>{local.entity});
}

TEST(ViewportInteractionControllerTests,
     GroupModeHoverUsesTheSharedResolvedHitInsteadOfClearingEarly) {
  ViewportInteractionHarness harness;
  const quader::QuaderModelingObjectRef box = create_unit_box(harness);
  ASSERT_TRUE(harness.editor.selection().select_object(box).ok());
  const auto group = harness.editor.scene().group_selection("Hover Group");
  ASSERT_TRUE(group.ok()) << group.error_message();
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Group)
                  .ok());
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Group);
  harness.input.mouse_local = {400.0f, 300.0f};
  harness.input.mouse_screen = harness.input.mouse_local;

  const quader::ViewportInteractionFrame idle = harness.frame();
  const auto before =
      harness.controller.handle_before_active_tool(harness.editor, idle);
  harness.controller.handle_after_active_tool(harness.editor, idle, {}, before);

  const auto resolved = harness.session.selection().resolved_hover_target();
  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(resolved->entity, group.value().id);
}

TEST(ViewportInteractionControllerTests,
     HoverArbitratesBillboardCurveAndMeshWithTheClickDepthPolicy) {
  ViewportInteractionHarness harness;
  (void)create_unit_box(harness);
  const auto curve = harness.editor.modeling().create_curve({
      .points = {{.position = {-2.0f, 0.0f, 2.0f}},
                 {.position = {2.0f, 0.0f, 2.0f}}},
  });
  ASSERT_TRUE(curve.ok()) << curve.error_message();
  const auto light = harness.editor.scene().create_point_light({
      .name = "Hover Depth Light",
  });
  ASSERT_TRUE(light.ok()) << light.error_message();
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Object)
                  .ok());
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
  harness.input.mouse_local = {400.0f, 300.0f};
  harness.input.mouse_screen = harness.input.mouse_local;
  harness.billboard_pick_candidates = {{
      .entity = light.value().id,
      .position = {0.0f, 0.0f, 3.0f},
      .size_pixels = quader::kOverlayPointLightGizmoSizePixels,
  }};

  auto idle = harness.frame();
  auto before =
      harness.controller.handle_before_active_tool(harness.editor, idle);
  harness.controller.handle_after_active_tool(harness.editor, idle, {}, before);
  ASSERT_TRUE(harness.session.selection().resolved_hover_target().has_value());
  EXPECT_EQ(harness.session.selection().resolved_hover_target()->entity,
            light.value().id);

  harness.billboard_pick_candidates.front().position.z = -2.0f;
  const auto next_idle = harness.frame();
  before =
      harness.controller.handle_before_active_tool(harness.editor, next_idle);
  harness.controller.handle_after_active_tool(harness.editor, next_idle, {},
                                              before);
  ASSERT_TRUE(harness.session.selection().resolved_hover_target().has_value());
  EXPECT_EQ(harness.session.selection().resolved_hover_target()->entity,
            curve.value().entity);
}

TEST(ViewportInteractionControllerTests,
     PublicModelingHoverPreviewAndCommitShareResolvedProviderAuthority) {
  ViewportInteractionHarness harness;
  const quader::QuaderModelingObjectRef box = create_unit_box(harness);
  const auto curve = harness.editor.modeling().create_curve({
      .points = {{.position = {-2.0f, 0.0f, 2.0f}},
                 {.position = {2.0f, 0.0f, 2.0f}}},
  });
  ASSERT_TRUE(curve.ok()) << curve.error_message();
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Object)
                  .ok());
  ASSERT_TRUE(harness.editor.selection().select_object(curve.value()).ok());
  ASSERT_TRUE(harness.session.curve().refresh().ok);
  ASSERT_TRUE(harness.session.curve().controls_editable());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);

  const quader::ViewportScreenPoint anchor =
      harness.camera.project_world_point({-2.0f, 0.0f, 2.0f});
  ASSERT_TRUE(anchor.visible);
  harness.input.mouse_local = anchor.position;
  harness.input.mouse_screen = anchor.position;
  const quader::ViewportInteractionFrame idle = harness.frame();
  const auto before = harness.controller.handle_before_active_tool(
      harness.editor, idle, harness.session.native_tools());
  harness.controller.handle_after_active_tool(
      harness.editor, idle, {}, before, harness.session.native_tools(),
      nullptr);

  const auto viewport_hover =
      harness.session.selection().resolved_hover_target();
  ASSERT_TRUE(viewport_hover.has_value());
  ASSERT_EQ(viewport_hover->domain,
            quader::ResolvedSelectionDomain::RegisteredSubElement);
  ASSERT_EQ(viewport_hover->entity, curve.value().entity);
  ASSERT_TRUE(viewport_hover->sub_element.has_value());
  const quader::EditorSubSelectionBaseline provider_hover =
      harness.session.curve().capture_sub_selection_baseline();
  ASSERT_TRUE(provider_hover.valid());
  ASSERT_EQ(provider_hover.hover, viewport_hover->sub_element);

  const quader::QuaderHoverRequest public_request{
      .ray = {.origin = {0.0f, 0.0f, 5.0f},
              .direction = {0.0f, 0.0f, -1.0f}},
      .face_plane_hover = true,
  };
  const quader::QuaderHoverSummary preview =
      harness.editor.selection().preview_hover(public_request);
  ASSERT_EQ(preview.target.kind, quader::QuaderActiveSelectionKind::Face);
  EXPECT_EQ(preview.target.object_id, box.modeling_object_id);
  EXPECT_EQ(harness.session.selection().resolved_hover_target(),
            viewport_hover);
  EXPECT_EQ(harness.session.curve().capture_sub_selection_baseline().hover,
            provider_hover.hover);

  const auto committed = harness.editor.selection().hover(public_request);
  ASSERT_TRUE(committed.ok()) << committed.error_message();
  ASSERT_EQ(committed.value().target.kind,
            quader::QuaderActiveSelectionKind::Face);
  EXPECT_EQ(committed.value().target.object_id, box.modeling_object_id);
  const auto resolved = harness.session.selection().resolved_hover_target();
  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(resolved->domain,
            quader::ResolvedSelectionDomain::ModelingComponent);
  EXPECT_EQ(resolved->entity, box.entity);
  EXPECT_EQ(resolved->modeling_object_id, box.modeling_object_id);
  EXPECT_EQ(resolved->component_kind,
            quader::ModelingActiveSelectionKind::Face);
  EXPECT_FALSE(
      harness.session.curve().capture_sub_selection_baseline().hover.has_value());
}

TEST(ViewportInteractionControllerTests,
     ResolvedClickFailureKeepsSelectionAndReturnsCheckedFailure) {
  ViewportInteractionHarness harness;
  const quader::QuaderModelingObjectRef selected =
      create_box_at(harness, {3.0f, 0.0f, 0.0f});
  ASSERT_TRUE(harness.editor.selection().select_object(selected).ok());
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Object)
                  .ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
  const quader::QVec2 hit{400.0f, 300.0f};
  harness.input.mouse_local = hit;
  harness.input.mouse_screen = hit;
  harness.billboard_pick_candidates = {{
      .entity = 987654u,
      .position = {},
      .size_pixels = quader::kOverlayPointLightGizmoSizePixels,
  }};

  quader::ViewportSelectionInteraction interaction;
  const quader::ViewportInteractionFrame press =
      harness.frame(true, true, false, hit);
  interaction.handle_frame(harness.editor, press, {}, {}, nullptr);
  const quader::ViewportInteractionFrame release =
      harness.frame(false, false, true, hit);
  interaction.handle_frame(harness.editor, release, {}, {}, nullptr);

  EXPECT_FALSE(interaction.last_selection_command_result().ok);
  EXPECT_FALSE(interaction.last_selection_command_result().message.empty());
  EXPECT_EQ(selected_entities(harness),
            std::vector<quader::EntityId>{selected.entity});
  EXPECT_EQ(harness.router.state().selection_mode,
            quader::ViewportSelectionMode::Object);
  EXPECT_FALSE(interaction.pointer_interaction_active());
  EXPECT_FALSE(harness.session.selection().resolved_hover_target().has_value());
}

TEST(ViewportInteractionControllerTests,
     ClickAndMarqueeMissesClearThroughTheSameResolvedOwnerPolicy) {
  ViewportInteractionHarness harness;
  const quader::QuaderModelingObjectRef box = create_unit_box(harness);
  ASSERT_TRUE(harness.editor.selection().select_object(box).ok());
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Object)
                  .ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);

  harness.click_selection({20.0f, 20.0f});
  EXPECT_TRUE(selected_entities(harness).empty());
  ASSERT_TRUE(harness.editor.selection().select_object(box).ok());

  const quader::QVec2 start{20.0f, 20.0f};
  const quader::QVec2 end{80.0f, 80.0f};
  harness.input.mouse_local = start;
  harness.input.mouse_screen = start;
  auto press = harness.frame(true, true, false, start);
  auto before =
      harness.controller.handle_before_active_tool(harness.editor, press);
  harness.controller.handle_after_active_tool(harness.editor, press, {}, before);
  harness.input.mouse_local = end;
  harness.input.mouse_screen = end;
  harness.controller.handle_after_active_tool(
      harness.editor, harness.frame(false, true, false, start), {}, {});
  harness.controller.handle_after_active_tool(
      harness.editor, harness.frame(false, false, true, start), {}, {});
  EXPECT_TRUE(selected_entities(harness).empty());
}

TEST(ViewportInteractionControllerTests,
     CurveStrokeClickUsesPixelRadiusSelectionModifiersAndHistory) {
  ViewportInteractionHarness harness;
  const auto box = create_unit_box(harness);
  const auto curve = harness.editor.modeling().create_curve({
      .points = {{.position = {-2.0f, 0.0f, 0.0f}},
                 {.position = {2.0f, 0.0f, 0.0f}}},
  });
  ASSERT_TRUE(curve.ok()) << curve.error_message();
  ASSERT_TRUE(harness.editor.selection().set_mode(quader::QuaderSelectionMode::Object).ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
  ASSERT_TRUE(harness.editor.selection().select_object(box).ok());
  const auto projected = harness.camera.project_world_point({1.6f, 0.0f, 0.0f});
  ASSERT_TRUE(projected.visible);
  const quader::QVec2 hit = projected.position + quader::QVec2{0.0f, 6.0f};

  harness.input.modifiers.shift = true;
  harness.click_selection(hit);
  EXPECT_EQ(selected_entities(harness).size(), 2u);
  EXPECT_TRUE(harness.editor.query().entity(curve.value().entity)->selected);
  ASSERT_TRUE(harness.editor.history().undo().ok());
  EXPECT_EQ(selected_entities(harness), std::vector<quader::EntityId>{box.entity});
  ASSERT_TRUE(harness.editor.history().redo().ok());
  EXPECT_EQ(selected_entities(harness).size(), 2u);

  harness.input.modifiers = {.primary = true};
  harness.click_selection(hit);
  EXPECT_EQ(selected_entities(harness), std::vector<quader::EntityId>{box.entity});
  harness.input.modifiers = {};
  harness.click_selection(hit);
  EXPECT_EQ(selected_entities(harness),
            std::vector<quader::EntityId>{curve.value().entity});

  harness.click_selection(projected.position + quader::QVec2{0.0f, 12.0f});
  EXPECT_TRUE(selected_entities(harness).empty());
  harness.input_result.navigation_active = true;
  harness.click_selection(hit);
  EXPECT_TRUE(selected_entities(harness).empty());
}

TEST(ViewportInteractionControllerTests,
     CurveStrokeClickArbitratesMeshDepthAndDefersComponentPickingToMeshes) {
  ViewportInteractionHarness harness;
  const auto box = create_unit_box(harness);
  const auto behind = harness.editor.modeling().create_curve({
      .points = {{.position = {-2.0f, 0.0f, -2.0f}},
                 {.position = {2.0f, 0.0f, -2.0f}}},
  });
  ASSERT_TRUE(behind.ok()) << behind.error_message();
  ASSERT_TRUE(harness.editor.selection().set_mode(quader::QuaderSelectionMode::Object).ok());
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
  harness.click_selection({400.0f, 300.0f});
  EXPECT_EQ(selected_entities(harness), std::vector<quader::EntityId>{box.entity});

  const auto front = harness.editor.modeling().create_curve({
      .points = {{.position = {-2.0f, 0.02f, 2.0f}},
                 {.position = {2.0f, 0.02f, 2.0f}}},
  });
  ASSERT_TRUE(front.ok()) << front.error_message();
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  // The rear stroke is closer in pixels; depth arbitration must retain the
  // slightly offset stroke in front of the mesh instead of falling back early.
  harness.click_selection({400.0f, 300.0f});
  EXPECT_EQ(selected_entities(harness),
            std::vector<quader::EntityId>{front.value().entity});

  ASSERT_TRUE(harness.editor.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Face);
  harness.click_selection({400.0f, 300.0f});
  EXPECT_EQ(harness.editor.selection().summary().selected_face_count, 1u);
  EXPECT_EQ(harness.editor.selection().summary().mode, quader::QuaderSelectionMode::Face);
}

TEST(ViewportInteractionControllerTests,
     CurveStrokeClickPromotesNearestGroup) {
  ViewportInteractionHarness harness;
  const auto curve = harness.editor.modeling().create_curve({
      .points = {{.position = {-2.0f, 0.0f, 0.0f}},
                 {.position = {2.0f, 0.0f, 0.0f}}},
  });
  ASSERT_TRUE(curve.ok()) << curve.error_message();
  ASSERT_TRUE(harness.editor.selection().select_object(curve.value()).ok());
  const auto inner = harness.editor.scene().group_selection("Curve Inner");
  ASSERT_TRUE(inner.ok()) << inner.error_message();
  const auto outer = harness.editor.scene().group_selection("Curve Outer");
  ASSERT_TRUE(outer.ok()) << outer.error_message();
  ASSERT_TRUE(harness.editor.selection().set_mode(quader::QuaderSelectionMode::Group).ok());
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Group);
  harness.click_selection({400.0f, 300.0f});
  EXPECT_EQ(selected_entities(harness), std::vector<quader::EntityId>{inner.value().id});
  EXPECT_EQ(harness.router.state().selection_mode, quader::ViewportSelectionMode::Group);
}

TEST(ViewportCurvePickingTests,
     ClippedPerspectiveSegmentsRetainWorldDepthAndRejectOutsidePixels) {
  ViewportInteractionHarness harness;
  const auto curve = harness.editor.modeling().create_curve({
      .points = {{.position = {-2.0f, 0.0f, 10.0f}},
                 {.position = {2.0f, 0.0f, -2.0f}}},
  });
  ASSERT_TRUE(curve.ok()) << curve.error_message();
  const auto candidates = harness.frame().scene_curve_pick_candidates;
  ASSERT_EQ(candidates.size(), 1u);
  const auto hit = quader::pick_viewport_curve(harness.camera, {400.0f, 300.0f}, candidates);
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->entity, curve.value().entity);
  EXPECT_NEAR(hit->world_position.x, 0.0f, 1.0e-4f);
  EXPECT_NEAR(hit->world_position.z, 4.0f, 1.0e-4f);
  EXPECT_NEAR(hit->ray_distance, 4.0, 1.0e-4);
  EXPECT_FALSE(quader::pick_viewport_curve(
      harness.camera, {400.0f, 300.0f}, candidates, 8.0, 3.9).has_value());
  EXPECT_TRUE(quader::pick_viewport_curve(
      harness.camera, {400.0f, 300.0f}, candidates, 8.0, 4.1).has_value());
  EXPECT_FALSE(quader::pick_viewport_curve(
      harness.camera, {400.0f, 312.0f}, candidates).has_value());
  EXPECT_FALSE(quader::pick_viewport_curve(
      harness.camera, {-1.0f, 300.0f}, candidates).has_value());
  EXPECT_FALSE(quader::pick_viewport_curve(
      harness.camera, {400.0f, 300.0f}, candidates, 8.0,
      std::numeric_limits<double>::quiet_NaN()).has_value());

  harness.camera.set_clip_planes(0.05f, 3.0f);
  EXPECT_FALSE(quader::pick_viewport_curve(
      harness.camera, {400.0f, 300.0f}, candidates).has_value());
}

TEST(ViewportCurvePickingTests,
     CurveIntersectionsChooseNearestDepthInPerspectiveAndOrthographicViews) {
  ViewportInteractionHarness harness;
  const auto far = harness.editor.modeling().create_curve({
      .points = {{.position = {-20.0f, 0.0f, -2.0f}},
                 {.position = {20.0f, 0.0f, -2.0f}}},
  });
  ASSERT_TRUE(far.ok()) << far.error_message();
  const auto near = harness.editor.modeling().create_curve({
      .points = {{.position = {-20.0f, 0.0f, 2.0f}},
                 {.position = {20.0f, 0.0f, 2.0f}}},
  });
  ASSERT_TRUE(near.ok()) << near.error_message();
  const auto candidates = harness.frame().scene_curve_pick_candidates;
  ASSERT_EQ(candidates.size(), 2u);
  for (const bool orthographic : {false, true}) {
    if (orthographic) {
      harness.camera.view_axis(quader::ViewportViewAxis::Z, 1);
    }
    const auto hit = quader::pick_viewport_curve(
        harness.camera, {400.0f, 300.0f}, candidates);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->entity, near.value().entity);
    EXPECT_NEAR(hit->world_position.z, 2.0f, 1.0e-4f);
  }
}

TEST(ViewportCurvePickingTests,
     SolidifiedCurveUsesExactTubeSurfaceAndReusesBlasAcrossTransforms) {
  ViewportInteractionHarness harness;
  const auto curve = harness.editor.modeling().create_curve({
      .points = {{.position = {-2.0f, 0.0f, 0.0f}},
                 {.position = {2.0f, 0.0f, 0.0f}}},
  });
  ASSERT_TRUE(curve.ok()) << curve.error_message();
  set_curve_solidify(harness, curve.value().entity, 1.0f);

  const auto initial = harness.frame().scene_curve_pick_candidates;
  ASSERT_EQ(initial.size(), 1u);
  ASSERT_TRUE(initial.front().solidified);
  ASSERT_NE(initial.front().tube_payload, nullptr);
  EXPECT_GT(initial.front().tube_payload->triangle_count(), 0u);
  EXPECT_EQ(harness.curve_pick_cache.tube_payload_build_count(), 1u);
  const auto payload = initial.front().tube_payload;

  const quader::QVec2 inside =
      harness.camera.project_world_point({0.0f, 0.5f, 0.0f}).position;
  const auto hit = quader::pick_viewport_curve(harness.camera, inside, initial);
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->entity, curve.value().entity);
  EXPECT_EQ(hit->distance_pixels, 0.0);
  const quader::QVec2 outside =
      harness.camera.project_world_point({0.0f, 1.1f, 0.0f}).position;
  EXPECT_FALSE(
      quader::pick_viewport_curve(harness.camera, outside, initial).has_value());

  ASSERT_TRUE(harness.session.scene().set_transform(
      curve.value().entity,
      quader::TransformComponent{.position = {0.0f, 2.0f, 0.0f}}));
  const auto moved = harness.frame().scene_curve_pick_candidates;
  ASSERT_EQ(moved.size(), 1u);
  EXPECT_EQ(moved.front().tube_payload, payload);
  EXPECT_EQ(harness.curve_pick_cache.tube_payload_build_count(), 1u)
      << "World-transform changes must not retessellate or rebuild the BLAS.";

  set_curve_solidify(harness, curve.value().entity, 0.5f, 12);
  const auto settings_changed = harness.frame().scene_curve_pick_candidates;
  ASSERT_EQ(settings_changed.size(), 1u);
  EXPECT_NE(settings_changed.front().tube_payload, payload);
  EXPECT_EQ(harness.curve_pick_cache.tube_payload_build_count(), 2u);
}

TEST(ViewportCurvePickingTests,
     DegenerateSolidifyFallsBackToCanonicalSampledCurveSemantics) {
  ViewportInteractionHarness harness;
  const auto curve = harness.editor.modeling().create_curve({
      .points = {{.position = {0.0f, 0.0f, 0.0f}},
                 {.position = {0.0f, 0.0f, 0.0f}},
                 {.position = {0.0f, 0.0f, 0.0f}}},
  });
  ASSERT_TRUE(curve.ok()) << curve.error_message();
  set_curve_solidify(harness, curve.value().entity, 0.25f);

  const auto candidates = harness.frame().scene_curve_pick_candidates;
  ASSERT_EQ(candidates.size(), 1u);
  ASSERT_NE(harness.session.scene().curve(curve.value().entity), nullptr);
  EXPECT_TRUE(harness.session.scene().curve(curve.value().entity)->solidify);
  EXPECT_FALSE(candidates.front().solidified)
      << "Semantic tube picking requires an actual derived payload.";
  EXPECT_EQ(candidates.front().tube_payload, nullptr);
  ASSERT_NE(candidates.front().sample_payload, nullptr);
  EXPECT_FALSE(candidates.front().sample_payload->samples.empty());
  EXPECT_FALSE(quader::pick_viewport_curve(
                   harness.camera, {400.0f, 300.0f}, candidates)
                   .has_value())
      << "The fallback must retain the sampled-stroke policy, including its "
         "canonical rejection of zero-length segments.";
}

TEST(ViewportCurvePickingTests,
     SolidifyBudgetFailureKeepsSampledStrokeClickAndMarqueePicking) {
  ViewportInteractionHarness harness;
  constexpr std::size_t kRingCount = 31'008u;
  quader::QuaderCreateCurveArgs args;
  args.points.reserve(kRingCount);
  for (std::size_t index = 0u; index < kRingCount; ++index) {
    const float fraction = static_cast<float>(index) /
                           static_cast<float>(kRingCount - 1u);
    args.points.push_back(
        {.position = {-1.5f + 3.0f * fraction, 0.0f, 0.0f}});
  }
  const auto curve = harness.editor.modeling().create_curve(args);
  ASSERT_TRUE(curve.ok()) << curve.error_message();
  set_curve_solidify(harness, curve.value().entity, 0.1f, 128);

  const auto candidates = harness.frame().scene_curve_pick_candidates;
  ASSERT_EQ(candidates.size(), 1u);
  ASSERT_NE(harness.session.scene().curve(curve.value().entity), nullptr);
  EXPECT_TRUE(harness.session.scene().curve(curve.value().entity)->solidify);
  EXPECT_FALSE(candidates.front().solidified);
  EXPECT_EQ(candidates.front().tube_payload, nullptr);
  ASSERT_NE(candidates.front().sample_payload, nullptr);
  ASSERT_GE(candidates.front().sample_payload->samples.size(), kRingCount);

  const quader::QVec2 center =
      harness.camera.project_world_point({0.0f, 0.0f, 0.0f}).position;
  const auto click =
      quader::pick_viewport_curve(harness.camera, center, candidates);
  ASSERT_TRUE(click.has_value());
  EXPECT_EQ(click->entity, curve.value().entity);

  quader::ModelingBoxSelectRequest request{
      .camera = {.orthographic = true,
                 .origin = {0.0f, 0.0f, 8.0f},
                 .viewport_size = {800.0f, 600.0f},
                 .orthographic_size = 12.0},
      .start = {390.0f, 290.0f},
      .end = {410.0f, 310.0f},
      .rule = quader::ModelingBoxSelectionRule::Crossing,
  };
  const auto visible = [](quader::EntityId) { return true; };
  EXPECT_EQ(harness.curve_pick_cache.box_pick_entities(
                harness.session.scene(), harness.session.assets(), request, 0u,
                visible),
            std::vector<quader::EntityId>{curve.value().entity});
}

TEST(ViewportCurvePickingTests,
     SolidifiedCurvePayloadCacheIsBoundedAndResetClearsItsCounters) {
  ViewportInteractionHarness harness;
  const auto curve = harness.editor.modeling().create_curve({
      .points = {{.position = {-1.0f, 0.0f, 0.0f}},
                 {.position = {1.0f, 0.0f, 0.0f}}},
  });
  ASSERT_TRUE(curve.ok()) << curve.error_message();

  constexpr std::size_t kDistinctSettings = 65u;
  std::vector<quader::ViewportSceneCurvePickCandidate> retained_payloads;
  retained_payloads.reserve(kDistinctSettings);
  for (std::size_t index = 0u; index < kDistinctSettings; ++index) {
    set_curve_solidify(harness, curve.value().entity,
                       0.1f + static_cast<float>(index) * 0.01f);
    const auto candidates = harness.frame().scene_curve_pick_candidates;
    ASSERT_EQ(candidates.size(), 1u);
    ASSERT_NE(candidates.front().tube_payload, nullptr);
    retained_payloads.push_back(candidates.front());
  }
  EXPECT_EQ(harness.curve_pick_cache.tube_payload_build_count(),
            kDistinctSettings);

  set_curve_solidify(harness, curve.value().entity, 0.1f);
  const auto replacement = harness.frame().scene_curve_pick_candidates;
  ASSERT_EQ(replacement.size(), 1u);
  ASSERT_NE(replacement.front().tube_payload, nullptr);
  EXPECT_EQ(harness.curve_pick_cache.tube_payload_build_count(),
            kDistinctSettings + 1u)
      << "The least-recently-used setting must be evicted at capacity.";
  std::size_t expected_live_payload_bytes =
      replacement.front().tube_payload->estimated_bytes();
  for (const auto &candidate : retained_payloads) {
    expected_live_payload_bytes += candidate.tube_payload->estimated_bytes();
  }
  EXPECT_EQ(harness.curve_pick_cache.tube_payload_cached_bytes(),
            expected_live_payload_bytes)
      << "Eviction from the lookup LRU cannot release caller-retained payload "
         "reservations.";

  const quader::ModelingViewportCamera projection_camera{
      .orthographic = true,
      .origin = {0.0f, 0.0f, 8.0f},
      .viewport_size = {800.0f, 600.0f},
      .orthographic_size = 12.0,
  };
  const auto runtime_camera =
      quader::modeling_runtime_camera(projection_camera);
  constexpr std::size_t kProjectionsPerPayload = 4u;
  for (auto candidate : retained_payloads) {
    for (std::size_t projection = 0u;
         projection < kProjectionsPerPayload; ++projection) {
      candidate.world_transform.values[12] +=
          static_cast<float>(projection) * 0.001f;
      EXPECT_TRUE(quader::project_viewport_curve_tube(
                      candidate,
                      {.camera = runtime_camera,
                       .near_plane = 0.01,
                       .far_plane = 1000.0})
                      .valid);
    }
  }
  constexpr std::size_t kGlobalProjectionCapacity = 64u * 4u;
  EXPECT_EQ(harness.curve_pick_cache.tube_projection_build_count(),
            kDistinctSettings * kProjectionsPerPayload);
  EXPECT_EQ(harness.curve_pick_cache.tube_projection_cache_entry_count(),
            kGlobalProjectionCapacity);
  EXPECT_EQ(harness.curve_pick_cache.tube_projection_eviction_count(),
            kDistinctSettings * kProjectionsPerPayload -
                kGlobalProjectionCapacity)
      << "All payloads must compete in one projection LRU.";
  EXPECT_EQ(harness.curve_pick_cache.tube_cached_bytes(),
            harness.curve_pick_cache.tube_payload_cached_bytes() +
                harness.curve_pick_cache.tube_projection_cached_bytes());
  EXPECT_LE(harness.curve_pick_cache.tube_cached_bytes(),
            harness.curve_pick_cache.tube_cache_byte_capacity());

  const auto first_projection_builds =
      retained_payloads.front().tube_payload->projection_build_count();
  EXPECT_TRUE(quader::project_viewport_curve_tube(
                  retained_payloads.front(),
                  {.camera = runtime_camera,
                   .near_plane = 0.01,
                   .far_plane = 1000.0})
                  .valid);
  EXPECT_EQ(retained_payloads.front().tube_payload->projection_build_count(),
            first_projection_builds + 1u)
      << "The oldest projection must be the first aggregate LRU victim.";
  EXPECT_EQ(harness.curve_pick_cache.tube_projection_eviction_count(), 5u);

  retained_payloads.clear();
  harness.curve_pick_cache.reset();
  EXPECT_EQ(harness.curve_pick_cache.build_count(), 0u);
  EXPECT_EQ(harness.curve_pick_cache.rectangle_build_count(), 0u);
  EXPECT_EQ(harness.curve_pick_cache.tube_payload_build_count(), 0u);
  EXPECT_EQ(harness.curve_pick_cache.tube_payload_cached_bytes(), 0u);
  EXPECT_EQ(harness.curve_pick_cache.tube_projection_cached_bytes(), 0u);
  EXPECT_EQ(harness.curve_pick_cache.tube_cached_bytes(), 0u);
  EXPECT_EQ(harness.curve_pick_cache.tube_projection_cache_entry_count(), 0u);
  EXPECT_EQ(harness.curve_pick_cache.tube_projection_build_count(), 0u);
  EXPECT_EQ(harness.curve_pick_cache.tube_projection_eviction_count(), 0u);
}

TEST(ViewportCurvePickingTests,
     OversizedTubePickPayloadCannotBypassTheAggregateByteBudget) {
  constexpr std::size_t kStandaloneBudgetBytes = 32u * 1024u * 1024u;
  constexpr std::size_t kEstimatedBytesPerTriangle = 256u;
  const std::size_t triangle_count =
      kStandaloneBudgetBytes / kEstimatedBytesPerTriangle + 1u;
  std::vector<quader::ViewportCurveTubeTriangle> triangles(
      triangle_count,
      {.a = {0.0f, 0.0f, 0.0f},
       .b = {1.0f, 0.0f, 0.0f},
       .c = {0.0f, 1.0f, 0.0f}});
  EXPECT_EQ(quader::ViewportCurveTubePickPayload::build(std::move(triangles)),
            nullptr)
      << "An oversized valid triangle set must fall back before allocating an "
         "unaccounted BVH.";
}

TEST(ViewportCurveAnchorSnapTests,
     SinglePointDocumentsContributeAnchorsButNeverHandleEndpoints) {
  const std::array anchor_near_ray{
      quader::modeling::CurvePointSpec{
          .position = {0.2f, 0.0f, 4.0f},
          .incoming_handle = {-0.2f, 0.0f, 0.0f},
          .outgoing_handle = {2.0f, 0.0f, 0.0f},
      },
  };
  const auto anchor_document =
      std::make_shared<const quader::modeling::CurveDocument>(
          quader::modeling::CurveDocument::make_bezier(anchor_near_ray)
              .or_throw());
  const std::array anchor_away_from_ray{
      quader::modeling::CurvePointSpec{
          .position = {2.0f, 0.0f, 4.0f},
          .incoming_handle = {-2.0f, 0.0f, 0.0f},
      },
  };
  const auto handle_document =
      std::make_shared<const quader::modeling::CurveDocument>(
          quader::modeling::CurveDocument::make_bezier(anchor_away_from_ray)
              .or_throw());
  const quader::ViewportRay ray{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
  const std::array candidates{
      quader::ViewportSceneCurvePickCandidate{
          .entity = 7u,
          .document = anchor_document,
      },
  };
  const auto anchors =
      quader::collect_viewport_curve_anchor_snap_candidates(candidates);
  ASSERT_EQ(anchors.size(), 1u);

  const auto hit = quader::resolve_viewport_curve_anchor_snap_target(
      ray, anchors, 0.3);
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->entity, 7u);
  EXPECT_EQ(hit->point, anchor_document->points().front().id);
  EXPECT_NEAR(hit->world_position.x, 0.2f, 1.0e-6f);
  EXPECT_NEAR(hit->ray_distance, 4.0, 1.0e-6);
  EXPECT_NEAR(hit->pick_distance, 0.2, 1.0e-6);
  EXPECT_FALSE(quader::resolve_viewport_curve_anchor_snap_target(
      ray, anchors, 0.19).has_value());

  const std::array handle_only_candidate{
      quader::ViewportSceneCurvePickCandidate{
          .entity = 8u,
          .document = handle_document,
      },
  };
  const auto handle_only_anchor =
      quader::collect_viewport_curve_anchor_snap_candidates(
          handle_only_candidate);
  EXPECT_FALSE(quader::resolve_viewport_curve_anchor_snap_target(
      ray, handle_only_anchor, 0.3).has_value());
  EXPECT_FALSE(quader::resolve_viewport_curve_anchor_snap_target(
      {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 2.0f}}, anchors, 0.3)
                   .has_value());
}

TEST(ViewportCurveAnchorSnapTests,
     ActiveDraftUsesTypedIdentityAndExcludesSelectedAnchors) {
  const std::array points{
      quader::modeling::CurvePointSpec{.position = {0.1f, 0.0f, 3.0f}},
      quader::modeling::CurvePointSpec{.position = {0.2f, 0.0f, 4.0f}},
  };
  quader::CurveToolPreviewState preview{
      .active = true,
      .drawing = true,
      .document =
          quader::modeling::CurveDocument::make_bezier(points).or_throw(),
  };
  const quader::modeling::CurvePointId selected =
      preview.document->points().front().id;
  const auto group =
      quader::collect_viewport_curve_draft_anchor_snap_candidate(
          preview, [selected](quader::modeling::CurvePointId point) {
            return point == selected;
          });
  ASSERT_TRUE(group.has_value());
  EXPECT_EQ(group->group.kind,
            quader::ViewportCurveAnchorSnapGroupKind::ActiveDraft);
  EXPECT_EQ(group->group.scene_entity, quader::kInvalidEntityId);
  ASSERT_EQ(group->anchors.size(), 1u);

  const std::array groups{*group};
  const auto hit = quader::resolve_viewport_curve_anchor_snap_target(
      {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}, groups, 0.3);
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->group.kind,
            quader::ViewportCurveAnchorSnapGroupKind::ActiveDraft);
  EXPECT_EQ(hit->entity, quader::kInvalidEntityId);
  EXPECT_EQ(hit->point, preview.document->points()[1].id);
}

TEST(ViewportCurveAnchorSnapTests,
     DepthIdentityExclusionAndMeshSurfaceOcclusionAreDeterministic) {
  const std::array near_points{
      quader::modeling::CurvePointSpec{.position = {0.29f, 0.0f, 3.0f}},
  };
  const auto near_document =
      std::make_shared<const quader::modeling::CurveDocument>(
          quader::modeling::CurveDocument::make_bezier(near_points).or_throw());
  const std::array far_points{
      quader::modeling::CurvePointSpec{.position = {0.0f, 0.0f, 4.0f}},
  };
  const auto far_document =
      std::make_shared<const quader::modeling::CurveDocument>(
          quader::modeling::CurveDocument::make_bezier(far_points).or_throw());
  const std::array candidates{
      quader::ViewportSceneCurvePickCandidate{
          .entity = 20u,
          .document = near_document,
      },
      quader::ViewportSceneCurvePickCandidate{
          .entity = 10u,
          .document = far_document,
      },
      quader::ViewportSceneCurvePickCandidate{
          .entity = 5u,
          .document = far_document,
      },
  };
  const quader::ViewportRay ray{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
  const auto anchors =
      quader::collect_viewport_curve_anchor_snap_candidates(candidates);

  const auto nearest = quader::resolve_viewport_curve_anchor_snap_target(
      ray, anchors, 0.3);
  ASSERT_TRUE(nearest.has_value());
  EXPECT_EQ(nearest->entity, 20u);

  const auto without_near = quader::resolve_viewport_curve_anchor_snap_target(
      ray, anchors, 0.3,
      [](quader::EntityId entity, quader::modeling::CurvePointId) {
        return entity == 20u;
      });
  ASSERT_TRUE(without_near.has_value());
  EXPECT_EQ(without_near->entity, 5u);

  EXPECT_FALSE(quader::resolve_viewport_curve_anchor_snap_target(
      ray, anchors, 0.3,
      [](quader::EntityId entity, quader::modeling::CurvePointId) {
        return entity == 20u;
      },
      3.5)
                   .has_value());
  EXPECT_TRUE(quader::resolve_viewport_curve_anchor_snap_target(
      ray, anchors, 0.3, {}, 3.0).has_value());

  const std::array tied_points{
      quader::modeling::CurvePointSpec{.position = {0.2f, 0.0f, 2.0f}},
      quader::modeling::CurvePointSpec{.position = {0.1f, 0.0f, 2.0f}},
      quader::modeling::CurvePointSpec{.position = {-0.1f, 0.0f, 2.0f}},
  };
  const auto tied_document =
      std::make_shared<const quader::modeling::CurveDocument>(
          quader::modeling::CurveDocument::make_bezier(tied_points).or_throw());
  const std::array tied_candidate{
      quader::ViewportSceneCurvePickCandidate{
          .entity = 3u,
          .document = tied_document,
      },
  };
  const auto tied_anchors =
      quader::collect_viewport_curve_anchor_snap_candidates(tied_candidate);
  const auto point_tie = quader::resolve_viewport_curve_anchor_snap_target(
      ray, tied_anchors, 0.3);
  ASSERT_TRUE(point_tie.has_value());
  EXPECT_EQ(point_tie->point, tied_document->points()[1].id);
}

TEST(ViewportCurveAnchorSnapTests,
     PerCurvePickDistanceWinnerCompetesGloballyByDepth) {
  const std::array first_curve_points{
      quader::modeling::CurvePointSpec{.position = {0.29f, 0.0f, 2.0f}},
      quader::modeling::CurvePointSpec{.position = {0.01f, 0.0f, 4.0f}},
  };
  const auto first_curve =
      std::make_shared<const quader::modeling::CurveDocument>(
          quader::modeling::CurveDocument::make_bezier(first_curve_points)
              .or_throw());
  const std::array second_curve_points{
      quader::modeling::CurvePointSpec{.position = {0.05f, 0.0f, 3.0f}},
  };
  const auto second_curve =
      std::make_shared<const quader::modeling::CurveDocument>(
          quader::modeling::CurveDocument::make_bezier(second_curve_points)
              .or_throw());
  const std::array first_candidate{
      quader::ViewportSceneCurvePickCandidate{
          .entity = 42u,
          .document = first_curve,
      },
  };
  const std::array combined_candidates{
      first_candidate.front(),
      quader::ViewportSceneCurvePickCandidate{
          .entity = 43u,
          .document = second_curve,
      },
  };
  const quader::ViewportRay ray{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};

  const auto first_anchors =
      quader::collect_viewport_curve_anchor_snap_candidates(first_candidate);
  const auto local_winner = quader::resolve_viewport_curve_anchor_snap_target(
      ray, first_anchors, 0.3);
  ASSERT_TRUE(local_winner.has_value());
  EXPECT_EQ(local_winner->point, first_curve->points()[1].id);
  EXPECT_NEAR(local_winner->ray_distance, 4.0, 1.0e-6);

  const auto combined_anchors =
      quader::collect_viewport_curve_anchor_snap_candidates(
          combined_candidates);
  const auto global_winner = quader::resolve_viewport_curve_anchor_snap_target(
      ray, combined_anchors, 0.3);
  ASSERT_TRUE(global_winner.has_value());
  EXPECT_EQ(global_winner->entity, 43u);
  EXPECT_NEAR(global_winner->ray_distance, 3.0, 1.0e-6);
}

TEST(ViewportCurveAnchorSnapTests,
     CrossEntityDepthDifferenceAboveMeshEpsilonKeepsNearerWinner) {
  const std::array farther_points{
      quader::modeling::CurvePointSpec{
          .position = {0.01f, 0.0f, 3.000005f}},
  };
  const auto farther =
      std::make_shared<const quader::modeling::CurveDocument>(
          quader::modeling::CurveDocument::make_bezier(farther_points)
              .or_throw());
  const std::array nearer_points{
      quader::modeling::CurvePointSpec{.position = {0.2f, 0.0f, 3.0f}},
  };
  const auto nearer =
      std::make_shared<const quader::modeling::CurveDocument>(
          quader::modeling::CurveDocument::make_bezier(nearer_points)
              .or_throw());
  const std::array candidates{
      quader::ViewportSceneCurvePickCandidate{
          .entity = 1u,
          .document = farther,
      },
      quader::ViewportSceneCurvePickCandidate{
          .entity = 2u,
          .document = nearer,
      },
  };
  const auto groups =
      quader::collect_viewport_curve_anchor_snap_candidates(candidates);
  const auto winner = quader::resolve_viewport_curve_anchor_snap_target(
      {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}, groups, 0.3);
  ASSERT_TRUE(winner.has_value());
  EXPECT_EQ(winner->entity, 2u);
}

TEST(ViewportCurveAnchorSnapTests,
     MeshSurfaceOcclusionUsesComponentToleranceBoundary) {
  const std::array points{
      quader::modeling::CurvePointSpec{.position = {0.0f, 0.0f, 4.0f}},
  };
  const auto document =
      std::make_shared<const quader::modeling::CurveDocument>(
          quader::modeling::CurveDocument::make_bezier(points).or_throw());
  const std::array candidates{
      quader::ViewportSceneCurvePickCandidate{
          .entity = 9u,
          .document = document,
      },
  };
  const auto anchors =
      quader::collect_viewport_curve_anchor_snap_candidates(candidates);
  const quader::ViewportRay ray{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};

  EXPECT_TRUE(quader::resolve_viewport_curve_anchor_snap_target(
      ray, anchors, 0.3, {}, 3.999)
                  .has_value());
  EXPECT_FALSE(quader::resolve_viewport_curve_anchor_snap_target(
      ray, anchors, 0.3, {}, 3.998)
                   .has_value());
}

TEST(ViewportCurveAnchorSnapTests,
     ExpandedEntityBoundsSkipOffRayAnchorGroups) {
  const std::array near_points{
      quader::modeling::CurvePointSpec{.position = {0.1f, 0.0f, 3.0f}},
  };
  const auto near_document =
      std::make_shared<const quader::modeling::CurveDocument>(
          quader::modeling::CurveDocument::make_bezier(near_points).or_throw());
  const std::array far_points{
      quader::modeling::CurvePointSpec{.position = {100.0f, 0.0f, 2.0f}},
      quader::modeling::CurvePointSpec{.position = {101.0f, 0.0f, 4.0f}},
  };
  const auto far_document =
      std::make_shared<const quader::modeling::CurveDocument>(
          quader::modeling::CurveDocument::make_bezier(far_points).or_throw());
  const std::array candidates{
      quader::ViewportSceneCurvePickCandidate{
          .entity = 1u,
          .document = far_document,
      },
      quader::ViewportSceneCurvePickCandidate{
          .entity = 2u,
          .document = near_document,
      },
  };
  const auto groups =
      quader::collect_viewport_curve_anchor_snap_candidates(candidates);
  ASSERT_EQ(groups.size(), 2u);
  quader::ViewportCurveAnchorSnapWorkCounters counters;
  const auto hit = quader::resolve_viewport_curve_anchor_snap_target(
      {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}, groups, 0.3, {},
      std::nullopt, &counters);
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->entity, 2u);
  EXPECT_EQ(counters.entity_bounds_tested, 2u);
  EXPECT_EQ(counters.anchors_tested, 1u);
}

TEST(ViewportCurvePickingTests,
     CandidateCacheRetainsSamplesAndFiltersHierarchyLayersAndLocalVisibility) {
  ViewportInteractionHarness harness;
  const auto first = harness.editor.modeling().create_curve({
      .points = {{.position = {-2.0f, 0.0f, 0.0f}},
                 {.position = {2.0f, 0.0f, 0.0f}}},
  });
  ASSERT_TRUE(first.ok()) << first.error_message();
  const auto initial = harness.frame().scene_curve_pick_candidates;
  ASSERT_EQ(initial.size(), 1u);
  const auto samples = initial.front().sample_payload;
  const auto document = initial.front().document;
  ASSERT_NE(document, nullptr);
  const auto *storage = initial.data();
  const auto builds = harness.curve_pick_cache.build_count();
  for (int frame = 0; frame < 8; ++frame) {
    EXPECT_EQ(harness.frame().scene_curve_pick_candidates.data(), storage);
    EXPECT_EQ(harness.curve_pick_cache.build_count(), builds);
  }
  ASSERT_TRUE(harness.editor.scene().set_transform(
      first.value().entity, {.position = {0.0f, 2.0f, 0.0f}}).ok());
  const auto moved = harness.frame().scene_curve_pick_candidates;
  ASSERT_EQ(moved.size(), 1u);
  EXPECT_EQ(moved.front().sample_payload, samples);
  EXPECT_EQ(moved.front().document, document);
  const auto moved_hit = quader::pick_viewport_curve(
      harness.camera, harness.camera.project_world_point({0.0f, 2.0f, 0.0f}).position,
      moved);
  ASSERT_TRUE(moved_hit.has_value());
  EXPECT_NEAR(moved_hit->world_position.y, 2.0f, 1.0e-4f);

  ASSERT_TRUE(harness.editor.selection().select_object(first.value()).ok());
  const auto group = harness.editor.scene().group_selection("Curve Visibility");
  ASSERT_TRUE(group.ok()) << group.error_message();
  ASSERT_TRUE(harness.editor.scene().set_visible(group.value().id, false).ok());
  EXPECT_TRUE(harness.frame().scene_curve_pick_candidates.empty());
  ASSERT_TRUE(harness.editor.scene().set_visible(group.value().id, true).ok());
  ASSERT_TRUE(harness.editor.scene().set_locked(group.value().id, true).ok());
  EXPECT_TRUE(harness.frame().scene_curve_pick_candidates.empty());
  ASSERT_TRUE(harness.editor.scene().set_locked(group.value().id, false).ok());
  ASSERT_TRUE(harness.session.scene().set_selectable(group.value().id, false));
  EXPECT_TRUE(harness.frame().scene_curve_pick_candidates.empty());
  ASSERT_TRUE(harness.session.scene().set_selectable(group.value().id, true));

  ASSERT_TRUE(harness.editor.scene().select(group.value().id).ok());
  const auto layer = harness.editor.layers().create_from_selection("Curve Layer");
  ASSERT_TRUE(layer.ok()) << layer.error_message();
  ASSERT_TRUE(harness.editor.layers().set_visible(layer.value().id, false).ok());
  EXPECT_TRUE(harness.frame().scene_curve_pick_candidates.empty());
  ASSERT_TRUE(harness.editor.layers().set_visible(layer.value().id, true).ok());
  ASSERT_TRUE(harness.editor.layers().set_locked(layer.value().id, true).ok());
  EXPECT_TRUE(harness.frame().scene_curve_pick_candidates.empty());
  ASSERT_TRUE(harness.editor.layers().set_locked(layer.value().id, false).ok());

  const auto other = harness.editor.modeling().create_curve({
      .points = {{.position = {-2.0f, 4.0f, 0.0f}},
                 {.position = {2.0f, 4.0f, 0.0f}}},
  });
  ASSERT_TRUE(other.ok()) << other.error_message();
  ASSERT_TRUE(harness.editor.selection().select_object(first.value()).ok());
  const std::vector<quader::EntityId> selected_before_local =
      harness.session.scene().selected_entities();
  ASSERT_EQ(selected_before_local.size(), 1u);
  EXPECT_EQ(selected_before_local.front(), first.value().entity);
  EXPECT_EQ(harness.session.modeling().selection_mode(),
            quader::ModelingSelectionMode::Object);
  const auto entered_local = harness.editor.viewports().toggle_local_visibility();
  ASSERT_TRUE(entered_local.ok()) << entered_local.error_message();
  const quader::ViewportLocalVisibilitySnapshot local_snapshot =
      harness.session.viewport_local_visibility().snapshot();
  ASSERT_EQ(local_snapshot.members().size(), 1u);
  EXPECT_EQ(local_snapshot.members().front(), first.value().entity);
  const auto local = harness.frame().scene_curve_pick_candidates;
  ASSERT_EQ(local.size(), 1u);
  EXPECT_EQ(local.front().entity, first.value().entity);
  EXPECT_EQ(local.front().sample_payload, samples);
  EXPECT_EQ(local.front().document, document);
}

TEST(ViewportCurveMarqueeTests,
     RectangleChangesReuseProjectionWhileCameraChangesKeepCandidateSamples) {
  ViewportInteractionHarness harness;
  const auto curve = harness.editor.modeling().create_curve({
      .points = {{.position = {-2.0f, 0.0f, 0.0f}},
                 {.position = {2.0f, 0.0f, 0.0f}}},
  });
  ASSERT_TRUE(curve.ok()) << curve.error_message();
  quader::ViewportCurvePickCache cache;
  quader::ModelingBoxSelectRequest request{
      .camera = {.orthographic = true,
                 .origin = {0.0f, 0.0f, 8.0f},
                 .viewport_size = {800.0f, 600.0f},
                 .orthographic_size = 12.0},
      .start = {290.0f, 290.0f},
      .end = {510.0f, 310.0f},
      .rule = quader::ModelingBoxSelectionRule::Contain,
  };
  const auto visible = [](quader::EntityId) { return true; };
  const auto pick = [&] {
    return cache.box_pick_entities(harness.session.scene(), harness.session.assets(),
                                   request, 0u, visible);
  };
  EXPECT_EQ(pick(), std::vector<quader::EntityId>{curve.value().entity});
  const auto candidates = cache.candidates(
      harness.session.scene(), harness.session.assets(), 0u, visible);
  ASSERT_EQ(candidates.size(), 1u);
  const auto *storage = candidates.data();
  const auto samples = candidates.front().sample_payload;
  EXPECT_EQ(cache.build_count(), 1u);
  EXPECT_EQ(cache.rectangle_build_count(), 1u);

  request.start = {0.0f, 0.0f};
  request.end = {10.0f, 10.0f};
  EXPECT_TRUE(pick().empty());
  request.start = {390.0f, 290.0f};
  request.end = {410.0f, 310.0f};
  request.edit = quader::ModelingSelectionEdit::Add;
  request.rule = quader::ModelingBoxSelectionRule::Crossing;
  EXPECT_EQ(pick(), std::vector<quader::EntityId>{curve.value().entity});
  EXPECT_EQ(cache.build_count(), 1u);
  EXPECT_EQ(cache.rectangle_build_count(), 1u);

  request.camera.origin.x = 10.0f;
  EXPECT_TRUE(pick().empty());
  EXPECT_EQ(cache.build_count(), 1u);
  EXPECT_EQ(cache.rectangle_build_count(), 2u);
  const auto retained = cache.candidates(
      harness.session.scene(), harness.session.assets(), 0u, visible);
  ASSERT_EQ(retained.size(), 1u);
  EXPECT_EQ(retained.data(), storage);
  EXPECT_EQ(retained.front().sample_payload, samples);
}

TEST(ViewportCurveMarqueeTests,
     SolidifiedCurveContainmentAndCrossingUseVisibleTubeSilhouette) {
  ViewportInteractionHarness harness;
  const auto curve = harness.editor.modeling().create_curve({
      .points = {{.position = {-2.0f, 0.0f, 0.0f}},
                 {.position = {2.0f, 0.0f, 0.0f}}},
  });
  ASSERT_TRUE(curve.ok()) << curve.error_message();
  set_curve_solidify(harness, curve.value().entity, 1.0f);

  quader::ViewportCurvePickCache cache;
  quader::ModelingBoxSelectRequest request{
      .camera = {.orthographic = true,
                 .origin = {0.0f, 0.0f, 8.0f},
                 .viewport_size = {800.0f, 600.0f},
                 .orthographic_size = 12.0},
      .start = {390.0f, 258.0f},
      .end = {410.0f, 266.0f},
      .rule = quader::ModelingBoxSelectionRule::Crossing,
  };
  const auto visible = [](quader::EntityId) { return true; };
  const auto pick = [&] {
    return cache.box_pick_entities(harness.session.scene(),
                                   harness.session.assets(), request, 0u,
                                   visible);
  };
  EXPECT_EQ(pick(), std::vector<quader::EntityId>{curve.value().entity})
      << "The rectangle intersects the tube but not its hidden centerline.";
  EXPECT_EQ(cache.rectangle_build_count(), 1u);
  const auto candidates = cache.candidates(
      harness.session.scene(), harness.session.assets(), 0u, visible);
  ASSERT_EQ(candidates.size(), 1u);
  ASSERT_NE(candidates.front().tube_payload, nullptr);
  EXPECT_EQ(candidates.front().tube_payload->projection_build_count(), 1u);

  request.start = {290.0f, 295.0f};
  request.end = {510.0f, 305.0f};
  request.rule = quader::ModelingBoxSelectionRule::Contain;
  EXPECT_TRUE(pick().empty())
      << "Containing only the centerline must not contain a solidified Curve.";
  EXPECT_EQ(cache.rectangle_build_count(), 1u)
      << "Rectangle edits reuse the camera/source tube projection.";
  EXPECT_EQ(candidates.front().tube_payload->projection_build_count(), 1u);

  request.start = {290.0f, 240.0f};
  request.end = {510.0f, 360.0f};
  EXPECT_EQ(pick(), std::vector<quader::EntityId>{curve.value().entity});

  auto api_request = curve_marquee_request({390.0f, 258.0f},
                                            {410.0f, 266.0f});
  api_request.rule = quader::QuaderBoxSelectionRule::Crossing;
  const auto selected = harness.editor.selection().box_select(api_request);
  ASSERT_TRUE(selected.ok()) << selected.error_message();
  EXPECT_EQ(selected_entities(harness),
            std::vector<quader::EntityId>{curve.value().entity});
}

TEST(ViewportCurveMarqueeTests,
     MixedMeshesAndCurvesHonorDirectionModifiersBaselineAndSingleUndo) {
  ViewportInteractionHarness harness;
  const auto box = create_unit_box(harness);
  const auto curve = harness.editor.modeling().create_curve({
      .points = {{.position = {-3.0f, 0.0f, 0.0f}},
                 {.position = {3.0f, 0.0f, 0.0f}}},
  });
  ASSERT_TRUE(curve.ok()) << curve.error_message();
  ASSERT_TRUE(harness.editor.selection().set_mode(quader::QuaderSelectionMode::Object).ok());
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  auto request = curve_marquee_request({340.0f, 240.0f}, {460.0f, 360.0f});
  request.rule = quader::QuaderBoxSelectionRule::DragDirection;
  auto selected = harness.editor.selection().box_select(request);
  ASSERT_TRUE(selected.ok()) << selected.error_message();
  EXPECT_EQ(selected_entities(harness), std::vector<quader::EntityId>{box.entity});

  std::swap(request.start, request.end);
  selected = harness.editor.selection().box_select(request);
  ASSERT_TRUE(selected.ok()) << selected.error_message();
  const std::vector<quader::EntityId> both{box.entity, curve.value().entity};
  EXPECT_EQ(selected_entities(harness), both);
  const auto undo = harness.editor.history().undo();
  ASSERT_TRUE(undo.ok()) << undo.error_message();
  EXPECT_EQ(selected_entities(harness), std::vector<quader::EntityId>{box.entity});
  const auto redo = harness.editor.history().redo();
  ASSERT_TRUE(redo.ok()) << redo.error_message();
  EXPECT_EQ(selected_entities(harness), both);

  request = curve_marquee_request({500.0f, 285.0f}, {540.0f, 315.0f});
  request.rule = quader::QuaderBoxSelectionRule::Crossing;
  request.edit = quader::QuaderSelectionEdit::Remove;
  selected = harness.editor.selection().box_select(request);
  ASSERT_TRUE(selected.ok()) << selected.error_message();
  EXPECT_EQ(selected_entities(harness), std::vector<quader::EntityId>{box.entity});
  const auto baseline = harness.editor.selection().capture_box_selection_baseline();
  ASSERT_TRUE(harness.editor.selection().select_object(curve.value()).ok());
  request.edit = quader::QuaderSelectionEdit::Add;
  selected = harness.editor.selection().box_select(request, baseline);
  ASSERT_TRUE(selected.ok()) << selected.error_message();
  EXPECT_EQ(selected_entities(harness), both);
  request.edit = quader::QuaderSelectionEdit::Toggle;
  selected = harness.editor.selection().box_select(request, baseline);
  ASSERT_TRUE(selected.ok()) << selected.error_message();
  EXPECT_EQ(selected_entities(harness), both)
      << "repeated drag updates must use the press-time baseline";
  selected = harness.editor.selection().box_select(request);
  ASSERT_TRUE(selected.ok()) << selected.error_message();
  EXPECT_EQ(selected_entities(harness), std::vector<quader::EntityId>{box.entity});
}

TEST(ViewportCurveMarqueeTests,
     UsesSampledPathInsteadOfBoundsAndDoesNotClampContainmentToViewport) {
  ViewportInteractionHarness harness;
  const auto curve = harness.editor.modeling().create_curve({
      .points = {{.position = {-2.0f, -1.0f, 0.0f},
                  .outgoing_handle = {0.0f, 4.0f, 0.0f}},
                 {.position = {2.0f, -1.0f, 0.0f},
                  .incoming_handle = {0.0f, 4.0f, 0.0f}}},
  });
  ASSERT_TRUE(curve.ok()) << curve.error_message();
  ASSERT_TRUE(harness.editor.selection().set_mode(quader::QuaderSelectionMode::Object).ok());
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  auto request = curve_marquee_request({390.0f, 315.0f}, {410.0f, 335.0f});
  request.rule = quader::QuaderBoxSelectionRule::Crossing;
  auto selected = harness.editor.selection().box_select(request);
  ASSERT_TRUE(selected.ok()) << selected.error_message();
  EXPECT_TRUE(selected_entities(harness).empty())
      << "this rectangle is inside the curve bounds but below the actual arch";
  request.start = {390.0f, 195.0f};
  request.end = {410.0f, 205.0f};
  selected = harness.editor.selection().box_select(request);
  ASSERT_TRUE(selected.ok()) << selected.error_message();
  EXPECT_EQ(selected_entities(harness), std::vector<quader::EntityId>{curve.value().entity});
  request = curve_marquee_request({290.0f, 340.0f}, {510.0f, 360.0f});
  selected = harness.editor.selection().box_select(request);
  ASSERT_TRUE(selected.ok()) << selected.error_message();
  EXPECT_TRUE(selected_entities(harness).empty())
      << "containing the anchors does not contain the arch between them";
  request = curve_marquee_request({290.0f, 190.0f}, {510.0f, 360.0f});
  selected = harness.editor.selection().box_select(request);
  ASSERT_TRUE(selected.ok()) << selected.error_message();
  EXPECT_EQ(selected_entities(harness), std::vector<quader::EntityId>{curve.value().entity});

  const auto offscreen = harness.editor.modeling().create_curve({
      .points = {{.position = {-20.0f, 4.0f, 0.0f}},
                 {.position = {20.0f, 4.0f, 0.0f}}},
  });
  ASSERT_TRUE(offscreen.ok()) << offscreen.error_message();
  request = curve_marquee_request({0.0f, 80.0f}, {800.0f, 120.0f});
  selected = harness.editor.selection().box_select(request);
  ASSERT_TRUE(selected.ok()) << selected.error_message();
  EXPECT_TRUE(selected_entities(harness).empty());
  request.rule = quader::QuaderBoxSelectionRule::Crossing;
  selected = harness.editor.selection().box_select(request);
  ASSERT_TRUE(selected.ok()) << selected.error_message();
  EXPECT_EQ(selected_entities(harness), std::vector<quader::EntityId>{offscreen.value().entity});
}

TEST(ViewportCurveMarqueeTests,
     PromotesNearestGroupAndHonorsAncestorLayerAndLocalVisibility) {
  ViewportInteractionHarness harness;
  const auto curve = harness.editor.modeling().create_curve({
      .points = {{.position = {-2.0f, 0.0f, 0.0f}},
                 {.position = {2.0f, 0.0f, 0.0f}}},
  });
  ASSERT_TRUE(curve.ok()) << curve.error_message();
  ASSERT_TRUE(harness.editor.selection().select_object(curve.value()).ok());
  const auto inner = harness.editor.scene().group_selection("Marquee Inner");
  ASSERT_TRUE(inner.ok()) << inner.error_message();
  const auto outer = harness.editor.scene().group_selection("Marquee Outer");
  ASSERT_TRUE(outer.ok()) << outer.error_message();
  ASSERT_TRUE(harness.editor.selection().set_mode(quader::QuaderSelectionMode::Group).ok());
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  const auto request = curve_marquee_request({295.0f, 290.0f}, {505.0f, 310.0f});
  auto selected = harness.editor.selection().box_select(request);
  ASSERT_TRUE(selected.ok()) << selected.error_message();
  EXPECT_EQ(selected_entities(harness), std::vector<quader::EntityId>{inner.value().id});

  ASSERT_TRUE(harness.editor.scene().set_visible(outer.value().id, false).ok());
  selected = harness.editor.selection().box_select(request);
  ASSERT_TRUE(selected.ok()) << selected.error_message();
  EXPECT_TRUE(selected_entities(harness).empty());
  ASSERT_TRUE(harness.editor.scene().set_visible(outer.value().id, true).ok());
  ASSERT_TRUE(harness.editor.scene().set_locked(outer.value().id, true).ok());
  selected = harness.editor.selection().box_select(request);
  ASSERT_TRUE(selected.ok()) << selected.error_message();
  EXPECT_TRUE(selected_entities(harness).empty());
  ASSERT_TRUE(harness.editor.scene().set_locked(outer.value().id, false).ok());
  ASSERT_TRUE(harness.editor.scene().select(outer.value().id).ok());
  const auto layer = harness.editor.layers().create_from_selection("Marquee Layer");
  ASSERT_TRUE(layer.ok()) << layer.error_message();
  ASSERT_TRUE(harness.editor.layers().set_visible(layer.value().id, false).ok());
  selected = harness.editor.selection().box_select(request);
  ASSERT_TRUE(selected.ok()) << selected.error_message();
  EXPECT_TRUE(selected_entities(harness).empty());
  ASSERT_TRUE(harness.editor.layers().set_visible(layer.value().id, true).ok());
  ASSERT_TRUE(harness.editor.layers().set_locked(layer.value().id, true).ok());
  selected = harness.editor.selection().box_select(request);
  ASSERT_TRUE(selected.ok()) << selected.error_message();
  EXPECT_TRUE(selected_entities(harness).empty());
  ASSERT_TRUE(harness.editor.layers().set_locked(layer.value().id, false).ok());

  ASSERT_TRUE(harness.editor.selection().set_mode(quader::QuaderSelectionMode::Object).ok());
  const auto other = harness.editor.modeling().create_curve({
      .points = {{.position = {-2.0f, 4.0f, 0.0f}},
                 {.position = {2.0f, 4.0f, 0.0f}}},
  });
  ASSERT_TRUE(other.ok()) << other.error_message();
  ASSERT_TRUE(harness.editor.selection().select_object(other.value()).ok());
  const auto local = harness.editor.viewports().toggle_local_visibility();
  ASSERT_TRUE(local.ok()) << local.error_message();
  ASSERT_TRUE(harness.editor.selection().set_mode(quader::QuaderSelectionMode::Group).ok());
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  selected = harness.editor.selection().box_select(request);
  ASSERT_TRUE(selected.ok()) << selected.error_message();
  EXPECT_TRUE(selected_entities(harness).empty());
  const auto restored = harness.editor.viewports().toggle_local_visibility();
  ASSERT_TRUE(restored.ok()) << restored.error_message();
  selected = harness.editor.selection().box_select(request);
  ASSERT_TRUE(selected.ok()) << selected.error_message();
  EXPECT_EQ(selected_entities(harness), std::vector<quader::EntityId>{inner.value().id});
}

TEST(ViewportCurveMarqueeTests,
     InstanceEditExcludesOrdinaryCurvesWithoutBreakingMemberMarquee) {
  ViewportInteractionHarness harness;
  const auto curve = harness.editor.modeling().create_curve({
      .points = {{.position = {-2.0f, 3.0f, 0.0f}},
                 {.position = {2.0f, 3.0f, 0.0f}}},
  });
  ASSERT_TRUE(curve.ok()) << curve.error_message();
  const auto box = create_unit_box(harness);
  ASSERT_TRUE(harness.editor.selection().set_mode(quader::QuaderSelectionMode::Object).ok());
  ASSERT_TRUE(harness.editor.selection().select_object(box).ok());
  const auto instance = harness.editor.modeling().create_instance();
  ASSERT_TRUE(instance.ok()) << instance.error_message();
  quader::EntityId occurrence = quader::kInvalidEntityId;
  for (const auto &entity : harness.editor.query().entities()) {
    if (entity.has_instance_occurrence) {
      occurrence = entity.id;
      break;
    }
  }
  ASSERT_NE(occurrence, quader::kInvalidEntityId);
  const auto entered = harness.editor.modeling().enter_instance_edit(occurrence);
  ASSERT_TRUE(entered.ok()) << entered.error_message();
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  auto request = curve_marquee_request({290.0f, 140.0f}, {510.0f, 160.0f});
  auto selected = harness.editor.selection().box_select(request);
  ASSERT_TRUE(selected.ok()) << selected.error_message();
  EXPECT_TRUE(selected_entities(harness).empty());
  request = curve_marquee_request({0.0f, 0.0f}, {800.0f, 600.0f});
  selected = harness.editor.selection().box_select(request);
  ASSERT_TRUE(selected.ok()) << selected.error_message();
  EXPECT_EQ(selected.value().selected_object_count, 1u);
  ASSERT_TRUE(harness.editor.query().entity(curve.value().entity).has_value());
  EXPECT_FALSE(harness.editor.query().entity(curve.value().entity)->selected);
  const auto exited = harness.editor.modeling().exit_instance_edit();
  ASSERT_TRUE(exited.ok()) << exited.error_message();
  request = curve_marquee_request({290.0f, 140.0f}, {510.0f, 160.0f});
  selected = harness.editor.selection().box_select(request);
  ASSERT_TRUE(selected.ok()) << selected.error_message();
  EXPECT_EQ(selected_entities(harness), std::vector<quader::EntityId>{curve.value().entity});
}

TEST(ViewportInteractionControllerTests,
     OrthographicModifiedDoubleClicksSelectAndRemoveThePoleFacePerimeter) {
  ViewportInteractionHarness harness;
  const auto box = create_unit_box(harness);
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Edge)
                  .ok());
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Edge);
  harness.camera.view_axis(quader::ViewportViewAxis::Z, 1);
  const auto edge = harness.camera.project_world_point({0.0f, 1.0f, 1.0f});
  ASSERT_TRUE(edge.visible);
  harness.click_selection(edge.position);
  ASSERT_EQ(harness.editor.selection().summary().selected_edge_count, 1u);

  harness.click_selection(edge.position, 2);
  const auto loop = harness.editor.selection().summary();
  ASSERT_EQ(loop.selected_edge_count, 4u);
  EXPECT_EQ(loop.active_object_id, box.modeling_object_id);
  EXPECT_EQ(loop.mode, quader::QuaderSelectionMode::Edge);

  harness.input.modifiers.primary = true;
  harness.click_selection(edge.position, 2);
  EXPECT_EQ(harness.editor.selection().summary().selected_edge_count, 0u);
  harness.input.modifiers = {.shift = true};
  harness.click_selection(edge.position, 2);
  EXPECT_EQ(harness.editor.selection().summary().selected_edge_count,
            loop.selected_edge_count);
}

TEST(ViewportInteractionControllerTests,
     CoalescedGizmoDoubleClickSelectsThePoleFacePerimeterOnce) {
  ViewportInteractionHarness harness;
  (void)create_unit_box(harness);
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Edge)
                  .ok());
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Edge);
  harness.camera.view_axis(quader::ViewportViewAxis::Z, 1);
  const auto edge = harness.camera.project_world_point({0.0f, 1.0f, 1.0f});
  ASSERT_TRUE(edge.visible);
  harness.click_selection(edge.position);
  ASSERT_EQ(harness.editor.selection().summary().selected_edge_count, 1u);
  ASSERT_TRUE(harness.editor.tools().activate(quader::ViewportToolId::Move).ok());
  harness.router.set_active_tool(quader::ViewportToolId::Move);
  (void)harness.controller.handle_before_active_tool(harness.editor,
                                                     harness.frame());
  const auto *gizmo = harness.controller.transform_gizmo_frame();
  ASSERT_NE(gizmo, nullptr);
  const auto *axis = find_axis(*gizmo, quader_editor_core::TransformAxis::X);
  ASSERT_NE(axis, nullptr);
  const auto hit = axis_screen_point(*axis, 0.25f);
  harness.input.mouse_local = hit;
  harness.input.mouse_screen = hit;

  auto press = harness.frame(true, true, false, hit);
  const auto before_press =
      harness.controller.handle_before_active_tool(harness.editor, press);
  ASSERT_TRUE(before_press.gizmo_consumed);
  harness.controller.handle_after_active_tool(
      harness.editor, press, {}, before_press);
  auto combined = harness.frame(true, true, true, hit);
  combined.left_click_count = 2;
  const auto before_combined =
      harness.controller.handle_before_active_tool(harness.editor, combined);
  ASSERT_TRUE(before_combined.selection_click_requested);
  EXPECT_FALSE(before_combined.selection_cycle_depth);
  EXPECT_EQ(before_combined.selection_click_count, 2);
  harness.controller.handle_after_active_tool(
      harness.editor, combined, {}, before_combined);
  const auto loop = harness.editor.selection().summary();
  EXPECT_EQ(loop.selected_edge_count, 4u);

  auto release = harness.frame(false, false, true, hit);
  release.left_click_count = 2;
  const auto before_release =
      harness.controller.handle_before_active_tool(harness.editor, release);
  harness.controller.handle_after_active_tool(
      harness.editor, release, {}, before_release);
  EXPECT_EQ(harness.editor.selection().summary().selected_edge_count,
            loop.selected_edge_count);
  EXPECT_FALSE(harness.controller.dragging());
}

TEST(ViewportInteractionControllerTests,
     OrthographicDepthCycleSurvivesNativeClickPairsAndWraps) {
  ViewportInteractionHarness harness;
  const auto front = create_box_at(harness, {0.0f, 0.0f, 3.0f});
  const auto middle = create_box_at(harness, {0.0f, 0.0f, 0.0f});
  const auto back = create_box_at(harness, {0.0f, 0.0f, -3.0f});
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Object)
                  .ok());
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
  harness.camera.view_axis(quader::ViewportViewAxis::Z, 1);
  harness.input.selection_click_delay_for_double_click = true;
  const quader::QVec2 hit{400.0f, 300.0f};

  for (const auto expected : {middle.entity, back.entity, front.entity}) {
    const auto selected_before = selected_entities(harness);
    const auto revision_before = harness.session.scene().selection_revision();
    harness.session.history().clear();
    harness.click_selection(hit);
    ASSERT_EQ(selected_entities(harness), selected_before);
    EXPECT_EQ(harness.session.scene().selection_revision(), revision_before);
    EXPECT_FALSE(harness.editor.history().can_undo());
    harness.click_selection(hit, 2, true);
    EXPECT_EQ(selected_entities(harness),
              std::vector<quader::EntityId>{expected});
    EXPECT_EQ(harness.session.scene().selection_revision(), revision_before + 1u);
    EXPECT_EQ(harness.editor.selection().hover_summary().target.kind,
              quader::QuaderActiveSelectionKind::None);
    EXPECT_FALSE(harness.controller.dragging());
  }
}

TEST(ViewportInteractionControllerTests,
     PublicApiAndViewportDepthCyclesShareOneTypedArbitrationCursor) {
  ViewportInteractionHarness harness;
  const auto front = create_box_at(harness, {0.0f, 0.0f, 3.0f});
  const auto middle = create_box_at(harness, {0.0f, 0.0f, 0.0f});
  const auto back = create_box_at(harness, {0.0f, 0.0f, -3.0f});
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Object)
                  .ok());
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
  harness.camera.view_axis(quader::ViewportViewAxis::Z, 1);
  harness.input.selection_click_delay_for_double_click = true;
  const quader::QVec2 hit{400.0f, 300.0f};
  const quader::ViewportRay ray =
      harness.camera.ray_from_viewport_point(hit);
  const quader_editor_core::ViewportCameraState runtime_camera =
      quader::ViewportCameraAdapter::runtime_camera(
          harness.camera, harness.camera.viewport_size());
  quader::QuaderPickSelectRequest api_cycle;
  api_cycle.ray = {.origin = ray.origin, .direction = ray.direction};
  api_cycle.click_count = 1;
  api_cycle.vertex_radius = harness.settings.float_value(
      quader::ViewportFloatSettingId::PickVertexRadius);
  api_cycle.edge_radius = harness.settings.float_value(
      quader::ViewportFloatSettingId::PickEdgeRadius);
  api_cycle.select_backfaces = harness.settings.bool_value(
      quader::ViewportBoolSettingId::BackfaceSelection);
  api_cycle.cycle_depth = true;
  api_cycle.cycle_origin_tolerance =
      quader_editor_core::kSelectionBoxDefaultDragThreshold *
      quader_editor_core::viewport_world_units_per_pixel(runtime_camera,
                                                          ray.origin);

  harness.click_selection(hit, 2, true);
  EXPECT_EQ(selected_entities(harness),
            std::vector<quader::EntityId>{middle.entity});

  const auto public_cycle = harness.editor.selection().pick_select(api_cycle);
  ASSERT_TRUE(public_cycle.ok()) << public_cycle.error_message();
  EXPECT_EQ(selected_entities(harness),
            std::vector<quader::EntityId>{back.entity});

  harness.click_selection(hit, 2, true);
  EXPECT_EQ(selected_entities(harness),
            std::vector<quader::EntityId>{front.entity});

  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Face)
                  .ok());
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Object)
                  .ok());
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
  const auto restarted = harness.editor.selection().pick_select(api_cycle);
  ASSERT_TRUE(restarted.ok()) << restarted.error_message();
  EXPECT_EQ(selected_entities(harness),
            std::vector<quader::EntityId>{middle.entity});
}

TEST(ViewportInteractionControllerTests,
     PublicApiAndViewportMarqueeResolveTheSameMixedProviderTargets) {
  ViewportInteractionHarness harness;
  const auto box = create_box_at(harness, {-2.0f, 0.0f, 0.0f});
  const auto curve = harness.editor.modeling().create_curve({
      .points = {{.position = {1.0f, -1.0f, 0.0f}},
                 {.position = {3.0f, 1.0f, 0.0f}}},
  });
  ASSERT_TRUE(curve.ok()) << curve.error_message();
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Object)
                  .ok());
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
  harness.camera.view_axis(quader::ViewportViewAxis::Z, 1);

  const quader::QVec2 start{550.0f, 450.0f};
  const quader::QVec2 end{250.0f, 150.0f};
  const quader::ModelingViewportCamera modeling_camera =
      quader::ViewportCameraAdapter::modeling_camera(
          harness.camera, harness.input.viewport_size);
  quader::QuaderBoxSelectRequest request;
  request.camera = {
      .orthographic = modeling_camera.orthographic,
      .origin = modeling_camera.origin,
      .x_axis = modeling_camera.x_axis,
      .y_axis = modeling_camera.y_axis,
      .z_axis = modeling_camera.z_axis,
      .viewport_size = modeling_camera.viewport_size,
      .orthographic_size = modeling_camera.orthographic_size,
      .fov_degrees = modeling_camera.fov_degrees,
  };
  request.start = start;
  request.end = end;
  request.rule = quader::QuaderBoxSelectionRule::DragDirection;
  const auto public_marquee = harness.editor.selection().box_select(request);
  ASSERT_TRUE(public_marquee.ok()) << public_marquee.error_message();
  const std::vector<quader::EntityId> public_targets =
      selected_entities(harness);
  ASSERT_EQ(public_targets.size(), 2u);
  EXPECT_NE(std::ranges::find(public_targets, box.entity),
            public_targets.end());
  EXPECT_NE(std::ranges::find(public_targets, curve.value().entity),
            public_targets.end());

  ASSERT_TRUE(harness.editor.selection().clear().ok());
  drag_selection_marquee(harness, start, end);
  EXPECT_EQ(selected_entities(harness), public_targets);
}

TEST(ViewportInteractionControllerTests,
     OrthographicDepthCycleToleratesSmallPointerJitter) {
  ViewportInteractionHarness harness;
  const auto front = create_box_at(harness, {0.0f, 0.0f, 3.0f});
  const auto middle = create_box_at(harness, {0.0f, 0.0f, 0.0f});
  const auto back = create_box_at(harness, {0.0f, 0.0f, -3.0f});
  ASSERT_TRUE(harness.editor.selection().select_object(front).ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
  harness.camera.view_axis(quader::ViewportViewAxis::Z, 1);
  harness.input.selection_click_delay_for_double_click = true;

  harness.click_selection({400.0f, 300.0f});
  harness.click_selection({401.0f, 300.0f}, 2, true);
  ASSERT_EQ(selected_entities(harness),
            std::vector<quader::EntityId>{middle.entity});
  harness.click_selection({402.0f, 300.0f});
  harness.click_selection({402.0f, 301.0f}, 2, true);
  ASSERT_EQ(selected_entities(harness),
            std::vector<quader::EntityId>{back.entity});

  harness.click_selection({410.0f, 300.0f});
  harness.click_selection({410.0f, 300.0f}, 2, true);
  EXPECT_EQ(selected_entities(harness),
            std::vector<quader::EntityId>{middle.entity});
}

TEST(ViewportInteractionControllerTests,
     DepthCycleIgnoresNonMeshSelectionModesEvenWithExplicitSemanticInput) {
  for (const auto mode : {quader::QuaderSelectionMode::Vertex,
                          quader::QuaderSelectionMode::Edge,
                          quader::QuaderSelectionMode::Face,
                          quader::QuaderSelectionMode::Group}) {
    SCOPED_TRACE(static_cast<int>(mode));
    ViewportInteractionHarness harness;
    (void)create_unit_box(harness);
    ASSERT_TRUE(harness.editor.selection().set_mode(mode).ok());
    ASSERT_TRUE(harness.editor.selection().clear().ok());
    harness.router.set_selection_mode(
        mode == quader::QuaderSelectionMode::Vertex
            ? quader::ViewportSelectionMode::Vertex
        : mode == quader::QuaderSelectionMode::Edge
            ? quader::ViewportSelectionMode::Edge
        : mode == quader::QuaderSelectionMode::Face
            ? quader::ViewportSelectionMode::Face
            : quader::ViewportSelectionMode::Group);
    harness.camera.view_axis(quader::ViewportViewAxis::Z, 1);
    const auto before = harness.editor.selection().summary();
    harness.input.cycle_selection_depth_pressed = true;
    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(), {}, {});
    const auto after = harness.editor.selection().summary();
    EXPECT_EQ(after.selection_revision, before.selection_revision);
    EXPECT_FALSE(after.has_selection);
    EXPECT_EQ(after.mode, mode);
  }
}

TEST(ViewportInteractionControllerTests,
     ReboundDepthCycleUsesCursorAndDoesNotTreatBindingModifiersAsRemoval) {
  ViewportInteractionHarness harness;
  const auto front = create_box_at(harness, {0.0f, 0.0f, 2.0f});
  const auto back = create_box_at(harness, {0.0f, 0.0f, -2.0f});
  ASSERT_TRUE(harness.editor.selection().select_object(front).ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
  harness.camera.view_axis(quader::ViewportViewAxis::Z, 1);
  harness.input.modifiers.primary = true;
  harness.input.cycle_selection_depth_pressed = true;
  harness.controller.handle_after_active_tool(
      harness.editor, harness.frame(), {}, {});
  EXPECT_EQ(selected_entities(harness),
            std::vector<quader::EntityId>{back.entity});
}

TEST(ViewportInteractionControllerTests,
     ModifiedDepthDoubleClickDiscardsStaleFirstClickBaselines) {
  /// Identifies a change that makes the saved first-click selection obsolete.
  enum class Invalidation {
    ExternalSelection,
    CameraPan,
    ExternalSelectionWhileDoubleHeld,
    CameraPanWhileDoubleHeld
  };
  for (const auto invalidation : {Invalidation::ExternalSelection,
                                  Invalidation::CameraPan,
                                  Invalidation::ExternalSelectionWhileDoubleHeld,
                                  Invalidation::CameraPanWhileDoubleHeld}) {
    SCOPED_TRACE(static_cast<int>(invalidation));
    ViewportInteractionHarness harness;
    (void)create_box_at(harness, {0.0f, 0.0f, 2.0f});
    const auto back = create_box_at(harness, {0.0f, 0.0f, -2.0f});
    const auto original = create_box_at(harness, {5.0f, 5.0f, 0.0f});
    const auto replacement = create_box_at(harness, {-5.0f, 5.0f, 0.0f});
    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Object)
                    .ok());
    ASSERT_TRUE(harness.editor.selection().select_object(original).ok());
    harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
    harness.camera.view_axis(quader::ViewportViewAxis::Z, 1);
    harness.input.selection_click_delay_for_double_click = true;
    harness.monotonic_time_seconds = 10.0;
    const quader::QVec2 hit{400.0f, 300.0f};
    harness.click_selection(hit);
    ASSERT_EQ(selected_entities(harness),
              std::vector<quader::EntityId>{original.entity});

    harness.monotonic_time_seconds += 0.1;
    const bool external_selection =
        invalidation == Invalidation::ExternalSelection ||
        invalidation == Invalidation::ExternalSelectionWhileDoubleHeld;
    const bool while_held =
        invalidation == Invalidation::ExternalSelectionWhileDoubleHeld ||
        invalidation == Invalidation::CameraPanWhileDoubleHeld;
    const auto invalidate = [&]() {
      if (external_selection) {
        ASSERT_TRUE(harness.editor.selection().select_object(replacement).ok());
      } else {
        // A changed camera pose invalidates even without a navigation frame.
        harness.camera.begin_drag(hit, quader::ViewportDragMode::Pan);
        harness.camera.update_drag(hit + quader::QVec2{2.0f, 0.0f});
        harness.camera.end_drag();
      }
    };
    if (while_held) {
      harness.input.cycle_selection_depth_pressed = true;
      harness.input.cycle_selection_depth_modifiers = {.shift = true};
      auto press = harness.frame(true, true, false, hit);
      press.left_click_count = 2;
      const auto before_press =
          harness.controller.handle_before_active_tool(harness.editor, press);
      harness.controller.handle_after_active_tool(harness.editor, press, {},
                                                  before_press);
      invalidate();
      harness.input.cycle_selection_depth_pressed = false;
      harness.input.cycle_selection_depth_modifiers = {};
      auto release = harness.frame(false, false, true, hit);
      release.left_click_count = 2;
      const auto before_release =
          harness.controller.handle_before_active_tool(harness.editor, release);
      harness.controller.handle_after_active_tool(harness.editor, release, {},
                                                  before_release);
    } else {
      invalidate();
      harness.click_selection(hit, 2, true, {.shift = true});
    }
    auto selected = selected_entities(harness);
    const quader::EntityId retained =
        external_selection ? replacement.entity : original.entity;
    std::vector<quader::EntityId> expected{retained, back.entity};
    std::sort(selected.begin(), selected.end());
    std::sort(expected.begin(), expected.end());
    EXPECT_EQ(selected, expected);
    EXPECT_FALSE(harness.controller.dragging());
  }
}

TEST(ViewportInteractionControllerTests,
     RecognizedModifiedDoubleClickKeepsBaselineAfterDelayedFrame) {
  ViewportInteractionHarness harness;
  (void)create_box_at(harness, {0.0f, 0.0f, 2.0f});
  const auto back = create_box_at(harness, {0.0f, 0.0f, -2.0f});
  const auto original = create_box_at(harness, {5.0f, 5.0f, 0.0f});
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Object)
                  .ok());
  ASSERT_TRUE(harness.editor.selection().select_object(original).ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
  harness.camera.view_axis(quader::ViewportViewAxis::Z, 1);
  harness.input.selection_click_delay_for_double_click = true;
  harness.input.selection_click_modifiers = {.shift = true};
  harness.monotonic_time_seconds = 10.0;
  harness.session.history().clear();
  const auto revision_before = harness.session.scene().selection_revision();
  const quader::QVec2 hit{400.0f, 300.0f};
  harness.click_selection(hit);
  ASSERT_EQ(selected_entities(harness),
            std::vector<quader::EntityId>{original.entity});
  EXPECT_EQ(harness.session.scene().selection_revision(), revision_before);
  EXPECT_FALSE(harness.editor.history().can_undo());

  // The native double was recognized before this slow frame consumed it.
  harness.monotonic_time_seconds = 20.0;
  harness.click_selection(hit, 2, true, {.shift = true});
  auto selected = selected_entities(harness);
  std::vector<quader::EntityId> expected{original.entity, back.entity};
  std::sort(selected.begin(), selected.end());
  std::sort(expected.begin(), expected.end());
  EXPECT_EQ(selected, expected);
  EXPECT_EQ(harness.session.scene().selection_revision(), revision_before + 1u);
}

TEST(ViewportInteractionControllerTests,
     ReboundModifiedDepthActionsIgnoreEarlierMouseSelectionBaselines) {
  for (const bool remove : {false, true}) {
    SCOPED_TRACE(remove);
    ViewportInteractionHarness harness;
    const auto front = create_box_at(harness, {0.0f, 0.0f, 2.0f});
    const auto back = create_box_at(harness, {0.0f, 0.0f, -2.0f});
    const auto original = create_box_at(harness, {5.0f, 5.0f, 0.0f});
    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Object)
                    .ok());
    ASSERT_TRUE(harness.editor.selection().select_object(original).ok());
    harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
    harness.camera.view_axis(quader::ViewportViewAxis::Z, 1);
    harness.click_selection({400.0f, 300.0f});
    ASSERT_EQ(selected_entities(harness),
              std::vector<quader::EntityId>{front.entity});

    // The resolver chooses the operation; modifiers on a rebound physical key
    // do not change it or make the key part of the preceding mouse gesture.
    harness.input.modifiers = {.primary = !remove, .shift = remove};
    harness.input.cycle_selection_depth_pressed = true;
    harness.input.cycle_selection_depth_modifiers = {
        .primary = remove, .shift = !remove};
    const auto keyboard_frame = harness.frame();
    const auto before_keyboard =
        harness.controller.handle_before_active_tool(harness.editor,
                                                     keyboard_frame);
    harness.controller.handle_after_active_tool(
        harness.editor, keyboard_frame, {}, before_keyboard);
    std::vector<quader::EntityId> expected{front.entity};
    if (!remove) {
      expected.push_back(back.entity);
    }
    std::sort(expected.begin(), expected.end());
    harness.input.modifiers = {};
    harness.input.cycle_selection_depth_pressed = false;
    harness.input.cycle_selection_depth_modifiers = {};
    for (int idle_frame = 0; idle_frame < 2; ++idle_frame) {
      auto selected = selected_entities(harness);
      std::sort(selected.begin(), selected.end());
      EXPECT_EQ(selected, expected);
      const auto frame = harness.frame();
      const auto before = harness.controller.handle_before_active_tool(
          harness.editor, frame);
      harness.controller.handle_after_active_tool(harness.editor, frame, {},
                                                  before);
    }
  }
}

TEST(ViewportInteractionControllerTests,
     DeferredOrthographicSingleUsesCapturedRayAndModifiersExactlyOnce) {
  for (const auto edit : {quader::QuaderSelectionEdit::Replace,
                          quader::QuaderSelectionEdit::Add,
                          quader::QuaderSelectionEdit::Remove}) {
    SCOPED_TRACE(static_cast<int>(edit));
    ViewportInteractionHarness harness;
    const auto front = create_box_at(harness, {0.0f, 0.0f, 2.0f});
    const auto original = create_box_at(harness, {5.0f, 5.0f, 0.0f});
    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Object)
                    .ok());
    ASSERT_TRUE(harness.editor.selection().select_object(original).ok());
    if (edit == quader::QuaderSelectionEdit::Remove) {
      ASSERT_TRUE(harness.editor.selection()
                      .select_object(front, quader::QuaderSelectionEdit::Add)
                      .ok());
    }
    ASSERT_TRUE(harness.editor.tools().activate(quader::ViewportToolId::Select)
                    .ok());
    harness.router.set_active_tool(quader::ViewportToolId::Select);
    harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
    harness.camera.view_axis(quader::ViewportViewAxis::Z, 1);
    harness.session.history().clear();
    const auto selected_before = selected_entities(harness);
    const auto active_before = harness.session.scene().primary_selection();
    const auto revision_before = harness.session.scene().selection_revision();
    const quader::QVec2 hit{400.0f, 300.0f};
    harness.monotonic_time_seconds = 10.0;
    harness.input.selection_click_delay_for_double_click = true;
    harness.input.selection_click_modifiers = {
        .primary = edit == quader::QuaderSelectionEdit::Remove,
        .shift = edit == quader::QuaderSelectionEdit::Add};
    (void)harness.dispatch_frame(harness.frame(true, true, false, hit));
    harness.input.selection_click_delay_for_double_click = false;
    harness.input.selection_click_modifiers = {};
    harness.monotonic_time_seconds = 10.1;
    (void)harness.dispatch_frame(harness.frame(false, false, true, hit));
    EXPECT_EQ(selected_entities(harness), selected_before);
    EXPECT_EQ(harness.session.scene().primary_selection(), active_before);
    EXPECT_EQ(harness.session.scene().selection_revision(), revision_before);
    EXPECT_FALSE(harness.editor.history().can_undo());

    // Hover may move and physical modifiers may change while the single waits.
    harness.input.mouse_local = {20.0f, 20.0f};
    harness.input.mouse_screen = harness.input.mouse_local;
    harness.input.modifiers = {
        .primary = edit != quader::QuaderSelectionEdit::Remove,
        .shift = edit == quader::QuaderSelectionEdit::Remove};
    harness.monotonic_time_seconds = 10.499;
    auto idle = harness.frame();
    idle.left_click_count = 2; // A stale count without a new edge is not a double.
    (void)harness.dispatch_frame(idle);
    EXPECT_EQ(selected_entities(harness), selected_before);
    EXPECT_EQ(harness.session.scene().selection_revision(), revision_before);
    EXPECT_FALSE(harness.editor.history().can_undo());

    std::vector<quader::EntityId> expected;
    if (edit != quader::QuaderSelectionEdit::Remove) {
      expected.push_back(front.entity);
    }
    if (edit != quader::QuaderSelectionEdit::Replace) {
      expected.push_back(original.entity);
    }
    for (const double time : {10.5, 11.0, 20.0}) {
      harness.monotonic_time_seconds = time;
      auto tick = harness.frame();
      tick.left_click_count = 2;
      (void)harness.dispatch_frame(tick);
      EXPECT_EQ(selected_entities(harness), expected);
      EXPECT_EQ(harness.session.scene().selection_revision(), revision_before + 1u);
      EXPECT_EQ(harness.editor.history().snapshot().undo_label, "Select Entities");
      EXPECT_FALSE(harness.session.modeling().transform_drag_active());
    }
    ASSERT_TRUE(harness.editor.history().undo().ok());
    EXPECT_EQ(selected_entities(harness), selected_before);
    EXPECT_EQ(harness.session.scene().primary_selection(), active_before);
    EXPECT_FALSE(harness.editor.history().can_undo());
    ASSERT_TRUE(harness.editor.history().redo().ok());
    EXPECT_EQ(selected_entities(harness), expected);
  }
}

TEST(ViewportInteractionControllerTests,
     HeldOrthographicSingleCommitsOnReleaseWithoutAnotherDelay) {
  ViewportInteractionHarness harness;
  const auto front = create_unit_box(harness);
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Object)
                  .ok());
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
  harness.camera.view_axis(quader::ViewportViewAxis::Z, 1);
  harness.session.history().clear();
  const auto revision_before = harness.session.scene().selection_revision();
  const quader::QVec2 hit{400.0f, 300.0f};
  harness.input.selection_click_delay_for_double_click = true;
  harness.monotonic_time_seconds = 10.0;
  (void)harness.dispatch_frame(harness.frame(true, true, false, hit));
  harness.input.selection_click_delay_for_double_click = false;
  harness.monotonic_time_seconds = 11.0;
  (void)harness.dispatch_frame(harness.frame(false, true, false, hit));
  EXPECT_TRUE(selected_entities(harness).empty());
  EXPECT_FALSE(harness.editor.history().can_undo());
  (void)harness.dispatch_frame(harness.frame(false, false, true, hit));
  EXPECT_EQ(selected_entities(harness),
            std::vector<quader::EntityId>{front.entity});
  EXPECT_EQ(harness.session.scene().selection_revision(), revision_before + 1u);
  harness.monotonic_time_seconds = 20.0;
  (void)harness.dispatch_frame(harness.frame());
  EXPECT_EQ(harness.session.scene().selection_revision(), revision_before + 1u);
}

TEST(ViewportInteractionControllerTests,
     DeferredOrthographicSingleCancelsOnContextChange) {
  /// Names events that invalidate a not-yet-published ordinary selection click.
  enum class Cancellation { CameraPan, Navigation, Mode, Selection, Escape, Leave };
  for (const auto cancellation : {Cancellation::CameraPan,
                                  Cancellation::Navigation,
                                  Cancellation::Mode,
                                  Cancellation::Selection,
                                  Cancellation::Escape,
                                  Cancellation::Leave}) {
    SCOPED_TRACE(static_cast<int>(cancellation));
    ViewportInteractionHarness harness;
    (void)create_unit_box(harness);
    const auto original = create_box_at(harness, {5.0f, 5.0f, 0.0f});
    const auto replacement = create_box_at(harness, {-5.0f, 5.0f, 0.0f});
    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Object)
                    .ok());
    ASSERT_TRUE(harness.editor.selection().select_object(original).ok());
    harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
    harness.camera.view_axis(quader::ViewportViewAxis::Z, 1);
    harness.session.history().clear();
    harness.monotonic_time_seconds = 10.0;
    harness.input.selection_click_delay_for_double_click = true;
    harness.click_selection({400.0f, 300.0f});
    ASSERT_EQ(selected_entities(harness),
              std::vector<quader::EntityId>{original.entity});
    EXPECT_FALSE(harness.editor.history().can_undo());
    harness.input.selection_click_delay_for_double_click = false;
    switch (cancellation) {
    case Cancellation::CameraPan:
      harness.camera.begin_drag({400.0f, 300.0f}, quader::ViewportDragMode::Pan);
      harness.camera.update_drag({402.0f, 300.0f});
      harness.camera.end_drag();
      break;
    case Cancellation::Navigation:
      harness.input_result.navigation_active = true;
      break;
    case Cancellation::Mode:
      ASSERT_TRUE(harness.editor.selection()
                      .set_mode(quader::QuaderSelectionMode::Face)
                      .ok());
      harness.router.set_selection_mode(quader::ViewportSelectionMode::Face);
      break;
    case Cancellation::Selection:
      ASSERT_TRUE(harness.editor.selection().select_object(replacement).ok());
      break;
    case Cancellation::Escape:
      harness.input_result.cancel_requested = true;
      break;
    case Cancellation::Leave:
      harness.input.viewport_hovered = false;
      break;
    }
    harness.monotonic_time_seconds = 10.2;
    (void)harness.dispatch_frame(harness.frame());
    harness.input_result.navigation_active = false;
    harness.input_result.cancel_requested = false;
    harness.input.viewport_hovered = true;
    if (cancellation == Cancellation::Mode) {
      ASSERT_TRUE(harness.editor.selection()
                      .set_mode(quader::QuaderSelectionMode::Object)
                      .ok());
      harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
    }
    const auto selected = selected_entities(harness);
    const auto active = harness.session.scene().primary_selection();
    const auto revision = harness.session.scene().selection_revision();
    const auto history = harness.editor.history().snapshot();
    harness.monotonic_time_seconds = 20.0;
    (void)harness.dispatch_frame(harness.frame());
    EXPECT_EQ(selected_entities(harness), selected);
    EXPECT_EQ(harness.session.scene().primary_selection(), active);
    EXPECT_EQ(harness.session.scene().selection_revision(), revision);
    EXPECT_EQ(harness.editor.history().snapshot().can_undo, history.can_undo);
    EXPECT_EQ(harness.editor.history().snapshot().can_redo, history.can_redo);
    EXPECT_EQ(harness.editor.history().snapshot().undo_label, history.undo_label);
    EXPECT_EQ(harness.editor.history().snapshot().redo_label, history.redo_label);
  }
}

TEST(ViewportInteractionControllerTests,
     EscapeOrFocusLossDiscardsFirstHeldClickAndPendingGizmo) {
  for (const auto [tool, focus_loss] : {
           std::pair{quader::ViewportToolId::Select, false},
           std::pair{quader::ViewportToolId::Move, false},
           std::pair{quader::ViewportToolId::Select, true},
           std::pair{quader::ViewportToolId::Move, true}}) {
    SCOPED_TRACE(static_cast<int>(tool));
    SCOPED_TRACE(focus_loss);
    ViewportInteractionHarness harness;
    (void)create_box_at(harness, {0.0f, 0.0f, 2.0f});
    const auto back = create_box_at(harness, {0.0f, 0.0f, -2.0f});
    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Object)
                    .ok());
    ASSERT_TRUE(harness.editor.selection().select_object(back).ok());
    ASSERT_TRUE(harness.editor.tools().activate(tool).ok());
    harness.router.set_active_tool(tool);
    harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
    harness.camera.view_axis(quader::ViewportViewAxis::Z, 1);
    harness.session.history().clear();
    const auto revision_before = harness.session.scene().selection_revision();
    (void)harness.dispatch_frame(harness.frame());
    quader::QVec2 hit{400.0f, 300.0f};
    if (tool == quader::ViewportToolId::Move) {
      const auto *gizmo = harness.controller.transform_gizmo_frame();
      ASSERT_NE(gizmo, nullptr);
      const auto *axis = find_axis(*gizmo, quader_editor_core::TransformAxis::X);
      ASSERT_NE(axis, nullptr);
      hit = axis_screen_point(*axis, 0.25f);
    }
    harness.input.mouse_local = hit;
    harness.input.mouse_screen = hit;
    harness.input.selection_click_delay_for_double_click = true;
    const auto press = harness.dispatch_frame(harness.frame(true, true, false, hit));
    EXPECT_EQ(press.gizmo_consumed, tool == quader::ViewportToolId::Move);
    harness.input.selection_click_delay_for_double_click = false;
    harness.monotonic_time_seconds = 0.1;
    if (focus_loss) {
      // Native focus loss can synthesize a release while the pointer remains
      // over the pane; it must not become an ordinary deferred gizmo click.
      harness.input.viewport_focused = false;
      ASSERT_TRUE(harness.input.viewport_hovered);
      const auto release =
          harness.dispatch_frame(harness.frame(false, false, true, hit));
      EXPECT_FALSE(release.selection_click_requested);
      harness.input.viewport_focused = true;
    } else {
      harness.input_result.cancel_requested = true;
      (void)harness.dispatch_frame(harness.frame(false, true, false, hit));
      harness.input_result.cancel_requested = false;
      harness.monotonic_time_seconds = 0.2;
      (void)harness.dispatch_frame(harness.frame(false, false, true, hit));
    }
    for (const double time : {0.2, 1.0}) {
      harness.monotonic_time_seconds = time;
      (void)harness.dispatch_frame(harness.frame());
      EXPECT_EQ(selected_entities(harness),
                std::vector<quader::EntityId>{back.entity});
      EXPECT_EQ(harness.session.scene().selection_revision(), revision_before);
      EXPECT_FALSE(harness.editor.history().can_undo());
      EXPECT_FALSE(harness.controller.dragging());
      EXPECT_FALSE(harness.session.modeling().transform_drag_active());
    }
  }
}

TEST(ViewportInteractionControllerTests,
     DistinctDeferredShiftClicksRefreshGizmoAndBoxSelectionBaselines) {
  for (const auto tool : {quader::ViewportToolId::Select,
                          quader::ViewportToolId::Move}) {
    for (const bool marquee : {false, true}) {
      SCOPED_TRACE(static_cast<int>(tool));
      SCOPED_TRACE(marquee);
      ViewportInteractionHarness harness;
      const auto first = create_box_at(harness, {-3.0f, 0.0f, 0.0f});
      const auto second = create_box_at(harness, {3.0f, 0.0f, 0.0f});
      ASSERT_TRUE(harness.editor.selection()
                      .set_mode(quader::QuaderSelectionMode::Object)
                      .ok());
      ASSERT_TRUE(harness.editor.selection().clear().ok());
      ASSERT_TRUE(harness.editor.tools().activate(tool).ok());
      harness.router.set_active_tool(tool);
      harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
      harness.camera.view_axis(quader::ViewportViewAxis::Z, 1);
      harness.camera.focus_bounds({-5.0f, -3.0f, -1.0f},
                                  {5.0f, 3.0f, 1.0f});
      const auto first_hit = harness.camera.project_world_point({-3.0f, 0.0f, 0.0f});
      const auto second_hit = harness.camera.project_world_point({3.0f, 0.0f, 0.0f});
      const auto upper_left = harness.camera.project_world_point({1.8f, 1.2f, 0.0f});
      const auto lower_right = harness.camera.project_world_point({4.2f, -1.2f, 0.0f});
      ASSERT_TRUE(first_hit.visible);
      ASSERT_TRUE(second_hit.visible);
      ASSERT_TRUE(upper_left.visible);
      ASSERT_TRUE(lower_right.visible);
      harness.session.history().clear();
      const auto revision_before = harness.session.scene().selection_revision();
      harness.input.modifiers = {.shift = true};
      harness.input.selection_click_delay_for_double_click = true;
      harness.input.selection_click_modifiers = {.shift = true};
      harness.monotonic_time_seconds = 10.0;
      harness.click_selection(first_hit.position);
      EXPECT_TRUE(selected_entities(harness).empty());
      EXPECT_FALSE(harness.editor.history().can_undo());

      const auto start = marquee ? upper_left.position : second_hit.position;
      harness.input.mouse_local = start;
      harness.input.mouse_screen = start;
      harness.monotonic_time_seconds = 10.1;
      const auto press = harness.frame(true, true, false, start);
      const auto before =
          harness.controller.handle_before_active_tool(harness.editor, press);
      ASSERT_TRUE(before.selection_after_pending_pick.has_value());
      EXPECT_EQ(before.selection_after_pending_pick->summary.active_object_id,
                first.modeling_object_id);
      EXPECT_EQ(before.selection_after_pending_pick->summary.selected_object_count,
                1u);
      EXPECT_EQ(before.selection_after_pending_pick->scene_revision,
                harness.session.scene().revision());
      EXPECT_EQ(selected_entities(harness),
                std::vector<quader::EntityId>{first.entity});
      if (tool == quader::ViewportToolId::Move) {
        const auto *gizmo = harness.controller.transform_gizmo_frame();
        ASSERT_NE(gizmo, nullptr);
        EXPECT_NEAR(gizmo->info.pivot.x, -3.0f, 0.001f);
        EXPECT_NEAR(gizmo->info.pivot.y, 0.0f, 0.001f);
      }
      ASSERT_FALSE(before.gizmo_consumed);
      harness.controller.handle_after_active_tool(harness.editor, press, {}, before);
      harness.monotonic_time_seconds = 10.2;
      if (marquee) {
        harness.input.mouse_local = lower_right.position;
        harness.input.mouse_screen = lower_right.position;
        (void)harness.dispatch_frame(harness.frame(false, true, false, start));
      }
      (void)harness.dispatch_frame(harness.frame(false, false, true, start));
      harness.input.modifiers = {};
      harness.input.selection_click_delay_for_double_click = false;
      harness.input.selection_click_modifiers = {};
      if (!marquee) {
        EXPECT_EQ(selected_entities(harness),
                  std::vector<quader::EntityId>{first.entity});
        EXPECT_EQ(harness.session.scene().selection_revision(), revision_before + 1u);
      }
      harness.monotonic_time_seconds = 10.7;
      (void)harness.dispatch_frame(harness.frame());
      EXPECT_EQ(selected_entities(harness),
                (std::vector<quader::EntityId>{first.entity, second.entity}));
      EXPECT_EQ(harness.session.scene().selection_revision(), revision_before + 2u);
      EXPECT_FALSE(harness.controller.dragging());
      ASSERT_TRUE(harness.editor.history().undo().ok());
      EXPECT_EQ(selected_entities(harness),
                std::vector<quader::EntityId>{first.entity});
      ASSERT_TRUE(harness.editor.history().undo().ok());
      EXPECT_TRUE(selected_entities(harness).empty());
      EXPECT_FALSE(harness.editor.history().can_undo());
    }
  }
}

TEST(ViewportInteractionControllerTests,
     NativeDoubleWithoutDepthActionCancelsTheDeferredOrdinarySingle) {
  ViewportInteractionHarness harness;
  const auto front = create_unit_box(harness);
  const auto original = create_box_at(harness, {5.0f, 5.0f, 0.0f});
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Object)
                  .ok());
  ASSERT_TRUE(harness.editor.selection().select_object(original).ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
  harness.camera.view_axis(quader::ViewportViewAxis::Z, 1);
  harness.session.history().clear();
  const auto revision_before = harness.session.scene().selection_revision();
  harness.input.selection_click_delay_for_double_click = true;
  harness.monotonic_time_seconds = 10.0;
  harness.click_selection({400.0f, 300.0f});
  EXPECT_EQ(selected_entities(harness),
            std::vector<quader::EntityId>{original.entity});
  EXPECT_FALSE(harness.editor.history().can_undo());
  harness.monotonic_time_seconds = 20.0;
  harness.click_selection({400.0f, 300.0f}, 2);
  EXPECT_EQ(selected_entities(harness),
            std::vector<quader::EntityId>{front.entity});
  EXPECT_EQ(harness.session.scene().selection_revision(), revision_before + 1u);
  harness.monotonic_time_seconds = 30.0;
  (void)harness.dispatch_frame(harness.frame());
  EXPECT_EQ(harness.session.scene().selection_revision(), revision_before + 1u);
  ASSERT_TRUE(harness.editor.history().undo().ok());
  EXPECT_EQ(selected_entities(harness),
            std::vector<quader::EntityId>{original.entity});
  EXPECT_FALSE(harness.editor.history().can_undo());
}

TEST(ViewportInteractionControllerTests,
     DepthCycleRejectsPerspectiveNavigationTextToolsAndConsumedInput) {
  /// Names each input owner or view state that must suppress depth cycling.
  enum class Guard {
    Perspective,
    Unfocused,
    Text,
    NavigationInput,
    NavigationResult,
    CameraChange,
    Preview,
    TransformDrag,
    ModalTool,
    OwningTool,
    ConsumedInput,
    HandledTool,
    ConsumedGizmo,
  };
  for (const auto guard :
       {Guard::Perspective, Guard::Unfocused, Guard::Text,
        Guard::NavigationInput, Guard::NavigationResult, Guard::CameraChange,
        Guard::Preview, Guard::TransformDrag, Guard::ModalTool,
        Guard::OwningTool, Guard::ConsumedInput, Guard::HandledTool,
        Guard::ConsumedGizmo}) {
    SCOPED_TRACE(static_cast<int>(guard));
    ViewportInteractionHarness harness;
    const auto front = create_box_at(harness, {0.0f, 0.0f, 2.0f});
    (void)create_box_at(harness, {0.0f, 0.0f, -2.0f});
    ASSERT_TRUE(harness.editor.selection().select_object(front).ok());
    harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
    if (guard != Guard::Perspective) {
      harness.camera.view_axis(quader::ViewportViewAxis::Z, 1);
    }
    harness.input.viewport_focused = guard != Guard::Unfocused;
    harness.input.text_input_active = guard == Guard::Text;
    harness.input.navigation_active = guard == Guard::NavigationInput;
    harness.input.operation_preview_active = guard == Guard::Preview;
    harness.input.active_transform_drag = guard == Guard::TransformDrag;
    harness.input.modal_tool_active = guard == Guard::ModalTool;
    harness.input.active_tool_owns_viewport = guard == Guard::OwningTool;
    harness.input_result.navigation_active = guard == Guard::NavigationResult;
    harness.input_result.camera_changed = guard == Guard::CameraChange;
    harness.input_result.consumed = guard == Guard::ConsumedInput;
    harness.input.cycle_selection_depth_pressed = true;
    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(),
        quader::ViewportToolResult{.handled = guard == Guard::HandledTool},
        quader::ViewportBeforeToolInteractionResult{
            .gizmo_consumed = guard == Guard::ConsumedGizmo});
    EXPECT_EQ(selected_entities(harness),
              std::vector<quader::EntityId>{front.entity});
  }
}

TEST(ViewportInteractionControllerTests,
     DepthCyclePendingClickCancelsWhenNavigationStartsBeforeRelease) {
  ViewportInteractionHarness harness;
  const auto front = create_box_at(harness, {0.0f, 0.0f, 2.0f});
  (void)create_box_at(harness, {0.0f, 0.0f, -2.0f});
  ASSERT_TRUE(harness.editor.selection().select_object(front).ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
  harness.camera.view_axis(quader::ViewportViewAxis::Z, 1);
  const quader::QVec2 hit{400.0f, 300.0f};
  harness.input.cycle_selection_depth_pressed = true;
  harness.controller.handle_after_active_tool(
      harness.editor, harness.frame(true, true, false, hit), {}, {});
  harness.input.cycle_selection_depth_pressed = false;
  harness.input_result.navigation_active = true;
  harness.controller.handle_after_active_tool(
      harness.editor, harness.frame(false, true, false, hit), {}, {});
  harness.input_result.navigation_active = false;
  auto release = harness.frame(false, false, true, hit);
  release.left_click_count = 2;
  harness.controller.handle_after_active_tool(harness.editor, release, {}, {});
  EXPECT_EQ(selected_entities(harness),
            std::vector<quader::EntityId>{front.entity});
}

TEST(ViewportInteractionControllerTests,
     OrthographicDepthCycleSurvivesDeferredGizmoClicksWithoutTransform) {
  for (const bool coalesced : {false, true}) {
    SCOPED_TRACE(coalesced);
    ViewportInteractionHarness harness;
    const auto front = create_box_at(harness, {0.0f, 0.0f, 2.0f});
    const auto back = create_box_at(harness, {0.0f, 0.0f, -2.0f});
    ASSERT_TRUE(harness.editor.selection().select_object(front).ok());
    ASSERT_TRUE(harness.editor.tools().activate(quader::ViewportToolId::Move).ok());
    harness.router.set_active_tool(quader::ViewportToolId::Move);
    harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
    harness.camera.view_axis(quader::ViewportViewAxis::Z, 1);
    (void)harness.controller.handle_before_active_tool(harness.editor,
                                                       harness.frame());
    const auto *gizmo = harness.controller.transform_gizmo_frame();
    ASSERT_NE(gizmo, nullptr);
    const auto *axis = find_axis(*gizmo, quader_editor_core::TransformAxis::X);
    ASSERT_NE(axis, nullptr);
    const auto hit = axis_screen_point(*axis, 0.25f);
    const auto before = *harness.session.scene().transform(front.entity);

    if (coalesced) {
      harness.input.mouse_local = hit;
      harness.input.mouse_screen = hit;
      harness.input.cycle_selection_depth_pressed = true;
      auto click = harness.frame(true, false, true, hit);
      click.left_click_count = 2;
      const auto before_tool =
          harness.controller.handle_before_active_tool(harness.editor, click);
      ASSERT_TRUE(before_tool.selection_click_requested);
      ASSERT_TRUE(before_tool.selection_cycle_depth);
      EXPECT_FALSE(before_tool.gizmo_consumed);
      harness.controller.handle_after_active_tool(
          harness.editor, click, {}, before_tool);
    } else {
      harness.click_selection(hit, 2, true);
    }
    EXPECT_EQ(selected_entities(harness),
              std::vector<quader::EntityId>{back.entity});
    EXPECT_FALSE(harness.controller.dragging());
    expect_transform_near(*harness.session.scene().transform(front.entity), before);
  }
}

TEST(ViewportInteractionControllerTests,
     ModifiedDepthCycleRetainsSelectionAcrossDeferredGizmoRelease) {
  for (const bool remove : {false, true}) {
    for (const bool coalesced : {false, true}) {
      SCOPED_TRACE(remove);
      SCOPED_TRACE(coalesced);
      ViewportInteractionHarness harness;
      const auto front = create_box_at(harness, {0.0f, 0.0f, 2.0f});
      const auto back = create_box_at(harness, {0.0f, 0.0f, -2.0f});
      ASSERT_TRUE(harness.editor.selection()
                      .set_mode(quader::QuaderSelectionMode::Object)
                      .ok());
      ASSERT_TRUE(harness.editor.selection().select_object(back).ok());
      if (remove) {
        ASSERT_TRUE(harness.editor.selection()
                        .select_object(front, quader::QuaderSelectionEdit::Add)
                        .ok());
      }
      ASSERT_TRUE(harness.editor.tools().activate(quader::ViewportToolId::Move)
                      .ok());
      harness.router.set_active_tool(quader::ViewportToolId::Move);
      harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
      harness.camera.view_axis(quader::ViewportViewAxis::Z, 1);
      harness.session.history().clear();
      const auto front_transform =
          *harness.session.scene().transform(front.entity);
      const auto back_transform = *harness.session.scene().transform(back.entity);
      const auto entity_count = harness.session.scene().entities().size();
      const auto content_revision = harness.session.modeling().content_revision();
      const auto selected_before = selected_entities(harness);
      const auto selection_revision_before =
          harness.session.scene().selection_revision();
      (void)harness.controller.handle_before_active_tool(harness.editor,
                                                         harness.frame());
      const auto *gizmo = harness.controller.transform_gizmo_frame();
      ASSERT_NE(gizmo, nullptr);
      const auto *axis = find_axis(*gizmo, quader_editor_core::TransformAxis::X);
      ASSERT_NE(axis, nullptr);
      const auto hit = axis_screen_point(*axis, 0.25f);
      const quader::ViewportModifiers operation{
          .primary = remove, .shift = !remove};
      harness.input.mouse_local = hit;
      harness.input.mouse_screen = hit;
      harness.input.modifiers = operation;
      harness.input.selection_click_delay_for_double_click = true;
      harness.input.selection_click_modifiers = operation;
      const auto first_press = harness.frame(true, true, false, hit);
      const auto before_first_press =
          harness.controller.handle_before_active_tool(harness.editor,
                                                       first_press);
      ASSERT_TRUE(before_first_press.gizmo_consumed);
      ASSERT_FALSE(before_first_press.selection_click_requested);
      harness.controller.handle_after_active_tool(
          harness.editor, first_press, {}, before_first_press);

      if (!coalesced) {
        harness.input.modifiers = {};
        harness.input.selection_click_delay_for_double_click = false;
        harness.input.selection_click_modifiers = {};
        const auto first_release = harness.frame(false, false, true, hit);
        const auto before_first_release =
            harness.controller.handle_before_active_tool(harness.editor,
                                                         first_release);
        ASSERT_TRUE(before_first_release.selection_click_requested);
        ASSERT_FALSE(before_first_release.selection_cycle_depth);
        harness.controller.handle_after_active_tool(
            harness.editor, first_release, {}, before_first_release);
        EXPECT_EQ(selected_entities(harness), selected_before);
        EXPECT_EQ(harness.session.scene().selection_revision(),
                  selection_revision_before);
        EXPECT_FALSE(harness.editor.history().can_undo());
      }

      harness.input.cycle_selection_depth_pressed = true;
      harness.input.cycle_selection_depth_modifiers = operation;
      auto double_press = harness.frame(true, true, coalesced, hit);
      double_press.left_click_count = 2;
      const auto before_double_press =
          harness.controller.handle_before_active_tool(harness.editor,
                                                       double_press);
      if (coalesced) {
        ASSERT_TRUE(before_double_press.selection_click_requested);
        ASSERT_TRUE(before_double_press.selection_cycle_depth);
        EXPECT_EQ(before_double_press.selection_cycle_depth_modifiers.primary,
                  operation.primary);
        EXPECT_EQ(before_double_press.selection_cycle_depth_modifiers.shift,
                  operation.shift);
      } else {
        ASSERT_TRUE(before_double_press.gizmo_consumed);
        ASSERT_FALSE(before_double_press.selection_click_requested);
      }
      harness.controller.handle_after_active_tool(
          harness.editor, double_press, {}, before_double_press);

      harness.input.modifiers = {};
      harness.input.selection_click_delay_for_double_click = false;
      harness.input.selection_click_modifiers = {};
      harness.input.cycle_selection_depth_pressed = false;
      harness.input.cycle_selection_depth_modifiers = {};
      auto release = harness.frame(false, false, true, hit);
      release.left_click_count = 2;
      const auto before_release =
          harness.controller.handle_before_active_tool(harness.editor, release);
      if (!coalesced) {
        ASSERT_TRUE(before_release.selection_click_requested);
        ASSERT_TRUE(before_release.selection_cycle_depth);
        EXPECT_EQ(before_release.selection_cycle_depth_modifiers.primary,
                  operation.primary);
        EXPECT_EQ(before_release.selection_cycle_depth_modifiers.shift,
                  operation.shift);
      }
      harness.controller.handle_after_active_tool(
          harness.editor, release, {}, before_release);
      harness.monotonic_time_seconds = 1.0;
      for (int idle_frame = 0; idle_frame < 2; ++idle_frame) {
        EXPECT_EQ(selected_entities(harness),
                  std::vector<quader::EntityId>{remove ? front.entity
                                                      : back.entity});
        EXPECT_FALSE(harness.controller.dragging());
        EXPECT_EQ(harness.session.scene().selection_revision(),
                  selection_revision_before + (remove ? 1u : 0u));
        EXPECT_EQ(harness.editor.history().can_undo(), remove);
        EXPECT_FALSE(harness.session.modeling().transform_drag_active());
        EXPECT_EQ(harness.session.scene().entities().size(), entity_count);
        EXPECT_EQ(harness.session.modeling().content_revision(), content_revision);
        expect_transform_near(
            *harness.session.scene().transform(front.entity), front_transform);
        expect_transform_near(*harness.session.scene().transform(back.entity),
                              back_transform);
        const auto frame = harness.frame();
        const auto before = harness.controller.handle_before_active_tool(
            harness.editor, frame);
        harness.controller.handle_after_active_tool(harness.editor, frame, {},
                                                    before);
      }
    }
  }
}

TEST(ViewportInteractionControllerTests,
     DepthCycleConsumesDoublePressOrReboundClickWhileFirstClickIsPending) {
  for (const auto [tool, release_only] : {
           std::pair{quader::ViewportToolId::Select, false},
           std::pair{quader::ViewportToolId::Move, false},
           std::pair{quader::ViewportToolId::Select, true},
           std::pair{quader::ViewportToolId::Move, true}}) {
    SCOPED_TRACE(static_cast<int>(tool));
    SCOPED_TRACE(release_only);
    ViewportInteractionHarness harness;
    const auto front = create_box_at(harness, {0.0f, 0.0f, 2.0f});
    const auto back = create_box_at(harness, {0.0f, 0.0f, -2.0f});
    ASSERT_TRUE(harness.editor.selection().select_object(front).ok());
    ASSERT_TRUE(harness.editor.tools().activate(tool).ok());
    harness.router.set_active_tool(tool);
    harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
    harness.camera.view_axis(quader::ViewportViewAxis::Z, 1);
    (void)harness.controller.handle_before_active_tool(harness.editor,
                                                       harness.frame());
    quader::QVec2 hit{400.0f, 300.0f};
    if (tool == quader::ViewportToolId::Move) {
      const auto *gizmo = harness.controller.transform_gizmo_frame();
      ASSERT_NE(gizmo, nullptr);
      const auto *axis = find_axis(*gizmo, quader_editor_core::TransformAxis::X);
      ASSERT_NE(axis, nullptr);
      hit = axis_screen_point(*axis, 0.25f);
    }
    harness.input.mouse_local = hit;
    harness.input.mouse_screen = hit;
    const auto before = *harness.session.scene().transform(front.entity);
    const auto dispatch = [&](quader::ViewportInteractionFrame frame) {
      const auto before_tool =
          harness.controller.handle_before_active_tool(harness.editor, frame);
      harness.controller.handle_after_active_tool(
          harness.editor, frame, {}, before_tool);
    };
    dispatch(harness.frame(true, true, false, hit));

    // Rebound clicks emit intent on release; the default double-click can
    // arrive in that same frame while the first press is still pending.
    harness.input.cycle_selection_depth_pressed = true;
    auto combined = harness.frame(!release_only, !release_only, true, hit);
    combined.left_click_count = release_only ? 1 : 2;
    dispatch(combined);
    EXPECT_EQ(selected_entities(harness),
              std::vector<quader::EntityId>{back.entity});

    harness.input.cycle_selection_depth_pressed = false;
    auto release = harness.frame(false, false, true, hit);
    release.left_click_count = combined.left_click_count;
    dispatch(release);
    EXPECT_EQ(selected_entities(harness),
              std::vector<quader::EntityId>{back.entity});
    EXPECT_FALSE(harness.controller.dragging());
    expect_transform_near(*harness.session.scene().transform(front.entity), before);
  }
}

TEST(ViewportInteractionControllerTests,
     DepthCycleCrossingIntoGizmoConsumesPendingSelectionBoxOnce) {
  ViewportInteractionHarness harness;
  const auto front = create_box_at(harness, {0.0f, 0.0f, 2.0f});
  const auto back = create_box_at(harness, {0.0f, 0.0f, -2.0f});
  ASSERT_TRUE(harness.editor.selection().select_object(front).ok());
  ASSERT_TRUE(harness.editor.tools().activate(quader::ViewportToolId::Move).ok());
  harness.router.set_active_tool(quader::ViewportToolId::Move);
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
  harness.camera.view_axis(quader::ViewportViewAxis::Z, 1);
  (void)harness.controller.handle_before_active_tool(harness.editor,
                                                     harness.frame());
  const auto *gizmo = harness.controller.transform_gizmo_frame();
  ASSERT_NE(gizmo, nullptr);
  const auto *axis = find_axis(*gizmo, quader_editor_core::TransformAxis::X);
  ASSERT_NE(axis, nullptr);
  const auto shaft = axis_screen_point(*axis, 0.25f);
  const auto outside = shaft + quader::QVec2{
      0.0f, quader::kOverlayTransformGizmoPickRadiusPixels + 0.5f};
  const auto inside = shaft + quader::QVec2{
      0.0f, quader::kOverlayTransformGizmoPickRadiusPixels - 0.5f};
  const auto before = *harness.session.scene().transform(front.entity);

  harness.input.mouse_local = outside;
  harness.input.mouse_screen = outside;
  auto press = harness.frame(true, true, false, outside);
  const auto before_press =
      harness.controller.handle_before_active_tool(harness.editor, press);
  ASSERT_FALSE(before_press.gizmo_consumed);
  harness.controller.handle_after_active_tool(
      harness.editor, press, {}, before_press);

  // One pixel of jitter transfers the pending click from selection to the
  // gizmo while the first release and second press share a native frame.
  harness.input.mouse_local = inside;
  harness.input.mouse_screen = inside;
  harness.input.cycle_selection_depth_pressed = true;
  auto combined = harness.frame(true, true, true, inside);
  combined.left_click_count = 2;
  const auto before_combined =
      harness.controller.handle_before_active_tool(harness.editor, combined);
  ASSERT_TRUE(before_combined.selection_click_requested);
  ASSERT_TRUE(before_combined.selection_cycle_depth);
  harness.controller.handle_after_active_tool(
      harness.editor, combined, {}, before_combined);
  EXPECT_EQ(selected_entities(harness),
            std::vector<quader::EntityId>{back.entity});

  harness.input.cycle_selection_depth_pressed = false;
  auto release = harness.frame(false, false, true, inside);
  release.left_click_count = 2;
  const auto before_release =
      harness.controller.handle_before_active_tool(harness.editor, release);
  harness.controller.handle_after_active_tool(
      harness.editor, release, {}, before_release);
  EXPECT_EQ(selected_entities(harness),
            std::vector<quader::EntityId>{back.entity});
  EXPECT_FALSE(harness.controller.dragging());
  expect_transform_near(*harness.session.scene().transform(front.entity), before);
}

TEST(ViewportInteractionControllerTests,
     CoalescedOrthographicDoubleClickCyclesOnceAndKeepsSelectionOnMiss) {
  ViewportInteractionHarness harness;
  (void)create_box_at(harness, {0.0f, 0.0f, 2.0f});
  const auto back = create_box_at(harness, {0.0f, 0.0f, -2.0f});
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Object)
                  .ok());
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
  harness.camera.view_axis(quader::ViewportViewAxis::Z, 1);
  harness.input.cycle_selection_depth_pressed = true;
  auto click = harness.frame(true, false, true, harness.input.mouse_local);
  click.left_click_count = 2;
  harness.controller.handle_after_active_tool(harness.editor, click, {}, {});
  EXPECT_EQ(selected_entities(harness),
            std::vector<quader::EntityId>{back.entity});

  harness.click_selection({20.0f, 20.0f}, 2, true);
  EXPECT_EQ(selected_entities(harness),
            std::vector<quader::EntityId>{back.entity});
}

TEST(ViewportInteractionControllerTests,
     PlainObjectDoubleClickEntersSelectedInstanceScope) {
  for (const bool coalesced_gizmo : {false, true}) {
    SCOPED_TRACE(coalesced_gizmo);
    ViewportInteractionHarness harness;
    const quader::QuaderModelingObjectRef box = create_unit_box(harness);
    ASSERT_NE(box.entity, quader::kInvalidEntityId);
    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Object)
                    .ok());
    harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
    ASSERT_TRUE(harness.editor.modeling().create_instance().ok());

    quader::EntityId occurrence = quader::kInvalidEntityId;
    for (const quader::QuaderEntitySnapshot &entity :
         harness.editor.query().entities()) {
      if (entity.kind == quader::QuaderEntityKind::Instance &&
          entity.has_instance_occurrence) {
        occurrence = entity.id;
        break;
      }
    }
    ASSERT_NE(occurrence, quader::kInvalidEntityId);
    ASSERT_TRUE(harness.editor.selection().clear().ok());

    quader::QVec2 hit{400.0f, 300.0f};
    if (coalesced_gizmo) {
      ASSERT_TRUE(harness.editor.selection().select_object(occurrence).ok());
      ASSERT_TRUE(harness.editor.tools().activate(quader::ViewportToolId::Move).ok());
      harness.router.set_active_tool(quader::ViewportToolId::Move);
      (void)harness.controller.handle_before_active_tool(harness.editor,
                                                         harness.frame());
      const auto *gizmo = harness.controller.transform_gizmo_frame();
      ASSERT_NE(gizmo, nullptr);
      const auto *axis = find_axis(*gizmo, quader_editor_core::TransformAxis::X);
      ASSERT_NE(axis, nullptr);
      hit = axis_screen_point(*axis, 0.25f);
    }
    harness.input.mouse_local = hit;
    harness.input.mouse_screen = hit;
    const auto press_frame = harness.frame(true, true, false, hit);
    const auto before_press =
        harness.controller.handle_before_active_tool(harness.editor, press_frame);
    EXPECT_EQ(before_press.gizmo_consumed, coalesced_gizmo);
    harness.controller.handle_after_active_tool(
        harness.editor, press_frame, {}, before_press);

    auto release_frame =
        harness.frame(coalesced_gizmo, coalesced_gizmo, true, hit);
    release_frame.left_click_count = 2;
    const auto before_release =
        harness.controller.handle_before_active_tool(harness.editor, release_frame);
    harness.controller.handle_after_active_tool(
        harness.editor, release_frame, {}, before_release);

    const auto edit = harness.editor.modeling().instance_edit_state();
    ASSERT_TRUE(edit.active);
    EXPECT_EQ(edit.occurrence.id, occurrence);
    EXPECT_TRUE(harness.session.scene().selected_entities().empty());
  }
}

TEST(ViewportInteractionControllerTests,
     InstanceDoubleClickEntryRejectsConsumedModifiedToolAndPreviewFrames) {
  /// Enumerates input states that must prevent instance-edit double-clicks.
  enum class GuardCase {
    ShiftModifier,
    NavigationInput,
    OperationPreview,
    ConsumedInput,
    NavigationResult,
    CameraChange,
    ModalTool,
    OwningTool,
    HandledToolResult,
    ConsumedGizmo,
  };

  const std::array cases{
      GuardCase::ShiftModifier,    GuardCase::NavigationInput,
      GuardCase::OperationPreview, GuardCase::ConsumedInput,
      GuardCase::NavigationResult, GuardCase::CameraChange,
      GuardCase::ModalTool,        GuardCase::OwningTool,
      GuardCase::HandledToolResult, GuardCase::ConsumedGizmo,
  };
  for (const GuardCase guard : cases) {
    ViewportInteractionHarness harness;
    (void)create_unit_box(harness);
    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Object)
                    .ok());
    harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
    ASSERT_TRUE(harness.editor.modeling().create_instance().ok());
    ASSERT_TRUE(harness.editor.selection().clear().ok());

    switch (guard) {
    case GuardCase::ShiftModifier:
      harness.input.modifiers.shift = true;
      break;
    case GuardCase::NavigationInput:
      harness.input.navigation_active = true;
      break;
    case GuardCase::OperationPreview:
      harness.input.operation_preview_active = true;
      break;
    case GuardCase::ConsumedInput:
      harness.input_result.consumed = true;
      break;
    case GuardCase::NavigationResult:
      harness.input_result.navigation_active = true;
      break;
    case GuardCase::CameraChange:
      harness.input_result.camera_changed = true;
      break;
    case GuardCase::ModalTool:
      harness.input.modal_tool_active = true;
      break;
    case GuardCase::OwningTool:
      harness.input.active_tool_owns_viewport = true;
      break;
    case GuardCase::HandledToolResult:
    case GuardCase::ConsumedGizmo:
      break;
    }

    const quader::QVec2 hit{400.0f, 300.0f};
    harness.input.mouse_local = hit;
    harness.input.mouse_screen = hit;
    const quader::ViewportInteractionFrame press =
        harness.frame(true, true, false, hit);
    harness.controller.handle_after_active_tool(
        harness.editor, press, quader::ViewportToolResult{},
        quader::ViewportBeforeToolInteractionResult{});

    quader::ViewportInteractionFrame release =
        harness.frame(false, false, true, hit);
    release.left_click_count = 2;
    quader::ViewportToolResult tool_result;
    quader::ViewportBeforeToolInteractionResult before_tool;
    tool_result.handled = guard == GuardCase::HandledToolResult;
    before_tool.gizmo_consumed = guard == GuardCase::ConsumedGizmo;
    harness.controller.handle_after_active_tool(harness.editor, release,
                                                tool_result, before_tool);

    EXPECT_FALSE(harness.editor.modeling().instance_edit_state().active)
        << "guard case " << static_cast<int>(guard);
  }
}

TEST(ViewportInteractionControllerTests,
     InstanceDoubleClickEntryRejectsSelectionDragRelease) {
  ViewportInteractionHarness harness;
  (void)create_unit_box(harness);
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Object)
                  .ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
  ASSERT_TRUE(harness.editor.modeling().create_instance().ok());
  ASSERT_TRUE(harness.editor.selection().clear().ok());

  const quader::QVec2 start{400.0f, 300.0f};
  const quader::QVec2 end{500.0f, 400.0f};
  harness.input.mouse_local = start;
  harness.input.mouse_screen = start;
  harness.controller.handle_after_active_tool(
      harness.editor, harness.frame(true, true, false, start),
      quader::ViewportToolResult{},
      quader::ViewportBeforeToolInteractionResult{});

  harness.input.mouse_local = end;
  harness.input.mouse_screen = end;
  quader::ViewportInteractionFrame release =
      harness.frame(false, false, true, start);
  release.left_click_count = 2;
  harness.controller.handle_after_active_tool(
      harness.editor, release, quader::ViewportToolResult{},
      quader::ViewportBeforeToolInteractionResult{});

  EXPECT_FALSE(harness.editor.modeling().instance_edit_state().active);
}

TEST(ViewportInteractionControllerTests,
     ContextMenuPointLightBillboardPreservesSelectedHitSelection) {
    ViewportInteractionHarness harness;
    quader::QuaderCreatePointLightArgs first_args;
    first_args.name = "First Billboard Point Light";
    const quader::QuaderApiResult<quader::QuaderEntityRef> first =
        harness.editor.scene().create_point_light(first_args);
    ASSERT_TRUE(first.ok()) << first.error_message();

    quader::QuaderCreatePointLightArgs second_args;
    second_args.name = "Second Billboard Point Light";
    second_args.transform.position = {3.0f, 0.0f, 0.0f};
    const quader::QuaderApiResult<quader::QuaderEntityRef> second =
        harness.editor.scene().create_point_light(second_args);
    ASSERT_TRUE(second.ok()) << second.error_message();

    ASSERT_TRUE(harness.editor.selection()
                    .select_objects({first.value().id, second.value().id},
                                    second.value().id)
                    .ok);
    harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);

    const quader::ViewportScreenPoint projected =
        harness.camera.project_world_point({});
    ASSERT_TRUE(projected.visible);
    harness.input.mouse_local = projected.position;
    harness.input.mouse_screen = projected.position;
    harness.billboard_pick_candidates.push_back({
        .entity = first.value().id,
        .position = {},
        .size_pixels = quader::kOverlayPointLightGizmoSizePixels,
    });

    const quader::QuaderSelectionSummary summary =
        harness.controller.pick_context_menu_target(harness.editor,
                                                    harness.frame());

    EXPECT_TRUE(summary.has_selection);
    EXPECT_EQ(summary.selected_object_count, 2u);
    const std::vector<quader::EntityId> selected = selected_entities(harness);
    ASSERT_EQ(selected.size(), 2u);
    EXPECT_NE(std::find(selected.begin(), selected.end(), first.value().id),
              selected.end());
    EXPECT_NE(std::find(selected.begin(), selected.end(), second.value().id),
              selected.end());
    EXPECT_EQ(harness.session.scene().primary_selection(), second.value().id);
}

TEST(ViewportInteractionControllerTests, VertexSnapDoesNotStealPendingMoveGizmoVertexClick) {
    ViewportInteractionHarness harness;
    harness.camera.set_view_direction({-0.45f, -0.35f, -0.82f});
    (void)harness.settings.set_bool_value(
        quader::ViewportBoolSettingId::VertexSnap, true);
    ASSERT_TRUE(
        harness.settings.bool_value(quader::ViewportBoolSettingId::VertexSnap));
    harness.input.transform_vertex_snap_down = true;

    const quader::QuaderModelingObjectRef box = create_unit_box(harness);
    select_single_component(harness, box, quader::QuaderSelectionMode::Vertex);
    ASSERT_TRUE(harness.editor.tools().activate(quader::ViewportToolId::Move).ok);
    harness.router.set_active_tool(quader::ViewportToolId::Move);

    const quader::ViewportBeforeToolInteractionResult warmup =
        harness.controller.handle_before_active_tool(harness.editor,
                                                     harness.frame());
    ASSERT_FALSE(warmup.gizmo_consumed);
    const quader_editor_core::TransformGizmoFrame *frame =
        harness.controller.transform_gizmo_frame();
    ASSERT_NE(frame, nullptr);
    const quader::QVec2 click = frame->info.screen_pivot;

    harness.input.mouse_local = click;
    harness.input.mouse_screen = click;
    const quader::ViewportBeforeToolInteractionResult press =
        harness.controller.handle_before_active_tool(
            harness.editor, harness.frame(true, true, false, click));
    ASSERT_TRUE(press.gizmo_consumed);
    EXPECT_FALSE(press.selection_click_requested);
    EXPECT_FALSE(harness.controller.dragging());
    EXPECT_FALSE(has_snap_target_vertex_point(
        harness.editor.query().selection_overlay()));

    const quader::ViewportInteractionFrame release_frame =
        harness.frame(false, false, true, click);
    const quader::ViewportBeforeToolInteractionResult release =
        harness.controller.handle_before_active_tool(harness.editor,
                                                     release_frame);
    EXPECT_FALSE(release.gizmo_consumed);
    ASSERT_TRUE(release.selection_click_requested);
    EXPECT_EQ(release.selection_click_local.x, click.x);
    EXPECT_EQ(release.selection_click_local.y, click.y);
    harness.controller.handle_after_active_tool(
        harness.editor, release_frame, quader::ViewportToolResult{}, release);

    const quader::QuaderSelectionSummary selection = harness.editor.selection().summary();
    EXPECT_EQ(selection.mode, quader::QuaderSelectionMode::Vertex);
    EXPECT_EQ(selection.selected_vertex_count, 1u);
    EXPECT_EQ(selection.active_kind, quader::QuaderActiveSelectionKind::Vertex);
    EXPECT_FALSE(harness.controller.dragging());
    EXPECT_FALSE(has_snap_target_vertex_point(
        harness.editor.query().selection_overlay()));
}

TEST(ViewportInteractionControllerTests,
     MoveGizmoVertexSnapRequiresHeldActionAndReevaluatesStationaryAxisDrag) {
  ViewportInteractionHarness harness;
  harness.camera.view_axis(quader::ViewportViewAxis::Z, 1);
  (void)harness.settings.set_bool_value(
      quader::ViewportBoolSettingId::SnappingEnabled, true);
  (void)harness.settings.set_bool_value(
      quader::ViewportBoolSettingId::GridSnap, false);
  (void)harness.settings.set_bool_value(
      quader::ViewportBoolSettingId::VertexSnap, true);
  ASSERT_TRUE(harness.settings.bool_value(
      quader::ViewportBoolSettingId::SnappingEnabled));
  ASSERT_FALSE(harness.settings.bool_value(
      quader::ViewportBoolSettingId::GridSnap));
  ASSERT_TRUE(harness.settings.bool_value(
      quader::ViewportBoolSettingId::VertexSnap));
  ASSERT_GT(quader::viewport_vertex_snap_radius(harness.settings), 0.0f);

  const quader::QuaderModelingObjectRef source =
      create_box_at(harness, {-3.0f, 0.0f, 0.0f});
  const quader::QuaderModelingObjectRef target =
      create_box_at(harness, {3.0f, 2.0f, 2.0f});
  const auto target_vertex =
      vertex_snap_target_at(harness, {2.0f, 1.0f, 3.0f});
  ASSERT_TRUE(target_vertex.has_value());
  ASSERT_EQ(target_vertex->modeling_object_id, target.modeling_object_id);

  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Object)
                  .ok());
  ASSERT_TRUE(harness.editor.selection().select_object(source).ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
  ASSERT_TRUE(
      harness.editor.tools().activate(quader::ViewportToolId::Move).ok());
  harness.router.set_active_tool(quader::ViewportToolId::Move);

  (void)harness.controller.handle_before_active_tool(harness.editor,
                                                     harness.frame());
  const quader_editor_core::TransformGizmoFrame *gizmo =
      harness.controller.transform_gizmo_frame();
  ASSERT_NE(gizmo, nullptr);
  const auto *axis = find_axis(*gizmo, quader_editor_core::TransformAxis::X);
  ASSERT_NE(axis, nullptr);
  const quader::QVec2 start = axis_screen_point(*axis, 0.25f);
  const quader::ViewportScreenPoint target_screen =
      harness.camera.project_world_point(target_vertex->world_position);
  ASSERT_TRUE(target_screen.visible);
  const quader::QVec2 snap_probe =
      target_screen.position + quader::QVec2{2.0f, 0.0f};

  harness.input.mouse_local = start;
  harness.input.mouse_screen = start;
  const auto press = harness.controller.handle_before_active_tool(
      harness.editor, harness.frame(true, true, false, start));
  ASSERT_TRUE(press.gizmo_consumed);

  harness.input.mouse_local = snap_probe;
  harness.input.mouse_screen = snap_probe;
  harness.input.transform_vertex_snap_down = false;
  const auto ordinary_drag = harness.controller.handle_before_active_tool(
      harness.editor, harness.frame(false, true, false, start));
  ASSERT_TRUE(ordinary_drag.gizmo_consumed);
  ASSERT_TRUE(harness.controller.dragging());
  const std::optional<quader::QVec3> ordinary_pivot =
      harness.editor.query().selection_transform_pivot();
  ASSERT_TRUE(ordinary_pivot.has_value());
  EXPECT_GT(std::abs(ordinary_pivot->x - target_vertex->world_position.x),
            0.01f)
      << "The persistent Vertex Snap setting must not activate gizmo snapping.";

  harness.input.transform_vertex_snap_down = true;
  const auto held_drag = harness.controller.handle_before_active_tool(
      harness.editor, harness.frame(false, true, false, start));
  ASSERT_TRUE(held_drag.gizmo_consumed);
  const std::optional<quader::QVec3> snapped_pivot =
      harness.editor.query().selection_transform_pivot();
  ASSERT_TRUE(snapped_pivot.has_value());
  EXPECT_NEAR(snapped_pivot->x, target_vertex->world_position.x, 0.001f);
  EXPECT_NEAR(snapped_pivot->y, 0.0f, 0.001f);
  EXPECT_NEAR(snapped_pivot->z, 0.0f, 0.001f);

  harness.input.transform_vertex_snap_down = false;
  const auto released_drag = harness.controller.handle_before_active_tool(
      harness.editor, harness.frame(false, true, false, start));
  ASSERT_TRUE(released_drag.gizmo_consumed);
  const std::optional<quader::QVec3> released_pivot =
      harness.editor.query().selection_transform_pivot();
  ASSERT_TRUE(released_pivot.has_value());
  expect_vec3_near(*released_pivot, *ordinary_pivot);

  const auto release = harness.controller.handle_before_active_tool(
      harness.editor, harness.frame(false, false, true, start));
  EXPECT_TRUE(release.gizmo_consumed);
}

TEST(ViewportInteractionControllerTests,
     HeldVertexSnapMovesCentralGizmoVertexExactlyDespiteCoarseGridSnap) {
  ViewportInteractionHarness harness;
  harness.camera.view_axis(quader::ViewportViewAxis::Z, 1);
  const quader::QVec3 source_center{-2.75f, -2.2f, 0.4f};
  const quader::QVec3 target_center{2.35f, 1.7f, 2.4f};
  const quader::QuaderModelingObjectRef source =
      create_box_at(harness, source_center);
  const quader::QuaderModelingObjectRef target =
      create_box_at(harness, target_center);
  const auto source_vertex = vertex_snap_target_at(
      harness, source_center + quader::QVec3{-1.0f, -1.0f, 1.0f});
  const auto target_vertex = vertex_snap_target_at(
      harness, target_center + quader::QVec3{-1.0f, -1.0f, 1.0f});
  ASSERT_TRUE(source_vertex.has_value());
  ASSERT_TRUE(target_vertex.has_value());
  ASSERT_EQ(source_vertex->modeling_object_id, source.modeling_object_id);
  ASSERT_EQ(target_vertex->modeling_object_id, target.modeling_object_id);

  ASSERT_TRUE(harness.editor.selection().select_object(source).ok());
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Vertex)
                  .ok());
  ASSERT_TRUE(harness.editor.selection()
                  .select_vertex(source, source_vertex->vertex_id)
                  .ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Vertex);
  ASSERT_TRUE(
      harness.editor.tools().activate(quader::ViewportToolId::Move).ok());
  harness.router.set_active_tool(quader::ViewportToolId::Move);

  (void)harness.settings.set_bool_value(
      quader::ViewportBoolSettingId::SnappingEnabled, true);
  (void)harness.settings.set_bool_value(
      quader::ViewportBoolSettingId::GridSnap, true);
  (void)harness.settings.set_bool_value(
      quader::ViewportBoolSettingId::VertexSnap, false);
  ASSERT_TRUE(harness.settings.bool_value(
      quader::ViewportBoolSettingId::SnappingEnabled));
  ASSERT_TRUE(harness.settings.bool_value(
      quader::ViewportBoolSettingId::GridSnap));
  ASSERT_FALSE(harness.settings.bool_value(
      quader::ViewportBoolSettingId::VertexSnap));
  harness.input.grid_preset = 10;
  harness.input.transform_vertex_snap_down = true;
  ASSERT_TRUE(quader::viewport_grid_snap_active(harness.settings, false));
  ASSERT_EQ(quader::viewport_vertex_snap_radius(harness.settings), 0.0f);
  ASSERT_GT(quader::viewport_grid_world_size_for_preset(
                harness.input.grid_preset),
            4.0f);

  const std::optional<quader::QVec3> initial_pivot =
      harness.editor.query().selection_transform_pivot();
  ASSERT_TRUE(initial_pivot.has_value());
  expect_vec3_near(*initial_pivot, source_vertex->world_position);
  harness.session.history().clear();

  (void)harness.controller.handle_before_active_tool(harness.editor,
                                                     harness.frame());
  const quader_editor_core::TransformGizmoFrame *gizmo =
      harness.controller.transform_gizmo_frame();
  ASSERT_NE(gizmo, nullptr);
  const quader::QVec2 start = gizmo->info.screen_pivot;
  const quader::ViewportScreenPoint target_screen =
      harness.camera.project_world_point(target_vertex->world_position);
  ASSERT_TRUE(target_screen.visible);

  harness.input.mouse_local = start;
  harness.input.mouse_screen = start;
  const auto press = harness.controller.handle_before_active_tool(
      harness.editor, harness.frame(true, true, false, start));
  ASSERT_TRUE(press.gizmo_consumed);

  harness.input.mouse_local = target_screen.position;
  harness.input.mouse_screen = target_screen.position;
  const auto drag = harness.controller.handle_before_active_tool(
      harness.editor, harness.frame(false, true, false, start));
  ASSERT_TRUE(drag.gizmo_consumed);
  ASSERT_TRUE(harness.controller.dragging());
  const std::optional<quader::QVec3> snapped_pivot =
      harness.editor.query().selection_transform_pivot();
  ASSERT_TRUE(snapped_pivot.has_value());
  expect_vec3_near(*snapped_pivot, target_vertex->world_position);

  const auto release = harness.controller.handle_before_active_tool(
      harness.editor, harness.frame(false, false, true, start));
  ASSERT_TRUE(release.gizmo_consumed);
  ASSERT_TRUE(harness.editor.history().snapshot().can_undo);
  ASSERT_TRUE(harness.editor.history().undo().ok());
  EXPECT_FALSE(harness.editor.history().snapshot().can_undo)
      << "The complete center-handle snap must commit as one undo step.";
  const std::optional<quader::QVec3> undone_pivot =
      harness.editor.query().selection_transform_pivot();
  ASSERT_TRUE(undone_pivot.has_value());
  expect_vec3_near(*undone_pivot, *initial_pivot);
}

TEST(ViewportInteractionControllerTests, ShortMoveGizmoClickKeepsEdgeSelection) {
    ViewportInteractionHarness harness;
    harness.camera.set_view_direction({-0.45f, -0.35f, -0.82f});

    const quader::QuaderModelingObjectRef box = create_unit_box(harness);
    select_single_component(harness, box, quader::QuaderSelectionMode::Edge);
    ASSERT_TRUE(harness.editor.tools().activate(quader::ViewportToolId::Move).ok);
    harness.router.set_active_tool(quader::ViewportToolId::Move);

    const quader::ViewportBeforeToolInteractionResult warmup =
        harness.controller.handle_before_active_tool(harness.editor,
                                                     harness.frame());
    ASSERT_FALSE(warmup.gizmo_consumed);
    const quader_editor_core::TransformGizmoFrame *frame =
        harness.controller.transform_gizmo_frame();
    ASSERT_NE(frame, nullptr);
    const quader::QVec2 click = frame->info.screen_pivot;

    harness.input.mouse_local = click;
    harness.input.mouse_screen = click;
    const quader::ViewportBeforeToolInteractionResult press =
        harness.controller.handle_before_active_tool(
            harness.editor, harness.frame(true, true, false, click));
    ASSERT_TRUE(press.gizmo_consumed);
    EXPECT_FALSE(press.selection_click_requested);

    const quader::ViewportInteractionFrame release_frame =
        harness.frame(false, false, true, click);
    const quader::ViewportBeforeToolInteractionResult release =
        harness.controller.handle_before_active_tool(harness.editor,
                                                     release_frame);
    EXPECT_FALSE(release.gizmo_consumed);
    ASSERT_TRUE(release.selection_click_requested);
    harness.controller.handle_after_active_tool(
        harness.editor, release_frame, quader::ViewportToolResult{}, release);

    const quader::QuaderSelectionSummary selection =
        harness.editor.selection().summary();
    EXPECT_EQ(selection.mode, quader::QuaderSelectionMode::Edge);
    EXPECT_EQ(selection.selected_edge_count, 1u);
    EXPECT_EQ(selection.active_kind, quader::QuaderActiveSelectionKind::Edge);
    EXPECT_FALSE(harness.controller.dragging());
}

TEST(ViewportInteractionControllerTests,
     AltClickAndDoubleClickSelectTheCompleteCylinderCapPerimeter) {
  for (const int side_count : {8, 32}) {
    std::vector<CylinderEdgeSelectionScenario::EdgeId> first_gesture_edges;
    for (const bool double_click : {false, true}) {
      SCOPED_TRACE(side_count);
      SCOPED_TRACE(double_click);
      CylinderEdgeSelectionScenario scenario;
      scenario.initialize(side_count);
      auto &harness = scenario.harness;
      const auto &side = scenario.document.faces[side_count / 4 - 1];
      const auto expected =
          CylinderEdgeSelectionScenario::perimeter(scenario.document.faces.back());
      ASSERT_EQ(expected.size(), static_cast<std::size_t>(side_count));
      const auto hit = scenario.midpoint(side.vertices[1], side.vertices[2]);
      const auto content_revision =
          harness.editor.selection().summary().content_revision;

      if (double_click) {
        harness.click_selection(hit);
        ASSERT_EQ(scenario.selected_edges(),
                  (std::vector<CylinderEdgeSelectionScenario::EdgeId>{
                      CylinderEdgeSelectionScenario::edge_id(
                          side.vertices[1], side.vertices[2])}));
      }
      harness.input.modifiers.alt = !double_click;
      harness.click_selection(hit, double_click ? 2 : 1);

      EXPECT_EQ(scenario.selected_edges(), expected);
      const auto selected = harness.editor.selection().summary();
      EXPECT_EQ(selected.active_object_id, scenario.object.modeling_object_id);
      EXPECT_EQ(selected.mode, quader::QuaderSelectionMode::Edge);
      EXPECT_EQ(selected.content_revision, content_revision);
      if (double_click) {
        EXPECT_EQ(scenario.selected_edges(), first_gesture_edges);
      } else {
        first_gesture_edges = scenario.selected_edges();
      }

      harness.click_selection(hit, double_click ? 2 : 1);
      EXPECT_EQ(scenario.selected_edges(), expected);
      EXPECT_EQ(harness.editor.selection().summary().content_revision,
                content_revision);
    }
  }
}

TEST(ViewportInteractionControllerTests,
     AltClickAndDoubleClickSelectOneCylinderSideQuadFromItsVerticalEdge) {
  for (const int side_count : {8, 32}) {
    std::vector<CylinderEdgeSelectionScenario::EdgeId> first_gesture_edges;
    for (const bool double_click : {false, true}) {
      SCOPED_TRACE(side_count);
      SCOPED_TRACE(double_click);
      CylinderEdgeSelectionScenario scenario;
      scenario.initialize(side_count);
      auto &harness = scenario.harness;
      const auto &side = scenario.document.faces[side_count / 4];
      const auto seed = CylinderEdgeSelectionScenario::edge_id(
          side.vertices[0], side.vertices[1]);
      const auto first_quad = CylinderEdgeSelectionScenario::perimeter(side);
      const auto second_quad = CylinderEdgeSelectionScenario::perimeter(
          scenario.document.faces[side_count / 4 - 1]);
      const auto hit = scenario.midpoint(seed.first, seed.second);

      if (double_click) {
        harness.click_selection(hit);
        ASSERT_EQ(scenario.selected_edges(),
                  (std::vector<CylinderEdgeSelectionScenario::EdgeId>{seed}));
      }
      harness.input.modifiers.alt = !double_click;
      harness.click_selection(hit, double_click ? 2 : 1);

      const auto selected = scenario.selected_edges();
      EXPECT_TRUE(selected == first_quad || selected == second_quad);
      EXPECT_EQ(selected.size(), 4u);
      EXPECT_TRUE(std::binary_search(selected.begin(), selected.end(), seed));
      if (double_click) {
        EXPECT_EQ(selected, first_gesture_edges);
      } else {
        first_gesture_edges = selected;
      }

      harness.click_selection(hit, double_click ? 2 : 1);
      EXPECT_EQ(scenario.selected_edges(), selected);
    }
  }
}

TEST(ViewportInteractionControllerTests,
     CtrlAltEdgeHoverNeverPublishesBlueRemovalFeedbackInEitherKeyOrder) {
  constexpr int kSideCount = 16;
  CylinderEdgeSelectionScenario scenario;
  scenario.initialize(kSideCount);
  auto &harness = scenario.harness;
  const auto &side = scenario.document.faces[kSideCount / 4];
  const auto seed = CylinderEdgeSelectionScenario::edge_id(
      side.vertices[0], side.vertices[1]);
  ASSERT_TRUE(harness.editor.selection()
                  .select_edge(scenario.object, seed.first, seed.second)
                  .ok());
  harness.input.mouse_local = scenario.midpoint(seed.first, seed.second);
  harness.input.mouse_screen = harness.input.mouse_local;
  ViewportSelectionWorkGateHarness work_gate(harness);
  const auto selection_revision =
      harness.editor.selection().summary().selection_revision;

  const auto expect_feedback = [&](quader::ViewportModifiers modifiers,
                                   bool remove) {
    harness.input.modifiers = modifiers;
    // Repeated stationary frames also exercise the real hover work cache.
    for (int frame_index = 0; frame_index < 2; ++frame_index) {
      SCOPED_TRACE(frame_index);
      const auto frame = harness.frame();
      const auto before =
          harness.controller.handle_before_active_tool(harness.editor, frame);
      harness.controller.handle_after_active_tool(
          harness.editor, frame, {}, before, work_gate.hover_port());
      scenario.expect_edge_hover_feedback(seed, remove);
    }
    EXPECT_EQ(scenario.selected_edges(),
              (std::vector<CylinderEdgeSelectionScenario::EdgeId>{seed}));
    EXPECT_EQ(harness.editor.selection().summary().selection_revision,
              selection_revision);
  };

  expect_feedback({}, false);
  expect_feedback({.primary = true}, true);
  expect_feedback({.primary = true, .alt = true}, false);
  expect_feedback({.primary = true}, true);
  expect_feedback({}, false);
  expect_feedback({.alt = true}, false);
  expect_feedback({.primary = true, .alt = true}, false);
  expect_feedback({.alt = true}, false);
}

TEST(ViewportInteractionControllerTests,
     HeldCtrlAltRingClickKeepsNormalEdgeFeedbackWhenAltIsReleased) {
  for (const bool alt_and_click_in_same_frame : {false, true}) {
    SCOPED_TRACE(alt_and_click_in_same_frame);
    constexpr int kSideCount = 16;
    CylinderEdgeSelectionScenario scenario;
    scenario.initialize(kSideCount);
    auto &harness = scenario.harness;
    const auto &side = scenario.document.faces[kSideCount / 4];
    const auto seed = CylinderEdgeSelectionScenario::edge_id(
        side.vertices[0], side.vertices[1]);
    ASSERT_TRUE(harness.editor.selection()
                    .select_edge(scenario.object, seed.first, seed.second)
                    .ok());
    const auto hit = scenario.midpoint(seed.first, seed.second);
    harness.input.mouse_local = hit;
    harness.input.mouse_screen = hit;
    harness.input.modifiers = {
        .primary = true, .alt = !alt_and_click_in_same_frame};

    harness.dispatch_frame(harness.frame());
    scenario.expect_edge_hover_feedback(seed, alt_and_click_in_same_frame);
    // Adding Alt and pressing LMB can arrive before another hover frame.
    harness.input.modifiers = {.primary = true, .alt = true};
    harness.dispatch_frame(harness.frame(true, true, false, hit));
    scenario.expect_edge_hover_feedback(seed, false);
    harness.input.modifiers = {.primary = true};
    harness.dispatch_frame(harness.frame(false, true, false, hit));
    scenario.expect_edge_hover_feedback(seed, false);
    harness.dispatch_frame(harness.frame(false, false, true, hit));

    std::vector<CylinderEdgeSelectionScenario::EdgeId> expected;
    for (int index = 0; index < kSideCount; ++index) {
      const auto &quad = scenario.document.faces[index];
      expected.push_back(CylinderEdgeSelectionScenario::edge_id(
          quad.vertices[0], quad.vertices[1]));
    }
    std::sort(expected.begin(), expected.end());
    EXPECT_EQ(scenario.selected_edges(), expected);

    harness.dispatch_frame(harness.frame());
    scenario.expect_edge_hover_feedback(seed, true);
  }
}

TEST(ViewportInteractionControllerTests,
     CtrlAltClickSelectsTheCylinderEdgeRingWithThePressedModifiers) {
  for (const bool release_modifiers_before_release : {false, true}) {
    SCOPED_TRACE(release_modifiers_before_release);
    constexpr int kSideCount = 16;
    CylinderEdgeSelectionScenario scenario;
    scenario.initialize(kSideCount);
    auto &harness = scenario.harness;
    const auto &side = scenario.document.faces[kSideCount / 4];
    const auto hit = scenario.midpoint(side.vertices[0], side.vertices[1]);
    std::vector<CylinderEdgeSelectionScenario::EdgeId> expected;
    for (int index = 0; index < kSideCount; ++index) {
      const auto &quad = scenario.document.faces[index];
      expected.push_back(CylinderEdgeSelectionScenario::edge_id(
          quad.vertices[0], quad.vertices[1]));
    }
    std::sort(expected.begin(), expected.end());
    // An existing cap edge must be replaced, not removed or added to the ring.
    ASSERT_TRUE(harness.editor.selection()
                    .select_edge(scenario.object, side.vertices[1],
                                 side.vertices[2])
                    .ok());
    harness.input.modifiers = {.primary = true, .alt = true};
    harness.input.mouse_local = hit;
    harness.input.mouse_screen = hit;

    harness.dispatch_frame(harness.frame(true, true, false, hit));
    EXPECT_NE(harness.controller.active_tool_pointer_cursor(
                  harness.router.state().active_tool),
              quader::ViewportPointerCursor::SelectionSubtract);
    if (release_modifiers_before_release) {
      harness.input.modifiers = {};
      harness.dispatch_frame(harness.frame(false, true, false, hit));
      EXPECT_NE(harness.controller.active_tool_pointer_cursor(
                    harness.router.state().active_tool),
                quader::ViewportPointerCursor::SelectionSubtract);
    }
    harness.dispatch_frame(harness.frame(false, false, true, hit));
    EXPECT_EQ(scenario.selected_edges(), expected);
  }
}

TEST(ViewportInteractionControllerTests,
     AltClickEdgeSelectsThePoleFacePerimeterThroughViewportFallback) {
    ViewportInteractionHarness harness;
    (void)create_unit_box(harness);

    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Edge)
                    .ok());
    harness.router.set_selection_mode(quader::ViewportSelectionMode::Edge);

    const quader::ViewportScreenPoint edge_midpoint =
        harness.camera.project_world_point({1.0f, 0.0f, 1.0f});
    ASSERT_TRUE(edge_midpoint.visible);
    harness.input.modifiers.alt = true;
    harness.input.mouse_local = edge_midpoint.position;
    harness.input.mouse_screen = edge_midpoint.position;

    const quader::ViewportInteractionFrame press_frame =
        harness.frame(true, true, false, edge_midpoint.position);
    const quader::ViewportBeforeToolInteractionResult before_tool =
        harness.controller.handle_before_active_tool(harness.editor,
                                                     press_frame);
    EXPECT_FALSE(before_tool.gizmo_consumed);
    harness.controller.handle_after_active_tool(
        harness.editor, press_frame, quader::ViewportToolResult{},
        before_tool);

    const quader::ViewportInteractionFrame release_frame =
        harness.frame(false, false, true, edge_midpoint.position);
    harness.controller.handle_after_active_tool(
        harness.editor, release_frame, quader::ViewportToolResult{},
        quader::ViewportBeforeToolInteractionResult{});

    const quader::QuaderSelectionSummary selection =
        harness.editor.selection().summary();
    EXPECT_EQ(selection.mode, quader::QuaderSelectionMode::Edge);
    EXPECT_EQ(selection.selected_edge_count, 4u);
    EXPECT_EQ(selection.active_kind, quader::QuaderActiveSelectionKind::Edge);
}

TEST(ViewportInteractionControllerTests,
     CtrlAltClickEdgeSelectsBlenderStyleRingThroughViewportFallback) {
    ViewportInteractionHarness harness;
    (void)create_unit_box(harness);

    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Edge)
                    .ok());
    harness.router.set_selection_mode(quader::ViewportSelectionMode::Edge);

    const quader::ViewportScreenPoint edge_midpoint =
        harness.camera.project_world_point({1.0f, 0.0f, 1.0f});
    ASSERT_TRUE(edge_midpoint.visible);
    harness.input.modifiers.primary = true;
    harness.input.modifiers.alt = true;

    harness.click_selection(edge_midpoint.position);

    const quader::QuaderSelectionSummary selection =
        harness.editor.selection().summary();
    EXPECT_EQ(selection.mode, quader::QuaderSelectionMode::Edge);
    EXPECT_EQ(selection.selected_edge_count, 4u);
    EXPECT_EQ(selection.active_kind, quader::QuaderActiveSelectionKind::Edge);
}

TEST(ViewportInteractionControllerTests,
     MultipleEdgeRingsInvertToExactOverlayComplementAndRestore) {
    using EdgeId = std::pair<int, int>;

    ViewportInteractionHarness harness;
    const quader::QuaderModelingObjectRef box = create_unit_box(harness);

    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Edge)
                    .ok());
    harness.router.set_selection_mode(quader::ViewportSelectionMode::Edge);

    const auto normalized_edge = [](int a, int b) -> EdgeId {
      return a < b ? EdgeId{a, b} : EdgeId{b, a};
    };
    const auto sorted_unique = [](std::vector<EdgeId> edges) {
      std::sort(edges.begin(), edges.end());
      edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
      return edges;
    };
    const auto exported_edges = [&](const quader::ModelingSelectionExport &value) {
      const auto object = std::find_if(
          value.objects.begin(), value.objects.end(), [&](const auto &candidate) {
            return candidate.modeling_object_id == box.modeling_object_id;
          });
      if (object == value.objects.end()) {
        ADD_FAILURE() << "The selected box is missing from the selection export.";
        return std::vector<EdgeId>{};
      }
      std::vector<EdgeId> result;
      result.reserve(object->selected_edges.size());
      for (const quader::ModelingSelectionExportEdge &edge :
           object->selected_edges) {
        result.push_back(normalized_edge(static_cast<int>(edge.a),
                                         static_cast<int>(edge.b)));
      }
      return sorted_unique(std::move(result));
    };
    const auto overlay_edges = [&](const quader::QuaderOverlayFrame &frame,
                                   quader::QuaderOverlayLayer layer) {
      std::vector<EdgeId> result;
      for (const quader::QuaderOverlayLine &line : frame.lines) {
        if (line.layer != layer ||
            line.element.object_id != box.modeling_object_id) {
          continue;
        }
        EXPECT_GT(line.element.edge_a, 0);
        EXPECT_GT(line.element.edge_b, 0);
        result.push_back(
            normalized_edge(line.element.edge_a, line.element.edge_b));
      }
      return sorted_unique(std::move(result));
    };

    const quader::ViewportScreenPoint y_ring_edge =
        harness.camera.project_world_point({1.0f, 0.0f, 1.0f});
    const quader::ViewportScreenPoint x_ring_edge =
        harness.camera.project_world_point({0.0f, -1.0f, 1.0f});
    ASSERT_TRUE(y_ring_edge.visible);
    ASSERT_TRUE(x_ring_edge.visible);

    harness.input.modifiers.primary = true;
    harness.input.modifiers.alt = true;
    harness.click_selection(y_ring_edge.position);
    ASSERT_EQ(harness.editor.selection().summary().selected_edge_count, 4u);

    harness.input.modifiers.shift = true;
    harness.click_selection(x_ring_edge.position);
    ASSERT_EQ(harness.editor.selection().summary().selected_edge_count, 8u);
    ASSERT_TRUE(harness.session.modeling().clear_selection_hover().ok);
    harness.input.modifiers = {};

    const std::vector<EdgeId> original_ring_edges{
        {1, 2}, {1, 4}, {2, 3}, {3, 4},
        {5, 6}, {5, 8}, {6, 7}, {7, 8},
    };
    const std::vector<EdgeId> all_box_edges{
        {1, 2}, {1, 4}, {1, 5}, {2, 3}, {2, 8}, {3, 4},
        {3, 7}, {4, 6}, {5, 6}, {5, 8}, {6, 7}, {7, 8},
    };
    const std::vector<EdgeId> expected_complement{
        {1, 5}, {2, 8}, {3, 7}, {4, 6},
    };

    const quader::ModelingSelectionExport before_export =
        harness.session.modeling().export_selection();
    EXPECT_EQ(exported_edges(before_export), original_ring_edges);

    const quader::QuaderOverlayFrame warm_overlay =
        harness.editor.query().selection_overlay();
    const std::vector<EdgeId> source_wire_edges = overlay_edges(
        warm_overlay, quader::QuaderOverlayLayer::SourceWire);
    EXPECT_EQ(source_wire_edges, all_box_edges);
    EXPECT_EQ(overlay_edges(warm_overlay,
                            quader::QuaderOverlayLayer::SelectedEdge),
              original_ring_edges);

    std::vector<EdgeId> source_wire_complement;
    for (const EdgeId edge : source_wire_edges) {
      if (!std::binary_search(original_ring_edges.begin(),
                              original_ring_edges.end(), edge)) {
        source_wire_complement.push_back(edge);
      }
    }
    ASSERT_EQ(source_wire_complement, expected_complement);

    const quader::QuaderApiVoidResult inverted =
        harness.editor.commands().execute(
            quader::quader_command::kSelectInverse);
    ASSERT_TRUE(inverted.ok()) << inverted.error_message();
    EXPECT_TRUE(inverted.changed());
    EXPECT_EQ(harness.editor.selection().summary().selected_edge_count, 4u);

    const std::vector<EdgeId> inverted_edges = exported_edges(
        harness.session.modeling().export_selection());
    EXPECT_EQ(inverted_edges, source_wire_complement);
    for (const EdgeId edge : original_ring_edges) {
      EXPECT_FALSE(std::binary_search(inverted_edges.begin(),
                                      inverted_edges.end(), edge));
    }

    const quader::QuaderOverlayFrame inverted_overlay =
        harness.editor.query().selection_overlay();
    EXPECT_EQ(overlay_edges(inverted_overlay,
                            quader::QuaderOverlayLayer::SourceWire),
              source_wire_edges);
    EXPECT_EQ(overlay_edges(inverted_overlay,
                            quader::QuaderOverlayLayer::SelectedEdge),
              inverted_edges);

    const quader::QuaderApiVoidResult restored =
        harness.editor.commands().execute(
            quader::quader_command::kSelectInverse);
    ASSERT_TRUE(restored.ok()) << restored.error_message();
    EXPECT_TRUE(restored.changed());
    EXPECT_EQ(harness.editor.selection().summary().selected_edge_count, 8u);
    EXPECT_EQ(exported_edges(harness.session.modeling().export_selection()),
              original_ring_edges);

    const quader::QuaderOverlayFrame restored_overlay =
        harness.editor.query().selection_overlay();
    EXPECT_EQ(overlay_edges(restored_overlay,
                            quader::QuaderOverlayLayer::SourceWire),
              source_wire_edges);
    EXPECT_EQ(overlay_edges(restored_overlay,
                            quader::QuaderOverlayLayer::SelectedEdge),
              original_ring_edges);
}

TEST(ViewportInteractionControllerTests,
     ShiftCtrlAltClickTogglesEntireBlenderStyleEdgeRingOff) {
    ViewportInteractionHarness harness;
    (void)create_unit_box(harness);

    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Edge)
                    .ok());
    harness.router.set_selection_mode(quader::ViewportSelectionMode::Edge);

    const quader::ViewportScreenPoint edge_midpoint =
        harness.camera.project_world_point({1.0f, 0.0f, 1.0f});
    ASSERT_TRUE(edge_midpoint.visible);
    harness.input.modifiers.primary = true;
    harness.input.modifiers.alt = true;
    harness.click_selection(edge_midpoint.position);
    ASSERT_EQ(harness.editor.selection().summary().selected_edge_count, 4u);

    harness.input.modifiers.shift = true;
    harness.click_selection(edge_midpoint.position);

    const quader::QuaderSelectionSummary selection =
        harness.editor.selection().summary();
    EXPECT_EQ(selection.mode, quader::QuaderSelectionMode::Edge);
    EXPECT_EQ(selection.selected_edge_count, 0u);
    EXPECT_EQ(selection.active_kind, quader::QuaderActiveSelectionKind::None);
}

TEST(ViewportInteractionControllerTests,
     EdgeRingClickUsesPressTimeModifiersAfterKeysAreReleased) {
    ViewportInteractionHarness harness;
    (void)create_unit_box(harness);

    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Edge)
                    .ok());
    harness.router.set_selection_mode(quader::ViewportSelectionMode::Edge);

    const quader::ViewportScreenPoint edge_midpoint =
        harness.camera.project_world_point({1.0f, 0.0f, 1.0f});
    ASSERT_TRUE(edge_midpoint.visible);
    harness.input.modifiers.primary = true;
    harness.input.modifiers.alt = true;
    harness.input.mouse_local = edge_midpoint.position;
    harness.input.mouse_screen = edge_midpoint.position;

    harness.dispatch_frame(
        harness.frame(true, true, false, edge_midpoint.position));

    harness.input.modifiers = {};
    harness.dispatch_frame(
        harness.frame(false, false, true, edge_midpoint.position));

    const quader::QuaderSelectionSummary selection =
        harness.editor.selection().summary();
    EXPECT_EQ(selection.mode, quader::QuaderSelectionMode::Edge);
    EXPECT_EQ(selection.selected_edge_count, 4u);
    EXPECT_EQ(selection.active_kind, quader::QuaderActiveSelectionKind::Edge);
}

TEST(ViewportInteractionControllerTests,
     AltHoverObjectModeLeavesFacePlaneHoverDormant) {
    ViewportInteractionHarness harness;
    (void)create_unit_box(harness);

    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Object)
                    .ok());
    harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);

    const quader::ViewportScreenPoint face_point =
        harness.camera.project_world_point({0.0f, 0.0f, 1.0f});
    ASSERT_TRUE(face_point.visible);
    harness.input.modifiers.alt = true;
    harness.input.mouse_local = face_point.position;
    harness.input.mouse_screen = face_point.position;

    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(), quader::ViewportToolResult{},
        quader::ViewportBeforeToolInteractionResult{});

    const quader::ViewportOverlayFrame overlay =
        quader::test_support::visible_modeling_overlay_frame(
            harness.session.modeling());
    EXPECT_FALSE(has_triangle_style(
        overlay, quader::ViewportOverlayStyleRole::FacePlaneHoverFace));
    EXPECT_FALSE(has_line_style(
        overlay, quader::ViewportOverlayStyleRole::FacePlaneHoverWire));
    EXPECT_FALSE(has_triangle_style(
        overlay, quader::ViewportOverlayStyleRole::RemoveSelectionFace));
}

TEST(ViewportInteractionControllerTests,
     AltRightClickLeavesFacePlaneCutDormantWhileExplicitApiStillCuts) {
    ViewportInteractionHarness harness;
    const quader::QuaderModelingObjectRef target = create_unit_box(harness);
    quader::QuaderTransform target_transform;
    target_transform.position = {4.0f, 0.0f, 0.0f};
    ASSERT_TRUE(harness.editor.scene()
                    .set_transform(target.entity, target_transform)
                    .ok());
    const quader::QuaderModelingObjectRef second_target =
        create_unit_box(harness);
    quader::QuaderTransform second_target_transform;
    second_target_transform.position = {4.0f, -3.0f, 0.0f};
    ASSERT_TRUE(harness.editor.scene()
                    .set_transform(second_target.entity,
                                   second_target_transform)
                    .ok());
    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> reference =
        harness.editor.modeling().create_box(
            quader::QuaderBounds{
                .min = {4.0f, 2.0f, -1.0f},
                .max = {6.0f, 4.0f, 1.0f},
            },
            "Face Plane Reference");
    ASSERT_TRUE(reference.ok) << reference.message;
    ASSERT_TRUE(harness.editor.selection()
                    .select_objects({target, second_target})
                    .ok());
    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Object)
                    .ok());
    harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
    const std::size_t object_count_before =
        harness.editor.modeling().objects().size();
    const quader::QuaderSelectionSummary selection_before =
        harness.editor.selection().summary();
    harness.session.history().clear();

    harness.camera.set_view_direction({1.0f, 0.0f, 0.0f});
    const quader::ViewportScreenPoint face_point =
        harness.camera.project_world_point({4.0f, 3.0f, 0.0f});
    ASSERT_TRUE(face_point.visible);
    harness.input.modifiers.alt = true;
    harness.input.mouse_local = face_point.position;
    harness.input.mouse_screen = face_point.position;

    harness.input.modifiers.primary = true;
    harness.controller.handle_after_active_tool(
        harness.editor,
        harness.frame(false, false, false, {}, true, true, false,
                      face_point.position),
        quader::ViewportToolResult{},
        quader::ViewportBeforeToolInteractionResult{});
    EXPECT_EQ(harness.editor.modeling().objects().size(), object_count_before);

    harness.input.modifiers.primary = false;
    harness.controller.handle_after_active_tool(
        harness.editor,
        harness.frame(false, false, false, {}, true, true, false,
                      face_point.position),
        quader::ViewportToolResult{},
        quader::ViewportBeforeToolInteractionResult{});

    EXPECT_EQ(harness.editor.modeling().objects().size(), object_count_before);
    const quader::QuaderSelectionSummary selection =
        harness.editor.selection().summary();
    EXPECT_EQ(selection.mode, quader::QuaderSelectionMode::Object);
    EXPECT_TRUE(selection.has_selection);
    EXPECT_EQ(selection.selected_object_count, 2u);
    EXPECT_EQ(selection.active_object_id, selection_before.active_object_id);
    EXPECT_EQ(selection.selection_revision, selection_before.selection_revision);
    EXPECT_EQ(selection.content_revision, selection_before.content_revision);
    EXPECT_FALSE(harness.editor.history().can_undo());

    const quader::ViewportRay ray =
        harness.camera.ray_from_viewport_point(face_point.position);
    const auto explicit_hover = harness.editor.selection().hover({
        .ray = {.origin = ray.origin, .direction = ray.direction},
        .face_plane_hover = true,
    });
    ASSERT_TRUE(explicit_hover.ok()) << explicit_hover.error_message();
    ASSERT_EQ(explicit_hover.value().target.kind,
              quader::QuaderActiveSelectionKind::Face);
    const auto cut =
        harness.editor.modeling().cut_selected_meshes_by_hovered_face_plane();
    ASSERT_TRUE(cut.ok()) << cut.error_message();
    ASSERT_TRUE(cut.changed());

    const std::unordered_set<int> original_ids{
        target.modeling_object_id, second_target.modeling_object_id,
        reference.value().modeling_object_id};
    const auto objects = harness.editor.modeling().objects();
    EXPECT_EQ(objects.size(), object_count_before + 2u);
    std::vector<quader::QuaderModelingObjectRecord> cut_halves;
    for (const auto &object : objects) {
      if (!original_ids.contains(object.modeling_object_id)) {
        cut_halves.push_back(object);
      }
    }
    ASSERT_EQ(cut_halves.size(), 2u);
    for (std::size_t index = 0; index < cut_halves.size(); ++index) {
      const auto *transform =
          harness.session.scene().transform(cut_halves[index].entity);
      ASSERT_NE(transform, nullptr);
      expect_vec3_near(transform->position,
                       index == 0 ? target_transform.position
                                  : second_target_transform.position);
    }
    EXPECT_FALSE(harness.editor.selection().summary().has_selection);
    EXPECT_EQ(harness.editor.selection().summary().selected_object_count, 0u);
    ASSERT_TRUE(harness.editor.history().undo().ok());
    EXPECT_EQ(harness.editor.modeling().objects().size(), object_count_before);
    EXPECT_EQ(harness.editor.selection().summary().selected_object_count, 2u);
    ASSERT_TRUE(harness.editor.history().redo().ok());
    EXPECT_EQ(harness.editor.modeling().objects().size(), object_count_before + 2u);
    EXPECT_FALSE(harness.editor.selection().summary().has_selection);
}

TEST(ViewportInteractionControllerTests, ShiftClickRebuildsSliceQuadPreviewThroughViewportFallback) {
    ViewportInteractionHarness harness;
    const quader::QuaderModelingObjectRef box = create_unit_box(harness);

    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Face)
                    .ok());
    ASSERT_TRUE(harness.editor.selection().select_face(box, 1).ok());
    harness.router.set_selection_mode(quader::ViewportSelectionMode::Face);

    const quader::QuaderApiVoidResult preview =
        harness.editor.commands().execute(
            harness.editor.commands().command_id_for_modeling_action(
                "slice_quad"));
    ASSERT_TRUE(preview.ok()) << preview.error_message();
    const std::optional<quader::QuaderMeshSnapshot> one_face_preview =
        harness.editor.query().mesh(box.mesh);
    ASSERT_TRUE(one_face_preview.has_value());

    harness.camera.set_view_direction({-1.0f, 0.0f, 0.0f});
    const quader::ViewportScreenPoint side_face =
        harness.camera.project_world_point({1.0f, 0.0f, 0.0f});
    ASSERT_TRUE(side_face.visible);
    harness.input.modifiers.shift = true;
    harness.input.operation_preview_active = true;
    harness.input.mouse_local = side_face.position;
    harness.input.mouse_screen = side_face.position;

    const quader::ViewportBeforeToolInteractionResult before_tool =
        harness.controller.handle_before_active_tool(
            harness.editor,
            harness.frame(true, true, false, side_face.position));
    EXPECT_FALSE(before_tool.gizmo_consumed);
    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(true, true, false, side_face.position),
        quader::ViewportToolResult{}, before_tool);
    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(false, false, true, side_face.position),
        quader::ViewportToolResult{},
        quader::ViewportBeforeToolInteractionResult{});

    const quader::QuaderSelectionSummary summary = harness.editor.selection().summary();
    EXPECT_EQ(summary.mode, quader::QuaderSelectionMode::Face);
    EXPECT_GT(summary.selected_face_count, 1u);
    const std::optional<quader::QuaderMeshSnapshot> two_face_preview =
        harness.editor.query().mesh(box.mesh);
    ASSERT_TRUE(two_face_preview.has_value());
    EXPECT_GT(two_face_preview->vertex_count, one_face_preview->vertex_count);
    EXPECT_GT(two_face_preview->primitive_count,
              one_face_preview->primitive_count);
}

TEST(ViewportInteractionControllerTests,
     BoxDragDefersViewportFallbackSelectionUntilRelease) {
    ViewportInteractionHarness harness;
    (void)create_unit_box(harness);

    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Object)
                    .ok);
    ASSERT_TRUE(harness.editor.selection().clear().ok);
    harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);

    const quader::QVec2 start{300.0f, 200.0f};
    const quader::QVec2 end{500.0f, 400.0f};
    harness.input.mouse_local = start;
    harness.input.mouse_screen = start;
    const quader::ViewportBeforeToolInteractionResult before_tool =
        harness.controller.handle_before_active_tool(
            harness.editor, harness.frame(true, true, false, start));
    EXPECT_FALSE(before_tool.gizmo_consumed);
    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(true, true, false, start),
        quader::ViewportToolResult{}, before_tool);

    harness.input.mouse_local = end;
    harness.input.mouse_screen = end;
    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(false, true, false, start),
        quader::ViewportToolResult{},
        quader::ViewportBeforeToolInteractionResult{});
    const quader::ViewportSelectionMarquee marquee =
        harness.controller.selection_marquee();
    ASSERT_TRUE(marquee.active);
    EXPECT_EQ(marquee.rule, quader::ModelingBoxSelectionRule::Contain);
    EXPECT_EQ(harness.editor.selection().summary().selected_object_count, 0u);

    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(false, false, true, start),
        quader::ViewportToolResult{},
        quader::ViewportBeforeToolInteractionResult{});

    const quader::QuaderSelectionSummary selection = harness.editor.selection().summary();
    EXPECT_EQ(selection.mode, quader::QuaderSelectionMode::Object);
    EXPECT_EQ(selection.selected_object_count, 1u);
    EXPECT_EQ(harness.router.state().selection_mode,
              quader::ViewportSelectionMode::Object);
    EXPECT_TRUE(harness.controller.selection_marquee().active);

    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(), quader::ViewportToolResult{},
        quader::ViewportBeforeToolInteractionResult{});
    EXPECT_FALSE(harness.controller.selection_marquee().active);
}

TEST(ViewportInteractionControllerTests,
     SelectionMarqueeUsesTheHostProvidedPhysicalDragThreshold) {
  ViewportInteractionHarness harness;
  (void)create_unit_box(harness);
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Object)
                  .ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);

  const quader::QVec2 start{40.0f, 40.0f};
  harness.input.mouse_local = start;
  harness.input.mouse_screen = start;
  quader::ViewportInteractionFrame press =
      harness.frame(true, true, false, start);
  press.pointer_drag_threshold_pixels = 20.0f;
  const quader::ViewportBeforeToolInteractionResult before =
      harness.controller.handle_before_active_tool(harness.editor, press);
  harness.controller.handle_after_active_tool(harness.editor, press, {},
                                              before);

  harness.input.mouse_local = {52.0f, 40.0f};
  harness.input.mouse_screen = harness.input.mouse_local;
  quader::ViewportInteractionFrame below_threshold =
      harness.frame(false, true, false, start);
  below_threshold.pointer_drag_threshold_pixels = 20.0f;
  harness.controller.handle_after_active_tool(
      harness.editor, below_threshold, {}, {});
  EXPECT_FALSE(harness.controller.selection_marquee().active);

  harness.input.mouse_local = {61.0f, 40.0f};
  harness.input.mouse_screen = harness.input.mouse_local;
  quader::ViewportInteractionFrame above_threshold =
      harness.frame(false, true, false, start);
  above_threshold.pointer_drag_threshold_pixels = 20.0f;
  harness.controller.handle_after_active_tool(
      harness.editor, above_threshold, {}, {});
  EXPECT_TRUE(harness.controller.selection_marquee().active);

  quader::ViewportInteractionFrame release =
      harness.frame(false, false, true, start);
  release.pointer_drag_threshold_pixels = 20.0f;
  harness.controller.handle_after_active_tool(harness.editor, release, {},
                                              {});
}

TEST(ViewportInteractionControllerTests,
     ObjectMarqueeAppliesMeshCurveBillboardAndCurveControlsAtomically) {
  ViewportInteractionHarness harness;
  const quader::QuaderModelingObjectRef box =
      create_box_at(harness, {2.5f, 0.0f, 0.0f});
  const auto curve = harness.editor.modeling().create_curve({
      .points = {{.position = {-3.0f, 0.0f, 0.0f}},
                 {.position = {-1.5f, 0.0f, 0.0f}}},
  });
  ASSERT_TRUE(curve.ok()) << curve.error_message();
  const auto light = harness.editor.scene().create_point_light({
      .name = "Mixed Marquee Light",
      .transform = {.position = {0.0f, 0.0f, 0.0f}},
  });
  ASSERT_TRUE(light.ok()) << light.error_message();
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Object)
                  .ok());
  ASSERT_TRUE(harness.editor.selection().select_object(curve.value()).ok());
  ASSERT_TRUE(harness.session.curve().refresh().ok);
  ASSERT_TRUE(harness.session.curve().controls_editable());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
  harness.billboard_pick_candidates = {{
      .entity = light.value().id,
      .position = {},
      .size_pixels = quader::kOverlayPointLightGizmoSizePixels,
  }};

  const auto upper_left =
      harness.camera.project_world_point({-4.0f, 2.0f, 0.0f});
  const auto lower_right =
      harness.camera.project_world_point({4.0f, -2.0f, 0.0f});
  ASSERT_TRUE(upper_left.visible);
  ASSERT_TRUE(lower_right.visible);
  const quader::QVec2 start = upper_left.position;
  const quader::QVec2 end = lower_right.position;
  harness.input.mouse_local = start;
  harness.input.mouse_screen = start;
  const auto press = harness.frame(true, true, false, start);
  const auto before =
      harness.controller.handle_before_active_tool(harness.editor, press,
                                                   harness.session.native_tools());
  harness.controller.handle_after_active_tool(
      harness.editor, press, {}, before, harness.session.native_tools(),
      nullptr);
  harness.input.mouse_local = end;
  harness.input.mouse_screen = end;
  harness.controller.handle_after_active_tool(
      harness.editor, harness.frame(false, true, false, start), {}, {},
      harness.session.native_tools(), nullptr);
  harness.controller.handle_after_active_tool(
      harness.editor, harness.frame(false, false, true, start), {}, {},
      harness.session.native_tools(), nullptr);

  const std::vector<quader::EntityId> selected = selected_entities(harness);
  EXPECT_NE(std::find(selected.begin(), selected.end(), box.entity),
            selected.end());
  EXPECT_NE(std::find(selected.begin(), selected.end(), curve.value().entity),
            selected.end());
  EXPECT_NE(std::find(selected.begin(), selected.end(), light.value().id),
            selected.end());
  EXPECT_FALSE(harness.session.curve().control_selection().empty());
}

TEST(ViewportInteractionControllerTests,
     ComponentMarqueeAggregatesHitsAcrossMultipleMeshesInOneCommit) {
  ViewportInteractionHarness harness;
  (void)create_box_at(harness, {-2.5f, 0.0f, 0.0f});
  (void)create_box_at(harness, {2.5f, 0.0f, 0.0f});
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Vertex)
                  .ok());
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Vertex);

  const auto upper_left =
      harness.camera.project_world_point({-4.0f, 2.0f, 0.0f});
  const auto lower_right =
      harness.camera.project_world_point({4.0f, -2.0f, 0.0f});
  ASSERT_TRUE(upper_left.visible);
  ASSERT_TRUE(lower_right.visible);
  const quader::QVec2 start = upper_left.position;
  const quader::QVec2 end = lower_right.position;
  harness.input.mouse_local = start;
  harness.input.mouse_screen = start;
  const auto press = harness.frame(true, true, false, start);
  const auto before =
      harness.controller.handle_before_active_tool(harness.editor, press);
  harness.controller.handle_after_active_tool(harness.editor, press, {}, before);
  harness.input.mouse_local = end;
  harness.input.mouse_screen = end;
  harness.controller.handle_after_active_tool(
      harness.editor, harness.frame(false, true, false, start), {}, {});
  harness.controller.handle_after_active_tool(
      harness.editor, harness.frame(false, false, true, start), {}, {});

  const quader::QuaderSelectionSummary selection =
      harness.editor.selection().summary();
  EXPECT_EQ(selection.mode, quader::QuaderSelectionMode::Vertex);
  EXPECT_GT(selection.selected_vertex_count, 8u)
      << "both cube documents must contribute component targets";
}

TEST(ViewportInteractionControllerTests,
     GroupMarqueePromotesTheWinningLeafAfterTypedArbitration) {
  ViewportInteractionHarness harness;
  const quader::QuaderModelingObjectRef box = create_unit_box(harness);
  ASSERT_TRUE(harness.editor.selection().select_object(box).ok());
  const auto inner = harness.editor.scene().group_selection("Marquee Inner");
  ASSERT_TRUE(inner.ok()) << inner.error_message();
  const auto outer = harness.editor.scene().group_selection("Marquee Outer");
  ASSERT_TRUE(outer.ok()) << outer.error_message();
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Group)
                  .ok());
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Group);

  const quader::QVec2 start{300.0f, 200.0f};
  const quader::QVec2 end{500.0f, 400.0f};
  harness.input.mouse_local = start;
  harness.input.mouse_screen = start;
  const auto press = harness.frame(true, true, false, start);
  const auto before =
      harness.controller.handle_before_active_tool(harness.editor, press);
  harness.controller.handle_after_active_tool(harness.editor, press, {}, before);
  harness.input.mouse_local = end;
  harness.input.mouse_screen = end;
  harness.controller.handle_after_active_tool(
      harness.editor, harness.frame(false, true, false, start), {}, {});
  harness.controller.handle_after_active_tool(
      harness.editor, harness.frame(false, false, true, start), {}, {});

  EXPECT_EQ(selected_entities(harness),
            std::vector<quader::EntityId>{inner.value().id});
}

TEST(ViewportInteractionControllerTests,
     GroupMarqueeProjectsFormalOccurrencesBeforeDeterministicPromotion) {
  ViewportInteractionHarness harness;
  const quader::QuaderModelingObjectRef source = create_unit_box(harness);
  ASSERT_TRUE(harness.editor.selection().select_object(source).ok());
  const auto instance =
      harness.editor.modeling().create_instance_from_selection();
  ASSERT_TRUE(instance.ok()) << instance.error_message();
  const quader::EntityId occurrence = instance.value().occurrence.root.id;
  ASSERT_NE(occurrence, quader::kInvalidEntityId);
  ASSERT_TRUE(harness.editor.selection().select_object(occurrence).ok());
  const auto group =
      harness.editor.scene().group_selection("Formal Marquee Group");
  ASSERT_TRUE(group.ok()) << group.error_message();
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Vertex)
                  .ok());
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Vertex);
  drag_selection_marquee(harness, {300.0f, 200.0f}, {500.0f, 400.0f});
  EXPECT_EQ(harness.editor.selection().summary().selected_vertex_count, 0u)
      << "Formal definition topology stays hidden outside instance edit.";

  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Group)
                  .ok());
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Group);

  drag_selection_marquee(harness, {300.0f, 200.0f}, {500.0f, 400.0f});

  EXPECT_EQ(selected_entities(harness),
            std::vector<quader::EntityId>{group.value().id});
}

TEST(ViewportInteractionControllerTests,
     MarqueeUsesLockedAndNonselectableMeshesOnlyAsRenderOccluders) {
  ViewportInteractionHarness harness;
  const quader::QuaderModelingObjectRef back =
      create_box_at(harness, {0.0f, 0.0f, -3.0f});
  const quader::QuaderModelingObjectRef front =
      create_box_at(harness, {0.0f, 0.0f, 2.0f});
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Object)
                  .ok());
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
  const quader::QVec2 start{300.0f, 200.0f};
  const quader::QVec2 end{500.0f, 400.0f};

  ASSERT_TRUE(harness.editor.scene().set_locked(front.entity, true).ok());
  drag_selection_marquee(harness, start, end);
  EXPECT_TRUE(selected_entities(harness).empty());

  ASSERT_TRUE(harness.editor.scene().set_locked(front.entity, false).ok());
  ASSERT_TRUE(harness.session.scene().set_selectable(front.entity, false));
  drag_selection_marquee(harness, start, end);
  EXPECT_TRUE(selected_entities(harness).empty());

  ASSERT_TRUE(harness.editor.scene().set_visible(front.entity, false).ok());
  drag_selection_marquee(harness, start, end);
  EXPECT_EQ(selected_entities(harness),
            std::vector<quader::EntityId>{back.entity});

  ASSERT_TRUE(harness.editor.selection().clear().ok());
  ASSERT_TRUE(harness.session.scene().set_selectable(front.entity, true));
  ASSERT_TRUE(harness.editor.scene().set_visible(front.entity, true).ok());
  ASSERT_TRUE(harness.editor.scene()
                  .set_transform(front.entity,
                                 {.position = {1.5f, 0.0f, 2.0f}})
                  .ok());
  ASSERT_TRUE(harness.editor.scene().set_locked(front.entity, true).ok());
  drag_selection_marquee(harness, start, end);
  EXPECT_EQ(selected_entities(harness),
            std::vector<quader::EntityId>{back.entity})
      << "An actual visible witness must preserve a partially occluded mesh.";
}

TEST(ViewportInteractionControllerTests,
     ComponentMarqueeRejectsWitnessesBehindLockedRenderSurfaces) {
  ViewportInteractionHarness harness;
  (void)create_box_at(harness, {0.0f, 0.0f, -3.0f});
  const quader::QuaderModelingObjectRef front =
      create_box_at(harness, {0.0f, 0.0f, 2.0f});
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Vertex)
                  .ok());
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Vertex);

  ASSERT_TRUE(harness.editor.scene().set_locked(front.entity, true).ok());
  drag_selection_marquee(harness, {300.0f, 200.0f}, {500.0f, 400.0f});
  EXPECT_EQ(harness.editor.selection().summary().selected_vertex_count, 0u);

  ASSERT_TRUE(harness.editor.scene().set_visible(front.entity, false).ok());
  drag_selection_marquee(harness, {300.0f, 200.0f}, {500.0f, 400.0f});
  EXPECT_GT(harness.editor.selection().summary().selected_vertex_count, 0u);
}

TEST(ViewportInteractionControllerTests,
     CurveMarqueeKeepsOnlyRenderVisiblePathFragments) {
  ViewportInteractionHarness harness;
  const auto curve = harness.editor.modeling().create_curve({
      .points = {{.position = {-4.0f, 0.0f, -3.0f}},
                 {.position = {0.0f, 0.0f, -3.0f}},
                 {.position = {4.0f, 0.0f, -3.0f}}},
  });
  ASSERT_TRUE(curve.ok()) << curve.error_message();
  const quader::QuaderModelingObjectRef occluder =
      create_box_at(harness, {0.0f, 0.0f, 2.0f});
  ASSERT_TRUE(harness.editor.scene().set_locked(occluder.entity, true).ok());
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Object)
                  .ok());
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);

  const auto center_right =
      harness.camera.project_world_point({0.25f, 0.25f, -3.0f});
  const auto center_left =
      harness.camera.project_world_point({-0.25f, -0.25f, -3.0f});
  ASSERT_TRUE(center_right.visible);
  ASSERT_TRUE(center_left.visible);
  drag_selection_marquee(harness, center_right.position,
                         center_left.position);
  EXPECT_TRUE(selected_entities(harness).empty());

  const auto visible_right =
      harness.camera.project_world_point({-2.75f, 0.25f, -3.0f});
  const auto visible_left =
      harness.camera.project_world_point({-3.25f, -0.25f, -3.0f});
  ASSERT_TRUE(visible_right.visible);
  ASSERT_TRUE(visible_left.visible);
  drag_selection_marquee(harness, visible_right.position,
                         visible_left.position);
  EXPECT_EQ(selected_entities(harness),
            std::vector<quader::EntityId>{curve.value().entity});

  ASSERT_TRUE(harness.editor.selection().clear().ok());
  ASSERT_TRUE(
      harness.editor.scene().set_visible(occluder.entity, false).ok());
  drag_selection_marquee(harness, center_right.position,
                         center_left.position);
  EXPECT_EQ(selected_entities(harness),
            std::vector<quader::EntityId>{curve.value().entity});
}

TEST(ViewportInteractionControllerTests,
     SelectionBoxRejectsAReleaseOlderThanItsAcceptedPress) {
  ViewportInteractionHarness harness;
  (void)create_unit_box(harness);
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Object)
                  .ok());
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);

  const quader::QVec2 start{300.0f, 200.0f};
  const quader::QVec2 end{500.0f, 400.0f};
  harness.input.mouse_local = start;
  harness.input.mouse_screen = start;
  harness.input.pointer_pressed_edges = {{
      .button = quader::QuaderMouseButton::Left,
      .local = start,
      .sequence = 20u,
  }};
  const quader::ViewportInteractionFrame press =
      harness.frame(true, true, false, start);
  const auto before_press =
      harness.controller.handle_before_active_tool(harness.editor, press);
  harness.controller.handle_after_active_tool(harness.editor, press, {},
                                              before_press);
  harness.input.pointer_pressed_edges.clear();

  harness.input.mouse_local = end;
  harness.input.mouse_screen = end;
  harness.controller.handle_after_active_tool(
      harness.editor, harness.frame(false, true, false, start), {}, {});
  ASSERT_TRUE(harness.controller.selection_marquee().active);

  harness.input.pointer_released_edges = {{
      .button = quader::QuaderMouseButton::Left,
      .local = end,
      .sequence = 19u,
  }};
  harness.controller.handle_after_active_tool(
      harness.editor, harness.frame(false, false, true, start), {}, {});
  EXPECT_FALSE(harness.controller.selection_marquee().active);
  EXPECT_EQ(harness.editor.selection().summary().selected_object_count, 0u);

  harness.input.pointer_released_edges.clear();
  harness.input.mouse_local = start;
  harness.input.mouse_screen = start;
  harness.input.pointer_pressed_edges = {{
      .button = quader::QuaderMouseButton::Left,
      .local = start,
      .sequence = 22u,
  }};
  const quader::ViewportInteractionFrame next_press =
      harness.frame(true, true, false, start);
  const auto before_next =
      harness.controller.handle_before_active_tool(harness.editor, next_press);
  harness.controller.handle_after_active_tool(harness.editor, next_press, {},
                                              before_next);
  harness.input.pointer_pressed_edges.clear();
  harness.input.mouse_local = end;
  harness.input.mouse_screen = end;
  harness.controller.handle_after_active_tool(
      harness.editor, harness.frame(false, true, false, start), {}, {});
  ASSERT_TRUE(harness.controller.selection_marquee().active);
  harness.input.pointer_released_edges = {{
      .button = quader::QuaderMouseButton::Left,
      .local = end,
      .sequence = 23u,
  }};
  harness.controller.handle_after_active_tool(
      harness.editor, harness.frame(false, false, true, start), {}, {});
  EXPECT_EQ(harness.editor.selection().summary().selected_object_count, 1u);
  EXPECT_TRUE(harness.controller.selection_marquee().active);
}

TEST(ViewportInteractionControllerTests,
     CanceledSelectionBoxReservesItsPhysicalRelease) {
  ViewportInteractionHarness harness;
  (void)create_unit_box(harness);
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Object)
                  .ok());
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);

  const quader::QVec2 start{300.0f, 200.0f};
  const quader::QVec2 end{500.0f, 400.0f};
  harness.input.mouse_local = start;
  harness.input.mouse_screen = start;
  harness.input.pointer_pressed_edges = {{
      .button = quader::QuaderMouseButton::Left,
      .local = start,
      .sequence = 30u,
  }};
  const quader::ViewportInteractionFrame press =
      harness.frame(true, true, false, start);
  const auto before_press =
      harness.controller.handle_before_active_tool(harness.editor, press);
  harness.controller.handle_after_active_tool(harness.editor, press, {},
                                              before_press);
  harness.input.pointer_pressed_edges.clear();

  harness.input.mouse_local = end;
  harness.input.mouse_screen = end;
  harness.controller.handle_after_active_tool(
      harness.editor, harness.frame(false, true, false, start), {}, {});
  ASSERT_TRUE(harness.controller.selection_marquee().active);

  harness.input.pointer_interaction_canceled = true;
  harness.controller.handle_after_active_tool(
      harness.editor, harness.frame(false, true, false, start), {}, {});
  EXPECT_FALSE(harness.controller.selection_marquee().active);
  harness.input.pointer_interaction_canceled = false;

  harness.input.pointer_released_edges = {{
      .button = quader::QuaderMouseButton::Left,
      .local = end,
      .sequence = 31u,
  }};
  harness.controller.handle_after_active_tool(
      harness.editor, harness.frame(false, false, true, start), {}, {});
  EXPECT_FALSE(harness.controller.selection_marquee().active);
  EXPECT_EQ(harness.editor.selection().summary().selected_object_count, 0u);
}

TEST(ViewportInteractionControllerTests,
     DenseComponentBoxDragDefersSelectionWorkUntilRelease) {
    ViewportInteractionHarness harness;

    quader_poly::CubePrimitiveSpec dense_cube;
    dense_cube.width = 2.0f;
    dense_cube.height = 2.0f;
    dense_cube.depth = 2.0f;
    dense_cube.width_segments = 24;
    dense_cube.height_segments = 24;
    dense_cube.depth_segments = 24;
    const quader::ModelingCreateBoxResult mesh =
        harness.session.modeling().create_mesh({
            .document = quader_poly::make_cube(dense_cube),
            .name = "Viewport Dense Box Selection Cube",
        });
    ASSERT_TRUE(mesh.ok) << mesh.message;

    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Face)
                    .ok);
    ASSERT_TRUE(harness.editor.selection().clear().ok);
    harness.router.set_selection_mode(quader::ViewportSelectionMode::Face);

    const quader::ViewportScreenPoint first_corner =
        harness.camera.project_world_point({-1.0f, -1.0f, 1.0f});
    const quader::ViewportScreenPoint second_corner =
        harness.camera.project_world_point({1.0f, 1.0f, 1.0f});
    ASSERT_TRUE(first_corner.visible);
    ASSERT_TRUE(second_corner.visible);
    const float left =
        std::min(first_corner.position.x, second_corner.position.x);
    const float right =
        std::max(first_corner.position.x, second_corner.position.x);
    const float top =
        std::min(first_corner.position.y, second_corner.position.y);
    const float bottom =
        std::max(first_corner.position.y, second_corner.position.y);
    const quader::QVec2 start{left - 2.0f, top - 2.0f};
    quader::QVec2 end{left, bottom + 2.0f};

    harness.input.mouse_local = start;
    harness.input.mouse_screen = start;
    const quader::ViewportBeforeToolInteractionResult before_tool =
        harness.controller.handle_before_active_tool(
            harness.editor, harness.frame(true, true, false, start));
    EXPECT_FALSE(before_tool.gizmo_consumed);
    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(true, true, false, start),
        quader::ViewportToolResult{}, before_tool);

    const std::uint64_t selection_revision_before_drag =
        harness.editor.selection().summary().selection_revision;
    const std::uint64_t rectangle_projections_before_drag =
        harness.session.modeling().rectangle_picking_projection_build_count();
    const std::uint64_t rectangle_resolutions_before_drag =
        harness.session.modeling().rectangle_selection_resolution_build_count();
    std::uint64_t observed_selection_revision = selection_revision_before_drag;
    int selection_changes_during_drag = 0;
    constexpr int kDragSamples = 24;
    for (int sample = 1; sample <= kDragSamples; ++sample) {
      end.x = left + (right - left + 2.0f) *
                         static_cast<float>(sample) /
                         static_cast<float>(kDragSamples);
      harness.input.mouse_local = end;
      harness.input.mouse_screen = end;
      harness.controller.handle_after_active_tool(
          harness.editor, harness.frame(false, true, false, start),
          quader::ViewportToolResult{},
          quader::ViewportBeforeToolInteractionResult{});

      const quader::QuaderSelectionSummary during_drag =
          harness.editor.selection().summary();
      if (during_drag.selection_revision != observed_selection_revision) {
        ++selection_changes_during_drag;
        observed_selection_revision = during_drag.selection_revision;
        (void)harness.editor.query().selection_overlay();
      }
    }

    const quader::QuaderSelectionSummary before_release =
        harness.editor.selection().summary();
    EXPECT_EQ(selection_changes_during_drag, 0);
    EXPECT_EQ(before_release.selection_revision, selection_revision_before_drag);
    EXPECT_EQ(before_release.selected_face_count, 0u);
    EXPECT_EQ(
        harness.session.modeling().rectangle_picking_projection_build_count(),
        rectangle_projections_before_drag);
    EXPECT_EQ(
        harness.session.modeling().rectangle_selection_resolution_build_count(),
        rectangle_resolutions_before_drag);

    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(false, false, true, start),
        quader::ViewportToolResult{},
        quader::ViewportBeforeToolInteractionResult{});
    const quader::QuaderSelectionSummary after_release =
        harness.editor.selection().summary();
    EXPECT_GT(after_release.selection_revision, selection_revision_before_drag);
    EXPECT_GT(after_release.selected_face_count, 0u);
    EXPECT_EQ(
        harness.session.modeling().rectangle_picking_projection_build_count(),
        rectangle_projections_before_drag + 1u);
    EXPECT_EQ(
        harness.session.modeling().rectangle_selection_resolution_build_count(),
        rectangle_resolutions_before_drag + 1u);
    (void)harness.editor.query().selection_overlay();
}

TEST(ViewportInteractionControllerTests, ShiftBoxDragAddsSelectionOnRelease) {
    ViewportInteractionHarness harness;
    const quader::QuaderModelingObjectRef left =
        create_box_at(harness, {-3.0f, 0.0f, 0.0f});
    const quader::QuaderModelingObjectRef right =
        create_box_at(harness, {3.0f, 0.0f, 0.0f});

    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Object)
                    .ok);
    ASSERT_TRUE(harness.editor.selection().select_object(left).ok);
    harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);

    const quader::ViewportScreenPoint projected_right =
        harness.camera.project_world_point({3.0f, 0.0f, 0.0f});
    ASSERT_TRUE(projected_right.visible);
    const quader::QVec2 start =
        projected_right.position - quader::QVec2{120.0f, 120.0f};
    const quader::QVec2 end =
        projected_right.position + quader::QVec2{120.0f, 120.0f};

    harness.input.modifiers.shift = true;
    harness.input.mouse_local = start;
    harness.input.mouse_screen = start;
    const quader::ViewportBeforeToolInteractionResult before_tool =
        harness.controller.handle_before_active_tool(
            harness.editor, harness.frame(true, true, false, start));
    EXPECT_FALSE(before_tool.gizmo_consumed);
    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(true, true, false, start),
        quader::ViewportToolResult{}, before_tool);

    harness.input.mouse_local = end;
    harness.input.mouse_screen = end;
    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(false, true, false, start),
        quader::ViewportToolResult{},
        quader::ViewportBeforeToolInteractionResult{});

    EXPECT_EQ(harness.editor.selection().summary().selected_object_count, 1u);
    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(false, false, true, start),
        quader::ViewportToolResult{},
        quader::ViewportBeforeToolInteractionResult{});

    EXPECT_EQ(harness.editor.selection().summary().selected_object_count, 2u);
    const std::vector<quader::EntityId> selected = selected_entities(harness);
    EXPECT_NE(std::find(selected.begin(), selected.end(), left.entity), selected.end());
    EXPECT_NE(std::find(selected.begin(), selected.end(), right.entity), selected.end());
}

TEST(ViewportInteractionControllerTests, PrimaryBoxDragRemovesSelectionOnRelease) {
    ViewportInteractionHarness harness;
    const quader::QuaderModelingObjectRef left =
        create_box_at(harness, {-3.0f, 0.0f, 0.0f});
    const quader::QuaderModelingObjectRef right =
        create_box_at(harness, {3.0f, 0.0f, 0.0f});

    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Object)
                    .ok);
    ASSERT_TRUE(harness.editor.selection().select_objects({left, right}).ok);
    harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);

    const quader::ViewportScreenPoint projected_right =
        harness.camera.project_world_point({3.0f, 0.0f, 0.0f});
    ASSERT_TRUE(projected_right.visible);
    const quader::QVec2 start =
        projected_right.position - quader::QVec2{120.0f, 120.0f};
    const quader::QVec2 end =
        projected_right.position + quader::QVec2{120.0f, 120.0f};

    harness.input.modifiers.primary = true;
    harness.input.mouse_local = start;
    harness.input.mouse_screen = start;
    const quader::ViewportBeforeToolInteractionResult before_tool =
        harness.controller.handle_before_active_tool(
            harness.editor, harness.frame(true, true, false, start));
    EXPECT_FALSE(before_tool.gizmo_consumed);
    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(true, true, false, start),
        quader::ViewportToolResult{}, before_tool);

    harness.input.mouse_local = end;
    harness.input.mouse_screen = end;
    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(false, true, false, start),
        quader::ViewportToolResult{},
        quader::ViewportBeforeToolInteractionResult{});

    EXPECT_EQ(harness.editor.selection().summary().selected_object_count, 2u);
    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(false, false, true, start),
        quader::ViewportToolResult{},
        quader::ViewportBeforeToolInteractionResult{});

    EXPECT_EQ(harness.editor.selection().summary().selected_object_count, 1u);
    const std::vector<quader::EntityId> selected = selected_entities(harness);
    EXPECT_NE(std::find(selected.begin(), selected.end(), left.entity), selected.end());
    EXPECT_EQ(std::find(selected.begin(), selected.end(), right.entity), selected.end());
}

TEST(ViewportInteractionControllerTests, FastDragFrameRetainsCommittedBoxMarqueeForRender) {
    ViewportInteractionHarness harness;
    (void)create_unit_box(harness);

    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Object)
                    .ok);
    ASSERT_TRUE(harness.editor.selection().clear().ok);
    harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);

    const quader::QVec2 start{500.0f, 400.0f};
    const quader::QVec2 end{300.0f, 200.0f};
    harness.input.mouse_local = end;
    harness.input.mouse_screen = end;

    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(true, false, true, start),
        quader::ViewportToolResult{},
        quader::ViewportBeforeToolInteractionResult{});

    const quader::ViewportSelectionMarquee marquee =
        harness.controller.selection_marquee();
    ASSERT_TRUE(marquee.active);
    EXPECT_EQ(marquee.start.x, start.x);
    EXPECT_EQ(marquee.start.y, start.y);
    EXPECT_EQ(marquee.end.x, end.x);
    EXPECT_EQ(marquee.end.y, end.y);
    EXPECT_EQ(marquee.rule, quader::ModelingBoxSelectionRule::Crossing);
    EXPECT_EQ(harness.editor.selection().summary().selected_object_count, 1u);

    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(), quader::ViewportToolResult{},
        quader::ViewportBeforeToolInteractionResult{});
    EXPECT_FALSE(harness.controller.selection_marquee().active);
}

TEST(ViewportInteractionControllerTests, FastDragStartsFromAcceptedPressWhenCurrentMouseIsOutsideViewport) {
    ViewportInteractionHarness harness;
    (void)create_unit_box(harness);

    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Object)
                    .ok);
    ASSERT_TRUE(harness.editor.selection().clear().ok);
    harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);

    const quader::QVec2 start{500.0f, 400.0f};
    const quader::QVec2 end{-20.0f, -20.0f};
    harness.input.viewport_hovered = false;
    harness.input.mouse_local = end;
    harness.input.mouse_screen = end;

    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(true, false, true, start),
        quader::ViewportToolResult{},
        quader::ViewportBeforeToolInteractionResult{});

    const quader::ViewportSelectionMarquee marquee =
        harness.controller.selection_marquee();
    ASSERT_TRUE(marquee.active);
    EXPECT_EQ(marquee.start.x, start.x);
    EXPECT_EQ(marquee.start.y, start.y);
    EXPECT_EQ(marquee.end.x, end.x);
    EXPECT_EQ(marquee.end.y, end.y);
    EXPECT_EQ(marquee.rule, quader::ModelingBoxSelectionRule::Crossing);
    EXPECT_EQ(harness.editor.selection().summary().selected_object_count, 1u);
}

TEST(ViewportInteractionControllerTests,
     TransformGizmoHoverClearsCandidateWireAndKeepsSelectedComponentWires) {
  for (const auto mode : {quader::QuaderSelectionMode::Vertex,
                         quader::QuaderSelectionMode::Edge,
                         quader::QuaderSelectionMode::Face}) {
    SCOPED_TRACE(static_cast<int>(mode));
    for (const auto tool : {quader::ViewportToolId::Move,
                            quader::ViewportToolId::Scale,
                            quader::ViewportToolId::Rotate}) {
      SCOPED_TRACE(static_cast<int>(tool));
      ViewportInteractionHarness harness;
      const auto background = harness.editor.modeling().create_box(
          quader::QuaderBounds{.min = {-4.0f, -3.0f, -6.0f},
                               .max = {4.0f, 3.0f, -5.0f}},
          "Gizmo hover background");
      ASSERT_TRUE(background.ok());
      const auto box = create_unit_box(harness);
      select_single_component(harness, box, mode);
      harness.router.set_selection_mode(mode == quader::QuaderSelectionMode::Vertex
          ? quader::ViewportSelectionMode::Vertex
          : mode == quader::QuaderSelectionMode::Edge
              ? quader::ViewportSelectionMode::Edge
              : quader::ViewportSelectionMode::Face);
      ASSERT_TRUE(harness.editor.tools().activate(tool).ok());
      harness.router.set_active_tool(tool);
      const auto selected = harness.editor.selection().summary();
      const auto dispatch = [&](quader::QVec2 point) {
        harness.input.mouse_local = point;
        harness.input.mouse_screen = point;
        return harness.dispatch_frame(harness.frame());
      };
      const auto source_wire_count = [&](int object_id) {
        const auto overlay = quader::test_support::visible_modeling_overlay_frame(
            harness.session.modeling());
        return std::count_if(overlay.lines.begin(), overlay.lines.end(), [&](const auto &line) {
          return line.layer == quader::ViewportOverlayLayer::SourceWire &&
                 line.element.object_id == object_id;
        });
      };
      (void)dispatch({20.0f, 20.0f});
      const auto *gizmo = harness.controller.transform_gizmo_frame();
      ASSERT_NE(gizmo, nullptr);
      const auto background_target = harness.camera.project_world_point(
          mode == quader::QuaderSelectionMode::Vertex ? quader::QVec3{4.0f, 3.0f, -5.0f}
          : mode == quader::QuaderSelectionMode::Edge ? quader::QVec3{4.0f, 0.0f, -5.0f}
                                                     : quader::QVec3{3.0f, 2.0f, -5.0f});
      ASSERT_TRUE(background_target.visible);
      const auto away = background_target.position;
      std::vector<quader::QVec2> handles;
      if (tool == quader::ViewportToolId::Rotate) {
        const auto *ring = find_ring(*gizmo, quader_editor_core::TransformAxis::Z);
        ASSERT_NE(ring, nullptr);
        handles.push_back(ring->screen_start);
        handles.push_back(gizmo->info.screen_pivot + quader::QVec2{25.0f, 25.0f});
      } else {
        handles.push_back(gizmo->info.screen_pivot);
        const auto *axis = find_axis(*gizmo, quader_editor_core::TransformAxis::X);
        ASSERT_NE(axis, nullptr);
        handles.push_back(axis->screen_tip);
        const auto plane = std::find_if(
            gizmo->planes.begin(), gizmo->planes.end(), [](const auto &value) {
              return value.axis == quader_editor_core::TransformAxis::Xy;
            });
        ASSERT_NE(plane, gizmo->planes.end());
        handles.push_back((plane->screen0 + plane->screen1 +
                           plane->screen2 + plane->screen3) * 0.25f);
      }
      for (const auto handle : handles) {
        SCOPED_TRACE(::testing::Message() << handle.x << ", " << handle.y);
        EXPECT_FALSE(dispatch(away).gizmo_hovered);
        ASSERT_EQ(harness.editor.selection().hover_summary().target.object_id,
                  background.value().modeling_object_id);
        ASSERT_GT(source_wire_count(background.value().modeling_object_id), 0);
        const auto selected_wire_count = source_wire_count(box.modeling_object_id);
        ASSERT_GT(selected_wire_count, 0);
        const auto hovered = dispatch(handle);
        EXPECT_TRUE(hovered.gizmo_hovered);
        EXPECT_FALSE(hovered.gizmo_consumed);
        gizmo = harness.controller.transform_gizmo_frame();
        ASSERT_NE(gizmo, nullptr);
        EXPECT_NE(gizmo->info.hover_axis, quader_editor_core::TransformAxis::None);
        EXPECT_EQ(harness.editor.selection().hover_summary().target.kind,
                  quader::QuaderActiveSelectionKind::None);
        EXPECT_EQ(source_wire_count(background.value().modeling_object_id), 0);
        EXPECT_EQ(source_wire_count(box.modeling_object_id), selected_wire_count);
        const auto scene_revision = harness.session.scene().revision();
        const auto content_revision = harness.session.modeling().content_revision();
        for (int frame = 0; frame < 5; ++frame) {
          EXPECT_TRUE(dispatch(handle).gizmo_hovered);
          EXPECT_EQ(harness.session.scene().revision(), scene_revision);
          EXPECT_EQ(harness.session.modeling().content_revision(), content_revision);
        }
        EXPECT_FALSE(dispatch(away).gizmo_hovered);
        EXPECT_GT(source_wire_count(background.value().modeling_object_id), 0);
        EXPECT_EQ(source_wire_count(box.modeling_object_id), selected_wire_count);
        EXPECT_EQ(harness.editor.selection().summary().selection_revision,
                  selected.selection_revision);
        EXPECT_EQ(harness.editor.selection().summary().selected_vertex_count,
                  selected.selected_vertex_count);
        EXPECT_EQ(harness.editor.selection().summary().selected_edge_count,
                  selected.selected_edge_count);
        EXPECT_EQ(harness.editor.selection().summary().selected_face_count,
                  selected.selected_face_count);
      }
    }
  }
}

TEST(ViewportInteractionControllerTests,
     TransformGizmoHoverOwnershipClearsWhenHandleIsUnavailable) {
  ViewportInteractionHarness harness;
  const auto box = create_unit_box(harness);
  ASSERT_TRUE(harness.editor.selection().select_object(box).ok());
  ASSERT_TRUE(harness.editor.tools().activate(quader::ViewportToolId::Move).ok());
  harness.router.set_active_tool(quader::ViewportToolId::Move);
  (void)harness.dispatch_frame(harness.frame());
  const auto *gizmo = harness.controller.transform_gizmo_frame();
  ASSERT_NE(gizmo, nullptr);
  harness.input.mouse_local = gizmo->info.screen_pivot;
  const auto hover = [&]() { return harness.dispatch_frame(harness.frame()); };
  ASSERT_TRUE(hover().gizmo_hovered);
  for (int unavailable = 0; unavailable < 6; ++unavailable) {
    SCOPED_TRACE(unavailable);
    harness.input.viewport_focused = unavailable != 0;
    harness.input.viewport_hovered = unavailable != 1;
    harness.input.navigation_active = unavailable == 2;
    harness.input.pointer_interaction_canceled = unavailable == 3;
    harness.settings.set_bool_value(quader::ViewportBoolSettingId::ShowGizmos,
                                    unavailable != 4);
    harness.input_result.camera_changed = unavailable == 5;
    EXPECT_FALSE(hover().gizmo_hovered);
  }
  harness.input.viewport_focused = true;
  harness.input.viewport_hovered = true;
  harness.input.navigation_active = false;
  harness.input.pointer_interaction_canceled = false;
  harness.settings.set_bool_value(quader::ViewportBoolSettingId::ShowGizmos, true);
  harness.input_result = {};
  EXPECT_TRUE(hover().gizmo_hovered);
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  EXPECT_FALSE(hover().gizmo_hovered);
}

TEST(ViewportInteractionControllerTests, TransformGizmoFrameSurvivesNavigationFrames) {
    ViewportInteractionHarness harness;
    const quader::QuaderModelingObjectRef box = create_unit_box(harness);
    ASSERT_TRUE(harness.editor.selection().select_object(box).ok);
    ASSERT_TRUE(harness.editor.tools().activate(quader::ViewportToolId::Move).ok);
    harness.router.set_active_tool(quader::ViewportToolId::Move);

    const quader::ViewportBeforeToolInteractionResult warmup =
        harness.controller.handle_before_active_tool(harness.editor,
                                                     harness.frame());
    ASSERT_FALSE(warmup.gizmo_consumed);
    ASSERT_NE(harness.controller.transform_gizmo_frame(), nullptr);

    harness.input_result.navigation_active = true;
    harness.input_result.camera_changed = true;
    const quader::ViewportBeforeToolInteractionResult navigation =
        harness.controller.handle_before_active_tool(harness.editor,
                                                     harness.frame());

    EXPECT_FALSE(navigation.gizmo_consumed);
    const quader_editor_core::TransformGizmoFrame *frame =
        harness.controller.transform_gizmo_frame();
    ASSERT_NE(frame, nullptr);
    EXPECT_TRUE(frame->info.ok);
}

TEST(ViewportInteractionControllerTests,
     CameraOnlyFramesReuseSelectionPivotWithoutBoundsRecomputation) {
    ViewportInteractionHarness harness;
    const quader::QuaderModelingObjectRef box = create_unit_box(harness);
    ASSERT_TRUE(harness.editor.selection().select_object(box).ok);
    ASSERT_TRUE(harness.editor.tools().activate(quader::ViewportToolId::Move).ok);
    harness.router.set_active_tool(quader::ViewportToolId::Move);

    (void)harness.controller.handle_before_active_tool(harness.editor,
                                                       harness.frame());
    EXPECT_EQ(harness.controller.transform_gizmo_selection_cache_stats()
                  .selection_pivot_recompute_count,
              1u);

    harness.input_result.navigation_active = true;
    harness.input_result.camera_changed = true;
    for (const quader::QVec3 direction : {
             quader::QVec3{-0.8f, -0.2f, -0.5f},
             quader::QVec3{-0.5f, -0.6f, -0.4f},
             quader::QVec3{-0.2f, -0.4f, -0.9f},
             quader::QVec3{-0.7f, -0.5f, -0.1f},
         }) {
      harness.camera.set_view_direction(direction);
      (void)harness.controller.handle_before_active_tool(harness.editor,
                                                         harness.frame());
    }

    const quader::TransformGizmoSelectionCacheStats &camera_stats =
        harness.controller.transform_gizmo_selection_cache_stats();
    EXPECT_EQ(camera_stats.selection_pivot_recompute_count, 1u);
    EXPECT_EQ(camera_stats.selection_cache_hit_count, 4u);
    ASSERT_NE(harness.controller.transform_gizmo_frame(), nullptr);

    harness.input_result = {};
    ASSERT_TRUE(harness.editor.selection().clear().ok);
    (void)harness.controller.handle_before_active_tool(harness.editor,
                                                       harness.frame());
    EXPECT_EQ(harness.controller.transform_gizmo_selection_cache_stats()
                  .selection_pivot_recompute_count,
              1u);
    EXPECT_EQ(harness.controller.transform_gizmo_frame(), nullptr);

    ASSERT_TRUE(harness.editor.selection().select_object(box).ok);
    (void)harness.controller.handle_before_active_tool(harness.editor,
                                                       harness.frame());
    EXPECT_EQ(harness.controller.transform_gizmo_selection_cache_stats()
                  .selection_pivot_recompute_count,
              2u);

    harness.session.pivot().begin_quick({3.0f, 4.0f, 5.0f});
    (void)harness.controller.handle_before_active_tool(harness.editor,
                                                       harness.frame());
    EXPECT_EQ(harness.controller.transform_gizmo_selection_cache_stats()
                  .selection_pivot_recompute_count,
              3u);
    const quader_editor_core::TransformGizmoFrame *quick_pivot_frame =
        harness.controller.transform_gizmo_frame();
    ASSERT_NE(quick_pivot_frame, nullptr);
    EXPECT_FLOAT_EQ(quick_pivot_frame->info.pivot.x, 3.0f);
    EXPECT_FLOAT_EQ(quick_pivot_frame->info.pivot.y, 4.0f);
    EXPECT_FLOAT_EQ(quick_pivot_frame->info.pivot.z, 5.0f);

    harness.session.pivot().end_quick_edit();
    (void)harness.controller.handle_before_active_tool(harness.editor,
                                                       harness.frame());
    EXPECT_EQ(harness.controller.transform_gizmo_selection_cache_stats()
                  .selection_pivot_recompute_count,
              3u);
}

TEST(ViewportInteractionControllerTests,
     PersistentPivotRejectsUnstableMeshBeforeActivation) {
    ViewportInteractionHarness harness;
    const quader::QuaderModelingObjectRef box = create_unit_box(harness);
    ASSERT_TRUE(harness.editor.selection().select_object(box).ok);
    const quader::AssetId mesh =
        harness.session.scene().mesh(box.entity)->mesh;
    ASSERT_TRUE(harness.session.assets().update_mesh(
        mesh, [](quader::MeshAsset &asset) {
          asset.has_stable_cpu_topology = false;
        }));

    EXPECT_FALSE(
        harness.editor.tools().can_activate(quader::ViewportToolId::Pivot));
    EXPECT_FALSE(
        harness.editor.tools().activate(quader::ViewportToolId::Pivot).ok);
    EXPECT_FALSE(harness.session.pivot().persistent_active());
}

TEST(ViewportInteractionControllerTests,
     FailedPivotActivationPreservesMultiSelectionAndActiveObject) {
    ViewportInteractionHarness harness;
    const quader::QuaderModelingObjectRef first = create_unit_box(harness);
    const quader::QuaderModelingObjectRef invalid = create_unit_box(harness);
    ASSERT_TRUE(harness.editor.selection()
                    .select_objects({first.entity, invalid.entity},
                                    invalid.entity)
                    .ok);
    const quader::AssetId mesh =
        harness.session.scene().mesh(invalid.entity)->mesh;
    ASSERT_TRUE(harness.session.assets().update_mesh(
        mesh, [](quader::MeshAsset &asset) {
          for (quader::MeshVertex &vertex :
               asset.edit_geometry().vertices) {
            vertex.position.x = std::numeric_limits<float>::max();
          }
        }));
    const std::vector<quader::EntityId> selected_before =
        harness.session.scene().selected_entities();
    const quader::EntityId active_before =
        harness.session.scene().primary_selection();

    EXPECT_FALSE(
        harness.editor.tools().can_activate(quader::ViewportToolId::Pivot));
    EXPECT_FALSE(
        harness.editor.tools().activate(quader::ViewportToolId::Pivot).ok);
    EXPECT_FALSE(harness.session.pivot().persistent_active());
    EXPECT_EQ(harness.session.scene().selected_entities(), selected_before);
    EXPECT_EQ(harness.session.scene().primary_selection(), active_before);
}

TEST(ViewportInteractionControllerTests,
     PersistentPivotRejectsSingularWorldTransformBeforeActivation) {
    ViewportInteractionHarness harness;
    const quader::QuaderModelingObjectRef box = create_unit_box(harness);
    quader::TransformComponent singular =
        *harness.session.scene().transform(box.entity);
    singular.scale = {1.0f, 0.0f, 1.0f};
    harness.session.scene().set_transform(box.entity, singular);
    ASSERT_TRUE(harness.editor.selection().select_object(box).ok);

    EXPECT_FALSE(
        harness.editor.tools().can_activate(quader::ViewportToolId::Pivot));
    EXPECT_FALSE(
        harness.editor.tools().activate(quader::ViewportToolId::Pivot).ok);
    EXPECT_FALSE(harness.session.pivot().persistent_active());
}

TEST(ViewportInteractionControllerTests,
     PersistentPivotNeverPublishesANonFinitePreview) {
    ViewportInteractionHarness harness;
    const quader::QuaderModelingObjectRef box =
        create_box_at(harness, {4.0f, -2.0f, 3.0f});
    ASSERT_TRUE(harness.editor.selection().select_object(box).ok);
    harness.session.history().clear();
    ASSERT_TRUE(
        harness.editor.tools().activate(quader::ViewportToolId::Pivot).ok);
    const std::optional<quader::QVec3> before =
        harness.session.pivot().persistent_preview_position();
    ASSERT_TRUE(before.has_value());

    harness.session.pivot().set_persistent_preview_position(
        {std::numeric_limits<float>::quiet_NaN(), 2.0f, 3.0f});
    const std::optional<quader::QVec3> after =
        harness.session.pivot().persistent_preview_position();
    ASSERT_TRUE(after.has_value());
    EXPECT_NEAR(after->x, before->x, 0.001f);
    EXPECT_NEAR(after->y, before->y, 0.001f);
    EXPECT_NEAR(after->z, before->z, 0.001f);
    const quader::PivotHandleState handle =
        harness.session.pivot().handle_state();
    EXPECT_TRUE(std::isfinite(handle.world_position.x));
    EXPECT_TRUE(std::isfinite(handle.world_position.y));
    EXPECT_TRUE(std::isfinite(handle.world_position.z));

    ASSERT_TRUE(harness.editor.tools().commit_active_tool_preview().ok);
    EXPECT_FALSE(harness.editor.history().can_undo());
    EXPECT_FALSE(harness.session.scene().transform(box.entity)->custom_pivot);
}

TEST(ViewportInteractionControllerTests,
     PivotActivationSurvivesTheQtPostActivationInteractionReset) {
    ViewportInteractionHarness harness;
    ASSERT_TRUE(harness.settings.set_bool_value(
        quader::ViewportBoolSettingId::GridSnap, false));
    ASSERT_TRUE(harness.settings.set_bool_value(
        quader::ViewportBoolSettingId::VertexSnap, false));
    const quader::QuaderModelingObjectRef box = create_unit_box(harness);
    ASSERT_TRUE(harness.editor.selection().select_object(box).ok);
    ASSERT_TRUE(
        harness.editor.tools().activate(quader::ViewportToolId::Move).ok);
    harness.router.set_active_tool(quader::ViewportToolId::Move);

    (void)harness.controller.handle_before_active_tool(harness.editor,
                                                       harness.frame());
    const quader_editor_core::TransformGizmoFrame *gizmo =
        harness.controller.transform_gizmo_frame();
    ASSERT_NE(gizmo, nullptr);
    const quader_editor_core::TransformGizmoAxisPrimitive *axis =
        find_axis(*gizmo, quader_editor_core::TransformAxis::X);
    ASSERT_NE(axis, nullptr);
    const quader::QVec2 press_position = axis->screen_tip;
    const quader::QVec2 drag_position =
        press_position + quader::QVec2{72.0f, 0.0f};
    const quader::TransformComponent before =
        *harness.session.scene().transform(box.entity);

    harness.input.mouse_local = press_position;
    harness.input.mouse_screen = press_position;
    ASSERT_TRUE(harness.controller
                    .handle_before_active_tool(
                        harness.editor,
                        harness.frame(true, true, false, press_position))
                    .gizmo_consumed);
    harness.input.mouse_local = drag_position;
    harness.input.mouse_screen = drag_position;
    ASSERT_TRUE(harness.controller
                    .handle_before_active_tool(
                        harness.editor,
                        harness.frame(false, true, false, press_position))
                    .gizmo_consumed);
    ASSERT_TRUE(harness.controller.dragging());
    EXPECT_GT(quader::length(
                  harness.session.scene().transform(box.entity)->position -
                  before.position),
              0.001f);

    const quader::QuaderApiVoidResult activated =
        harness.editor.tools().activate(quader::ViewportToolId::Pivot);
    ASSERT_TRUE(activated.ok) << activated.message;
    ASSERT_TRUE(harness.session.pivot().persistent_active());
    EXPECT_TRUE(harness.controller.dragging());
    expect_transform_near(*harness.session.scene().transform(box.entity),
                          before);
    const std::uint64_t scene_revision_after_activation =
        harness.session.scene().revision();
    const std::uint64_t asset_revision_after_activation =
        harness.session.assets().revision();

    // Qt clears its local interaction state immediately after dispatching the
    // native activation. The resulting second modeling cancel is a no-op and
    // must not invalidate the newly captured Pivot session.
    harness.controller.reset(&harness.editor);

    EXPECT_FALSE(harness.controller.dragging());
    EXPECT_TRUE(harness.session.pivot().persistent_active());
    EXPECT_EQ(harness.editor.tools().active_tool(), "pivot");
    EXPECT_TRUE(harness.session.pivot().handle_state().visible);
    EXPECT_EQ(harness.session.scene().revision(),
              scene_revision_after_activation);
    EXPECT_EQ(harness.session.assets().revision(),
              asset_revision_after_activation);
    expect_transform_near(*harness.session.scene().transform(box.entity),
                          before);
}

TEST(ViewportInteractionControllerTests,
     PersistentPivotStartsAtMovePivotForFreshOffOriginMesh) {
    ViewportInteractionHarness harness;
    const quader::QuaderModelingObjectRef box =
        create_box_at(harness, {4.0f, -2.0f, 3.0f});
    ASSERT_TRUE(harness.editor.selection().select_object(box).ok);
    harness.session.history().clear();

    const quader::TransformComponent transform_before =
        *harness.session.scene().transform(box.entity);
    const quader::AssetId mesh_before =
        harness.session.scene().mesh(box.entity)->mesh;
    const std::uint64_t scene_revision_before =
        harness.session.scene().revision();
    const std::uint64_t asset_revision_before =
        harness.session.assets().revision();
    const std::optional<quader::SceneBounds> bounds_before =
        quader::entity_world_bounds(harness.session.scene(),
                                    harness.session.assets(), box.entity);
    ASSERT_TRUE(bounds_before.has_value());
    EXPECT_FALSE(transform_before.custom_pivot);

    ASSERT_TRUE(
        harness.editor.tools().activate(quader::ViewportToolId::Move).ok);
    harness.router.set_active_tool(quader::ViewportToolId::Move);
    (void)harness.controller.handle_before_active_tool(harness.editor,
                                                       harness.frame());
    const quader_editor_core::TransformGizmoFrame *move =
        harness.controller.transform_gizmo_frame();
    ASSERT_NE(move, nullptr);
    const quader::QVec3 move_pivot = move->info.pivot;
    EXPECT_NEAR(move_pivot.x, 4.0f, 0.001f);
    EXPECT_NEAR(move_pivot.y, -2.0f, 0.001f);
    EXPECT_NEAR(move_pivot.z, 3.0f, 0.001f);
    EXPECT_GT(quader::length(move_pivot), 0.1f);

    ASSERT_TRUE(
        harness.editor.tools().activate(quader::ViewportToolId::Pivot).ok);
    const quader::PivotHandleState pivot =
        harness.session.pivot().handle_state();
    ASSERT_TRUE(pivot.visible);
    ASSERT_TRUE(pivot.editing);
    ASSERT_TRUE(pivot.persistent);
    EXPECT_EQ(pivot.target, box.entity);
    EXPECT_NEAR(pivot.world_position.x, move_pivot.x, 0.001f);
    EXPECT_NEAR(pivot.world_position.y, move_pivot.y, 0.001f);
    EXPECT_NEAR(pivot.world_position.z, move_pivot.z, 0.001f);
    const std::optional<quader::QVec3> preview =
        harness.session.pivot().persistent_preview_position();
    ASSERT_TRUE(preview.has_value());
    EXPECT_NEAR(preview->x, move_pivot.x, 0.001f);
    EXPECT_NEAR(preview->y, move_pivot.y, 0.001f);
    EXPECT_NEAR(preview->z, move_pivot.z, 0.001f);

    const quader::QuaderApiVoidResult applied =
        harness.editor.tools().commit_active_tool_preview();
    ASSERT_TRUE(applied.ok) << applied.message;
    EXPECT_FALSE(harness.session.pivot().persistent_active());
    EXPECT_FALSE(harness.session.pivot().handle_state().visible);
    const auto settings = harness.editor.tools().active_settings();
    EXPECT_TRUE(settings.active);
    EXPECT_EQ(settings.tool_id, "move");
    EXPECT_FALSE(settings.preview_active);
    EXPECT_FALSE(settings.apply_visible);
    EXPECT_FALSE(harness.editor.history().can_undo());
    ASSERT_NE(harness.session.scene().transform(box.entity), nullptr);
    expect_transform_near(*harness.session.scene().transform(box.entity),
                          transform_before);
    EXPECT_FALSE(harness.session.scene().transform(box.entity)->custom_pivot);
    ASSERT_NE(harness.session.scene().mesh(box.entity), nullptr);
    EXPECT_EQ(harness.session.scene().mesh(box.entity)->mesh, mesh_before);
    EXPECT_EQ(harness.session.scene().revision(), scene_revision_before);
    EXPECT_EQ(harness.session.assets().revision(), asset_revision_before);
    const std::optional<quader::SceneBounds> bounds_after =
        quader::entity_world_bounds(harness.session.scene(),
                                    harness.session.assets(), box.entity);
    ASSERT_TRUE(bounds_after.has_value());
    EXPECT_NEAR(bounds_after->min.x, bounds_before->min.x, 0.001f);
    EXPECT_NEAR(bounds_after->min.y, bounds_before->min.y, 0.001f);
    EXPECT_NEAR(bounds_after->min.z, bounds_before->min.z, 0.001f);
    EXPECT_NEAR(bounds_after->max.x, bounds_before->max.x, 0.001f);
    EXPECT_NEAR(bounds_after->max.y, bounds_before->max.y, 0.001f);
    EXPECT_NEAR(bounds_after->max.z, bounds_before->max.z, 0.001f);
}

TEST(ViewportInteractionControllerTests,
     PersistentPivotBakesAndClearsTheVisibleQuickPivotOverride) {
    ViewportInteractionHarness harness;
    const quader::QuaderModelingObjectRef box =
        create_box_at(harness, {4.0f, -2.0f, 3.0f});
    ASSERT_TRUE(harness.editor.selection().select_object(box).ok);
    const std::optional<quader::SceneBounds> bounds_before =
        quader::entity_world_bounds(harness.session.scene(),
                                    harness.session.assets(), box.entity);
    ASSERT_TRUE(bounds_before.has_value());
    harness.session.history().clear();

    const quader::QVec3 quick_position{5.0f, -1.0f, 4.0f};
    harness.session.pivot().begin_quick(quick_position);
    harness.session.pivot().end_quick_edit();
    const std::optional<quader::QVec3> quick_query =
        harness.editor.query().selection_transform_pivot();
    ASSERT_TRUE(quick_query.has_value());
    EXPECT_NEAR(quick_query->x, quick_position.x, 0.001f);
    EXPECT_NEAR(quick_query->y, quick_position.y, 0.001f);
    EXPECT_NEAR(quick_query->z, quick_position.z, 0.001f);

    ASSERT_TRUE(
        harness.editor.tools().activate(quader::ViewportToolId::Pivot).ok);
    const quader::PivotHandleState handle =
        harness.session.pivot().handle_state();
    ASSERT_TRUE(handle.visible);
    EXPECT_NEAR(handle.world_position.x, quick_position.x, 0.001f);
    EXPECT_NEAR(handle.world_position.y, quick_position.y, 0.001f);
    EXPECT_NEAR(handle.world_position.z, quick_position.z, 0.001f);

    const quader::QuaderApiVoidResult applied =
        harness.editor.tools().commit_active_tool_preview();
    ASSERT_TRUE(applied.ok) << applied.message;
    ASSERT_TRUE(applied.changed);
    EXPECT_FALSE(harness.session.pivot().selection_pivot_override().valid);
    ASSERT_TRUE(harness.session.scene().transform(box.entity)->custom_pivot);
    std::optional<quader::QVec3> committed =
        harness.editor.query().selection_transform_pivot();
    ASSERT_TRUE(committed.has_value());
    EXPECT_NEAR(committed->x, quick_position.x, 0.001f);
    EXPECT_NEAR(committed->y, quick_position.y, 0.001f);
    EXPECT_NEAR(committed->z, quick_position.z, 0.001f);
    const std::optional<quader::SceneBounds> bounds_after =
        quader::entity_world_bounds(harness.session.scene(),
                                    harness.session.assets(), box.entity);
    ASSERT_TRUE(bounds_after.has_value());
    EXPECT_NEAR(bounds_after->min.x, bounds_before->min.x, 0.001f);
    EXPECT_NEAR(bounds_after->min.y, bounds_before->min.y, 0.001f);
    EXPECT_NEAR(bounds_after->min.z, bounds_before->min.z, 0.001f);
    EXPECT_NEAR(bounds_after->max.x, bounds_before->max.x, 0.001f);
    EXPECT_NEAR(bounds_after->max.y, bounds_before->max.y, 0.001f);
    EXPECT_NEAR(bounds_after->max.z, bounds_before->max.z, 0.001f);

    ASSERT_TRUE(harness.editor.history().undo().ok);
    EXPECT_FALSE(harness.session.scene().transform(box.entity)->custom_pivot);
    committed = harness.editor.query().selection_transform_pivot();
    ASSERT_TRUE(committed.has_value());
    EXPECT_NEAR(committed->x, 4.0f, 0.001f);
    EXPECT_NEAR(committed->y, -2.0f, 0.001f);
    EXPECT_NEAR(committed->z, 3.0f, 0.001f);
}

TEST(ViewportInteractionControllerTests,
     PersistentPivotApplyRefreshesTransformGizmoAndUndoRedo) {
    ViewportInteractionHarness harness;
    const quader::QuaderModelingObjectRef box = create_unit_box(harness);
    ASSERT_TRUE(harness.editor.selection().select_object(box).ok);
    ASSERT_TRUE(harness.editor.tools().activate(quader::ViewportToolId::Move).ok);
    harness.router.set_active_tool(quader::ViewportToolId::Move);

    (void)harness.controller.handle_before_active_tool(harness.editor,
                                                       harness.frame());
    const quader_editor_core::TransformGizmoFrame *gizmo =
        harness.controller.transform_gizmo_frame();
    ASSERT_NE(gizmo, nullptr);
    EXPECT_FLOAT_EQ(gizmo->info.pivot.x, 0.0f);
    EXPECT_FLOAT_EQ(gizmo->info.pivot.y, 0.0f);
    EXPECT_FLOAT_EQ(gizmo->info.pivot.z, 0.0f);

    ASSERT_TRUE(harness.editor.tools().activate(quader::ViewportToolId::Pivot).ok);
    harness.router.set_active_tool(quader::ViewportToolId::Pivot);
    harness.session.pivot().set_persistent_preview_position(
        {0.0f, 1.0f, 0.0f});
    const quader::QuaderApiVoidResult applied =
        harness.editor.tools().commit_active_tool_preview();
    ASSERT_TRUE(applied.ok) << applied.message;
    ASSERT_TRUE(applied.changed);
    EXPECT_EQ(harness.editor.tools().active_tool(), "move");

    harness.router.set_active_tool(quader::ViewportToolId::Move);
    (void)harness.controller.handle_before_active_tool(harness.editor,
                                                       harness.frame());
    gizmo = harness.controller.transform_gizmo_frame();
    ASSERT_NE(gizmo, nullptr);
    EXPECT_FLOAT_EQ(gizmo->info.pivot.x, 0.0f);
    EXPECT_FLOAT_EQ(gizmo->info.pivot.y, 1.0f);
    EXPECT_FLOAT_EQ(gizmo->info.pivot.z, 0.0f);

    ASSERT_TRUE(harness.editor.history().undo().ok);
    (void)harness.controller.handle_before_active_tool(harness.editor,
                                                       harness.frame());
    gizmo = harness.controller.transform_gizmo_frame();
    ASSERT_NE(gizmo, nullptr);
    EXPECT_FLOAT_EQ(gizmo->info.pivot.x, 0.0f);
    EXPECT_FLOAT_EQ(gizmo->info.pivot.y, 0.0f);
    EXPECT_FLOAT_EQ(gizmo->info.pivot.z, 0.0f);

    ASSERT_TRUE(harness.editor.history().redo().ok);
    (void)harness.controller.handle_before_active_tool(harness.editor,
                                                       harness.frame());
    gizmo = harness.controller.transform_gizmo_frame();
    ASSERT_NE(gizmo, nullptr);
    EXPECT_FLOAT_EQ(gizmo->info.pivot.x, 0.0f);
    EXPECT_FLOAT_EQ(gizmo->info.pivot.y, 1.0f);
    EXPECT_FLOAT_EQ(gizmo->info.pivot.z, 0.0f);
}

TEST(ViewportSelectionInteractionTests,
     FlyReleaseDoesNotPickTheReticleOrAdoptItsMeshAsAHoverCandidate) {
  struct ModeCase final {
    quader::QuaderSelectionMode api_mode;
    quader::ViewportSelectionMode viewport_mode;
    quader::QuaderActiveSelectionKind hover_kind;
    quader::QVec3 target;
  };
  const std::array cases{
      ModeCase{quader::QuaderSelectionMode::Object,
               quader::ViewportSelectionMode::Object,
               quader::QuaderActiveSelectionKind::Object, {0.0f, 0.0f, 1.0f}},
      ModeCase{quader::QuaderSelectionMode::Vertex,
               quader::ViewportSelectionMode::Vertex,
               quader::QuaderActiveSelectionKind::Vertex, {1.0f, 1.0f, 1.0f}},
      ModeCase{quader::QuaderSelectionMode::Edge,
               quader::ViewportSelectionMode::Edge,
               quader::QuaderActiveSelectionKind::Edge, {1.0f, 0.0f, 1.0f}},
      ModeCase{quader::QuaderSelectionMode::Face,
               quader::ViewportSelectionMode::Face,
               quader::QuaderActiveSelectionKind::Face, {0.0f, 0.0f, 1.0f}},
  };
  for (const auto &mode_case : cases) {
    SCOPED_TRACE(static_cast<int>(mode_case.api_mode));
    ContextMenuNavigationHarness harness;
    const auto box = create_unit_box(harness);
    ASSERT_TRUE(harness.editor.selection().set_mode(mode_case.api_mode).ok());
    ASSERT_TRUE(harness.editor.selection().clear().ok());
    ASSERT_TRUE(harness.editor.selection().clear_hover().ok());
    harness.router.set_selection_mode(mode_case.viewport_mode);
    harness.session.history().clear();
    const auto selection_before = harness.editor.selection().summary();
    quader::ViewportSelectionInteraction interaction;
    ViewportSelectionWorkGateHarness work_gate(harness);
    const auto dispatch = [&]() {
      interaction.handle_frame(
          harness.editor,
          harness.frame(false, false, false, {}, harness.input.right_pressed,
                        harness.input.right_down, harness.input.right_released,
                        harness.input.mouse_local),
          {}, {}, work_gate.hover_port());
    };
    const auto source_wire_count = [&]() {
      const auto overlay =
          quader::test_support::visible_modeling_overlay_frame(
              harness.session.modeling());
      return std::count_if(overlay.lines.begin(), overlay.lines.end(),
                           [&](const auto &line) {
        return line.layer == quader::ViewportOverlayLayer::SourceWire &&
               line.element.object_id == box.modeling_object_id;
      });
    };
    const auto expect_no_candidate = [&]() {
      EXPECT_EQ(harness.editor.selection().hover_summary().target.kind,
                quader::QuaderActiveSelectionKind::None);
      EXPECT_FALSE(harness.session.selection().resolved_hover_target().has_value());
      EXPECT_EQ(source_wire_count(), 0);
    };

    harness.input.mouse_local = {20.0f, 20.0f};
    harness.input.mouse_screen = harness.input.mouse_local;
    dispatch();
    ASSERT_EQ(interaction.work_stats().hover_pick_count, 1u);
    expect_no_candidate();

    // Captured fly input uses the center reticle, not the eventual free pointer.
    harness.input.mouse_local = {400.0f, 300.0f};
    harness.input.mouse_screen = harness.input.mouse_local;
    (void)harness.right_frame(true, true, false);
    dispatch();
    harness.input.fly_local_direction = {0.0f, 0.0f, 1.0f};
    harness.input.delta_seconds = 1.0f / 600.0f;
    (void)harness.right_frame(false, true, false);
    dispatch();
    harness.input.fly_local_direction = {};
    (void)harness.right_frame(false, false, true);
    ASSERT_TRUE(harness.input.navigation_active);
    ASSERT_FALSE(harness.input_result.navigation_active);
    ASSERT_FALSE(harness.input_result.camera_changed);
    dispatch();
    EXPECT_EQ(interaction.work_stats().hover_pick_count, 1u);
    EXPECT_EQ(interaction.work_stats().paint_pick_count, 0u);
    expect_no_candidate();

    // The first uncaptured pointer sample is elsewhere and must stay empty.
    harness.input.mouse_local = {20.0f, 20.0f};
    harness.input.mouse_screen = harness.input.mouse_local;
    (void)harness.right_frame(false, false, false);
    dispatch();
    EXPECT_EQ(interaction.work_stats().hover_pick_count, 2u);
    expect_no_candidate();
    for (int frame = 0; frame < 4; ++frame) {
      dispatch();
    }
    EXPECT_EQ(interaction.work_stats().hover_pick_count, 2u);

    const auto target = harness.camera.project_world_point(mode_case.target);
    ASSERT_TRUE(target.visible);
    harness.input.mouse_local = target.position;
    harness.input.mouse_screen = target.position;
    dispatch();
    EXPECT_EQ(interaction.work_stats().hover_pick_count, 3u);
    const auto hover = harness.editor.selection().hover_summary();
    EXPECT_EQ(hover.target.kind, mode_case.hover_kind);
    EXPECT_EQ(hover.target.object_id, box.modeling_object_id);
    ASSERT_TRUE(harness.session.selection().resolved_hover_target().has_value());
    if (mode_case.api_mode != quader::QuaderSelectionMode::Object) {
      EXPECT_GT(source_wire_count(), 0);
    }
    for (int frame = 0; frame < 4; ++frame) {
      dispatch();
    }
    EXPECT_EQ(interaction.work_stats().hover_pick_count, 3u);
    const auto selection_after = harness.editor.selection().summary();
    EXPECT_FALSE(selection_after.has_selection);
    EXPECT_EQ(selection_after.selection_revision,
              selection_before.selection_revision);
    EXPECT_EQ(selection_after.content_revision, selection_before.content_revision);
    EXPECT_FALSE(harness.editor.history().can_undo());
    EXPECT_FALSE(harness.editor.history().can_redo());
  }
}

TEST(ViewportSelectionInteractionTests,
     StationaryFallbackHoverIssuesOnePickPerWorkKey) {
  ViewportInteractionHarness harness;
  (void)create_unit_box(harness);
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Face)
                  .ok());
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Face);
  harness.input.mouse_local = {400.0f, 300.0f};
  harness.input.mouse_screen = harness.input.mouse_local;

  quader::ViewportSelectionInteraction interaction;
  ViewportSelectionWorkGateHarness work_gate(harness);
  const auto handle_frame = [&]() {
    interaction.handle_frame(
        harness.editor, harness.frame(), quader::ViewportToolResult{},
        quader::ViewportBeforeToolInteractionResult{},
        work_gate.hover_port());
  };

  handle_frame();
  EXPECT_EQ(interaction.work_stats().hover_pick_count, 1u);
  ASSERT_NE(harness.editor.selection().hover_summary().target.kind,
            quader::QuaderActiveSelectionKind::None);

  for (int frame = 0; frame < 4; ++frame) {
    handle_frame();
  }
  EXPECT_EQ(interaction.work_stats().hover_pick_count, 1u);

  harness.input.mouse_local = {410.0f, 300.0f};
  harness.input.mouse_screen = harness.input.mouse_local;
  handle_frame();
  EXPECT_EQ(interaction.work_stats().hover_pick_count, 2u);
  handle_frame();
  EXPECT_EQ(interaction.work_stats().hover_pick_count, 2u);

  harness.camera.set_view_direction({0.1f, 0.0f, -1.0f});
  handle_frame();
  EXPECT_EQ(interaction.work_stats().hover_pick_count, 3u);
  handle_frame();
  EXPECT_EQ(interaction.work_stats().hover_pick_count, 3u);

  ++work_gate.local_visibility_revision;
  handle_frame();
  EXPECT_EQ(interaction.work_stats().hover_pick_count, 4u);
  handle_frame();
  EXPECT_EQ(interaction.work_stats().hover_pick_count, 4u);
}

TEST(ViewportSelectionInteractionTests,
     StationaryHeldPaintIssuesOnePickPerWorkKeyAndStopsOnRelease) {
  ViewportInteractionHarness harness;
  (void)create_unit_box(harness);
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Face)
                  .ok());
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Face);

  quader::ViewportSelectionInteraction interaction;
  harness.input.mouse_local = {0.0f, 0.0f};
  harness.input.mouse_screen = harness.input.mouse_local;
  const quader::QuaderSelectionSummary miss =
      interaction.pick_context_menu_target(harness.editor, harness.frame());
  ASSERT_EQ(miss.selected_face_count, 0u);

  harness.input.mouse_local = {400.0f, 300.0f};
  harness.input.mouse_screen = harness.input.mouse_local;
  ViewportSelectionWorkGateHarness work_gate(harness);
  const auto handle_held_frame = [&]() {
    interaction.handle_frame(
        harness.editor, harness.frame(false, true, false),
        quader::ViewportToolResult{},
        quader::ViewportBeforeToolInteractionResult{},
        work_gate.hover_port());
  };

  handle_held_frame();
  EXPECT_EQ(interaction.work_stats().paint_pick_count, 1u);
  ASSERT_EQ(harness.editor.selection().summary().selected_face_count, 1u);

  for (int frame = 0; frame < 4; ++frame) {
    handle_held_frame();
  }
  EXPECT_EQ(interaction.work_stats().paint_pick_count, 1u);

  harness.input.mouse_local = {410.0f, 300.0f};
  harness.input.mouse_screen = harness.input.mouse_local;
  handle_held_frame();
  EXPECT_EQ(interaction.work_stats().paint_pick_count, 2u);
  handle_held_frame();
  EXPECT_EQ(interaction.work_stats().paint_pick_count, 2u);

  harness.camera.set_view_direction({0.1f, 0.0f, -1.0f});
  handle_held_frame();
  EXPECT_EQ(interaction.work_stats().paint_pick_count, 3u);
  handle_held_frame();
  EXPECT_EQ(interaction.work_stats().paint_pick_count, 3u);

  ++work_gate.local_visibility_revision;
  handle_held_frame();
  EXPECT_EQ(interaction.work_stats().paint_pick_count, 4u);
  handle_held_frame();
  EXPECT_EQ(interaction.work_stats().paint_pick_count, 4u);

  interaction.handle_frame(
      harness.editor, harness.frame(false, false, true),
      quader::ViewportToolResult{},
      quader::ViewportBeforeToolInteractionResult{}, work_gate.hover_port());
  interaction.handle_frame(
      harness.editor, harness.frame(false, true, false),
      quader::ViewportToolResult{},
      quader::ViewportBeforeToolInteractionResult{}, work_gate.hover_port());
  EXPECT_EQ(interaction.work_stats().paint_pick_count, 4u);
}

TEST(ViewportSelectionInteractionTests,
     HeldEdgePaintDoesNotAdoptMeshBodyWithoutAnEdgeHit) {
  ViewportInteractionHarness harness;
  const quader::QuaderModelingObjectRef selected =
      create_box_at(harness, {-3.0f, 0.0f, 0.0f});
  const quader::QuaderModelingObjectRef body =
      create_box_at(harness, {3.0f, 0.0f, 0.0f});
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Edge)
                  .ok());
  ASSERT_TRUE(harness.editor.selection().select_edge(selected, 1, 2).ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Edge);

  quader::ViewportSelectionInteraction interaction;
  harness.input.mouse_local = {20.0f, 20.0f};
  harness.input.mouse_screen = harness.input.mouse_local;
  const quader::QuaderSelectionSummary armed =
      interaction.pick_context_menu_target(harness.editor, harness.frame());
  ASSERT_EQ(armed.selected_object_count, 1u);
  ASSERT_EQ(armed.active_object_id, selected.modeling_object_id);
  ASSERT_EQ(armed.selected_edge_count, 1u);

  const quader::ViewportScreenPoint body_interior =
      harness.camera.project_world_point({3.0f, 0.0f, 1.0f});
  ASSERT_TRUE(body_interior.visible);
  harness.input.mouse_local = body_interior.position;
  harness.input.mouse_screen = body_interior.position;
  ViewportSelectionWorkGateHarness work_gate(harness);
  interaction.handle_frame(
      harness.editor, harness.frame(false, true, false),
      quader::ViewportToolResult{},
      quader::ViewportBeforeToolInteractionResult{}, work_gate.hover_port());

  EXPECT_EQ(interaction.work_stats().paint_pick_count, 1u);
  EXPECT_TRUE(interaction.last_selection_command_result().ok);
  const quader::QuaderSelectionSummary after =
      harness.editor.selection().summary();
  EXPECT_EQ(after.selected_object_count, 1u);
  EXPECT_EQ(after.active_object_id, selected.modeling_object_id);
  EXPECT_EQ(after.selected_edge_count, 1u);
  EXPECT_EQ(after.selection_revision, armed.selection_revision);
  EXPECT_NE(after.active_object_id, body.modeling_object_id);
}

TEST(ViewportSelectionInteractionTests,
     MarqueeReleaseAtDifferentTargetRestoresHoverImmediately) {
  ViewportInteractionHarness harness;
  const quader::QuaderModelingObjectRef left =
      create_box_at(harness, {-3.0f, 0.0f, 0.0f});
  const quader::QuaderModelingObjectRef right =
      create_box_at(harness, {3.0f, 0.0f, 0.0f});
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Face)
                  .ok());
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Face);

  const quader::ViewportScreenPoint left_screen =
      harness.camera.project_world_point({-3.0f, 0.0f, 0.0f});
  const quader::ViewportScreenPoint right_screen =
      harness.camera.project_world_point({3.0f, 0.0f, 0.0f});
  ASSERT_TRUE(left_screen.visible);
  ASSERT_TRUE(right_screen.visible);

  quader::ViewportSelectionInteraction interaction;
  ViewportSelectionWorkGateHarness work_gate(harness);
  harness.input.mouse_local = left_screen.position;
  harness.input.mouse_screen = harness.input.mouse_local;
  interaction.handle_frame(
      harness.editor, harness.frame(), quader::ViewportToolResult{},
      quader::ViewportBeforeToolInteractionResult{}, work_gate.hover_port());
  ASSERT_EQ(harness.editor.selection().hover_summary().target.object_id,
            left.modeling_object_id);
  ASSERT_EQ(interaction.work_stats().hover_pick_count, 1u);

  interaction.handle_frame(
      harness.editor,
      harness.frame(true, true, false, left_screen.position),
      quader::ViewportToolResult{},
      quader::ViewportBeforeToolInteractionResult{}, work_gate.hover_port());
  harness.input.mouse_local = right_screen.position;
  harness.input.mouse_screen = harness.input.mouse_local;
  interaction.handle_frame(
      harness.editor,
      harness.frame(false, false, true, left_screen.position),
      quader::ViewportToolResult{},
      quader::ViewportBeforeToolInteractionResult{}, work_gate.hover_port());

  EXPECT_EQ(interaction.work_stats().hover_pick_count, 2u);
  EXPECT_EQ(harness.editor.selection().hover_summary().target.object_id,
            right.modeling_object_id);
}

TEST(ViewportInteractionControllerTests,
     InwardRoomInternalRearRimUsesConfiguredEdgeRadiusWithBackfacesOff) {
  ViewportInteractionHarness harness;
  ASSERT_TRUE(harness.camera.restore_perspective_pose({
      .eye = {0.0f, 0.0f, 8.0f},
      .target = {0.0f, 0.0f, 0.0f},
      .up = {0.0f, 1.0f, 0.0f},
  }));
  const quader::QuaderModelingObjectRef room = create_unit_box(harness);
  ASSERT_TRUE(harness.editor.selection().select_object(room).ok());
  ASSERT_TRUE(
      harness.editor.commands()
          .execute(quader::quader_command::kModelingFlipFaceNormals)
          .ok());
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Edge)
                  .ok());
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Edge);
  (void)harness.settings.set_bool_value(
      quader::ViewportBoolSettingId::BackfaceSelection, false);
  ASSERT_FALSE(harness.settings.bool_value(
      quader::ViewportBoolSettingId::BackfaceSelection));

  // After flipping the unit box, edge (6, 7) is the visible internal rear top
  // rim where the back wall meets the ceiling.
  constexpr int kRearRimA = 6;
  constexpr int kRearRimB = 7;
  const quader::ViewportScreenPoint back_wall =
      harness.camera.project_world_point({0.0f, 0.0f, -1.0f});
  const quader::ViewportScreenPoint rear_rim =
      harness.camera.project_world_point({0.0f, 1.0f, -1.0f});
  ASSERT_TRUE(back_wall.visible);
  ASSERT_TRUE(rear_rim.visible);

  const auto dispatch_hover = [&](quader::QVec2 local) {
    harness.input.mouse_local = local;
    harness.input.mouse_screen = local;
    harness.dispatch_frame(harness.frame());
  };
  const auto is_rear_rim = [](int edge_a, int edge_b) {
    return (edge_a == kRearRimA && edge_b == kRearRimB) ||
           (edge_a == kRearRimB && edge_b == kRearRimA);
  };
  const auto overlay_line_count = [&](quader::ViewportOverlayLayer layer) {
    const quader::ViewportOverlayFrame overlay =
        quader::test_support::visible_modeling_overlay_frame(
            harness.session.modeling());
    return std::count_if(
        overlay.lines.begin(), overlay.lines.end(),
        [&](const quader::ViewportOverlayLine &line) {
          return line.layer == layer &&
                 line.element.object_id == room.modeling_object_id &&
                 (layer != quader::ViewportOverlayLayer::HoverEdge ||
                  is_rear_rim(line.element.edge_a, line.element.edge_b));
        });
  };
  const auto expect_rear_rim_hover = [&]() {
    const quader::QuaderHoverSummary hover =
        harness.editor.selection().hover_summary();
    ASSERT_EQ(hover.target.kind, quader::QuaderActiveSelectionKind::Edge);
    EXPECT_EQ(hover.target.object_id, room.modeling_object_id);
    EXPECT_TRUE(is_rear_rim(hover.target.edge_a, hover.target.edge_b));
    EXPECT_EQ(overlay_line_count(quader::ViewportOverlayLayer::HoverEdge), 1);
  };

  // Establish the component candidate on the exact rim, then move onto the
  // back wall. This recreates the screenshot's already-sticky room wire
  // before any non-exact edge-radius probe.
  (void)harness.settings.set_float_value(
      quader::ViewportFloatSettingId::PickEdgeRadius, 0.20f);
  dispatch_hover(rear_rim.position);
  expect_rear_rim_hover();
  EXPECT_EQ(harness.session.modeling()
                .selection_hover_summary()
                .candidate_object_id,
            room.modeling_object_id);
  EXPECT_GT(overlay_line_count(quader::ViewportOverlayLayer::SourceWire), 0);

  dispatch_hover(back_wall.position);
  EXPECT_EQ(harness.editor.selection().hover_summary().target.kind,
            quader::QuaderActiveSelectionKind::None);
  EXPECT_EQ(harness.session.modeling()
                .selection_hover_summary()
                .candidate_object_id,
            room.modeling_object_id);
  EXPECT_GT(overlay_line_count(quader::ViewportOverlayLayer::SourceWire), 0);

  quader::QVec2 six_pixels_below = rear_rim.position;
  six_pixels_below.y += 6.0f;
  quader::QVec2 six_pixels_above = rear_rim.position;
  six_pixels_above.y -= 6.0f;
  quader::QVec2 fourteen_pixels_below = rear_rim.position;
  fourteen_pixels_below.y += 14.0f;

  // Six pixels is outside a deliberately tight world-space radius at this
  // depth, but within the configured 0.20 edge radius.
  ASSERT_TRUE(harness.settings.set_float_value(
      quader::ViewportFloatSettingId::PickEdgeRadius, 0.05f));
  dispatch_hover(six_pixels_below);
  EXPECT_EQ(harness.editor.selection().hover_summary().target.kind,
            quader::QuaderActiveSelectionKind::None);
  EXPECT_EQ(overlay_line_count(quader::ViewportOverlayLayer::HoverEdge), 0);
  EXPECT_EQ(harness.session.modeling()
                .selection_hover_summary()
                .candidate_object_id,
            room.modeling_object_id);

  ASSERT_TRUE(harness.settings.set_float_value(
      quader::ViewportFloatSettingId::PickEdgeRadius, 0.20f));
  dispatch_hover(six_pixels_below);
  expect_rear_rim_hover();
  dispatch_hover(six_pixels_above);
  expect_rear_rim_hover();

  // A clearly farther probe must not accidentally turn the sticky source
  // wire into an edge hover.
  dispatch_hover(fourteen_pixels_below);
  EXPECT_EQ(harness.editor.selection().hover_summary().target.kind,
            quader::QuaderActiveSelectionKind::None);
  EXPECT_EQ(overlay_line_count(quader::ViewportOverlayLayer::HoverEdge), 0);
  EXPECT_GT(overlay_line_count(quader::ViewportOverlayLayer::SourceWire), 0);

  // Click through the same viewport interaction path used by the native
  // surface, not through a direct selection-service request.
  harness.click_selection(six_pixels_below);
  const quader::QuaderSelectionSummary selected =
      harness.editor.selection().summary();
  ASSERT_EQ(selected.selected_edge_count, 1u);
  EXPECT_EQ(selected.active_object_id, room.modeling_object_id);
  EXPECT_TRUE(is_rear_rim(static_cast<int>(selected.active_edge_a),
                          static_cast<int>(selected.active_edge_b)));

  // The same-participant allowance must remain narrow. A different rendered
  // mesh between the camera and room still occludes this otherwise-valid rim
  // radius hit for both hover and click.
  const quader::QuaderModelingObjectRef blocker =
      create_box_at(harness, {0.0f, 0.0f, 4.0f});
  ASSERT_NE(blocker.entity, room.entity);
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  dispatch_hover(six_pixels_below);
  const quader::QuaderHoverSummary occluded_hover =
      harness.editor.selection().hover_summary();
  EXPECT_FALSE(occluded_hover.target.kind ==
                       quader::QuaderActiveSelectionKind::Edge &&
               occluded_hover.target.object_id == room.modeling_object_id &&
               is_rear_rim(occluded_hover.target.edge_a,
                           occluded_hover.target.edge_b));
  EXPECT_EQ(overlay_line_count(quader::ViewportOverlayLayer::HoverEdge), 0);

  harness.click_selection(six_pixels_below);
  EXPECT_EQ(harness.editor.selection().summary().selected_edge_count, 0u);
}

TEST(ViewportInteractionControllerTests,
     SiblingInstanceOccurrenceStillOccludesActiveComponentRadiusHit) {
  ViewportInteractionHarness harness;
  ASSERT_TRUE(harness.camera.restore_perspective_pose({
      .eye = {0.0f, 0.0f, 8.0f},
      .target = {0.0f, 0.0f, 0.0f},
      .up = {0.0f, 1.0f, 0.0f},
  }));
  const quader::QuaderModelingObjectRef member = create_unit_box(harness);
  ASSERT_TRUE(harness.editor.selection().select_object(member).ok());
  const auto instance =
      harness.editor.modeling().create_instance_from_selection();
  ASSERT_TRUE(instance.ok()) << instance.error_message();
  const quader::EntityId active_occurrence =
      instance.value().occurrence.root.id;
  ASSERT_NE(active_occurrence, quader::kInvalidEntityId);

  ASSERT_TRUE(
      harness.editor.selection().select_object(active_occurrence).ok());
  const auto duplicated =
      harness.editor.modeling().duplicate_selected_entity_hierarchy();
  ASSERT_TRUE(duplicated.ok()) << duplicated.error_message();
  const std::vector<quader::EntityId> duplicate_selection =
      selected_entities(harness);
  ASSERT_EQ(duplicate_selection.size(), 1u);
  const quader::EntityId sibling_occurrence = duplicate_selection.front();
  ASSERT_NE(sibling_occurrence, active_occurrence);

  const auto active_snapshot =
      harness.editor.query().entity(active_occurrence);
  const auto sibling_snapshot =
      harness.editor.query().entity(sibling_occurrence);
  ASSERT_TRUE(active_snapshot.has_value());
  ASSERT_TRUE(sibling_snapshot.has_value());
  ASSERT_TRUE(active_snapshot->has_instance_occurrence);
  ASSERT_TRUE(sibling_snapshot->has_instance_occurrence);
  ASSERT_EQ(active_snapshot->instance_definition,
            sibling_snapshot->instance_definition);

  ASSERT_TRUE(harness.editor.scene()
                  .set_transform(active_occurrence,
                                 {.position = {0.0f, 0.0f, 0.0f}})
                  .ok());
  ASSERT_TRUE(harness.editor.scene()
                  .set_transform(sibling_occurrence,
                                 {.position = {0.0f, 0.0f, 4.0f}})
                  .ok());
  ASSERT_TRUE(
      harness.editor.selection().select_object(active_occurrence).ok());
  ASSERT_TRUE(
      harness.editor.modeling().enter_instance_edit(active_occurrence).ok());
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Edge)
                  .ok());
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Edge);
  (void)harness.settings.set_bool_value(
      quader::ViewportBoolSettingId::BackfaceSelection, false);
  EXPECT_FALSE(harness.settings.bool_value(
      quader::ViewportBoolSettingId::BackfaceSelection));
  (void)harness.settings.set_float_value(
      quader::ViewportFloatSettingId::PickEdgeRadius, 0.20f);
  EXPECT_FLOAT_EQ(harness.settings.float_value(
                      quader::ViewportFloatSettingId::PickEdgeRadius),
                  0.20f);

  const quader::ViewportScreenPoint edge_screen =
      harness.camera.project_world_point({0.0f, 1.0f, 1.0f});
  ASSERT_TRUE(edge_screen.visible);
  quader::QVec2 probe = edge_screen.position;
  probe.y += 6.0f;
  const quader::ViewportRay ray =
      harness.camera.ray_from_viewport_point(probe);

  // Instance-edit modeling sees only the active occurrence, and therefore
  // resolves the intended component before provider-neutral scene occlusion.
  const quader::QuaderHoverSummary modeling_hit =
      harness.editor.selection().preview_hover({
          .ray = {.origin = ray.origin, .direction = ray.direction},
          .edge_radius = 0.20,
          .select_backfaces = false,
      });
  ASSERT_EQ(modeling_hit.target.kind,
            quader::QuaderActiveSelectionKind::Edge);
  EXPECT_EQ(modeling_hit.target.object_id, member.modeling_object_id);

  // The render surface belongs to a different public occurrence of that same
  // definition geometry. Geometry identity alone must never bypass it.
  const quader::SceneSurfaceQuerySnapshot surfaces =
      harness.session.scene_surface_query().snapshot();
  quader::SceneSurfaceQueryScratch surface_scratch;
  const std::optional<quader::SceneSurfaceHit> surface_hit =
      surfaces.nearest_hit(
          {
              .world_origin = ray.origin,
              .world_direction = ray.direction,
              .include_backfaces = false,
              .eligibility =
                  quader::SceneSurfaceEligibility::RenderVisible,
          },
          surface_scratch);
  ASSERT_TRUE(surface_hit.has_value());
  EXPECT_EQ(surface_hit->participant.display_entity, sibling_occurrence);
  EXPECT_EQ(surface_hit->participant.geometry_entity, member.entity);

  const auto dispatch_hover = [&](quader::QVec2 local) {
    harness.input.mouse_local = local;
    harness.input.mouse_screen = local;
    harness.dispatch_frame(harness.frame());
  };
  dispatch_hover(probe);
  EXPECT_EQ(harness.editor.selection().hover_summary().target.kind,
            quader::QuaderActiveSelectionKind::None);
  harness.click_selection(probe);
  EXPECT_EQ(harness.editor.selection().summary().selected_edge_count, 0u);

  // Once the sibling is hidden, the active occurrence is the matching render
  // participant. The same non-exact radius probe must hover and select it.
  ASSERT_TRUE(harness.editor.scene()
                  .set_visible(sibling_occurrence, false)
                  .ok());
  dispatch_hover(probe);
  const quader::QuaderHoverSummary visible_hover =
      harness.editor.selection().hover_summary();
  ASSERT_EQ(visible_hover.target.kind,
            quader::QuaderActiveSelectionKind::Edge);
  EXPECT_EQ(visible_hover.target.object_id, member.modeling_object_id);
  harness.click_selection(probe);
  EXPECT_EQ(harness.editor.selection().summary().selected_edge_count, 1u);
}

TEST(ViewportInteractionControllerTests,
     ForegroundMemberInSameInstanceOccurrenceOccludesRearComponentRadiusHit) {
  ViewportInteractionHarness harness;
  ASSERT_TRUE(harness.camera.restore_perspective_pose({
      .eye = {0.0f, 0.0f, 8.0f},
      .target = {0.0f, 0.0f, 0.0f},
      .up = {0.0f, 1.0f, 0.0f},
  }));
  const quader::QuaderModelingObjectRef rear =
      create_box_at(harness, {0.0f, 0.0f, 0.0f});
  const quader::QuaderModelingObjectRef foreground =
      create_box_at(harness, {0.0f, 0.0f, 4.0f});
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({rear.entity, foreground.entity},
                                  rear.entity)
                  .ok());
  const auto instance =
      harness.editor.modeling().create_instance_from_selection();
  ASSERT_TRUE(instance.ok()) << instance.error_message();
  ASSERT_EQ(instance.value().definition.members.size(), 2u);
  const quader::EntityId occurrence = instance.value().occurrence.root.id;
  ASSERT_NE(occurrence, quader::kInvalidEntityId);

  // Keep the front member rendered while making its components ineligible.
  // The rear member then remains a valid raw modeling-radius candidate.
  ASSERT_TRUE(
      harness.editor.scene().set_locked(foreground.entity, true).ok());
  ASSERT_TRUE(harness.editor.selection().select_object(occurrence).ok());
  ASSERT_TRUE(harness.editor.modeling().enter_instance_edit(occurrence).ok());
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Edge)
                  .ok());
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Edge);
  (void)harness.settings.set_bool_value(
      quader::ViewportBoolSettingId::BackfaceSelection, false);
  EXPECT_FALSE(harness.settings.bool_value(
      quader::ViewportBoolSettingId::BackfaceSelection));
  (void)harness.settings.set_float_value(
      quader::ViewportFloatSettingId::PickEdgeRadius, 0.20f);
  EXPECT_FLOAT_EQ(harness.settings.float_value(
                      quader::ViewportFloatSettingId::PickEdgeRadius),
                  0.20f);

  const quader::ViewportScreenPoint rear_edge_screen =
      harness.camera.project_world_point({0.0f, 1.0f, 1.0f});
  ASSERT_TRUE(rear_edge_screen.visible);
  quader::QVec2 probe = rear_edge_screen.position;
  probe.y += 6.0f;
  const quader::ViewportRay ray =
      harness.camera.ray_from_viewport_point(probe);

  const quader::QuaderHoverSummary modeling_hit =
      harness.editor.selection().preview_hover({
          .ray = {.origin = ray.origin, .direction = ray.direction},
          .edge_radius = 0.20,
          .select_backfaces = false,
      });
  ASSERT_EQ(modeling_hit.target.kind,
            quader::QuaderActiveSelectionKind::Edge);
  EXPECT_EQ(modeling_hit.target.object_id, rear.modeling_object_id);

  // Both surfaces have the same public occurrence, but the closest rendered
  // geometry is the distinct foreground member. Occurrence identity alone
  // must therefore not authorize the rear component through that surface.
  const quader::SceneSurfaceQuerySnapshot surfaces =
      harness.session.scene_surface_query().snapshot();
  quader::SceneSurfaceQueryScratch surface_scratch;
  const std::optional<quader::SceneSurfaceHit> surface_hit =
      surfaces.nearest_hit(
          {
              .world_origin = ray.origin,
              .world_direction = ray.direction,
              .include_backfaces = false,
              .eligibility =
                  quader::SceneSurfaceEligibility::RenderVisible,
          },
          surface_scratch);
  ASSERT_TRUE(surface_hit.has_value());
  EXPECT_EQ(surface_hit->participant.display_entity, occurrence);
  EXPECT_EQ(surface_hit->participant.geometry_entity, foreground.entity);
  ASSERT_NE(surface_hit->participant.geometry_entity, rear.entity);

  harness.input.mouse_local = probe;
  harness.input.mouse_screen = probe;
  harness.dispatch_frame(harness.frame());
  EXPECT_EQ(harness.editor.selection().hover_summary().target.kind,
            quader::QuaderActiveSelectionKind::None);
  harness.click_selection(probe);
  EXPECT_EQ(harness.editor.selection().summary().selected_edge_count, 0u);
}

TEST(ViewportInteractionControllerTests,
     EdgeModeFaceInteriorClickKeepsMeshEditTargetUntilBackgroundClick) {
  ViewportInteractionHarness harness;
  const quader::QuaderModelingObjectRef box = create_unit_box(harness);
  ASSERT_TRUE(harness.editor.selection().select_object(box).ok());
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Edge)
                  .ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Edge);

  const quader::ViewportScreenPoint face_interior =
      harness.camera.project_world_point({0.0f, 0.0f, 1.0f});
  ASSERT_TRUE(face_interior.visible);
  const auto source_wire_count = [&]() {
    const quader::ViewportOverlayFrame overlay =
        quader::test_support::visible_modeling_overlay_frame(
            harness.session.modeling());
    return std::count_if(
        overlay.lines.begin(), overlay.lines.end(),
        [&](const quader::ViewportOverlayLine &line) {
          return line.layer == quader::ViewportOverlayLayer::SourceWire &&
                 line.element.object_id == box.modeling_object_id;
        });
  };

  const quader::QuaderSelectionSummary edit_target =
      harness.editor.selection().summary();
  ASSERT_EQ(edit_target.mode, quader::QuaderSelectionMode::Edge);
  ASSERT_EQ(edit_target.selected_object_count, 1u);
  ASSERT_EQ(edit_target.active_object_id, box.modeling_object_id);
  ASSERT_EQ(edit_target.selected_edge_count, 0u);
  const auto edit_target_wire_count = source_wire_count();
  ASSERT_GT(edit_target_wire_count, 0);

  // A surface hit is still a hit on this editable mesh even when it is too
  // far from an edge. It must not be treated as an empty-viewport miss.
  harness.click_selection(face_interior.position);
  const quader::QuaderSelectionSummary unchanged =
      harness.editor.selection().summary();
  EXPECT_EQ(unchanged.selected_object_count, 1u);
  EXPECT_EQ(unchanged.active_object_id, box.modeling_object_id);
  EXPECT_EQ(unchanged.selected_edge_count, 0u);
  EXPECT_EQ(unchanged.selection_revision, edit_target.selection_revision);
  EXPECT_EQ(source_wire_count(), edit_target_wire_count);

  // Replacing an actual edge with the face-interior surface fallback clears
  // only the component selection and keeps the mesh as the active edit target.
  ASSERT_TRUE(harness.editor.selection().select_edge(box, 1, 2).ok());
  ASSERT_EQ(harness.editor.selection().summary().selected_edge_count, 1u);
  harness.click_selection(face_interior.position);
  const quader::QuaderSelectionSummary component_cleared =
      harness.editor.selection().summary();
  EXPECT_EQ(component_cleared.selected_object_count, 1u);
  EXPECT_EQ(component_cleared.active_object_id, box.modeling_object_id);
  EXPECT_EQ(component_cleared.selected_edge_count, 0u);
  EXPECT_GT(source_wire_count(), 0);

  // A real background click retains the established replace-on-miss behavior.
  harness.click_selection({20.0f, 20.0f});
  const quader::QuaderSelectionSummary cleared =
      harness.editor.selection().summary();
  EXPECT_EQ(cleared.selected_object_count, 0u);
  EXPECT_EQ(cleared.selected_edge_count, 0u);
}

TEST(ViewportSelectionInteractionTests,
     FormalInstanceBodyDoesNotBecomeAComponentEditTargetOutsideInstanceEdit) {
  ViewportInteractionHarness harness;
  const quader::QuaderModelingObjectRef member = create_unit_box(harness);
  ASSERT_TRUE(harness.editor.selection().select_object(member).ok());
  const auto instance =
      harness.editor.modeling().create_instance_from_selection();
  ASSERT_TRUE(instance.ok()) << instance.error_message();
  const quader::EntityId occurrence = instance.value().occurrence.root.id;
  ASSERT_NE(occurrence, quader::kInvalidEntityId);

  ASSERT_TRUE(harness.editor.selection().clear().ok());
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Edge)
                  .ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Edge);
  const quader::QuaderSelectionSummary before =
      harness.editor.selection().summary();
  ASSERT_EQ(before.selected_object_count, 0u);
  ASSERT_EQ(before.selected_edge_count, 0u);

  const quader::ViewportScreenPoint body_interior =
      harness.camera.project_world_point({0.0f, 0.0f, 1.0f});
  ASSERT_TRUE(body_interior.visible);
  const quader::ViewportRay ray =
      harness.camera.ray_from_viewport_point(body_interior.position);
  const quader::SceneSurfaceQuerySnapshot surfaces =
      harness.session.scene_surface_query().snapshot();
  quader::SceneSurfaceQueryScratch surface_scratch;
  const std::optional<quader::SceneSurfaceHit> formal_surface =
      surfaces.nearest_hit(
          {
              .world_origin = ray.origin,
              .world_direction = ray.direction,
              .eligibility =
                  quader::SceneSurfaceEligibility::SelectableUnlocked,
          },
          surface_scratch);
  ASSERT_TRUE(formal_surface.has_value());
  EXPECT_EQ(formal_surface->participant.display_entity, occurrence);
  EXPECT_EQ(formal_surface->participant.geometry_entity, member.entity);
  EXPECT_NE(formal_surface->participant.display_entity,
            formal_surface->participant.geometry_entity);

  harness.input.mouse_local = body_interior.position;
  harness.input.mouse_screen = body_interior.position;
  quader::ViewportSelectionInteraction interaction;
  interaction.handle_frame(
      harness.editor,
      harness.frame(true, true, false, body_interior.position),
      quader::ViewportToolResult{},
      quader::ViewportBeforeToolInteractionResult{}, nullptr);
  interaction.handle_frame(
      harness.editor,
      harness.frame(false, false, true, body_interior.position),
      quader::ViewportToolResult{},
      quader::ViewportBeforeToolInteractionResult{}, nullptr);

  EXPECT_TRUE(interaction.last_selection_command_result().ok)
      << interaction.last_selection_command_result().message;
  const quader::QuaderSelectionSummary after =
      harness.editor.selection().summary();
  EXPECT_EQ(after.selected_object_count, 0u);
  EXPECT_EQ(after.selected_edge_count, 0u);
  EXPECT_EQ(after.selection_revision, before.selection_revision);
}

TEST(ViewportInteractionControllerTests,
     ComponentHoverWireSticksAcrossEmptySpaceAndSelectedMeshesStayAdditive) {
  /// Exercises the complete viewport-resolved hover publication path for each
  /// component mode. The lower-level polygon picker already preserves its
  /// candidate on a miss; this protects the cross-domain editor owner too.
  struct ModeCase final {
    quader::QuaderSelectionMode api_mode;
    quader::ViewportSelectionMode viewport_mode;
    quader::QuaderActiveSelectionKind hover_kind;
    quader::QVec3 left_target;
    quader::QVec3 right_target;
  };
  constexpr std::array<ModeCase, 3> kCases{
      ModeCase{quader::QuaderSelectionMode::Vertex,
               quader::ViewportSelectionMode::Vertex,
               quader::QuaderActiveSelectionKind::Vertex,
               {-2.0f, 1.0f, 1.0f}, {4.0f, 1.0f, 1.0f}},
      ModeCase{quader::QuaderSelectionMode::Edge,
               quader::ViewportSelectionMode::Edge,
               quader::QuaderActiveSelectionKind::Edge,
               {-2.0f, 0.0f, 1.0f}, {4.0f, 0.0f, 1.0f}},
      ModeCase{quader::QuaderSelectionMode::Face,
               quader::ViewportSelectionMode::Face,
               quader::QuaderActiveSelectionKind::Face,
               {-3.0f, 0.0f, 1.0f}, {3.0f, 0.0f, 1.0f}},
  };

  for (const ModeCase &mode_case : kCases) {
    SCOPED_TRACE(static_cast<int>(mode_case.api_mode));
    ViewportInteractionHarness harness;
    const quader::QuaderModelingObjectRef left =
        create_box_at(harness, {-3.0f, 0.0f, 0.0f});
    const quader::QuaderModelingObjectRef right =
        create_box_at(harness, {3.0f, 0.0f, 0.0f});
    ASSERT_TRUE(harness.editor.selection().set_mode(mode_case.api_mode).ok());
    ASSERT_TRUE(harness.editor.selection().clear().ok());
    harness.router.set_selection_mode(mode_case.viewport_mode);

    const quader::ViewportScreenPoint left_screen =
        harness.camera.project_world_point(mode_case.left_target);
    const quader::ViewportScreenPoint right_screen =
        harness.camera.project_world_point(mode_case.right_target);
    ASSERT_TRUE(left_screen.visible);
    ASSERT_TRUE(right_screen.visible);

    const auto dispatch_hover = [&](quader::QVec2 local) {
      harness.input.mouse_local = local;
      harness.input.mouse_screen = local;
      harness.dispatch_frame(harness.frame());
    };
    const auto source_wire_count = [&](int object_id) {
      const quader::ViewportOverlayFrame overlay =
          quader::test_support::visible_modeling_overlay_frame(
              harness.session.modeling());
      return std::count_if(
          overlay.lines.begin(), overlay.lines.end(),
          [&](const quader::ViewportOverlayLine &line) {
            return line.layer == quader::ViewportOverlayLayer::SourceWire &&
                   line.element.object_id == object_id;
          });
    };
    const auto select_one_left_component = [&]() {
      switch (mode_case.api_mode) {
      case quader::QuaderSelectionMode::Vertex:
        return harness.editor.selection().select_vertex(left, 1);
      case quader::QuaderSelectionMode::Edge:
        return harness.editor.selection().select_edge(left, 1, 2);
      case quader::QuaderSelectionMode::Face:
        return harness.editor.selection().select_face(left, 1);
      case quader::QuaderSelectionMode::Object:
      case quader::QuaderSelectionMode::Group:
        break;
      }
      quader::QuaderApiVoidResult unsupported;
      unsupported.message = "Unsupported component hover test mode.";
      return unsupported;
    };
    const auto selected_component_count = [&]() {
      const quader::QuaderSelectionSummary selected =
          harness.editor.selection().summary();
      switch (mode_case.api_mode) {
      case quader::QuaderSelectionMode::Vertex:
        return selected.selected_vertex_count;
      case quader::QuaderSelectionMode::Edge:
        return selected.selected_edge_count;
      case quader::QuaderSelectionMode::Face:
        return selected.selected_face_count;
      case quader::QuaderSelectionMode::Object:
      case quader::QuaderSelectionMode::Group:
        break;
      }
      return std::uint32_t{0};
    };

    dispatch_hover(left_screen.position);
    ASSERT_EQ(harness.editor.selection().hover_summary().target.object_id,
              left.modeling_object_id);
    ASSERT_EQ(harness.editor.selection().hover_summary().target.kind,
              mode_case.hover_kind);
    const auto left_wire_count = source_wire_count(left.modeling_object_id);
    ASSERT_GT(left_wire_count, 0);
    EXPECT_EQ(source_wire_count(right.modeling_object_id), 0);
    const std::uint64_t empty_selection_revision =
        harness.editor.selection().summary().selection_revision;

    // Grid/background motion clears only the immediate element hover. The
    // last mesh wire remains the component-edit candidate.
    dispatch_hover({20.0f, 20.0f});
    EXPECT_EQ(harness.editor.selection().hover_summary().target.kind,
              quader::QuaderActiveSelectionKind::None);
    EXPECT_EQ(source_wire_count(left.modeling_object_id), left_wire_count);
    EXPECT_EQ(source_wire_count(right.modeling_object_id), 0);
    EXPECT_EQ(selected_component_count(), 0u);
    EXPECT_EQ(harness.editor.selection().summary().selection_revision,
              empty_selection_revision);

    // With no selected component on the old mesh, entering another mesh
    // transfers the sole sticky candidate wire.
    dispatch_hover(right_screen.position);
    ASSERT_EQ(harness.editor.selection().hover_summary().target.object_id,
              right.modeling_object_id);
    ASSERT_EQ(harness.editor.selection().hover_summary().target.kind,
              mode_case.hover_kind);
    EXPECT_EQ(source_wire_count(left.modeling_object_id), 0);
    const auto right_wire_count = source_wire_count(right.modeling_object_id);
    ASSERT_GT(right_wire_count, 0);

    // A real component selection makes the original mesh independently
    // sticky. Hovering another mesh is additive, and a later empty hover must
    // preserve both that selection wire and the latest candidate wire.
    const quader::QuaderApiVoidResult selected = select_one_left_component();
    ASSERT_TRUE(selected.ok()) << selected.error_message();
    ASSERT_TRUE(selected.changed());
    ASSERT_EQ(selected_component_count(), 1u);
    const std::uint64_t selected_revision =
        harness.editor.selection().summary().selection_revision;

    dispatch_hover(right_screen.position);
    EXPECT_EQ(source_wire_count(left.modeling_object_id), left_wire_count);
    EXPECT_EQ(source_wire_count(right.modeling_object_id), right_wire_count);
    EXPECT_EQ(selected_component_count(), 1u);
    EXPECT_EQ(harness.editor.selection().summary().selection_revision,
              selected_revision);

    dispatch_hover({20.0f, 20.0f});
    EXPECT_EQ(harness.editor.selection().hover_summary().target.kind,
              quader::QuaderActiveSelectionKind::None);
    EXPECT_EQ(source_wire_count(left.modeling_object_id), left_wire_count);
    EXPECT_EQ(source_wire_count(right.modeling_object_id), right_wire_count);
    EXPECT_EQ(selected_component_count(), 1u);
    EXPECT_EQ(harness.editor.selection().summary().selection_revision,
              selected_revision);
  }
}

TEST(ViewportInteractionControllerTests,
     ComponentHoverWireSurvivesPointerLeavingViewportForEditorUi) {
  /// Qt surface Leave events publish viewport_hovered=false. Exercise that
  /// complete controller path for every component mode, both with an empty
  /// selection and with a selected mesh plus a different hovered candidate.
  struct ModeCase final {
    quader::QuaderSelectionMode api_mode;
    quader::ViewportSelectionMode viewport_mode;
    quader::QuaderActiveSelectionKind hover_kind;
    quader::QVec3 left_target;
    quader::QVec3 right_target;
  };
  constexpr std::array<ModeCase, 3> kCases{
      ModeCase{quader::QuaderSelectionMode::Vertex,
               quader::ViewportSelectionMode::Vertex,
               quader::QuaderActiveSelectionKind::Vertex,
               {-2.0f, 1.0f, 1.0f}, {4.0f, 1.0f, 1.0f}},
      ModeCase{quader::QuaderSelectionMode::Edge,
               quader::ViewportSelectionMode::Edge,
               quader::QuaderActiveSelectionKind::Edge,
               {-2.0f, 0.0f, 1.0f}, {4.0f, 0.0f, 1.0f}},
      ModeCase{quader::QuaderSelectionMode::Face,
               quader::ViewportSelectionMode::Face,
               quader::QuaderActiveSelectionKind::Face,
               {-3.0f, 0.0f, 1.0f}, {3.0f, 0.0f, 1.0f}},
  };

  for (const ModeCase &mode_case : kCases) {
    SCOPED_TRACE(static_cast<int>(mode_case.api_mode));
    ViewportInteractionHarness harness;
    const quader::QuaderModelingObjectRef left =
        create_box_at(harness, {-3.0f, 0.0f, 0.0f});
    const quader::QuaderModelingObjectRef right =
        create_box_at(harness, {3.0f, 0.0f, 0.0f});
    ASSERT_TRUE(harness.editor.selection().set_mode(mode_case.api_mode).ok());
    ASSERT_TRUE(harness.editor.selection().clear().ok());
    harness.router.set_selection_mode(mode_case.viewport_mode);

    const quader::ViewportScreenPoint left_screen =
        harness.camera.project_world_point(mode_case.left_target);
    const quader::ViewportScreenPoint right_screen =
        harness.camera.project_world_point(mode_case.right_target);
    ASSERT_TRUE(left_screen.visible);
    ASSERT_TRUE(right_screen.visible);

    const auto dispatch_frame = [&]() {
      harness.dispatch_frame(harness.frame());
    };
    const auto point_at = [&](quader::QVec2 local) {
      harness.input.mouse_local = local;
      harness.input.mouse_screen = local;
    };
    const auto source_wire_count = [&](int object_id) {
      const quader::ViewportOverlayFrame overlay =
          quader::test_support::visible_modeling_overlay_frame(
              harness.session.modeling());
      return std::count_if(
          overlay.lines.begin(), overlay.lines.end(),
          [&](const quader::ViewportOverlayLine &line) {
            return line.layer == quader::ViewportOverlayLayer::SourceWire &&
                   line.element.object_id == object_id;
          });
    };
    const auto select_one_left_component = [&]() {
      switch (mode_case.api_mode) {
      case quader::QuaderSelectionMode::Vertex:
        return harness.editor.selection().select_vertex(left, 1);
      case quader::QuaderSelectionMode::Edge:
        return harness.editor.selection().select_edge(left, 1, 2);
      case quader::QuaderSelectionMode::Face:
        return harness.editor.selection().select_face(left, 1);
      case quader::QuaderSelectionMode::Object:
      case quader::QuaderSelectionMode::Group:
        break;
      }
      quader::QuaderApiVoidResult unsupported;
      unsupported.message = "Unsupported component hover test mode.";
      return unsupported;
    };

    point_at(left_screen.position);
    dispatch_frame();
    ASSERT_EQ(harness.editor.selection().hover_summary().target.object_id,
              left.modeling_object_id);
    ASSERT_EQ(harness.editor.selection().hover_summary().target.kind,
              mode_case.hover_kind);
    const auto left_wire_count = source_wire_count(left.modeling_object_id);
    ASSERT_GT(left_wire_count, 0);
    EXPECT_EQ(source_wire_count(right.modeling_object_id), 0);
    const std::uint64_t empty_selection_revision =
        harness.editor.selection().summary().selection_revision;

    // Crossing from the native surface onto a toolbar, panel, or other Qt UI
    // clears the element highlight but must keep the last mesh wire sticky.
    harness.input.viewport_hovered = false;
    dispatch_frame();
    EXPECT_EQ(harness.editor.selection().hover_summary().target.kind,
              quader::QuaderActiveSelectionKind::None);
    EXPECT_EQ(source_wire_count(left.modeling_object_id), left_wire_count);
    EXPECT_EQ(source_wire_count(right.modeling_object_id), 0);
    EXPECT_FALSE(harness.editor.selection().summary().has_selection);
    EXPECT_EQ(harness.editor.selection().summary().selection_revision,
              empty_selection_revision);

    // Remaining over UI for more frames must be stable and side-effect free.
    dispatch_frame();
    EXPECT_EQ(source_wire_count(left.modeling_object_id), left_wire_count);
    EXPECT_EQ(harness.editor.selection().summary().selection_revision,
              empty_selection_revision);

    // Re-enter directly over another mesh. With no selected component, its
    // wire replaces the previously sticky candidate.
    point_at(right_screen.position);
    harness.input.viewport_hovered = true;
    dispatch_frame();
    ASSERT_EQ(harness.editor.selection().hover_summary().target.object_id,
              right.modeling_object_id);
    ASSERT_EQ(harness.editor.selection().hover_summary().target.kind,
              mode_case.hover_kind);
    EXPECT_EQ(source_wire_count(left.modeling_object_id), 0);
    const auto right_wire_count = source_wire_count(right.modeling_object_id);
    ASSERT_GT(right_wire_count, 0);

    // A selected component makes the left wire selection-owned while the
    // right wire remains hover-owned. Leaving for UI must retain both.
    const quader::QuaderApiVoidResult selected = select_one_left_component();
    ASSERT_TRUE(selected.ok()) << selected.error_message();
    ASSERT_TRUE(selected.changed());
    dispatch_frame();
    ASSERT_EQ(source_wire_count(left.modeling_object_id), left_wire_count);
    ASSERT_EQ(source_wire_count(right.modeling_object_id), right_wire_count);
    const std::uint64_t selected_revision =
        harness.editor.selection().summary().selection_revision;

    harness.input.viewport_hovered = false;
    dispatch_frame();
    EXPECT_EQ(harness.editor.selection().hover_summary().target.kind,
              quader::QuaderActiveSelectionKind::None);
    EXPECT_EQ(source_wire_count(left.modeling_object_id), left_wire_count);
    EXPECT_EQ(source_wire_count(right.modeling_object_id), right_wire_count);
    EXPECT_EQ(harness.editor.selection().summary().selection_revision,
              selected_revision);

    // A stronger lifecycle reason that starts while the pointer remains over
    // UI must supersede the spatial leave. It removes only the hover-owned
    // candidate while the real selected-component wire survives.
    harness.input.modal_tool_active = true;
    dispatch_frame();
    EXPECT_EQ(harness.editor.selection().hover_summary().target.kind,
              quader::QuaderActiveSelectionKind::None);
    EXPECT_EQ(source_wire_count(left.modeling_object_id), left_wire_count);
    EXPECT_EQ(source_wire_count(right.modeling_object_id), 0);
    EXPECT_EQ(harness.editor.selection().summary().selection_revision,
              selected_revision);
  }
}

TEST(ViewportInteractionControllerTests,
     MeshModeHoverNeverShowsAnOutlineAndClickSelectionDoes) {
  ViewportInteractionHarness harness;
  const quader::QuaderModelingObjectRef left =
      create_box_at(harness, {-3.0f, 0.0f, 0.0f});
  const quader::QuaderModelingObjectRef right =
      create_box_at(harness, {3.0f, 0.0f, 0.0f});
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Object)
                  .ok());
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);

  const quader::ViewportScreenPoint left_screen =
      harness.camera.project_world_point({-3.0f, 0.0f, 0.0f});
  const quader::ViewportScreenPoint right_screen =
      harness.camera.project_world_point({3.0f, 0.0f, 0.0f});
  ASSERT_TRUE(left_screen.visible);
  ASSERT_TRUE(right_screen.visible);

  const auto dispatch_hover = [&](quader::QVec2 local) {
    harness.input.mouse_local = local;
    harness.input.mouse_screen = local;
    harness.dispatch_frame(harness.frame());
  };
  const auto outline_count = [&](int object_id) {
    const quader::ViewportOverlayFrame overlay =
        quader::test_support::visible_modeling_overlay_frame(
            harness.session.modeling());
    return std::count_if(
        overlay.lines.begin(), overlay.lines.end(),
        [&](const quader::ViewportOverlayLine &line) {
          return (line.layer == quader::ViewportOverlayLayer::SourceWire ||
                  line.layer == quader::ViewportOverlayLayer::SelectedEdge) &&
                 line.element.object_id == object_id;
        });
  };

  dispatch_hover(left_screen.position);
  ASSERT_EQ(harness.editor.selection().hover_summary().target.object_id,
            left.modeling_object_id);
  EXPECT_EQ(outline_count(left.modeling_object_id), 0);
  EXPECT_EQ(outline_count(right.modeling_object_id), 0);
  EXPECT_EQ(harness.editor.selection().summary().selected_object_count, 0u);

  dispatch_hover(right_screen.position);
  ASSERT_EQ(harness.editor.selection().hover_summary().target.object_id,
            right.modeling_object_id);
  EXPECT_EQ(outline_count(left.modeling_object_id), 0);
  EXPECT_EQ(outline_count(right.modeling_object_id), 0);

  harness.click_selection(left_screen.position);
  ASSERT_EQ(harness.editor.selection().summary().selected_object_count, 1u);
  ASSERT_EQ(harness.editor.selection().summary().active_object_id,
            left.modeling_object_id);
  EXPECT_GT(outline_count(left.modeling_object_id), 0);
  EXPECT_EQ(outline_count(right.modeling_object_id), 0);

  dispatch_hover(right_screen.position);
  EXPECT_GT(outline_count(left.modeling_object_id), 0);
  EXPECT_EQ(outline_count(right.modeling_object_id), 0);
  EXPECT_EQ(harness.editor.selection().summary().selected_object_count, 1u);
  EXPECT_EQ(harness.editor.selection().summary().active_object_id,
            left.modeling_object_id);
}

TEST(ViewportSelectionInteractionTests,
     PrimarySelectionRestoresStationaryHoverException) {
  ViewportInteractionHarness harness;
  const quader::QuaderModelingObjectRef box = create_unit_box(harness);
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Face)
                  .ok());
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Face);
  harness.input.mouse_local = {400.0f, 300.0f};
  harness.input.mouse_screen = harness.input.mouse_local;

  quader::ViewportSelectionInteraction interaction;
  ViewportSelectionWorkGateHarness work_gate(harness);
  interaction.handle_frame(
      harness.editor, harness.frame(), quader::ViewportToolResult{},
      quader::ViewportBeforeToolInteractionResult{}, work_gate.hover_port());
  ASSERT_EQ(interaction.work_stats().hover_pick_count, 1u);
  ASSERT_EQ(harness.editor.selection().hover_summary().target.object_id,
            box.modeling_object_id);
  ASSERT_EQ(harness.editor.selection().hover_summary().target.kind,
            quader::QuaderActiveSelectionKind::Face);
  ASSERT_TRUE(harness.editor.selection()
                  .select_face(
                      box,
                      harness.editor.selection().hover_summary().target.face_id)
                  .ok());

  harness.input.modifiers.primary = true;
  interaction.handle_frame(
      harness.editor,
      harness.frame(true, true, false, harness.input.mouse_local),
      quader::ViewportToolResult{},
      quader::ViewportBeforeToolInteractionResult{}, work_gate.hover_port());
  interaction.handle_frame(
      harness.editor,
      harness.frame(false, false, true, harness.input.mouse_local),
      quader::ViewportToolResult{},
      quader::ViewportBeforeToolInteractionResult{}, work_gate.hover_port());

  EXPECT_EQ(harness.editor.selection().summary().selected_object_count, 0u);
  EXPECT_EQ(interaction.work_stats().hover_pick_count, 2u);
  EXPECT_EQ(harness.editor.selection().hover_summary().target.object_id,
            box.modeling_object_id);

  interaction.handle_frame(
      harness.editor, harness.frame(), quader::ViewportToolResult{},
      quader::ViewportBeforeToolInteractionResult{}, work_gate.hover_port());
  EXPECT_EQ(interaction.work_stats().hover_pick_count, 2u);
  EXPECT_EQ(harness.editor.selection().hover_summary().target.object_id,
            box.modeling_object_id);
}

TEST(ViewportSelectionInteractionTests,
     GizmoHoverClearsCandidateAfterSpatialMissAndRearmsStationaryPick) {
  for (int entry_path = 0; entry_path < 3; ++entry_path) {
    SCOPED_TRACE(entry_path);
    ViewportInteractionHarness harness;
    const auto candidate = create_box_at(harness, {0.0f, 0.0f, 2.0f});
    const auto box = create_box_at(harness, {0.0f, 0.0f, -2.0f});
    select_single_component(harness, box, quader::QuaderSelectionMode::Face);
    harness.router.set_selection_mode(quader::ViewportSelectionMode::Face);
    ASSERT_TRUE(harness.editor.tools().activate(quader::ViewportToolId::Move).ok());
    harness.router.set_active_tool(quader::ViewportToolId::Move);
    (void)harness.controller.handle_before_active_tool(harness.editor, harness.frame());
    const auto *gizmo = harness.controller.transform_gizmo_frame();
    ASSERT_NE(gizmo, nullptr);
    const auto handle = gizmo->info.screen_pivot;
    harness.input.mouse_local = handle;
    harness.input.mouse_screen = harness.input.mouse_local;

    quader::ViewportSelectionInteraction interaction;
    ViewportSelectionWorkGateHarness work_gate(harness);
    const auto dispatch = [&]() {
      const auto frame = harness.frame();
      const auto before = harness.controller.handle_before_active_tool(harness.editor, frame);
      interaction.handle_frame(harness.editor, frame, {}, before, work_gate.hover_port());
      return before;
    };
    const auto source_wire_count = [&](int object_id) {
      const auto overlay = quader::test_support::visible_modeling_overlay_frame(
          harness.session.modeling());
      return std::count_if(overlay.lines.begin(), overlay.lines.end(), [&](const auto &line) {
        return line.layer == quader::ViewportOverlayLayer::SourceWire &&
               line.element.object_id == object_id;
      });
    };
    harness.settings.set_bool_value(quader::ViewportBoolSettingId::ShowGizmos, false);
    EXPECT_FALSE(dispatch().gizmo_hovered);
    ASSERT_EQ(interaction.work_stats().hover_pick_count, 1u);
    ASSERT_EQ(harness.editor.selection().hover_summary().target.kind,
              quader::QuaderActiveSelectionKind::Face);
    ASSERT_EQ(harness.editor.selection().hover_summary().target.object_id,
              candidate.modeling_object_id);
    const auto selected_wire_count = source_wire_count(box.modeling_object_id);
    const auto candidate_wire_count = source_wire_count(candidate.modeling_object_id);
    ASSERT_GT(selected_wire_count, 0);
    ASSERT_GT(candidate_wire_count, 0);
    if (entry_path != 0) {
      // A prior grid miss or editor-UI leave clears the face while retaining its
      // candidate wire. Entering a gizmo must also clear this intermediate state.
      if (entry_path == 1) harness.input.mouse_local = {20.0f, 20.0f};
      else harness.input.viewport_hovered = false;
      EXPECT_FALSE(dispatch().gizmo_hovered);
      EXPECT_EQ(harness.editor.selection().hover_summary().target.kind,
                quader::QuaderActiveSelectionKind::None);
      EXPECT_EQ(source_wire_count(candidate.modeling_object_id), candidate_wire_count);
      harness.input.mouse_local = handle;
      harness.input.viewport_hovered = true;
    }
    const auto picks_before_gizmo = interaction.work_stats().hover_pick_count;
    harness.settings.set_bool_value(quader::ViewportBoolSettingId::ShowGizmos, true);
    ASSERT_TRUE(dispatch().gizmo_hovered);
    EXPECT_EQ(harness.editor.selection().hover_summary().target.kind,
              quader::QuaderActiveSelectionKind::None);
    EXPECT_EQ(source_wire_count(candidate.modeling_object_id), 0);
    EXPECT_EQ(source_wire_count(box.modeling_object_id), selected_wire_count);
    EXPECT_EQ(interaction.work_stats().hover_pick_count, picks_before_gizmo);
    const auto selection_revision = harness.editor.selection().summary().selection_revision;
    const auto preview_revision = harness.session.modeling().tool_preview_source_revision();
    for (int frame = 0; frame < 8; ++frame) {
      EXPECT_TRUE(dispatch().gizmo_hovered);
      EXPECT_EQ(interaction.work_stats().hover_pick_count, picks_before_gizmo);
      EXPECT_EQ(harness.editor.selection().summary().selection_revision, selection_revision);
      EXPECT_EQ(harness.session.modeling().tool_preview_source_revision(), preview_revision);
    }
    harness.settings.set_bool_value(quader::ViewportBoolSettingId::ShowGizmos, false);
    EXPECT_FALSE(dispatch().gizmo_hovered);
    EXPECT_EQ(interaction.work_stats().hover_pick_count, picks_before_gizmo + 1u);
    EXPECT_EQ(harness.editor.selection().hover_summary().target.kind,
              quader::QuaderActiveSelectionKind::Face);
    EXPECT_EQ(harness.editor.selection().hover_summary().target.object_id,
              candidate.modeling_object_id);
    EXPECT_EQ(source_wire_count(candidate.modeling_object_id), candidate_wire_count);
    EXPECT_EQ(source_wire_count(box.modeling_object_id), selected_wire_count);
    EXPECT_FALSE(dispatch().gizmo_hovered);
    EXPECT_EQ(interaction.work_stats().hover_pick_count, picks_before_gizmo + 1u);
    EXPECT_EQ(harness.editor.selection().summary().selection_revision, selection_revision);
  }
}

TEST(ViewportSelectionInteractionTests,
     PointerReentryAtSameCoordinatesIssuesOneFreshHoverPick) {
  ViewportInteractionHarness harness;
  const quader::QuaderModelingObjectRef box = create_unit_box(harness);
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Face)
                  .ok());
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Face);
  harness.input.mouse_local = {400.0f, 300.0f};
  harness.input.mouse_screen = harness.input.mouse_local;

  quader::ViewportSelectionInteraction interaction;
  ViewportSelectionWorkGateHarness work_gate(harness);
  const auto handle_frame = [&]() {
    interaction.handle_frame(
        harness.editor, harness.frame(), quader::ViewportToolResult{},
        quader::ViewportBeforeToolInteractionResult{},
        work_gate.hover_port());
  };

  handle_frame();
  ASSERT_EQ(interaction.work_stats().hover_pick_count, 1u);
  ASSERT_EQ(harness.editor.selection().hover_summary().target.object_id,
            box.modeling_object_id);

  harness.input.viewport_hovered = false;
  handle_frame();
  EXPECT_EQ(interaction.work_stats().hover_pick_count, 1u);
  EXPECT_EQ(harness.editor.selection().hover_summary().target.kind,
            quader::QuaderActiveSelectionKind::None);
  handle_frame();
  EXPECT_EQ(interaction.work_stats().hover_pick_count, 1u);

  harness.input.viewport_hovered = true;
  handle_frame();
  EXPECT_EQ(interaction.work_stats().hover_pick_count, 2u);
  EXPECT_EQ(harness.editor.selection().hover_summary().target.object_id,
            box.modeling_object_id);
  handle_frame();
  EXPECT_EQ(interaction.work_stats().hover_pick_count, 2u);
}

TEST(ViewportInteractionControllerTests, HoverSurvivesNavigationSuppressionAndClearsWhenPointerLeaves) {
    ViewportInteractionHarness harness;
    const quader::QuaderModelingObjectRef box = create_unit_box(harness);

    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Face)
                    .ok);
    harness.router.set_selection_mode(quader::ViewportSelectionMode::Face);
    const quader::QuaderApiResult<quader::QuaderHoverSummary> hover =
        harness.editor.selection().hover_face(box, 0);
    ASSERT_TRUE(hover.ok) << hover.message;
    ASSERT_EQ(harness.editor.selection().hover_summary().target.kind,
              quader::QuaderActiveSelectionKind::Face);

    harness.input_result.navigation_active = true;
    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(), quader::ViewportToolResult{},
        quader::ViewportBeforeToolInteractionResult{});
    EXPECT_EQ(harness.editor.selection().hover_summary().target.kind,
              quader::QuaderActiveSelectionKind::Face);

    harness.input_result.navigation_active = false;
    harness.input_result.camera_changed = true;
    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(), quader::ViewportToolResult{},
        quader::ViewportBeforeToolInteractionResult{});
    EXPECT_EQ(harness.editor.selection().hover_summary().target.kind,
              quader::QuaderActiveSelectionKind::Face);

    harness.input_result.camera_changed = false;
    harness.input.viewport_hovered = false;
    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(), quader::ViewportToolResult{},
        quader::ViewportBeforeToolInteractionResult{});
    EXPECT_EQ(harness.editor.selection().hover_summary().target.kind,
              quader::QuaderActiveSelectionKind::None);
}

TEST(ViewportInteractionControllerTests,
     HandledOwningKnifeFramePreservesToolHoverUntilPointerLeaves) {
    ViewportInteractionHarness harness;
    const quader::QuaderModelingObjectRef box = create_unit_box(harness);

    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Edge)
                    .ok());
    harness.router.set_selection_mode(quader::ViewportSelectionMode::Edge);
    harness.router.set_active_tool(quader::ViewportToolId::Knife);

    const quader::QuaderApiResult<quader::QuaderHoverSummary> hover =
        harness.editor.selection().hover_edge(box, 1, 2);
    ASSERT_TRUE(hover.ok) << hover.message;
    ASSERT_EQ(harness.editor.selection().hover_summary().target.kind,
              quader::QuaderActiveSelectionKind::Edge);

    harness.input.modal_tool_active = true;
    harness.input.active_tool_owns_viewport = true;
    const quader::ViewportToolResult tool_result{.handled = true};
    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(), tool_result,
        quader::ViewportBeforeToolInteractionResult{});
    EXPECT_EQ(harness.editor.selection().hover_summary().target.kind,
              quader::QuaderActiveSelectionKind::Edge);

    harness.input.viewport_hovered = false;
    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(), tool_result,
        quader::ViewportBeforeToolInteractionResult{});
    EXPECT_EQ(harness.editor.selection().hover_summary().target.kind,
              quader::QuaderActiveSelectionKind::None);
}

TEST(ViewportInteractionControllerTests,
     HandledOwningNonKnifeFrameClearsSelectionHover) {
    ViewportInteractionHarness harness;
    const quader::QuaderModelingObjectRef box = create_unit_box(harness);

    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Edge)
                    .ok());
    harness.router.set_selection_mode(quader::ViewportSelectionMode::Edge);
    harness.router.set_active_tool(quader::ViewportToolId::Poly);

    const quader::QuaderApiResult<quader::QuaderHoverSummary> hover =
        harness.editor.selection().hover_edge(box, 1, 2);
    ASSERT_TRUE(hover.ok) << hover.message;

    harness.input.modal_tool_active = true;
    harness.input.active_tool_owns_viewport = true;
    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(),
        quader::ViewportToolResult{.handled = true},
        quader::ViewportBeforeToolInteractionResult{});
    EXPECT_EQ(harness.editor.selection().hover_summary().target.kind,
              quader::QuaderActiveSelectionKind::None);
}

TEST(ViewportInteractionControllerTests,
     SuppressedKnifeFrameClearsHoverAndNextEligibleFrameRearmsLeaveClear) {
    ViewportInteractionHarness harness;
    (void)create_unit_box(harness);

    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Edge)
                    .ok());
    harness.router.set_selection_mode(quader::ViewportSelectionMode::Edge);
    harness.router.set_active_tool(quader::ViewportToolId::Knife);
    harness.input.modal_tool_active = true;
    harness.input.active_tool_owns_viewport = true;
    const quader::ViewportToolResult tool_result{.handled = true};
    quader::ModelingSessionService &modeling = harness.session.modeling();
    ASSERT_TRUE(modeling
                    .activate_viewport_tool(quader::ModelingViewportTool::Knife)
                    .ok);
    int hover_invalidations = 0;
    quader::ViewportModelingToolPortCallbacks callbacks;
    callbacks.invalidate_hover = [&]() { ++hover_invalidations; };
    quader::ViewportModelingToolPortAdapter tool_ports(modeling, callbacks);
    const quader::ModelingToolRayEvent event =
        knife_event_at(harness, {0.0f, -1.0f, 1.0f});
    const auto has_knife_handle = [&]() {
      const quader::ViewportOverlayFrame overlay =
          quader::test_support::visible_modeling_overlay_frame(modeling);
      return std::any_of(
          overlay.points.begin(), overlay.points.end(),
          [](const quader::ViewportOverlayPoint &point) {
            return point.layer == quader::ViewportOverlayLayer::ToolHandle;
          });
    };
    const auto author_knife_hover = [&]() {
      const quader::ModelingCommandResult update =
          modeling.update_knife_hover(event);
      ASSERT_TRUE(update.ok) << update.message;
      ASSERT_TRUE(modeling.tool_status().knife_has_current);
      ASSERT_TRUE(has_knife_handle());
      harness.controller.handle_after_active_tool(
          harness.editor, harness.frame(), tool_result,
          quader::ViewportBeforeToolInteractionResult{}, &tool_ports);
      EXPECT_TRUE(modeling.tool_status().knife_has_current);
    };

    author_knife_hover();
    harness.input_result.navigation_active = true;
    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(), tool_result,
        quader::ViewportBeforeToolInteractionResult{}, &tool_ports);
    EXPECT_EQ(harness.editor.selection().hover_summary().target.kind,
              quader::QuaderActiveSelectionKind::None);
    EXPECT_FALSE(modeling.tool_status().knife_has_current);
    EXPECT_FALSE(has_knife_handle());
    EXPECT_EQ(hover_invalidations, 1);

    harness.input_result.navigation_active = false;
    author_knife_hover();
    EXPECT_EQ(harness.editor.selection().hover_summary().target.kind,
              quader::QuaderActiveSelectionKind::Edge);

    harness.input.viewport_hovered = false;
    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(), tool_result,
        quader::ViewportBeforeToolInteractionResult{}, &tool_ports);
    EXPECT_EQ(harness.editor.selection().hover_summary().target.kind,
              quader::QuaderActiveSelectionKind::None);
    EXPECT_FALSE(modeling.tool_status().knife_has_current);
    EXPECT_FALSE(has_knife_handle());
    EXPECT_EQ(hover_invalidations, 2);

    harness.input.viewport_hovered = true;
    author_knife_hover();
    harness.input.text_input_active = true;
    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(), tool_result,
        quader::ViewportBeforeToolInteractionResult{}, &tool_ports);
    EXPECT_FALSE(modeling.tool_status().knife_has_current);
    EXPECT_FALSE(has_knife_handle());
    EXPECT_EQ(hover_invalidations, 3);
}

TEST(ViewportInteractionControllerTests, MoveRotateAndScaleGizmosDragSelectedObjectThroughViewportFrames) {
    /// Names one transform tool case covered by the shared gizmo drag assertions.
    struct ToolCase {
        quader::ViewportToolId tool = quader::ViewportToolId::Move;
        const char* label = "";
    };

    for (const ToolCase tool_case : {
             ToolCase{quader::ViewportToolId::Move, "move"},
             ToolCase{quader::ViewportToolId::Rotate, "rotate"},
             ToolCase{quader::ViewportToolId::Scale, "scale"},
         }) {
      SCOPED_TRACE(tool_case.label);
      ViewportInteractionHarness harness;
      const quader::QuaderModelingObjectRef box = create_unit_box(harness);
      ASSERT_TRUE(harness.editor.selection().select_object(box).ok);
      ASSERT_TRUE(harness.editor.tools().activate(tool_case.tool).ok);
      harness.router.set_active_tool(tool_case.tool);

      const quader::ViewportBeforeToolInteractionResult warmup =
          harness.controller.handle_before_active_tool(harness.editor,
                                                       harness.frame());
      EXPECT_FALSE(warmup.gizmo_consumed);
      const quader_editor_core::TransformGizmoFrame *frame =
          harness.controller.transform_gizmo_frame();
      ASSERT_NE(frame, nullptr);

      quader::ViewportOverlayFrame overlay;
      quader::ViewportToolOverlayProjection::append_transform_gizmo(
          overlay,
          quader::ViewportCameraAdapter::overlay_camera(
              harness.camera, harness.input.viewport_size),
          *frame);
      EXPECT_FALSE(overlay.lines.empty());

      quader::QVec2 start{};
      quader::QVec2 update{};
      if (tool_case.tool == quader::ViewportToolId::Rotate) {
        const quader_editor_core::TransformGizmoRingSegmentPrimitive *ring =
            find_ring(*frame, quader_editor_core::TransformAxis::Z);
        ASSERT_NE(ring, nullptr);
        start = ring->screen_start;
        update = rotated_point(frame->info.screen_pivot, start, 0.6f);
      } else if (tool_case.tool == quader::ViewportToolId::Scale) {
        start = frame->info.screen_pivot;
        update = start + quader::QVec2{120.0f, 0.0f};
      } else {
        const quader_editor_core::TransformGizmoAxisPrimitive *axis =
            find_axis(*frame, quader_editor_core::TransformAxis::X);
        ASSERT_NE(axis, nullptr);
        start = axis->screen_tip;
        update = start + quader::QVec2{120.0f, 0.0f};
      }

      const quader::TransformComponent before_transform =
          *harness.session.scene().transform(box.entity);
      press_update_release_gizmo(harness, start, update);
      const quader::TransformComponent after_transform =
          *harness.session.scene().transform(box.entity);

      if (tool_case.tool == quader::ViewportToolId::Move) {
        EXPECT_GT(length(after_transform.position - before_transform.position),
                  0.001f);
      } else if (tool_case.tool == quader::ViewportToolId::Rotate) {
        EXPECT_GT(length(after_transform.rotation_euler -
                         before_transform.rotation_euler),
                  0.001f);
      } else {
        EXPECT_GT(length(after_transform.scale - before_transform.scale),
                  0.001f);
      }
      EXPECT_EQ(harness.editor.selection().summary().selected_object_count, 1u);

      ASSERT_TRUE(harness.editor.history().can_undo());
      const quader::QuaderApiVoidResult undo =
          harness.editor.commands().execute(
              quader::quader_command::kEditUndo);
      ASSERT_TRUE(undo.ok()) << undo.error_message();
      EXPECT_TRUE(undo.changed());
      ASSERT_NE(harness.session.scene().transform(box.entity), nullptr);
      expect_transform_near(*harness.session.scene().transform(box.entity),
                            before_transform);
    }
}

TEST(ViewportInteractionControllerTests,
     MoveGizmoCenterDragsObjectsAndComponentsFreelyInTheViewPlane) {
  struct SelectionCase {
    quader::QuaderSelectionMode mode = quader::QuaderSelectionMode::Object;
    const char *label = "";
  };

  for (const SelectionCase selection_case : {
           SelectionCase{quader::QuaderSelectionMode::Object, "object"},
           SelectionCase{quader::QuaderSelectionMode::Vertex, "vertex"},
           SelectionCase{quader::QuaderSelectionMode::Edge, "edge"},
           SelectionCase{quader::QuaderSelectionMode::Face, "face"},
       }) {
    SCOPED_TRACE(selection_case.label);
    ViewportInteractionHarness harness;
    ASSERT_TRUE(harness.settings.set_bool_value(
        quader::ViewportBoolSettingId::GridSnap, false));
    ASSERT_TRUE(harness.settings.set_bool_value(
        quader::ViewportBoolSettingId::VertexSnap, false));
    const quader::QuaderModelingObjectRef box = create_unit_box(harness);
    if (selection_case.mode == quader::QuaderSelectionMode::Object) {
      ASSERT_TRUE(harness.editor.selection().select_object(box).ok());
    } else {
      select_single_component(harness, box, selection_case.mode);
    }
    ASSERT_TRUE(
        harness.editor.tools().activate(quader::ViewportToolId::Move).ok());
    harness.router.set_active_tool(quader::ViewportToolId::Move);

    (void)harness.controller.handle_before_active_tool(harness.editor,
                                                       harness.frame());
    const quader_editor_core::TransformGizmoFrame *frame =
        harness.controller.transform_gizmo_frame();
    ASSERT_NE(frame, nullptr);
    const quader::QVec2 start = frame->info.screen_pivot;
    harness.input.mouse_local = start;
    harness.input.mouse_screen = start;
    (void)harness.controller.handle_before_active_tool(harness.editor,
                                                       harness.frame());
    frame = harness.controller.transform_gizmo_frame();
    ASSERT_NE(frame, nullptr);
    EXPECT_EQ(frame->info.hover_axis,
              quader_editor_core::TransformAxis::All);

    const std::optional<quader::QVec3> before_center =
        harness.editor.query().selection_center();
    ASSERT_TRUE(before_center.has_value());
    const quader::TransformComponent before_transform =
        *harness.session.scene().transform(box.entity);
    const quader::QuaderSelectionSummary before_selection =
        harness.editor.selection().summary();
    harness.session.history().clear();

    const quader::QVec2 update = start + quader::QVec2{72.0f, -48.0f};
    const quader::ViewportBeforeToolInteractionResult press =
        harness.controller.handle_before_active_tool(
            harness.editor, harness.frame(true, true, false, start));
    ASSERT_TRUE(press.gizmo_consumed);
    harness.input.mouse_local = update;
    harness.input.mouse_screen = update;
    const quader::ViewportBeforeToolInteractionResult drag =
        harness.controller.handle_before_active_tool(
            harness.editor, harness.frame(false, true, false, start));
    ASSERT_TRUE(drag.gizmo_consumed);
    ASSERT_TRUE(harness.controller.dragging());
    frame = harness.controller.transform_gizmo_frame();
    ASSERT_NE(frame, nullptr);
    EXPECT_EQ(frame->info.active_axis,
              quader_editor_core::TransformAxis::All);

    const quader::ViewportBeforeToolInteractionResult release =
        harness.controller.handle_before_active_tool(
            harness.editor, harness.frame(false, false, true, start));
    ASSERT_TRUE(release.gizmo_consumed);
    EXPECT_FALSE(harness.controller.dragging());

    const std::optional<quader::QVec3> after_center =
        harness.editor.query().selection_center();
    ASSERT_TRUE(after_center.has_value());
    const quader::QVec3 delta = *after_center - *before_center;
    const quader::ViewportCameraBasis camera_basis =
        quader::ViewportCameraAdapter::basis(harness.camera);
    EXPECT_GT(std::abs(dot(delta, camera_basis.right)), 0.01f);
    EXPECT_GT(std::abs(dot(delta, camera_basis.up)), 0.01f);
    EXPECT_NEAR(dot(delta, camera_basis.forward), 0.0f, 0.001f);

    const quader::TransformComponent after_transform =
        *harness.session.scene().transform(box.entity);
    if (selection_case.mode == quader::QuaderSelectionMode::Object) {
      EXPECT_GT(length(after_transform.position - before_transform.position),
                0.01f);
    } else {
      expect_transform_near(after_transform, before_transform);
    }
    const quader::QuaderSelectionSummary after_selection =
        harness.editor.selection().summary();
    EXPECT_EQ(after_selection.mode, before_selection.mode);
    EXPECT_EQ(after_selection.selected_object_count,
              before_selection.selected_object_count);
    EXPECT_EQ(after_selection.selected_vertex_count,
              before_selection.selected_vertex_count);
    EXPECT_EQ(after_selection.selected_edge_count,
              before_selection.selected_edge_count);
    EXPECT_EQ(after_selection.selected_face_count,
              before_selection.selected_face_count);

    ASSERT_TRUE(harness.editor.history().undo().ok());
    const std::optional<quader::QVec3> undone_center =
        harness.editor.query().selection_center();
    ASSERT_TRUE(undone_center.has_value());
    expect_vec3_near(*undone_center, *before_center);
    expect_transform_near(*harness.session.scene().transform(box.entity),
                          before_transform);

    ASSERT_TRUE(harness.editor.history().redo().ok());
    const std::optional<quader::QVec3> redone_center =
        harness.editor.query().selection_center();
    ASSERT_TRUE(redone_center.has_value());
    expect_vec3_near(*redone_center, *after_center);
    expect_transform_near(*harness.session.scene().transform(box.entity),
                          after_transform);
  }
}

TEST(ViewportInteractionControllerTests,
     UniformScaleGizmoShrinksFreshGridSizedMeshBelowCreationSize) {
  ViewportInteractionHarness harness;
  const quader::QuaderApiResult<quader::QuaderModelingObjectRef> created =
      harness.editor.modeling().create_box(
          quader::QuaderBounds{
              .min = {-0.5f, -0.5f, -0.5f},
              .max = {0.5f, 0.5f, 0.5f},
          },
          "Grid Sized Scale Regression Box");
  ASSERT_TRUE(created.ok()) << created.error_message();
  const quader::QuaderModelingObjectRef box = created.value();
  ASSERT_TRUE(harness.editor.selection().select_object(box).ok());
  ASSERT_TRUE(
      harness.editor.tools().activate(quader::ViewportToolId::Scale).ok());
  harness.router.set_active_tool(quader::ViewportToolId::Scale);

  (void)harness.controller.handle_before_active_tool(harness.editor,
                                                     harness.frame());
  const quader_editor_core::TransformGizmoFrame *frame =
      harness.controller.transform_gizmo_frame();
  ASSERT_NE(frame, nullptr);

  const quader::TransformComponent before =
      *harness.session.scene().transform(box.entity);
  const quader::QVec2 center_handle = frame->info.screen_pivot;
  press_update_release_gizmo(harness, center_handle,
                             center_handle + quader::QVec2{-48.0f, 0.0f});

  const quader::TransformComponent after =
      *harness.session.scene().transform(box.entity);
  EXPECT_LT(after.scale.x, before.scale.x);
  EXPECT_LT(after.scale.y, before.scale.y);
  EXPECT_LT(after.scale.z, before.scale.z);
  EXPECT_NEAR(after.scale.x, after.scale.y, 0.001f);
  EXPECT_NEAR(after.scale.y, after.scale.z, 0.001f);
}

TEST(ViewportInteractionControllerTests,
     GlobalAndLocalMoveGizmosUseWorldSingleMeshAndActiveMeshAxes) {
  ViewportInteractionHarness harness;
  ASSERT_TRUE(harness.settings.set_bool_value(
      quader::ViewportBoolSettingId::GridSnap, false));
  ASSERT_TRUE(harness.settings.set_bool_value(
      quader::ViewportBoolSettingId::VertexSnap, false));

  const quader::QuaderModelingObjectRef active =
      create_box_at(harness, {-2.0f, 0.0f, 0.0f});
  const quader::QuaderModelingObjectRef other =
      create_box_at(harness, {3.0f, 0.0f, 0.0f});
  quader::TransformComponent active_transform;
  active_transform.position = {-2.0f, 0.0f, 0.0f};
  active_transform.rotation_euler = {0.0f, 0.0f, 1.57079632679f};
  quader::TransformComponent other_transform;
  other_transform.position = {3.0f, 0.0f, 0.0f};
  other_transform.rotation_euler = {0.25f, -0.4f, -0.65f};
  ASSERT_TRUE(
      harness.session.scene().set_transform(active.entity, active_transform));
  ASSERT_TRUE(
      harness.session.scene().set_transform(other.entity, other_transform));
  ASSERT_TRUE(harness.editor.selection().select_object(active).ok());
  ASSERT_TRUE(
      harness.editor.tools().activate(quader::ViewportToolId::Move).ok());
  harness.router.set_active_tool(quader::ViewportToolId::Move);

  ASSERT_EQ(harness.editor.viewports().transform_space(),
            quader::QuaderTransformSpace::Global);
  (void)harness.controller.handle_before_active_tool(harness.editor,
                                                     harness.frame());
  const quader_editor_core::TransformGizmoFrame *frame =
      harness.controller.transform_gizmo_frame();
  ASSERT_NE(frame, nullptr);
  const quader_editor_core::TransformGizmoAxisPrimitive *x_axis =
      find_axis(*frame, quader_editor_core::TransformAxis::X);
  ASSERT_NE(x_axis, nullptr);
  expect_vec3_near(gizmo_axis_world_direction(*x_axis),
                   {1.0f, 0.0f, 0.0f});

  const quader::QuaderApiVoidResult local_space =
      harness.editor.viewports().set_transform_space(
          quader::QuaderTransformSpace::Local);
  ASSERT_TRUE(local_space.ok()) << local_space.error_message();
  ASSERT_TRUE(local_space.changed());
  const std::optional<quader::QuaderTransform3> active_orientation =
      harness.editor.query().selection_transform_orientation();
  ASSERT_TRUE(active_orientation.has_value());

  (void)harness.controller.handle_before_active_tool(harness.editor,
                                                     harness.frame());
  frame = harness.controller.transform_gizmo_frame();
  ASSERT_NE(frame, nullptr);
  x_axis = find_axis(*frame, quader_editor_core::TransformAxis::X);
  ASSERT_NE(x_axis, nullptr);
  expect_vec3_near(gizmo_axis_world_direction(*x_axis),
                   active_orientation->x_axis);
  EXPECT_GT(std::abs(active_orientation->x_axis.y), 0.999f);
  EXPECT_NEAR(active_orientation->x_axis.x, 0.0f, 0.001f);

  const auto drag_along_axis = [&](const auto &axis) {
    const quader::QVec2 projected = axis.screen_tip - axis.screen_start;
    const float projected_length =
        std::sqrt(projected.x * projected.x + projected.y * projected.y);
    ASSERT_GT(projected_length, 1.0f);
    const quader::QVec2 screen_direction = projected * (1.0f / projected_length);
    press_update_release_gizmo(harness, axis.screen_tip,
                               axis.screen_tip + screen_direction * 120.0f);
  };

  const quader::TransformComponent single_before =
      *harness.session.scene().transform(active.entity);
  drag_along_axis(*x_axis);
  const quader::TransformComponent single_after =
      *harness.session.scene().transform(active.entity);
  const quader::QVec3 single_delta =
      single_after.position - single_before.position;
  ASSERT_GT(quader::length(single_delta), 0.01f);
  EXPECT_GT(quader::dot(quader::normalize(single_delta),
                        active_orientation->x_axis),
            0.999f);
  ASSERT_TRUE(harness.editor.history().undo().ok());
  expect_transform_near(*harness.session.scene().transform(active.entity),
                        single_before);

  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({other.entity, active.entity}, active.entity)
                  .ok());
  ASSERT_EQ(harness.session.scene().primary_selection(), active.entity);
  const std::vector<quader::EntityId> selected_before =
      harness.session.scene().selected_entities();
  const quader::TransformComponent active_multi_before =
      *harness.session.scene().transform(active.entity);
  const quader::TransformComponent other_multi_before =
      *harness.session.scene().transform(other.entity);

  (void)harness.controller.handle_before_active_tool(harness.editor,
                                                     harness.frame());
  frame = harness.controller.transform_gizmo_frame();
  ASSERT_NE(frame, nullptr);
  x_axis = find_axis(*frame, quader_editor_core::TransformAxis::X);
  ASSERT_NE(x_axis, nullptr);
  expect_vec3_near(gizmo_axis_world_direction(*x_axis),
                   active_orientation->x_axis);
  drag_along_axis(*x_axis);

  const quader::TransformComponent active_multi_after =
      *harness.session.scene().transform(active.entity);
  const quader::TransformComponent other_multi_after =
      *harness.session.scene().transform(other.entity);
  const quader::QVec3 active_delta =
      active_multi_after.position - active_multi_before.position;
  const quader::QVec3 other_delta =
      other_multi_after.position - other_multi_before.position;
  ASSERT_GT(quader::length(active_delta), 0.01f);
  expect_vec3_near(other_delta, active_delta);
  EXPECT_GT(quader::dot(quader::normalize(active_delta),
                        active_orientation->x_axis),
            0.999f);
  EXPECT_EQ(harness.session.scene().selected_entities(), selected_before);
  EXPECT_EQ(harness.session.scene().primary_selection(), active.entity);
  EXPECT_EQ(harness.editor.selection().summary().active_object_id,
            active.modeling_object_id);

  ASSERT_TRUE(harness.editor.history().undo().ok());
  expect_transform_near(*harness.session.scene().transform(active.entity),
                        active_multi_before);
  expect_transform_near(*harness.session.scene().transform(other.entity),
                        other_multi_before);
  EXPECT_EQ(harness.session.scene().selected_entities(), selected_before);
  EXPECT_EQ(harness.session.scene().primary_selection(), active.entity);

  ASSERT_TRUE(harness.editor.history().redo().ok());
  expect_transform_near(*harness.session.scene().transform(active.entity),
                        active_multi_after);
  expect_transform_near(*harness.session.scene().transform(other.entity),
                        other_multi_after);
  EXPECT_EQ(harness.session.scene().selected_entities(), selected_before);
  EXPECT_EQ(harness.session.scene().primary_selection(), active.entity);
}

TEST(ViewportInteractionControllerTests,
     LocalMeshGizmoAxesFollowExactRenderedDirectionsThroughScaledHierarchy) {
  ViewportInteractionHarness harness;
  const quader::QuaderModelingObjectRef box = create_unit_box(harness);
  const quader::QuaderApiResult<quader::QuaderEntityRef> parent =
      harness.editor.scene().create_entity({
          .name = "Nonuniform Local Transform Parent",
          .kind = quader::QuaderEntityKind::Generic,
          .transform = {
              .rotation_euler = {0.31f, -0.27f, 0.42f},
              .scale = {2.75f, 0.65f, 1.8f},
          },
      });
  ASSERT_TRUE(parent.ok()) << parent.error_message();
  ASSERT_TRUE(
      harness.editor.scene().reparent(box.entity, parent.value().id, false).ok());
  ASSERT_TRUE(harness.editor.scene()
                  .set_transform(box.entity,
                                 {
                                     .rotation_euler = {0.18f, -0.23f, 0.71f},
                                     .scale = {1.4f, 0.8f, 2.1f},
                                 })
                  .ok());
  ASSERT_TRUE(harness.editor.selection().select_object(box).ok());
  ASSERT_TRUE(harness.editor.viewports()
                  .set_transform_space(quader::QuaderTransformSpace::Local)
                  .ok());
  ASSERT_TRUE(
      harness.editor.tools().activate(quader::ViewportToolId::Move).ok());
  harness.router.set_active_tool(quader::ViewportToolId::Move);

  const quader::QMat4 rendered_world =
      harness.session.scene().world_transform_matrix(box.entity);
  const std::array expected_directions{
      std::pair{quader_editor_core::TransformAxis::X,
                quader::normalize_or_zero(quader::transform_vector(
                    rendered_world, {1.0f, 0.0f, 0.0f}))},
      std::pair{quader_editor_core::TransformAxis::Y,
                quader::normalize_or_zero(quader::transform_vector(
                    rendered_world, {0.0f, 1.0f, 0.0f}))},
      std::pair{quader_editor_core::TransformAxis::Z,
                quader::normalize_or_zero(quader::transform_vector(
                    rendered_world, {0.0f, 0.0f, 1.0f}))},
  };
  EXPECT_GT(std::abs(quader::dot(expected_directions[0].second,
                                 expected_directions[1].second)),
            0.1f);

  (void)harness.controller.handle_before_active_tool(harness.editor,
                                                     harness.frame());
  const quader_editor_core::TransformGizmoFrame *frame =
      harness.controller.transform_gizmo_frame();
  ASSERT_NE(frame, nullptr);
  for (const auto &[axis, expected] : expected_directions) {
    const quader_editor_core::TransformGizmoAxisPrimitive *primitive =
        find_axis(*frame, axis);
    ASSERT_NE(primitive, nullptr);
    expect_vec3_near(gizmo_axis_world_direction(*primitive), expected);
  }

  ASSERT_TRUE(
      harness.editor.tools().activate(quader::ViewportToolId::Rotate).ok());
  harness.router.set_active_tool(quader::ViewportToolId::Rotate);
  (void)harness.controller.handle_before_active_tool(harness.editor,
                                                     harness.frame());
  frame = harness.controller.transform_gizmo_frame();
  ASSERT_NE(frame, nullptr);
  for (const auto &[axis, expected] : expected_directions) {
    const quader_editor_core::TransformGizmoRingSegmentPrimitive *ring =
        find_ring(*frame, axis);
    ASSERT_NE(ring, nullptr);
    const quader::QVec3 ring_radius =
        quader::normalize_or_zero(ring->world_start - frame->info.pivot);
    EXPECT_NEAR(quader::dot(ring_radius, expected), 0.0f, 0.001f);
  }
}

TEST(ViewportInteractionControllerTests,
     LocalMeshGizmoUsesEditedInstanceOccurrenceDisplayDirections) {
  constexpr float kHalfPi = 1.57079632679f;
  ViewportInteractionHarness harness;
  const quader::QuaderModelingObjectRef member = create_unit_box(harness);
  const quader::QuaderApiResult<quader::QuaderInstanceCreateResult> instance =
      harness.editor.modeling().create_instance_from_selection();
  ASSERT_TRUE(instance.ok()) << instance.error_message();
  const quader::EntityId occurrence = instance.value().occurrence.root.id;
  const quader::EntityId definition = instance.value().definition.root.id;
  ASSERT_NE(occurrence, quader::kInvalidEntityId);
  ASSERT_NE(definition, quader::kInvalidEntityId);

  quader::TransformComponent definition_transform;
  definition_transform.rotation_euler = {0.0f, 0.0f, kHalfPi};
  definition_transform.scale = {2.0f, -3.0f, 0.5f};
  quader::TransformComponent occurrence_transform;
  occurrence_transform.rotation_euler = {0.0f, kHalfPi, 0.0f};
  occurrence_transform.scale = {-1.5f, 4.0f, 2.0f};
  quader::TransformComponent member_transform;
  member_transform.rotation_euler = {kHalfPi, 0.0f, 0.0f};
  member_transform.scale = {3.0f, 0.75f, -2.0f};
  ASSERT_TRUE(harness.session.scene().set_transform(definition,
                                                     definition_transform));
  ASSERT_TRUE(harness.session.scene().set_transform(occurrence,
                                                     occurrence_transform));
  ASSERT_TRUE(
      harness.session.scene().set_transform(member.entity, member_transform));
  ASSERT_TRUE(harness.editor.modeling().enter_instance_edit(occurrence).ok());
  ASSERT_TRUE(harness.editor.selection().select_object(member).ok());
  ASSERT_TRUE(harness.editor.viewports()
                  .set_transform_space(quader::QuaderTransformSpace::Local)
                  .ok());
  ASSERT_TRUE(
      harness.editor.tools().activate(quader::ViewportToolId::Move).ok());
  harness.router.set_active_tool(quader::ViewportToolId::Move);

  const std::optional<quader::QuaderTransform3> orientation =
      harness.editor.query().selection_transform_orientation();
  ASSERT_TRUE(orientation.has_value());
  const std::optional<quader::QMat4> inverse_definition =
      quader::try_scene_matrix_inverse(
          harness.session.scene().world_transform_matrix(definition));
  ASSERT_TRUE(inverse_definition.has_value());
  const quader::QMat4 display_world = quader::scene_transform_multiply(
      harness.session.scene().world_transform_matrix(occurrence),
      quader::scene_transform_multiply(
          *inverse_definition,
          harness.session.scene().world_transform_matrix(member.entity)));
  const quader::QVec3 expected_y = quader::normalize_or_zero(
      quader::transform_vector(display_world, {0.0f, 1.0f, 0.0f}));
  expect_vec3_near(orientation->y_axis, expected_y);

  (void)harness.controller.handle_before_active_tool(harness.editor,
                                                     harness.frame());
  const quader_editor_core::TransformGizmoFrame *frame =
      harness.controller.transform_gizmo_frame();
  ASSERT_NE(frame, nullptr);
  const quader_editor_core::TransformGizmoAxisPrimitive *y_axis =
      find_axis(*frame, quader_editor_core::TransformAxis::Y);
  ASSERT_NE(y_axis, nullptr);
  expect_vec3_near(gizmo_axis_world_direction(*y_axis), expected_y);
}

TEST(ViewportInteractionControllerTests,
     RepeatLocalMoveUsesTrueInverseForNonorthogonalRecordedBasis) {
  constexpr float kHalfPi = 1.57079632679f;
  ViewportInteractionHarness harness;
  const quader::QuaderModelingObjectRef recorded = create_unit_box(harness);
  const quader::QuaderModelingObjectRef replayed = create_unit_box(harness);
  const quader::QuaderApiResult<quader::QuaderEntityRef> recorded_parent =
      harness.editor.scene().create_entity({
          .name = "Repeat Nonorthogonal Parent",
          .kind = quader::QuaderEntityKind::Generic,
          .transform = {
              .rotation_euler = {0.0f, 0.0f, 0.25f},
              .scale = {3.0f, 0.5f, 1.0f},
          },
      });
  ASSERT_TRUE(recorded_parent.ok()) << recorded_parent.error_message();
  ASSERT_TRUE(harness.editor.scene()
                  .reparent(recorded.entity, recorded_parent.value().id, false)
                  .ok());
  ASSERT_TRUE(harness.editor.scene()
                  .set_transform(recorded.entity,
                                 {
                                     .position = {-1.0f, 0.0f, 0.0f},
                                     .rotation_euler = {0.0f, 0.0f, 0.65f},
                                 })
                  .ok());
  ASSERT_TRUE(harness.editor.scene()
                  .set_transform(replayed.entity,
                                 {
                                     .position = {3.0f, 0.0f, 0.0f},
                                     .rotation_euler = {0.0f, 0.0f, kHalfPi},
                                 })
                  .ok());
  ASSERT_TRUE(harness.editor.selection().select_object(recorded).ok());
  ASSERT_TRUE(harness.editor.viewports()
                  .set_transform_space(quader::QuaderTransformSpace::Local)
                  .ok());
  const std::optional<quader::QuaderTransform3> recorded_orientation =
      harness.editor.query().selection_transform_orientation();
  ASSERT_TRUE(recorded_orientation.has_value());
  EXPECT_GT(std::abs(quader::dot(recorded_orientation->x_axis,
                                 recorded_orientation->y_axis)),
            0.1f);

  const quader::QuaderTransformDragPreview recorded_preview =
      perform_local_x_scene_drag(harness, quader::QuaderTransformTool::Move,
                                 {220.0f, 100.0f});
  ASSERT_GT(quader::length(recorded_preview.move_delta), 0.01f);

  ASSERT_TRUE(harness.editor.selection().select_object(replayed).ok());
  const std::optional<quader::QuaderTransform3> replay_orientation =
      harness.editor.query().selection_transform_orientation();
  ASSERT_TRUE(replay_orientation.has_value());
  const quader::TransformComponent before =
      *harness.session.scene().transform(replayed.entity);
  const quader::QuaderApiVoidResult repeated = harness.editor.commands().execute(
      quader::quader_command::kEditRepeatLastAction);
  ASSERT_TRUE(repeated.ok()) << repeated.error_message();
  ASSERT_TRUE(repeated.changed());
  const quader::TransformComponent after =
      *harness.session.scene().transform(replayed.entity);

  const float recorded_local_x_distance =
      quader::dot(recorded_preview.move_delta, recorded_orientation->x_axis);
  const quader::QVec3 expected_delta =
      replay_orientation->x_axis * recorded_local_x_distance;
  expect_vec3_near(after.position - before.position, expected_delta);
  EXPECT_GT(std::abs(expected_delta.y), 0.01f);
  EXPECT_NEAR(expected_delta.x, 0.0f, 0.001f);
}

TEST(ViewportInteractionControllerTests,
     LocalMultiOffOriginMeshesRotateAndScaleInPlaceAlongEachAuthoredFrame) {
  for (const quader::QuaderTransformTool tool : {
           quader::QuaderTransformTool::Rotate,
           quader::QuaderTransformTool::Scale,
       }) {
    SCOPED_TRACE(tool == quader::QuaderTransformTool::Rotate ? "rotate"
                                                             : "scale");
    ViewportInteractionHarness harness;
    const quader::QuaderModelingObjectRef active =
        create_box_at(harness, {-3.0f, 1.0f, 0.0f});
    const quader::QuaderModelingObjectRef other =
        create_box_at(harness, {4.0f, -2.0f, 0.0f});
    quader::TransformComponent active_before;
    active_before.position = {-3.0f, 1.0f, 0.0f};
    active_before.rotation_euler = {0.0f, 0.0f, 1.57079632679f};
    active_before.scale = {1.25f, 0.75f, 1.5f};
    quader::TransformComponent other_before;
    other_before.position = {4.0f, -2.0f, 0.0f};
    other_before.rotation_euler = {0.35f, -0.5f, -0.7f};
    other_before.scale = {0.6f, 1.4f, 2.0f};
    ASSERT_TRUE(
        harness.session.scene().set_transform(active.entity, active_before));
    ASSERT_TRUE(
        harness.session.scene().set_transform(other.entity, other_before));
    const std::optional<quader::SceneBounds> active_bounds_before =
        quader::entity_world_bounds(harness.session.scene(),
                                    harness.session.assets(), active.entity);
    const std::optional<quader::SceneBounds> other_bounds_before =
        quader::entity_world_bounds(harness.session.scene(),
                                    harness.session.assets(), other.entity);
    ASSERT_TRUE(active_bounds_before.has_value());
    ASSERT_TRUE(other_bounds_before.has_value());
    const quader::QVec3 active_visible_pivot_before =
        quader::bounds_center(*active_bounds_before);
    const quader::QVec3 other_visible_pivot_before =
        quader::bounds_center(*other_bounds_before);
    EXPECT_GT(quader::length(active_visible_pivot_before -
                             active_before.position),
              0.1f);
    EXPECT_GT(
        quader::length(other_visible_pivot_before - other_before.position),
        0.1f);
    ASSERT_TRUE(harness.editor.selection()
                    .select_objects({other.entity, active.entity}, active.entity)
                    .ok());
    ASSERT_TRUE(harness.editor.viewports()
                    .set_transform_space(quader::QuaderTransformSpace::Local)
                    .ok());
    const std::vector<quader::EntityId> selected_before =
        harness.session.scene().selected_entities();
    ASSERT_EQ(harness.session.scene().primary_selection(), active.entity);

    const quader::QuaderTransformDragPreview preview =
        perform_local_x_scene_drag(
            harness, tool,
            tool == quader::QuaderTransformTool::Rotate
                ? quader::QVec2{100.0f, 130.0f}
                : quader::QVec2{220.0f, 100.0f});
    const quader::TransformComponent active_after =
        *harness.session.scene().transform(active.entity);
    const quader::TransformComponent other_after =
        *harness.session.scene().transform(other.entity);
    const std::optional<quader::SceneBounds> active_bounds_after =
        quader::entity_world_bounds(harness.session.scene(),
                                    harness.session.assets(), active.entity);
    const std::optional<quader::SceneBounds> other_bounds_after =
        quader::entity_world_bounds(harness.session.scene(),
                                    harness.session.assets(), other.entity);
    ASSERT_TRUE(active_bounds_after.has_value());
    ASSERT_TRUE(other_bounds_after.has_value());

    expect_vec3_near(quader::bounds_center(*active_bounds_after),
                     active_visible_pivot_before);
    expect_vec3_near(quader::bounds_center(*other_bounds_after),
                     other_visible_pivot_before);
    if (tool == quader::QuaderTransformTool::Rotate) {
      EXPECT_GT(std::abs(preview.rotate_angle_degrees), 1.0);
      const quader::QQuat active_local_delta =
          local_rotation_delta(active_before, active_after);
      const quader::QQuat other_local_delta =
          local_rotation_delta(other_before, other_after);
      EXPECT_GT(std::abs(active_local_delta.x), 0.01f);
      EXPECT_NEAR(active_local_delta.y, 0.0f, 0.001f);
      EXPECT_NEAR(active_local_delta.z, 0.0f, 0.001f);
      expect_quaternion_near(other_local_delta, active_local_delta);
      expect_vec3_near(active_after.scale, active_before.scale);
      expect_vec3_near(other_after.scale, other_before.scale);
    } else {
      ASSERT_GT(preview.scale.x, 1.01f);
      EXPECT_NEAR(preview.scale.y, 1.0f, 0.001f);
      EXPECT_NEAR(preview.scale.z, 1.0f, 0.001f);
      expect_vec3_near(
          active_after.scale,
          {active_before.scale.x * preview.scale.x, active_before.scale.y,
           active_before.scale.z});
      expect_vec3_near(
          other_after.scale,
          {other_before.scale.x * preview.scale.x, other_before.scale.y,
           other_before.scale.z});
      expect_quaternion_near(
          quader::quat_from_euler_radians(active_after.rotation_euler),
          quader::quat_from_euler_radians(active_before.rotation_euler));
      expect_quaternion_near(
          quader::quat_from_euler_radians(other_after.rotation_euler),
          quader::quat_from_euler_radians(other_before.rotation_euler));
    }

    EXPECT_EQ(harness.session.scene().selected_entities(), selected_before);
    EXPECT_EQ(harness.session.scene().primary_selection(), active.entity);
    EXPECT_EQ(harness.editor.selection().summary().active_object_id,
              active.modeling_object_id);

    ASSERT_TRUE(harness.editor.history().undo().ok());
    expect_transform_near(*harness.session.scene().transform(active.entity),
                          active_before);
    expect_transform_near(*harness.session.scene().transform(other.entity),
                          other_before);
    EXPECT_EQ(harness.session.scene().selected_entities(), selected_before);
    EXPECT_EQ(harness.session.scene().primary_selection(), active.entity);

    ASSERT_TRUE(harness.editor.history().redo().ok());
    expect_transform_near(*harness.session.scene().transform(active.entity),
                          active_after);
    expect_transform_near(*harness.session.scene().transform(other.entity),
                          other_after);
    EXPECT_EQ(harness.session.scene().selected_entities(), selected_before);
    EXPECT_EQ(harness.session.scene().primary_selection(), active.entity);
  }
}

TEST(ViewportInteractionControllerTests,
     ShiftMoveRotateAndScaleDuplicateCommitOnceAndRepeatTwice) {
  /// Names one scene-owned duplicate transform routed through the same gizmo
  /// transaction and repeat contract.
  struct ToolCase {
    quader::ViewportToolId tool = quader::ViewportToolId::Move;
    const char *label = "";
  };

  for (const ToolCase tool_case : {
           ToolCase{quader::ViewportToolId::Move, "move"},
           ToolCase{quader::ViewportToolId::Rotate, "rotate"},
           ToolCase{quader::ViewportToolId::Scale, "scale"},
       }) {
    SCOPED_TRACE(tool_case.label);
    ViewportInteractionHarness harness;
    ASSERT_TRUE(harness.settings.set_bool_value(
        quader::ViewportBoolSettingId::GridSnap, false));
    ASSERT_TRUE(harness.settings.set_bool_value(
        quader::ViewportBoolSettingId::VertexSnap, false));
    const quader::QuaderModelingObjectRef source = create_unit_box(harness);
    quader::TransformComponent source_transform;
    source_transform.position = {0.75f, -0.5f, 0.25f};
    source_transform.rotation_euler = {0.15f, 0.25f, -0.2f};
    source_transform.scale = {1.1f, 0.9f, 1.2f};
    ASSERT_TRUE(harness.session.scene().set_transform(source.entity,
                                                      source_transform));
    ASSERT_TRUE(harness.editor.selection().select_object(source).ok);
    const std::size_t entity_count_before =
        harness.session.scene().entities().size();

    const quader::MeshComponent *source_binding =
        harness.session.scene().mesh(source.entity);
    ASSERT_NE(source_binding, nullptr);
    const quader::AssetId source_mesh_id = source_binding->mesh;
    const quader::MeshAsset *source_mesh =
        harness.session.assets().mesh(source_mesh_id);
    ASSERT_NE(source_mesh, nullptr);
    const quader::MeshGeometryContentId source_geometry =
        source_mesh->geometry_content_id();

    ASSERT_TRUE(harness.editor.tools().activate(tool_case.tool).ok);
    harness.router.set_active_tool(tool_case.tool);
    harness.input.modifiers.shift = true;
    const quader::ViewportBeforeToolInteractionResult warmup =
        harness.controller.handle_before_active_tool(harness.editor,
                                                     harness.frame());
    ASSERT_FALSE(warmup.gizmo_consumed);
    const quader_editor_core::TransformGizmoFrame *gizmo =
        harness.controller.transform_gizmo_frame();
    ASSERT_NE(gizmo, nullptr);

    quader::QVec2 start{};
    quader::QVec2 update{};
    if (tool_case.tool == quader::ViewportToolId::Rotate) {
      const quader_editor_core::TransformGizmoRingSegmentPrimitive *ring =
          find_ring(*gizmo, quader_editor_core::TransformAxis::Z);
      ASSERT_NE(ring, nullptr);
      start = ring->screen_start;
      update = rotated_point(gizmo->info.screen_pivot, start, 0.6f);
    } else if (tool_case.tool == quader::ViewportToolId::Scale) {
      start = gizmo->info.screen_pivot;
      update = start + quader::QVec2{120.0f, 0.0f};
    } else {
      const quader_editor_core::TransformGizmoAxisPrimitive *axis =
          find_axis(*gizmo, quader_editor_core::TransformAxis::X);
      ASSERT_NE(axis, nullptr);
      start = axis->screen_tip;
      update = start + quader::QVec2{120.0f, 0.0f};
    }

    harness.input.mouse_local = start;
    harness.input.mouse_screen = start;
    const quader::ViewportBeforeToolInteractionResult press =
        harness.controller.handle_before_active_tool(
            harness.editor, harness.frame(true, true, false, start));
    ASSERT_TRUE(press.gizmo_consumed);

    harness.input.mouse_local = update;
    harness.input.mouse_screen = update;
    const quader::ViewportBeforeToolInteractionResult drag =
        harness.controller.handle_before_active_tool(
            harness.editor, harness.frame(false, true, false, start));
    ASSERT_TRUE(drag.gizmo_consumed);
    ASSERT_EQ(harness.session.scene().entities().size(),
              entity_count_before + 1u);
    const std::vector<quader::EntityId> preview_selection =
        harness.session.scene().selected_entities();
    ASSERT_EQ(preview_selection.size(), 1u);
    const quader::EntityId preview_duplicate = preview_selection.front();
    const quader::TransformComponent *preview_transform_ptr =
        harness.session.scene().transform(preview_duplicate);
    ASSERT_NE(preview_transform_ptr, nullptr);
    const quader::TransformComponent preview_transform =
        *preview_transform_ptr;

    const quader::ViewportBeforeToolInteractionResult release =
        harness.controller.handle_before_active_tool(
            harness.editor, harness.frame(false, false, true, start));
    ASSERT_TRUE(release.gizmo_consumed);
    harness.input.modifiers.shift = false;
    ASSERT_EQ(harness.session.scene().entities().size(),
              entity_count_before + 1u);
    const std::vector<quader::EntityId> selected_after_drag =
        harness.session.scene().selected_entities();
    ASSERT_EQ(selected_after_drag.size(), 1u);
    const quader::EntityId duplicate = selected_after_drag.front();
    ASSERT_NE(duplicate, source.entity);
    EXPECT_EQ(duplicate, preview_duplicate);
    ASSERT_NE(harness.session.scene().transform(duplicate), nullptr);
    expect_transform_near(*harness.session.scene().transform(duplicate),
                          preview_transform);
    const quader::MeshComponent *duplicate_binding =
        harness.session.scene().mesh(duplicate);
    ASSERT_NE(duplicate_binding, nullptr);
    const quader::MeshAsset *duplicate_mesh =
        harness.session.assets().mesh(duplicate_binding->mesh);
    ASSERT_NE(duplicate_mesh, nullptr);
    EXPECT_EQ(duplicate_mesh->geometry_content_id(), source_geometry);

    const quader::TransformComponent *duplicate_transform =
        harness.session.scene().transform(duplicate);
    ASSERT_NE(duplicate_transform, nullptr);
    if (tool_case.tool == quader::ViewportToolId::Move) {
      EXPECT_GT(length(duplicate_transform->position -
                       source_transform.position),
                0.001f);
    } else if (tool_case.tool == quader::ViewportToolId::Rotate) {
      EXPECT_GT(length(duplicate_transform->rotation_euler -
                       source_transform.rotation_euler),
                0.001f);
    } else {
      EXPECT_GT(length(duplicate_transform->scale - source_transform.scale),
                0.001f);
    }
    expect_transform_near(
        *harness.session.scene().transform(source.entity), source_transform);

    quader::TransformComponent previous_repeat_transform =
        *duplicate_transform;
    const quader::QVec3 original_move_step =
        duplicate_transform->position - source_transform.position;
    for (std::size_t repeat_index = 0u; repeat_index < 2u; ++repeat_index) {
      const quader::QuaderApiVoidResult repeated =
          harness.editor.commands().execute(
              quader::quader_command::kEditRepeatLastAction);
      ASSERT_TRUE(repeated.ok()) << repeated.error_message();
      EXPECT_TRUE(repeated.changed());
      EXPECT_EQ(harness.session.scene().entities().size(),
                entity_count_before + 2u + repeat_index);
      const std::vector<quader::EntityId> repeated_selection =
          harness.session.scene().selected_entities();
      ASSERT_EQ(repeated_selection.size(), 1u);
      const quader::MeshComponent *repeated_binding =
          harness.session.scene().mesh(repeated_selection.front());
      ASSERT_NE(repeated_binding, nullptr);
      const quader::MeshAsset *repeated_mesh =
          harness.session.assets().mesh(repeated_binding->mesh);
      ASSERT_NE(repeated_mesh, nullptr);
      EXPECT_EQ(repeated_mesh->geometry_content_id(), source_geometry);
      const quader::TransformComponent *repeated_transform =
          harness.session.scene().transform(repeated_selection.front());
      ASSERT_NE(repeated_transform, nullptr);
      if (tool_case.tool == quader::ViewportToolId::Move) {
        const quader::QVec3 repeat_step =
            repeated_transform->position - previous_repeat_transform.position;
        EXPECT_NEAR(repeat_step.x, original_move_step.x, 0.001f);
        EXPECT_NEAR(repeat_step.y, original_move_step.y, 0.001f);
        EXPECT_NEAR(repeat_step.z, original_move_step.z, 0.001f);
      } else if (tool_case.tool == quader::ViewportToolId::Rotate) {
        EXPECT_GT(length(repeated_transform->rotation_euler -
                         previous_repeat_transform.rotation_euler),
                  0.001f);
      } else {
        EXPECT_GT(length(repeated_transform->scale -
                         previous_repeat_transform.scale),
                  0.001f);
      }
      previous_repeat_transform = *repeated_transform;
    }

    ASSERT_TRUE(harness.editor.history().undo().ok());
    EXPECT_EQ(harness.session.scene().entities().size(),
              entity_count_before + 2u);
    ASSERT_TRUE(harness.editor.history().undo().ok());
    EXPECT_EQ(harness.session.scene().entities().size(),
              entity_count_before + 1u);
    ASSERT_TRUE(harness.editor.history().undo().ok());
    EXPECT_EQ(harness.session.scene().entities().size(), entity_count_before);
    ASSERT_TRUE(harness.editor.history().redo().ok());
    EXPECT_EQ(harness.session.scene().entities().size(),
              entity_count_before + 1u);
  }
}

TEST(ViewportInteractionControllerTests,
     ShiftOrAltMoveGizmoDragCreatesAndSelectsOneLinkedInstanceOccurrence) {
  for (const bool use_shift : {true, false}) {
    SCOPED_TRACE(use_shift ? "Shift" : "Alt");
    ViewportInteractionHarness harness;
    ASSERT_TRUE(harness.settings.set_bool_value(
        quader::ViewportBoolSettingId::GridSnap, false));
    ASSERT_TRUE(harness.settings.set_bool_value(
        quader::ViewportBoolSettingId::VertexSnap, false));
    const quader::QuaderModelingObjectRef member = create_unit_box(harness);
    ASSERT_TRUE(harness.editor.selection().select_object(member).ok);
    ASSERT_TRUE(harness.editor.modeling().create_instance().ok());
    const quader::EntityId source =
        harness.session.scene().selected_entities().front();
    const quader::InstanceOccurrenceComponent *source_instance =
        harness.session.scene().instance_occurrence(source);
    ASSERT_NE(source_instance, nullptr);
    const quader::EntityId definition = source_instance->definition;
    const quader::TransformComponent source_transform =
        *harness.session.scene().transform(source);

    ASSERT_TRUE(
        harness.editor.tools().activate(quader::ViewportToolId::Move).ok);
    harness.router.set_active_tool(quader::ViewportToolId::Move);
    harness.input.modifiers.shift = use_shift;
    harness.input.modifiers.alt = !use_shift;
    const quader::ViewportBeforeToolInteractionResult warmup =
        harness.controller.handle_before_active_tool(harness.editor,
                                                     harness.frame());
    ASSERT_FALSE(warmup.gizmo_consumed);
    const quader_editor_core::TransformGizmoFrame *gizmo =
        harness.controller.transform_gizmo_frame();
    ASSERT_NE(gizmo, nullptr);
    const quader_editor_core::TransformGizmoAxisPrimitive *axis =
        find_axis(*gizmo, quader_editor_core::TransformAxis::X);
    ASSERT_NE(axis, nullptr);
    const quader::QVec2 start = axis->screen_tip;
    const quader::QVec2 update = start + quader::QVec2{120.0f, 0.0f};

    press_update_release_gizmo(harness, start, update);

    const std::vector<quader::EntityId> selected =
        harness.session.scene().selected_entities();
    ASSERT_EQ(selected.size(), 1u);
    const quader::EntityId duplicate = selected.front();
    ASSERT_NE(duplicate, source);
    const quader::InstanceOccurrenceComponent *duplicate_instance =
        harness.session.scene().instance_occurrence(duplicate);
    ASSERT_NE(duplicate_instance, nullptr);
    EXPECT_EQ(duplicate_instance->definition, definition);
    EXPECT_GT(length(harness.session.scene().transform(duplicate)->position -
                     source_transform.position),
              0.001f);
    expect_transform_near(*harness.session.scene().transform(source),
                          source_transform);
    EXPECT_EQ(harness.editor.query().history().undo_label,
              "Duplicate Instance");

    ASSERT_TRUE(harness.editor.history().undo().ok());
    EXPECT_FALSE(harness.session.scene().exists(duplicate));
    EXPECT_TRUE(harness.session.scene().exists(source));
    EXPECT_EQ(harness.session.scene().selected_entities(),
              std::vector<quader::EntityId>{source});
  }
}

TEST(ViewportInteractionControllerTests,
     MoveGizmoCommitsPressMoveReleaseCollapsedIntoOneFrame) {
    ViewportInteractionHarness harness;
    ASSERT_TRUE(harness.settings.set_bool_value(
        quader::ViewportBoolSettingId::GridSnap, false));
    ASSERT_TRUE(harness.settings.set_bool_value(
        quader::ViewportBoolSettingId::VertexSnap, false));
    const quader::QuaderModelingObjectRef box = create_unit_box(harness);
    ASSERT_TRUE(harness.editor.selection().select_object(box).ok);
    ASSERT_TRUE(
        harness.editor.tools().activate(quader::ViewportToolId::Move).ok);
    harness.router.set_active_tool(quader::ViewportToolId::Move);

    (void)harness.controller.handle_before_active_tool(harness.editor,
                                                       harness.frame());
    const quader_editor_core::TransformGizmoFrame *gizmo =
        harness.controller.transform_gizmo_frame();
    ASSERT_NE(gizmo, nullptr);
    const quader_editor_core::TransformGizmoAxisPrimitive *axis =
        find_axis(*gizmo, quader_editor_core::TransformAxis::X);
    ASSERT_NE(axis, nullptr);
    const quader::QVec2 press_position = axis->screen_tip;
    const quader::QVec2 release_position =
        press_position + quader::QVec2{120.0f, 0.0f};
    const quader::TransformComponent before =
        *harness.session.scene().transform(box.entity);

    harness.input.mouse_local = release_position;
    harness.input.mouse_screen = release_position;
    const quader::ViewportBeforeToolInteractionResult collapsed =
        harness.controller.handle_before_active_tool(
            harness.editor,
            harness.frame(true, false, true, press_position));

    EXPECT_TRUE(collapsed.gizmo_consumed);
    EXPECT_FALSE(collapsed.selection_click_requested);
    EXPECT_FALSE(harness.controller.dragging());
    const quader::TransformComponent after =
        *harness.session.scene().transform(box.entity);
    EXPECT_GT(length(after.position - before.position), 0.001f);
    ASSERT_TRUE(harness.editor.history().can_undo());
    const quader::QuaderApiVoidResult undo = harness.editor.commands().execute(
        quader::quader_command::kEditUndo);
    ASSERT_TRUE(undo.ok()) << undo.error_message();
    EXPECT_TRUE(undo.changed());
    expect_transform_near(*harness.session.scene().transform(box.entity),
                          before);
}

TEST(ViewportInteractionControllerTests,
     MoveGizmoAppliesFinalReleasePositionBeforeCommit) {
    ViewportInteractionHarness harness;
    ASSERT_TRUE(harness.settings.set_bool_value(
        quader::ViewportBoolSettingId::GridSnap, false));
    ASSERT_TRUE(harness.settings.set_bool_value(
        quader::ViewportBoolSettingId::VertexSnap, false));
    const quader::QuaderModelingObjectRef box = create_unit_box(harness);
    ASSERT_TRUE(harness.editor.selection().select_object(box).ok);
    ASSERT_TRUE(
        harness.editor.tools().activate(quader::ViewportToolId::Move).ok);
    harness.router.set_active_tool(quader::ViewportToolId::Move);

    (void)harness.controller.handle_before_active_tool(harness.editor,
                                                       harness.frame());
    const quader_editor_core::TransformGizmoFrame *gizmo =
        harness.controller.transform_gizmo_frame();
    ASSERT_NE(gizmo, nullptr);
    const quader_editor_core::TransformGizmoAxisPrimitive *axis =
        find_axis(*gizmo, quader_editor_core::TransformAxis::X);
    ASSERT_NE(axis, nullptr);
    const quader::QVec2 press_position = axis->screen_tip;
    const quader::QVec2 drag_position =
        press_position + quader::QVec2{48.0f, 0.0f};
    const quader::QVec2 release_position =
        press_position + quader::QVec2{140.0f, 0.0f};
    const quader::TransformComponent before =
        *harness.session.scene().transform(box.entity);

    harness.input.mouse_local = press_position;
    harness.input.mouse_screen = press_position;
    EXPECT_TRUE(harness.controller
                    .handle_before_active_tool(
                        harness.editor,
                        harness.frame(true, true, false, press_position))
                    .gizmo_consumed);
    harness.input.mouse_local = drag_position;
    harness.input.mouse_screen = drag_position;
    EXPECT_TRUE(harness.controller
                    .handle_before_active_tool(
                        harness.editor,
                        harness.frame(false, true, false, press_position))
                    .gizmo_consumed);
    const quader::TransformComponent during_drag =
        *harness.session.scene().transform(box.entity);

    harness.input.mouse_local = release_position;
    harness.input.mouse_screen = release_position;
    EXPECT_TRUE(harness.controller
                    .handle_before_active_tool(
                        harness.editor,
                        harness.frame(false, false, true, press_position))
                    .gizmo_consumed);

    EXPECT_FALSE(harness.controller.dragging());
    const quader::TransformComponent after =
        *harness.session.scene().transform(box.entity);
    EXPECT_GT(length(during_drag.position - before.position), 0.001f);
    EXPECT_GT(length(after.position - during_drag.position), 0.001f);
    ASSERT_TRUE(harness.editor.history().can_undo());
    const quader::QuaderApiVoidResult undo = harness.editor.commands().execute(
        quader::quader_command::kEditUndo);
    ASSERT_TRUE(undo.ok()) << undo.error_message();
    EXPECT_TRUE(undo.changed());
    expect_transform_near(*harness.session.scene().transform(box.entity),
                          before);
}

TEST(ViewportInteractionControllerTests,
     ShiftRotateGizmoDragExtrudesSelectedFace) {
    ViewportInteractionHarness harness;
    const quader::QuaderModelingObjectRef box = create_unit_box(harness);
    select_single_component(harness, box, quader::QuaderSelectionMode::Face);
    ASSERT_TRUE(harness.editor.tools().activate(quader::ViewportToolId::Rotate).ok);
    harness.router.set_active_tool(quader::ViewportToolId::Rotate);
    harness.input.modifiers.shift = true;

    const std::optional<quader::QuaderMeshSnapshot> before_mesh =
        harness.editor.query().mesh(box.mesh);
    ASSERT_TRUE(before_mesh.has_value());

    const quader::ViewportBeforeToolInteractionResult warmup =
        harness.controller.handle_before_active_tool(harness.editor,
                                                     harness.frame());
    ASSERT_FALSE(warmup.gizmo_consumed);
    const quader_editor_core::TransformGizmoFrame *frame =
        harness.controller.transform_gizmo_frame();
    ASSERT_NE(frame, nullptr);
    const quader_editor_core::TransformGizmoRingSegmentPrimitive *ring =
        find_ring(*frame, quader_editor_core::TransformAxis::Z);
    ASSERT_NE(ring, nullptr);

    const quader::QVec2 start = ring->screen_start;
    const quader::QVec2 update =
        rotated_point(frame->info.screen_pivot, start, 0.6f);
    press_update_release_gizmo(harness, start, update);

    const std::optional<quader::QuaderMeshSnapshot> after_mesh =
        harness.editor.query().mesh(box.mesh);
    ASSERT_TRUE(after_mesh.has_value());
    EXPECT_GT(after_mesh->vertex_count, before_mesh->vertex_count);
    EXPECT_GT(after_mesh->index_count, before_mesh->index_count);

    const quader::QuaderSelectionSummary selection =
        harness.editor.selection().summary();
    EXPECT_EQ(selection.mode, quader::QuaderSelectionMode::Face);
    EXPECT_EQ(selection.selected_face_count, 1u);
    EXPECT_EQ(selection.selected_object_count, 1u);
}

TEST(ViewportInteractionControllerTests,
     LocalComponentGizmoFollowsExplicitActiveVertexEdgeAndFaceOrientation) {
  for (const quader::QuaderSelectionMode mode : {
           quader::QuaderSelectionMode::Vertex,
           quader::QuaderSelectionMode::Edge,
           quader::QuaderSelectionMode::Face,
       }) {
    SCOPED_TRACE(static_cast<int>(mode));
    ViewportInteractionHarness harness;
    harness.camera.set_view_direction({-0.45f, -0.35f, -0.82f});
    const quader::QuaderModelingObjectRef box = create_unit_box(harness);
    quader::TransformComponent transform;
    transform.rotation_euler = {0.37f, -0.51f, 0.29f};
    transform.scale = {1.4f, 0.8f, 1.9f};
    ASSERT_TRUE(harness.session.scene().set_transform(box.entity, transform));
    select_single_component(harness, box, mode);
    ASSERT_TRUE(harness.editor.viewports()
                    .set_transform_space(quader::QuaderTransformSpace::Local)
                    .ok());
    ASSERT_TRUE(
        harness.editor.tools().activate(quader::ViewportToolId::Move).ok());
    harness.router.set_active_tool(quader::ViewportToolId::Move);

    const std::optional<quader::QuaderTransform3> first_orientation =
        harness.editor.query().selection_transform_orientation();
    ASSERT_TRUE(first_orientation.has_value());
    (void)harness.controller.handle_before_active_tool(harness.editor,
                                                       harness.frame());
    const auto expect_frame_orientation = [&](const auto &orientation) {
      const quader_editor_core::TransformGizmoFrame *frame =
          harness.controller.transform_gizmo_frame();
      ASSERT_NE(frame, nullptr);
      for (const auto &[axis, expected] :
           std::array{
               std::pair{quader_editor_core::TransformAxis::X,
                         orientation.x_axis},
               std::pair{quader_editor_core::TransformAxis::Y,
                         orientation.y_axis},
               std::pair{quader_editor_core::TransformAxis::Z,
                         orientation.z_axis},
           }) {
        const quader_editor_core::TransformGizmoAxisPrimitive *primitive =
            find_axis(*frame, axis);
        ASSERT_NE(primitive, nullptr);
        expect_vec3_near(gizmo_axis_world_direction(*primitive), expected);
      }
    };
    expect_frame_orientation(*first_orientation);

    switch (mode) {
    case quader::QuaderSelectionMode::Vertex:
      ASSERT_TRUE(harness.editor.selection()
                      .select_vertex(box, 2,
                                     quader::QuaderSelectionEdit::Add)
                      .ok());
      break;
    case quader::QuaderSelectionMode::Edge:
      ASSERT_TRUE(harness.editor.selection()
                      .select_edge(box, 1, 4,
                                   quader::QuaderSelectionEdit::Add)
                      .ok());
      break;
    case quader::QuaderSelectionMode::Face:
      ASSERT_TRUE(harness.editor.selection()
                      .select_face(box, 3, quader::QuaderSelectionEdit::Add)
                      .ok());
      break;
    case quader::QuaderSelectionMode::Object:
    case quader::QuaderSelectionMode::Group:
      FAIL() << "Local component gizmo received a non-component mode";
      break;
    }

    const quader::QuaderSelectionSummary summary =
        harness.editor.selection().summary();
    const std::optional<quader::QuaderTransform3> active_orientation =
        harness.editor.query().selection_transform_orientation();
    ASSERT_TRUE(active_orientation.has_value());
    EXPECT_GT(quader::length(first_orientation->x_axis -
                             active_orientation->x_axis) +
                  quader::length(first_orientation->z_axis -
                                 active_orientation->z_axis),
              0.1f);
    (void)harness.controller.handle_before_active_tool(harness.editor,
                                                       harness.frame());
    expect_frame_orientation(*active_orientation);

    if (mode == quader::QuaderSelectionMode::Vertex) {
      EXPECT_EQ(summary.selected_vertex_count, 2u);
      EXPECT_EQ(summary.active_vertex_id, 2u);
    } else if (mode == quader::QuaderSelectionMode::Edge) {
      EXPECT_EQ(summary.selected_edge_count, 2u);
      EXPECT_EQ(summary.active_edge_a, 1u);
      EXPECT_EQ(summary.active_edge_b, 4u);
    } else {
      EXPECT_EQ(summary.selected_face_count, 2u);
      EXPECT_EQ(summary.active_face_id, 3u);
    }
  }
}

TEST(ViewportInteractionControllerTests, MoveGizmoComponentAxisDragKeepsPickedAxisUnderMouse) {
    /**
     * Stores the component selection mode exercised by a gizmo axis drag case.
     */
    struct ComponentModeCase {
        quader::QuaderSelectionMode mode = quader::QuaderSelectionMode::Vertex;
        const char* label = "";
    };
    /**
     * Stores the transform axis exercised by a gizmo drag case.
     */
    struct AxisCase {
      quader_editor_core::TransformAxis axis =
          quader_editor_core::TransformAxis::X;
      const char *label = "";
    };

    for (const ComponentModeCase mode_case : {
             ComponentModeCase{quader::QuaderSelectionMode::Vertex, "vertex"},
             ComponentModeCase{quader::QuaderSelectionMode::Edge, "edge"},
             ComponentModeCase{quader::QuaderSelectionMode::Face, "face"},
         }) {
      for (const AxisCase axis_case : {
               AxisCase{quader_editor_core::TransformAxis::X, "x"},
               AxisCase{quader_editor_core::TransformAxis::Z, "z"},
           }) {
        SCOPED_TRACE(mode_case.label);
        SCOPED_TRACE(axis_case.label);
        ViewportInteractionHarness harness;
        harness.camera.set_view_direction({-0.45f, -0.35f, -0.82f});
        ASSERT_TRUE(harness.settings.set_bool_value(
            quader::ViewportBoolSettingId::GridSnap, false));
        ASSERT_TRUE(harness.settings.set_bool_value(
            quader::ViewportBoolSettingId::VertexSnap, false));

        const quader::QuaderModelingObjectRef box = create_unit_box(harness);
        select_single_component(harness, box, mode_case.mode);
        ASSERT_TRUE(
            harness.editor.tools().activate(quader::ViewportToolId::Move).ok);
        harness.router.set_active_tool(quader::ViewportToolId::Move);

        const std::optional<quader::QVec3> before_center =
            harness.editor.query().selection_center();
        ASSERT_TRUE(before_center.has_value());

        const quader::ViewportBeforeToolInteractionResult warmup =
            harness.controller.handle_before_active_tool(harness.editor,
                                                         harness.frame());
        ASSERT_FALSE(warmup.gizmo_consumed);
        const quader_editor_core::TransformGizmoFrame *frame =
            harness.controller.transform_gizmo_frame();
        ASSERT_NE(frame, nullptr);
        const quader_editor_core::TransformGizmoAxisPrimitive *axis =
            find_axis(*frame, axis_case.axis);
        ASSERT_NE(axis, nullptr);

        const quader::QVec2 axis_delta = axis->screen_tip - axis->screen_start;
        const float axis_length = std::sqrt(axis_delta.x * axis_delta.x +
                                            axis_delta.y * axis_delta.y);
        ASSERT_GT(axis_length, 1.0f);
        const quader::QVec2 axis_direction = axis_delta * (1.0f / axis_length);
        const quader::QVec2 start = axis_screen_point(*axis, 0.25f);
        const quader::QVec2 update = start + axis_direction * 120.0f;

        harness.input.mouse_local = start;
        harness.input.mouse_screen = start;
        const quader::ViewportBeforeToolInteractionResult press =
            harness.controller.handle_before_active_tool(
                harness.editor, harness.frame(true, true, false, start));
        ASSERT_TRUE(press.gizmo_consumed);

        harness.input.mouse_local = update;
        harness.input.mouse_screen = update;
        const quader::ViewportBeforeToolInteractionResult drag =
            harness.controller.handle_before_active_tool(
                harness.editor, harness.frame(false, true, false, start));
        ASSERT_TRUE(drag.gizmo_consumed);

        const std::uint64_t scene_revision_after_drag =
            harness.session.scene().revision();
        const std::uint64_t asset_revision_after_drag =
            harness.session.assets().revision();

        const quader::ViewportBeforeToolInteractionResult refresh =
            harness.controller.handle_before_active_tool(
                harness.editor, harness.frame(false, true, false, start));
        ASSERT_TRUE(refresh.gizmo_consumed);
        EXPECT_EQ(harness.session.scene().revision(), scene_revision_after_drag);
        EXPECT_EQ(harness.session.assets().revision(), asset_revision_after_drag);
        frame = harness.controller.transform_gizmo_frame();
        ASSERT_NE(frame, nullptr);
        axis = find_axis(*frame, axis_case.axis);
        ASSERT_NE(axis, nullptr);
        const quader::QVec2 refreshed_grab_point =
            axis_screen_point(*axis, 0.25f);
        EXPECT_NEAR(refreshed_grab_point.x, update.x, 12.0f);
        EXPECT_NEAR(refreshed_grab_point.y, update.y, 12.0f);

        const quader::ViewportBeforeToolInteractionResult release =
            harness.controller.handle_before_active_tool(
                harness.editor, harness.frame(false, false, true, start));
        ASSERT_TRUE(release.gizmo_consumed);

        const std::optional<quader::QVec3> after_center =
            harness.editor.query().selection_center();
        ASSERT_TRUE(after_center.has_value());
        const quader::QVec3 delta = *after_center - *before_center;
        EXPECT_NEAR(delta.y, 0.0f, 0.01f);
        if (axis_case.axis == quader_editor_core::TransformAxis::X) {
          EXPECT_GT(std::abs(delta.x), 0.01f);
          EXPECT_NEAR(delta.z, 0.0f, 0.01f);
        } else {
          EXPECT_NEAR(delta.x, 0.0f, 0.01f);
          EXPECT_GT(std::abs(delta.z), 0.01f);
        }
      }
    }
}

namespace {

quader::QuaderEntityRef create_radius_test_light(ViewportInteractionHarness &harness) {
  harness.editor.preferences().set_runtime_hooks(quader::QuaderPreferencesRuntimeHooks{
      .overlays_visible = [&harness]() {
        return harness.settings.bool_value(quader::ViewportBoolSettingId::ShowOverlays);
      },
      .set_overlays_visible = [&harness](bool visible) {
        return quader::QuaderApiVoidResult{
            .ok = true,
            .changed = harness.settings.set_bool_value(
                quader::ViewportBoolSettingId::ShowOverlays, visible)};
      },
  });
  quader::QuaderCreatePointLightArgs args;
  args.light.range = 2.0f;
  args.select_created = true;
  const auto created = harness.editor.scene().create_point_light(args);
  EXPECT_TRUE(created.ok()) << created.error_message();
  return created.value();
}

void refresh_radius_candidates(ViewportInteractionHarness &harness) {
  harness.billboard_pick_candidates = quader::ViewportSceneProjection::point_light_pick_candidates(
      harness.session.scene(), harness.session.viewport_workspace().local_visibility_snapshot());
}

quader::QVec2 radius_handle(ViewportInteractionHarness &harness, quader::EntityId entity) {
  refresh_radius_candidates(harness);
  const auto found = std::find_if(harness.billboard_pick_candidates.begin(),
      harness.billboard_pick_candidates.end(), [entity](const auto &candidate) {
        return candidate.entity == entity;
      });
  EXPECT_NE(found, harness.billboard_pick_candidates.end());
  if (found == harness.billboard_pick_candidates.end()) return {};
  const auto pose = harness.camera.pose();
  const auto right = quader::normalize(quader::cross(
      quader::normalize(pose.target - pose.eye), pose.up));
  return harness.camera.project_world_point(found->position +
      right * std::max(found->point_light_range.value_or(0.0f), 0.001f)).position;
}

bool radius_frame(ViewportInteractionHarness &harness, quader::QVec2 local,
                  bool pressed, bool down, bool released) {
  refresh_radius_candidates(harness);
  harness.input.mouse_local = local;
  harness.input.mouse_screen = local;
  return harness.dispatch_frame(harness.frame(pressed, down, released, local)).gizmo_consumed;
}

} // namespace

TEST(ViewportInteractionControllerTests, PointLightRadiusDragPublishesLiveAndOneUndoStep) {
  for (const bool orthographic : {false, true}) {
    ViewportInteractionHarness harness;
    if (orthographic) harness.camera.view_axis(quader::ViewportViewAxis::Z, 1);
    const auto light = create_radius_test_light(harness);
    const auto start = radius_handle(harness, light.id);
    const auto transform = *harness.session.scene().transform(light.id);
    const auto history_before = harness.session.history().undo_count();
    ASSERT_TRUE(radius_frame(harness, start, true, true, false));
    EXPECT_TRUE(harness.controller.point_light_range_pointer_active());
    EXPECT_FALSE(harness.controller.dragging());
    EXPECT_EQ(harness.controller.active_tool_pointer_cursor(harness.router.state().active_tool),
              quader::ViewportPointerCursor::OpenHand);
    ASSERT_TRUE(radius_frame(harness, start + quader::QVec2{30, 40}, false, true, false));
    EXPECT_TRUE(harness.controller.dragging());
    EXPECT_EQ(harness.controller.active_tool_pointer_cursor(harness.router.state().active_tool),
              quader::ViewportPointerCursor::ClosedHand);
    const float middle = harness.editor.query().entity(light.id)->light.range;
    EXPECT_GT(middle, 2.0f);
    EXPECT_EQ(harness.session.history().undo_count(), history_before);
    const auto revision = harness.session.scene().revision();
    ASSERT_TRUE(radius_frame(harness, start + quader::QVec2{30, 40}, false, true, false));
    EXPECT_EQ(harness.session.scene().revision(), revision);
    ASSERT_TRUE(radius_frame(harness, start + quader::QVec2{60, 80}, false, false, true));
    const float after = harness.editor.query().entity(light.id)->light.range;
    EXPECT_GT(after, middle);
    EXPECT_FALSE(harness.controller.point_light_range_pointer_active());
    EXPECT_NE(harness.controller.active_tool_pointer_cursor(harness.router.state().active_tool),
              quader::ViewportPointerCursor::ClosedHand);
    EXPECT_EQ(harness.session.history().undo_count(), history_before + 1u);
    expect_transform_near(*harness.session.scene().transform(light.id), transform);
    ASSERT_TRUE(harness.editor.history().undo().ok());
    EXPECT_FLOAT_EQ(harness.editor.query().entity(light.id)->light.range, 2.0f);
    ASSERT_TRUE(harness.editor.history().redo().ok());
    EXPECT_FLOAT_EQ(harness.editor.query().entity(light.id)->light.range, after);
  }
}

TEST(ViewportInteractionControllerTests, PointLightRadiusHoverCachesPicksAndClearsOnUnavailableHandles) {
  ViewportInteractionHarness harness;
  const auto light = create_radius_test_light(harness);
  harness.input.mouse_local = radius_handle(harness, light.id);
  const auto cursor = [&] {
    return harness.controller.active_tool_pointer_cursor(harness.router.state().active_tool);
  };
  const auto hover = [&](std::uint64_t revision) {
    auto frame = harness.frame();
    frame.scene_billboard_pick_revision = revision;
    return harness.dispatch_frame(frame);
  };
  const auto first_hover = hover(1);
  EXPECT_TRUE(first_hover.gizmo_hovered);
  EXPECT_FALSE(first_hover.gizmo_consumed);
  EXPECT_EQ(cursor(), quader::ViewportPointerCursor::OpenHand);
  const auto picks = harness.controller.point_light_range_hover_pick_count();
  const auto scene_revision = harness.session.scene().revision();
  for (int index = 0; index < 8; ++index) EXPECT_TRUE(hover(1).gizmo_hovered);
  EXPECT_EQ(harness.controller.point_light_range_hover_pick_count(), picks);
  EXPECT_EQ(harness.session.scene().revision(), scene_revision);
  harness.billboard_pick_candidates.front().effectively_locked = true;
  EXPECT_FALSE(hover(2).gizmo_hovered);
  EXPECT_EQ(cursor(), quader::ViewportPointerCursor::SelectionDefault);
  harness.billboard_pick_candidates.front().effectively_locked = false;
  EXPECT_TRUE(hover(3).gizmo_hovered);
  ASSERT_EQ(cursor(), quader::ViewportPointerCursor::OpenHand);
  for (int unavailable = 0; unavailable < 5; ++unavailable) {
    harness.input.viewport_focused = unavailable != 0;
    harness.input.viewport_hovered = unavailable != 1;
    harness.input.navigation_active = unavailable == 2;
    harness.input.pointer_interaction_canceled = unavailable == 3;
    harness.settings.set_bool_value(quader::ViewportBoolSettingId::ShowGizmos,
                                    unavailable != 4);
    EXPECT_FALSE(hover(3).gizmo_hovered);
    EXPECT_EQ(cursor(), unavailable == 0 || unavailable == 4
                            ? quader::ViewportPointerCursor::SelectionDefault
                            : quader::ViewportPointerCursor::Default);
  }
  harness.input.viewport_focused = true;
  harness.input.viewport_hovered = true;
  harness.input.navigation_active = false;
  harness.input.pointer_interaction_canceled = false;
  harness.settings.set_bool_value(quader::ViewportBoolSettingId::ShowGizmos, true);
  EXPECT_TRUE(hover(3).gizmo_hovered);
  EXPECT_EQ(cursor(), quader::ViewportPointerCursor::OpenHand);
  harness.controller.reset();
  EXPECT_EQ(cursor(), quader::ViewportPointerCursor::Default);
}

TEST(ViewportInteractionControllerTests, PointLightRadiusDiagonalDragUsesPlaneDistance) {
  ViewportInteractionHarness harness;
  const auto light = create_radius_test_light(harness);
  const auto start = radius_handle(harness, light.id);
  ASSERT_TRUE(radius_frame(harness, start, true, true, false));
  ASSERT_TRUE(radius_frame(harness, start + quader::QVec2{0, 90}, false, true, false));
  EXPECT_GT(harness.editor.query().entity(light.id)->light.range, 2.0f);
  EXPECT_TRUE(harness.controller.cancel_active_drag(harness.editor));
  EXPECT_FLOAT_EQ(harness.editor.query().entity(light.id)->light.range, 2.0f);
}

TEST(ViewportInteractionControllerTests, PointLightRadiusProductionLifecycleRefreshesSelectionBeforeDrag) {
  ViewportInteractionHarness harness;
  const auto light = create_radius_test_light(harness);
  const auto start = radius_handle(harness, light.id);
  const auto dispatch = [&](quader::QVec2 local, bool pressed, bool down, bool released) {
    refresh_radius_candidates(harness);
    harness.input.mouse_local = local;
    harness.input.mouse_screen = local;
    auto interaction = harness.frame(pressed, down, released, local);
    interaction.selection_state = {};
    const quader::ViewportNativeToolInteractionFrame native{
        .input = harness.input, .input_result = harness.input_result,
        .left_pressed = pressed, .left_down = down, .left_released = released,
        .left_click_local = local};
    return harness.controller.handle_tool_pointer_lifecycle(
        harness.editor, harness.session.scene(), harness.session.assets(),
        harness.session.native_tools(), &harness.session.point_placement(),
        harness.camera, {.interaction = interaction, .native_tool = native});
  };
  EXPECT_TRUE(dispatch(start, true, true, false).before_tool.gizmo_consumed);
  EXPECT_TRUE(dispatch(start + quader::QVec2{60, 0}, false, true, false).before_tool.gizmo_consumed);
  EXPECT_TRUE(harness.controller.dragging());
  EXPECT_GT(harness.editor.query().entity(light.id)->light.range, 2.0f);
  EXPECT_TRUE(dispatch(start + quader::QVec2{60, 0}, false, false, true).before_tool.gizmo_consumed);
  EXPECT_FALSE(harness.controller.point_light_range_pointer_active());
}

TEST(ViewportInteractionControllerTests, PointLightRadiusClickAndRoundTripKeepHistoryAndCleanState) {
  for (const bool round_trip : {false, true}) {
    ViewportInteractionHarness harness;
    const auto light = create_radius_test_light(harness);
    const auto start = radius_handle(harness, light.id);
    harness.session.mark_clean();
    const auto before = harness.session.history().undo_count();
    ASSERT_TRUE(radius_frame(harness, start, true, true, false));
    if (round_trip) {
      ASSERT_TRUE(radius_frame(harness, start + quader::QVec2{60, 0}, false, true, false));
    }
    ASSERT_TRUE(radius_frame(harness, start, false, false, true));
    EXPECT_FLOAT_EQ(harness.editor.query().entity(light.id)->light.range, 2.0f);
    EXPECT_EQ(harness.session.history().undo_count(), before);
    EXPECT_FALSE(harness.session.dirty());
    ASSERT_NE(harness.session.scene().selection(light.id), nullptr);
    EXPECT_TRUE(harness.session.scene().selection(light.id)->selected);
  }
}

TEST(ViewportInteractionControllerTests, PointLightRadiusCancelReservesReleaseAndRetainsRedo) {
  ViewportInteractionHarness harness;
  const auto light = create_radius_test_light(harness);
  ASSERT_TRUE(harness.editor.scene().rename(light.id, "Renamed Light").ok());
  ASSERT_TRUE(harness.editor.history().undo().ok());
  ASSERT_TRUE(harness.editor.history().can_redo());
  harness.session.mark_clean();
  const auto start = radius_handle(harness, light.id);
  const auto before = harness.session.history().undo_count();
  ASSERT_TRUE(radius_frame(harness, start, true, true, false));
  ASSERT_TRUE(radius_frame(harness, start + quader::QVec2{60, 0}, false, true, false));
  EXPECT_FALSE(harness.editor.history().can_redo());
  ASSERT_TRUE(harness.controller.cancel_active_drag(harness.editor));
  EXPECT_FLOAT_EQ(harness.editor.query().entity(light.id)->light.range, 2.0f);
  EXPECT_TRUE(harness.editor.history().can_redo());
  EXPECT_FALSE(harness.session.dirty());
  ASSERT_TRUE(radius_frame(harness, {20, 20}, false, false, true));
  ASSERT_NE(harness.session.scene().selection(light.id), nullptr);
  EXPECT_TRUE(harness.session.scene().selection(light.id)->selected);
  EXPECT_EQ(harness.session.history().undo_count(), before);
}

TEST(ViewportInteractionControllerTests, PointLightRadiusInterruptionRestoresPreview) {
  for (int interruption = 0; interruption < 6; ++interruption) {
    SCOPED_TRACE(interruption);
    ViewportInteractionHarness harness;
    const auto light = create_radius_test_light(harness);
    const auto start = radius_handle(harness, light.id);
    ASSERT_TRUE(radius_frame(harness, start, true, true, false));
    ASSERT_TRUE(radius_frame(harness, start + quader::QVec2{60, 0}, false, true, false));
    switch (interruption) {
    case 0: harness.input.pointer_interaction_canceled = true; break;
    case 1: harness.input.viewport_focused = false; break;
    case 2: harness.input_result.navigation_active = true; break;
    case 3: harness.input_result.cancel_requested = true; break;
    case 4: ASSERT_TRUE(harness.editor.preferences().set_overlays_visible(false).ok()); break;
    case 5: ASSERT_TRUE(harness.editor.scene().set_locked(light.id, true).ok()); break;
    }
    ASSERT_TRUE(radius_frame(harness, start + quader::QVec2{80, 0}, false, true, false));
    EXPECT_FALSE(harness.controller.dragging());
    EXPECT_TRUE(harness.controller.point_light_range_pointer_active());
    EXPECT_FLOAT_EQ(harness.editor.query().entity(light.id)->light.range, 2.0f);
    harness.input_result.cancel_requested = false;
    EXPECT_TRUE(radius_frame(harness, start, false, false, true));
    EXPECT_FALSE(harness.controller.point_light_range_pointer_active());
  }
}

TEST(ViewportInteractionControllerTests, PointLightRadiusMatchesExactReleaseAndCoalescedGesture) {
  ViewportInteractionHarness harness;
  const auto light = create_radius_test_light(harness);
  const auto start = radius_handle(harness, light.id);
  harness.input.pointer_pressed_edges = {{.button = quader::QuaderMouseButton::Left,
                                          .local = start, .sequence = 20}};
  ASSERT_TRUE(radius_frame(harness, start, true, true, false));
  harness.input.pointer_pressed_edges.clear();
  harness.input.pointer_released_edges = {{.button = quader::QuaderMouseButton::Left,
                                           .local = start + quader::QVec2{60, 0}, .sequence = 19}};
  ASSERT_TRUE(radius_frame(harness, start + quader::QVec2{60, 0}, false, true, true));
  EXPECT_TRUE(harness.controller.point_light_range_pointer_active());
  harness.input.pointer_released_edges = {{.button = quader::QuaderMouseButton::Left,
                                           .local = start + quader::QVec2{70, 0}, .sequence = 21}};
  ASSERT_TRUE(radius_frame(harness, start + quader::QVec2{160, 0}, false, false, true));
  const float exact_release_range = harness.editor.query().entity(light.id)->light.range;
  EXPECT_FALSE(harness.controller.point_light_range_pointer_active());
  ASSERT_TRUE(harness.editor.history().undo().ok());
  harness.input.pointer_pressed_edges = {{.button = quader::QuaderMouseButton::Left,
                                          .local = start, .sequence = 30}};
  harness.input.pointer_released_edges = {{.button = quader::QuaderMouseButton::Left,
                                           .local = start + quader::QVec2{70, 0}, .sequence = 31}};
  refresh_radius_candidates(harness);
  harness.input.mouse_local = start + quader::QVec2{160, 0};
  ASSERT_TRUE(harness.dispatch_frame(harness.frame(true, false, true, start)).gizmo_consumed);
  EXPECT_FLOAT_EQ(harness.editor.query().entity(light.id)->light.range, exact_release_range);
}

TEST(ViewportInteractionControllerTests, PointLightRadiusApiUndoCancelsBeforePriorHistory) {
  ViewportInteractionHarness harness;
  const auto light = create_radius_test_light(harness);
  ASSERT_TRUE(harness.editor.scene().rename(light.id, "Renamed").ok());
  ASSERT_TRUE(harness.editor.history().undo().ok());
  const auto before = harness.session.history().undo_count();
  const auto begun = harness.editor.scene().begin_point_light_range_edit(light.id);
  ASSERT_TRUE(begun.ok());
  ASSERT_TRUE(harness.editor.scene().update_point_light_range_edit(begun.value(), 4.0f).ok());
  EXPECT_TRUE(harness.editor.history().can_undo());
  EXPECT_FALSE(harness.editor.history().can_redo());
  ASSERT_TRUE(harness.editor.history().undo().ok());
  EXPECT_FLOAT_EQ(harness.editor.query().entity(light.id)->light.range, 2.0f);
  EXPECT_EQ(harness.session.history().undo_count(), before);
  EXPECT_TRUE(harness.editor.history().can_redo());
  EXPECT_FALSE(harness.session.document_commands().point_light_range_edit_active());
}

TEST(ViewportInteractionControllerTests,
     PointLightRadiusCancelPreservesAuthoredDirtyStateAndIgnoresCameraNavigation) {
  for (const bool change_camera : {false, true}) {
    ViewportInteractionHarness harness;
    const auto light = create_radius_test_light(harness);
    harness.session.mark_clean();
    const auto begun = harness.editor.scene().begin_point_light_range_edit(light.id);
    ASSERT_TRUE(begun.ok());
    ASSERT_TRUE(harness.editor.scene().update_point_light_range_edit(begun.value(), 4.0f).ok());
    if (change_camera) {
      auto camera = harness.session.persisted_viewport_camera_pose();
      camera.eye.x += 1.0f;
      ASSERT_TRUE(harness.session.restore_persisted_viewport_camera_pose(camera));
    } else {
      harness.session.document().rename("Concurrent Scene Name");
    }
    ASSERT_TRUE(harness.editor.scene().finish_point_light_range_edit(begun.value(), true).ok());
    EXPECT_FLOAT_EQ(harness.editor.query().entity(light.id)->light.range, 2.0f);
    EXPECT_EQ(harness.session.dirty(), !change_camera);
  }
}

TEST(ViewportInteractionControllerTests, PointLightRadiusDocumentReplacementRetiresPreview) {
  ViewportInteractionHarness harness;
  const auto light = create_radius_test_light(harness);
  const auto begun = harness.editor.scene().begin_point_light_range_edit(light.id);
  ASSERT_TRUE(begun.ok());
  ASSERT_TRUE(harness.editor.scene().update_point_light_range_edit(begun.value(), 4.0f).ok());
  harness.session.replace_document(quader::SceneContentDocument{});
  EXPECT_FALSE(harness.session.document_commands().point_light_range_edit_active());
  const auto replacement = create_radius_test_light(harness);
  ASSERT_EQ(replacement.id, light.id);
  const auto replacement_edit = harness.editor.scene().begin_point_light_range_edit(replacement.id);
  ASSERT_TRUE(replacement_edit.ok());
  ASSERT_TRUE(harness.editor.scene().update_point_light_range_edit(replacement_edit.value(), 6.0f).ok());
  EXPECT_FALSE(harness.editor.scene().finish_point_light_range_edit(begun.value()).ok());
  EXPECT_FALSE(harness.editor.scene().finish_point_light_range_edit(begun.value(), true).ok());
  EXPECT_TRUE(harness.session.document_commands().point_light_range_edit_active());
  EXPECT_FLOAT_EQ(harness.editor.query().entity(replacement.id)->light.range, 6.0f);
  ASSERT_TRUE(harness.editor.scene().finish_point_light_range_edit(replacement_edit.value(), true).ok());
  EXPECT_FLOAT_EQ(harness.editor.query().entity(replacement.id)->light.range, 2.0f);
}

TEST(ViewportInteractionControllerTests, PointLightRadiusRejectsCompetingToolAndNoOpInspectorDoesNotCommit) {
  ViewportInteractionHarness harness;
  const auto light = create_radius_test_light(harness);
  harness.session.mark_clean();
  const auto before = harness.session.history().undo_count();
  const auto begun = harness.editor.scene().begin_point_light_range_edit(light.id);
  ASSERT_TRUE(begun.ok());
  ASSERT_TRUE(harness.editor.scene().update_point_light_range_edit(begun.value(), 4.0f).ok());
  EXPECT_FALSE(harness.editor.tools().activate(quader::ViewportToolId::CreatePrimitive).ok());
  const auto unchanged = harness.editor.scene().set_light_component(
      light.id, harness.editor.query().entity(light.id)->light);
  ASSERT_TRUE(unchanged.ok());
  EXPECT_FALSE(unchanged.changed());
  EXPECT_TRUE(harness.session.document_commands().point_light_range_edit_active());
  EXPECT_EQ(harness.session.history().undo_count(), before);
  ASSERT_TRUE(harness.editor.scene().finish_point_light_range_edit(begun.value(), true).ok());
  EXPECT_FLOAT_EQ(harness.editor.query().entity(light.id)->light.range, 2.0f);
  EXPECT_FALSE(harness.session.dirty());
}

TEST(ViewportInteractionControllerTests, PointLightRadiusProjectionTracksSelectionAndInheritedPosition) {
  ViewportInteractionHarness harness;
  auto parent = harness.editor.scene().create_entity({.name = "Light Parent"});
  ASSERT_TRUE(parent.ok());
  quader::QuaderTransform parent_transform;
  parent_transform.position = {3, 4, 5};
  parent_transform.scale = {2, 3, 4};
  ASSERT_TRUE(harness.editor.scene().set_transform(parent.value().id, parent_transform).ok());
  quader::QuaderCreatePointLightArgs args;
  args.parent = parent.value().id;
  args.transform.position = {1, 0, 0};
  args.light.range = 2;
  const auto light = harness.editor.scene().create_point_light(args);
  ASSERT_TRUE(light.ok());
  refresh_radius_candidates(harness);
  ASSERT_EQ(harness.billboard_pick_candidates.size(), 1u);
  EXPECT_FALSE(harness.billboard_pick_candidates.front().point_light_range);
  ASSERT_TRUE(harness.editor.scene().select(light.value().id).ok());
  refresh_radius_candidates(harness);
  const auto selected = harness.billboard_pick_candidates.front();
  ASSERT_TRUE(selected.point_light_range);
  EXPECT_FLOAT_EQ(*selected.point_light_range, 2.0f);
  EXPECT_FLOAT_EQ(selected.position.x, 5.0f);
  EXPECT_FLOAT_EQ(selected.position.y, 4.0f);
  EXPECT_FLOAT_EQ(selected.position.z, 5.0f);
  auto component = harness.editor.query().entity(light.value().id)->light;
  component.range = 7;
  ASSERT_TRUE(harness.editor.scene().set_light_component(light.value().id, component).ok());
  refresh_radius_candidates(harness);
  EXPECT_FLOAT_EQ(*harness.billboard_pick_candidates.front().point_light_range, 7.0f);
  ASSERT_TRUE(harness.editor.scene().set_visible(parent.value().id, false).ok());
  refresh_radius_candidates(harness);
  EXPECT_TRUE(harness.billboard_pick_candidates.empty());
}

TEST(ViewportInteractionControllerTests, DISABLED_PointLightRadiusPreviewCpuBenchmark) {
  using Clock = std::chrono::steady_clock;
  constexpr int kSamples = 256;
  for (const int mesh_count : {0, 128}) {
    ViewportInteractionHarness harness;
    for (int index = 0; index < mesh_count; ++index) {
      (void)create_box_at(harness, {static_cast<float>(index % 16) * 3.0f,
                                    0.0f, static_cast<float>(index / 16) * 3.0f});
    }
    const auto light = create_radius_test_light(harness);
    const auto &scene = harness.session.scene();
    const auto &assets = harness.session.assets();
    const auto visibility = harness.session.viewport_workspace().local_visibility_snapshot();
    for (const bool retained_refresh : {false, true}) {
      const auto begun = harness.editor.scene().begin_point_light_range_edit(light.id);
      ASSERT_TRUE(begun.ok());
      auto render = quader::build_render_scene_snapshot(scene, assets, {.local_visibility = &visibility});
      (void)harness.editor.selection().summary();
      (void)harness.session.scene_surface_query().snapshot();
      const auto sdk_before = harness.session.modeling().modeling_sdk_projection_stats_for_tests();
      const auto surface_before = harness.session.scene_surface_query().stats();
      std::array<double, 5> elapsed_us{};
      int full_render_builds = 0;
      for (int index = 0; index < kSamples; ++index) {
        const auto begin = Clock::now();
        const auto updated = harness.editor.scene().update_point_light_range_edit(
            begun.value(), 2.0f + static_cast<float>(index + 1) * 0.005f);
        const auto after_api = Clock::now();
        const auto selection = harness.editor.selection().summary();
        const auto after_selection = Clock::now();
        const auto surfaces = harness.session.scene_surface_query().snapshot();
        const auto after_surfaces = Clock::now();
        const auto picks = quader::ViewportSceneProjection::point_light_pick_candidates(scene, visibility);
        const auto after_picks = Clock::now();
        if (!quader::render_scene_snapshot_matches_sources(render, scene, assets, {}, 0, {}, &visibility) &&
            !(retained_refresh && quader::try_refresh_render_scene_snapshot_light_properties(render, scene, assets, index, {}, 0, {}, &visibility)) &&
            !quader::try_refresh_render_scene_snapshot_curve_publication(render, scene, assets, index, {}, 0, {}, &visibility) &&
            !quader::try_refresh_render_scene_snapshot_selection(render, scene, assets, index, {}, 0, {}, &visibility) &&
            !quader::try_refresh_render_scene_snapshot_transforms(render, scene, assets, index, {}, 0, {}, &visibility)) {
          render = quader::build_render_scene_snapshot(scene, assets,
              {.previous = &render, .frame_index = static_cast<std::uint64_t>(index),
               .local_visibility = &visibility});
          ++full_render_builds;
        }
        const auto after_render = Clock::now();
        ASSERT_TRUE(updated.ok());
        ASSERT_TRUE(selection.has_selection);
        ASSERT_TRUE(surfaces.valid());
        ASSERT_EQ(picks.size(), 1u);
        ASSERT_EQ(render.lights.size(), 1u);
        ASSERT_FLOAT_EQ(render.lights.front().light.range, scene.light(light.id)->range);
        const std::array times{begin, after_api, after_selection, after_surfaces, after_picks, after_render};
        for (std::size_t stage = 0; stage < elapsed_us.size(); ++stage) {
          elapsed_us[stage] += std::chrono::duration<double, std::micro>(times[stage + 1] - times[stage]).count();
        }
      }
      const auto sdk_after = harness.session.modeling().modeling_sdk_projection_stats_for_tests();
      const auto surface_after = harness.session.scene_surface_query().stats();
      std::printf("LIGHT_RADIUS_CPU mode=%s meshes=%d samples=%d api_us=%.3f selection_us=%.3f surface_us=%.3f pick_projection_us=%.3f render_extraction_us=%.3f full_render_builds=%d sdk_resets=%llu sdk_document_syncs=%llu surface_rebuilds=%llu\n",
          retained_refresh ? "retained" : "baseline", mesh_count, kSamples, elapsed_us[0] / kSamples, elapsed_us[1] / kSamples,
          elapsed_us[2] / kSamples, elapsed_us[3] / kSamples, elapsed_us[4] / kSamples,
          full_render_builds,
          static_cast<unsigned long long>(sdk_after.full_scene_reset_count - sdk_before.full_scene_reset_count),
          static_cast<unsigned long long>(sdk_after.targeted_scene_sync_document_count - sdk_before.targeted_scene_sync_document_count),
          static_cast<unsigned long long>(surface_after.snapshot_build_count - surface_before.snapshot_build_count));
      EXPECT_EQ(full_render_builds, retained_refresh ? 0 : kSamples);
      ASSERT_TRUE(harness.editor.scene().finish_point_light_range_edit(begun.value(), true).ok());
    }
  }
}

TEST(ViewportInteractionControllerTests,
     PointLightPropertyRefreshRetainsMeshStorageAndUpdatesLiveProperties) {
  for (const int mesh_count : {1, 2}) {
    SCOPED_TRACE(mesh_count);
    ViewportInteractionHarness harness;
    std::vector<quader::EntityId> mesh_entities;
    for (int index = 0; index < mesh_count; ++index) {
      mesh_entities.push_back(create_box_at(harness, {}).entity);
    }
    const auto other_light = create_radius_test_light(harness);
    const auto light = create_radius_test_light(harness);
    auto &scene = harness.session.scene();
    const auto &assets = harness.session.assets();
    if (mesh_count == 2) {
      // The modeling fixture creates separate authored payloads. Explicitly
      // share one mesh asset so this case exercises retained GPU occurrences.
      const auto shared_mesh = *scene.mesh(mesh_entities.front());
      ASSERT_TRUE(scene.set_mesh(mesh_entities.back(), shared_mesh));
      auto placement = *scene.transform(mesh_entities.back());
      placement.position.x = 3.0f;
      ASSERT_TRUE(scene.set_transform(mesh_entities.back(), placement));
    }
    auto render = quader::build_render_scene_snapshot(scene, assets);
    ASSERT_EQ(render.lights.size(), 2u);
    if (mesh_count == 1) {
      ASSERT_EQ(render.meshes.size(), 1u);
    } else {
      ASSERT_FALSE(render.instance_primitives.empty());
    }
    const auto *const meshes = render.meshes.data();
    const auto *const instances = render.instance_primitives.data();
    const auto *const lights = render.lights.data();
    const auto geometry = mesh_count == 1
        ? render.meshes.front().mesh.geometry_payload
        : render.instance_primitives.front().mesh.geometry_payload;
    const auto *const occurrences = mesh_count == 1
        ? nullptr : render.instance_primitives.front().occurrences.data();
    auto changed = *scene.light(light.id);
    changed.range = 5.0f;
    changed.color = {0.1f, 0.2f, 0.3f};
    changed.intensity = 3.5f;
    changed.casts_shadows = !changed.casts_shadows;
    ASSERT_TRUE(scene.set_light(light.id, changed));

    ASSERT_TRUE(quader::try_refresh_render_scene_snapshot_light_properties(
        render, scene, assets, 9u));
    EXPECT_EQ(render.frame_index, 9u);
    EXPECT_EQ(render.meshes.data(), meshes);
    EXPECT_EQ(render.instance_primitives.data(), instances);
    EXPECT_EQ(render.lights.data(), lights);
    EXPECT_TRUE(render.reused_previous_records);
    EXPECT_TRUE(render.dirty.render_components_changed);
    EXPECT_FALSE(render.dirty.transforms_changed);
    for (const auto &mesh : render.meshes) {
      EXPECT_FALSE(mesh.mesh_dirty);
      EXPECT_FALSE(mesh.material_dirty);
      EXPECT_FALSE(mesh.binding_dirty);
      EXPECT_FALSE(mesh.transform_dirty);
      EXPECT_FALSE(mesh.texture_mapping_dirty);
    }
    if (mesh_count == 1) {
      EXPECT_EQ(render.meshes.front().mesh.geometry_payload, geometry);
    } else {
      EXPECT_EQ(render.instance_primitives.front().mesh.geometry_payload, geometry);
      EXPECT_EQ(render.instance_primitives.front().occurrences.data(), occurrences);
    }
    for (const auto &record : render.lights) {
      EXPECT_EQ(record.light, *scene.light(record.entity_id));
      EXPECT_EQ(record.light_dirty, record.entity_id == light.id);
      EXPECT_FALSE(record.transform_dirty);
      if (record.entity_id == other_light.id) {
        EXPECT_FLOAT_EQ(record.light.range, 2.0f);
      }
    }
    EXPECT_TRUE(quader::render_scene_snapshot_matches_sources(render, scene, assets));
    EXPECT_FALSE(quader::try_refresh_render_scene_snapshot_light_properties(
        render, scene, assets, 10u));
  }
}

TEST(ViewportInteractionControllerTests,
     PointLightPropertyRefreshRejectsMixedEditsAndChangedBindings) {
  for (const int mixed_edit : {0, 1, 2, 3, 4, 5, 6, 7, 8}) {
    SCOPED_TRACE(mixed_edit);
    ViewportInteractionHarness harness;
    const auto box = create_box_at(harness, {0.0f, 0.0f, 0.0f});
    const auto light = create_radius_test_light(harness);
    auto &scene = harness.session.scene();
    const auto &assets = harness.session.assets();
    quader::RenderSceneBindingKey binding{
        .scene_session_id = 19u, .activation_generation = 1u, .binding_revision = 1u};
    auto render = quader::build_render_scene_snapshot(
        scene, assets, {.scene_binding = binding});
    const quader::RenderSourceStamp before = render;
    const auto *const meshes = render.meshes.data();
    auto component = *scene.light(light.id);
    component.range = 5.0f;
    ASSERT_TRUE(scene.set_light(light.id, component));
    std::uint64_t project_material_revision = 0u;
    switch (mixed_edit) {
    case 0:
      ASSERT_TRUE(harness.editor.scene().create_point_light({}).ok());
      break;
    case 1:
      ASSERT_TRUE(scene.clear_light(light.id));
      break;
    case 2: {
      auto mesh = *scene.mesh(box.entity);
      mesh.cast_shadows = !mesh.cast_shadows;
      ASSERT_TRUE(scene.set_mesh(box.entity, mesh));
      break;
    }
    case 3: {
      auto transform = *scene.transform(box.entity);
      transform.position.x += 1.0f;
      ASSERT_TRUE(scene.set_transform(box.entity, transform));
      break;
    }
    case 4:
      ASSERT_TRUE(harness.editor.scene().rename(light.id, "Mixed Radius Edit").ok());
      break;
    case 5:
      ASSERT_TRUE(harness.editor.selection().select_object(box).ok());
      break;
    case 6:
      ASSERT_TRUE(harness.editor.scene().set_visible(box.entity, false).ok());
      break;
    case 7:
      project_material_revision = 1u;
      break;
    case 8:
      ++binding.activation_generation;
      break;
    }
    EXPECT_FALSE(quader::try_refresh_render_scene_snapshot_light_properties(
        render, scene, assets, 9u, binding, project_material_revision));
    EXPECT_EQ(static_cast<const quader::RenderSourceStamp &>(render), before);
    EXPECT_EQ(render.meshes.data(), meshes);
    ASSERT_EQ(render.lights.size(), 1u);
    EXPECT_FLOAT_EQ(render.lights.front().light.range, 2.0f);
  }
}

TEST(ViewportInteractionControllerTests,
     PointLightPropertyRefreshRejectsRevisionRollbackAndRecoversAfterRebuild) {
  ViewportInteractionHarness harness;
  const auto light = create_radius_test_light(harness);
  auto &scene = harness.session.scene();
  const auto &assets = harness.session.assets();
  const std::array participants{light.id};
  const auto checkpoint = scene.capture_modeling_projection_checkpoint(
      participants, quader::SceneModelingProjectionDomain::Structure |
                        quader::SceneModelingProjectionDomain::Selection);
  ASSERT_TRUE(checkpoint.valid());
  const auto initial_light_revision = scene.light_property_revision();
  auto component = *scene.light(light.id);
  component.range = 5.0f;
  ASSERT_TRUE(scene.set_light(light.id, component));
  auto render = quader::build_render_scene_snapshot(scene, assets);
  ASSERT_TRUE(scene.restore_modeling_projection_checkpoint(checkpoint));
  EXPECT_EQ(scene.light_property_revision(), initial_light_revision);
  EXPECT_FALSE(quader::try_refresh_render_scene_snapshot_light_properties(
      render, scene, assets));
  render = quader::build_render_scene_snapshot(scene, assets);
  EXPECT_FLOAT_EQ(render.lights.front().light.range, 2.0f);
  ASSERT_TRUE(scene.set_light(light.id, component));
  EXPECT_TRUE(quader::try_refresh_render_scene_snapshot_light_properties(
      render, scene, assets));
  EXPECT_FLOAT_EQ(render.lights.front().light.range, 5.0f);
}

TEST(ViewportInteractionControllerTests, LightPropertyRevisionDistinguishesEditsAndExactRollback) {
  ViewportInteractionHarness harness;
  const auto light = create_radius_test_light(harness);
  auto &scene = harness.session.scene();
  const auto baseline = scene.light_property_revision();
  const auto scene_baseline = scene.revision();
  const auto original = *scene.light(light.id);
  auto changed = original;
  changed.range = 5.0f;
  ASSERT_TRUE(scene.set_light(light.id, changed));
  EXPECT_EQ(scene.light_property_revision(), baseline + 1u);
  EXPECT_EQ(scene.revision(), scene_baseline + 1u);
  ASSERT_TRUE(scene.set_light(light.id, changed));
  EXPECT_EQ(scene.light_property_revision(), baseline + 1u);
  ASSERT_TRUE(harness.editor.scene().rename(light.id, "Renamed Radius Light").ok());
  EXPECT_EQ(scene.light_property_revision(), baseline + 1u);
  EXPECT_GT(scene.revision() - scene_baseline, scene.light_property_revision() - baseline);

  auto checkpoint = scene.capture_scalar_compensation_checkpoint();
  ASSERT_TRUE(checkpoint.valid());
  const auto before_checkpoint_edit = scene.light_property_revision();
  ASSERT_TRUE(scene.set_light(light.id, original));
  ASSERT_TRUE(scene.acknowledge_scalar_compensation_checkpoint(checkpoint));
  ASSERT_TRUE(scene.begin_scalar_compensation_restore(checkpoint));
  ASSERT_TRUE(scene.set_light(light.id, changed));
  ASSERT_TRUE(scene.restore_scalar_compensation_checkpoint(checkpoint));
  EXPECT_EQ(scene.light_property_revision(), before_checkpoint_edit);
  EXPECT_FLOAT_EQ(scene.light(light.id)->range, changed.range);
}

TEST(ViewportInteractionControllerTests, MoveRotateAndScaleGizmosDragSelectedPointLightThroughViewportFrames) {
    /**
     * Captures the viewport tool under test and the trace label for per-tool gizmo assertions.
     */
    struct ToolCase {
        quader::ViewportToolId tool = quader::ViewportToolId::Move;
        const char* label = "";
    };

    for (const ToolCase tool_case : {
             ToolCase{quader::ViewportToolId::Move, "move"},
             ToolCase{quader::ViewportToolId::Rotate, "rotate"},
             ToolCase{quader::ViewportToolId::Scale, "scale"},
         }) {
      SCOPED_TRACE(tool_case.label);
      ViewportInteractionHarness harness;
      quader::QuaderCreatePointLightArgs args;
      args.name = "Viewport Point Light";
      args.select_created = true;
      const quader::QuaderApiResult<quader::QuaderEntityRef> light =
          harness.editor.scene().create_point_light(args);
      ASSERT_TRUE(light.ok()) << light.error_message();
      ASSERT_NE(harness.session.scene().light(light.value().id), nullptr);

      ASSERT_TRUE(harness.editor.tools().activate(tool_case.tool).ok);
      harness.router.set_active_tool(tool_case.tool);

      const quader::ViewportBeforeToolInteractionResult warmup =
          harness.controller.handle_before_active_tool(harness.editor,
                                                       harness.frame());
      EXPECT_FALSE(warmup.gizmo_consumed);
      const quader_editor_core::TransformGizmoFrame *frame =
          harness.controller.transform_gizmo_frame();
      ASSERT_NE(frame, nullptr);

      quader::QVec2 start{};
      quader::QVec2 update{};
      if (tool_case.tool == quader::ViewportToolId::Rotate) {
        const quader_editor_core::TransformGizmoRingSegmentPrimitive *ring =
            find_ring(*frame, quader_editor_core::TransformAxis::Z);
        ASSERT_NE(ring, nullptr);
        start = ring->screen_start;
        update = rotated_point(frame->info.screen_pivot, start, 0.6f);
      } else if (tool_case.tool == quader::ViewportToolId::Scale) {
        start = frame->info.screen_pivot;
        update = start + quader::QVec2{120.0f, 0.0f};
      } else {
        const quader_editor_core::TransformGizmoAxisPrimitive *axis =
            find_axis(*frame, quader_editor_core::TransformAxis::X);
        ASSERT_NE(axis, nullptr);
        start = axis->screen_tip;
        update = start + quader::QVec2{120.0f, 0.0f};
      }

      const quader::TransformComponent before_transform =
          *harness.session.scene().transform(light.value().id);
      press_update_release_gizmo(harness, start, update);
      const quader::TransformComponent after_transform =
          *harness.session.scene().transform(light.value().id);

      if (tool_case.tool == quader::ViewportToolId::Move) {
        EXPECT_GT(length(after_transform.position - before_transform.position),
                  0.001f);
      } else if (tool_case.tool == quader::ViewportToolId::Rotate) {
        EXPECT_GT(length(after_transform.rotation_euler -
                         before_transform.rotation_euler),
                  0.001f);
      } else {
        EXPECT_GT(length(after_transform.scale - before_transform.scale),
                  0.001f);
      }
      EXPECT_EQ(harness.editor.selection().summary().active_kind,
                quader::QuaderActiveSelectionKind::Light);

      ASSERT_TRUE(harness.editor.history().can_undo());
      const quader::QuaderApiVoidResult undo =
          harness.editor.commands().execute(
              quader::quader_command::kEditUndo);
      ASSERT_TRUE(undo.ok()) << undo.error_message();
      EXPECT_TRUE(undo.changed());
      ASSERT_NE(harness.session.scene().transform(light.value().id), nullptr);
      expect_transform_near(
          *harness.session.scene().transform(light.value().id),
          before_transform);
    }
}

TEST(ViewportBoxOverlayBuilderTests,
     EmitsNothingThenOneLineThenTheDashedFootprintPerimeterWithoutFill) {
    quader::ViewportOverlayCamera camera;
    camera.eye = {0.0f, 10.0f, 0.0f};
    camera.forward = {0.0f, -1.0f, 0.0f};
    camera.right = {1.0f, 0.0f, 0.0f};
    camera.up = {0.0f, 0.0f, 1.0f};
    camera.viewport_size = {800.0f, 600.0f};
    camera.projection = quader::ViewportOverlayProjection::Orthographic;
    camera.orthographic_size = 8.0f;

    quader::ModelingBoxPreview idle;
    idle.ok = true;
    idle.valid = false;
    idle.drag_state = quader::ModelingBoxDragState::Idle;

    quader::ViewportOverlayFrame idle_overlay;
    quader::ViewportToolOverlayProjection::append_box(idle_overlay, camera,
                                                      idle);
    EXPECT_TRUE(idle_overlay.lines.empty());
    EXPECT_TRUE(idle_overlay.triangles.empty());

    quader::ModelingBoxPreview line = idle;
    line.drag_state = quader::ModelingBoxDragState::Footprint;
    line.footprint_count = 2u;
    line.footprint[0] = {-1.0f, 0.0f, 0.0f};
    line.footprint[1] = {1.0f, 0.0f, 0.0f};

    quader::ViewportOverlayFrame line_overlay;
    quader::ViewportToolOverlayProjection::append_box(line_overlay, camera,
                                                      line);
    ASSERT_GT(line_overlay.lines.size(), 1u)
        << "a noncommittable one-axis footprint must remain visible";
    EXPECT_TRUE(line_overlay.triangles.empty());
    float line_segment_length = 0.0f;
    float previous_segment_end = -std::numeric_limits<float>::infinity();
    for (const quader::ViewportOverlayLine &segment : line_overlay.lines) {
      EXPECT_FLOAT_EQ(segment.start.y, 0.0f);
      EXPECT_FLOAT_EQ(segment.start.z, 0.0f);
      EXPECT_FLOAT_EQ(segment.end.y, 0.0f);
      EXPECT_FLOAT_EQ(segment.end.z, 0.0f);
      EXPECT_GE(segment.start.x, -1.0f);
      EXPECT_LE(segment.end.x, 1.0f);
      EXPECT_LT(segment.start.x, segment.end.x);
      EXPECT_GE(segment.start.x, previous_segment_end)
          << "one footprint edge must produce one ordered dash stream";
      previous_segment_end = segment.end.x;
      line_segment_length += segment.end.x - segment.start.x;
    }
    EXPECT_GT(line_segment_length, 0.0f);
    EXPECT_LE(line_segment_length, 2.0f)
        << "the single footprint edge must not be emitted twice";

    quader::ModelingBoxPreview footprint = line;
    footprint.valid = true;
    footprint.footprint_count = 4u;
    footprint.footprint = {{
            {-1.0f, 0.0f, -1.0f},
            {1.0f, 0.0f, -1.0f},
            {1.0f, 0.0f, 1.0f},
            {-1.0f, 0.0f, 1.0f},
    }};

    quader::ViewportOverlayFrame footprint_overlay;
    quader::ViewportToolOverlayProjection::append_box(footprint_overlay, camera,
                                                      footprint);
    ASSERT_EQ(footprint_overlay.lines.size(), line_overlay.lines.size() * 4u);
    EXPECT_TRUE(footprint_overlay.triangles.empty());
    std::array<std::size_t, 4> perimeter_segment_counts{};
    for (const quader::ViewportOverlayLine &line : footprint_overlay.lines) {
      EXPECT_EQ(line.layer, quader::ViewportOverlayLayer::ToolPreviewLine);
      EXPECT_EQ(line.color, quader::kOverlayToolBoxPreviewLineColor);
      EXPECT_FLOAT_EQ(line.width_pixels, 2.0f);
      EXPECT_EQ(line.style_role,
                quader::ViewportOverlayStyleRole::ExplicitColor);
      if (line.start.z == -1.0f && line.end.z == -1.0f) {
        ++perimeter_segment_counts[0];
      } else if (line.start.x == 1.0f && line.end.x == 1.0f) {
        ++perimeter_segment_counts[1];
      } else if (line.start.z == 1.0f && line.end.z == 1.0f) {
        ++perimeter_segment_counts[2];
      } else if (line.start.x == -1.0f && line.end.x == -1.0f) {
        ++perimeter_segment_counts[3];
      } else {
        ADD_FAILURE() << "footprint segment escaped the rectangle perimeter";
      }
    }
    for (const std::size_t segment_count : perimeter_segment_counts) {
      EXPECT_GT(segment_count, 0u);
    }

    quader::ModelingBoxPreview box = footprint;
    box.drag_state = quader::ModelingBoxDragState::Height;
    box.corner_count = 8u;
    box.corners = {{
            {-1.0f, 0.0f, -1.0f},
            {1.0f, 0.0f, -1.0f},
            {1.0f, 0.0f, 1.0f},
            {-1.0f, 0.0f, 1.0f},
            {-1.0f, 2.0f, -1.0f},
            {1.0f, 2.0f, -1.0f},
            {1.0f, 2.0f, 1.0f},
            {-1.0f, 2.0f, 1.0f},
    }};

    quader::ViewportOverlayFrame box_overlay;
    quader::ViewportToolOverlayProjection::append_box(box_overlay, camera, box);
    EXPECT_EQ(box_overlay.lines.size(), footprint_overlay.lines.size());
    EXPECT_TRUE(box_overlay.triangles.empty());
}

TEST(ViewportToolOverlaySourceTests,
     CameraAndFixedSemanticStateInvalidateBeforeLazyCompilation) {
    quader::ViewportCameraController viewport_camera;
    viewport_camera.set_viewport_size(800.0f, 600.0f);
    viewport_camera.focus_origin(8.0f);
    quader::ViewportOverlayCamera overlay_camera =
        quader::ViewportCameraAdapter::overlay_camera(viewport_camera,
                                                      {800.0f, 600.0f});
    quader::ModelingBoxPreview box;
    box.ok = true;
    box.valid = true;
    box.drag_state = quader::ModelingBoxDragState::Footprint;
    box.footprint_count = 4u;
    box.footprint = {{
        {-1.0f, 0.0f, -1.0f},
        {1.0f, 0.0f, -1.0f},
        {1.0f, 0.0f, 1.0f},
        {-1.0f, 0.0f, 1.0f},
    }};
    quader::ViewportToolOverlaySourceInputs inputs{
        .source_identity = 42u,
        .overlay_camera = &overlay_camera,
        .viewport_camera = &viewport_camera,
        .box_preview = &box,
        .interaction_pane = true,
        .tool_overlay_visible = true,
    };

    const quader::ViewportToolOverlaySource initial{inputs};
    const quader::ViewportToolOverlaySource identical{inputs};
    EXPECT_EQ(identical.source_stamp(), initial.source_stamp());
    const quader::ViewportOverlayFrame initial_frame =
        initial.build_dynamic_tool_overlay_frame();
    EXPECT_FALSE(initial_frame.lines.empty());

    inputs.raster_scale = 2.0f;
    const quader::ViewportToolOverlaySource scaled{inputs};
    const quader::ViewportOverlayFrame scaled_frame =
        scaled.build_dynamic_tool_overlay_frame();
    EXPECT_NE(scaled.source_stamp(), initial.source_stamp());
    EXPECT_LT(scaled_frame.lines.size(), initial_frame.lines.size())
        << "larger dashes should rebuild into fewer semantic segments";
    inputs.raster_scale = 1.0f;

    overlay_camera.eye.x += 1.0f;
    const quader::ViewportToolOverlaySource camera_changed{inputs};
    EXPECT_NE(camera_changed.source_stamp(), initial.source_stamp());

    box.footprint[0].x -= 1.0f;
    const quader::ViewportToolOverlaySource box_changed{inputs};
    EXPECT_NE(box_changed.source_stamp(), camera_changed.source_stamp());

    inputs.tool_overlay_visible = false;
    const quader::ViewportToolOverlaySource empty_before_camera{inputs};
    overlay_camera.eye.x += 1.0f;
    const quader::ViewportToolOverlaySource empty_after_camera{inputs};
    EXPECT_EQ(empty_after_camera.source_stamp(),
              empty_before_camera.source_stamp())
        << "camera-only updates must not rebuild an empty semantic source";

    quader_editor_core::TransformGizmoFrame gizmo;
    gizmo.info.ok = true;
    gizmo.info.tool = quader_editor_core::ViewportInteractionTool::Move;
    inputs.transform_gizmo = &gizmo;
    inputs.gizmos_visible = true;
    inputs.revisions.scene_transform = 5u;
    const quader::ViewportToolOverlaySource gizmo_before_center_resize{inputs};
    gizmo.info.move_center_radius_pixels += 1.0;
    const quader::ViewportToolOverlaySource gizmo_after_center_resize{inputs};
    EXPECT_NE(gizmo_after_center_resize.source_stamp(),
              gizmo_before_center_resize.source_stamp())
        << "center handle size must invalidate retained overlay geometry";

    const quader::ViewportToolOverlaySource gizmo_before_owner_change{inputs};
    ++inputs.revisions.scene_transform;
    const quader::ViewportToolOverlaySource gizmo_after_owner_change{inputs};
    EXPECT_NE(gizmo_after_owner_change.source_stamp(),
              gizmo_before_owner_change.source_stamp())
        << "same-sized gizmo vectors rely on their fixed owner revisions";

    std::vector<quader_editor_core::SelectionOverlayPoint> snap_points(2u);
    snap_points[0].position = {1.0f, 2.0f, 3.0f};
    snap_points[1].position = {4.0f, 5.0f, 6.0f};
    inputs.gizmos_visible = false;
    inputs.transform_gizmo = nullptr;
    inputs.overlays_visible = true;
    inputs.transform_snap_points = &snap_points;
    const quader::ViewportToolOverlaySource snap_before{inputs};
    snap_points[1].position.x += 1.0f;
    const quader::ViewportToolOverlaySource snap_after{inputs};
    EXPECT_NE(snap_after.source_stamp(), snap_before.source_stamp());
}

TEST(ViewportInteractionScaleTests,
     TransformGizmoGeometryAndPickingUseTheSamePixelScale) {
  ViewportInteractionHarness harness;
  const quader::QuaderModelingObjectRef box = create_unit_box(harness);
  ASSERT_TRUE(harness.editor.selection().select_object(box).ok());
  ASSERT_TRUE(
      harness.editor.tools().activate(quader::ViewportToolId::Move).ok());
  harness.router.set_active_tool(quader::ViewportToolId::Move);

  quader::ViewportInteractionFrame unit = harness.frame();
  unit.viewport_pixel_scale = 1.0f;
  (void)harness.controller.handle_before_active_tool(harness.editor, unit);
  const quader_editor_core::TransformGizmoFrame *unit_gizmo =
      harness.controller.transform_gizmo_frame();
  ASSERT_NE(unit_gizmo, nullptr);
  const quader_editor_core::TransformGizmoAxisPrimitive *unit_x =
      find_axis(*unit_gizmo, quader_editor_core::TransformAxis::X);
  ASSERT_NE(unit_x, nullptr);
  const quader::QVec2 unit_delta =
      unit_x->screen_tip - unit_gizmo->info.screen_pivot;
  const float unit_extent = std::hypot(unit_delta.x, unit_delta.y);
  const double unit_center_radius =
      unit_gizmo->info.move_center_radius_pixels;

  quader::ViewportInteractionFrame scaled = harness.frame();
  scaled.viewport_pixel_scale = 2.0f;
  (void)harness.controller.handle_before_active_tool(harness.editor, scaled);
  const quader_editor_core::TransformGizmoFrame *scaled_gizmo =
      harness.controller.transform_gizmo_frame();
  ASSERT_NE(scaled_gizmo, nullptr);
  const quader_editor_core::TransformGizmoAxisPrimitive *scaled_x =
      find_axis(*scaled_gizmo, quader_editor_core::TransformAxis::X);
  ASSERT_NE(scaled_x, nullptr);
  const quader::QVec2 scaled_delta =
      scaled_x->screen_tip - scaled_gizmo->info.screen_pivot;
  const float scaled_extent = std::hypot(scaled_delta.x, scaled_delta.y);
  EXPECT_NEAR(scaled_extent, unit_extent * 2.0f, 0.1f);
  EXPECT_DOUBLE_EQ(scaled_gizmo->info.move_center_radius_pixels,
                   unit_center_radius * 2.0);

  const quader::QVec2 scaled_tip = scaled_x->screen_tip;
  harness.input.mouse_local = scaled_tip;
  quader::ViewportInteractionFrame unit_hover = harness.frame();
  unit_hover.viewport_pixel_scale = 1.0f;
  (void)harness.controller.handle_before_active_tool(harness.editor,
                                                     unit_hover);
  ASSERT_NE(harness.controller.transform_gizmo_frame(), nullptr);
  EXPECT_EQ(harness.controller.transform_gizmo_frame()->info.hover_axis,
            quader_editor_core::TransformAxis::None);

  quader::ViewportInteractionFrame scaled_hover = harness.frame();
  scaled_hover.viewport_pixel_scale = 2.0f;
  (void)harness.controller.handle_before_active_tool(harness.editor,
                                                     scaled_hover);
  ASSERT_NE(harness.controller.transform_gizmo_frame(), nullptr);
  EXPECT_EQ(harness.controller.transform_gizmo_frame()->info.hover_axis,
            quader_editor_core::TransformAxis::X);
}

TEST(ViewportInteractionScaleTests,
     TransformVertexSnapUsesInterfaceFactorAndReevaluatesStationaryDrag) {
  ViewportInteractionHarness harness;
  harness.camera.view_axis(quader::ViewportViewAxis::Z, 1);
  (void)harness.settings.set_bool_value(
      quader::ViewportBoolSettingId::SnappingEnabled, true);
  ASSERT_TRUE(harness.settings.set_bool_value(
      quader::ViewportBoolSettingId::GridSnap, false));
  (void)harness.settings.set_float_value(
      quader::ViewportFloatSettingId::PickVertexRadius, 0.3f);
  ASSERT_TRUE(harness.settings.bool_value(
      quader::ViewportBoolSettingId::SnappingEnabled));
  ASSERT_FLOAT_EQ(harness.settings.float_value(
                      quader::ViewportFloatSettingId::PickVertexRadius),
                  0.3f);
  harness.input.transform_vertex_snap_down = true;
  harness.input.device_pixel_ratio = 2.0f;

  const quader::QuaderModelingObjectRef source =
      create_box_at(harness, {-3.0f, 0.0f, 0.0f});
  const quader::QuaderModelingObjectRef target =
      create_box_at(harness, {3.0f, 2.0f, 2.0f});
  const auto target_vertex =
      vertex_snap_target_at(harness, {2.0f, 1.0f, 3.0f});
  ASSERT_TRUE(target_vertex.has_value());
  ASSERT_EQ(target_vertex->modeling_object_id, target.modeling_object_id);
  ASSERT_TRUE(harness.editor.selection().select_object(source).ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
  ASSERT_TRUE(
      harness.editor.tools().activate(quader::ViewportToolId::Move).ok());
  harness.router.set_active_tool(quader::ViewportToolId::Move);

  quader::ViewportInteractionFrame warmup = harness.frame();
  warmup.viewport_pixel_scale = 2.0f;
  (void)harness.controller.handle_before_active_tool(harness.editor, warmup);
  const quader_editor_core::TransformGizmoFrame *gizmo =
      harness.controller.transform_gizmo_frame();
  ASSERT_NE(gizmo, nullptr);
  const auto *axis = find_axis(*gizmo, quader_editor_core::TransformAxis::X);
  ASSERT_NE(axis, nullptr);
  const quader::QVec2 start = axis_screen_point(*axis, 0.25f);

  harness.input.mouse_local = start;
  harness.input.mouse_screen = start;
  quader::ViewportInteractionFrame press =
      harness.frame(true, true, false, start);
  press.viewport_pixel_scale = 2.0f;
  ASSERT_TRUE(harness.controller
                  .handle_before_active_tool(harness.editor, press)
                  .gizmo_consumed);

  const quader::ViewportScreenPoint scaled_only_probe =
      harness.camera.project_world_point(target_vertex->world_position +
                                         quader::QVec3{0.45f, 0.0f, 0.0f});
  ASSERT_TRUE(scaled_only_probe.visible);
  harness.input.mouse_local = scaled_only_probe.position;
  harness.input.mouse_screen = scaled_only_probe.position;

  quader::ViewportInteractionFrame authored_radius =
      harness.frame(false, true, false, start);
  authored_radius.viewport_pixel_scale = 2.0f;
  ASSERT_TRUE(harness.controller
                  .handle_before_active_tool(harness.editor, authored_radius)
                  .gizmo_consumed);
  ASSERT_TRUE(harness.controller.dragging());
  const std::optional<quader::QVec3> unscaled_pivot =
      harness.editor.query().selection_transform_pivot();
  ASSERT_TRUE(unscaled_pivot.has_value());
  EXPECT_GT(std::abs(unscaled_pivot->x - target_vertex->world_position.x),
            0.1f)
      << "combined 2x scale at 2x DPR must retain the authored world radius";

  quader::ViewportInteractionFrame doubled_interface_scale =
      harness.frame(false, true, false, start);
  doubled_interface_scale.viewport_pixel_scale = 4.0f;
  ASSERT_TRUE(harness.controller
                  .handle_before_active_tool(harness.editor,
                                             doubled_interface_scale)
                  .gizmo_consumed);
  const std::optional<quader::QVec3> scaled_pivot =
      harness.editor.query().selection_transform_pivot();
  ASSERT_TRUE(scaled_pivot.has_value());
  EXPECT_NEAR(scaled_pivot->x, target_vertex->world_position.x, 0.001f);

  const std::uint64_t revision_after_interface_scale =
      harness.session.modeling().content_revision();
  quader::ViewportInteractionFrame cached_scale =
      harness.frame(false, true, false, start);
  cached_scale.viewport_pixel_scale = 4.0f;
  ASSERT_TRUE(harness.controller
                  .handle_before_active_tool(harness.editor, cached_scale)
                  .gizmo_consumed);
  EXPECT_EQ(harness.session.modeling().content_revision(),
            revision_after_interface_scale)
      << "an unchanged effective radius must return to the retained fast path";
  const std::optional<quader::QVec3> retained_pivot =
      harness.editor.query().selection_transform_pivot();
  ASSERT_TRUE(retained_pivot.has_value());
  expect_vec3_near(*retained_pivot, *scaled_pivot);

  quader::ViewportInteractionFrame release =
      harness.frame(false, false, true, start);
  release.viewport_pixel_scale = 4.0f;
  EXPECT_TRUE(harness.controller
                  .handle_before_active_tool(harness.editor, release)
                  .gizmo_consumed);
}

TEST(ViewportInteractionScaleTests,
     PointLightsAndNativeHandleHitTargetsScaleWithoutMovingTheirAnchors) {
  ViewportInteractionHarness harness;
  const auto created = harness.editor.scene().create_point_light();
  ASSERT_TRUE(created.ok()) << created.error_message();
  const auto &local_visibility =
      harness.session.viewport_workspace().local_visibility_snapshot();
  const std::vector<quader::ViewportSceneBillboardPickCandidate> unit =
      quader::ViewportSceneProjection::point_light_pick_candidates(
          harness.session.scene(), local_visibility, 1.0f);
  const std::vector<quader::ViewportSceneBillboardPickCandidate> scaled =
      quader::ViewportSceneProjection::point_light_pick_candidates(
          harness.session.scene(), local_visibility, 2.0f);
  ASSERT_EQ(unit.size(), 1u);
  ASSERT_EQ(scaled.size(), 1u);
  EXPECT_FLOAT_EQ(scaled.front().position.x, unit.front().position.x);
  EXPECT_FLOAT_EQ(scaled.front().position.y, unit.front().position.y);
  EXPECT_FLOAT_EQ(scaled.front().position.z, unit.front().position.z);
  EXPECT_FLOAT_EQ(scaled.front().size_pixels, unit.front().size_pixels * 2.0f);

  quader::PivotHandleState pivot{
      .visible = true,
      .world_position = unit.front().position,
  };
  const quader::ViewportScreenPoint screen =
      harness.camera.project_world_point(pivot.world_position);
  ASSERT_TRUE(screen.visible);
  const quader::QVec2 outside_unit_radius =
      screen.position + quader::QVec2{24.0f, 0.0f};
  EXPECT_FALSE(quader::ViewportNativeToolInteraction::pivot_handle_hit(
      harness.camera, pivot, outside_unit_radius, 1.0f));
  EXPECT_TRUE(quader::ViewportNativeToolInteraction::pivot_handle_hit(
      harness.camera, pivot, outside_unit_radius, 2.0f));
  EXPECT_FALSE(quader::ViewportNativeToolInteraction::pivot_handle_hit(
      harness.camera, pivot, outside_unit_radius,
      std::numeric_limits<float>::quiet_NaN()));
}

TEST(ViewportInteractionScaleTests,
     WorldPickRadiiUseInterfaceFactorWithoutApplyingMonitorDprTwice) {
  constexpr double kAuthoredRadius = 0.125;
  EXPECT_FLOAT_EQ(quader::viewport_user_interface_scale(2.0f, 2.0f),
                  1.0f);
  EXPECT_DOUBLE_EQ(quader::viewport_scaled_world_pick_radius(
                       kAuthoredRadius, 2.0f, 2.0f),
                   kAuthoredRadius);
  EXPECT_DOUBLE_EQ(quader::viewport_scaled_world_pick_radius(
                       kAuthoredRadius, 4.0f, 2.0f),
                   kAuthoredRadius * 2.0);
  EXPECT_DOUBLE_EQ(quader::viewport_scaled_world_pick_radius(
                       kAuthoredRadius, 6.0f, 2.0f),
                   kAuthoredRadius * 3.0);
  EXPECT_DOUBLE_EQ(quader::viewport_scaled_world_pick_radius(
                       kAuthoredRadius,
                       std::numeric_limits<float>::quiet_NaN(), 2.0f),
                   kAuthoredRadius);
}

TEST(ViewportSelectionMarqueeOverlayBuilderTests, EmitsFillAndRuleSpecificEdges) {
    quader::ViewportOverlayCamera camera;
    camera.eye = {0.0f, 0.0f, 10.0f};
    camera.forward = {0.0f, 0.0f, -1.0f};
    camera.right = {1.0f, 0.0f, 0.0f};
    camera.up = {0.0f, 1.0f, 0.0f};
    camera.viewport_size = {800.0f, 600.0f};
    camera.projection = quader::ViewportOverlayProjection::Orthographic;
    camera.orthographic_size = 12.0f;

    quader::ViewportOverlayFrame inactive_overlay;
    quader::ViewportSelectionMarquee inactive;
    quader::ViewportToolOverlayProjection::append_selection_marquee(
        inactive_overlay, camera, inactive);
    EXPECT_TRUE(inactive_overlay.lines.empty());
    EXPECT_TRUE(inactive_overlay.triangles.empty());

    quader::ViewportSelectionMarquee contain;
    contain.active = true;
    contain.start = {100.0f, 120.0f};
    contain.end = {180.0f, 160.0f};
    contain.rule = quader::ModelingBoxSelectionRule::Contain;

    quader::ViewportOverlayFrame contain_overlay;
    quader::ViewportToolOverlayProjection::append_selection_marquee(
        contain_overlay, camera, contain);
    ASSERT_EQ(contain_overlay.triangles.size(), 2u);
    ASSERT_EQ(contain_overlay.lines.size(), 4u);
    for (const quader::ViewportOverlayTriangle &triangle :
         contain_overlay.triangles) {
      EXPECT_EQ(triangle.layer, quader::ViewportOverlayLayer::ToolPreviewFill);
      EXPECT_EQ(triangle.color, quader::kOverlaySelectionMarqueeFillColor);
      EXPECT_EQ(triangle.style_role,
                quader::ViewportOverlayStyleRole::ExplicitColor);
      EXPECT_EQ(triangle.source, quader::ViewportOverlaySource::Tool);
    }
    for (const quader::ViewportOverlayLine &line : contain_overlay.lines) {
      EXPECT_EQ(line.layer, quader::ViewportOverlayLayer::ToolPreviewLine);
      EXPECT_EQ(line.color, quader::kOverlaySelectionMarqueeLineColor);
      EXPECT_FLOAT_EQ(line.width_pixels, 1.5f);
      EXPECT_EQ(line.style_role,
                quader::ViewportOverlayStyleRole::ExplicitColor);
      EXPECT_EQ(line.source, quader::ViewportOverlaySource::Tool);
    }

    quader::ViewportSelectionMarquee crossing = contain;
    crossing.rule = quader::ModelingBoxSelectionRule::Crossing;
    quader::ViewportOverlayFrame crossing_overlay;
    quader::ViewportToolOverlayProjection::append_selection_marquee(
        crossing_overlay, camera, crossing);
    EXPECT_EQ(crossing_overlay.triangles.size(), 2u);
    EXPECT_GT(crossing_overlay.lines.size(), contain_overlay.lines.size());
}

TEST(ViewportHostInteractionPolicyTests,
     FocusFollowsHoverWithoutStealingNonViewportKeyboardFocus) {
  using quader::ViewportPaneFocusRequest;
  using quader::ViewportPaneFocusTrigger;
  using quader::viewport_pane_focus_decision;

  const auto passive_hover = viewport_pane_focus_decision({
      .trigger = ViewportPaneFocusTrigger::Hover,
  });
  EXPECT_TRUE(passive_hover.focus_workspace_pane);
  EXPECT_FALSE(passive_hover.focus_native_surface);

  const auto viewport_hover = viewport_pane_focus_decision({
      .trigger = ViewportPaneFocusTrigger::Hover,
      .viewport_keyboard_focus = true,
  });
  EXPECT_TRUE(viewport_hover.focus_workspace_pane);
  EXPECT_TRUE(viewport_hover.focus_native_surface);

  const auto native_focus_in = viewport_pane_focus_decision({
      .trigger = ViewportPaneFocusTrigger::FocusIn,
  });
  EXPECT_TRUE(native_focus_in.focus_workspace_pane);
  EXPECT_FALSE(native_focus_in.focus_native_surface);

  for (const ViewportPaneFocusTrigger trigger : {
           ViewportPaneFocusTrigger::PointerPress,
           ViewportPaneFocusTrigger::Wheel,
           ViewportPaneFocusTrigger::Programmatic,
       }) {
    const auto decision = viewport_pane_focus_decision({.trigger = trigger});
    EXPECT_TRUE(decision.focus_workspace_pane);
    EXPECT_TRUE(decision.focus_native_surface);
  }

  const auto rejected_other_pane =
      viewport_pane_focus_decision(ViewportPaneFocusRequest{
          .trigger = ViewportPaneFocusTrigger::PointerPress,
          .interaction_locked = true,
          .requested_pane_is_interaction_owner = false,
      });
  EXPECT_FALSE(rejected_other_pane.focus_workspace_pane);
  EXPECT_FALSE(rejected_other_pane.focus_native_surface);

  const auto retained_owner =
      viewport_pane_focus_decision(ViewportPaneFocusRequest{
          .trigger = ViewportPaneFocusTrigger::Wheel,
          .interaction_locked = true,
          .requested_pane_is_interaction_owner = true,
      });
  EXPECT_TRUE(retained_owner.focus_workspace_pane);
  EXPECT_TRUE(retained_owner.focus_native_surface);
}

TEST(ViewportHostInteractionPolicyTests,
     OutsidePointerCancelsModalToolsWithLegacyExceptions) {
  using quader::ViewportExternalPointerCancelRequest;
  using quader::should_cancel_modal_tool_for_external_pointer;

  ViewportExternalPointerCancelRequest request{
      .external_pointer_event = true,
      .modal_tool_owns_viewport = true,
  };
  EXPECT_TRUE(should_cancel_modal_tool_for_external_pointer(request));

  request.external_pointer_event = false;
  EXPECT_FALSE(should_cancel_modal_tool_for_external_pointer(request));
  request.external_pointer_event = true;
  request.modal_tool_owns_viewport = false;
  EXPECT_FALSE(should_cancel_modal_tool_for_external_pointer(request));
  request.modal_tool_owns_viewport = true;
  request.persistent_native_tool_active = true;
  EXPECT_FALSE(should_cancel_modal_tool_for_external_pointer(request));
  request.persistent_native_tool_active = false;
  request.navigation_active = true;
  EXPECT_FALSE(should_cancel_modal_tool_for_external_pointer(request));

  request.navigation_active = false;
  request.left_button_release_only = true;
  request.insert_edge_loop_dragging = true;
  EXPECT_FALSE(should_cancel_modal_tool_for_external_pointer(request));
  request.insert_edge_loop_dragging = false;
  request.cut_dragging = true;
  EXPECT_FALSE(should_cancel_modal_tool_for_external_pointer(request));
  request.cut_dragging = false;
  request.mirror_dragging = true;
  EXPECT_FALSE(should_cancel_modal_tool_for_external_pointer(request));
  request.mirror_dragging = false;
  EXPECT_TRUE(should_cancel_modal_tool_for_external_pointer(request));

  request.left_button_release_only = false;
  request.insert_edge_loop_dragging = true;
  EXPECT_TRUE(should_cancel_modal_tool_for_external_pointer(request));
}

TEST(ViewportHostInteractionPolicyTests,
     RepeatedShortcutDoesNotRestartAnAlreadyActiveEditorTool) {
  using quader::should_dispatch_viewport_tool_activation;

  EXPECT_FALSE(should_dispatch_viewport_tool_activation({}));
  EXPECT_TRUE(should_dispatch_viewport_tool_activation({
      .activation_requested = true,
      .tool_changed = true,
      .editor_tool_matches_requested = true,
  }));
  EXPECT_TRUE(should_dispatch_viewport_tool_activation({
      .activation_requested = true,
      .tool_changed = false,
      .editor_tool_matches_requested = false,
  }));
  EXPECT_FALSE(should_dispatch_viewport_tool_activation({
      .activation_requested = true,
      .tool_changed = false,
      .editor_tool_matches_requested = true,
  }));
}

TEST(ViewportHostInteractionPolicyTests,
     OrderedEdgesDispatchEverySameFrameKeyboardPress) {
  const std::vector<quader::QuaderInputChord> pressed{
      {.device = quader::QuaderInputDevice::Keyboard,
       .key = quader::QuaderKeyCode::W},
      {.device = quader::QuaderInputDevice::Keyboard,
       .key = quader::QuaderKeyCode::E},
  };
  std::vector<quader::QuaderKeyCode> dispatched;

  quader::dispatch_ordered_viewport_input_edges(
      pressed, {},
      [&](const quader::QuaderInputChord &chord,
          quader::ViewportInputEdgeKind kind, double wheel_step_delta) {
        EXPECT_EQ(kind, quader::ViewportInputEdgeKind::Pressed);
        EXPECT_DOUBLE_EQ(wheel_step_delta, 0.0);
        dispatched.push_back(chord.key);
        return true;
      });

  EXPECT_EQ(dispatched,
            (std::vector{quader::QuaderKeyCode::W,
                         quader::QuaderKeyCode::E}));
}

TEST(ViewportHostInteractionPolicyTests,
     StatefulDispatchPrecedesStatelessActionsAndPendingOwnsMismatch) {
  const quader::QuaderInputChord continuation{
      .device = quader::QuaderInputDevice::Keyboard,
      .key = quader::QuaderKeyCode::D,
  };
  int stateless_action_attempts = 0;
  int stateful_dispatches = 0;

  EXPECT_TRUE(quader::dispatch_viewport_input_edge(
      continuation, quader::ViewportInputEdgeKind::Pressed,
      []() { return false; },
      [&](const quader::QuaderInputChord &,
          quader::ViewportInputEdgeKind) {
        ++stateless_action_attempts;
        return true;
      },
      [&](const quader::QuaderInputChord &chord,
          quader::ViewportInputEdgeKind kind) {
        EXPECT_EQ(chord, continuation);
        EXPECT_EQ(kind, quader::ViewportInputEdgeKind::Pressed);
        ++stateful_dispatches;
        return true;
      }));
  EXPECT_EQ(stateless_action_attempts, 0);
  EXPECT_EQ(stateful_dispatches, 1);

  stateless_action_attempts = 0;
  stateful_dispatches = 0;
  EXPECT_TRUE(quader::dispatch_viewport_input_edge(
      continuation, quader::ViewportInputEdgeKind::Pressed,
      []() { return true; },
      [&](const quader::QuaderInputChord &,
          quader::ViewportInputEdgeKind) {
        ++stateless_action_attempts;
        return true;
      },
      [&](const quader::QuaderInputChord &chord,
          quader::ViewportInputEdgeKind kind) {
        EXPECT_EQ(chord, continuation);
        EXPECT_EQ(kind, quader::ViewportInputEdgeKind::Pressed);
        ++stateful_dispatches;
        return false;
      }));
  EXPECT_EQ(stateless_action_attempts, 0);
  EXPECT_EQ(stateful_dispatches, 1);

  stateless_action_attempts = 0;
  stateful_dispatches = 0;
  EXPECT_TRUE(quader::dispatch_viewport_input_edge(
      continuation, quader::ViewportInputEdgeKind::Pressed,
      []() { return false; },
      [&](const quader::QuaderInputChord &,
          quader::ViewportInputEdgeKind) {
        ++stateless_action_attempts;
        return true;
      },
      [&](const quader::QuaderInputChord &,
          quader::ViewportInputEdgeKind) {
        ++stateful_dispatches;
        return false;
      }));
  EXPECT_EQ(stateless_action_attempts, 1);
  EXPECT_EQ(stateful_dispatches, 1);
}

TEST(ViewportHostInteractionPolicyTests,
     DirectionalWheelBindingPrecedesGenericWheelFallback) {
  const quader::QuaderInputChord generic{
      .device = quader::QuaderInputDevice::Mouse,
      .gesture = quader::QuaderInputGesture::Wheel,
  };
  const quader::QuaderInputChord up{
      .device = quader::QuaderInputDevice::Mouse,
      .gesture = quader::QuaderInputGesture::WheelUp,
  };
  const std::vector<quader::QuaderWheelInputEdge> wheel{
      {.chord = generic, .step_delta = 2.0},
      {.chord = up, .step_delta = 2.0},
  };
  std::vector<quader::QuaderInputGesture> dispatched;

  quader::dispatch_ordered_viewport_input_edges(
      {}, wheel,
      [&](const quader::QuaderInputChord &chord,
          quader::ViewportInputEdgeKind kind, double wheel_step_delta) {
        EXPECT_EQ(kind, quader::ViewportInputEdgeKind::Wheel);
        EXPECT_DOUBLE_EQ(wheel_step_delta, 2.0);
        dispatched.push_back(chord.gesture);
        return chord.gesture == quader::QuaderInputGesture::WheelUp;
      });
  EXPECT_EQ(dispatched,
            (std::vector{quader::QuaderInputGesture::WheelUp}));

  dispatched.clear();
  quader::dispatch_ordered_viewport_input_edges(
      {}, wheel,
      [&](const quader::QuaderInputChord &chord,
          quader::ViewportInputEdgeKind, double wheel_step_delta) {
        EXPECT_DOUBLE_EQ(wheel_step_delta, 2.0);
        dispatched.push_back(chord.gesture);
        return chord.gesture == quader::QuaderInputGesture::Wheel;
      });
  EXPECT_EQ(dispatched,
            (std::vector{quader::QuaderInputGesture::WheelUp,
                         quader::QuaderInputGesture::Wheel}));
}

TEST(ViewportHostInteractionPolicyTests,
     OrderedWheelDispatchCompletesEachPhysicalEventAfterFallback) {
  const quader::QuaderInputChord generic{
      .device = quader::QuaderInputDevice::Mouse,
      .gesture = quader::QuaderInputGesture::Wheel,
  };
  const quader::QuaderInputChord up{
      .device = quader::QuaderInputDevice::Mouse,
      .gesture = quader::QuaderInputGesture::WheelUp,
  };
  const quader::QuaderInputChord down{
      .device = quader::QuaderInputDevice::Mouse,
      .gesture = quader::QuaderInputGesture::WheelDown,
  };
  const std::vector<quader::QuaderWheelInputEdge> wheel{
      {.chord = generic, .step_delta = 0.6},
      {.chord = up, .step_delta = 0.6},
      {.chord = generic, .step_delta = -0.4},
      {.chord = down, .step_delta = -0.4},
  };
  std::vector<quader::QuaderInputGesture> dispatched;
  std::vector<double> completed_deltas;

  quader::dispatch_ordered_viewport_input_edges(
      {}, wheel,
      [&](const quader::QuaderInputChord &chord,
          quader::ViewportInputEdgeKind kind, double) {
        EXPECT_EQ(kind, quader::ViewportInputEdgeKind::Wheel);
        dispatched.push_back(chord.gesture);
        // Models a user remap to generic Wheel: each directional candidate
        // falls through before the generic chord handles the same event.
        return chord.gesture == quader::QuaderInputGesture::Wheel;
      },
      [&](std::span<const quader::QuaderWheelInputEdge> physical_event) {
        ASSERT_EQ(physical_event.size(), 2u);
        EXPECT_EQ(physical_event[0].chord.gesture,
                  quader::QuaderInputGesture::Wheel);
        completed_deltas.push_back(physical_event[0].step_delta);
      });

  EXPECT_EQ(dispatched,
            (std::vector{quader::QuaderInputGesture::WheelUp,
                         quader::QuaderInputGesture::Wheel,
                         quader::QuaderInputGesture::WheelDown,
                         quader::QuaderInputGesture::Wheel}));
  EXPECT_EQ(completed_deltas, (std::vector<double>{0.6, -0.4}));
}

TEST(ViewportHostInteractionPolicyTests,
     DiscreteWheelStepsRetainFractionalSignedMotion) {
  double remainder = 0.0;
  EXPECT_EQ(quader::accumulate_discrete_wheel_steps(0.4, remainder), 0);
  EXPECT_NEAR(remainder, 0.4, 0.000000001);
  EXPECT_EQ(quader::accumulate_discrete_wheel_steps(0.6, remainder), 1);
  EXPECT_DOUBLE_EQ(remainder, 0.0);

  EXPECT_EQ(quader::accumulate_discrete_wheel_steps(-0.25, remainder), 0);
  EXPECT_NEAR(remainder, -0.25, 0.000000001);
  EXPECT_EQ(quader::accumulate_discrete_wheel_steps(0.25, remainder), 0);
  EXPECT_DOUBLE_EQ(remainder, 0.0);

  EXPECT_EQ(quader::accumulate_discrete_wheel_steps(-2.25, remainder), -2);
  EXPECT_NEAR(remainder, -0.25, 0.000000001);
}

TEST(ViewportHostInteractionPolicyTests,
     ArrayWheelRemainderResetsOnUnmatchedPhysicalEvent) {
  double remainder = 0.6;
  quader::finish_array_wheel_event(true, remainder);
  EXPECT_DOUBLE_EQ(remainder, 0.6);

  quader::finish_array_wheel_event(false, remainder);
  EXPECT_DOUBLE_EQ(remainder, 0.0);
}

TEST(ViewportHostInteractionPolicyTests,
     BoundedWheelRemainderDropsOnlyOutwardMotion) {
  double remainder = 0.6;
  quader::reconcile_bounded_wheel_remainder(false, true, remainder);
  EXPECT_DOUBLE_EQ(remainder, 0.0);

  remainder = -0.6;
  quader::reconcile_bounded_wheel_remainder(false, true, remainder);
  EXPECT_DOUBLE_EQ(remainder, -0.6);

  quader::reconcile_bounded_wheel_remainder(true, false, remainder);
  EXPECT_DOUBLE_EQ(remainder, 0.0);

  remainder = 0.6;
  quader::reconcile_bounded_wheel_remainder(true, false, remainder);
  EXPECT_DOUBLE_EQ(remainder, 0.6);
}

TEST(ViewportHoverControllerTests,
     ResolvedGridSnapToggleInvalidatesStationaryBoxHover) {
  quader::ViewportHoverController controller;
  quader::ViewportCameraController camera;
  camera.set_viewport_size(800.0f, 600.0f);
  quader::ViewportHoverWorkRequest request{
      .active_tool = quader::ViewportToolId::CreatePrimitive,
      .grid_preset = 6,
      .grid_snap_enabled = true,
      .mouse_local = {320.0f, 240.0f},
      .viewport_size = {800.0f, 600.0f},
      .local_visibility_revision = 11u,
  };

  const quader::ViewportHoverWorkKey snapped =
      controller.make_key(camera, request);
  EXPECT_TRUE(controller.claim(snapped));
  EXPECT_FALSE(controller.claim(snapped));

  request.grid_snap_enabled = false;
  const quader::ViewportHoverWorkKey unsnapped =
      controller.make_key(camera, request);
  EXPECT_FALSE(snapped.matches(unsnapped));
  EXPECT_TRUE(controller.claim(unsnapped));

  request.viewport_pixel_scale = 2.0f;
  const quader::ViewportHoverWorkKey scale_changed =
      controller.make_key(camera, request);
  EXPECT_FALSE(unsnapped.matches(scale_changed));
  EXPECT_TRUE(controller.claim(scale_changed));

  request.local_visibility_revision = 12u;
  const quader::ViewportHoverWorkKey local_visibility_changed =
      controller.make_key(camera, request);
  EXPECT_FALSE(scale_changed.matches(local_visibility_changed));
  EXPECT_TRUE(controller.claim(local_visibility_changed));
  EXPECT_FALSE(controller.claim(local_visibility_changed));
}

TEST(ViewportHoverControllerTests,
     BackfaceSelectionToggleInvalidatesStationarySelectionHover) {
  quader::ViewportHoverController controller;
  quader::ViewportCameraController camera;
  camera.set_viewport_size(800.0f, 600.0f);
  quader::ViewportHoverWorkRequest request{
      .active_tool = quader::ViewportToolId::Select,
      .selection_mode = quader::ViewportSelectionMode::Face,
      .mouse_local = {320.0f, 240.0f},
      .viewport_size = {800.0f, 600.0f},
      .select_backfaces = false,
  };

  const quader::ViewportHoverWorkKey front_only =
      controller.make_key(camera, request);
  EXPECT_TRUE(controller.claim(front_only));
  EXPECT_FALSE(controller.claim(front_only));

  request.select_backfaces = true;
  const quader::ViewportHoverWorkKey two_sided =
      controller.make_key(camera, request);
  EXPECT_FALSE(front_only.matches(two_sided));
  EXPECT_TRUE(controller.claim(two_sided));
  EXPECT_FALSE(controller.claim(two_sided));
}

TEST(ViewportHoverControllerTests,
     EffectiveWorldRadiiInvalidateWhenCombinedPixelScaleIsUnchanged) {
  quader::ViewportHoverController controller;
  quader::ViewportCameraController camera;
  camera.set_viewport_size(800.0f, 600.0f);
  constexpr double kVertexRadius = 0.3;
  constexpr double kEdgeRadius = 0.2;
  quader::ViewportHoverWorkRequest request{
      .active_tool = quader::ViewportToolId::Select,
      .selection_mode = quader::ViewportSelectionMode::Vertex,
      .mouse_local = {320.0f, 240.0f},
      .viewport_size = {800.0f, 600.0f},
      .viewport_pixel_scale = 2.0f,
      .vertex_radius = kVertexRadius,
      .edge_radius = kEdgeRadius,
      .vertex_world_radius = quader::viewport_scaled_world_pick_radius(
          kVertexRadius, 2.0f, 2.0f),
      .edge_world_radius = quader::viewport_scaled_world_pick_radius(
          kEdgeRadius, 2.0f, 2.0f),
  };

  const quader::ViewportHoverWorkKey monitor_scaled =
      controller.make_key(camera, request);
  EXPECT_TRUE(controller.claim(monitor_scaled));
  EXPECT_FALSE(controller.claim(monitor_scaled));

  request.vertex_world_radius = quader::viewport_scaled_world_pick_radius(
      kVertexRadius, 2.0f, 1.0f);
  request.edge_world_radius = quader::viewport_scaled_world_pick_radius(
      kEdgeRadius, 2.0f, 1.0f);
  const quader::ViewportHoverWorkKey interface_scaled =
      controller.make_key(camera, request);
  EXPECT_EQ(monitor_scaled.viewport_pixel_scale,
            interface_scaled.viewport_pixel_scale);
  EXPECT_EQ(monitor_scaled.vertex_radius, interface_scaled.vertex_radius);
  EXPECT_EQ(monitor_scaled.edge_radius, interface_scaled.edge_radius);
  EXPECT_FALSE(monitor_scaled.matches(interface_scaled));
  EXPECT_TRUE(controller.claim(interface_scaled));
  EXPECT_FALSE(controller.claim(interface_scaled));
}

TEST(ViewportHostInteractionPolicyTests,
     BoxFootprintKeepsOverlayPassVisibleWhenDisplayTogglesAreOff) {
  using quader::ViewportOverlayPassVisibilityRequest;
  using quader::should_enable_viewport_overlay_pass;

  EXPECT_FALSE(should_enable_viewport_overlay_pass({}));
  EXPECT_TRUE(should_enable_viewport_overlay_pass({
      .tool_overlay_visible = true,
  }));
  EXPECT_TRUE(should_enable_viewport_overlay_pass(
      ViewportOverlayPassVisibilityRequest{.overlays_visible = true}));
  EXPECT_TRUE(should_enable_viewport_overlay_pass(
      ViewportOverlayPassVisibilityRequest{.gizmos_visible = true}));
  EXPECT_TRUE(should_enable_viewport_overlay_pass(
      ViewportOverlayPassVisibilityRequest{.wireframe_pane = true}));
  EXPECT_TRUE(should_enable_viewport_overlay_pass(
      ViewportOverlayPassVisibilityRequest{.pivot_handle_visible = true}));
  EXPECT_TRUE(should_enable_viewport_overlay_pass(
      ViewportOverlayPassVisibilityRequest{
          .native_tool_overlay_visible = true,
      }));
}

TEST(ViewportHostInteractionPolicyTests,
     PointPlacementMarkerIsPaneLocalUntilItIsLocked) {
  using quader::should_show_point_placement_overlay;

  EXPECT_FALSE(should_show_point_placement_overlay(false, false, true));
  EXPECT_TRUE(should_show_point_placement_overlay(true, false, true));
  EXPECT_FALSE(should_show_point_placement_overlay(true, false, false));
  EXPECT_TRUE(should_show_point_placement_overlay(true, true, true));
  EXPECT_TRUE(should_show_point_placement_overlay(true, true, false));
}

TEST(ViewportHostInteractionPolicyTests,
     SceneOverlayExtractionCountStaysOneForOneToFourRequestingPanes) {
  std::array<bool, 4> pane_requests = {};
  EXPECT_EQ(quader::viewport_scene_overlay_snapshot_extraction_count(
                pane_requests),
            0u);

  for (std::size_t pane_count = 1; pane_count <= pane_requests.size();
       ++pane_count) {
    pane_requests.fill(false);
    std::fill_n(pane_requests.begin(), pane_count, true);
    EXPECT_EQ(quader::viewport_scene_overlay_snapshot_extraction_count(
                  std::span<const bool>(pane_requests.data(), pane_count)),
              1u)
        << "pane_count=" << pane_count;
  }
}

TEST(Phase4ViewportPrecedenceTests,
     P4V01NavigationConsumesFrameBeforeViewportShortcutDispatch) {
  quader::ViewportInputRouter router;
  quader::ViewportCameraController camera;
  quader::ViewportInputFrame input;
  input.viewport_hovered = true;
  input.viewport_focused = true;
  input.viewport_size = {800.0f, 600.0f};
  input.navigation_bindings.orbit_pressed = true;
  input.navigation_bindings.orbit_down = true;
  input.resolved_shortcuts.push_back({
      .kind = quader::ViewportShortcutKind::SetView,
      .view = quader::ViewportView::Top,
  });

  const quader::ViewportInputResult result = router.handle_frame(input, camera);
  EXPECT_TRUE(result.navigation_active);
  EXPECT_TRUE(result.consumed);
  EXPECT_FALSE(result.shortcut_handled);
  EXPECT_FALSE(result.view_changed);
}

TEST(Phase4ViewportPrecedenceTests,
     P4V02PaneResolutionOrdersLockActivityHoverFocusAndWorkspaceFocus) {
  using quader::ViewportPaneId;
  std::array<quader::ViewportPaneInputCandidate, 4> panes{{
      {.pane_id = ViewportPaneId::Perspective},
      {.pane_id = ViewportPaneId::Top, .has_input_activity = true},
      {.pane_id = ViewportPaneId::Front, .hovered = true},
      {.pane_id = ViewportPaneId::Side, .focused = true},
  }};
  quader::ViewportPaneInputResolutionRequest request{
      .active_panes = panes,
      .focused_pane = ViewportPaneId::Perspective,
  };

  EXPECT_EQ(quader::resolve_viewport_input_pane(request), ViewportPaneId::Top);
  panes[1].has_input_activity = false;
  EXPECT_EQ(quader::resolve_viewport_input_pane(request),
            ViewportPaneId::Front);
  panes[2].hovered = false;
  EXPECT_EQ(quader::resolve_viewport_input_pane(request), ViewportPaneId::Side);
  panes[3].focused = false;
  EXPECT_EQ(quader::resolve_viewport_input_pane(request),
            ViewportPaneId::Perspective);

  request.interaction_locked = true;
  request.interaction_owner = ViewportPaneId::Side;
  panes[1].has_input_activity = true;
  EXPECT_EQ(quader::resolve_viewport_input_pane(request), ViewportPaneId::Side);
}

TEST(Phase4ViewportPrecedenceTests,
     P4V09CameraFramingUsesBoundsThenExplicitEmptyPolicy) {
  quader::ViewportCameraController camera;
  camera.set_viewport_size(800.0f, 600.0f);
  camera.focus_origin(12.0f);
  const quader::ViewportCameraState before = camera.state();

  EXPECT_FALSE(camera.focus_optional_bounds(
      std::nullopt, quader::ViewportEmptyFocusBehavior::KeepCamera));
  EXPECT_FLOAT_EQ(camera.state().target.x, before.target.x);
  EXPECT_FLOAT_EQ(camera.state().target.y, before.target.y);
  EXPECT_FLOAT_EQ(camera.state().target.z, before.target.z);
  EXPECT_FLOAT_EQ(camera.state().distance, before.distance);

  EXPECT_TRUE(camera.focus_optional_bounds(quader::ViewportFocusBounds{
      .min = {-2.0f, -1.0f, -3.0f},
      .max = {4.0f, 5.0f, 3.0f},
  }));
  EXPECT_FLOAT_EQ(camera.state().target.x, 1.0f);
  EXPECT_FLOAT_EQ(camera.state().target.y, 2.0f);
  EXPECT_FLOAT_EQ(camera.state().target.z, 0.0f);

  EXPECT_TRUE(camera.focus_optional_bounds(
      std::nullopt, quader::ViewportEmptyFocusBehavior::FocusOrigin));
  EXPECT_FLOAT_EQ(camera.state().target.x, 0.0f);
  EXPECT_FLOAT_EQ(camera.state().target.y, 0.0f);
  EXPECT_FLOAT_EQ(camera.state().target.z, 0.0f);
}

TEST(ViewportInteractionControllerMaterialTests,
     CtrlDoubleAppliesClickedFaceMaterialImmediatelyInOneUndo) {
  ProjectViewportInteractionHarness harness;
  harness.initialize_with_second_material();
  const quader::QuaderModelingObjectRef box =
      create_project_box_at(harness, {});
  const quader::ViewportRay ray =
      harness.camera.ray_from_viewport_point({400.0f, 300.0f});
  const quader::QuaderHoverSummary hover =
      harness.editor.selection().preview_hover({
          .ray = {.origin = ray.origin, .direction = ray.direction},
          .face_plane_hover = true,
      });
  ASSERT_EQ(hover.target.kind, quader::QuaderActiveSelectionKind::Face);
  ASSERT_EQ(hover.target.object_id, box.modeling_object_id);

  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Face)
                  .ok());
  ASSERT_TRUE(harness.editor.selection()
                  .select_face(box, hover.target.face_id)
                  .ok());
  ASSERT_TRUE(harness.editor.modeling()
                  .apply_material(quader::QuaderMaterialReference::project(
                      harness.second_material()))
                  .ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Face);
  harness.scene_session().history().clear();
  const quader::QuaderSelectionSummary selection_before =
      harness.editor.selection().summary();

  harness.input.apply_face_material_to_mesh_pressed = true;
  harness.controller.handle_after_active_tool(
      harness.editor, harness.frame(), quader::ViewportToolResult{},
      quader::ViewportBeforeToolInteractionResult{});
  harness.input.apply_face_material_to_mesh_pressed = false;
  harness.monotonic_time_seconds = 20.0;
  harness.controller.handle_after_active_tool(
      harness.editor, harness.frame(), quader::ViewportToolResult{},
      quader::ViewportBeforeToolInteractionResult{});

  const auto &stats = harness.controller.material_interaction_work_stats();
  EXPECT_EQ(stats.apply_face_to_mesh_request_count, 1u);
  EXPECT_EQ(stats.face_pick_count, 1u);
  ASSERT_TRUE(harness.editor.history().can_undo());
  EXPECT_EQ(harness.editor.query().history().undo_label,
            "Apply Face Material to Mesh");
  const std::vector<quader::QuaderElementId> faces =
      authored_face_ids(harness, box);
  ASSERT_FALSE(faces.empty());
  for (const quader::QuaderElementId face : faces) {
    const auto material = harness.editor.modeling().project_material_for_face(
        box.modeling_object_id, face);
    ASSERT_TRUE(material.ok()) << material.error_message();
    EXPECT_EQ(material.value().id, harness.second_material());
  }
  const quader::QuaderSelectionSummary selection_after =
      harness.editor.selection().summary();
  EXPECT_EQ(selection_after.mode, selection_before.mode);
  EXPECT_EQ(selection_after.selected_face_count,
            selection_before.selected_face_count);
  EXPECT_EQ(selection_after.active_face_id, selection_before.active_face_id);

  ASSERT_TRUE(harness.editor.history().undo().ok());
  EXPECT_FALSE(harness.editor.history().can_undo());
  for (const quader::QuaderElementId face : faces) {
    const auto material = harness.editor.modeling().project_material_for_face(
        box.modeling_object_id, face);
    ASSERT_TRUE(material.ok()) << material.error_message();
    EXPECT_EQ(material.value().id, face == hover.target.face_id
                                       ? harness.second_material()
                                       : default_project_material_id());
  }
  ASSERT_TRUE(harness.editor.history().redo().ok());
  for (const quader::QuaderElementId face : faces) {
    const auto material = harness.editor.modeling().project_material_for_face(
        box.modeling_object_id, face);
    ASSERT_TRUE(material.ok()) << material.error_message();
    EXPECT_EQ(material.value().id, harness.second_material());
  }
}

TEST(ViewportInteractionControllerMaterialTests,
     IdleAndMissFramesAvoidMaterialQueriesAndMutations) {
  ViewportInteractionHarness harness;
  (void)create_unit_box(harness);
  harness.session.history().clear();
  for (int frame = 0; frame < 4; ++frame) {
    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(), quader::ViewportToolResult{},
        quader::ViewportBeforeToolInteractionResult{});
  }
  const auto idle = harness.controller.material_interaction_work_stats();
  EXPECT_EQ(idle.face_pick_count, 0u);
  EXPECT_EQ(idle.apply_face_to_mesh_request_count, 0u);
  EXPECT_EQ(idle.transfer_face_texture_request_count, 0u);

  harness.input.mouse_local = {0.0f, 0.0f};
  harness.input.mouse_screen = harness.input.mouse_local;
  harness.input.apply_face_material_to_mesh_pressed = true;
  harness.controller.handle_after_active_tool(
      harness.editor, harness.frame(false, false, false, {}, false, false,
                                    false, harness.input.mouse_local),
      quader::ViewportToolResult{},
      quader::ViewportBeforeToolInteractionResult{});
  harness.input.apply_face_material_to_mesh_pressed = false;
  const auto miss = harness.controller.material_interaction_work_stats();
  EXPECT_EQ(miss.face_pick_count, 1u);
  EXPECT_EQ(miss.apply_face_to_mesh_request_count, 0u);
  EXPECT_FALSE(harness.editor.history().can_undo());
}

TEST(ViewportInteractionControllerMaterialTests,
     GroupModeAppliesClickedFaceMaterialWithoutSelectionFallback) {
  ProjectViewportInteractionHarness harness;
  harness.initialize_with_second_material();
  const quader::QuaderModelingObjectRef box =
      create_project_box_at(harness, {});
  const quader::ViewportRay ray =
      harness.camera.ray_from_viewport_point({400.0f, 300.0f});
  const quader::QuaderHoverSummary hover =
      harness.editor.selection().preview_hover({
          .ray = {.origin = ray.origin, .direction = ray.direction},
          .face_plane_hover = true,
      });
  ASSERT_EQ(hover.target.kind, quader::QuaderActiveSelectionKind::Face);
  ASSERT_EQ(hover.target.object_id, box.modeling_object_id);
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Face)
                  .ok());
  ASSERT_TRUE(harness.editor.selection()
                  .select_face(box, hover.target.face_id)
                  .ok());
  ASSERT_TRUE(harness.editor.modeling()
                  .apply_material(quader::QuaderMaterialReference::project(
                      harness.second_material()))
                  .ok());
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Group)
                  .ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Group);
  harness.scene_session().history().clear();

  harness.input.apply_face_material_to_mesh_pressed = true;
  harness.controller.handle_after_active_tool(
      harness.editor, harness.frame(), quader::ViewportToolResult{},
      quader::ViewportBeforeToolInteractionResult{});
  harness.input.apply_face_material_to_mesh_pressed = false;

  EXPECT_EQ(harness.controller.material_interaction_work_stats()
                .apply_face_to_mesh_request_count,
            1u);
  ASSERT_TRUE(harness.editor.history().can_undo());
  EXPECT_EQ(harness.editor.query().history().undo_label,
            "Apply Face Material to Mesh");
  const std::vector<quader::QuaderElementId> faces =
      authored_face_ids(harness, box);
  ASSERT_FALSE(faces.empty());
  for (const quader::QuaderElementId face : faces) {
    const auto material = harness.editor.modeling().project_material_for_face(
        box.modeling_object_id, face);
    ASSERT_TRUE(material.ok()) << material.error_message();
    EXPECT_EQ(material.value().id, harness.second_material());
  }
}

TEST(ViewportInteractionControllerMaterialTests,
     TransferPressUsesAcceptedClickAndPreservesSourceThroughHeldRelease) {
  FaceTextureTransferScenario scenario;
  scenario.initialize();
  auto &harness = scenario.harness;
  ASSERT_TRUE(harness.editor.tools().activate(quader::ViewportToolId::Move).ok());
  harness.router.set_active_tool(quader::ViewportToolId::Move);
  harness.scene_session().history().clear();
  const auto selection_before = harness.editor.selection().summary();

  // The current cursor can already have moved away from the accepted press.
  harness.input.mouse_local = {0.0f, 0.0f};
  harness.input.modifiers.alt = true;
  harness.input.transfer_face_texture_pressed = true;
  const auto press = harness.dispatch_frame(
      harness.left_frame(scenario.target_screen, true, true, false));
  harness.input.transfer_face_texture_pressed = false;
  EXPECT_TRUE(press.transfer_face_texture_consumed);
  EXPECT_TRUE(press.gizmo_consumed);
  EXPECT_TRUE(harness.controller.texture_transfer_pointer_active());
  scenario.expect_target_material(harness.second_material());
  scenario.expect_source_selection();
  EXPECT_EQ(harness.editor.selection().summary().selection_revision,
            selection_before.selection_revision);
  EXPECT_EQ(harness.editor.query().history().undo_label, "Transfer Face Texture");

  // Modifier changes and motion beyond the selection/gizmo threshold must not
  // turn a completed transfer into painting, a marquee, or a transform.
  harness.input.modifiers = {};
  harness.input.mouse_local = scenario.source_screen + quader::QVec2{100.0f, 60.0f};
  for (int frame = 0; frame < 3; ++frame) {
    const auto held = harness.dispatch_frame(
        harness.left_frame(scenario.target_screen, false, true, false));
    EXPECT_TRUE(held.transfer_face_texture_consumed);
    EXPECT_FALSE(harness.controller.dragging());
    EXPECT_FALSE(harness.controller.selection_marquee().active);
    scenario.expect_source_selection();
  }
  const auto release = harness.dispatch_frame(
      harness.left_frame(scenario.target_screen, false, false, true));
  EXPECT_TRUE(release.transfer_face_texture_consumed);
  EXPECT_FALSE(harness.controller.texture_transfer_pointer_active());
  scenario.expect_source_selection();
  const auto after_release = harness.controller.material_interaction_work_stats();
  EXPECT_EQ(after_release.face_pick_count, 1u);
  EXPECT_EQ(after_release.transfer_face_texture_request_count, 1u);
  for (int frame = 0; frame < 4; ++frame) {
    (void)harness.dispatch_frame(harness.frame());
  }
  EXPECT_EQ(harness.controller.material_interaction_work_stats().face_pick_count,
            after_release.face_pick_count);
  EXPECT_EQ(harness.controller.material_interaction_work_stats()
                .transfer_face_texture_request_count,
            after_release.transfer_face_texture_request_count);

  ASSERT_TRUE(harness.editor.history().undo().ok());
  EXPECT_FALSE(harness.editor.history().can_undo());
  scenario.expect_target_material(default_project_material_id());
  scenario.expect_source_selection();
  ASSERT_TRUE(harness.editor.history().redo().ok());
  scenario.expect_target_material(harness.second_material());
  scenario.expect_source_selection();
  const auto source_material = harness.editor.modeling().project_material_for_face(
      scenario.source.modeling_object_id, scenario.source_face);
  ASSERT_TRUE(source_material.ok());
  EXPECT_EQ(source_material.value().id, harness.second_material());
}

TEST(ViewportInteractionControllerMaterialTests,
     TransferMissAndSameFaceAtGizmoPreserveSourceWithoutStartingDrag) {
  for (const bool miss : {false, true}) {
    SCOPED_TRACE(miss ? "miss" : "same face at gizmo pivot");
    FaceTextureTransferScenario scenario;
    scenario.initialize();
    auto &harness = scenario.harness;
    ASSERT_TRUE(harness.editor.tools().activate(quader::ViewportToolId::Move).ok());
    harness.router.set_active_tool(quader::ViewportToolId::Move);
    (void)harness.dispatch_frame(harness.frame());
    const auto *gizmo = harness.controller.transform_gizmo_frame();
    ASSERT_NE(gizmo, nullptr);
    const quader::QVec2 click = miss ? quader::QVec2{0.0f, 0.0f}
                                    : gizmo->info.screen_pivot;
    harness.scene_session().history().clear();
    harness.input.mouse_local = click;
    harness.input.transfer_face_texture_pressed = true;
    const auto press = harness.dispatch_frame(
        harness.left_frame(click, true, true, false));
    harness.input.transfer_face_texture_pressed = false;
    EXPECT_TRUE(press.transfer_face_texture_consumed);
    harness.input.mouse_local = click + quader::QVec2{130.0f, 70.0f};
    (void)harness.dispatch_frame(harness.left_frame(click, false, true, false));
    EXPECT_FALSE(harness.controller.dragging());
    EXPECT_FALSE(harness.controller.selection_marquee().active);
    (void)harness.dispatch_frame(harness.left_frame(click, false, false, true));
    scenario.expect_source_selection();
    scenario.expect_target_material(default_project_material_id());
    EXPECT_FALSE(harness.editor.history().can_undo());
    EXPECT_EQ(harness.controller.material_interaction_work_stats()
                  .transfer_face_texture_request_count,
              miss ? 0u : 1u);

    // Ordinary selection resumes after release without a lingering claim.
    harness.input.mouse_local = scenario.target_screen;
    (void)harness.dispatch_frame(
        harness.left_frame(scenario.target_screen, true, true, false));
    (void)harness.dispatch_frame(
        harness.left_frame(scenario.target_screen, false, false, true));
    const auto selection = harness.editor.selection().summary();
    EXPECT_EQ(selection.selected_face_count, 1u);
    EXPECT_EQ(selection.active_object_id, scenario.target.modeling_object_id);
    EXPECT_EQ(selection.active_face_id, scenario.target_face);
  }
}

TEST(ViewportInteractionControllerMaterialTests,
     InvalidTransferSourceCountConsumesGestureWithoutChangingSelection) {
  for (const bool multiple_faces : {false, true}) {
    SCOPED_TRACE(multiple_faces ? "multiple source faces" : "no source face");
    FaceTextureTransferScenario scenario;
    scenario.initialize();
    auto &harness = scenario.harness;
    if (multiple_faces) {
      ASSERT_TRUE(harness.editor.selection()
                      .select_face(scenario.target, scenario.target_face,
                                   quader::QuaderSelectionEdit::Add)
                      .ok());
    } else {
      ASSERT_TRUE(harness.editor.selection().clear().ok());
    }
    harness.scene_session().history().clear();
    const auto before = harness.editor.selection().summary();
    harness.input.transfer_face_texture_pressed = true;
    const auto press = harness.dispatch_frame(
        harness.left_frame(scenario.target_screen, true, true, false));
    harness.input.transfer_face_texture_pressed = false;
    EXPECT_TRUE(press.transfer_face_texture_consumed);
    (void)harness.dispatch_frame(
        harness.left_frame(scenario.target_screen, false, true, false));
    (void)harness.dispatch_frame(
        harness.left_frame(scenario.target_screen, false, false, true));
    const auto after = harness.editor.selection().summary();
    EXPECT_EQ(after.selected_face_count, before.selected_face_count);
    EXPECT_EQ(after.active_object_id, before.active_object_id);
    EXPECT_EQ(after.active_face_id, before.active_face_id);
    EXPECT_EQ(after.selection_revision, before.selection_revision);
    EXPECT_EQ(after.content_revision, before.content_revision);
    EXPECT_FALSE(harness.editor.history().can_undo());
    EXPECT_FALSE(harness.controller.dragging());
    EXPECT_FALSE(harness.controller.selection_marquee().active);
    scenario.expect_target_material(default_project_material_id());
  }
}

TEST(ViewportInteractionControllerMaterialTests,
     TransferBlockedContextsDoNotPickOrDispatch) {
  FaceTextureTransferScenario scenario;
  scenario.initialize();
  auto &harness = scenario.harness;
  harness.input.transfer_face_texture_pressed = true;
  const std::array input_blockers{
      &quader::ViewportInputFrame::text_input_active,
      &quader::ViewportInputFrame::navigation_active,
      &quader::ViewportInputFrame::modal_tool_active,
      &quader::ViewportInputFrame::active_tool_owns_viewport,
      &quader::ViewportInputFrame::active_transform_drag,
      &quader::ViewportInputFrame::operation_preview_active,
  };
  for (bool quader::ViewportInputFrame::*blocker : input_blockers) {
    harness.input.*blocker = true;
    (void)harness.dispatch_frame(harness.frame());
    harness.input.*blocker = false;
  }
  harness.input.viewport_focused = false;
  (void)harness.dispatch_frame(harness.frame());
  harness.input.viewport_focused = true;
  harness.input.viewport_hovered = false;
  (void)harness.dispatch_frame(harness.frame());
  harness.input.viewport_hovered = true;
  harness.input.mouse_local = {harness.input.viewport_size.x + 1.0f, 100.0f};
  (void)harness.dispatch_frame(harness.frame());
  harness.input.mouse_local = scenario.target_screen;
  harness.input_result.navigation_active = true;
  (void)harness.dispatch_frame(harness.frame());
  harness.input_result.navigation_active = false;
  harness.input_result.camera_changed = true;
  (void)harness.dispatch_frame(harness.frame());
  harness.input_result.camera_changed = false;
  harness.input_result.cancel_requested = true;
  (void)harness.dispatch_frame(harness.frame());
  harness.input_result.cancel_requested = false;
  harness.controller.handle_after_active_tool(
      harness.editor, harness.frame(), {}, {.gizmo_consumed = true});
  const auto prepared =
      harness.controller.handle_before_active_tool(harness.editor, harness.frame());
  harness.controller.handle_after_active_tool(
      harness.editor, harness.frame(), {.handled = true}, prepared);
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Edge);
  (void)harness.dispatch_frame(harness.frame());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Face);
  EXPECT_EQ(harness.controller.material_interaction_work_stats().face_pick_count, 0u);
  EXPECT_EQ(harness.controller.material_interaction_work_stats()
                .transfer_face_texture_request_count,
            0u);
  EXPECT_FALSE(harness.editor.history().can_undo());
  scenario.expect_source_selection();
  scenario.expect_target_material(default_project_material_id());
}

TEST(ViewportInteractionControllerMaterialTests,
     TransferDoesNotTakeOverPendingGizmoOrSelectionMarquee) {
  FaceTextureTransferScenario scenario;
  scenario.initialize();
  auto &harness = scenario.harness;
  ASSERT_TRUE(harness.editor.tools().activate(quader::ViewportToolId::Move).ok());
  harness.router.set_active_tool(quader::ViewportToolId::Move);
  (void)harness.dispatch_frame(harness.frame());
  const auto *gizmo = harness.controller.transform_gizmo_frame();
  ASSERT_NE(gizmo, nullptr);
  const auto *axis = find_axis(*gizmo, quader_editor_core::TransformAxis::X);
  ASSERT_NE(axis, nullptr);
  const quader::QVec2 start = axis->screen_tip;
  harness.input.mouse_local = start;
  const auto pending = harness.dispatch_frame(
      harness.left_frame(start, true, true, false));
  EXPECT_TRUE(pending.gizmo_consumed);
  EXPECT_FALSE(harness.controller.dragging());
  harness.input.transfer_face_texture_pressed = true;
  const auto held = harness.dispatch_frame(
      harness.left_frame(start, false, true, false));
  EXPECT_FALSE(held.transfer_face_texture_consumed);
  EXPECT_FALSE(harness.controller.texture_transfer_pointer_active());
  EXPECT_EQ(harness.controller.material_interaction_work_stats().face_pick_count, 0u);
  harness.input.transfer_face_texture_pressed = false;
  (void)harness.dispatch_frame(harness.left_frame(start, false, false, true));
  scenario.expect_source_selection();

  harness.input.mouse_local = start;
  (void)harness.dispatch_frame(harness.left_frame(start, true, true, false));
  harness.input.mouse_local = start + quader::QVec2{120.0f, 0.0f};
  (void)harness.dispatch_frame(harness.left_frame(start, false, true, false));
  ASSERT_TRUE(harness.controller.dragging());
  harness.input.transfer_face_texture_pressed = true;
  (void)harness.dispatch_frame(harness.left_frame(start, false, true, false));
  EXPECT_TRUE(harness.controller.dragging());
  EXPECT_EQ(harness.controller.material_interaction_work_stats().face_pick_count, 0u);
  harness.input.transfer_face_texture_pressed = false;
  ASSERT_TRUE(harness.controller.cancel_active_drag(harness.editor));
  (void)harness.dispatch_frame(harness.left_frame(start, false, false, true));

  ASSERT_TRUE(harness.editor.tools().activate(quader::ViewportToolId::Select).ok());
  harness.router.set_active_tool(quader::ViewportToolId::Select);
  harness.controller.reset(&harness.editor);
  const quader::QVec2 corner{10.0f, 10.0f};
  harness.input.mouse_local = corner;
  (void)harness.dispatch_frame(harness.left_frame(corner, true, true, false));
  harness.input.transfer_face_texture_pressed = true;
  harness.input.mouse_local = {790.0f, 590.0f};
  const auto marquee = harness.dispatch_frame(
      harness.left_frame(corner, false, true, false));
  EXPECT_FALSE(marquee.transfer_face_texture_consumed);
  EXPECT_TRUE(harness.controller.selection_marquee().active);
  EXPECT_EQ(harness.controller.material_interaction_work_stats().face_pick_count, 0u);
  harness.input.transfer_face_texture_pressed = false;
  (void)harness.dispatch_frame(harness.left_frame(corner, false, false, true));
  scenario.expect_target_material(default_project_material_id());
}

TEST(ViewportInteractionControllerMaterialTests,
     ReboundLeftClickReservesPressAndTransfersOnlyOnItsSemanticClick) {
  FaceTextureTransferScenario scenario;
  scenario.initialize();
  auto &harness = scenario.harness;
  ASSERT_TRUE(harness.editor.commands()
                  .set_keybinding_storage_path(
                      harness.editor.project().manifest_path().parent_path() /
                      "transfer-keybindings.json")
                  .ok());
  ASSERT_TRUE(harness.editor.commands()
                  .set_shortcut("viewport.material.transfer_face_texture",
                                "Alt+LeftMouseClick")
                  .ok());
  const auto press = quader::quader_input_parse_shortcut("Alt+LeftMouse");
  const auto click = quader::quader_input_parse_shortcut("Alt+LeftMouseClick");
  ASSERT_TRUE(press.has_value());
  ASSERT_TRUE(click.has_value());
  std::vector<quader::QuaderInputChord> suppressed;
  double linear_remainder = 0.0;
  double radial_remainder = 0.0;
  const auto resolve = [&]() {
    quader::ViewportKeybindingResolver::resolve(
        harness.editor, harness.router.state(), harness.input, suppressed,
        linear_remainder, radial_remainder);
  };
  harness.input.pressed_input_chords = *press;
  resolve();
  EXPECT_TRUE(harness.input.transfer_face_texture_pointer_reserved);
  EXPECT_FALSE(harness.input.transfer_face_texture_pressed);
  (void)harness.dispatch_frame(
      harness.left_frame(scenario.target_screen, true, true, false));
  EXPECT_TRUE(harness.controller.texture_transfer_pointer_active());
  EXPECT_EQ(harness.controller.material_interaction_work_stats().face_pick_count, 0u);
  scenario.expect_source_selection();
  scenario.expect_target_material(default_project_material_id());
  harness.input.pressed_input_chords.clear();
  harness.input.active_input_chords = *press;
  resolve();
  (void)harness.dispatch_frame(
      harness.left_frame(scenario.target_screen, false, true, false));
  EXPECT_EQ(harness.controller.material_interaction_work_stats().face_pick_count, 0u);
  harness.input.active_input_chords.clear();
  harness.input.released_input_chords = *press;
  harness.input.pressed_input_chords = *click;
  resolve();
  EXPECT_TRUE(harness.input.transfer_face_texture_pressed);
  (void)harness.dispatch_frame(
      harness.left_frame(scenario.target_screen, false, false, true));
  EXPECT_FALSE(harness.controller.texture_transfer_pointer_active());
  EXPECT_EQ(harness.controller.material_interaction_work_stats()
                .transfer_face_texture_request_count,
            1u);
  scenario.expect_source_selection();
  scenario.expect_target_material(harness.second_material());
  ASSERT_TRUE(harness.editor.history().undo().ok());
  EXPECT_FALSE(harness.editor.history().can_undo());
}

TEST(ViewportInteractionControllerMaterialTests,
     NativeDoublePressKeepsTransferOwnershipForPressAndClickBindings) {
  for (const bool use_click_binding : {false, true}) {
    SCOPED_TRACE(use_click_binding ? "Click mapping" : "Press mapping");
    FaceTextureTransferScenario scenario;
    scenario.initialize();
    auto &harness = scenario.harness;
    ASSERT_TRUE(harness.editor.commands()
                    .set_keybinding_storage_path(
                        harness.editor.project().manifest_path().parent_path() /
                        "double-transfer-keybindings.json")
                    .ok());
    if (use_click_binding) {
      ASSERT_TRUE(harness.editor.commands()
                      .set_shortcut("viewport.material.transfer_face_texture",
                                    "Alt+LeftMouseClick")
                      .ok());
    }
    std::vector<quader::QuaderInputChord> suppressed;
    double linear_remainder = 0.0;
    double radial_remainder = 0.0;
    const auto resolve = [&](std::string_view chord) {
      harness.input.pressed_input_chords.clear();
      if (!chord.empty()) {
        const auto parsed = quader::quader_input_parse_shortcut(chord);
        EXPECT_TRUE(parsed.has_value());
        if (parsed.has_value()) {
          harness.input.pressed_input_chords = *parsed;
        }
      }
      quader::ViewportKeybindingResolver::resolve(
          harness.editor, harness.router.state(), harness.input, suppressed,
          linear_remainder, radial_remainder);
    };

    resolve("Alt+LeftMouse");
    (void)harness.dispatch_frame(
        harness.left_frame(scenario.target_screen, true, true, false));
    resolve("Alt+LeftMouseClick");
    (void)harness.dispatch_frame(
        harness.left_frame(scenario.target_screen, false, false, true));
    scenario.expect_target_material(harness.second_material());
    scenario.expect_source_selection();
    EXPECT_EQ(harness.controller.material_interaction_work_stats()
                  .transfer_face_texture_request_count,
              1u);

    // Qt emits only DoubleClick on its second press, and no Click at all on
    // the second release. Do not replace this with two synthetic Press edges.
    resolve("Alt+LeftMouseDoubleClick");
    EXPECT_EQ(harness.input.transfer_face_texture_pointer_reserved,
              use_click_binding);
    EXPECT_EQ(harness.input.transfer_face_texture_pressed, !use_click_binding);
    auto second_press =
        harness.left_frame(scenario.target_screen, true, true, false);
    second_press.left_click_count = 2;
    const auto before_second = harness.dispatch_frame(second_press);
    EXPECT_TRUE(before_second.transfer_face_texture_consumed);
    EXPECT_TRUE(harness.controller.texture_transfer_pointer_active());
    scenario.expect_source_selection();
    resolve({});
    harness.input.mouse_local = scenario.target_screen + quader::QVec2{100.0f, 50.0f};
    (void)harness.dispatch_frame(
        harness.left_frame(scenario.target_screen, false, true, false));
    EXPECT_FALSE(harness.controller.dragging());
    EXPECT_FALSE(harness.controller.selection_marquee().active);
    auto second_release =
        harness.left_frame(scenario.target_screen, false, false, true);
    second_release.left_click_count = 2;
    (void)harness.dispatch_frame(second_release);
    EXPECT_FALSE(harness.controller.texture_transfer_pointer_active());
    scenario.expect_source_selection();
    EXPECT_EQ(harness.controller.material_interaction_work_stats()
                  .transfer_face_texture_request_count,
              use_click_binding ? 1u : 2u);
    ASSERT_TRUE(harness.editor.history().undo().ok());
    EXPECT_FALSE(harness.editor.history().can_undo());
  }
}

TEST(ViewportInteractionControllerMaterialTests,
     KeyboardTransferUsesCurrentCursorWithoutPhysicalAltOrLeftPress) {
  FaceTextureTransferScenario scenario;
  scenario.initialize();
  auto &harness = scenario.harness;
  harness.input.modifiers = {};
  harness.input.transfer_face_texture_pressed = true;
  // Existing material gestures use this right-click point; transfer must not.
  (void)harness.dispatch_frame(harness.frame({0.0f, 0.0f}));
  EXPECT_FALSE(harness.controller.texture_transfer_pointer_active());
  EXPECT_EQ(harness.controller.material_interaction_work_stats()
                .transfer_face_texture_request_count,
            1u);
  scenario.expect_source_selection();
  scenario.expect_target_material(harness.second_material());
}

TEST(ViewportInteractionControllerMaterialTests,
     TransferUsesExactMatchingReleaseAndSuppressesForeignOrStaleEdges) {
  {
    FaceTextureTransferScenario scenario;
    scenario.initialize();
    auto &harness = scenario.harness;
    harness.input.transfer_face_texture_pointer_reserved = true;
    harness.input.pointer_pressed_edges = {{
        .button = quader::QuaderMouseButton::Left,
        .local = scenario.target_screen,
        .sequence = 101u,
    }};
    const auto press = harness.dispatch_frame(
        harness.left_frame(scenario.target_screen, true, true, false));
    EXPECT_TRUE(press.transfer_face_texture_consumed);
    EXPECT_FALSE(press.transfer_face_texture_requested);
    ASSERT_TRUE(harness.controller.texture_transfer_pointer_active());

    harness.input.transfer_face_texture_pointer_reserved = false;
    harness.input.pointer_pressed_edges.clear();
    harness.input.pointer_released_edges = {{
        .button = quader::QuaderMouseButton::Right,
        .local = {12.0f, 14.0f},
        .sequence = 102u,
    }};
    const auto foreign = harness.dispatch_frame(
        harness.left_frame(scenario.target_screen, false, true, false));
    EXPECT_TRUE(foreign.transfer_face_texture_consumed);
    EXPECT_FALSE(foreign.transfer_face_texture_requested);
    EXPECT_TRUE(harness.controller.texture_transfer_pointer_active());

    harness.input.pointer_released_edges = {{
        .button = quader::QuaderMouseButton::Left,
        .local = scenario.target_screen,
        .sequence = 103u,
    }};
    harness.input.mouse_local = {0.0f, 0.0f};
    harness.input.mouse_screen = harness.input.mouse_local;
    harness.input.transfer_face_texture_pressed = true;
    const auto release = harness.dispatch_frame(
        harness.left_frame(scenario.target_screen, false, false, true));
    harness.input.transfer_face_texture_pressed = false;
    harness.input.pointer_released_edges.clear();
    EXPECT_TRUE(release.transfer_face_texture_consumed);
    EXPECT_TRUE(release.transfer_face_texture_requested);
    EXPECT_FLOAT_EQ(release.transfer_face_texture_local.x,
                    scenario.target_screen.x);
    EXPECT_FLOAT_EQ(release.transfer_face_texture_local.y,
                    scenario.target_screen.y);
    EXPECT_FALSE(harness.controller.texture_transfer_pointer_active());
    EXPECT_EQ(harness.controller.material_interaction_work_stats()
                  .transfer_face_texture_request_count,
              1u);
    scenario.expect_target_material(harness.second_material());
  }

  {
    FaceTextureTransferScenario scenario;
    scenario.initialize();
    auto &harness = scenario.harness;
    harness.input.transfer_face_texture_pointer_reserved = true;
    harness.input.pointer_pressed_edges = {{
        .button = quader::QuaderMouseButton::Left,
        .local = scenario.target_screen,
        .sequence = 201u,
    }};
    (void)harness.dispatch_frame(
        harness.left_frame(scenario.target_screen, true, true, false));
    ASSERT_TRUE(harness.controller.texture_transfer_pointer_active());

    harness.input.transfer_face_texture_pointer_reserved = false;
    harness.input.pointer_pressed_edges.clear();
    harness.input.pointer_released_edges = {{
        .button = quader::QuaderMouseButton::Left,
        .local = scenario.target_screen,
        .sequence = 200u,
    }};
    harness.input.transfer_face_texture_pressed = true;
    const auto stale = harness.dispatch_frame(
        harness.left_frame(scenario.target_screen, false, false, true));
    EXPECT_TRUE(stale.transfer_face_texture_consumed);
    EXPECT_FALSE(stale.transfer_face_texture_requested);
    EXPECT_FALSE(harness.controller.texture_transfer_pointer_active());

    harness.input.pointer_released_edges = {{
        .button = quader::QuaderMouseButton::Left,
        .local = scenario.target_screen,
        .sequence = 202u,
    }};
    const auto orphan = harness.dispatch_frame(
        harness.left_frame(scenario.target_screen, false, false, true));
    harness.input.transfer_face_texture_pressed = false;
    harness.input.pointer_released_edges.clear();
    EXPECT_TRUE(orphan.transfer_face_texture_consumed);
    EXPECT_FALSE(orphan.transfer_face_texture_requested);
    EXPECT_EQ(harness.controller.material_interaction_work_stats()
                  .transfer_face_texture_request_count,
              0u);
    scenario.expect_source_selection();
    scenario.expect_target_material(default_project_material_id());
  }
}

TEST(ViewportInteractionControllerMaterialTests,
     TransferHonorsMiddleAndRightSettingsButtonsWithoutForeignReleaseOrMenu) {
  for (const quader::QuaderMouseButton button : {
           quader::QuaderMouseButton::Middle,
           quader::QuaderMouseButton::Right,
       }) {
    SCOPED_TRACE(button == quader::QuaderMouseButton::Middle ? "middle"
                                                             : "right");
    FaceTextureTransferScenario scenario;
    scenario.initialize();
    auto &harness = scenario.harness;
    quader::QuaderInputChord semantic{
        .alt = true,
        .device = quader::QuaderInputDevice::Mouse,
        .mouse_button = button,
        .gesture = quader::QuaderInputGesture::Click,
    };

    const auto pointer_frame = [&](bool pressed, bool down, bool released) {
      quader::ViewportInteractionFrame frame = harness.frame();
      harness.input.middle_pressed = false;
      harness.input.middle_down = false;
      harness.input.middle_released = false;
      harness.input.right_pressed = false;
      harness.input.right_down = false;
      harness.input.right_released = false;
      if (button == quader::QuaderMouseButton::Middle) {
        harness.input.middle_pressed = pressed;
        harness.input.middle_down = down;
        harness.input.middle_released = released;
      } else {
        harness.input.right_pressed = pressed;
        harness.input.right_down = down;
        harness.input.right_released = released;
        frame.right_pressed = pressed;
        frame.right_down = down;
        frame.right_released = released;
        frame.right_click_local = scenario.target_screen;
      }
      return frame;
    };
    const auto reserve_press = [&](std::uint64_t sequence) {
      const quader::ViewportPointerButtonEdge edge{
          .button = button,
          .local = scenario.target_screen,
          .sequence = sequence,
      };
      harness.input.transfer_face_texture_pressed = false;
      harness.input.transfer_face_texture_pointer_reserved = true;
      harness.input.transfer_face_texture_edges = {{
          .kind = quader::ViewportSemanticPointerEdgeKind::ReservePress,
          .semantic_chord = semantic,
          .pointer_edge = edge,
      }};
      harness.input.pointer_pressed_edges = {edge};
      harness.input.pointer_released_edges.clear();
      const quader::ViewportInteractionFrame frame =
          pointer_frame(true, true, false);
      if (button == quader::QuaderMouseButton::Right) {
        const auto menu =
            harness.controller.handle_context_menu_pointer(frame, false);
        EXPECT_FALSE(menu.completed);
        EXPECT_FALSE(harness.controller.context_menu_pointer_active());
      }
      const auto reserved = harness.dispatch_frame(frame);
      EXPECT_TRUE(reserved.transfer_face_texture_consumed);
      EXPECT_FALSE(reserved.transfer_face_texture_requested);
      EXPECT_TRUE(harness.controller.texture_transfer_pointer_active());
      harness.input.transfer_face_texture_pointer_reserved = false;
      harness.input.transfer_face_texture_edges.clear();
      harness.input.pointer_pressed_edges.clear();
    };
    const auto semantic_release = [&](std::uint64_t sequence) {
      const quader::ViewportPointerButtonEdge edge{
          .button = button,
          .local = scenario.target_screen,
          .sequence = sequence,
      };
      harness.input.transfer_face_texture_pressed = true;
      harness.input.transfer_face_texture_edges = {{
          .kind = quader::ViewportSemanticPointerEdgeKind::Trigger,
          .semantic_chord = semantic,
          .pointer_edge = edge,
      }};
      harness.input.pointer_released_edges = {edge};
      const quader::ViewportInteractionFrame frame =
          pointer_frame(false, false, true);
      if (button == quader::QuaderMouseButton::Right) {
        const auto menu =
            harness.controller.handle_context_menu_pointer(frame, false);
        EXPECT_FALSE(menu.completed);
        EXPECT_FALSE(harness.controller.context_menu_pointer_active());
      }
      const auto result = harness.dispatch_frame(frame);
      harness.input.transfer_face_texture_pressed = false;
      harness.input.transfer_face_texture_edges.clear();
      harness.input.pointer_released_edges.clear();
      return result;
    };

    reserve_press(100u);
    const quader::QuaderMouseButton foreign_button =
        button == quader::QuaderMouseButton::Middle
            ? quader::QuaderMouseButton::Right
            : quader::QuaderMouseButton::Middle;
    harness.input.pointer_released_edges = {{
        .button = foreign_button,
        .local = {12.0f, 14.0f},
        .sequence = 101u,
    }};
    const auto foreign =
        harness.dispatch_frame(pointer_frame(false, true, false));
    EXPECT_TRUE(foreign.transfer_face_texture_consumed);
    EXPECT_FALSE(foreign.transfer_face_texture_requested);
    EXPECT_TRUE(harness.controller.texture_transfer_pointer_active());

    const auto stale = semantic_release(99u);
    EXPECT_TRUE(stale.transfer_face_texture_consumed);
    EXPECT_FALSE(stale.transfer_face_texture_requested);
    EXPECT_FALSE(harness.controller.texture_transfer_pointer_active());
    const auto orphan = semantic_release(102u);
    EXPECT_TRUE(orphan.transfer_face_texture_consumed);
    EXPECT_FALSE(orphan.transfer_face_texture_requested);
    EXPECT_EQ(harness.controller.material_interaction_work_stats()
                  .transfer_face_texture_request_count,
              0u);
    scenario.expect_target_material(default_project_material_id());
    scenario.expect_source_selection();

    reserve_press(200u);
    const auto matching = semantic_release(201u);
    EXPECT_TRUE(matching.transfer_face_texture_consumed);
    EXPECT_TRUE(matching.transfer_face_texture_requested);
    EXPECT_FALSE(harness.controller.texture_transfer_pointer_active());
    EXPECT_EQ(harness.controller.material_interaction_work_stats()
                  .transfer_face_texture_request_count,
              1u);
    scenario.expect_target_material(harness.second_material());
    scenario.expect_source_selection();
  }
}

TEST(ViewportInteractionControllerMaterialTests,
     TransferPointerClaimCancelsOnExplicitResetAndSceneReplacement) {
  FaceTextureTransferScenario scenario;
  scenario.initialize();
  auto &harness = scenario.harness;
  const auto reserve_press = [&]() {
    harness.input.transfer_face_texture_pointer_reserved = true;
    (void)harness.dispatch_frame(
        harness.left_frame(scenario.target_screen, true, true, false));
    harness.input.transfer_face_texture_pointer_reserved = false;
    EXPECT_TRUE(harness.controller.texture_transfer_pointer_active());
  };
  reserve_press();
  harness.controller.cancel_pending_material_interaction();
  EXPECT_FALSE(harness.controller.texture_transfer_pointer_active());
  harness.input.transfer_face_texture_pressed = true;
  const auto canceled_release = harness.dispatch_frame(
      harness.left_frame(scenario.target_screen, false, false, true));
  EXPECT_TRUE(canceled_release.transfer_face_texture_consumed);
  EXPECT_FALSE(canceled_release.transfer_face_texture_requested);
  harness.input.transfer_face_texture_pressed = false;
  reserve_press();
  harness.controller.reset(&harness.editor);
  EXPECT_FALSE(harness.controller.texture_transfer_pointer_active());
  harness.input.transfer_face_texture_pressed = true;
  const auto reset_release = harness.dispatch_frame(
      harness.left_frame(scenario.target_screen, false, false, true));
  EXPECT_FALSE(reset_release.transfer_face_texture_requested);
  harness.input.transfer_face_texture_pressed = false;
  reserve_press();
  ASSERT_TRUE(harness.editor.selection()
                  .select_face(scenario.target, scenario.target_face)
                  .ok());
  harness.input.transfer_face_texture_pressed = true;
  const auto changed_source_release = harness.dispatch_frame(
      harness.left_frame(scenario.target_screen, false, false, true));
  EXPECT_FALSE(changed_source_release.transfer_face_texture_requested);
  harness.input.transfer_face_texture_pressed = false;
  ASSERT_TRUE(harness.editor.selection()
                  .select_face(scenario.source, scenario.source_face)
                  .ok());
  reserve_press();
  ASSERT_TRUE(harness.editor.scene_documents().new_scene().ok());
  harness.input.transfer_face_texture_pressed = true;
  const auto stale = harness.dispatch_frame(
      harness.left_frame(scenario.target_screen, false, false, true));
  EXPECT_TRUE(stale.transfer_face_texture_consumed);
  EXPECT_FALSE(stale.transfer_face_texture_requested);
  EXPECT_FALSE(harness.controller.texture_transfer_pointer_active());
  EXPECT_EQ(harness.controller.material_interaction_work_stats().face_pick_count, 0u);
  EXPECT_TRUE(harness.editor.modeling().objects().empty());
  EXPECT_FALSE(harness.editor.history().can_undo());
}

TEST(ViewportInteractionControllerTests,
     PathArraySourcePointerUsesSharedHitGroupAndGesturePolicy) {
  ViewportInteractionHarness harness;
  const quader::QuaderModelingObjectRef grouped_member =
      create_box_at(harness, {0.0f, 0.0f, 2.0f});
  const quader::QuaderModelingObjectRef behind_source =
      create_box_at(harness, {0.0f, 0.0f, -2.0f});
  const quader::QuaderModelingObjectRef replacement =
      create_box_at(harness, {3.0f, 0.0f, 0.0f});
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Object)
                  .ok());
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  ASSERT_TRUE(
      harness.editor.selection().select_object(grouped_member).ok());
  const auto group =
      harness.editor.scene().group_selection("Path Array Source Group");
  ASSERT_TRUE(group.ok()) << group.error_message();

  const auto path = harness.editor.modeling().create_curve({
      .points = {{.position = {-5.0f, 4.0f, 0.0f},
                  .outgoing_handle = {1.0f, 0.0f, 0.0f}},
                 {.position = {5.0f, 4.0f, 0.0f},
                  .incoming_handle = {-1.0f, 0.0f, 0.0f}}},
      .name = "Path Array Pointer Driver",
  });
  ASSERT_TRUE(path.ok()) << path.error_message();
  ASSERT_TRUE(harness.editor.selection().select_object(path.value()).ok());
  ASSERT_TRUE(
      harness.editor.tools().activate(quader::ViewportToolId::PathArray).ok());
  harness.router.set_active_tool(quader::ViewportToolId::PathArray);
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
  harness.input.active_tool_owns_viewport = true;
  harness.input.modal_tool_active = true;

  const quader::ViewportScreenPoint grouped_screen =
      harness.camera.project_world_point({0.0f, 0.0f, 2.0f});
  const quader::ViewportScreenPoint replacement_screen =
      harness.camera.project_world_point({3.0f, 0.0f, 0.0f});
  ASSERT_TRUE(grouped_screen.visible);
  ASSERT_TRUE(replacement_screen.visible);

  std::uint64_t sequence = 100U;
  const auto dispatch = [&](bool pressed, bool down, bool released,
                            quader::QVec2 local,
                            quader::ViewportModifiers modifiers = {},
                            bool navigation = false) {
    harness.input.mouse_local = local;
    harness.input.mouse_screen = local;
    harness.input.modifiers = modifiers;
    harness.input.navigation_active = navigation;
    harness.input_result.navigation_active = navigation;
    harness.input_result.camera_changed = navigation;
    harness.input.pointer_pressed_edges.clear();
    harness.input.pointer_released_edges.clear();
    if (pressed) {
      harness.input.pointer_pressed_edges.push_back({
          .button = quader::QuaderMouseButton::Left,
          .local = local,
          .sequence = sequence++,
      });
    }
    if (released) {
      harness.input.pointer_released_edges.push_back({
          .button = quader::QuaderMouseButton::Left,
          .local = local,
          .sequence = sequence++,
      });
    }
    const quader::ViewportInteractionFrame interaction =
        harness.frame(pressed, down, released, local);
    return harness.controller.handle_tool_pointer_lifecycle(
        harness.editor, harness.session.scene(), harness.session.assets(),
        harness.session.native_tools(), &harness.session.point_placement(),
        harness.camera,
        quader::ViewportToolPointerLifecycleFrame{
            .interaction = interaction,
            .native_tool =
                {
                    .input = harness.input,
                    .input_result = harness.input_result,
                    .left_pressed = pressed,
                    .left_down = down,
                    .left_released = released,
                    .left_click_local = local,
                    .pointer_drag_threshold_pixels = 4.0f,
                },
        });
  };
  const auto click = [&](quader::QVec2 local,
                         quader::ViewportModifiers modifiers = {}) {
    (void)dispatch(true, true, false, local, modifiers);
    return dispatch(false, false, true, local, modifiers);
  };

  const auto hovered = dispatch(false, false, false, grouped_screen.position);
  EXPECT_TRUE(hovered.native_tool.path_array_consumed);
  EXPECT_TRUE(hovered.native_tool.blocks_transform_gizmo);
  const auto hover_target =
      harness.session.selection().resolved_hover_target();
  ASSERT_TRUE(hover_target.has_value());
  EXPECT_EQ(hover_target->entity, group.value().id);

  const auto selected = click(grouped_screen.position);
  EXPECT_TRUE(selected.native_tool.path_array_consumed);
  ASSERT_TRUE(harness.session.path_array().has_source());
  EXPECT_EQ(harness.session.path_array().source_root(), group.value().id);
  EXPECT_EQ(harness.session.scene().primary_selection(), group.value().id);

  // The authored source still exists in SceneDocument, but preview
  // suppression removes it before occlusion and canonical arbitration. The
  // otherwise hidden object behind it becomes the ordinary winner.
  (void)dispatch(false, false, false, grouped_screen.position);
  const auto behind_hover =
      harness.session.selection().resolved_hover_target();
  ASSERT_TRUE(behind_hover.has_value());
  EXPECT_EQ(behind_hover->entity, behind_source.entity);

  (void)click(replacement_screen.position,
              {.primary = true, .shift = false, .alt = false});
  EXPECT_EQ(harness.session.path_array().source_root(), group.value().id);

  (void)dispatch(true, true, false, replacement_screen.position);
  quader::QVec2 dragged_release = replacement_screen.position;
  dragged_release.x += 12.0f;
  (void)dispatch(false, false, true, dragged_release);
  EXPECT_EQ(harness.session.path_array().source_root(), group.value().id);

  (void)dispatch(true, true, false, replacement_screen.position);
  (void)dispatch(false, true, false, replacement_screen.position, {}, true);
  (void)dispatch(false, false, true, replacement_screen.position);
  EXPECT_EQ(harness.session.path_array().source_root(), group.value().id);

  const auto replaced = click(replacement_screen.position);
  EXPECT_TRUE(replaced.native_tool.path_array_consumed);
  EXPECT_EQ(harness.session.path_array().source_root(), replacement.entity);

  (void)click({790.0f, 590.0f});
  EXPECT_EQ(harness.session.path_array().source_root(), replacement.entity);
}

namespace {

struct CurvePresetPointerOptions final {
  quader::ViewportModifiers modifiers = {};
  std::optional<std::uint64_t> release_sequence;
  bool navigation_gizmo_consumed = false;
  bool transform_gizmo_consumed = false;
  bool suppress_hover_work = false;
  bool scene_snap_enabled = false;
  bool grid_snap_enabled = false;
};

void configure_curve_preset_viewport(ViewportInteractionHarness &harness) {
  harness.camera.view_axis(quader::ViewportViewAxis::Y, 1);
  harness.router.set_active_tool(quader::ViewportToolId::Curve);
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
  harness.input.orthographic_view = true;
  harness.input.active_tool_owns_viewport = true;
  harness.input.modal_tool_active = true;
}

quader::ViewportToolPointerLifecycleResult
dispatch_curve_preset_pointer(ViewportInteractionHarness &harness,
                              std::uint64_t &sequence, bool pressed, bool down,
                              bool released, quader::QVec2 local,
                              CurvePresetPointerOptions options = {}) {
  harness.input.mouse_local = local;
  harness.input.mouse_screen = local;
  harness.input.modifiers = options.modifiers;
  harness.input.pointer_pressed_edges.clear();
  harness.input.pointer_released_edges.clear();
  if (pressed) {
    harness.input.pointer_pressed_edges.push_back({
        .button = quader::QuaderMouseButton::Left,
        .local = local,
        .sequence = sequence++,
    });
  }
  if (released) {
    const std::uint64_t release_sequence =
        options.release_sequence.has_value() ? *options.release_sequence
                                             : sequence++;
    harness.input.pointer_released_edges.push_back({
        .button = quader::QuaderMouseButton::Left,
        .local = local,
        .sequence = release_sequence,
    });
  }

  const quader::ViewportInteractionFrame interaction =
      harness.frame(pressed, down, released, local);
  return harness.controller.handle_tool_pointer_lifecycle(
      harness.editor, harness.session.scene(), harness.session.assets(),
      harness.session.native_tools(), &harness.session.point_placement(),
      harness.camera,
      quader::ViewportToolPointerLifecycleFrame{
          .interaction = interaction,
          .native_tool =
              {
                  .input = harness.input,
                  .input_result = harness.input_result,
                  .navigation_gizmo_consumed =
                      options.navigation_gizmo_consumed,
                  .transform_gizmo_consumed = options.transform_gizmo_consumed,
                  .suppress_hover_work = options.suppress_hover_work,
                  .left_pressed = pressed,
                  .left_down = down,
                  .left_released = released,
                  .left_click_local = local,
                  .pointer_drag_threshold_pixels = 4.0f,
                  .scene_snap_enabled = options.scene_snap_enabled,
                  .grid_snap_enabled = options.grid_snap_enabled,
              },
      });
}

quader::QVec3
curve_preset_hover_center(const quader::CurveToolPreviewState &preview) {
  return {
      preview.preset_hover_transform.values[12],
      preview.preset_hover_transform.values[13],
      preview.preset_hover_transform.values[14],
  };
}

quader::QVec3
curve_anchor_centroid(const quader::modeling::CurveDocument &document) {
  quader::QVec3 result{};
  for (const auto &point : document.points()) {
    result.x += point.position.x;
    result.y += point.position.y;
    result.z += point.position.z;
  }
  if (document.point_count() != 0u) {
    const float inverse_count =
        1.0f / static_cast<float>(document.point_count());
    result = result * inverse_count;
  }
  return result;
}

enum class CurvePresetPointerInterruption {
  PointerCanceled,
  FocusLost,
  Navigation,
  CameraChanged,
  ViewChanged,
  NavigationGizmo,
  Abandoned,
};

} // namespace

TEST(ViewportInteractionControllerTests,
     CurvePresetsHoverFreezeAndPlaceOnOneStationaryClick) {
  struct PresetCase final {
    quader::CurveCreationKind kind;
    std::size_t expected_points = 0u;
    std::string_view label;
  };
  for (const PresetCase preset : {
           PresetCase{quader::CurveCreationKind::Circle, 4u, "circle"},
           PresetCase{quader::CurveCreationKind::Oval, 6u, "oval"},
       }) {
    SCOPED_TRACE(preset.label);
    ViewportInteractionHarness harness;
    ASSERT_TRUE(
        harness.editor.tools().activate(quader::ViewportToolId::Curve).ok());
    ASSERT_TRUE(harness.session.curve().set_creation_kind(preset.kind).ok);
    configure_curve_preset_viewport(harness);

    std::uint64_t sequence = 100u;
    constexpr quader::QVec2 kPressLocal{447.0f, 327.0f};
    const CurvePresetPointerOptions snapped{.grid_snap_enabled = true};
    const auto hover = dispatch_curve_preset_pointer(
        harness, sequence, false, false, false, kPressLocal, snapped);
    EXPECT_TRUE(hover.native_tool.curve_consumed);
    EXPECT_TRUE(hover.native_tool.blocks_transform_gizmo);
    EXPECT_EQ(harness.session.curve().point_count(), 0u);
    const quader::CurveToolPreviewState &hover_preview =
        harness.session.curve().preview_state();
    ASSERT_TRUE(hover_preview.preset_hover_document.has_value());
    EXPECT_EQ(hover_preview.preset_hover_document->point_count(),
              preset.expected_points);
    const quader::QVec3 frozen_center =
        curve_preset_hover_center(hover_preview);
    const float grid_size =
        quader::viewport_grid_world_size_for_preset(harness.input.grid_preset);
    EXPECT_NEAR(frozen_center.x / grid_size,
                std::round(frozen_center.x / grid_size), 0.0001f);
    EXPECT_NEAR(frozen_center.y / grid_size,
                std::round(frozen_center.y / grid_size), 0.0001f);
    EXPECT_NEAR(frozen_center.z / grid_size,
                std::round(frozen_center.z / grid_size), 0.0001f);

    const auto press = dispatch_curve_preset_pointer(
        harness, sequence, true, true, false, kPressLocal, snapped);
    EXPECT_TRUE(press.native_tool.claim_interaction_pane);
    EXPECT_TRUE(harness.session.native_tools().pointer_dragging());
    EXPECT_EQ(harness.session.curve().point_count(), 0u);

    quader::QVec2 within_threshold = kPressLocal;
    within_threshold.x += 2.0f;
    (void)dispatch_curve_preset_pointer(harness, sequence, false, true, false,
                                        within_threshold, snapped);
    ASSERT_TRUE(harness.session.curve()
                    .preview_state()
                    .preset_hover_document.has_value());
    const quader::QVec3 held_center =
        curve_preset_hover_center(harness.session.curve().preview_state());
    EXPECT_FLOAT_EQ(held_center.x, frozen_center.x);
    EXPECT_FLOAT_EQ(held_center.y, frozen_center.y);
    EXPECT_FLOAT_EQ(held_center.z, frozen_center.z);
    EXPECT_EQ(harness.session.curve().point_count(), 0u);

    const auto release = dispatch_curve_preset_pointer(
        harness, sequence, false, false, true, within_threshold, snapped);
    EXPECT_TRUE(release.native_tool.curve_consumed);
    EXPECT_FALSE(harness.session.native_tools().pointer_dragging());
    EXPECT_EQ(harness.session.curve().point_count(), preset.expected_points);
    EXPECT_TRUE(harness.session.curve().can_apply());
    const quader::CurveToolPreviewState &placed_preview =
        harness.session.curve().preview_state();
    ASSERT_TRUE(placed_preview.document.has_value());
    EXPECT_TRUE(placed_preview.document->closed());
    EXPECT_FALSE(placed_preview.preset_hover_document.has_value());
    const quader::QVec3 centroid =
        curve_anchor_centroid(*placed_preview.document);
    EXPECT_NEAR(centroid.x, frozen_center.x, 0.0001f);
    EXPECT_NEAR(centroid.y, frozen_center.y, 0.0001f);
    EXPECT_NEAR(centroid.z, frozen_center.z, 0.0001f);
    const quader::modeling::CurveContentIdentity placed_identity =
        placed_preview.document->content_identity();

    constexpr quader::QVec2 kEmptyLocal{790.0f, 590.0f};
    (void)dispatch_curve_preset_pointer(harness, sequence, true, true, false,
                                        kEmptyLocal);
    (void)dispatch_curve_preset_pointer(harness, sequence, false, false, true,
                                        kEmptyLocal);
    ASSERT_TRUE(harness.session.curve().preview_state().document.has_value());
    EXPECT_EQ(
        harness.session.curve().preview_state().document->content_identity(),
        placed_identity);
    EXPECT_EQ(harness.session.curve().point_count(), preset.expected_points);
    EXPECT_FALSE(harness.session.native_tools().pointer_dragging());
  }
}

TEST(ViewportInteractionControllerTests,
     CurvePresetModifiersAndDragThresholdCannotPlaceGeometry) {
  ViewportInteractionHarness harness;
  ASSERT_TRUE(
      harness.editor.tools().activate(quader::ViewportToolId::Curve).ok());
  ASSERT_TRUE(harness.session.curve()
                  .set_creation_kind(quader::CurveCreationKind::Circle)
                  .ok);
  configure_curve_preset_viewport(harness);

  std::uint64_t sequence = 200u;
  constexpr quader::QVec2 kPressLocal{420.0f, 315.0f};
  const CurvePresetPointerOptions modified{
      .modifiers = {.primary = false, .shift = true, .alt = false},
  };
  (void)dispatch_curve_preset_pointer(harness, sequence, true, true, false,
                                      kPressLocal, modified);
  (void)dispatch_curve_preset_pointer(harness, sequence, false, false, true,
                                      kPressLocal, modified);
  EXPECT_FALSE(harness.session.native_tools().pointer_dragging());
  EXPECT_EQ(harness.session.curve().point_count(), 0u);

  (void)dispatch_curve_preset_pointer(harness, sequence, true, true, false,
                                      kPressLocal);
  ASSERT_TRUE(harness.session.native_tools().pointer_dragging());
  quader::QVec2 dragged = kPressLocal;
  dragged.x += 4.0f;
  (void)dispatch_curve_preset_pointer(harness, sequence, false, true, false,
                                      dragged);
  EXPECT_FALSE(harness.session.native_tools().pointer_dragging());
  EXPECT_EQ(harness.session.curve().point_count(), 0u);
  EXPECT_FALSE(harness.session.curve()
                   .preview_state()
                   .preset_hover_document.has_value());

  const auto suppressed_release = dispatch_curve_preset_pointer(
      harness, sequence, false, false, true, dragged);
  EXPECT_TRUE(suppressed_release.native_tool.consumed);
  EXPECT_EQ(harness.session.curve().point_count(), 0u);

  (void)dispatch_curve_preset_pointer(harness, sequence, true, true, false,
                                      kPressLocal);
  (void)dispatch_curve_preset_pointer(harness, sequence, false, false, true,
                                      kPressLocal);
  EXPECT_EQ(harness.session.curve().point_count(), 4u);
}

TEST(ViewportInteractionControllerTests,
     CurvePresetPressCancelsAcrossEveryViewportInterruption) {
  struct InterruptionCase final {
    CurvePresetPointerInterruption kind;
    std::string_view label;
  };
  for (const InterruptionCase interruption : {
           InterruptionCase{CurvePresetPointerInterruption::PointerCanceled,
                            "pointer canceled"},
           InterruptionCase{CurvePresetPointerInterruption::FocusLost,
                            "focus lost"},
           InterruptionCase{CurvePresetPointerInterruption::Navigation,
                            "navigation"},
           InterruptionCase{CurvePresetPointerInterruption::CameraChanged,
                            "camera changed"},
           InterruptionCase{CurvePresetPointerInterruption::ViewChanged,
                            "view changed"},
           InterruptionCase{CurvePresetPointerInterruption::NavigationGizmo,
                            "navigation gizmo"},
           InterruptionCase{CurvePresetPointerInterruption::Abandoned,
                            "abandoned"},
       }) {
    SCOPED_TRACE(interruption.label);
    ViewportInteractionHarness harness;
    ASSERT_TRUE(
        harness.editor.tools().activate(quader::ViewportToolId::Curve).ok());
    ASSERT_TRUE(harness.session.curve()
                    .set_creation_kind(quader::CurveCreationKind::Circle)
                    .ok);
    configure_curve_preset_viewport(harness);

    std::uint64_t sequence = 300u;
    constexpr quader::QVec2 kLocal{430.0f, 320.0f};
    (void)dispatch_curve_preset_pointer(harness, sequence, true, true, false,
                                        kLocal);
    ASSERT_TRUE(harness.session.native_tools().pointer_dragging());

    CurvePresetPointerOptions options;
    bool down = true;
    switch (interruption.kind) {
    case CurvePresetPointerInterruption::PointerCanceled:
      harness.input.pointer_interaction_canceled = true;
      break;
    case CurvePresetPointerInterruption::FocusLost:
      harness.input.viewport_focused = false;
      break;
    case CurvePresetPointerInterruption::Navigation:
      harness.input_result.navigation_active = true;
      break;
    case CurvePresetPointerInterruption::CameraChanged:
      harness.input_result.camera_changed = true;
      break;
    case CurvePresetPointerInterruption::ViewChanged:
      harness.input_result.view_changed = true;
      break;
    case CurvePresetPointerInterruption::NavigationGizmo:
      options.navigation_gizmo_consumed = true;
      break;
    case CurvePresetPointerInterruption::Abandoned:
      down = false;
      break;
    }
    (void)dispatch_curve_preset_pointer(harness, sequence, false, down, false,
                                        kLocal, options);
    EXPECT_FALSE(harness.session.native_tools().pointer_dragging());
    EXPECT_EQ(harness.session.curve().point_count(), 0u);
    EXPECT_FALSE(harness.session.curve()
                     .preview_state()
                     .preset_hover_document.has_value());

    harness.input.pointer_interaction_canceled = false;
    harness.input.viewport_focused = true;
    harness.input_result = {};
    (void)dispatch_curve_preset_pointer(harness, sequence, false, false, true,
                                        kLocal);
    EXPECT_EQ(harness.session.curve().point_count(), 0u);
    EXPECT_FALSE(harness.session.native_tools().pointer_dragging());
  }
}

TEST(ViewportInteractionControllerTests,
     CurvePresetPressUsesExactReleaseProvenanceAndExplicitDragReset) {
  ViewportInteractionHarness harness;
  ASSERT_TRUE(
      harness.editor.tools().activate(quader::ViewportToolId::Curve).ok());
  ASSERT_TRUE(harness.session.curve()
                  .set_creation_kind(quader::CurveCreationKind::Oval)
                  .ok);
  configure_curve_preset_viewport(harness);

  std::uint64_t sequence = 500u;
  constexpr quader::QVec2 kLocal{410.0f, 310.0f};
  (void)dispatch_curve_preset_pointer(harness, sequence, true, true, false,
                                      kLocal);
  ASSERT_TRUE(harness.session.native_tools().pointer_dragging());
  (void)dispatch_curve_preset_pointer(harness, sequence, false, false, true,
                                      kLocal, {.release_sequence = 499u});
  EXPECT_FALSE(harness.session.native_tools().pointer_dragging());
  EXPECT_EQ(harness.session.curve().point_count(), 0u);

  // A later release cannot complete the canceled press.
  (void)dispatch_curve_preset_pointer(harness, sequence, false, false, true,
                                      kLocal);
  EXPECT_EQ(harness.session.curve().point_count(), 0u);

  (void)dispatch_curve_preset_pointer(harness, sequence, true, true, false,
                                      kLocal);
  ASSERT_TRUE(harness.session.native_tools().pointer_dragging());
  ASSERT_TRUE(harness.session.curve()
                  .preview_state()
                  .preset_hover_document.has_value());
  harness.session.native_tools().clear_pointer_drag_state();
  EXPECT_FALSE(harness.session.native_tools().pointer_dragging());
  EXPECT_FALSE(harness.session.curve()
                   .preview_state()
                   .preset_hover_document.has_value());
  (void)dispatch_curve_preset_pointer(harness, sequence, false, false, true,
                                      kLocal);
  EXPECT_EQ(harness.session.curve().point_count(), 0u);
}
