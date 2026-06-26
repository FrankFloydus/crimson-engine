////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "support/BehaviorExpect.h"
#include "support/BehaviorScenario.h"

#include <gtest/gtest.h>

#if QUADER_TEST_HAS_PROGRAMMATIC_API
namespace {

/// Describes one synthetic component gizmo workflow so the same behavior can be
/// verified across vertex, edge, and face selection modes.
struct ComponentPickCase {
    quader::QuaderSelectionMode mode = quader::QuaderSelectionMode::Face;
    quader::QVec3 rayOrigin = {};
    quader::ModelingTransformTool tool = quader::ModelingTransformTool::Move;
};

void expectSelectionCountForMode(
        quader::test::BehaviorScenario& s,
        quader::QuaderSelectionMode mode) {
    s.then().selection()
            .mode(mode)
            .activeAlias("box");
    switch (mode) {
    case quader::QuaderSelectionMode::Vertex:
        s.then().selection().vertexCount(1);
        break;
    case quader::QuaderSelectionMode::Edge:
        s.then().selection().edgeCount(1);
        break;
    case quader::QuaderSelectionMode::Face:
        s.then().selection().faceCount(1);
        break;
    case quader::QuaderSelectionMode::Object:
    case quader::QuaderSelectionMode::Group:
        ADD_FAILURE() << "component selection test received non-component mode";
        break;
    }
}

void runComponentTransformDragCase(ComponentPickCase testCase) {
    auto s = quader::test::BehaviorScenario::newProject();
    s.given()
            .unitBox("box", "Component Drag Box")
            .selectionMode(testCase.mode);
    s.when().pick(testCase.rayOrigin);

    const quader::test::BehaviorSnapshot before = s.snapshot("before component drag");
    ASSERT_EQ(before.meshes.size(), 1u);

    s.when()
            .beginTransformDrag([&] {
                quader::test::DragBeginOptions options;
                options.tool = testCase.tool;
                if (testCase.mode == quader::QuaderSelectionMode::Vertex &&
                        testCase.tool != quader::ModelingTransformTool::Move) {
                    options.useSelectionCenter = false;
                    options.pivot = {};
                }
                if (testCase.tool == quader::ModelingTransformTool::Rotate) {
                    options.axis = quader::ModelingTransformAxis::Z;
                    options.screenPosition = {120.0f, 100.0f};
                    options.screenPivot = {100.0f, 100.0f};
                    options.screenTip = {120.0f, 100.0f};
                    options.worldDirection = {0.0f, 0.0f, 1.0f};
                } else if (testCase.tool == quader::ModelingTransformTool::Scale) {
                    options.axis = quader::ModelingTransformAxis::X;
                    options.screenPosition = {200.0f, 200.0f};
                    options.screenPivot = {200.0f, 200.0f};
                    options.screenTip = {260.0f, 200.0f};
                    options.worldDirection = {1.0f, 0.0f, 0.0f};
                }
                return options;
            }());
    if (testCase.tool == quader::ModelingTransformTool::Rotate) {
        s.when().updateTransformDrag({100.0f, 80.0f});
    } else if (testCase.tool == quader::ModelingTransformTool::Scale) {
        s.when().updateTransformDrag({300.0f, 200.0f});
    } else {
        s.when().updateTransformDrag(120.0f);
    }
    s.when().commitDrag();

    s.then().command().okChanged();
    expectSelectionCountForMode(s, testCase.mode);
    s.then().mesh("box")
            .vertexCount(before.meshes.front().vertexCount)
            .verticesDifferFrom(before.meshes.front());
}

} // namespace
#endif

TEST(ProgrammaticBehaviorScenarioTests, ShiftDuplicatePreviewUsesAbsoluteDeltasAndKeepsDuplicatePickable) {
#if !QUADER_TEST_HAS_PROGRAMMATIC_API
    GTEST_SKIP() << "QuaderApi tests are disabled until the production API target is linked into quader_tests.";
#else
    auto s = quader::test::BehaviorScenario::newProject();

    s.given()
            .unitBox("source", "Shift Duplicate Source")
            .selected("source");

    s.when()
            .beginTransformPreview(true)
            .translatePreview({1.0f, 0.0f, 0.0f});

    const quader::test::BehaviorSnapshot firstMove = s.snapshot("first move");

    s.when()
            .translatePreview({3.0f, 0.0f, 0.0f})
            .commitTransformPreview();

    s.then().command().okChanged();
    s.then().scene()
            .entityCount(2)
            .selectedAliasIs("source.copy")
            .selectedBoundsCenterNear({3.0f, 0.0f, 0.0f});

    s.then().snapshot(firstMove)
            .selectedBoundsCenterNear({1.0f, 0.0f, 0.0f});

    s.when().pick({3.0f, 0.0f, 5.0f});
    s.then().selection()
            .objectCount(1)
            .activeAlias("source.copy");
#endif
}

TEST(ProgrammaticBehaviorScenarioTests, ComponentGizmoMoveRotateScaleKeepComponentSelectionsAndMutateMesh) {
#if !QUADER_TEST_HAS_PROGRAMMATIC_API
    GTEST_SKIP() << "QuaderApi tests are disabled until the production API target is linked into quader_tests.";
#else
    for (const ComponentPickCase testCase : {
                 ComponentPickCase{
                         .mode = quader::QuaderSelectionMode::Face,
                         .rayOrigin = {0.0f, 0.0f, 5.0f},
                         .tool = quader::ModelingTransformTool::Move,
                 },
                 ComponentPickCase{
                         .mode = quader::QuaderSelectionMode::Face,
                         .rayOrigin = {0.0f, 0.0f, 5.0f},
                         .tool = quader::ModelingTransformTool::Rotate,
                 },
                 ComponentPickCase{
                         .mode = quader::QuaderSelectionMode::Face,
                         .rayOrigin = {0.0f, 0.0f, 5.0f},
                         .tool = quader::ModelingTransformTool::Scale,
                 },
                 ComponentPickCase{
                         .mode = quader::QuaderSelectionMode::Edge,
                         .rayOrigin = {0.0f, 1.0f, 5.0f},
                         .tool = quader::ModelingTransformTool::Move,
                 },
                 ComponentPickCase{
                         .mode = quader::QuaderSelectionMode::Edge,
                         .rayOrigin = {0.0f, 1.0f, 5.0f},
                         .tool = quader::ModelingTransformTool::Rotate,
                 },
                 ComponentPickCase{
                         .mode = quader::QuaderSelectionMode::Edge,
                         .rayOrigin = {0.0f, 1.0f, 5.0f},
                         .tool = quader::ModelingTransformTool::Scale,
                 },
                 ComponentPickCase{
                         .mode = quader::QuaderSelectionMode::Vertex,
                         .rayOrigin = {1.0f, 1.0f, 5.0f},
                         .tool = quader::ModelingTransformTool::Move,
                 },
                 ComponentPickCase{
                         .mode = quader::QuaderSelectionMode::Vertex,
                         .rayOrigin = {1.0f, 1.0f, 5.0f},
                         .tool = quader::ModelingTransformTool::Rotate,
                 },
                 ComponentPickCase{
                         .mode = quader::QuaderSelectionMode::Vertex,
                         .rayOrigin = {1.0f, 1.0f, 5.0f},
                         .tool = quader::ModelingTransformTool::Scale,
                 },
         }) {
        runComponentTransformDragCase(testCase);
    }
#endif
}

TEST(ProgrammaticBehaviorScenarioTests, FaceGizmoDragMovesOnlyPickedFaceAndKeepsComponentSelection) {
#if !QUADER_TEST_HAS_PROGRAMMATIC_API
    GTEST_SKIP() << "QuaderApi tests are disabled until the production API target is linked into quader_tests.";
#else
    auto s = quader::test::BehaviorScenario::newProject();

    s.given()
            .unitBox("box", "Face Gizmo Drag Box")
            .selectionMode(quader::QuaderSelectionMode::Face);

    s.when()
            .pick({0.0f, 0.0f, 5.0f})
            .beginXMoveDrag()
            .updateXMoveDrag(120.0f)
            .commitDrag();

    s.then().command().okChanged();
    s.then().selection()
            .mode(quader::QuaderSelectionMode::Face)
            .faceCount(1)
            .activeAlias("box");
    s.then().mesh("box")
            .boundsMinNear({-1.0f, -1.0f, -1.0f})
            .boundsMaxNear({3.0f, 1.0f, 1.0f});
#endif
}

TEST(ProgrammaticBehaviorScenarioTests, ShiftFaceMoveExtrudesAndShiftFaceScaleInsetsThroughGizmoDrag) {
#if !QUADER_TEST_HAS_PROGRAMMATIC_API
    GTEST_SKIP() << "QuaderApi tests are disabled until the production API target is linked into quader_tests.";
#else
    {
        auto s = quader::test::BehaviorScenario::newProject();
        s.given()
                .unitBox("box", "Shift Extrude Box")
                .selectionMode(quader::QuaderSelectionMode::Face);
        s.when().pick({0.0f, 0.0f, 5.0f});
        const quader::test::BehaviorSnapshot before = s.snapshot("before shift extrude");
        ASSERT_EQ(before.meshes.size(), 1u);

        s.when()
                .beginTransformDrag({
                        .tool = quader::ModelingTransformTool::Move,
                        .operation = quader::ModelingTransformOperation::Extrude,
                })
                .updateTransformDrag(120.0f)
                .commitDrag();

        s.then().command().okChanged();
        s.then().selection()
                .mode(quader::QuaderSelectionMode::Face)
                .faceCount(1)
                .activeAlias("box");
        s.then().mesh("box").verticesGreaterThan(before.meshes.front());
    }

    {
        auto s = quader::test::BehaviorScenario::newProject();
        s.given()
                .unitBox("box", "Shift Inset Box")
                .selectionMode(quader::QuaderSelectionMode::Face);
        s.when().pick({0.0f, 0.0f, 5.0f});
        const quader::test::BehaviorSnapshot before = s.snapshot("before shift inset");
        ASSERT_EQ(before.meshes.size(), 1u);

        s.when()
                .beginTransformDrag({
                        .tool = quader::ModelingTransformTool::Scale,
                        .operation = quader::ModelingTransformOperation::Inset,
                })
                .updateTransformDrag(120.0f)
                .commitDrag();

        s.then().command().okChanged();
        s.then().selection()
                .mode(quader::QuaderSelectionMode::Face)
                .faceCount(1)
                .activeAlias("box");
        s.then().mesh("box").verticesGreaterThan(before.meshes.front());
    }
#endif
}

TEST(ProgrammaticBehaviorScenarioTests, RayPickingAndContextClickSemanticsPreserveComponentAuthority) {
#if !QUADER_TEST_HAS_PROGRAMMATIC_API
    GTEST_SKIP() << "QuaderApi tests are disabled until the production API target is linked into quader_tests.";
#else
    {
        auto s = quader::test::BehaviorScenario::newProject();
        s.given()
                .unitBox("box", "Face Pick Box")
                .selectionMode(quader::QuaderSelectionMode::Face);
        s.when().pick({0.0f, 0.0f, 5.0f});
        s.then().selection()
                .mode(quader::QuaderSelectionMode::Face)
                .faceCount(1)
                .activeAlias("box");

        const quader::ModelingSelectionSummary selected = s.snapshot("selected face").selection;
        s.when().pick({0.0f, 0.0f, 5.0f}, {.preserveSelectionOnSelectedHit = true});
        s.then().selection().revisionSameAs(selected);
    }

    {
        auto s = quader::test::BehaviorScenario::newProject();
        s.given()
                .unitBox("box", "Edge Pick Box")
                .selectionMode(quader::QuaderSelectionMode::Edge);
        s.when().pick({0.0f, 1.0f, 5.0f});
        s.then().selection()
                .mode(quader::QuaderSelectionMode::Edge)
                .edgeCount(1)
                .activeAlias("box");
    }

    {
        auto s = quader::test::BehaviorScenario::newProject();
        s.given()
                .unitBox("box", "Vertex Pick Box")
                .selectionMode(quader::QuaderSelectionMode::Vertex);
        s.when().pick({1.0f, 1.0f, 5.0f});
        s.then().selection()
                .mode(quader::QuaderSelectionMode::Vertex)
                .vertexCount(1)
                .activeAlias("box");
    }

    {
        auto s = quader::test::BehaviorScenario::newProject();
        quader::QuaderTransform transform;
        transform.position = {4.0f, 0.0f, 0.0f};
        s.given()
                .unitBox("box", "Transformed Pick Box")
                .transform("box", transform)
                .selectionMode(quader::QuaderSelectionMode::Face);
        s.when().pick({4.0f, 0.0f, 5.0f});
        s.then().selection()
                .mode(quader::QuaderSelectionMode::Face)
                .faceCount(1)
                .activeAlias("box");
    }

    {
        auto s = quader::test::BehaviorScenario::newProject();
        s.given()
                .unitBox("left", "Left Pick Box")
                .box("right", {.min = {4.0f, -1.0f, -1.0f}, .max = {6.0f, 1.0f, 1.0f}}, "Right Pick Box")
                .selected("left");
        s.when().pick({5.0f, 0.0f, 5.0f});
        s.then().selection()
                .mode(quader::QuaderSelectionMode::Object)
                .objectCount(1)
                .activeAlias("right");
    }
#endif
}

TEST(ProgrammaticBehaviorScenarioTests, SelectionAndHoverOverlaysAreBoundedDistinctDataLayers) {
#if !QUADER_TEST_HAS_PROGRAMMATIC_API
    GTEST_SKIP() << "QuaderApi tests are disabled until the production API target is linked into quader_tests.";
#else
    {
        auto s = quader::test::BehaviorScenario::newProject();
        s.given()
                .unitBox("box", "Face Overlay Box")
                .selectionMode(quader::QuaderSelectionMode::Face);
        s.when().pick({0.0f, 0.0f, 5.0f});
        s.then().overlay()
                .hasTriangle(quader::ViewportOverlayLayer::SelectedVisibleFace)
                .hasLine(quader::ViewportOverlayLayer::SelectedFaceEdge)
                .triangleBoundsWithin(
                        quader::ViewportOverlayLayer::SelectedVisibleFace,
                        {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}})
                .lineBoundsWithin(
                        quader::ViewportOverlayLayer::SelectedFaceEdge,
                        {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}})
                .finiteCoordinates();

        s.when().hover({0.0f, 1.0f, 5.0f});
        s.then().overlay()
                .hasTriangle(quader::ViewportOverlayLayer::SelectedVisibleFace)
                .hasTriangle(quader::ViewportOverlayLayer::HoverFace)
                .finiteCoordinates();
    }

    {
        auto s = quader::test::BehaviorScenario::newProject();
        s.given()
                .unitBox("box", "Edge Overlay Box")
                .selectionMode(quader::QuaderSelectionMode::Edge);
        s.when().pick({0.0f, 1.0f, 5.0f});
        s.then().overlay()
                .hasLine(quader::ViewportOverlayLayer::SelectedEdge)
                .lineBoundsWithin(
                        quader::ViewportOverlayLayer::SelectedEdge,
                        {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}})
                .finiteCoordinates();
    }

    {
        auto s = quader::test::BehaviorScenario::newProject();
        s.given()
                .unitBox("box", "Vertex Overlay Box")
                .selectionMode(quader::QuaderSelectionMode::Vertex);
        s.when().pick({1.0f, 1.0f, 5.0f});
        s.then().overlay()
                .hasPoint(quader::ViewportOverlayLayer::SelectedVertex)
                .pointBoundsCenterNear(quader::ViewportOverlayLayer::SelectedVertex, {1.0f, 1.0f, 1.0f})
                .finiteCoordinates();
    }
#endif
}
