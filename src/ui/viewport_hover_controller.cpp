////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/viewport_hover_controller.hpp"

#include "viewport/viewport_camera_adapter.hpp"

namespace quader {
namespace {

bool vec3Matches(QVec3 left, QVec3 right) {
    return left.x == right.x && left.y == right.y && left.z == right.z;
}

} // namespace

bool ViewportHoverWorkKey::matches(const ViewportHoverWorkKey& other) const {
    return valid &&
            other.valid &&
            paneId == other.paneId &&
            activeTool == other.activeTool &&
            selectionMode == other.selectionMode &&
            boxDragState == other.boxDragState &&
            gridPreset == other.gridPreset &&
            mouseLocal.x == other.mouseLocal.x &&
            mouseLocal.y == other.mouseLocal.y &&
            viewportSize.x == other.viewportSize.x &&
            viewportSize.y == other.viewportSize.y &&
            vec3Matches(cameraEye, other.cameraEye) &&
            vec3Matches(cameraForward, other.cameraForward) &&
            vec3Matches(cameraRight, other.cameraRight) &&
            vec3Matches(cameraUp, other.cameraUp) &&
            cameraProjection == other.cameraProjection &&
            cameraOrthographicSize == other.cameraOrthographicSize &&
            cameraFovDegrees == other.cameraFovDegrees &&
            cameraNearPlane == other.cameraNearPlane &&
            cameraFarPlane == other.cameraFarPlane &&
            primaryModifier == other.primaryModifier &&
            shiftModifier == other.shiftModifier &&
            altModifier == other.altModifier &&
            gizmoConsumed == other.gizmoConsumed &&
            vertexRadius == other.vertexRadius &&
            edgeRadius == other.edgeRadius &&
            sceneRevision == other.sceneRevision &&
            assetRevision == other.assetRevision &&
            contentRevision == other.contentRevision &&
            selectionRevision == other.selectionRevision;
}

ViewportHoverWorkKey ViewportHoverController::makeKey(
        const ViewportCameraController& camera,
        const ViewportHoverWorkRequest& request) const {
    const ViewportOverlayCamera overlayCamera = ViewportCameraAdapter::overlayCamera(camera, request.viewportSize);
    return {
            .valid = true,
            .paneId = request.paneId,
            .activeTool = request.activeTool,
            .selectionMode = request.selectionMode,
            .boxDragState = request.boxDragState,
            .gridPreset = request.gridPreset,
            .mouseLocal = request.mouseLocal,
            .viewportSize = request.viewportSize,
            .cameraEye = overlayCamera.eye,
            .cameraForward = overlayCamera.forward,
            .cameraRight = overlayCamera.right,
            .cameraUp = overlayCamera.up,
            .cameraProjection = overlayCamera.projection,
            .cameraOrthographicSize = overlayCamera.orthographicSize,
            .cameraFovDegrees = overlayCamera.fovDegrees,
            .cameraNearPlane = overlayCamera.nearPlane,
            .cameraFarPlane = overlayCamera.farPlane,
            .primaryModifier = request.modifiers.primary,
            .shiftModifier = request.modifiers.shift,
            .altModifier = request.modifiers.alt,
            .gizmoConsumed = request.gizmoConsumed,
            .vertexRadius = request.vertexRadius,
            .edgeRadius = request.edgeRadius,
            .sceneRevision = request.sceneRevision,
            .assetRevision = request.assetRevision,
            .contentRevision = request.contentRevision,
            .selectionRevision = request.selectionRevision,
    };
}

bool ViewportHoverController::claim(const ViewportHoverWorkKey& key) {
    if (lastKey_.matches(key)) {
        return false;
    }
    lastKey_ = key;
    return true;
}

void ViewportHoverController::invalidate() {
    lastKey_.valid = false;
}

} // namespace quader
