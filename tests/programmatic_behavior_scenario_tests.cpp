////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "support/behavior_expect.hpp"
#include "support/behavior_scenario.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <string_view>
#include <vector>

namespace {

/// Describes one synthetic component gizmo workflow so the same behavior can be
/// verified across vertex, edge, and face selection modes.
struct ComponentPickCase {
    quader::QuaderSelectionMode mode = quader::QuaderSelectionMode::Face;
    quader::QVec3 ray_origin = {};
    quader::ModelingTransformTool tool = quader::ModelingTransformTool::Move;
};

/// Describes one repeat-rotation workflow across component and object modes.
struct RepeatRotationCase {
  quader::QuaderSelectionMode mode = quader::QuaderSelectionMode::Face;
  quader::QVec3 ray_origin = {};
  const char *label = "";
};

constexpr quader::QVec3 kRepeatRotationWorldPivot{4.0f, -3.0f, 0.25f};

using PointKey = std::array<int, 3>;
using EdgeKey = std::array<int, 6>;

/**
 * Records one directed edge use in a triangulated mesh snapshot.
 */
struct TriangleEdgeUse {
  PointKey from = {};
  PointKey to = {};
};

const quader::test::MeshSnapshot *
mesh_snapshot_for_alias(const quader::test::BehaviorSnapshot &snapshot,
                        std::string_view alias) {
  const auto entity =
      std::find_if(snapshot.entities.begin(), snapshot.entities.end(),
                   [alias](const quader::test::EntitySnapshot &value) {
                     return value.alias == alias;
                   });
  if (entity == snapshot.entities.end()) {
    return nullptr;
  }
  const auto mesh = std::find_if(
      snapshot.meshes.begin(), snapshot.meshes.end(),
      [mesh_id = entity->mesh](const quader::test::MeshSnapshot &value) {
        return value.mesh == mesh_id;
      });
  return mesh == snapshot.meshes.end() ? nullptr : &*mesh;
}

void expect_mesh_bounds_near(const quader::test::MeshSnapshot &actual,
                             const quader::test::MeshSnapshot &expected,
                             float epsilon = 0.001f) {
  EXPECT_NEAR(actual.bounds.min.x, expected.bounds.min.x, epsilon);
  EXPECT_NEAR(actual.bounds.min.y, expected.bounds.min.y, epsilon);
  EXPECT_NEAR(actual.bounds.min.z, expected.bounds.min.z, epsilon);
  EXPECT_NEAR(actual.bounds.max.x, expected.bounds.max.x, epsilon);
  EXPECT_NEAR(actual.bounds.max.y, expected.bounds.max.y, epsilon);
  EXPECT_NEAR(actual.bounds.max.z, expected.bounds.max.z, epsilon);
}

void expect_vec3_near(quader::QVec3 actual, quader::QVec3 expected,
                      float epsilon = 0.001f) {
  EXPECT_NEAR(actual.x, expected.x, epsilon);
  EXPECT_NEAR(actual.y, expected.y, epsilon);
  EXPECT_NEAR(actual.z, expected.z, epsilon);
}

quader::QVec3 rotate_positive_quarter_turn_about(quader::QVec3 point,
                                                  quader::QVec3 pivot) {
  const quader::QVec3 relative = point - pivot;
  return {
      pivot.x - relative.y,
      pivot.y + relative.x,
      point.z,
  };
}

float rotation_radius_xy(quader::QVec3 point, quader::QVec3 pivot) {
  const float x = point.x - pivot.x;
  const float y = point.y - pivot.y;
  return std::sqrt(x * x + y * y);
}

void expect_positive_quarter_turn_step(quader::QVec3 before,
                                       quader::QVec3 after,
                                       quader::QVec3 pivot) {
  expect_vec3_near(after, rotate_positive_quarter_turn_about(before, pivot));
  EXPECT_NEAR(rotation_radius_xy(after, pivot),
              rotation_radius_xy(before, pivot), 0.001f);

  const quader::QVec3 before_relative = before - pivot;
  const quader::QVec3 after_relative = after - pivot;
  EXPECT_NEAR(before_relative.x * after_relative.x +
                  before_relative.y * after_relative.y,
              0.0f, 0.001f);
  EXPECT_GT(before_relative.x * after_relative.y -
                before_relative.y * after_relative.x,
            0.0f);
}

quader::test::DragBeginOptions
repeat_rotation_drag_options(quader::ModelingTransformOperation operation,
                             bool duplicate_selected = false) {
  return {
      .tool = quader::ModelingTransformTool::Rotate,
      .axis = quader::ModelingTransformAxis::Z,
      .operation = operation,
      .pivot = kRepeatRotationWorldPivot,
      .use_selection_center = false,
      .screen_position = {120.0f, 100.0f},
      .screen_pivot = {100.0f, 100.0f},
      .screen_tip = {120.0f, 100.0f},
      .world_direction = {0.0f, 0.0f, 1.0f},
      .grid_snap_enabled = false,
      .duplicate_selected = duplicate_selected,
  };
}

void select_repeat_rotation_target(quader::test::BehaviorScenario &s,
                                   const RepeatRotationCase &test_case) {
  if (test_case.mode == quader::QuaderSelectionMode::Object) {
    s.given().selection_mode(test_case.mode).selected("box");
    return;
  }
  s.given().selection_mode(test_case.mode);
  s.when().pick(test_case.ray_origin);
}

void expect_repeat_rotation_selection(quader::test::BehaviorScenario &s,
                                      quader::QuaderSelectionMode mode) {
  s.then().selection().mode(mode);
  switch (mode) {
  case quader::QuaderSelectionMode::Edge:
    s.then().selection().edge_count(1).active_alias("box");
    break;
  case quader::QuaderSelectionMode::Face:
    s.then().selection().face_count(1).active_alias("box");
    break;
  case quader::QuaderSelectionMode::Object:
    s.then().selection().object_count(1);
    break;
  case quader::QuaderSelectionMode::Vertex:
  case quader::QuaderSelectionMode::Group:
    ADD_FAILURE() << "repeat rotation test received an unsupported mode";
    break;
  }
}

int quantized_component(float value) {
  return static_cast<int>(std::lround(value * 1000.0f));
}

PointKey point_key(quader::QVec3 position) {
  return {
      quantized_component(position.x),
      quantized_component(position.y),
      quantized_component(position.z),
  };
}

EdgeKey edge_key(PointKey first, PointKey second) {
  if (second < first) {
    std::swap(first, second);
  }
  return {
      first[0], first[1], first[2],
      second[0], second[1], second[2],
  };
}

bool shared_triangle_edges_have_opposite_winding(
    const quader::QuaderMeshAsset &mesh) {
  if (mesh.indices.size() % 3U != 0U) {
    return false;
  }

  std::map<EdgeKey, std::vector<TriangleEdgeUse>> uses_by_edge;
  for (std::size_t triangle = 0; triangle < mesh.indices.size();
       triangle += 3U) {
    std::array<PointKey, 3> points;
    for (std::size_t corner = 0; corner < points.size(); ++corner) {
      const std::uint32_t vertex_index = mesh.indices[triangle + corner];
      if (vertex_index >= mesh.vertices.size()) {
        return false;
      }
      points[corner] = point_key(mesh.vertices[vertex_index].position);
    }
    for (std::size_t corner = 0; corner < points.size(); ++corner) {
      const PointKey from = points[corner];
      const PointKey to = points[(corner + 1U) % points.size()];
      if (from == to) {
        return false;
      }
      uses_by_edge[edge_key(from, to)].push_back({from, to});
    }
  }

  for (const auto &[edge, uses] : uses_by_edge) {
    (void)edge;
    if (uses.size() == 1U) {
      continue;
    }
    if (uses.size() != 2U) {
      return false;
    }
    if (uses[0].from != uses[1].to || uses[0].to != uses[1].from) {
      return false;
    }
  }
  return true;
}

void expect_compiled_mesh_winding_coherent(quader::test::BehaviorScenario &s,
                                           std::string_view alias) {
  const std::optional<quader::QuaderMeshAsset> mesh =
      s.api().query().compiled_object_mesh(s.object(alias));
  ASSERT_TRUE(mesh.has_value());
  EXPECT_TRUE(shared_triangle_edges_have_opposite_winding(*mesh));
}

void run_shift_rotate_extrude_stability_case(
    quader::test::BehaviorScenario &s, std::string_view alias) {
  const std::optional<quader::QVec3> center_before =
      s.api().query().selection_center();
  ASSERT_TRUE(center_before.has_value());
  const quader::test::BehaviorSnapshot before =
      s.snapshot("before shift rotate extrude");
  const quader::test::MeshSnapshot *before_mesh =
      mesh_snapshot_for_alias(before, alias);
  ASSERT_NE(before_mesh, nullptr);

  s.when()
      .begin_transform_drag({
          .tool = quader::ModelingTransformTool::Rotate,
          .axis = quader::ModelingTransformAxis::Z,
          .operation = quader::ModelingTransformOperation::Extrude,
          .screen_position = {120.0f, 100.0f},
          .screen_pivot = {100.0f, 100.0f},
          .screen_tip = {120.0f, 100.0f},
          .world_direction = {0.0f, 0.0f, 1.0f},
      })
      .update_transform_drag({116.0f, 112.0f})
      .commit_drag();

  s.then().command().ok_changed();
  s.then()
      .selection()
      .mode(quader::QuaderSelectionMode::Face)
      .face_count(1)
      .active_alias(alias);

  const std::optional<quader::QVec3> center_after =
      s.api().query().selection_center();
  ASSERT_TRUE(center_after.has_value());
  expect_vec3_near(*center_after, *center_before);
  s.then().mesh(alias).vertices_greater_than(*before_mesh);
}

void expect_selection_count_for_mode(quader::test::BehaviorScenario &s,
                                     quader::QuaderSelectionMode mode) {
  s.then().selection().mode(mode).active_alias("box");
  switch (mode) {
  case quader::QuaderSelectionMode::Vertex:
    s.then().selection().vertex_count(1);
    break;
  case quader::QuaderSelectionMode::Edge:
    s.then().selection().edge_count(1);
    break;
  case quader::QuaderSelectionMode::Face:
    s.then().selection().face_count(1);
    break;
  case quader::QuaderSelectionMode::Object:
  case quader::QuaderSelectionMode::Group:
    ADD_FAILURE() << "component selection test received non-component mode";
    break;
  }
}

void run_component_transform_drag_case(ComponentPickCase test_case) {
  auto s = quader::test::BehaviorScenario::new_project();
  s.given()
      .unit_box("box", "Component Drag Box")
      .selection_mode(test_case.mode);
  s.when().pick(test_case.ray_origin);

  const quader::test::BehaviorSnapshot before =
      s.snapshot("before component drag");
  ASSERT_EQ(before.meshes.size(), 1u);

  s.when().begin_transform_drag([&] {
    quader::test::DragBeginOptions options;
    options.tool = test_case.tool;
    if (test_case.mode == quader::QuaderSelectionMode::Vertex &&
        test_case.tool != quader::ModelingTransformTool::Move) {
      options.use_selection_center = false;
      options.pivot = {};
    }
    if (test_case.tool == quader::ModelingTransformTool::Rotate) {
      options.axis = quader::ModelingTransformAxis::Z;
      options.screen_position = {120.0f, 100.0f};
      options.screen_pivot = {100.0f, 100.0f};
      options.screen_tip = {120.0f, 100.0f};
      options.world_direction = {0.0f, 0.0f, 1.0f};
    } else if (test_case.tool == quader::ModelingTransformTool::Scale) {
      options.axis = quader::ModelingTransformAxis::X;
      options.screen_position = {200.0f, 200.0f};
      options.screen_pivot = {200.0f, 200.0f};
      options.screen_tip = {260.0f, 200.0f};
      options.world_direction = {1.0f, 0.0f, 0.0f};
    }
    return options;
  }());
  if (test_case.tool == quader::ModelingTransformTool::Rotate) {
    s.when().update_transform_drag({100.0f, 80.0f});
  } else if (test_case.tool == quader::ModelingTransformTool::Scale) {
    s.when().update_transform_drag({300.0f, 200.0f});
  } else {
    s.when().update_transform_drag(120.0f);
  }
  s.when().commit_drag();

  s.then().command().ok_changed();
  expect_selection_count_for_mode(s, test_case.mode);
  s.then()
      .mesh("box")
      .vertex_count(before.meshes.front().vertex_count)
      .vertices_differ_from(before.meshes.front());
}

} // namespace

TEST(ProgrammaticBehaviorScenarioTests,
     ShiftDuplicatePreviewKeepsDurableMeshStableUntilAbsoluteDeltaCommit) {
  auto s = quader::test::BehaviorScenario::new_project();

  s.given().unit_box("source", "Shift Duplicate Source").selected("source");

  s.when().begin_transform_preview(true).translate_preview({1.0f, 0.0f, 0.0f});

  const quader::test::BehaviorSnapshot first_move = s.snapshot("first move");

  s.when().translate_preview({3.0f, 0.0f, 0.0f}).commit_transform_preview();

  s.then().command().ok_changed();
  s.then()
      .scene()
      .entity_count(2)
      .selected_alias_is("source.copy")
      .selected_bounds_center_near({3.0f, 0.0f, 0.0f});

  // The renderer-owned position stream moves the visible duplicate; behavior
  // snapshots intentionally continue to expose the durable scene until commit.
  s.then().snapshot(first_move).selected_bounds_center_near({0.0f, 0.0f, 0.0f});

  s.when().pick({3.0f, 0.0f, 5.0f});
  s.then().selection().object_count(1).active_alias("source.copy");
}

TEST(ProgrammaticBehaviorScenarioTests, ComponentGizmoMoveRotateScaleKeepComponentSelectionsAndMutateMesh) {
  for (const ComponentPickCase test_case : {
           ComponentPickCase{
               .mode = quader::QuaderSelectionMode::Face,
               .ray_origin = {0.0f, 0.0f, 5.0f},
               .tool = quader::ModelingTransformTool::Move,
           },
           ComponentPickCase{
               .mode = quader::QuaderSelectionMode::Face,
               .ray_origin = {0.0f, 0.0f, 5.0f},
               .tool = quader::ModelingTransformTool::Rotate,
           },
           ComponentPickCase{
               .mode = quader::QuaderSelectionMode::Face,
               .ray_origin = {0.0f, 0.0f, 5.0f},
               .tool = quader::ModelingTransformTool::Scale,
           },
           ComponentPickCase{
               .mode = quader::QuaderSelectionMode::Edge,
               .ray_origin = {0.0f, 1.0f, 5.0f},
               .tool = quader::ModelingTransformTool::Move,
           },
           ComponentPickCase{
               .mode = quader::QuaderSelectionMode::Edge,
               .ray_origin = {0.0f, 1.0f, 5.0f},
               .tool = quader::ModelingTransformTool::Rotate,
           },
           ComponentPickCase{
               .mode = quader::QuaderSelectionMode::Edge,
               .ray_origin = {0.0f, 1.0f, 5.0f},
               .tool = quader::ModelingTransformTool::Scale,
           },
           ComponentPickCase{
               .mode = quader::QuaderSelectionMode::Vertex,
               .ray_origin = {1.0f, 1.0f, 5.0f},
               .tool = quader::ModelingTransformTool::Move,
           },
           ComponentPickCase{
               .mode = quader::QuaderSelectionMode::Vertex,
               .ray_origin = {1.0f, 1.0f, 5.0f},
               .tool = quader::ModelingTransformTool::Rotate,
           },
           ComponentPickCase{
               .mode = quader::QuaderSelectionMode::Vertex,
               .ray_origin = {1.0f, 1.0f, 5.0f},
               .tool = quader::ModelingTransformTool::Scale,
           },
       }) {
    run_component_transform_drag_case(test_case);
  }
}

TEST(ProgrammaticBehaviorScenarioTests,
     RepeatRotateReusesRecordedWorldPivotForEdgeFaceAndObjectSelections) {
  for (const RepeatRotationCase test_case : {
           RepeatRotationCase{
               .mode = quader::QuaderSelectionMode::Edge,
               .ray_origin = {0.0f, 1.0f, 5.0f},
               .label = "edge",
           },
           RepeatRotationCase{
               .mode = quader::QuaderSelectionMode::Face,
               .ray_origin = {0.0f, 0.0f, 5.0f},
               .label = "face",
           },
           RepeatRotationCase{
               .mode = quader::QuaderSelectionMode::Object,
               .label = "object",
           },
       }) {
    SCOPED_TRACE(test_case.label);
    auto s = quader::test::BehaviorScenario::new_project();
    s.given().unit_box("box", "Repeat Rotate Box");
    select_repeat_rotation_target(s, test_case);
    expect_repeat_rotation_selection(s, test_case.mode);

    const std::optional<quader::QVec3> center_before =
        s.api().query().selection_center();
    ASSERT_TRUE(center_before.has_value());
    const quader::test::BehaviorSnapshot topology_before =
        s.snapshot("before repeat rotate");
    const quader::test::MeshSnapshot *mesh_before =
        mesh_snapshot_for_alias(topology_before, "box");
    ASSERT_NE(mesh_before, nullptr);

    s.when()
        .begin_transform_drag(repeat_rotation_drag_options(
            quader::ModelingTransformOperation::Transform))
        .update_transform_drag({100.0f, 80.0f})
        .commit_drag();

    s.then().command().ok_changed();
    expect_repeat_rotation_selection(s, test_case.mode);
    s.then().scene().entity_count(1);
    const std::optional<quader::QVec3> center_after_drag =
        s.api().query().selection_center();
    ASSERT_TRUE(center_after_drag.has_value());
    expect_positive_quarter_turn_step(*center_before, *center_after_drag,
                                      kRepeatRotationWorldPivot);

    const quader::test::BehaviorSnapshot topology_after_drag =
        s.snapshot("after rotate drag");
    const quader::test::MeshSnapshot *mesh_after_drag =
        mesh_snapshot_for_alias(topology_after_drag, "box");
    ASSERT_NE(mesh_after_drag, nullptr);
    if (test_case.mode != quader::QuaderSelectionMode::Object) {
      EXPECT_EQ(mesh_after_drag->vertex_count, mesh_before->vertex_count);
      EXPECT_EQ(mesh_after_drag->index_count, mesh_before->index_count);
    }

    s.when().execute(quader::quader_command::kEditRepeatLastAction);

    s.then().command().ok_changed();
    expect_repeat_rotation_selection(s, test_case.mode);
    s.then().scene().entity_count(1);
    const std::optional<quader::QVec3> center_after_repeat =
        s.api().query().selection_center();
    ASSERT_TRUE(center_after_repeat.has_value());
    expect_positive_quarter_turn_step(*center_after_drag, *center_after_repeat,
                                      kRepeatRotationWorldPivot);

    const quader::test::BehaviorSnapshot topology_after_repeat =
        s.snapshot("after repeated rotate");
    const quader::test::MeshSnapshot *mesh_after_repeat =
        mesh_snapshot_for_alias(topology_after_repeat, "box");
    ASSERT_NE(mesh_after_repeat, nullptr);
    if (test_case.mode != quader::QuaderSelectionMode::Object) {
      EXPECT_EQ(mesh_after_repeat->vertex_count, mesh_before->vertex_count);
      EXPECT_EQ(mesh_after_repeat->index_count, mesh_before->index_count);
    }
  }
}

TEST(ProgrammaticBehaviorScenarioTests,
     RepeatShiftRotateReusesRecordedWorldPivotAndRepeatsExtrudeOrDuplicate) {
  for (const RepeatRotationCase test_case : {
           RepeatRotationCase{
               .mode = quader::QuaderSelectionMode::Edge,
               .ray_origin = {0.0f, 1.0f, 5.0f},
               .label = "edge extrude",
           },
           RepeatRotationCase{
               .mode = quader::QuaderSelectionMode::Face,
               .ray_origin = {0.0f, 0.0f, 5.0f},
               .label = "face extrude",
           },
           RepeatRotationCase{
               .mode = quader::QuaderSelectionMode::Object,
               .label = "object duplicate",
           },
       }) {
    SCOPED_TRACE(test_case.label);
    auto s = quader::test::BehaviorScenario::new_project();
    s.given().unit_box("box", "Repeat Shift Rotate Box");
    select_repeat_rotation_target(s, test_case);
    expect_repeat_rotation_selection(s, test_case.mode);

    const bool object_mode =
        test_case.mode == quader::QuaderSelectionMode::Object;
    const std::optional<quader::QVec3> center_before =
        s.api().query().selection_center();
    ASSERT_TRUE(center_before.has_value());
    const quader::test::BehaviorSnapshot topology_before =
        s.snapshot("before shift rotate");
    const quader::test::MeshSnapshot *mesh_before =
        mesh_snapshot_for_alias(topology_before, "box");
    ASSERT_NE(mesh_before, nullptr);

    s.when()
        .begin_transform_drag(repeat_rotation_drag_options(
            object_mode ? quader::ModelingTransformOperation::Transform
                        : quader::ModelingTransformOperation::Extrude,
            object_mode))
        .update_transform_drag({100.0f, 80.0f})
        .commit_drag();

    s.then().command().ok_changed();
    expect_repeat_rotation_selection(s, test_case.mode);
    if (object_mode) {
      EXPECT_EQ(s.modeling_object_count(), 2u);
    } else {
      s.then().scene().entity_count(1u);
    }
    const std::optional<quader::QVec3> center_after_drag =
        s.api().query().selection_center();
    ASSERT_TRUE(center_after_drag.has_value());
    expect_positive_quarter_turn_step(*center_before, *center_after_drag,
                                      kRepeatRotationWorldPivot);

    const quader::test::BehaviorSnapshot topology_after_drag =
        s.snapshot("after shift rotate");
    const quader::test::MeshSnapshot *mesh_after_drag =
        mesh_snapshot_for_alias(topology_after_drag, "box");
    ASSERT_NE(mesh_after_drag, nullptr);
    if (!object_mode) {
      EXPECT_GT(mesh_after_drag->vertex_count, mesh_before->vertex_count);
      EXPECT_GT(mesh_after_drag->index_count, mesh_before->index_count);
    }

    s.when().execute(quader::quader_command::kEditRepeatLastAction);

    s.then().command().ok_changed();
    expect_repeat_rotation_selection(s, test_case.mode);
    if (object_mode) {
      EXPECT_EQ(s.modeling_object_count(), 3u);
    } else {
      s.then().scene().entity_count(1u);
    }
    const std::optional<quader::QVec3> center_after_repeat =
        s.api().query().selection_center();
    ASSERT_TRUE(center_after_repeat.has_value());
    expect_positive_quarter_turn_step(*center_after_drag, *center_after_repeat,
                                      kRepeatRotationWorldPivot);

    const quader::test::BehaviorSnapshot topology_after_repeat =
        s.snapshot("after repeated shift rotate");
    const quader::test::MeshSnapshot *mesh_after_repeat =
        mesh_snapshot_for_alias(topology_after_repeat, "box");
    ASSERT_NE(mesh_after_repeat, nullptr);
    if (!object_mode) {
      EXPECT_GT(mesh_after_repeat->vertex_count, mesh_after_drag->vertex_count);
      EXPECT_GT(mesh_after_repeat->index_count, mesh_after_drag->index_count);
    }
  }
}

TEST(ProgrammaticBehaviorScenarioTests, FaceGizmoDragMovesOnlyPickedFaceAndKeepsComponentSelection) {
  auto s = quader::test::BehaviorScenario::new_project();

  s.given()
      .unit_box("box", "Face Gizmo Drag Box")
      .selection_mode(quader::QuaderSelectionMode::Face);

  s.when()
      .pick({0.0f, 0.0f, 5.0f})
      .begin_x_move_drag()
      .update_x_move_drag(120.0f)
      .commit_drag();

  s.then().command().ok_changed();
  s.then()
      .selection()
      .mode(quader::QuaderSelectionMode::Face)
      .face_count(1)
      .active_alias("box");
  s.then()
      .mesh("box")
      .bounds_min_near({-1.0f, -1.0f, -1.0f})
      .bounds_max_near({3.0f, 1.0f, 1.0f});
}

TEST(ProgrammaticBehaviorScenarioTests, ShiftFaceMoveAndRotateExtrudeAndShiftFaceScaleInsetsThroughGizmoDrag) {
    {
      auto s = quader::test::BehaviorScenario::new_project();
      s.given()
          .unit_box("box", "Shift Extrude Box")
          .selection_mode(quader::QuaderSelectionMode::Face);
      s.when().pick({0.0f, 0.0f, 5.0f});
      const quader::test::BehaviorSnapshot before =
          s.snapshot("before shift extrude");
      ASSERT_EQ(before.meshes.size(), 1u);

      s.when()
          .begin_transform_drag({
              .tool = quader::ModelingTransformTool::Move,
              .operation = quader::ModelingTransformOperation::Extrude,
          })
          .update_transform_drag(120.0f)
          .commit_drag();

      s.then().command().ok_changed();
      s.then()
          .selection()
          .mode(quader::QuaderSelectionMode::Face)
          .face_count(1)
          .active_alias("box");
      s.then().mesh("box").vertices_greater_than(before.meshes.front());
    }

    {
      auto s = quader::test::BehaviorScenario::new_project();
      s.given()
          .unit_box("box", "Shift Rotate Extrude Box")
          .selection_mode(quader::QuaderSelectionMode::Face);
      s.when().pick({0.0f, 0.0f, 5.0f});
      const quader::test::BehaviorSnapshot before =
          s.snapshot("before shift rotate extrude");
      ASSERT_EQ(before.meshes.size(), 1u);

      s.when()
          .begin_transform_drag({
              .tool = quader::ModelingTransformTool::Rotate,
              .axis = quader::ModelingTransformAxis::Z,
              .operation = quader::ModelingTransformOperation::Extrude,
              .screen_position = {120.0f, 100.0f},
              .screen_pivot = {100.0f, 100.0f},
              .screen_tip = {120.0f, 100.0f},
              .world_direction = {0.0f, 0.0f, 1.0f},
          })
          .update_transform_drag({116.0f, 112.0f})
          .commit_drag();

      s.then().command().ok_changed();
      s.then()
          .selection()
          .mode(quader::QuaderSelectionMode::Face)
          .face_count(1)
          .active_alias("box");
      s.then().mesh("box").vertices_greater_than(before.meshes.front());
    }

    {
      auto s = quader::test::BehaviorScenario::new_project();
      s.given()
          .unit_box("box", "Shift Inset Box")
          .selection_mode(quader::QuaderSelectionMode::Face);
      s.when().pick({0.0f, 0.0f, 5.0f});
      const quader::test::BehaviorSnapshot before =
          s.snapshot("before shift inset");
      ASSERT_EQ(before.meshes.size(), 1u);

      s.when()
          .begin_transform_drag({
              .tool = quader::ModelingTransformTool::Scale,
              .operation = quader::ModelingTransformOperation::Inset,
          })
          .update_transform_drag(120.0f)
          .commit_drag();

      s.then().command().ok_changed();
      s.then()
          .selection()
          .mode(quader::QuaderSelectionMode::Face)
          .face_count(1)
          .active_alias("box");
      s.then().mesh("box").vertices_greater_than(before.meshes.front());
    }
}

TEST(ProgrammaticBehaviorScenarioTests, ShiftRotateExtrudeUsesWorldPivotForTransformedComponentSelection) {
  auto s = quader::test::BehaviorScenario::new_project();
  s.given()
      .unit_box("box", "Shift Rotate Scene Pivot Box")
      .transform("box", {.position = {10.0f, 0.0f, 0.0f}})
      .selection_mode(quader::QuaderSelectionMode::Face);

  s.when().pick({10.0f, 0.0f, 5.0f});
  ASSERT_TRUE(s.api().query().selection_center().has_value());
  EXPECT_NEAR(s.api().query().selection_center()->x, 10.0f, 0.001f);
  EXPECT_NEAR(s.api().query().selection_center()->y, 0.0f, 0.001f);

  s.when()
      .begin_transform_drag({
          .tool = quader::ModelingTransformTool::Rotate,
          .axis = quader::ModelingTransformAxis::Z,
          .operation = quader::ModelingTransformOperation::Extrude,
          .screen_position = {120.0f, 100.0f},
          .screen_pivot = {100.0f, 100.0f},
          .screen_tip = {120.0f, 100.0f},
          .world_direction = {0.0f, 0.0f, 1.0f},
      })
      .update_transform_drag({116.0f, 112.0f})
      .commit_drag();

  const std::optional<quader::QVec3> selected_center =
      s.api().query().selection_center();
  ASSERT_TRUE(selected_center.has_value());
  EXPECT_NEAR(selected_center->x, 10.0f, 0.001f);
  EXPECT_NEAR(selected_center->y, 0.0f, 0.001f);
}

TEST(ProgrammaticBehaviorScenarioTests, BridgeAfterShiftRotateExtrudeKeepsTransformedSourceInWorldPlace) {
  auto s = quader::test::BehaviorScenario::new_project();
  s.given()
      .unit_box("source", "Bridge Shift Rotate Source")
      .transform("source", {.position = {10.0f, 0.0f, 0.0f}})
      .box("target",
           {.min = {13.0f, -1.0f, -1.0f},
            .max = {15.0f, 1.0f, 1.0f}},
           "Bridge Target")
      .selection_mode(quader::QuaderSelectionMode::Face);

  s.when()
      .pick({10.0f, 0.0f, 5.0f})
      .begin_transform_drag({
          .tool = quader::ModelingTransformTool::Rotate,
          .axis = quader::ModelingTransformAxis::Z,
          .operation = quader::ModelingTransformOperation::Extrude,
          .screen_position = {120.0f, 100.0f},
          .screen_pivot = {100.0f, 100.0f},
          .screen_tip = {120.0f, 100.0f},
          .world_direction = {0.0f, 0.0f, 1.0f},
      })
      .update_transform_drag({116.0f, 112.0f})
      .commit_drag();

  const quader::test::BehaviorSnapshot before_bridge =
      s.snapshot("before bridge");
  const quader::test::MeshSnapshot *source_before_bridge =
      mesh_snapshot_for_alias(before_bridge, "source");
  ASSERT_NE(source_before_bridge, nullptr);

  s.when().pick({14.0f, 0.0f, 5.0f}, {.shift_pressed = true});
  s.then().selection().mode(quader::QuaderSelectionMode::Face).face_count(2);

  s.when().execute(quader::quader_command::kModelingBridgeFaces);

  s.then().command().ok_changed();
  s.then().scene().selected_alias_is("target");
  s.then()
      .mesh("target")
      .bounds_min_near({8.633974f, -1.366026f, -1.0f}, 0.01f);

  s.when().undo();

  s.then().command().ok_changed();
  const quader::test::BehaviorSnapshot after_undo =
      s.snapshot("after bridge undo");
  const quader::test::MeshSnapshot *source_after_undo =
      mesh_snapshot_for_alias(after_undo, "source");
  ASSERT_NE(source_after_undo, nullptr);
  expect_mesh_bounds_near(*source_after_undo, *source_before_bridge, 0.001f);
}

TEST(ProgrammaticBehaviorScenarioTests,
     InterpolatedBridgePreviewUpdateCommitKeepsSceneTransformedSourceInWorldPlace) {
  auto s = quader::test::BehaviorScenario::new_project();
  s.given()
      .unit_box("source", "Bridge Preview Source")
      .transform("source", {.position = {10.0f, 0.0f, 0.0f}})
      .unit_box("target", "Bridge Preview Target")
      .transform("target", {.position = {14.0f, 0.0f, 0.0f}})
      .selection_mode(quader::QuaderSelectionMode::Face);

  const quader::QuaderApiResult<quader::QuaderSelectionSummary> source_pick =
      s.api().selection().pick_select({
          .ray = {.origin = {12.0f, 0.0f, 0.0f},
                  .direction = {-1.0f, 0.0f, 0.0f}},
          .click_count = 1,
      });
  ASSERT_TRUE(source_pick.ok()) << source_pick.error_message();
  const quader::QuaderApiResult<quader::QuaderSelectionSummary> target_pick =
      s.api().selection().pick_select({
          .ray = {.origin = {12.0f, 0.0f, 0.0f},
                  .direction = {1.0f, 0.0f, 0.0f}},
          .click_count = 1,
          .shift_pressed = true,
      });
  ASSERT_TRUE(target_pick.ok()) << target_pick.error_message();
  s.then()
      .selection()
      .mode(quader::QuaderSelectionMode::Face)
      .face_count(2)
      .active_alias("target");

  const quader::test::BehaviorSnapshot before_preview =
      s.snapshot("before bridge preview");
  const quader::test::MeshSnapshot *source_before =
      mesh_snapshot_for_alias(before_preview, "source");
  const quader::test::MeshSnapshot *target_before =
      mesh_snapshot_for_alias(before_preview, "target");
  ASSERT_NE(source_before, nullptr);
  ASSERT_NE(target_before, nullptr);

  const quader::QuaderApiVoidResult begin =
      s.api().modeling().begin_preview("interpolated_bridge_faces");
  ASSERT_TRUE(begin.ok()) << begin.error_message();
  EXPECT_TRUE(begin.changed());
  s.then()
      .mesh("target")
      .bounds_min_near({-5.0f, -1.0f, -1.0f})
      .bounds_max_near({1.0f, 1.0f, 1.0f});

  quader::QuaderPreviewSettings settings;
  settings.interaction.bridge_steps = 3;
  const quader::QuaderApiVoidResult update =
      s.api().modeling().update_preview(settings);
  ASSERT_TRUE(update.ok()) << update.error_message();
  EXPECT_TRUE(update.changed());
  s.then()
      .mesh("target")
      .bounds_min_near({-5.0f, -1.0f, -1.0f})
      .bounds_max_near({1.0f, 1.0f, 1.0f});

  const quader::QuaderApiVoidResult commit =
      s.api().modeling().commit_preview("Interpolated Bridge");
  ASSERT_TRUE(commit.ok()) << commit.error_message();
  EXPECT_TRUE(commit.changed());
  s.then()
      .mesh("target")
      .bounds_min_near({-5.0f, -1.0f, -1.0f})
      .bounds_max_near({1.0f, 1.0f, 1.0f});

  s.when().undo();

  s.then().command().ok_changed();
  const quader::test::BehaviorSnapshot after_undo =
      s.snapshot("after bridge preview undo");
  const quader::test::MeshSnapshot *source_after_undo =
      mesh_snapshot_for_alias(after_undo, "source");
  const quader::test::MeshSnapshot *target_after_undo =
      mesh_snapshot_for_alias(after_undo, "target");
  ASSERT_NE(source_after_undo, nullptr);
  ASSERT_NE(target_after_undo, nullptr);
  expect_mesh_bounds_near(*source_after_undo, *source_before);
  expect_mesh_bounds_near(*target_after_undo, *target_before);
}

TEST(ProgrammaticBehaviorScenarioTests,
     BridgeFacesPreservesWindingForMirroredSceneSource) {
  auto s = quader::test::BehaviorScenario::new_project();
  s.given()
      .unit_box("source", "Mirrored Bridge Source")
      .transform("source",
                 {.position = {10.0f, 0.0f, 0.0f},
                  .scale = {-1.0f, 1.0f, 1.0f}})
      .unit_box("target", "Mirrored Bridge Target")
      .transform("target", {.position = {14.0f, 0.0f, 0.0f}})
      .selection_mode(quader::QuaderSelectionMode::Face);

  ASSERT_TRUE(s.api()
                  .selection()
                  .pick_select({
                      .ray = {.origin = {12.0f, 0.0f, 0.0f},
                              .direction = {-1.0f, 0.0f, 0.0f}},
                      .click_count = 1,
                  })
                  .ok());
  ASSERT_TRUE(s.api()
                  .selection()
                  .pick_select({
                      .ray = {.origin = {12.0f, 0.0f, 0.0f},
                              .direction = {1.0f, 0.0f, 0.0f}},
                      .click_count = 1,
                      .shift_pressed = true,
                  })
                  .ok());
  s.then()
      .selection()
      .mode(quader::QuaderSelectionMode::Face)
      .face_count(2)
      .active_alias("target");

  s.when().execute(quader::quader_command::kModelingBridgeFaces);

  s.then().command().ok_changed();
  s.then().scene().selected_alias_is("target");
  expect_compiled_mesh_winding_coherent(s, "target");
}

TEST(ProgrammaticBehaviorScenarioTests,
     InterpolatedBridgePreviewPreservesWindingForMirroredSceneSource) {
  auto s = quader::test::BehaviorScenario::new_project();
  s.given()
      .unit_box("source", "Mirrored Bridge Preview Source")
      .transform("source",
                 {.position = {10.0f, 0.0f, 0.0f},
                  .scale = {-1.0f, 1.0f, 1.0f}})
      .unit_box("target", "Mirrored Bridge Preview Target")
      .transform("target", {.position = {14.0f, 0.0f, 0.0f}})
      .selection_mode(quader::QuaderSelectionMode::Face);

  ASSERT_TRUE(s.api()
                  .selection()
                  .pick_select({
                      .ray = {.origin = {12.0f, 0.0f, 0.0f},
                              .direction = {-1.0f, 0.0f, 0.0f}},
                      .click_count = 1,
                  })
                  .ok());
  ASSERT_TRUE(s.api()
                  .selection()
                  .pick_select({
                      .ray = {.origin = {12.0f, 0.0f, 0.0f},
                              .direction = {1.0f, 0.0f, 0.0f}},
                      .click_count = 1,
                      .shift_pressed = true,
                  })
                  .ok());
  s.then()
      .selection()
      .mode(quader::QuaderSelectionMode::Face)
      .face_count(2)
      .active_alias("target");

  const quader::QuaderApiVoidResult begin =
      s.api().modeling().begin_preview("interpolated_bridge_faces");
  ASSERT_TRUE(begin.ok()) << begin.error_message();
  EXPECT_TRUE(begin.changed());
  expect_compiled_mesh_winding_coherent(s, "target");

  quader::QuaderPreviewSettings settings;
  settings.interaction.bridge_steps = 3;
  const quader::QuaderApiVoidResult update =
      s.api().modeling().update_preview(settings);
  ASSERT_TRUE(update.ok()) << update.error_message();
  EXPECT_TRUE(update.changed());
  expect_compiled_mesh_winding_coherent(s, "target");

  const quader::QuaderApiVoidResult commit =
      s.api().modeling().commit_preview("Interpolated Bridge");
  ASSERT_TRUE(commit.ok()) << commit.error_message();
  EXPECT_TRUE(commit.changed());
  expect_compiled_mesh_winding_coherent(s, "target");
}

TEST(ProgrammaticBehaviorScenarioTests,
     ShiftRotateExtrudeKeepsWorldCenterStableAndAddsTopologyForMirroredAndPivotEditedSelection) {
  {
    auto s = quader::test::BehaviorScenario::new_project();
    s.given()
        .unit_box("box", "Mirrored Shift Rotate Extrude Box")
        .transform("box",
                   {.position = {10.0f, 0.0f, 0.0f},
                    .scale = {-1.0f, 1.0f, 1.0f}})
        .selection_mode(quader::QuaderSelectionMode::Face);

    s.when().pick({10.0f, 0.0f, 5.0f});
    run_shift_rotate_extrude_stability_case(s, "box");
  }

  {
    auto s = quader::test::BehaviorScenario::new_project();
    s.given().unit_box("box", "Pivot Shift Rotate Extrude Box").selected("box");
    ASSERT_TRUE(s.editor().tools().activate(quader::ViewportToolId::Pivot).ok());
    ASSERT_TRUE(s.editor()
                    .tools()
                    .execute_action("pivot", "pivot.align.top_center")
                    .ok());
    ASSERT_TRUE(s.editor().tools().commit_active_tool_preview().ok());

    s.given().selection_mode(quader::QuaderSelectionMode::Face);
    s.when().pick({0.0f, 0.0f, 5.0f});
    run_shift_rotate_extrude_stability_case(s, "box");
  }
}

TEST(ProgrammaticBehaviorScenarioTests, CombineMeshesConsumesTransformedSourcesInSurvivorSpace) {
  auto s = quader::test::BehaviorScenario::new_project();
  s.given()
      .unit_box("source", "Combine Source")
      .transform("source", {.position = {10.0f, 0.0f, 0.0f}})
      .box("target",
           {.min = {13.0f, -1.0f, -1.0f},
            .max = {15.0f, 1.0f, 1.0f}},
           "Combine Target")
      .selection_mode(quader::QuaderSelectionMode::Object)
      .selected_aliases({"source", "target"}, "target");

  s.when().execute(quader::quader_command::kModelingCombineMeshes);

  s.then().command().ok_changed();
  s.then().scene().selected_alias_is("target");
  s.then()
      .mesh("target")
      .bounds_min_near({9.0f, -1.0f, -1.0f}, 0.01f)
      .bounds_max_near({15.0f, 1.0f, 1.0f}, 0.01f);
}

TEST(ProgrammaticBehaviorScenarioTests,
     CombineMeshesPreservesWindingForMirroredSceneSource) {
  auto s = quader::test::BehaviorScenario::new_project();
  s.given()
      .unit_box("source", "Mirrored Combine Source")
      .transform("source",
                 {.position = {10.0f, 0.0f, 0.0f},
                  .scale = {-1.0f, 1.0f, 1.0f}})
      .unit_box("target", "Mirrored Combine Target")
      .transform("target", {.position = {14.0f, 0.0f, 0.0f}})
      .selection_mode(quader::QuaderSelectionMode::Object)
      .selected_aliases({"source", "target"}, "target");

  s.when().execute(quader::quader_command::kModelingCombineMeshes);

  s.then().command().ok_changed();
  s.then().scene().selected_alias_is("target");
  expect_compiled_mesh_winding_coherent(s, "target");
}

TEST(ProgrammaticBehaviorScenarioTests, RayPickingAndContextClickSemanticsPreserveComponentAuthority) {
    {
      auto s = quader::test::BehaviorScenario::new_project();
      s.given()
          .unit_box("box", "Face Pick Box")
          .selection_mode(quader::QuaderSelectionMode::Face);
      s.when().pick({0.0f, 0.0f, 5.0f});
      s.then()
          .selection()
          .mode(quader::QuaderSelectionMode::Face)
          .face_count(1)
          .active_alias("box");

      const quader::ModelingSelectionSummary selected =
          s.snapshot("selected face").selection;
      s.when().pick({0.0f, 0.0f, 5.0f},
                    {.preserve_selection_on_selected_hit = true});
      s.then().selection().revision_same_as(selected);
    }

    {
      auto s = quader::test::BehaviorScenario::new_project();
      s.given()
          .unit_box("box", "Edge Pick Box")
          .selection_mode(quader::QuaderSelectionMode::Edge);
      s.when().pick({0.0f, 1.0f, 5.0f});
      s.then()
          .selection()
          .mode(quader::QuaderSelectionMode::Edge)
          .edge_count(1)
          .active_alias("box");
    }

    {
      auto s = quader::test::BehaviorScenario::new_project();
      s.given()
          .unit_box("box", "Vertex Pick Box")
          .selection_mode(quader::QuaderSelectionMode::Vertex);
      s.when().pick({1.0f, 1.0f, 5.0f});
      s.then()
          .selection()
          .mode(quader::QuaderSelectionMode::Vertex)
          .vertex_count(1)
          .active_alias("box");
    }

    {
      auto s = quader::test::BehaviorScenario::new_project();
      quader::QuaderTransform transform;
      transform.position = {4.0f, 0.0f, 0.0f};
      s.given()
          .unit_box("box", "Transformed Pick Box")
          .transform("box", transform)
          .selection_mode(quader::QuaderSelectionMode::Face);
      s.when().pick({4.0f, 0.0f, 5.0f});
      s.then()
          .selection()
          .mode(quader::QuaderSelectionMode::Face)
          .face_count(1)
          .active_alias("box");
    }

    {
      auto s = quader::test::BehaviorScenario::new_project();
      s.given()
          .unit_box("left", "Left Pick Box")
          .box("right",
               {.min = {4.0f, -1.0f, -1.0f}, .max = {6.0f, 1.0f, 1.0f}},
               "Right Pick Box")
          .selected("left");
      s.when().pick({5.0f, 0.0f, 5.0f});
      s.then()
          .selection()
          .mode(quader::QuaderSelectionMode::Object)
          .object_count(1)
          .active_alias("right");
    }
}

TEST(ProgrammaticBehaviorScenarioTests, SelectionAndHoverOverlaysAreBoundedDistinctDataLayers) {
    {
      auto s = quader::test::BehaviorScenario::new_project();
      s.given()
          .unit_box("box", "Face Overlay Box")
          .selection_mode(quader::QuaderSelectionMode::Face);
      s.when().pick({0.0f, 0.0f, 5.0f});
      s.then()
          .overlay()
          .has_triangle(quader::ViewportOverlayLayer::SelectedVisibleFace)
          .has_line(quader::ViewportOverlayLayer::SelectedFaceEdge)
          .triangle_bounds_within(
              quader::ViewportOverlayLayer::SelectedVisibleFace,
              {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}})
          .line_bounds_within(
              quader::ViewportOverlayLayer::SelectedFaceEdge,
              {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}})
          .finite_coordinates();

      s.when().hover({0.0f, 1.0f, 5.0f});
      s.then()
          .overlay()
          .has_triangle(quader::ViewportOverlayLayer::SelectedVisibleFace)
          .has_triangle(quader::ViewportOverlayLayer::HoverFace)
          .finite_coordinates();
    }

    {
      auto s = quader::test::BehaviorScenario::new_project();
      s.given()
          .unit_box("box", "Edge Overlay Box")
          .selection_mode(quader::QuaderSelectionMode::Edge);
      s.when().pick({0.0f, 1.0f, 5.0f});
      s.then()
          .overlay()
          .has_line(quader::ViewportOverlayLayer::SelectedEdge)
          .line_bounds_within(
              quader::ViewportOverlayLayer::SelectedEdge,
              {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}})
          .finite_coordinates();
    }

    {
      auto s = quader::test::BehaviorScenario::new_project();
      s.given()
          .unit_box("box", "Vertex Overlay Box")
          .selection_mode(quader::QuaderSelectionMode::Vertex);
      s.when().pick({1.0f, 1.0f, 5.0f});
      s.then()
          .overlay()
          .has_point(quader::ViewportOverlayLayer::SelectedVertex)
          .point_bounds_center_near(
              quader::ViewportOverlayLayer::SelectedVertex, {1.0f, 1.0f, 1.0f})
          .finite_coordinates();
    }
}
