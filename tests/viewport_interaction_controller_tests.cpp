////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"
#include "api/quader_command_ids.hpp"
#include "api/quader_api_session_internal_access.hpp"
#include "assets/default_material_catalog.hpp"
#include "assets/mesh_asset.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/interactions/transform_gizmo.hpp"
#include "editor/pivot/pivot_tool_service.hpp"
#include "editor/viewport/viewport_tool_overlay_projection.hpp"
#include "editor/tools/viewport_tool.hpp"
#include "editor/tools/viewport_tool_ports.hpp"
#include "editor/viewport/viewport_interaction_controller.hpp"
#include "editor/viewport/viewport_selection_interaction.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/session/project_session.hpp"
#include "scene/scene_bounds.hpp"
#include "scene/components.hpp"
#include "support/viewport_overlay_test_helpers.hpp"
#include "ui/viewport_hover_controller.hpp"
#include "viewport/viewport_host_interaction_policy.hpp"
#include "viewport/viewport_camera_adapter.hpp"
#include "viewport/viewport_camera_controller.hpp"
#include "viewport/viewport_input_router.hpp"
#include "viewport/viewport_settings.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <unordered_set>
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
              .vertex_radius =
                  settings.float_value(
                      quader::ViewportFloatSettingId::PickVertexRadius),
              .edge_radius =
                  settings.float_value(
                      quader::ViewportFloatSettingId::PickEdgeRadius),
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
  const quader::ViewportBeforeToolInteractionResult press =
      harness.controller.handle_before_active_tool(
          harness.editor, harness.frame(true, true, false, start));
  ASSERT_TRUE(press.gizmo_consumed);

  harness.input.mouse_local = update;
  harness.input.mouse_screen = update;
  const quader::ViewportBeforeToolInteractionResult drag =
      harness.controller.handle_before_active_tool(
          harness.editor, harness.frame(false, true, false, start));
  EXPECT_TRUE(drag.gizmo_consumed);

  const quader::ViewportBeforeToolInteractionResult release =
      harness.controller.handle_before_active_tool(
          harness.editor, harness.frame(false, false, true, start));
  EXPECT_TRUE(release.gizmo_consumed);
}

std::vector<quader::EntityId>
selected_entities(const ViewportInteractionHarness &harness) {
  return harness.session.scene().selected_entities();
}

bool has_snap_target_vertex_point(const quader::QuaderOverlayFrame &frame) {
  return std::any_of(frame.points.begin(), frame.points.end(),
                     [](const quader::QuaderOverlayPoint &point) {
                       return point.layer ==
                              quader::QuaderOverlayLayer::SnapTargetVertex;
                     });
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

    const quader_editor_core::TransformGizmoFrame move_frame =
        quader_editor_core::build_transform_gizmo_frame(test_gizmo_input(
            quader_editor_core::ViewportInteractionTool::Move, harness));
    ASSERT_TRUE(move_frame.info.ok);
    const quader::ViewportOverlayFrame move_overlay =
        build_gizmo_overlay(move_frame, harness);

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
    EXPECT_TRUE(std::none_of(
        move_overlay.triangles.begin(), move_overlay.triangles.end(),
        [](const quader::ViewportOverlayTriangle &triangle) {
          return triangle.color == quader::kOverlayToolCenterColor;
        }));
    EXPECT_TRUE(
        std::none_of(move_overlay.lines.begin(), move_overlay.lines.end(),
                     [](const quader::ViewportOverlayLine &line) {
                       return line.color == quader::kOverlayToolLineShadowColor;
                     }));

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
     PlainObjectDoubleClickEntersSelectedInstanceScope) {
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

  const quader::QVec2 hit{400.0f, 300.0f};
  harness.input.mouse_local = hit;
  harness.input.mouse_screen = hit;
  const quader::ViewportInteractionFrame press_frame =
      harness.frame(true, true, false, hit);
  const quader::ViewportBeforeToolInteractionResult before_tool =
      harness.controller.handle_before_active_tool(harness.editor, press_frame);
  harness.controller.handle_after_active_tool(
      harness.editor, press_frame, quader::ViewportToolResult{}, before_tool);

  quader::ViewportInteractionFrame release_frame =
      harness.frame(false, false, true, hit);
  release_frame.left_click_count = 2;
  harness.controller.handle_after_active_tool(
      harness.editor, release_frame, quader::ViewportToolResult{},
      quader::ViewportBeforeToolInteractionResult{});

  const quader::QuaderInstanceEditState edit =
      harness.editor.modeling().instance_edit_state();
  ASSERT_TRUE(edit.active);
  EXPECT_EQ(edit.occurrence.id, occurrence);
  EXPECT_TRUE(harness.session.scene().selected_entities().empty());
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

TEST(ViewportInteractionControllerTests, AltClickEdgeSelectsLoopThroughViewportFallback) {
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
     AltHoverObjectModeUsesFacePlaneHoverOverlay) {
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
    EXPECT_TRUE(has_triangle_style(
        overlay, quader::ViewportOverlayStyleRole::FacePlaneHoverFace));
    EXPECT_TRUE(has_line_style(
        overlay, quader::ViewportOverlayStyleRole::FacePlaneHoverWire));
    EXPECT_FALSE(has_triangle_style(
        overlay, quader::ViewportOverlayStyleRole::RemoveSelectionFace));
}

TEST(ViewportInteractionControllerTests,
     AltRightClickCutsSelectedMeshByHoveredFacePlane) {
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
    const std::unordered_set<int> object_ids_before{
        target.modeling_object_id, second_target.modeling_object_id,
        reference.value().modeling_object_id};

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
    EXPECT_EQ(harness.editor.modeling().objects().size(), object_count_before)
        << "Alt face-plane cut must require the exact Alt gesture";

    harness.input.modifiers.primary = false;
    harness.controller.handle_after_active_tool(
        harness.editor,
        harness.frame(false, false, false, {}, true, true, false,
                      face_point.position),
        quader::ViewportToolResult{},
        quader::ViewportBeforeToolInteractionResult{});

    const std::vector<quader::QuaderModelingObjectRecord> objects =
        harness.editor.modeling().objects();
    EXPECT_EQ(objects.size(), object_count_before + 2U);
    std::vector<quader::QuaderModelingObjectRecord> created_cut_halves;
    for (const quader::QuaderModelingObjectRecord &object : objects) {
      if (!object_ids_before.contains(object.modeling_object_id)) {
        created_cut_halves.push_back(object);
      }
    }
    ASSERT_EQ(created_cut_halves.size(), 2U);
    const auto expect_created_transform =
        [&](std::size_t index, quader::QVec3 expected_position) {
          const quader::TransformComponent *cut_half_transform =
              harness.session.scene().transform(created_cut_halves[index].entity);
          ASSERT_NE(cut_half_transform, nullptr);
          EXPECT_NEAR(cut_half_transform->position.x, expected_position.x,
                      0.001f);
          EXPECT_NEAR(cut_half_transform->position.y, expected_position.y,
                      0.001f);
          EXPECT_NEAR(cut_half_transform->position.z, expected_position.z,
                      0.001f);
        };
    expect_created_transform(0, target_transform.position);
    expect_created_transform(1, second_target_transform.position);
    const quader::QuaderSelectionSummary selection =
        harness.editor.selection().summary();
    EXPECT_EQ(selection.mode, quader::QuaderSelectionMode::Object);
    EXPECT_FALSE(selection.has_selection);
    EXPECT_EQ(selection.selected_object_count, 0u);
    EXPECT_EQ(selection.selected_vertex_count, 0u);
    EXPECT_EQ(selection.selected_edge_count, 0u);
    EXPECT_EQ(selection.selected_face_count, 0u);
    EXPECT_TRUE(harness.editor.history().can_undo());

    ASSERT_TRUE(harness.editor.selection()
                    .select_object(created_cut_halves.front().entity)
                    .ok());
    const quader::QuaderSelectionSummary selected_cut_half =
        harness.editor.selection().summary();
    EXPECT_EQ(selected_cut_half.selected_object_count, 1u);
    EXPECT_EQ(harness.session.scene().selected_entities().front(),
              created_cut_halves.front().entity);
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
    EXPECT_FALSE(harness.editor.tools().active_settings().active);
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

TEST(ViewportBoxOverlayBuilderTests, EmitsDashedFootprintOutlineWithoutFill) {
    quader::ViewportOverlayCamera camera;
    camera.eye = {0.0f, 10.0f, 0.0f};
    camera.forward = {0.0f, -1.0f, 0.0f};
    camera.right = {1.0f, 0.0f, 0.0f};
    camera.up = {0.0f, 0.0f, 1.0f};
    camera.viewport_size = {800.0f, 600.0f};
    camera.projection = quader::ViewportOverlayProjection::Orthographic;
    camera.orthographic_size = 8.0f;

    quader::ModelingBoxPreview footprint;
    footprint.ok = true;
    footprint.valid = true;
    footprint.drag_state = quader::ModelingBoxDragState::Idle;
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
    ASSERT_GT(footprint_overlay.lines.size(), 4u);
    EXPECT_TRUE(footprint_overlay.triangles.empty());
    for (const quader::ViewportOverlayLine &line : footprint_overlay.lines) {
      EXPECT_EQ(line.layer, quader::ViewportOverlayLayer::ToolPreviewLine);
      EXPECT_EQ(line.color, quader::kOverlayToolBoxPreviewLineColor);
      EXPECT_FLOAT_EQ(line.width_pixels, 2.0f);
      EXPECT_EQ(line.style_role,
                quader::ViewportOverlayStyleRole::ExplicitColor);
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
     PendingChordOwnsContinuationBeforeStatelessQuickAction) {
  const quader::QuaderInputChord continuation{
      .device = quader::QuaderInputDevice::Keyboard,
      .key = quader::QuaderKeyCode::D,
  };
  int stateless_action_attempts = 0;
  int stateful_dispatches = 0;

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

  request.local_visibility_revision = 12u;
  const quader::ViewportHoverWorkKey local_visibility_changed =
      controller.make_key(camera, request);
  EXPECT_FALSE(unsnapped.matches(local_visibility_changed));
  EXPECT_TRUE(controller.claim(local_visibility_changed));
  EXPECT_FALSE(controller.claim(local_visibility_changed));
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
     ShiftSampleUsesFacePickAcrossSelectionModesWithoutSelectionOrHistory) {
  ProjectViewportInteractionHarness harness;
  harness.initialize_with_second_material();
  const quader::QuaderModelingObjectRef box =
      create_project_box_at(harness, {});
  ASSERT_TRUE(harness.editor.project_assets()
                  .set_active_material(harness.second_material())
                  .ok());
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Edge)
                  .ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Edge);
  harness.scene_session().history().clear();
  const quader::QuaderSelectionSummary selection_before =
      harness.editor.selection().summary();

  harness.input.sample_face_material_pressed = true;
  harness.controller.handle_after_active_tool(
      harness.editor, harness.frame(), quader::ViewportToolResult{},
      quader::ViewportBeforeToolInteractionResult{});
  harness.input.sample_face_material_pressed = false;

  const quader::QuaderApiResult<quader::QuaderProjectMaterialInfo> active =
      harness.editor.project_assets().active_material();
  ASSERT_TRUE(active.ok()) << active.error_message();
  EXPECT_EQ(active.value().id, default_project_material_id());
  const quader::QuaderSelectionSummary selection_after =
      harness.editor.selection().summary();
  EXPECT_EQ(selection_after.mode, selection_before.mode);
  EXPECT_EQ(selection_after.has_selection, selection_before.has_selection);
  EXPECT_EQ(selection_after.selected_object_count,
            selection_before.selected_object_count);
  EXPECT_EQ(selection_after.selected_edge_count,
            selection_before.selected_edge_count);
  EXPECT_FALSE(harness.editor.history().can_undo());
  const auto &stats = harness.controller.material_interaction_work_stats();
  EXPECT_EQ(stats.face_pick_count, 1u);
  EXPECT_EQ(stats.sample_request_count, 1u);
  EXPECT_EQ(stats.apply_selection_command_count, 0u);
  EXPECT_EQ(stats.apply_face_to_mesh_request_count, 0u);

  const std::vector<quader::QuaderElementId> faces =
      authored_face_ids(harness, box);
  ASSERT_FALSE(faces.empty());
  const auto sampled = harness.editor.modeling().project_material_for_face(
      box.modeling_object_id, faces.front());
  ASSERT_TRUE(sampled.ok()) << sampled.error_message();
  EXPECT_EQ(sampled.value().id, default_project_material_id());
}

TEST(ViewportInteractionControllerMaterialTests,
     CtrlSingleWaitsForDoubleClickWindowThenAppliesOnceToMultiSelection) {
  ProjectViewportInteractionHarness harness;
  harness.initialize_with_second_material();
  const quader::QuaderModelingObjectRef left =
      create_project_box_at(harness, {-3.0f, 0.0f, 0.0f}, "Left");
  const quader::QuaderModelingObjectRef right =
      create_project_box_at(harness, {3.0f, 0.0f, 0.0f}, "Right");
  ASSERT_TRUE(harness.editor.project_assets()
                  .set_active_material(harness.second_material())
                  .ok());
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Object)
                  .ok());
  ASSERT_TRUE(harness.editor.selection().select_objects({left, right}).ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
  harness.scene_session().history().clear();

  harness.monotonic_time_seconds = 10.0;
  harness.input.apply_active_material_to_selection_pressed = true;
  harness.input.apply_active_material_delay_for_double_click = true;
  harness.input.delta_seconds = 0.0001f;
  harness.controller.handle_after_active_tool(
      harness.editor, harness.frame(), quader::ViewportToolResult{},
      quader::ViewportBeforeToolInteractionResult{});
  harness.input.apply_active_material_to_selection_pressed = false;
  EXPECT_EQ(harness.controller.material_interaction_work_stats()
                .apply_selection_command_count,
            0u);
  EXPECT_FALSE(harness.editor.history().can_undo());

  harness.monotonic_time_seconds = 10.299;
  harness.input.delta_seconds = 20.0f;
  harness.controller.handle_after_active_tool(
      harness.editor, harness.frame(), quader::ViewportToolResult{},
      quader::ViewportBeforeToolInteractionResult{});
  EXPECT_EQ(harness.controller.material_interaction_work_stats()
                .apply_selection_command_count,
            0u);
  harness.monotonic_time_seconds = 25.0;
  harness.input.delta_seconds = 0.0f;
  harness.controller.handle_after_active_tool(
      harness.editor, harness.frame(), quader::ViewportToolResult{},
      quader::ViewportBeforeToolInteractionResult{});
  EXPECT_EQ(harness.controller.material_interaction_work_stats()
                .apply_selection_command_count,
            1u);
  ASSERT_TRUE(harness.editor.history().can_undo());
  EXPECT_EQ(harness.editor.query().history().undo_label, "Assign Material");
  EXPECT_EQ(harness.editor.selection().summary().selected_object_count, 2u);

  for (const quader::QuaderModelingObjectRef object : {left, right}) {
    const std::vector<quader::QuaderElementId> faces =
        authored_face_ids(harness, object);
    ASSERT_FALSE(faces.empty());
    const auto material = harness.editor.modeling().project_material_for_face(
        object.modeling_object_id, faces.front());
    ASSERT_TRUE(material.ok()) << material.error_message();
    EXPECT_EQ(material.value().id, harness.second_material());
  }

  harness.monotonic_time_seconds = 50.0;
  harness.controller.handle_after_active_tool(
      harness.editor, harness.frame(), quader::ViewportToolResult{},
      quader::ViewportBeforeToolInteractionResult{});
  EXPECT_EQ(harness.controller.material_interaction_work_stats()
                .apply_selection_command_count,
            1u);
}

TEST(ViewportInteractionControllerMaterialTests,
     NonMouseApplyBindingExecutesImmediatelyWithoutDoubleClickDelay) {
  ProjectViewportInteractionHarness harness;
  harness.initialize_with_second_material();
  const quader::QuaderModelingObjectRef box =
      create_project_box_at(harness, {});
  ASSERT_TRUE(harness.editor.project_assets()
                  .set_active_material(harness.second_material())
                  .ok());
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Object)
                  .ok());
  ASSERT_TRUE(harness.editor.selection().select_object(box).ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
  harness.scene_session().history().clear();

  harness.input.apply_active_material_to_selection_pressed = true;
  harness.input.apply_active_material_delay_for_double_click = false;
  harness.controller.handle_after_active_tool(
      harness.editor, harness.frame(), quader::ViewportToolResult{},
      quader::ViewportBeforeToolInteractionResult{});
  harness.input.apply_active_material_to_selection_pressed = false;

  EXPECT_EQ(harness.controller.material_interaction_work_stats()
                .apply_selection_command_count,
            1u);
  ASSERT_TRUE(harness.editor.history().can_undo());
  const std::vector<quader::QuaderElementId> faces =
      authored_face_ids(harness, box);
  ASSERT_FALSE(faces.empty());
  const auto material = harness.editor.modeling().project_material_for_face(
      box.modeling_object_id, faces.front());
  ASSERT_TRUE(material.ok()) << material.error_message();
  EXPECT_EQ(material.value().id, harness.second_material());
}

TEST(ViewportInteractionControllerMaterialTests,
     CtrlDoubleCancelsPendingSingleAndAppliesClickedFaceMaterialImmediately) {
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

  ASSERT_TRUE(harness.editor.project_assets()
                  .set_active_material(harness.second_material())
                  .ok());
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Face)
                  .ok());
  ASSERT_TRUE(harness.editor.selection()
                  .select_face(box, hover.target.face_id)
                  .ok());
  ASSERT_TRUE(harness.editor.modeling().apply_active_material().ok());
  ASSERT_TRUE(harness.editor.project_assets()
                  .set_active_material(default_project_material_id())
                  .ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Face);
  harness.scene_session().history().clear();
  const quader::QuaderSelectionSummary selection_before =
      harness.editor.selection().summary();

  harness.monotonic_time_seconds = 10.0;
  harness.input.apply_active_material_to_selection_pressed = true;
  harness.input.apply_active_material_delay_for_double_click = true;
  harness.controller.handle_after_active_tool(
      harness.editor, harness.frame(), quader::ViewportToolResult{},
      quader::ViewportBeforeToolInteractionResult{});
  harness.input.apply_active_material_to_selection_pressed = false;
  harness.monotonic_time_seconds = 10.1;
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
  EXPECT_EQ(stats.apply_selection_command_count, 0u);
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
  const auto active = harness.editor.project_assets().active_material();
  ASSERT_TRUE(active.ok()) << active.error_message();
  EXPECT_EQ(active.value().id, default_project_material_id());

  ASSERT_TRUE(harness.editor.history().undo().ok());
  EXPECT_FALSE(harness.editor.history().can_undo())
      << "the delayed Ctrl single must not leave a second undo step";

  harness.scene_session().history().clear();
  harness.monotonic_time_seconds = 30.0;
  harness.input.apply_active_material_to_selection_pressed = true;
  harness.input.apply_active_material_delay_for_double_click = true;
  harness.input.apply_face_material_to_mesh_pressed = true;
  harness.controller.handle_after_active_tool(
      harness.editor, harness.frame(), quader::ViewportToolResult{},
      quader::ViewportBeforeToolInteractionResult{});
  harness.input.apply_active_material_to_selection_pressed = false;
  harness.input.apply_face_material_to_mesh_pressed = false;
  harness.monotonic_time_seconds = 40.0;
  harness.controller.handle_after_active_tool(
      harness.editor, harness.frame(), quader::ViewportToolResult{},
      quader::ViewportBeforeToolInteractionResult{});
  EXPECT_EQ(harness.controller.material_interaction_work_stats()
                .apply_selection_command_count,
            0u)
      << "a click and double-click delivered in one frame still prefer double";
  EXPECT_EQ(harness.controller.material_interaction_work_stats()
                .apply_face_to_mesh_request_count,
            2u);
}

TEST(ViewportInteractionControllerMaterialTests,
     PendingSingleCancelsOnEveryViewportTakeoverAndExplicitReset) {
  ProjectViewportInteractionHarness harness;
  harness.initialize_with_second_material();
  const quader::QuaderModelingObjectRef box =
      create_project_box_at(harness, {});
  ASSERT_TRUE(harness.editor.project_assets()
                  .set_active_material(harness.second_material())
                  .ok());
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Object)
                  .ok());
  ASSERT_TRUE(harness.editor.selection().select_object(box).ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
  harness.scene_session().history().clear();
  const auto run = [&](const quader::ViewportToolResult &tool_result,
                       const quader::ViewportBeforeToolInteractionResult
                           &before_tool) {
    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(), tool_result, before_tool);
  };
  const auto run_idle = [&]() {
    run(quader::ViewportToolResult{},
        quader::ViewportBeforeToolInteractionResult{});
  };
  const auto schedule = [&]() {
    harness.monotonic_time_seconds += 10.0;
    harness.input.apply_active_material_to_selection_pressed = true;
    harness.input.apply_active_material_delay_for_double_click = true;
    run_idle();
    harness.input.apply_active_material_to_selection_pressed = false;
  };
  const auto expect_canceled = [&]() {
    harness.monotonic_time_seconds += 1.0;
    run_idle();
    EXPECT_EQ(harness.controller.material_interaction_work_stats()
                  .apply_selection_command_count,
              0u);
    harness.controller.reset(&harness.editor);
  };

  const std::array input_blockers{
      &quader::ViewportInputFrame::text_input_active,
      &quader::ViewportInputFrame::navigation_active,
      &quader::ViewportInputFrame::modal_tool_active,
      &quader::ViewportInputFrame::active_tool_owns_viewport,
      &quader::ViewportInputFrame::active_transform_drag,
      &quader::ViewportInputFrame::operation_preview_active,
  };
  for (bool quader::ViewportInputFrame::*blocker : input_blockers) {
    schedule();
    harness.monotonic_time_seconds += 0.1;
    harness.input.*blocker = true;
    run_idle();
    harness.input.*blocker = false;
    expect_canceled();
  }

  schedule();
  harness.monotonic_time_seconds += 0.1;
  harness.input.viewport_focused = false;
  run_idle();
  harness.input.viewport_focused = true;
  expect_canceled();

  schedule();
  harness.monotonic_time_seconds += 0.1;
  harness.input_result.navigation_active = true;
  run_idle();
  harness.input_result.navigation_active = false;
  expect_canceled();

  schedule();
  harness.monotonic_time_seconds += 0.1;
  run(quader::ViewportToolResult{},
      quader::ViewportBeforeToolInteractionResult{.gizmo_consumed = true});
  expect_canceled();

  schedule();
  harness.monotonic_time_seconds += 0.1;
  run(quader::ViewportToolResult{.handled = true},
      quader::ViewportBeforeToolInteractionResult{});
  expect_canceled();

  schedule();
  harness.controller.cancel_pending_material_interaction();
  expect_canceled();

  schedule();
  harness.controller.reset(&harness.editor);
  expect_canceled();
}

TEST(ViewportInteractionControllerMaterialTests,
     PendingSingleCancelsWhenSelectionIntentChanges) {
  ProjectViewportInteractionHarness harness;
  harness.initialize_with_second_material();
  const quader::QuaderModelingObjectRef box =
      create_project_box_at(harness, {});
  ASSERT_TRUE(harness.editor.project_assets()
                  .set_active_material(harness.second_material())
                  .ok());
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Object)
                  .ok());
  ASSERT_TRUE(harness.editor.selection().select_object(box).ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
  harness.scene_session().history().clear();

  harness.monotonic_time_seconds = 10.0;
  harness.input.apply_active_material_to_selection_pressed = true;
  harness.input.apply_active_material_delay_for_double_click = true;
  harness.controller.handle_after_active_tool(
      harness.editor, harness.frame(), quader::ViewportToolResult{},
      quader::ViewportBeforeToolInteractionResult{});
  harness.input.apply_active_material_to_selection_pressed = false;
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  harness.scene_session().history().clear();
  harness.monotonic_time_seconds = 11.0;
  harness.controller.handle_after_active_tool(
      harness.editor, harness.frame(), quader::ViewportToolResult{},
      quader::ViewportBeforeToolInteractionResult{});

  EXPECT_EQ(harness.controller.material_interaction_work_stats()
                .apply_selection_command_count,
            0u);
  EXPECT_FALSE(harness.editor.history().can_undo());
  const std::vector<quader::QuaderElementId> faces =
      authored_face_ids(harness, box);
  ASSERT_FALSE(faces.empty());
  const auto material = harness.editor.modeling().project_material_for_face(
      box.modeling_object_id, faces.front());
  ASSERT_TRUE(material.ok()) << material.error_message();
  EXPECT_EQ(material.value().id, default_project_material_id());
}

TEST(ViewportInteractionControllerMaterialTests,
     PendingSingleCancelsWhenActiveMaterialIntentChanges) {
  ProjectViewportInteractionHarness harness;
  harness.initialize_with_second_material();
  const quader::QuaderModelingObjectRef box =
      create_project_box_at(harness, {});
  ASSERT_TRUE(harness.editor.project_assets()
                  .set_active_material(harness.second_material())
                  .ok());
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Object)
                  .ok());
  ASSERT_TRUE(harness.editor.selection().select_object(box).ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
  harness.scene_session().history().clear();

  harness.monotonic_time_seconds = 10.0;
  harness.input.apply_active_material_to_selection_pressed = true;
  harness.input.apply_active_material_delay_for_double_click = true;
  harness.controller.handle_after_active_tool(
      harness.editor, harness.frame(), quader::ViewportToolResult{},
      quader::ViewportBeforeToolInteractionResult{});
  harness.input.apply_active_material_to_selection_pressed = false;
  ASSERT_TRUE(harness.editor.project_assets()
                  .set_active_material(default_project_material_id())
                  .ok());
  harness.monotonic_time_seconds = 11.0;
  harness.controller.handle_after_active_tool(
      harness.editor, harness.frame(), quader::ViewportToolResult{},
      quader::ViewportBeforeToolInteractionResult{});

  EXPECT_EQ(harness.controller.material_interaction_work_stats()
                .apply_selection_command_count,
            0u);
  EXPECT_FALSE(harness.editor.history().can_undo());
  const std::vector<quader::QuaderElementId> faces =
      authored_face_ids(harness, box);
  ASSERT_FALSE(faces.empty());
  const auto material = harness.editor.modeling().project_material_for_face(
      box.modeling_object_id, faces.front());
  ASSERT_TRUE(material.ok()) << material.error_message();
  EXPECT_EQ(material.value().id, default_project_material_id());
}

TEST(ViewportInteractionControllerMaterialTests,
     PendingSingleCancelsWhenActiveSceneIntentChanges) {
  ProjectViewportInteractionHarness harness;
  harness.initialize_with_second_material();
  const quader::QuaderModelingObjectRef box =
      create_project_box_at(harness, {});
  ASSERT_TRUE(harness.editor.project_assets()
                  .set_active_material(harness.second_material())
                  .ok());
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Object)
                  .ok());
  ASSERT_TRUE(harness.editor.selection().select_object(box).ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
  harness.scene_session().history().clear();
  const std::optional<quader::SceneSessionId> original_scene =
      harness.editor.scene_documents().active_scene_id();
  ASSERT_TRUE(original_scene.has_value());

  harness.monotonic_time_seconds = 10.0;
  harness.input.apply_active_material_to_selection_pressed = true;
  harness.input.apply_active_material_delay_for_double_click = true;
  harness.controller.handle_after_active_tool(
      harness.editor, harness.frame(), quader::ViewportToolResult{},
      quader::ViewportBeforeToolInteractionResult{});
  harness.input.apply_active_material_to_selection_pressed = false;
  ASSERT_TRUE(harness.editor.scene_documents().new_scene().ok());
  const std::optional<quader::SceneSessionId> replacement_scene =
      harness.editor.scene_documents().active_scene_id();
  ASSERT_TRUE(replacement_scene.has_value());
  EXPECT_NE(*replacement_scene, *original_scene);
  harness.monotonic_time_seconds = 11.0;
  harness.controller.handle_after_active_tool(
      harness.editor, harness.frame(), quader::ViewportToolResult{},
      quader::ViewportBeforeToolInteractionResult{});

  EXPECT_EQ(harness.controller.material_interaction_work_stats()
                .apply_selection_command_count,
            0u);
  EXPECT_FALSE(harness.editor.history().can_undo());
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
  EXPECT_EQ(idle.sample_request_count, 0u);
  EXPECT_EQ(idle.apply_selection_command_count, 0u);

  harness.input.mouse_local = {0.0f, 0.0f};
  harness.input.mouse_screen = harness.input.mouse_local;
  harness.input.sample_face_material_pressed = true;
  harness.controller.handle_after_active_tool(
      harness.editor, harness.frame(false, false, false, {}, false, false,
                                    false, harness.input.mouse_local),
      quader::ViewportToolResult{},
      quader::ViewportBeforeToolInteractionResult{});
  harness.input.sample_face_material_pressed = false;
  EXPECT_EQ(harness.controller.material_interaction_work_stats()
                .face_pick_count,
            1u);
  EXPECT_EQ(harness.controller.material_interaction_work_stats()
                .sample_request_count,
            0u);
  EXPECT_FALSE(harness.editor.history().can_undo());
}

TEST(ViewportInteractionControllerMaterialTests,
     GroupModeSamplesAndAppliesClickedFaceMaterialWithoutSelectionFallback) {
  ProjectViewportInteractionHarness harness;
  harness.initialize_with_second_material();
  const quader::QuaderModelingObjectRef box =
      create_project_box_at(harness, {});
  ASSERT_TRUE(harness.editor.project_assets()
                  .set_active_material(harness.second_material())
                  .ok());
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Group)
                  .ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Group);
  harness.scene_session().history().clear();

  harness.input.sample_face_material_pressed = true;
  harness.controller.handle_after_active_tool(
      harness.editor, harness.frame(), quader::ViewportToolResult{},
      quader::ViewportBeforeToolInteractionResult{});
  harness.input.sample_face_material_pressed = false;
  const auto sampled_active =
      harness.editor.project_assets().active_material();
  ASSERT_TRUE(sampled_active.ok()) << sampled_active.error_message();
  EXPECT_EQ(sampled_active.value().id, default_project_material_id());
  EXPECT_EQ(harness.controller.material_interaction_work_stats()
                .sample_request_count,
            1u);
  EXPECT_FALSE(harness.editor.history().can_undo());

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
  ASSERT_TRUE(harness.editor.project_assets()
                  .set_active_material(harness.second_material())
                  .ok());
  ASSERT_TRUE(harness.editor.modeling().apply_active_material().ok());
  ASSERT_TRUE(harness.editor.project_assets()
                  .set_active_material(default_project_material_id())
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
