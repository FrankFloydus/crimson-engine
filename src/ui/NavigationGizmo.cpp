////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/NavigationGizmo.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

#include <imgui_internal.h>

#include <modeling/interactions/viewport_navigation.h>

namespace quader {
namespace {


std::uint8_t alphaByte(float alpha) {
    return static_cast<std::uint8_t>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f);
}

ImU32 viewAxisImColor(quader_editor_core::ViewAxis axis, float alpha = 1.0f) {
    const std::uint8_t a = alphaByte(alpha);
    switch (axis) {
    case quader_editor_core::ViewAxis::x:
        return IM_COL32(235, 76, 82, a);
    case quader_editor_core::ViewAxis::y:
        return IM_COL32(90, 205, 118, a);
    case quader_editor_core::ViewAxis::z:
        return IM_COL32(78, 142, 255, a);
    }
    return IM_COL32(255, 255, 255, a);
}

ViewportViewAxis toViewportViewAxis(quader_editor_core::ViewAxis axis) {
    switch (axis) {
    case quader_editor_core::ViewAxis::x:
        return ViewportViewAxis::X;
    case quader_editor_core::ViewAxis::y:
        return ViewportViewAxis::Y;
    case quader_editor_core::ViewAxis::z:
        return ViewportViewAxis::Z;
    }
    return ViewportViewAxis::Z;
}

quader_poly::Vec3 toRuntime(Vec3 value) {
    return {value.x, value.y, value.z};
}

quader_editor_core::ViewportCameraState makeRuntimeCamera(const ViewportCameraController& camera, ImVec2 itemSize) {
    const ViewportCameraPose pose = camera.pose();
    const Vec3 forward = normalize(pose.target - pose.eye);
    const Vec3 right = normalize(cross(forward, pose.up));
    const Vec3 up = normalize(pose.up);

    quader_editor_core::ViewportCameraState runtimeCamera;
    runtimeCamera.projection = camera.state().projection == ViewportProjection::Orthographic
            ? quader_editor_core::CameraProjection::orthographic
            : quader_editor_core::CameraProjection::perspective;
    runtimeCamera.transform.origin = toRuntime(pose.eye);
    runtimeCamera.transform.x_axis = toRuntime(right);
    runtimeCamera.transform.y_axis = toRuntime(up);
    runtimeCamera.transform.z_axis = toRuntime(forward * -1.0f);
    runtimeCamera.viewport_size = {std::max(itemSize.x, 1.0f), std::max(itemSize.y, 1.0f)};
    runtimeCamera.orthographic_size = std::max(camera.state().orthographicSize, 0.0001f);
    runtimeCamera.fov_degrees = camera.state().fovDegrees;
    return runtimeCamera;
}


} // namespace

void NavigationGizmo::setVisible(bool visible) {
    visible_ = visible;
    if (!visible_) {
        dragging_ = false;
        dragPaneActive_ = false;
        dragStarted_ = false;
        pressedFace_ = false;
    }
}

bool NavigationGizmo::draw(ViewportCameraController& camera,
        ViewportPaneId paneId,
        ImVec2 itemMin,
        ImVec2 itemSize,
        Vec2 capturedMouseDelta,
        bool* viewportCursorCaptureRequested,
        Vec2* viewportCursorCapturePoint,
        bool interactive) {
    if (!visible_) {
        return false;
    }

    quader_editor_core::ViewCubePrimitiveInput input;
    input.camera_transform = makeRuntimeCamera(camera, itemSize).transform;
    input.viewport_size = {std::max(itemSize.x, 1.0f), std::max(itemSize.y, 1.0f)};
    input.widget_size = 70.0;
    input.margin = {46.0f, 46.0f};

    const quader_editor_core::ViewCubePrimitives primitives = quader_editor_core::build_view_cube_primitives(input);
    if (!primitives.ok) {
        return false;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mouse = io.MousePos;
    const bool activeDrag = dragging_ && dragPaneActive_ && dragPaneId_ == paneId;
    const bool mouseInsideViewport =
            mouse.x >= itemMin.x && mouse.y >= itemMin.y &&
            mouse.x <= itemMin.x + itemSize.x && mouse.y <= itemMin.y + itemSize.y;

    quader_editor_core::ViewCubeHit hoverHit;
    if (interactive && mouseInsideViewport) {
        quader_editor_core::ViewCubePickInput pickInput;
        pickInput.primitives = input;
        pickInput.screen_position = {mouse.x - itemMin.x, mouse.y - itemMin.y};
        hoverHit = quader_editor_core::pick_view_cube(pickInput);
    }

    auto toViewCubeScreenPoint = [&](quader_poly::Vec2 point) {
        return ImVec2{
                std::floor(itemMin.x + point.x) + 0.5f,
                std::floor(itemMin.y + point.y) + 0.5f,
        };
    };

    const ImDrawListFlags previousDrawFlags = drawList->Flags;
    drawList->Flags &= ~ImDrawListFlags_AntiAliasedFill;
    for (const quader_editor_core::ViewCubeFacePrimitive& face : primitives.faces) {
        std::vector<ImVec2> polygon;
        polygon.reserve(face.polygon.size());
        for (quader_poly::Vec2 point : face.polygon) {
            polygon.push_back(toViewCubeScreenPoint(point));
        }
        if (polygon.size() < 3u) {
            continue;
        }

        const float depth = std::clamp(static_cast<float>(face.depth), 0.0f, 1.0f);
        const std::uint8_t shade = static_cast<std::uint8_t>(178.0f + depth * 46.0f);
        const bool hoverFace =
                hoverHit.hit &&
                hoverHit.kind == quader_editor_core::ViewCubeHitKind::face &&
                hoverHit.axis == face.axis &&
                hoverHit.sign == face.sign;
        const bool pressedFace =
                activeDrag &&
                !dragStarted_ &&
                pressedFace_ &&
                pressedAxis_ == toViewportViewAxis(face.axis) &&
                pressedSign_ == face.sign;
        const bool highlighted = hoverFace || pressedFace;
        const ImU32 fillColor = highlighted
                ? IM_COL32(255, 221, 111, 255)
                : IM_COL32(shade, shade, static_cast<std::uint8_t>(std::min<int>(shade + 6, 255)), 248);
        drawList->AddConvexPolyFilled(
                polygon.data(),
                static_cast<int>(polygon.size()),
                fillColor);
    }
    drawList->Flags = previousDrawFlags;

    constexpr ImU32 neutralEdgeColor = IM_COL32(248, 250, 252, 205);
    for (const quader_editor_core::ViewCubeEdgePrimitive& edge : primitives.edges) {
        drawList->AddLine(
                toViewCubeScreenPoint(edge.screen_start),
                toViewCubeScreenPoint(edge.screen_end),
                neutralEdgeColor,
                1.5f);
    }

    for (const quader_editor_core::ViewCubeEdgePrimitive& edge : primitives.axis_edges) {
        drawList->AddLine(
                toViewCubeScreenPoint(edge.screen_start),
                toViewCubeScreenPoint(edge.screen_end),
                viewAxisImColor(edge.axis, 1.0f),
                2.8f);
    }

    for (const quader_editor_core::ViewCubeAxisMarkerPrimitive& marker : primitives.axis_markers) {
        const ImVec2 markerCenter{
                std::floor(itemMin.x + marker.screen_position.x) + 0.5f,
                std::floor(itemMin.y + marker.screen_position.y) + 0.5f};
        constexpr float radius = 8.0f;
        const ImU32 markerColor = viewAxisImColor(marker.axis, 1.0f);
        drawList->AddCircleFilled(markerCenter, radius, markerColor, 18);
        drawList->AddCircle(markerCenter, radius, IM_COL32(16, 20, 24, 112), 18, 1.2f);

        const std::string_view label = quader_editor_core::view_axis_label(marker.axis, marker.sign);
        constexpr float labelSize = 11.5f;
        ImFont* labelFont = ImGui::GetFont();
        const ImVec2 textSize = labelFont->CalcTextSizeA(
                labelSize,
                std::numeric_limits<float>::max(),
                0.0f,
                label.data(),
                label.data() + label.size());
        drawList->AddText(
                labelFont,
                labelSize,
                {markerCenter.x - textSize.x * 0.5f, markerCenter.y - textSize.y * 0.5f - 0.5f},
                IM_COL32(17, 22, 28, 255),
                label.data(),
                label.data() + label.size());
    }

    auto requestCapture = [&]() {
        if (viewportCursorCaptureRequested != nullptr) {
            *viewportCursorCaptureRequested = true;
        }
        if (viewportCursorCapturePoint != nullptr) {
            const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
            *viewportCursorCapturePoint = {
                    cursorCaptureScreen_.x - mainViewport->Pos.x,
                    cursorCaptureScreen_.y - mainViewport->Pos.y,
            };
        }
    };

    if (activeDrag) {
        requestCapture();
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const Vec2 fallbackDelta{io.MouseDelta.x, io.MouseDelta.y};
            const float capturedDeltaLengthSquared =
                    capturedMouseDelta.x * capturedMouseDelta.x + capturedMouseDelta.y * capturedMouseDelta.y;
            const Vec2 delta = capturedDeltaLengthSquared > 0.000001f ? capturedMouseDelta : fallbackDelta;
            accumulatedDrag_ = accumulatedDrag_ + delta;
            const float accumulatedLengthSquared =
                    accumulatedDrag_.x * accumulatedDrag_.x +
                    accumulatedDrag_.y * accumulatedDrag_.y;
            if (accumulatedLengthSquared > 9.0f) {
                dragStarted_ = true;
            }
            if (dragStarted_ &&
                    (delta.x * delta.x + delta.y * delta.y) > 0.000001f) {
                camera.updateDragDelta(delta);
            }
            return true;
        }

        if (!dragStarted_ && pressedFace_) {
            camera.viewAxis(pressedAxis_, pressedSign_, ViewportAxisLock::Unlocked);
        }
        camera.endDrag();
        dragging_ = false;
        dragPaneActive_ = false;
        dragStarted_ = false;
        pressedFace_ = false;
        return true;
    }

    if (!interactive || !mouseInsideViewport || !ImGui::IsMouseClicked(ImGuiMouseButton_Left) || !hoverHit.hit) {
        return false;
    }

    dragging_ = true;
    dragPaneActive_ = true;
    dragPaneId_ = paneId;
    dragStarted_ = false;
    pressedFace_ = hoverHit.kind == quader_editor_core::ViewCubeHitKind::face;
    pressedAxis_ = toViewportViewAxis(hoverHit.axis);
    pressedSign_ = hoverHit.sign;
    accumulatedDrag_ = {};
    cursorCaptureScreen_ = {mouse.x, mouse.y};
    camera.unlockOrthographicView();
    camera.beginDrag({mouse.x, mouse.y}, ViewportDragMode::Orbit);
    requestCapture();
    ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    return true;
}

} // namespace quader
