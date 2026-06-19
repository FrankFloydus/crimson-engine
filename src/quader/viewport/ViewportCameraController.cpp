#include "quader/viewport/ViewportCameraController.h"

#if QUADER_HAS_MODELING_SDK
#include <quader_editor_core/tool/viewport_navigation.h>
#include <quader_editor_core/tool/viewport_projection.h>
#endif

#include <cmath>
#include <numbers>
#include <optional>

namespace quader {
namespace {

constexpr float kOrbitRadiansPerPixel = 0.006f;
constexpr float kFlyLookRadiansPerPixel = 0.004f;
constexpr float kNavigationZoomStepFactor = 1.18f;
constexpr float kFlyBaseSpeed = 6.0f;
constexpr float kFlyFastMultiplier = 4.0f;
constexpr float kFlySlowMultiplier = 0.25f;
constexpr float kFlyMaxDeltaSeconds = 0.25f;
constexpr float kMinCameraDistance = 0.05f;

float verticalFovRadians(float fovDegrees) {
    return fovDegrees * std::numbers::pi_v<float> / 180.0f;
}

#if QUADER_HAS_MODELING_SDK

quader_poly::Vec2 toSdk(Vec2 value) {
    return {value.x, value.y};
}

quader_poly::Vec3 toSdk(Vec3 value) {
    return {value.x, value.y, value.z};
}

Vec3 fromSdk(quader_poly::Vec3 value) {
    return {value.x, value.y, value.z};
}

quader_editor_core::ViewAxis toSdk(ViewportViewAxis axis) {
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

quader_poly::Transform3 makeSdkTransform(const ViewportCameraPose& pose) {
    Vec3 forward = normalize(pose.target - pose.eye);
    if (length(forward) <= 0.000001f) {
        forward = {0.0f, 0.0f, -1.0f};
    }

    Vec3 right = normalize(cross(forward, pose.up));
    if (length(right) <= 0.000001f) {
        right = {1.0f, 0.0f, 0.0f};
    }

    Vec3 up = normalize(cross(right, forward));
    if (length(up) <= 0.000001f) {
        up = {0.0f, 1.0f, 0.0f};
    }

    return {
            .x_axis = toSdk(right),
            .y_axis = toSdk(up),
            .z_axis = toSdk(forward * -1.0f),
            .origin = toSdk(pose.eye),
    };
}

quader_editor_core::ViewportCameraState makeSdkCamera(
        const ViewportCameraPose& pose,
        const ViewportCameraState& state,
        float viewportWidth,
        float viewportHeight) {
    quader_editor_core::ViewportCameraState camera;
    camera.projection = state.projection == ViewportProjection::Orthographic
            ? quader_editor_core::CameraProjection::orthographic
            : quader_editor_core::CameraProjection::perspective;
    camera.transform = makeSdkTransform(pose);
    camera.viewport_size = {std::max(viewportWidth, 1.0f), std::max(viewportHeight, 1.0f)};
    camera.orthographic_size = std::max(0.0001f, state.orthographicSize);
    camera.fov_degrees = state.fovDegrees;
    return camera;
}

float distanceAlongForwardToPivot(Vec3 origin, Vec3 forward, Vec3 pivot) {
    const float forwardDistance = dot(pivot - origin, forward);
    if (forwardDistance > kMinCameraDistance) {
        return forwardDistance;
    }
    return std::max(kMinCameraDistance, length(pivot - origin));
}

void applySdkTransform(ViewportCameraState& state,
        Vec3& pivot,
        const quader_editor_core::ViewportNavigationResult& result,
        std::optional<float> preservedViewDistance = std::nullopt) {
    if (!result.ok) {
        return;
    }

    const Vec3 origin = fromSdk(result.camera_transform.origin);
    Vec3 forward = normalize(fromSdk(result.camera_transform.z_axis) * -1.0f);
    if (length(forward) <= 0.000001f) {
        const float cp = std::cos(state.pitchRadians);
        forward = normalize({
                std::sin(state.yawRadians) * cp,
                std::sin(state.pitchRadians),
                -std::cos(state.yawRadians) * cp,
        });
    }

    pivot = fromSdk(result.pivot);
    const float viewDistance = std::max(kMinCameraDistance,
            preservedViewDistance.value_or(distanceAlongForwardToPivot(origin, forward, pivot)));
    const float clampedViewDistance = clamp(viewDistance, kMinCameraDistance, 500.0f);

    state.target = origin + forward * clampedViewDistance;
    state.distance = clampedViewDistance;
    if (result.has_camera_size) {
        state.orthographicSize = static_cast<float>(std::max(0.0001, result.camera_size));
    }
    state.pitchRadians = std::asin(clamp(forward.y, -1.0f, 1.0f));
    state.yawRadians = std::atan2(forward.x, -forward.z);
}

#endif

Vec3 fallbackForwardForAxis(ViewportViewAxis axis, int sign) {
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

void ViewportCameraController::setViewportSize(float width, float height) {
    viewportWidth_ = std::max(1.0f, width);
    viewportHeight_ = std::max(1.0f, height);
}

void ViewportCameraController::beginDrag(Vec2 mouse, ViewportDragMode mode) {
    dragMode_ = mode;
    lastMouse_ = mouse;
}

void ViewportCameraController::updateDrag(Vec2 mouse) {
    const Vec2 delta{mouse.x - lastMouse_.x, mouse.y - lastMouse_.y};
    lastMouse_ = mouse;

    if (dragMode_ == ViewportDragMode::Orbit || dragMode_ == ViewportDragMode::OrbitSnap) {
        if (orthographicViewLocked_) {
            state_.projection = ViewportProjection::Orthographic;
            return;
        }
#if QUADER_HAS_MODELING_SDK
        quader_editor_core::ViewportOrbitInput input;
        input.camera_transform = makeSdkTransform(pose());
        input.pivot = toSdk(pivot_);
        input.screen_delta = toSdk(delta);
        input.sensitivity = kOrbitRadiansPerPixel;
        input.snap_to_orthogonal = dragMode_ == ViewportDragMode::OrbitSnap;
        applySdkTransform(state_, pivot_, quader_editor_core::viewport_orbit(input));
#else
        state_.yawRadians += delta.x * kOrbitRadiansPerPixel;
        state_.pitchRadians -= delta.y * kOrbitRadiansPerPixel;
        const float limit = std::numbers::pi_v<float> * 0.49f;
        state_.pitchRadians = clamp(state_.pitchRadians, -limit, limit);
#endif
    } else if (dragMode_ == ViewportDragMode::Pan) {
#if QUADER_HAS_MODELING_SDK
        quader_editor_core::ViewportPanInput input;
        input.camera = makeSdkCamera(pose(), state_, viewportWidth_, viewportHeight_);
        input.pivot = toSdk(pivot_);
        input.screen_delta = toSdk(delta);
        applySdkTransform(state_, pivot_, quader_editor_core::viewport_pan(input));
#else
        const float verticalSpan = 2.0f * state_.distance * std::tan(verticalFovRadians(state_.fovDegrees) * 0.5f);
        const float worldPerPixel = std::max(0.0001f, verticalSpan) / viewportHeight_;
        const Vec3 pan = right() * (-delta.x * worldPerPixel) + up() * (delta.y * worldPerPixel);
        state_.target = state_.target + pan;
        pivot_ = pivot_ + pan;
#endif
    } else if (dragMode_ == ViewportDragMode::Fly) {
#if QUADER_HAS_MODELING_SDK
        quader_editor_core::ViewportFlyLookInput input;
        input.camera_transform = makeSdkTransform(pose());
        input.pivot = toSdk(pivot_);
        input.screen_delta = toSdk(delta);
        input.sensitivity = kFlyLookRadiansPerPixel;
        applySdkTransform(state_, pivot_, quader_editor_core::viewport_fly_look(input), state_.distance);
#else
        const Vec3 eye = pose().eye;
        state_.yawRadians += delta.x * kFlyLookRadiansPerPixel;
        state_.pitchRadians -= delta.y * kFlyLookRadiansPerPixel;
        const float limit = std::numbers::pi_v<float> * 0.49f;
        state_.pitchRadians = clamp(state_.pitchRadians, -limit, limit);
        state_.target = eye + forward() * state_.distance;
#endif
    }
}

void ViewportCameraController::endDrag() {
    dragMode_ = ViewportDragMode::None;
}

void ViewportCameraController::scroll(float wheelDelta) {
#if QUADER_HAS_MODELING_SDK
    quader_editor_core::ViewportZoomInput input;
    input.camera = makeSdkCamera(pose(), state_, viewportWidth_, viewportHeight_);
    input.pivot = toSdk(pivot_);
    input.steps = wheelDelta;
    applySdkTransform(state_, pivot_, quader_editor_core::viewport_zoom(input));
#else
    state_.distance = zoomedDistance(wheelDelta);
#endif
}

void ViewportCameraController::scrollToCursor(float wheelDelta, Vec2 localMouse) {
    if (std::abs(wheelDelta) <= 0.000001f) {
        return;
    }

#if QUADER_HAS_MODELING_SDK
    const ViewportRay ray = rayFromViewportPoint(localMouse);
    quader_editor_core::ViewportZoomToCursorInput input;
    input.camera = makeSdkCamera(pose(), state_, viewportWidth_, viewportHeight_);
    input.pivot = toSdk(pivot_);
    input.ray_origin = toSdk(ray.origin);
    input.ray_direction = toSdk(ray.direction);
    input.has_cursor_target = false;
    input.steps = wheelDelta;
    applySdkTransform(state_, pivot_, quader_editor_core::viewport_zoom_to_cursor(input));
#else
    const float oldDistance = std::max(0.0001f, state_.distance);
    const float newDistance = zoomedDistance(wheelDelta);
    const float aspect = viewportWidth_ / viewportHeight_;
    const float halfHeight = oldDistance * std::tan(verticalFovRadians(state_.fovDegrees) * 0.5f);
    const float halfWidth = halfHeight * aspect;
    const float ndcX = clamp((localMouse.x / viewportWidth_) * 2.0f - 1.0f, -1.0f, 1.0f);
    const float ndcY = clamp(1.0f - (localMouse.y / viewportHeight_) * 2.0f, -1.0f, 1.0f);
    const Vec3 cursorOffset = right() * (ndcX * halfWidth) + up() * (ndcY * halfHeight);
    const float distanceRatio = newDistance / oldDistance;

    state_.target = state_.target + cursorOffset * (1.0f - distanceRatio);
    pivot_ = state_.target;
    state_.distance = newDistance;
#endif
}

void ViewportCameraController::focusOrigin(float distance) {
    pivot_ = {};
    state_.target = {};
    state_.distance = clamp(distance, kMinCameraDistance, 500.0f);
    if (state_.projection == ViewportProjection::Orthographic) {
        state_.orthographicSize = std::max(0.0001f, state_.distance);
    }
}

void ViewportCameraController::focusBounds(Vec3 minBounds, Vec3 maxBounds, float margin) {
#if QUADER_HAS_MODELING_SDK
    quader_editor_core::ViewportFrameBoundsInput input;
    input.camera = makeSdkCamera(pose(), state_, viewportWidth_, viewportHeight_);
    input.min_bounds = toSdk(minBounds);
    input.max_bounds = toSdk(maxBounds);
    input.margin = margin;
    applySdkTransform(state_, pivot_, quader_editor_core::viewport_frame_bounds(input));
#else
    const Vec3 center = (minBounds + maxBounds) * 0.5f;
    const Vec3 diagonal = maxBounds - minBounds;
    const float radius = std::max(length(diagonal) * 0.5f, 0.1f);
    const float safeMargin = std::max(margin, 1.0f);
    const float distance = radius * safeMargin / std::max(std::tan(verticalFovRadians(state_.fovDegrees) * 0.5f), 0.0001f);

    state_.target = center;
    pivot_ = center;
    state_.distance = clamp(distance, kMinCameraDistance, 500.0f);
#endif
}

void ViewportCameraController::viewAxis(ViewportViewAxis axis, int sign) {
#if QUADER_HAS_MODELING_SDK
    const quader_editor_core::ViewportCameraState camera =
            makeSdkCamera(pose(), state_, viewportWidth_, viewportHeight_);
    applySdkTransform(state_, pivot_,
            quader_editor_core::viewport_view_axis(camera, toSdk(pivot_), toSdk(axis), sign));
    state_.projection = ViewportProjection::Orthographic;
    orthographicViewLocked_ = true;
#else
    setViewDirection(fallbackForwardForAxis(axis, sign));
    const float visibleHeight = 2.0f * std::max(kMinCameraDistance, state_.distance) *
            std::tan(verticalFovRadians(state_.fovDegrees) * 0.5f);
    state_.orthographicSize = std::max(0.0001f, visibleHeight);
    state_.projection = ViewportProjection::Orthographic;
    orthographicViewLocked_ = true;
#endif
}

void ViewportCameraController::setViewDirection(Vec3 forwardDirection) {
    const Vec3 direction = normalize(forwardDirection);
    if (length(direction) <= 0.000001f) {
        return;
    }

    const float limit = std::numbers::pi_v<float> * 0.49f;
    state_.pitchRadians = clamp(std::asin(clamp(direction.y, -1.0f, 1.0f)), -limit, limit);
    state_.yawRadians = std::atan2(direction.x, -direction.z);
    pivot_ = state_.target;
}

void ViewportCameraController::flyMove(Vec3 localDirection, float deltaSeconds, bool fast, bool slow) {
    if (length(localDirection) <= 0.000001f) {
        return;
    }

    float speed = kFlyBaseSpeed;
    if (fast) {
        speed *= kFlyFastMultiplier;
    }
    if (slow) {
        speed *= kFlySlowMultiplier;
    }

    const float clampedDelta = clamp(deltaSeconds, 0.0f, kFlyMaxDeltaSeconds);
    const float distance = speed * clampedDelta;
    if (distance <= 0.000001f) {
        return;
    }

#if QUADER_HAS_MODELING_SDK
    quader_editor_core::ViewportFlyMoveInput input;
    input.camera_transform = makeSdkTransform(pose());
    input.pivot = toSdk(pivot_);
    input.local_direction = toSdk(localDirection);
    input.distance = quader_editor_core::viewport_fly_move_distance(deltaSeconds, fast, slow);
    applySdkTransform(state_, pivot_, quader_editor_core::viewport_fly_move(input), state_.distance);
#else
    const Vec3 worldDirection = normalize(
            right() * localDirection.x +
            up() * localDirection.y +
            forward() * localDirection.z);
    const Vec3 move = worldDirection * distance;
    state_.target = state_.target + move;
    pivot_ = pivot_ + move;
#endif
}

ViewportCameraPose ViewportCameraController::pose() const {
    const Vec3 fwd = forward();
    const Vec3 eye = state_.target - fwd * state_.distance;
    const Vec3 r = right();
    const Vec3 up = normalize(cross(r, fwd));
    return {eye, state_.target, up};
}

ViewportRay ViewportCameraController::rayFromViewportPoint(Vec2 localMouse) const {
#if QUADER_HAS_MODELING_SDK
    const quader_poly::Ray ray = quader_editor_core::ray_from_viewport_screen_point(
            makeSdkCamera(pose(), state_, viewportWidth_, viewportHeight_),
            toSdk(localMouse));
    return {
            .origin = fromSdk(ray.origin),
            .direction = normalize(fromSdk(ray.direction)),
    };
#else
    const ViewportCameraPose cameraPose = pose();
    const Vec3 fwd = normalize(cameraPose.target - cameraPose.eye);
    const Vec3 r = normalize(cross(fwd, cameraPose.up));
    const Vec3 u = normalize(cross(r, fwd));
    const float aspect = viewportWidth_ / viewportHeight_;
    const float ndcX = clamp((localMouse.x / viewportWidth_) * 2.0f - 1.0f, -1.0f, 1.0f);
    const float ndcY = clamp(1.0f - (localMouse.y / viewportHeight_) * 2.0f, -1.0f, 1.0f);
    if (state_.projection == ViewportProjection::Orthographic) {
        const float halfHeight = std::max(0.0001f, state_.orthographicSize * 0.5f);
        return {
                .origin = cameraPose.eye + r * (ndcX * halfHeight * aspect) + u * (ndcY * halfHeight),
                .direction = fwd,
        };
    }

    const float tanHalfFov = std::tan(verticalFovRadians(state_.fovDegrees) * 0.5f);
    const Vec3 direction = normalize(fwd + r * (ndcX * tanHalfFov * aspect) + u * (ndcY * tanHalfFov));
    return {
            .origin = cameraPose.eye,
            .direction = direction,
    };
#endif
}

ViewportScreenPoint ViewportCameraController::projectWorldPoint(Vec3 worldPoint) const {
#if QUADER_HAS_MODELING_SDK
    const quader_editor_core::ViewportCameraState camera =
            makeSdkCamera(pose(), state_, viewportWidth_, viewportHeight_);
    quader_poly::Vec2 screen{};
    const bool projected = quader_editor_core::project_viewport_point(camera, toSdk(worldPoint), screen);
    const ViewportCameraPose cameraPose = pose();
    const Vec3 fwd = normalize(cameraPose.target - cameraPose.eye);
    const float depth = dot(worldPoint - cameraPose.eye, fwd);
    return {
            .position = {screen.x, screen.y},
            .depth = depth,
            .visible = projected && depth > state_.nearPlane && depth < state_.farPlane,
    };
#else
    const ViewportCameraPose cameraPose = pose();
    const Vec3 fwd = normalize(cameraPose.target - cameraPose.eye);
    const Vec3 r = normalize(cross(fwd, cameraPose.up));
    const Vec3 u = normalize(cross(r, fwd));
    const Vec3 relative = worldPoint - cameraPose.eye;
    const float depth = dot(relative, fwd);

    if (state_.projection != ViewportProjection::Orthographic &&
            (depth <= state_.nearPlane || depth >= state_.farPlane)) {
        return {.depth = depth, .visible = false};
    }

    const float halfHeight = state_.projection == ViewportProjection::Orthographic
            ? std::max(0.0001f, state_.orthographicSize * 0.5f)
            : depth * std::tan(verticalFovRadians(state_.fovDegrees) * 0.5f);
    const float halfWidth = halfHeight * (viewportWidth_ / viewportHeight_);
    if (halfHeight <= 0.000001f || halfWidth <= 0.000001f) {
        return {.depth = depth, .visible = false};
    }

    const float ndcX = dot(relative, r) / halfWidth;
    const float ndcY = dot(relative, u) / halfHeight;
    const Vec2 position{
            (ndcX * 0.5f + 0.5f) * viewportWidth_,
            (0.5f - ndcY * 0.5f) * viewportHeight_,
    };

    return {
            .position = position,
            .depth = depth,
            .visible = std::abs(ndcX) <= 1.0f && std::abs(ndcY) <= 1.0f,
    };
#endif
}

float ViewportCameraController::zoomedDistance(float wheelDelta) const {
    const float zoomFactor = std::pow(kNavigationZoomStepFactor, wheelDelta);
    return clamp(state_.distance / zoomFactor, kMinCameraDistance, 500.0f);
}

Vec3 ViewportCameraController::forward() const {
    const float cp = std::cos(state_.pitchRadians);
    return normalize({
        std::sin(state_.yawRadians) * cp,
        std::sin(state_.pitchRadians),
        -std::cos(state_.yawRadians) * cp,
    });
}

Vec3 ViewportCameraController::right() const {
    Vec3 value = cross(forward(), {0.0f, 1.0f, 0.0f});
    if (length(value) <= 0.000001f) {
        value = {1.0f, 0.0f, 0.0f};
    }
    return normalize(value);
}

Vec3 ViewportCameraController::up() const {
    Vec3 value = cross(right(), forward());
    if (length(value) <= 0.000001f) {
        value = {0.0f, 1.0f, 0.0f};
    }
    return normalize(value);
}

} // namespace quader
