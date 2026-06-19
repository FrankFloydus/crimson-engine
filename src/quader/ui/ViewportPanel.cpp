#include "quader/ui/ViewportPanel.h"

#include "quader/core/Logger.h"
#include "quader/render/ViewportOverlayPolicy.h"
#include "quader/scene/SceneBounds.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#if QUADER_HAS_MODELING_SDK
#include <quader_editor_core/tool/tool_transform_math.h>
#include <quader_editor_core/tool/transform_gizmo.h>
#include <quader_editor_core/tool/viewport_navigation.h>
#endif

namespace quader {
namespace {

constexpr float kPi = 3.14159265358979323846f;

struct BoxPlacement {
    ModelingBounds localBounds;
    TransformComponent transform;
};

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

void addPressedKey(ViewportInputFrame& input, ImGuiKey imguiKey, ViewportKey viewportKey) {
    if (ImGui::IsKeyPressed(imguiKey, false)) {
        input.pressedKeys.push_back(viewportKey);
    }
}

void addPressedKeyPair(ViewportInputFrame& input, ImGuiKey primaryKey, ImGuiKey keypadKey, ViewportKey viewportKey) {
    if (ImGui::IsKeyPressed(primaryKey, false) || ImGui::IsKeyPressed(keypadKey, false)) {
        input.pressedKeys.push_back(viewportKey);
    }
}

ViewportInputFrame makeViewportInputFrame(bool hovered, bool focused, ImVec2 itemMin, ImVec2 itemSize) {
    ImGuiIO& io = ImGui::GetIO();
    ViewportInputFrame input;
    input.viewportHovered = hovered;
    input.viewportFocused = focused;
    input.textInputActive = io.WantTextInput;
    input.middlePressed = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle);
    input.middleDown = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
    input.middleReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Middle);
    input.rightPressed = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    input.rightDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    input.rightReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Right);
    input.mouseWheel = hovered ? io.MouseWheel : 0.0f;
    input.deltaSeconds = std::max(io.DeltaTime, 0.0f);
    input.mouseScreen = {io.MousePos.x, io.MousePos.y};
    input.mouseLocal = {io.MousePos.x - itemMin.x, io.MousePos.y - itemMin.y};
    input.viewportSize = {itemSize.x, itemSize.y};
    input.modifiers = {
            .primary = io.KeyCtrl || io.KeySuper,
            .shift = io.KeyShift,
            .alt = io.KeyAlt,
    };
    input.flyFast = io.KeyShift;
    input.flySlow = io.KeyCtrl || io.KeySuper;
    if (ImGui::IsKeyDown(ImGuiKey_W)) {
        input.flyLocalDirection.z += 1.0f;
    }
    if (ImGui::IsKeyDown(ImGuiKey_S)) {
        input.flyLocalDirection.z -= 1.0f;
    }
    if (ImGui::IsKeyDown(ImGuiKey_D)) {
        input.flyLocalDirection.x += 1.0f;
    }
    if (ImGui::IsKeyDown(ImGuiKey_A)) {
        input.flyLocalDirection.x -= 1.0f;
    }

    addPressedKey(input, ImGuiKey_Q, ViewportKey::Q);
    addPressedKey(input, ImGuiKey_W, ViewportKey::W);
    addPressedKey(input, ImGuiKey_R, ViewportKey::R);
    addPressedKey(input, ImGuiKey_S, ViewportKey::S);
    addPressedKey(input, ImGuiKey_B, ViewportKey::B);
    addPressedKey(input, ImGuiKey_C, ViewportKey::C);
    addPressedKey(input, ImGuiKey_P, ViewportKey::P);
    addPressedKeyPair(input, ImGuiKey_1, ImGuiKey_Keypad1, ViewportKey::Number1);
    addPressedKeyPair(input, ImGuiKey_2, ImGuiKey_Keypad2, ViewportKey::Number2);
    addPressedKeyPair(input, ImGuiKey_3, ImGuiKey_Keypad3, ViewportKey::Number3);
    addPressedKeyPair(input, ImGuiKey_4, ImGuiKey_Keypad4, ViewportKey::Number4);
    addPressedKey(input, ImGuiKey_Home, ViewportKey::Home);
    addPressedKey(input, ImGuiKey_F, ViewportKey::F);
    addPressedKey(input, ImGuiKey_Space, ViewportKey::Space);
    addPressedKey(input, ImGuiKey_F1, ViewportKey::F1);
    addPressedKey(input, ImGuiKey_F2, ViewportKey::F2);
    addPressedKey(input, ImGuiKey_F3, ViewportKey::F3);
    addPressedKey(input, ImGuiKey_F4, ViewportKey::F4);
    addPressedKey(input, ImGuiKey_F5, ViewportKey::F5);
    addPressedKey(input, ImGuiKey_F6, ViewportKey::F6);
    addPressedKey(input, ImGuiKey_F7, ViewportKey::F7);
    addPressedKey(input, ImGuiKey_F8, ViewportKey::F8);
    addPressedKey(input, ImGuiKey_F9, ViewportKey::F9);
    addPressedKey(input, ImGuiKey_F10, ViewportKey::F10);
    addPressedKey(input, ImGuiKey_Escape, ViewportKey::Escape);

    return input;
}

float gridSizeForPreset(int preset) {
    return viewportGridWorldSizeForPreset(preset);
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

void drawWorldLine(ImDrawList* drawList,
        Vec3 a,
        Vec3 b,
        const ViewportCameraController& camera,
        ImVec2 itemMin,
        ImVec2 itemSize,
        ImU32 color,
        float thickness) {
    const std::optional<ImVec2> screenA = projectWorldToViewport(a, camera, itemMin, itemSize);
    const std::optional<ImVec2> screenB = projectWorldToViewport(b, camera, itemMin, itemSize);
    if (screenA && screenB) {
        drawList->AddLine(*screenA, *screenB, color, thickness);
    }
}

ModelingBoxToolEvent modelingBoxEvent(
        const ViewportCameraController& camera,
        const ViewportInputFrame& input,
        float gridSize) {
    const ViewportRay ray = camera.rayFromViewportPoint(input.mouseLocal);
    return {
            .screenPosition = input.mouseLocal,
            .ray = {
                    .origin = ray.origin,
                    .direction = ray.direction,
            },
            .gridSize = gridSize,
    };
}

bool hasActiveBoxPreview(const ModelingBoxPreview& preview) {
    return preview.valid || preview.dragState != ModelingBoxDragState::Idle;
}

void logBoxPreviewFailure(const char* action, const ModelingBoxPreview& preview) {
    if (!preview.ok) {
        LOG_ERROR("{} failed: {}", action, preview.message.empty() ? "Box preview command failed." : preview.message);
    }
}

void drawBoxPreview(ImDrawList* drawList,
        const ModelingBoxPreview& preview,
        const ViewportCameraController& camera,
        ImVec2 itemMin,
        ImVec2 itemSize) {
    if (!preview.valid) {
        return;
    }

    const ImU32 color = IM_COL32(84, 180, 255, 230);
    const ImU32 baseColor = IM_COL32(84, 180, 255, 110);
    if (preview.cornerCount >= 8U) {
        constexpr std::array<std::array<int, 2>, 12> edges = {{
                {{0, 1}}, {{1, 2}}, {{2, 3}}, {{3, 0}},
                {{4, 5}}, {{5, 6}}, {{6, 7}}, {{7, 4}},
                {{0, 4}}, {{1, 5}}, {{2, 6}}, {{3, 7}},
        }};

        for (const auto& edge : edges) {
            drawWorldLine(drawList, preview.corners[edge[0]], preview.corners[edge[1]], camera, itemMin, itemSize, color, 2.0f);
        }
        return;
    }

    if (preview.footprintCount >= 4U) {
        for (std::size_t index = 0; index < preview.footprintCount; ++index) {
            const Vec3 a = preview.footprint[index];
            const Vec3 b = preview.footprint[(index + 1U) % preview.footprintCount];
            drawWorldLine(drawList, a, b, camera, itemMin, itemSize, color, 2.0f);
        }
        drawWorldLine(drawList, preview.footprint[0], preview.footprint[2], camera, itemMin, itemSize, baseColor, 1.0f);
        drawWorldLine(drawList, preview.footprint[1], preview.footprint[3], camera, itemMin, itemSize, baseColor, 1.0f);
    }
}

void focusDocument(SceneDocument& document, const AssetRegistry& assets, ViewportCameraController& camera) {
    std::optional<SceneBounds> bounds = selectedWorldBounds(document, assets);
    if (!bounds) {
        bounds = documentWorldBounds(document, assets);
    }
    if (bounds) {
        camera.focusBounds(bounds->min, bounds->max);
    } else {
        camera.focusOrigin();
    }
}

bool rayIntersectsBounds(Vec3 origin, Vec3 direction, const SceneBounds& bounds, float& outDistance) {
    float tMin = 0.0f;
    float tMax = std::numeric_limits<float>::max();

    auto intersectAxis = [&](float originAxis, float directionAxis, float minAxis, float maxAxis) {
        if (std::abs(directionAxis) <= 0.000001f) {
            return originAxis >= minAxis && originAxis <= maxAxis;
        }

        float t1 = (minAxis - originAxis) / directionAxis;
        float t2 = (maxAxis - originAxis) / directionAxis;
        if (t1 > t2) {
            std::swap(t1, t2);
        }
        tMin = std::max(tMin, t1);
        tMax = std::min(tMax, t2);
        return tMin <= tMax;
    };

    if (!intersectAxis(origin.x, direction.x, bounds.min.x, bounds.max.x)) {
        return false;
    }
    if (!intersectAxis(origin.y, direction.y, bounds.min.y, bounds.max.y)) {
        return false;
    }
    if (!intersectAxis(origin.z, direction.z, bounds.min.z, bounds.max.z)) {
        return false;
    }

    outDistance = tMin;
    return tMax >= 0.0f;
}

bool rayIntersectsTriangle(Vec3 origin, Vec3 direction, Vec3 a, Vec3 b, Vec3 c, float& outDistance) {
    constexpr float epsilon = 0.000001f;
    const Vec3 edgeA = b - a;
    const Vec3 edgeB = c - a;
    const Vec3 h = cross(direction, edgeB);
    const float determinant = dot(edgeA, h);
    if (std::abs(determinant) < epsilon) {
        return false;
    }

    const float inverseDeterminant = 1.0f / determinant;
    const Vec3 s = origin - a;
    const float u = inverseDeterminant * dot(s, h);
    if (u < 0.0f || u > 1.0f) {
        return false;
    }

    const Vec3 q = cross(s, edgeA);
    const float v = inverseDeterminant * dot(direction, q);
    if (v < 0.0f || u + v > 1.0f) {
        return false;
    }

    const float distance = inverseDeterminant * dot(edgeB, q);
    if (distance <= epsilon) {
        return false;
    }

    outDistance = distance;
    return true;
}

bool pointVisibleFromCamera(
        Vec3 point,
        const ViewportCameraController& camera,
        const SceneDocument& document,
        const AssetRegistry& assets) {
    const ViewportCameraPose pose = camera.pose();
    const Vec3 toPoint = point - pose.eye;
    const float pointDistance = length(toPoint);
    if (pointDistance <= 0.0001f) {
        return true;
    }

    const Vec3 rayDirection = toPoint / pointDistance;
    float nearestHit = std::numeric_limits<float>::max();
    for (const EntityId entity : document.entities()) {
        const VisibilityComponent* visibility = document.visibility(entity);
        if (visibility && !visibility->visible) {
            continue;
        }

        const MeshComponent* meshComponent = document.mesh(entity);
        const TransformComponent* transform = document.transform(entity);
        if (!meshComponent || !transform) {
            continue;
        }

        const MeshAsset* mesh = assets.mesh(meshComponent->mesh);
        if (!mesh || mesh->vertices.empty() || mesh->indices.size() < 3u) {
            continue;
        }

        const Mat4 worldFromLocal = composeTransform(transform->spatialTransform());
        for (std::size_t index = 0; index + 2u < mesh->indices.size(); index += 3u) {
            const std::uint32_t ia = mesh->indices[index];
            const std::uint32_t ib = mesh->indices[index + 1u];
            const std::uint32_t ic = mesh->indices[index + 2u];
            if (ia >= mesh->vertices.size() || ib >= mesh->vertices.size() || ic >= mesh->vertices.size()) {
                continue;
            }

            const Vec3 a = transformPoint(worldFromLocal, mesh->vertices[ia].position);
            const Vec3 b = transformPoint(worldFromLocal, mesh->vertices[ib].position);
            const Vec3 c = transformPoint(worldFromLocal, mesh->vertices[ic].position);

            float hitDistance = 0.0f;
            if (rayIntersectsTriangle(pose.eye, rayDirection, a, b, c, hitDistance)) {
                nearestHit = std::min(nearestHit, hitDistance);
            }
        }
    }

    return nearestHit >= pointDistance - 0.035f;
}

bool commitBoxPlacement(ModelingSessionService& modeling,
        AssetId defaultMaterial,
        SceneDocument& document,
        const BoxPlacement& placement,
        const std::string& name) {
    const ModelingCreateBoxResult result = modeling.createBoxFromBounds({
            .bounds = placement.localBounds,
            .name = name,
    });

    if (!result.ok) {
        LOG_ERROR("Box creation failed: {}", result.message);
        return false;
    }

    document.setTransform(result.object.entity, placement.transform);
    if (defaultMaterial != InvalidAssetId) {
        document.setMaterial(result.object.entity, MaterialComponent{.material = defaultMaterial});
    }
    document.clearSelection();
    document.setSelected(result.object.entity, true);
    return true;
}

bool commitBoxPreview(ModelingSessionService& modeling,
        AssetId defaultMaterial,
        SceneDocument& document,
        const std::string& name) {
    const ModelingCreateBoxResult result = modeling.commitBox(name);
    if (!result.ok) {
        LOG_ERROR("Box commit failed: {}", result.message);
        return false;
    }

    if (defaultMaterial != InvalidAssetId) {
        document.setMaterial(result.object.entity, MaterialComponent{.material = defaultMaterial});
    }
    document.clearSelection();
    document.setSelected(result.object.entity, true);
    return true;
}

ImU32 toImColor(std::uint32_t rgba) {
    return static_cast<ImU32>(rgba);
}

void drawOverlayPoint(ImDrawList* drawList,
        ImVec2 center,
        ViewportOverlayLayer layer,
        float nativeSizePixels,
        ImU32 color) {
    const float fillSize = viewportOverlayPointSizePixels(layer, nativeSizePixels);
    const float half = fillSize * 0.5f;

    if (viewportOverlayPointIsOutlineOnly(layer)) {
        drawList->AddCircle(center, half, color, 24, std::max(1.0f, half * (1.0f - viewportOverlayPointInnerRadius(layer))));
        return;
    }

    if (viewportOverlayPointHasOutline(layer)) {
        const float outlineHalf = half + viewportOverlayPointOutlineGrowthPixels(layer) * 0.5f;
        drawList->AddRectFilled(
                {center.x - outlineHalf, center.y - outlineHalf},
                {center.x + outlineHalf, center.y + outlineHalf},
                toImColor(kOverlayVertexOutlineColor));
    }
    drawList->AddRectFilled(
            {center.x - half, center.y - half},
            {center.x + half, center.y + half},
            color);
}

void drawVisibilityClippedOverlayLine(
        ImDrawList* drawList,
        const ViewportOverlayLine& line,
        const ViewportCameraController& camera,
        const SceneDocument& document,
        const AssetRegistry& assets,
        ImVec2 itemMin,
        ImVec2 itemSize) {
    constexpr int sampleCount = 33;
    std::array<Vec3, sampleCount> points{};
    std::array<bool, sampleCount> visible{};
    for (int index = 0; index < sampleCount; ++index) {
        const float t = static_cast<float>(index) / static_cast<float>(sampleCount - 1);
        points[static_cast<std::size_t>(index)] = line.start + (line.end - line.start) * t;
        visible[static_cast<std::size_t>(index)] =
                pointVisibleFromCamera(points[static_cast<std::size_t>(index)], camera, document, assets);
    }

    for (int index = 0; index + 1 < sampleCount; ++index) {
        if (!visible[static_cast<std::size_t>(index)] || !visible[static_cast<std::size_t>(index + 1)]) {
            continue;
        }

        const std::optional<ImVec2> start =
                projectWorldToViewport(points[static_cast<std::size_t>(index)], camera, itemMin, itemSize);
        const std::optional<ImVec2> end =
                projectWorldToViewport(points[static_cast<std::size_t>(index + 1)], camera, itemMin, itemSize);
        if (start && end) {
            drawList->AddLine(*start, *end, toImColor(line.color), viewportOverlayLineWidthPixels(line.layer, line.widthPixels));
        }
    }
}

void drawSelectionOverlay2d(
        ImDrawList* drawList,
        const ViewportOverlayFrame& frame,
        const SceneDocument& document,
        const AssetRegistry& assets,
        const ViewportCameraController& camera,
        ImVec2 itemMin,
        ImVec2 itemSize,
        bool componentSelectionMode) {
    for (const ViewportOverlayLayer layer : kViewportOverlayDrawOrder) {
        const bool depthTestedLine =
                componentSelectionMode && viewportOverlayDepthTested(layer, ViewportOverlayGeometryKind::Lines);
        const bool depthTestedPoint =
                componentSelectionMode && viewportOverlayDepthTested(layer, ViewportOverlayGeometryKind::Points);

        for (const ViewportOverlayLine& line : frame.lines) {
            if (line.layer != layer) {
                continue;
            }
            if (depthTestedLine) {
                drawVisibilityClippedOverlayLine(drawList, line, camera, document, assets, itemMin, itemSize);
                continue;
            }
            const std::optional<ImVec2> start = projectWorldToViewport(line.start, camera, itemMin, itemSize);
            const std::optional<ImVec2> end = projectWorldToViewport(line.end, camera, itemMin, itemSize);
            if (start && end) {
                drawList->AddLine(*start, *end, toImColor(line.color), viewportOverlayLineWidthPixels(layer, line.widthPixels));
            }
        }

        for (const ViewportOverlayPoint& point : frame.points) {
            if (point.layer != layer) {
                continue;
            }
            if (depthTestedPoint && !pointVisibleFromCamera(point.position, camera, document, assets)) {
                continue;
            }
            const std::optional<ImVec2> screen = projectWorldToViewport(point.position, camera, itemMin, itemSize);
            if (screen) {
                drawOverlayPoint(drawList, *screen, layer, point.sizePixels, toImColor(point.color));
            }
        }
    }
}

ModelingSelectionMode toModelingSelectionMode(ViewportSelectionMode mode) {
    switch (mode) {
    case ViewportSelectionMode::Vertex:
        return ModelingSelectionMode::Vertex;
    case ViewportSelectionMode::Edge:
        return ModelingSelectionMode::Edge;
    case ViewportSelectionMode::Face:
        return ModelingSelectionMode::Face;
    case ViewportSelectionMode::Object:
        return ModelingSelectionMode::Object;
    }
    return ModelingSelectionMode::Object;
}

ViewportSelectionMode toViewportSelectionMode(ModelingSelectionMode mode) {
    switch (mode) {
    case ModelingSelectionMode::Vertex:
        return ViewportSelectionMode::Vertex;
    case ModelingSelectionMode::Edge:
        return ViewportSelectionMode::Edge;
    case ModelingSelectionMode::Face:
        return ViewportSelectionMode::Face;
    case ModelingSelectionMode::Object:
        return ViewportSelectionMode::Object;
    }
    return ViewportSelectionMode::Object;
}

#if QUADER_HAS_MODELING_SDK

quader_poly::Vec3 toSdk(Vec3 value) {
    return {value.x, value.y, value.z};
}

quader_poly::Vec2 toSdk(Vec2 value) {
    return {value.x, value.y};
}

quader_poly::Ray toSdk(ViewportRay value) {
    return {
            .origin = toSdk(value.origin),
            .direction = toSdk(normalize(value.direction)),
    };
}

Vec3 fromSdk(quader_poly::Vec3 value) {
    return {value.x, value.y, value.z};
}

quader_editor_core::ViewportInteractionTool toSdkTool(ViewportTool tool) {
    switch (tool) {
    case ViewportTool::Move:
        return quader_editor_core::ViewportInteractionTool::move;
    case ViewportTool::Rotate:
        return quader_editor_core::ViewportInteractionTool::rotate;
    case ViewportTool::Scale:
        return quader_editor_core::ViewportInteractionTool::scale;
    default:
        return quader_editor_core::ViewportInteractionTool::select;
    }
}

bool toolAllowsSelectionFallback(ViewportTool tool) {
    return tool == ViewportTool::Select ||
            tool == ViewportTool::Move ||
            tool == ViewportTool::Rotate ||
            tool == ViewportTool::Scale;
}

quader_editor_core::TransformAxis toSdkAxis(ViewportPanel::GizmoAxis axis) {
    switch (axis) {
    case ViewportPanel::GizmoAxis::X:
        return quader_editor_core::TransformAxis::x;
    case ViewportPanel::GizmoAxis::Y:
        return quader_editor_core::TransformAxis::y;
    case ViewportPanel::GizmoAxis::Z:
        return quader_editor_core::TransformAxis::z;
    case ViewportPanel::GizmoAxis::XY:
        return quader_editor_core::TransformAxis::xy;
    case ViewportPanel::GizmoAxis::XZ:
        return quader_editor_core::TransformAxis::xz;
    case ViewportPanel::GizmoAxis::YZ:
        return quader_editor_core::TransformAxis::yz;
    case ViewportPanel::GizmoAxis::All:
        return quader_editor_core::TransformAxis::all;
    case ViewportPanel::GizmoAxis::None:
        return quader_editor_core::TransformAxis::none;
    }
    return quader_editor_core::TransformAxis::none;
}

ViewportPanel::GizmoAxis fromSdkAxis(quader_editor_core::TransformAxis axis) {
    switch (axis) {
    case quader_editor_core::TransformAxis::x:
        return ViewportPanel::GizmoAxis::X;
    case quader_editor_core::TransformAxis::y:
        return ViewportPanel::GizmoAxis::Y;
    case quader_editor_core::TransformAxis::z:
        return ViewportPanel::GizmoAxis::Z;
    case quader_editor_core::TransformAxis::xy:
        return ViewportPanel::GizmoAxis::XY;
    case quader_editor_core::TransformAxis::xz:
        return ViewportPanel::GizmoAxis::XZ;
    case quader_editor_core::TransformAxis::yz:
        return ViewportPanel::GizmoAxis::YZ;
    case quader_editor_core::TransformAxis::all:
        return ViewportPanel::GizmoAxis::All;
    case quader_editor_core::TransformAxis::none:
        return ViewportPanel::GizmoAxis::None;
    }
    return ViewportPanel::GizmoAxis::None;
}

std::uint8_t alphaByte(float alpha) {
    return static_cast<std::uint8_t>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f);
}

std::uint32_t axisColor(quader_editor_core::TransformAxis axis, float alpha = 1.0f) {
    const std::uint8_t a = alphaByte(alpha);
    switch (axis) {
    case quader_editor_core::TransformAxis::x:
        return rgba8(235, 76, 82, a);
    case quader_editor_core::TransformAxis::y:
        return rgba8(90, 205, 118, a);
    case quader_editor_core::TransformAxis::z:
        return rgba8(78, 142, 255, a);
    case quader_editor_core::TransformAxis::xy:
        return rgba8(238, 207, 63, a);
    case quader_editor_core::TransformAxis::xz:
        return rgba8(192, 84, 255, a);
    case quader_editor_core::TransformAxis::yz:
        return rgba8(73, 224, 224, a);
    default:
        return rgba8(255, 255, 255, a);
    }
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

quader_editor_core::ViewportCameraState makeSdkCamera(const ViewportCameraController& camera, ImVec2 itemSize) {
    const ViewportCameraPose pose = camera.pose();
    const Vec3 forward = normalize(pose.target - pose.eye);
    const Vec3 right = normalize(cross(forward, pose.up));
    const Vec3 up = normalize(pose.up);

    quader_editor_core::ViewportCameraState sdkCamera;
    sdkCamera.projection = camera.state().projection == ViewportProjection::Orthographic
            ? quader_editor_core::CameraProjection::orthographic
            : quader_editor_core::CameraProjection::perspective;
    sdkCamera.transform.origin = toSdk(pose.eye);
    sdkCamera.transform.x_axis = toSdk(right);
    sdkCamera.transform.y_axis = toSdk(up);
    sdkCamera.transform.z_axis = toSdk(forward * -1.0f);
    sdkCamera.viewport_size = {std::max(itemSize.x, 1.0f), std::max(itemSize.y, 1.0f)};
    sdkCamera.orthographic_size = std::max(camera.state().orthographicSize, 0.0001f);
    sdkCamera.fov_degrees = camera.state().fovDegrees;
    return sdkCamera;
}

quader_editor_core::TransformGizmoFrameInput makeSdkGizmoInput(
        const ViewportCameraController& camera,
        Vec3 pivot,
        ViewportTool tool,
        ViewportPanel::GizmoAxis activeAxis,
        ViewportPanel::GizmoAxis hoverAxis,
        double gridSize,
        ImVec2 itemSize) {
    quader_editor_core::TransformGizmoFrameInput input;
    input.tool = toSdkTool(tool);
    input.active_axis = toSdkAxis(activeAxis);
    input.hover_axis = toSdkAxis(hoverAxis);
    input.has_selection = true;
    input.has_pivot = true;
    input.pivot = toSdk(pivot);
    input.camera = makeSdkCamera(camera, itemSize);
    input.grid_size = gridSize;
    input.gizmo_size_pixels = 68.0;
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

void drawSquareHandle(ImDrawList* drawList, ImVec2 center, float size, ImU32 color) {
    const float half = size * 0.5f;
    const ImVec2 min{center.x - half, center.y - half};
    const ImVec2 max{center.x + half, center.y + half};
    drawList->AddRectFilled(min, max, color, 1.5f);
    drawList->AddRect(min, max, IM_COL32(0, 0, 0, 120), 1.5f, 0, 1.0f);
}

std::optional<ImVec2> projectSdkWorldToViewport(
        quader_poly::Vec3 world,
        const ViewportCameraController& camera,
        ImVec2 itemMin,
        ImVec2 itemSize) {
    return projectWorldToViewport(fromSdk(world), camera, itemMin, itemSize);
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

} // namespace

void ViewportPanel::draw(ViewportRenderer& renderer,
        ViewportCameraController& camera,
        SceneRenderBridge& renderBridge,
        SceneDocument& document,
        AssetRegistry& assets,
        ModelingSessionService& modeling,
        AssetId defaultMaterial) {
    if (!ImGui::Begin("Viewport")) {
        renderBridge.setOverlayFrame({});
        ImGui::End();
        return;
    }

    ImVec2 available = ImGui::GetContentRegionAvail();
    available.x = std::max(available.x, 1.0f);
    available.y = std::max(available.y, 1.0f);
    renderer.resize(static_cast<std::uint32_t>(available.x), static_cast<std::uint32_t>(available.y));

    ImGui::Image(renderer.textureId(), available, {0.0f, 1.0f}, {1.0f, 0.0f});
    const ImVec2 itemMin = ImGui::GetItemRectMin();
    const ImVec2 itemMax = ImGui::GetItemRectMax();
    const ImVec2 itemSize{itemMax.x - itemMin.x, itemMax.y - itemMin.y};
    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    const ViewportInputFrame input = makeViewportInputFrame(hovered, focused, itemMin, itemSize);
    const ViewportInputResult result = inputRouter_.handleFrame(input, camera);
    if (result.focusDocumentRequested) {
        focusDocument(document, assets, camera);
    }
    if (result.selectionModeChanged) {
        const ModelingCommandResult command =
                modeling.setSelectionMode(toModelingSelectionMode(inputRouter_.state().selectionMode));
        if (!command.ok) {
            LOG_ERROR("Selection mode change failed: {}", command.message);
        }
        paintSelectionActive_ = false;
    }
    if (result.toolChanged) {
        LOG_INFO("Viewport tool changed to {}", viewportToolId(inputRouter_.state().activeTool));
        paintSelectionActive_ = false;
        if (inputRouter_.state().activeTool == ViewportTool::Box) {
            const ModelingCommandResult command = modeling.activateBoxTool();
            boxPreview_ = {};
            if (!command.ok) {
                LOG_ERROR("Box tool activation failed: {}", command.message);
            }
        } else if (hasActiveBoxPreview(boxPreview_)) {
            const ModelingCommandResult cancel = modeling.cancelBox();
            if (!cancel.ok) {
                LOG_ERROR("Box cancel failed: {}", cancel.message);
            }
            boxPreview_ = {};
        }
    }

    bool cancelConsumed = false;
    if (result.cancelRequested) {
        if (gizmoDragging_) {
            for (const GizmoStartTransform& start : gizmoStartTransforms_) {
                if (start.entity != InvalidEntityId && document.transform(start.entity) != nullptr) {
                    document.setTransform(start.entity, start.transform);
                }
            }
            gizmoStartTransforms_.clear();
            gizmoDragging_ = false;
            activeGizmoAxis_ = GizmoAxis::None;
#if QUADER_HAS_MODELING_SDK
            sdkMoveDrag_ = {};
            sdkRotateDrag_ = {};
            sdkScaleDrag_ = {};
#endif
            renderBridge.setOverlayFrame({});
            cancelConsumed = true;
        }

        if (inputRouter_.state().activeTool == ViewportTool::Box) {
            const ModelingCommandResult cancel = modeling.cancelBox();
            if (!cancel.ok) {
                LOG_ERROR("Box cancel failed: {}", cancel.message);
            }
            boxPreview_ = {};
            paintSelectionActive_ = false;
            cancelConsumed = true;
        }
    }

    const bool viewCubeConsumed = drawViewCube(camera, itemMin, itemSize);
    const bool gizmoConsumed = viewCubeConsumed || cancelConsumed || drawTransformGizmo(document, assets, modeling, camera, renderBridge, itemMin, itemSize);
    const bool leftClicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const bool leftDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    const bool leftReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    const int leftClickCount = std::max(1, ImGui::GetMouseClickedCount(ImGuiMouseButton_Left));

    if (inputRouter_.state().activeTool == ViewportTool::Box) {
        const float gridSize = gridSizeForPreset(inputRouter_.state().gridPreset);
        const ModelingBoxToolEvent boxEvent = modelingBoxEvent(camera, input, gridSize);
        bool boxActionConsumed = false;

        if (hovered && !leftDown && boxPreview_.dragState == ModelingBoxDragState::Idle) {
            boxPreview_ = modeling.updateBoxHover(boxEvent);
            logBoxPreviewFailure("Box hover", boxPreview_);
        }

        if (leftClicked && !gizmoConsumed && !result.navigationActive) {
            if (boxPreview_.dragState == ModelingBoxDragState::Height) {
                const std::string name = "Box " + std::to_string(createdBoxCount_);
                if (commitBoxPreview(modeling, defaultMaterial, document, name)) {
                    ++createdBoxCount_;
                    inputRouter_.setSelectionMode(ViewportSelectionMode::Object);
                    (void)modeling.setSelectionMode(ModelingSelectionMode::Object);
                    inputRouter_.setActiveTool(ViewportTool::Move);
                }
                boxPreview_ = {};
            } else {
                boxPreview_ = modeling.beginBoxFootprint(boxEvent);
                logBoxPreviewFailure("Box footprint begin", boxPreview_);
            }
            boxActionConsumed = true;
        }

        if (!boxActionConsumed && boxPreview_.dragState == ModelingBoxDragState::Footprint && leftDown) {
            boxPreview_ = modeling.updateBoxFootprint(boxEvent);
            logBoxPreviewFailure("Box footprint update", boxPreview_);
        }

        if (!boxActionConsumed && boxPreview_.dragState == ModelingBoxDragState::Footprint && leftReleased) {
            boxPreview_ = modeling.beginBoxHeight(boxEvent);
            logBoxPreviewFailure("Box height begin", boxPreview_);
        }

        if (!boxActionConsumed && hovered && !leftDown && boxPreview_.dragState == ModelingBoxDragState::Height) {
            boxPreview_ = modeling.updateBoxHeight(boxEvent);
            logBoxPreviewFailure("Box height update", boxPreview_);
        }

        drawBoxPreview(ImGui::GetWindowDrawList(), boxPreview_, camera, itemMin, itemSize);
    } else {
        if (hasActiveBoxPreview(boxPreview_)) {
            const ModelingCommandResult cancel = modeling.cancelBox();
            if (!cancel.ok) {
                LOG_ERROR("Box cancel failed: {}", cancel.message);
            }
            boxPreview_ = {};
        }

        const bool selectionFallbackActive = toolAllowsSelectionFallback(inputRouter_.state().activeTool);
        if (selectionFallbackActive &&
                leftClicked &&
                !gizmoConsumed &&
                !result.navigationActive) {
            selectFromViewport(document, assets, modeling, camera, input.mouseLocal, input.modifiers, leftClickCount);
            paintSelectionActive_ = inputRouter_.state().selectionMode != ViewportSelectionMode::Object;
        } else if (selectionFallbackActive &&
                paintSelectionActive_ &&
                leftDown &&
                hovered &&
                !gizmoConsumed &&
                !result.navigationActive) {
            const ViewportRay worldRay = camera.rayFromViewportPoint(input.mouseLocal);
            const ModelingSelectionSummary summary = modeling.pickSelect(ModelingPickSelectRequest{
                    .ray = {
                            .origin = worldRay.origin,
                            .direction = worldRay.direction,
                    },
                    .clickCount = 0,
                    .controlPressed = input.modifiers.primary,
                    .shiftPressed = input.modifiers.shift,
                    .commandPressed = input.modifiers.primary,
                    .altPressed = input.modifiers.alt,
            });
            inputRouter_.setSelectionMode(toViewportSelectionMode(summary.mode));
            paintSelectionActive_ = summary.mode != ModelingSelectionMode::Object;
        }

        if (leftReleased) {
            paintSelectionActive_ = false;
        }

        if (selectionFallbackActive &&
                hovered &&
                !result.navigationActive &&
                !gizmoConsumed) {
            const ViewportRay worldRay = camera.rayFromViewportPoint(input.mouseLocal);
            (void)modeling.updateSelectionHover(ModelingHoverRequest{
                    .ray = {
                            .origin = worldRay.origin,
                            .direction = worldRay.direction,
                    },
                    .selectedModifierPressed = input.modifiers.primary,
            });
        } else if (!hovered || !selectionFallbackActive) {
            (void)modeling.clearSelectionHover();
        }
    }

    const ViewportOverlayFrame selectionOverlay = modeling.selectionOverlayFrame();
    renderBridge.setOverlayFrame(selectionOverlay);
    drawSelectionOverlay2d(
            ImGui::GetWindowDrawList(),
            selectionOverlay,
            document,
            assets,
            camera,
            itemMin,
            itemSize,
            inputRouter_.state().selectionMode != ViewportSelectionMode::Object);

    ImGui::End();
}

bool ViewportPanel::drawViewCube(ViewportCameraController& camera, ImVec2 itemMin, ImVec2 itemSize) {
#if QUADER_HAS_MODELING_SDK
    quader_editor_core::ViewCubePrimitiveInput input;
    input.camera_transform = makeSdkCamera(camera, itemSize).transform;
    input.viewport_size = {std::max(itemSize.x, 1.0f), std::max(itemSize.y, 1.0f)};

    const quader_editor_core::ViewCubePrimitives primitives = quader_editor_core::build_view_cube_primitives(input);
    if (!primitives.ok) {
        return false;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 center{itemMin.x + primitives.screen_position.x, itemMin.y + primitives.screen_position.y};
    const ImVec2 rectMin{itemMin.x + primitives.rect_position.x, itemMin.y + primitives.rect_position.y};
    const ImVec2 rectMax{rectMin.x + primitives.rect_size.x, rectMin.y + primitives.rect_size.y};

    drawList->AddCircleFilled(center, primitives.rect_size.x * 0.55f, IM_COL32(18, 22, 28, 168), 32);
    drawList->AddRect(rectMin, rectMax, IM_COL32(255, 255, 255, 72), 4.0f, 0, 1.0f);

    for (const quader_editor_core::ViewCubeFacePrimitive& face : primitives.faces) {
        std::vector<ImVec2> polygon;
        polygon.reserve(face.polygon.size());
        for (quader_poly::Vec2 point : face.polygon) {
            polygon.push_back({itemMin.x + point.x, itemMin.y + point.y});
        }
        if (polygon.size() < 3u) {
            continue;
        }

        drawList->AddConvexPolyFilled(
                polygon.data(),
                static_cast<int>(polygon.size()),
                viewAxisImColor(face.axis, face.sign > 0 ? 0.32f : 0.20f));
        drawList->AddPolyline(
                polygon.data(),
                static_cast<int>(polygon.size()),
                viewAxisImColor(face.axis, 0.9f),
                ImDrawFlags_Closed,
                1.0f);

        const std::string_view label = quader_editor_core::view_axis_label(face.axis, face.sign);
        const ImVec2 textSize = ImGui::CalcTextSize(label.data(), label.data() + label.size());
        drawList->AddText(
                {itemMin.x + face.screen_position.x - textSize.x * 0.5f,
                        itemMin.y + face.screen_position.y - textSize.y * 0.5f},
                IM_COL32(248, 250, 252, 230),
                label.data(),
                label.data() + label.size());
    }

    for (const quader_editor_core::ViewCubeAxisMarkerPrimitive& marker : primitives.axis_markers) {
        const ImVec2 markerCenter{itemMin.x + marker.screen_position.x, itemMin.y + marker.screen_position.y};
        const ImU32 markerColor = viewAxisImColor(marker.axis, marker.depth >= 0.0 ? 1.0f : 0.45f);
        drawList->AddCircleFilled(markerCenter, 8.0f, IM_COL32(18, 22, 28, 215), 16);
        drawList->AddCircle(markerCenter, 8.0f, markerColor, 16, 1.5f);

        const std::string_view label = quader_editor_core::view_axis_label(marker.axis, marker.sign);
        const ImVec2 textSize = ImGui::CalcTextSize(label.data(), label.data() + label.size());
        drawList->AddText(
                {markerCenter.x - textSize.x * 0.5f, markerCenter.y - textSize.y * 0.5f},
                IM_COL32(248, 250, 252, 240),
                label.data(),
                label.data() + label.size());
    }

    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mouse = io.MousePos;
    const bool mouseInsideViewport =
            mouse.x >= itemMin.x && mouse.y >= itemMin.y &&
            mouse.x <= itemMin.x + itemSize.x && mouse.y <= itemMin.y + itemSize.y;
    if (!mouseInsideViewport || !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        return false;
    }

    quader_editor_core::ViewCubePickInput pickInput;
    pickInput.primitives = input;
    pickInput.screen_position = {mouse.x - itemMin.x, mouse.y - itemMin.y};
    const quader_editor_core::ViewCubeHit hit = quader_editor_core::pick_view_cube(pickInput);
    if (!hit.hit) {
        return false;
    }

    if (hit.kind == quader_editor_core::ViewCubeHitKind::face) {
        camera.viewAxis(toViewportViewAxis(hit.axis), hit.sign);
    }
    return true;
#else
    (void)camera;
    (void)itemMin;
    (void)itemSize;
    return false;
#endif
}

void ViewportPanel::selectFromViewport(
        SceneDocument& document,
        const AssetRegistry& assets,
        ModelingSessionService& modeling,
        const ViewportCameraController& camera,
        Vec2 localMouse,
        ViewportModifiers modifiers,
        int clickCount) {
    const ViewportRay worldRay = camera.rayFromViewportPoint(localMouse);
    if (modeling.sdkAvailable()) {
        const ModelingSelectionSummary summary = modeling.pickSelect(ModelingPickSelectRequest{
                .ray = {
                        .origin = worldRay.origin,
                        .direction = worldRay.direction,
                },
                .clickCount = clickCount,
                .controlPressed = modifiers.primary,
                .shiftPressed = modifiers.shift,
                .commandPressed = modifiers.primary,
                .altPressed = modifiers.alt,
        });
        inputRouter_.setSelectionMode(toViewportSelectionMode(summary.mode));
        return;
    }

    EntityId bestEntity = InvalidEntityId;
    float bestDistance = std::numeric_limits<float>::max();

    for (const EntityId id : document.entities()) {
        const VisibilityComponent* visibility = document.visibility(id);
        if (visibility && (!visibility->visible || !visibility->selectable)) {
            continue;
        }

        const MeshComponent* meshComponent = document.mesh(id);
        const TransformComponent* transform = document.transform(id);
        if (!meshComponent || !transform) {
            continue;
        }

        const MeshAsset* mesh = assets.mesh(meshComponent->mesh);
        if (!mesh) {
            continue;
        }

        const std::optional<SceneBounds> bounds = localMeshBounds(*mesh);
        if (!bounds) {
            continue;
        }

        const Mat4 worldFromLocal = composeTransform(transform->spatialTransform());
        const Mat4 localFromWorld = inverse(worldFromLocal);
        const Vec3 localOrigin = transformPoint(localFromWorld, worldRay.origin);
        const Vec3 unnormalizedLocalDirection = transformVector(localFromWorld, worldRay.direction);
        if (length(unnormalizedLocalDirection) <= 0.000001f) {
            continue;
        }
        const Vec3 localDirection = normalize(unnormalizedLocalDirection);

        float localDistance = 0.0f;
        if (!rayIntersectsBounds(localOrigin, localDirection, *bounds, localDistance)) {
            continue;
        }

        const Vec3 localHit = localOrigin + localDirection * localDistance;
        const Vec3 worldHit = transformPoint(worldFromLocal, localHit);
        const float worldDistance = length(worldHit - worldRay.origin);
        if (worldDistance < bestDistance) {
            bestDistance = worldDistance;
            bestEntity = id;
        }
    }

    if (!modifiers.primary && !modifiers.shift) {
        document.clearSelection();
    }
    if (bestEntity != InvalidEntityId) {
        const bool alreadySelected =
                document.selection(bestEntity) != nullptr && document.selection(bestEntity)->selected;
        document.setSelected(bestEntity, modifiers.primary && alreadySelected ? false : true);
    }
}

bool ViewportPanel::drawTransformGizmo(
        SceneDocument& document,
        const AssetRegistry& assets,
        ModelingSessionService& modeling,
        ViewportCameraController& camera,
        SceneRenderBridge& renderBridge,
        ImVec2 itemMin,
        ImVec2 itemSize) {
    const EntityId selected = document.primarySelection();
    if (selected == InvalidEntityId) {
        renderBridge.setOverlayFrame({});
        gizmoDragging_ = false;
        activeGizmoAxis_ = GizmoAxis::None;
        return false;
    }

    TransformComponent* transform = document.transform(selected);
    if (!transform) {
        renderBridge.setOverlayFrame({});
        return false;
    }

    const ViewportTool tool = inputRouter_.state().activeTool;
    if (tool != ViewportTool::Move && tool != ViewportTool::Rotate && tool != ViewportTool::Scale) {
        renderBridge.setOverlayFrame({});
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            gizmoDragging_ = false;
            activeGizmoAxis_ = GizmoAxis::None;
        }
        return false;
    }

    modeling.syncObjectSelectionFromScene();
    const std::optional<SceneBounds> selectionBounds = selectedWorldBounds(document, assets);
    const std::optional<Vec3> sdkSelectionCenter = modeling.selectionCenter();
    const bool componentSelectionMode = modeling.selectionMode() != ModelingSelectionMode::Object;
    const Vec3 gizmoPivot =
            (componentSelectionMode && sdkSelectionCenter) ? *sdkSelectionCenter :
            (selectionBounds ? boundsCenter(*selectionBounds) : sdkSelectionCenter.value_or(transform->position));
    const ViewportScreenPoint projectedPivot = camera.projectWorldPoint(gizmoPivot);
    if (!projectedPivot.visible) {
        renderBridge.setOverlayFrame({});
        gizmoDragging_ = false;
        activeGizmoAxis_ = GizmoAxis::None;
        return false;
    }
    const ImVec2 origin{itemMin.x + projectedPivot.position.x, itemMin.y + projectedPivot.position.y};

    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mouse = io.MousePos;
    const bool mouseInsideViewport =
            mouse.x >= itemMin.x && mouse.y >= itemMin.y &&
            mouse.x <= itemMin.x + itemSize.x && mouse.y <= itemMin.y + itemSize.y;
    const double activeGridSize = std::max(static_cast<double>(gridSizeForPreset(inputRouter_.state().gridPreset)), 0.0001);

#if QUADER_HAS_MODELING_SDK
    quader_editor_core::TransformGizmoFrameInput sdkInput =
            makeSdkGizmoInput(camera, gizmoPivot, tool, activeGizmoAxis_, GizmoAxis::None, activeGridSize, itemSize);
    if (!gizmoDragging_ && mouseInsideViewport) {
        const Vec2 localMouse{mouse.x - itemMin.x, mouse.y - itemMin.y};
        const quader_editor_core::TransformGizmoPickHit hoverHit =
                quader_editor_core::pick_transform_gizmo(sdkInput, toSdk(localMouse));
        sdkInput.hover_axis = hoverHit.hit ? hoverHit.axis : quader_editor_core::TransformAxis::none;
    }
    const quader_editor_core::TransformGizmoFrame sdkFrame =
            quader_editor_core::build_transform_gizmo_frame(sdkInput);
    renderBridge.setOverlayFrame({});
    drawUnityStyleGizmo(sdkFrame, camera, itemMin, itemSize);
#else
    renderBridge.setOverlayFrame({});
#endif

    const ViewportCameraPose pose = camera.pose();
    const float cameraDistance = std::max(length(pose.eye - gizmoPivot), 0.1f);
    const float worldLength = std::clamp(cameraDistance * 0.22f, 0.35f, 8.0f);
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
    const std::array<GizmoAxis, 3> axes = {{GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z}};

    std::array<std::optional<ImVec2>, 3> axisEnds;
    for (std::size_t index = 0; index < axisVectors.size(); ++index) {
        axisEnds[index] = projectWorldToViewport(gizmoPivot + axisVectors[index] * worldLength, camera, itemMin, itemSize);
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
#if !QUADER_HAS_MODELING_SDK
    if (tool == ViewportTool::Rotate) {
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
#endif

    bool consumed = gizmoDragging_;
    if (!gizmoDragging_ && mouseInsideViewport && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
#if QUADER_HAS_MODELING_SDK
        quader_editor_core::TransformGizmoPickHit sdkHit;
#endif

#if QUADER_HAS_MODELING_SDK
        const Vec2 localMouse{mouse.x - itemMin.x, mouse.y - itemMin.y};
        sdkHit = quader_editor_core::pick_transform_gizmo(sdkInput, toSdk(localMouse));
        activeGizmoAxis_ = sdkHit.hit ? fromSdkAxis(sdkHit.axis) : GizmoAxis::None;
#else
        if (tool == ViewportTool::Rotate) {
            const float radius = std::sqrt(distanceSquared(mouse, origin));
            const float baseRadius = std::clamp(std::min(itemSize.x, itemSize.y) * 0.075f, 34.0f, 92.0f);
            if (std::abs(radius - baseRadius) < 8.0f) {
                activeGizmoAxis_ = GizmoAxis::X;
            } else if (std::abs(radius - baseRadius * 0.78f) < 8.0f) {
                activeGizmoAxis_ = GizmoAxis::Y;
            } else if (std::abs(radius - baseRadius * 0.56f) < 8.0f) {
                activeGizmoAxis_ = GizmoAxis::Z;
            }
        } else {
            for (std::size_t index = 0; index < axisEnds.size(); ++index) {
                if (axisEnds[index] && distanceToSegmentSquared(mouse, origin, *axisEnds[index]) < 100.0f) {
                    activeGizmoAxis_ = axes[index];
                    break;
                }
            }
        }
#endif

        if (activeGizmoAxis_ != GizmoAxis::None) {
            bool dragReady = true;
#if QUADER_HAS_MODELING_SDK
            dragReady = false;
            sdkMoveDrag_ = {};
            sdkRotateDrag_ = {};
            sdkScaleDrag_ = {};

            const Vec2 localMouse{mouse.x - itemMin.x, mouse.y - itemMin.y};
            const quader_editor_core::TransformAxis sdkAxis = toSdkAxis(activeGizmoAxis_);
            const quader_editor_core::ViewportCameraState sdkCamera = makeSdkCamera(camera, itemSize);
            const quader_poly::Vec2 sdkScreenPosition = toSdk(localMouse);
            const quader_poly::Vec2 sdkScreenPivot = toSdk(projectedPivot.position);
            const quader_poly::Vec2 sdkScreenTip = sdkHit.hit ? sdkHit.screen_tip : sdkScreenPivot;
            const quader_poly::Ray sdkStartRay = toSdk(camera.rayFromViewportPoint(localMouse));

            if (tool == ViewportTool::Move) {
                quader_editor_core::MoveBeginInput begin;
                begin.axis = sdkAxis;
                begin.center_local = {};
                begin.has_world_center = true;
                begin.world_center = toSdk(gizmoPivot);
                begin.has_screen_axis = true;
                begin.screen_pivot = sdkScreenPivot;
                begin.screen_tip = sdkScreenTip;
                begin.screen_position = sdkScreenPosition;
                begin.has_start_ray = true;
                begin.start_ray = sdkStartRay;
                begin.has_camera = true;
                begin.camera_transform = sdkCamera.transform;

                const quader_editor_core::MoveBeginResult beginResult =
                        quader_editor_core::begin_move_drag(begin);
                if (beginResult.ok) {
                    sdkMoveDrag_ = beginResult.state;
                    dragReady = true;
                }
            } else if (tool == ViewportTool::Rotate) {
                quader_editor_core::RotateBeginInput begin;
                begin.axis = sdkAxis;
                begin.pivot = toSdk(gizmoPivot);
                begin.screen_position = sdkScreenPosition;
                begin.has_screen_pivot = true;
                begin.screen_pivot = sdkScreenPivot;
                begin.has_world_direction = sdkHit.hit;
                begin.world_direction = sdkHit.world_direction;
                begin.has_world_pivot = true;
                begin.world_pivot = toSdk(gizmoPivot);
                begin.has_camera = true;
                begin.camera_transform = sdkCamera.transform;

                const quader_editor_core::RotateBeginResult beginResult =
                        quader_editor_core::begin_rotate_drag(begin);
                if (beginResult.ok) {
                    sdkRotateDrag_ = beginResult.state;
                    dragReady = true;
                }
            } else if (tool == ViewportTool::Scale) {
                quader_editor_core::ScaleBeginInput begin;
                begin.axis = sdkAxis;
                begin.pivot = toSdk(gizmoPivot);
                begin.screen_position = sdkScreenPosition;
                begin.has_screen_pivot = true;
                begin.screen_pivot = sdkScreenPivot;
                begin.has_screen_tip = true;
                begin.screen_tip = sdkScreenTip;

                const quader_editor_core::ScaleBeginResult beginResult =
                        quader_editor_core::begin_scale_drag(begin);
                if (beginResult.ok) {
                    sdkScaleDrag_ = beginResult.state;
                    dragReady = true;
                }
            }
#endif

            if (!dragReady) {
                activeGizmoAxis_ = GizmoAxis::None;
                return consumed;
            }
            gizmoDragging_ = true;
            lastGizmoMouse_ = {mouse.x, mouse.y};
            gizmoStartWorldPivot_ = gizmoPivot;
            gizmoStartTransforms_.clear();
            for (const EntityId entity : document.selectedEntities()) {
                if (const TransformComponent* selectedTransform = document.transform(entity)) {
                    gizmoStartTransforms_.push_back(GizmoStartTransform{
                            .entity = entity,
                            .transform = *selectedTransform,
                            .localPivot = transformPoint(inverse(composeTransform(selectedTransform->spatialTransform())), gizmoPivot),
                    });
                }
            }
            if (gizmoStartTransforms_.empty()) {
                gizmoDragging_ = false;
                activeGizmoAxis_ = GizmoAxis::None;
                return consumed;
            }
            consumed = true;
        }
    }

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const bool wasDragging = gizmoDragging_;
        gizmoDragging_ = false;
        activeGizmoAxis_ = GizmoAxis::None;
        gizmoStartTransforms_.clear();
#if QUADER_HAS_MODELING_SDK
        sdkMoveDrag_ = {};
        sdkRotateDrag_ = {};
        sdkScaleDrag_ = {};
#endif
        return consumed || wasDragging;
    }

    if (!gizmoDragging_ || activeGizmoAxis_ == GizmoAxis::None) {
        return consumed;
    }

    const Vec2 currentMouse{mouse.x, mouse.y};
    auto pinStartPivot = [&](TransformComponent& next, const GizmoStartTransform& start) {
        Transform spatial = next.spatialTransform();
        spatial.position = {};
        next.position = gizmoStartWorldPivot_ - transformPoint(composeTransform(spatial), start.localPivot);
    };

#if QUADER_HAS_MODELING_SDK
    const Vec2 localMouse{mouse.x - itemMin.x, mouse.y - itemMin.y};
    const quader_poly::Vec2 sdkScreenPosition = toSdk(localMouse);
    const quader_poly::Ray sdkRay = toSdk(camera.rayFromViewportPoint(localMouse));
    const quader_editor_core::ViewportCameraState sdkCamera = makeSdkCamera(camera, itemSize);
    const bool precision = io.KeyCtrl || io.KeySuper;
    bool previewOk = false;
    Vec3 previewMoveDelta = {};
    Vec3 previewRotationDelta = {};
    Vec3 previewScaleFactor = {1.0f, 1.0f, 1.0f};

    if (tool == ViewportTool::Move && sdkMoveDrag_.active) {
        const quader_editor_core::MovePreviewResult preview =
                quader_editor_core::preview_move_from_viewport(
                        sdkMoveDrag_,
                        sdkScreenPosition,
                        sdkRay,
                        sdkCamera,
                        precision,
                        !precision,
                        activeGridSize);
        if (preview.ok) {
            previewMoveDelta = fromSdk(preview.preview_delta);
            previewOk = true;
        }
    } else if (tool == ViewportTool::Rotate && sdkRotateDrag_.active) {
        const quader_editor_core::RotatePreviewResult preview =
                quader_editor_core::preview_rotate_from_screen_position(
                        sdkRotateDrag_,
                        sdkScreenPosition,
                        precision ? 0.0 : 15.0);
        if (preview.ok) {
            const float angleRadians = static_cast<float>(preview.preview_angle_degrees * kPi / 180.0);
            if (activeGizmoAxis_ == GizmoAxis::X) {
                previewRotationDelta.x = angleRadians;
            } else if (activeGizmoAxis_ == GizmoAxis::Y) {
                previewRotationDelta.y = angleRadians;
            } else if (activeGizmoAxis_ == GizmoAxis::Z) {
                previewRotationDelta.z = angleRadians;
            }
            previewOk = true;
        }
    } else if (tool == ViewportTool::Scale && sdkScaleDrag_.active) {
        const quader_editor_core::ScalePreviewResult preview =
                quader_editor_core::preview_scale_from_screen_position(
                        sdkScaleDrag_,
                        sdkScreenPosition,
                        !precision,
                        activeGridSize);
        if (preview.ok) {
            previewScaleFactor = fromSdk(preview.preview_scale);
            previewOk = true;
        }
    }

    if (!previewOk) {
        return consumed;
    }

    for (const GizmoStartTransform& start : gizmoStartTransforms_) {
        TransformComponent next = start.transform;
        if (tool == ViewportTool::Move) {
            next.position = start.transform.position + previewMoveDelta;
        } else if (tool == ViewportTool::Rotate) {
            next.rotationEuler = next.rotationEuler + previewRotationDelta;
            pinStartPivot(next, start);
        } else if (tool == ViewportTool::Scale) {
            next.scale.x = std::max(start.transform.scale.x * previewScaleFactor.x, 0.001f);
            next.scale.y = std::max(start.transform.scale.y * previewScaleFactor.y, 0.001f);
            next.scale.z = std::max(start.transform.scale.z * previewScaleFactor.z, 0.001f);
            pinStartPivot(next, start);
        }
        document.setTransform(start.entity, next);
    }
#else
    const Vec2 delta = currentMouse - lastGizmoMouse_;
    lastGizmoMouse_ = currentMouse;

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

    auto screenAxisDelta = [&](GizmoAxis axis) {
        switch (axis) {
        case GizmoAxis::X:
            return screenAxisDeltaAt(0u);
        case GizmoAxis::Y:
            return screenAxisDeltaAt(1u);
        case GizmoAxis::Z:
            return screenAxisDeltaAt(2u);
        case GizmoAxis::XY:
            return (screenAxisDeltaAt(0u) + screenAxisDeltaAt(1u)) * 0.5f;
        case GizmoAxis::XZ:
            return (screenAxisDeltaAt(0u) + screenAxisDeltaAt(2u)) * 0.5f;
        case GizmoAxis::YZ:
            return (screenAxisDeltaAt(1u) + screenAxisDeltaAt(2u)) * 0.5f;
        case GizmoAxis::All:
            return (screenAxisDeltaAt(0u) + screenAxisDeltaAt(1u) + screenAxisDeltaAt(2u)) / 3.0f;
        case GizmoAxis::None:
            return 0.0f;
        }
        return 0.0f;
    };

    auto applyAxis = [&](float& x, float& y, float& z, float value) {
        if (activeGizmoAxis_ == GizmoAxis::X ||
                activeGizmoAxis_ == GizmoAxis::XY ||
                activeGizmoAxis_ == GizmoAxis::XZ ||
                activeGizmoAxis_ == GizmoAxis::All) {
            x += value;
        }
        if (activeGizmoAxis_ == GizmoAxis::Y ||
                activeGizmoAxis_ == GizmoAxis::XY ||
                activeGizmoAxis_ == GizmoAxis::YZ ||
                activeGizmoAxis_ == GizmoAxis::All) {
            y += value;
        }
        if (activeGizmoAxis_ == GizmoAxis::Z ||
                activeGizmoAxis_ == GizmoAxis::XZ ||
                activeGizmoAxis_ == GizmoAxis::YZ ||
                activeGizmoAxis_ == GizmoAxis::All) {
            z += value;
        }
    };

    for (const GizmoStartTransform& start : gizmoStartTransforms_) {
        TransformComponent next = start.transform;
        if (tool == ViewportTool::Move) {
            applyAxis(next.position.x, next.position.y, next.position.z, screenAxisDelta(activeGizmoAxis_) * worldPerPixel);
        } else if (tool == ViewportTool::Rotate) {
            const float screenDelta = (std::abs(delta.x) > std::abs(delta.y)) ? delta.x : -delta.y;
            applyAxis(next.rotationEuler.x, next.rotationEuler.y, next.rotationEuler.z, screenDelta * 0.01f);
            pinStartPivot(next, start);
        } else if (tool == ViewportTool::Scale) {
            applyAxis(next.scale.x, next.scale.y, next.scale.z, screenAxisDelta(activeGizmoAxis_) * 0.01f);
            next.scale.x = std::max(next.scale.x, 0.001f);
            next.scale.y = std::max(next.scale.y, 0.001f);
            next.scale.z = std::max(next.scale.z, 0.001f);
            pinStartPivot(next, start);
        }
        document.setTransform(start.entity, next);
    }
#endif

    return true;
}

} // namespace quader
