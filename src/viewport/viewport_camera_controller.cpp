////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "viewport/viewport_camera_controller.hpp"

#include "viewport/host_viewport_contract_adapter.hpp"

#include <editor/interactions/viewport_navigation.hpp>
#include <editor/interactions/viewport_projection.hpp>

#include <cmath>
#include <numbers>
#include <optional>

namespace quader {
namespace {

constexpr float kNavigationZoomStepFactor = 1.18f;
constexpr float kFlyMaxDeltaSeconds = 0.25f;
constexpr float kFlySpeedWheelStep = 1.0f;
constexpr float kMinCameraDistance = 0.05f;

float verticalFovRadians(float fovDegrees) {
    return fovDegrees * std::numbers::pi_v<float> / 180.0f;
}

float defaultFlySpeed() {
    return static_cast<float>(viewportHostDefaults().flyBaseSpeed);
}

float perspectiveVisibleHeight(const ViewportCameraState& state) {
    return std::max(0.0001f,
            2.0f * std::max(kMinCameraDistance, state.distance) *
                    std::tan(verticalFovRadians(state.fovDegrees) * 0.5f));
}

float perspectiveDistanceForVisibleHeight(float visibleHeight, float fovDegrees) {
    const float halfFovTangent = std::tan(verticalFovRadians(fovDegrees) * 0.5f);
    if (halfFovTangent <= 0.000001f) {
        return kMinCameraDistance;
    }
    return std::max(kMinCameraDistance, std::max(0.0001f, visibleHeight) / (2.0f * halfFovTangent));
}

float currentVisibleHeight(const ViewportCameraState& state) {
    return state.projection == ViewportProjection::Orthographic
            ? std::max(0.0001f, state.orthographicSize)
            : perspectiveVisibleHeight(state);
}

void syncPerspectiveDistanceFromOrthographicZoom(ViewportCameraState& state) {
    if (state.projection == ViewportProjection::Orthographic) {
        state.distance = perspectiveDistanceForVisibleHeight(state.orthographicSize, state.fovDegrees);
    }
}

QVec3 forwardFromAngles(float yawRadians, float pitchRadians) {
    const float cp = std::cos(pitchRadians);
    return normalize({
        std::sin(yawRadians) * cp,
        std::sin(pitchRadians),
        -std::cos(yawRadians) * cp,
    });
}

QVec3 fallbackUpForForward(QVec3 forward) {
    if (std::abs(forward.y) > 0.999f) {
        return forward.y < 0.0f ? QVec3{0.0f, 0.0f, 1.0f} : QVec3{0.0f, 0.0f, -1.0f};
    }
    return {0.0f, 1.0f, 0.0f};
}

/**
 * Represents a Camera Basis value used by the viewport input, layout, and camera layer.
 */
struct CameraBasis {
    QVec3 forward;
    QVec3 up;
};

CameraBasis makeCameraBasis(QVec3 forwardDirection, QVec3 upDirection, QVec3 fallbackForward = {0.0f, 0.0f, -1.0f}) {
    QVec3 forward = normalize(forwardDirection);
    if (length(forward) <= 0.000001f) {
        forward = normalize(fallbackForward);
    }
    if (length(forward) <= 0.000001f) {
        forward = {0.0f, 0.0f, -1.0f};
    }

    QVec3 candidateUp = normalize(upDirection);
    if (length(candidateUp) <= 0.000001f || std::abs(dot(candidateUp, forward)) > 0.999f) {
        candidateUp = fallbackUpForForward(forward);
    }

    QVec3 right = normalize(cross(forward, candidateUp));
    if (length(right) <= 0.000001f) {
        right = {1.0f, 0.0f, 0.0f};
    }

    QVec3 up = normalize(cross(right, forward));
    if (length(up) <= 0.000001f) {
        up = fallbackUpForForward(forward);
    }

    return {.forward = forward, .up = up};
}

void syncAnglesForForward(ViewportCameraState& state, QVec3 forward) {
    state.pitchRadians = std::asin(clamp(forward.y, -1.0f, 1.0f));
    state.yawRadians = std::atan2(forward.x, -forward.z);
}


quader::QVec2 toRuntime(QVec2 value) {
    return {value.x, value.y};
}

quader::QVec3 toRuntime(QVec3 value) {
    return {value.x, value.y, value.z};
}

QVec3 fromRuntime(quader::QVec3 value) {
    return {value.x, value.y, value.z};
}

quader_editor_core::ViewAxis toRuntime(ViewportViewAxis axis) {
    switch (axis) {
    case ViewportViewAxis::X:
        return quader_editor_core::ViewAxis::x;
    case ViewportViewAxis::Y:
        return quader_editor_core::ViewAxis::y;
    case ViewportViewAxis::Z:
        return quader_editor_core::ViewAxis::z;
    }
    return quader_editor_core::ViewAxis::z;
}

quader_poly::Transform3 makeRuntimeTransform(const ViewportCameraPose& pose) {
    QVec3 forward = normalize(pose.target - pose.eye);
    if (length(forward) <= 0.000001f) {
        forward = {0.0f, 0.0f, -1.0f};
    }

    QVec3 right = normalize(cross(forward, pose.up));
    if (length(right) <= 0.000001f) {
        right = {1.0f, 0.0f, 0.0f};
    }

    QVec3 up = normalize(cross(right, forward));
    if (length(up) <= 0.000001f) {
        up = {0.0f, 1.0f, 0.0f};
    }

    return {
            .x_axis = toRuntime(right),
            .y_axis = toRuntime(up),
            .z_axis = toRuntime(forward * -1.0f),
            .origin = toRuntime(pose.eye),
    };
}

ViewportCameraPose poseFromRuntimeTransform(const quader_poly::Transform3& transform, float distance) {
    const QVec3 origin = fromRuntime(transform.origin);
    QVec3 forward = normalize(fromRuntime(transform.z_axis) * -1.0f);
    if (length(forward) <= 0.000001f) {
        forward = {0.0f, 0.0f, -1.0f};
    }

    QVec3 up = normalize(fromRuntime(transform.y_axis));
    if (length(up) <= 0.000001f) {
        up = {0.0f, 1.0f, 0.0f};
    }

    return {
            .eye = origin,
            .target = origin + forward * std::max(kMinCameraDistance, distance),
            .up = up,
    };
}

quader_editor_core::ViewportCameraState makeRuntimeCamera(
        const ViewportCameraPose& pose,
        const ViewportCameraState& state,
        float viewportWidth,
        float viewportHeight) {
    quader_editor_core::ViewportCameraState camera;
    camera.projection = state.projection == ViewportProjection::Orthographic
            ? quader_editor_core::CameraProjection::orthographic
            : quader_editor_core::CameraProjection::perspective;
    camera.transform = makeRuntimeTransform(pose);
    camera.viewport_size = {std::max(viewportWidth, 1.0f), std::max(viewportHeight, 1.0f)};
    camera.orthographic_size = std::max(0.0001f, state.orthographicSize);
    camera.fov_degrees = state.fovDegrees;
    return camera;
}

float distanceAlongForwardToPivot(QVec3 origin, QVec3 forward, QVec3 pivot) {
    const float forwardDistance = dot(pivot - origin, forward);
    if (forwardDistance > kMinCameraDistance) {
        return forwardDistance;
    }
    return std::max(kMinCameraDistance, length(pivot - origin));
}

void applyRuntimeTransform(ViewportCameraState& state,
        QVec3& pivot,
        QVec3& cameraForward,
        QVec3& cameraUp,
        const quader_editor_core::ViewportNavigationResult& result,
        std::optional<float> preservedViewDistance = std::nullopt) {
    if (!result.ok) {
        return;
    }

    const QVec3 origin = fromRuntime(result.camera_transform.origin);
    QVec3 forward = normalize(fromRuntime(result.camera_transform.z_axis) * -1.0f);
    if (length(forward) <= 0.000001f) {
        forward = forwardFromAngles(state.yawRadians, state.pitchRadians);
    }
    const CameraBasis basis = makeCameraBasis(forward, fromRuntime(result.camera_transform.y_axis), cameraForward);

    pivot = fromRuntime(result.pivot);
    const float viewDistance = std::max(kMinCameraDistance,
            preservedViewDistance.value_or(distanceAlongForwardToPivot(origin, basis.forward, pivot)));
    const float clampedViewDistance = clamp(viewDistance, kMinCameraDistance, 500.0f);

    cameraForward = basis.forward;
    cameraUp = basis.up;
    state.target = origin + cameraForward * clampedViewDistance;
    state.distance = clampedViewDistance;
    if (result.has_camera_size) {
        state.orthographicSize = static_cast<float>(std::max(0.0001, result.camera_size));
    }
    syncAnglesForForward(state, cameraForward);
}


QVec3 fallbackForwardForAxis(ViewportViewAxis axis, int sign) {
    const float value = sign < 0 ? 1.0f : -1.0f;
    switch (axis) {
    case ViewportViewAxis::X:
        return {value, 0.0f, 0.0f};
    case ViewportViewAxis::Y:
        return {0.0f, value, 0.0f};
    case ViewportViewAxis::Z:
        return {0.0f, 0.0f, value};
    }
    return {0.0f, 0.0f, -1.0f};
}

} // namespace

ViewportCameraController::ViewportCameraController() {
    const ViewportHostDefaults defaults = viewportHostDefaults();
    state_.orthographicSize = static_cast<float>(defaults.defaultOrthographicSize);
    state_.fovDegrees = static_cast<float>(defaults.defaultFovDegrees);
    flySpeed_ = static_cast<float>(defaults.flyBaseSpeed);
    syncBasisFromAngles();
}

void ViewportCameraController::setViewportSize(float width, float height) {
    viewportWidth_ = std::max(1.0f, width);
    viewportHeight_ = std::max(1.0f, height);
}

void ViewportCameraController::beginDrag(QVec2 mouse, ViewportDragMode mode) {
    dragMode_ = mode;
    lastMouse_ = mouse;
    clearOrbitSnapSource();
    if (mode == ViewportDragMode::Fly) {
        state_.projection = ViewportProjection::Perspective;
        orthographicViewLocked_ = false;
    }
}

void ViewportCameraController::updateDrag(QVec2 mouse) {
    updateDrag(mouse, dragMode_);
}

void ViewportCameraController::updateDrag(QVec2 mouse, ViewportDragMode mode) {
    const ViewportDragMode previousMode = dragMode_;
    const bool enteringOrbitSnap =
            previousMode != ViewportDragMode::OrbitSnap && mode == ViewportDragMode::OrbitSnap;
    const QVec2 delta = enteringOrbitSnap ? QVec2{} : QVec2{mouse.x - lastMouse_.x, mouse.y - lastMouse_.y};
    lastMouse_ = mouse;
    if (previousMode == ViewportDragMode::OrbitSnap && mode != ViewportDragMode::OrbitSnap) {
        clearOrbitSnapSource();
    }
    dragMode_ = mode;
    updateDragDelta(delta);
}

void ViewportCameraController::updateDragDelta(QVec2 delta) {
    if (dragMode_ == ViewportDragMode::Orbit || dragMode_ == ViewportDragMode::OrbitSnap) {
        const ViewportHostDefaults defaults = viewportHostDefaults();
        if (orthographicViewLocked_) {
            state_.projection = ViewportProjection::Orthographic;
            clearOrbitSnapSource();
            return;
        }
        quader_editor_core::ViewportOrbitInput input;
        const bool snapToOrthographic = dragMode_ == ViewportDragMode::OrbitSnap;
        if (!snapToOrthographic) {
            clearOrbitSnapSource();
        }
        const float snappedOrthographicSize = currentVisibleHeight(state_);
        syncPerspectiveDistanceFromOrthographicZoom(state_);
        input.camera_transform = snapToOrthographic && hasOrbitSnapSource_
                ? makeRuntimeTransform(orbitSnapSourcePose_)
                : makeRuntimeTransform(pose());
        input.pivot = toRuntime(pivot_);
        input.screen_delta = toRuntime(delta);
        input.sensitivity = defaults.orbitRadiansPerPixel;
        input.snap_to_orthogonal = snapToOrthographic;
        const quader_editor_core::ViewportNavigationResult orbitResult = quader_editor_core::viewport_orbit(input);
        applyRuntimeTransform(state_, pivot_, cameraForward_, cameraUp_, orbitResult);
        if (snapToOrthographic && orbitResult.ok) {
            orbitSnapSourcePose_ = poseFromRuntimeTransform(orbitResult.unsnapped_camera_transform, state_.distance);
            hasOrbitSnapSource_ = true;
        } else {
            clearOrbitSnapSource();
        }
        if (snapToOrthographic && orbitResult.orthogonal_snap) {
            state_.projection = ViewportProjection::Orthographic;
            state_.orthographicSize = snappedOrthographicSize;
            orthographicViewLocked_ = false;
        } else {
            state_.projection = ViewportProjection::Perspective;
            orthographicViewLocked_ = false;
        }
    } else if (dragMode_ == ViewportDragMode::Pan) {
        quader_editor_core::ViewportPanInput input;
        input.camera = makeRuntimeCamera(pose(), state_, viewportWidth_, viewportHeight_);
        input.pivot = toRuntime(pivot_);
        input.screen_delta = toRuntime(delta);
        applyRuntimeTransform(state_, pivot_, cameraForward_, cameraUp_, quader_editor_core::viewport_pan(input));
    } else if (dragMode_ == ViewportDragMode::Fly) {
        const ViewportHostDefaults defaults = viewportHostDefaults();
        quader_editor_core::ViewportFlyLookInput input;
        input.camera_transform = makeRuntimeTransform(pose());
        input.pivot = toRuntime(pivot_);
        input.screen_delta = toRuntime(delta);
        input.sensitivity = defaults.flyLookRadiansPerPixel;
        applyRuntimeTransform(state_,
                pivot_,
                cameraForward_,
                cameraUp_,
                quader_editor_core::viewport_fly_look(input),
                state_.distance);
        state_.projection = ViewportProjection::Perspective;
        orthographicViewLocked_ = false;
        clearOrbitSnapSource();
    }
}

void ViewportCameraController::endDrag() {
    dragMode_ = ViewportDragMode::None;
    clearOrbitSnapSource();
}

void ViewportCameraController::scroll(float wheelDelta) {
    quader_editor_core::ViewportZoomInput input;
    input.camera = makeRuntimeCamera(pose(), state_, viewportWidth_, viewportHeight_);
    input.pivot = toRuntime(pivot_);
    input.steps = wheelDelta;
    applyRuntimeTransform(state_, pivot_, cameraForward_, cameraUp_, quader_editor_core::viewport_zoom(input));
}

void ViewportCameraController::scrollToCursor(float wheelDelta, QVec2 localMouse) {
    if (std::abs(wheelDelta) <= 0.000001f) {
        return;
    }

    const ViewportRay ray = rayFromViewportPoint(localMouse);
    quader_editor_core::ViewportZoomToCursorInput input;
    input.camera = makeRuntimeCamera(pose(), state_, viewportWidth_, viewportHeight_);
    input.pivot = toRuntime(pivot_);
    input.ray_origin = toRuntime(ray.origin);
    input.ray_direction = toRuntime(ray.direction);
    input.has_cursor_target = false;
    input.steps = wheelDelta;
    applyRuntimeTransform(state_, pivot_, cameraForward_, cameraUp_, quader_editor_core::viewport_zoom_to_cursor(input));
}

void ViewportCameraController::focusOrigin(float distance) {
    clearOrbitSnapSource();
    pivot_ = {};
    state_.target = {};
    state_.distance = clamp(distance, kMinCameraDistance, 500.0f);
    if (state_.projection == ViewportProjection::Orthographic) {
        state_.orthographicSize = std::max(0.0001f, state_.distance);
    }
}

void ViewportCameraController::focusBounds(QVec3 minBounds, QVec3 maxBounds, float margin) {
    clearOrbitSnapSource();
    quader_editor_core::ViewportFrameBoundsInput input;
    input.camera = makeRuntimeCamera(pose(), state_, viewportWidth_, viewportHeight_);
    input.min_bounds = toRuntime(minBounds);
    input.max_bounds = toRuntime(maxBounds);
    input.margin = margin;
    applyRuntimeTransform(state_, pivot_, cameraForward_, cameraUp_, quader_editor_core::viewport_frame_bounds(input));
}

void ViewportCameraController::viewAxis(ViewportViewAxis axis, int sign, ViewportAxisLock lock) {
    clearOrbitSnapSource();
    const quader_editor_core::ViewportCameraState camera =
            makeRuntimeCamera(pose(), state_, viewportWidth_, viewportHeight_);
    applyRuntimeTransform(state_, pivot_, cameraForward_, cameraUp_,
            quader_editor_core::viewport_view_axis(camera, toRuntime(pivot_), toRuntime(axis), sign));
    state_.projection = ViewportProjection::Orthographic;
    orthographicViewLocked_ = lock == ViewportAxisLock::Locked;
}

void ViewportCameraController::unlockOrthographicView() {
    clearOrbitSnapSource();
    orthographicViewLocked_ = false;
    if (state_.projection == ViewportProjection::Orthographic) {
        state_.projection = ViewportProjection::Perspective;
    }
}

void ViewportCameraController::setViewDirection(QVec3 forwardDirection) {
    clearOrbitSnapSource();
    const QVec3 direction = normalize(forwardDirection);
    if (length(direction) <= 0.000001f) {
        return;
    }

    setCameraBasis(direction, fallbackUpForForward(direction));
    pivot_ = state_.target;
}

void ViewportCameraController::flyMove(QVec3 localDirection, float deltaSeconds) {
    if (length(localDirection) <= 0.000001f) {
        return;
    }

    const float clampedDelta = clamp(deltaSeconds, 0.0f, kFlyMaxDeltaSeconds);
    const float distance = flySpeed_ * clampedDelta;
    if (distance <= 0.000001f) {
        return;
    }

    quader_editor_core::ViewportFlyMoveInput input;
    input.camera_transform = makeRuntimeTransform(pose());
    input.pivot = toRuntime(pivot_);
    input.local_direction = toRuntime(localDirection);
    input.distance = distance;
    applyRuntimeTransform(state_,
            pivot_,
            cameraForward_,
            cameraUp_,
            quader_editor_core::viewport_fly_move(input),
            state_.distance);
}

void ViewportCameraController::adjustFlySpeed(float wheelSteps) {
    if (std::abs(wheelSteps) <= 0.000001f) {
        return;
    }
    flySpeed_ = clamp(flySpeed_ + wheelSteps * kFlySpeedWheelStep, 1.0f, 1000.0f);
}

void ViewportCameraController::resetFlySpeed() {
    flySpeed_ = defaultFlySpeed();
}

bool ViewportCameraController::flySpeedIsDefault() const {
    return std::abs(flySpeed_ - defaultFlySpeed()) <= 0.0001f;
}

ViewportCameraPose ViewportCameraController::pose() const {
    const QVec3 fwd = forward();
    const QVec3 eye = state_.target - fwd * state_.distance;
    return {eye, state_.target, up()};
}

ViewportRay ViewportCameraController::rayFromViewportPoint(QVec2 localMouse) const {
    const quader_poly::Ray ray = quader_editor_core::ray_from_viewport_screen_point(
            makeRuntimeCamera(pose(), state_, viewportWidth_, viewportHeight_),
            toRuntime(localMouse));
    return {
            .origin = fromRuntime(ray.origin),
            .direction = normalize(fromRuntime(ray.direction)),
    };
}

ViewportScreenPoint ViewportCameraController::projectWorldPoint(QVec3 worldPoint) const {
    const quader_editor_core::ViewportCameraState camera =
            makeRuntimeCamera(pose(), state_, viewportWidth_, viewportHeight_);
    quader::QVec2 screen{};
    const bool projected = quader_editor_core::project_viewport_point(camera, toRuntime(worldPoint), screen);
    const ViewportCameraPose cameraPose = pose();
    const QVec3 fwd = normalize(cameraPose.target - cameraPose.eye);
    const float depth = dot(worldPoint - cameraPose.eye, fwd);
    return {
            .position = {screen.x, screen.y},
            .depth = depth,
            .visible = projected && depth > state_.nearPlane && depth < state_.farPlane,
    };
}

float ViewportCameraController::zoomedDistance(float wheelDelta) const {
    const float zoomFactor = std::pow(kNavigationZoomStepFactor, wheelDelta);
    return clamp(state_.distance / zoomFactor, kMinCameraDistance, 500.0f);
}

QVec3 ViewportCameraController::forward() const {
    return cameraForward_;
}

QVec3 ViewportCameraController::right() const {
    QVec3 value = cross(cameraForward_, cameraUp_);
    if (length(value) <= 0.000001f) {
        value = {1.0f, 0.0f, 0.0f};
    }
    return normalize(value);
}

QVec3 ViewportCameraController::up() const {
    QVec3 value = normalize(cameraUp_);
    if (length(value) <= 0.000001f) {
        value = {0.0f, 1.0f, 0.0f};
    }
    return value;
}

void ViewportCameraController::setCameraBasis(QVec3 forwardDirection, QVec3 upDirection) {
    const CameraBasis basis = makeCameraBasis(forwardDirection, upDirection, cameraForward_);
    cameraForward_ = basis.forward;
    cameraUp_ = basis.up;
    syncAnglesFromBasis();
}

void ViewportCameraController::syncBasisFromAngles() {
    setCameraBasis(forwardFromAngles(state_.yawRadians, state_.pitchRadians),
            fallbackUpForForward(forwardFromAngles(state_.yawRadians, state_.pitchRadians)));
}

void ViewportCameraController::syncAnglesFromBasis() {
    syncAnglesForForward(state_, cameraForward_);
}

void ViewportCameraController::clearOrbitSnapSource() {
    orbitSnapSourcePose_ = {};
    hasOrbitSnapSource_ = false;
}

} // namespace quader
