////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "math/Types.h"

namespace quader {

/**
 * Enumerates ViewportDragMode values used by the viewport layer.
 */
enum class ViewportDragMode {
    None,
    Orbit,
    OrbitSnap,
    Pan,
    Fly
};

/**
 * Enumerates ViewportViewAxis values used by the viewport layer.
 */
enum class ViewportViewAxis {
    X,
    Y,
    Z,
};

/**
 * Enumerates ViewportAxisLock values used by the viewport layer.
 */
enum class ViewportAxisLock {
    Locked,
    Unlocked,
};

/**
 * Enumerates ViewportProjection values used by the viewport layer.
 */
enum class ViewportProjection {
    Perspective,
    Orthographic,
};

/**
 * Stores the Viewport Camera State data contract used by the viewport input, layout, and camera layer.
 */
struct ViewportCameraState {
    QVec3 target = {};
    float yawRadians = -0.6435011f;
    float pitchRadians = -0.4636476f;
    float distance = 11.18034f;
    ViewportProjection projection = ViewportProjection::Perspective;
    float orthographicSize = 24.0f;
    float fovDegrees = 60.0f;
    float nearPlane = 0.05f;
    float farPlane = 1000.0f;
};

/**
 * Represents a Viewport Camera Pose value used by the viewport input, layout, and camera layer.
 */
struct ViewportCameraPose {
    QVec3 eye;
    QVec3 target;
    QVec3 up;
};

/**
 * Represents a Viewport Ray value used by the viewport input, layout, and camera layer.
 */
struct ViewportRay {
    QVec3 origin;
    QVec3 direction;
};

/**
 * Represents a Viewport Screen Point value used by the viewport input, layout, and camera layer.
 */
struct ViewportScreenPoint {
    QVec2 position;
    float depth = 0.0f;
    bool visible = false;
};

/**
 * Coordinates Viewport Camera Controller state transitions for the viewport input, layout, and camera layer.
 */
class ViewportCameraController {
public:
    ViewportCameraController();

    void setViewportSize(float width, float height);
    void beginDrag(QVec2 mouse, ViewportDragMode mode);
    void updateDrag(QVec2 mouse);
    void updateDrag(QVec2 mouse, ViewportDragMode mode);
    void endDrag();
    void scroll(float wheelDelta);
    void scrollToCursor(float wheelDelta, QVec2 localMouse);
    void focusOrigin(float distance = 64.0f);
    void focusBounds(QVec3 minBounds, QVec3 maxBounds, float margin = 1.25f);
    void viewAxis(ViewportViewAxis axis, int sign, ViewportAxisLock lock = ViewportAxisLock::Locked);
    void unlockOrthographicView();
    void setViewDirection(QVec3 forwardDirection);
    void updateDragDelta(QVec2 delta);
    void flyMove(QVec3 localDirection, float deltaSeconds);
    void adjustFlySpeed(float wheelSteps);
    void resetFlySpeed();

    const ViewportCameraState& state() const { return state_; }
    float flySpeed() const { return flySpeed_; }
    bool flySpeedIsDefault() const;
    ViewportCameraPose pose() const;
    ViewportRay rayFromViewportPoint(QVec2 localMouse) const;
    ViewportScreenPoint projectWorldPoint(QVec3 worldPoint) const;
    bool isDragging() const { return dragMode_ != ViewportDragMode::None; }
    bool orthographicViewLocked() const { return orthographicViewLocked_; }

private:
    float zoomedDistance(float wheelDelta) const;
    QVec3 forward() const;
    QVec3 right() const;
    QVec3 up() const;
    void setCameraBasis(QVec3 forwardDirection, QVec3 upDirection);
    void syncBasisFromAngles();
    void syncAnglesFromBasis();
    void clearOrbitSnapSource();

    ViewportCameraState state_;
    QVec3 cameraForward_ = {0.0f, 0.0f, -1.0f};
    QVec3 cameraUp_ = {0.0f, 1.0f, 0.0f};
    QVec3 pivot_ = {};
    float viewportWidth_ = 1.0f;
    float viewportHeight_ = 1.0f;
    float flySpeed_ = 6.0f;
    QVec2 lastMouse_ = {};
    ViewportDragMode dragMode_ = ViewportDragMode::None;
    bool orthographicViewLocked_ = false;
    ViewportCameraPose orbitSnapSourcePose_ = {};
    bool hasOrbitSnapSource_ = false;
};

} // namespace quader
