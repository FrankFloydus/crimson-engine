#include "quader/ui/ViewportPanel.h"

#include "quader/core/ColorSpace.h"
#include "quader/core/Logger.h"
#include "quader/render/ViewportOverlayPolicy.h"
#include "quader/scene/SceneBounds.h"
#include "quader/scene/SceneEntityNaming.h"
#include "quader/scene/TransformPivot.h"
#include "quader/render/RenderLayers.h"
#include "quader/render/RenderSystem.h"
#include "quader/ui/UIContext.h"
#include "quader/ui/ViewportContextMenuPolicy.h"
#include "quader/viewport/HostViewportContractAdapter.h"
#include "quader/viewport/ViewportModelingCommandRouter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <imgui_internal.h>

#if QUADER_HAS_MODELING_SDK
#include <quader_editor_core/tool/tool_transform_math.h>
#include <quader_editor_core/tool/transform_gizmo.h>
#include <quader_editor_core/tool/viewport_navigation.h>
#endif

namespace quader {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr std::uint64_t kViewportPanelHashOffset = 1469598103934665603ull;
constexpr std::uint64_t kViewportPanelHashPrime = 1099511628211ull;

std::uint64_t mixViewportPanelHash(std::uint64_t hash, std::uint64_t value) {
    hash ^= value;
    hash *= kViewportPanelHashPrime;
    return hash;
}

std::uint64_t mixViewportPanelHash(std::uint64_t hash, bool value) {
    return mixViewportPanelHash(hash, value ? 1ull : 0ull);
}

std::uint64_t mixViewportPanelHash(std::uint64_t hash, float value) {
    return mixViewportPanelHash(hash, static_cast<std::uint64_t>(std::hash<float>{}(value)));
}

std::uint64_t mixViewportPanelHash(std::uint64_t hash, double value) {
    return mixViewportPanelHash(hash, static_cast<std::uint64_t>(std::hash<double>{}(value)));
}

std::uint64_t mixViewportPanelHash(std::uint64_t hash, Vec3 value) {
    hash = mixViewportPanelHash(hash, value.x);
    hash = mixViewportPanelHash(hash, value.y);
    return mixViewportPanelHash(hash, value.z);
}

template <typename Enum>
std::uint64_t mixViewportPanelEnum(std::uint64_t hash, Enum value) {
    return mixViewportPanelHash(hash, static_cast<std::uint64_t>(value));
}

std::uint64_t toolOverlaySignature(ModelingSessionService& modeling) {
    const ModelingToolStatus status = modeling.toolStatus();
    const ModelingInteractionSettings settings = modeling.interactionSettings();
    std::uint64_t hash = mixViewportPanelEnum(kViewportPanelHashOffset, status.activeTool);
    hash = mixViewportPanelEnum(hash, settings.cutMode);
    hash = mixViewportPanelHash(hash, status.cutActive);
    hash = mixViewportPanelHash(hash, status.cutCanPreview);
    hash = mixViewportPanelHash(hash, status.cutPreviewValid);
    hash = mixViewportPanelHash(hash, status.cutDragging);
    hash = mixViewportPanelHash(hash, status.cutDraggingNewPoint);
    hash = mixViewportPanelHash(hash, status.cutDraggingLine);
    hash = mixViewportPanelHash(hash, static_cast<std::uint64_t>(status.cutDraggingPointIndex));
    hash = mixViewportPanelHash(hash, static_cast<std::uint64_t>(status.cutPointCount));
    hash = mixViewportPanelHash(hash, static_cast<std::uint64_t>(status.cutCapturedPointCount));
    hash = mixViewportPanelHash(hash, status.cutHasLine);
    for (const Vec3 point : status.cutPoints) {
        hash = mixViewportPanelHash(hash, point);
    }
    hash = mixViewportPanelHash(hash, status.mirrorActive);
    hash = mixViewportPanelHash(hash, status.mirrorCanPreview);
    hash = mixViewportPanelHash(hash, status.mirrorPreviewValid);
    hash = mixViewportPanelHash(hash, status.mirrorDragging);
    hash = mixViewportPanelHash(hash, status.mirrorDraggingNewPoint);
    hash = mixViewportPanelHash(hash, status.mirrorDraggingLine);
    hash = mixViewportPanelHash(hash, static_cast<std::uint64_t>(status.mirrorDraggingPointIndex));
    hash = mixViewportPanelHash(hash, static_cast<std::uint64_t>(status.mirrorPointCount));
    hash = mixViewportPanelHash(hash, static_cast<std::uint64_t>(status.mirrorCapturedPointCount));
    hash = mixViewportPanelHash(hash, status.mirrorHasLine);
    for (const Vec3 point : status.mirrorPoints) {
        hash = mixViewportPanelHash(hash, point);
    }
    return hash;
}

std::uint32_t settingColorRgba(ViewportSettingColor color) {
    return static_cast<std::uint32_t>(normalizedToByte(color.r)) |
            (static_cast<std::uint32_t>(normalizedToByte(color.g)) << 8u) |
            (static_cast<std::uint32_t>(normalizedToByte(color.b)) << 16u) |
            (static_cast<std::uint32_t>(normalizedToByte(color.a)) << 24u);
}

std::uint32_t settingColor(const ViewportSettings& settings, ViewportColorSettingId id) {
    return settingColorRgba(settings.color(id));
}

void applyViewportSettingsToOverlayFrame(ViewportOverlayFrame& frame, const ViewportSettings& settings) {
    for (ViewportOverlayTriangle& triangle : frame.triangles) {
        switch (triangle.layer) {
        case ViewportOverlayLayer::SelectedVisibleFace:
        case ViewportOverlayLayer::SelectedXrayFace:
        case ViewportOverlayLayer::SelectedFaceMask:
            triangle.color = settingColor(settings, ViewportColorSettingId::SelectionFace);
            break;
        case ViewportOverlayLayer::HoverFace:
            triangle.color = viewportOverlayIsNativeRemoveColor(triangle.color)
                    ? settingColor(settings, ViewportColorSettingId::RemoveSelectionFace)
                    : settingColor(settings, ViewportColorSettingId::HoverFace);
            break;
        default:
            break;
        }
    }

    for (ViewportOverlayLine& line : frame.lines) {
        switch (line.layer) {
        case ViewportOverlayLayer::SourceWire:
            line.color = line.color == kOverlaySelectionWireColor
                    ? settingColor(settings, ViewportColorSettingId::SelectionWire)
                    : settingColor(settings, ViewportColorSettingId::SourceWire);
            line.widthPixels = settings.floatValue(ViewportFloatSettingId::SourceWireLineSize);
            break;
        case ViewportOverlayLayer::SelectedFaceEdge:
            line.color = settingColor(settings, ViewportColorSettingId::SelectionWire);
            line.widthPixels = settings.floatValue(ViewportFloatSettingId::SelectionFaceWireLineSize);
            break;
        case ViewportOverlayLayer::SelectedEdge:
            line.color = settingColor(settings, ViewportColorSettingId::SelectionWire);
            line.widthPixels = settings.floatValue(ViewportFloatSettingId::SelectionEdgeLineSize);
            break;
        case ViewportOverlayLayer::HoverFaceEdge:
        case ViewportOverlayLayer::HoverEdge:
            line.color = viewportOverlayIsNativeRemoveColor(line.color)
                    ? settingColor(settings, ViewportColorSettingId::RemoveSelectionWire)
                    : settingColor(settings, ViewportColorSettingId::HoverWire);
            line.widthPixels = settings.floatValue(ViewportFloatSettingId::HoverWireLineSize);
            break;
        case ViewportOverlayLayer::OpenEdge:
            line.color = settingColor(settings, ViewportColorSettingId::OpenEdge);
            line.widthPixels = settings.floatValue(ViewportFloatSettingId::OpenEdgeLineSize);
            break;
        case ViewportOverlayLayer::DiagnosticEdge:
            line.color = settingColor(settings, ViewportColorSettingId::DiagnosticEdge);
            line.widthPixels = settings.floatValue(ViewportFloatSettingId::DiagnosticEdgeLineSize);
            break;
        default:
            break;
        }
    }

    const float vertexSize = settings.floatValue(ViewportFloatSettingId::VertexSize);
    for (ViewportOverlayPoint& point : frame.points) {
        switch (point.layer) {
        case ViewportOverlayLayer::UnselectedVertex:
            point.color = settingColor(settings, ViewportColorSettingId::Vertex);
            point.sizePixels = vertexSize;
            break;
        case ViewportOverlayLayer::SelectedVertex:
            point.color = settingColor(settings, ViewportColorSettingId::SelectedVertex);
            point.sizePixels = vertexSize + settings.floatValue(ViewportFloatSettingId::SelectedVertexGrowth);
            break;
        case ViewportOverlayLayer::HoverVertex:
            point.color = viewportOverlayIsNativeRemoveColor(point.color)
                    ? settingColor(settings, ViewportColorSettingId::RemoveSelectionVertex)
                    : settingColor(settings, ViewportColorSettingId::HoverVertex);
            point.sizePixels = vertexSize + settings.floatValue(ViewportFloatSettingId::HoverVertexGrowth);
            break;
        case ViewportOverlayLayer::SnapTargetVertex:
            point.color = settingColor(settings, ViewportColorSettingId::HoverVertex);
            point.sizePixels = vertexSize + settings.floatValue(ViewportFloatSettingId::HoverVertexGrowth);
            break;
        default:
            break;
        }
    }
}

struct BoxPlacement {
    ModelingBounds localBounds;
    TransformComponent transform;
};

ViewportSelectionMode toViewportSelectionMode(ModelingSelectionMode mode);

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

std::uint64_t edgeKey(std::uint32_t a, std::uint32_t b) {
    if (b < a) {
        std::swap(a, b);
    }
    return (static_cast<std::uint64_t>(a) << 32u) | static_cast<std::uint64_t>(b);
}

bool entitySelected(const SceneDocument& document, EntityId entity) {
    const SelectionComponent* selection = document.selection(entity);
    return selection != nullptr && selection->selected;
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

void applyDocumentWorldRotationDeltaAroundPivot(
        const SceneDocument& document,
        EntityId entity,
        TransformComponent& transform,
        Vec3 worldRotationDeltaRadians,
        Vec3 localPivot,
        Vec3 worldPivot) {
    transform.rotationEuler = eulerRadiansAfterWorldRotationDelta(
            transform.rotationEuler,
            parentLocalVectorForWorldVector(document, entity, worldRotationDeltaRadians));
    pinDocumentLocalPivotToWorldPoint(document, entity, transform, localPivot, worldPivot);
}

void appendMeshSelectionWire(
        ViewportOverlayFrame& frame,
        const SceneDocument& document,
        const AssetRegistry& assets,
        EntityId entity) {
    if (!document.isEffectivelyVisible(entity)) {
        return;
    }

    const MeshComponent* meshComponent = document.mesh(entity);
    if (meshComponent == nullptr) {
        return;
    }

    const MeshAsset* mesh = assets.mesh(meshComponent->mesh);
    if (mesh == nullptr || mesh->vertices.empty() || mesh->indices.size() < 3u) {
        return;
    }

    const Mat4 worldFromLocal = document.worldTransformMatrix(entity);
    std::unordered_set<std::uint64_t> emittedEdges;
    emittedEdges.reserve(mesh->indices.size());

    auto appendEdge = [&](std::uint32_t a, std::uint32_t b) {
        if (a >= mesh->vertices.size() || b >= mesh->vertices.size() || a == b) {
            return;
        }
        const std::uint64_t key = edgeKey(a, b);
        if (!emittedEdges.insert(key).second) {
            return;
        }
        frame.lines.push_back(ViewportOverlayLine{
                .layer = ViewportOverlayLayer::SourceWire,
                .start = transformPoint(worldFromLocal, mesh->vertices[a].position),
                .end = transformPoint(worldFromLocal, mesh->vertices[b].position),
                .color = kOverlaySelectionWireColor,
                .widthPixels = 0.0f,
        });
    };

    for (std::size_t index = 0; index + 2u < mesh->indices.size(); index += 3u) {
        const std::uint32_t a = mesh->indices[index];
        const std::uint32_t b = mesh->indices[index + 1u];
        const std::uint32_t c = mesh->indices[index + 2u];
        appendEdge(a, b);
        appendEdge(b, c);
        appendEdge(c, a);
    }
}

void appendHierarchySelectionWires(
        ViewportOverlayFrame& frame,
        const SceneDocument& document,
        const AssetRegistry& assets) {
    for (const EntityId selected : document.selectedEntities()) {
        if (!document.hasChildren(selected)) {
            continue;
        }
        for (const EntityId descendant : document.descendants(selected, false)) {
            if (entitySelected(document, descendant)) {
                continue;
            }
            appendMeshSelectionWire(frame, document, assets, descendant);
        }
    }
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

bool viewportFocusInputActive() {
    const ImGuiIO& io = ImGui::GetIO();
    return ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
            ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
            ImGui::IsMouseClicked(ImGuiMouseButton_Middle) ||
            io.MouseWheel != 0.0f ||
            (ImGui::IsMouseDown(ImGuiMouseButton_Left) && (std::abs(io.MouseDelta.x) > 0.0f || std::abs(io.MouseDelta.y) > 0.0f));
}

ViewportOverlayCamera makeViewportOverlayCamera(const ViewportCameraController& camera, ImVec2 itemSize) {
    const ViewportCameraPose pose = camera.pose();
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

    const ViewportCameraState& state = camera.state();
    return {
            .eye = pose.eye,
            .forward = forward,
            .right = right,
            .up = up,
            .viewportSize = {std::max(itemSize.x, 1.0f), std::max(itemSize.y, 1.0f)},
            .projection = state.projection == ViewportProjection::Orthographic
                    ? ViewportOverlayProjection::Orthographic
                    : ViewportOverlayProjection::Perspective,
            .orthographicSize = state.orthographicSize,
            .fovDegrees = state.fovDegrees,
            .nearPlane = state.nearPlane,
            .farPlane = state.farPlane,
    };
}

ViewportInputFrame makeViewportInputFrame(
        bool hovered,
        bool focused,
        ImVec2 itemMin,
        ImVec2 itemSize,
        bool routeActiveNavigationInput,
        bool useCapturedMouseDelta,
        Vec2 capturedMouseDelta) {
    ImGuiIO& io = ImGui::GetIO();
    ViewportInputFrame input;
    input.viewportHovered = hovered;
    input.viewportFocused = focused;
    input.textInputActive = io.WantTextInput;
    input.middlePressed = (hovered || routeActiveNavigationInput) && ImGui::IsMouseClicked(ImGuiMouseButton_Middle);
    input.middleDown = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
    input.middleReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Middle);
    input.rightPressed = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    input.rightDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    input.rightReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Right);
    input.mouseWheel = (hovered || routeActiveNavigationInput) ? io.MouseWheel : 0.0f;
    input.deltaSeconds = std::max(io.DeltaTime, 0.0f);
    input.mouseScreen = {io.MousePos.x, io.MousePos.y};
    input.mouseDelta = useCapturedMouseDelta ? capturedMouseDelta : Vec2{io.MouseDelta.x, io.MouseDelta.y};
    input.mouseLocal = {io.MousePos.x - itemMin.x, io.MousePos.y - itemMin.y};
    input.viewportSize = {itemSize.x, itemSize.y};
    input.modifiers = {
            .primary = io.KeyCtrl || io.KeySuper,
            .shift = io.KeyShift,
            .alt = io.KeyAlt,
    };
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
    addPressedKey(input, ImGuiKey_A, ViewportKey::A);
    addPressedKey(input, ImGuiKey_D, ViewportKey::D);
    addPressedKey(input, ImGuiKey_H, ViewportKey::H);
    addPressedKey(input, ImGuiKey_I, ViewportKey::I);
    addPressedKey(input, ImGuiKey_M, ViewportKey::M);
    addPressedKey(input, ImGuiKey_R, ViewportKey::R);
    addPressedKey(input, ImGuiKey_S, ViewportKey::S);
    addPressedKey(input, ImGuiKey_B, ViewportKey::B);
    addPressedKey(input, ImGuiKey_C, ViewportKey::C);
    addPressedKey(input, ImGuiKey_P, ViewportKey::P);
    addPressedKey(input, ImGuiKey_T, ViewportKey::T);
    addPressedKey(input, ImGuiKey_V, ViewportKey::V);
    addPressedKey(input, ImGuiKey_X, ViewportKey::X);
    addPressedKey(input, ImGuiKey_Y, ViewportKey::Y);
    addPressedKey(input, ImGuiKey_Z, ViewportKey::Z);
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
    addPressedKey(input, ImGuiKey_Delete, ViewportKey::Delete);
    addPressedKey(input, ImGuiKey_Backspace, ViewportKey::Backspace);

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

std::optional<int> hitDrawnPlaneCapturedPoint(
        const std::array<Vec3, 3>& points,
        int capturedPointCount,
        Vec2 mouseLocal,
        const ViewportCameraController& camera,
        ImVec2 itemMin,
        ImVec2 itemSize) {
    constexpr float kPointHitRadiusPixels = 10.0f;
    const ImVec2 mouse{itemMin.x + mouseLocal.x, itemMin.y + mouseLocal.y};
    const int count = std::min(capturedPointCount, 2);
    for (int index = count - 1; index >= 0; --index) {
        const std::optional<ImVec2> screen =
                projectWorldToViewport(points[static_cast<std::size_t>(index)], camera, itemMin, itemSize);
        if (!screen) {
            continue;
        }
        if (distanceSquared(mouse, *screen) <= kPointHitRadiusPixels * kPointHitRadiusPixels) {
            return index;
        }
    }
    return std::nullopt;
}

bool hitDrawnPlaneLine(
        const std::array<Vec3, 3>& points,
        int capturedPointCount,
        bool hasLine,
        Vec2 mouseLocal,
        const ViewportCameraController& camera,
        ImVec2 itemMin,
        ImVec2 itemSize) {
    constexpr float kLineHitRadiusPixels = 10.0f;
    if (!hasLine || capturedPointCount < 2) {
        return false;
    }

    const std::optional<ImVec2> start = projectWorldToViewport(points[0], camera, itemMin, itemSize);
    const std::optional<ImVec2> end = projectWorldToViewport(points[1], camera, itemMin, itemSize);
    if (!start || !end) {
        return false;
    }

    const ImVec2 mouse{itemMin.x + mouseLocal.x, itemMin.y + mouseLocal.y};
    return distanceToSegmentSquared(mouse, *start, *end) <= kLineHitRadiusPixels * kLineHitRadiusPixels;
}

ModelingViewportCamera modelingViewportCamera(
        const ViewportCameraController& camera,
        Vec2 viewportSize) {
    const ViewportCameraPose pose = camera.pose();
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

    const ViewportCameraState& state = camera.state();
    return {
            .orthographic = state.projection == ViewportProjection::Orthographic,
            .origin = pose.eye,
            .xAxis = right,
            .yAxis = up,
            .zAxis = forward * -1.0f,
            .viewportSize = viewportSize,
            .orthographicSize = state.orthographicSize,
            .fovDegrees = state.fovDegrees,
    };
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
            .hasCamera = true,
            .camera = modelingViewportCamera(camera, input.viewportSize),
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

    const ImU32 color = IM_COL32(255, 235, 41, 209);
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

bool applyDefaultMaterialToSelection(SceneDocument& document, AssetId defaultMaterial) {
    if (defaultMaterial == InvalidAssetId) {
        return true;
    }
    bool applied = false;
    for (const EntityId entity : document.selectedEntities()) {
        document.setMaterial(entity, MaterialComponent{.material = defaultMaterial});
        applied = true;
    }
    return applied;
}

UndoRedoResult modelingResult(ModelingCommandResult result) {
    return toUndoRedoResult(std::move(result));
}

UndoRedoResult createBoxResult(ModelingCreateBoxResult result) {
    return {
            .ok = result.ok,
            .changed = result.changed,
            .message = std::move(result.message),
    };
}

bool commitBoxPlacement(UndoRedoManager& undoRedo,
        ModelingSessionService& modeling,
        AssetId defaultMaterial,
        SceneDocument& document,
        const BoxPlacement& placement,
        const std::string& name) {
    auto applyNativeState = [&]() {
        const std::vector<EntityId> selected = document.selectedEntities();
        if (selected.empty()) {
            return;
        }
        const EntityId entity = selected.front();
        document.setTransform(entity, placement.transform);
        if (defaultMaterial != InvalidAssetId) {
            document.setMaterial(entity, MaterialComponent{.material = defaultMaterial});
        }
        document.clearSelection();
        document.setSelected(entity, true);
    };

    const UndoRedoResult result = undoRedo.executeExternal(
            "Create Box",
            [&]() {
                const ModelingCreateBoxResult created = modeling.createBoxFromBounds({
                        .bounds = placement.localBounds,
                        .name = name,
                });
                if (created.ok && created.changed) {
                    document.setTransform(created.object.entity, placement.transform);
                    if (defaultMaterial != InvalidAssetId) {
                        document.setMaterial(created.object.entity, MaterialComponent{.material = defaultMaterial});
                    }
                    document.clearSelection();
                    document.setSelected(created.object.entity, true);
                }
                return createBoxResult(std::move(created));
            },
            [&]() {
                return modelingResult(modeling.undo());
            },
            [&]() {
                UndoRedoResult redo = modelingResult(modeling.redo());
                if (redo.ok && redo.changed) {
                    applyNativeState();
                }
                return redo;
            });

    if (!result.ok) {
        LOG_ERROR("Box creation failed: {}", result.message);
        return false;
    }
    return result.changed;
}

bool commitBoxPreview(UndoRedoManager& undoRedo,
        ModelingSessionService& modeling,
        AssetId defaultMaterial,
        SceneDocument& document,
        const std::string& name) {
    const UndoRedoResult result = undoRedo.executeExternal(
            "Create Box",
            [&]() {
                const ModelingCreateBoxResult committed = modeling.commitBox(name);
                if (committed.ok && committed.changed) {
                    if (defaultMaterial != InvalidAssetId) {
                        document.setMaterial(committed.object.entity, MaterialComponent{.material = defaultMaterial});
                    }
                    document.clearSelection();
                    document.setSelected(committed.object.entity, true);
                }
                return createBoxResult(std::move(committed));
            },
            [&]() {
                return modelingResult(modeling.undo());
            },
            [&]() {
                UndoRedoResult redo = modelingResult(modeling.redo());
                if (redo.ok && redo.changed) {
                    applyDefaultMaterialToSelection(document, defaultMaterial);
                }
                return redo;
            });
    if (!result.ok) {
        LOG_ERROR("Box commit failed: {}", result.message);
        return false;
    }
    return result.changed;
}

void drawViewportOperationsPopup(
        UndoRedoManager& undoRedo,
        ModelingSessionService& modeling,
        ViewportInputRouter& inputRouter,
        bool modalToolOwnsViewport) {
    if (!ImGui::BeginPopup("ViewportOperationsContext")) {
        return;
    }

    const ModelingActionQuery query{
            .surface = ModelingActionSurface::ViewportContextMenu,
            .modalToolOwnsViewport = modalToolOwnsViewport,
            .includeDisabled = true,
            .includeHidden = false,
    };
    const std::vector<ModelingActionItem> actions = modeling.contextActions(query);
    for (const ModelingActionItem& action : actions) {
        const bool enabled = action.visibility == ModelingActionVisibility::Enabled;
        if (ImGui::MenuItem(action.label.c_str(), nullptr, false, enabled)) {
            const ModelingCommandResult result = action.previewKind != ModelingActionPreviewKind::None ?
                    modeling.executeContextAction(action.actionId) :
                    executeTrackedModelingCommand(
                            undoRedo,
                            modeling,
                            action.label,
                            [&]() {
                                return modeling.executeContextAction(action.actionId);
                            });
            if (!result.ok) {
                LOG_ERROR("Operation '{}' failed: {}", action.actionId, result.message.empty() ? "Operation failed." : result.message);
            }
            inputRouter.setSelectionMode(toViewportSelectionMode(modeling.selectionSummary().mode));
        }
        if (!enabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !action.disabledReason.empty()) {
            ImGui::SetTooltip("%s", action.disabledReason.c_str());
        }
    }

    ImGui::EndPopup();
}

void drawFlyNavigationOverlay(
        ImDrawList* drawList,
        ImVec2 itemMin,
        ImVec2 itemSize,
        bool drawCrosshair,
        bool drawSpeed,
        bool drawResetHint,
        float speed) {
    const ImVec2 center{itemMin.x + itemSize.x * 0.5f, itemMin.y + itemSize.y * 0.5f};
    if (drawCrosshair) {
        const float gap = 4.0f;
        const float length = 9.0f;
        const ImU32 shadow = ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.65f));
        const ImU32 color = ImGui::GetColorU32(ImVec4(0.94f, 0.96f, 1.0f, 0.88f));
        drawList->AddLine({center.x - length - 1.0f, center.y}, {center.x - gap - 1.0f, center.y}, shadow, 2.0f);
        drawList->AddLine({center.x + gap + 1.0f, center.y}, {center.x + length + 1.0f, center.y}, shadow, 2.0f);
        drawList->AddLine({center.x, center.y - length - 1.0f}, {center.x, center.y - gap - 1.0f}, shadow, 2.0f);
        drawList->AddLine({center.x, center.y + gap + 1.0f}, {center.x, center.y + length + 1.0f}, shadow, 2.0f);
        drawList->AddLine({center.x - length, center.y}, {center.x - gap, center.y}, color, 1.25f);
        drawList->AddLine({center.x + gap, center.y}, {center.x + length, center.y}, color, 1.25f);
        drawList->AddLine({center.x, center.y - length}, {center.x, center.y - gap}, color, 1.25f);
        drawList->AddLine({center.x, center.y + gap}, {center.x, center.y + length}, color, 1.25f);
    }

    if (!drawSpeed) {
        return;
    }

    char label[96]{};
    if (drawResetHint) {
        std::snprintf(label, sizeof(label), "Speed %.1f (Middle Mouse to reset)", speed);
    } else {
        std::snprintf(label, sizeof(label), "Speed %.1f", speed);
    }
    const ImVec2 textSize = ImGui::CalcTextSize(label);
    const ImVec2 padding{10.0f, 5.0f};
    const ImVec2 min{
            itemMin.x + itemSize.x * 0.5f - textSize.x * 0.5f - padding.x,
            itemMin.y + itemSize.y - 36.0f,
    };
    const ImVec2 max{min.x + textSize.x + padding.x * 2.0f, min.y + textSize.y + padding.y * 2.0f};
    drawList->AddRectFilled(min, max, ImGui::GetColorU32(ImVec4(0.05f, 0.06f, 0.07f, 0.78f)), 4.0f);
    drawList->AddText({min.x + padding.x, min.y + padding.y}, ImGui::GetColorU32(ImVec4(0.92f, 0.95f, 1.0f, 1.0f)), label);
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

Vec2 fromSdk(quader_poly::Vec2 value) {
    return {value.x, value.y};
}

ModelingTransform3 fromSdk(quader_poly::Transform3 value) {
    return {
            .xAxis = fromSdk(value.x_axis),
            .yAxis = fromSdk(value.y_axis),
            .zAxis = fromSdk(value.z_axis),
            .origin = fromSdk(value.origin),
    };
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

ModelingViewportTool toModelingViewportTool(ViewportTool tool) {
    switch (tool) {
    case ViewportTool::Move:
        return ModelingViewportTool::Move;
    case ViewportTool::Rotate:
        return ModelingViewportTool::Rotate;
    case ViewportTool::Scale:
        return ModelingViewportTool::Scale;
    case ViewportTool::Box:
        return ModelingViewportTool::Box;
    case ViewportTool::InsertEdgeLoop:
        return ModelingViewportTool::InsertEdgeLoop;
    case ViewportTool::Knife:
        return ModelingViewportTool::Knife;
    case ViewportTool::Cut:
        return ModelingViewportTool::Cut;
    case ViewportTool::Poly:
        return ModelingViewportTool::Poly;
    case ViewportTool::Mirror:
        return ModelingViewportTool::Mirror;
    case ViewportTool::Select:
        return ModelingViewportTool::Select;
    }
    return ModelingViewportTool::Select;
}

ViewportTool fromModelingViewportTool(ModelingViewportTool tool) {
    switch (tool) {
    case ModelingViewportTool::Move:
        return ViewportTool::Move;
    case ModelingViewportTool::Rotate:
        return ViewportTool::Rotate;
    case ModelingViewportTool::Scale:
        return ViewportTool::Scale;
    case ModelingViewportTool::Box:
        return ViewportTool::Box;
    case ModelingViewportTool::InsertEdgeLoop:
        return ViewportTool::InsertEdgeLoop;
    case ModelingViewportTool::Knife:
        return ViewportTool::Knife;
    case ModelingViewportTool::Cut:
        return ViewportTool::Cut;
    case ModelingViewportTool::Poly:
        return ViewportTool::Poly;
    case ModelingViewportTool::Mirror:
        return ViewportTool::Mirror;
    case ModelingViewportTool::Select:
        return ViewportTool::Select;
    }
    return ViewportTool::Select;
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

ModelingTransformAxis toModelingTransformAxis(ViewportPanel::GizmoAxis axis) {
    switch (axis) {
    case ViewportPanel::GizmoAxis::X:
        return ModelingTransformAxis::X;
    case ViewportPanel::GizmoAxis::Y:
        return ModelingTransformAxis::Y;
    case ViewportPanel::GizmoAxis::Z:
        return ModelingTransformAxis::Z;
    case ViewportPanel::GizmoAxis::XY:
        return ModelingTransformAxis::XY;
    case ViewportPanel::GizmoAxis::XZ:
        return ModelingTransformAxis::XZ;
    case ViewportPanel::GizmoAxis::YZ:
        return ModelingTransformAxis::YZ;
    case ViewportPanel::GizmoAxis::All:
        return ModelingTransformAxis::All;
    case ViewportPanel::GizmoAxis::None:
        return ModelingTransformAxis::None;
    }
    return ModelingTransformAxis::None;
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
        float gizmoSizeScale,
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

ViewportPanel::ViewportPanel() {
    panes_[viewportPaneIndex(ViewportPaneId::Perspective)].id = ViewportPaneId::Perspective;
    panes_[viewportPaneIndex(ViewportPaneId::Top)].id = ViewportPaneId::Top;
    panes_[viewportPaneIndex(ViewportPaneId::Front)].id = ViewportPaneId::Front;
    panes_[viewportPaneIndex(ViewportPaneId::Side)].id = ViewportPaneId::Side;
    for (Pane& paneState : panes_) {
        resetPaneCamera(paneState);
    }
}

ViewportPanel::~ViewportPanel() {
    shutdown();
}

void ViewportPanel::initialize(RenderSystem& renderSystem) {
    for (Pane& paneState : panes_) {
        if (paneState.initialized) {
            continue;
        }
        paneState.renderer.initialize(renderSystem.engine(),
                renderSystem.scene(),
                renderSystem.textures(),
                renderSystem.groundGridMaterial(),
                viewportGridLayerMask(viewportPaneIndex(paneState.id)));
        paneState.initialized = true;
    }
}

void ViewportPanel::shutdown() {
    for (Pane& paneState : panes_) {
        paneState.renderer.shutdown();
        paneState.initialized = false;
        paneState.visibleThisFrame = false;
    }
    activeRenderPanes_.clear();
}

void ViewportPanel::prepareViewports(SceneRenderBridge& renderBridge) {
    const float gridWorldSize = viewportGridWorldSizeForPreset(inputRouter_.state().gridPreset);
    const ViewportSettings& settings = activeViewportSettings();
    for (ViewportPaneId paneId : activeRenderPanes_) {
        Pane& paneState = pane(paneId);
        if (!paneState.initialized || !paneState.visibleThisFrame) {
            continue;
        }
        paneState.renderer.updateCamera(paneState.camera);
        paneState.renderer.updateGroundGrid(paneState.camera, gridWorldSize, settings);
        paneState.renderer.setOverlayVisible(true);
        ViewportOverlayFrame overlayFrame = cachedSelectionOverlayFrameValid_ ? cachedSelectionOverlayFrame_ : ViewportOverlayFrame{};
        applyViewportSettingsToOverlayFrame(overlayFrame, settings);
        renderBridge.setOverlayFrame(
                viewportPaneIndex(paneId),
                viewportGridLayerMask(viewportPaneIndex(paneId)),
                std::move(overlayFrame),
                makeViewportOverlayCamera(paneState.camera, {paneState.renderSize.x, paneState.renderSize.y}));
    }
}

void ViewportPanel::renderViewports(filament::Renderer& renderer) {
    for (ViewportPaneId paneId : activeRenderPanes_) {
        Pane& paneState = pane(paneId);
        if (!paneState.initialized || !paneState.visibleThisFrame) {
            continue;
        }
        paneState.renderer.render(renderer);
    }
}

void ViewportPanel::setViewportLayout(ViewportLayoutId layoutId) {
    if (layoutId_ == layoutId) {
        return;
    }
    layoutId_ = layoutId;
    inputRouter_.setMaximized(false);
    const std::vector<ViewportPanePlacement> placements = activePanePlacements();
    if (std::none_of(placements.begin(), placements.end(), [&](const ViewportPanePlacement& placement) {
                return placement.paneId == focusedPaneId_;
            })) {
        focusedPaneId_ = ViewportPaneId::Perspective;
    }
}

bool ViewportPanel::activateToolFromMenu(ViewportTool tool, ModelingSessionService& modeling) {
    LOG_INFO("Tools menu requested viewport tool {}", viewportToolId(tool));
    const ModelingCommandResult command = modeling.activateViewportTool(toModelingViewportTool(tool));
    if (!command.ok) {
        LOG_ERROR("Viewport tool activation failed: {}", command.message.empty() ? "Command failed." : command.message);
        return false;
    }
    const ViewportTool activeTool = fromModelingViewportTool(modeling.toolStatus().activeTool);
    LOG_INFO("Viewport tool activated from menu: requested {}, active {}", viewportToolId(tool), viewportToolId(activeTool));
    inputRouter_.setActiveTool(activeTool);
    inputRouter_.setSelectionMode(toViewportSelectionMode(modeling.selectionSummary().mode));
    boxPreview_ = {};
    insertEdgeLoopDragActive_ = false;
    cutDragActive_ = false;
    mirrorDragActive_ = false;
    paintSelectionActive_ = false;
    selectionHoverClearIssued_ = false;
    lastHoverWorkKey_.valid = false;
    cachedSelectionOverlayFrameDirty_ = true;
    return true;
}

ViewportPanel::Pane& ViewportPanel::pane(ViewportPaneId paneId) {
    return panes_[viewportPaneIndex(paneId)];
}

const ViewportPanel::Pane& ViewportPanel::pane(ViewportPaneId paneId) const {
    return panes_[viewportPaneIndex(paneId)];
}

std::vector<ViewportPanePlacement> ViewportPanel::activePanePlacements() const {
    if (inputRouter_.state().maximized) {
        return {{
                .paneId = focusedPaneId_,
                .normalizedRect = {},
        }};
    }
    return viewportLayoutPlacements(layoutId_, layoutState_);
}

std::vector<ViewportSplitHandle> ViewportPanel::activeSplitHandles() const {
    if (inputRouter_.state().maximized) {
        return {};
    }
    return viewportLayoutSplitHandles(layoutId_, layoutState_);
}

void ViewportPanel::resetPaneCamera(Pane& paneState) {
    paneState.camera = ViewportCameraController{};
    switch (viewportViewForPane(paneState.id)) {
    case ViewportView::Top:
        paneState.camera.focusOrigin(64.0f);
        paneState.camera.viewAxis(ViewportViewAxis::Y, 1);
        break;
    case ViewportView::Bottom:
        paneState.camera.focusOrigin(64.0f);
        paneState.camera.viewAxis(ViewportViewAxis::Y, -1);
        break;
    case ViewportView::Front:
        paneState.camera.focusOrigin(64.0f);
        paneState.camera.viewAxis(ViewportViewAxis::Z, -1);
        break;
    case ViewportView::Back:
        paneState.camera.focusOrigin(64.0f);
        paneState.camera.viewAxis(ViewportViewAxis::Z, 1);
        break;
    case ViewportView::Side:
        paneState.camera.focusOrigin(64.0f);
        paneState.camera.viewAxis(ViewportViewAxis::X, 1);
        break;
    case ViewportView::Left:
        paneState.camera.focusOrigin(64.0f);
        paneState.camera.viewAxis(ViewportViewAxis::X, -1);
        break;
    case ViewportView::Perspective:
        break;
    }
}

void ViewportPanel::drawPaneChrome(const Pane& paneState, ImVec2 itemMin, ImVec2 itemSize) const {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 itemMax{itemMin.x + itemSize.x, itemMin.y + itemSize.y};
    const ImU32 dividerColor = IM_COL32(19, 19, 20, 255);
    const ImU32 focusedColor = IM_COL32(46, 46, 48, 242);
    drawList->AddRect(itemMin, itemMax, paneState.id == focusedPaneId_ ? focusedColor : dividerColor, 0.0f, 0, 1.0f);
    const std::string_view label = viewportPaneLabel(paneState.id);
    const ImVec2 textSize = ImGui::CalcTextSize(label.data());
    const ImVec2 labelMin{itemMin.x + 8.0f, itemMin.y + 7.0f};
    const ImVec2 labelMax{labelMin.x + textSize.x + 12.0f, labelMin.y + textSize.y + 6.0f};
    drawList->AddRectFilled(labelMin, labelMax, IM_COL32(16, 18, 21, 176), 3.0f);
    drawList->AddText({labelMin.x + 6.0f, labelMin.y + 3.0f}, IM_COL32(228, 231, 236, 230), label.data());
}

bool ViewportPanel::handleSplitInput(ImVec2 viewportMin, ImVec2 viewportSize, const std::vector<ViewportSplitHandle>& handles) {
    constexpr float hitThickness = 7.0f;
    bool consumed = false;
    auto handleRect = [&](const ViewportSplitHandle& handle, float thickness) {
        const float half = thickness * 0.5f;
        if (handle.axis == ViewportSplitHandleAxis::Vertical) {
            const float x = viewportMin.x + viewportSize.x * handle.position;
            const float y = viewportMin.y + viewportSize.y * handle.start;
            const float height = viewportSize.y * std::max(0.0f, handle.end - handle.start);
            return ImRect{{x - half, y}, {x + half, y + height}};
        }
        const float y = viewportMin.y + viewportSize.y * handle.position;
        const float x = viewportMin.x + viewportSize.x * handle.start;
        const float width = viewportSize.x * std::max(0.0f, handle.end - handle.start);
        return ImRect{{x, y - half}, {x + width, y + half}};
    };

    for (const ViewportSplitHandle& handle : handles) {
        const ImRect hit = handleRect(handle, hitThickness);
        if (hit.Contains(ImGui::GetMousePos())) {
            ImGui::SetMouseCursor(handle.axis == ViewportSplitHandleAxis::Vertical
                            ? ImGuiMouseCursor_ResizeEW
                            : ImGuiMouseCursor_ResizeNS);
        }
    }

    if (!splitHandleDragging_ && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        for (auto it = handles.rbegin(); it != handles.rend(); ++it) {
            if (handleRect(*it, hitThickness).Contains(ImGui::GetMousePos())) {
                activeSplitHandle_ = *it;
                splitHandleDragging_ = true;
                consumed = true;
                break;
            }
        }
    }

    if (splitHandleDragging_ && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const ImVec2 mouse = ImGui::GetMousePos();
        const float normalizedPosition = activeSplitHandle_.axis == ViewportSplitHandleAxis::Vertical
                ? std::clamp((mouse.x - viewportMin.x) / std::max(viewportSize.x, 1.0f), 0.0f, 1.0f)
                : std::clamp((mouse.y - viewportMin.y) / std::max(viewportSize.y, 1.0f), 0.0f, 1.0f);
        layoutState_.setHandlePosition(activeSplitHandle_.id, normalizedPosition);
        consumed = true;
    }
    if (splitHandleDragging_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        splitHandleDragging_ = false;
        consumed = true;
    }

    if (ImGui::GetCurrentWindow()->DrawList != nullptr) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        for (const ViewportSplitHandle& handle : handles) {
            const ImRect line = handleRect(handle, 1.0f);
            drawList->AddRectFilled(line.Min, line.Max, IM_COL32(31, 31, 33, 255));
        }
    }
    return consumed || splitHandleDragging_;
}

ImVec2 ViewportPanel::paneRectMin(ImVec2 viewportMin, ImVec2 viewportSize, ViewportNormalizedRect normalizedRect) {
    const ViewportNormalizedRect absolute = viewportAbsolutePaneRect(
            {.x = viewportMin.x, .y = viewportMin.y, .width = viewportSize.x, .height = viewportSize.y},
            normalizedRect);
    return {absolute.x, absolute.y};
}

ImVec2 ViewportPanel::paneRectSize(ImVec2 viewportSize, ViewportNormalizedRect normalizedRect) {
    const ViewportNormalizedRect absolute = viewportAbsolutePaneRect(
            {.x = 0.0f, .y = 0.0f, .width = viewportSize.x, .height = viewportSize.y},
            normalizedRect);
    return {absolute.width, absolute.height};
}

const ViewportSettings& ViewportPanel::activeViewportSettings() const {
    return viewportSettings_ != nullptr ? *viewportSettings_ : fallbackViewportSettings_;
}

void ViewportPanel::draw(UIContext& context) {
    viewportSettings_ = &context.viewportSettings;
    draw(context.renderBridge,
            context.document,
            context.assets,
            context.undoRedo,
            context.documentCommands,
            context.modeling,
            context.defaultMaterial,
            context.viewportRelativeMouseDelta,
            context.viewportCursorCaptureRequested,
            context.viewportCursorCapturePoint,
            context);
}

void ViewportPanel::draw(SceneRenderBridge& renderBridge,
        SceneDocument& document,
        AssetRegistry& assets,
        UndoRedoManager& undoRedo,
        DocumentCommandService& documentCommands,
        ModelingSessionService& modeling,
        AssetId defaultMaterial,
        Vec2 capturedMouseDelta,
        bool* viewportCursorCaptureRequested,
        Vec2* viewportCursorCapturePoint,
        const UIContext& uiContext) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    const bool visible = ImGui::Begin(
            "Viewport",
            nullptr,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(2);
    if (!visible) {
        for (Pane& paneState : panes_) {
            renderBridge.setOverlayFrame(
                    viewportPaneIndex(paneState.id),
                    viewportGridLayerMask(viewportPaneIndex(paneState.id)),
                    {},
                    makeViewportOverlayCamera(paneState.camera, {1.0f, 1.0f}));
        }
        lastFlyNavigationActive_ = false;
        for (Pane& paneState : panes_) {
            paneState.visibleThisFrame = false;
        }
        activeRenderPanes_.clear();
        ImGui::End();
        return;
    }

    ImVec2 available = ImGui::GetContentRegionAvail();
    available.x = std::max(available.x, 1.0f);
    available.y = std::max(available.y, 1.0f);
    const ImVec2 canvasMin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("ViewportLayoutCanvas", available);

    for (Pane& paneState : panes_) {
        paneState.visibleThisFrame = false;
    }
    activeRenderPanes_.clear();

    struct DrawPane {
        Pane* pane = nullptr;
        ImVec2 itemMin = {};
        ImVec2 itemSize = {};
        bool hovered = false;
        bool focusHovered = false;
    };
    std::vector<DrawPane> drawPanes;
    const std::vector<ViewportPanePlacement> placements = activePanePlacements();
    drawPanes.reserve(placements.size());
    for (const ViewportPanePlacement& placement : placements) {
        Pane& paneState = pane(placement.paneId);
        const ImVec2 itemMin = paneRectMin(canvasMin, available, placement.normalizedRect);
        const ImVec2 itemSize = paneRectSize(available, placement.normalizedRect);
        paneState.renderSize = {std::max(itemSize.x, 1.0f), std::max(itemSize.y, 1.0f)};
        paneState.visibleThisFrame = true;
        paneState.renderer.resize(
                static_cast<std::uint32_t>(paneState.renderSize.x),
                static_cast<std::uint32_t>(paneState.renderSize.y));
        activeRenderPanes_.push_back(placement.paneId);

        ImGui::SetCursorScreenPos(itemMin);
        ImGui::PushID(static_cast<int>(viewportPaneIndex(placement.paneId)));
        ImGui::Image(paneState.renderer.textureId(), itemSize, {0.0f, 1.0f}, {1.0f, 0.0f});
        DrawPane drawPane{
                .pane = &paneState,
                .itemMin = ImGui::GetItemRectMin(),
                .itemSize = {ImGui::GetItemRectSize().x, ImGui::GetItemRectSize().y},
                .hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem),
                .focusHovered = ImGui::IsItemHovered(),
        };
        drawPanes.push_back(drawPane);
        ImGui::PopID();
    }

    const std::vector<ViewportSplitHandle> splitHandles = activeSplitHandles();
    const bool splitConsumed = handleSplitInput(canvasMin, available, splitHandles);
    DrawPane* inputPane = nullptr;
    const bool gizmoDragLocksInput = gizmoDragging_ && gizmoDragPaneActive_;
    const bool navigationGizmoDragLocksInput = navigationGizmo_.inputLocked();
    for (DrawPane& drawPane : drawPanes) {
        if (!gizmoDragLocksInput && !navigationGizmoDragLocksInput) {
            if (drawPane.focusHovered && !splitConsumed) {
                focusedPaneId_ = drawPane.pane->id;
            } else if (drawPane.focusHovered && viewportFocusInputActive()) {
                focusedPaneId_ = drawPane.pane->id;
            }
        }
        if (gizmoDragLocksInput && drawPane.pane->id == gizmoDragPaneId_) {
            inputPane = &drawPane;
        } else if (navigationGizmoDragLocksInput && drawPane.pane->id == navigationGizmo_.lockedPaneId()) {
            inputPane = &drawPane;
        } else if (!gizmoDragLocksInput && !navigationGizmoDragLocksInput && drawPane.hovered && !splitConsumed) {
            inputPane = &drawPane;
        }
        drawPaneChrome(*drawPane.pane, drawPane.itemMin, drawPane.itemSize);
    }
    if (inputPane == nullptr) {
        for (DrawPane& drawPane : drawPanes) {
            if (drawPane.pane->id == focusedPaneId_) {
                inputPane = &drawPane;
                break;
            }
        }
    }
    if (inputPane != nullptr) {
        const bool inputOwnsActiveGizmoDrag =
                gizmoDragLocksInput && inputPane->pane->id == gizmoDragPaneId_;
        const bool inputOwnsActiveNavigationGizmoDrag =
                navigationGizmoDragLocksInput && inputPane->pane->id == navigationGizmo_.lockedPaneId();
        drawPaneContents(*inputPane->pane,
                document,
                assets,
                undoRedo,
                documentCommands,
                modeling,
                defaultMaterial,
                capturedMouseDelta,
                viewportCursorCaptureRequested,
                viewportCursorCapturePoint,
                uiContext,
                inputPane->itemMin,
                inputPane->itemSize,
                (inputPane->hovered && !splitConsumed) || inputOwnsActiveGizmoDrag || inputOwnsActiveNavigationGizmoDrag,
                (inputPane->focusHovered && !splitConsumed) || inputOwnsActiveGizmoDrag || inputOwnsActiveNavigationGizmoDrag);
    }
    for (DrawPane& drawPane : drawPanes) {
        if (&drawPane == inputPane) {
            continue;
        }
        drawPaneDecorations(
                *drawPane.pane,
                document,
                assets,
                undoRedo,
                documentCommands,
                modeling,
                drawPane.itemMin,
                drawPane.itemSize);
    }
    refreshCachedSelectionOverlayFrame(document, assets, modeling);
    ImGui::SetCursorScreenPos({canvasMin.x, canvasMin.y + available.y});
    ImGui::End();
}

void ViewportPanel::drawPaneDecorations(
        Pane& paneState,
        SceneDocument& document,
        const AssetRegistry& assets,
        UndoRedoManager& undoRedo,
        DocumentCommandService& documentCommands,
        ModelingSessionService& modeling,
        ImVec2 itemMin,
        ImVec2 itemSize) {
    navigationGizmo_.draw(paneState.camera, paneState.id, itemMin, itemSize, {}, nullptr, nullptr, false);
    (void)drawTransformGizmo(
            document,
            assets,
            undoRedo,
            documentCommands,
            modeling,
            paneState.camera,
            paneState.id,
            itemMin,
            itemSize,
            true,
            false);
}

void ViewportPanel::refreshCachedSelectionOverlayFrame(
        const SceneDocument& document,
        const AssetRegistry& assets,
        ModelingSessionService& modeling) {
    const ModelingSelectionSummary overlaySummary = modeling.selectionSummary();
    const std::uint64_t overlayToolSignature = toolOverlaySignature(modeling);
    const bool overlayRevisionChanged =
            !cachedSelectionOverlayFrameValid_ ||
            cachedSelectionOverlayContentRevision_ != overlaySummary.contentRevision ||
            cachedSelectionOverlaySelectionRevision_ != overlaySummary.selectionRevision ||
            cachedSelectionOverlaySceneRevision_ != document.revision() ||
            cachedSelectionOverlayAssetRevision_ != assets.revision() ||
            cachedSelectionOverlayToolSignature_ != overlayToolSignature ||
            cachedSelectionOverlayMode_ != overlaySummary.mode;
    if (!cachedSelectionOverlayFrameDirty_ && !overlayRevisionChanged) {
        return;
    }

    cachedSelectionOverlayFrame_ = modeling.selectionOverlayFrame();
    appendHierarchySelectionWires(cachedSelectionOverlayFrame_, document, assets);
    cachedSelectionOverlayFrameValid_ = true;
    cachedSelectionOverlayFrameDirty_ = false;
    cachedSelectionOverlayContentRevision_ = overlaySummary.contentRevision;
    cachedSelectionOverlaySelectionRevision_ = overlaySummary.selectionRevision;
    cachedSelectionOverlaySceneRevision_ = document.revision();
    cachedSelectionOverlayAssetRevision_ = assets.revision();
    cachedSelectionOverlayToolSignature_ = overlayToolSignature;
    cachedSelectionOverlayMode_ = overlaySummary.mode;
}

void ViewportPanel::drawPaneContents(Pane& paneState,
        SceneDocument& document,
        AssetRegistry& assets,
        UndoRedoManager& undoRedo,
        DocumentCommandService& documentCommands,
        ModelingSessionService& modeling,
        AssetId defaultMaterial,
        Vec2 capturedMouseDelta,
        bool* viewportCursorCaptureRequested,
        Vec2* viewportCursorCapturePoint,
        const UIContext& uiContext,
        ImVec2 itemMin,
        ImVec2 itemSize,
        bool hovered,
        bool focusHovered) {
    ViewportCameraController& camera = paneState.camera;
    const ImVec2 itemMax{itemMin.x + itemSize.x, itemMin.y + itemSize.y};
    const bool focusRequestedThisFrame = focusHovered && viewportFocusInputActive();
    if (focusRequestedThisFrame) {
        uiContext.requestPanelFocus(UIPanelId::Viewport);
        ImGui::SetWindowFocus();
    }
    const bool appFocused =
            uiContext.focusedPanel == UIPanelId::Viewport ||
            (uiContext.focusedPanelStorage != nullptr && *uiContext.focusedPanelStorage == UIPanelId::Viewport);
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || appFocused || focusRequestedThisFrame;
    auto modalToolOwnsViewport = [&]() {
        return inputRouter_.state().activeTool == ViewportTool::Box ||
                inputRouter_.state().activeTool == ViewportTool::InsertEdgeLoop ||
                inputRouter_.state().activeTool == ViewportTool::Knife ||
                inputRouter_.state().activeTool == ViewportTool::Cut ||
                inputRouter_.state().activeTool == ViewportTool::Mirror ||
                inputRouter_.state().activeTool == ViewportTool::Poly ||
                hasActiveBoxPreview(boxPreview_) ||
                modeling.modalToolOwnsViewport();
    };

    const bool useCapturedMouseDelta = inputRouter_.flyCursorCaptureActive();
    const ViewportInputFrame input =
            makeViewportInputFrame(hovered, focused, itemMin, itemSize, inputRouter_.navigating(), useCapturedMouseDelta, capturedMouseDelta);
    const ViewportInputResult result = inputRouter_.handleFrame(input, camera);
    bool operationPreviewShortcutConsumed = false;
    const ModelingToolSettingsTarget toolSettingsTarget = modeling.toolSettingsTarget();
    if (result.shortcutHandled && toolSettingsTarget.operationPreviewActive) {
        if (result.shortcut.kind == ViewportShortcutKind::ToggleMaximized) {
            inputRouter_.setMaximized(!inputRouter_.state().maximized);
            const ModelingCommandResult command = executeTrackedModelingCommand(
                    undoRedo,
                    modeling,
                    toolSettingsTarget.label.empty() ? std::string("Apply Tool Preview") : std::string("Apply ") + toolSettingsTarget.label,
                    [&]() {
                        return modeling.commitActiveOperationPreview();
                    });
            if (!command.ok) {
                LOG_ERROR("Tool preview apply failed: {}", command.message.empty() ? "Command failed." : command.message);
            }
            operationPreviewShortcutConsumed = true;
            lastHoverWorkKey_.valid = false;
            cachedSelectionOverlayFrameDirty_ = true;
        } else if (result.shortcut.kind == ViewportShortcutKind::CancelActiveTool) {
            const ModelingCommandResult command = modeling.cancelActiveOperationPreview();
            if (!command.ok) {
                LOG_ERROR("Tool preview cancel failed: {}", command.message.empty() ? "Command failed." : command.message);
            }
            operationPreviewShortcutConsumed = true;
            lastHoverWorkKey_.valid = false;
            cachedSelectionOverlayFrameDirty_ = true;
        }
    }
    if (hovered &&
            result.shortcutHandled &&
            !operationPreviewShortcutConsumed &&
            result.shortcut.kind == ViewportShortcutKind::ToggleMaximized) {
        focusedPaneId_ = paneState.id;
    }
    if (input.rightPressed && hovered && !input.modifiers.primary && !input.modifiers.shift && !input.modifiers.alt) {
        flyCursorCaptureScreen_ = input.mouseScreen;
    }
    lastFlyNavigationActive_ = result.flyCursorCaptureActive;
    if (result.flyCursorCaptureActive) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
        if (viewportCursorCaptureRequested != nullptr) {
            *viewportCursorCaptureRequested = true;
        }
        if (viewportCursorCapturePoint != nullptr) {
            const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
            *viewportCursorCapturePoint = {
                    flyCursorCaptureScreen_.x - mainViewport->Pos.x,
                    flyCursorCaptureScreen_.y - mainViewport->Pos.y,
            };
        }
    }
    const bool modalToolOwnsViewportForMenu = modalToolOwnsViewport();
    if (!modalToolOwnsViewportForMenu &&
            input.rightPressed &&
            hovered &&
            !input.modifiers.primary &&
            !input.modifiers.shift &&
            !input.modifiers.alt) {
        contextMenuRightPressActive_ = true;
        contextMenuRightPressScreen_ = input.mouseScreen;
        contextMenuRightDragDelta_ = {};
    }
    if (contextMenuRightPressActive_ && input.rightDown && !input.rightPressed) {
        contextMenuRightDragDelta_ = contextMenuRightDragDelta_ + input.mouseDelta;
    }
    bool pendingContextMenuClick = false;
    Vec2 pendingContextMenuLocalMouse = {};
    ViewportContextMenuOpenRequest pendingContextMenuRequest;
    if (input.rightReleased && contextMenuRightPressActive_) {
        const bool releaseInsideViewport =
                input.mouseScreen.x >= itemMin.x &&
                input.mouseScreen.x <= itemMax.x &&
                input.mouseScreen.y >= itemMin.y &&
                input.mouseScreen.y <= itemMax.y;
        pendingContextMenuRequest = {
                .modalToolOwnsViewport = modalToolOwnsViewport(),
                .rightPressTracked = contextMenuRightPressActive_,
                .rightReleased = input.rightReleased,
                .releaseInsideViewport = releaseInsideViewport,
                .pressScreen = contextMenuRightPressScreen_,
                .releaseScreen = input.mouseScreen,
                .accumulatedDragDelta = contextMenuRightDragDelta_,
        };
        if (isViewportContextMenuClickGesture(pendingContextMenuRequest)) {
            pendingContextMenuClick = true;
            pendingContextMenuLocalMouse = input.mouseLocal;
        }
        contextMenuRightPressActive_ = false;
        contextMenuRightDragDelta_ = {};
    }
    if (!input.rightDown && !input.rightReleased) {
        contextMenuRightPressActive_ = false;
        contextMenuRightDragDelta_ = {};
    }
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
        insertEdgeLoopDragActive_ = false;
        cutDragActive_ = false;
        mirrorDragActive_ = false;
        lastHoverWorkKey_.valid = false;
        cachedSelectionOverlayFrameDirty_ = true;
    }
    if (result.toolChanged) {
        LOG_INFO("Viewport tool changed to {}", viewportToolId(inputRouter_.state().activeTool));
        paintSelectionActive_ = false;
        insertEdgeLoopDragActive_ = false;
        cutDragActive_ = false;
        mirrorDragActive_ = false;
        lastHoverWorkKey_.valid = false;
        cachedSelectionOverlayFrameDirty_ = true;
        const ModelingCommandResult command =
                modeling.activateViewportTool(toModelingViewportTool(inputRouter_.state().activeTool));
        if (!command.ok) {
            LOG_ERROR("Viewport tool activation failed: {}", command.message);
            inputRouter_.setActiveTool(ViewportTool::Select);
            (void)modeling.activateViewportTool(ModelingViewportTool::Select);
        } else {
            inputRouter_.setSelectionMode(toViewportSelectionMode(modeling.selectionSummary().mode));
        }
        if (inputRouter_.state().activeTool == ViewportTool::Box || hasActiveBoxPreview(boxPreview_)) {
            boxPreview_ = {};
        }
    }

    if (result.shortcutHandled && !operationPreviewShortcutConsumed) {
        if (const std::optional<ModelingCommandResult> command =
                        executeViewportModelingShortcut(undoRedo, modeling, result.shortcut)) {
            if (!command->ok) {
                LOG_ERROR("Viewport shortcut failed: {}", command->message.empty() ? "Command failed." : command->message);
            }
            inputRouter_.setSelectionMode(toViewportSelectionMode(modeling.selectionSummary().mode));
            lastHoverWorkKey_.valid = false;
            cachedSelectionOverlayFrameDirty_ = true;
        }
    }

    auto cancelActiveBoxPlacement = [&](bool exitBoxTool) {
        const ModelingCommandResult cancel = modeling.cancelBox();
        if (!cancel.ok) {
            LOG_ERROR("Box cancel failed: {}", cancel.message);
        }
        boxPreview_ = {};
        paintSelectionActive_ = false;
        insertEdgeLoopDragActive_ = false;
        cutDragActive_ = false;
        mirrorDragActive_ = false;
        lastHoverWorkKey_.valid = false;
        cachedSelectionOverlayFrameDirty_ = true;
        if (exitBoxTool) {
            inputRouter_.setActiveTool(ViewportTool::Move);
        }
    };

    bool cancelConsumed = false;
    const bool globalEscCancelRequested =
            modalToolOwnsViewport() &&
            !input.textInputActive &&
            !result.flyNavigationActive &&
            ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    if (!operationPreviewShortcutConsumed && (result.cancelRequested || globalEscCancelRequested)) {
        if (gizmoDragging_) {
#if QUADER_HAS_MODELING_SDK
            const bool previewDrivenCancel = modelingTransformDragPreviewDriven_;
            if (modelingTransformDragActive_) {
                const ModelingCommandResult cancel = modeling.cancelTransformDrag();
                if (!cancel.ok) {
                    LOG_ERROR("Transform drag cancel failed: {}", cancel.message);
                }
            }
            if (!previewDrivenCancel)
#endif
            {
                for (const GizmoStartTransform& start : gizmoStartTransforms_) {
                    if (start.entity != InvalidEntityId && document.transform(start.entity) != nullptr) {
                        document.setTransform(start.entity, start.transform);
                    }
                }
            }
            gizmoStartTransforms_.clear();
            gizmoDragging_ = false;
            gizmoDragPaneActive_ = false;
            activeGizmoAxis_ = GizmoAxis::None;
            modelingTransformDragActive_ = false;
            modelingTransformDragPreviewDriven_ = false;
            sceneOnlyGizmoDrag_ = false;
            cachedSelectionOverlayFrameDirty_ = true;
            cancelConsumed = true;
        }

        if (modalToolOwnsViewport()) {
            if (inputRouter_.state().activeTool == ViewportTool::Box || hasActiveBoxPreview(boxPreview_)) {
                cancelActiveBoxPlacement(true);
            } else {
                const ModelingCommandResult cancel = modeling.cancelActiveModalTool();
                if (!cancel.ok) {
                    LOG_ERROR("Modal tool cancel failed: {}", cancel.message);
                }
                inputRouter_.setActiveTool(ViewportTool::Move);
                paintSelectionActive_ = false;
                insertEdgeLoopDragActive_ = false;
                cutDragActive_ = false;
                mirrorDragActive_ = false;
                lastHoverWorkKey_.valid = false;
                cachedSelectionOverlayFrameDirty_ = true;
            }
            cancelConsumed = true;
        }
    }

    const bool suppressHoverWork = result.navigationActive || result.cameraChanged;
    const bool preserveContextMenuHover = shouldPreserveViewportContextMenuHover({
            .rightPressTracked = contextMenuRightPressActive_,
            .flyHudActive = result.flyHudActive,
            .accumulatedDragDelta = contextMenuRightDragDelta_,
    });
    const bool navigationGizmoConsumed = navigationGizmo_.draw(
            camera,
            paneState.id,
            itemMin,
            itemSize,
            capturedMouseDelta,
            viewportCursorCaptureRequested,
            viewportCursorCapturePoint);
    if (suppressHoverWork || navigationGizmoConsumed || result.gridPresetChanged) {
        lastHoverWorkKey_.valid = false;
    }
    const bool gizmoConsumed =
            navigationGizmoConsumed ||
            cancelConsumed ||
            drawTransformGizmo(document, assets, undoRedo, documentCommands, modeling, camera, paneState.id, itemMin, itemSize, suppressHoverWork);
    const ImGuiIO& io = ImGui::GetIO();
    const bool leftClicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const bool leftDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    const bool leftReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    const ImVec2 leftClickScreen = io.MouseClickedPos[ImGuiMouseButton_Left];
    const Vec2 leftClickLocal = leftClicked ?
            Vec2{leftClickScreen.x - itemMin.x, leftClickScreen.y - itemMin.y} :
            input.mouseLocal;
    const int leftClickCount = std::max(1, ImGui::GetMouseClickedCount(ImGuiMouseButton_Left));
    const ModelingToolStatus releaseToolStatus = modeling.toolStatus();
    const bool insertEdgeLoopDragRelease =
            inputRouter_.state().activeTool == ViewportTool::InsertEdgeLoop &&
            (insertEdgeLoopDragActive_ || releaseToolStatus.insertEdgeLoopDragging) &&
            leftReleased;
    const bool drawnPlaneDragRelease =
            (inputRouter_.state().activeTool == ViewportTool::Cut ||
                    inputRouter_.state().activeTool == ViewportTool::Mirror) &&
            (cutDragActive_ ||
                    mirrorDragActive_ ||
                    releaseToolStatus.cutDragging ||
                    releaseToolStatus.mirrorDragging) &&
            leftReleased;

    const bool mouseClickedOutsideViewport =
            modalToolOwnsViewport() &&
            !io.WantCaptureMouse &&
            !insertEdgeLoopDragRelease &&
            !drawnPlaneDragRelease &&
            !hovered &&
            !result.navigationActive &&
            ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) &&
            (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
                    ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
                    ImGui::IsMouseClicked(ImGuiMouseButton_Middle) ||
                    ImGui::IsMouseReleased(ImGuiMouseButton_Left) ||
                    ImGui::IsMouseReleased(ImGuiMouseButton_Right) ||
                    ImGui::IsMouseReleased(ImGuiMouseButton_Middle));
    if (mouseClickedOutsideViewport) {
        if (inputRouter_.state().activeTool == ViewportTool::Box) {
            cancelActiveBoxPlacement(true);
        } else {
            const ModelingCommandResult cancel = modeling.cancelActiveModalTool();
            if (!cancel.ok) {
                LOG_ERROR("Modal tool cancel failed: {}", cancel.message);
            }
            inputRouter_.setActiveTool(ViewportTool::Move);
            insertEdgeLoopDragActive_ = false;
            cutDragActive_ = false;
            mirrorDragActive_ = false;
        }
        cancelConsumed = true;
    }

    const ModelingSelectionSummary hoverSelectionSummary = modeling.selectionSummary();
    auto makeHoverWorkKey = [&](bool consumedByGizmo) {
        const ViewportOverlayCamera overlayCamera = makeViewportOverlayCamera(camera, itemSize);
        return HoverWorkKey{
                .valid = true,
                .paneId = paneState.id,
                .activeTool = inputRouter_.state().activeTool,
                .selectionMode = inputRouter_.state().selectionMode,
                .boxDragState = boxPreview_.dragState,
                .gridPreset = inputRouter_.state().gridPreset,
                .mouseLocal = input.mouseLocal,
                .viewportSize = input.viewportSize,
                .cameraEye = overlayCamera.eye,
                .cameraForward = overlayCamera.forward,
                .cameraRight = overlayCamera.right,
                .cameraUp = overlayCamera.up,
                .cameraProjection = overlayCamera.projection,
                .cameraOrthographicSize = overlayCamera.orthographicSize,
                .cameraFovDegrees = overlayCamera.fovDegrees,
                .cameraNearPlane = overlayCamera.nearPlane,
                .cameraFarPlane = overlayCamera.farPlane,
                .primaryModifier = input.modifiers.primary,
                .shiftModifier = input.modifiers.shift,
                .altModifier = input.modifiers.alt,
                .gizmoConsumed = consumedByGizmo,
                .sceneRevision = document.revision(),
                .assetRevision = assets.revision(),
                .contentRevision = hoverSelectionSummary.contentRevision,
                .selectionRevision = hoverSelectionSummary.selectionRevision,
        };
    };
    auto claimHoverWork = [&](const HoverWorkKey& key) {
        if (lastHoverWorkKey_.matches(key)) {
            return false;
        }
        lastHoverWorkKey_ = key;
        return true;
    };
    auto makeModelingToolEventAt = [&](Vec2 mouseLocal, bool dragging = false) {
        const ViewportRay worldRay = camera.rayFromViewportPoint(mouseLocal);
        return ModelingToolRayEvent{
                .screenPosition = mouseLocal,
                .ray = {
                        .origin = worldRay.origin,
                        .direction = worldRay.direction,
                },
                .camera = modelingViewportCamera(camera, {itemSize.x, itemSize.y}),
                .gridSize = std::max(static_cast<double>(gridSizeForPreset(inputRouter_.state().gridPreset)), 0.0001),
                .vertexRadius = activeViewportSettings().floatValue(ViewportFloatSettingId::PickVertexRadius),
                .edgeRadius = activeViewportSettings().floatValue(ViewportFloatSettingId::PickEdgeRadius),
                .snapEnabled = !input.modifiers.primary,
                .constrainAxis = input.modifiers.shift,
                .dragging = dragging,
        };
    };
    auto makeModelingToolEvent = [&](bool dragging = false) {
        return makeModelingToolEventAt(input.mouseLocal, dragging);
    };
    auto handleToolCommandResult = [&](std::string_view label, const ModelingCommandResult& command) {
        if (!command.ok) {
            LOG_ERROR("{} failed: {}", label, command.message.empty() ? "Command failed." : command.message);
        }
        if (command.changed) {
            lastHoverWorkKey_.valid = false;
            cachedSelectionOverlayFrameDirty_ = true;
        }
    };

    if (inputRouter_.state().activeTool == ViewportTool::Box) {
        const float gridSize = gridSizeForPreset(inputRouter_.state().gridPreset);
        const ModelingBoxToolEvent boxEvent = modelingBoxEvent(camera, input, gridSize);
        bool boxActionConsumed = false;

        HoverWorkKey hoverWorkKey = makeHoverWorkKey(gizmoConsumed);
        if (hovered &&
                !leftDown &&
                !suppressHoverWork &&
                boxPreview_.dragState == ModelingBoxDragState::Idle &&
                claimHoverWork(hoverWorkKey)) {
            boxPreview_ = modeling.updateBoxHover(boxEvent);
            logBoxPreviewFailure("Box hover", boxPreview_);
        }

        if (leftClicked && !gizmoConsumed && !result.navigationActive) {
            if (boxPreview_.dragState == ModelingBoxDragState::Height) {
                const std::string name = SceneEntityNaming::nextNameForKind(document, SceneEntityKind::Mesh);
                if (commitBoxPreview(undoRedo, modeling, defaultMaterial, document, name)) {
                    inputRouter_.setSelectionMode(ViewportSelectionMode::Object);
                    (void)modeling.setSelectionMode(ModelingSelectionMode::Object);
                    inputRouter_.setActiveTool(ViewportTool::Move);
                }
                boxPreview_ = {};
            } else {
                boxPreview_ = modeling.beginBoxFootprint(boxEvent);
                logBoxPreviewFailure("Box footprint begin", boxPreview_);
            }
            lastHoverWorkKey_.valid = false;
            cachedSelectionOverlayFrameDirty_ = true;
            boxActionConsumed = true;
        }

        if (!boxActionConsumed && boxPreview_.dragState == ModelingBoxDragState::Footprint && leftDown) {
            boxPreview_ = modeling.updateBoxFootprint(boxEvent);
            logBoxPreviewFailure("Box footprint update", boxPreview_);
            lastHoverWorkKey_.valid = false;
        }

        if (!boxActionConsumed && boxPreview_.dragState == ModelingBoxDragState::Footprint && leftReleased) {
            boxPreview_ = modeling.beginBoxHeight(boxEvent);
            logBoxPreviewFailure("Box height begin", boxPreview_);
            lastHoverWorkKey_.valid = false;
        }

        hoverWorkKey = makeHoverWorkKey(gizmoConsumed);
        if (!boxActionConsumed &&
                hovered &&
                !leftDown &&
                !suppressHoverWork &&
                boxPreview_.dragState == ModelingBoxDragState::Height &&
                claimHoverWork(hoverWorkKey)) {
            boxPreview_ = modeling.updateBoxHeight(boxEvent);
            logBoxPreviewFailure("Box height update", boxPreview_);
        }

        drawBoxPreview(ImGui::GetWindowDrawList(), boxPreview_, camera, itemMin, itemSize);
    } else if (inputRouter_.state().activeTool == ViewportTool::InsertEdgeLoop ||
            inputRouter_.state().activeTool == ViewportTool::Knife ||
            inputRouter_.state().activeTool == ViewportTool::Cut ||
            inputRouter_.state().activeTool == ViewportTool::Mirror ||
            inputRouter_.state().activeTool == ViewportTool::Poly) {
        if (hasActiveBoxPreview(boxPreview_)) {
            const ModelingCommandResult cancel = modeling.cancelBox();
            if (!cancel.ok) {
                LOG_ERROR("Box cancel failed: {}", cancel.message);
            }
            boxPreview_ = {};
        }

        const bool canInteractWithModalTool = hovered && !gizmoConsumed && !result.navigationActive;
        const bool finalizePressed =
                !input.textInputActive &&
                !result.navigationActive &&
                (ImGui::IsKeyPressed(ImGuiKey_Space, false) ||
                        ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                        ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));

        if (inputRouter_.state().activeTool == ViewportTool::InsertEdgeLoop) {
            const bool insertEdgeLoopDragging =
                    insertEdgeLoopDragActive_ || modeling.toolStatus().insertEdgeLoopDragging;
            if (canInteractWithModalTool &&
                    !leftDown &&
                    !leftReleased &&
                    !insertEdgeLoopDragging &&
                    !suppressHoverWork) {
                const HoverWorkKey hoverWorkKey = makeHoverWorkKey(gizmoConsumed);
                if (claimHoverWork(hoverWorkKey)) {
                    handleToolCommandResult("Insert edge loop hover",
                            modeling.updateInsertEdgeLoopHover(makeModelingToolEvent()));
                }
            }
            if (canInteractWithModalTool && leftClicked) {
                const ModelingCommandResult command =
                        modeling.beginInsertEdgeLoopDrag(makeModelingToolEvent(true));
                handleToolCommandResult("Insert edge loop drag begin", command);
                insertEdgeLoopDragActive_ = command.ok && modeling.toolStatus().insertEdgeLoopDragging;
            } else if (canInteractWithModalTool &&
                    leftDown &&
                    insertEdgeLoopDragging) {
                handleToolCommandResult("Insert edge loop drag update",
                        modeling.updateInsertEdgeLoopDrag(makeModelingToolEvent(true)));
            }
            if (leftReleased && insertEdgeLoopDragging) {
                const ModelingCommandResult command = executeTrackedModelingCommand(
                        undoRedo,
                        modeling,
                        "Insert Edge Loop",
                        [&]() {
                            return modeling.commitInsertEdgeLoopDrag();
                        });
                handleToolCommandResult("Insert edge loop commit", command);
                insertEdgeLoopDragActive_ = false;
                inputRouter_.setSelectionMode(toViewportSelectionMode(modeling.selectionSummary().mode));
            }
        } else if (inputRouter_.state().activeTool == ViewportTool::Knife) {
            if (canInteractWithModalTool && !suppressHoverWork) {
                const HoverWorkKey hoverWorkKey = makeHoverWorkKey(gizmoConsumed);
                if (claimHoverWork(hoverWorkKey)) {
                    handleToolCommandResult("Knife hover", modeling.updateKnifeHover(makeModelingToolEvent()));
                }
            }
            if (canInteractWithModalTool && leftClicked) {
                handleToolCommandResult("Knife point", modeling.acceptKnifePoint(makeModelingToolEvent()));
            }
            if (finalizePressed) {
                const ModelingCommandResult command = executeTrackedModelingCommand(
                        undoRedo,
                        modeling,
                        "Knife",
                        [&]() {
                            return modeling.commitKnifeTool();
                        });
                handleToolCommandResult("Knife commit", command);
                if (command.ok) {
                    inputRouter_.setActiveTool(ViewportTool::Move);
                    inputRouter_.setSelectionMode(toViewportSelectionMode(modeling.selectionSummary().mode));
                }
            }
        } else if (inputRouter_.state().activeTool == ViewportTool::Cut) {
            if (canInteractWithModalTool && !suppressHoverWork && !(leftDown && cutDragActive_)) {
                const HoverWorkKey hoverWorkKey = makeHoverWorkKey(gizmoConsumed);
                if (claimHoverWork(hoverWorkKey)) {
                    handleToolCommandResult("Cut hover", modeling.updateCutHover(makeModelingToolEvent()));
                }
            }
            if (canInteractWithModalTool && leftClicked) {
                const ModelingToolStatus before = modeling.toolStatus();
                ModelingCommandResult command;
                if (const std::optional<int> pointIndex = hitDrawnPlaneCapturedPoint(
                            before.cutPoints,
                            before.cutCapturedPointCount,
                            leftClickLocal,
                            camera,
                            itemMin,
                            itemSize)) {
                    command = modeling.beginCutPointDrag(*pointIndex);
                    handleToolCommandResult("Cut point drag begin", command);
                } else if (hitDrawnPlaneLine(
                            before.cutPoints,
                            before.cutCapturedPointCount,
                            before.cutHasLine,
                            leftClickLocal,
                            camera,
                            itemMin,
                            itemSize)) {
                    command = modeling.beginCutLineDrag(makeModelingToolEventAt(leftClickLocal, true));
                    handleToolCommandResult("Cut line drag begin", command);
                } else {
                    command = modeling.clickCut(makeModelingToolEventAt(leftClickLocal));
                    handleToolCommandResult("Cut point", command);
                }
                cutDragActive_ = command.ok && modeling.toolStatus().cutDragging;
            } else if (canInteractWithModalTool &&
                    leftDown &&
                    (cutDragActive_ || modeling.toolStatus().cutDragging)) {
                handleToolCommandResult("Cut drag update", modeling.updateCutHover(makeModelingToolEvent(true)));
                cutDragActive_ = modeling.toolStatus().cutDragging;
            }
            if (leftReleased && (cutDragActive_ || modeling.toolStatus().cutDragging)) {
                const ModelingCommandResult command = modeling.endCutDrag();
                handleToolCommandResult("Cut drag end", command);
                cutDragActive_ = false;
            }
            if (!input.textInputActive &&
                    !result.navigationActive &&
                    ImGui::IsKeyPressed(ImGuiKey_F, false)) {
                handleToolCommandResult("Cut mode", modeling.cycleCutMode());
            }
            if (finalizePressed) {
                const ModelingToolStatus status = modeling.toolStatus();
                if (status.cutDragging) {
                    LOG_ERROR("Cut commit failed: finish the current drag before committing.");
                } else if (!status.cutCanPreview || !status.cutPreviewValid) {
                    LOG_ERROR("Cut commit failed: place a valid cut line first.");
                } else {
                    const ModelingCommandResult command = executeTrackedModelingCommand(
                            undoRedo,
                            modeling,
                            "Cut",
                            [&]() {
                                return modeling.finalizeCutTool();
                            });
                    handleToolCommandResult("Cut commit", command);
                    if (command.ok) {
                        inputRouter_.setActiveTool(ViewportTool::Select);
                        inputRouter_.setSelectionMode(toViewportSelectionMode(modeling.selectionSummary().mode));
                    }
                }
            }
        } else if (inputRouter_.state().activeTool == ViewportTool::Mirror) {
            if (canInteractWithModalTool && !suppressHoverWork && !(leftDown && mirrorDragActive_)) {
                const HoverWorkKey hoverWorkKey = makeHoverWorkKey(gizmoConsumed);
                if (claimHoverWork(hoverWorkKey)) {
                    handleToolCommandResult("Mirror hover", modeling.updateMirrorHover(makeModelingToolEvent()));
                }
            }
            if (canInteractWithModalTool && leftClicked) {
                const ModelingToolStatus before = modeling.toolStatus();
                ModelingCommandResult command;
                if (const std::optional<int> pointIndex = hitDrawnPlaneCapturedPoint(
                            before.mirrorPoints,
                            before.mirrorCapturedPointCount,
                            leftClickLocal,
                            camera,
                            itemMin,
                            itemSize)) {
                    command = modeling.beginMirrorPointDrag(*pointIndex);
                    handleToolCommandResult("Mirror point drag begin", command);
                } else if (hitDrawnPlaneLine(
                            before.mirrorPoints,
                            before.mirrorCapturedPointCount,
                            before.mirrorHasLine,
                            leftClickLocal,
                            camera,
                            itemMin,
                            itemSize)) {
                    command = modeling.beginMirrorLineDrag(makeModelingToolEventAt(leftClickLocal, true));
                    handleToolCommandResult("Mirror line drag begin", command);
                } else {
                    command = modeling.clickMirror(makeModelingToolEventAt(leftClickLocal));
                    handleToolCommandResult("Mirror point", command);
                }
                mirrorDragActive_ = command.ok && modeling.toolStatus().mirrorDragging;
            } else if (canInteractWithModalTool &&
                    leftDown &&
                    (mirrorDragActive_ || modeling.toolStatus().mirrorDragging)) {
                handleToolCommandResult("Mirror drag update", modeling.updateMirrorHover(makeModelingToolEvent(true)));
                mirrorDragActive_ = modeling.toolStatus().mirrorDragging;
            }
            if (leftReleased && (mirrorDragActive_ || modeling.toolStatus().mirrorDragging)) {
                const ModelingCommandResult command = modeling.endMirrorDrag();
                handleToolCommandResult("Mirror drag end", command);
                mirrorDragActive_ = false;
            }
            if (finalizePressed) {
                const ModelingToolStatus status = modeling.toolStatus();
                if (status.mirrorDragging) {
                    LOG_ERROR("Mirror commit failed: finish the current drag before committing.");
                } else if (!status.mirrorCanPreview || !status.mirrorPreviewValid) {
                    LOG_ERROR("Mirror commit failed: place a valid mirror line first.");
                } else {
                    const ModelingCommandResult command = executeTrackedModelingCommand(
                            undoRedo,
                            modeling,
                            "Mirror",
                            [&]() {
                                return modeling.finalizeMirrorTool();
                            });
                    handleToolCommandResult("Mirror commit", command);
                    if (command.ok) {
                        inputRouter_.setActiveTool(ViewportTool::Select);
                        inputRouter_.setSelectionMode(toViewportSelectionMode(modeling.selectionSummary().mode));
                    }
                }
            }
        } else if (inputRouter_.state().activeTool == ViewportTool::Poly) {
            if (canInteractWithModalTool && !suppressHoverWork) {
                const HoverWorkKey hoverWorkKey = makeHoverWorkKey(gizmoConsumed);
                if (claimHoverWork(hoverWorkKey)) {
                    handleToolCommandResult("Poly hover", modeling.updatePolyHover(makeModelingToolEvent()));
                }
            }
            if (canInteractWithModalTool && leftClicked) {
                handleToolCommandResult("Poly point", modeling.acceptPolyPoint());
            }
            if (finalizePressed) {
                const ModelingCommandResult command = executeTrackedModelingCommand(
                        undoRedo,
                        modeling,
                        "Poly",
                        [&]() {
                            return modeling.commitPolyTool();
                        });
                handleToolCommandResult("Poly commit", command);
                if (command.ok) {
                    inputRouter_.setActiveTool(ViewportTool::Move);
                    inputRouter_.setSelectionMode(toViewportSelectionMode(modeling.selectionSummary().mode));
                }
            }
        }
    } else {
        if (hasActiveBoxPreview(boxPreview_)) {
            const ModelingCommandResult cancel = modeling.cancelBox();
            if (!cancel.ok) {
                LOG_ERROR("Box cancel failed: {}", cancel.message);
            }
            boxPreview_ = {};
        }

        const bool selectionFallbackActive = viewportToolAllowsSelectionFallback(inputRouter_.state().activeTool);
        if (selectionFallbackActive &&
                leftClicked &&
                !gizmoConsumed &&
                !result.navigationActive) {
            (void)selectFromViewport(document, assets, modeling, camera, input.mouseLocal, input.modifiers, leftClickCount);
            paintSelectionActive_ = inputRouter_.state().selectionMode != ViewportSelectionMode::Object;
            const ModelingCommandResult clearHover = modeling.clearSelectionHover();
            if (clearHover.changed) {
                cachedSelectionOverlayFrameDirty_ = true;
            }
            selectionHoverClearIssued_ = true;
            lastHoverWorkKey_ = makeHoverWorkKey(gizmoConsumed);
            cachedSelectionOverlayFrameDirty_ = true;
        } else if (selectionFallbackActive &&
                pendingContextMenuClick &&
                !gizmoConsumed) {
            const ModelingSelectionSummary summary =
                    selectFromViewport(document, assets, modeling, camera, pendingContextMenuLocalMouse, {}, 1, true, true);
            paintSelectionActive_ = summary.mode != ModelingSelectionMode::Object;
            selectionHoverClearIssued_ = false;

            const ModelingActionQuery query{
                    .surface = ModelingActionSurface::ViewportContextMenu,
                    .modalToolOwnsViewport = modalToolOwnsViewport(),
                    .includeDisabled = true,
                    .includeHidden = false,
            };
            const std::vector<ModelingActionItem> actions =
                    summary.hasSelection ? modeling.contextActions(query) : std::vector<ModelingActionItem>{};
            pendingContextMenuRequest.targetHit = summary.hasSelection;
            pendingContextMenuRequest.hasActions = !actions.empty();
            if (shouldOpenViewportContextMenu(pendingContextMenuRequest)) {
                ImGui::OpenPopup("ViewportOperationsContext");
            }
            lastHoverWorkKey_.valid = false;
            cachedSelectionOverlayFrameDirty_ = true;
        } else if (selectionFallbackActive &&
                paintSelectionActive_ &&
                leftDown &&
                hovered &&
                !gizmoConsumed &&
                !suppressHoverWork) {
            const ViewportRay worldRay = camera.rayFromViewportPoint(input.mouseLocal);
            const ViewportSettings& settings = activeViewportSettings();
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
                    .vertexRadius = settings.floatValue(ViewportFloatSettingId::PickVertexRadius),
                    .edgeRadius = settings.floatValue(ViewportFloatSettingId::PickEdgeRadius),
            });
            inputRouter_.setSelectionMode(toViewportSelectionMode(summary.mode));
            paintSelectionActive_ = summary.mode != ModelingSelectionMode::Object;
            selectionHoverClearIssued_ = false;
            lastHoverWorkKey_.valid = false;
            cachedSelectionOverlayFrameDirty_ = true;
        }

        if (leftReleased) {
            paintSelectionActive_ = false;
        }

        if (selectionFallbackActive &&
                hovered &&
                !suppressHoverWork &&
                !gizmoConsumed) {
            const HoverWorkKey hoverWorkKey = makeHoverWorkKey(gizmoConsumed);
            if (claimHoverWork(hoverWorkKey)) {
                const ViewportRay worldRay = camera.rayFromViewportPoint(input.mouseLocal);
                const ViewportSettings& settings = activeViewportSettings();
                const ModelingCommandResult hoverUpdate = modeling.updateSelectionHover(ModelingHoverRequest{
                        .ray = {
                                .origin = worldRay.origin,
                                .direction = worldRay.direction,
                        },
                        .selectedModifierPressed = input.modifiers.primary,
                        .vertexRadius = settings.floatValue(ViewportFloatSettingId::PickVertexRadius),
                        .edgeRadius = settings.floatValue(ViewportFloatSettingId::PickEdgeRadius),
                });
                if (hoverUpdate.changed) {
                    cachedSelectionOverlayFrameDirty_ = true;
                }
                selectionHoverClearIssued_ = false;
            }
        } else if ((!hovered || !selectionFallbackActive) &&
                !preserveContextMenuHover &&
                !selectionHoverClearIssued_) {
            const ModelingCommandResult clearHover = modeling.clearSelectionHover();
            if (clearHover.changed) {
                cachedSelectionOverlayFrameDirty_ = true;
            }
            selectionHoverClearIssued_ = true;
            lastHoverWorkKey_.valid = false;
        }
    }

    refreshCachedSelectionOverlayFrame(document, assets, modeling);
    drawFlyNavigationOverlay(
            ImGui::GetWindowDrawList(),
            itemMin,
            itemSize,
            result.flyHudActive,
            result.flyHudActive && !camera.flySpeedIsDefault(),
            result.flyHudActive,
            camera.flySpeed());
    drawViewportOperationsPopup(undoRedo, modeling, inputRouter_, modalToolOwnsViewport());
}

ModelingSelectionSummary ViewportPanel::selectFromViewport(
        SceneDocument& document,
        const AssetRegistry& assets,
        ModelingSessionService& modeling,
        const ViewportCameraController& camera,
        Vec2 localMouse,
        ViewportModifiers modifiers,
        int clickCount,
        bool preserveSelectionOnMiss,
        bool preserveSelectionOnSelectedHit) {
    const ViewportRay worldRay = camera.rayFromViewportPoint(localMouse);
    if (modeling.sdkAvailable()) {
        const ViewportSettings& settings = activeViewportSettings();
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
                .preserveSelectionOnMiss = preserveSelectionOnMiss,
                .preserveSelectionOnSelectedHit = preserveSelectionOnSelectedHit,
                .vertexRadius = settings.floatValue(ViewportFloatSettingId::PickVertexRadius),
                .edgeRadius = settings.floatValue(ViewportFloatSettingId::PickEdgeRadius),
        });
        inputRouter_.setSelectionMode(toViewportSelectionMode(summary.mode));
        return summary;
    }

    EntityId bestEntity = InvalidEntityId;
    float bestDistance = std::numeric_limits<float>::max();

    for (const EntityId id : document.entities()) {
        const VisibilityComponent* visibility = document.visibility(id);
        if (!document.isEffectivelyVisible(id) ||
                (visibility != nullptr && !visibility->selectable)) {
            continue;
        }

        const MeshComponent* meshComponent = document.mesh(id);
        if (!meshComponent || document.transform(id) == nullptr) {
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

        const Mat4 worldFromLocal = document.worldTransformMatrix(id);
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

    const bool bestEntitySelected =
            bestEntity != InvalidEntityId &&
            document.selection(bestEntity) != nullptr &&
            document.selection(bestEntity)->selected;
    const bool preserveSelectedHit = preserveSelectionOnSelectedHit && bestEntitySelected;
    if (!modifiers.primary &&
            !modifiers.shift &&
            !preserveSelectedHit &&
            (!preserveSelectionOnMiss || bestEntity != InvalidEntityId)) {
        document.clearSelection();
    }
    if (bestEntity != InvalidEntityId && !preserveSelectedHit) {
        const bool alreadySelected =
                document.selection(bestEntity) != nullptr && document.selection(bestEntity)->selected;
        document.setSelected(bestEntity, modifiers.primary && alreadySelected ? false : true);
    }
    const bool hasSelection = !document.selectedEntities().empty();
    return {
            .mode = ModelingSelectionMode::Object,
            .hasSelection = hasSelection,
            .activeKind = hasSelection ? ModelingActiveSelectionKind::Object : ModelingActiveSelectionKind::None,
            .selectedObjectCount = static_cast<std::uint32_t>(document.selectedEntities().size()),
    };
}

bool ViewportPanel::drawTransformGizmo(
        SceneDocument& document,
        const AssetRegistry& assets,
        UndoRedoManager& undoRedo,
        DocumentCommandService& documentCommands,
        ModelingSessionService& modeling,
        ViewportCameraController& camera,
        ViewportPaneId paneId,
        ImVec2 itemMin,
        ImVec2 itemSize,
        bool suppressHoverWork,
        bool interactive) {
    const EntityId selected = document.primarySelection();
    if (selected == InvalidEntityId) {
        if (!interactive) {
            return false;
        }
#if QUADER_HAS_MODELING_SDK
        if (modelingTransformDragActive_) {
            const ModelingCommandResult cancel = modeling.cancelTransformDrag();
            if (!cancel.ok) {
                LOG_ERROR("Transform drag cancel failed: {}", cancel.message);
            }
        }
#endif
        modelingTransformDragActive_ = false;
        modelingTransformDragPreviewDriven_ = false;
        sceneOnlyGizmoDrag_ = false;
        gizmoDragging_ = false;
        gizmoDragPaneActive_ = false;
        activeGizmoAxis_ = GizmoAxis::None;
        return false;
    }

    TransformComponent* transform = document.transform(selected);
    if (!transform) {
        if (!interactive) {
            return false;
        }
#if QUADER_HAS_MODELING_SDK
        if (modelingTransformDragActive_) {
            const ModelingCommandResult cancel = modeling.cancelTransformDrag();
            if (!cancel.ok) {
                LOG_ERROR("Transform drag cancel failed: {}", cancel.message);
            }
        }
#endif
        modelingTransformDragActive_ = false;
        modelingTransformDragPreviewDriven_ = false;
        sceneOnlyGizmoDrag_ = false;
        gizmoDragging_ = false;
        gizmoDragPaneActive_ = false;
        return false;
    }

    const ViewportTool tool = inputRouter_.state().activeTool;
    if (tool != ViewportTool::Move && tool != ViewportTool::Rotate && tool != ViewportTool::Scale) {
        if (!interactive) {
            return false;
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
#if QUADER_HAS_MODELING_SDK
            if (modelingTransformDragActive_) {
                const ModelingCommandResult cancel = modeling.cancelTransformDrag();
                if (!cancel.ok) {
                    LOG_ERROR("Transform drag cancel failed: {}", cancel.message);
                }
            }
#endif
            modelingTransformDragActive_ = false;
            modelingTransformDragPreviewDriven_ = false;
            sceneOnlyGizmoDrag_ = false;
            gizmoDragging_ = false;
            gizmoDragPaneActive_ = false;
            activeGizmoAxis_ = GizmoAxis::None;
        }
        return false;
    }

    if (interactive) {
        modeling.syncObjectSelectionFromScene();
    }
    const bool componentSelectionMode = modeling.selectionMode() != ModelingSelectionMode::Object;
    const std::optional<SceneBounds> selectionBounds = selectedWorldBounds(document, assets);
    const std::optional<Vec3> sdkSelectionCenter = modeling.selectionCenter();
    const Vec3 selectedWorldPosition = transformPoint(document.worldTransformMatrix(selected), Vec3{});
    const bool usePrimaryEntityPivot =
            !componentSelectionMode &&
            (document.modelingObject(selected) == nullptr || document.hasChildren(selected));
    const Vec3 gizmoPivot =
            (componentSelectionMode && sdkSelectionCenter) ? *sdkSelectionCenter :
            usePrimaryEntityPivot ? selectedWorldPosition :
            (selectionBounds ? boundsCenter(*selectionBounds) : sdkSelectionCenter.value_or(selectedWorldPosition));
    const ViewportScreenPoint projectedPivot = camera.projectWorldPoint(gizmoPivot);
    if (!projectedPivot.visible) {
        if (!interactive) {
            return false;
        }
#if QUADER_HAS_MODELING_SDK
        if (modelingTransformDragActive_) {
            const ModelingCommandResult cancel = modeling.cancelTransformDrag();
            if (!cancel.ok) {
                LOG_ERROR("Transform drag cancel failed: {}", cancel.message);
            }
        }
#endif
        modelingTransformDragActive_ = false;
        modelingTransformDragPreviewDriven_ = false;
        sceneOnlyGizmoDrag_ = false;
        gizmoDragging_ = false;
        gizmoDragPaneActive_ = false;
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
    const bool useModelingTransformDrag =
            selectedHasModelingObjectTransform &&
            !selectedHasSceneOnlyTransform &&
            (componentSelectionMode || !selectedHasHierarchyTransform);

#if QUADER_HAS_MODELING_SDK
    const float gizmoSizeScale = activeViewportSettings().floatValue(ViewportFloatSettingId::GizmoSize);
    quader_editor_core::TransformGizmoFrameInput sdkInput =
            makeSdkGizmoInput(camera, gizmoPivot, tool, activeGizmoAxis_, GizmoAxis::None, activeGridSize, gizmoSizeScale, itemSize);
    if (interactive && !gizmoDragging_ && mouseInsideViewport && !suppressHoverWork) {
        const Vec2 localMouse{mouse.x - itemMin.x, mouse.y - itemMin.y};
        const quader_editor_core::TransformGizmoPickHit hoverHit =
                quader_editor_core::pick_transform_gizmo(sdkInput, toSdk(localMouse));
        sdkInput.hover_axis = hoverHit.hit ? hoverHit.axis : quader_editor_core::TransformAxis::none;
    }
    const quader_editor_core::TransformGizmoFrame sdkFrame =
            quader_editor_core::build_transform_gizmo_frame(sdkInput);
    drawUnityStyleGizmo(sdkFrame, camera, itemMin, itemSize);
#else
#endif

    const ViewportCameraPose pose = camera.pose();
    const float cameraDistance = std::max(length(pose.eye - gizmoPivot), 0.1f);
    const float worldLength = std::clamp(cameraDistance * 0.22f * gizmoSizeScale, 0.35f, 8.0f);
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

    if (!interactive) {
        return false;
    }

    auto fallbackGizmoHit = [&]() {
        if (tool == ViewportTool::Rotate) {
            const float radius = std::sqrt(distanceSquared(mouse, origin));
            const float baseRadius = std::clamp(std::min(itemSize.x, itemSize.y) * 0.075f, 34.0f, 92.0f);
            if (std::abs(radius - baseRadius) < 8.0f) {
                return GizmoAxis::X;
            }
            if (std::abs(radius - baseRadius * 0.78f) < 8.0f) {
                return GizmoAxis::Y;
            }
            if (std::abs(radius - baseRadius * 0.56f) < 8.0f) {
                return GizmoAxis::Z;
            }
            return GizmoAxis::None;
        }

        for (std::size_t index = 0; index < axisEnds.size(); ++index) {
            if (axisEnds[index] && distanceToSegmentSquared(mouse, origin, *axisEnds[index]) < 144.0f) {
                return axes[index];
            }
        }
        return GizmoAxis::None;
    };

    bool consumed = gizmoDragging_;
    if (!gizmoDragging_ && mouseInsideViewport && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
#if QUADER_HAS_MODELING_SDK
        quader_editor_core::TransformGizmoPickHit sdkHit;
#endif

#if QUADER_HAS_MODELING_SDK
        const Vec2 localMouse{mouse.x - itemMin.x, mouse.y - itemMin.y};
        sdkHit = quader_editor_core::pick_transform_gizmo(sdkInput, toSdk(localMouse));
        activeGizmoAxis_ = sdkHit.hit ? fromSdkAxis(sdkHit.axis) : GizmoAxis::None;
        if (activeGizmoAxis_ == GizmoAxis::None && !useModelingTransformDrag) {
            activeGizmoAxis_ = fallbackGizmoHit();
        }
#else
        activeGizmoAxis_ = fallbackGizmoHit();
#endif

        if (activeGizmoAxis_ != GizmoAxis::None) {
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
                activeGizmoAxis_ = GizmoAxis::None;
                return consumed;
            }

            bool dragReady = true;
#if QUADER_HAS_MODELING_SDK
            if (useModelingTransformDrag) {
                dragReady = false;

                const Vec2 localMouse{mouse.x - itemMin.x, mouse.y - itemMin.y};
                const quader_poly::Vec2 sdkScreenPivot = toSdk(projectedPivot.position);
                const quader_poly::Vec2 sdkScreenTip = sdkHit.hit ? sdkHit.screen_tip : sdkScreenPivot;
                const ViewportRay startRay = camera.rayFromViewportPoint(localMouse);
                const bool duplicateSelected = !componentSelectionMode && io.KeyShift;
                const bool topologyModifier = componentSelectionMode && io.KeyShift;
                const ModelingTransformOperation operation =
                        topologyModifier && tool == ViewportTool::Move ? ModelingTransformOperation::Extrude :
                        topologyModifier && tool == ViewportTool::Scale ? ModelingTransformOperation::Inset :
                        ModelingTransformOperation::Transform;
                const ModelingTransformDragPreview beginPreview = modeling.beginTransformDrag({
                        .tool = viewportToolToModelingTransformTool(tool),
                        .operation = operation,
                        .axis = toModelingTransformAxis(activeGizmoAxis_),
                        .pivot = gizmoPivot,
                        .screenPosition = localMouse,
                        .screenPivot = projectedPivot.position,
                        .screenTip = fromSdk(sdkScreenTip),
                        .hasWorldDirection = sdkHit.hit,
                        .worldDirection = fromSdk(sdkHit.world_direction),
                        .ray = {
                                .origin = startRay.origin,
                                .direction = startRay.direction,
                        },
                        .camera = modelingViewportCamera(camera, {itemSize.x, itemSize.y}),
                        .gridSize = activeGridSize,
                        .vertexSnapRadius = 0.3,
                        .duplicateSelected = duplicateSelected,
                });
                if (!beginPreview.ok) {
                    LOG_ERROR("Transform drag begin failed: {}", beginPreview.message);
                }
                dragReady = beginPreview.ok && beginPreview.active;
                modelingTransformDragActive_ = dragReady;
                modelingTransformDragPreviewDriven_ = beginPreview.previewDriven;
                sceneOnlyGizmoDrag_ = false;
            } else {
                modelingTransformDragActive_ = false;
                modelingTransformDragPreviewDriven_ = false;
                sceneOnlyGizmoDrag_ = true;
            }
#endif

            if (!dragReady) {
                modelingTransformDragActive_ = false;
                modelingTransformDragPreviewDriven_ = false;
                sceneOnlyGizmoDrag_ = false;
                activeGizmoAxis_ = GizmoAxis::None;
                return consumed;
            }
            gizmoDragging_ = true;
            gizmoDragPaneActive_ = true;
            gizmoDragPaneId_ = paneId;
            lastGizmoMouse_ = {mouse.x, mouse.y};
            gizmoStartMouse_ = lastGizmoMouse_;
            gizmoStartWorldPivot_ = gizmoPivot;
            gizmoStartTransforms_.clear();
            for (const EntityId entity : document.selectedEntities()) {
                if (document.isEffectivelyLocked(entity)) {
                    continue;
                }
                if (hasSelectedAncestor(document, entity)) {
                    continue;
                }
                if (const TransformComponent* selectedTransform = document.transform(entity)) {
                    gizmoStartTransforms_.push_back(GizmoStartTransform{
                            .entity = entity,
                            .transform = *selectedTransform,
                            .localPivot = documentLocalPivotForWorldPoint(document, entity, gizmoPivot),
                    });
                }
            }
            if (gizmoStartTransforms_.empty()) {
#if QUADER_HAS_MODELING_SDK
                if (modelingTransformDragActive_) {
                    const ModelingCommandResult cancel = modeling.cancelTransformDrag();
                    if (!cancel.ok) {
                        LOG_ERROR("Transform drag cancel failed: {}", cancel.message);
                    }
                }
                modelingTransformDragActive_ = false;
                modelingTransformDragPreviewDriven_ = false;
                sceneOnlyGizmoDrag_ = false;
#endif
                gizmoDragging_ = false;
                gizmoDragPaneActive_ = false;
                activeGizmoAxis_ = GizmoAxis::None;
                return consumed;
            }
            consumed = true;
        }
    }

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const bool wasDragging = gizmoDragging_;
        bool modelingPreviewHandled = false;
#if QUADER_HAS_MODELING_SDK
        if (wasDragging && !sceneOnlyGizmoDrag_) {
            ModelingTransformDragPreview committedPreview;
            const ModelingCommandResult commit = executeTrackedModelingCommand(
                    undoRedo,
                    modeling,
                    "Transform Selection",
                    [&]() {
                        committedPreview = modeling.commitTransformDrag();
                        return ModelingCommandResult{
                                .ok = committedPreview.ok,
                                .changed = committedPreview.changed,
                                .message = committedPreview.message,
                        };
                    });
            if (!commit.ok) {
                LOG_ERROR("Transform drag commit failed: {}", commit.message);
            }
            modelingPreviewHandled = committedPreview.previewDriven || modelingTransformDragPreviewDriven_;
            modelingTransformDragActive_ = false;
            modelingTransformDragPreviewDriven_ = false;
        }
#endif
        if (wasDragging && !modelingPreviewHandled) {
            std::vector<DocumentTransformEdit> edits;
            edits.reserve(gizmoStartTransforms_.size());
            for (const GizmoStartTransform& start : gizmoStartTransforms_) {
                if (start.entity == InvalidEntityId) {
                    continue;
                }
                if (const TransformComponent* current = document.transform(start.entity)) {
                    edits.push_back(DocumentTransformEdit{
                            .entity = start.entity,
                            .before = start.transform,
                            .after = *current,
                    });
                }
            }
            const DocumentCommandResult recorded = documentCommands.setTransforms(std::move(edits));
            if (!recorded.ok) {
                LOG_ERROR("Transform command record failed: {}", recorded.message);
            }
        }
        gizmoDragging_ = false;
        gizmoDragPaneActive_ = false;
        activeGizmoAxis_ = GizmoAxis::None;
        gizmoStartTransforms_.clear();
        modelingTransformDragActive_ = false;
        modelingTransformDragPreviewDriven_ = false;
        sceneOnlyGizmoDrag_ = false;
        return consumed || wasDragging;
    }

    if (!gizmoDragging_ || activeGizmoAxis_ == GizmoAxis::None) {
        return consumed;
    }

    const Vec2 currentMouse{mouse.x, mouse.y};
    auto pinStartPivot = [&](TransformComponent& next, const GizmoStartTransform& start) {
        pinDocumentLocalPivotToWorldPoint(document, start.entity, next, start.localPivot, gizmoStartWorldPivot_);
    };

#if QUADER_HAS_MODELING_SDK
    if (!sceneOnlyGizmoDrag_) {
    const Vec2 localMouse{mouse.x - itemMin.x, mouse.y - itemMin.y};
    const ViewportRay dragRay = camera.rayFromViewportPoint(localMouse);
    const bool precision = io.KeyCtrl || io.KeySuper;
    const ModelingTransformDragPreview preview = modeling.updateTransformDrag({
            .screenPosition = localMouse,
            .ray = {
                    .origin = dragRay.origin,
                    .direction = dragRay.direction,
            },
            .camera = modelingViewportCamera(camera, {itemSize.x, itemSize.y}),
            .gridSize = activeGridSize,
            .vertexSnapRadius = 0.3,
            .precision = precision,
    });
    if (!preview.ok) {
        LOG_ERROR("Transform drag update failed: {}", preview.message);
        return consumed;
    }
    modelingTransformDragActive_ = preview.active;
    if (!preview.active) {
        return consumed;
    }

    if (preview.previewDriven) {
        modelingTransformDragPreviewDriven_ = true;
        return true;
    }

    Vec3 previewMoveDelta = preview.moveDelta;
    Vec3 previewRotationDelta = {};
    const float angleRadians = static_cast<float>(preview.rotateAngleDegrees * kPi / 180.0);
    if (activeGizmoAxis_ == GizmoAxis::X) {
        previewRotationDelta.x = angleRadians;
    } else if (activeGizmoAxis_ == GizmoAxis::Y) {
        previewRotationDelta.y = angleRadians;
    } else if (activeGizmoAxis_ == GizmoAxis::Z) {
        previewRotationDelta.z = angleRadians;
    }
    Vec3 previewScaleFactor = preview.scale;

    for (const GizmoStartTransform& start : gizmoStartTransforms_) {
        TransformComponent next = start.transform;
        if (tool == ViewportTool::Move) {
            next.position = start.transform.position + parentLocalVectorForWorldVector(document, start.entity, previewMoveDelta);
        } else if (tool == ViewportTool::Rotate) {
            applyDocumentWorldRotationDeltaAroundPivot(
                    document,
                    start.entity,
                    next,
                    previewRotationDelta,
                    start.localPivot,
                    gizmoStartWorldPivot_);
        } else if (tool == ViewportTool::Scale) {
            next.scale.x = std::max(start.transform.scale.x * previewScaleFactor.x, 0.001f);
            next.scale.y = std::max(start.transform.scale.y * previewScaleFactor.y, 0.001f);
            next.scale.z = std::max(start.transform.scale.z * previewScaleFactor.z, 0.001f);
            pinStartPivot(next, start);
        }
        document.setTransform(start.entity, next);
    }
    } else
#endif
    {
    const Vec2 delta = currentMouse - gizmoStartMouse_;
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
            Vec3 worldMoveDelta = {};
            applyAxis(worldMoveDelta.x, worldMoveDelta.y, worldMoveDelta.z, screenAxisDelta(activeGizmoAxis_) * worldPerPixel);
            next.position = start.transform.position + parentLocalVectorForWorldVector(document, start.entity, worldMoveDelta);
        } else if (tool == ViewportTool::Rotate) {
            const float screenDelta = (std::abs(delta.x) > std::abs(delta.y)) ? delta.x : -delta.y;
            Vec3 worldRotationDelta = {};
            applyAxis(worldRotationDelta.x, worldRotationDelta.y, worldRotationDelta.z, screenDelta * 0.01f);
            applyDocumentWorldRotationDeltaAroundPivot(
                    document,
                    start.entity,
                    next,
                    worldRotationDelta,
                    start.localPivot,
                    gizmoStartWorldPivot_);
        } else if (tool == ViewportTool::Scale) {
            applyAxis(next.scale.x, next.scale.y, next.scale.z, screenAxisDelta(activeGizmoAxis_) * 0.01f);
            next.scale.x = std::max(next.scale.x, 0.001f);
            next.scale.y = std::max(next.scale.y, 0.001f);
            next.scale.z = std::max(next.scale.z, 0.001f);
            pinStartPivot(next, start);
        }
        document.setTransform(start.entity, next);
    }
    }

    return true;
}

} // namespace quader
