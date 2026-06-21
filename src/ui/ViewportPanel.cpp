#include "ui/ViewportPanel.h"

#include "core/LocalizationService.h"
#include "core/Logger.h"
#include "render/overlay/ViewportOverlayStyleResolver.h"
#include "render/ViewportOverlayPolicy.h"
#include "scene/SceneBounds.h"
#include "scene/SceneEntityNaming.h"
#include "render/RenderLayers.h"
#include "render/RenderSystem.h"
#include "ui/UIContext.h"
#include "ui/ViewportContextMenuPolicy.h"
#include "viewport/ViewportCameraAdapter.h"
#include "viewport/HostViewportContractAdapter.h"
#include "viewport/tools/ViewportToolCatalog.h"

#include <algorithm>
#include <array>
#include <cmath>
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

namespace quader {
namespace {

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

bool hoverTargetValid(const ModelingHoverTarget& target) {
    return target.kind != ModelingActiveSelectionKind::None && target.objectId > 0;
}

bool hoverTargetsMatch(const ModelingHoverTarget& left, const ModelingHoverTarget& right) {
    if (left.kind != right.kind || left.objectId != right.objectId) {
        return false;
    }
    switch (left.kind) {
    case ModelingActiveSelectionKind::Object:
        return true;
    case ModelingActiveSelectionKind::Vertex:
        return left.vertexId == right.vertexId;
    case ModelingActiveSelectionKind::Edge:
        return (left.edgeA == right.edgeA && left.edgeB == right.edgeB) ||
                (left.edgeA == right.edgeB && left.edgeB == right.edgeA);
    case ModelingActiveSelectionKind::Face:
        return left.faceId == right.faceId;
    case ModelingActiveSelectionKind::None:
        break;
    }
    return false;
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
            .camera = ViewportCameraAdapter::modelingCamera(camera, input.viewportSize),
    };
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

ViewportToolId fromModelingViewportTool(ModelingViewportTool tool) {
    return viewportToolForModeling(tool);
}

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
        ViewportOverlayFrame overlayFrame = selectionOverlayCache_.valid() ? selectionOverlayCache_.frame() : ViewportOverlayFrame{};
        ViewportOverlayStyleResolver::apply(overlayFrame, settings);
        renderBridge.setOverlayFrame(
                viewportPaneIndex(paneId),
                viewportGridLayerMask(viewportPaneIndex(paneId)),
                std::move(overlayFrame),
                ViewportCameraAdapter::overlayCamera(paneState.camera, paneState.renderSize));
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

bool ViewportPanel::activateToolFromMenu(ViewportToolId tool, ModelingSessionService& modeling) {
    LOG_INFO("Tools menu requested viewport tool {}", viewportToolId(tool));
    const ViewportTool* toolDefinition = findViewportTool(tool);
    const ModelingCommandResult command = toolDefinition != nullptr ?
            toolDefinition->activate(modeling) :
            ModelingCommandResult{
                    .ok = false,
                    .changed = false,
                    .message = "Unknown viewport tool.",
            };
    if (!command.ok) {
        LOG_ERROR("Viewport tool activation failed: {}", command.message.empty() ? "Command failed." : command.message);
        return false;
    }
    const ViewportToolId activeTool = fromModelingViewportTool(modeling.toolStatus().activeTool);
    LOG_INFO("Viewport tool activated from menu: requested {}, active {}", viewportToolId(tool), viewportToolId(activeTool));
    inputRouter_.setActiveTool(activeTool);
    inputRouter_.setSelectionMode(toViewportSelectionMode(modeling.selectionSummary().mode));
    inputRouter_.clearModalToolState();
    paintSelectionActive_ = false;
    selectionHoverClearIssued_ = false;
    selectionHoverSuppressedTarget_.reset();
    hoverController_.invalidate();
    selectionOverlayCache_.markDirty();
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
    paneController_.resetCamera(paneState.id, paneState.camera);
}

void ViewportPanel::drawPaneChrome(
        const Pane& paneState,
        ImVec2 itemMin,
        ImVec2 itemSize,
        const LocalizationService& localization) const {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 itemMax{itemMin.x + itemSize.x, itemMin.y + itemSize.y};
    const ImU32 dividerColor = IM_COL32(19, 19, 20, 255);
    const ImU32 focusedColor = IM_COL32(46, 46, 48, 242);
    drawList->AddRect(itemMin, itemMax, paneState.id == focusedPaneId_ ? focusedColor : dividerColor, 0.0f, 0, 1.0f);
    std::string key = "viewport.pane.";
    key.append(viewportPaneStableId(paneState.id));
    key.append(".label");
    const std::string label = localization.text(key, viewportPaneLabel(paneState.id));
    const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
    const ImVec2 labelMin{itemMin.x + 8.0f, itemMin.y + 7.0f};
    const ImVec2 labelMax{labelMin.x + textSize.x + 12.0f, labelMin.y + textSize.y + 6.0f};
    drawList->AddRectFilled(labelMin, labelMax, IM_COL32(16, 18, 21, 176), 3.0f);
    drawList->AddText({labelMin.x + 6.0f, labelMin.y + 3.0f}, IM_COL32(228, 231, 236, 230), label.c_str());
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
    return ViewportPaneController::paneRectMin(viewportMin, viewportSize, normalizedRect);
}

ImVec2 ViewportPanel::paneRectSize(ImVec2 viewportSize, ViewportNormalizedRect normalizedRect) {
    return ViewportPaneController::paneRectSize(viewportSize, normalizedRect);
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
    const std::string windowTitle =
            uiContext.localization.imguiLabel("ui.panel.viewport", uiPanelKey(UIPanelId::Viewport), "Viewport");
    const bool visible = ImGui::Begin(
            windowTitle.c_str(),
            nullptr,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(2);
    if (!visible) {
        for (Pane& paneState : panes_) {
            renderBridge.setOverlayFrame(
                    viewportPaneIndex(paneState.id),
                    viewportGridLayerMask(viewportPaneIndex(paneState.id)),
                    {},
                    ViewportCameraAdapter::overlayCamera(paneState.camera, {1.0f, 1.0f}));
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
    const bool transformDragLocksInput = transformGizmoController_.dragLocksInput();
    const bool navigationGizmoDragLocksInput = navigationGizmo_.inputLocked();
    for (DrawPane& drawPane : drawPanes) {
        if (!transformDragLocksInput && !navigationGizmoDragLocksInput) {
            if (drawPane.focusHovered && !splitConsumed) {
                focusedPaneId_ = drawPane.pane->id;
            } else if (drawPane.focusHovered && focusController_.inputActive()) {
                focusedPaneId_ = drawPane.pane->id;
            }
        }
        if (transformDragLocksInput && drawPane.pane->id == transformGizmoController_.dragPaneId()) {
            inputPane = &drawPane;
        } else if (navigationGizmoDragLocksInput && drawPane.pane->id == navigationGizmo_.lockedPaneId()) {
            inputPane = &drawPane;
        } else if (!transformDragLocksInput && !navigationGizmoDragLocksInput && drawPane.hovered && !splitConsumed) {
            inputPane = &drawPane;
        }
        drawPaneChrome(*drawPane.pane, drawPane.itemMin, drawPane.itemSize, uiContext.localization);
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
                transformDragLocksInput && inputPane->pane->id == transformGizmoController_.dragPaneId();
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
    (void)transformGizmoController_.draw({
            .document = document,
            .assets = assets,
            .undoRedo = undoRedo,
            .documentCommands = documentCommands,
            .modeling = modeling,
            .inputRouter = inputRouter_,
            .viewportSettings = activeViewportSettings(),
            .camera = paneState.camera,
            .paneId = paneState.id,
            .itemMin = itemMin,
            .itemSize = itemSize,
            .suppressHoverWork = true,
            .interactive = false,
    });
}

void ViewportPanel::refreshCachedSelectionOverlayFrame(
        const SceneDocument& document,
        const AssetRegistry& assets,
        ModelingSessionService& modeling) {
    const ModelingSelectionSummary overlaySummary = modeling.selectionSummary();
    const ViewportPaneOverlayCacheKey key{
            .contentRevision = overlaySummary.contentRevision,
            .selectionRevision = overlaySummary.selectionRevision,
            .sceneRevision = document.revision(),
            .assetRevision = assets.revision(),
            .toolSignature = toolOverlaySignature(modeling),
            .selectionMode = overlaySummary.mode,
    };
    if (!selectionOverlayCache_.needsRefresh(key)) {
        return;
    }

    ViewportOverlayFrame frame = modeling.selectionOverlayFrame();
    appendHierarchySelectionWires(frame, document, assets);
    selectionOverlayCache_.update(std::move(frame), key);
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
    const ViewportFocusState focusState = focusController_.resolve(focusHovered, uiContext, UIPanelId::Viewport);
    auto modalToolOwnsViewport = [&]() {
        return inputRouter_.activeToolOwnsViewport() ||
                inputRouter_.hasActiveModalPreview() ||
                modeling.modalToolOwnsViewport();
    };

    const bool useCapturedMouseDelta = inputRouter_.flyCursorCaptureActive();
    const ViewportInputFrame input = inputFrameBuilder_.build({
            .hovered = hovered,
            .focused = focusState.focused,
            .itemMin = itemMin,
            .itemSize = itemSize,
            .routeActiveNavigationInput = inputRouter_.navigating(),
            .useCapturedMouseDelta = useCapturedMouseDelta,
            .capturedMouseDelta = capturedMouseDelta,
    });
    const ViewportInputResult result = inputRouter_.handleFrame(input, camera);
    const ModelingToolSettingsTarget toolSettingsTarget = modeling.toolSettingsTarget();
    const ViewportShortcutControllerResult operationPreviewShortcut =
            shortcutController_.handleOperationPreviewShortcut(result, toolSettingsTarget, inputRouter_, undoRedo, modeling);
    const bool operationPreviewShortcutConsumed = operationPreviewShortcut.consumed;
    if (operationPreviewShortcut.invalidateHover) {
        hoverController_.invalidate();
    }
    if (operationPreviewShortcut.markOverlayDirty) {
        selectionOverlayCache_.markDirty();
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
        inputRouter_.clearModalDragState();
        selectionHoverSuppressedTarget_.reset();
        hoverController_.invalidate();
        selectionOverlayCache_.markDirty();
    }
    if (result.toolChanged) {
        LOG_INFO("Viewport tool changed to {}", viewportToolId(inputRouter_.state().activeTool));
        paintSelectionActive_ = false;
        inputRouter_.clearModalDragState();
        selectionHoverSuppressedTarget_.reset();
        hoverController_.invalidate();
        selectionOverlayCache_.markDirty();
        const ViewportTool* activeTool = inputRouter_.activeTool();
        const ModelingCommandResult command = activeTool != nullptr ?
                activeTool->activate(modeling) :
                ModelingCommandResult{
                        .ok = false,
                        .changed = false,
                        .message = "No active viewport tool.",
                };
        if (!command.ok) {
            LOG_ERROR("Viewport tool activation failed: {}", command.message);
            inputRouter_.setActiveTool(ViewportToolId::Select);
            if (const ViewportTool* selectTool = inputRouter_.activeTool()) {
                (void)selectTool->activate(modeling);
            }
        } else {
            inputRouter_.setSelectionMode(toViewportSelectionMode(modeling.selectionSummary().mode));
        }
        inputRouter_.clearModalToolState();
    }

    if (!operationPreviewShortcutConsumed) {
        const ViewportShortcutControllerResult shortcut =
                shortcutController_.handleModelingShortcut(result, undoRedo, modeling);
        if (shortcut.commandExecuted) {
            inputRouter_.setSelectionMode(toViewportSelectionMode(modeling.selectionSummary().mode));
        }
        if (shortcut.invalidateHover) {
            hoverController_.invalidate();
        }
        if (shortcut.markOverlayDirty) {
            selectionOverlayCache_.markDirty();
        }
    }

    auto cancelActiveModalTool = [&](bool exitTool) {
        ViewportToolContext toolContext{
                .state = const_cast<ViewportToolState&>(inputRouter_.state()),
                .camera = camera,
                .modeling = modeling,
        };
        toolContext.setActiveTool = [&](ViewportToolId tool) {
            inputRouter_.setActiveTool(tool);
        };
        toolContext.invalidateHover = [&]() {
            hoverController_.invalidate();
        };
        toolContext.markOverlayDirty = [&]() {
            selectionOverlayCache_.markDirty();
        };
        (void)inputRouter_.handleActiveToolEvent(toolContext, ViewportToolEvent{
                .kind = ViewportToolEventKind::Cancel,
                .tool = inputRouter_.state().activeTool,
                .exitActiveModalTool = exitTool,
        });
        paintSelectionActive_ = false;
        selectionHoverSuppressedTarget_.reset();
        hoverController_.invalidate();
        selectionOverlayCache_.markDirty();
    };

    bool cancelConsumed = false;
    const bool globalEscCancelRequested =
            modalToolOwnsViewport() &&
            !input.textInputActive &&
            !result.flyNavigationActive &&
            ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    if (!operationPreviewShortcutConsumed && (result.cancelRequested || globalEscCancelRequested)) {
        if (transformGizmoController_.dragging()) {
            (void)transformGizmoController_.cancelActiveDrag(document, modeling);
            selectionOverlayCache_.markDirty();
            cancelConsumed = true;
        }

        if (modalToolOwnsViewport()) {
            cancelActiveModalTool(true);
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
        hoverController_.invalidate();
    }
    const bool transformGizmoConsumed =
            !cancelConsumed &&
            transformGizmoController_.draw({
                    .document = document,
                    .assets = assets,
                    .undoRedo = undoRedo,
                    .documentCommands = documentCommands,
                    .modeling = modeling,
                    .inputRouter = inputRouter_,
                    .viewportSettings = activeViewportSettings(),
                    .camera = camera,
                    .paneId = paneState.id,
                    .itemMin = itemMin,
                    .itemSize = itemSize,
                    .suppressHoverWork = suppressHoverWork,
            });
    const bool gizmoConsumed =
            cancelConsumed ||
            transformGizmoController_.suppressesHover(navigationGizmoConsumed, transformGizmoConsumed);
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
            releaseToolStatus.insertEdgeLoopDragging &&
            leftReleased;
    const bool drawnPlaneDragRelease =
            (releaseToolStatus.cutDragging ||
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
        cancelActiveModalTool(true);
        cancelConsumed = true;
    }

    const ModelingSelectionSummary hoverSelectionSummary = modeling.selectionSummary();
    auto makeViewportHoverWorkKey = [&](bool consumedByGizmo) {
        return hoverController_.makeKey(camera, ViewportHoverWorkRequest{
                .paneId = paneState.id,
                .activeTool = inputRouter_.state().activeTool,
                .selectionMode = inputRouter_.state().selectionMode,
                .boxDragState = inputRouter_.boxPreview().dragState,
                .gridPreset = inputRouter_.state().gridPreset,
                .mouseLocal = input.mouseLocal,
                .viewportSize = input.viewportSize,
                .modifiers = input.modifiers,
                .gizmoConsumed = consumedByGizmo,
                .sceneRevision = document.revision(),
                .assetRevision = assets.revision(),
                .contentRevision = hoverSelectionSummary.contentRevision,
                .selectionRevision = hoverSelectionSummary.selectionRevision,
        });
    };
    auto makeSelectionHoverRequestAt = [&](Vec2 mouseLocal, bool selectedModifierPressed) {
        const ViewportRay worldRay = camera.rayFromViewportPoint(mouseLocal);
        const ViewportSettings& settings = activeViewportSettings();
        return ModelingHoverRequest{
                .ray = {
                        .origin = worldRay.origin,
                        .direction = worldRay.direction,
                },
                .selectedModifierPressed = selectedModifierPressed,
                .vertexRadius = settings.floatValue(ViewportFloatSettingId::PickVertexRadius),
                .edgeRadius = settings.floatValue(ViewportFloatSettingId::PickEdgeRadius),
        };
    };
    auto makeModelingToolEventAt = [&](Vec2 mouseLocal, bool dragging = false) {
        const ViewportRay worldRay = camera.rayFromViewportPoint(mouseLocal);
        return ModelingToolRayEvent{
                .screenPosition = mouseLocal,
                .ray = {
                        .origin = worldRay.origin,
                        .direction = worldRay.direction,
                },
                .camera = ViewportCameraAdapter::modelingCamera(camera, {itemSize.x, itemSize.y}),
                .gridSize = std::max(static_cast<double>(gridSizeForPreset(inputRouter_.state().gridPreset)), 0.0001),
                .vertexRadius = activeViewportSettings().floatValue(ViewportFloatSettingId::PickVertexRadius),
                .edgeRadius = activeViewportSettings().floatValue(ViewportFloatSettingId::PickEdgeRadius),
                .snapEnabled = !input.modifiers.primary,
                .constrainAxis = input.modifiers.shift,
                .dragging = dragging,
        };
    };
    auto claimToolHover = [&](ModelingBoxDragState) {
        const ViewportHoverWorkKey hoverWorkKey = makeViewportHoverWorkKey(gizmoConsumed);
        return hoverController_.claim(hoverWorkKey);
    };
    auto handleToolCommandResult = [&](std::string_view label, const ModelingCommandResult& command) {
        if (!command.ok) {
            LOG_ERROR("{} failed: {}", label, command.message.empty() ? "Command failed." : command.message);
        }
        if (command.changed) {
            hoverController_.invalidate();
            selectionOverlayCache_.markDirty();
        }
    };
    auto suppressSelectionHoverAfterSelection = [&](Vec2 mouseLocal, ModelingHoverTarget hoveredTargetBeforeSelection = {}) {
        if (hoverTargetValid(hoveredTargetBeforeSelection)) {
            selectionHoverSuppressedTarget_ = hoveredTargetBeforeSelection;
        } else {
            const ModelingHoverSummary hover =
                    modeling.previewSelectionHover(makeSelectionHoverRequestAt(mouseLocal, input.modifiers.primary));
            if (hoverTargetValid(hover.target)) {
                selectionHoverSuppressedTarget_ = hover.target;
            } else {
                selectionHoverSuppressedTarget_.reset();
            }
        }
        const ModelingCommandResult clearHover = modeling.clearSelectionHover();
        if (clearHover.changed) {
            selectionOverlayCache_.markDirty();
        }
        selectionHoverClearIssued_ = true;
        hoverController_.invalidate();
        selectionOverlayCache_.markDirty();
    };

    const bool finalizePressed =
            !input.textInputActive &&
            !result.navigationActive &&
            (ImGui::IsKeyPressed(ImGuiKey_Space, false) ||
                    ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                    ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
    const bool modeCyclePressed =
            !input.textInputActive &&
            !result.navigationActive &&
            ImGui::IsKeyPressed(ImGuiKey_F, false);

    ViewportToolContext toolContext{
            .state = const_cast<ViewportToolState&>(inputRouter_.state()),
            .camera = camera,
            .modeling = modeling,
            .undoRedo = &undoRedo,
            .document = &document,
            .defaultMaterial = defaultMaterial,
            .settings = &activeViewportSettings(),
    };
    toolContext.makeBoxEvent = [&]() {
        const float gridSize = gridSizeForPreset(inputRouter_.state().gridPreset);
        return modelingBoxEvent(camera, input, gridSize);
    };
    toolContext.makeToolEventAt = makeModelingToolEventAt;
    toolContext.claimHover = claimToolHover;
    toolContext.invalidateHover = [&]() {
        hoverController_.invalidate();
    };
    toolContext.markOverlayDirty = [&]() {
        selectionOverlayCache_.markDirty();
    };
    toolContext.setActiveTool = [&](ViewportToolId tool) {
        inputRouter_.setActiveTool(tool);
    };
    toolContext.setSelectionMode = [&](ViewportSelectionMode mode) {
        inputRouter_.setSelectionMode(mode);
    };
    toolContext.nextMeshName = [&]() {
        return SceneEntityNaming::nextNameForKind(document, SceneEntityKind::Mesh);
    };
    toolContext.handleToolCommandResult = handleToolCommandResult;
    toolContext.hitDrawnPlaneCapturedPoint = [&](const std::array<Vec3, 3>& points, int capturedPointCount) {
        return hitDrawnPlaneCapturedPoint(points, capturedPointCount, leftClickLocal, camera, itemMin, itemSize);
    };
    toolContext.hitDrawnPlaneLine = [&](const std::array<Vec3, 3>& points, int capturedPointCount, bool hasLine) {
        return hitDrawnPlaneLine(points, capturedPointCount, hasLine, leftClickLocal, camera, itemMin, itemSize);
    };

    const bool canInteractWithActiveTool = hovered && !gizmoConsumed && !result.navigationActive;
    const ViewportToolResult activeToolFrameResult = inputRouter_.handleActiveToolEvent(toolContext, ViewportToolEvent{
            .kind = ViewportToolEventKind::Frame,
            .tool = inputRouter_.state().activeTool,
            .pointer = {
                    .hovered = hovered,
                    .leftClicked = leftClicked,
                    .leftDown = leftDown,
                    .leftReleased = leftReleased,
                    .canInteract = canInteractWithActiveTool,
                    .finalizePressed = finalizePressed,
                    .modeCyclePressed = modeCyclePressed,
                    .suppressHoverWork = suppressHoverWork,
                    .gizmoConsumed = gizmoConsumed,
                    .navigationActive = result.navigationActive,
                    .textInputActive = input.textInputActive,
                    .mouseLocal = input.mouseLocal,
                    .leftClickLocal = leftClickLocal,
                    .viewportSize = input.viewportSize,
                    .modifiers = input.modifiers,
            },
    });
    modalToolOverlayRenderer_.drawBoxPreview(ImGui::GetWindowDrawList(), inputRouter_.boxPreview(), camera, itemMin, itemSize);

    if (!activeToolFrameResult.handled) {
        const bool selectionFallbackActive = inputRouter_.activeToolAllowsSelectionFallback();
        if (selectionFallbackActive &&
                leftClicked &&
                !gizmoConsumed &&
                !result.navigationActive) {
            const ModelingHoverTarget hoveredBeforeSelection = modeling.selectionHoverSummary().target;
            const ModelingSelectionSummary summary = selectionController_.pickSelection(
                    document,
                    documentCommands,
                    assets,
                    modeling,
                    inputRouter_,
                    activeViewportSettings(),
                    camera,
                    input.mouseLocal,
                    input.modifiers,
                    leftClickCount);
            paintSelectionActive_ = summary.mode != ModelingSelectionMode::Object;
            suppressSelectionHoverAfterSelection(input.mouseLocal, hoveredBeforeSelection);
        } else if (selectionFallbackActive &&
                pendingContextMenuClick &&
                !gizmoConsumed) {
            const ModelingSelectionSummary summary =
                    selectionController_.pickSelection(
                            document,
                            documentCommands,
                            assets,
                            modeling,
                            inputRouter_,
                            activeViewportSettings(),
                            camera,
                            pendingContextMenuLocalMouse,
                            {},
                            1,
                            true,
                            true);
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
            hoverController_.invalidate();
            selectionOverlayCache_.markDirty();
        } else if (selectionFallbackActive &&
                paintSelectionActive_ &&
                leftDown &&
                hovered &&
                !gizmoConsumed &&
                !suppressHoverWork) {
            const ModelingHoverTarget hoveredBeforeSelection = modeling.selectionHoverSummary().target;
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
            suppressSelectionHoverAfterSelection(input.mouseLocal, hoveredBeforeSelection);
        }

        if (leftReleased) {
            paintSelectionActive_ = false;
        }

        bool selectionHoverSuppressedThisFrame = false;
        if (selectionFallbackActive &&
                hovered &&
                !suppressHoverWork &&
                !gizmoConsumed &&
                selectionHoverSuppressedTarget_.has_value()) {
            const ModelingHoverSummary hover =
                    modeling.previewSelectionHover(makeSelectionHoverRequestAt(input.mouseLocal, input.modifiers.primary));
            if (hoverTargetValid(hover.target) && hoverTargetsMatch(*selectionHoverSuppressedTarget_, hover.target)) {
                if (!input.modifiers.primary) {
                    const ModelingCommandResult clearHover = modeling.clearSelectionHover();
                    if (clearHover.changed) {
                        selectionOverlayCache_.markDirty();
                    }
                    selectionHoverClearIssued_ = true;
                    hoverController_.invalidate();
                    selectionHoverSuppressedThisFrame = true;
                }
            } else {
                selectionHoverSuppressedTarget_.reset();
                hoverController_.invalidate();
            }
        }

        if (selectionFallbackActive &&
                hovered &&
                !suppressHoverWork &&
                !gizmoConsumed &&
                !selectionHoverSuppressedThisFrame) {
            const ViewportHoverWorkKey hoverWorkKey = makeViewportHoverWorkKey(gizmoConsumed);
            if (hoverController_.claim(hoverWorkKey)) {
                const ModelingCommandResult hoverUpdate =
                        modeling.updateSelectionHover(makeSelectionHoverRequestAt(input.mouseLocal, input.modifiers.primary));
                if (hoverUpdate.changed) {
                    selectionOverlayCache_.markDirty();
                }
                selectionHoverClearIssued_ = false;
            }
        } else if ((!hovered || !selectionFallbackActive) &&
                !preserveContextMenuHover) {
            selectionHoverSuppressedTarget_.reset();
            if (!selectionHoverClearIssued_) {
                const ModelingCommandResult clearHover = modeling.clearSelectionHover();
                if (clearHover.changed) {
                    selectionOverlayCache_.markDirty();
                }
                selectionHoverClearIssued_ = true;
            }
            hoverController_.invalidate();
        }
    }

    refreshCachedSelectionOverlayFrame(document, assets, modeling);
    modalToolOverlayRenderer_.drawFlyNavigationOverlay(
            ImGui::GetWindowDrawList(),
            itemMin,
            itemSize,
            result.flyHudActive,
            result.flyHudActive && !camera.flySpeedIsDefault(),
            result.flyHudActive,
            camera.flySpeed(),
            uiContext.localization);
    contextMenuPresenter_.drawOperationsPopup(undoRedo, modeling, inputRouter_, uiContext.localization, modalToolOwnsViewport());
}

} // namespace quader
