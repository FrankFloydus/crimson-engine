////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"
#include "api/quader_command_ids.hpp"
#include "api/quader_api_session_internal_access.hpp"
#include "editor/tools/viewport_tool.hpp"
#include "editor/viewport/viewport_interaction_controller.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "render/overlay/viewport_box_overlay_builder.hpp"
#include "render/overlay/viewport_selection_marquee_overlay_builder.hpp"
#include "render/overlay/viewport_transform_gizmo_overlay_builder.hpp"
#include "scene/document/project_session.hpp"
#include "viewport/viewport_camera_adapter.hpp"
#include "viewport/viewport_camera_controller.hpp"
#include "viewport/viewport_input_router.hpp"
#include "viewport/viewport_settings.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace {

/// Owns a complete viewport interaction harness for one synthetic controller scenario.
struct ViewportInteractionHarness {
    quader::ProjectSession session;
    quader::QuaderEditorApi editor;
    quader::ViewportInputRouter router;
    quader::ViewportCameraController camera;
    quader::ViewportSettings settings;
    quader::ViewportInteractionController controller;
    quader::ViewportInputFrame input;
    quader::ViewportInputResult inputResult;

    ViewportInteractionHarness()
        : editor(quader::QuaderApiSessionInternalAccess::fromProjectSession(session)) {
        camera.setViewportSize(800.0f, 600.0f);
        camera.focusOrigin(8.0f);
        camera.setViewDirection({0.0f, 0.0f, -1.0f});

        input.viewportHovered = true;
        input.viewportFocused = true;
        input.viewportSize = {800.0f, 600.0f};
        input.mouseScreen = {400.0f, 300.0f};
        input.mouseLocal = {400.0f, 300.0f};
    }

    [[nodiscard]] quader::ViewportInteractionFrame frame(
            bool leftPressed = false,
            bool leftDown = false,
            bool leftReleased = false,
            quader::QVec2 leftClickLocal = {}) {
        return {
                .inputRouter = router,
                .camera = camera,
                .settings = settings,
                .input = input,
                .inputResult = inputResult,
                .leftClickLocal = leftClickLocal,
                .leftClickCount = 1,
                .leftPressed = leftPressed,
                .leftDown = leftDown,
                .leftReleased = leftReleased,
        };
    }
};

quader::QuaderModelingObjectRef createUnitBox(ViewportInteractionHarness& harness) {
    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> box =
            harness.editor.modeling().createBox(
                    quader::QuaderBounds{
                            .min = {-1.0f, -1.0f, -1.0f},
                            .max = {1.0f, 1.0f, 1.0f},
                    },
                    "Viewport Interaction Box");
    EXPECT_TRUE(box.ok) << box.message;
    return box.value();
}

quader::QuaderModelingObjectRef createBoxAt(ViewportInteractionHarness& harness, quader::QVec3 center) {
    const quader::QVec3 half{1.0f, 1.0f, 1.0f};
    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> box =
            harness.editor.modeling().createBox(
                    quader::QuaderBounds{
                            .min = center - half,
                            .max = center + half,
                    },
                    "Viewport Interaction Box");
    EXPECT_TRUE(box.ok) << box.message;
    return box.value();
}

const quader_editor_core::TransformGizmoAxisPrimitive* findAxis(
        const quader_editor_core::TransformGizmoFrame& frame,
        quader_editor_core::TransformAxis axis) {
    const auto found = std::find_if(frame.axes.begin(), frame.axes.end(), [axis](const auto& primitive) {
        return primitive.axis == axis;
    });
    return found == frame.axes.end() ? nullptr : &*found;
}

const quader_editor_core::TransformGizmoRingSegmentPrimitive* findRing(
        const quader_editor_core::TransformGizmoFrame& frame,
        quader_editor_core::TransformAxis axis) {
    const auto found = std::find_if(frame.ring_segments.begin(), frame.ring_segments.end(), [axis](const auto& primitive) {
        return primitive.axis == axis && primitive.front_facing;
    });
    return found == frame.ring_segments.end() ? nullptr : &*found;
}

quader::QVec2 rotatedPoint(quader::QVec2 pivot, quader::QVec2 point, float radians) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    const quader::QVec2 delta = point - pivot;
    return {
            pivot.x + delta.x * c - delta.y * s,
            pivot.y + delta.x * s + delta.y * c,
    };
}

void expectTransformNear(
        const quader::TransformComponent& actual,
        const quader::TransformComponent& expected) {
    EXPECT_NEAR(actual.position.x, expected.position.x, 0.001f);
    EXPECT_NEAR(actual.position.y, expected.position.y, 0.001f);
    EXPECT_NEAR(actual.position.z, expected.position.z, 0.001f);
    EXPECT_NEAR(actual.rotationEuler.x, expected.rotationEuler.x, 0.001f);
    EXPECT_NEAR(actual.rotationEuler.y, expected.rotationEuler.y, 0.001f);
    EXPECT_NEAR(actual.rotationEuler.z, expected.rotationEuler.z, 0.001f);
    EXPECT_NEAR(actual.scale.x, expected.scale.x, 0.001f);
    EXPECT_NEAR(actual.scale.y, expected.scale.y, 0.001f);
    EXPECT_NEAR(actual.scale.z, expected.scale.z, 0.001f);
}

void pressUpdateReleaseGizmo(
        ViewportInteractionHarness& harness,
        quader::QVec2 start,
        quader::QVec2 update) {
    harness.input.mouseLocal = start;
    harness.input.mouseScreen = start;
    const quader::ViewportBeforeToolInteractionResult press =
            harness.controller.handleBeforeActiveTool(
                    harness.editor,
                    harness.frame(true, true, false, start));
    ASSERT_TRUE(press.gizmoConsumed);

    harness.input.mouseLocal = update;
    harness.input.mouseScreen = update;
    const quader::ViewportBeforeToolInteractionResult drag =
            harness.controller.handleBeforeActiveTool(
                    harness.editor,
                    harness.frame(false, true, false, start));
    EXPECT_TRUE(drag.gizmoConsumed);

    const quader::ViewportBeforeToolInteractionResult release =
            harness.controller.handleBeforeActiveTool(
                    harness.editor,
                    harness.frame(false, false, true, start));
    EXPECT_TRUE(release.gizmoConsumed);
}

std::vector<quader::EntityId> selectedEntities(const ViewportInteractionHarness& harness) {
    return harness.session.scene().selectedEntities();
}

} // namespace

TEST(ViewportInteractionControllerTests, ClickFrameSelectsComponentThroughViewportFallback) {
    ViewportInteractionHarness harness;
    (void)createUnitBox(harness);

    ASSERT_TRUE(harness.editor.selection().setMode(quader::QuaderSelectionMode::Face).ok);
    ASSERT_TRUE(harness.editor.selection().clear().ok);
    harness.router.setSelectionMode(quader::ViewportSelectionMode::Face);

    const quader::ViewportBeforeToolInteractionResult beforeTool =
            harness.controller.handleBeforeActiveTool(harness.editor, harness.frame(true, true, false, {400.0f, 300.0f}));
    EXPECT_FALSE(beforeTool.gizmoConsumed);
    harness.controller.handleAfterActiveTool(
            harness.editor,
            harness.frame(true, true, false, {400.0f, 300.0f}),
            quader::ViewportToolResult{},
            beforeTool);
    EXPECT_EQ(harness.editor.selection().summary().selectedFaceCount, 0u);

    harness.controller.handleAfterActiveTool(
            harness.editor,
            harness.frame(false, false, true, {400.0f, 300.0f}),
            quader::ViewportToolResult{},
            quader::ViewportBeforeToolInteractionResult{});

    const quader::QuaderSelectionSummary selection = harness.editor.selection().summary();
    EXPECT_EQ(selection.mode, quader::QuaderSelectionMode::Face);
    EXPECT_EQ(selection.selectedFaceCount, 1u);
    EXPECT_EQ(selection.activeKind, quader::QuaderActiveSelectionKind::Face);
    EXPECT_EQ(harness.router.state().selectionMode, quader::ViewportSelectionMode::Face);
    EXPECT_EQ(harness.editor.selection().hoverSummary().target.kind, quader::QuaderActiveSelectionKind::None);

    harness.controller.handleAfterActiveTool(
            harness.editor,
            harness.frame(),
            quader::ViewportToolResult{},
            quader::ViewportBeforeToolInteractionResult{});
    EXPECT_EQ(harness.editor.selection().hoverSummary().target.kind, quader::QuaderActiveSelectionKind::None);
}

TEST(ViewportInteractionControllerTests, ShiftClickRebuildsSliceQuadPreviewThroughViewportFallback) {
    ViewportInteractionHarness harness;
    const quader::QuaderModelingObjectRef box = createUnitBox(harness);

    ASSERT_TRUE(harness.editor.selection().setMode(quader::QuaderSelectionMode::Face).ok());
    ASSERT_TRUE(harness.editor.selection().selectFace(box, 1).ok());
    harness.router.setSelectionMode(quader::ViewportSelectionMode::Face);

    const quader::QuaderApiVoidResult preview =
            harness.editor.commands().execute(harness.editor.commands().commandIdForModelingAction("slice_quad"));
    ASSERT_TRUE(preview.ok()) << preview.errorMessage();
    const std::optional<quader::QuaderMeshSnapshot> oneFacePreview = harness.editor.query().mesh(box.mesh);
    ASSERT_TRUE(oneFacePreview.has_value());

    harness.camera.setViewDirection({-1.0f, 0.0f, 0.0f});
    const quader::ViewportScreenPoint sideFace = harness.camera.projectWorldPoint({1.0f, 0.0f, 0.0f});
    ASSERT_TRUE(sideFace.visible);
    harness.input.modifiers.shift = true;
    harness.input.operationPreviewActive = true;
    harness.input.mouseLocal = sideFace.position;
    harness.input.mouseScreen = sideFace.position;

    const quader::ViewportBeforeToolInteractionResult beforeTool =
            harness.controller.handleBeforeActiveTool(harness.editor, harness.frame(true, true, false, sideFace.position));
    EXPECT_FALSE(beforeTool.gizmoConsumed);
    harness.controller.handleAfterActiveTool(
            harness.editor,
            harness.frame(true, true, false, sideFace.position),
            quader::ViewportToolResult{},
            beforeTool);
    harness.controller.handleAfterActiveTool(
            harness.editor,
            harness.frame(false, false, true, sideFace.position),
            quader::ViewportToolResult{},
            quader::ViewportBeforeToolInteractionResult{});

    const quader::QuaderSelectionSummary summary = harness.editor.selection().summary();
    EXPECT_EQ(summary.mode, quader::QuaderSelectionMode::Face);
    EXPECT_GT(summary.selectedFaceCount, 1u);
    const std::optional<quader::QuaderMeshSnapshot> twoFacePreview = harness.editor.query().mesh(box.mesh);
    ASSERT_TRUE(twoFacePreview.has_value());
    EXPECT_GT(twoFacePreview->vertexCount, oneFacePreview->vertexCount);
    EXPECT_GT(twoFacePreview->primitiveCount, oneFacePreview->primitiveCount);
}

TEST(ViewportInteractionControllerTests, DragFrameBoxSelectsThroughViewportFallback) {
    ViewportInteractionHarness harness;
    (void)createUnitBox(harness);

    ASSERT_TRUE(harness.editor.selection().setMode(quader::QuaderSelectionMode::Object).ok);
    ASSERT_TRUE(harness.editor.selection().clear().ok);
    harness.router.setSelectionMode(quader::ViewportSelectionMode::Object);

    const quader::QVec2 start{300.0f, 200.0f};
    const quader::QVec2 end{500.0f, 400.0f};
    harness.input.mouseLocal = start;
    harness.input.mouseScreen = start;
    const quader::ViewportBeforeToolInteractionResult beforeTool =
            harness.controller.handleBeforeActiveTool(harness.editor, harness.frame(true, true, false, start));
    EXPECT_FALSE(beforeTool.gizmoConsumed);
    harness.controller.handleAfterActiveTool(
            harness.editor,
            harness.frame(true, true, false, start),
            quader::ViewportToolResult{},
            beforeTool);

    harness.input.mouseLocal = end;
    harness.input.mouseScreen = end;
    harness.controller.handleAfterActiveTool(
            harness.editor,
            harness.frame(false, true, false, start),
            quader::ViewportToolResult{},
            quader::ViewportBeforeToolInteractionResult{});
    const quader::ViewportSelectionMarquee marquee = harness.controller.selectionMarquee();
    ASSERT_TRUE(marquee.active);
    EXPECT_EQ(marquee.rule, quader::ModelingBoxSelectionRule::Contain);
    EXPECT_EQ(harness.editor.selection().summary().selectedObjectCount, 1u);

    harness.controller.handleAfterActiveTool(
            harness.editor,
            harness.frame(false, false, true, start),
            quader::ViewportToolResult{},
            quader::ViewportBeforeToolInteractionResult{});

    const quader::QuaderSelectionSummary selection = harness.editor.selection().summary();
    EXPECT_EQ(selection.mode, quader::QuaderSelectionMode::Object);
    EXPECT_EQ(selection.selectedObjectCount, 1u);
    EXPECT_EQ(harness.router.state().selectionMode, quader::ViewportSelectionMode::Object);
    EXPECT_TRUE(harness.controller.selectionMarquee().active);

    harness.controller.handleAfterActiveTool(
            harness.editor,
            harness.frame(),
            quader::ViewportToolResult{},
            quader::ViewportBeforeToolInteractionResult{});
    EXPECT_FALSE(harness.controller.selectionMarquee().active);
}

TEST(ViewportInteractionControllerTests, ShiftBoxDragAddsSelectionBeforeRelease) {
    ViewportInteractionHarness harness;
    const quader::QuaderModelingObjectRef left = createBoxAt(harness, {-3.0f, 0.0f, 0.0f});
    const quader::QuaderModelingObjectRef right = createBoxAt(harness, {3.0f, 0.0f, 0.0f});

    ASSERT_TRUE(harness.editor.selection().setMode(quader::QuaderSelectionMode::Object).ok);
    ASSERT_TRUE(harness.editor.selection().selectObject(left).ok);
    harness.router.setSelectionMode(quader::ViewportSelectionMode::Object);

    const quader::ViewportScreenPoint projectedRight = harness.camera.projectWorldPoint({3.0f, 0.0f, 0.0f});
    ASSERT_TRUE(projectedRight.visible);
    const quader::QVec2 start = projectedRight.position - quader::QVec2{120.0f, 120.0f};
    const quader::QVec2 end = projectedRight.position + quader::QVec2{120.0f, 120.0f};

    harness.input.modifiers.shift = true;
    harness.input.mouseLocal = start;
    harness.input.mouseScreen = start;
    const quader::ViewportBeforeToolInteractionResult beforeTool =
            harness.controller.handleBeforeActiveTool(harness.editor, harness.frame(true, true, false, start));
    EXPECT_FALSE(beforeTool.gizmoConsumed);
    harness.controller.handleAfterActiveTool(
            harness.editor,
            harness.frame(true, true, false, start),
            quader::ViewportToolResult{},
            beforeTool);

    harness.input.mouseLocal = end;
    harness.input.mouseScreen = end;
    harness.controller.handleAfterActiveTool(
            harness.editor,
            harness.frame(false, true, false, start),
            quader::ViewportToolResult{},
            quader::ViewportBeforeToolInteractionResult{});

    EXPECT_EQ(harness.editor.selection().summary().selectedObjectCount, 2u);
    const std::vector<quader::EntityId> selected = selectedEntities(harness);
    EXPECT_NE(std::find(selected.begin(), selected.end(), left.entity), selected.end());
    EXPECT_NE(std::find(selected.begin(), selected.end(), right.entity), selected.end());
}

TEST(ViewportInteractionControllerTests, PrimaryBoxDragRemovesSelectionBeforeRelease) {
    ViewportInteractionHarness harness;
    const quader::QuaderModelingObjectRef left = createBoxAt(harness, {-3.0f, 0.0f, 0.0f});
    const quader::QuaderModelingObjectRef right = createBoxAt(harness, {3.0f, 0.0f, 0.0f});

    ASSERT_TRUE(harness.editor.selection().setMode(quader::QuaderSelectionMode::Object).ok);
    ASSERT_TRUE(harness.editor.selection().selectObjects({left, right}).ok);
    harness.router.setSelectionMode(quader::ViewportSelectionMode::Object);

    const quader::ViewportScreenPoint projectedRight = harness.camera.projectWorldPoint({3.0f, 0.0f, 0.0f});
    ASSERT_TRUE(projectedRight.visible);
    const quader::QVec2 start = projectedRight.position - quader::QVec2{120.0f, 120.0f};
    const quader::QVec2 end = projectedRight.position + quader::QVec2{120.0f, 120.0f};

    harness.input.modifiers.primary = true;
    harness.input.mouseLocal = start;
    harness.input.mouseScreen = start;
    const quader::ViewportBeforeToolInteractionResult beforeTool =
            harness.controller.handleBeforeActiveTool(harness.editor, harness.frame(true, true, false, start));
    EXPECT_FALSE(beforeTool.gizmoConsumed);
    harness.controller.handleAfterActiveTool(
            harness.editor,
            harness.frame(true, true, false, start),
            quader::ViewportToolResult{},
            beforeTool);

    harness.input.mouseLocal = end;
    harness.input.mouseScreen = end;
    harness.controller.handleAfterActiveTool(
            harness.editor,
            harness.frame(false, true, false, start),
            quader::ViewportToolResult{},
            quader::ViewportBeforeToolInteractionResult{});

    EXPECT_EQ(harness.editor.selection().summary().selectedObjectCount, 1u);
    const std::vector<quader::EntityId> selected = selectedEntities(harness);
    EXPECT_NE(std::find(selected.begin(), selected.end(), left.entity), selected.end());
    EXPECT_EQ(std::find(selected.begin(), selected.end(), right.entity), selected.end());
}

TEST(ViewportInteractionControllerTests, FastDragFrameRetainsCommittedBoxMarqueeForRender) {
    ViewportInteractionHarness harness;
    (void)createUnitBox(harness);

    ASSERT_TRUE(harness.editor.selection().setMode(quader::QuaderSelectionMode::Object).ok);
    ASSERT_TRUE(harness.editor.selection().clear().ok);
    harness.router.setSelectionMode(quader::ViewportSelectionMode::Object);

    const quader::QVec2 start{500.0f, 400.0f};
    const quader::QVec2 end{300.0f, 200.0f};
    harness.input.mouseLocal = end;
    harness.input.mouseScreen = end;

    harness.controller.handleAfterActiveTool(
            harness.editor,
            harness.frame(true, false, true, start),
            quader::ViewportToolResult{},
            quader::ViewportBeforeToolInteractionResult{});

    const quader::ViewportSelectionMarquee marquee = harness.controller.selectionMarquee();
    ASSERT_TRUE(marquee.active);
    EXPECT_EQ(marquee.start.x, start.x);
    EXPECT_EQ(marquee.start.y, start.y);
    EXPECT_EQ(marquee.end.x, end.x);
    EXPECT_EQ(marquee.end.y, end.y);
    EXPECT_EQ(marquee.rule, quader::ModelingBoxSelectionRule::Crossing);
    EXPECT_EQ(harness.editor.selection().summary().selectedObjectCount, 1u);

    harness.controller.handleAfterActiveTool(
            harness.editor,
            harness.frame(),
            quader::ViewportToolResult{},
            quader::ViewportBeforeToolInteractionResult{});
    EXPECT_FALSE(harness.controller.selectionMarquee().active);
}

TEST(ViewportInteractionControllerTests, FastDragStartsFromAcceptedPressWhenCurrentMouseIsOutsideViewport) {
    ViewportInteractionHarness harness;
    (void)createUnitBox(harness);

    ASSERT_TRUE(harness.editor.selection().setMode(quader::QuaderSelectionMode::Object).ok);
    ASSERT_TRUE(harness.editor.selection().clear().ok);
    harness.router.setSelectionMode(quader::ViewportSelectionMode::Object);

    const quader::QVec2 start{500.0f, 400.0f};
    const quader::QVec2 end{-20.0f, -20.0f};
    harness.input.viewportHovered = false;
    harness.input.mouseLocal = end;
    harness.input.mouseScreen = end;

    harness.controller.handleAfterActiveTool(
            harness.editor,
            harness.frame(true, false, true, start),
            quader::ViewportToolResult{},
            quader::ViewportBeforeToolInteractionResult{});

    const quader::ViewportSelectionMarquee marquee = harness.controller.selectionMarquee();
    ASSERT_TRUE(marquee.active);
    EXPECT_EQ(marquee.start.x, start.x);
    EXPECT_EQ(marquee.start.y, start.y);
    EXPECT_EQ(marquee.end.x, end.x);
    EXPECT_EQ(marquee.end.y, end.y);
    EXPECT_EQ(marquee.rule, quader::ModelingBoxSelectionRule::Crossing);
    EXPECT_EQ(harness.editor.selection().summary().selectedObjectCount, 1u);
}

TEST(ViewportInteractionControllerTests, HoverSurvivesNavigationSuppressionAndClearsWhenPointerLeaves) {
    ViewportInteractionHarness harness;
    const quader::QuaderModelingObjectRef box = createUnitBox(harness);

    ASSERT_TRUE(harness.editor.selection().setMode(quader::QuaderSelectionMode::Face).ok);
    harness.router.setSelectionMode(quader::ViewportSelectionMode::Face);
    const quader::QuaderApiResult<quader::QuaderHoverSummary> hover =
            harness.editor.selection().hoverFace(box, 0);
    ASSERT_TRUE(hover.ok) << hover.message;
    ASSERT_EQ(harness.editor.selection().hoverSummary().target.kind, quader::QuaderActiveSelectionKind::Face);

    harness.inputResult.navigationActive = true;
    harness.controller.handleAfterActiveTool(
            harness.editor,
            harness.frame(),
            quader::ViewportToolResult{},
            quader::ViewportBeforeToolInteractionResult{});
    EXPECT_EQ(harness.editor.selection().hoverSummary().target.kind, quader::QuaderActiveSelectionKind::Face);

    harness.inputResult.navigationActive = false;
    harness.inputResult.cameraChanged = true;
    harness.controller.handleAfterActiveTool(
            harness.editor,
            harness.frame(),
            quader::ViewportToolResult{},
            quader::ViewportBeforeToolInteractionResult{});
    EXPECT_EQ(harness.editor.selection().hoverSummary().target.kind, quader::QuaderActiveSelectionKind::Face);

    harness.inputResult.cameraChanged = false;
    harness.input.viewportHovered = false;
    harness.controller.handleAfterActiveTool(
            harness.editor,
            harness.frame(),
            quader::ViewportToolResult{},
            quader::ViewportBeforeToolInteractionResult{});
    EXPECT_EQ(harness.editor.selection().hoverSummary().target.kind, quader::QuaderActiveSelectionKind::None);
}

TEST(ViewportInteractionControllerTests, MoveRotateAndScaleGizmosDragSelectedObjectThroughViewportFrames) {
    /// Names one transform tool case covered by the shared gizmo drag assertions.
    struct ToolCase {
        quader::ViewportToolId tool = quader::ViewportToolId::Move;
        const char* label = "";
    };

    for (const ToolCase toolCase : {
                 ToolCase{quader::ViewportToolId::Move, "move"},
                 ToolCase{quader::ViewportToolId::Rotate, "rotate"},
                 ToolCase{quader::ViewportToolId::Scale, "scale"},
         }) {
        SCOPED_TRACE(toolCase.label);
        ViewportInteractionHarness harness;
        const quader::QuaderModelingObjectRef box = createUnitBox(harness);
        ASSERT_TRUE(harness.editor.selection().selectObject(box).ok);
        ASSERT_TRUE(harness.editor.tools().activate(toolCase.tool).ok);
        harness.router.setActiveTool(toolCase.tool);

        const quader::ViewportBeforeToolInteractionResult warmup =
                harness.controller.handleBeforeActiveTool(harness.editor, harness.frame());
        EXPECT_FALSE(warmup.gizmoConsumed);
        const quader_editor_core::TransformGizmoFrame* frame = harness.controller.transformGizmoFrame();
        ASSERT_NE(frame, nullptr);

        quader::ViewportOverlayFrame overlay;
        quader::ViewportTransformGizmoOverlayBuilder::append(
                overlay,
                *frame,
                quader::ViewportCameraAdapter::overlayCamera(harness.camera, harness.input.viewportSize));
        EXPECT_FALSE(overlay.lines.empty());

        quader::QVec2 start{};
        quader::QVec2 update{};
        if (toolCase.tool == quader::ViewportToolId::Rotate) {
            const quader_editor_core::TransformGizmoRingSegmentPrimitive* ring =
                    findRing(*frame, quader_editor_core::TransformAxis::z);
            ASSERT_NE(ring, nullptr);
            start = ring->screen_start;
            update = rotatedPoint(frame->info.screen_pivot, start, 0.6f);
        } else if (toolCase.tool == quader::ViewportToolId::Scale) {
            start = frame->info.screen_pivot;
            update = start + quader::QVec2{120.0f, 0.0f};
        } else {
            const quader_editor_core::TransformGizmoAxisPrimitive* axis =
                    findAxis(*frame, quader_editor_core::TransformAxis::x);
            ASSERT_NE(axis, nullptr);
            start = axis->screen_tip;
            update = start + quader::QVec2{120.0f, 0.0f};
        }

        const quader::TransformComponent beforeTransform =
                *harness.session.scene().transform(box.entity);
        pressUpdateReleaseGizmo(harness, start, update);
        const quader::TransformComponent afterTransform =
                *harness.session.scene().transform(box.entity);

        if (toolCase.tool == quader::ViewportToolId::Move) {
            EXPECT_GT(length(afterTransform.position - beforeTransform.position), 0.001f);
        } else if (toolCase.tool == quader::ViewportToolId::Rotate) {
            EXPECT_GT(length(afterTransform.rotationEuler - beforeTransform.rotationEuler), 0.001f);
        } else {
            EXPECT_GT(length(afterTransform.scale - beforeTransform.scale), 0.001f);
        }
        EXPECT_EQ(harness.editor.selection().summary().selectedObjectCount, 1u);

        ASSERT_TRUE(harness.editor.history().canUndo());
        const quader::QuaderApiVoidResult undo =
                harness.editor.commands().execute(quader::QuaderCommand::HistoryUndo);
        ASSERT_TRUE(undo.ok()) << undo.errorMessage();
        EXPECT_TRUE(undo.changed());
        ASSERT_NE(harness.session.scene().transform(box.entity), nullptr);
        expectTransformNear(*harness.session.scene().transform(box.entity), beforeTransform);
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

    for (const ToolCase toolCase : {
                 ToolCase{quader::ViewportToolId::Move, "move"},
                 ToolCase{quader::ViewportToolId::Rotate, "rotate"},
                 ToolCase{quader::ViewportToolId::Scale, "scale"},
         }) {
        SCOPED_TRACE(toolCase.label);
        ViewportInteractionHarness harness;
        quader::QuaderCreatePointLightArgs args;
        args.name = "Viewport Point Light";
        args.selectCreated = true;
        const quader::QuaderApiResult<quader::QuaderEntityRef> light =
                harness.editor.scene().createPointLight(args);
        ASSERT_TRUE(light.ok()) << light.errorMessage();
        ASSERT_NE(harness.session.scene().light(light.value().id), nullptr);

        ASSERT_TRUE(harness.editor.tools().activate(toolCase.tool).ok);
        harness.router.setActiveTool(toolCase.tool);

        const quader::ViewportBeforeToolInteractionResult warmup =
                harness.controller.handleBeforeActiveTool(harness.editor, harness.frame());
        EXPECT_FALSE(warmup.gizmoConsumed);
        const quader_editor_core::TransformGizmoFrame* frame = harness.controller.transformGizmoFrame();
        ASSERT_NE(frame, nullptr);

        quader::QVec2 start{};
        quader::QVec2 update{};
        if (toolCase.tool == quader::ViewportToolId::Rotate) {
            const quader_editor_core::TransformGizmoRingSegmentPrimitive* ring =
                    findRing(*frame, quader_editor_core::TransformAxis::z);
            ASSERT_NE(ring, nullptr);
            start = ring->screen_start;
            update = rotatedPoint(frame->info.screen_pivot, start, 0.6f);
        } else if (toolCase.tool == quader::ViewportToolId::Scale) {
            start = frame->info.screen_pivot;
            update = start + quader::QVec2{120.0f, 0.0f};
        } else {
            const quader_editor_core::TransformGizmoAxisPrimitive* axis =
                    findAxis(*frame, quader_editor_core::TransformAxis::x);
            ASSERT_NE(axis, nullptr);
            start = axis->screen_tip;
            update = start + quader::QVec2{120.0f, 0.0f};
        }

        const quader::TransformComponent beforeTransform =
                *harness.session.scene().transform(light.value().id);
        pressUpdateReleaseGizmo(harness, start, update);
        const quader::TransformComponent afterTransform =
                *harness.session.scene().transform(light.value().id);

        if (toolCase.tool == quader::ViewportToolId::Move) {
            EXPECT_GT(length(afterTransform.position - beforeTransform.position), 0.001f);
        } else if (toolCase.tool == quader::ViewportToolId::Rotate) {
            EXPECT_GT(length(afterTransform.rotationEuler - beforeTransform.rotationEuler), 0.001f);
        } else {
            EXPECT_GT(length(afterTransform.scale - beforeTransform.scale), 0.001f);
        }
        EXPECT_EQ(harness.editor.selection().summary().activeKind, quader::QuaderActiveSelectionKind::Light);

        ASSERT_TRUE(harness.editor.history().canUndo());
        const quader::QuaderApiVoidResult undo =
                harness.editor.commands().execute(quader::QuaderCommand::HistoryUndo);
        ASSERT_TRUE(undo.ok()) << undo.errorMessage();
        EXPECT_TRUE(undo.changed());
        ASSERT_NE(harness.session.scene().transform(light.value().id), nullptr);
        expectTransformNear(*harness.session.scene().transform(light.value().id), beforeTransform);
    }
}

TEST(ViewportBoxOverlayBuilderTests, EmitsDashedFootprintOutlineWithoutFill) {
    quader::ViewportOverlayCamera camera;
    camera.eye = {0.0f, 10.0f, 0.0f};
    camera.forward = {0.0f, -1.0f, 0.0f};
    camera.right = {1.0f, 0.0f, 0.0f};
    camera.up = {0.0f, 0.0f, 1.0f};
    camera.viewportSize = {800.0f, 600.0f};
    camera.projection = quader::ViewportOverlayProjection::Orthographic;
    camera.orthographicSize = 8.0f;

    quader::ModelingBoxPreview footprint;
    footprint.ok = true;
    footprint.valid = true;
    footprint.dragState = quader::ModelingBoxDragState::Idle;
    footprint.footprintCount = 4u;
    footprint.footprint = {{
            {-1.0f, 0.0f, -1.0f},
            {1.0f, 0.0f, -1.0f},
            {1.0f, 0.0f, 1.0f},
            {-1.0f, 0.0f, 1.0f},
    }};

    quader::ViewportOverlayFrame footprintOverlay;
    quader::ViewportBoxOverlayBuilder::append(footprintOverlay, camera, footprint);
    ASSERT_GT(footprintOverlay.lines.size(), 4u);
    EXPECT_TRUE(footprintOverlay.triangles.empty());
    for (const quader::ViewportOverlayLine& line : footprintOverlay.lines) {
        EXPECT_EQ(line.layer, quader::ViewportOverlayLayer::ToolPreviewLine);
        EXPECT_EQ(line.color, quader::kOverlayToolBoxPreviewLineColor);
        EXPECT_FLOAT_EQ(line.widthPixels, 2.0f);
        EXPECT_EQ(line.styleRole, quader::ViewportOverlayStyleRole::ExplicitColor);
    }

    quader::ModelingBoxPreview box = footprint;
    box.dragState = quader::ModelingBoxDragState::Height;
    box.cornerCount = 8u;
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

    quader::ViewportOverlayFrame boxOverlay;
    quader::ViewportBoxOverlayBuilder::append(boxOverlay, camera, box);
    EXPECT_EQ(boxOverlay.lines.size(), footprintOverlay.lines.size());
    EXPECT_TRUE(boxOverlay.triangles.empty());
}

TEST(ViewportSelectionMarqueeOverlayBuilderTests, EmitsFillAndRuleSpecificEdges) {
    quader::ViewportOverlayCamera camera;
    camera.eye = {0.0f, 0.0f, 10.0f};
    camera.forward = {0.0f, 0.0f, -1.0f};
    camera.right = {1.0f, 0.0f, 0.0f};
    camera.up = {0.0f, 1.0f, 0.0f};
    camera.viewportSize = {800.0f, 600.0f};
    camera.projection = quader::ViewportOverlayProjection::Orthographic;
    camera.orthographicSize = 12.0f;

    quader::ViewportOverlayFrame inactiveOverlay;
    quader::ViewportSelectionMarquee inactive;
    quader::ViewportSelectionMarqueeOverlayBuilder::append(inactiveOverlay, camera, inactive);
    EXPECT_TRUE(inactiveOverlay.lines.empty());
    EXPECT_TRUE(inactiveOverlay.triangles.empty());

    quader::ViewportSelectionMarquee contain;
    contain.active = true;
    contain.start = {100.0f, 120.0f};
    contain.end = {180.0f, 160.0f};
    contain.rule = quader::ModelingBoxSelectionRule::Contain;

    quader::ViewportOverlayFrame containOverlay;
    quader::ViewportSelectionMarqueeOverlayBuilder::append(containOverlay, camera, contain);
    ASSERT_EQ(containOverlay.triangles.size(), 2u);
    ASSERT_EQ(containOverlay.lines.size(), 4u);
    for (const quader::ViewportOverlayTriangle& triangle : containOverlay.triangles) {
        EXPECT_EQ(triangle.layer, quader::ViewportOverlayLayer::ToolPreviewFill);
        EXPECT_EQ(triangle.color, quader::kOverlaySelectionMarqueeFillColor);
        EXPECT_EQ(triangle.styleRole, quader::ViewportOverlayStyleRole::ExplicitColor);
        EXPECT_EQ(triangle.source, quader::ViewportOverlaySource::Tool);
    }
    for (const quader::ViewportOverlayLine& line : containOverlay.lines) {
        EXPECT_EQ(line.layer, quader::ViewportOverlayLayer::ToolPreviewLine);
        EXPECT_EQ(line.color, quader::kOverlaySelectionMarqueeLineColor);
        EXPECT_FLOAT_EQ(line.widthPixels, 1.5f);
        EXPECT_EQ(line.styleRole, quader::ViewportOverlayStyleRole::ExplicitColor);
        EXPECT_EQ(line.source, quader::ViewportOverlaySource::Tool);
    }

    quader::ViewportSelectionMarquee crossing = contain;
    crossing.rule = quader::ModelingBoxSelectionRule::Crossing;
    quader::ViewportOverlayFrame crossingOverlay;
    quader::ViewportSelectionMarqueeOverlayBuilder::append(crossingOverlay, camera, crossing);
    EXPECT_EQ(crossingOverlay.triangles.size(), 2u);
    EXPECT_GT(crossingOverlay.lines.size(), containOverlay.lines.size());
}
