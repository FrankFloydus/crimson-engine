#include "ui/TransformGizmoController.h"

#include "core/Logger.h"
#include "scene/SceneBounds.h"
#include "scene/TransformPivot.h"
#include "viewport/ViewportCameraAdapter.h"
#include "viewport/tools/ViewportToolCatalog.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

#if QUADER_HAS_MODELING_SDK
#include <quader_editor_core/tool/tool_transform_math.h>
#include <quader_editor_core/tool/transform_gizmo.h>
#include <quader_editor_core/tool_constants.h>
#endif

namespace quader {
namespace {

constexpr float kPi = 3.14159265358979323846f;

float distanceToSegmentSquared(ImVec2 point, ImVec2 a, ImVec2 b) {
    const ImVec2 ab{b.x - a.x, b.y - a.y};
    const ImVec2 ap{point.x - a.x, point.y - a.y};
    const float lengthSquared = ab.x * ab.x + ab.y * ab.y;
    if (lengthSquared <= 0.0001f) {
        const float dx = point.x - a.x;
        const float dy = point.y - a.y;
        return dx * dx + dy * dy;
    }

    const float t = std::clamp((ap.x * ab.x + ap.y * ab.y) / lengthSquared, 0.0f, 1.0f);
    const ImVec2 closest{a.x + ab.x * t, a.y + ab.y * t};
    const float dx = point.x - closest.x;
    const float dy = point.y - closest.y;
    return dx * dx + dy * dy;
}

float distanceSquared(ImVec2 a, ImVec2 b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return dx * dx + dy * dy;
}

bool entitySelected(const SceneDocument& document, EntityId entity) {
    const SelectionComponent* selection = document.selection(entity);
    return selection != nullptr && selection->selected;
}

bool isComponentSelectionMode(ModelingSelectionMode mode) {
    return mode == ModelingSelectionMode::Vertex ||
            mode == ModelingSelectionMode::Edge ||
            mode == ModelingSelectionMode::Face;
}

bool hasSelectedAncestor(const SceneDocument& document, EntityId entity) {
    EntityId parent = document.parent(entity);
    while (parent != InvalidEntityId) {
        if (entitySelected(document, parent)) {
            return true;
        }
        parent = document.parent(parent);
    }
    return false;
}

Vec3 parentLocalPointForWorldPoint(const SceneDocument& document, EntityId entity, Vec3 worldPoint) {
    const EntityId parent = document.parent(entity);
    if (parent == InvalidEntityId) {
        return worldPoint;
    }
    return transformPoint(inverse(document.worldTransformMatrix(parent)), worldPoint);
}

Vec3 parentLocalVectorForWorldVector(const SceneDocument& document, EntityId entity, Vec3 worldVector) {
    const EntityId parent = document.parent(entity);
    if (parent == InvalidEntityId) {
        return worldVector;
    }
    return transformVector(inverse(document.worldTransformMatrix(parent)), worldVector);
}

Vec3 documentLocalPivotForWorldPoint(const SceneDocument& document, EntityId entity, Vec3 worldPivot) {
    return transformPoint(inverse(document.worldTransformMatrix(entity)), worldPivot);
}

Vec3 worldAxisForHandle(TransformAxisHandle axis) {
    switch (axis) {
    case TransformAxisHandle::X:
        return {1.0f, 0.0f, 0.0f};
    case TransformAxisHandle::Y:
        return {0.0f, 1.0f, 0.0f};
    case TransformAxisHandle::Z:
        return {0.0f, 0.0f, 1.0f};
    case TransformAxisHandle::XY:
    case TransformAxisHandle::XZ:
    case TransformAxisHandle::YZ:
    case TransformAxisHandle::All:
    case TransformAxisHandle::None:
        return {};
    }
    return {};
}

glm::quat normalizedRotationQuaternion(Vec3 eulerRadians) {
    return glm::normalize(toGlm(quatFromEulerRadians(eulerRadians)));
}

glm::quat worldRotationQuaternion(const SceneDocument& document, EntityId entity) {
    const TransformComponent* transform = document.transform(entity);
    const glm::quat local = transform == nullptr
            ? glm::quat{1.0f, 0.0f, 0.0f, 0.0f}
            : normalizedRotationQuaternion(transform->rotationEuler);
    const EntityId parent = document.parent(entity);
    if (parent == InvalidEntityId) {
        return local;
    }
    return glm::normalize(worldRotationQuaternion(document, parent) * local);
}

TransformComponent localTransformAfterWorldQuaternionRotation(
        const SceneDocument& document,
        EntityId entity,
        const TransformComponent& startTransform,
        const Mat4& startWorld,
        Vec3 worldAxis,
        float angleRadians,
        Vec3 worldPivot,
        Vec3 eulerReference) {
    const Vec3 normalizedAxis = normalize(worldAxis);
    if (length(normalizedAxis) <= 0.000001f || std::abs(angleRadians) <= 0.000001f) {
        return startTransform;
    }

    const glm::quat delta = glm::normalize(glm::angleAxis(angleRadians, toGlm(normalizedAxis)));
    const Vec3 startWorldOrigin = transformPoint(startWorld, {});
    const Vec3 nextWorldOrigin =
            worldPivot + fromGlm(delta * toGlm(startWorldOrigin - worldPivot));

    const EntityId parent = document.parent(entity);
    const glm::quat parentWorldRotation = parent == InvalidEntityId
            ? glm::quat{1.0f, 0.0f, 0.0f, 0.0f}
            : worldRotationQuaternion(document, parent);
    const glm::quat startLocalRotation = normalizedRotationQuaternion(startTransform.rotationEuler);
    const glm::quat startWorldRotation = parent == InvalidEntityId
            ? startLocalRotation
            : glm::normalize(parentWorldRotation * startLocalRotation);
    const glm::quat nextWorldRotation = glm::normalize(delta * startWorldRotation);
    const glm::quat nextLocalRotation = glm::normalize(glm::inverse(parentWorldRotation) * nextWorldRotation);

    TransformComponent next = startTransform;
    next.position = parentLocalPointForWorldPoint(document, entity, nextWorldOrigin);
    next.rotationEuler = positiveEulerRadians(equivalentEulerRadiansClosestTo(
            fromGlm(glm::eulerAngles(nextLocalRotation)),
            eulerReference));
    return next;
}

void pinDocumentLocalPivotToWorldPoint(
        const SceneDocument& document,
        EntityId entity,
        TransformComponent& transform,
        Vec3 localPivot,
        Vec3 worldPivot) {
    Transform spatial = transform.spatialTransform();
    spatial.position = {};
    transform.position =
            parentLocalPointForWorldPoint(document, entity, worldPivot) -
            transformPoint(composeTransform(spatial), localPivot);
}

void includeBounds(std::optional<SceneBounds>& result, const SceneBounds& bounds) {
    if (!result) {
        result = bounds;
        return;
    }
    includePoint(*result, bounds.min);
    includePoint(*result, bounds.max);
}

std::optional<SceneBounds> selectedTransformWorldBounds(const SceneDocument& document, const AssetRegistry& assets) {
    std::optional<SceneBounds> result;
    for (const EntityId entity : document.selectedEntities()) {
        const std::optional<SceneBounds> bounds = document.hasChildren(entity)
                ? entityHierarchyWorldBounds(document, assets, entity)
                : entityWorldBounds(document, assets, entity);
        if (bounds) {
            includeBounds(result, *bounds);
        }
    }
    return result;
}

bool handleIncludesAxis(TransformAxisHandle activeAxis, TransformAxisHandle axis) {
    if (activeAxis == TransformAxisHandle::None || activeAxis == axis) {
        return true;
    }
    if (activeAxis == TransformAxisHandle::XY) {
        return axis == TransformAxisHandle::X || axis == TransformAxisHandle::Y;
    }
    if (activeAxis == TransformAxisHandle::XZ) {
        return axis == TransformAxisHandle::X || axis == TransformAxisHandle::Z;
    }
    if (activeAxis == TransformAxisHandle::YZ) {
        return axis == TransformAxisHandle::Y || axis == TransformAxisHandle::Z;
    }
    return activeAxis == TransformAxisHandle::All;
}

float snapWorldValue(float value, double gridSize) {
    const double grid = std::max(gridSize, 0.000001);
    return static_cast<float>(std::round(static_cast<double>(value) / grid) * grid);
}

Vec3 snapWorldDeltaForHandle(Vec3 delta, TransformAxisHandle activeAxis, Vec3 startPivot, double gridSize, bool snapEnabled) {
    if (!snapEnabled) {
        return delta;
    }
    if (handleIncludesAxis(activeAxis, TransformAxisHandle::X)) {
        delta.x = snapWorldValue(startPivot.x + delta.x, gridSize) - startPivot.x;
    }
    if (handleIncludesAxis(activeAxis, TransformAxisHandle::Y)) {
        delta.y = snapWorldValue(startPivot.y + delta.y, gridSize) - startPivot.y;
    }
    if (handleIncludesAxis(activeAxis, TransformAxisHandle::Z)) {
        delta.z = snapWorldValue(startPivot.z + delta.z, gridSize) - startPivot.z;
    }
    return delta;
}

float snapRotationRadians(float radians, bool snapEnabled) {
    if (!snapEnabled) {
        return radians;
    }
    constexpr double kRotateSnapDegrees = 15.0;
    const double degrees = static_cast<double>(radians) * 180.0 / static_cast<double>(kPi);
    const double snappedDegrees = std::round(degrees / kRotateSnapDegrees) * kRotateSnapDegrees;
    return static_cast<float>(snappedDegrees * static_cast<double>(kPi) / 180.0);
}

Vec3 axisScaleForHandle(TransformAxisHandle activeAxis, double factor) {
    const float value = static_cast<float>(factor);
    Vec3 scale{1.0f, 1.0f, 1.0f};
    if (handleIncludesAxis(activeAxis, TransformAxisHandle::X)) {
        scale.x = value;
    }
    if (handleIncludesAxis(activeAxis, TransformAxisHandle::Y)) {
        scale.y = value;
    }
    if (handleIncludesAxis(activeAxis, TransformAxisHandle::Z)) {
        scale.z = value;
    }
    return scale;
}

std::optional<ImVec2> projectWorldToViewport(
        Vec3 world,
        const ViewportCameraController& camera,
        ImVec2 itemMin,
        ImVec2 itemSize) {
    (void)itemSize;

    const ViewportScreenPoint projected = camera.projectWorldPoint(world);
    if (projected.depth <= camera.state().nearPlane || projected.depth >= camera.state().farPlane) {
        return std::nullopt;
    }

    return ImVec2{
            itemMin.x + projected.position.x,
            itemMin.y + projected.position.y,
    };
}

#if QUADER_HAS_MODELING_SDK

quader_poly::Vec3 toSdk(Vec3 value) {
    return {value.x, value.y, value.z};
}

quader_poly::Vec2 toSdk(Vec2 value) {
    return {value.x, value.y};
}

Vec3 fromSdk(quader_poly::Vec3 value) {
    return {value.x, value.y, value.z};
}

Vec2 fromSdk(quader_poly::Vec2 value) {
    return {value.x, value.y};
}

quader_editor_core::TransformAxis toSdkAxis(TransformAxisHandle axis) {
    switch (axis) {
    case TransformAxisHandle::X:
        return quader_editor_core::TransformAxis::x;
    case TransformAxisHandle::Y:
        return quader_editor_core::TransformAxis::y;
    case TransformAxisHandle::Z:
        return quader_editor_core::TransformAxis::z;
    case TransformAxisHandle::XY:
        return quader_editor_core::TransformAxis::xy;
    case TransformAxisHandle::XZ:
        return quader_editor_core::TransformAxis::xz;
    case TransformAxisHandle::YZ:
        return quader_editor_core::TransformAxis::yz;
    case TransformAxisHandle::All:
        return quader_editor_core::TransformAxis::all;
    case TransformAxisHandle::None:
        return quader_editor_core::TransformAxis::none;
    }
    return quader_editor_core::TransformAxis::none;
}

ModelingTransformAxis toModelingTransformAxis(TransformAxisHandle axis) {
    switch (axis) {
    case TransformAxisHandle::X:
        return ModelingTransformAxis::X;
    case TransformAxisHandle::Y:
        return ModelingTransformAxis::Y;
    case TransformAxisHandle::Z:
        return ModelingTransformAxis::Z;
    case TransformAxisHandle::XY:
        return ModelingTransformAxis::XY;
    case TransformAxisHandle::XZ:
        return ModelingTransformAxis::XZ;
    case TransformAxisHandle::YZ:
        return ModelingTransformAxis::YZ;
    case TransformAxisHandle::All:
        return ModelingTransformAxis::All;
    case TransformAxisHandle::None:
        return ModelingTransformAxis::None;
    }
    return ModelingTransformAxis::None;
}

TransformAxisHandle fromSdkAxis(quader_editor_core::TransformAxis axis) {
    switch (axis) {
    case quader_editor_core::TransformAxis::x:
        return TransformAxisHandle::X;
    case quader_editor_core::TransformAxis::y:
        return TransformAxisHandle::Y;
    case quader_editor_core::TransformAxis::z:
        return TransformAxisHandle::Z;
    case quader_editor_core::TransformAxis::xy:
        return TransformAxisHandle::XY;
    case quader_editor_core::TransformAxis::xz:
        return TransformAxisHandle::XZ;
    case quader_editor_core::TransformAxis::yz:
        return TransformAxisHandle::YZ;
    case quader_editor_core::TransformAxis::all:
        return TransformAxisHandle::All;
    case quader_editor_core::TransformAxis::none:
        return TransformAxisHandle::None;
    }
    return TransformAxisHandle::None;
}

std::uint8_t alphaByte(float alpha) {
    return static_cast<std::uint8_t>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f);
}

ImU32 axisImColor(quader_editor_core::TransformAxis axis, float alpha = 1.0f) {
    const std::uint8_t a = alphaByte(alpha);
    switch (axis) {
    case quader_editor_core::TransformAxis::x:
        return IM_COL32(242, 46, 41, a);
    case quader_editor_core::TransformAxis::y:
        return IM_COL32(51, 209, 66, a);
    case quader_editor_core::TransformAxis::z:
        return IM_COL32(51, 107, 255, a);
    default:
        return IM_COL32(235, 240, 245, a);
    }
}

quader_editor_core::ViewportCameraState makeSdkCamera(const ViewportCameraController& camera, ImVec2 itemSize) {
    const ViewportCameraBasis cameraBasis = ViewportCameraAdapter::basis(camera);

    quader_editor_core::ViewportCameraState sdkCamera;
    sdkCamera.projection = camera.state().projection == ViewportProjection::Orthographic
            ? quader_editor_core::CameraProjection::orthographic
            : quader_editor_core::CameraProjection::perspective;
    sdkCamera.transform.origin = toSdk(cameraBasis.eye);
    sdkCamera.transform.x_axis = toSdk(cameraBasis.right);
    sdkCamera.transform.y_axis = toSdk(cameraBasis.up);
    sdkCamera.transform.z_axis = toSdk(cameraBasis.forward * -1.0f);
    sdkCamera.viewport_size = {std::max(itemSize.x, 1.0f), std::max(itemSize.y, 1.0f)};
    sdkCamera.orthographic_size = std::max(camera.state().orthographicSize, 0.0001f);
    sdkCamera.fov_degrees = camera.state().fovDegrees;
    return sdkCamera;
}

quader_editor_core::TransformGizmoFrameInput makeSdkGizmoInput(
        const ViewportCameraController& camera,
        Vec3 pivot,
        quader_editor_core::ViewportInteractionTool tool,
        TransformAxisHandle activeAxis,
        TransformAxisHandle hoverAxis,
        double gridSize,
        float gizmoSizeScale,
        ImVec2 itemSize) {
    quader_editor_core::TransformGizmoFrameInput input;
    input.tool = tool;
    input.active_axis = toSdkAxis(activeAxis);
    input.hover_axis = toSdkAxis(hoverAxis);
    input.has_selection = true;
    input.has_pivot = true;
    input.pivot = toSdk(pivot);
    input.camera = makeSdkCamera(camera, itemSize);
    input.grid_size = gridSize;
    input.gizmo_size_pixels = 68.0;
    input.gizmo_scale = std::max(gizmoSizeScale, 0.1f);
    input.pick_radius_pixels = 12.0;
    input.ring_segments = 64;
    return input;
}

float visualWidthScale(const quader_editor_core::TransformGizmoFrame& frame) {
    return std::clamp(static_cast<float>(frame.info.gizmo_scale), 0.75f, 1.5f);
}

bool dragAxisIncludes(quader_editor_core::TransformAxis activeAxis, quader_editor_core::TransformAxis axis) {
    using quader_editor_core::TransformAxis;
    if (activeAxis == TransformAxis::none || activeAxis == axis) {
        return true;
    }
    if (activeAxis == TransformAxis::xy) {
        return axis == TransformAxis::x || axis == TransformAxis::y;
    }
    if (activeAxis == TransformAxis::xz) {
        return axis == TransformAxis::x || axis == TransformAxis::z;
    }
    if (activeAxis == TransformAxis::yz) {
        return axis == TransformAxis::y || axis == TransformAxis::z;
    }
    return false;
}

bool dragPlaneIncludes(quader_editor_core::TransformAxis activeAxis, quader_editor_core::TransformAxis planeAxis) {
    return activeAxis == quader_editor_core::TransformAxis::none || activeAxis == planeAxis;
}

ImU32 movePlaneImColor(quader_editor_core::TransformAxis axis) {
    switch (axis) {
    case quader_editor_core::TransformAxis::xy:
        return IM_COL32(255, 31, 41, 255);
    case quader_editor_core::TransformAxis::yz:
        return IM_COL32(5, 219, 46, 255);
    case quader_editor_core::TransformAxis::xz:
        return IM_COL32(0, 184, 255, 255);
    default:
        return IM_COL32(255, 255, 255, 255);
    }
}

ImVec2 toScreen(quader_poly::Vec2 point, ImVec2 itemMin) {
    return {itemMin.x + point.x, itemMin.y + point.y};
}

void drawGizmoLine(ImDrawList* drawList, ImVec2 start, ImVec2 end, ImU32 color, float width) {
    drawList->AddLine(start, end, color, std::max(width, 1.0f));
}

float signedArea(const std::array<ImVec2, 4>& points) {
    float area = 0.0f;
    for (std::size_t index = 0; index < points.size(); ++index) {
        const ImVec2 a = points[index];
        const ImVec2 b = points[(index + 1u) % points.size()];
        area += a.x * b.y - b.x * a.y;
    }
    return area * 0.5f;
}

void drawGizmoPlane(
        ImDrawList* drawList,
        const quader_editor_core::TransformGizmoPlanePrimitive& plane,
        ImVec2 itemMin,
        ImU32 color) {
    std::array<ImVec2, 4> points{{
            toScreen(plane.screen0, itemMin),
            toScreen(plane.screen1, itemMin),
            toScreen(plane.screen2, itemMin),
            toScreen(plane.screen3, itemMin),
    }};
    if (std::abs(signedArea(points)) <= 2.0f) {
        return;
    }
    drawList->AddConvexPolyFilled(points.data(), static_cast<int>(points.size()), color);
    drawList->AddPolyline(points.data(), static_cast<int>(points.size()), color, ImDrawFlags_Closed, 1.2f);
}

void drawArrowHead(ImDrawList* drawList, ImVec2 start, ImVec2 tip, ImU32 color, float size) {
    const ImVec2 delta{tip.x - start.x, tip.y - start.y};
    const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    if (length <= 0.0001f) {
        return;
    }
    const ImVec2 direction{delta.x / length, delta.y / length};
    const ImVec2 perpendicular{-direction.y, direction.x};
    const ImVec2 base{tip.x - direction.x * size, tip.y - direction.y * size};
    const float halfWidth = size * 0.68f;
    const ImVec2 p1{base.x + perpendicular.x * halfWidth, base.y + perpendicular.y * halfWidth};
    const ImVec2 p2{base.x - perpendicular.x * halfWidth, base.y - perpendicular.y * halfWidth};
    drawList->AddTriangleFilled(tip, p1, p2, color);
    const std::array<ImVec2, 3> outline = {{tip, p1, p2}};
    drawList->AddPolyline(outline.data(), static_cast<int>(outline.size()), color, ImDrawFlags_Closed, 1.1f);
}

Vec3 fallbackPerpendicular(Vec3 axis) {
    Vec3 side = cross(axis, Vec3{0.0f, 1.0f, 0.0f});
    if (dot(side, side) <= 0.000001f) {
        side = cross(axis, Vec3{1.0f, 0.0f, 0.0f});
    }
    if (dot(side, side) <= 0.000001f) {
        return {0.0f, 0.0f, 1.0f};
    }
    return normalize(side);
}

bool drawProjectedConeHandle(
        ImDrawList* drawList,
        const ViewportCameraController& camera,
        ImVec2 itemMin,
        ImVec2 itemSize,
        quader_poly::Vec3 start,
        quader_poly::Vec3 tip,
        ImU32 color,
        float sizeScale) {
    constexpr int coneSegments = 14;
    constexpr float circleTau = kPi * 2.0f;

    const Vec3 rawStart = fromSdk(start);
    const Vec3 rawTip = fromSdk(tip);
    Vec3 axis = rawTip - rawStart;
    const float axisLength = length(axis);
    if (axisLength <= 0.000001f) {
        return false;
    }
    axis = axis / axisLength;
    const float coneLength = axisLength * 0.22f * sizeScale;
    const float radius = coneLength * 0.38f;
    Vec3 u = fallbackPerpendicular(axis);
    Vec3 v = cross(axis, u);
    if (dot(v, v) <= 0.000001f) {
        v = fallbackPerpendicular(u);
    } else {
        v = normalize(v);
    }
    u = normalize(cross(v, axis));
    const Vec3 baseCenter = rawTip - axis * coneLength;

    std::array<Vec3, coneSegments> basePoints = {};
    for (int i = 0; i < coneSegments; ++i) {
        const float angle = circleTau * static_cast<float>(i) / static_cast<float>(coneSegments);
        basePoints[static_cast<std::size_t>(i)] = baseCenter +
                (u * std::cos(angle) + v * std::sin(angle)) * radius;
    }

    const std::optional<ImVec2> tipScreen = projectWorldToViewport(rawTip, camera, itemMin, itemSize);
    if (!tipScreen) {
        return false;
    }

    for (int i = 0; i < coneSegments; ++i) {
        const int next = (i + 1) % coneSegments;
        const std::optional<ImVec2> a =
                projectWorldToViewport(basePoints[static_cast<std::size_t>(i)], camera, itemMin, itemSize);
        const std::optional<ImVec2> b =
                projectWorldToViewport(basePoints[static_cast<std::size_t>(next)], camera, itemMin, itemSize);
        if (!a || !b) {
            continue;
        }
        drawList->AddTriangleFilled(*tipScreen, *a, *b, color);
        drawGizmoLine(drawList, *a, *b, color, 1.0f);
    }

    const std::optional<ImVec2> baseScreen = projectWorldToViewport(baseCenter, camera, itemMin, itemSize);
    if (baseScreen) {
        drawGizmoLine(drawList, *baseScreen, *tipScreen, color, 1.0f);
    }
    return true;
}

void drawProjectedCubeHandle(
        ImDrawList* drawList,
        const ViewportCameraController& camera,
        ImVec2 itemMin,
        ImVec2 itemSize,
        quader_poly::Vec3 center,
        float worldSize,
        ImU32 color,
        bool hovered) {
    if (worldSize <= 0.000001f) {
        return;
    }

    const Vec3 rawCenter = fromSdk(center);
    const float half = worldSize * 0.5f;
    const std::array<Vec3, 8> corners = {{
            rawCenter + Vec3{-half, -half, -half},
            rawCenter + Vec3{half, -half, -half},
            rawCenter + Vec3{half, half, -half},
            rawCenter + Vec3{-half, half, -half},
            rawCenter + Vec3{-half, -half, half},
            rawCenter + Vec3{half, -half, half},
            rawCenter + Vec3{half, half, half},
            rawCenter + Vec3{-half, half, half},
    }};
    constexpr std::array<std::array<int, 4>, 6> faces = {{
            {{1, 5, 6, 2}},
            {{4, 0, 3, 7}},
            {{3, 2, 6, 7}},
            {{4, 5, 1, 0}},
            {{5, 4, 7, 6}},
            {{0, 1, 2, 3}},
    }};
    const std::array<Vec3, 6> normals = {{
            {1.0f, 0.0f, 0.0f},
            {-1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
            {0.0f, -1.0f, 0.0f},
            {0.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, -1.0f},
    }};

    Vec3 viewDirection = camera.pose().eye - rawCenter;
    if (dot(viewDirection, viewDirection) <= 0.000001f) {
        viewDirection = {0.0f, 0.0f, 1.0f};
    } else {
        viewDirection = normalize(viewDirection);
    }

    for (std::size_t faceIndex = 0; faceIndex < faces.size(); ++faceIndex) {
        if (dot(normals[faceIndex], viewDirection) <= 0.000001f) {
            continue;
        }

        std::array<ImVec2, 4> polygon = {};
        bool ok = true;
        for (std::size_t i = 0; i < polygon.size(); ++i) {
            const std::optional<ImVec2> screen =
                    projectWorldToViewport(corners[static_cast<std::size_t>(faces[faceIndex][i])], camera, itemMin, itemSize);
            if (!screen) {
                ok = false;
                break;
            }
            polygon[i] = *screen;
        }
        if (!ok) {
            continue;
        }
        drawList->AddConvexPolyFilled(polygon.data(), static_cast<int>(polygon.size()), color);
        drawList->AddPolyline(
                polygon.data(),
                static_cast<int>(polygon.size()),
                color,
                ImDrawFlags_Closed,
                hovered ? 1.4f : 0.9f);
    }
}

void drawUnityStyleGizmo(
        const quader_editor_core::TransformGizmoFrame& frame,
        const ViewportCameraController& camera,
        ImVec2 itemMin,
        ImVec2 itemSize) {
    if (!frame.info.ok) {
        return;
    }

    constexpr float moveCenterRadius = 3.5f;
    constexpr float moveArrowSize = 14.0f;
    constexpr float scaleHandleSize = 13.0f;
    constexpr float scaleCenterSize = 14.0f;
    const ImU32 hoverColor = IM_COL32(255, 235, 20, 255);
    const ImU32 moveCenterColor = IM_COL32(255, 31, 219, 255);
    const float sizeScale = 1.0f;
    const float widthScale = visualWidthScale(frame);
    const float gizmoScale = std::max(0.1f, static_cast<float>(frame.info.gizmo_scale)) * sizeScale;
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    if (frame.info.tool == quader_editor_core::ViewportInteractionTool::rotate) {
        for (const quader_editor_core::TransformGizmoRingSegmentPrimitive& segment : frame.ring_segments) {
            const bool hovered = frame.info.hover_axis == segment.axis || frame.info.active_axis == segment.axis;
            drawGizmoLine(drawList,
                    toScreen(segment.screen_start, itemMin),
                    toScreen(segment.screen_end, itemMin),
                    hovered ? hoverColor : axisImColor(segment.axis),
                    2.0f * widthScale * sizeScale);
        }
        return;
    }

    for (const quader_editor_core::TransformGizmoPlanePrimitive& plane : frame.planes) {
        if (!dragPlaneIncludes(frame.info.active_axis, plane.axis)) {
            continue;
        }
        const bool hovered = frame.info.hover_axis == plane.axis || frame.info.active_axis == plane.axis;
        drawGizmoPlane(drawList, plane, itemMin, hovered ? hoverColor : movePlaneImColor(plane.axis));
    }

    for (const quader_editor_core::TransformGizmoAxisPrimitive& axis : frame.axes) {
        if (!dragAxisIncludes(frame.info.active_axis, axis.axis)) {
            continue;
        }
        const bool hovered = frame.info.hover_axis == axis.axis || frame.info.active_axis == axis.axis;
        const ImU32 color = hovered ? hoverColor : axisImColor(axis.axis, axis.alpha);
        const ImVec2 start = toScreen(axis.screen_start, itemMin);
        const ImVec2 tip = toScreen(axis.screen_tip, itemMin);
        drawGizmoLine(drawList, start, tip, color, 2.25f * widthScale * sizeScale);
        if (frame.info.tool == quader_editor_core::ViewportInteractionTool::scale) {
            const float worldSize = std::max(
                    0.0001f,
                    length(fromSdk(axis.world_tip) - fromSdk(axis.world_start)) *
                            (scaleHandleSize / 68.0f) * sizeScale);
            drawProjectedCubeHandle(drawList, camera, itemMin, itemSize, axis.world_tip, worldSize, color, false);
        } else {
            if (!drawProjectedConeHandle(drawList, camera, itemMin, itemSize, axis.world_start, axis.world_tip, color, sizeScale)) {
                drawArrowHead(drawList, start, tip, color, moveArrowSize * gizmoScale);
            }
        }
    }

    const ImVec2 pivot = toScreen(frame.info.screen_pivot, itemMin);
    const bool centerHovered =
            frame.info.hover_axis == quader_editor_core::TransformAxis::all ||
            frame.info.active_axis == quader_editor_core::TransformAxis::all;
    if (frame.info.tool == quader_editor_core::ViewportInteractionTool::scale) {
        if (frame.axes.empty()) {
            return;
        }
        const float worldSize = std::max(
                0.0001f,
                length(fromSdk(frame.axes[0].world_tip) - fromSdk(frame.axes[0].world_start)) *
                        (scaleCenterSize / 68.0f) * sizeScale);
        drawProjectedCubeHandle(
                drawList,
                camera,
                itemMin,
                itemSize,
                frame.info.pivot,
                worldSize,
                centerHovered ? hoverColor : moveCenterColor,
                false);
    } else {
        drawList->AddCircleFilled(pivot, moveCenterRadius * gizmoScale, moveCenterColor, 18);
    }
}

#endif

double sceneScalePixelsPerFactor() {
#if QUADER_HAS_MODELING_SDK
    return quader_editor_core::scale_pixels_per_factor;
#else
    return 96.0;
#endif
}

double snappedSceneScaleFactor(
        TransformAxisHandle activeAxis,
        double factor,
        const std::optional<SceneBounds>& bounds,
        Vec3 pivot,
        bool snapEnabled,
        double gridSize) {
#if QUADER_HAS_MODELING_SDK
    quader_editor_core::ToolBounds3 sdkBounds;
    if (bounds) {
        sdkBounds.has_bounds = true;
        sdkBounds.min = toSdk(bounds->min);
        sdkBounds.max = toSdk(bounds->max);
    }
    return quader_editor_core::snapped_scale_factor(
            toSdkAxis(activeAxis),
            factor,
            sdkBounds,
            toSdk(pivot),
            snapEnabled,
            gridSize);
#else
    (void)activeAxis;
    (void)bounds;
    (void)pivot;
    (void)snapEnabled;
    (void)gridSize;
    return factor;
#endif
}

} // namespace

bool TransformGizmoController::draw(const TransformGizmoFrameRequest& request) {
    SceneDocument& document = request.document;
    ModelingSessionService& modeling = request.modeling;
    ViewportInputRouter& inputRouter = request.inputRouter;
    ViewportCameraController& camera = request.camera;
    const ImVec2 itemMin = request.itemMin;
    const ImVec2 itemSize = request.itemSize;
    const bool interactive = request.interactive;

    const EntityId selected = document.primarySelection();
    if (selected == InvalidEntityId) {
        if (!interactive) {
            return false;
        }
#if QUADER_HAS_MODELING_SDK
        if (modelingDragActive_) {
            const ModelingCommandResult cancel = modeling.cancelTransformDrag();
            if (!cancel.ok) {
                LOG_ERROR("Transform drag cancel failed: {}", cancel.message);
            }
        }
#endif
        resetDragState();
        return false;
    }

    TransformComponent* transform = document.transform(selected);
    if (!transform) {
        if (!interactive) {
            return false;
        }
#if QUADER_HAS_MODELING_SDK
        if (modelingDragActive_) {
            const ModelingCommandResult cancel = modeling.cancelTransformDrag();
            if (!cancel.ok) {
                LOG_ERROR("Transform drag cancel failed: {}", cancel.message);
            }
        }
#endif
        resetDragState();
        return false;
    }

    const ViewportToolId tool = inputRouter.state().activeTool;
    if (!supportsViewportTool(tool)) {
        if (!interactive) {
            return false;
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
#if QUADER_HAS_MODELING_SDK
            if (modelingDragActive_) {
                const ModelingCommandResult cancel = modeling.cancelTransformDrag();
                if (!cancel.ok) {
                    LOG_ERROR("Transform drag cancel failed: {}", cancel.message);
                }
            }
#endif
            resetDragState();
        }
        return false;
    }

    const ModelingSelectionMode activeSelectionMode = modeling.selectionMode();
    if (interactive && activeSelectionMode != ModelingSelectionMode::Group) {
        modeling.syncObjectSelectionFromScene();
    }
    const bool componentSelectionMode = isComponentSelectionMode(activeSelectionMode);
    const bool groupSelectionMode = activeSelectionMode == ModelingSelectionMode::Group;
    const std::optional<SceneBounds> selectionBounds = selectedTransformWorldBounds(document, request.assets);
    const std::optional<Vec3> sdkSelectionCenter = modeling.selectionCenter();
    const Vec3 selectedWorldPosition = transformPoint(document.worldTransformMatrix(selected), Vec3{});
    const bool usePrimaryEntityPivot =
            !componentSelectionMode &&
            (document.modelingObject(selected) == nullptr || document.hasChildren(selected));
    const Vec3 pivot =
            (componentSelectionMode && sdkSelectionCenter) ? *sdkSelectionCenter :
            usePrimaryEntityPivot ? selectedWorldPosition :
            (selectionBounds ? boundsCenter(*selectionBounds) : sdkSelectionCenter.value_or(selectedWorldPosition));
    const ViewportScreenPoint projectedPivot = camera.projectWorldPoint(pivot);
    if (!projectedPivot.visible) {
        if (!interactive) {
            return false;
        }
#if QUADER_HAS_MODELING_SDK
        if (modelingDragActive_) {
            const ModelingCommandResult cancel = modeling.cancelTransformDrag();
            if (!cancel.ok) {
                LOG_ERROR("Transform drag cancel failed: {}", cancel.message);
            }
        }
#endif
        resetDragState();
        return false;
    }
    const ImVec2 origin{itemMin.x + projectedPivot.position.x, itemMin.y + projectedPivot.position.y};

    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mouse = io.MousePos;
    const bool mouseInsideViewport =
            mouse.x >= itemMin.x && mouse.y >= itemMin.y &&
            mouse.x <= itemMin.x + itemSize.x && mouse.y <= itemMin.y + itemSize.y;
    const double activeGridSize =
            std::max(static_cast<double>(viewportGridWorldSizeForPreset(inputRouter.state().gridPreset)), 0.0001);
    const double vertexSnapRadius =
            static_cast<double>(request.viewportSettings.floatValue(ViewportFloatSettingId::PickVertexRadius));
    bool selectedHasModelingObjectTransform = false;
    bool selectedHasSceneOnlyTransform = false;
    bool selectedHasHierarchyTransform = false;
    for (const EntityId entity : document.selectedEntities()) {
        if (document.transform(entity) == nullptr) {
            continue;
        }
        if (document.modelingObject(entity) != nullptr) {
            selectedHasModelingObjectTransform = true;
        } else {
            selectedHasSceneOnlyTransform = true;
        }
        selectedHasHierarchyTransform =
                selectedHasHierarchyTransform ||
                document.parent(entity) != InvalidEntityId ||
                document.hasChildren(entity);
    }
    const bool objectDuplicateRequested = !componentSelectionMode && io.KeyShift;
    const bool useModelingTransformDrag =
            selectedHasModelingObjectTransform &&
            !selectedHasSceneOnlyTransform &&
            (componentSelectionMode || !selectedHasHierarchyTransform || objectDuplicateRequested);

    const float sizeScale = request.viewportSettings.floatValue(ViewportFloatSettingId::GizmoSize);
#if QUADER_HAS_MODELING_SDK
    const quader_editor_core::ViewportInteractionTool sdkTool = sdkToolFor(tool);
    quader_editor_core::TransformGizmoFrameInput sdkInput =
            makeSdkGizmoInput(camera, pivot, sdkTool, activeAxis_, TransformAxisHandle::None, activeGridSize, sizeScale, itemSize);
    if (interactive && !dragging_ && mouseInsideViewport && !request.suppressHoverWork) {
        const Vec2 localMouse{mouse.x - itemMin.x, mouse.y - itemMin.y};
        const quader_editor_core::TransformGizmoPickHit hoverHit =
                quader_editor_core::pick_transform_gizmo(sdkInput, toSdk(localMouse));
        sdkInput.hover_axis = hoverHit.hit ? hoverHit.axis : quader_editor_core::TransformAxis::none;
    }
    const quader_editor_core::TransformGizmoFrame sdkFrame =
            quader_editor_core::build_transform_gizmo_frame(sdkInput);
    drawUnityStyleGizmo(sdkFrame, camera, itemMin, itemSize);
#endif

    const ViewportCameraPose pose = camera.pose();
    const float cameraDistance = std::max(length(pose.eye - pivot), 0.1f);
    const float worldLength = std::clamp(cameraDistance * 0.22f * sizeScale, 0.35f, 8.0f);
    const std::array<Vec3, 3> axisVectors = {{
            {1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
            {0.0f, 0.0f, 1.0f},
    }};
    const std::array<ImU32, 3> axisColors = {{
            IM_COL32(235, 76, 82, 255),
            IM_COL32(90, 205, 118, 255),
            IM_COL32(78, 142, 255, 255),
    }};
    const std::array<TransformAxisHandle, 3> axes = {{
            TransformAxisHandle::X,
            TransformAxisHandle::Y,
            TransformAxisHandle::Z,
    }};

    std::array<std::optional<ImVec2>, 3> axisEnds;
    for (std::size_t index = 0; index < axisVectors.size(); ++index) {
        axisEnds[index] = projectWorldToViewport(pivot + axisVectors[index] * worldLength, camera, itemMin, itemSize);
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
#if !QUADER_HAS_MODELING_SDK
    if (tool == ViewportToolId::Rotate) {
        const float radius = std::clamp(std::min(itemSize.x, itemSize.y) * 0.075f, 34.0f, 92.0f);
        drawList->AddCircle(origin, radius, axisColors[0], 64, 2.0f);
        drawList->AddCircle(origin, radius * 0.78f, axisColors[1], 64, 2.0f);
        drawList->AddCircle(origin, radius * 0.56f, axisColors[2], 64, 2.0f);
    } else {
        for (std::size_t index = 0; index < axisEnds.size(); ++index) {
            if (!axisEnds[index]) {
                continue;
            }
            drawList->AddLine(origin, *axisEnds[index], axisColors[index], 3.0f);
            drawList->AddCircleFilled(*axisEnds[index], 5.5f, axisColors[index]);
        }
    }
#else
    (void)drawList;
    (void)axisColors;
#endif

    if (!interactive) {
        return false;
    }

    auto fallbackHit = [&]() {
        if (tool == ViewportToolId::Rotate) {
            const float radius = std::sqrt(distanceSquared(mouse, origin));
            const float baseRadius = std::clamp(std::min(itemSize.x, itemSize.y) * 0.075f, 34.0f, 92.0f);
            if (std::abs(radius - baseRadius) < 8.0f) {
                return TransformAxisHandle::X;
            }
            if (std::abs(radius - baseRadius * 0.78f) < 8.0f) {
                return TransformAxisHandle::Y;
            }
            if (std::abs(radius - baseRadius * 0.56f) < 8.0f) {
                return TransformAxisHandle::Z;
            }
            return TransformAxisHandle::None;
        }

        for (std::size_t index = 0; index < axisEnds.size(); ++index) {
            if (axisEnds[index] && distanceToSegmentSquared(mouse, origin, *axisEnds[index]) < 144.0f) {
                return axes[index];
            }
        }
        return TransformAxisHandle::None;
    };

    bool consumed = dragging_;
    if (!dragging_ && mouseInsideViewport && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
#if QUADER_HAS_MODELING_SDK
        quader_editor_core::TransformGizmoPickHit sdkHit;
        const Vec2 localMouse{mouse.x - itemMin.x, mouse.y - itemMin.y};
        sdkHit = quader_editor_core::pick_transform_gizmo(sdkInput, toSdk(localMouse));
        activeAxis_ = sdkHit.hit ? fromSdkAxis(sdkHit.axis) : TransformAxisHandle::None;
        if (activeAxis_ == TransformAxisHandle::None && !useModelingTransformDrag) {
            activeAxis_ = fallbackHit();
        }
#else
        activeAxis_ = fallbackHit();
#endif

        if (activeAxis_ != TransformAxisHandle::None) {
            bool hasEditableTransform = false;
            bool hasLockedTransform = false;
            for (const EntityId entity : document.selectedEntities()) {
                if (document.transform(entity) == nullptr) {
                    continue;
                }
                if (document.isEffectivelyLocked(entity)) {
                    hasLockedTransform = true;
                } else {
                    hasEditableTransform = true;
                }
            }
            if (!hasEditableTransform || hasLockedTransform) {
                activeAxis_ = TransformAxisHandle::None;
                return consumed;
            }

            bool dragReady = true;
#if QUADER_HAS_MODELING_SDK
            if (useModelingTransformDrag) {
                dragReady = false;
                sceneOnlyRotateState_.reset();

                const quader_poly::Vec2 sdkScreenPivot = toSdk(projectedPivot.position);
                const quader_poly::Vec2 sdkScreenTip = sdkHit.hit ? sdkHit.screen_tip : sdkScreenPivot;
                const ViewportRay startRay = camera.rayFromViewportPoint(localMouse);
                const bool duplicateSelected = !componentSelectionMode && io.KeyShift;
                const bool topologyModifier = componentSelectionMode && io.KeyShift;
                const ModelingTransformOperation operation =
                        topologyModifier && tool == ViewportToolId::Move ? ModelingTransformOperation::Extrude :
                        topologyModifier && tool == ViewportToolId::Scale ? ModelingTransformOperation::Inset :
                        ModelingTransformOperation::Transform;
                const ModelingTransformDragPreview beginPreview = modeling.beginTransformDrag({
                        .tool = viewportToolTransformTool(tool),
                        .operation = operation,
                        .axis = toModelingTransformAxis(activeAxis_),
                        .pivot = pivot,
                        .screenPosition = localMouse,
                        .screenPivot = projectedPivot.position,
                        .screenTip = fromSdk(sdkScreenTip),
                        .hasWorldDirection = sdkHit.hit,
                        .worldDirection = fromSdk(sdkHit.world_direction),
                        .ray = {
                                .origin = startRay.origin,
                                .direction = startRay.direction,
                        },
                        .camera = ViewportCameraAdapter::modelingCamera(camera, {itemSize.x, itemSize.y}),
                        .gridSize = activeGridSize,
                        .vertexSnapRadius = vertexSnapRadius,
                        .duplicateSelected = duplicateSelected,
                });
                if (!beginPreview.ok) {
                    LOG_ERROR("Transform drag begin failed: {}", beginPreview.message);
                }
                dragReady = beginPreview.ok && beginPreview.active;
                modelingDragActive_ = dragReady;
                modelingDragPreviewDriven_ = beginPreview.previewDriven;
                sceneOnlyDrag_ = false;
            } else {
                modelingDragActive_ = false;
                modelingDragPreviewDriven_ = false;
                sceneOnlyDrag_ = true;
                sceneOnlyRotateState_.reset();
                if (tool == ViewportToolId::Rotate) {
                    const quader_editor_core::ViewportCameraState sdkCamera = makeSdkCamera(camera, itemSize);
                    const quader_editor_core::RotateBeginResult rotateBegin =
                            quader_editor_core::begin_rotate_drag({
                                    .axis = toSdkAxis(activeAxis_),
                                    .pivot = toSdk(pivot),
                                    .screen_position = toSdk(localMouse),
                                    .has_screen_pivot = true,
                                    .screen_pivot = toSdk(projectedPivot.position),
                                    .has_world_direction = true,
                                    .world_direction = toSdk(worldAxisForHandle(activeAxis_)),
                                    .has_world_pivot = true,
                                    .world_pivot = toSdk(pivot),
                                    .has_camera = true,
                                    .camera_transform = sdkCamera.transform,
                            });
                    if (rotateBegin.ok) {
                        sceneOnlyRotateState_ = rotateBegin.state;
                    } else {
                        LOG_ERROR("Scene transform rotate begin failed: {}", rotateBegin.message);
                    }
                }
            }
#endif

            if (!dragReady) {
                modelingDragActive_ = false;
                modelingDragPreviewDriven_ = false;
                sceneOnlyDrag_ = false;
                activeAxis_ = TransformAxisHandle::None;
                return consumed;
            }
            if (sceneOnlyDrag_ && groupSelectionMode && io.KeyShift) {
                const ModelingCommandResult duplicate =
                        modeling.duplicateSelectedHierarchyForGroupDrag(request.documentCommands);
                if (!duplicate.ok) {
                    LOG_ERROR("Group shift-drag duplicate failed: {}", duplicate.message);
                }
            }
            dragging_ = true;
            dragPaneActive_ = true;
            dragPaneId_ = request.paneId;
            lastMouse_ = {mouse.x, mouse.y};
            startMouse_ = lastMouse_;
            startWorldPivot_ = pivot;
            startTransforms_.clear();
            for (const EntityId entity : document.selectedEntities()) {
                if (document.isEffectivelyLocked(entity)) {
                    continue;
                }
                if (hasSelectedAncestor(document, entity)) {
                    continue;
                }
                if (const TransformComponent* selectedTransform = document.transform(entity)) {
                    startTransforms_.push_back(StartTransform{
                            .entity = entity,
                            .transform = *selectedTransform,
                            .localPivot = documentLocalPivotForWorldPoint(document, entity, pivot),
                            .worldTransform = document.worldTransformMatrix(entity),
                    });
                }
            }
            if (startTransforms_.empty()) {
#if QUADER_HAS_MODELING_SDK
                if (modelingDragActive_) {
                    const ModelingCommandResult cancel = modeling.cancelTransformDrag();
                    if (!cancel.ok) {
                        LOG_ERROR("Transform drag cancel failed: {}", cancel.message);
                    }
                }
#endif
                resetDragState();
                return consumed;
            }
#if QUADER_HAS_MODELING_SDK
            const bool needsSceneTransformPreview = sceneOnlyDrag_ || !modelingDragPreviewDriven_;
#else
            const bool needsSceneTransformPreview = true;
#endif
            if (needsSceneTransformPreview) {
                std::vector<EntityId> previewEntities;
                previewEntities.reserve(startTransforms_.size());
                for (const StartTransform& start : startTransforms_) {
                    previewEntities.push_back(start.entity);
                }
                sceneTransformPreview_.emplace(document, std::move(previewEntities), "Transform Selection");
            }
            consumed = true;
        }
    }

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const bool wasDragging = dragging_;
        bool modelingPreviewHandled = false;
#if QUADER_HAS_MODELING_SDK
        if (wasDragging && !sceneOnlyDrag_) {
            (void)request.undoRedo;
            const ModelingTransformDragPreview committedPreview = modeling.commitTransformDrag();
            if (!committedPreview.ok) {
                LOG_ERROR("Transform drag commit failed: {}", committedPreview.message);
            }
            modelingPreviewHandled = committedPreview.previewDriven || modelingDragPreviewDriven_;
            modelingDragActive_ = false;
            modelingDragPreviewDriven_ = false;
        }
#endif
        if (wasDragging && !modelingPreviewHandled && sceneTransformPreview_) {
            const DocumentCommandResult recorded = sceneTransformPreview_->commit(request.undoRedo);
            if (!recorded.ok) {
                LOG_ERROR("Transform command record failed: {}", recorded.message);
            }
        }
        resetDragState();
        return consumed || wasDragging;
    }

    if (!dragging_ || activeAxis_ == TransformAxisHandle::None) {
        return consumed;
    }

    const Vec2 currentMouse{mouse.x, mouse.y};
    auto pinStartPivot = [&](TransformComponent& next, const StartTransform& start) {
        pinDocumentLocalPivotToWorldPoint(document, start.entity, next, start.localPivot, startWorldPivot_);
    };
    auto previewDocumentTransform = [&](const StartTransform& start, const TransformComponent& next) {
        if (!sceneTransformPreview_) {
            return;
        }
        const DocumentCommandResult preview = sceneTransformPreview_->previewTransform(start.entity, next);
        if (!preview.ok) {
            LOG_ERROR("Transform preview update failed: {}", preview.message);
        }
    };
    auto rotationEulerReference = [&](const StartTransform& start) {
        if (const TransformComponent* current = document.transform(start.entity)) {
            return positiveEulerRadians(current->rotationEuler);
        }
        return positiveEulerRadians(start.transform.rotationEuler);
    };

#if QUADER_HAS_MODELING_SDK
    if (!sceneOnlyDrag_) {
        const Vec2 localMouse{mouse.x - itemMin.x, mouse.y - itemMin.y};
        const ViewportRay dragRay = camera.rayFromViewportPoint(localMouse);
        const bool precision = io.KeyCtrl || io.KeySuper;
        const ModelingTransformDragPreview preview = modeling.updateTransformDrag({
                .screenPosition = localMouse,
                .ray = {
                        .origin = dragRay.origin,
                        .direction = dragRay.direction,
                },
                .camera = ViewportCameraAdapter::modelingCamera(camera, {itemSize.x, itemSize.y}),
                .gridSize = activeGridSize,
                .vertexSnapRadius = vertexSnapRadius,
                .precision = precision,
        });
        if (!preview.ok) {
            LOG_ERROR("Transform drag update failed: {}", preview.message);
            return consumed;
        }
        modelingDragActive_ = preview.active;
        if (!preview.active) {
            return consumed;
        }

        if (preview.previewDriven) {
            modelingDragPreviewDriven_ = true;
            return true;
        }

        Vec3 previewMoveDelta = preview.moveDelta;
        const float angleRadians = static_cast<float>(preview.rotateAngleDegrees * kPi / 180.0);
        Vec3 previewScaleFactor = preview.scale;

        for (const StartTransform& start : startTransforms_) {
            TransformComponent next = start.transform;
            if (tool == ViewportToolId::Move) {
                next.position = start.transform.position + parentLocalVectorForWorldVector(document, start.entity, previewMoveDelta);
            } else if (tool == ViewportToolId::Rotate) {
                next = localTransformAfterWorldQuaternionRotation(
                        document,
                        start.entity,
                        start.transform,
                        start.worldTransform,
                        worldAxisForHandle(activeAxis_),
                        angleRadians,
                        startWorldPivot_,
                        rotationEulerReference(start));
            } else if (tool == ViewportToolId::Scale) {
                next.scale.x = std::max(start.transform.scale.x * previewScaleFactor.x, 0.001f);
                next.scale.y = std::max(start.transform.scale.y * previewScaleFactor.y, 0.001f);
                next.scale.z = std::max(start.transform.scale.z * previewScaleFactor.z, 0.001f);
                pinStartPivot(next, start);
            }
            previewDocumentTransform(start, next);
        }
    } else
#endif
    {
        const Vec2 delta = currentMouse - startMouse_;
        const bool precision = io.KeyCtrl || io.KeySuper;
        const bool snapEnabled = !precision;
        lastMouse_ = currentMouse;

        const float worldPerPixel =
                2.0f * cameraDistance * std::tan(camera.state().fovDegrees * kPi / 180.0f * 0.5f) /
                std::max(itemSize.y, 1.0f);

        auto screenAxisDeltaAt = [&](std::size_t index) {
            if (!axisEnds[index]) {
                return (std::abs(delta.x) > std::abs(delta.y)) ? delta.x : -delta.y;
            }
            const ImVec2 screenAxis{
                    axisEnds[index]->x - origin.x,
                    axisEnds[index]->y - origin.y,
            };
            const float len = std::sqrt(screenAxis.x * screenAxis.x + screenAxis.y * screenAxis.y);
            if (len <= 0.0001f) {
                return (std::abs(delta.x) > std::abs(delta.y)) ? delta.x : -delta.y;
            }
            return (delta.x * screenAxis.x + delta.y * screenAxis.y) / len;
        };

        auto screenAxisDelta = [&](TransformAxisHandle axis) {
            switch (axis) {
            case TransformAxisHandle::X:
                return screenAxisDeltaAt(0u);
            case TransformAxisHandle::Y:
                return screenAxisDeltaAt(1u);
            case TransformAxisHandle::Z:
                return screenAxisDeltaAt(2u);
            case TransformAxisHandle::XY:
                return (screenAxisDeltaAt(0u) + screenAxisDeltaAt(1u)) * 0.5f;
            case TransformAxisHandle::XZ:
                return (screenAxisDeltaAt(0u) + screenAxisDeltaAt(2u)) * 0.5f;
            case TransformAxisHandle::YZ:
                return (screenAxisDeltaAt(1u) + screenAxisDeltaAt(2u)) * 0.5f;
            case TransformAxisHandle::All:
                return (screenAxisDeltaAt(0u) + screenAxisDeltaAt(1u) + screenAxisDeltaAt(2u)) / 3.0f;
            case TransformAxisHandle::None:
                return 0.0f;
            }
            return 0.0f;
        };

        auto applyAxis = [&](float& x, float& y, float& z, float value) {
            if (activeAxis_ == TransformAxisHandle::X ||
                    activeAxis_ == TransformAxisHandle::XY ||
                    activeAxis_ == TransformAxisHandle::XZ ||
                    activeAxis_ == TransformAxisHandle::All) {
                x += value;
            }
            if (activeAxis_ == TransformAxisHandle::Y ||
                    activeAxis_ == TransformAxisHandle::XY ||
                    activeAxis_ == TransformAxisHandle::YZ ||
                    activeAxis_ == TransformAxisHandle::All) {
                y += value;
            }
            if (activeAxis_ == TransformAxisHandle::Z ||
                    activeAxis_ == TransformAxisHandle::XZ ||
                    activeAxis_ == TransformAxisHandle::YZ ||
                    activeAxis_ == TransformAxisHandle::All) {
                z += value;
            }
        };

        for (const StartTransform& start : startTransforms_) {
            TransformComponent next = start.transform;
            if (tool == ViewportToolId::Move) {
                Vec3 worldMoveDelta = {};
                applyAxis(worldMoveDelta.x, worldMoveDelta.y, worldMoveDelta.z, screenAxisDelta(activeAxis_) * worldPerPixel);
                worldMoveDelta = snapWorldDeltaForHandle(worldMoveDelta, activeAxis_, startWorldPivot_, activeGridSize, snapEnabled);
                next.position = start.transform.position + parentLocalVectorForWorldVector(document, start.entity, worldMoveDelta);
            } else if (tool == ViewportToolId::Rotate) {
                float angleRadians = 0.0f;
#if QUADER_HAS_MODELING_SDK
                if (sceneOnlyRotateState_) {
                    const Vec2 localMouse{mouse.x - itemMin.x, mouse.y - itemMin.y};
                    const quader_editor_core::RotatePreviewResult rotatePreview =
                            quader_editor_core::preview_rotate_from_screen_position(
                                    *sceneOnlyRotateState_,
                                    toSdk(localMouse),
                                    snapEnabled ? 15.0 : 0.0);
                    if (rotatePreview.ok) {
                        angleRadians = static_cast<float>(rotatePreview.preview_angle_degrees * kPi / 180.0);
                    } else {
                        LOG_ERROR("Scene transform rotate preview failed: {}", rotatePreview.message);
                    }
                } else
#endif
                {
                    const float screenDelta = (std::abs(delta.x) > std::abs(delta.y)) ? delta.x : -delta.y;
                    angleRadians = snapRotationRadians(screenDelta * 0.01f, snapEnabled);
                }
                next = localTransformAfterWorldQuaternionRotation(
                        document,
                        start.entity,
                        start.transform,
                        start.worldTransform,
                        worldAxisForHandle(activeAxis_),
                        angleRadians,
                        startWorldPivot_,
                        rotationEulerReference(start));
            } else if (tool == ViewportToolId::Scale) {
                const double rawFactor = std::max(
                        0.01,
                        1.0 + static_cast<double>(screenAxisDelta(activeAxis_)) / sceneScalePixelsPerFactor());
                const double factor = snappedSceneScaleFactor(
                        activeAxis_,
                        rawFactor,
                        selectionBounds,
                        startWorldPivot_,
                        snapEnabled,
                        activeGridSize);
                const Vec3 scaleFactor = axisScaleForHandle(activeAxis_, factor);
                next.scale.x = start.transform.scale.x * scaleFactor.x;
                next.scale.y = start.transform.scale.y * scaleFactor.y;
                next.scale.z = start.transform.scale.z * scaleFactor.z;
                next.scale.x = std::max(next.scale.x, 0.001f);
                next.scale.y = std::max(next.scale.y, 0.001f);
                next.scale.z = std::max(next.scale.z, 0.001f);
                pinStartPivot(next, start);
            }
            previewDocumentTransform(start, next);
        }
    }

    return true;
}

bool TransformGizmoController::cancelActiveDrag(SceneDocument& document, ModelingSessionService& modeling) {
    if (!dragging_) {
        return false;
    }

#if QUADER_HAS_MODELING_SDK
    const bool previewDrivenCancel = modelingDragPreviewDriven_;
    if (modelingDragActive_) {
        const ModelingCommandResult cancel = modeling.cancelTransformDrag();
        if (!cancel.ok) {
            LOG_ERROR("Transform drag cancel failed: {}", cancel.message);
        }
    }
    if (!previewDrivenCancel)
#endif
    {
        if (sceneTransformPreview_) {
            const DocumentCommandResult cancel = sceneTransformPreview_->cancel();
            if (!cancel.ok) {
                LOG_ERROR("Transform preview cancel failed: {}", cancel.message);
            }
        }
    }

    resetDragState();
    return true;
}

bool TransformGizmoController::supportsViewportTool(ViewportToolId tool) const {
    return tool == ViewportToolId::Move || tool == ViewportToolId::Rotate || tool == ViewportToolId::Scale;
}

bool TransformGizmoController::suppressesHover(bool navigationGizmoConsumed, bool transformGizmoConsumed) const {
    return navigationGizmoConsumed || transformGizmoConsumed;
}

#if QUADER_HAS_MODELING_SDK
quader_editor_core::ViewportInteractionTool TransformGizmoController::sdkToolFor(ViewportToolId tool) const {
    switch (tool) {
    case ViewportToolId::Move:
        return quader_editor_core::ViewportInteractionTool::move;
    case ViewportToolId::Extend:
        return quader_editor_core::ViewportInteractionTool::extend;
    case ViewportToolId::Extrude:
        return quader_editor_core::ViewportInteractionTool::extrude;
    case ViewportToolId::Rotate:
        return quader_editor_core::ViewportInteractionTool::rotate;
    case ViewportToolId::Scale:
        return quader_editor_core::ViewportInteractionTool::scale;
    case ViewportToolId::Select:
    case ViewportToolId::Box:
    case ViewportToolId::InsertEdgeLoop:
    case ViewportToolId::Knife:
    case ViewportToolId::Cut:
    case ViewportToolId::Poly:
    case ViewportToolId::Mirror:
        return quader_editor_core::ViewportInteractionTool::select;
    }
    return quader_editor_core::ViewportInteractionTool::select;
}
#endif

void TransformGizmoController::resetDragState() {
    dragging_ = false;
    dragPaneActive_ = false;
    modelingDragActive_ = false;
    modelingDragPreviewDriven_ = false;
    sceneOnlyDrag_ = false;
#if QUADER_HAS_MODELING_SDK
    sceneOnlyRotateState_.reset();
#endif
    sceneTransformPreview_.reset();
    activeAxis_ = TransformAxisHandle::None;
    startTransforms_.clear();
}

} // namespace quader
