#pragma once

#include "core/Types.h"
#include "modeling/ModelingSessionService.h"
#include "render/ViewportOverlayFrame.h"
#include "viewport/ViewportCameraController.h"
#include "viewport/ViewportInputRouter.h"
#include "viewport/ViewportLayout.h"

#include <cstdint>

namespace quader {

struct ViewportHoverWorkKey {
    bool valid = false;
    ViewportPaneId paneId = ViewportPaneId::Perspective;
    ViewportToolId activeTool = ViewportToolId::Select;
    ViewportSelectionMode selectionMode = ViewportSelectionMode::Object;
    ModelingBoxDragState boxDragState = ModelingBoxDragState::Idle;
    int gridPreset = 0;
    Vec2 mouseLocal = {};
    Vec2 viewportSize = {};
    Vec3 cameraEye = {};
    Vec3 cameraForward = {0.0f, 0.0f, -1.0f};
    Vec3 cameraRight = {1.0f, 0.0f, 0.0f};
    Vec3 cameraUp = {0.0f, 1.0f, 0.0f};
    ViewportOverlayProjection cameraProjection = ViewportOverlayProjection::Perspective;
    float cameraOrthographicSize = 24.0f;
    float cameraFovDegrees = 60.0f;
    float cameraNearPlane = 0.05f;
    float cameraFarPlane = 1000.0f;
    bool primaryModifier = false;
    bool shiftModifier = false;
    bool altModifier = false;
    bool gizmoConsumed = false;
    std::uint64_t sceneRevision = 0;
    std::uint64_t assetRevision = 0;
    std::uint64_t contentRevision = 0;
    std::uint64_t selectionRevision = 0;

    [[nodiscard]] bool matches(const ViewportHoverWorkKey& other) const;
};

struct ViewportHoverWorkRequest {
    ViewportPaneId paneId = ViewportPaneId::Perspective;
    ViewportToolId activeTool = ViewportToolId::Select;
    ViewportSelectionMode selectionMode = ViewportSelectionMode::Object;
    ModelingBoxDragState boxDragState = ModelingBoxDragState::Idle;
    int gridPreset = 0;
    Vec2 mouseLocal = {};
    Vec2 viewportSize = {};
    ViewportModifiers modifiers = {};
    bool gizmoConsumed = false;
    std::uint64_t sceneRevision = 0;
    std::uint64_t assetRevision = 0;
    std::uint64_t contentRevision = 0;
    std::uint64_t selectionRevision = 0;
};

class ViewportHoverController {
public:
    [[nodiscard]] ViewportHoverWorkKey makeKey(
            const ViewportCameraController& camera,
            const ViewportHoverWorkRequest& request) const;

    [[nodiscard]] bool claim(const ViewportHoverWorkKey& key);
    void invalidate();

private:
    ViewportHoverWorkKey lastKey_;
};

} // namespace quader
