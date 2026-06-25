////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/QuaderEditorApi.h"
#include "editor/tools/ViewportTool.h"
#include "editor/viewport/ViewportInteractionController.h"
#include "render/ViewportOverlayPolicy.h"
#include "render/overlay/ViewportBoxOverlayBuilder.h"
#include "render/overlay/ViewportTransformGizmoOverlayBuilder.h"
#include "scene/document/ProjectSession.h"
#include "viewport/ViewportCameraAdapter.h"
#include "viewport/ViewportCameraController.h"
#include "viewport/ViewportInputRouter.h"
#include "viewport/ViewportSettings.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <optional>

namespace {

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
        : editor(session) {
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
                    quader::ModelingBounds{
                            .min = {-1.0f, -1.0f, -1.0f},
                            .max = {1.0f, 1.0f, 1.0f},
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

} // namespace

TEST(ViewportInteractionControllerTests, ClickFrameSelectsComponentThroughViewportFallback) {
    ViewportInteractionHarness harness;
    (void)createUnitBox(harness);

    ASSERT_TRUE(harness.editor.selection().setMode(quader::ModelingSelectionMode::Face).ok);
    harness.router.setSelectionMode(quader::ViewportSelectionMode::Face);

    const quader::ViewportBeforeToolInteractionResult beforeTool =
            harness.controller.handleBeforeActiveTool(harness.editor, harness.frame(true, true, false, {400.0f, 300.0f}));
    EXPECT_FALSE(beforeTool.gizmoConsumed);
    harness.controller.handleAfterActiveTool(
            harness.editor,
            harness.frame(true, true, false, {400.0f, 300.0f}),
            quader::ViewportToolResult{},
            beforeTool);

    const quader::ModelingSelectionSummary selection = harness.editor.selection().summary();
    EXPECT_EQ(selection.mode, quader::ModelingSelectionMode::Face);
    EXPECT_EQ(selection.selectedFaceCount, 1u);
    EXPECT_EQ(selection.activeKind, quader::ModelingActiveSelectionKind::Face);
    EXPECT_EQ(harness.router.state().selectionMode, quader::ViewportSelectionMode::Face);
    EXPECT_EQ(harness.editor.selection().hoverSummary().target.kind, quader::ModelingActiveSelectionKind::None);

    harness.controller.handleAfterActiveTool(
            harness.editor,
            harness.frame(),
            quader::ViewportToolResult{},
            quader::ViewportBeforeToolInteractionResult{});
    EXPECT_EQ(harness.editor.selection().hoverSummary().target.kind, quader::ModelingActiveSelectionKind::None);
}

TEST(ViewportInteractionControllerTests, MoveRotateAndScaleGizmosDragSelectedObjectThroughViewportFrames) {
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
    }
}

TEST(ViewportBoxOverlayBuilderTests, EmitsOldFootprintAndBoxLinePreviewContract) {
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
    quader::ViewportBoxOverlayBuilder::append(footprintOverlay, footprint);
    ASSERT_EQ(footprintOverlay.lines.size(), 4u);
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
    quader::ViewportBoxOverlayBuilder::append(boxOverlay, box);
    EXPECT_EQ(boxOverlay.lines.size(), 12u);
}
