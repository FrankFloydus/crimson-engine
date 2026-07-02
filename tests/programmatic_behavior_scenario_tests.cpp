////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "support/behavior_expect.hpp"
#include "support/behavior_scenario.hpp"

#include <gtest/gtest.h>

namespace {

/// Describes one synthetic component gizmo workflow so the same behavior can be
/// verified across vertex, edge, and face selection modes.
struct ComponentPickCase {
    quader::QuaderSelectionMode mode = quader::QuaderSelectionMode::Face;
    quader::QVec3 ray_origin = {};
    quader::ModelingTransformTool tool = quader::ModelingTransformTool::Move;
};

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

TEST(ProgrammaticBehaviorScenarioTests, ShiftDuplicatePreviewUsesAbsoluteDeltasAndKeepsDuplicatePickable) {
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

  s.then().snapshot(first_move).selected_bounds_center_near({1.0f, 0.0f, 0.0f});

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

TEST(ProgrammaticBehaviorScenarioTests, ShiftFaceMoveExtrudesAndShiftFaceScaleInsetsThroughGizmoDrag) {
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
