#pragma once

#include "quader/math/Types.h"

namespace quader {

enum class ViewportDragMode {
    None,
    Orbit,
    OrbitSnap,
    Pan,
    Fly
};

enum class ViewportViewAxis {
    X,
    Y,
    Z,
};

enum class ViewportProjection {
    Perspective,
    Orthographic,
};

struct ViewportCameraState {
    Vec3 target = {};
    float yawRadians = -0.6435011f;
    float pitchRadians = -0.4636476f;
    float distance = 11.18034f;
    ViewportProjection projection = ViewportProjection::Perspective;
    float orthographicSize = 24.0f;
    float fovDegrees = 60.0f;
    float nearPlane = 0.05f;
    float farPlane = 1000.0f;
};

struct ViewportCameraPose {
    Vec3 eye;
    Vec3 target;
    Vec3 up;
};

struct ViewportRay {
    Vec3 origin;
    Vec3 direction;
};

struct ViewportScreenPoint {
    Vec2 position;
    float depth = 0.0f;
    bool visible = false;
};

class ViewportCameraController {
public:
    ViewportCameraController();

    void setViewportSize(float width, float height);
    void beginDrag(Vec2 mouse, ViewportDragMode mode);
    void updateDrag(Vec2 mouse);
    void endDrag();
    void scroll(float wheelDelta);
    void scrollToCursor(float wheelDelta, Vec2 localMouse);
    void focusOrigin(float distance = 64.0f);
    void focusBounds(Vec3 minBounds, Vec3 maxBounds, float margin = 1.25f);
    void viewAxis(ViewportViewAxis axis, int sign);
    void setViewDirection(Vec3 forwardDirection);
    void flyMove(Vec3 localDirection, float deltaSeconds, bool fast, bool slow);

    const ViewportCameraState& state() const { return state_; }
    ViewportCameraPose pose() const;
    ViewportRay rayFromViewportPoint(Vec2 localMouse) const;
    ViewportScreenPoint projectWorldPoint(Vec3 worldPoint) const;
    bool isDragging() const { return dragMode_ != ViewportDragMode::None; }
    bool orthographicViewLocked() const { return orthographicViewLocked_; }

private:
    float zoomedDistance(float wheelDelta) const;
    Vec3 forward() const;
    Vec3 right() const;
    Vec3 up() const;

    ViewportCameraState state_;
    Vec3 pivot_ = {};
    float viewportWidth_ = 1.0f;
    float viewportHeight_ = 1.0f;
    Vec2 lastMouse_ = {};
    ViewportDragMode dragMode_ = ViewportDragMode::None;
    bool orthographicViewLocked_ = false;
};

} // namespace quader
