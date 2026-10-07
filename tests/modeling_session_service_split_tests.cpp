////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"
#include "api/quader_api_session_internal_access.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/modes/editor_mode_service.hpp"
#include "support/curve_modeling_test_support.hpp"
#include "support/command_observable_state.hpp"
#include "assets/asset_registry.hpp"
#include "assets/mesh_asset_payload.hpp"
#include "editor/commands/modeling/scene_aware_delete_selection_command.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/history/modeling_memento_command.hpp"
#include "editor/material/material_reference_service.hpp"
#include "editor/modeling/runtime/modeling_object_binding_registry.hpp"
#include "editor/modeling/services/modeling_host_publication_transaction.hpp"
#include "editor/modeling/services/modeling_instant_operation_catalog.hpp"
#include "editor/modeling/services/modeling_operation_settings.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/modeling/modeling_viewport_camera_adapter.hpp"
#include "editor/modeling/services/modeling_session_services.hpp"
#include "editor/selection/scene_selection_application.hpp"
#include "editor/selection/modeling_selection_projection_snapshot.hpp"
#include "editor/selection/selection_projection_service.hpp"
#include "editor/tools/viewport_tool_descriptor_catalog.hpp"
#include "scene/scene_document.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <editor/interactions/cut_tool.hpp>
#include <editor/modeling/runtime/session.hpp>
#include <quader/modeling/neutral/polygon/document.hpp>

namespace quader {
namespace {

TEST(ModelingViewportCameraAdapterTests,
     PreservesAffineProjectionAndClampsDegenerateExtents) {
  ModelingViewportCamera source;
  source.orthographic = true;
  source.affine_model_space = true;
  source.viewport_size = {0.0f, -20.0f};
  source.orthographic_size = 0.0;
  source.fov_degrees = 67.0;
  source.origin = {1.0f, 2.0f, 3.0f};

  const quader_editor_core::ViewportCameraState runtime =
      modeling_runtime_camera(source);
  EXPECT_EQ(runtime.projection,
            quader_editor_core::CameraProjection::Orthographic);
  EXPECT_TRUE(runtime.affine_model_space);
  EXPECT_FLOAT_EQ(runtime.viewport_size.x, 1.0f);
  EXPECT_FLOAT_EQ(runtime.viewport_size.y, 1.0f);
  EXPECT_DOUBLE_EQ(runtime.orthographic_size, 0.0001);
  EXPECT_DOUBLE_EQ(runtime.fov_degrees, 67.0);
  EXPECT_FLOAT_EQ(runtime.transform.origin.x, 1.0f);
  EXPECT_FLOAT_EQ(runtime.transform.origin.y, 2.0f);
  EXPECT_FLOAT_EQ(runtime.transform.origin.z, 3.0f);
}

void expect_vec3_eq(QVec3 actual, QVec3 expected) {
  EXPECT_FLOAT_EQ(actual.x, expected.x);
  EXPECT_FLOAT_EQ(actual.y, expected.y);
  EXPECT_FLOAT_EQ(actual.z, expected.z);
}

ModelingSelectionProjectionSnapshot selection_snapshot_from_session(
    const quader_editor_core::EditorSession &session) {
  return ModelingSelectionProjectionSnapshot(
      std::vector<quader_editor_core::PolygonObject>(session.objects().begin(),
                                                     session.objects().end()),
      session.selection_mode(), session.active_object_id(),
      session.state().content_revision, session.state().selection_revision);
}

quader_poly::Document service_uv_peel_strip_document() {
  quader_poly::Document document;
  const std::array vertices{
      quader_poly::add_vertex(document, {-2.0f, -1.0f, 0.0f}),
      quader_poly::add_vertex(document, {0.0f, -1.0f, 0.0f}),
      quader_poly::add_vertex(document, {2.0f, -1.0f, 0.0f}),
      quader_poly::add_vertex(document, {-2.0f, 1.0f, 0.0f}),
      quader_poly::add_vertex(document, {0.0f, 1.0f, 0.0f}),
      quader_poly::add_vertex(document, {2.0f, 1.0f, 0.0f}),
  };
  (void)quader_poly::add_face(
      document, std::array{vertices[0], vertices[1], vertices[4], vertices[3]});
  (void)quader_poly::add_face(
      document, std::array{vertices[1], vertices[2], vertices[5], vertices[4]});
  return document;
}

void expect_selection_overlay_frame_eq(
    const quader_editor_core::SelectionOverlayFrame &actual,
    const quader_editor_core::SelectionOverlayFrame &expected) {
  ASSERT_EQ(actual.triangles.size(), expected.triangles.size());
  for (std::size_t index = 0; index < expected.triangles.size(); ++index) {
    SCOPED_TRACE(index);
    const auto &a = actual.triangles[index];
    const auto &b = expected.triangles[index];
    EXPECT_EQ(a.layer, b.layer);
    expect_vec3_eq(a.a, b.a);
    expect_vec3_eq(a.b, b.b);
    expect_vec3_eq(a.c, b.c);
    EXPECT_EQ(a.color_abgr, b.color_abgr);
    EXPECT_EQ(a.object_id, b.object_id);
    EXPECT_EQ(a.face_id, b.face_id);
    EXPECT_EQ(a.vertex_a, b.vertex_a);
    EXPECT_EQ(a.vertex_b, b.vertex_b);
    EXPECT_EQ(a.vertex_c, b.vertex_c);
  }
  ASSERT_EQ(actual.lines.size(), expected.lines.size());
  for (std::size_t index = 0; index < expected.lines.size(); ++index) {
    SCOPED_TRACE(index);
    const auto &a = actual.lines[index];
    const auto &b = expected.lines[index];
    EXPECT_EQ(a.layer, b.layer);
    expect_vec3_eq(a.start, b.start);
    expect_vec3_eq(a.end, b.end);
    EXPECT_EQ(a.color_abgr, b.color_abgr);
    EXPECT_FLOAT_EQ(a.width_pixels, b.width_pixels);
    EXPECT_EQ(a.object_id, b.object_id);
    EXPECT_EQ(a.face_id, b.face_id);
    EXPECT_EQ(a.edge, b.edge);
  }
  ASSERT_EQ(actual.points.size(), expected.points.size());
  for (std::size_t index = 0; index < expected.points.size(); ++index) {
    SCOPED_TRACE(index);
    const auto &a = actual.points[index];
    const auto &b = expected.points[index];
    EXPECT_EQ(a.layer, b.layer);
    expect_vec3_eq(a.position, b.position);
    EXPECT_EQ(a.color_abgr, b.color_abgr);
    EXPECT_FLOAT_EQ(a.size_pixels, b.size_pixels);
    EXPECT_EQ(a.object_id, b.object_id);
    EXPECT_EQ(a.vertex_id, b.vertex_id);
  }
  EXPECT_EQ(actual.hover_target_selected, expected.hover_target_selected);
  ASSERT_EQ(actual.hover_edge_dimension_context.has_value(),
            expected.hover_edge_dimension_context.has_value());
  if (expected.hover_edge_dimension_context.has_value()) {
    const auto &a = *actual.hover_edge_dimension_context;
    const auto &b = *expected.hover_edge_dimension_context;
    expect_vec3_eq(a.inside_reference, b.inside_reference);
    EXPECT_EQ(a.object_id, b.object_id);
    EXPECT_EQ(a.face_id, b.face_id);
    EXPECT_EQ(a.edge, b.edge);
  }
}

TEST(ModelingSessionServiceSplitTests,
     SemanticOverlayGeometrySurvivesSelectionModeAndActiveChanges) {
  using namespace quader_editor_core;
  PolygonObject object;
  object.id = 1;
  object.selected = true;
  object.component_edit_target = true;
  object.document = service_uv_peel_strip_document();
  const auto first_face = object.document.faces[0].id;
  const auto second_face = object.document.faces[1].id;
  const auto boundary_edge = quader_poly::make_edge(1, 2);
  const auto interior_edge = quader_poly::make_edge(2, 5);
  ModelingOverlayService overlay;
  std::uint64_t selection_revision = 1;
  const auto verify_frame = [&](SelectionMode mode, int active_id) {
    const ModelingSelectionProjectionSnapshot snapshot(
        {object}, mode, active_id, 7, selection_revision++);
    const auto &actual = overlay.selection_overlay_base_frame(snapshot);
    const auto expected = build_selection_overlay_frame(
        snapshot.objects(), mode, active_id, {});
    expect_selection_overlay_frame_eq(actual, expected);
    return actual;
  };
  object.selection.mode = quader_poly::SelectionMode::Face;
  object.selection.faces = {first_face, second_face};
  const auto warm_frame = verify_frame(SelectionMode::Face, object.id);
  const auto warm_work = overlay.selection_overlay_geometry_work_counters();
  EXPECT_EQ(warm_work.geometry_build_count, 1U);
  EXPECT_GE(warm_work.face_triangulation_count, object.document.faces.size());
  SelectionOverlayFrame warm_source;
  for (const auto &line : warm_frame.lines) {
    if (line.layer == SelectionOverlayLayer::SourceWire) {
      warm_source.lines.push_back(line);
    }
  }
  ASSERT_FALSE(warm_source.lines.empty());
  object.selected = false;
  object.component_edit_target = false;
  object.selection = {};
  const auto cleared = verify_frame(SelectionMode::Face, -1);
  EXPECT_TRUE(cleared.triangles.empty());
  EXPECT_TRUE(cleared.lines.empty());
  EXPECT_TRUE(cleared.points.empty());
  object.selected = true;
  object.component_edit_target = true;
  object.selection.mode = quader_poly::SelectionMode::Face;
  object.selection.faces = {second_face};
  const auto reselected = verify_frame(SelectionMode::Face, object.id);
  EXPECT_TRUE(std::ranges::any_of(reselected.triangles, [&](const auto &triangle) {
    return triangle.layer == SelectionOverlayLayer::SelectedVisibleFace &&
           triangle.face_id == second_face;
  }));
  EXPECT_FALSE(std::ranges::any_of(reselected.triangles, [&](const auto &triangle) {
    return triangle.layer == SelectionOverlayLayer::SelectedVisibleFace &&
           triangle.face_id == first_face;
  }));
  const auto reselected_work = overlay.selection_overlay_geometry_work_counters();
  EXPECT_EQ(reselected_work.geometry_build_count, warm_work.geometry_build_count);
  EXPECT_EQ(reselected_work.face_triangulation_count, warm_work.face_triangulation_count);
  const SelectionOverlayStyle style;
  for (const auto mode : std::array{SelectionMode::Object, SelectionMode::Vertex,
                                   SelectionMode::Edge, SelectionMode::Face}) {
    SCOPED_TRACE(static_cast<int>(mode));
    object.selection = {};
    object.selection.has_active = true;
    if (mode == SelectionMode::Vertex) {
      object.selection.mode = quader_poly::SelectionMode::Vertex;
      object.selection.vertices = {1, 2};
      object.selection.active_kind = quader_poly::ElementKind::Vertex;
      object.selection.active_vertex = 2;
    } else if (mode == SelectionMode::Edge) {
      object.selection.mode = quader_poly::SelectionMode::Edge;
      object.selection.edges = {boundary_edge, interior_edge};
      object.selection.active_kind = quader_poly::ElementKind::Edge;
      object.selection.active_edge = interior_edge;
    } else if (mode == SelectionMode::Face) {
      object.selection.mode = quader_poly::SelectionMode::Face;
      object.selection.faces = {second_face, first_face};
      object.selection.active_kind = quader_poly::ElementKind::Face;
      object.selection.active_face = first_face;
    }
    const auto frame = verify_frame(mode, object.id);
    if (mode == SelectionMode::Object) {
      EXPECT_TRUE(frame.points.empty());
      EXPECT_TRUE(std::ranges::any_of(frame.triangles, [](const auto &triangle) {
        return triangle.layer == SelectionOverlayLayer::SelectedXrayFace;
      }));
      object.selected = false;
      const auto deselected = verify_frame(mode, -1);
      EXPECT_TRUE(deselected.triangles.empty());
      EXPECT_TRUE(deselected.lines.empty());
      object.selected = true;
      (void)verify_frame(mode, object.id);
    } else {
      SelectionOverlayFrame source;
      for (const auto &line : frame.lines) {
        if (line.layer == SelectionOverlayLayer::SourceWire) {
          source.lines.push_back(line);
        }
      }
      expect_selection_overlay_frame_eq(source, warm_source);
      if (mode == SelectionMode::Vertex) {
        EXPECT_EQ(std::ranges::count_if(frame.points, [](const auto &point) {
          return point.layer == SelectionOverlayLayer::SelectedVertex;
        }), 2);
        EXPECT_TRUE(std::ranges::any_of(frame.points, [&](const auto &point) {
          return point.vertex_id == 2 &&
                 point.color_abgr == style.active_selected_handle_color_abgr;
        }));
        object.selection.active_vertex = 1;
      } else if (mode == SelectionMode::Edge) {
        EXPECT_TRUE(std::ranges::any_of(frame.lines, [&](const auto &line) {
          return line.layer == SelectionOverlayLayer::OpenEdge &&
                 line.edge == boundary_edge;
        }));
        EXPECT_FALSE(std::ranges::any_of(frame.lines, [&](const auto &line) {
          return line.layer == SelectionOverlayLayer::OpenEdge &&
                 line.edge == interior_edge;
        }));
        object.selection.active_edge = boundary_edge;
      } else {
        EXPECT_TRUE(std::ranges::any_of(frame.triangles, [&](const auto &triangle) {
          return triangle.layer == SelectionOverlayLayer::SelectedVisibleFace &&
                 triangle.face_id == first_face &&
                 triangle.color_abgr == style.active_selection_face_color_abgr;
        }));
        object.selection.active_face = second_face;
      }
      (void)verify_frame(mode, object.id);
      (void)verify_frame(mode, -1);
      object.selection.has_active = false;
      if (mode == SelectionMode::Vertex) {
        object.selection.vertices = {2};
      } else if (mode == SelectionMode::Edge) {
        object.selection.edges = {interior_edge};
      } else {
        object.selection.faces = {second_face};
      }
      (void)verify_frame(mode, object.id);
    }
    const auto &work = overlay.selection_overlay_geometry_work_counters();
    EXPECT_EQ(work.geometry_build_count, warm_work.geometry_build_count);
    EXPECT_EQ(work.face_triangulation_count, warm_work.face_triangulation_count);
  }
}

TEST(ModelingSessionServiceSplitTests,
     SemanticOverlayGeometryRetainsTopologyAcrossPositionChanges) {
  using namespace quader_editor_core;
  PolygonObject object;
  object.id = 1;
  object.selected = true;
  object.component_edit_target = true;
  object.document = service_uv_peel_strip_document();
  object.selection.mode = quader_poly::SelectionMode::Edge;
  object.selection.edges = {quader_poly::make_edge(1, 2)};
  ModelingOverlayService overlay;
  std::uint64_t content_revision = 1;
  const auto verify_frame = [&] {
    const ModelingSelectionProjectionSnapshot snapshot(
        {object}, SelectionMode::Edge, object.id, content_revision, 1);
    const auto &actual = overlay.selection_overlay_base_frame(snapshot);
    const auto expected = build_selection_overlay_frame(
        snapshot.objects(), SelectionMode::Edge, object.id, {});
    expect_selection_overlay_frame_eq(actual, expected);
    return actual;
  };
  const auto initial_frame = verify_frame();
  const auto initial_work = overlay.selection_overlay_geometry_work_counters();
  EXPECT_EQ(initial_work.geometry_build_count, 1U);
  EXPECT_GE(initial_work.face_triangulation_count, object.document.faces.size());
  ASSERT_FALSE(initial_frame.triangles.empty());
  object.document.edit().vertices[0].position.x -= 0.5F;
  ++content_revision;
  const auto moved_frame = verify_frame();
  const auto moved_work = overlay.selection_overlay_geometry_work_counters();
  EXPECT_EQ(moved_work.geometry_build_count, initial_work.geometry_build_count);
  EXPECT_EQ(moved_work.geometry_position_refresh_count,
            initial_work.geometry_position_refresh_count + 1U);
  EXPECT_EQ(moved_work.frame_position_refresh_count,
            initial_work.frame_position_refresh_count);
  EXPECT_GT(moved_work.face_triangulation_count, initial_work.face_triangulation_count);
  EXPECT_TRUE(std::ranges::any_of(moved_frame.lines, [](const auto &line) {
    return line.start.x == -2.5F || line.end.x == -2.5F;
  }));
  EXPECT_FALSE(std::ranges::any_of(initial_frame.lines, [](const auto &line) {
    return line.start.x == -2.5F || line.end.x == -2.5F;
  }));
  // A new neighboring face closes the selected boundary edge. This must
  // replace its old open-edge ticks as well as extend the source wire.
  const auto a = quader_poly::add_vertex(object.document.edit(), {-2.5F, -2.0F, 0.0F});
  const auto b = quader_poly::add_vertex(object.document.edit(), {0.0F, -2.0F, 0.0F});
  ASSERT_NE(quader_poly::add_face(object.document.edit(), std::array{1U, a, b, 2U}),
            quader_poly::kInvalidElementId);
  ++content_revision;
  const auto extended_frame = verify_frame();
  const auto extended_work = overlay.selection_overlay_geometry_work_counters();
  EXPECT_EQ(extended_work.geometry_build_count, moved_work.geometry_build_count + 1U);
  EXPECT_GT(extended_work.face_triangulation_count, moved_work.face_triangulation_count);
  EXPECT_GT(extended_frame.triangles.size(), moved_frame.triangles.size());
  EXPECT_TRUE(std::ranges::any_of(moved_frame.lines, [](const auto &line) {
    return line.layer == SelectionOverlayLayer::OpenEdge;
  }));
  EXPECT_FALSE(std::ranges::any_of(extended_frame.lines, [](const auto &line) {
    return line.layer == SelectionOverlayLayer::OpenEdge;
  }));
  (void)verify_frame();
  const auto &unchanged_work = overlay.selection_overlay_geometry_work_counters();
  EXPECT_EQ(unchanged_work.geometry_build_count, extended_work.geometry_build_count);
  EXPECT_EQ(unchanged_work.face_triangulation_count, extended_work.face_triangulation_count);
}

TEST(ModelingSessionServiceSplitTests,
     SemanticOverlayPositionRefreshPreservesFrozenFramesAndUndoCoordinates) {
  using namespace quader_editor_core;
  for (const auto mode : {SelectionMode::Vertex, SelectionMode::Edge,
                          SelectionMode::Face}) {
    SCOPED_TRACE(static_cast<int>(mode));
    PolygonObject object;
    object.id = 1;
    object.selected = true;
    object.component_edit_target = true;
    object.document = quader_poly::make_cube({});
    object.selection.mode = mode == SelectionMode::Vertex
        ? quader_poly::SelectionMode::Vertex
        : mode == SelectionMode::Edge ? quader_poly::SelectionMode::Edge
                                      : quader_poly::SelectionMode::Face;
    object.selection.vertices = {object.document.vertices.front().id};
    object.selection.edges = {quader_poly::make_edge(
        object.document.faces.front().vertices[0],
        object.document.faces.front().vertices[1])};
    object.selection.faces = {object.document.faces.front().id};
    const auto original_document = object.document;
    ModelingOverlayService overlay;
    std::uint64_t content_revision = 1U;
    const auto frame_for_current_document = [&] {
      const ModelingSelectionProjectionSnapshot snapshot(
          {object}, mode, object.id, content_revision++, 1U);
      const auto actual = overlay.selection_overlay_base_frame(snapshot);
      const auto expected = build_selection_overlay_frame(
          snapshot.objects(), mode, object.id, {});
      expect_selection_overlay_frame_eq(actual, expected);
      return actual;
    };
    const auto frozen_frame = frame_for_current_document();
    const auto initial_work = overlay.selection_overlay_geometry_work_counters();
    for (auto &vertex : object.document.edit().vertices) {
      vertex.position += QVec3{4.0F, -2.0F, 8.0F};
    }
    (void)frame_for_current_document();
    const auto moved_work = overlay.selection_overlay_geometry_work_counters();
    EXPECT_EQ(moved_work.geometry_build_count, initial_work.geometry_build_count);
    EXPECT_EQ(moved_work.geometry_position_refresh_count,
              initial_work.geometry_position_refresh_count + 1U);
    EXPECT_EQ(moved_work.frame_position_refresh_count,
              initial_work.frame_position_refresh_count + 1U);
    object.document = original_document;
    const auto undo_frame = frame_for_current_document();
    expect_selection_overlay_frame_eq(undo_frame, frozen_frame);
    const auto restored_work = overlay.selection_overlay_geometry_work_counters();
    EXPECT_EQ(restored_work.geometry_build_count, initial_work.geometry_build_count);
    EXPECT_EQ(restored_work.frame_position_refresh_count,
              moved_work.frame_position_refresh_count + 1U);
  }
}

TEST(ModelingSessionServiceSplitTests,
     SemanticOverlayPositionRefreshRejectsChangedTriangulationAndCollapsedEdges) {
  using namespace quader_editor_core;
  PolygonObject object;
  object.id = 1;
  object.selected = true;
  object.component_edit_target = true;
  object.document = service_uv_peel_strip_document();
  object.selection.mode = quader_poly::SelectionMode::Vertex;
  object.selection.vertices = {object.document.vertices.front().id};
  ModelingOverlayService overlay;
  std::uint64_t content_revision = 1U;
  const auto verify = [&] {
    const ModelingSelectionProjectionSnapshot snapshot(
        {object}, SelectionMode::Vertex, object.id, content_revision++, 1U);
    const auto &actual = overlay.selection_overlay_base_frame(snapshot);
    const auto expected = build_selection_overlay_frame(
        snapshot.objects(), SelectionMode::Vertex, object.id, {});
    expect_selection_overlay_frame_eq(actual, expected);
  };
  verify();
  const auto initial_work = overlay.selection_overlay_geometry_work_counters();
  object.document.edit().vertices[0].position =
      object.document.vertices[1].position;
  verify();
  object.document.edit().vertices[0].position = {-3.0F, 1.0F, 0.25F};
  verify();
  const auto work = overlay.selection_overlay_geometry_work_counters();
  EXPECT_EQ(work.geometry_build_count, initial_work.geometry_build_count);
  EXPECT_EQ(work.geometry_position_refresh_count,
            initial_work.geometry_position_refresh_count + 2U);
}

TEST(ModelingSessionServiceSplitTests,
     ActiveComponentChangesInvalidateOnlyRelevantSemanticObjectFrames) {
  using namespace quader_editor_core;
  PolygonObject first;
  first.id = 1;
  first.selected = true;
  first.component_edit_target = true;
  first.document = service_uv_peel_strip_document();
  first.selection.mode = quader_poly::SelectionMode::Vertex;
  first.selection.vertices = {1, 2};
  first.selection.has_active = true;
  first.selection.active_kind = quader_poly::ElementKind::Vertex;
  first.selection.active_vertex = 2;
  PolygonObject second = first;
  second.id = 2;
  ModelingOverlayService overlay;
  const SelectionOverlayStyle style;
  const auto expect_active = [&](int active_id) {
    // Identical geometry and local selections deliberately exercise template
    // reuse as well as reusing the previous per-object entries.
    const ModelingSelectionProjectionSnapshot snapshot(
        {first, second}, SelectionMode::Vertex, active_id, 7, 10);
    const auto &frame = overlay.selection_overlay_base_frame(snapshot);
    std::size_t active_count = 0;
    std::size_t selected_count = 0;
    for (const auto &point : frame.points) {
      if (point.layer == SelectionOverlayLayer::SelectedVertex) {
        ++selected_count;
      }
      if (point.color_abgr == style.active_selected_handle_color_abgr) {
        ++active_count;
        EXPECT_EQ(point.object_id, active_id);
        EXPECT_EQ(point.vertex_id, 2);
      }
    }
    EXPECT_EQ(selected_count, 4U);
    EXPECT_EQ(active_count, 1U);
    for (int object_id : {first.id, second.id}) {
      EXPECT_TRUE(std::ranges::any_of(frame.lines, [object_id](const auto &line) {
        return line.object_id == object_id && line.layer == SelectionOverlayLayer::SourceWire;
      }));
    }
    const auto *retained_points = frame.points.data();
    const auto *retained_lines = frame.lines.data();
    const auto &unchanged = overlay.selection_overlay_base_frame(snapshot);
    EXPECT_EQ(unchanged.points.data(), retained_points);
    EXPECT_EQ(unchanged.lines.data(), retained_lines);
  };
  expect_active(first.id);
  expect_active(second.id);
  expect_active(first.id);
}

TEST(ModelingSessionServiceSplitTests,
     RuntimeToolCommandServiceBuildsTypedToolCommands) {
  ModelingToolRayEvent ray_event;
  ray_event.ray.origin = {1.0f, 2.0f, 3.0f};
  ray_event.ray.direction = {0.0f, -2.0f, 0.0f};
  ray_event.camera.viewport_size = {640.0f, 480.0f};
  ray_event.grid_size = 0.25;
  ray_event.vertex_radius = 0.11;
  ray_event.edge_radius = 0.22;
  ray_event.snap_enabled = false;
  ray_event.constrain_axis = true;
  ray_event.dragging = true;

  const quader_editor_core::EditorRuntimeCommand insert_hover =
      RuntimeToolCommandService::update_insert_edge_loop_hover(ray_event);
  EXPECT_EQ(
      insert_hover.command_id,
      quader_editor_core::EditorRuntimeCommandId::UpdateInsertEdgeLoopHover);
  const auto *insert_settings =
      std::get_if<quader_editor_core::InsertEdgeLoopPickRuntimeCommandSettings>(
          &insert_hover.settings);
  ASSERT_NE(insert_settings, nullptr);
  EXPECT_NEAR(insert_settings->ray.origin.x, 1.0, 0.0001);
  EXPECT_NEAR(insert_settings->ray.direction.y, -1.0, 0.0001);
  EXPECT_EQ(insert_settings->grid_size, ray_event.grid_size);
  EXPECT_EQ(insert_settings->vertex_radius, ray_event.vertex_radius);
  EXPECT_EQ(insert_settings->edge_radius, ray_event.edge_radius);
  EXPECT_FALSE(insert_settings->snap_enabled);

  const quader_editor_core::EditorRuntimeCommand cut_line =
      RuntimeToolCommandService::begin_cut_line_drag(ray_event);
  EXPECT_EQ(cut_line.command_id,
            quader_editor_core::EditorRuntimeCommandId::BeginCutLineDrag);
  const auto *cut_settings =
      std::get_if<quader_editor_core::CutRayRuntimeCommandSettings>(
          &cut_line.settings);
  ASSERT_NE(cut_settings, nullptr);
  EXPECT_TRUE(cut_settings->dragging);
  EXPECT_EQ(cut_settings->grid_size, ray_event.grid_size);

  const quader_editor_core::EditorRuntimeCommand cut_mode =
      RuntimeToolCommandService::set_cut_mode(ModelingCutMode::DiscardLeft);
  const auto *cut_mode_settings =
      std::get_if<quader_editor_core::SetCutModeRuntimeCommandSettings>(
          &cut_mode.settings);
  ASSERT_NE(cut_mode_settings, nullptr);
  EXPECT_EQ(
      cut_mode_settings->mode,
      quader_editor_core::cut_mode_id(quader_editor_core::CutMode::KeepBack));

  const quader_editor_core::EditorRuntimeCommand mirror_point =
      RuntimeToolCommandService::begin_mirror_point_drag(2);
  EXPECT_EQ(mirror_point.command_id,
            quader_editor_core::EditorRuntimeCommandId::BeginMirrorPointDrag);
  const auto *mirror_settings =
      std::get_if<quader_editor_core::MirrorPointDragRuntimeCommandSettings>(
          &mirror_point.settings);
  ASSERT_NE(mirror_settings, nullptr);
  EXPECT_EQ(mirror_settings->point_index, 2);

  const quader_editor_core::EditorRuntimeCommand poly_hover =
      RuntimeToolCommandService::update_poly_hover(ray_event, std::nullopt);
  EXPECT_EQ(poly_hover.command_id,
            quader_editor_core::EditorRuntimeCommandId::UpdatePolyHover);
  const auto *poly_settings =
      std::get_if<quader_editor_core::PolyHoverRuntimeCommandSettings>(
          &poly_hover.settings);
  ASSERT_NE(poly_settings, nullptr);
  EXPECT_TRUE(poly_settings->constrain_axis);
  EXPECT_FALSE(poly_settings->snap_enabled);
  EXPECT_EQ(poly_settings->grid_size, ray_event.grid_size);
  EXPECT_TRUE(poly_settings->surface_hit_resolved);
  EXPECT_FALSE(poly_settings->resolved_surface_hit.has_value());

  ModelingBoxToolEvent box_event;
  box_event.screen_position = {320.0f, 240.0f};
  box_event.ray = ray_event.ray;
  box_event.grid_size = 2.0;
  box_event.snap_enabled = false;
  box_event.has_camera = true;
  box_event.camera.viewport_size = {800.0f, 600.0f};

  const quader_editor_core::EditorRuntimeCommand box_height =
      RuntimeToolCommandService::begin_box_height(box_event);
  EXPECT_EQ(box_height.command_id,
            quader_editor_core::EditorRuntimeCommandId::BeginBoxHeight);
  const auto *box_height_settings =
      std::get_if<quader_editor_core::BoxHeightRuntimeCommandSettings>(
          &box_height.settings);
  ASSERT_NE(box_height_settings, nullptr);
  EXPECT_NEAR(box_height_settings->screen_position.x, 320.0, 0.0001);
  EXPECT_TRUE(box_height_settings->has_camera);
  EXPECT_FALSE(box_height_settings->snap_enabled);

}


TEST(ModelingSessionServiceSplitTests,
     ObjectRuntimeMapOwnsModelingSceneMappingsAndBuildsRecords) {
  SceneDocument scene;
  const EntityId second = scene.create_entity("Second", EntityKind::Mesh);
  const EntityId first = scene.create_entity("First", EntityKind::Mesh);
  scene.set_mesh(second, MeshComponent{.mesh = 42});
  scene.set_mesh(first, MeshComponent{.mesh = 7});

  ModelingObjectRuntimeMap object_map;
  ASSERT_TRUE(object_map.bind(200, second));
  ASSERT_TRUE(object_map.bind(100, first));

  EXPECT_EQ(object_map.size(), 2U);
  EXPECT_EQ(object_map.entity_for_modeling_object(100), first);
  EXPECT_EQ(object_map.modeling_object_for_entity(second), 200);

  const std::vector<ModelingObjectRecord> records = object_map.records(scene);
  ASSERT_EQ(records.size(), 2U);
  EXPECT_EQ(records[0].entity, second);
  EXPECT_EQ(records[0].modeling_object_id, 200);
  EXPECT_EQ(records[0].mesh, 42);
  EXPECT_EQ(records[0].name, "Second");
  EXPECT_EQ(records[1].entity, first);
  EXPECT_EQ(records[1].modeling_object_id, 100);
  EXPECT_EQ(records[1].mesh, 7);
  EXPECT_EQ(records[1].name, "First");

  object_map.unbind_object(100);
  EXPECT_EQ(object_map.entity_for_modeling_object(100), std::nullopt);
  EXPECT_EQ(object_map.modeling_object_for_entity(first), std::nullopt);
}

TEST(ModelingSessionServiceSplitTests,
     ObjectRuntimeMapRebindRejectsForeignEndpointTheft) {
  SceneDocument scene;
  const EntityId first = scene.create_entity("First", EntityKind::Mesh);
  const EntityId second = scene.create_entity("Second", EntityKind::Mesh);

  ModelingObjectRuntimeMap object_map;
  ASSERT_TRUE(object_map.bind(100, first));
  ASSERT_TRUE(object_map.bind(100, second));

  EXPECT_EQ(object_map.entity_for_modeling_object(100), second);
  EXPECT_EQ(object_map.modeling_object_for_entity(first), std::nullopt);
  EXPECT_EQ(object_map.modeling_object_for_entity(second), 100);

  EXPECT_FALSE(object_map.bind(200, second));

  EXPECT_EQ(object_map.entity_for_modeling_object(100), second);
  EXPECT_EQ(object_map.entity_for_modeling_object(200), std::nullopt);
  EXPECT_EQ(object_map.modeling_object_for_entity(second), 100);
}

TEST(ModelingSessionServiceSplitTests,
     ObjectBindingRegistryOwnsSceneSdkAndTombstoneIdentityEdges) {
  SceneDocument scene;
  const EntityId first = scene.create_entity("First", EntityKind::Mesh);
  const EntityId second = scene.create_entity("Second", EntityKind::Mesh);
  ASSERT_NE(first, kInvalidEntityId);
  ASSERT_NE(second, kInvalidEntityId);
  ASSERT_TRUE(scene.set_mesh(first, MeshComponent{.mesh = 11U}));
  ASSERT_TRUE(scene.set_mesh(second, MeshComponent{.mesh = 22U}));

  ModelingObjectBindingRegistry bindings;
  const modeling::ObjectId first_sdk =
      modeling::make_id<modeling::ObjectTag>(1U);
  const modeling::ObjectId second_sdk =
      modeling::make_id<modeling::ObjectTag>(2U);
  ASSERT_TRUE(bindings.bind(100, first));
  ASSERT_TRUE(bindings.bind_sdk_object(100, first_sdk));
  bindings.remember_scene_entity(100, scene, first);
  const std::uint64_t stable_revision = bindings.revision();

  EXPECT_EQ(bindings.entity_for_modeling_object(100), first);
  EXPECT_EQ(bindings.modeling_object_for_entity(first), 100);
  EXPECT_EQ(bindings.sdk_object_for_modeling_object(100), first_sdk);
  EXPECT_EQ(bindings.modeling_object_for_sdk_object(first_sdk), 100);
  EXPECT_TRUE(bindings.bind(100, first));
  EXPECT_TRUE(bindings.bind_sdk_object(100, first_sdk));
  EXPECT_EQ(bindings.revision(), stable_revision);

  ASSERT_TRUE(bindings.bind_sdk_object(100, second_sdk));
  EXPECT_EQ(bindings.modeling_object_for_sdk_object(first_sdk), std::nullopt);
  EXPECT_EQ(bindings.modeling_object_for_sdk_object(second_sdk), 100);
  EXPECT_FALSE(bindings.bind_sdk_object(200, second_sdk));
  EXPECT_EQ(bindings.sdk_object_for_modeling_object(100), second_sdk);
  EXPECT_EQ(bindings.sdk_object_for_modeling_object(200), std::nullopt);

  bindings.unbind_object(100);
  bindings.unbind_sdk_object(100);
  EXPECT_TRUE(bindings.scene_identity_for_modeling_object(100).has_value());

  ASSERT_TRUE(bindings.restore_sdk_object_bindings(
      {{300, first_sdk}, {400, second_sdk}}));
  EXPECT_EQ(bindings.sdk_object_for_modeling_object(300), first_sdk);
  EXPECT_EQ(bindings.modeling_object_for_sdk_object(second_sdk), 400);
}

TEST(ModelingSessionServiceSplitTests,
     ObjectBindingRegistryRejectsEntityAndSdkEndpointCollisionsAtomically) {
  ModelingObjectBindingRegistry bindings;
  const EntityId first_entity = 11u;
  const EntityId second_entity = 22u;
  const modeling::ObjectId first_sdk =
      modeling::make_id<modeling::ObjectTag>(7u, 1u);
  const modeling::ObjectId second_sdk =
      modeling::make_id<modeling::ObjectTag>(7u, 2u);
  ASSERT_TRUE(bindings.bind(100, first_entity));
  ASSERT_TRUE(bindings.bind(200, second_entity));
  ASSERT_TRUE(bindings.bind_sdk_object(100, first_sdk));
  ASSERT_TRUE(bindings.bind_sdk_object(200, second_sdk));
  const std::uint64_t revision = bindings.revision();

  EXPECT_FALSE(bindings.bind(100, second_entity));
  EXPECT_FALSE(bindings.bind_sdk_object(100, second_sdk));
  EXPECT_EQ(bindings.revision(), revision);
  EXPECT_EQ(bindings.entity_for_modeling_object(100), first_entity);
  EXPECT_EQ(bindings.entity_for_modeling_object(200), second_entity);
  EXPECT_EQ(bindings.modeling_object_for_entity(first_entity), 100);
  EXPECT_EQ(bindings.modeling_object_for_entity(second_entity), 200);
  EXPECT_EQ(bindings.sdk_object_for_modeling_object(100), first_sdk);
  EXPECT_EQ(bindings.sdk_object_for_modeling_object(200), second_sdk);
  EXPECT_EQ(bindings.modeling_object_for_sdk_object(first_sdk), 100);
  EXPECT_EQ(bindings.modeling_object_for_sdk_object(second_sdk), 200);

  EXPECT_FALSE(bindings.restore_sdk_object_bindings(
      {{300, first_sdk}, {400, first_sdk}}));
  EXPECT_EQ(bindings.sdk_object_for_modeling_object(100), first_sdk);
  EXPECT_EQ(bindings.sdk_object_for_modeling_object(200), second_sdk);
  EXPECT_EQ(bindings.revision(), revision);
}

TEST(ModelingSessionServiceSplitTests,
     ObjectBindingMutationFenceTracksAbsentAndLiveIdsWithBoundedWork) {
  ModelingObjectBindingRegistry bindings;
  const modeling::ObjectId first_sdk =
      modeling::make_id<modeling::ObjectTag>(7u, 1u);
  ASSERT_TRUE(bindings.bind(100, 10u));
  ASSERT_TRUE(bindings.bind_sdk_object(100, first_sdk));
  for (int index = 0; index < 512; ++index) {
    ASSERT_TRUE(bindings.bind(1000 + index,
                              static_cast<EntityId>(10000 + index)));
  }

  const std::array participants{100, 100, kInvalidModelingObjectId, 300};
  ModelingObjectBindingMutationFence fence =
      bindings.capture_mutation_fence(participants);
  ASSERT_TRUE(fence.valid());
  EXPECT_EQ(fence.size(), 2u);
  EXPECT_EQ(fence.work().input_reference_count, participants.size());
  EXPECT_EQ(fence.work().participant_record_count, 2u);
  EXPECT_EQ(fence.work().mutation_counter_lookup_count, 2u);

  ASSERT_TRUE(bindings.bind(1000, 20000u));
  ModelingObjectBindingMutationFenceWork work;
  EXPECT_TRUE(fence.matches_current(bindings, &work));
  EXPECT_EQ(work.participant_record_count, 2u);
  EXPECT_EQ(work.mutation_counter_lookup_count, 2u);

  bindings.unbind_sdk_object(100);
  EXPECT_FALSE(fence.matches_current(bindings));
  ASSERT_TRUE(fence.acknowledge_current(bindings, {}, &work));
  EXPECT_EQ(work.participant_record_count, 2u);
  EXPECT_EQ(work.mutation_counter_lookup_count, 2u);
  EXPECT_TRUE(fence.matches_current(bindings));

  ASSERT_TRUE(bindings.bind(300, 30u));
  EXPECT_FALSE(fence.matches_current(bindings));

  ModelingObjectBindingRegistry foreign;
  EXPECT_FALSE(fence.matches_current(foreign));
  EXPECT_FALSE(fence.acknowledge_current(foreign));
}

TEST(ModelingSessionServiceSplitTests,
     ObjectBindingMutationFenceAcknowledgesAndEnrollsNewNativeIds) {
  ModelingObjectBindingRegistry bindings;
  const std::array participants{100};
  ModelingObjectBindingMutationFence fence =
      bindings.capture_mutation_fence(participants);
  ASSERT_TRUE(fence.valid());
  EXPECT_TRUE(fence.matches_current(bindings));

  ASSERT_TRUE(bindings.bind(200, 20u));
  EXPECT_TRUE(fence.matches_current(bindings));
  const std::array created{200};
  ModelingObjectBindingMutationFenceWork work;
  ASSERT_TRUE(fence.acknowledge_current(bindings, created, &work));
  EXPECT_EQ(fence.size(), 2u);
  EXPECT_EQ(work.input_reference_count, created.size());
  EXPECT_EQ(work.participant_record_count, 2u);
  EXPECT_EQ(work.mutation_counter_lookup_count, 2u);
  EXPECT_TRUE(fence.matches_current(bindings));

  bindings.unbind_object(200);
  EXPECT_FALSE(fence.matches_current(bindings));
  ASSERT_TRUE(fence.acknowledge_current(bindings));
  EXPECT_TRUE(fence.matches_current(bindings));
  ASSERT_TRUE(bindings.bind(200, 21u));
  EXPECT_FALSE(fence.matches_current(bindings));
}

TEST(ModelingSessionServiceSplitTests,
     ObjectBindingCheckpointRestoresSdkOnlyTombstoneAndAbsentRecordsExactly) {
  SceneDocument scene;
  const EntityId first = scene.create_entity("First", EntityKind::Mesh);
  const EntityId replacement =
      scene.create_entity("Replacement", EntityKind::Mesh);
  ASSERT_TRUE(scene.set_mesh(first, {.mesh = 11u}));
  ASSERT_TRUE(scene.set_mesh(replacement, {.mesh = 22u}));

  ModelingObjectBindingRegistry bindings;
  const modeling::ObjectId first_sdk =
      modeling::make_id<modeling::ObjectTag>(9u, 1u);
  const modeling::ObjectId stale_generation =
      modeling::make_id<modeling::ObjectTag>(9u, 2u);
  const modeling::ObjectId replacement_sdk =
      modeling::make_id<modeling::ObjectTag>(10u, 1u);
  ASSERT_TRUE(bindings.bind(100, first));
  ASSERT_TRUE(bindings.bind_sdk_object(100, first_sdk));
  bindings.remember_scene_entity(100, scene, first);
  ASSERT_TRUE(bindings.bind_sdk_object(200, stale_generation));
  bindings.remember_scene_entity(300, scene, replacement);

  for (int index = 0; index < 512; ++index) {
    const int native_id = 1000 + index;
    ASSERT_TRUE(bindings.bind(native_id,
                              static_cast<EntityId>(10000 + index)));
  }

  const std::array participants{300, 100, 200, 100,
                                kInvalidModelingObjectId, 400};
  const ModelingObjectBindingProjectionCheckpoint checkpoint =
      bindings.capture_projection_checkpoint(participants);
  ASSERT_TRUE(checkpoint.valid());
  EXPECT_EQ(checkpoint.size(), 4u);
  EXPECT_EQ(checkpoint.work().participant_reference_count,
            participants.size());
  EXPECT_EQ(checkpoint.work().record_count, 4u);
  EXPECT_EQ(checkpoint.work().native_state_lookup_count, 16u);
  EXPECT_EQ(checkpoint.work().entity_edge_record_count, 1u);
  EXPECT_EQ(checkpoint.work().sdk_edge_record_count, 2u);
  EXPECT_EQ(checkpoint.work().tombstone_record_count, 2u);
  const std::uint64_t checkpoint_revision = bindings.revision();

  bindings.unbind_object(100);
  bindings.unbind_sdk_object(100);
  ASSERT_TRUE(bindings.bind(100, replacement));
  ASSERT_TRUE(bindings.bind_sdk_object(100, replacement_sdk));
  bindings.remember_scene_entity(100, scene, replacement);
  bindings.unbind_sdk_object(200);
  ASSERT_TRUE(bindings.bind(200, replacement + 100u));
  bindings.remember_scene_entity(300, scene, first);
  ASSERT_TRUE(bindings.bind(400, replacement + 200u));

  const std::uint64_t before_preflight = bindings.revision();
  ASSERT_TRUE(bindings.can_restore_projection_checkpoint(checkpoint));
  EXPECT_EQ(bindings.revision(), before_preflight);
  EXPECT_EQ(bindings.entity_for_modeling_object(100), replacement);
  EXPECT_EQ(bindings.sdk_object_for_modeling_object(100), replacement_sdk);

  ASSERT_TRUE(bindings.restore_projection_checkpoint(checkpoint));
  EXPECT_EQ(bindings.revision(), checkpoint_revision);
  EXPECT_EQ(bindings.entity_for_modeling_object(100), first);
  EXPECT_EQ(bindings.modeling_object_for_entity(first), 100);
  EXPECT_EQ(bindings.sdk_object_for_modeling_object(100), first_sdk);
  EXPECT_EQ(bindings.modeling_object_for_sdk_object(first_sdk), 100);
  EXPECT_EQ(bindings.entity_for_modeling_object(200), std::nullopt);
  EXPECT_EQ(bindings.sdk_object_for_modeling_object(200), stale_generation);
  EXPECT_EQ(bindings.entity_for_modeling_object(300), std::nullopt);
  EXPECT_EQ(bindings.sdk_object_for_modeling_object(300), std::nullopt);
  EXPECT_EQ(bindings.entity_for_modeling_object(400), std::nullopt);
  EXPECT_EQ(bindings.scene_identity_for_modeling_object(400), std::nullopt);
  const auto first_identity =
      bindings.scene_identity_for_modeling_object(100);
  const auto tombstone = bindings.scene_identity_for_modeling_object(300);
  ASSERT_TRUE(first_identity.has_value());
  ASSERT_TRUE(tombstone.has_value());
  EXPECT_EQ(first_identity->id, first);
  EXPECT_EQ(first_identity->name, "First");
  EXPECT_EQ(tombstone->id, replacement);
  EXPECT_EQ(tombstone->name, "Replacement");
  EXPECT_EQ(bindings.entity_for_modeling_object(1000), EntityId{10000});
}

TEST(ModelingSessionServiceSplitTests,
     ObjectBindingCheckpointRejectsOutOfScopeMutationWithoutPartialRestore) {
  ModelingObjectBindingRegistry bindings;
  const modeling::ObjectId first_sdk =
      modeling::make_id<modeling::ObjectTag>(41u, 1u);
  const modeling::ObjectId changed_sdk =
      modeling::make_id<modeling::ObjectTag>(42u, 1u);
  ASSERT_TRUE(bindings.bind(100, 10u));
  ASSERT_TRUE(bindings.bind_sdk_object(100, first_sdk));
  ASSERT_TRUE(bindings.bind(200, 20u));
  const std::array participant{100};
  const auto checkpoint =
      bindings.capture_projection_checkpoint(participant);

  bindings.unbind_sdk_object(100);
  ASSERT_TRUE(bindings.bind_sdk_object(100, changed_sdk));
  ASSERT_TRUE(bindings.bind(200, 21u));
  const std::uint64_t before_failed_restore = bindings.revision();

  EXPECT_FALSE(bindings.can_restore_projection_checkpoint(checkpoint));
  EXPECT_EQ(bindings.revision(), before_failed_restore);
  EXPECT_EQ(bindings.sdk_object_for_modeling_object(100), changed_sdk);
  EXPECT_EQ(bindings.entity_for_modeling_object(200), EntityId{21u});

  EXPECT_FALSE(bindings.restore_projection_checkpoint(checkpoint));
  EXPECT_EQ(bindings.revision(), before_failed_restore);
  EXPECT_EQ(bindings.sdk_object_for_modeling_object(100), changed_sdk);
  EXPECT_EQ(bindings.modeling_object_for_sdk_object(changed_sdk), 100);
  EXPECT_EQ(bindings.entity_for_modeling_object(200), EntityId{21u});
  EXPECT_EQ(bindings.modeling_object_for_entity(21u), 200);
}

TEST(ModelingSessionServiceSplitTests,
     ObjectBindingConcurrentRestorePreservesUnrelatedRegistryProgress) {
  ModelingObjectBindingRegistry bindings;
  const modeling::ObjectId baseline_sdk =
      modeling::make_id<modeling::ObjectTag>(41u, 1u);
  const modeling::ObjectId preview_sdk =
      modeling::make_id<modeling::ObjectTag>(42u, 1u);
  const modeling::ObjectId unrelated_sdk =
      modeling::make_id<modeling::ObjectTag>(43u, 1u);
  ASSERT_TRUE(bindings.bind(100, 10u));
  ASSERT_TRUE(bindings.bind_sdk_object(100, baseline_sdk));
  const std::array participants{100};
  const ModelingObjectBindingProjectionCheckpoint checkpoint =
      bindings.capture_projection_checkpoint(participants);
  ModelingObjectBindingMutationFence fence =
      bindings.capture_mutation_fence(participants);

  ASSERT_TRUE(bindings.bind(100, 11u));
  ASSERT_TRUE(bindings.bind_sdk_object(100, preview_sdk));
  ASSERT_TRUE(fence.acknowledge_current(bindings));
  ASSERT_TRUE(bindings.bind(200, 20u));
  ASSERT_TRUE(bindings.bind_sdk_object(200, unrelated_sdk));
  const std::uint64_t revision_before_restore = bindings.revision();
  ASSERT_TRUE(fence.matches_current(bindings));

  EXPECT_FALSE(bindings.can_restore_projection_checkpoint(checkpoint));
  ASSERT_TRUE(
      bindings.can_restore_projection_checkpoint_preserving_concurrent_changes(
          checkpoint, fence));
  EXPECT_EQ(bindings.revision(), revision_before_restore);
  EXPECT_EQ(bindings.entity_for_modeling_object(100), EntityId{11u});

  ASSERT_TRUE(
      bindings.restore_projection_checkpoint_preserving_concurrent_changes(
          checkpoint, fence));
  EXPECT_EQ(bindings.entity_for_modeling_object(100), EntityId{10u});
  EXPECT_EQ(bindings.modeling_object_for_entity(10u), 100);
  EXPECT_EQ(bindings.sdk_object_for_modeling_object(100), baseline_sdk);
  EXPECT_EQ(bindings.modeling_object_for_sdk_object(baseline_sdk), 100);
  EXPECT_EQ(bindings.entity_for_modeling_object(200), EntityId{20u});
  EXPECT_EQ(bindings.sdk_object_for_modeling_object(200), unrelated_sdk);
  EXPECT_GT(bindings.revision(), revision_before_restore);
}

TEST(ModelingSessionServiceSplitTests,
     ObjectBindingConcurrentRestoreRejectsParticipantDriftAtomically) {
  ModelingObjectBindingRegistry bindings;
  const modeling::ObjectId baseline_sdk =
      modeling::make_id<modeling::ObjectTag>(51u, 1u);
  const modeling::ObjectId preview_sdk =
      modeling::make_id<modeling::ObjectTag>(52u, 1u);
  ASSERT_TRUE(bindings.bind(100, 10u));
  ASSERT_TRUE(bindings.bind_sdk_object(100, baseline_sdk));
  const std::array participants{100};
  const ModelingObjectBindingProjectionCheckpoint checkpoint =
      bindings.capture_projection_checkpoint(participants);
  ModelingObjectBindingMutationFence fence =
      bindings.capture_mutation_fence(participants);
  ASSERT_TRUE(bindings.bind(100, 11u));
  ASSERT_TRUE(fence.acknowledge_current(bindings));
  ASSERT_TRUE(bindings.bind_sdk_object(100, preview_sdk));
  const std::uint64_t revision = bindings.revision();

  EXPECT_FALSE(
      bindings.can_restore_projection_checkpoint_preserving_concurrent_changes(
          checkpoint, fence));
  EXPECT_FALSE(
      bindings.restore_projection_checkpoint_preserving_concurrent_changes(
          checkpoint, fence));
  EXPECT_EQ(bindings.entity_for_modeling_object(100), EntityId{11u});
  EXPECT_EQ(bindings.sdk_object_for_modeling_object(100), preview_sdk);
  EXPECT_EQ(bindings.revision(), revision);
}

TEST(ModelingSessionServiceSplitTests,
     HostProjectionCheckpointIsASingleUseRollbackCapability) {
  SceneDocument scene;
  AssetRegistry assets;
  const EntityId entity = scene.create_entity("Participant", EntityKind::Mesh);
  ASSERT_NE(entity, kInvalidEntityId);
  ASSERT_TRUE(scene.set_mesh(entity, {.mesh = 11u}));
  const std::array participants{entity};

  ModelingHostProjectionCheckpoint checkpoint(
      scene, assets, nullptr,
      {.capture_assets = false,
       .capture_scene = true,
       .capture_bindings = false,
       .scene_domains = SceneModelingProjectionDomain::MeshBindings,
       .scene_entities = participants});
  ASSERT_TRUE(checkpoint.valid());
  ASSERT_TRUE(scene.set_mesh(entity, {.mesh = 22u}));

  ASSERT_TRUE(checkpoint.restore());
  EXPECT_FALSE(checkpoint.valid());
  ASSERT_NE(scene.mesh(entity), nullptr);
  EXPECT_EQ(scene.mesh(entity)->mesh, 11u);

  ASSERT_TRUE(scene.set_mesh(entity, {.mesh = 33u}));
  EXPECT_FALSE(checkpoint.restore());
  ASSERT_NE(scene.mesh(entity), nullptr);
  EXPECT_EQ(scene.mesh(entity)->mesh, 33u);
}

TEST(ModelingSessionServiceSplitTests,
     HostProjectionCheckpointMoveInvalidatesTheSourceCapability) {
  SceneDocument scene;
  AssetRegistry assets;
  const EntityId entity = scene.create_entity("Participant", EntityKind::Mesh);
  ASSERT_NE(entity, kInvalidEntityId);
  ASSERT_TRUE(scene.set_mesh(entity, {.mesh = 11u}));
  const std::array participants{entity};
  ModelingHostProjectionCheckpoint source(
      scene, assets, nullptr,
      {.capture_assets = false,
       .capture_scene = true,
       .capture_bindings = false,
       .scene_domains = SceneModelingProjectionDomain::MeshBindings,
       .scene_entities = participants});
  ASSERT_TRUE(source.valid());

  ModelingHostProjectionCheckpoint destination(std::move(source));
  EXPECT_FALSE(source.valid());
  EXPECT_FALSE(source.can_restore());
  EXPECT_FALSE(source.restore());
  ASSERT_TRUE(destination.valid());
  ASSERT_TRUE(scene.set_mesh(entity, {.mesh = 22u}));
  ASSERT_TRUE(destination.restore());
  ASSERT_NE(scene.mesh(entity), nullptr);
  EXPECT_EQ(scene.mesh(entity)->mesh, 11u);
}

TEST(ModelingSessionServiceSplitTests,
     HostProjectionConcurrentRestoreRequiresAcknowledgedFence) {
  SceneDocument scene;
  AssetRegistry assets;
  const EntityId entity = scene.create_entity("Participant", EntityKind::Mesh);
  ASSERT_NE(entity, kInvalidEntityId);
  const std::array participants{entity};
  ModelingHostProjectionCheckpoint checkpoint(
      scene, assets, nullptr,
      {.capture_assets = false,
       .capture_scene = true,
       .capture_bindings = false,
       .preserve_concurrent_owner_changes = true,
       .scene_domains = SceneModelingProjectionDomain::Structure,
       .scene_entities = participants});
  EXPECT_FALSE(checkpoint.valid());
  EXPECT_FALSE(checkpoint.can_restore());
  EXPECT_FALSE(checkpoint.restore());
}

TEST(ModelingSessionServiceSplitTests,
     HostProjectionMutationFenceIgnoresUnrelatedAndDetectsParticipantDrift) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingObjectBindingRegistry bindings;
  const auto named_mesh = [](std::string name) {
    MeshAsset mesh;
    mesh.name = std::move(name);
    return mesh;
  };
  const AssetId participant_asset =
      assets.add_mesh(named_mesh("Participant"));
  const AssetId replacement_asset =
      assets.add_mesh(named_mesh("Replacement"));
  const AssetId unrelated_asset = assets.add_mesh(named_mesh("Unrelated"));
  ASSERT_NE(participant_asset, kInvalidAssetId);
  ASSERT_NE(replacement_asset, kInvalidAssetId);
  ASSERT_NE(unrelated_asset, kInvalidAssetId);

  const EntityId participant =
      scene.create_entity("Participant", EntityKind::Mesh);
  const EntityId rebound = scene.create_entity("Rebound", EntityKind::Mesh);
  const EntityId unrelated =
      scene.create_entity("Unrelated", EntityKind::Mesh);
  const EntityId unrelated_rebound =
      scene.create_entity("Unrelated Rebound", EntityKind::Mesh);
  ASSERT_TRUE(scene.set_mesh(participant, {.mesh = participant_asset}));
  ASSERT_TRUE(scene.set_mesh(rebound, {.mesh = replacement_asset}));
  ASSERT_TRUE(scene.set_mesh(unrelated, {.mesh = unrelated_asset}));
  ASSERT_TRUE(bindings.bind(100, participant));
  ASSERT_TRUE(bindings.bind(900, unrelated));

  const std::array scene_participants{participant};
  const std::array asset_participants{participant_asset};
  const std::array binding_participants{100};
  ModelingHostProjectionCheckpoint checkpoint(
      scene, assets, &bindings,
      {.capture_assets = true,
       .capture_scene = true,
       .capture_bindings = true,
       .capture_mutation_fence = true,
       .scene_domains = SceneModelingProjectionDomain::MeshBindings |
                        SceneModelingProjectionDomain::Structure,
       .scene_entities = scene_participants,
       .asset_ids = asset_participants,
       .modeling_object_ids = binding_participants});
  ASSERT_TRUE(checkpoint.valid());
  EXPECT_TRUE(checkpoint.mutations_match_acknowledged_fence());

  ASSERT_TRUE(assets.update_mesh(unrelated_asset, [](MeshAsset &asset) {
    asset.name = "Unrelated Changed";
  }));
  ASSERT_TRUE(scene.set_mesh(unrelated, {.mesh = replacement_asset}));
  ASSERT_TRUE(bindings.bind(900, unrelated_rebound));
  EXPECT_TRUE(checkpoint.mutations_match_acknowledged_fence());

  ASSERT_TRUE(assets.update_mesh(participant_asset, [](MeshAsset &asset) {
    asset.name = "Participant Changed";
  }));
  ASSERT_TRUE(scene.set_mesh(participant, {.mesh = replacement_asset}));
  ASSERT_TRUE(bindings.bind(100, rebound));
  EXPECT_FALSE(checkpoint.mutations_match_acknowledged_fence());
  EXPECT_FALSE(checkpoint.can_restore());
}

TEST(ModelingSessionServiceSplitTests,
     HostProjectionMutationFenceAcknowledgesAndRestoresCreatedParticipants) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingObjectBindingRegistry bindings;
  const auto named_mesh = [](std::string name) {
    MeshAsset mesh;
    mesh.name = std::move(name);
    return mesh;
  };
  const AssetId participant_asset =
      assets.add_mesh(named_mesh("Participant"));
  const AssetId replacement_asset =
      assets.add_mesh(named_mesh("Replacement"));
  ASSERT_NE(participant_asset, kInvalidAssetId);
  ASSERT_NE(replacement_asset, kInvalidAssetId);
  const EntityId participant =
      scene.create_entity("Participant", EntityKind::Mesh);
  const EntityId rebound = scene.create_entity("Rebound", EntityKind::Mesh);
  ASSERT_TRUE(scene.set_mesh(participant, {.mesh = participant_asset}));
  ASSERT_TRUE(scene.set_mesh(rebound, {.mesh = replacement_asset}));
  ASSERT_TRUE(bindings.bind(100, participant));

  const std::array scene_participants{participant};
  const std::array asset_participants{participant_asset};
  const std::array binding_participants{100, 200};
  ModelingHostProjectionCheckpoint checkpoint(
      scene, assets, &bindings,
      {.capture_assets = true,
       .capture_scene = true,
       .capture_bindings = true,
       .capture_mutation_fence = true,
       .scene_domains = SceneModelingProjectionDomain::MeshBindings |
                        SceneModelingProjectionDomain::Structure,
       .scene_entities = scene_participants,
       .asset_ids = asset_participants,
       .modeling_object_ids = binding_participants});
  ASSERT_TRUE(checkpoint.valid());

  ASSERT_TRUE(assets.update_mesh(participant_asset, [](MeshAsset &asset) {
    asset.name = "Participant Changed";
  }));
  ASSERT_TRUE(scene.set_mesh(participant, {.mesh = replacement_asset}));
  ASSERT_TRUE(bindings.bind(100, rebound));
  EXPECT_FALSE(checkpoint.mutations_match_acknowledged_fence());
  ASSERT_TRUE(checkpoint.acknowledge_current_mutations());
  EXPECT_TRUE(checkpoint.mutations_match_acknowledged_fence());

  const AssetId created_asset = assets.add_mesh(named_mesh("Created"));
  const EntityId created_entity =
      scene.create_entity("Created", EntityKind::Mesh);
  ASSERT_NE(created_asset, kInvalidAssetId);
  ASSERT_NE(created_entity, kInvalidEntityId);
  ASSERT_TRUE(scene.set_mesh(created_entity, {.mesh = created_asset}));
  ASSERT_TRUE(bindings.bind(200, created_entity));
  const std::array created_entities{created_entity};
  const std::array created_assets{created_asset};
  const std::array created_modeling_ids{200};
  ASSERT_TRUE(checkpoint.acknowledge_current_mutations(
      created_entities, created_assets, created_modeling_ids));
  EXPECT_TRUE(checkpoint.mutations_match_acknowledged_fence());
  ASSERT_TRUE(checkpoint.can_restore(created_entities, created_assets));
  ASSERT_TRUE(checkpoint.restore(created_entities, created_assets));

  EXPECT_FALSE(scene.exists(created_entity));
  EXPECT_FALSE(assets.has_mesh(created_asset));
  EXPECT_FALSE(bindings.entity_for_modeling_object(200).has_value());
  ASSERT_NE(scene.mesh(participant), nullptr);
  EXPECT_EQ(scene.mesh(participant)->mesh, participant_asset);
  ASSERT_NE(assets.mesh(participant_asset), nullptr);
  EXPECT_EQ(assets.mesh(participant_asset)->name, "Participant");
  EXPECT_EQ(bindings.entity_for_modeling_object(100), participant);
}

TEST(ModelingSessionServiceSplitTests,
     HostProjectionConcurrentRestorePreservesUnrelatedOwnerPublications) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingObjectBindingRegistry bindings;
  const auto named_mesh = [](std::string name) {
    MeshAsset mesh;
    mesh.name = std::move(name);
    return mesh;
  };
  const AssetId participant_asset =
      assets.add_mesh(named_mesh("Participant"));
  const AssetId replacement_asset =
      assets.add_mesh(named_mesh("Replacement"));
  const AssetId unrelated_asset = assets.add_mesh(named_mesh("Unrelated"));
  const EntityId participant =
      scene.create_entity("Participant", EntityKind::Mesh);
  const EntityId rebound = scene.create_entity("Rebound", EntityKind::Mesh);
  const EntityId unrelated =
      scene.create_entity("Unrelated", EntityKind::Mesh);
  const EntityId unrelated_rebound =
      scene.create_entity("Unrelated Rebound", EntityKind::Mesh);
  ASSERT_TRUE(scene.set_mesh(participant, {.mesh = participant_asset}));
  ASSERT_TRUE(scene.set_mesh(rebound, {.mesh = replacement_asset}));
  ASSERT_TRUE(scene.set_mesh(unrelated, {.mesh = unrelated_asset}));
  ASSERT_TRUE(bindings.bind(100, participant));
  ASSERT_TRUE(bindings.bind(900, unrelated));
  const std::array scene_participants{participant};
  const std::array asset_participants{participant_asset};
  const std::array binding_participants{100, 200};
  ModelingHostProjectionCheckpoint checkpoint(
      scene, assets, &bindings,
      {.capture_assets = true,
       .capture_scene = true,
       .capture_bindings = true,
       .capture_mutation_fence = true,
       .preserve_concurrent_owner_changes = true,
       .scene_domains = SceneModelingProjectionDomain::MeshBindings |
                        SceneModelingProjectionDomain::Structure,
       .scene_entities = scene_participants,
       .asset_ids = asset_participants,
       .modeling_object_ids = binding_participants});
  ASSERT_TRUE(checkpoint.valid());

  ASSERT_TRUE(assets.update_mesh(participant_asset, [](MeshAsset &asset) {
    asset.name = "Preview Candidate";
  }));
  ASSERT_TRUE(scene.set_mesh(participant, {.mesh = replacement_asset}));
  ASSERT_TRUE(bindings.bind(100, rebound));
  const AssetId provisional_asset =
      assets.add_mesh(named_mesh("Preview Provisional"));
  const EntityId provisional_entity =
      scene.create_entity("Preview Provisional", EntityKind::Mesh);
  ASSERT_NE(provisional_asset, kInvalidAssetId);
  ASSERT_NE(provisional_entity, kInvalidEntityId);
  ASSERT_TRUE(
      scene.set_mesh(provisional_entity, {.mesh = provisional_asset}));
  ASSERT_TRUE(bindings.bind(200, provisional_entity));
  const std::array created_entities{provisional_entity};
  const std::array created_assets{provisional_asset};
  const std::array created_modeling_ids{200};
  ASSERT_TRUE(checkpoint.acknowledge_current_mutations(
      created_entities, created_assets, created_modeling_ids));

  ASSERT_TRUE(assets.update_mesh(unrelated_asset, [](MeshAsset &asset) {
    asset.name = "Unrelated Authored Edit";
  }));
  ASSERT_TRUE(scene.set_mesh(unrelated, {.mesh = replacement_asset}));
  ASSERT_TRUE(bindings.bind(900, unrelated_rebound));
  const AssetId unrelated_created_asset =
      assets.add_mesh(named_mesh("Unrelated Created"));
  const EntityId unrelated_created_entity =
      scene.create_entity("Unrelated Created", EntityKind::Mesh);
  ASSERT_NE(unrelated_created_asset, kInvalidAssetId);
  ASSERT_NE(unrelated_created_entity, kInvalidEntityId);
  ASSERT_TRUE(scene.set_mesh(
      unrelated_created_entity, {.mesh = unrelated_created_asset}));
  const std::uint64_t scene_revision_before_restore = scene.revision();
  const std::uint64_t asset_revision_before_restore = assets.revision();
  const std::uint64_t binding_revision_before_restore = bindings.revision();

  ASSERT_TRUE(checkpoint.mutations_match_acknowledged_fence());
  ASSERT_TRUE(checkpoint.can_restore(created_entities, created_assets));
  ASSERT_TRUE(checkpoint.restore(created_entities, created_assets));

  EXPECT_FALSE(scene.exists(provisional_entity));
  EXPECT_FALSE(assets.has_mesh(provisional_asset));
  EXPECT_FALSE(bindings.entity_for_modeling_object(200).has_value());
  ASSERT_NE(scene.mesh(participant), nullptr);
  EXPECT_EQ(scene.mesh(participant)->mesh, participant_asset);
  ASSERT_NE(assets.mesh(participant_asset), nullptr);
  EXPECT_EQ(assets.mesh(participant_asset)->name, "Participant");
  EXPECT_EQ(bindings.entity_for_modeling_object(100), participant);

  ASSERT_TRUE(scene.exists(unrelated_created_entity));
  ASSERT_NE(scene.mesh(unrelated), nullptr);
  EXPECT_EQ(scene.mesh(unrelated)->mesh, replacement_asset);
  ASSERT_NE(assets.mesh(unrelated_asset), nullptr);
  EXPECT_EQ(assets.mesh(unrelated_asset)->name, "Unrelated Authored Edit");
  ASSERT_NE(assets.mesh(unrelated_created_asset), nullptr);
  EXPECT_EQ(bindings.entity_for_modeling_object(900), unrelated_rebound);
  EXPECT_GT(scene.revision(), scene_revision_before_restore);
  EXPECT_GT(assets.revision(), asset_revision_before_restore);
  EXPECT_GT(bindings.revision(), binding_revision_before_restore);
  EXPECT_GT(assets.add_mesh(named_mesh("After Restore")),
            unrelated_created_asset);
  EXPECT_GT(scene.create_entity("After Restore"), unrelated_created_entity);
}

TEST(ModelingSessionServiceSplitTests,
     HostProjectionPreselectsMixedExactAndConcurrentOwnerBranches) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingObjectBindingRegistry bindings;
  const auto named_mesh = [](std::string name) {
    MeshAsset mesh;
    mesh.name = std::move(name);
    return mesh;
  };
  const AssetId participant_asset =
      assets.add_mesh(named_mesh("Participant"));
  const AssetId replacement_asset =
      assets.add_mesh(named_mesh("Replacement"));
  const AssetId unrelated_asset = assets.add_mesh(named_mesh("Unrelated"));
  const EntityId participant =
      scene.create_entity("Participant", EntityKind::Mesh);
  const EntityId rebound = scene.create_entity("Rebound", EntityKind::Mesh);
  ASSERT_TRUE(scene.set_mesh(participant, {.mesh = participant_asset}));
  ASSERT_TRUE(scene.set_mesh(rebound, {.mesh = replacement_asset}));
  ASSERT_TRUE(bindings.bind(100, participant));
  const std::uint64_t scene_revision_before = scene.revision();
  const std::uint64_t asset_revision_before = assets.revision();
  const std::uint64_t binding_revision_before = bindings.revision();
  const std::array scene_participants{participant};
  const std::array asset_participants{participant_asset};
  const std::array binding_participants{100};
  ModelingHostProjectionCheckpoint checkpoint(
      scene, assets, &bindings,
      {.capture_assets = true,
       .capture_scene = true,
       .capture_bindings = true,
       .capture_mutation_fence = true,
       .preserve_concurrent_owner_changes = true,
       .scene_domains = SceneModelingProjectionDomain::MeshBindings,
       .scene_entities = scene_participants,
       .asset_ids = asset_participants,
       .modeling_object_ids = binding_participants});
  ASSERT_TRUE(checkpoint.valid());

  ASSERT_TRUE(scene.set_mesh(participant, {.mesh = replacement_asset}));
  ASSERT_TRUE(assets.update_mesh(participant_asset, [](MeshAsset &asset) {
    asset.name = "Preview Candidate";
  }));
  ASSERT_TRUE(bindings.bind(100, rebound));
  ASSERT_TRUE(checkpoint.acknowledge_current_mutations());

  // Only AssetRegistry has unrelated work, so Scene and bindings can take
  // their exact branches while AssetRegistry must preserve concurrent work.
  ASSERT_TRUE(assets.update_mesh(unrelated_asset, [](MeshAsset &asset) {
    asset.name = "Unrelated Authored Edit";
  }));
  const std::uint64_t asset_revision_before_restore = assets.revision();
  ASSERT_TRUE(checkpoint.can_restore());
  ASSERT_TRUE(checkpoint.restore());

  ASSERT_NE(scene.mesh(participant), nullptr);
  EXPECT_EQ(scene.mesh(participant)->mesh, participant_asset);
  EXPECT_EQ(scene.revision(), scene_revision_before);
  ASSERT_NE(assets.mesh(participant_asset), nullptr);
  EXPECT_EQ(assets.mesh(participant_asset)->name, "Participant");
  ASSERT_NE(assets.mesh(unrelated_asset), nullptr);
  EXPECT_EQ(assets.mesh(unrelated_asset)->name,
            "Unrelated Authored Edit");
  EXPECT_GT(assets.revision(), asset_revision_before_restore);
  EXPECT_GT(assets.revision(), asset_revision_before);
  EXPECT_EQ(bindings.entity_for_modeling_object(100), participant);
  EXPECT_EQ(bindings.revision(), binding_revision_before);
}

TEST(ModelingSessionServiceSplitTests,
     HostProjectionMutationFenceRejectsExternalParticipantDrift) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingObjectBindingRegistry bindings;
  const AssetId asset = assets.add_mesh(MeshAsset{});
  const AssetId replacement_asset = assets.add_mesh(MeshAsset{});
  const EntityId entity = scene.create_entity("Participant", EntityKind::Mesh);
  const EntityId rebound = scene.create_entity("Rebound", EntityKind::Mesh);
  ASSERT_NE(asset, kInvalidAssetId);
  ASSERT_NE(entity, kInvalidEntityId);
  ASSERT_TRUE(scene.set_mesh(entity, {.mesh = asset}));
  ASSERT_TRUE(bindings.bind(100, entity));
  const std::array scene_participants{entity};
  const std::array asset_participants{asset};
  const std::array binding_participants{100};
  ModelingHostProjectionCheckpoint checkpoint(
      scene, assets, &bindings,
      {.capture_assets = true,
       .capture_scene = true,
       .capture_bindings = true,
       .capture_mutation_fence = true,
       .preserve_concurrent_owner_changes = true,
       .scene_domains = SceneModelingProjectionDomain::MeshBindings,
       .scene_entities = scene_participants,
       .asset_ids = asset_participants,
       .modeling_object_ids = binding_participants});
  ASSERT_TRUE(checkpoint.valid());
  ASSERT_TRUE(checkpoint.mutations_match_acknowledged_fence());

  ASSERT_TRUE(scene.set_mesh(entity, {.mesh = replacement_asset}));
  ASSERT_TRUE(bindings.bind(100, rebound));
  ASSERT_TRUE(checkpoint.acknowledge_current_mutations());
  ASSERT_TRUE(assets.update_mesh(asset, [](MeshAsset &mesh) {
    mesh.name = "External Edit";
  }));
  EXPECT_FALSE(checkpoint.mutations_match_acknowledged_fence());
  EXPECT_FALSE(checkpoint.can_restore());
  EXPECT_FALSE(checkpoint.restore());
  ASSERT_NE(assets.mesh(asset), nullptr);
  EXPECT_EQ(assets.mesh(asset)->name, "External Edit");
  ASSERT_NE(scene.mesh(entity), nullptr);
  EXPECT_EQ(scene.mesh(entity)->mesh, replacement_asset);
  EXPECT_EQ(bindings.entity_for_modeling_object(100), rebound);
}

TEST(ModelingSessionServiceSplitTests,
     HostProjectionAggregatePreflightLeavesEveryOwnerOnCurrentBranch) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingObjectBindingRegistry bindings;
  const auto named_mesh = [](std::string name) {
    MeshAsset mesh;
    mesh.name = std::move(name);
    return mesh;
  };
  const AssetId participant_asset =
      assets.add_mesh(named_mesh("Participant"));
  const AssetId replacement_asset =
      assets.add_mesh(named_mesh("Replacement"));
  const AssetId unrelated_asset =
      assets.add_mesh(named_mesh("Unrelated"));
  ASSERT_NE(participant_asset, kInvalidAssetId);
  ASSERT_NE(replacement_asset, kInvalidAssetId);
  ASSERT_NE(unrelated_asset, kInvalidAssetId);
  const EntityId participant =
      scene.create_entity("Participant", EntityKind::Mesh);
  const EntityId rebound = scene.create_entity("Rebound", EntityKind::Mesh);
  ASSERT_TRUE(scene.set_mesh(participant, {.mesh = participant_asset}));
  ASSERT_TRUE(scene.set_mesh(rebound, {.mesh = replacement_asset}));
  ASSERT_TRUE(bindings.bind(100, participant));
  const std::array scene_participants{participant};
  const std::array asset_participants{participant_asset};
  const std::array binding_participants{100};
  ModelingHostProjectionCheckpoint checkpoint(
      scene, assets, &bindings,
      {.capture_assets = true,
       .capture_scene = true,
       .capture_bindings = true,
       .scene_domains = SceneModelingProjectionDomain::MeshBindings,
       .scene_entities = scene_participants,
       .asset_ids = asset_participants,
       .modeling_object_ids = binding_participants});
  ASSERT_TRUE(checkpoint.valid());

  ASSERT_TRUE(scene.set_mesh(participant, {.mesh = replacement_asset}));
  ASSERT_TRUE(assets.update_mesh(participant_asset, [](MeshAsset &asset) {
    asset.name = "Participant Changed";
  }));
  ASSERT_TRUE(assets.update_mesh(unrelated_asset, [](MeshAsset &asset) {
    asset.name = "Unrelated Changed";
  }));
  ASSERT_TRUE(bindings.bind(100, rebound));
  const std::uint64_t scene_revision = scene.revision();
  const std::uint64_t asset_revision = assets.revision();
  const std::uint64_t binding_revision = bindings.revision();

  EXPECT_FALSE(checkpoint.can_restore());
  EXPECT_FALSE(checkpoint.restore());
  EXPECT_EQ(scene.revision(), scene_revision);
  ASSERT_NE(scene.mesh(participant), nullptr);
  EXPECT_EQ(scene.mesh(participant)->mesh, replacement_asset);
  EXPECT_EQ(assets.revision(), asset_revision);
  ASSERT_NE(assets.mesh(participant_asset), nullptr);
  EXPECT_EQ(assets.mesh(participant_asset)->name, "Participant Changed");
  ASSERT_NE(assets.mesh(unrelated_asset), nullptr);
  EXPECT_EQ(assets.mesh(unrelated_asset)->name, "Unrelated Changed");
  EXPECT_EQ(bindings.revision(), binding_revision);
  EXPECT_EQ(bindings.entity_for_modeling_object(100), rebound);
}

TEST(ModelingSessionServiceSplitTests,
     HostPublicationNestingGuardCoversEverySharedOwnerIdentity) {
  SceneDocument first_scene;
  SceneDocument second_scene;
  AssetRegistry first_assets;
  AssetRegistry second_assets;
  ModelingObjectBindingRegistry first_bindings;
  ModelingObjectBindingRegistry second_bindings;
  modeling::ModelingSession first_session;
  modeling::ModelingSession second_session;
  const ModelingHostPublicationTransactionOptions options{
      .sdk_rollback = ModelingSdkRollbackPolicy::None,
      .checkpoint_scope =
          ModelingHostPublicationCheckpointScope::ModelingProjection,
      .capture_assets = false,
      .capture_scene = false,
      .capture_bindings = false,
      .capture_hierarchy = false,
  };
  ModelingHostPublicationTransaction outer(
      first_scene, first_assets, first_bindings, first_session, {}, options);
  ASSERT_TRUE(outer.ready());

  ModelingHostPublicationTransaction same_scene(
      first_scene, second_assets, second_bindings, second_session, {},
      options);
  EXPECT_FALSE(same_scene.ready());
  ModelingHostPublicationTransaction same_assets(
      second_scene, first_assets, second_bindings, second_session, {},
      options);
  EXPECT_FALSE(same_assets.ready());
  ModelingHostPublicationTransaction same_bindings(
      second_scene, second_assets, first_bindings, second_session, {},
      options);
  EXPECT_FALSE(same_bindings.ready());
  ModelingHostPublicationTransaction same_session(
      second_scene, second_assets, second_bindings, first_session, {},
      options);
  EXPECT_FALSE(same_session.ready());

  ASSERT_TRUE(outer.commit());
  ModelingHostPublicationTransaction released(
      first_scene, first_assets, first_bindings, first_session, {}, options);
  EXPECT_TRUE(released.ready());
  EXPECT_TRUE(released.commit());
}

TEST(ModelingSessionServiceSplitTests,
     HostPublicationWorkRemainsBoundedWithThousandsOfUnrelatedRecords) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingObjectBindingRegistry bindings;
  modeling::ModelingSession session;
  auto document = modeling::PolygonDocument::make_box({});
  ASSERT_TRUE(document.ok()) << document.error_message();
  auto object = session.add_document(std::move(document).value(), "Participant");
  ASSERT_TRUE(object.ok()) << object.error_message();
  const AssetId asset = assets.add_mesh(MeshAsset{});
  const EntityId entity = scene.create_entity("Participant", EntityKind::Mesh);
  ASSERT_NE(asset, kInvalidAssetId);
  ASSERT_NE(entity, kInvalidEntityId);
  ASSERT_TRUE(scene.set_mesh(entity, {.mesh = asset}));
  ASSERT_TRUE(scene.set_modeling_object(
      entity, {.object_id = 100, .sdk_owned = true}));
  ASSERT_TRUE(bindings.bind(100, entity));
  ASSERT_TRUE(bindings.bind_sdk_object(100, object.value()));

  constexpr std::size_t kUnrelatedCount = 6000u;
  for (std::size_t index = 0u; index < kUnrelatedCount; ++index) {
    const AssetId unrelated_asset = assets.add_mesh(MeshAsset{});
    const EntityId unrelated_entity =
        scene.create_entity("Unrelated", EntityKind::Mesh);
    ASSERT_NE(unrelated_asset, kInvalidAssetId);
    ASSERT_NE(unrelated_entity, kInvalidEntityId);
    ASSERT_TRUE(scene.set_mesh(unrelated_entity, {.mesh = unrelated_asset}));
    ASSERT_TRUE(bindings.bind(static_cast<int>(1000u + index),
                              unrelated_entity));
  }

  const std::array sdk_participants{object.value()};
  const std::array scene_participants{entity};
  const std::array asset_participants{asset};
  const std::array binding_participants{100};
  ModelingHostPublicationTransaction publication(
      scene, assets, bindings, session, {},
      {.sdk_rollback = ModelingSdkRollbackPolicy::MutationCheckpoint,
       .checkpoint_scope =
           ModelingHostPublicationCheckpointScope::ModelingProjection,
       .capture_hierarchy = false,
       .affected_scene_entities = scene_participants,
       .affected_asset_ids = asset_participants,
       .affected_modeling_object_ids = binding_participants,
       .affected_sdk_object_ids = sdk_participants});
  ASSERT_TRUE(publication.ready());
  EXPECT_EQ(publication.work().sdk_participant_records, 1u);
  EXPECT_EQ(publication.work().asset_records, 1u);
  EXPECT_EQ(publication.work().scene_entity_records, 1u);
  EXPECT_EQ(publication.work().hierarchy_records, 0u);
  EXPECT_EQ(publication.work().binding_records, 1u);
  EXPECT_FALSE(publication.work().full_sdk_snapshot);
  EXPECT_TRUE(publication.commit());
}

TEST(ModelingSessionServiceSplitTests,
     SnapshotPublicationRejectsAnExternalMutationCheckpoint) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingObjectBindingRegistry bindings;
  modeling::ModelingSession session;
  auto document = modeling::PolygonDocument::make_box({});
  ASSERT_TRUE(document.ok()) << document.error_message();
  auto object = session.add_document(std::move(document).value(), "Object");
  ASSERT_TRUE(object.ok()) << object.error_message();
  const std::array participants{object.value()};
  auto checkpoint = session.capture_mutation_checkpoint(participants);
  ASSERT_TRUE(checkpoint.ok()) << checkpoint.error_message();
  ASSERT_TRUE(session.mutation_checkpoint_active());

  ModelingHostPublicationTransaction publication(
      scene, assets, bindings, session, {},
      {.sdk_rollback = ModelingSdkRollbackPolicy::Snapshot,
       .checkpoint_scope =
           ModelingHostPublicationCheckpointScope::ModelingProjection,
       .capture_assets = false,
       .capture_scene = false,
       .capture_bindings = false,
       .capture_hierarchy = false});
  EXPECT_FALSE(publication.ready());
  ASSERT_TRUE(session.restore_mutation_checkpoint(
                         std::move(checkpoint).value())
                  .ok());
  EXPECT_FALSE(session.mutation_checkpoint_active());
  EXPECT_TRUE(session.rename_object(object.value(), "Usable").ok());
}

TEST(ModelingSessionServiceSplitTests,
     FailedNativePreflightConsumesSdkCheckpointAndLeavesSessionUsable) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingObjectBindingRegistry bindings;
  modeling::ModelingSession session;
  auto document = modeling::PolygonDocument::make_box({});
  ASSERT_TRUE(document.ok()) << document.error_message();
  auto object = session.add_document(std::move(document).value(), "Before");
  ASSERT_TRUE(object.ok()) << object.error_message();
  const EntityId participant =
      scene.create_entity("Participant", EntityKind::Mesh);
  const EntityId unrelated = scene.create_entity("Unrelated");
  ASSERT_TRUE(scene.set_mesh(participant, {.mesh = 11u}));
  const std::array sdk_participants{object.value()};
  const std::array scene_participants{participant};
  ModelingHostPublicationTransaction publication(
      scene, assets, bindings, session, {},
      {.sdk_rollback = ModelingSdkRollbackPolicy::MutationCheckpoint,
       .checkpoint_scope =
           ModelingHostPublicationCheckpointScope::ModelingProjection,
       .capture_assets = false,
       .capture_scene = true,
       .capture_bindings = false,
       .capture_hierarchy = false,
       .affected_scene_entities = scene_participants,
       .affected_sdk_object_ids = sdk_participants});
  ASSERT_TRUE(publication.ready());
  ASSERT_TRUE(session.rename_object(object.value(), "Current Branch").ok());
  ASSERT_TRUE(scene.set_mesh(participant, {.mesh = 12u}));
  TransformComponent moved;
  moved.position = {3.0f, 2.0f, 1.0f};
  ASSERT_TRUE(scene.set_transform(unrelated, moved));

  EXPECT_FALSE(publication.rollback());
  EXPECT_FALSE(session.mutation_checkpoint_active());
  ASSERT_EQ(session.objects().size(), 1u);
  EXPECT_EQ(session.objects().front().name, "Current Branch");
  EXPECT_TRUE(session.rename_object(object.value(), "Still Usable").ok());
  ASSERT_NE(scene.mesh(participant), nullptr);
  EXPECT_EQ(scene.mesh(participant)->mesh, 12u);
  ASSERT_NE(scene.transform(unrelated), nullptr);
  EXPECT_FLOAT_EQ(scene.transform(unrelated)->position.x, 3.0f);
}

TEST(ModelingSessionServiceSplitTests,
     HostPublicationTransactionRollbackRestoresEveryPublicationDomain) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingObjectBindingRegistry bindings;
  modeling::ModelingSession session;

  modeling::Result<modeling::PolygonDocument> first_document =
      modeling::PolygonDocument::make_box({});
  ASSERT_TRUE(first_document.ok()) << first_document.error_message();
  modeling::Result<modeling::ObjectId> first_object =
      session.add_document(std::move(first_document).value(), "First");
  ASSERT_TRUE(first_object.ok()) << first_object.error_message();
  const AssetId first_mesh = assets.add_mesh(MeshAsset{});
  ASSERT_NE(first_mesh, kInvalidAssetId);
  const EntityId first_entity =
      scene.create_entity("First", EntityKind::Mesh);
  ASSERT_NE(first_entity, kInvalidEntityId);
  ASSERT_TRUE(scene.set_mesh(first_entity, MeshComponent{.mesh = first_mesh}));
  ASSERT_TRUE(scene.set_modeling_object(
      first_entity,
      {.object_id = 100, .material_slot = 0U, .sdk_owned = true}));
  ASSERT_TRUE(scene.replace_selection(
      std::span<const EntityId>(&first_entity, 1U), first_entity));
  ASSERT_TRUE(bindings.bind(100, first_entity));
  ASSERT_TRUE(bindings.bind_sdk_object(100, first_object.value()));
  bindings.remember_scene_entity(100, scene, first_entity);

  modeling::Result<modeling::PolygonDocument> other_document =
      modeling::PolygonDocument::make_box({});
  ASSERT_TRUE(other_document.ok()) << other_document.error_message();
  modeling::Result<modeling::ObjectId> other_object =
      session.add_document(std::move(other_document).value(), "Other");
  ASSERT_TRUE(other_object.ok()) << other_object.error_message();
  const AssetId other_mesh = assets.add_mesh(MeshAsset{});
  ASSERT_NE(other_mesh, kInvalidAssetId);
  const EntityId other_entity =
      scene.create_entity("Other", EntityKind::Mesh);
  ASSERT_NE(other_entity, kInvalidEntityId);
  ASSERT_TRUE(scene.set_mesh(other_entity, MeshComponent{.mesh = other_mesh}));
  ASSERT_TRUE(scene.set_modeling_object(
      other_entity,
      {.object_id = 101, .material_slot = 0U, .sdk_owned = true}));
  ASSERT_TRUE(bindings.bind(101, other_entity));
  ASSERT_TRUE(bindings.bind_sdk_object(101, other_object.value()));
  bindings.remember_scene_entity(101, scene, other_entity);

  const std::size_t sdk_object_count = session.objects().size();
  const std::size_t sdk_undo_depth = session.undo_depth();
  const std::uint64_t sdk_content_revision = session.content_revision();
  const std::vector<AssetId> asset_ids = assets.mesh_ids();
  const std::uint64_t asset_revision = assets.revision();
  const std::vector<EntityId> scene_entities = scene.entities();
  const std::uint64_t scene_revision = scene.revision();
  const std::uint64_t scene_content_revision = scene.content_revision();
  const std::uint64_t scene_selection_revision = scene.selection_revision();
  const std::uint64_t scene_mesh_revision = scene.mesh_binding_revision();
  const std::uint64_t binding_revision = bindings.revision();
  const auto sdk_bindings =
      bindings.sdk_object_bindings_by_modeling_object();
  int adapter_state = 7;
  int restore_count = 0;
  const std::array affected_entities{first_entity, other_entity};
  const std::array affected_assets{first_mesh, other_mesh};
  const std::array affected_modeling_ids{100, 101, 200};

  {
    ModelingHostPublicationTransaction publication(
        scene, assets, bindings, session,
        [&]() noexcept {
          adapter_state = 7;
          ++restore_count;
        },
        {.checkpoint_scope =
             ModelingHostPublicationCheckpointScope::ModelingProjection,
         .capture_hierarchy = true,
         .capture_scene_selection = true,
         .affected_scene_entities = affected_entities,
         .affected_asset_ids = affected_assets,
         .affected_modeling_object_ids = affected_modeling_ids});
    ASSERT_TRUE(publication.ready());
    modeling::Result<modeling::PolygonDocument> second_document =
        modeling::PolygonDocument::make_box({});
    ASSERT_TRUE(second_document.ok()) << second_document.error_message();
    modeling::Result<modeling::ObjectId> second_object =
        session.add_document(std::move(second_document).value(), "Second");
    ASSERT_TRUE(second_object.ok()) << second_object.error_message();
    const AssetId second_mesh = assets.add_mesh(MeshAsset{});
    ASSERT_NE(second_mesh, kInvalidAssetId);
    publication.track_created_asset(second_mesh);
    const EntityId second_entity =
        scene.create_entity("Second", EntityKind::Mesh);
    ASSERT_NE(second_entity, kInvalidEntityId);
    publication.track_created_entity(second_entity);
    ASSERT_TRUE(
        scene.set_mesh(second_entity, MeshComponent{.mesh = second_mesh}));
    ASSERT_TRUE(scene.set_modeling_object(
        second_entity,
        {.object_id = 200, .material_slot = 0U, .sdk_owned = true}));
    ASSERT_TRUE(scene.replace_selection(
        std::span<const EntityId>(&second_entity, 1U), second_entity));
    ASSERT_TRUE(bindings.bind(200, second_entity));
    ASSERT_TRUE(bindings.bind_sdk_object(200, second_object.value()));
    ASSERT_TRUE(scene.destroy_entity(first_entity));
    bindings.unbind_object(100);
    ASSERT_TRUE(scene.clear_mesh(other_entity));
    ASSERT_TRUE(scene.set_curve(other_entity, CurveComponent{.curve = 999U}));
    adapter_state = 99;
  }

  EXPECT_EQ(restore_count, 1);
  EXPECT_EQ(adapter_state, 7);
  EXPECT_EQ(session.objects().size(), sdk_object_count);
  EXPECT_EQ(session.undo_depth(), sdk_undo_depth);
  EXPECT_EQ(session.content_revision(), sdk_content_revision);
  EXPECT_EQ(assets.mesh_ids(), asset_ids);
  EXPECT_EQ(assets.revision(), asset_revision);
  EXPECT_EQ(scene.entities(), scene_entities);
  EXPECT_EQ(scene.revision(), scene_revision);
  EXPECT_EQ(scene.content_revision(), scene_content_revision);
  EXPECT_EQ(scene.selection_revision(), scene_selection_revision);
  EXPECT_EQ(scene.mesh_binding_revision(), scene_mesh_revision);
  ASSERT_TRUE(scene.exists(first_entity));
  ASSERT_NE(scene.mesh(first_entity), nullptr);
  EXPECT_EQ(scene.mesh(first_entity)->mesh, first_mesh);
  ASSERT_NE(scene.mesh(other_entity), nullptr);
  EXPECT_EQ(scene.mesh(other_entity)->mesh, other_mesh);
  EXPECT_EQ(scene.curve(other_entity), nullptr);
  EXPECT_EQ(scene.primary_selection(), first_entity);
  EXPECT_EQ(bindings.revision(), binding_revision);
  EXPECT_EQ(bindings.sdk_object_bindings_by_modeling_object(), sdk_bindings);
  EXPECT_EQ(bindings.entity_for_modeling_object(100), first_entity);
  EXPECT_TRUE(bindings.scene_identity_for_modeling_object(100).has_value());
}

TEST(ModelingSessionServiceSplitTests,
     HostPublicationTransactionCommitAndRollbackPoliciesAreExplicit) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingObjectBindingRegistry bindings;
  modeling::ModelingSession session;
  int restore_count = 0;

  {
    ModelingHostPublicationTransaction publication(
        scene, assets, bindings, session,
        [&]() noexcept { ++restore_count; },
        {.checkpoint_scope =
             ModelingHostPublicationCheckpointScope::ModelingProjection,
         .capture_assets = false,
         .capture_scene = false,
         .capture_bindings = false});
    modeling::Result<modeling::PolygonDocument> document =
        modeling::PolygonDocument::make_box({});
    ASSERT_TRUE(document.ok()) << document.error_message();
    ASSERT_TRUE(session.add_document(std::move(document).value(), "Committed")
                    .ok());
    ASSERT_TRUE(publication.commit());
    EXPECT_FALSE(publication.active());
  }
  EXPECT_EQ(restore_count, 0);
  ASSERT_EQ(session.objects().size(), 1U);
  const std::array committed_participants{session.objects().front().id};

  {
    ModelingHostPublicationTransaction publication(
        scene, assets, bindings, session,
        [&]() noexcept { ++restore_count; },
        {.sdk_rollback = ModelingSdkRollbackPolicy::MutationCheckpoint,
         .checkpoint_scope =
             ModelingHostPublicationCheckpointScope::ModelingProjection,
         .capture_assets = false,
         .capture_scene = false,
         .capture_bindings = false,
         .affected_sdk_object_ids = committed_participants});
    ASSERT_TRUE(publication.ready());
    ASSERT_TRUE(
        session.rename_object(committed_participants.front(), "Explicit")
            .ok());
    EXPECT_TRUE(publication.rollback());
    EXPECT_FALSE(publication.active());
  }
  EXPECT_EQ(restore_count, 1);
  ASSERT_EQ(session.objects().size(), 1U);
  EXPECT_EQ(session.objects().front().name, "Committed");

  {
    ModelingHostPublicationTransaction publication(
        scene, assets, bindings, session,
        [&]() noexcept { ++restore_count; },
        {.sdk_rollback = ModelingSdkRollbackPolicy::MutationCheckpoint,
         .checkpoint_scope =
             ModelingHostPublicationCheckpointScope::ModelingProjection,
         .capture_assets = false,
         .capture_scene = false,
         .capture_bindings = false,
         .affected_sdk_object_ids = committed_participants});
    ASSERT_TRUE(publication.ready());
    ASSERT_TRUE(
        session.rename_object(committed_participants.front(), "Destructor")
            .ok());
  }
  EXPECT_EQ(restore_count, 2);
  ASSERT_EQ(session.objects().size(), 1U);
  EXPECT_EQ(session.objects().front().name, "Committed");

  const modeling::ObjectId retained_sdk =
      modeling::make_id<modeling::ObjectTag>(77U);
  {
    const std::array affected_modeling_ids{500};
    ModelingHostPublicationTransactionOptions options{
        .sdk_rollback = ModelingSdkRollbackPolicy::None,
        .checkpoint_scope =
            ModelingHostPublicationCheckpointScope::ModelingProjection,
        .capture_assets = false,
        .capture_scene = false,
        .capture_bindings = true,
        .capture_hierarchy = false,
        .affected_modeling_object_ids = affected_modeling_ids,
    };
    ModelingHostPublicationTransaction publication(
        scene, assets, bindings, session,
        [&]() noexcept { ++restore_count; }, options);
    ASSERT_TRUE(publication.ready());
    modeling::Result<modeling::PolygonDocument> document =
        modeling::PolygonDocument::make_box({});
    ASSERT_TRUE(document.ok()) << document.error_message();
    ASSERT_TRUE(session.add_document(std::move(document).value(), "Retained")
                    .ok());
    ASSERT_TRUE(bindings.bind_sdk_object(500, retained_sdk));
  }
  EXPECT_EQ(restore_count, 3);
  EXPECT_EQ(session.objects().size(), 2U);
  EXPECT_EQ(bindings.sdk_object_for_modeling_object(500), std::nullopt);
}

TEST(ModelingSessionServiceSplitTests,
     HostPublicationRollbackPreservesLiveFormalInstanceGraphExactly) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingObjectBindingRegistry bindings;
  modeling::ModelingSession session;

  const EntityId definition =
      scene.create_entity("Definition", EntityKind::Empty);
  const EntityId member = scene.create_entity("Member", EntityKind::Mesh);
  const EntityId occurrence =
      scene.create_entity("Occurrence", EntityKind::Generic);
  const EntityId target = scene.create_entity("Target", EntityKind::Mesh);
  ASSERT_NE(definition, kInvalidEntityId);
  ASSERT_NE(member, kInvalidEntityId);
  ASSERT_NE(occurrence, kInvalidEntityId);
  ASSERT_NE(target, kInvalidEntityId);
  ASSERT_TRUE(scene.set_mesh(member, {.mesh = 10u}));
  ASSERT_TRUE(scene.set_modeling_object(
      member, {.object_id = 10, .sdk_owned = true}));
  ASSERT_TRUE(scene.set_parent(member, definition, false));
  ASSERT_TRUE(scene.set_instance_definition(definition));
  ASSERT_TRUE(scene.set_instance_occurrence(
      occurrence, {.definition = definition}));
  ASSERT_TRUE(scene.set_mesh(target, {.mesh = 20u}));
  ASSERT_TRUE(scene.set_modeling_object(
      target, {.object_id = 20, .sdk_owned = true}));
  ASSERT_TRUE(bindings.bind(10, member));
  ASSERT_TRUE(bindings.bind(20, target));
  bindings.remember_scene_entity(10, scene, member);
  bindings.remember_scene_entity(20, scene, target);

  const std::vector<EntityId> hierarchy_before = scene.entity_tree_order();
  const std::uint64_t revision_before = scene.revision();
  const std::uint64_t content_revision_before = scene.content_revision();
  const std::uint64_t hierarchy_revision_before =
      scene.hierarchy_presentation_revision();
  const std::uint64_t mesh_revision_before = scene.mesh_binding_revision();
  const std::uint64_t curve_revision_before = scene.curve_binding_revision();
  const std::uint64_t render_revision_before =
      scene.render_component_revision();

  {
    const std::array affected_entities{target};
    ModelingHostPublicationTransaction publication(
        scene, assets, bindings, session, {},
        {.sdk_rollback = ModelingSdkRollbackPolicy::None,
         .checkpoint_scope =
             ModelingHostPublicationCheckpointScope::ModelingProjection,
         .capture_assets = false,
         .capture_scene = true,
         .capture_bindings = false,
         .capture_hierarchy = true,
         .affected_scene_entities = affected_entities});
    ASSERT_TRUE(publication.ready());
    ASSERT_TRUE(scene.clear_mesh(target));
    ASSERT_TRUE(scene.set_curve(target, {.curve = 99u}));
    ASSERT_TRUE(scene.set_material(target, {.material = 88u}));
  }

  EXPECT_EQ(scene.entity_tree_order(), hierarchy_before);
  EXPECT_EQ(scene.revision(), revision_before);
  EXPECT_EQ(scene.content_revision(), content_revision_before);
  EXPECT_EQ(scene.hierarchy_presentation_revision(),
            hierarchy_revision_before);
  EXPECT_EQ(scene.mesh_binding_revision(), mesh_revision_before);
  EXPECT_EQ(scene.curve_binding_revision(), curve_revision_before);
  EXPECT_EQ(scene.render_component_revision(), render_revision_before);
  ASSERT_NE(scene.instance_definition(definition), nullptr);
  ASSERT_NE(scene.instance_occurrence(occurrence), nullptr);
  EXPECT_EQ(scene.instance_occurrence(occurrence)->definition, definition);
  EXPECT_EQ(scene.parent(member), definition);
  ASSERT_NE(scene.mesh(member), nullptr);
  EXPECT_EQ(scene.mesh(member)->mesh, 10u);
  ASSERT_NE(scene.mesh(target), nullptr);
  EXPECT_EQ(scene.mesh(target)->mesh, 20u);
  EXPECT_EQ(scene.curve(target), nullptr);
  EXPECT_EQ(scene.material(target), nullptr);
}

TEST(ModelingSessionServiceSplitTests,
     ObjectRuntimeMapKeepsDestroyedSceneIdentityForRedoProjection) {
  SceneDocument scene;
  const EntityId parent = scene.create_entity("Parent", EntityKind::Empty);
  const EntityId entity = scene.create_entity("Redo Source", EntityKind::Mesh);
  scene.set_mesh(entity, MeshComponent{.mesh = 99});
  scene.set_material(entity, MaterialComponent{.material = 42});
  TransformComponent transform;
  transform.position = {2.0f, 3.0f, 4.0f};
  scene.set_transform(entity, transform);
  ASSERT_TRUE(scene.set_parent(entity, parent, false));
  scene.set_visible(entity, false);
  scene.set_locked(entity, true);
  const SceneLayerId layer = scene.create_layer("Redo Layer");
  ASSERT_NE(layer, kInvalidSceneLayerId);
  ASSERT_TRUE(scene.set_entity_layer(entity, layer));
  const EditorObjectId editor_object_id = scene.editor_object_id(entity);

  ModelingObjectRuntimeMap object_map;
  ASSERT_TRUE(object_map.bind(300, entity));
  object_map.remember_scene_entity(300, scene, entity);

  const std::optional<ModelingObjectSceneIdentity> identity =
      object_map.scene_identity_for_modeling_object(300);
  ASSERT_TRUE(identity.has_value());
  EXPECT_EQ(identity->id, entity);
  EXPECT_EQ(identity->editor_object_id, editor_object_id);
  EXPECT_EQ(identity->name, "Redo Source");
  EXPECT_EQ(identity->parent, parent);
  EXPECT_EQ(identity->transform.position.x, transform.position.x);
  EXPECT_FALSE(identity->visibility.visible);
  EXPECT_TRUE(identity->lock.locked);
  ASSERT_TRUE(identity->mesh.has_value());
  EXPECT_EQ(identity->mesh->mesh, 99);
  ASSERT_TRUE(identity->material.has_value());
  EXPECT_EQ(identity->material->material.embedded_asset(), AssetId{42u});
  EXPECT_EQ(identity->layer_id, layer);

  ASSERT_TRUE(scene.destroy_entity(entity));
  object_map.unbind_object(300);
  EXPECT_EQ(object_map.entity_for_modeling_object(300), std::nullopt);
  EXPECT_TRUE(object_map.records(scene).empty());
  ASSERT_TRUE(object_map.scene_identity_for_modeling_object(300).has_value());
  EXPECT_EQ(object_map.scene_identity_for_modeling_object(300)->id, entity);
}

TEST(ModelingSessionServiceSplitTests,
     ObjectRuntimeMapCapturesSceneIdentitiesInRequestedOrder) {
  SceneDocument scene;
  const EntityId first = scene.create_entity("First", EntityKind::Mesh);
  const EntityId second = scene.create_entity("Second", EntityKind::Mesh);
  TransformComponent first_transform;
  first_transform.position = {1.0f, 0.0f, 0.0f};
  TransformComponent second_transform;
  second_transform.position = {2.0f, 0.0f, 0.0f};
  scene.set_transform(first, first_transform);
  scene.set_transform(second, second_transform);

  ModelingObjectRuntimeMap object_map;
  ASSERT_TRUE(object_map.bind(100, first));
  ASSERT_TRUE(object_map.bind(200, second));

  const std::array<int, 2> requested{200, 100};
  const std::vector<ModelingObjectSceneIdentity> identities =
      object_map.capture_scene_identities_for_modeling_objects(scene,
                                                               requested);

  ASSERT_EQ(identities.size(), 2U);
  EXPECT_EQ(identities[0].id, second);
  EXPECT_EQ(identities[0].transform.position.x, second_transform.position.x);
  EXPECT_EQ(identities[1].id, first);
  EXPECT_EQ(identities[1].transform.position.x, first_transform.position.x);
}

TEST(ModelingSessionServiceSplitTests,
     ObjectRuntimeMapPreservesLocalFrameVersionAcrossTombstoneRestore) {
  SceneDocument scene;
  const EntityId source =
      scene.create_entity_with_id(77u, "Canonical Source", EntityKind::Mesh);
  ASSERT_NE(source, kInvalidEntityId);
  ASSERT_TRUE(scene.set_modeling_object(
      source, ModelingObjectComponent{
                  .object_id = 100,
                  .material_slot = 3u,
                  .sdk_owned = true,
                  .local_frame_version =
                      kCanonicalModelingObjectLocalFrameVersion,
              }));
  ASSERT_TRUE(scene.set_mesh(source, MeshComponent{.mesh = 11u}));

  ModelingObjectRuntimeMap object_map;
  ASSERT_TRUE(object_map.bind(100, source));
  object_map.remember_scene_entity(100, scene, source);
  const std::optional<ModelingObjectSceneIdentity> identity =
      object_map.scene_identity_for_modeling_object(100);
  ASSERT_TRUE(identity.has_value());
  ASSERT_TRUE(identity->modeling_object.has_value());
  EXPECT_EQ(identity->modeling_object->local_frame_version,
            kCanonicalModelingObjectLocalFrameVersion);

  ASSERT_TRUE(scene.destroy_entity(source));
  object_map.unbind_object(100);
  ModelingMeshSyncService mesh_sync;
  const EntityId restored =
      mesh_sync.restore_projected_mesh_entity(scene, *identity);
  ASSERT_EQ(restored, source);
  ASSERT_TRUE(identity->mesh.has_value());
  ASSERT_TRUE(mesh_sync.bind_projected_object(
      scene, restored, 100, identity->modeling_object->material_slot,
      identity->modeling_object->sdk_owned, identity->mesh->mesh,
      identity->modeling_object->local_frame_version));
  const ModelingObjectComponent *component = scene.modeling_object(restored);
  ASSERT_NE(component, nullptr);
  EXPECT_EQ(component->local_frame_version,
            kCanonicalModelingObjectLocalFrameVersion);
}

TEST(ModelingSessionServiceSplitTests,
     MeshSyncServiceAppliesDuplicateSceneIdentityPreservingMeshAsset) {
  SceneDocument scene;
  const EntityId parent = scene.create_entity("Parent", EntityKind::Empty);
  const EntityId source = scene.create_entity("Source", EntityKind::Mesh);
  const EntityId duplicate = scene.create_entity("Duplicate", EntityKind::Mesh);

  TransformComponent transform;
  transform.position = {3.0f, 4.0f, 5.0f};
  scene.set_transform(source, transform);
  ASSERT_TRUE(scene.set_parent(source, parent, false));
  scene.set_visible(source, false);
  scene.set_locked(source, true);
  scene.set_mesh(source, MeshComponent{
                             .mesh = 11,
                             .editor_only = true,
                             .cast_shadows = false,
                             .double_sided = false,
                             .generate_collisions = false,
                             .collision_type = MeshCollisionType::Mesh,
                             .display_collision = true,
                             .body_type = MeshPhysicsBodyType::RigidBody,
                         });
  scene.set_material(source, MaterialComponent{.material = 44});
  const SceneLayerId layer = scene.create_layer("Duplicate Layer");
  ASSERT_NE(layer, kInvalidSceneLayerId);
  ASSERT_TRUE(scene.set_entity_layer(source, layer));
  scene.set_mesh(duplicate, MeshComponent{.mesh = 99});
  scene.set_material(duplicate, MaterialComponent{.material = 55});

  ModelingObjectRuntimeMap object_map;
  ASSERT_TRUE(object_map.bind(100, source));
  const std::array<int, 1> requested{100};
  const std::vector<ModelingObjectSceneIdentity> identities =
      object_map.capture_scene_identities_for_modeling_objects(scene,
                                                               requested);
  ASSERT_EQ(identities.size(), 1U);

  ModelingMeshSyncService mesh_sync;
  ASSERT_TRUE(mesh_sync.apply_scene_identity_to_duplicate_entity(
      scene, duplicate, identities.front()));

  const TransformComponent *duplicate_transform = scene.transform(duplicate);
  ASSERT_NE(duplicate_transform, nullptr);
  EXPECT_EQ(duplicate_transform->position.x, transform.position.x);
  EXPECT_EQ(scene.parent(duplicate), parent);
  ASSERT_NE(scene.visibility(duplicate), nullptr);
  EXPECT_FALSE(scene.visibility(duplicate)->visible);
  ASSERT_NE(scene.lock(duplicate), nullptr);
  EXPECT_TRUE(scene.lock(duplicate)->locked);
  const MeshComponent *duplicate_mesh = scene.mesh(duplicate);
  ASSERT_NE(duplicate_mesh, nullptr);
  EXPECT_EQ(duplicate_mesh->mesh, 99);
  EXPECT_TRUE(duplicate_mesh->editor_only);
  EXPECT_FALSE(duplicate_mesh->cast_shadows);
  EXPECT_FALSE(duplicate_mesh->double_sided);
  EXPECT_FALSE(duplicate_mesh->generate_collisions);
  EXPECT_EQ(duplicate_mesh->collision_type, MeshCollisionType::Mesh);
  EXPECT_TRUE(duplicate_mesh->display_collision);
  EXPECT_EQ(duplicate_mesh->body_type, MeshPhysicsBodyType::RigidBody);
  ASSERT_NE(scene.material(duplicate), nullptr);
  EXPECT_EQ(scene.material(duplicate)->material.embedded_asset(),
            AssetId{44u});
  EXPECT_EQ(scene.entity_layer(duplicate), layer);
}

TEST(ModelingSessionServiceSplitTests,
     MeshSyncServicePreservesCurveSolidifySettingsAcrossRebindingAndDuplicate) {
  SceneDocument scene;
  const EntityId source = scene.create_entity("Source Curve");
  const CurveComponent source_component{
      .curve = 11u,
      .solidify = true,
      .solidify_resolution = 36,
      .solidify_radius = 2.25F,
  };
  ASSERT_TRUE(scene.set_curve(source, source_component));
  ASSERT_TRUE(scene.set_modeling_object(
      source, {.object_id = 100, .sdk_owned = true}));

  ModelingObjectRuntimeMap object_map;
  ASSERT_TRUE(object_map.bind(100, source));
  const std::array<int, 1> requested{100};
  const std::vector<ModelingObjectSceneIdentity> identities =
      object_map.capture_scene_identities_for_modeling_objects(scene,
                                                               requested);
  ASSERT_EQ(identities.size(), 1U);

  const EntityId duplicate = scene.create_entity("Duplicate Curve");
  ASSERT_TRUE(scene.set_curve(duplicate, {.curve = 99u}));
  ModelingMeshSyncService mesh_sync;
  ASSERT_TRUE(mesh_sync.apply_scene_identity_to_duplicate_entity(
      scene, duplicate, identities.front()));
  ASSERT_NE(scene.curve(duplicate), nullptr);
  EXPECT_EQ(scene.curve(duplicate)->curve, 99u);
  EXPECT_TRUE(scene.curve(duplicate)->solidify);
  EXPECT_EQ(scene.curve(duplicate)->solidify_resolution, 36);
  EXPECT_FLOAT_EQ(scene.curve(duplicate)->solidify_radius, 2.25F);

  ASSERT_TRUE(
      mesh_sync.set_projected_curve_binding(scene, duplicate, 101u));
  ASSERT_NE(scene.curve(duplicate), nullptr);
  EXPECT_EQ(scene.curve(duplicate)->curve, 101u);
  EXPECT_TRUE(scene.curve(duplicate)->solidify);
  EXPECT_EQ(scene.curve(duplicate)->solidify_resolution, 36);
  EXPECT_FLOAT_EQ(scene.curve(duplicate)->solidify_radius, 2.25F);

  ASSERT_TRUE(mesh_sync.bind_projected_curve_object(scene, duplicate, 101,
                                                    102u));
  ASSERT_NE(scene.curve(duplicate), nullptr);
  EXPECT_EQ(scene.curve(duplicate)->curve, 102u);
  EXPECT_TRUE(scene.curve(duplicate)->solidify);
  EXPECT_EQ(scene.curve(duplicate)->solidify_resolution, 36);
  EXPECT_FLOAT_EQ(scene.curve(duplicate)->solidify_radius, 2.25F);
}

TEST(ModelingSessionServiceSplitTests,
     MeshSyncEntityMaterialReportsNoOpMutationAndOwnerFailure) {
  SceneDocument scene;
  ModelingMeshSyncService mesh_sync;
  const EntityId entity =
      scene.create_entity("Material Result Mesh", EntityKind::Mesh);
  ASSERT_NE(entity, kInvalidEntityId);
  const MaterialComponent material{.material = MaterialReference(91u)};

  EXPECT_TRUE(mesh_sync.set_entity_material(scene, entity, material));
  EXPECT_FALSE(mesh_sync.set_entity_material(scene, entity, material));
  EXPECT_FALSE(
      mesh_sync.set_entity_material(scene, kInvalidEntityId, material));
}

TEST(ModelingSessionServiceSplitTests,
     DirectEdgeHoverRoundTripsItsPickedContextFace) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);
  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f},
                 .max = {1.0f, 1.0f, 1.0f}},
      .name = "Hover Context Box",
  });
  ASSERT_TRUE(box.ok) << box.message;

  const ModelingDirectHoverRequest request{
      .modeling_object_id = box.object.modeling_object_id,
      .candidate_object_id = box.object.modeling_object_id,
      .kind = ModelingActiveSelectionKind::Edge,
      .edge = {1, 2},
      .face_id = 6,
  };
  const ModelingCommandResult changed = service.set_selection_hover(request);
  ASSERT_TRUE(changed.ok) << changed.message;
  EXPECT_TRUE(changed.changed);
  const ModelingHoverSummary summary = service.selection_hover_summary();
  EXPECT_EQ(summary.target.kind, ModelingActiveSelectionKind::Edge);
  EXPECT_EQ(summary.target.object_id, box.object.modeling_object_id);
  EXPECT_EQ(summary.target.edge_a, 1);
  EXPECT_EQ(summary.target.edge_b, 2);
  EXPECT_EQ(summary.target.face_id, 6);

  const ModelingCommandResult unchanged = service.set_selection_hover(request);
  ASSERT_TRUE(unchanged.ok) << unchanged.message;
  EXPECT_FALSE(unchanged.changed);
}

TEST(ModelingSessionServiceSplitTests,
     InvalidCreationInputsLeaveRuntimeSceneAssetsSelectionAndHistoryExact) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult baseline = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f},
                 .max = {1.0f, 1.0f, 1.0f}},
      .name = "Creation Boundary Baseline",
  });
  ASSERT_TRUE(baseline.ok) << baseline.message;
  ASSERT_TRUE(service
                  .set_selection_hover({
                      .modeling_object_id = baseline.object.modeling_object_id,
                      .kind = ModelingActiveSelectionKind::Object,
                  })
                  .ok);

  const ModelingSelectionSummary selection_before =
      service.selection_summary();
  const ModelingSelectionExport export_before = service.export_selection();
  const ModelingHoverSummary hover_before = service.selection_hover_summary();
  const std::vector<ModelingObjectRecord> objects_before = service.objects();
  const std::vector<EntityId> entities_before = scene.entities();
  const std::vector<EntityId> selected_before = scene.selected_entities();
  const EntityId active_before = scene.primary_selection();
  const std::uint64_t scene_revision_before = scene.revision();
  const std::uint64_t scene_selection_revision_before =
      scene.selection_revision();
  const std::vector<AssetId> mesh_ids_before = assets.mesh_ids();
  const std::uint64_t assets_revision_before = assets.revision();

  const auto expect_exact_state = [&]() {
    const ModelingSelectionSummary selection = service.selection_summary();
    EXPECT_EQ(selection.mode, selection_before.mode);
    EXPECT_EQ(selection.has_selection, selection_before.has_selection);
    EXPECT_EQ(selection.active_kind, selection_before.active_kind);
    EXPECT_EQ(selection.active_object_id, selection_before.active_object_id);
    EXPECT_EQ(selection.selected_object_count,
              selection_before.selected_object_count);
    EXPECT_EQ(selection.content_revision, selection_before.content_revision);
    EXPECT_EQ(selection.selection_revision,
              selection_before.selection_revision);

    const ModelingSelectionExport exported = service.export_selection();
    ASSERT_EQ(exported.objects.size(), export_before.objects.size());
    ASSERT_FALSE(exported.objects.empty());
    EXPECT_EQ(exported.objects.front().modeling_object_id,
              export_before.objects.front().modeling_object_id);
    EXPECT_EQ(exported.objects.front().entity,
              export_before.objects.front().entity);
    EXPECT_EQ(exported.objects.front().selected,
              export_before.objects.front().selected);

    const ModelingHoverSummary hover = service.selection_hover_summary();
    EXPECT_EQ(hover.target.kind, hover_before.target.kind);
    EXPECT_EQ(hover.target.object_id, hover_before.target.object_id);
    EXPECT_EQ(hover.target_selected, hover_before.target_selected);

    const std::vector<ModelingObjectRecord> objects = service.objects();
    ASSERT_EQ(objects.size(), objects_before.size());
    ASSERT_FALSE(objects.empty());
    EXPECT_EQ(objects.front().modeling_object_id,
              objects_before.front().modeling_object_id);
    EXPECT_EQ(objects.front().entity, objects_before.front().entity);
    EXPECT_EQ(objects.front().mesh, objects_before.front().mesh);
    EXPECT_EQ(scene.entities(), entities_before);
    EXPECT_EQ(scene.selected_entities(), selected_before);
    EXPECT_EQ(scene.primary_selection(), active_before);
    EXPECT_EQ(scene.revision(), scene_revision_before);
    EXPECT_EQ(scene.selection_revision(), scene_selection_revision_before);
    EXPECT_EQ(assets.mesh_ids(), mesh_ids_before);
    EXPECT_EQ(assets.revision(), assets_revision_before);
  };

  EditorHistoryService history;
  ModelingCreateBoxRequest invalid_box;
  invalid_box.bounds.min.x = std::numeric_limits<float>::quiet_NaN();
  const CommandResult box_result = history.execute(
      std::make_unique<CreateBoxFromBoundsHistoryCommand>(service,
                                                           invalid_box));
  EXPECT_FALSE(box_result.ok);
  EXPECT_FALSE(box_result.changed);
  EXPECT_FALSE(history.can_undo());
  EXPECT_FALSE(history.can_redo());
  expect_exact_state();

  ModelingCreateMeshRequest invalid_mesh;
  invalid_mesh.document = quader_poly::make_cube({});
  ASSERT_FALSE(invalid_mesh.document.vertices.empty());
  invalid_mesh.document.vertices.front().position.y =
      std::numeric_limits<float>::infinity();
  const CommandResult mesh_result = history.execute(
      std::make_unique<CreateMeshHistoryCommand>(service,
                                                  std::move(invalid_mesh)));
  EXPECT_FALSE(mesh_result.ok);
  EXPECT_FALSE(mesh_result.changed);
  EXPECT_FALSE(history.can_undo());
  EXPECT_FALSE(history.can_redo());
  expect_exact_state();
}

TEST(ModelingSessionServiceSplitTests,
     MementoRestoreKeepsRevisionsAndObjectIdsSessionMonotonic) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult baseline = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f},
                 .max = {1.0f, 1.0f, 1.0f}},
      .name = "Memento Baseline",
  });
  ASSERT_TRUE(baseline.ok) << baseline.message;
  const EntityId baseline_entity = baseline.object.entity;
  const ModelingSessionMemento before = service.capture_history_memento();
  ASSERT_TRUE(before.valid());

  const ModelingCreateBoxResult first_transient = service.create_box_from_bounds({
      .bounds = {.min = {2.0f, 2.0f, 2.0f},
                 .max = {3.0f, 3.0f, 3.0f}},
      .name = "Canceled Primitive One",
  });
  ASSERT_TRUE(first_transient.ok) << first_transient.message;
  const ModelingCreateBoxResult second_transient = service.create_box_from_bounds({
      .bounds = {.min = {4.0f, 4.0f, 4.0f},
                 .max = {5.0f, 5.0f, 5.0f}},
      .name = "Canceled Primitive Two",
  });
  ASSERT_TRUE(second_transient.ok) << second_transient.message;
  const ModelingSelectionSummary transient_summary =
      service.selection_summary();

  const ModelingCommandResult restored = service.restore_history_memento(before);
  ASSERT_TRUE(restored.ok) << restored.message;
  ASSERT_TRUE(restored.changed);
  const std::vector<ModelingObjectRecord> restored_objects = service.objects();
  ASSERT_EQ(restored_objects.size(), 1u);
  EXPECT_EQ(restored_objects.front().modeling_object_id,
            baseline.object.modeling_object_id);
  EXPECT_EQ(restored_objects.front().entity, baseline_entity);
  const ModelingSelectionSummary restored_summary =
      service.selection_summary();
  EXPECT_GT(restored_summary.content_revision,
            transient_summary.content_revision);
  EXPECT_GT(restored_summary.selection_revision,
            transient_summary.selection_revision);

  const ModelingCreateBoxResult replacement = service.create_box_from_bounds({
      .bounds = {.min = {6.0f, 6.0f, 6.0f},
                 .max = {7.0f, 7.0f, 7.0f}},
      .name = "Replacement Primitive",
  });
  ASSERT_TRUE(replacement.ok) << replacement.message;
  const int last_transient_id = std::max(
      first_transient.object.modeling_object_id,
      second_transient.object.modeling_object_id);
  EXPECT_EQ(replacement.object.modeling_object_id, last_transient_id + 1);
}

TEST(ModelingSessionServiceSplitTests,
     StrictMementoRestoreRejectsMissingNativeSiblingWithoutReportingMutation) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult first = service.create_box_from_bounds({
      .bounds = {.min = {-2.0f, -1.0f, -1.0f},
                 .max = {-1.0f, 1.0f, 1.0f}},
      .name = "Strict Restore First",
  });
  ASSERT_TRUE(first.ok) << first.message;
  const EntityId native_sibling = scene.create_empty("Strict Native Sibling");
  ASSERT_NE(native_sibling, kInvalidEntityId);
  const ModelingCreateBoxResult second = service.create_box_from_bounds({
      .bounds = {.min = {1.0f, -1.0f, -1.0f},
                 .max = {2.0f, 1.0f, 1.0f}},
      .name = "Strict Restore Second",
  });
  ASSERT_TRUE(second.ok) << second.message;
  EXPECT_EQ(scene.root_entities(),
            (std::vector<EntityId>{first.object.entity, native_sibling,
                                   second.object.entity}));
  const ModelingSessionMemento before = service.capture_history_memento();
  ASSERT_TRUE(before.valid());

  const std::array selected{first.object.entity, native_sibling,
                            second.object.entity};
  ASSERT_TRUE(scene.replace_selection(selected, second.object.entity));
  std::unique_ptr<IUndoableCommand> delete_command =
      service.create_delete_selection_command();
  ASSERT_NE(delete_command, nullptr);
  const CommandResult removed = delete_command->redo();
  ASSERT_TRUE(removed.ok) << removed.message;
  ASSERT_TRUE(removed.changed);
  ASSERT_TRUE(scene.root_entities().empty());
  const std::uint64_t scene_revision = scene.revision();
  const std::uint64_t asset_revision = assets.revision();

  const ModelingCommandResult restored = service.restore_history_memento(before);
  EXPECT_FALSE(restored.ok);
  EXPECT_FALSE(restored.changed);
  EXPECT_NE(restored.message.find(
                "SDK history scene identity sibling order could not be restored"),
            std::string::npos);
  EXPECT_TRUE(scene.root_entities().empty());
  EXPECT_TRUE(service.objects().empty());
  EXPECT_EQ(scene.revision(), scene_revision);
  EXPECT_EQ(assets.revision(), asset_revision);
}

TEST(ModelingSessionServiceSplitTests,
     CreationProjectionFailureRestoresRuntimeSelectionAndAddsNoHistory) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult baseline = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f},
                 .max = {1.0f, 1.0f, 1.0f}},
      .name = "Projection Failure Baseline",
  });
  ASSERT_TRUE(baseline.ok) << baseline.message;
  ASSERT_TRUE(service
                  .set_selection_hover({
                      .modeling_object_id = baseline.object.modeling_object_id,
                      .kind = ModelingActiveSelectionKind::Object,
                  })
                  .ok);

  const ModelingSelectionSummary selection_before =
      service.selection_summary();
  const ModelingHoverSummary hover_before = service.selection_hover_summary();
  const std::vector<ModelingObjectRecord> objects_before = service.objects();
  const std::vector<EntityId> entities_before = scene.entities();
  const std::vector<EntityId> selected_before = scene.selected_entities();
  const EntityId active_before = scene.primary_selection();
  const std::vector<AssetId> mesh_ids_before = assets.mesh_ids();

  scene.set_editor_object_id_allocator(
      [](EditorObjectId) { return kInvalidEditorObjectId; });
  EditorHistoryService history;
  const CommandResult failed = history.execute(
      std::make_unique<CreateBoxFromBoundsHistoryCommand>(
          service,
          ModelingCreateBoxRequest{
              .bounds = {.min = {2.0f, 2.0f, 2.0f},
                         .max = {4.0f, 4.0f, 4.0f}},
              .name = "Must Roll Back",
          }));
  EXPECT_FALSE(failed.ok);
  EXPECT_FALSE(failed.changed);
  EXPECT_FALSE(history.can_undo());
  EXPECT_FALSE(history.can_redo());

  const ModelingSelectionSummary selection_after =
      service.selection_summary();
  EXPECT_EQ(selection_after.active_object_id,
            selection_before.active_object_id);
  EXPECT_EQ(selection_after.selected_object_count,
            selection_before.selected_object_count);
  EXPECT_EQ(selection_after.content_revision,
            selection_before.content_revision);
  EXPECT_EQ(selection_after.selection_revision,
            selection_before.selection_revision);
  const ModelingHoverSummary hover_after = service.selection_hover_summary();
  EXPECT_EQ(hover_after.target.kind, hover_before.target.kind);
  EXPECT_EQ(hover_after.target.object_id, hover_before.target.object_id);
  EXPECT_EQ(hover_after.target_selected, hover_before.target_selected);
  const std::vector<ModelingObjectRecord> objects_after = service.objects();
  ASSERT_EQ(objects_after.size(), objects_before.size());
  ASSERT_FALSE(objects_after.empty());
  EXPECT_EQ(objects_after.front().modeling_object_id,
            objects_before.front().modeling_object_id);
  EXPECT_EQ(objects_after.front().entity, objects_before.front().entity);
  EXPECT_EQ(scene.entities(), entities_before);
  EXPECT_EQ(scene.selected_entities(), selected_before);
  EXPECT_EQ(scene.primary_selection(), active_before);
  EXPECT_EQ(assets.mesh_ids(), mesh_ids_before);

  ModelingCreateMeshRequest valid_mesh;
  valid_mesh.document = quader_poly::make_cube({});
  valid_mesh.name = "Must Also Roll Back";
  const CommandResult failed_mesh = history.execute(
      std::make_unique<CreateMeshHistoryCommand>(service,
                                                  std::move(valid_mesh)));
  EXPECT_FALSE(failed_mesh.ok);
  EXPECT_FALSE(failed_mesh.changed);
  EXPECT_FALSE(history.can_undo());
  EXPECT_FALSE(history.can_redo());
  const ModelingSelectionSummary selection_after_mesh =
      service.selection_summary();
  EXPECT_EQ(selection_after_mesh.active_object_id,
            selection_before.active_object_id);
  EXPECT_EQ(selection_after_mesh.selected_object_count,
            selection_before.selected_object_count);
  EXPECT_EQ(selection_after_mesh.content_revision,
            selection_before.content_revision);
  EXPECT_EQ(selection_after_mesh.selection_revision,
            selection_before.selection_revision);
  const ModelingHoverSummary hover_after_mesh =
      service.selection_hover_summary();
  EXPECT_EQ(hover_after_mesh.target.kind, hover_before.target.kind);
  EXPECT_EQ(hover_after_mesh.target.object_id, hover_before.target.object_id);
  EXPECT_EQ(hover_after_mesh.target_selected, hover_before.target_selected);
  EXPECT_EQ(service.objects().size(), objects_before.size());
  EXPECT_EQ(scene.entities(), entities_before);
  EXPECT_EQ(scene.selected_entities(), selected_before);
  EXPECT_EQ(scene.primary_selection(), active_before);
  EXPECT_EQ(assets.mesh_ids(), mesh_ids_before);
}

TEST(ModelingSessionServiceSplitTests,
     FailedBoxCommitRestoresTheReadyPreviewForAnExactRetry) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingBoxToolEvent start{
      .screen_position = {300.0f, 240.0f},
      .ray = {.origin = {0.0f, 4.0f, 0.0f},
              .direction = {0.0f, -1.0f, 0.0f}},
      .grid_size = 1.0,
  };
  const ModelingBoxToolEvent footprint_end{
      .screen_position = {420.0f, 240.0f},
      .ray = {.origin = {2.0f, 4.0f, 2.0f},
              .direction = {0.0f, -1.0f, 0.0f}},
      .grid_size = 1.0,
  };
  const ModelingBoxToolEvent height_end{
      .screen_position = {420.0f, 340.0f},
      .ray = start.ray,
      .grid_size = 1.0,
  };

  ASSERT_TRUE(service.activate_box_tool().ok);
  ASSERT_TRUE(service.begin_box_footprint(start).ok);
  ASSERT_TRUE(service.update_box_footprint(footprint_end).ok);
  ASSERT_TRUE(service.begin_box_height(start).ok);
  ASSERT_TRUE(service.update_box_height(height_end).ok);
  const ModelingBoxPreview preview_before = service.box_preview();
  ASSERT_TRUE(preview_before.valid);
  ASSERT_EQ(preview_before.drag_state, ModelingBoxDragState::Height);
  const ModelingSelectionSummary selection_before =
      service.selection_summary();
  const std::vector<EntityId> entities_before = scene.entities();
  const std::vector<EntityId> selected_before = scene.selected_entities();
  const std::vector<AssetId> mesh_ids_before = assets.mesh_ids();

  scene.set_editor_object_id_allocator(
      [](EditorObjectId) { return kInvalidEditorObjectId; });
  const ModelingCreateBoxResult failed = service.commit_box("Failed Commit");
  EXPECT_FALSE(failed.ok);
  EXPECT_FALSE(failed.changed);
  const ModelingBoxPreview preview_after = service.box_preview();
  EXPECT_TRUE(preview_after.valid);
  EXPECT_EQ(preview_after.drag_state, preview_before.drag_state);
  EXPECT_EQ(preview_after.corner_count, preview_before.corner_count);
  EXPECT_DOUBLE_EQ(preview_after.distance, preview_before.distance);
  EXPECT_EQ(service.selection_summary().content_revision,
            selection_before.content_revision);
  EXPECT_EQ(service.selection_summary().selection_revision,
            selection_before.selection_revision);
  EXPECT_EQ(scene.entities(), entities_before);
  EXPECT_EQ(scene.selected_entities(), selected_before);
  EXPECT_EQ(assets.mesh_ids(), mesh_ids_before);

  scene.set_editor_object_id_allocator({});
  const ModelingCreateBoxResult retried = service.commit_box("Retried Commit");
  ASSERT_TRUE(retried.ok) << retried.message;
  EXPECT_TRUE(retried.changed);
  EXPECT_EQ(service.box_preview().drag_state, ModelingBoxDragState::Idle);
}

TEST(ModelingSessionServiceSplitTests,
     HistoryMementoRestoresSharedMeshCloneIdentityAcrossUndoRedo) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f},
                 .max = {1.0f, 1.0f, 1.0f}},
      .name = "Shared History Source",
  });
  ASSERT_TRUE(box.ok) << box.message;
  const MeshComponent *source_mesh = scene.mesh(box.object.entity);
  ASSERT_NE(source_mesh, nullptr);
  const AssetId shared_mesh = source_mesh->mesh;
  ASSERT_NE(shared_mesh, kInvalidAssetId);
  const MeshAsset *shared_asset = assets.mesh(shared_mesh);
  ASSERT_NE(shared_asset, nullptr);
  ASSERT_FALSE(shared_asset->geometry().vertices.empty());
  const QVec3 original_first_vertex =
      shared_asset->geometry().vertices.front().position;

  const EntityId observer =
      scene.create_entity("Shared History Observer", EntityKind::Mesh);
  scene.set_mesh(observer, MeshComponent{.mesh = shared_mesh});
  const std::size_t original_mesh_count = assets.mesh_count();

  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Vertex).ok);
  ASSERT_TRUE(service
                  .set_component_selection({
                      .modeling_object_id = box.object.modeling_object_id,
                      .mode = ModelingSelectionMode::Vertex,
                      .vertices = {1},
                      .edit = ModelingSelectionEdit::Add,
                      .active_kind = ModelingActiveSelectionKind::Vertex,
                      .active_vertex = 1,
                  })
                  .ok);
  ASSERT_TRUE(service.select_all().ok);
  EditorHistoryService history;
  const CommandResult moved = history.execute(
      std::make_unique<MoveSelectionHistoryCommand>(
          service, QVec3{2.0f, 0.0f, 0.0f}));
  ASSERT_TRUE(moved.ok) << moved.message;
  ASSERT_TRUE(moved.changed);

  ASSERT_NE(scene.mesh(box.object.entity), nullptr);
  const AssetId cloned_mesh = scene.mesh(box.object.entity)->mesh;
  ASSERT_NE(cloned_mesh, shared_mesh);
  EXPECT_EQ(scene.mesh(observer)->mesh, shared_mesh);
  ASSERT_EQ(assets.mesh_count(), original_mesh_count + 1U);
  ASSERT_NE(assets.mesh(cloned_mesh), nullptr);
  const QVec3 changed_first_vertex =
      assets.mesh(cloned_mesh)->geometry().vertices.front().position;
  EXPECT_NE(changed_first_vertex.x, original_first_vertex.x);

  CommandResult restored = history.undo();
  ASSERT_TRUE(restored.ok) << restored.message;
  ASSERT_NE(scene.mesh(box.object.entity), nullptr);
  ASSERT_NE(scene.mesh(observer), nullptr);
  EXPECT_EQ(scene.mesh(box.object.entity)->mesh, shared_mesh);
  EXPECT_EQ(scene.mesh(observer)->mesh, shared_mesh);
  EXPECT_EQ(assets.mesh_count(), original_mesh_count);
  EXPECT_EQ(assets.mesh(cloned_mesh), nullptr);
  ASSERT_NE(assets.mesh(shared_mesh), nullptr);
  expect_vec3_eq(
      assets.mesh(shared_mesh)->geometry().vertices.front().position,
      original_first_vertex);

  restored = history.redo();
  ASSERT_TRUE(restored.ok) << restored.message;
  ASSERT_NE(scene.mesh(box.object.entity), nullptr);
  ASSERT_NE(scene.mesh(observer), nullptr);
  EXPECT_EQ(scene.mesh(box.object.entity)->mesh, cloned_mesh);
  EXPECT_EQ(scene.mesh(observer)->mesh, shared_mesh);
  EXPECT_EQ(assets.mesh_count(), original_mesh_count + 1U);
  ASSERT_NE(assets.mesh(cloned_mesh), nullptr);
  expect_vec3_eq(
      assets.mesh(cloned_mesh)->geometry().vertices.front().position,
      changed_first_vertex);

  restored = history.undo();
  ASSERT_TRUE(restored.ok) << restored.message;
  EXPECT_EQ(scene.mesh(box.object.entity)->mesh, shared_mesh);
  EXPECT_EQ(assets.mesh_count(), original_mesh_count);
  restored = history.redo();
  ASSERT_TRUE(restored.ok) << restored.message;
  EXPECT_EQ(scene.mesh(box.object.entity)->mesh, cloned_mesh);
  EXPECT_EQ(assets.mesh_count(), original_mesh_count + 1U);
}

TEST(ModelingSessionServiceSplitTests,
     DeepNativeMementoHistoryRestoresOneWorkspaceExactly) {
  constexpr std::size_t kHistoryDepth = 128U;
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f},
                 .max = {1.0f, 1.0f, 1.0f}},
      .name = "Deep Memento Source",
  });
  ASSERT_TRUE(box.ok) << box.message;
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Vertex).ok);
  ASSERT_TRUE(service
                  .set_component_selection({
                      .modeling_object_id = box.object.modeling_object_id,
                      .mode = ModelingSelectionMode::Vertex,
                      .vertices = {1},
                      .edit = ModelingSelectionEdit::Replace,
                      .active_kind = ModelingActiveSelectionKind::Vertex,
                      .active_vertex = 1,
                  })
                  .ok);
  ASSERT_TRUE(service.select_all().ok);

  const MeshComponent *initial_binding = scene.mesh(box.object.entity);
  ASSERT_NE(initial_binding, nullptr);
  const AssetId initial_mesh_id = initial_binding->mesh;
  const MeshAsset *initial_asset = assets.mesh(initial_mesh_id);
  ASSERT_NE(initial_asset, nullptr);
  const MeshAsset initial_payload = clone_mesh_asset_payload(*initial_asset);
  const ModelingSelectionSummary initial_selection =
      service.selection_summary();
  const std::uint64_t initial_content_revision =
      initial_selection.content_revision;

  EditorHistoryService history;
  history.set_max_steps(kHistoryDepth);
  for (std::size_t index = 0; index < kHistoryDepth; ++index) {
    const CommandResult moved = history.execute(
        std::make_unique<MoveSelectionHistoryCommand>(
            service, QVec3{0.01F, 0.0F, 0.0F}));
    ASSERT_TRUE(moved.ok) << moved.message;
    ASSERT_TRUE(moved.changed);
  }
  ASSERT_EQ(history.undo_count(), kHistoryDepth);
  ASSERT_TRUE(history.can_undo());
  const MeshComponent *moved_binding = scene.mesh(box.object.entity);
  ASSERT_NE(moved_binding, nullptr);
  const AssetId moved_mesh_id = moved_binding->mesh;
  const MeshAsset *moved_asset = assets.mesh(moved_mesh_id);
  ASSERT_NE(moved_asset, nullptr);
  const MeshAsset moved_payload = clone_mesh_asset_payload(*moved_asset);
  EXPECT_FALSE(mesh_asset_payload_equal(initial_payload, moved_payload));

  for (std::size_t index = 0; index < kHistoryDepth; ++index) {
    const CommandResult restored = history.undo();
    ASSERT_TRUE(restored.ok) << restored.message;
    ASSERT_TRUE(restored.changed);
  }
  EXPECT_FALSE(history.can_undo());
  EXPECT_TRUE(history.can_redo());
  ASSERT_EQ(service.objects().size(), 1U);
  EXPECT_EQ(service.objects().front().modeling_object_id,
            box.object.modeling_object_id);
  EXPECT_EQ(service.objects().front().entity, box.object.entity);
  const MeshComponent *undone_binding = scene.mesh(box.object.entity);
  ASSERT_NE(undone_binding, nullptr);
  EXPECT_EQ(undone_binding->mesh, initial_mesh_id);
  const MeshAsset *undone_asset = assets.mesh(undone_binding->mesh);
  ASSERT_NE(undone_asset, nullptr);
  EXPECT_TRUE(mesh_asset_payload_equal(*undone_asset, initial_payload));
  const ModelingSelectionSummary undone_selection =
      service.selection_summary();
  EXPECT_EQ(undone_selection.mode, initial_selection.mode);
  EXPECT_EQ(undone_selection.active_kind, initial_selection.active_kind);
  EXPECT_EQ(undone_selection.active_object_id,
            initial_selection.active_object_id);
  EXPECT_EQ(undone_selection.selected_vertex_count,
            initial_selection.selected_vertex_count);
  EXPECT_GT(undone_selection.content_revision, initial_content_revision);

  const std::uint64_t undo_content_revision =
      undone_selection.content_revision;
  for (std::size_t index = 0; index < kHistoryDepth; ++index) {
    const CommandResult restored = history.redo();
    ASSERT_TRUE(restored.ok) << restored.message;
    ASSERT_TRUE(restored.changed);
  }
  EXPECT_TRUE(history.can_undo());
  EXPECT_FALSE(history.can_redo());
  ASSERT_EQ(service.objects().size(), 1U);
  EXPECT_EQ(service.objects().front().modeling_object_id,
            box.object.modeling_object_id);
  EXPECT_EQ(service.objects().front().entity, box.object.entity);
  const MeshComponent *redone_binding = scene.mesh(box.object.entity);
  ASSERT_NE(redone_binding, nullptr);
  EXPECT_EQ(redone_binding->mesh, moved_mesh_id);
  const MeshAsset *redone_asset = assets.mesh(redone_binding->mesh);
  ASSERT_NE(redone_asset, nullptr);
  EXPECT_TRUE(mesh_asset_payload_equal(*redone_asset, moved_payload));
  const ModelingSelectionSummary redone_selection =
      service.selection_summary();
  EXPECT_EQ(redone_selection.mode, initial_selection.mode);
  EXPECT_EQ(redone_selection.active_kind, initial_selection.active_kind);
  EXPECT_EQ(redone_selection.active_object_id,
            initial_selection.active_object_id);
  EXPECT_EQ(redone_selection.selected_vertex_count,
            initial_selection.selected_vertex_count);
  EXPECT_GT(redone_selection.content_revision, undo_content_revision);
}

TEST(ModelingSessionServiceSplitTests,
     SelectionProjectionServiceProjectsSceneSelectionIntoModelingOnly) {
  SceneDocument scene;
  const EntityId group = scene.create_entity("Group", EntityKind::Empty);
  const EntityId first = scene.create_entity("First", EntityKind::Mesh);
  const EntityId second = scene.create_entity("Second", EntityKind::Mesh);

  ModelingObjectRuntimeMap object_map;
  ASSERT_TRUE(object_map.bind(100, first));
  ASSERT_TRUE(object_map.bind(200, second));

  quader_editor_core::EditorSession session({
      quader_editor_core::PolygonObject{.id = 100},
      quader_editor_core::PolygonObject{.id = 200},
  });

  scene.set_selected(group, true);
  scene.set_selected(first, true);
  scene.set_selected(second, true);
  scene.set_active_selection(second);

  const SelectionProjectionService projection;
  const SelectionProjectionEditResult projected =
      projection.project_object_selection_from_scene(
          selection_snapshot_from_session(session), scene, object_map);
  EXPECT_TRUE(projected.changed);

  std::vector<int> selected_object_ids =
      projected.selection.selected_object_ids();
  std::sort(selected_object_ids.begin(), selected_object_ids.end());
  EXPECT_EQ(selected_object_ids, (std::vector<int>{100, 200}));
  EXPECT_EQ(projected.selection.active_object_id(), 200);

  std::vector<EntityId> selected_entities = scene.selected_entities();
  std::sort(selected_entities.begin(), selected_entities.end());
  EXPECT_EQ(selected_entities, (std::vector<EntityId>{group, first, second}));
  EXPECT_EQ(scene.primary_selection(), second);
}

TEST(ModelingSessionServiceSplitTests,
     SelectionProjectionServiceComponentBoundsWorkScalesLinearly) {
  const auto exercise = [](std::size_t vertex_count) {
    SceneDocument scene;
    AssetRegistry assets;
    const EntityId entity =
        scene.create_entity("Selected Mesh", EntityKind::Mesh);
    TransformComponent transform;
    transform.position = {10.0f, 20.0f, 30.0f};
    EXPECT_TRUE(scene.set_transform(entity, transform));

    ModelingObjectRuntimeMap object_map;
    EXPECT_TRUE(object_map.bind(100, entity));

    quader_poly::Document document;
    document.vertices.reserve(vertex_count);
    quader_poly::Selection selection;
    selection.vertices.reserve(vertex_count);
    for (std::size_t index = 0; index < vertex_count; ++index) {
      const quader_poly::ElementId id =
          static_cast<quader_poly::ElementId>(index + 1u);
      document.vertices.push_back({
          .id = id,
          .position = {static_cast<float>(index),
                       static_cast<float>(index % 7u),
                       -static_cast<float>(index)},
      });
      selection.vertices.push_back(id);
    }

    quader_editor_core::PolygonObject object;
    object.id = 100;
    object.document = std::move(document);
    quader_editor_core::EditorSession session({std::move(object)});
    session.set_component_selection(
        100, std::move(selection),
        quader_editor_core::SelectionMode::Vertex);

    const ModelingSelectionProjectionSnapshot selection_snapshot(
        std::vector<quader_editor_core::PolygonObject>(
            session.objects().begin(), session.objects().end()),
        session.selection_mode(), session.active_object_id(),
        session.state().content_revision, session.state().selection_revision);
    const SelectionProjectionService projection;
    SelectionBoundsWorkStats work;
    const std::optional<SceneBounds> bounds =
        projection.world_selection_bounds(selection_snapshot, scene, assets,
                                          object_map, &work);
    EXPECT_TRUE(bounds.has_value());
    if (bounds.has_value()) {
      EXPECT_FLOAT_EQ(bounds->min.x, 10.0f);
      EXPECT_FLOAT_EQ(bounds->max.x,
                      10.0f + static_cast<float>(vertex_count - 1u));
      EXPECT_FLOAT_EQ(bounds->max.z, 30.0f);
      EXPECT_FLOAT_EQ(bounds->min.z,
                      30.0f - static_cast<float>(vertex_count - 1u));
    }
    EXPECT_EQ(work.selected_object_visit_count, 1u);
    EXPECT_EQ(work.vertex_index_visit_count, vertex_count);
    EXPECT_EQ(work.selected_vertex_lookup_count, vertex_count);
    EXPECT_EQ(work.world_transform_evaluation_count, 1u);
    EXPECT_EQ(work.included_point_count, vertex_count);
    return work;
  };

  const SelectionBoundsWorkStats work_n = exercise(256u);
  const SelectionBoundsWorkStats work_2n = exercise(512u);
  EXPECT_EQ(work_2n.vertex_index_visit_count,
            work_n.vertex_index_visit_count * 2u);
  EXPECT_EQ(work_2n.selected_vertex_lookup_count,
            work_n.selected_vertex_lookup_count * 2u);
  EXPECT_EQ(work_2n.included_point_count,
            work_n.included_point_count * 2u);
  EXPECT_EQ(work_2n.world_transform_evaluation_count,
            work_n.world_transform_evaluation_count);
}

TEST(ModelingSessionServiceSplitTests,
     SceneOwnedSelectionRevisionIgnoresUnrelatedSceneMutations) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const EntityId group = scene.create_empty("Revision Group");
  ASSERT_NE(group, kInvalidEntityId);
  ASSERT_TRUE(scene.set_selected(group, true));
  ASSERT_TRUE(scene.set_active_selection(group));
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Group).ok);

  const ModelingSelectionSummary before = service.selection_summary();
  ASSERT_EQ(before.mode, ModelingSelectionMode::Group);
  ASSERT_TRUE(before.has_selection);
  const std::uint64_t scene_revision_before = scene.revision();

  TransformComponent moved;
  moved.position = {3.0f, -2.0f, 7.0f};
  ASSERT_TRUE(scene.set_transform(group, moved));
  ASSERT_TRUE(scene.set_locked(group, true));
  ASSERT_TRUE(scene.set_visible(group, false));
  ASSERT_GT(scene.revision(), scene_revision_before);

  const ModelingSelectionSummary after_content_edits =
      service.selection_summary();
  EXPECT_EQ(after_content_edits.selection_revision,
            before.selection_revision);
  EXPECT_EQ(after_content_edits.selected_object_count,
            before.selected_object_count);

  ASSERT_TRUE(scene.set_light(group, LightComponent{}));
  const ModelingSelectionSummary after_active_kind_edit =
      service.selection_summary();
  EXPECT_EQ(after_active_kind_edit.active_kind,
            ModelingActiveSelectionKind::Light);
  EXPECT_GT(after_active_kind_edit.selection_revision,
            after_content_edits.selection_revision);

  scene.clear_selection();
  const ModelingSelectionSummary after_selection_edit =
      service.selection_summary();
  EXPECT_GT(after_selection_edit.selection_revision,
            before.selection_revision);
  EXPECT_FALSE(after_selection_edit.has_selection);
}

TEST(ModelingSessionServiceSplitTests,
     ComponentBatchModeOnlyChangeAdvancesSelectionRevision) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);

  const ModelingCreateBoxResult box = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f},
                 .max = {1.0f, 1.0f, 1.0f}},
      .name = "Component Revision Box",
  });
  ASSERT_TRUE(box.ok) << box.message;
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Face).ok);
  const ModelingComponentSelectionRequest face_selection{
      .modeling_object_id = box.object.modeling_object_id,
      .mode = ModelingSelectionMode::Face,
      .faces = {1},
      .edit = ModelingSelectionEdit::Replace,
      .active_kind = ModelingActiveSelectionKind::Face,
      .active_face = 1,
  };
  ASSERT_TRUE(service.set_component_selection(face_selection).ok);
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Object).ok);
  ASSERT_EQ(service.selection_summary().mode, ModelingSelectionMode::Object);
  const std::uint64_t selection_revision_before =
      service.selection_revision();

  const ModelingCommandResult republished =
      service.set_component_selection(face_selection);
  ASSERT_TRUE(republished.ok) << republished.message;
  EXPECT_TRUE(republished.changed);
  const ModelingSelectionSummary after = service.selection_summary();
  EXPECT_EQ(after.mode, ModelingSelectionMode::Face);
  EXPECT_EQ(after.active_face_id, 1);
  EXPECT_EQ(after.selected_face_count, 1U);
  EXPECT_GT(service.selection_revision(), selection_revision_before);
}

TEST(ModelingSessionServiceSplitTests,
     SelectionProjectionServiceProjectsModelingSelectionIntoScene) {
  SceneDocument scene;
  const EntityId group = scene.create_entity("Group", EntityKind::Empty);
  const EntityId first = scene.create_entity("First", EntityKind::Mesh);
  const EntityId second = scene.create_entity("Second", EntityKind::Mesh);

  ModelingObjectRuntimeMap object_map;
  ASSERT_TRUE(object_map.bind(100, first));
  ASSERT_TRUE(object_map.bind(200, second));

  quader_editor_core::EditorSession session({
      quader_editor_core::PolygonObject{.id = 100},
      quader_editor_core::PolygonObject{.id = 200},
  });
  session.set_selected_object_ids({100, 200}, 200);
  scene.set_selected(group, true);

  const SelectionProjectionService projection;
  EXPECT_TRUE(projection.project_scene_selection_from_modeling(
      selection_snapshot_from_session(session), scene, object_map));

  std::vector<EntityId> selected_entities = scene.selected_entities();
  std::sort(selected_entities.begin(), selected_entities.end());
  EXPECT_EQ(selected_entities, (std::vector<EntityId>{first, second}));
  EXPECT_EQ(scene.primary_selection(), second);
}

TEST(ModelingSessionServiceSplitTests,
     SelectionProjectionServiceUsesOneActiveFallbackInBothDirections) {
  SceneDocument scene;
  const EntityId first = scene.create_entity("First", EntityKind::Mesh);
  const EntityId second = scene.create_entity("Second", EntityKind::Mesh);

  ModelingObjectRuntimeMap object_map;
  ASSERT_TRUE(object_map.bind(100, first));
  ASSERT_TRUE(object_map.bind(200, second));

  quader_editor_core::EditorSession session({
      quader_editor_core::PolygonObject{.id = 100},
      quader_editor_core::PolygonObject{.id = 200},
  });

  const SelectionProjectionService projection;
  const std::array<EntityId, 2> scene_selection{first, second};
  RuntimeObjectSelectionProjectionResult result =
      projection.project_object_selection_for_scene_entities(
          selection_snapshot_from_session(session), scene_selection,
          kInvalidEntityId, object_map);
  EXPECT_TRUE(result.ok);
  EXPECT_TRUE(result.changed);
  EXPECT_EQ(result.selection.active_object_id(), 200);

  session.set_selected_object_ids({100, 200}, -1);
  EXPECT_TRUE(projection.project_scene_selection_from_modeling(
      selection_snapshot_from_session(session), scene, object_map));
  EXPECT_EQ(scene.primary_selection(), second);
}

TEST(ModelingSessionServiceSplitTests,
     SelectionProjectionServiceProjectsExplicitObjectSelection) {
  SceneDocument scene;
  const EntityId group = scene.create_entity("Group", EntityKind::Empty);
  const EntityId first = scene.create_entity("First", EntityKind::Mesh);
  const EntityId second = scene.create_entity("Second", EntityKind::Mesh);

  ModelingObjectRuntimeMap object_map;
  ASSERT_TRUE(object_map.bind(100, first));
  ASSERT_TRUE(object_map.bind(200, second));

  quader_editor_core::EditorSession session({
      quader_editor_core::PolygonObject{.id = 100},
      quader_editor_core::PolygonObject{.id = 200},
  });

  const SelectionProjectionService projection;
  const std::array<EntityId, 2> selected{first, second};
  ModelingSelectionProjectionSnapshot selection =
      selection_snapshot_from_session(session);
  RuntimeObjectSelectionProjectionResult result =
      projection.project_object_selection_for_scene_entities(
          selection, selected, kInvalidEntityId, object_map);
  EXPECT_TRUE(result.ok);
  EXPECT_TRUE(result.changed);
  EXPECT_TRUE(result.has_modeling_objects);
  selection = result.selection;

  std::vector<int> selected_object_ids = selection.selected_object_ids();
  std::sort(selected_object_ids.begin(), selected_object_ids.end());
  EXPECT_EQ(selected_object_ids, (std::vector<int>{100, 200}));
  EXPECT_EQ(selection.active_object_id(), 200);

  result = projection.project_object_selection_for_scene_entities(
      selection, selected, second, object_map);
  EXPECT_TRUE(result.ok);
  EXPECT_FALSE(result.changed);
  EXPECT_TRUE(result.has_modeling_objects);
  selection = result.selection;

  result = projection.project_object_selection_for_scene_entities(
      selection, selected, first, object_map);
  EXPECT_TRUE(result.ok);
  EXPECT_TRUE(result.changed);
  selection = result.selection;
  EXPECT_EQ(selection.active_object_id(), 100);

  const std::array<EntityId, 1> non_modeling{group};
  result = projection.project_object_selection_for_scene_entities(
      selection, non_modeling, group, object_map);
  EXPECT_TRUE(result.ok);
  EXPECT_TRUE(result.changed);
  EXPECT_FALSE(result.has_modeling_objects);
  selection = result.selection;
  EXPECT_TRUE(selection.selected_object_ids().empty());
  EXPECT_EQ(selection.active_object_id(), -1);
}

TEST(ModelingSessionServiceSplitTests,
     SelectionProjectionServicePromotesToolObjectSelection) {
  SceneDocument scene;
  const EntityId group = scene.create_entity("Group", EntityKind::Empty);
  const EntityId first = scene.create_entity("First", EntityKind::Mesh);
  const EntityId second = scene.create_entity("Second", EntityKind::Mesh);

  ModelingObjectRuntimeMap object_map;
  ASSERT_TRUE(object_map.bind(100, first));
  ASSERT_TRUE(object_map.bind(200, second));

  quader_editor_core::EditorSession session({
      quader_editor_core::PolygonObject{.id = 100},
      quader_editor_core::PolygonObject{.id = 200},
  });

  scene.set_selected(group, true);
  scene.set_selected(first, true);
  scene.set_selected(second, true);
  scene.set_active_selection(second);

  const SelectionProjectionService projection;
  const SelectionProjectionEditResult promoted =
      projection.promote_object_selection_for_tool(
          selection_snapshot_from_session(session), scene, object_map);
  EXPECT_TRUE(promoted.changed);

  std::vector<int> selected_object_ids =
      promoted.selection.selected_object_ids();
  std::sort(selected_object_ids.begin(), selected_object_ids.end());
  EXPECT_EQ(selected_object_ids, (std::vector<int>{100, 200}));
  EXPECT_EQ(promoted.selection.active_object_id(), 200);
}

TEST(ModelingSessionServiceSplitTests,
     SelectionProjectionServicePromotionClearsRuntimeForSceneOnlySelection) {
  SceneDocument scene;
  const EntityId group = scene.create_entity("Group", EntityKind::Empty);
  const EntityId first = scene.create_entity("First", EntityKind::Mesh);
  const EntityId second = scene.create_entity("Second", EntityKind::Mesh);

  ModelingObjectRuntimeMap object_map;
  ASSERT_TRUE(object_map.bind(100, first));
  ASSERT_TRUE(object_map.bind(200, second));

  quader_editor_core::EditorSession session({
      quader_editor_core::PolygonObject{.id = 100},
      quader_editor_core::PolygonObject{.id = 200},
  });
  session.set_selected_object_ids({100, 200}, -1);
  scene.set_selected(group, true);
  scene.set_active_selection(group);

  const SelectionProjectionService projection;
  const SelectionProjectionEditResult promoted =
      projection.promote_object_selection_for_tool(
          selection_snapshot_from_session(session), scene, object_map);
  EXPECT_TRUE(promoted.changed);

  EXPECT_TRUE(promoted.selection.selected_object_ids().empty());
  EXPECT_EQ(promoted.selection.active_object_id(), -1);
  EXPECT_EQ(promoted.selection.selection_mode(),
            quader_editor_core::SelectionMode::Object);
}

TEST(ModelingSessionServiceSplitTests,
     SceneSelectionApplicationModesAreExplicit) {
  SceneDocument scene;
  const EntityId first = scene.create_entity("First", EntityKind::Mesh);
  const EntityId second = scene.create_entity("Second", EntityKind::Mesh);
  const std::array<EntityId, 2> selected{first, second};
  const SceneSelectionState state =
      make_scene_selection(scene, selected, second);

  EXPECT_TRUE(apply_scene_selection(
      scene, state, SceneSelectionApplicationMode::UndoableCommand));
  EXPECT_EQ(scene.selected_entities(), (std::vector<EntityId>{first, second}));
  EXPECT_EQ(scene.primary_selection(), second);

  const std::uint64_t revision_after_first_apply = scene.selection_revision();
  EXPECT_FALSE(apply_scene_selection(
      scene, state, SceneSelectionApplicationMode::RuntimeSync));
  EXPECT_EQ(scene.selection_revision(), revision_after_first_apply);

  EXPECT_TRUE(apply_scene_selection(
      scene, state, SceneSelectionApplicationMode::UndoableCommand));
  EXPECT_GT(scene.selection_revision(), revision_after_first_apply);
}

TEST(ModelingSessionServiceSplitTests,
     EachWorkspaceOwnsOneIndependentSdkProjectionSession) {
  SceneDocument first_scene;
  SceneDocument second_scene;
  AssetRegistry first_assets;
  AssetRegistry second_assets;
  ModelingSessionService first(first_scene, first_assets);
  ModelingSessionService second(second_scene, second_assets);

  EXPECT_NE(first.modeling_sdk_session_identity_for_tests(),
            second.modeling_sdk_session_identity_for_tests());
  EXPECT_EQ(first.modeling_sdk_object_count_for_tests(), 0u);
  EXPECT_EQ(second.modeling_sdk_object_count_for_tests(), 0u);

  const ModelingCreateBoxResult created = first.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f},
                 .max = {1.0f, 1.0f, 1.0f}},
      .name = "Workspace SDK Box",
  });
  ASSERT_TRUE(created.ok) << created.message;
  EXPECT_EQ(first.modeling_sdk_object_count_for_tests(), 1u);
  EXPECT_EQ(second.modeling_sdk_object_count_for_tests(), 0u);
  const ModelingObjectComponent *component =
      first_scene.modeling_object(created.object.entity);
  ASSERT_NE(component, nullptr);
  EXPECT_TRUE(component->sdk_owned);
  EXPECT_TRUE(second_scene.entities().empty());
}

TEST(ModelingSessionServiceSplitTests,
     SdkBoxHistoryRestoresTheSameHostMappingAndOwnership) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);
  EditorHistoryService history;
  const CommandResult created = history.execute(
      std::make_unique<CreateBoxFromBoundsHistoryCommand>(
          service,
          ModelingCreateBoxRequest{
              .bounds = {.min = {-1.0f, -1.0f, -1.0f},
                         .max = {1.0f, 1.0f, 1.0f}},
              .name = "SDK History Box",
          }));
  ASSERT_TRUE(created.ok) << created.message;
  ASSERT_TRUE(created.changed);
  ASSERT_EQ(service.objects().size(), 1u);
  const ModelingObjectRecord original = service.objects().front();
  ASSERT_NE(scene.modeling_object(original.entity), nullptr);
  EXPECT_TRUE(scene.modeling_object(original.entity)->sdk_owned);
  EXPECT_EQ(service.modeling_sdk_object_count_for_tests(), 1u);

  const CommandResult undone = history.undo();
  ASSERT_TRUE(undone.ok) << undone.message;
  EXPECT_TRUE(service.objects().empty());
  EXPECT_EQ(service.modeling_sdk_object_count_for_tests(), 0u);
  EXPECT_FALSE(scene.exists(original.entity));

  const CommandResult redone = history.redo();
  ASSERT_TRUE(redone.ok) << redone.message;
  ASSERT_EQ(service.objects().size(), 1u);
  EXPECT_EQ(service.objects().front().modeling_object_id,
            original.modeling_object_id);
  EXPECT_EQ(service.objects().front().entity, original.entity);
  ASSERT_NE(scene.modeling_object(original.entity), nullptr);
  EXPECT_TRUE(scene.modeling_object(original.entity)->sdk_owned);
  EXPECT_EQ(service.modeling_sdk_object_count_for_tests(), 1u);
}

TEST(ModelingSessionServiceSplitTests,
     SdkBoxProjectionFailureBoundariesRestoreHostStateExactly) {
  constexpr std::array<ModelingSdkProjectionFailurePoint, 6> kFailures = {
      ModelingSdkProjectionFailurePoint::AfterSdkMutation,
      ModelingSdkProjectionFailurePoint::AfterAssetInstall,
      ModelingSdkProjectionFailurePoint::AfterEntityCreate,
      ModelingSdkProjectionFailurePoint::AfterSceneBinding,
      ModelingSdkProjectionFailurePoint::AfterSelection,
      ModelingSdkProjectionFailurePoint::BeforeCommit,
  };

  for (const ModelingSdkProjectionFailurePoint failure_point : kFailures) {
    SCOPED_TRACE(static_cast<int>(failure_point));
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);
    const EntityId sentinel =
        scene.create_entity("Projection Sentinel", EntityKind::Empty);
    ASSERT_NE(sentinel, kInvalidEntityId);
    ASSERT_TRUE(scene.set_selected(sentinel, true));

    const std::vector<EntityId> entities_before = scene.entities();
    const std::vector<EntityId> selected_before = scene.selected_entities();
    const std::vector<AssetId> meshes_before = assets.mesh_ids();
    const std::uint64_t scene_revision_before = scene.revision();
    const std::uint64_t scene_selection_revision_before =
        scene.selection_revision();
    const std::uint64_t asset_revision_before = assets.revision();
    const std::uint64_t content_revision_before = service.content_revision();
    const std::uint64_t selection_revision_before =
        service.selection_revision();

    service.set_modeling_sdk_projection_failure_for_tests(failure_point);
    const ModelingCreateBoxResult failed = service.create_box_from_bounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f},
                   .max = {1.0f, 1.0f, 1.0f}},
        .name = "Must Roll Back",
    });
    EXPECT_FALSE(failed.ok);
    EXPECT_FALSE(failed.changed);
    EXPECT_EQ(service.modeling_sdk_object_count_for_tests(), 0u);
    EXPECT_EQ(scene.entities(), entities_before);
    EXPECT_EQ(scene.selected_entities(), selected_before);
    EXPECT_EQ(scene.primary_selection(), sentinel);
    EXPECT_EQ(scene.revision(), scene_revision_before);
    EXPECT_EQ(scene.selection_revision(), scene_selection_revision_before);
    EXPECT_EQ(assets.mesh_ids(), meshes_before);
    EXPECT_EQ(assets.revision(), asset_revision_before);
    EXPECT_EQ(service.content_revision(), content_revision_before);
    EXPECT_EQ(service.selection_revision(), selection_revision_before);

    service.set_modeling_sdk_projection_failure_for_tests(
        ModelingSdkProjectionFailurePoint::None);
    const ModelingCreateBoxResult retried = service.create_box_from_bounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f},
                   .max = {1.0f, 1.0f, 1.0f}},
        .name = "Retry Succeeds",
    });
    ASSERT_TRUE(retried.ok) << retried.message;
    EXPECT_EQ(service.modeling_sdk_object_count_for_tests(), 1u);
    EXPECT_EQ(retried.object.modeling_object_id, 1);
  }
}

const QuaderToolSettingControl *find_bevel_setting_control(
    const QuaderToolSettingsTarget &target, std::string_view id) {
  const auto found = std::find_if(
      target.controls.begin(), target.controls.end(),
      [id](const QuaderToolSettingControl &control) { return control.id == id; });
  return found == target.controls.end() ? nullptr : &(*found);
}

TEST(ModelingSessionServiceSplitTests,
     EdgeBevelPublishesUsefulWidthAndRetainsTheKnownLimitWithoutMeshWork) {
  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto source = editor.modeling().create_box(
      {.min = {-0.25f, -0.125f, -0.25f},
       .max = {0.25f, 2.125f, 0.25f}}, "Narrow Bevel Source");
  ASSERT_TRUE(source.ok()) << source.error_message();
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Edge).ok());
  ASSERT_TRUE(editor.selection().select_edge(source.value(), 2, 3).ok());
  ASSERT_TRUE(editor.commands().execute(
      editor.commands().command_id_for_modeling_action("bevel_edges")).ok());
  const auto descriptor = [&editor](std::string_view id) {
    const auto target = editor.tools().active_settings();
    const auto *control = find_bevel_setting_control(target, id);
    EXPECT_NE(control, nullptr);
    return control != nullptr ? *control : quader::QuaderToolSettingControl{};
  };
  constexpr std::string_view kWidth = "operation.edge_bevel.width";
  const auto initial = descriptor(kWidth);
  ASSERT_LT(initial.max_value, 1.0);
  EXPECT_NEAR(initial.number_value, 0.4995, 1.0e-7);
  EXPECT_DOUBLE_EQ(initial.number_value, initial.max_value);
  EXPECT_EQ(initial.number_decimals, std::numeric_limits<double>::max_digits10);
  EXPECT_DOUBLE_EQ(session.modeling().interaction_settings().edge_bevel_width,
                   initial.number_value);
  EXPECT_TRUE(descriptor("operation.edge_bevel.profile").enabled);

  const auto revision = editor.tools().active_settings_revision();
  const auto assets_revision = editor.revisions().asset_revision;
  const auto before = session.modeling().modeling_sdk_projection_stats_for_tests();
  for (int repetition = 0; repetition < 8; ++repetition) {
    const auto unchanged = editor.tools().set_setting("edge_bevel", kWidth, 900.0);
    ASSERT_TRUE(unchanged.ok()) << unchanged.error_message();
    EXPECT_FALSE(unchanged.changed());
    EXPECT_DOUBLE_EQ(descriptor(kWidth).number_value, initial.number_value);
    EXPECT_EQ(editor.tools().active_settings_revision(), revision);
  }
  const auto after = session.modeling().modeling_sdk_projection_stats_for_tests();
  EXPECT_EQ(after.mesh_compile_count, before.mesh_compile_count);
  EXPECT_EQ(after.preview_publication_count, before.preview_publication_count);
  EXPECT_EQ(after.asset_publication_count, before.asset_publication_count);
  EXPECT_EQ(editor.revisions().asset_revision, assets_revision);

  ASSERT_TRUE(editor.tools().set_setting("edge_bevel", kWidth, 0.4).ok());
  EXPECT_DOUBLE_EQ(descriptor(kWidth).number_value, 0.4);
  EXPECT_DOUBLE_EQ(descriptor(kWidth).max_value, initial.max_value);
  ASSERT_TRUE(editor.tools().reset_active_settings().ok());
  EXPECT_DOUBLE_EQ(descriptor(kWidth).number_value, initial.max_value);

  ASSERT_TRUE(editor.tools().set_setting("edge_bevel", kWidth, 0.1).ok());
  ASSERT_TRUE(editor.tools().set_setting(
      "edge_bevel", "operation.edge_bevel.segments", 3.0).ok());
  EXPECT_TRUE(descriptor("operation.edge_bevel.profile").enabled);
  EXPECT_DOUBLE_EQ(descriptor(kWidth).number_value, 0.1);
  EXPECT_DOUBLE_EQ(descriptor(kWidth).max_value, 512.0);
  ASSERT_TRUE(editor.tools().set_setting("edge_bevel", kWidth, 1.0).ok());
  EXPECT_LT(descriptor(kWidth).max_value, 1.0);
  EXPECT_DOUBLE_EQ(descriptor(kWidth).number_value, descriptor(kWidth).max_value);

  ASSERT_TRUE(editor.tools().set_setting("edge_bevel", kWidth, 0.1).ok());
  ASSERT_TRUE(editor.tools().set_setting(
      "edge_bevel", "operation.edge_bevel.profile", 0.6).ok());
  EXPECT_DOUBLE_EQ(descriptor(kWidth).max_value, 512.0);
  EXPECT_DOUBLE_EQ(descriptor(kWidth).number_value, 0.1);
  ASSERT_TRUE(editor.tools().reset_active_settings().ok());
  EXPECT_DOUBLE_EQ(descriptor(kWidth).max_value, initial.max_value);
  EXPECT_DOUBLE_EQ(descriptor(kWidth).number_value, initial.max_value);
  EXPECT_TRUE(descriptor("operation.edge_bevel.profile").enabled);
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
}

TEST(ModelingSessionServiceSplitTests,
     EdgeBevelFailedPublicationDoesNotAcceptSaturationFeedback) {
  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto source = editor.modeling().create_box(
      {.min = {-0.25f, -0.125f, -0.25f},
       .max = {0.25f, 2.125f, 0.25f}}, "Failed Bevel Source");
  ASSERT_TRUE(source.ok());
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Edge).ok());
  ASSERT_TRUE(editor.selection().select_edge(source.value(), 2, 3).ok());
  ASSERT_TRUE(editor.tools().set_setting(
      "edge_bevel", "operation.edge_bevel.width", 0.12).ok());
  const auto settings_before = session.modeling().interaction_settings();
  const auto revision_before = editor.tools().active_settings_revision();
  session.modeling().set_modeling_sdk_projection_failure_for_tests(
      quader::ModelingSdkProjectionFailurePoint::PreviewAssetReplace);
  const auto failed_begin = editor.commands().execute(
      editor.commands().command_id_for_modeling_action("bevel_edges"));
  EXPECT_FALSE(failed_begin.ok());
  EXPECT_EQ(session.modeling().interaction_settings(), settings_before);
  EXPECT_EQ(editor.tools().active_settings_revision(), revision_before);
  EXPECT_FALSE(session.modeling().operation_preview_active());

  session.modeling().set_modeling_sdk_projection_failure_for_tests(
      quader::ModelingSdkProjectionFailurePoint::None);
  ASSERT_TRUE(editor.commands().execute(
      editor.commands().command_id_for_modeling_action("bevel_edges")).ok());
  ASSERT_TRUE(editor.tools().set_setting(
      "edge_bevel", "operation.edge_bevel.width", 0.1).ok());
  ASSERT_TRUE(editor.tools().set_setting(
      "edge_bevel", "operation.edge_bevel.profile", 0.6).ok());
  const auto previous = session.modeling().interaction_settings();
  session.modeling().set_modeling_sdk_projection_failure_for_tests(
      quader::ModelingSdkProjectionFailurePoint::PreviewAssetReplace);
  const auto failed_update = editor.tools().set_setting(
      "edge_bevel", "operation.edge_bevel.width", 1.0);
  EXPECT_FALSE(failed_update.ok());
  EXPECT_EQ(session.modeling().interaction_settings(), previous);
  EXPECT_FALSE(session.modeling().operation_preview_active());
  session.modeling().set_modeling_sdk_projection_failure_for_tests(
      quader::ModelingSdkProjectionFailurePoint::None);
  ASSERT_TRUE(editor.commands().execute(
      editor.commands().command_id_for_modeling_action("bevel_edges")).ok());
  const auto retry = editor.tools().active_settings();
  const auto *width = find_bevel_setting_control(retry,
                                               "operation.edge_bevel.width");
  ASSERT_NE(width, nullptr);
  EXPECT_DOUBLE_EQ(width->max_value, 512.0);
  EXPECT_DOUBLE_EQ(width->number_value, 0.1);
  const auto *profile = find_bevel_setting_control(
      retry, "operation.edge_bevel.profile");
  ASSERT_NE(profile, nullptr);
  EXPECT_DOUBLE_EQ(profile->number_value, 0.6);
  EXPECT_EQ(session.modeling().interaction_settings(), previous);
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  EXPECT_EQ(session.modeling().interaction_settings(), previous);
  ASSERT_TRUE(editor.commands().execute(
      editor.commands().command_id_for_modeling_action("bevel_edges")).ok());
  EXPECT_EQ(session.modeling().interaction_settings(), previous);
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
}

TEST(ModelingSessionServiceSplitTests,
     BorrowedInteractionOptionsOutliveScenesAndLeavePreviewLimitsLocal) {
  ModelingInteractionSettingsService remembered;
  ModelingInteractionSettings expected = remembered.settings();
  expected.bridge_steps = 7;
  expected.edge_bevel_width = 0.1;
  expected.edge_bevel_profile = 0.6;
  expected.edge_bevel_segments = 3;
  expected.thicken_thickness = 0.375;
  expected.thicken_from_center = true;
  expected.slice_quad_x_slices = 3;
  expected.slice_quad_y_slices = 4;
  expected.merge_by_distance_tolerance = 0.0625;
  expected.half_cut_axis = ModelingHalfCutAxis::Z;
  expected.half_cut_separate_parts = true;
  expected.half_cut_fill_parts = false;
  expected.move_auto_merge_vertices = false;
  expected.lasso_select_partial_intersection = true;
  {
    SceneDocument first_scene;
    AssetRegistry first_assets;
    ModelingSessionService first(first_scene, first_assets, {}, &remembered);
    SceneDocument second_scene;
    AssetRegistry second_assets;
    ModelingSessionService second(second_scene, second_assets, {}, &remembered);
    const auto second_revision = second.tool_settings_revision();
    ASSERT_TRUE(first.set_interaction_settings(expected).ok);
    EXPECT_EQ(second.interaction_settings(), expected);
    EXPECT_GT(second.tool_settings_revision(), second_revision);
    const auto stable_revision = second.tool_settings_revision();
    EXPECT_EQ(second.tool_settings_revision(), stable_revision);
    first.reset_from_scene();
    EXPECT_EQ(first.interaction_settings(), expected);

    const auto created = first.create_box_from_bounds({
        .bounds = {.min = {-0.25f, -0.125f, -0.25f},
                   .max = {0.25f, 2.125f, 0.25f}},
        .name = "Remembered Bevel Source",
    });
    ASSERT_TRUE(created.ok) << created.message;
    ASSERT_TRUE(first.activate_viewport_tool(ModelingViewportTool::Cut).ok);
    expected.cut_mode = ModelingCutMode::DiscardRight;
    ASSERT_TRUE(first.set_interaction_settings(expected).ok);
    EXPECT_EQ(second.interaction_settings(), expected);
    ASSERT_TRUE(first.activate_viewport_tool(ModelingViewportTool::Select).ok);
    ASSERT_TRUE(first.set_selection_mode(ModelingSelectionMode::Edge).ok);
    ASSERT_TRUE(first.set_component_selection({
        .modeling_object_id = created.object.modeling_object_id,
        .mode = ModelingSelectionMode::Edge,
        .edges = {{2, 3}},
        .active_kind = ModelingActiveSelectionKind::Edge,
        .active_edge = {2, 3},
    }).ok);
    ASSERT_TRUE(first.execute_context_action("bevel_edges").ok);
    EXPECT_EQ(first.interaction_settings(), expected);
    auto candidate = expected;
    candidate.edge_bevel_width = 1.0;
    const auto revision_before_failure = remembered.revision();
    first.set_modeling_sdk_projection_failure_for_tests(
        ModelingSdkProjectionFailurePoint::PreviewAssetReplace);
    EXPECT_FALSE(first.set_interaction_settings(candidate).ok);
    EXPECT_EQ(remembered.settings(), expected);
    EXPECT_EQ(remembered.revision(), revision_before_failure);
    EXPECT_EQ(second.interaction_settings(), expected);
    first.set_modeling_sdk_projection_failure_for_tests(
        ModelingSdkProjectionFailurePoint::None);
    ASSERT_TRUE(first.execute_context_action("bevel_edges").ok);
    EXPECT_EQ(first.interaction_settings(), expected);
    ASSERT_TRUE(first.set_interaction_settings(candidate).ok);
    EXPECT_TRUE(first.tool_settings_target().edge_bevel_maximum_width.has_value());
    EXPECT_FALSE(second.tool_settings_target().edge_bevel_maximum_width.has_value());
    expected = first.interaction_settings();
    EXPECT_EQ(second.interaction_settings(), expected);
    ASSERT_TRUE(first.cancel_active_operation_preview().ok);
    EXPECT_EQ(remembered.settings(), expected);
  }

  SceneDocument replacement_scene;
  AssetRegistry replacement_assets;
  ModelingSessionService replacement(replacement_scene, replacement_assets, {},
                                     &remembered);
  EXPECT_EQ(replacement.interaction_settings(), expected);
  EXPECT_FALSE(replacement.tool_settings_target().edge_bevel_maximum_width.has_value());
  SceneDocument independent_scene;
  AssetRegistry independent_assets;
  ModelingSessionService independent(independent_scene, independent_assets);
  ModelingInteractionSettingsService fresh_application;
  EXPECT_EQ(independent.interaction_settings(), fresh_application.settings());
  EXPECT_NE(independent.interaction_settings(), expected);
}

TEST(ModelingSessionServiceSplitTests,
     SdkSelectionTranslationPreviewAndCountersStayEventDriven) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);
  const ModelingCreateBoxResult created = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f},
                 .max = {1.0f, 1.0f, 1.0f}},
      .name = "SDK Interaction Box",
  });
  ASSERT_TRUE(created.ok) << created.message;
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Face).ok);
  ASSERT_TRUE(service
                  .set_component_selection({
                      .modeling_object_id = created.object.modeling_object_id,
                      .mode = ModelingSelectionMode::Face,
                      .faces = {1},
                      .edit = ModelingSelectionEdit::Replace,
                      .active_kind = ModelingActiveSelectionKind::Face,
                      .active_face = 1,
                  })
                  .ok);
  EXPECT_EQ(service.selection_summary().selected_face_count, 1u);
  const ModelingSdkProjectionStats after_initial_summary =
      service.modeling_sdk_projection_stats_for_tests();
  for (int index = 0; index < 16; ++index) {
    EXPECT_EQ(service.selection_summary().selected_face_count, 1u);
  }
  EXPECT_EQ(service.modeling_sdk_projection_stats_for_tests()
                .selection_summary_build_count,
            after_initial_summary.selection_summary_build_count);

  const ModelingSdkProjectionStats before_move =
      service.modeling_sdk_projection_stats_for_tests();
  ASSERT_TRUE(service.move_selection({1.0f, 0.0f, 0.0f}).ok);
  const ModelingSdkProjectionStats after_move =
      service.modeling_sdk_projection_stats_for_tests();
  EXPECT_EQ(after_move.mesh_compile_count,
            before_move.mesh_compile_count + 1u);
  EXPECT_EQ(after_move.asset_publication_count,
            before_move.asset_publication_count + 1u);
  EXPECT_EQ(after_move.scene_publication_count,
            before_move.scene_publication_count + 1u);

  const MeshComponent *binding = scene.mesh(created.object.entity);
  ASSERT_NE(binding, nullptr);
  const MeshAsset *committed = assets.mesh(binding->mesh);
  ASSERT_NE(committed, nullptr);
  const MeshGeometryPayload committed_geometry = committed->geometry();
  const std::uint64_t mesh_revision_before_preview =
      assets.mesh_revision(binding->mesh).value;
  const std::uint64_t asset_revision_before_preview = assets.revision();
  const std::uint64_t scene_revision_before_preview = scene.revision();

  ASSERT_TRUE(service.begin_transform_preview(false).ok);
  ASSERT_TRUE(service.update_transform_translate_preview({0.0f, 2.0f, 0.0f})
                  .ok);
  EXPECT_TRUE(service.can_cancel_active_preview_for_undo());
  EXPECT_TRUE(service.transient_transform_preview_active());
  const ModelingSdkProjectionStats after_preview =
      service.modeling_sdk_projection_stats_for_tests();
  EXPECT_EQ(after_preview.preview_publication_count,
            after_move.preview_publication_count);
  EXPECT_EQ(after_preview.mesh_compile_count, after_move.mesh_compile_count);
  EXPECT_EQ(after_preview.asset_publication_count,
            after_move.asset_publication_count);
  EXPECT_EQ(after_preview.scene_publication_count,
            after_move.scene_publication_count);
  EXPECT_EQ(assets.mesh_revision(binding->mesh).value,
            mesh_revision_before_preview);
  EXPECT_EQ(assets.revision(), asset_revision_before_preview);
  EXPECT_EQ(scene.revision(), scene_revision_before_preview);
  const std::vector<ModelingToolPreviewMesh> preview_meshes =
      service.tool_preview_meshes();
  ASSERT_EQ(preview_meshes.size(), 1u);
  EXPECT_TRUE(preview_meshes.front().dynamic_positions);
  EXPECT_TRUE(preview_meshes.front().suppress_source);
  ASSERT_NE(preview_meshes.front().dynamic_position_payload, nullptr);
  EXPECT_EQ(preview_meshes.front().dynamic_position_payload->size(),
            preview_meshes.front().mesh.geometry().vertices.size());
  ASSERT_TRUE(service.cancel_transform_preview().ok);
  EXPECT_FALSE(service.can_cancel_active_preview_for_undo());
  EXPECT_FALSE(service.transient_transform_preview_active());
  EXPECT_TRUE(service.tool_preview_meshes().empty());
  ASSERT_NE(assets.mesh(scene.mesh(created.object.entity)->mesh), nullptr);
  EXPECT_EQ(assets.mesh(scene.mesh(created.object.entity)->mesh)
                ->geometry()
                .content_id(),
            committed_geometry.content_id());

  (void)service.selection_summary();
  const ModelingSdkProjectionStats before_queries =
      service.modeling_sdk_projection_stats_for_tests();
  for (int index = 0; index < 16; ++index) {
    (void)service.selection_summary();
    (void)service.export_selection();
    (void)service.objects();
  }
  const ModelingSdkProjectionStats after_queries =
      service.modeling_sdk_projection_stats_for_tests();
  EXPECT_EQ(after_queries.mesh_compile_count,
            before_queries.mesh_compile_count);
  EXPECT_EQ(after_queries.asset_publication_count,
            before_queries.asset_publication_count);
  EXPECT_EQ(after_queries.scene_publication_count,
            before_queries.scene_publication_count);
  EXPECT_EQ(after_queries.preview_publication_count,
            before_queries.preview_publication_count);
  EXPECT_EQ(after_queries.selection_summary_build_count,
            before_queries.selection_summary_build_count);
}

TEST(ModelingSessionServiceSplitTests,
     SdkPreviewPublicationFailuresRestoreOriginalAndEndPreview) {
  constexpr std::array<ModelingSdkProjectionFailurePoint, 3> kFailures = {
      ModelingSdkProjectionFailurePoint::AfterSdkMutation,
      ModelingSdkProjectionFailurePoint::PreviewAssetReplace,
      ModelingSdkProjectionFailurePoint::PreviewSceneNotify,
  };
  for (const ModelingSdkProjectionFailurePoint failure_point : kFailures) {
    SCOPED_TRACE(static_cast<int>(failure_point));
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);
    const ModelingCreateBoxResult created = service.create_box_from_bounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f},
                   .max = {1.0f, 1.0f, 1.0f}},
        .name = "Preview Failure Box",
    });
    ASSERT_TRUE(created.ok) << created.message;
    ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Face).ok);
    ASSERT_TRUE(service
                    .set_component_selection({
                        .modeling_object_id = created.object.modeling_object_id,
                        .mode = ModelingSelectionMode::Face,
                        .faces = {1},
                        .edit = ModelingSelectionEdit::Replace,
                        .active_kind = ModelingActiveSelectionKind::Face,
                        .active_face = 1,
                    })
                    .ok);
    ASSERT_TRUE(service.begin_transform_preview(false).ok);
    const AssetId mesh = scene.mesh(created.object.entity)->mesh;
    const MeshGeometryContentId content_before =
        assets.mesh(mesh)->geometry().content_id();
    const std::uint64_t asset_revision_before = assets.revision();
    const std::uint64_t scene_revision_before = scene.revision();

    service.set_modeling_sdk_projection_failure_for_tests(failure_point);
    const ModelingCommandResult failed =
        service.update_transform_extrude_preview({0.0f, 0.0f, 1.0f}, 1.0f);
    EXPECT_FALSE(failed.ok);
    EXPECT_FALSE(failed.changed);
    EXPECT_FALSE(service.can_cancel_active_preview_for_undo());
    EXPECT_EQ(assets.revision(), asset_revision_before);
    EXPECT_EQ(scene.revision(), scene_revision_before);
    EXPECT_EQ(assets.mesh(mesh)->geometry().content_id(), content_before);

    service.set_modeling_sdk_projection_failure_for_tests(
        ModelingSdkProjectionFailurePoint::None);
    ASSERT_TRUE(service.begin_transform_preview(false).ok);
    ASSERT_TRUE(
        service.update_transform_extrude_preview({0.0f, 0.0f, 1.0f}, 1.0f)
            .ok);
    ASSERT_TRUE(service.cancel_transform_preview().ok);
    EXPECT_FALSE(service.can_cancel_active_preview_for_undo());
    EXPECT_EQ(assets.mesh(mesh)->geometry().content_id(), content_before);
  }
}

TEST(ModelingSessionServiceSplitTests,
     SdkSharedMeshPublicationFailureRestoresTheOriginalBinding) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);
  const ModelingCreateBoxResult created = service.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f},
                 .max = {1.0f, 1.0f, 1.0f}},
      .name = "Shared SDK Mesh",
  });
  ASSERT_TRUE(created.ok) << created.message;
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Face).ok);
  ASSERT_TRUE(service
                  .set_component_selection({
                      .modeling_object_id = created.object.modeling_object_id,
                      .mode = ModelingSelectionMode::Face,
                      .faces = {1},
                      .edit = ModelingSelectionEdit::Replace,
                      .active_kind = ModelingActiveSelectionKind::Face,
                      .active_face = 1,
                  })
                  .ok);

  const MeshComponent *original_binding = scene.mesh(created.object.entity);
  ASSERT_NE(original_binding, nullptr);
  const AssetId original_mesh = original_binding->mesh;
  const EntityId shared_entity =
      scene.create_entity("Shared Mesh Consumer", EntityKind::Mesh);
  ASSERT_NE(shared_entity, kInvalidEntityId);
  ASSERT_TRUE(scene.set_mesh(shared_entity, {.mesh = original_mesh}));
  const std::vector<AssetId> mesh_ids_before = assets.mesh_ids();
  const std::uint64_t scene_revision_before = scene.revision();
  const std::uint64_t asset_revision_before = assets.revision();

  service.set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::CommittedSceneNotify);
  const ModelingCommandResult failed =
      service.move_selection({1.0f, 0.0f, 0.0f});
  EXPECT_FALSE(failed.ok);
  EXPECT_FALSE(failed.changed);
  ASSERT_NE(scene.mesh(created.object.entity), nullptr);
  ASSERT_NE(scene.mesh(shared_entity), nullptr);
  EXPECT_EQ(scene.mesh(created.object.entity)->mesh, original_mesh);
  EXPECT_EQ(scene.mesh(shared_entity)->mesh, original_mesh);
  EXPECT_EQ(assets.mesh_ids(), mesh_ids_before);
  EXPECT_EQ(scene.revision(), scene_revision_before);
  EXPECT_EQ(assets.revision(), asset_revision_before);
}

TEST(ModelingSessionServiceSplitTests,
     SdkProjectionRejectsMissingHostEntityOrAssetWithoutSdkDrift) {
  for (const bool remove_entity : {false, true}) {
    SCOPED_TRACE(remove_entity ? "missing entity" : "missing asset");
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);
    const ModelingCreateBoxResult created = service.create_box_from_bounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f},
                   .max = {1.0f, 1.0f, 1.0f}},
        .name = "Broken Host Projection",
    });
    ASSERT_TRUE(created.ok) << created.message;
    ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Face).ok);
    ASSERT_TRUE(service
                    .set_component_selection({
                        .modeling_object_id =
                            created.object.modeling_object_id,
                        .mode = ModelingSelectionMode::Face,
                        .faces = {1},
                        .edit = ModelingSelectionEdit::Replace,
                        .active_kind = ModelingActiveSelectionKind::Face,
                        .active_face = 1,
                    })
                    .ok);
    const ModelingSelectionSummary before = service.selection_summary();
    if (remove_entity) {
      ASSERT_TRUE(scene.destroy_entity(created.object.entity));
    } else {
      ASSERT_TRUE(assets.remove_mesh(created.object.mesh));
    }

    const ModelingCommandResult failed =
        service.move_selection({1.0f, 0.0f, 0.0f});
    EXPECT_FALSE(failed.ok);
    EXPECT_FALSE(failed.changed);
    EXPECT_EQ(service.modeling_sdk_object_count_for_tests(), 1u);
    EXPECT_EQ(service.selection_summary().content_revision,
              before.content_revision);
    EXPECT_EQ(service.selection_summary().selection_revision,
              before.selection_revision);
  }
}

TEST(ModelingSessionServiceSplitTests,
     SdkSelectionAndCommittedMoveFailuresRestoreExactObservableState) {
  {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);
    const ModelingCreateBoxResult created = service.create_box_from_bounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f},
                   .max = {1.0f, 1.0f, 1.0f}},
        .name = "Selection Rollback Box",
    });
    ASSERT_TRUE(created.ok) << created.message;
    const ModelingSelectionSummary before = service.selection_summary();
    const std::uint64_t scene_revision_before = scene.revision();
    const std::uint64_t scene_selection_revision_before =
        scene.selection_revision();
    service.set_modeling_sdk_projection_failure_for_tests(
        ModelingSdkProjectionFailurePoint::SelectionScenePublication);
    const ModelingCommandResult failed =
        service.set_selection_mode(ModelingSelectionMode::Face);
    EXPECT_FALSE(failed.ok);
    EXPECT_FALSE(failed.changed);
    EXPECT_EQ(service.selection_mode(), ModelingSelectionMode::Object);
    EXPECT_EQ(service.selection_summary().content_revision,
              before.content_revision);
    EXPECT_EQ(service.selection_summary().selection_revision,
              before.selection_revision);
    EXPECT_EQ(scene.revision(), scene_revision_before);
    EXPECT_EQ(scene.selection_revision(), scene_selection_revision_before);
    service.set_modeling_sdk_projection_failure_for_tests(
        ModelingSdkProjectionFailurePoint::None);
    EXPECT_TRUE(service.set_selection_mode(ModelingSelectionMode::Face).ok);
  }

  constexpr std::array<ModelingSdkProjectionFailurePoint, 3> kMoveFailures = {
      ModelingSdkProjectionFailurePoint::AfterSdkMutation,
      ModelingSdkProjectionFailurePoint::CommittedAssetReplace,
      ModelingSdkProjectionFailurePoint::CommittedSceneNotify,
  };
  for (const ModelingSdkProjectionFailurePoint failure_point : kMoveFailures) {
    SCOPED_TRACE(static_cast<int>(failure_point));
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);
    const ModelingCreateBoxResult created = service.create_box_from_bounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f},
                   .max = {1.0f, 1.0f, 1.0f}},
        .name = "Move Rollback Box",
    });
    ASSERT_TRUE(created.ok) << created.message;
    ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Face).ok);
    ASSERT_TRUE(service
                    .set_component_selection({
                        .modeling_object_id =
                            created.object.modeling_object_id,
                        .mode = ModelingSelectionMode::Face,
                        .faces = {1},
                        .edit = ModelingSelectionEdit::Replace,
                        .active_kind = ModelingActiveSelectionKind::Face,
                        .active_face = 1,
                    })
                    .ok);
    const AssetId mesh = scene.mesh(created.object.entity)->mesh;
    const MeshGeometryContentId content_before =
        assets.mesh(mesh)->geometry().content_id();
    const ModelingSelectionSummary selection_before =
        service.selection_summary();
    const std::uint64_t scene_revision_before = scene.revision();
    const std::uint64_t asset_revision_before = assets.revision();

    service.set_modeling_sdk_projection_failure_for_tests(failure_point);
    const ModelingCommandResult failed =
        service.move_selection({1.0f, 0.0f, 0.0f});
    EXPECT_FALSE(failed.ok);
    EXPECT_FALSE(failed.changed);
    EXPECT_EQ(service.selection_summary().content_revision,
              selection_before.content_revision);
    EXPECT_EQ(service.selection_summary().selection_revision,
              selection_before.selection_revision);
    EXPECT_EQ(scene.revision(), scene_revision_before);
    EXPECT_EQ(assets.revision(), asset_revision_before);
    ASSERT_NE(assets.mesh(mesh), nullptr);
    EXPECT_EQ(assets.mesh(mesh)->geometry().content_id(), content_before);

    service.set_modeling_sdk_projection_failure_for_tests(
        ModelingSdkProjectionFailurePoint::None);
    EXPECT_TRUE(service.move_selection({1.0f, 0.0f, 0.0f}).ok);
  }
}














TEST(ModelingSessionServiceSplitTests,
     SceneAwareDeleteRestoreUsesRuntimeMapInsteadOfStaleDuplicateSceneScan) {
  SceneDocument scene;
  ModelingObjectRuntimeMap object_map;
  const int modeling_object_id = 42;

  const EntityId stale =
      scene.create_entity("Stale Duplicate", EntityKind::Mesh);
  scene.set_modeling_object(
      stale, ModelingObjectComponent{.object_id = modeling_object_id});

  const EntityId selected =
      scene.create_entity("Selected Runtime", EntityKind::Mesh);
  scene.set_modeling_object(
      selected, ModelingObjectComponent{.object_id = modeling_object_id});
  ASSERT_TRUE(scene.set_selected(selected, true));
  ASSERT_TRUE(scene.set_active_selection(selected));
  ASSERT_TRUE(object_map.bind(modeling_object_id, selected));

  std::unique_ptr<IUndoableCommand> command =
      create_scene_aware_delete_selection_command(
          scene,
          SceneAwareDeleteRuntime{
              .capture_modeling_memento =
                  [] { return ModelingSessionMemento{}; },
              .restore_modeling_memento =
                  [&](const ModelingSessionMemento &) {
                    const EntityId restored = scene.create_entity(
                        "Runtime Restored", EntityKind::Mesh);
                    scene.set_modeling_object(
                        restored, ModelingObjectComponent{
                                      .object_id = modeling_object_id});
                    EXPECT_TRUE(object_map.bind(modeling_object_id, restored));
                    return ModelingCommandResult{.ok = true, .changed = true};
                  },
              .app_selection_mode =
                  [] { return ModelingSelectionMode::Object; },
              .set_app_selection_mode = [](ModelingSelectionMode) {},
              .delete_modeling_objects =
                  [&](const std::vector<int> &object_ids) {
                    EXPECT_EQ(object_ids, std::vector<int>{modeling_object_id});
                    object_map.unbind_object(modeling_object_id);
                    return ModelingCommandResult{.ok = true, .changed = true};
                  },
              .entity_for_modeling_object =
                  [&](int object_id) {
                    return object_map.entity_for_modeling_object(object_id);
                  },
              .sync_object_selection_from_scene = [](bool) {
                return ModelingCommandResult{.ok = true};
              },
              .clear_runtime_object_selection_for_group_mode = [] {
                return ModelingCommandResult{.ok = true};
              },
              .invalidate_selection_overlay_caches = [] {},
          });

  const CommandResult deleted = command->redo();
  ASSERT_TRUE(deleted.ok) << deleted.message;
  EXPECT_FALSE(scene.exists(selected));
  ASSERT_TRUE(scene.exists(stale));
  ASSERT_NE(scene.name(stale), nullptr);
  EXPECT_EQ(scene.name(stale)->name, "Stale Duplicate");

  const CommandResult restored = command->undo();
  ASSERT_TRUE(restored.ok) << restored.message;
  const std::optional<EntityId> restored_entity =
      object_map.entity_for_modeling_object(modeling_object_id);
  ASSERT_TRUE(restored_entity.has_value());
  ASSERT_TRUE(scene.exists(*restored_entity));
  ASSERT_NE(scene.name(*restored_entity), nullptr);
  EXPECT_EQ(scene.name(*restored_entity)->name, "Selected Runtime");
  ASSERT_NE(scene.name(stale), nullptr);
  EXPECT_EQ(scene.name(stale)->name, "Stale Duplicate");
}

TEST(ModelingSessionServiceSplitTests,
     SceneAwareDeleteUndoSelectionPublicationFailureRestoresDeletedState) {
  SceneDocument scene;
  const EntityId selected =
      scene.create_entity("Selected Scene Object", EntityKind::Mesh);
  ASSERT_TRUE(scene.set_selected(selected, true));
  ASSERT_TRUE(scene.set_active_selection(selected));

  bool fail_selection_publication = false;
  std::unique_ptr<IUndoableCommand> command =
      create_scene_aware_delete_selection_command(
          scene,
          SceneAwareDeleteRuntime{
              .capture_modeling_memento =
                  [] { return ModelingSessionMemento{}; },
              .restore_modeling_memento =
                  [](const ModelingSessionMemento &) {
                    return ModelingCommandResult{.ok = true};
                  },
              .app_selection_mode =
                  [] { return ModelingSelectionMode::Object; },
              .set_app_selection_mode = [](ModelingSelectionMode) {},
              .sync_object_selection_from_scene =
                  [&](bool) {
                    return fail_selection_publication
                               ? ModelingCommandResult{
                                     .ok = false,
                                     .message =
                                         "Injected selection publication failure.",
                                 }
                               : ModelingCommandResult{.ok = true};
                  },
              .clear_runtime_object_selection_for_group_mode = [] {
                return ModelingCommandResult{.ok = true};
              },
              .invalidate_selection_overlay_caches = [] {},
          });

  const CommandResult deleted = command->redo();
  ASSERT_TRUE(deleted.ok) << deleted.message;
  EXPECT_TRUE(deleted.changed);
  EXPECT_FALSE(scene.exists(selected));
  const std::uint64_t deleted_revision = scene.revision();
  const std::uint64_t deleted_selection_revision = scene.selection_revision();

  fail_selection_publication = true;
  const CommandResult restored = command->undo();
  EXPECT_FALSE(restored.ok) << restored.message;
  EXPECT_FALSE(restored.changed) << restored.message;
  EXPECT_NE(restored.message.find("Injected selection publication failure"),
            std::string::npos);
  EXPECT_FALSE(scene.exists(selected));
  EXPECT_TRUE(scene.selected_entities().empty());
  EXPECT_EQ(scene.revision(), deleted_revision);
  EXPECT_EQ(scene.selection_revision(), deleted_selection_revision);
}

TEST(ModelingSessionServiceSplitTests,
     SceneAwareDeleteInstanceUndoPublicationFailureRestoresDeletedStateAndRetries) {
  SceneDocument scene;
  const EntityId parent = scene.create_empty("Retained Parent");
  const EntityId definition = scene.create_empty("Instance Definition");
  ASSERT_TRUE(scene.set_parent(definition, parent, false));
  ASSERT_TRUE(scene.set_instance_definition(definition));
  const EntityId member = scene.create_entity("Instance Member", EntityKind::Mesh);
  ASSERT_TRUE(scene.set_mesh(member, {.mesh = 77u}));
  ASSERT_TRUE(scene.set_parent(member, definition, false));
  const EntityId occurrence =
      scene.create_entity("Final Occurrence", EntityKind::Instance);
  ASSERT_TRUE(scene.set_parent(occurrence, parent, false));
  ASSERT_TRUE(
      scene.set_instance_occurrence(occurrence, {.definition = definition}));
  ASSERT_TRUE(scene.set_selected(occurrence, true));
  ASSERT_TRUE(scene.set_active_selection(occurrence));
  const std::string definition_uuid = scene.uuid(definition);
  const std::string member_uuid = scene.uuid(member);
  const std::string occurrence_uuid = scene.uuid(occurrence);
  const EditorObjectId definition_editor = scene.editor_object_id(definition);
  const EditorObjectId member_editor = scene.editor_object_id(member);
  const EditorObjectId occurrence_editor = scene.editor_object_id(occurrence);
  const auto revisions = [&] {
    return std::array{scene.revision(), scene.content_revision(),
                      scene.selection_revision(),
                      scene.hierarchy_presentation_revision(),
                      scene.visibility_revision(), scene.transform_revision(),
                      scene.mesh_binding_revision(),
                      scene.render_component_revision(), scene.layer_revision()};
  };
  bool fail_selection_publication = false;
  std::unique_ptr<IUndoableCommand> command =
      create_scene_aware_delete_selection_command(
          scene,
          SceneAwareDeleteRuntime{
              .capture_modeling_memento =
                  [] { return ModelingSessionMemento{}; },
              .restore_modeling_memento =
                  [](const ModelingSessionMemento &) {
                    return ModelingCommandResult{.ok = true};
                  },
              .app_selection_mode =
                  [] { return ModelingSelectionMode::Object; },
              .set_app_selection_mode = [](ModelingSelectionMode) {},
              .sync_object_selection_from_scene = [&](bool) {
                EXPECT_TRUE(scene.exists(definition));
                EXPECT_TRUE(scene.exists(member));
                EXPECT_TRUE(scene.exists(occurrence));
                EXPECT_EQ(scene.parent(member), definition);
                EXPECT_EQ(scene.primary_selection(), occurrence);
                return fail_selection_publication
                           ? ModelingCommandResult{
                                 .ok = false,
                                 .message =
                                     "Injected instance selection publication failure.",
                             }
                           : ModelingCommandResult{.ok = true};
              },
              .invalidate_selection_overlay_caches = [] {},
          });
  ASSERT_NE(command, nullptr);
  const CommandResult deleted = command->redo();
  ASSERT_TRUE(deleted.ok) << deleted.message;
  ASSERT_TRUE(deleted.changed);
  ASSERT_EQ(scene.entities(), std::vector<EntityId>{parent});
  ASSERT_TRUE(scene.children(parent).empty());
  ASSERT_EQ(scene.mesh_binding_reference_count(77u), 0u);
  const auto deleted_revisions = revisions();

  fail_selection_publication = true;
  const CommandResult failed = command->undo();
  EXPECT_FALSE(failed.ok);
  EXPECT_FALSE(failed.changed) << failed.message;
  EXPECT_EQ(failed.message, "Injected instance selection publication failure.");
  EXPECT_EQ(scene.entities(), std::vector<EntityId>{parent});
  EXPECT_TRUE(scene.children(parent).empty());
  EXPECT_TRUE(scene.selected_entities().empty());
  EXPECT_EQ(scene.primary_selection(), kInvalidEntityId);
  EXPECT_EQ(scene.mesh_binding_reference_count(77u), 0u);
  EXPECT_EQ(scene.entity_for_uuid(definition_uuid), kInvalidEntityId);
  EXPECT_EQ(scene.entity_for_uuid(member_uuid), kInvalidEntityId);
  EXPECT_EQ(scene.entity_for_uuid(occurrence_uuid), kInvalidEntityId);
  EXPECT_EQ(revisions(), deleted_revisions);

  fail_selection_publication = false;
  const CommandResult retried = command->undo();
  ASSERT_TRUE(retried.ok) << retried.message;
  EXPECT_TRUE(retried.changed);
  EXPECT_EQ(scene.children(parent), (std::vector<EntityId>{definition, occurrence}));
  EXPECT_EQ(scene.children(definition), std::vector<EntityId>{member});
  ASSERT_NE(scene.instance_occurrence(occurrence), nullptr);
  EXPECT_EQ(scene.instance_occurrence(occurrence)->definition, definition);
  EXPECT_EQ(scene.instance_occurrences_for_definition(definition),
            std::vector<EntityId>{occurrence});
  EXPECT_EQ(scene.entity_for_uuid(definition_uuid), definition);
  EXPECT_EQ(scene.entity_for_uuid(member_uuid), member);
  EXPECT_EQ(scene.entity_for_uuid(occurrence_uuid), occurrence);
  EXPECT_EQ(scene.editor_object_id(definition), definition_editor);
  EXPECT_EQ(scene.editor_object_id(member), member_editor);
  EXPECT_EQ(scene.editor_object_id(occurrence), occurrence_editor);
  EXPECT_EQ(scene.mesh_binding_reference_count(77u), 1u);
  EXPECT_EQ(scene.selected_entities(), std::vector<EntityId>{occurrence});
  EXPECT_EQ(scene.primary_selection(), occurrence);
}

TEST(ModelingSessionServiceSplitTests,
     SceneAwareDeleteModeledInstanceUndoFailureRetainsDeletedStateAndRetries) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);
  // Supply a real checkpoint while simulating the modeling callbacks so the
  // test exercises inverse publication and its scene allocation receipts.
  const ModelingSessionMemento modeling_checkpoint =
      service.capture_history_memento();
  ASSERT_TRUE(modeling_checkpoint.valid());
  ModelingObjectRuntimeMap object_map;
  constexpr int kModelingObject = 42;

  const EntityId retained_definition = scene.create_empty("Retained Definition");
  ASSERT_TRUE(scene.set_instance_definition(retained_definition));
  const EntityId retained_member = scene.create_empty("Retained Member");
  ASSERT_TRUE(scene.set_parent(retained_member, retained_definition, false));
  const EntityId retained_occurrence =
      scene.create_entity("Retained Occurrence", EntityKind::Instance);
  ASSERT_TRUE(scene.set_instance_occurrence(
      retained_occurrence, {.definition = retained_definition}));
  const std::string retained_uuid = scene.uuid(retained_occurrence);
  const EntityId definition = scene.create_empty("Deleted Definition");
  ASSERT_TRUE(scene.set_instance_definition(definition));
  const EntityId member = scene.create_entity("Modeled Member", EntityKind::Mesh);
  ASSERT_TRUE(scene.set_mesh(member, {.mesh = 77u}));
  ASSERT_TRUE(scene.set_modeling_object(
      member, {.object_id = kModelingObject, .sdk_owned = true}));
  ASSERT_TRUE(scene.set_parent(member, definition, false));
  ASSERT_TRUE(object_map.bind(kModelingObject, member));
  const EditorObjectId member_editor = scene.editor_object_id(member);
  const std::string member_uuid = scene.uuid(member);
  const EntityId occurrence =
      scene.create_entity("Deleted Occurrence", EntityKind::Instance);
  ASSERT_TRUE(
      scene.set_instance_occurrence(occurrence, {.definition = definition}));
  ASSERT_TRUE(scene.set_selected(occurrence, true));
  ASSERT_TRUE(scene.set_active_selection(occurrence));
  const auto revisions = [&] {
    return std::array{scene.revision(), scene.content_revision(),
                      scene.selection_revision(),
                      scene.hierarchy_presentation_revision(),
                      scene.visibility_revision(), scene.transform_revision(),
                      scene.mesh_binding_revision(),
                      scene.render_component_revision(), scene.layer_revision()};
  };
  bool fail_selection_publication = false;
  std::size_t inverse_modeling_calls = 0u;
  std::unique_ptr<IUndoableCommand> command =
      create_scene_aware_delete_selection_command(
          scene,
          SceneAwareDeleteRuntime{
              .capture_modeling_memento = [&] { return modeling_checkpoint; },
              .restore_modeling_memento = [&](const ModelingSessionMemento &) {
                const EntityId restored = scene.create_entity_with_id(
                    member, "Runtime Restored", EntityKind::Mesh, member_editor);
                if (restored == kInvalidEntityId ||
                    !scene.set_entity_uuid(restored, member_uuid) ||
                    !scene.set_mesh(restored, {.mesh = 77u}) ||
                    !scene.set_modeling_object(
                        restored,
                        {.object_id = kModelingObject, .sdk_owned = true}) ||
                    !object_map.bind(kModelingObject, restored)) {
                  return ModelingCommandResult{.ok = false,
                                               .message = "Runtime restore failed."};
                }
                return ModelingCommandResult{.ok = true, .changed = true};
              },
              .restore_modeling_memento_exact =
                  [&](const ModelingSessionMemento &) {
                    ++inverse_modeling_calls;
                    const InstanceOccurrenceComponent *retained =
                        scene.instance_occurrence(retained_occurrence);
                    EXPECT_NE(retained, nullptr);
                    if (retained != nullptr) {
                      EXPECT_EQ(retained->definition, retained_definition);
                    }
                    EXPECT_EQ(scene.uuid(retained_occurrence), retained_uuid);
                    const bool removed = scene.destroy_entity(member);
                    if (removed) {
                      object_map.unbind_object(kModelingObject);
                    }
                    return ModelingCommandResult{
                        .ok = removed,
                        .changed = removed,
                        .message = removed ? ""
                                           : "Inverse modeled member removal failed.",
                    };
                  },
              .app_selection_mode =
                  [] { return ModelingSelectionMode::Object; },
              .set_app_selection_mode = [](ModelingSelectionMode) {},
              .delete_modeling_objects = [&](const std::vector<int> &objects) {
                EXPECT_EQ(objects, std::vector<int>{kModelingObject});
                object_map.unbind_object(kModelingObject);
                return ModelingCommandResult{.ok = true, .changed = true};
              },
              .entity_for_modeling_object = [&](int object) {
                return object_map.entity_for_modeling_object(object);
              },
              .sync_object_selection_from_scene = [&](bool) {
                EXPECT_TRUE(scene.exists(definition));
                EXPECT_TRUE(scene.exists(member));
                EXPECT_NE(scene.instance_occurrence(occurrence), nullptr);
                EXPECT_EQ(scene.parent(member), definition);
                return fail_selection_publication
                           ? ModelingCommandResult{
                                 .ok = false,
                                 .message = "Injected modeled instance publication failure.",
                             }
                           : ModelingCommandResult{.ok = true};
              },
              .invalidate_selection_overlay_caches = [] {},
          });
  ASSERT_NE(command, nullptr);
  const CommandResult deleted = command->redo();
  ASSERT_TRUE(deleted.ok) << deleted.message;
  ASSERT_TRUE(deleted.changed);
  ASSERT_FALSE(scene.exists(definition));
  ASSERT_FALSE(scene.exists(member));
  ASSERT_FALSE(scene.exists(occurrence));
  const std::vector<EntityId> deleted_entities = scene.entities();
  const auto deleted_revisions = revisions();

  fail_selection_publication = true;
  const CommandResult failed = command->undo();
  EXPECT_FALSE(failed.ok);
  EXPECT_FALSE(failed.changed) << failed.message;
  EXPECT_EQ(failed.message, "Injected modeled instance publication failure.");
  EXPECT_EQ(inverse_modeling_calls, 1u);
  EXPECT_EQ(scene.entities(), deleted_entities);
  EXPECT_EQ(revisions(), deleted_revisions);
  EXPECT_TRUE(scene.selected_entities().empty());
  EXPECT_EQ(scene.mesh_binding_reference_count(77u), 0u);
  EXPECT_FALSE(object_map.entity_for_modeling_object(kModelingObject).has_value());
  ASSERT_NE(scene.instance_occurrence(retained_occurrence), nullptr);
  EXPECT_EQ(scene.instance_occurrence(retained_occurrence)->definition,
            retained_definition);
  EXPECT_EQ(scene.children(retained_definition),
            std::vector<EntityId>{retained_member});
  EXPECT_EQ(scene.uuid(retained_occurrence), retained_uuid);

  fail_selection_publication = false;
  const CommandResult retried = command->undo();
  ASSERT_TRUE(retried.ok) << retried.message;
  EXPECT_TRUE(retried.changed);
  EXPECT_EQ(inverse_modeling_calls, 1u);
  EXPECT_EQ(object_map.entity_for_modeling_object(kModelingObject), member);
  EXPECT_EQ(scene.uuid(member), member_uuid);
  EXPECT_EQ(scene.editor_object_id(member), member_editor);
  EXPECT_EQ(scene.parent(member), definition);
  ASSERT_NE(scene.instance_occurrence(occurrence), nullptr);
  EXPECT_EQ(scene.instance_occurrence(occurrence)->definition, definition);
  EXPECT_EQ(scene.mesh_binding_reference_count(77u), 1u);
  EXPECT_EQ(scene.selected_entities(), std::vector<EntityId>{occurrence});
}

TEST(ModelingSessionServiceSplitTests,
     SceneAwareDeleteRedoFailureRestoresReallocatedModeledIdentityAndRetries) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);
  const ModelingSessionMemento modeling_checkpoint =
      service.capture_history_memento();
  ASSERT_TRUE(modeling_checkpoint.valid());
  ModelingObjectRuntimeMap object_map;
  constexpr int kModelingObject = 42;
  const EntityId selected = scene.create_entity("Modeled Mesh", EntityKind::Mesh);
  ASSERT_TRUE(scene.set_mesh(selected, {.mesh = 77u}));
  ASSERT_TRUE(scene.set_modeling_object(
      selected, {.object_id = kModelingObject, .sdk_owned = true}));
  ASSERT_TRUE(scene.set_selected(selected, true));
  ASSERT_TRUE(object_map.bind(kModelingObject, selected));
  const EditorObjectId editor_identity = scene.editor_object_id(selected);
  const std::string uuid = scene.uuid(selected);
  const auto restore_modeled_entity = [&] {
    EXPECT_FALSE(scene.exists(selected));
    const EntityId restored = scene.create_entity_with_id(
        selected, "Modeled Mesh", EntityKind::Mesh, editor_identity);
    const bool restored_ok =
        restored == selected && scene.set_entity_uuid(restored, uuid) &&
        scene.set_mesh(restored, {.mesh = 77u}) &&
        scene.set_modeling_object(
            restored, {.object_id = kModelingObject, .sdk_owned = true}) &&
        object_map.bind(kModelingObject, restored);
    return ModelingCommandResult{.ok = restored_ok, .changed = restored_ok};
  };
  bool redo_transition = false;
  bool fail_redo = false;
  std::size_t inverse_modeling_calls = 0u;
  std::unique_ptr<IUndoableCommand> command =
      create_scene_aware_delete_selection_command(
          scene,
          SceneAwareDeleteRuntime{
              .capture_modeling_memento = [&] { return modeling_checkpoint; },
              .restore_modeling_memento = [&](const ModelingSessionMemento &) {
                if (!redo_transition) {
                  return restore_modeled_entity();
                }
                if (fail_redo) {
                  return ModelingCommandResult{
                      .ok = false,
                      .message = "Injected modeling redo failure.",
                  };
                }
                object_map.unbind_object(kModelingObject);
                return ModelingCommandResult{.ok = true, .changed = true};
              },
              .restore_modeling_memento_exact =
                  [&](const ModelingSessionMemento &) {
                    ++inverse_modeling_calls;
                    return restore_modeled_entity();
                  },
              .app_selection_mode =
                  [] { return ModelingSelectionMode::Object; },
              .set_app_selection_mode = [](ModelingSelectionMode) {},
              .delete_modeling_objects = [&](const std::vector<int> &objects) {
                EXPECT_EQ(objects, std::vector<int>{kModelingObject});
                object_map.unbind_object(kModelingObject);
                return ModelingCommandResult{.ok = true, .changed = true};
              },
              .entity_for_modeling_object = [&](int object) {
                return object_map.entity_for_modeling_object(object);
              },
              .sync_object_selection_from_scene = [](bool) {
                return ModelingCommandResult{.ok = true};
              },
              .invalidate_selection_overlay_caches = [] {},
          });
  ASSERT_NE(command, nullptr);
  ASSERT_TRUE(command->redo().ok);
  const CommandResult undone = command->undo();
  ASSERT_TRUE(undone.ok) << undone.message;
  const auto revisions = [&] {
    return std::array{scene.revision(), scene.content_revision(),
                      scene.selection_revision(), scene.mesh_binding_revision(),
                      scene.hierarchy_presentation_revision(),
                      scene.render_component_revision()};
  };
  const auto restored_revisions = revisions();
  ASSERT_EQ(scene.entities(), std::vector<EntityId>{selected});

  redo_transition = true;
  fail_redo = true;
  const CommandResult failed = command->redo();
  EXPECT_FALSE(failed.ok);
  EXPECT_FALSE(failed.changed) << failed.message;
  EXPECT_EQ(failed.message, "Injected modeling redo failure.");
  EXPECT_EQ(inverse_modeling_calls, 1u);
  EXPECT_EQ(scene.entities(), std::vector<EntityId>{selected});
  EXPECT_EQ(scene.uuid(selected), uuid);
  EXPECT_EQ(scene.editor_object_id(selected), editor_identity);
  EXPECT_EQ(object_map.entity_for_modeling_object(kModelingObject), selected);
  EXPECT_EQ(scene.mesh_binding_reference_count(77u), 1u);
  EXPECT_EQ(scene.selected_entities(), std::vector<EntityId>{selected});
  EXPECT_EQ(revisions(), restored_revisions);

  fail_redo = false;
  const CommandResult retried = command->redo();
  ASSERT_TRUE(retried.ok) << retried.message;
  EXPECT_TRUE(retried.changed);
  EXPECT_TRUE(scene.entities().empty());
  EXPECT_TRUE(scene.selected_entities().empty());
  EXPECT_EQ(scene.mesh_binding_reference_count(77u), 0u);
  EXPECT_FALSE(object_map.entity_for_modeling_object(kModelingObject).has_value());
}

TEST(ModelingSessionServiceSplitTests,
     SceneAwareDeleteInitialDestroyFailureRestoresNestedSceneExactly) {
  SceneDocument scene;
  const EntityId group = scene.create_entity("Group", EntityKind::Empty);
  const EntityId generic = scene.create_empty("Generic Child");
  const EntityId curve = scene.create_entity("Curve Child", EntityKind::Curve);
  ASSERT_TRUE(scene.set_parent(generic, group));
  ASSERT_TRUE(scene.set_parent(curve, group));
  scene.set_curve(curve, CurveComponent{77});
  ASSERT_TRUE(scene.set_selected(group, true));
  ASSERT_TRUE(scene.set_active_selection(group));
  const std::uint64_t revision_before = scene.revision();
  const std::uint64_t selection_revision_before = scene.selection_revision();

  std::unique_ptr<IUndoableCommand> command =
      create_scene_aware_delete_selection_command(
          scene,
          SceneAwareDeleteRuntime{
              .capture_modeling_memento =
                  [] { return ModelingSessionMemento{}; },
              .restore_modeling_memento =
                  [](const ModelingSessionMemento &) {
                    return ModelingCommandResult{.ok = true};
                  },
              .app_selection_mode =
                  [] { return ModelingSelectionMode::Group; },
              .set_app_selection_mode = [](ModelingSelectionMode) {},
              .destroy_scene_entities =
                  [&](const std::vector<EntityId> &entities) {
                    EXPECT_EQ(entities, std::vector<EntityId>{group});
                    EXPECT_TRUE(scene.destroy_entities(entities));
                    return false;
                  },
              .sync_object_selection_from_scene = [](bool) {
                return ModelingCommandResult{.ok = true};
              },
              .clear_runtime_object_selection_for_group_mode = [] {
                return ModelingCommandResult{.ok = true};
              },
              .invalidate_selection_overlay_caches = [] {},
          });

  const CommandResult failed = command->redo();
  EXPECT_FALSE(failed.ok);
  EXPECT_FALSE(failed.changed);
  ASSERT_TRUE(scene.exists(group));
  ASSERT_TRUE(scene.exists(generic));
  ASSERT_TRUE(scene.exists(curve));
  EXPECT_EQ(scene.parent(generic), group);
  EXPECT_EQ(scene.parent(curve), group);
  ASSERT_NE(scene.curve(curve), nullptr);
  EXPECT_EQ(scene.curve(curve)->curve, 77u);
  EXPECT_EQ(scene.selected_entities(), std::vector<EntityId>{group});
  EXPECT_EQ(scene.primary_selection(), group);
  EXPECT_EQ(scene.revision(), revision_before);
  EXPECT_EQ(scene.selection_revision(), selection_revision_before);
}

TEST(ModelingSessionServiceSplitTests,
     SceneAwareDeleteUndoRestoreFailurePreservesDeletedStateExactly) {
  SceneDocument scene;
  constexpr int kObjectId = 81;
  const EntityId selected =
      scene.create_entity("Selected Modeled Object", EntityKind::Mesh);
  scene.set_modeling_object(selected,
                            ModelingObjectComponent{.object_id = kObjectId});
  ASSERT_TRUE(scene.set_selected(selected, true));
  ASSERT_TRUE(scene.set_active_selection(selected));

  bool fail_restore = false;
  std::unique_ptr<IUndoableCommand> command =
      create_scene_aware_delete_selection_command(
          scene,
          SceneAwareDeleteRuntime{
              .capture_modeling_memento =
                  [] { return ModelingSessionMemento{}; },
              .restore_modeling_memento =
                  [&](const ModelingSessionMemento &) {
                    return fail_restore
                               ? ModelingCommandResult{
                                     .ok = false,
                                     .changed = false,
                                     .message =
                                         "Injected modeling restore failure.",
                                 }
                               : ModelingCommandResult{.ok = true,
                                                       .changed = true};
                  },
              .app_selection_mode =
                  [] { return ModelingSelectionMode::Object; },
              .set_app_selection_mode = [](ModelingSelectionMode) {},
              .delete_modeling_objects =
                  [](const std::vector<int> &objects) {
                    EXPECT_EQ(objects, std::vector<int>{kObjectId});
                    return ModelingCommandResult{.ok = true, .changed = true};
                  },
              .sync_object_selection_from_scene = [](bool) {
                return ModelingCommandResult{.ok = true};
              },
              .clear_runtime_object_selection_for_group_mode = [] {
                return ModelingCommandResult{.ok = true};
              },
              .invalidate_selection_overlay_caches = [] {},
          });

  const CommandResult deleted = command->redo();
  ASSERT_TRUE(deleted.ok) << deleted.message;
  ASSERT_FALSE(scene.exists(selected));
  const std::uint64_t deleted_revision = scene.revision();
  const std::uint64_t deleted_selection_revision = scene.selection_revision();

  fail_restore = true;
  const CommandResult failed = command->undo();
  EXPECT_FALSE(failed.ok);
  EXPECT_FALSE(failed.changed);
  EXPECT_NE(failed.message.find("Injected modeling restore failure"),
            std::string::npos);
  EXPECT_FALSE(scene.exists(selected));
  EXPECT_TRUE(scene.selected_entities().empty());
  EXPECT_EQ(scene.revision(), deleted_revision);
  EXPECT_EQ(scene.selection_revision(), deleted_selection_revision);
}

TEST(ModelingSessionServiceSplitTests, ServicesOwnIndependentModelingState) {
  TransformDragController drag;
  EXPECT_FALSE(drag.preview_driven());
  drag.set_preview_driven(true, true);
  EXPECT_TRUE(drag.preview_driven());
  EXPECT_TRUE(drag.duplicate_selected());
  drag.clear();
  EXPECT_FALSE(drag.preview_driven());
  const TransformDragPreviewRouting object_transform =
      TransformDragController::preview_routing(false, true, true);
  EXPECT_FALSE(object_transform.preview_driven);
  EXPECT_FALSE(object_transform.needs_runtime_preview_transaction);
  EXPECT_TRUE(object_transform.scene_transform_preview_eligible);
  const TransformDragPreviewRouting duplicate_object_transform =
      TransformDragController::preview_routing(true, true, true);
  EXPECT_FALSE(duplicate_object_transform.preview_driven);
  EXPECT_TRUE(duplicate_object_transform.needs_runtime_preview_transaction);
  EXPECT_TRUE(duplicate_object_transform.scene_transform_preview_eligible);
  const TransformDragPreviewRouting duplicate_scene_object_transform =
      TransformDragController::preview_routing(true, true, true, true);
  EXPECT_FALSE(duplicate_scene_object_transform.preview_driven);
  EXPECT_FALSE(
      duplicate_scene_object_transform.needs_runtime_preview_transaction);
  EXPECT_TRUE(
      duplicate_scene_object_transform.scene_transform_preview_eligible);
  const TransformDragPreviewRouting component_transform =
      TransformDragController::preview_routing(false, false, true);
  EXPECT_TRUE(component_transform.preview_driven);
  EXPECT_TRUE(component_transform.needs_runtime_preview_transaction);
  EXPECT_FALSE(component_transform.scene_transform_preview_eligible);
  const TransformDragPreviewRouting object_extrude =
      TransformDragController::preview_routing(false, true, false);
  EXPECT_FALSE(object_extrude.preview_driven);
  EXPECT_FALSE(object_extrude.needs_runtime_preview_transaction);
  EXPECT_FALSE(object_extrude.scene_transform_preview_eligible);

  ModelingToolSettingsService tool_settings;
  EXPECT_EQ(tool_settings.revision(), 0u);
  tool_settings.set_preview_metadata("edge_bevel", "edge_bevel", "Edge Bevel");
  const std::uint64_t metadata_revision = tool_settings.revision();
  EXPECT_GT(metadata_revision, 0u);
  tool_settings.set_preview_metadata("edge_bevel", "edge_bevel", "Edge Bevel");
  EXPECT_EQ(tool_settings.revision(), metadata_revision);
  const ModelingToolSettingsTarget target =
      tool_settings.operation_preview_target(
          ModelingToolSettingsTargetKind::EdgeBevel, "Fallback",
          "fallback_operation");
  EXPECT_TRUE(target.active);
  EXPECT_TRUE(target.operation_preview_active);
  EXPECT_EQ(target.action_id, "edge_bevel");
  EXPECT_EQ(target.operation_id, "edge_bevel");
  EXPECT_EQ(target.label, "Edge Bevel");
  tool_settings.clear_preview_metadata();
  EXPECT_GT(tool_settings.revision(), metadata_revision);
  const std::uint64_t cleared_metadata_revision = tool_settings.revision();
  tool_settings.clear_preview_metadata();
  EXPECT_EQ(tool_settings.revision(), cleared_metadata_revision);
  const ModelingToolSettingsTarget fallback =
      tool_settings.operation_preview_target(
          ModelingToolSettingsTargetKind::Thicken, "Thicken", "thicken");
  EXPECT_EQ(fallback.label, "Thicken");
  EXPECT_EQ(fallback.operation_id, "thicken");
  const ModelingToolSettingsTarget preview_target =
      tool_settings.active_preview_target(
          quader_editor_core::PreviewTransactionKind::EdgeBevel);
  EXPECT_TRUE(preview_target.active);
  EXPECT_TRUE(preview_target.operation_preview_active);
  EXPECT_EQ(preview_target.kind, ModelingToolSettingsTargetKind::EdgeBevel);
  EXPECT_EQ(preview_target.operation_id, "edge_bevel");
  EXPECT_EQ(preview_target.label, "Bevel Edges");
  quader_editor_core::EditorRuntimeStatus runtime_status;
  runtime_status.cut_active = true;
  const ModelingToolSettingsTarget runtime_target =
      tool_settings.active_runtime_tool_target(runtime_status);
  EXPECT_TRUE(runtime_target.active);
  EXPECT_FALSE(runtime_target.operation_preview_active);
  EXPECT_EQ(runtime_target.kind, ModelingToolSettingsTargetKind::Cut);
  EXPECT_EQ(runtime_target.label, "Cut");

  ModelingInteractionSettingsService interaction_settings;
  EXPECT_EQ(interaction_settings.revision(), 0u);
  EXPECT_EQ(interaction_settings.settings().cut_mode,
            ModelingCutMode::KeepBoth);
  interaction_settings.set_cut_mode(ModelingCutMode::DiscardLeft);
  const std::uint64_t cut_mode_revision = interaction_settings.revision();
  EXPECT_GT(cut_mode_revision, 0u);
  interaction_settings.set_cut_mode(ModelingCutMode::DiscardLeft);
  EXPECT_EQ(interaction_settings.revision(), cut_mode_revision);
  EXPECT_EQ(interaction_settings.settings().cut_mode,
            ModelingCutMode::DiscardLeft);
  interaction_settings.cycle_cut_mode_without_runtime();
  EXPECT_GT(interaction_settings.revision(), cut_mode_revision);
  EXPECT_EQ(interaction_settings.settings().cut_mode,
            ModelingCutMode::DiscardRight);
}

TEST(MaterialReferenceServiceTests,
     SelectedFaceFailureRollsBackTopologyBindingAndSelectionExactly) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService modeling(scene, assets);
  const ModelingCreateBoxResult created = modeling.create_box_from_bounds({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f},
                 .max = {1.0f, 1.0f, 1.0f}},
      .name = "Material Reference Rollback Box",
  });
  ASSERT_TRUE(created.ok) << created.message;
  constexpr quader_poly::ElementId kSelectedFace = 1;
  const MaterialReference source(101u);
  const MaterialReference replacement(202u);
  const MaterialComponent component_before{.material = source};
  ASSERT_TRUE(scene.set_material(created.object.entity, component_before));
  ASSERT_TRUE(modeling.set_selection_mode(ModelingSelectionMode::Face).ok);
  ASSERT_TRUE(modeling
                  .set_component_selection({
                      .modeling_object_id = created.object.modeling_object_id,
                      .mode = ModelingSelectionMode::Face,
                      .faces = {kSelectedFace},
                      .edit = ModelingSelectionEdit::Replace,
                      .active_kind = ModelingActiveSelectionKind::Face,
                      .active_face = kSelectedFace,
                  })
                  .ok);

  const MeshComponent *mesh_component = scene.mesh(created.object.entity);
  ASSERT_NE(mesh_component, nullptr);
  const MeshAsset *mesh_before = assets.mesh(mesh_component->mesh);
  ASSERT_NE(mesh_before, nullptr);
  ASSERT_TRUE(mesh_before->has_authored_polygon_document);
  ASSERT_NE(mesh_before->authored_polygon_document, nullptr);
  const quader_poly::Face *face_before = quader_poly::find_face(
      *mesh_before->authored_polygon_document, kSelectedFace);
  ASSERT_NE(face_before, nullptr);
  const std::uint32_t slot_before = face_before->material_slot;
  const ModelingSelectionExport selection_before = modeling.export_selection();

  MaterialReferenceService references(scene, assets, modeling);
  MaterialReferenceReplacementPreparation prepared =
      references.prepare_replacement(
          source, replacement, MaterialReferenceUsageScope::Selection,
          [](const MaterialReference &) { return true; });
  ASSERT_TRUE(prepared.ok) << prepared.message;
  ASSERT_TRUE(prepared.changed);
  ASSERT_NE(prepared.command, nullptr);

  modeling.set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::CommittedAssetReplace);
  const CommandResult failed = prepared.command->redo();
  modeling.set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::None);
  EXPECT_FALSE(failed.ok);
  EXPECT_FALSE(failed.changed);
  ASSERT_NE(scene.material(created.object.entity), nullptr);
  EXPECT_EQ(*scene.material(created.object.entity), component_before);

  mesh_component = scene.mesh(created.object.entity);
  ASSERT_NE(mesh_component, nullptr);
  const MeshAsset *mesh_after = assets.mesh(mesh_component->mesh);
  ASSERT_NE(mesh_after, nullptr);
  ASSERT_NE(mesh_after->authored_polygon_document, nullptr);
  const quader_poly::Face *face_after = quader_poly::find_face(
      *mesh_after->authored_polygon_document, kSelectedFace);
  ASSERT_NE(face_after, nullptr);
  EXPECT_EQ(face_after->material_slot, slot_before);

  const ModelingSelectionExport selection_after = modeling.export_selection();
  EXPECT_EQ(selection_after.summary.mode, selection_before.summary.mode);
  ASSERT_EQ(selection_after.objects.size(), selection_before.objects.size());
  ASSERT_EQ(selection_after.objects.size(), 1u);
  EXPECT_EQ(selection_after.objects.front().modeling_object_id,
            selection_before.objects.front().modeling_object_id);
  ASSERT_EQ(selection_after.objects.front().selected_faces.size(), 1u);
  ASSERT_EQ(selection_before.objects.front().selected_faces.size(), 1u);
  EXPECT_EQ(selection_after.objects.front().selected_faces.front().id,
            selection_before.objects.front().selected_faces.front().id);
}

TEST(ModelingSessionServiceSplitTests,
     ResetForTargetRestoresOnlyThatOperationInOneRevision) {
  ModelingInteractionSettingsService service;
  ModelingInteractionSettings edited = service.settings();
  edited.edge_bevel_width = 4.0;
  edited.half_cut_axis = ModelingHalfCutAxis::Z;
  edited.half_cut_separate_parts = true;
  edited.half_cut_fill_parts = false;
  ASSERT_TRUE(service.set_normalized(edited));
  const std::uint64_t edited_revision = service.revision();

  EXPECT_TRUE(
      service.reset_for_target(ModelingToolSettingsTargetKind::HalfCut));
  EXPECT_EQ(service.revision(), edited_revision + 1u);
  const ModelingInteractionSettings reset = service.settings();
  EXPECT_EQ(reset.half_cut_axis, ModelingHalfCutAxis::X);
  EXPECT_FALSE(reset.half_cut_separate_parts);
  EXPECT_TRUE(reset.half_cut_fill_parts);
  EXPECT_DOUBLE_EQ(reset.edge_bevel_width, 4.0);

  EXPECT_FALSE(
      service.reset_for_target(ModelingToolSettingsTargetKind::HalfCut));
  EXPECT_EQ(service.revision(), edited_revision + 1u);
  EXPECT_FALSE(
      service.reset_for_target(ModelingToolSettingsTargetKind::Mirror));
  EXPECT_EQ(service.revision(), edited_revision + 1u);
}

TEST(ModelingSessionServiceSplitTests, PolicyServicesExposeDirectBehavior) {
  ModelingSelectionSummary component_selection;
  EXPECT_FALSE(
      ModelingSelectionService::has_component_selection(component_selection));
  component_selection.selected_edge_count = 1;
  EXPECT_TRUE(
      ModelingSelectionService::has_component_selection(component_selection));

  ModelingToolStatus status;
  EXPECT_FALSE(ModelingOverlayService::has_modal_overlay(status));
  status.cut_has_line = true;
  EXPECT_TRUE(ModelingOverlayService::has_modal_overlay(status));

  quader_editor_core::EditorRuntimeState runtime_state;
  runtime_state.active_tool = quader_editor_core::ViewportInteractionTool::Cut;
  runtime_state.cut.points = {{1.0f, 2.0f, 3.0f}};
  runtime_state.cut.mode = quader_editor_core::CutMode::KeepFront;
  runtime_state.cut.dragging_new_point = true;
  runtime_state.cut.dragging_point_index = 0;
  runtime_state.cut.preview_valid = true;
  runtime_state.cut_hover.valid = true;
  runtime_state.cut_hover.position = {4.0f, 5.0f, 6.0f};
  runtime_state.mirror.plane.points = {{7.0f, 8.0f, 9.0f}};
  runtime_state.mirror.plane.dragging_line = true;
  runtime_state.mirror.plane.drag_line_anchor = {7.0f, 8.0f, 9.0f};
  runtime_state.mirror.preview_valid = true;
  runtime_state.mirror_hover.valid = true;
  runtime_state.mirror_hover.position = {10.0f, 11.0f, 12.0f};
  runtime_state.poly.active = true;
  runtime_state.poly.closed = true;
  runtime_state.poly.has_current = true;
  runtime_state.poly.points = {
      {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};

  const ModelingToolStatus projected_status =
      ModelingOverlayService{}.tool_status(
          quader_editor_core::EditorRuntime(runtime_state), true);
  EXPECT_EQ(projected_status.active_tool, ModelingViewportTool::Cut);
  EXPECT_TRUE(projected_status.modal_tool_owns_viewport);
  EXPECT_TRUE(projected_status.cut_active);
  EXPECT_TRUE(projected_status.cut_dragging);
  EXPECT_TRUE(projected_status.cut_dragging_new_point);
  EXPECT_EQ(projected_status.cut_dragging_point_index, 0);
  EXPECT_EQ(projected_status.cut_mode, ModelingCutMode::DiscardRight);
  EXPECT_EQ(projected_status.cut_captured_point_count, 1);
  EXPECT_EQ(projected_status.cut_point_count, 2);
  EXPECT_TRUE(projected_status.cut_has_line);
  expect_vec3_eq(projected_status.cut_points[0], {1.0f, 2.0f, 3.0f});
  expect_vec3_eq(projected_status.cut_points[1], {4.0f, 5.0f, 6.0f});
  EXPECT_TRUE(projected_status.mirror_active);
  EXPECT_TRUE(projected_status.mirror_dragging);
  EXPECT_TRUE(projected_status.mirror_dragging_line);
  EXPECT_EQ(projected_status.mirror_captured_point_count, 1);
  EXPECT_EQ(projected_status.mirror_point_count, 2);
  EXPECT_TRUE(projected_status.mirror_has_line);
  expect_vec3_eq(projected_status.mirror_points[0], {7.0f, 8.0f, 9.0f});
  expect_vec3_eq(projected_status.mirror_points[1], {10.0f, 11.0f, 12.0f});
  EXPECT_TRUE(projected_status.poly_active);
  EXPECT_TRUE(projected_status.poly_closed);
  EXPECT_TRUE(projected_status.poly_has_current);
  EXPECT_TRUE(projected_status.poly_can_commit);
  EXPECT_EQ(projected_status.poly_point_count, 3);

  EXPECT_TRUE(ModalToolRuntime::owns_viewport(ModelingViewportTool::Box));
  EXPECT_TRUE(ModalToolRuntime::owns_viewport(ModelingViewportTool::Mirror));
  EXPECT_FALSE(ModalToolRuntime::owns_viewport(ModelingViewportTool::Move));
  for (const ToolDescriptor &descriptor : viewport_tool_descriptors()) {
    if (descriptor.editor_owned_native_lifecycle) {
      continue;
    }
    EXPECT_EQ(ModalToolRuntime::owns_viewport(descriptor.modeling_tool),
              descriptor.owns_viewport)
        << descriptor.id;
  }
  EXPECT_TRUE(ModelingMeshSyncService::should_force_refresh(true, false));
  EXPECT_TRUE(ModelingMeshSyncService::should_force_refresh(false, true));
  EXPECT_FALSE(ModelingMeshSyncService::should_force_refresh(false, false));
  EXPECT_FALSE(ModelingCommandExecutor::runtime_unavailable().ok);
  EXPECT_STREQ(
      fallback_tool_settings_label(ModelingToolSettingsTargetKind::EdgeBevel),
      "Bevel Edges");
  EXPECT_STREQ(
      fallback_tool_settings_label(ModelingToolSettingsTargetKind::Thicken),
      "Thicken");
  EXPECT_STREQ(
      fallback_tool_settings_label(ModelingToolSettingsTargetKind::HalfCut),
      "Half-Cut");
  EXPECT_STREQ(
      fallback_tool_settings_label(ModelingToolSettingsTargetKind::None),
      "Settings");
}

TEST(ModelingSessionServiceSplitTests,
     ContextActionServiceAppliesModalQueryState) {
  ModelingContextActionService service;
  ModelingActionQuery query;
  EXPECT_FALSE(query.modal_tool_owns_viewport);

  const ModelingActionQuery effective =
      service.effective_action_query(query, true);
  EXPECT_TRUE(effective.modal_tool_owns_viewport);

  query.suppress_when_modal_tool_owns_viewport = false;
  const ModelingActionQuery unsuppressed =
      service.effective_action_query(query, true);
  EXPECT_FALSE(unsuppressed.modal_tool_owns_viewport);

  ModelingSelectionSummary summary;
  summary.mode = ModelingSelectionMode::Object;
  summary.has_selection = true;
  summary.selected_object_count = 1;
  const ModelingActionSet action_set = service.action_set(summary, {}, false);
  EXPECT_EQ(action_set.surface, ModelingActionSurface::OperationsPanel);
  EXPECT_EQ(action_set.surface_id, "operations_top_bar");

  const ModelingContextActionResolution modal_result =
      service.resolve_executable_action(summary, "delete_selection", true);
  EXPECT_FALSE(modal_result.ok);
  EXPECT_EQ(modal_result.message, "Viewport is owned by an active modal tool.");

  ModelingSelectionSummary empty_summary;
  empty_summary.mode = ModelingSelectionMode::Object;
  const ModelingContextActionResolution disabled_result =
      service.resolve_executable_action(empty_summary, "delete_selection",
                                        false);
  EXPECT_FALSE(disabled_result.ok);
  EXPECT_EQ(disabled_result.message,
            "Action is disabled for the current selection.");
  EXPECT_EQ(disabled_result.action.disabled_reason_key,
            "modeling.action.disabled.delete_selection");

  const ModelingContextActionResolution enabled_result =
      service.resolve_executable_action(summary, "delete_selection", false);
  EXPECT_TRUE(enabled_result.ok);
  EXPECT_EQ(enabled_result.action.action_id, "delete_selection");
}

TEST(ModelingSessionServiceSplitTests,
     DuplicateMeshesSharesObjectOrGroupPolicyAcrossActionSetAndAvailability) {
  const ModelingInstantOperationCatalogEntry *duplicate =
      find_modeling_instant_operation_catalog_entry(
          ModelingInstantOperationId::DuplicateMeshes);
  ASSERT_NE(duplicate, nullptr);
  EXPECT_EQ(duplicate->availability.requirement,
            ModelingActionSelectionRequirement::ObjectOrGroupSelection);
  EXPECT_EQ(duplicate->availability.minimum_selected_objects, 1U);

  ModelingContextActionService actions;
  const ModelingActionQuery query{
      .surface = ModelingActionSurface::ViewportContextMenu,
      .include_disabled = true,
  };
  for (const ModelingSelectionMode mode : {ModelingSelectionMode::Object,
                                           ModelingSelectionMode::Group}) {
    SCOPED_TRACE(static_cast<int>(mode));
    const ModelingSelectionSummary selected{
        .mode = mode,
        .has_selection = true,
        .active_kind = ModelingActiveSelectionKind::Object,
        .selected_object_count = 1,
    };

    EXPECT_TRUE(modeling_instant_operation_enabled(*duplicate, selected));
    EXPECT_TRUE(ModelingActionCatalog::action_available(selected,
                                                        duplicate->id));
    const ModelingActionSet action_set =
        actions.action_set(selected, query, false);
    ASSERT_EQ(action_set.panels.size(), 1U);
    const auto item = std::find_if(
        action_set.panels.front().actions.begin(),
        action_set.panels.front().actions.end(),
        [&](const ModelingActionItem &candidate) {
          return candidate.action_id == duplicate->id;
        });
    ASSERT_NE(item, action_set.panels.front().actions.end());
    EXPECT_EQ(item->visibility, ModelingActionVisibility::Enabled);
    EXPECT_TRUE(actions.resolve_executable_action(selected, duplicate->id,
                                                  false)
                    .ok);
  }

  const ModelingSelectionSummary empty_group{
      .mode = ModelingSelectionMode::Group,
  };
  EXPECT_FALSE(modeling_instant_operation_enabled(*duplicate, empty_group));
  const std::optional<ModelingActionItem> disabled =
      ModelingActionCatalog::action_for(empty_group, duplicate->id);
  ASSERT_TRUE(disabled.has_value());
  EXPECT_EQ(disabled->visibility, ModelingActionVisibility::Disabled);
  EXPECT_EQ(disabled->disabled_reason_key,
            "modeling.action.disabled.minimum_object");
  EXPECT_EQ(disabled->disabled_reason_count, 1U);

  const ModelingSelectionSummary vertex_selection{
      .mode = ModelingSelectionMode::Vertex,
      .has_selection = true,
      .active_kind = ModelingActiveSelectionKind::Vertex,
      .selected_object_count = 1,
      .selected_vertex_count = 1,
  };
  EXPECT_FALSE(
      modeling_instant_operation_enabled(*duplicate, vertex_selection));
  const std::optional<ModelingActionItem> hidden =
      ModelingActionCatalog::action_for(vertex_selection, duplicate->id);
  ASSERT_TRUE(hidden.has_value());
  EXPECT_EQ(hidden->visibility, ModelingActionVisibility::Hidden);
}

TEST(ModelingSessionServiceSplitTests,
     InstantOperationEnablementAllowsConnectAcrossApplicableObjects) {
  const ModelingInstantOperationCatalogEntry *connect =
      find_modeling_instant_operation_catalog_entry(
          ModelingInstantOperationId::ConnectVertices);
  ASSERT_NE(connect, nullptr);
  const ModelingInstantOperationCatalogEntry *combine_faces =
      find_modeling_instant_operation_catalog_entry(
          ModelingInstantOperationId::CombineFaces);
  ASSERT_NE(combine_faces, nullptr);

  ModelingSelectionSummary summary;
  summary.mode = ModelingSelectionMode::Vertex;
  summary.has_selection = true;
  summary.active_kind = ModelingActiveSelectionKind::Vertex;
  summary.selected_vertex_count = 1;
  summary.selected_object_count = 1;

  EXPECT_FALSE(modeling_instant_operation_enabled(*connect, summary));

  summary.selected_vertex_count = 2;
  summary.selected_object_count = 2;

  EXPECT_TRUE(modeling_instant_operation_enabled(*connect, summary));

  summary.selected_object_count = 1;
  EXPECT_TRUE(modeling_instant_operation_enabled(*connect, summary));

  ModelingSelectionSummary face_summary;
  face_summary.mode = ModelingSelectionMode::Face;
  face_summary.has_selection = true;
  face_summary.selected_face_count = 1;
  face_summary.selected_object_count = 1;

  EXPECT_FALSE(
      modeling_instant_operation_enabled(*combine_faces, face_summary));

  face_summary.selected_face_count = 2;
  EXPECT_TRUE(modeling_instant_operation_enabled(*combine_faces, face_summary));
  face_summary.selected_object_count = 2;
  EXPECT_FALSE(modeling_instant_operation_enabled(*combine_faces, face_summary));
}

TEST(ModelingSessionServiceSplitTests,
     RadialAlignAvailabilityExcludesObjectAndGroupSelectionModes) {
  const ModelingInstantOperationCatalogEntry *radial_align =
      find_modeling_instant_operation_catalog_entry(
          ModelingInstantOperationId::RadialAlign);
  ASSERT_NE(radial_align, nullptr);

  ModelingSelectionSummary summary{
      .mode = ModelingSelectionMode::Group,
      .has_selection = true,
      .active_kind = ModelingActiveSelectionKind::Object,
      .selected_object_count = 1,
  };
  EXPECT_FALSE(modeling_instant_operation_enabled(*radial_align, summary));

  summary.mode = ModelingSelectionMode::Object;
  EXPECT_FALSE(modeling_instant_operation_enabled(*radial_align, summary));

  summary.mode = ModelingSelectionMode::Vertex;
  summary.active_kind = ModelingActiveSelectionKind::Vertex;
  summary.selected_vertex_count = 3;
  EXPECT_TRUE(modeling_instant_operation_enabled(*radial_align, summary));
}


TEST(ModelingSessionServiceSplitTests,
     InstantOperationSettingsComeFromSharedOperationSettingsOwnerExceptNormalBridge) {
  ModelingInteractionSettings interaction_settings;
  interaction_settings.bridge_steps = 4;

  const std::optional<quader_editor_core::EditorOperationCommand> bridge =
      modeling_instant_operation_command(ModelingInstantOperationId::Bridge,
                                         interaction_settings);
  ASSERT_TRUE(bridge.has_value());
  EXPECT_EQ(bridge->operation_id,
            quader_editor_core::EditorOperationId::Bridge);
  const auto &bridge_settings =
      std::get<quader_editor_core::BridgeOperationSettings>(bridge->settings);
  const auto interpolated_bridge =
      std::get<quader_editor_core::BridgeOperationSettings>(
          settings_for_operation(quader_editor_core::EditorOperationId::Bridge,
                                 interaction_settings));
  EXPECT_EQ(interpolated_bridge.steps, 4);
  EXPECT_EQ(bridge_settings.steps, 1);

  const std::optional<quader_editor_core::EditorOperationCommand> bevel =
      modeling_instant_operation_command(
          ModelingInstantOperationId::BevelVertices, interaction_settings);
  ASSERT_TRUE(bevel.has_value());
  EXPECT_TRUE(
      std::holds_alternative<
          quader_editor_core::BevelVerticesOperationSettings>(bevel->settings));

  const std::optional<quader_editor_core::EditorOperationCommand> recalculate =
      modeling_instant_operation_command(
          ModelingInstantOperationId::RecalculateFaceNormals,
          interaction_settings);
  ASSERT_TRUE(recalculate.has_value());
  const auto &recalculate_settings =
      std::get<quader_editor_core::RecalculateFaceNormalsOperationSettings>(
          recalculate->settings);
  EXPECT_TRUE(recalculate_settings.outside);
}

TEST(ModelingSessionServiceSplitTests,
     ToolSettingMutationComesFromOperationSettingsOwner) {
  ModelingInteractionSettings settings;

  EXPECT_TRUE(
      apply_modeling_interaction_setting(settings, "edge_bevel", "width", 2.5));
  EXPECT_DOUBLE_EQ(settings.edge_bevel_width, 2.5);

  EXPECT_TRUE(apply_modeling_interaction_setting(settings, "edge_bevel",
                                                 "edge_bevel_segments", 5.0));
  EXPECT_EQ(settings.edge_bevel_segments, 5);

  EXPECT_TRUE(apply_modeling_interaction_setting(
      settings, "edge_bevel", "operation.edge_bevel.profile", 0.25));
  EXPECT_DOUBLE_EQ(settings.edge_bevel_profile, 0.25);

  EXPECT_TRUE(apply_modeling_interaction_setting(settings, "thicken",
                                                 "from_center", 1.0));
  EXPECT_TRUE(settings.thicken_from_center);

  EXPECT_TRUE(
      apply_modeling_interaction_setting(settings, "", "distance", 0.0625));
  EXPECT_DOUBLE_EQ(settings.merge_by_distance_tolerance, 0.0625);

  EXPECT_TRUE(apply_modeling_interaction_setting(
      settings, "half_cut", "operation.half_cut.axis", 2.0));
  EXPECT_EQ(settings.half_cut_axis, ModelingHalfCutAxis::Z);
  EXPECT_TRUE(apply_modeling_interaction_setting(
      settings, "half_cut", "separate_parts", 1.0));
  EXPECT_TRUE(settings.half_cut_separate_parts);
  EXPECT_TRUE(apply_modeling_interaction_setting(
      settings, "half_cut", "fill_parts", 0.0));
  EXPECT_FALSE(settings.half_cut_fill_parts);

  const auto half_cut =
      std::get<quader_editor_core::HalfCutOperationSettings>(
          settings_for_operation(quader_editor_core::EditorOperationId::HalfCut,
                                 settings));
  EXPECT_EQ(half_cut.axis, quader_editor_core::HalfCutAxis::Z);
  EXPECT_TRUE(half_cut.separate_parts);
  EXPECT_FALSE(half_cut.fill_parts);

  EXPECT_FALSE(apply_modeling_interaction_setting(settings, "edge_bevel",
                                                  "tolerance", 0.25));
  EXPECT_FALSE(apply_modeling_interaction_setting(settings, "edge_bevel",
                                                  "__missing__", 1.0));
}

TEST(ModelingSessionServiceSplitTests,
     KnifeActivationEntersEdgeModeAndCancelAndCommitRetainIt) {
  constexpr std::array kModes{
      ModelingSelectionMode::Vertex,
      ModelingSelectionMode::Edge,
      ModelingSelectionMode::Face,
      ModelingSelectionMode::Object,
      ModelingSelectionMode::Group,
  };
  const auto knife_event = [](float y) {
    ModelingToolRayEvent event;
    event.ray = {
        .origin = {0.0f, y, 6.0f},
        .direction = {0.0f, 0.0f, -1.0f},
    };
    event.camera = {
        .orthographic = true,
        .origin = event.ray.origin,
        .viewport_size = {800.0f, 600.0f},
        .orthographic_size = 4.0,
    };
    event.grid_size = 0.5;
    event.vertex_radius = 0.001;
    event.edge_radius = 0.12;
    event.snap_enabled = false;
    return event;
  };
  const auto source_wire_segments = [](const ViewportOverlayFrame &frame,
                                       int object_id) {
    std::vector<std::array<float, 6U>> segments;
    for (const ViewportOverlayLine &line : frame.lines) {
      if (line.layer != ViewportOverlayLayer::SourceWire ||
          line.element.object_id != object_id) {
        continue;
      }
      segments.push_back({line.start.x, line.start.y, line.start.z, line.end.x,
                          line.end.y, line.end.z});
    }
    return segments;
  };
  const auto source_wire_depth_stamps = [](const ViewportOverlayFrame &frame,
                                            int object_id) {
    std::vector<std::array<float, 9U>> triangles;
    for (const ViewportOverlayTriangle &triangle : frame.triangles) {
      if (triangle.layer != ViewportOverlayLayer::SourceWireDepthStamp ||
          triangle.element.object_id != object_id) {
        continue;
      }
      triangles.push_back({triangle.a.x, triangle.a.y, triangle.a.z,
                           triangle.b.x, triangle.b.y, triangle.b.z,
                           triangle.c.x, triangle.c.y, triangle.c.z});
    }
    return triangles;
  };
  const auto tool_preview_line_count = [](const ViewportOverlayFrame &frame) {
    return std::ranges::count_if(
        frame.lines, [](const ViewportOverlayLine &line) {
          return line.source == ViewportOverlaySource::Tool &&
                 line.layer == ViewportOverlayLayer::ToolPreviewLine;
        });
  };
  const auto tool_handle_count = [](const ViewportOverlayFrame &frame) {
    return std::ranges::count_if(
        frame.points, [](const ViewportOverlayPoint &point) {
          return point.source == ViewportOverlaySource::Tool &&
                 point.layer == ViewportOverlayLayer::ToolHandle;
        });
  };

  for (const ModelingSelectionMode mode : kModes) {
    SCOPED_TRACE(static_cast<int>(mode));
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);
    const ModelingCreateBoxResult box = service.create_box_from_bounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f},
                   .max = {1.0f, 1.0f, 1.0f}},
        .name = "Knife Persistent Mode Source",
    });
    ASSERT_TRUE(box.ok) << box.message;
    ASSERT_TRUE(service.set_selection_mode(mode).ok);

    ModelingCommandResult command =
        service.activate_viewport_tool(ModelingViewportTool::Knife);
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_EQ(service.selection_mode(), ModelingSelectionMode::Edge);
    EXPECT_EQ(service.selection_summary().mode, ModelingSelectionMode::Edge);

    const int object_id = box.object.modeling_object_id;
    const ViewportOverlayFrame activated_persistent =
        service.selection_overlay_frame();
    const auto activated_source_wire =
        source_wire_segments(activated_persistent, object_id);
    const auto activated_depth_stamps =
        source_wire_depth_stamps(activated_persistent, object_id);
    EXPECT_EQ(activated_source_wire.size(), 12U);
    EXPECT_FALSE(activated_depth_stamps.empty());
    const std::uint64_t activated_signature =
        service.selection_overlay_signature();

    command = service.update_knife_hover(knife_event(0.0f));
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_FALSE(service.tool_status().knife_has_current);
    EXPECT_TRUE(std::ranges::none_of(
        service.volatile_overlay_frame().points,
        [](const ViewportOverlayPoint &point) {
          return point.source == ViewportOverlaySource::Tool &&
                 point.layer == ViewportOverlayLayer::ToolHandle;
        }));
    const ViewportOverlayFrame missed_persistent =
        service.selection_overlay_frame();
    EXPECT_EQ(source_wire_segments(missed_persistent, object_id),
              activated_source_wire);
    EXPECT_EQ(source_wire_depth_stamps(missed_persistent, object_id),
              activated_depth_stamps);
    EXPECT_EQ(service.selection_overlay_signature(), activated_signature);

    command = service.update_knife_hover(knife_event(-1.0f));
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_TRUE(service.tool_status().knife_has_current);
    EXPECT_TRUE(std::ranges::any_of(
        service.volatile_overlay_frame().points,
        [](const ViewportOverlayPoint &point) {
          return point.source == ViewportOverlaySource::Tool &&
                 point.layer == ViewportOverlayLayer::ToolHandle &&
                 length_squared(point.position - QVec3{0.0f, -1.0f, 1.0f}) <
                     0.0001f;
        }));

    command = service.cancel_active_modal_tool();
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_EQ(service.selection_mode(), ModelingSelectionMode::Edge);

    command = service.activate_viewport_tool(ModelingViewportTool::Knife);
    ASSERT_TRUE(command.ok) << command.message;
    ASSERT_EQ(service.selection_mode(), ModelingSelectionMode::Edge);
    command = service.accept_knife_point(knife_event(-1.0f));
    ASSERT_TRUE(command.ok) << command.message;
    ASSERT_TRUE(command.changed);
    command = service.accept_knife_point(knife_event(1.0f));
    ASSERT_TRUE(command.ok) << command.message;
    ASSERT_TRUE(command.changed);
    ASSERT_TRUE(service.tool_status().knife_can_commit);

    const ViewportOverlayFrame locked_persistent =
        service.selection_overlay_frame();
    const auto locked_source_wire =
        source_wire_segments(locked_persistent, object_id);
    const auto locked_depth_stamps =
        source_wire_depth_stamps(locked_persistent, object_id);
    ASSERT_EQ(locked_source_wire.size(), 12U);
    ASSERT_FALSE(locked_depth_stamps.empty());
    const std::uint64_t locked_signature =
        service.selection_overlay_signature();

    command = service.update_knife_hover(knife_event(0.0f));
    ASSERT_TRUE(command.ok) << command.message;
    ASSERT_TRUE(service.tool_status().knife_has_current);
    const ViewportOverlayFrame valid_hover =
        service.volatile_overlay_frame();
    EXPECT_EQ(tool_preview_line_count(valid_hover), 2);
    EXPECT_EQ(tool_handle_count(valid_hover), 3);

    command = service.update_knife_hover(knife_event(3.0f));
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_FALSE(service.tool_status().knife_has_current);
    const ViewportOverlayFrame invalid_hover =
        service.volatile_overlay_frame();
    EXPECT_EQ(tool_preview_line_count(invalid_hover), 1);
    EXPECT_EQ(tool_handle_count(invalid_hover), 2);
    EXPECT_TRUE(std::ranges::none_of(
        invalid_hover.lines, [](const ViewportOverlayLine &line) {
          return line.source == ViewportOverlaySource::ComponentHover;
        }));

    const ViewportOverlayFrame invalid_hover_persistent =
        service.selection_overlay_frame();
    EXPECT_EQ(source_wire_segments(invalid_hover_persistent, object_id),
              locked_source_wire);
    EXPECT_EQ(source_wire_depth_stamps(invalid_hover_persistent, object_id),
              locked_depth_stamps);
    EXPECT_EQ(service.selection_overlay_signature(), locked_signature);
    EXPECT_EQ(service.selection_mode(), ModelingSelectionMode::Edge);

    command = service.commit_knife_tool();
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_TRUE(command.changed);
    EXPECT_EQ(service.selection_mode(), ModelingSelectionMode::Edge);
    EXPECT_EQ(service.selection_summary().mode, ModelingSelectionMode::Edge);
  }
}

TEST(ModelingSessionServiceSplitTests,
     KnifeReactivationPreservesIncompleteAndCompletePendingStrokes) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto box = editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      "Retained knife draft");
  ASSERT_TRUE(box.ok()) << box.error_message();
  ASSERT_TRUE(editor.selection().select_object(box.value()).ok());
  ASSERT_TRUE(editor.mode().set(QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::Knife).ok());
  for (const float y : {-1.0f, 1.0f}) {
    SCOPED_TRACE(y);
    ModelingToolRayEvent event;
    event.ray = {.origin = {0.0f, y, 6.0f}, .direction = {0.0f, 0.0f, -1.0f}};
    event.camera = {.orthographic = true, .origin = event.ray.origin,
                    .viewport_size = {800.0f, 600.0f}, .orthographic_size = 4.0};
    event.vertex_radius = 0.001;
    event.edge_radius = 0.12;
    event.snap_enabled = false;
    ASSERT_TRUE(session.modeling().accept_knife_point(event).ok);
    ASSERT_TRUE(session.modeling().pending_operation_active());
    EXPECT_EQ(editor.query().tool_status().knife_can_commit, y > 0.0f);
    const auto before = test::capture_command_observable_state(editor);
    const auto repeated = editor.tools().activate(ViewportToolId::Knife);
    ASSERT_TRUE(repeated.ok()) << repeated.error_message();
    EXPECT_FALSE(repeated.changed());
    const auto difference = test::diff_command_observable_state(
        before, test::capture_command_observable_state(editor));
    EXPECT_TRUE(difference.empty()) << difference.summary;
    EXPECT_TRUE(session.modeling().pending_operation_active());
    EXPECT_EQ(editor.query().tool_status().knife_can_commit, y > 0.0f);
  }
}

TEST(ModelingSessionServiceSplitTests,
     KnifePrelockSourceWireCacheRetainsAllEditParticipants) {
  SceneSession session;
  ModelingSessionService &service = session.modeling();
  const ModelingCreateBoxResult first = service.create_box_from_bounds({
      .bounds = {.min = {-3.0f, -1.0f, -1.0f},
                 .max = {-1.0f, 1.0f, 1.0f}},
      .name = "Knife First Selected Source",
  });
  const ModelingCreateBoxResult second = service.create_box_from_bounds({
      .bounds = {.min = {1.0f, -1.0f, -1.0f},
                 .max = {3.0f, 1.0f, 1.0f}},
      .name = "Knife Second Selected Source",
  });
  ASSERT_TRUE(first.ok) << first.message;
  ASSERT_TRUE(second.ok) << second.message;
  const std::array selected{first.object.entity, second.object.entity};
  ASSERT_TRUE(service.set_object_selection(selected, first.object.entity).ok);
  ASSERT_TRUE(session.editor_mode().set_mode(EditorMode::Edit).ok);
  ASSERT_TRUE(
      service.activate_viewport_tool(ModelingViewportTool::Knife).ok);

  const auto source_wire_count = [](const ViewportOverlayFrame &frame,
                                    int object_id) {
    return std::ranges::count_if(
        frame.lines, [object_id](const ViewportOverlayLine &line) {
          return line.layer == ViewportOverlayLayer::SourceWire &&
                 line.element.object_id == object_id;
        });
  };
  const std::uint64_t first_signature =
      service.selection_overlay_signature();
  const ViewportOverlayFrame first_frame = service.selection_overlay_frame();
  EXPECT_EQ(source_wire_count(first_frame,
                              first.object.modeling_object_id),
            12);
  EXPECT_EQ(source_wire_count(first_frame,
                              second.object.modeling_object_id),
            12);

  const ModelingCommandResult switched =
      service.set_object_selection(selected, second.object.entity);
  EXPECT_FALSE(switched.ok);
  EXPECT_FALSE(switched.changed);
  ASSERT_EQ(session.editor_mode().snapshot().participants.size(), 2u);
  const std::uint64_t second_signature =
      service.selection_overlay_signature();
  EXPECT_EQ(second_signature, first_signature);
  const ViewportOverlayFrame second_frame = service.selection_overlay_frame();
  EXPECT_EQ(source_wire_count(second_frame,
                              first.object.modeling_object_id),
            12);
  EXPECT_EQ(source_wire_count(second_frame,
                              second.object.modeling_object_id),
            12);
}



TEST(ModelingSessionServiceSplitTests,
     FacadeDelegatesToSplitServicesThroughPublicApi) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService facade(scene, assets);

  EXPECT_TRUE(facade.objects().empty());
  EXPECT_EQ(facade.selection_mode(), ModelingSelectionMode::Object);
  EXPECT_FALSE(facade.selection_summary().has_selection);
  EXPECT_FALSE(facade.modal_tool_owns_viewport());

  const ModelingToolSettingsTarget settings = facade.tool_settings_target();
  EXPECT_FALSE(settings.active);
  EXPECT_EQ(facade.context_action_set().surface_id, "operations_top_bar");
}

TEST(ModelingSessionServiceSplitTests,
     MeshUvProjectionEligibilityReusesTargetsUntilOwnerRevisionsChange) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);
  const auto created = service.create_box_from_bounds({
      .bounds = {.min = {-1, -1, -1}, .max = {1, 1, 1}},
      .name = "Cached UV projection targets",
  });
  ASSERT_TRUE(created.ok) << created.message;
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Object).ok);
  const std::array selected{created.object.entity};
  ASSERT_TRUE(service.set_object_selection(selected).ok);
  ASSERT_TRUE(service.can_project_uv());
  const auto before = service.modeling_sdk_projection_stats_for_tests();
  for (int frame = 0; frame < 20; ++frame) {
    EXPECT_TRUE(service.can_project_uv());
  }
  const auto after = service.modeling_sdk_projection_stats_for_tests();
  EXPECT_EQ(after.face_texture_summary_build_count, before.face_texture_summary_build_count);
  EXPECT_EQ(after.mesh_compile_count, before.mesh_compile_count);
  EXPECT_EQ(after.asset_publication_count, before.asset_publication_count);
  EXPECT_EQ(after.selection_summary_build_count, before.selection_summary_build_count);
  ASSERT_TRUE(scene.set_locked(created.object.entity, true));
  EXPECT_FALSE(service.can_project_uv());
  const auto locked = service.modeling_sdk_projection_stats_for_tests();
  for (int frame = 0; frame < 20; ++frame) {
    EXPECT_FALSE(service.can_project_uv());
  }
  EXPECT_EQ(service.modeling_sdk_projection_stats_for_tests().face_texture_summary_build_count,
            locked.face_texture_summary_build_count);
  ASSERT_TRUE(scene.set_locked(created.object.entity, false));
  ASSERT_TRUE(service.set_object_selection(selected).ok);
  EXPECT_TRUE(service.can_project_uv());
  const auto reusable = service.modeling_sdk_projection_stats_for_tests();
  const auto projected = service.apply_face_texture_action({
      .options = {.action = ModelingFaceTextureAction::ProjectCube},
      .selection_token = service.selection_revision(),
  });
  ASSERT_TRUE(projected.ok) << projected.message;
  EXPECT_EQ(service.modeling_sdk_projection_stats_for_tests().face_texture_summary_build_count,
            reusable.face_texture_summary_build_count);
  EXPECT_TRUE(service.can_project_uv());
  EXPECT_GT(service.modeling_sdk_projection_stats_for_tests().face_texture_summary_build_count,
            reusable.face_texture_summary_build_count);
}

TEST(ModelingSessionServiceSplitTests,
     FaceTextureSummaryIsRevisionGatedAndUsesTexturePixelDimensions) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);
  const auto created = service.create_box_from_bounds({
      .bounds = {.min = {-1, -1, -1}, .max = {1, 1, 1}},
      .name = "Pixel Texture Settings",
  });
  ASSERT_TRUE(created.ok) << created.message;
  TextureAsset texture;
  texture.name = "Reference texture";
  texture.width = 256;
  texture.height = 128;
  const AssetId texture_id = assets.add_texture(std::move(texture));
  MaterialDocument material = make_default_quader_material("Pixel reference");
  ASSERT_TRUE(set_material_texture_slot(material, material_texture_slot::kBaseColor,
                                       texture_id));
  const AssetId material_id = assets.add_material(std::move(material));
  ASSERT_TRUE(scene.set_material(created.object.entity, {.material = material_id}));
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Face).ok);
  ASSERT_TRUE(service.set_component_selection({
      .modeling_object_id = created.object.modeling_object_id,
      .mode = ModelingSelectionMode::Face,
      .faces = {1},
      .active_kind = ModelingActiveSelectionKind::Face,
      .active_face = 1,
  }).ok);
  const auto summary = service.face_texture_settings();
  ASSERT_TRUE(summary.editable);
  ASSERT_TRUE(service.set_face_texture_setting({
      .field = ModelingFaceTextureField::ShiftX,
      .value = 128,
      .selection_token = summary.selection_token,
  }).ok);
  EXPECT_DOUBLE_EQ(service.face_texture_settings().shift_x, 128);
  const MeshAsset *mesh = assets.mesh(scene.mesh(created.object.entity)->mesh);
  ASSERT_NE(mesh, nullptr);
  ASSERT_NE(mesh->authored_polygon_document, nullptr);
  const auto *face = quader_poly::find_face(*mesh->authored_polygon_document, 1);
  ASSERT_NE(face, nullptr);
  EXPECT_FLOAT_EQ(face->texture_transform.shift_uv.x, 0.5f);

  const auto before_queries = service.modeling_sdk_projection_stats_for_tests();
  const auto revision = service.face_texture_settings_revision();
  for (int index = 0; index < 20; ++index) {
    EXPECT_EQ(service.face_texture_settings_revision(), revision);
    EXPECT_DOUBLE_EQ(service.face_texture_settings().shift_x, 128);
  }
  const auto after_queries = service.modeling_sdk_projection_stats_for_tests();
  EXPECT_EQ(after_queries.face_texture_summary_build_count,
            before_queries.face_texture_summary_build_count);
  EXPECT_EQ(after_queries.mesh_compile_count, before_queries.mesh_compile_count);
  EXPECT_EQ(after_queries.asset_publication_count, before_queries.asset_publication_count);

  ASSERT_TRUE(assets.update_texture(texture_id, [](TextureAsset &updated) {
    updated.width = 512;
  }));
  EXPECT_DOUBLE_EQ(service.face_texture_settings().shift_x, 256);
  const auto before_noop = service.content_revision();
  const auto unchanged = service.set_face_texture_setting({
      .field = ModelingFaceTextureField::ShiftX,
      .value = 256,
      .selection_token = service.face_texture_settings().selection_token,
  });
  EXPECT_TRUE(unchanged.ok);
  EXPECT_FALSE(unchanged.changed);
  EXPECT_EQ(service.content_revision(), before_noop);

  ASSERT_TRUE(service.move_selection({0.1f, 0.0f, 0.0f}).ok);
  EXPECT_DOUBLE_EQ(service.face_texture_settings().shift_x, 256);
  ASSERT_TRUE(scene.set_locked(created.object.entity, true));
  const auto locked = service.face_texture_settings();
  EXPECT_EQ(locked.selected_face_count, 1u);
  EXPECT_FALSE(locked.editable);
  EXPECT_EQ(locked.block_reason, ModelingFaceTextureBlockReason::TargetUnavailable);
  EXPECT_FALSE(service.set_face_texture_setting({
      .field = ModelingFaceTextureField::ScaleX,
      .value = 3,
      .selection_token = locked.selection_token,
  }).ok);
}

TEST(ModelingSessionServiceSplitTests,
     FaceTextureBatchRollsBackSharedBindingsAndPublishesAllObjectsAtomically) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);
  const auto first = service.create_box_from_bounds({
      .bounds = {.min = {-1, -1, -1}, .max = {1, 1, 1}}, .name = "First",
  });
  const auto second = service.create_box_from_bounds({
      .bounds = {.min = {3, -1, -1}, .max = {5, 1, 1}}, .name = "Second",
  });
  ASSERT_TRUE(first.ok);
  ASSERT_TRUE(second.ok);
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Face).ok);
  for (const auto *created : {&first, &second}) {
    ASSERT_TRUE(service.set_component_selection({
        .modeling_object_id = created->object.modeling_object_id,
        .mode = ModelingSelectionMode::Face,
        .faces = {1},
        .edit = created == &first ? ModelingSelectionEdit::Replace
                                 : ModelingSelectionEdit::Add,
        .active_kind = ModelingActiveSelectionKind::Face,
        .active_face = 1,
    }).ok);
  }
  const AssetId shared_mesh = scene.mesh(first.object.entity)->mesh;
  const EntityId shared_entity = scene.create_entity("Shared consumer", EntityKind::Mesh);
  ASSERT_TRUE(scene.set_mesh(shared_entity, {.mesh = shared_mesh}));
  const auto summary = service.face_texture_settings();
  ASSERT_EQ(summary.selected_face_count, 2u);
  const auto before_mesh_ids = assets.mesh_ids();
  const auto before_scene_revision = scene.revision();
  const auto before_asset_revision = assets.revision();
  const auto before_content_revision = service.content_revision();
  const auto before_selection_revision = service.selection_revision();

  service.set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::CommittedSceneNotify);
  const auto failed = service.set_face_texture_setting({
      .field = ModelingFaceTextureField::ScaleY,
      .value = 4,
      .selection_token = summary.selection_token,
  });
  EXPECT_FALSE(failed.ok);
  EXPECT_FALSE(failed.changed);
  EXPECT_EQ(scene.revision(), before_scene_revision);
  EXPECT_EQ(assets.revision(), before_asset_revision);
  EXPECT_EQ(assets.mesh_ids(), before_mesh_ids);
  EXPECT_EQ(service.content_revision(), before_content_revision);
  EXPECT_EQ(service.selection_revision(), before_selection_revision);
  EXPECT_EQ(scene.mesh(first.object.entity)->mesh, shared_mesh);
  EXPECT_EQ(scene.mesh(shared_entity)->mesh, shared_mesh);
  EXPECT_EQ(service.face_texture_settings().scale_y, 1.0);

  service.set_modeling_sdk_projection_failure_for_tests(ModelingSdkProjectionFailurePoint::None);
  const auto edited = service.set_face_texture_setting({
      .field = ModelingFaceTextureField::ScaleY,
      .value = 4,
      .selection_token = summary.selection_token,
  });
  ASSERT_TRUE(edited.ok) << edited.message;
  EXPECT_TRUE(edited.changed);
  EXPECT_EQ(service.content_revision(), before_content_revision + 1u);
  EXPECT_EQ(service.selection_revision(), before_selection_revision);
  EXPECT_EQ(service.face_texture_settings().selected_face_count, 2u);
  EXPECT_EQ(service.face_texture_settings().scale_y, 4.0);
  EXPECT_NE(scene.mesh(first.object.entity)->mesh, shared_mesh);
  EXPECT_EQ(scene.mesh(shared_entity)->mesh, shared_mesh);
  const auto *untouched = quader_poly::find_face(
      *assets.mesh(shared_mesh)->authored_polygon_document, 1);
  ASSERT_NE(untouched, nullptr);
  EXPECT_FLOAT_EQ(untouched->texture_transform.scale.y, 1);
}

TEST(ModelingSessionServiceSplitTests,
     FaceTextureActionShiftUsesPerFaceExtentsAndRepeatReadsCurrentTextures) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);
  const auto created = service.create_box_from_bounds({
      .bounds = {.min = {-1, -1, -1}, .max = {1, 1, 1}},
      .name = "Mixed Texture Dimensions",
  });
  ASSERT_TRUE(created.ok) << created.message;
  constexpr std::array kWidths{256u, 128u, 64u};
  std::array<AssetId, 2> texture_ids;
  for (std::size_t index = 0; index < texture_ids.size(); ++index) {
    TextureAsset texture;
    texture.name = "Pixel step source";
    texture.width = kWidths[index];
    texture.height = 128;
    texture_ids[index] = assets.add_texture(std::move(texture));
    MaterialDocument material = make_default_quader_material("Pixel step material");
    ASSERT_TRUE(set_material_texture_slot(
        material, material_texture_slot::kBaseColor, texture_ids[index]));
    const AssetId material_id = assets.add_material(std::move(material));
    const auto assigned = service.apply_material_to_face(
        created.object.modeling_object_id, static_cast<int>(index) + 1,
        material_id);
    ASSERT_TRUE(assigned.ok) << assigned.message;
  }
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Face).ok);
  ASSERT_TRUE(service.set_component_selection({
      .modeling_object_id = created.object.modeling_object_id,
      .mode = ModelingSelectionMode::Face,
      .faces = {1, 2, 3},
      .active_kind = ModelingActiveSelectionKind::Face,
      .active_face = 1,
  }).ok);
  const auto shifted = service.apply_face_texture_action({
      .options = {.action = ModelingFaceTextureAction::ShiftRight,
                  .treat_as_one = true},
      .selection_token = service.face_texture_settings().selection_token,
  });
  ASSERT_TRUE(shifted.ok) << shifted.message;
  ASSERT_TRUE(shifted.changed);
  const auto *mesh = assets.mesh(scene.mesh(created.object.entity)->mesh);
  ASSERT_NE(mesh, nullptr);
  ASSERT_NE(mesh->authored_polygon_document, nullptr);
  for (std::size_t index = 0; index < kWidths.size(); ++index) {
    SCOPED_TRACE(index);
    const auto *face = quader_poly::find_face(
        *mesh->authored_polygon_document, static_cast<int>(index) + 1);
    ASSERT_NE(face, nullptr);
    EXPECT_FLOAT_EQ(face->texture_transform.shift_uv.x,
                    -1.0f / static_cast<float>(kWidths[index]));
    EXPECT_FLOAT_EQ(face->texture_transform.shift_uv.y, 0.0f);
  }

  ASSERT_TRUE(assets.update_texture(texture_ids[0], [](TextureAsset &texture) {
    texture.width = 512;
  }));
  ASSERT_TRUE(assets.update_texture(texture_ids[1], [](TextureAsset &texture) {
    texture.width = 32;
  }));
  const auto repeated = service.repeat_last_action();
  ASSERT_TRUE(repeated.ok) << repeated.message;
  ASSERT_TRUE(repeated.changed);
  constexpr std::array kRepeatedWidths{512u, 32u, 64u};
  mesh = assets.mesh(scene.mesh(created.object.entity)->mesh);
  ASSERT_NE(mesh, nullptr);
  ASSERT_NE(mesh->authored_polygon_document, nullptr);
  for (std::size_t index = 0; index < kWidths.size(); ++index) {
    SCOPED_TRACE(index);
    const auto *face = quader_poly::find_face(
        *mesh->authored_polygon_document, static_cast<int>(index) + 1);
    ASSERT_NE(face, nullptr);
    EXPECT_FLOAT_EQ(face->texture_transform.shift_uv.x,
                    -1.0f / static_cast<float>(kWidths[index]) -
                        1.0f / static_cast<float>(kRepeatedWidths[index]));
  }
  EXPECT_EQ(service.face_texture_settings().selected_face_count, 3u);
  const auto before_numeric_shift = service.content_revision();
  EXPECT_FALSE(service.set_face_texture_setting({
      .field = ModelingFaceTextureField::ShiftX,
      .value = 8.0,
      .selection_token = service.face_texture_settings().selection_token,
  }).ok);
  EXPECT_EQ(service.content_revision(), before_numeric_shift);
}

TEST(ModelingSessionServiceSplitTests,
     FaceTextureActionBatchFailurePreservesSharedMeshesAndAllRevisions) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);
  const auto first = service.create_box_from_bounds({
      .bounds = {.min = {-1, -1, -1}, .max = {1, 1, 1}}, .name = "First",
  });
  const auto second = service.create_box_from_bounds({
      .bounds = {.min = {3, -1, -1}, .max = {5, 1, 1}}, .name = "Second",
  });
  ASSERT_TRUE(first.ok);
  ASSERT_TRUE(second.ok);
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Face).ok);
  for (const auto *created : {&first, &second}) {
    ASSERT_TRUE(service.set_component_selection({
        .modeling_object_id = created->object.modeling_object_id,
        .mode = ModelingSelectionMode::Face,
        .faces = {1},
        .edit = created == &first ? ModelingSelectionEdit::Replace
                                 : ModelingSelectionEdit::Add,
        .active_kind = ModelingActiveSelectionKind::Face,
        .active_face = 1,
    }).ok);
  }
  const AssetId shared_mesh = scene.mesh(first.object.entity)->mesh;
  const AssetId second_mesh = scene.mesh(second.object.entity)->mesh;
  const EntityId shared_entity =
      scene.create_entity("Unselected shared consumer", EntityKind::Mesh);
  ASSERT_TRUE(scene.set_mesh(shared_entity, {.mesh = shared_mesh}));
  const auto before_first = assets.mesh(shared_mesh)->authored_polygon_document;
  const auto before_second = assets.mesh(second_mesh)->authored_polygon_document;
  const auto before_mesh_ids = assets.mesh_ids();
  const auto before_scene_revision = scene.revision();
  const auto before_asset_revision = assets.revision();
  const auto before_content_revision = service.content_revision();
  const auto before_selection_revision = service.selection_revision();
  const ModelingFaceTextureActionRequest request{
      .options = {.action = ModelingFaceTextureAction::ScaleYUp},
      .selection_token = service.face_texture_settings().selection_token,
  };

  service.set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::CommittedSceneNotify);
  const auto failed = service.apply_face_texture_action(request);
  EXPECT_FALSE(failed.ok);
  EXPECT_FALSE(failed.changed);
  EXPECT_EQ(scene.revision(), before_scene_revision);
  EXPECT_EQ(assets.revision(), before_asset_revision);
  EXPECT_EQ(assets.mesh_ids(), before_mesh_ids);
  EXPECT_EQ(service.content_revision(), before_content_revision);
  EXPECT_EQ(service.selection_revision(), before_selection_revision);
  EXPECT_EQ(scene.mesh(first.object.entity)->mesh, shared_mesh);
  EXPECT_EQ(scene.mesh(second.object.entity)->mesh, second_mesh);
  EXPECT_EQ(scene.mesh(shared_entity)->mesh, shared_mesh);
  EXPECT_EQ(assets.mesh(shared_mesh)->authored_polygon_document, before_first);
  EXPECT_EQ(assets.mesh(second_mesh)->authored_polygon_document, before_second);

  service.set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::None);
  const auto edited = service.apply_face_texture_action(request);
  ASSERT_TRUE(edited.ok) << edited.message;
  EXPECT_TRUE(edited.changed);
  EXPECT_EQ(service.content_revision(), before_content_revision + 1u);
  EXPECT_EQ(service.selection_revision(), before_selection_revision);
  EXPECT_NE(scene.mesh(first.object.entity)->mesh, shared_mesh);
  EXPECT_EQ(scene.mesh(shared_entity)->mesh, shared_mesh);
  for (const EntityId entity : {first.object.entity, second.object.entity}) {
    const auto *face = quader_poly::find_face(
        *assets.mesh(scene.mesh(entity)->mesh)->authored_polygon_document, 1);
    ASSERT_NE(face, nullptr);
    EXPECT_FLOAT_EQ(face->texture_transform.scale.y, 2.0f);
  }
  const auto *untouched = quader_poly::find_face(*before_first, 1);
  ASSERT_NE(untouched, nullptr);
  EXPECT_FLOAT_EQ(untouched->texture_transform.scale.y, 1.0f);
}

TEST(ModelingSessionServiceSplitTests,
     UvPeelLatePublicationFailurePreservesSharedBindingsAndAllObjects) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);
  const auto first = service.create_mesh({
      .document = service_uv_peel_strip_document(), .name = "First strip",
  });
  const auto second = service.create_mesh({
      .document = service_uv_peel_strip_document(), .name = "Second strip",
  });
  ASSERT_TRUE(first.ok) << first.message;
  ASSERT_TRUE(second.ok) << second.message;
  for (const auto *created : {&first, &second}) {
    const auto selected = service.set_component_selection({
        .modeling_object_id = created->object.modeling_object_id,
        .mode = ModelingSelectionMode::Edge,
        .edges = {{2, 5}},
        .edit = created == &first ? ModelingSelectionEdit::Replace
                                 : ModelingSelectionEdit::Add,
        .active_kind = ModelingActiveSelectionKind::Edge,
        .active_edge = {2, 5},
    });
    ASSERT_TRUE(selected.ok) << selected.message;
  }
  const AssetId shared_mesh = scene.mesh(first.object.entity)->mesh;
  const AssetId second_mesh = scene.mesh(second.object.entity)->mesh;
  const EntityId shared_entity =
      scene.create_entity("Unselected shared consumer", EntityKind::Mesh);
  ASSERT_TRUE(scene.set_mesh(shared_entity, {.mesh = shared_mesh}));
  const auto before_first = assets.mesh(shared_mesh)->authored_polygon_document;
  const auto before_second = assets.mesh(second_mesh)->authored_polygon_document;
  const auto settings = service.uv_peel_settings();
  ASSERT_TRUE(settings.editable);
  ASSERT_EQ(settings.selected_edge_count, 2u);
  const auto before_queries = service.modeling_sdk_projection_stats_for_tests();
  const auto settings_revision = service.uv_peel_settings_revision();
  for (int index = 0; index < 20; ++index) {
    EXPECT_EQ(service.uv_peel_settings_revision(), settings_revision);
    EXPECT_EQ(service.uv_peel_settings().selection_token, settings.selection_token);
  }
  const auto after_queries = service.modeling_sdk_projection_stats_for_tests();
  EXPECT_EQ(after_queries.uv_peel_summary_build_count,
            before_queries.uv_peel_summary_build_count);
  EXPECT_EQ(after_queries.mesh_compile_count, before_queries.mesh_compile_count);
  EXPECT_EQ(after_queries.asset_publication_count,
            before_queries.asset_publication_count);

  const auto before_mesh_ids = assets.mesh_ids();
  const auto before_scene_revision = scene.revision();
  const auto before_asset_revision = assets.revision();
  const auto before_content_revision = service.content_revision();
  const auto before_selection_revision = service.selection_revision();
  const ModelingUvPeelRequest request{
      .options = {.world_space = false, .u_offset = 0.25, .v_offset = -0.5},
      .selection_token = settings.selection_token,
  };
  service.set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::CommittedSceneNotify);
  const auto failed = service.peel_uv(request);
  EXPECT_FALSE(failed.ok);
  EXPECT_FALSE(failed.changed);
  EXPECT_EQ(scene.revision(), before_scene_revision);
  EXPECT_EQ(assets.revision(), before_asset_revision);
  EXPECT_EQ(assets.mesh_ids(), before_mesh_ids);
  EXPECT_EQ(service.content_revision(), before_content_revision);
  EXPECT_EQ(service.selection_revision(), before_selection_revision);
  EXPECT_EQ(scene.mesh(first.object.entity)->mesh, shared_mesh);
  EXPECT_EQ(scene.mesh(second.object.entity)->mesh, second_mesh);
  EXPECT_EQ(scene.mesh(shared_entity)->mesh, shared_mesh);
  EXPECT_EQ(assets.mesh(shared_mesh)->authored_polygon_document, before_first);
  EXPECT_EQ(assets.mesh(second_mesh)->authored_polygon_document, before_second);
  EXPECT_EQ(service.uv_peel_settings().selection_token, settings.selection_token);

  service.set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::None);
  const auto peeled = service.peel_uv(request);
  ASSERT_TRUE(peeled.ok) << peeled.message;
  EXPECT_TRUE(peeled.changed);
  EXPECT_EQ(service.content_revision(), before_content_revision + 1u);
  EXPECT_EQ(service.selection_revision(), before_selection_revision);
  EXPECT_NE(scene.mesh(first.object.entity)->mesh, shared_mesh);
  EXPECT_EQ(scene.mesh(shared_entity)->mesh, shared_mesh);
  EXPECT_EQ(assets.mesh(shared_mesh)->authored_polygon_document, before_first);
  EXPECT_EQ(service.uv_peel_settings().selected_edge_count, 2u);
}

TEST(ModelingSessionServiceSplitTests,
     UvPeelPreviewPreservesAnUnrelatedCollapsedAuthoredFace) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);
  auto document = service_uv_peel_strip_document();
  const std::array vertices{
      quader_poly::add_vertex(document, {4.0f, 0.0f, 0.0f}),
      quader_poly::add_vertex(document, {5.0f, 0.0f, 0.0f}),
      quader_poly::add_vertex(document, {6.0f, 0.0f, 0.0f}),
  };
  const int collapsed_id = quader_poly::add_face(document, vertices, 3u);
  auto *collapsed = quader_poly::find_face(document, collapsed_id);
  ASSERT_NE(collapsed, nullptr);
  collapsed->uvs = {{0.2f, 0.8f}, {0.7f, 0.4f}, {0.9f, 0.1f}};
  const auto original_face = *collapsed;
  const auto mesh = service.create_mesh({
      .document = std::move(document), .name = "Strip with collapsed face",
  });
  ASSERT_TRUE(mesh.ok) << mesh.message;
  ASSERT_TRUE(service.set_component_selection({
      .modeling_object_id = mesh.object.modeling_object_id,
      .mode = ModelingSelectionMode::Edge,
      .edges = {{2, 5}},
      .active_kind = ModelingActiveSelectionKind::Edge,
      .active_edge = {2, 5},
  }).ok);
  ASSERT_TRUE(service.refresh_uv_peel_preview().ok);
  ASSERT_TRUE(service.uv_peel_preview_valid()) << service.uv_peel_preview_error();
  const auto result = service.commit_uv_peel_preview();
  ASSERT_TRUE(result.ok) << result.message;
  const auto authored = assets.mesh(scene.mesh(mesh.object.entity)->mesh)
                            ->authored_polygon_document;
  ASSERT_NE(authored, nullptr);
  const auto *retained = quader_poly::find_face(*authored, collapsed_id);
  ASSERT_NE(retained, nullptr);
  EXPECT_EQ(retained->vertices, original_face.vertices);
  EXPECT_EQ(retained->material_slot, original_face.material_slot);
  ASSERT_EQ(retained->uvs.size(), original_face.uvs.size());
  for (std::size_t index = 0; index < retained->uvs.size(); ++index) {
    EXPECT_FLOAT_EQ(retained->uvs[index].x, original_face.uvs[index].x);
    EXPECT_FLOAT_EQ(retained->uvs[index].y, original_face.uvs[index].y);
  }
}

TEST(ModelingSessionServiceSplitTests,
     UvPeelPreviewRebuildsCanonicalGuidesAndSkipsUnchangedSolves) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);
  const auto mesh = service.create_mesh({
      .document = service_uv_peel_strip_document(), .name = "Peel strip",
  });
  ASSERT_TRUE(mesh.ok) << mesh.message;
  ASSERT_TRUE(service.set_component_selection({
      .modeling_object_id = mesh.object.modeling_object_id,
      .mode = ModelingSelectionMode::Edge,
      .edges = {{2, 5}},
      .active_kind = ModelingActiveSelectionKind::Edge,
      .active_edge = {2, 5},
  }).ok);
  const AssetId original_mesh = scene.mesh(mesh.object.entity)->mesh;
  const auto original = assets.mesh(original_mesh)->authored_polygon_document;
  ASSERT_TRUE(service.set_uv_peel_options({.world_space = false}).ok);
  ASSERT_TRUE(service.refresh_uv_peel_preview().ok);
  ASSERT_TRUE(service.uv_peel_preview_active());
  ASSERT_TRUE(service.uv_peel_preview_valid()) << service.uv_peel_preview_error();
  EXPECT_FALSE(service.operation_preview_active());
  EXPECT_TRUE(service.uv_peel_settings().editable);
  const auto stable = service.modeling_sdk_projection_stats_for_tests();
  const auto stable_revision = service.uv_peel_settings_revision();
  for (int index = 0; index < 20; ++index) {
    EXPECT_TRUE(service.refresh_uv_peel_preview().ok);
    EXPECT_EQ(service.uv_peel_settings_revision(), stable_revision);
  }
  const auto unchanged = service.modeling_sdk_projection_stats_for_tests();
  EXPECT_EQ(unchanged.uv_peel_preparation_count, stable.uv_peel_preparation_count);
  EXPECT_EQ(unchanged.uv_peel_summary_build_count, stable.uv_peel_summary_build_count);
  EXPECT_EQ(unchanged.preview_publication_count, stable.preview_publication_count);

  ASSERT_TRUE(service.set_component_selection({
      .modeling_object_id = mesh.object.modeling_object_id,
      .mode = ModelingSelectionMode::Edge,
      .edges = {{1, 4}},
      .active_kind = ModelingActiveSelectionKind::Edge,
      .active_edge = {1, 4},
  }).ok);
  EXPECT_TRUE(service.uv_peel_preview_valid()) << service.uv_peel_preview_error();
  const auto selected = service.uv_peel_settings();
  EXPECT_EQ(selected.selected_edge_count, 1u);
  ASSERT_TRUE(service.cancel_uv_peel_preview().ok);
  EXPECT_FALSE(service.uv_peel_preview_active());
  EXPECT_EQ(service.selection_mode(), ModelingSelectionMode::Edge);
  EXPECT_EQ(service.uv_peel_settings().selection_token, selected.selection_token);
  EXPECT_EQ(scene.mesh(mesh.object.entity)->mesh, original_mesh);
  EXPECT_EQ(assets.mesh(original_mesh)->authored_polygon_document, original);
}

TEST(ModelingSessionServiceSplitTests,
     UvPeelPreviewDeselectRestoresUvsAndKeepsToolAwaitingGuides) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);
  const auto mesh = service.create_mesh({
      .document = service_uv_peel_strip_document(), .name = "Peel strip",
  });
  ASSERT_TRUE(mesh.ok) << mesh.message;
  ASSERT_TRUE(service.set_component_selection({
      .modeling_object_id = mesh.object.modeling_object_id,
      .mode = ModelingSelectionMode::Edge,
      .edges = {{2, 5}},
      .active_kind = ModelingActiveSelectionKind::Edge,
      .active_edge = {2, 5},
  }).ok);
  const AssetId original_mesh = scene.mesh(mesh.object.entity)->mesh;
  const auto original = assets.mesh(original_mesh)->authored_polygon_document;
  ASSERT_TRUE(service.refresh_uv_peel_preview().ok);
  ASSERT_TRUE(service.uv_peel_preview_valid()) << service.uv_peel_preview_error();
  ASSERT_TRUE(service.clear_selection().ok);
  EXPECT_TRUE(service.uv_peel_preview_active());
  EXPECT_FALSE(service.uv_peel_preview_valid());
  EXPECT_FALSE(service.uv_peel_preview_error().empty());
  EXPECT_EQ(service.uv_peel_settings().selected_edge_count, 0u);
  EXPECT_EQ(scene.mesh(mesh.object.entity)->mesh, original_mesh);
  EXPECT_EQ(assets.mesh(original_mesh)->authored_polygon_document, original);
  EXPECT_FALSE(service.commit_uv_peel_preview().ok);
  ASSERT_TRUE(service.set_component_selection({
      .modeling_object_id = mesh.object.modeling_object_id,
      .mode = ModelingSelectionMode::Edge,
      .edges = {{1, 4}},
      .active_kind = ModelingActiveSelectionKind::Edge,
      .active_edge = {1, 4},
  }).ok);
  // Selection itself must restore a valid candidate; there is no render tick
  // between this new guide and the settings panel's next readiness query.
  EXPECT_TRUE(service.uv_peel_preview_valid()) << service.uv_peel_preview_error();
  EXPECT_TRUE(service.cancel_uv_peel_preview().ok);
}

TEST(ModelingSessionServiceSplitTests,
     UvPeelPreviewPublicationFailureRestoresAllTargetsAndSharedBindings) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);
  const auto first = service.create_mesh({
      .document = service_uv_peel_strip_document(), .name = "First strip",
  });
  const auto second = service.create_mesh({
      .document = service_uv_peel_strip_document(), .name = "Second strip",
  });
  ASSERT_TRUE(first.ok) << first.message;
  ASSERT_TRUE(second.ok) << second.message;
  for (const auto *created : {&first, &second}) {
    ASSERT_TRUE(service.set_component_selection({
        .modeling_object_id = created->object.modeling_object_id,
        .mode = ModelingSelectionMode::Edge,
        .edges = {{2, 5}},
        .edit = created == &first ? ModelingSelectionEdit::Replace
                                 : ModelingSelectionEdit::Add,
        .active_kind = ModelingActiveSelectionKind::Edge,
        .active_edge = {2, 5},
    }).ok);
  }
  const AssetId first_mesh = scene.mesh(first.object.entity)->mesh;
  const AssetId second_mesh = scene.mesh(second.object.entity)->mesh;
  const EntityId shared = scene.create_entity("Shared consumer", EntityKind::Mesh);
  ASSERT_TRUE(scene.set_mesh(shared, {.mesh = first_mesh}));
  const auto original_first = assets.mesh(first_mesh)->authored_polygon_document;
  const auto original_second = assets.mesh(second_mesh)->authored_polygon_document;
  const auto mesh_ids = assets.mesh_ids();
  const auto content_revision = service.content_revision();
  const auto selection_revision = service.selection_revision();
  service.set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::PreviewSceneNotify);
  ASSERT_TRUE(service.refresh_uv_peel_preview().ok);
  EXPECT_TRUE(service.uv_peel_preview_active());
  EXPECT_FALSE(service.uv_peel_preview_valid());
  EXPECT_FALSE(service.uv_peel_preview_error().empty());
  EXPECT_EQ(service.content_revision(), content_revision);
  EXPECT_EQ(service.selection_revision(), selection_revision);
  EXPECT_EQ(assets.mesh_ids(), mesh_ids);
  EXPECT_EQ(scene.mesh(first.object.entity)->mesh, first_mesh);
  EXPECT_EQ(scene.mesh(second.object.entity)->mesh, second_mesh);
  EXPECT_EQ(scene.mesh(shared)->mesh, first_mesh);
  EXPECT_EQ(assets.mesh(first_mesh)->authored_polygon_document, original_first);
  EXPECT_EQ(assets.mesh(second_mesh)->authored_polygon_document, original_second);
  service.set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::None);
  ASSERT_TRUE(service.set_uv_peel_options({.u_offset = 0.25}).ok);
  ASSERT_TRUE(service.refresh_uv_peel_preview().ok);
  EXPECT_TRUE(service.uv_peel_preview_valid()) << service.uv_peel_preview_error();
  ASSERT_TRUE(service.cancel_uv_peel_preview().ok);
  EXPECT_EQ(scene.mesh(shared)->mesh, first_mesh);
  EXPECT_EQ(assets.mesh(first_mesh)->authored_polygon_document, original_first);
  EXPECT_EQ(assets.mesh(second_mesh)->authored_polygon_document, original_second);
}

TEST(ModelingSessionServiceSplitTests,
     UvPeelPreviewCommitFailureRetainsCandidateForRetry) {
  SceneDocument scene;
  AssetRegistry assets;
  ModelingSessionService service(scene, assets);
  const auto mesh = service.create_mesh({
      .document = service_uv_peel_strip_document(), .name = "Peel strip",
  });
  ASSERT_TRUE(mesh.ok) << mesh.message;
  ASSERT_TRUE(service.set_component_selection({
      .modeling_object_id = mesh.object.modeling_object_id,
      .mode = ModelingSelectionMode::Edge,
      .edges = {{2, 5}},
      .active_kind = ModelingActiveSelectionKind::Edge,
      .active_edge = {2, 5},
  }).ok);
  ASSERT_TRUE(service.refresh_uv_peel_preview().ok);
  ASSERT_TRUE(service.uv_peel_preview_valid()) << service.uv_peel_preview_error();
  const AssetId candidate_mesh = scene.mesh(mesh.object.entity)->mesh;
  const auto candidate = assets.mesh(candidate_mesh)->authored_polygon_document;
  service.set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::CommittedSceneNotify);
  const auto failed = service.commit_uv_peel_preview();
  EXPECT_FALSE(failed.ok);
  EXPECT_FALSE(failed.changed);
  EXPECT_TRUE(service.uv_peel_preview_active());
  EXPECT_TRUE(service.uv_peel_preview_valid());
  EXPECT_EQ(scene.mesh(mesh.object.entity)->mesh, candidate_mesh);
  EXPECT_EQ(assets.mesh(candidate_mesh)->authored_polygon_document, candidate);
  service.set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::None);
  const auto committed = service.commit_uv_peel_preview();
  ASSERT_TRUE(committed.ok) << committed.message;
  EXPECT_TRUE(committed.changed);
  EXPECT_FALSE(service.uv_peel_preview_active());
  EXPECT_EQ(assets.mesh(scene.mesh(mesh.object.entity)->mesh)->authored_polygon_document,
            candidate);
}


namespace face_extrusion_projection {

/// Exercises the native publication boundary through the owning service.
class FaceExtrusionProjectionTests : public testing::Test {
protected:
  ModelingCreateBoxResult add_strip(std::string name = "Extrusion strip") {
    return service_.create_mesh({.document = service_uv_peel_strip_document(),
                                .name = std::move(name)});
  }

  void select_strip_faces(const ModelingObjectRecord &object,
                          ModelingSelectionEdit edit = ModelingSelectionEdit::Replace) {
    ASSERT_TRUE(service_.set_component_selection({
        .modeling_object_id = object.modeling_object_id,
        .mode = ModelingSelectionMode::Face,
        .faces = {1, 2},
        .edit = edit,
        .active_kind = ModelingActiveSelectionKind::Face,
        .active_face = 2}).ok);
  }

  MeshGeometryContentId geometry(EntityId entity) const {
    return service_.compile_object_mesh(
        *service_.modeling_object_for_entity(entity))->geometry().content_id();
  }

  SceneDocument scene_;
  AssetRegistry assets_;
  ModelingSessionService service_{scene_, assets_};
};

TEST_F(FaceExtrusionProjectionTests, AdjustmentsRegenerateOriginalAndZeroRestoresSource) {
  const auto mesh = add_strip();
  ASSERT_TRUE(mesh.ok) << mesh.message;
  select_strip_faces(mesh.object);
  const auto original = geometry(mesh.object.entity);
  const auto captured = service_.capture_face_extrusion_source();
  ASSERT_TRUE(captured.ok) << captured.message;
  ASSERT_TRUE(captured.source.valid());
  EXPECT_EQ(captured.face_count, 2u);
  expect_vec3_eq(captured.direction_world, {0, 0, 1});
  const auto initial_revision = service_.content_revision();
  const auto zero = service_.apply_face_extrusion(
      captured.source, modeling::FaceExtrudeMode::Region, 0.0);
  ASSERT_TRUE(zero.ok) << zero.message;
  EXPECT_FALSE(zero.changed);
  EXPECT_EQ(service_.content_revision(), initial_revision);
  EXPECT_EQ(geometry(mesh.object.entity), original);

  ASSERT_TRUE(service_.apply_face_extrusion(
      captured.source, modeling::FaceExtrudeMode::Individual, 2.15).ok);
  const auto individual = geometry(mesh.object.entity);
  const MeshGeometryPayload individual_geometry =
      assets_.mesh(scene_.mesh(mesh.object.entity)->mesh)->geometry();
  const auto caps = service_.export_selection().objects.front().selected_faces;
  ASSERT_EQ(caps.size(), 2u);
  ASSERT_TRUE(service_.apply_face_extrusion(
      captured.source, modeling::FaceExtrudeMode::Region, 1.0).ok);
  EXPECT_NE(geometry(mesh.object.entity), individual);
  ASSERT_TRUE(service_.apply_face_extrusion(
      captured.source, modeling::FaceExtrudeMode::Individual, 2.15).ok);
  const auto &regenerated = assets_.mesh(scene_.mesh(mesh.object.entity)->mesh)->geometry();
  ASSERT_EQ(regenerated.vertices.size(), individual_geometry.vertices.size());
  for (std::size_t index = 0; index < regenerated.vertices.size(); ++index) {
    expect_vec3_eq(regenerated.vertices[index].position,
                   individual_geometry.vertices[index].position);
  }
  const auto unchanged_geometry = geometry(mesh.object.entity);
  const auto unchanged_revision = service_.content_revision();
  EXPECT_FALSE(service_.apply_face_extrusion(
      captured.source, modeling::FaceExtrudeMode::Individual, 2.15).changed);
  EXPECT_EQ(service_.content_revision(), unchanged_revision);
  EXPECT_EQ(geometry(mesh.object.entity), unchanged_geometry);
  const auto repeated_caps = service_.export_selection().objects.front().selected_faces;
  ASSERT_EQ(repeated_caps.size(), caps.size());
  for (std::size_t index = 0; index < caps.size(); ++index) {
    EXPECT_EQ(repeated_caps[index].id, caps[index].id);
  }
  ASSERT_TRUE(service_.apply_face_extrusion(
      captured.source, modeling::FaceExtrudeMode::Individual, 0.0).ok);
  EXPECT_EQ(geometry(mesh.object.entity), original);
  EXPECT_EQ(service_.selection_summary().selected_face_count, 2u);
  EXPECT_FALSE(service_.face_extrusion_preview_active());
}

TEST_F(FaceExtrusionProjectionTests, SourcePivotWeightsUniqueVerticesAcrossObjects) {
  const auto strip = add_strip();
  ASSERT_TRUE(strip.ok) << strip.message;
  quader_poly::Document quad_document;
  const std::array quad_vertices{
      quader_poly::add_vertex(quad_document, {9.0f, -1.0f, 0.0f}),
      quader_poly::add_vertex(quad_document, {11.0f, -1.0f, 0.0f}),
      quader_poly::add_vertex(quad_document, {11.0f, 1.0f, 0.0f}),
      quader_poly::add_vertex(quad_document, {9.0f, 1.0f, 0.0f})};
  (void)quader_poly::add_face(quad_document, quad_vertices);
  const auto quad = service_.create_mesh({.document = std::move(quad_document),
                                        .name = "Pivot quad"});
  ASSERT_TRUE(quad.ok) << quad.message;
  select_strip_faces(strip.object);
  ASSERT_TRUE(service_.set_component_selection({
      .modeling_object_id = quad.object.modeling_object_id,
      .mode = ModelingSelectionMode::Face,
      .faces = {1},
      .edit = ModelingSelectionEdit::Add,
      .active_kind = ModelingActiveSelectionKind::Face,
      .active_face = 1}).ok);
  const auto captured = service_.capture_face_extrusion_source();
  ASSERT_TRUE(captured.ok) << captured.message;
  EXPECT_EQ(captured.face_count, 3u);
  // Six strip vertices at mean X=0 and four quad vertices at mean X=10.
  expect_vec3_eq(captured.center_world, {4.0f, 0.0f, 0.0f});
  expect_vec3_eq(captured.direction_world, {0.0f, 0.0f, 1.0f});
  expect_vec3_eq(captured.handle_direction_world, captured.direction_world);
}

TEST_F(FaceExtrusionProjectionTests,
       OpposingNormalsUseWorldUpHandleAndActiveFaceOperationDirection) {
  const auto first = add_strip("Positive normal strip");
  const auto second = add_strip("Mirrored normal strip");
  ASSERT_TRUE(first.ok && second.ok);
  ASSERT_TRUE(scene_.set_transform(second.object.entity, {
      .position = {6.0f, 0.0f, 0.0f}, .scale = {-2.0f, 1.0f, 1.0f}}));
  select_strip_faces(first.object);
  select_strip_faces(second.object, ModelingSelectionEdit::Add);
  const auto captured = service_.capture_face_extrusion_source();
  ASSERT_TRUE(captured.ok) << captured.message;
  EXPECT_EQ(captured.face_count, 4u);
  expect_vec3_eq(captured.center_world, {3.0f, 0.0f, 0.0f});
  expect_vec3_eq(captured.direction_world, {0.0f, 0.0f, -1.0f});
  expect_vec3_eq(captured.handle_direction_world, {0.0f, 1.0f, 0.0f});
  const auto extruded = service_.apply_face_extrusion(
      captured.source, modeling::FaceExtrudeMode::Region, 1.0);
  ASSERT_TRUE(extruded.ok) << extruded.message;
  const auto caps = service_.capture_face_extrusion_source();
  ASSERT_TRUE(caps.ok) << caps.message;
  expect_vec3_eq(caps.center_world, {3.0f, 0.0f, -1.0f});
  expect_vec3_eq(caps.handle_direction_world, {0.0f, 1.0f, 0.0f});
}

TEST_F(FaceExtrusionProjectionTests, PreviewCancelAndCommitKeepSourceReusable) {
  const auto mesh = add_strip();
  ASSERT_TRUE(mesh.ok) << mesh.message;
  select_strip_faces(mesh.object);
  const auto original = geometry(mesh.object.entity);
  const auto captured = service_.capture_face_extrusion_source();
  ASSERT_TRUE(captured.ok) << captured.message;
  const auto begin_preview = [&] {
    const auto result = service_.begin_face_extrusion_preview(captured.source);
    EXPECT_TRUE(result.ok) << result.message;
    return result.ok;
  };
  ASSERT_TRUE(begin_preview());
  EXPECT_TRUE(service_.face_extrusion_preview_active());
  EXPECT_EQ(geometry(mesh.object.entity), original);
  ASSERT_TRUE(service_.update_face_extrusion_preview(
      captured.source, modeling::FaceExtrudeMode::AlongNormals, 2.0).ok);
  EXPECT_NE(geometry(mesh.object.entity), original);
  ASSERT_TRUE(service_.update_face_extrusion_preview(
      captured.source, modeling::FaceExtrudeMode::AlongNormals, 0.0).ok);
  EXPECT_EQ(geometry(mesh.object.entity), original);
  const auto unchanged = service_.commit_face_extrusion_preview();
  ASSERT_TRUE(unchanged.ok) << unchanged.message;
  EXPECT_FALSE(unchanged.changed);
  EXPECT_FALSE(service_.face_extrusion_preview_active());

  EXPECT_TRUE(service_.face_extrusion_source_current(captured.source));
  ASSERT_TRUE(begin_preview());
  ASSERT_TRUE(service_.update_face_extrusion_preview(
      captured.source, modeling::FaceExtrudeMode::Individual, 1.5).ok);
  ASSERT_TRUE(service_.cancel_face_extrusion_preview().ok);
  EXPECT_EQ(geometry(mesh.object.entity), original);
  EXPECT_TRUE(service_.face_extrusion_source_current(captured.source));
  ASSERT_TRUE(begin_preview());
  ASSERT_TRUE(service_.update_face_extrusion_preview(
      captured.source, modeling::FaceExtrudeMode::Individual, 1.5).ok);
  const auto candidate = geometry(mesh.object.entity);
  ASSERT_TRUE(service_.commit_face_extrusion_preview().ok);
  EXPECT_EQ(geometry(mesh.object.entity), candidate);
  ASSERT_TRUE(service_.apply_face_extrusion(
      captured.source, modeling::FaceExtrudeMode::Region, -0.75).ok);
  EXPECT_NE(geometry(mesh.object.entity), candidate);
}

TEST_F(FaceExtrusionProjectionTests, MultiObjectFailurePreservesCommittedAdjustment) {
  const auto first = add_strip("First strip");
  const auto second = add_strip("Second strip");
  ASSERT_TRUE(first.ok && second.ok);
  select_strip_faces(first.object);
  select_strip_faces(second.object, ModelingSelectionEdit::Add);
  const auto captured = service_.capture_face_extrusion_source();
  ASSERT_TRUE(captured.ok) << captured.message;
  ASSERT_EQ(captured.face_count, 4u);
  ASSERT_TRUE(service_.apply_face_extrusion(
      captured.source, modeling::FaceExtrudeMode::Region, 1.0).ok);
  const auto first_before = geometry(first.object.entity);
  const auto second_before = geometry(second.object.entity);
  const auto content_before = service_.content_revision();
  const auto selection_before = service_.selection_revision();
  const auto assets_before = assets_.revision();
  const auto scene_before = scene_.revision();
  for (const auto failure : {ModelingSdkProjectionFailurePoint::AfterSdkMutation,
                             ModelingSdkProjectionFailurePoint::CommittedAssetReplace,
                             ModelingSdkProjectionFailurePoint::CommittedSceneNotify,
                             ModelingSdkProjectionFailurePoint::BeforeCommit}) {
    SCOPED_TRACE(static_cast<int>(failure));
    service_.set_modeling_sdk_projection_failure_for_tests(failure);
    const auto result = service_.apply_face_extrusion(
        captured.source, modeling::FaceExtrudeMode::Individual, 2.15);
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.changed);
    EXPECT_EQ(geometry(first.object.entity), first_before);
    EXPECT_EQ(geometry(second.object.entity), second_before);
    EXPECT_EQ(service_.content_revision(), content_before);
    EXPECT_EQ(service_.selection_revision(), selection_before);
    EXPECT_EQ(assets_.revision(), assets_before);
    EXPECT_EQ(scene_.revision(), scene_before);
  }
  service_.set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::None);
  EXPECT_TRUE(service_.apply_face_extrusion(
      captured.source, modeling::FaceExtrudeMode::Individual, 2.15).ok);
}

TEST_F(FaceExtrusionProjectionTests,
       AdjustmentPreviewUsesCommittedBaselineForCancelAndUnchangedCommit) {
  const auto mesh = add_strip();
  ASSERT_TRUE(mesh.ok) << mesh.message;
  select_strip_faces(mesh.object);
  const auto original = geometry(mesh.object.entity);
  const auto captured = service_.capture_face_extrusion_source();
  ASSERT_TRUE(captured.ok) << captured.message;
  ASSERT_TRUE(service_.apply_face_extrusion(
      captured.source, modeling::FaceExtrudeMode::Region, 2.15).ok);
  const auto committed = geometry(mesh.object.entity);
  const auto begin_adjustment = [&] {
    const auto result = service_.begin_face_extrusion_adjustment_preview(captured.source);
    EXPECT_TRUE(result.ok) << result.message;
    return result.ok;
  };

  ASSERT_TRUE(begin_adjustment());
  const auto unchanged_update = service_.update_face_extrusion_preview(
      captured.source, modeling::FaceExtrudeMode::Region, 2.15);
  ASSERT_TRUE(unchanged_update.ok) << unchanged_update.message;
  EXPECT_FALSE(unchanged_update.changed);
  const auto unchanged_commit = service_.commit_face_extrusion_preview();
  ASSERT_TRUE(unchanged_commit.ok) << unchanged_commit.message;
  EXPECT_FALSE(unchanged_commit.changed);
  EXPECT_EQ(geometry(mesh.object.entity), committed);
  EXPECT_TRUE(service_.face_extrusion_source_current(captured.source));

  ASSERT_TRUE(begin_adjustment());
  ASSERT_TRUE(service_.update_face_extrusion_preview(
      captured.source, modeling::FaceExtrudeMode::Region, 4.0).ok);
  ASSERT_TRUE(service_.cancel_face_extrusion_preview().ok);
  EXPECT_EQ(geometry(mesh.object.entity), committed);
  EXPECT_TRUE(service_.face_extrusion_source_current(captured.source));
  EXPECT_FALSE(service_.apply_face_extrusion(
      captured.source, modeling::FaceExtrudeMode::Region, 2.15).changed);

  ASSERT_TRUE(begin_adjustment());
  ASSERT_TRUE(service_.update_face_extrusion_preview(
      captured.source, modeling::FaceExtrudeMode::Region, 4.0).ok);
  ASSERT_TRUE(service_.update_face_extrusion_preview(
      captured.source, modeling::FaceExtrudeMode::Region, 2.15).ok);
  const auto returned = service_.commit_face_extrusion_preview();
  ASSERT_TRUE(returned.ok) << returned.message;
  EXPECT_FALSE(returned.changed);
  EXPECT_EQ(geometry(mesh.object.entity), committed);

  ASSERT_TRUE(begin_adjustment());
  ASSERT_TRUE(service_.update_face_extrusion_preview(
      captured.source, modeling::FaceExtrudeMode::Region, 0.0).ok);
  EXPECT_EQ(geometry(mesh.object.entity), original);
  ASSERT_TRUE(service_.cancel_face_extrusion_preview().ok);
  EXPECT_EQ(geometry(mesh.object.entity), committed);

  ASSERT_TRUE(begin_adjustment());
  ASSERT_TRUE(service_.update_face_extrusion_preview(
      captured.source, modeling::FaceExtrudeMode::Region, 0.0).ok);
  const auto removed = service_.commit_face_extrusion_preview();
  ASSERT_TRUE(removed.ok) << removed.message;
  EXPECT_TRUE(removed.changed);
  EXPECT_EQ(geometry(mesh.object.entity), original);
  EXPECT_TRUE(service_.face_extrusion_source_current(captured.source));
  ASSERT_TRUE(service_.apply_face_extrusion(
      captured.source, modeling::FaceExtrudeMode::Region, 3.0).ok);
  EXPECT_NE(geometry(mesh.object.entity), original);
}

TEST_F(FaceExtrusionProjectionTests,
       FailedAdjustmentPreviewRestoresAllCommittedObjectsAndSourceEligibility) {
  const auto first = add_strip("First adjustment strip");
  const auto second = add_strip("Second adjustment strip");
  ASSERT_TRUE(first.ok && second.ok);
  select_strip_faces(first.object);
  select_strip_faces(second.object, ModelingSelectionEdit::Add);
  const auto captured = service_.capture_face_extrusion_source();
  ASSERT_TRUE(captured.ok) << captured.message;
  ASSERT_TRUE(service_.apply_face_extrusion(
      captured.source, modeling::FaceExtrudeMode::Region, 2.15).ok);
  const auto first_committed = geometry(first.object.entity);
  const auto second_committed = geometry(second.object.entity);
  ASSERT_TRUE(service_.begin_face_extrusion_adjustment_preview(captured.source).ok);
  ASSERT_TRUE(service_.update_face_extrusion_preview(
      captured.source, modeling::FaceExtrudeMode::Individual, 3.0).ok);
  service_.set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::AfterSdkMutation);
  const auto failed = service_.update_face_extrusion_preview(
      captured.source, modeling::FaceExtrudeMode::Individual, 4.0);
  EXPECT_FALSE(failed.ok);
  EXPECT_FALSE(failed.changed);
  EXPECT_FALSE(service_.face_extrusion_preview_active());
  EXPECT_EQ(geometry(first.object.entity), first_committed);
  EXPECT_EQ(geometry(second.object.entity), second_committed);
  EXPECT_TRUE(service_.face_extrusion_source_current(captured.source));
  service_.set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::None);
  const auto unchanged = service_.apply_face_extrusion(
      captured.source, modeling::FaceExtrudeMode::Region, 2.15);
  ASSERT_TRUE(unchanged.ok) << unchanged.message;
  EXPECT_FALSE(unchanged.changed);
  ASSERT_TRUE(service_.begin_face_extrusion_adjustment_preview(captured.source).ok);
  ASSERT_TRUE(service_.update_face_extrusion_preview(
      captured.source, modeling::FaceExtrudeMode::Region, 1.0).ok);
  ASSERT_TRUE(service_.commit_face_extrusion_preview().ok);
  EXPECT_NE(geometry(first.object.entity), first_committed);
  EXPECT_NE(geometry(second.object.entity), second_committed);
}

TEST_F(FaceExtrusionProjectionTests,
       DynamicPreviewKeepsStableIdsAndPublishesTheAssetOnlyOnRelease) {
  const auto mesh = add_strip();
  ASSERT_TRUE(mesh.ok) << mesh.message;
  select_strip_faces(mesh.object);
  const auto captured = service_.capture_face_extrusion_source();
  ASSERT_TRUE(captured.ok) << captured.message;
  const auto asset_id = scene_.mesh(mesh.object.entity)->mesh;
  const auto durable = assets_.mesh(asset_id)->geometry().content_id();
  ASSERT_TRUE(service_.begin_face_extrusion_preview(captured.source).ok);
  const auto before = service_.modeling_sdk_projection_stats_for_tests();
  ASSERT_TRUE(service_.update_face_extrusion_preview(
      captured.source, modeling::FaceExtrudeMode::Region, 1.0).ok);
  const auto first = service_.tool_preview_meshes();
  ASSERT_EQ(first.size(), 1u);
  EXPECT_EQ(first.front().source_entity, mesh.object.entity);
  EXPECT_TRUE(first.front().suppress_source);
  EXPECT_TRUE(first.front().dynamic_positions);
  ASSERT_NE(first.front().dynamic_position_payload, nullptr);
  const auto first_positions = *first.front().dynamic_position_payload;
  const auto first_document = first.front().mesh.authored_polygon_document;
  ASSERT_NE(first_document, nullptr);
  auto position_revision = first.front().position_revision;
  auto render_signature = service_.tool_preview_signature();

  for (const double amount : {1.001, 1.002, 1.125, 3.125, -0.125}) {
    ASSERT_TRUE(service_.update_face_extrusion_preview(
        captured.source, modeling::FaceExtrudeMode::Region, amount).ok);
    const auto visible = service_.tool_preview_meshes();
    ASSERT_EQ(visible.size(), 1u);
    EXPECT_NE(service_.tool_preview_signature(), render_signature);
    render_signature = service_.tool_preview_signature();
    EXPECT_GT(visible.front().position_revision, position_revision);
    position_revision = visible.front().position_revision;
    const auto &document = *visible.front().mesh.authored_polygon_document;
    ASSERT_EQ(document.vertices.size(), first_document->vertices.size());
    ASSERT_EQ(document.faces.size(), first_document->faces.size());
    for (std::size_t index = 0; index < document.vertices.size(); ++index) {
      EXPECT_EQ(document.vertices[index].id, first_document->vertices[index].id);
    }
    for (std::size_t index = 0; index < document.faces.size(); ++index) {
      EXPECT_EQ(document.faces[index].id, first_document->faces[index].id);
    }
    EXPECT_EQ(visible.front().layout_key, first.front().layout_key);
    EXPECT_EQ(assets_.mesh(asset_id)->geometry().content_id(), durable);
    EXPECT_EQ(geometry(mesh.object.entity),
              visible.front().mesh.geometry().content_id());
    const auto selection = service_.export_selection();
    ASSERT_EQ(selection.objects.size(), 1u);
    ASSERT_EQ(selection.objects.front().selected_faces.size(), 2u);
    for (const auto &face : selection.objects.front().selected_faces) {
      for (const auto vertex_id : face.vertices) {
        const auto *vertex = quader_poly::find_vertex(document, vertex_id);
        ASSERT_NE(vertex, nullptr);
        EXPECT_NEAR(vertex->position.z, amount, 1.0e-5);
      }
    }
    const auto overlay = service_.selection_overlay_frame();
    ASSERT_FALSE(overlay.triangles.empty());
    std::size_t selected_triangles = 0;
    for (const auto &triangle : overlay.triangles) {
      if (triangle.element.object_id != mesh.object.modeling_object_id) continue;
      // Source-wire depth stamps also cover stationary bases and perimeter
      // walls. Only selection fill and mask triangles describe selected caps.
      if (triangle.layer != ViewportOverlayLayer::SelectedVisibleFace &&
          triangle.layer != ViewportOverlayLayer::SelectedFaceMask) continue;
      EXPECT_TRUE(std::ranges::any_of(
          selection.objects.front().selected_faces,
          [&](const auto &face) { return face.id == triangle.element.face_id; }));
      ++selected_triangles;
      EXPECT_NEAR(triangle.a.z, amount, 1.0e-5);
      EXPECT_NEAR(triangle.b.z, amount, 1.0e-5);
      EXPECT_NEAR(triangle.c.z, amount, 1.0e-5);
    }
    EXPECT_GT(selected_triangles, 0u);
    for (const auto &face : selection.objects.front().selected_faces) {
      EXPECT_TRUE(std::ranges::any_of(overlay.triangles, [&](const auto &triangle) {
        return triangle.element.object_id == mesh.object.modeling_object_id &&
               triangle.element.face_id == face.id &&
               triangle.layer == ViewportOverlayLayer::SelectedVisibleFace;
      }));
    }
  }
  ASSERT_EQ(first.front().dynamic_position_payload->size(), first_positions.size());
  for (std::size_t index = 0; index < first_positions.size(); ++index) {
    expect_vec3_eq((*first.front().dynamic_position_payload)[index], first_positions[index]);
  }
  const auto accepted = service_.modeling_sdk_projection_stats_for_tests();
  EXPECT_EQ(accepted.asset_publication_count, before.asset_publication_count);
  EXPECT_EQ(accepted.scene_publication_count, before.scene_publication_count);
  const auto content = service_.content_revision();
  const auto visible = service_.tool_preview_meshes().front();
  const auto overlay_builds = accepted.native_selection_projection_build_count;
  const auto unchanged = service_.update_face_extrusion_preview(
      captured.source, modeling::FaceExtrudeMode::Region, -0.125);
  ASSERT_TRUE(unchanged.ok) << unchanged.message;
  EXPECT_FALSE(unchanged.changed);
  EXPECT_EQ(service_.content_revision(), content);
  EXPECT_EQ(service_.modeling_sdk_projection_stats_for_tests().preview_publication_count,
            accepted.preview_publication_count);
  EXPECT_EQ(service_.tool_preview_meshes().front().dynamic_position_payload,
            visible.dynamic_position_payload);
  EXPECT_EQ(service_.tool_preview_meshes().front().position_revision,
            position_revision);
  EXPECT_EQ(service_.tool_preview_signature(), render_signature);
  (void)service_.selection_overlay_frame();
  EXPECT_EQ(service_.modeling_sdk_projection_stats_for_tests().native_selection_projection_build_count,
            overlay_builds);
  ASSERT_TRUE(service_.commit_face_extrusion_preview().ok);
  EXPECT_TRUE(service_.tool_preview_meshes().empty());
  EXPECT_NE(service_.tool_preview_signature(), render_signature);
  EXPECT_EQ(geometry(mesh.object.entity), visible.mesh.geometry().content_id());
  EXPECT_EQ(service_.modeling_sdk_projection_stats_for_tests().asset_publication_count,
            before.asset_publication_count + 1u);
}

TEST_F(FaceExtrusionProjectionTests, ZeroRetiresPreparedIdsBeforeAnotherExtrusion) {
  const auto mesh = add_strip();
  ASSERT_TRUE(mesh.ok) << mesh.message;
  select_strip_faces(mesh.object);
  const auto original = service_.compile_object_mesh(mesh.object.modeling_object_id);
  ASSERT_TRUE(original.has_value());
  const auto captured = service_.capture_face_extrusion_source();
  ASSERT_TRUE(captured.ok) << captured.message;
  ASSERT_TRUE(service_.begin_face_extrusion_preview(captured.source).ok);
  ASSERT_TRUE(service_.update_face_extrusion_preview(
      captured.source, modeling::FaceExtrudeMode::Individual, 1.0).ok);
  const auto first = service_.compile_object_mesh(mesh.object.modeling_object_id);
  const auto cursor = first->authored_polygon_document->next_vertex_id;
  ASSERT_TRUE(service_.update_face_extrusion_preview(
      captured.source, modeling::FaceExtrudeMode::Individual, 0.0).ok);
  EXPECT_EQ(geometry(mesh.object.entity), original->geometry().content_id());
  ASSERT_TRUE(service_.update_face_extrusion_preview(
      captured.source, modeling::FaceExtrudeMode::Individual, -1.0).ok);
  const auto second = service_.compile_object_mesh(mesh.object.modeling_object_id);
  const auto &document = *second->authored_polygon_document;
  for (const auto &vertex : document.vertices) {
    const bool was_original = std::ranges::any_of(
        original->authored_polygon_document->vertices,
        [&](const auto &source) { return source.id == vertex.id; });
    if (!was_original) EXPECT_GE(vertex.id, cursor);
  }
  ASSERT_TRUE(service_.cancel_face_extrusion_preview().ok);
  EXPECT_TRUE(service_.tool_preview_meshes().empty());
  EXPECT_EQ(geometry(mesh.object.entity), original->geometry().content_id());
}

TEST_F(FaceExtrusionProjectionTests,
       FailedDynamicPreparationAndCommitPreserveTheAcceptedCandidate) {
  const auto mesh = add_strip();
  ASSERT_TRUE(mesh.ok) << mesh.message;
  select_strip_faces(mesh.object);
  const auto captured = service_.capture_face_extrusion_source();
  ASSERT_TRUE(captured.ok) << captured.message;
  const auto original = geometry(mesh.object.entity);
  ASSERT_TRUE(service_.begin_face_extrusion_preview(captured.source).ok);
  ASSERT_TRUE(service_.update_face_extrusion_preview(
      captured.source, modeling::FaceExtrudeMode::Region, 1.0).ok);
  const auto accepted = service_.tool_preview_meshes().front();
  const auto invalid = service_.update_face_extrusion_preview(
      captured.source, static_cast<modeling::FaceExtrudeMode>(999), 2.0);
  EXPECT_FALSE(invalid.ok);
  EXPECT_EQ(service_.tool_preview_meshes().front().dynamic_position_payload,
            accepted.dynamic_position_payload);
  ASSERT_TRUE(service_.update_face_extrusion_preview(
      captured.source, modeling::FaceExtrudeMode::Region, 2.0).ok);
  EXPECT_EQ(service_.tool_preview_meshes().front().layout_key, accepted.layout_key);
  service_.set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::CommittedAssetReplace);
  const auto failed = service_.commit_face_extrusion_preview();
  EXPECT_FALSE(failed.ok);
  EXPECT_TRUE(service_.face_extrusion_preview_active());
  EXPECT_EQ(assets_.mesh(scene_.mesh(mesh.object.entity)->mesh)->geometry().content_id(),
            original);
  EXPECT_FALSE(service_.tool_preview_meshes().empty());
  service_.set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::None);
  ASSERT_TRUE(service_.cancel_face_extrusion_preview().ok);
  EXPECT_TRUE(service_.tool_preview_meshes().empty());
  EXPECT_EQ(geometry(mesh.object.entity), original);
}

TEST_F(FaceExtrusionProjectionTests,
       NewPreviewRetainsPriorAdjustmentOnlyAfterCancelOrUnchangedCommit) {
  for (const int outcome : {0, 1, 2, 3}) {
    SCOPED_TRACE(outcome);
    SceneDocument local_scene;
    AssetRegistry local_assets;
    ModelingSessionService projection(local_scene, local_assets);
    const auto mesh = projection.create_mesh({
        .document = service_uv_peel_strip_document(), .name = "Retained source"});
    ASSERT_TRUE(mesh.ok) << mesh.message;
    ASSERT_TRUE(projection.set_component_selection({
        .modeling_object_id = mesh.object.modeling_object_id,
        .mode = ModelingSelectionMode::Face,
        .faces = {1, 2},
        .active_kind = ModelingActiveSelectionKind::Face,
        .active_face = 2}).ok);
    const auto prior = projection.capture_face_extrusion_source();
    ASSERT_TRUE(prior.ok) << prior.message;
    ASSERT_TRUE(projection.apply_face_extrusion(
        prior.source, modeling::FaceExtrudeMode::Region, 2.15).ok);
    const auto committed = local_assets.mesh(
        local_scene.mesh(mesh.object.entity)->mesh)->geometry().content_id();
    const auto next = projection.capture_face_extrusion_source();
    ASSERT_TRUE(next.ok) << next.message;
    ASSERT_TRUE(projection.begin_face_extrusion_preview(next.source, prior.source).ok);
    ASSERT_TRUE(projection.update_face_extrusion_preview(
        next.source, modeling::FaceExtrudeMode::Region, 1.0).ok);
    if (outcome == 0) {
      ASSERT_TRUE(projection.cancel_face_extrusion_preview().ok);
    } else if (outcome == 1) {
      ASSERT_TRUE(projection.update_face_extrusion_preview(
          next.source, modeling::FaceExtrudeMode::Region, 0.0).ok);
      const auto unchanged = projection.commit_face_extrusion_preview();
      ASSERT_TRUE(unchanged.ok) << unchanged.message;
      EXPECT_FALSE(unchanged.changed);
    } else if (outcome == 2) {
      projection.set_modeling_sdk_projection_failure_for_tests(
          ModelingSdkProjectionFailurePoint::AfterSdkMutation);
      const auto failed = projection.update_face_extrusion_preview(
          next.source, modeling::FaceExtrudeMode::Region, 3.0);
      EXPECT_FALSE(failed.ok);
      EXPECT_FALSE(failed.changed);
      projection.set_modeling_sdk_projection_failure_for_tests(
          ModelingSdkProjectionFailurePoint::None);
    } else {
      ASSERT_TRUE(projection.commit_face_extrusion_preview().ok);
    }
    EXPECT_EQ(projection.face_extrusion_source_current(prior.source), outcome != 3);
    if (outcome != 3) {
      EXPECT_EQ(local_assets.mesh(local_scene.mesh(mesh.object.entity)->mesh)
                    ->geometry().content_id(), committed);
      ASSERT_TRUE(projection.begin_face_extrusion_adjustment_preview(prior.source).ok);
      ASSERT_TRUE(projection.update_face_extrusion_preview(
          prior.source, modeling::FaceExtrudeMode::Region, 4.0).ok);
      ASSERT_TRUE(projection.cancel_face_extrusion_preview().ok);
    }
  }
}

TEST_F(FaceExtrusionProjectionTests, MissingSourceAndInvalidAmountDoNotMutate) {
  const auto empty = service_.capture_face_extrusion_source();
  EXPECT_TRUE(empty.ok);
  EXPECT_FALSE(empty.source.valid());
  const auto mesh = add_strip();
  ASSERT_TRUE(mesh.ok);
  select_strip_faces(mesh.object);
  const auto captured = service_.capture_face_extrusion_source();
  ASSERT_TRUE(captured.ok) << captured.message;
  const auto original = geometry(mesh.object.entity);
  EXPECT_FALSE(service_.apply_face_extrusion(captured.source,
      modeling::FaceExtrudeMode::Region,
      std::numeric_limits<double>::quiet_NaN()).ok);
  EXPECT_EQ(geometry(mesh.object.entity), original);
  service_.reset_from_scene();
  EXPECT_FALSE(service_.apply_face_extrusion(
      captured.source, modeling::FaceExtrudeMode::Region, 1.0).ok);
  EXPECT_EQ(geometry(mesh.object.entity), original);
}

TEST_F(FaceExtrusionProjectionTests, ContextAndSelectionChangesRetireSourceFence) {
  const auto mesh = add_strip();
  ASSERT_TRUE(mesh.ok);
  select_strip_faces(mesh.object);
  ModelingInstanceEditScope scope;
  service_.set_instance_edit_scope_provider([&scope] { return scope; });
  const auto captured = service_.capture_face_extrusion_source();
  ASSERT_TRUE(captured.ok) << captured.message;
  EXPECT_TRUE(service_.face_extrusion_source_current(captured.source));
  const auto original = geometry(mesh.object.entity);
  const auto validation = service_.validate_face_extrusion(
      captured.source, modeling::FaceExtrudeMode::Individual, 2.15);
  EXPECT_TRUE(validation.ok) << validation.message;
  EXPECT_EQ(geometry(mesh.object.entity), original);
  scope = {.occurrence = mesh.object.entity, .definition = mesh.object.entity};
  EXPECT_FALSE(service_.face_extrusion_source_current(captured.source));
  EXPECT_FALSE(service_.apply_face_extrusion(
      captured.source, modeling::FaceExtrudeMode::Individual, 2.15).ok);
  EXPECT_EQ(geometry(mesh.object.entity), original);
  scope = {};
  ASSERT_TRUE(service_.clear_selection().ok);
  EXPECT_FALSE(service_.face_extrusion_source_current(captured.source));
}

TEST_F(FaceExtrusionProjectionTests, CleanScenePreviewCancelZeroAndFailureRemainClean) {
  for (const bool adjust_existing : {false, true}) {
    SCOPED_TRACE(adjust_existing);
    for (const int outcome : {0, 1, 2, 3}) {
      SCOPED_TRACE(outcome);
      SceneSession session;
      auto &modeling = session.modeling();
      const auto mesh = modeling.create_mesh({
          .document = service_uv_peel_strip_document(),
          .name = "Clean extrusion strip"});
      ASSERT_TRUE(mesh.ok) << mesh.message;
      ASSERT_TRUE(session.editor_mode().set_mode(EditorMode::Edit).ok);
      ASSERT_TRUE(modeling.set_component_selection({
          .modeling_object_id = mesh.object.modeling_object_id,
          .mode = ModelingSelectionMode::Face,
          .faces = {1, 2},
          .active_kind = ModelingActiveSelectionKind::Face,
          .active_face = 2}).ok);
      const auto captured = modeling.capture_face_extrusion_source();
      ASSERT_TRUE(captured.ok) << captured.message;
      if (adjust_existing) {
        ASSERT_TRUE(modeling.apply_face_extrusion(
            captured.source, modeling::FaceExtrudeMode::Region, 2.15).ok);
      }
      session.mark_clean();
      const auto history_steps = session.history().undo_count();
      ASSERT_FALSE(session.dirty());
      const auto begun = adjust_existing
                             ? modeling.begin_face_extrusion_adjustment_preview(captured.source)
                             : modeling.begin_face_extrusion_preview(captured.source);
      ASSERT_TRUE(begun.ok) << begun.message;
      ASSERT_TRUE(modeling.update_face_extrusion_preview(
          captured.source, modeling::FaceExtrudeMode::Region, 1.0).ok);
      EXPECT_FALSE(session.dirty());
      if (outcome == 0) {
        ASSERT_TRUE(modeling.cancel_face_extrusion_preview().ok);
      } else if (outcome == 1) {
        ASSERT_TRUE(modeling.update_face_extrusion_preview(
            captured.source, modeling::FaceExtrudeMode::Region,
            adjust_existing ? 2.15 : 0.0).ok);
        const auto committed = modeling.commit_face_extrusion_preview();
        ASSERT_TRUE(committed.ok) << committed.message;
        EXPECT_FALSE(committed.changed);
      } else if (outcome == 2) {
        modeling.set_modeling_sdk_projection_failure_for_tests(
            ModelingSdkProjectionFailurePoint::AfterSdkMutation);
        const auto failed = modeling.update_face_extrusion_preview(
            captured.source, modeling::FaceExtrudeMode::Region, 2.0);
        EXPECT_FALSE(failed.ok);
        EXPECT_FALSE(failed.changed);
        EXPECT_FALSE(modeling.face_extrusion_preview_active());
      } else {
        const auto unrelated = session.scene().create_entity(
            "Concurrent edit", EntityKind::Empty);
        ASSERT_NE(unrelated, kInvalidEntityId);
        ASSERT_TRUE(modeling.cancel_face_extrusion_preview().ok);
        EXPECT_TRUE(session.scene().exists(unrelated));
      }
      EXPECT_EQ(session.dirty(), outcome == 3);
      EXPECT_EQ(session.history().undo_count(), history_steps);
    }
  }
}

} // namespace face_extrusion_projection

namespace curve_projection {

using namespace quader::test::curve;

std::uint64_t overlay_frame_fingerprint(const ViewportOverlayFrame &frame) {
  std::uint64_t hash = 1469598103934665603ULL;
  const auto mix = [&hash](std::uint64_t value) {
    hash ^= value;
    hash *= 1099511628211ULL;
  };
  const auto mix_float = [&mix](float value) {
    mix(std::bit_cast<std::uint32_t>(value));
  };
  const auto mix_vec3 = [&mix_float](QVec3 value) {
    mix_float(value.x);
    mix_float(value.y);
    mix_float(value.z);
  };
  const auto mix_element = [&mix](const ViewportOverlayElementRef &element) {
    mix(static_cast<std::uint32_t>(element.object_id));
    mix(static_cast<std::uint32_t>(element.vertex_id));
    mix(static_cast<std::uint32_t>(element.edge_a));
    mix(static_cast<std::uint32_t>(element.edge_b));
    mix(static_cast<std::uint32_t>(element.face_id));
  };

  mix(frame.lines.size());
  for (const ViewportOverlayLine &line : frame.lines) {
    mix(static_cast<std::uint64_t>(line.layer));
    mix_vec3(line.start);
    mix_vec3(line.end);
    mix(line.color);
    mix_float(line.width_pixels);
    mix(static_cast<std::uint64_t>(line.style_role));
    mix(static_cast<std::uint64_t>(line.source));
    mix_element(line.element);
  }
  mix(frame.triangles.size());
  for (const ViewportOverlayTriangle &triangle : frame.triangles) {
    mix(static_cast<std::uint64_t>(triangle.layer));
    mix_vec3(triangle.a);
    mix_vec3(triangle.b);
    mix_vec3(triangle.c);
    mix(triangle.color);
    mix(static_cast<std::uint64_t>(triangle.style_role));
    mix(static_cast<std::uint64_t>(triangle.source));
    mix_element(triangle.element);
  }
  mix(frame.points.size());
  for (const ViewportOverlayPoint &point : frame.points) {
    mix(static_cast<std::uint64_t>(point.layer));
    mix_vec3(point.position);
    mix(point.color);
    mix_float(point.size_pixels);
    mix(static_cast<std::uint64_t>(point.style_role));
    mix(static_cast<std::uint64_t>(point.source));
    mix_element(point.element);
    mix(point.outline_growth_pixels.has_value());
    if (point.outline_growth_pixels.has_value()) {
      mix_float(*point.outline_growth_pixels);
    }
  }
  mix(frame.icons.size());
  for (const ViewportOverlayIcon &icon : frame.icons) {
    mix(static_cast<std::uint64_t>(icon.layer));
    mix(static_cast<std::uint64_t>(icon.kind));
    mix_vec3(icon.position);
    mix(icon.color);
    mix_float(icon.size_pixels);
    mix(static_cast<std::uint64_t>(icon.source));
    mix_element(icon.element);
  }
  mix(frame.hover_target_selected);
  return hash;
}

class ModelingSelectionProjectionRollbackTests : public testing::Test {
protected:
  ModelingSelectionProjectionRollbackTests()
      : api_session_(QuaderApiSessionInternalAccess::from_scene_session(session_)),
        editor_(api_session_) {}

  void inject(ModelingSdkProjectionFailurePoint point) {
    session_.modeling().set_modeling_sdk_projection_failure_for_tests(point);
  }

  SceneSession session_;
  QuaderApiSession api_session_;
  QuaderEditorApi editor_;
};

TEST_F(ModelingSelectionProjectionRollbackTests,
       ComponentModeFailureRestoresLightPrimaryAndWarmSelectionOverlay) {
  const auto box = create_box(editor_);
  const auto light = editor_.scene().create_point_light({.name = "Conversion Light"});
  ASSERT_TRUE(light.ok());
  ASSERT_TRUE(editor_.selection().select_object(box).ok());
  ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Edit).ok());
  const auto selected_vertices = [&] {
    std::vector<std::pair<int, int>> ids;
    for (const auto &object : session_.modeling().export_selection().objects) {
      for (const auto &vertex : object.selected_vertices) {
        ids.emplace_back(object.modeling_object_id, vertex.id);
      }
    }
    std::ranges::sort(ids);
    return ids;
  };
  for (const auto failure : {ModelingSdkProjectionFailurePoint::AfterSdkMutation,
                            ModelingSdkProjectionFailurePoint::SelectionScenePublication}) {
    SCOPED_TRACE(static_cast<int>(failure));
    ASSERT_TRUE(editor_.selection().select_vertices(box, {1, 2, 3, 4}).ok());
    ASSERT_EQ(session_.modeling().export_selection().summary.selected_vertex_count, 4u);
    ASSERT_TRUE(session_.scene().set_selected(light.value().id, true));
    ASSERT_TRUE(session_.scene().set_active_selection(light.value().id));
    ASSERT_EQ(session_.modeling().export_selection().summary.selected_vertex_count, 4u);
    ASSERT_EQ(session_.modeling().export_selection().summary.mode, ModelingSelectionMode::Vertex);
    ASSERT_EQ(session_.scene().primary_selection(), light.value().id);
    const auto selection = session_.modeling().export_selection().summary;
    const auto vertices = selected_vertices();
    ASSERT_EQ(vertices, (std::vector<std::pair<int, int>>{
        {box.modeling_object_id, 1}, {box.modeling_object_id, 2},
        {box.modeling_object_id, 3}, {box.modeling_object_id, 4}}));
    const auto scene_selection = session_.scene().selected_entities();
    const auto components = session_.modeling().selected_component_entities();
    const std::vector<EntityId> component_entities(components.begin(), components.end());
    const auto revisions = editor_.revisions();
    const auto scene_selection_revision = session_.scene().selection_revision();
    const auto history = editor_.query().history();
    const auto history_revision = session_.history().revision();
    const auto signature = session_.modeling().selection_overlay_signature();
    const auto fingerprint = overlay_frame_fingerprint(
        session_.modeling().selection_overlay_frame());
    inject(failure);
    const auto failed = editor_.selection().set_mode(QuaderSelectionMode::Face);
    inject(ModelingSdkProjectionFailurePoint::None);
    EXPECT_FALSE(failed.ok());
    EXPECT_FALSE(failed.changed());
    const auto restored = session_.modeling().export_selection().summary;
    EXPECT_EQ(restored.mode, ModelingSelectionMode::Vertex);
    EXPECT_EQ(restored.active_kind, selection.active_kind);
    EXPECT_EQ(restored.active_object_id, selection.active_object_id);
    EXPECT_EQ(restored.active_vertex_id, selection.active_vertex_id);
    EXPECT_EQ(restored.selected_vertex_count, 4u);
    EXPECT_EQ(selected_vertices(), vertices);
    EXPECT_EQ(session_.scene().selected_entities(), scene_selection);
    EXPECT_EQ(session_.scene().primary_selection(), light.value().id);
    const auto restored_components = session_.modeling().selected_component_entities();
    EXPECT_EQ(std::vector<EntityId>(restored_components.begin(), restored_components.end()),
              component_entities);
    expect_same_revisions(editor_.revisions(), revisions);
    EXPECT_EQ(session_.scene().selection_revision(), scene_selection_revision);
    expect_same_history(editor_.query().history(), history);
    EXPECT_EQ(session_.history().revision(), history_revision);
    EXPECT_EQ(session_.modeling().selection_overlay_signature(), signature);
    EXPECT_EQ(overlay_frame_fingerprint(session_.modeling().selection_overlay_frame()),
              fingerprint);
    ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Face).ok());
    EXPECT_EQ(session_.modeling().export_selection().summary.mode, ModelingSelectionMode::Face);
    EXPECT_EQ(session_.modeling().export_selection().summary.selected_face_count, 1u);
  }
}

TEST_F(ModelingSelectionProjectionRollbackTests,
       ComponentInverseFailureBoundariesRestoreExactSelectionAndOverlay) {
  const QuaderModelingObjectRef first = create_box(editor_);
  const QuaderModelingObjectRef second = create_box(editor_);
  ASSERT_TRUE(editor_.selection().select_objects(
      {first.entity, second.entity}, first.entity).ok());
  ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Edge).ok());
  ASSERT_TRUE(editor_.selection().select_edge(first, 1, 2).ok());
  ASSERT_TRUE(editor_.selection()
                  .select_edge(first, 2, 3, QuaderSelectionEdit::Add)
                  .ok());

  // Both meshes are fixed participants even though only the first has selected
  // edges. Inversion includes the empty participant without enrolling a new mesh.
  session_.modeling().set_viewport_visibility_filter(
      [first_entity = first.entity,
       second_entity = second.entity](EntityId entity) {
        return entity == first_entity || entity == second_entity;
      },
      [] { return 1U; }, [] { return true; });

  const auto exact_selected_edges = [](const ModelingSelectionExport &selection) {
    std::vector<std::array<std::uint64_t, 3>> edges;
    for (const ModelingSelectionExportObject &object : selection.objects) {
      for (const ModelingSelectionExportEdge &edge : object.selected_edges) {
        edges.push_back({
            static_cast<std::uint64_t>(object.modeling_object_id),
            static_cast<std::uint64_t>(edge.a),
            static_cast<std::uint64_t>(edge.b),
        });
      }
    }
    std::ranges::sort(edges);
    return edges;
  };
  const auto exact_selected_objects = [](const ModelingSelectionExport &selection) {
    std::vector<std::pair<int, bool>> objects;
    objects.reserve(selection.objects.size());
    for (const ModelingSelectionExportObject &object : selection.objects) {
      objects.emplace_back(object.modeling_object_id, object.selected);
    }
    std::ranges::sort(objects);
    return objects;
  };

  const ModelingSelectionExport selection_before =
      session_.modeling().export_selection();
  const std::vector<std::array<std::uint64_t, 3>> edges_before =
      exact_selected_edges(selection_before);
  ASSERT_EQ(edges_before,
            (std::vector<std::array<std::uint64_t, 3>>{
                {static_cast<std::uint64_t>(first.modeling_object_id), 1U, 2U},
                {static_cast<std::uint64_t>(first.modeling_object_id), 2U, 3U},
            }));
  ASSERT_EQ(selection_before.summary.mode, ModelingSelectionMode::Edge);
  ASSERT_EQ(selection_before.summary.active_kind,
            ModelingActiveSelectionKind::Edge);
  ASSERT_EQ(selection_before.summary.active_object_id,
            first.modeling_object_id);
  ASSERT_EQ(selection_before.summary.active_edge_a, 2);
  ASSERT_EQ(selection_before.summary.active_edge_b, 3);
  ASSERT_EQ(selection_before.summary.selected_edge_count, 2U);

  const std::vector<std::pair<int, bool>> selected_objects_before =
      exact_selected_objects(selection_before);
  const std::vector<EntityId> selected_scene_entities_before =
      session_.scene().selected_entities();
  const std::span<const EntityId> selected_component_entities =
      session_.modeling().selected_component_entities();
  const std::vector<EntityId> selected_component_entities_before(
      selected_component_entities.begin(), selected_component_entities.end());
  const EntityId primary_scene_selection_before =
      session_.scene().primary_selection();
  ASSERT_EQ(selected_scene_entities_before,
            (std::vector<EntityId>{first.entity, second.entity}));
  ASSERT_EQ(primary_scene_selection_before, first.entity);

  const QuaderRevisionStamp revisions_before = editor_.revisions();
  const QuaderHistorySnapshot history_before = editor_.query().history();
  const std::uint64_t modeling_content_revision_before =
      session_.modeling().content_revision();
  const std::uint64_t modeling_selection_revision_before =
      session_.modeling().selection_revision();
  const std::uint64_t scene_revision_before = session_.scene().revision();
  const std::uint64_t scene_selection_revision_before =
      session_.scene().selection_revision();
  const std::uint64_t assets_revision_before = session_.assets().revision();
  const std::uint64_t overlay_signature_before =
      session_.modeling().selection_overlay_signature();
  const std::uint64_t overlay_fingerprint_before = overlay_frame_fingerprint(
      session_.modeling().selection_overlay_frame());

  constexpr std::array kFailures{
      ModelingSdkProjectionFailurePoint::AfterSdkMutation,
      ModelingSdkProjectionFailurePoint::SelectionScenePublication,
  };
  for (const ModelingSdkProjectionFailurePoint failure_point : kFailures) {
    SCOPED_TRACE(static_cast<int>(failure_point));
    inject(failure_point);
    const QuaderApiVoidResult failed = editor_.selection().select_inverse();
    inject(ModelingSdkProjectionFailurePoint::None);
    EXPECT_FALSE(failed.ok());
    EXPECT_FALSE(failed.changed());

    const ModelingSelectionExport selection_after =
        session_.modeling().export_selection();
    EXPECT_EQ(exact_selected_edges(selection_after), edges_before);
    EXPECT_EQ(exact_selected_objects(selection_after), selected_objects_before);
    EXPECT_EQ(selection_after.summary.mode, selection_before.summary.mode);
    EXPECT_EQ(selection_after.summary.has_selection,
              selection_before.summary.has_selection);
    EXPECT_EQ(selection_after.summary.active_kind,
              selection_before.summary.active_kind);
    EXPECT_EQ(selection_after.summary.active_object_id,
              selection_before.summary.active_object_id);
    EXPECT_EQ(selection_after.summary.active_edge_a,
              selection_before.summary.active_edge_a);
    EXPECT_EQ(selection_after.summary.active_edge_b,
              selection_before.summary.active_edge_b);
    EXPECT_EQ(selection_after.summary.selected_object_count,
              selection_before.summary.selected_object_count);
    EXPECT_EQ(selection_after.summary.selected_edge_count,
              selection_before.summary.selected_edge_count);
    EXPECT_EQ(selection_after.summary.content_revision,
              selection_before.summary.content_revision);
    EXPECT_EQ(selection_after.summary.selection_revision,
              selection_before.summary.selection_revision);
    const std::span<const EntityId> selected_component_entities_after =
        session_.modeling().selected_component_entities();
    EXPECT_EQ(std::vector<EntityId>(selected_component_entities_after.begin(),
                                    selected_component_entities_after.end()),
              selected_component_entities_before);

    EXPECT_EQ(session_.scene().selected_entities(),
              selected_scene_entities_before);
    EXPECT_EQ(session_.scene().primary_selection(),
              primary_scene_selection_before);
    EXPECT_EQ(session_.modeling().content_revision(),
              modeling_content_revision_before);
    EXPECT_EQ(session_.modeling().selection_revision(),
              modeling_selection_revision_before);
    EXPECT_EQ(session_.scene().revision(), scene_revision_before);
    EXPECT_EQ(session_.scene().selection_revision(),
              scene_selection_revision_before);
    EXPECT_EQ(session_.assets().revision(), assets_revision_before);
    expect_same_revisions(editor_.revisions(), revisions_before);
    expect_same_history(editor_.query().history(), history_before);

    EXPECT_EQ(session_.modeling().selection_overlay_signature(),
              overlay_signature_before);
    EXPECT_EQ(overlay_frame_fingerprint(
                  session_.modeling().selection_overlay_frame()),
              overlay_fingerprint_before);
  }
}

/// Exercises native publication checkpoints, retained asset revisions, workspace
/// hydration and out-of-band scene changes. Ordinary edit intent still uses the
/// public API; only native boundary injection and inspection bypass that facade.
class CurveModelingProjectionTests : public testing::Test {
protected:
  CurveModelingProjectionTests()
      : api_session_(QuaderApiSessionInternalAccess::from_scene_session(session_)),
        editor_(api_session_) {}

  void inject(ModelingSdkProjectionFailurePoint point) {
    session_.modeling().set_modeling_sdk_projection_failure_for_tests(point);
  }

  SceneSession session_;
  QuaderApiSession api_session_;
  QuaderEditorApi editor_;
};

TEST_F(CurveModelingProjectionTests,
       SolidCurveMaterialAvailabilityRequiresAReference) {
  const QuaderModelingObjectRef curve = create_curve(editor_);
  ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Object).ok());
  ASSERT_TRUE(editor_.selection().select_object(curve).ok());
  ASSERT_TRUE(
      editor_.tools().set_setting("curve", "curve.solidify", true).ok());

  const ModelingCommandResult missing_reference =
      session_.modeling().material_selection_status(false);
  EXPECT_FALSE(missing_reference.ok);
  EXPECT_FALSE(missing_reference.changed);
  EXPECT_FALSE(missing_reference.message.empty());
  EXPECT_TRUE(session_.modeling().material_selection_status(true).ok);
}

TEST_F(CurveModelingProjectionTests,
       CancelPreviewRestoresPayloadAndExactOwnerRevisionsAndKeepsRedo) {
  const auto curve = create_curve(editor_);
  const auto original = read_curve(editor_, curve);
  const auto edited = with_middle_height(original, 3.0F);
  ASSERT_TRUE(editor_.modeling().replace_curve(curve, edited).ok());
  ASSERT_TRUE(editor_.history().undo().ok());
  const auto revisions = editor_.revisions();
  const auto history = editor_.query().history();
  ASSERT_TRUE(history.can_redo);
  const auto identity = editor_.query().entity(curve.entity);
  ASSERT_TRUE(identity.has_value());
  const auto asset_revision = session_.assets().curve_revision(identity->curve);
  const auto publication = read_curve(editor_, curve).content_identity();

  ASSERT_TRUE(editor_.modeling().begin_curve_preview(curve).ok());
  ASSERT_TRUE(editor_.modeling().update_curve_preview(
      with_middle_height(original, 7.0F)).ok());
  ASSERT_TRUE(editor_.modeling().cancel_curve_preview().ok());
  expect_curve(editor_, curve, original);
  EXPECT_EQ(read_curve(editor_, curve).content_identity(), publication);
  EXPECT_EQ(session_.assets().curve_revision(identity->curve), asset_revision);
  expect_same_revisions(editor_.revisions(), revisions);
  expect_same_history(editor_.query().history(), history);
  ASSERT_TRUE(editor_.history().redo().ok());
  expect_curve(editor_, curve, edited);
}

TEST_F(CurveModelingProjectionTests,
       NoOpCurvePreviewUpdateDoesNotPublishAndKeepsRedo) {
  const auto curve = create_curve(editor_);
  const auto original = read_curve(editor_, curve);
  const auto edited = with_middle_height(original, 3.0F);
  ASSERT_TRUE(editor_.modeling().replace_curve(curve, edited).ok());
  ASSERT_TRUE(editor_.history().undo().ok());
  const auto revisions = editor_.revisions();
  const auto history = editor_.query().history();
  ASSERT_TRUE(history.can_redo);
  const auto identity = editor_.query().entity(curve.entity);
  ASSERT_TRUE(identity.has_value());
  const auto asset_revision = session_.assets().curve_revision(identity->curve);

  ASSERT_TRUE(editor_.modeling().begin_curve_preview(curve).ok());
  const ModelingSdkProjectionStats before_update =
      session_.modeling().modeling_sdk_projection_stats_for_tests();
  const auto unchanged = editor_.modeling().update_curve_preview(original);
  ASSERT_TRUE(unchanged.ok()) << unchanged.error_message();
  EXPECT_FALSE(unchanged.changed());
  const ModelingSdkProjectionStats after_update =
      session_.modeling().modeling_sdk_projection_stats_for_tests();
  EXPECT_EQ(after_update.curve_preview_checkpoint_capture_count,
            before_update.curve_preview_checkpoint_capture_count);
  EXPECT_EQ(after_update.curve_preview_checkpoint_curve_asset_count,
            before_update.curve_preview_checkpoint_curve_asset_count);
  EXPECT_EQ(after_update.preview_publication_count,
            before_update.preview_publication_count);
  EXPECT_EQ(after_update.asset_publication_count,
            before_update.asset_publication_count);
  EXPECT_EQ(after_update.scene_publication_count,
            before_update.scene_publication_count);

  const auto committed = editor_.modeling().commit_curve_preview();
  ASSERT_TRUE(committed.ok()) << committed.error_message();
  EXPECT_FALSE(committed.changed());
  expect_curve(editor_, curve, original);
  EXPECT_EQ(session_.assets().curve_revision(identity->curve), asset_revision);
  expect_same_revisions(editor_.revisions(), revisions);
  expect_same_history(editor_.query().history(), history);
  ASSERT_TRUE(editor_.history().redo().ok());
  expect_curve(editor_, curve, edited);
}

TEST_F(CurveModelingProjectionTests,
       CreationFailureBoundariesRestoreSceneAssetsSelectionAndRedo) {
  const auto mesh = create_box(editor_);
  ASSERT_TRUE(editor_.scene().rename(mesh.entity, "Renamed Companion").ok());
  ASSERT_TRUE(editor_.history().undo().ok());
  constexpr std::array kFailures{
      ModelingSdkProjectionFailurePoint::AfterSdkMutation,
      ModelingSdkProjectionFailurePoint::AfterAssetInstall,
      ModelingSdkProjectionFailurePoint::AfterEntityCreate,
      ModelingSdkProjectionFailurePoint::AfterSceneBinding,
      ModelingSdkProjectionFailurePoint::AfterSelection,
      ModelingSdkProjectionFailurePoint::BeforeCommit};
  const auto revisions = editor_.revisions();
  const auto history = editor_.query().history();
  const auto entities = session_.scene().entities();
  const auto selected = session_.scene().selected_entities();
  const auto active = session_.scene().primary_selection();
  const auto scene_selection_revision = session_.scene().selection_revision();
  const auto asset_counts = editor_.query().assets();
  ASSERT_TRUE(history.can_redo);
  for (const auto point : kFailures) {
    SCOPED_TRACE(static_cast<int>(point));
    inject(point);
    const auto failed = editor_.modeling().create_curve(example_curve_args());
    EXPECT_FALSE(failed.ok());
    EXPECT_FALSE(failed.changed());
    inject(ModelingSdkProjectionFailurePoint::None);
    expect_same_revisions(editor_.revisions(), revisions);
    expect_same_history(editor_.query().history(), history);
    EXPECT_EQ(session_.scene().entities(), entities);
    EXPECT_EQ(session_.scene().selected_entities(), selected);
    EXPECT_EQ(session_.scene().primary_selection(), active);
    EXPECT_EQ(session_.scene().selection_revision(), scene_selection_revision);
    EXPECT_EQ(editor_.query().assets().curve_count, asset_counts.curve_count);
    EXPECT_EQ(editor_.query().assets().mesh_count, asset_counts.mesh_count);
    EXPECT_EQ(editor_.query().modeling_objects().size(), 1U);
  }
  const auto retry = editor_.modeling().create_curve(example_curve_args());
  ASSERT_TRUE(retry.ok()) << retry.error_message();
  EXPECT_EQ(editor_.query().modeling_objects().size(), 2U);
}

TEST_F(CurveModelingProjectionTests,
       ReplacementFailureBoundariesRestoreAuthoredAndProjectedCurve) {
  const auto curve = create_curve(editor_);
  const auto original = read_curve(editor_, curve);
  ASSERT_TRUE(editor_.modeling().set_curve_closed(curve, true).ok());
  ASSERT_TRUE(editor_.history().undo().ok());
  // Closing and undoing retires a segment ID. Build the candidate from a fresh
  // snapshot so fault injection reaches publication, not the stale-ID guard.
  const auto edited = with_middle_height(read_curve(editor_, curve), 4.0F);
  const auto revisions = editor_.revisions();
  const auto history = editor_.query().history();
  const auto identity = editor_.query().entity(curve.entity);
  ASSERT_TRUE(identity.has_value());
  const auto asset_revision = session_.assets().curve_revision(identity->curve);
  constexpr std::array kFailures{
      ModelingSdkProjectionFailurePoint::AfterSdkMutation,
      ModelingSdkProjectionFailurePoint::CommittedAssetReplace,
      ModelingSdkProjectionFailurePoint::CommittedSceneNotify,
      ModelingSdkProjectionFailurePoint::BeforeCommit};
  for (const auto point : kFailures) {
    SCOPED_TRACE(static_cast<int>(point));
    inject(point);
    const auto failed = editor_.modeling().replace_curve(curve, edited);
    EXPECT_FALSE(failed.ok());
    EXPECT_FALSE(failed.changed());
    EXPECT_EQ(failed.error_message(), "Curve projection could not be published.");
    inject(ModelingSdkProjectionFailurePoint::None);
    expect_curve(editor_, curve, original);
    expect_same_revisions(editor_.revisions(), revisions);
    expect_same_history(editor_.query().history(), history);
    EXPECT_EQ(session_.assets().curve_revision(identity->curve), asset_revision);
    const auto *projected = session_.assets().curve(identity->curve);
    ASSERT_NE(projected, nullptr);
    ASSERT_NE(projected->document, nullptr);
    EXPECT_TRUE(projected->document->has_same_geometry(original));
  }
  const auto retry = editor_.modeling().replace_curve(curve, edited);
  ASSERT_TRUE(retry.ok()) << retry.error_message();
  expect_curve(editor_, curve, edited);
}

TEST_F(CurveModelingProjectionTests,
       PreviewBeginAndUpdateFailuresRestoreOriginalAndEndPreview) {
  const auto curve = create_curve(editor_);
  const auto original = read_curve(editor_, curve);
  const auto first = with_middle_height(original, 2.0F);
  const auto second = with_middle_height(original, 4.0F);
  const auto original_revisions = editor_.revisions();
  inject(ModelingSdkProjectionFailurePoint::AfterSdkMutation);
  EXPECT_FALSE(editor_.modeling().begin_curve_preview(curve).ok());
  inject(ModelingSdkProjectionFailurePoint::None);
  expect_same_revisions(editor_.revisions(), original_revisions);
  const auto original_history = editor_.query().history();
  constexpr std::array kFailures{
      ModelingSdkProjectionFailurePoint::AfterSdkMutation,
      ModelingSdkProjectionFailurePoint::PreviewAssetReplace,
      ModelingSdkProjectionFailurePoint::PreviewSceneNotify};
  for (const auto point : kFailures) {
    SCOPED_TRACE(static_cast<int>(point));
    ASSERT_TRUE(editor_.modeling().begin_curve_preview(curve).ok());
    ASSERT_TRUE(editor_.modeling().update_curve_preview(first).ok());
    inject(point);
    const auto failed = editor_.modeling().update_curve_preview(second);
    EXPECT_FALSE(failed.ok());
    EXPECT_FALSE(failed.changed());
    inject(ModelingSdkProjectionFailurePoint::None);
    expect_curve(editor_, curve, original);
    expect_curve(editor_, curve, original, false);
    expect_same_revisions(editor_.revisions(), original_revisions);
    expect_same_history(editor_.query().history(), original_history);
    const auto inactive_cancel = editor_.modeling().cancel_curve_preview();
    EXPECT_TRUE(inactive_cancel.ok());
    EXPECT_FALSE(inactive_cancel.changed());
    EXPECT_FALSE(session_.modeling().curve_document_preview_active());
  }
  ASSERT_TRUE(editor_.modeling().begin_curve_preview(curve).ok());
  ASSERT_TRUE(editor_.modeling().update_curve_preview(second).ok());
  expect_curve(editor_, curve, second);
  ASSERT_TRUE(editor_.modeling().cancel_curve_preview().ok());
  expect_curve(editor_, curve, original);
  expect_same_revisions(editor_.revisions(), original_revisions);
}

TEST_F(CurveModelingProjectionTests,
       ParticipantAssetMutationRejectsNoOpUpdateAndCancelWithoutRewind) {
  const auto curve = create_curve(editor_);
  const auto original = read_curve(editor_, curve);
  const auto edited = with_middle_height(original, 4.0F);
  ASSERT_TRUE(editor_.modeling().begin_curve_preview(curve).ok());
  ASSERT_TRUE(editor_.modeling().update_curve_preview(edited).ok());
  const auto identity = editor_.query().entity(curve.entity);
  ASSERT_TRUE(identity.has_value());
  const CurveAsset *published = session_.assets().curve(identity->curve);
  ASSERT_NE(published, nullptr);
  CurveAsset external = *published;
  external.name = "Externally Mutated Preview Asset";
  ASSERT_TRUE(
      session_.assets().replace_curve(identity->curve, std::move(external)));
  const auto external_revision =
      session_.assets().curve_revision(identity->curve);
  const auto revisions = editor_.revisions();
  const auto history = editor_.query().history();

  const auto no_op = editor_.modeling().update_curve_preview(edited);
  EXPECT_FALSE(no_op.ok());
  EXPECT_FALSE(no_op.changed());
  EXPECT_EQ(no_op.error_message(),
            "Curve preview participants changed outside this preview.");
  const auto canceled = editor_.modeling().cancel_curve_preview();
  EXPECT_FALSE(canceled.ok());
  EXPECT_FALSE(canceled.changed());
  ASSERT_TRUE(session_.modeling().curve_document_preview_active());
  const CurveAsset *retained = session_.assets().curve(identity->curve);
  ASSERT_NE(retained, nullptr);
  EXPECT_EQ(retained->name, "Externally Mutated Preview Asset");
  EXPECT_EQ(session_.assets().curve_revision(identity->curve),
            external_revision);
  expect_curve(editor_, curve, edited);
  expect_curve(editor_, curve, original, false);
  expect_same_revisions(editor_.revisions(), revisions);
  expect_same_history(editor_.query().history(), history);
  session_.modeling().reset_from_scene();
}

TEST_F(CurveModelingProjectionTests,
       ParticipantCurveBindingMutationRejectsCancelWithoutRewind) {
  const auto source = create_curve(editor_);
  const auto peer = create_curve(editor_);
  const auto original = read_curve(editor_, source);
  ASSERT_TRUE(editor_.modeling().begin_curve_preview(source).ok());
  ASSERT_TRUE(editor_.modeling().update_curve_preview(
      with_middle_height(original, 4.0F)).ok());
  const auto peer_identity = editor_.query().entity(peer.entity);
  ASSERT_TRUE(peer_identity.has_value());
  ASSERT_TRUE(session_.scene().set_curve(
      source.entity, CurveComponent{.curve = peer_identity->curve}));
  const auto binding_revision = session_.scene().curve_binding_revision();
  const auto revisions = editor_.revisions();
  const auto history = editor_.query().history();

  const auto canceled = editor_.modeling().cancel_curve_preview();
  EXPECT_FALSE(canceled.ok());
  EXPECT_FALSE(canceled.changed());
  EXPECT_EQ(canceled.error_message(),
            "Curve preview participants changed outside this preview.");
  ASSERT_TRUE(session_.modeling().curve_document_preview_active());
  const CurveComponent *retained = session_.scene().curve(source.entity);
  ASSERT_NE(retained, nullptr);
  EXPECT_EQ(retained->curve, peer_identity->curve);
  EXPECT_EQ(session_.scene().curve_binding_revision(), binding_revision);
  expect_same_revisions(editor_.revisions(), revisions);
  expect_same_history(editor_.query().history(), history);
  session_.modeling().reset_from_scene();
}

TEST_F(CurveModelingProjectionTests,
       PreviewUpdatesCheckpointOnlyCurvesAndRetainMeshSelectionProjection) {
  const auto mesh = create_box(editor_);
  const auto curve = create_curve(editor_);
  const auto mesh_identity = editor_.query().entity(mesh.entity);
  ASSERT_TRUE(mesh_identity.has_value());
  const MeshAsset *mesh_asset = session_.assets().mesh(mesh_identity->mesh);
  ASSERT_NE(mesh_asset, nullptr);
  const MeshAsset shared_payload = *mesh_asset;
  const auto curve_identity = editor_.query().entity(curve.entity);
  ASSERT_TRUE(curve_identity.has_value());
  const CurveAsset *curve_asset =
      session_.assets().curve(curve_identity->curve);
  ASSERT_NE(curve_asset, nullptr);
  const CurveAsset shared_curve_payload = *curve_asset;
  constexpr std::size_t kUnrelatedObjectCount = 128U;
  for (std::size_t index = 0; index < kUnrelatedObjectCount; ++index) {
    const AssetId unrelated_mesh =
        session_.assets().add_mesh(shared_payload);
    ASSERT_NE(unrelated_mesh, kInvalidAssetId);
    const EntityId mesh_entity =
        session_.scene().create_empty("Unrelated Mesh");
    ASSERT_NE(mesh_entity, kInvalidEntityId);
    ASSERT_TRUE(session_.scene().set_mesh(
        mesh_entity, MeshComponent{.mesh = unrelated_mesh}));

    const AssetId unrelated_curve =
        session_.assets().add_curve(shared_curve_payload);
    ASSERT_NE(unrelated_curve, kInvalidAssetId);
    const EntityId curve_entity =
        session_.scene().create_empty("Unrelated Curve");
    ASSERT_NE(curve_entity, kInvalidEntityId);
    ASSERT_TRUE(session_.scene().set_curve(
        curve_entity, CurveComponent{.curve = unrelated_curve}));
  }

  const auto original = read_curve(editor_, curve);
  ASSERT_TRUE(editor_.modeling().begin_curve_preview(curve).ok());
  const ModelingRay ray{
      .origin = {0.0F, 0.0F, 10.0F},
      .direction = {0.0F, 0.0F, -1.0F},
  };
  (void)session_.modeling().pick_mesh_surface_distance(ray);
  const ModelingMeshSnapQueryResult warm_snap =
      session_.modeling().pick_mesh_snap_query(ray, 2.0, false);
  ASSERT_TRUE(warm_snap.vertex_target.has_value());
  ASSERT_TRUE(warm_snap.nearest_surface_ray_distance.has_value());
  const std::uint64_t mesh_snap_queries_before =
      session_.modeling().mesh_snap_query_count();
  const std::uint64_t picking_projections_before =
      session_.modeling().transformed_picking_projection_build_count();
  const std::uint64_t picking_bounds_before =
      session_.modeling().world_pick_bounds_build_count();
  const std::uint64_t picking_face_geometry_before =
      session_.modeling().local_face_geometry_build_count();
  const ModelingSdkProjectionStats before =
      session_.modeling().modeling_sdk_projection_stats_for_tests();

  ASSERT_TRUE(editor_.modeling().update_curve_preview(
      with_middle_height(original, 3.0F)).ok());
  (void)session_.modeling().pick_mesh_surface_distance(ray);
  const ModelingMeshSnapQueryResult first_snap =
      session_.modeling().pick_mesh_snap_query(ray, 2.0, false);
  ASSERT_TRUE(first_snap.vertex_target.has_value());
  ASSERT_TRUE(first_snap.nearest_surface_ray_distance.has_value());
  const ModelingSdkProjectionStats after_first =
      session_.modeling().modeling_sdk_projection_stats_for_tests();
  EXPECT_EQ(after_first.curve_preview_checkpoint_capture_count,
            before.curve_preview_checkpoint_capture_count + 1U);
  EXPECT_EQ(after_first.curve_preview_checkpoint_curve_asset_count,
            before.curve_preview_checkpoint_curve_asset_count + 1U);
  EXPECT_EQ(after_first.curve_preview_shared_binding_scan_count,
            before.curve_preview_shared_binding_scan_count);
  EXPECT_EQ(after_first.curve_preview_shared_binding_entity_visit_count,
            before.curve_preview_shared_binding_entity_visit_count);
  EXPECT_EQ(after_first.native_selection_projection_build_count,
            before.native_selection_projection_build_count);
  EXPECT_EQ(session_.modeling().transformed_picking_projection_build_count(),
            picking_projections_before);
  EXPECT_EQ(session_.modeling().world_pick_bounds_build_count(),
            picking_bounds_before);
  EXPECT_EQ(session_.modeling().local_face_geometry_build_count(),
            picking_face_geometry_before);
  EXPECT_EQ(session_.modeling().mesh_snap_query_count(),
            mesh_snap_queries_before + 1u);

  ASSERT_TRUE(editor_.modeling().update_curve_preview(
      with_middle_height(original, 5.0F)).ok());
  (void)session_.modeling().pick_mesh_surface_distance(ray);
  const ModelingMeshSnapQueryResult second_snap =
      session_.modeling().pick_mesh_snap_query(ray, 2.0, false);
  ASSERT_TRUE(second_snap.vertex_target.has_value());
  ASSERT_TRUE(second_snap.nearest_surface_ray_distance.has_value());
  const ModelingSdkProjectionStats after_second =
      session_.modeling().modeling_sdk_projection_stats_for_tests();
  EXPECT_EQ(after_second.curve_preview_checkpoint_capture_count,
            after_first.curve_preview_checkpoint_capture_count + 1U);
  EXPECT_EQ(after_second.curve_preview_checkpoint_curve_asset_count,
            after_first.curve_preview_checkpoint_curve_asset_count + 1U);
  EXPECT_EQ(after_second.native_selection_projection_build_count,
            after_first.native_selection_projection_build_count);
  EXPECT_EQ(session_.modeling().transformed_picking_projection_build_count(),
            picking_projections_before);
  EXPECT_EQ(session_.modeling().world_pick_bounds_build_count(),
            picking_bounds_before);
  EXPECT_EQ(session_.modeling().local_face_geometry_build_count(),
            picking_face_geometry_before);
  EXPECT_EQ(session_.modeling().mesh_snap_query_count(),
            mesh_snap_queries_before + 2u);

  ASSERT_TRUE(session_.assets().update_mesh(
      mesh_identity->mesh,
      [](MeshAsset &asset) { asset.name = "Externally Updated Mesh"; }));
  (void)session_.modeling().pick_mesh_surface_distance(ray);
  const ModelingSdkProjectionStats after_mesh =
      session_.modeling().modeling_sdk_projection_stats_for_tests();
  EXPECT_EQ(after_mesh.native_selection_projection_build_count,
            after_second.native_selection_projection_build_count + 1U);
  ASSERT_TRUE(editor_.modeling().cancel_curve_preview().ok());
  expect_curve(editor_, curve, original);
  ASSERT_NE(session_.assets().mesh(mesh_identity->mesh), nullptr);
  EXPECT_EQ(session_.assets().mesh(mesh_identity->mesh)->name,
            "Externally Updated Mesh");

  const auto before_selection =
      session_.modeling().modeling_sdk_projection_stats_for_tests();
  ASSERT_TRUE(editor_.selection().select_object(mesh).ok());
  (void)session_.modeling().pick_mesh_surface_distance(ray);
  const auto after_selection =
      session_.modeling().modeling_sdk_projection_stats_for_tests();
  EXPECT_GT(after_selection.native_selection_projection_build_count,
            before_selection.native_selection_projection_build_count);
}

TEST_F(CurveModelingProjectionTests,
       RepeatedCurvePreviewUpdatesRetainPolygonSelectionOverlay) {
  constexpr std::size_t kMeshCount = 64U;
  std::vector<QuaderModelingObjectRef> meshes;
  meshes.reserve(kMeshCount);
  for (std::size_t index = 0; index < kMeshCount; ++index) {
    meshes.push_back(create_box(editor_));
  }
  const auto curve = create_curve(editor_);
  const auto original = read_curve(editor_, curve, false);
  ASSERT_TRUE(editor_.selection().select_object(meshes.front()).ok());
  ASSERT_TRUE(editor_.modeling().begin_curve_preview(curve).ok());

  const ViewportOverlayFrame baseline =
      session_.modeling().selection_overlay_frame();
  ASSERT_FALSE(baseline.lines.empty() && baseline.triangles.empty() &&
               baseline.points.empty());
  const std::uint64_t baseline_fingerprint =
      overlay_frame_fingerprint(baseline);
  const std::uint64_t baseline_signature =
      session_.modeling().selection_overlay_signature();
  for (const float height : {2.0F, 3.0F, 4.0F}) {
    ASSERT_TRUE(editor_.modeling().update_curve_preview(
        with_middle_height(original, height)).ok());
    const ViewportOverlayFrame after_preview =
        session_.modeling().selection_overlay_frame();
    EXPECT_EQ(overlay_frame_fingerprint(after_preview), baseline_fingerprint);
    EXPECT_EQ(session_.modeling().selection_overlay_signature(),
              baseline_signature);
  }

  EXPECT_FALSE(
      editor_.selection().set_mode(QuaderSelectionMode::Face).ok());
  const ViewportOverlayFrame after_selection =
      session_.modeling().selection_overlay_frame();
  EXPECT_EQ(overlay_frame_fingerprint(after_selection), baseline_fingerprint);
  ASSERT_TRUE(editor_.modeling().cancel_curve_preview().ok());
}

TEST_F(CurveModelingProjectionTests,
       FailedPreviewCommitKeepsTheCandidateForRetryWithoutHistory) {
  const auto curve = create_curve(editor_);
  const auto original = read_curve(editor_, curve);
  const auto edited = with_middle_height(original, 6.0F);
  ASSERT_TRUE(editor_.modeling().begin_curve_preview(curve).ok());
  ASSERT_TRUE(editor_.modeling().update_curve_preview(edited).ok());
  const auto revisions = editor_.revisions();
  const auto history = editor_.query().history();
  inject(ModelingSdkProjectionFailurePoint::BeforeCommit);
  const auto failed = editor_.modeling().commit_curve_preview();
  EXPECT_FALSE(failed.ok());
  EXPECT_FALSE(failed.changed());
  EXPECT_TRUE(session_.modeling().curve_document_preview_active());
  inject(ModelingSdkProjectionFailurePoint::None);
  expect_curve(editor_, curve, edited);
  expect_curve(editor_, curve, original, false);
  expect_same_revisions(editor_.revisions(), revisions);
  expect_same_history(editor_.query().history(), history);
  ASSERT_TRUE(editor_.modeling().commit_curve_preview().ok());
  expect_curve(editor_, curve, edited, false);
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_curve(editor_, curve, original);
}

TEST_F(CurveModelingProjectionTests,
       RehydrateFromSceneRetainsBothMeshAndCurveDocumentsAndSelection) {
  const auto mesh = create_box(editor_);
  const auto curve = create_curve(editor_);
  const auto document = read_curve(editor_, curve);
  const auto mesh_before = editor_.query().compiled_object_mesh(mesh);
  ASSERT_TRUE(mesh_before.has_value());
  ASSERT_TRUE(editor_.selection().select_objects({mesh, curve}).ok());
  const auto selected = session_.scene().selected_entities();
  const auto active = session_.scene().primary_selection();
  const auto scene_revision = session_.scene().revision();
  const auto asset_revision = session_.assets().revision();

  session_.modeling().reset_from_scene();

  EXPECT_EQ(editor_.query().modeling_objects().size(), 2U);
  EXPECT_EQ(session_.scene().selected_entities(), selected);
  EXPECT_EQ(session_.scene().primary_selection(), active);
  EXPECT_EQ(session_.scene().revision(), scene_revision);
  EXPECT_EQ(session_.assets().revision(), asset_revision);
  expect_curve(editor_, curve, document);
  const auto mesh_after = editor_.query().compiled_object_mesh(mesh);
  ASSERT_TRUE(mesh_after.has_value());
  expect_same_mesh_geometry(*mesh_after, *mesh_before);
  ASSERT_TRUE(editor_.modeling().set_curve_closed(curve, true).ok());
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_curve(editor_, curve, document);
  EXPECT_EQ(editor_.query().modeling_objects().size(), 2U);
}

TEST_F(CurveModelingProjectionTests,
       UnrelatedSceneEditSurvivesCancelAndPointIdsRemainMonotonic) {
  const auto companion = create_box(editor_);
  const auto peer = create_curve(editor_);
  const auto curve = create_curve(editor_);
  const auto original = read_curve(editor_, curve, false);
  ASSERT_TRUE(editor_.modeling().begin_curve_preview(curve).ok());
  auto preview = editor_.modeling().curve_snapshot(curve);
  ASSERT_TRUE(preview.ok()) << preview.error_message();
  ASSERT_TRUE(preview.value().document.append_point(
      {.position = {6.0F, 0.0F, 0.0F}}).ok());
  const auto canceled_point = preview.value().document.points().back().id;
  ASSERT_TRUE(editor_.modeling().update_curve_preview(preview.value().document).ok());

  // An unrelated scene participant may publish while the curve preview is
  // active. Cancel restores only the enrolled curve projection.
  const auto *current_transform = session_.scene().transform(companion.entity);
  ASSERT_NE(current_transform, nullptr);
  auto moved = *current_transform;
  moved.position = {3.0F, 2.0F, 1.0F};
  ASSERT_TRUE(session_.scene().set_transform(companion.entity, moved));
  const auto peer_identity = editor_.query().entity(peer.entity);
  ASSERT_TRUE(peer_identity.has_value());
  const CurveAsset *peer_asset =
      session_.assets().curve(peer_identity->curve);
  ASSERT_NE(peer_asset, nullptr);
  CurveAsset concurrent_curve = *peer_asset;
  concurrent_curve.name = "Concurrent Unrelated Curve";
  const AssetId concurrent_curve_id =
      session_.assets().add_curve(std::move(concurrent_curve));
  ASSERT_NE(concurrent_curve_id, kInvalidAssetId);
  ASSERT_TRUE(session_.scene().set_curve(
      peer.entity, CurveComponent{.curve = concurrent_curve_id}));
  ASSERT_TRUE(editor_.modeling().cancel_curve_preview().ok());
  const auto canceled = read_curve(editor_, curve, false);
  EXPECT_TRUE(canceled.has_same_geometry(original));
  EXPECT_GT(canceled.identity_high_water().next_point_index, canceled_point.index);

  session_.modeling().reset_from_scene();

  const auto retained_peer = editor_.query().entity(peer.entity);
  ASSERT_TRUE(retained_peer.has_value());
  EXPECT_EQ(retained_peer->curve, concurrent_curve_id);
  const CurveAsset *retained_concurrent =
      session_.assets().curve(concurrent_curve_id);
  ASSERT_NE(retained_concurrent, nullptr);
  EXPECT_EQ(retained_concurrent->name, "Concurrent Unrelated Curve");

  auto next = editor_.modeling().curve_snapshot(curve, false);
  ASSERT_TRUE(next.ok()) << next.error_message();
  EXPECT_TRUE(next.value().document.has_same_geometry(original));
  ASSERT_TRUE(next.value().document.append_point(
      {.position = {8.0F, 1.0F, 0.0F}}).ok());
  const auto next_point = next.value().document.points().back().id;
  EXPECT_GT(next_point.index, canceled_point.index);
  const auto replaced = editor_.modeling().replace_curve(curve, next.value().document);
  ASSERT_TRUE(replaced.ok()) << replaced.error_message();
  EXPECT_TRUE(replaced.changed());
  expect_curve(editor_, curve, next.value().document, false);
  const auto entity = editor_.query().entity(companion.entity);
  ASSERT_TRUE(entity.has_value());
  EXPECT_FLOAT_EQ(entity->transform.position.x, 3.0F);
  EXPECT_FLOAT_EQ(entity->transform.position.y, 2.0F);
  EXPECT_FLOAT_EQ(entity->transform.position.z, 1.0F);
}

TEST_F(CurveModelingProjectionTests,
       ParticipantDeletionRejectsCancelWithoutResurrectingTheCurve) {
  const auto mesh = create_box(editor_);
  const auto curve = create_curve(editor_);
  const auto original = read_curve(editor_, curve);
  ASSERT_TRUE(editor_.modeling().begin_curve_preview(curve).ok());
  ASSERT_TRUE(editor_.modeling().update_curve_preview(
      with_middle_height(original, 4.0F)).ok());
  // A structural edit to an enrolled participant invalidates the retained
  // fence. Cancellation must reject it without replaying the baseline.
  ASSERT_TRUE(session_.scene().destroy_entity(curve.entity));
  const auto canceled = editor_.modeling().cancel_curve_preview();
  EXPECT_FALSE(canceled.ok());
  EXPECT_FALSE(canceled.changed());
  EXPECT_FALSE(editor_.query().entity(curve.entity).has_value());
  EXPECT_FALSE(editor_.modeling().curve_snapshot(curve).ok());
  EXPECT_TRUE(editor_.query().entity(mesh.entity).has_value());
  session_.modeling().reset_from_scene();
  EXPECT_EQ(editor_.query().modeling_objects().size(), 1U);
}

TEST_F(CurveModelingProjectionTests,
       SharedCurvePreviewFailureRestoresOriginalBindingAndProjection) {
  const auto source = create_curve(editor_);
  const auto peer = create_curve(editor_);
  const auto source_entity = editor_.query().entity(source.entity);
  ASSERT_TRUE(source_entity.has_value());
  const AssetId shared_asset = source_entity->curve;
  // Simulate a scene owner assigning the same immutable asset to two editable
  // curve entities, then reconcile the native bindings with the SDK workspace.
  ASSERT_TRUE(session_.scene().set_curve(peer.entity, {.curve = shared_asset}));
  session_.modeling().reset_from_scene();
  ASSERT_TRUE(editor_.selection().select_object(source).ok());
  const auto original = read_curve(editor_, source, false);
  const auto edited = with_middle_height(original, 5.0F);
  ASSERT_TRUE(editor_.modeling().begin_curve_preview(source).ok());
  const auto revisions = editor_.revisions();
  const auto history = editor_.query().history();
  const auto asset_ids = session_.assets().curve_ids();
  const auto asset_revision = session_.assets().curve_revision(shared_asset);
  const auto binding_revision = session_.scene().curve_binding_revision();
  const auto render_revision = session_.scene().render_component_revision();

  inject(ModelingSdkProjectionFailurePoint::PreviewSceneNotify);
  const auto failed = editor_.modeling().update_curve_preview(edited);
  EXPECT_FALSE(failed.ok());
  EXPECT_FALSE(failed.changed());
  EXPECT_EQ(failed.error_message(), "Curve preview projection could not be published.");
  inject(ModelingSdkProjectionFailurePoint::None);
  EXPECT_FALSE(session_.modeling().curve_document_preview_active());

  for (const auto object : {source, peer}) {
    const auto entity = editor_.query().entity(object.entity);
    ASSERT_TRUE(entity.has_value());
    EXPECT_EQ(entity->curve, shared_asset);
    EXPECT_NE(session_.assets().curve(entity->curve), nullptr);
    expect_curve(editor_, object, original);
    expect_curve(editor_, object, original, false);
  }
  EXPECT_EQ(session_.assets().curve_ids(), asset_ids);
  EXPECT_EQ(session_.assets().curve_revision(shared_asset), asset_revision);
  EXPECT_EQ(session_.scene().curve_binding_revision(), binding_revision);
  EXPECT_EQ(session_.scene().render_component_revision(), render_revision);
  expect_same_revisions(editor_.revisions(), revisions);
  expect_same_history(editor_.query().history(), history);
  const auto *shared = session_.assets().curve(shared_asset);
  ASSERT_NE(shared, nullptr);
  ASSERT_NE(shared->document, nullptr);
  EXPECT_TRUE(shared->document->has_same_geometry(original));
}

TEST_F(CurveModelingProjectionTests,
       SharedCurvePreviewCancelRebindsOriginalAndRemovesPrivateAsset) {
  const auto source = create_curve(editor_);
  const auto peer = create_curve(editor_);
  const auto source_entity = editor_.query().entity(source.entity);
  ASSERT_TRUE(source_entity.has_value());
  const AssetId shared_asset = source_entity->curve;
  ASSERT_TRUE(session_.scene().set_curve(peer.entity, {.curve = shared_asset}));
  session_.modeling().reset_from_scene();
  ASSERT_TRUE(editor_.selection().select_object(source).ok());
  const auto original = read_curve(editor_, source, false);
  const auto edited = with_middle_height(original, 5.0F);
  const auto revisions = editor_.revisions();
  const auto history = editor_.query().history();
  const auto asset_ids = session_.assets().curve_ids();
  const auto asset_revision = session_.assets().curve_revision(shared_asset);
  const auto binding_revision = session_.scene().curve_binding_revision();
  const auto render_revision = session_.scene().render_component_revision();
  ASSERT_TRUE(editor_.modeling().begin_curve_preview(source).ok());
  ASSERT_TRUE(editor_.modeling().update_curve_preview(edited).ok());
  const auto preview_entity = editor_.query().entity(source.entity);
  ASSERT_TRUE(preview_entity.has_value());
  const AssetId private_asset = preview_entity->curve;
  ASSERT_NE(private_asset, shared_asset);
  ASSERT_NE(session_.assets().curve(private_asset), nullptr);
  EXPECT_EQ(session_.assets().curve_count(), asset_ids.size() + 1U);
  expect_curve(editor_, source, edited);
  expect_curve(editor_, source, original, false);
  expect_curve(editor_, peer, original);

  const auto canceled = editor_.modeling().cancel_curve_preview();
  ASSERT_TRUE(canceled.ok()) << canceled.error_message();
  for (const auto object : {source, peer}) {
    const auto entity = editor_.query().entity(object.entity);
    ASSERT_TRUE(entity.has_value());
    EXPECT_EQ(entity->curve, shared_asset);
    EXPECT_NE(session_.assets().curve(entity->curve), nullptr);
    expect_curve(editor_, object, original, false);
  }
  EXPECT_EQ(session_.assets().curve_ids(), asset_ids);
  EXPECT_FALSE(session_.assets().has_curve(private_asset));
  EXPECT_EQ(session_.assets().curve_revision(shared_asset), asset_revision);
  EXPECT_EQ(session_.scene().curve_binding_revision(), binding_revision);
  EXPECT_EQ(session_.scene().render_component_revision(), render_revision);
  expect_same_revisions(editor_.revisions(), revisions);
  expect_same_history(editor_.query().history(), history);
  const auto *shared = session_.assets().curve(shared_asset);
  ASSERT_NE(shared, nullptr);
  ASSERT_NE(shared->document, nullptr);
  EXPECT_TRUE(shared->document->has_same_geometry(original));
}

TEST_F(CurveModelingProjectionTests,
       SharedCurveCommittedFailureRestoresOriginalBindingAndProjection) {
  const auto source = create_curve(editor_);
  const auto peer = create_curve(editor_);
  const auto source_entity = editor_.query().entity(source.entity);
  ASSERT_TRUE(source_entity.has_value());
  const AssetId shared_asset = source_entity->curve;
  ASSERT_TRUE(session_.scene().set_curve(peer.entity, {.curve = shared_asset}));
  session_.modeling().reset_from_scene();
  ASSERT_TRUE(editor_.selection().select_object(source).ok());
  const auto original = read_curve(editor_, source, false);
  const auto edited = with_middle_height(original, 5.0F);
  const auto revisions = editor_.revisions();
  const auto history = editor_.query().history();
  const auto asset_ids = session_.assets().curve_ids();
  const auto asset_revision = session_.assets().curve_revision(shared_asset);
  const auto binding_revision = session_.scene().curve_binding_revision();
  const auto render_revision = session_.scene().render_component_revision();

  for (const auto point : {ModelingSdkProjectionFailurePoint::CommittedSceneNotify,
                           ModelingSdkProjectionFailurePoint::BeforeCommit}) {
    SCOPED_TRACE(static_cast<int>(point));
    inject(point);
    const auto failed = editor_.modeling().replace_curve(source, edited);
    EXPECT_FALSE(failed.ok());
    EXPECT_FALSE(failed.changed());
    EXPECT_EQ(failed.error_message(), "Curve projection could not be published.");
    inject(ModelingSdkProjectionFailurePoint::None);

    for (const auto object : {source, peer}) {
      const auto entity = editor_.query().entity(object.entity);
      ASSERT_TRUE(entity.has_value());
      EXPECT_EQ(entity->curve, shared_asset);
      EXPECT_NE(session_.assets().curve(entity->curve), nullptr);
      expect_curve(editor_, object, original, false);
    }
    EXPECT_EQ(session_.assets().curve_ids(), asset_ids);
    EXPECT_EQ(session_.assets().curve_revision(shared_asset), asset_revision);
    EXPECT_EQ(session_.scene().curve_binding_revision(), binding_revision);
    EXPECT_EQ(session_.scene().render_component_revision(), render_revision);
    expect_same_revisions(editor_.revisions(), revisions);
    expect_same_history(editor_.query().history(), history);
    const auto *shared = session_.assets().curve(shared_asset);
    ASSERT_NE(shared, nullptr);
    ASSERT_NE(shared->document, nullptr);
    EXPECT_TRUE(shared->document->has_same_geometry(original));
  }
}

} // namespace curve_projection

} // namespace
} // namespace quader
