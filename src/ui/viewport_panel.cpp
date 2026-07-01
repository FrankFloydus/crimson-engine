////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/viewport_panel.hpp"

#include "api/quader_api_type_conversions.hpp"
#include "api/quader_command_ids.hpp"
#include "api/quader_editor_api.hpp"
#include "assets/mesh_asset.hpp"
#include "core/localization_service.hpp"
#include "core/logger.hpp"
#include "diagnostics/profile.hpp"
#include "editor/commands/modeling/scene_aware_delete_selection_command.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"
#include "editor/viewport/quader_viewport_api_runtime_access.hpp"
#include "render/overlay/viewport_scene_overlay_builder.hpp"
#include "render/overlay/viewport_overlay_style_resolver.hpp"
#include "render/overlay/viewport_selection_marquee_overlay_builder.hpp"
#include "render/overlay/viewport_transform_gizmo_overlay_builder.hpp"
#include "scene/scene_bounds.hpp"
#include "scene/scene_entity_naming.hpp"
#include "render/render_layers.hpp"
#include "render/render_system.hpp"
#include "ui/imgui_popup_utils.hpp"
#include "ui/imgui_quader_widgets.hpp"
#include "ui/ui_context.hpp"
#include "ui/viewport_context_menu_policy.hpp"
#include "ui/viewport_pane_controller.hpp"
#include "viewport/viewport_shading_mode_conversions.hpp"
#include "ui/viewport_screen_geometry.hpp"
#include "viewport/viewport_camera_adapter.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"
#include "viewport/viewport_settings.hpp"
#include "editor/tools/viewport_tool_catalog.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <imgui_internal.h>

namespace quader {
namespace {

ViewportOverlaySelectionContext overlaySelectionContext(ModelingSelectionMode mode) {
    switch (mode) {
    case ModelingSelectionMode::Vertex:
        return ViewportOverlaySelectionContext::Vertex;
    case ModelingSelectionMode::Edge:
        return ViewportOverlaySelectionContext::Edge;
    case ModelingSelectionMode::Face:
        return ViewportOverlaySelectionContext::Face;
    case ModelingSelectionMode::Group:
        return ViewportOverlaySelectionContext::Group;
    case ModelingSelectionMode::Object:
        return ViewportOverlaySelectionContext::Object;
    }
    return ViewportOverlaySelectionContext::Any;
}

float gridSizeForPreset(int preset) {
    return viewportGridWorldSizeForPreset(preset);
}

std::string viewportViewLocalizationKey(ViewportView view) {
    std::string key = "viewport.view.";
    key.append(viewportViewStableId(view));
    return key;
}

std::string localizedViewportViewLabel(const LocalizationService& localization, ViewportView view) {
    return localization.text(viewportViewLocalizationKey(view), std::string(viewportViewLabel(view)));
}

constexpr std::array<ViewportView, 7> kViewportChromeViews{{
        ViewportView::Perspective,
        ViewportView::Top,
        ViewportView::Bottom,
        ViewportView::Front,
        ViewportView::Back,
        ViewportView::Side,
        ViewportView::Left,
}};

std::string_view viewportShadingModeStableId(ViewportShadingMode mode) {
    switch (mode) {
    case ViewportShadingMode::Wireframe:
        return "wireframe";
    case ViewportShadingMode::Shaded:
        return "shaded";
    case ViewportShadingMode::Rendered:
        return "rendered";
    }
    return "shaded";
}

std::string_view viewportShadingModeLabel(ViewportShadingMode mode) {
    switch (mode) {
    case ViewportShadingMode::Wireframe:
        return "Wireframe";
    case ViewportShadingMode::Shaded:
        return "Shaded";
    case ViewportShadingMode::Rendered:
        return "Rendered";
    }
    return "Shaded";
}

std::string viewportShadingModeLocalizationKey(ViewportShadingMode mode) {
    std::string key = "viewport.shading.";
    key.append(viewportShadingModeStableId(mode));
    return key;
}

std::string localizedViewportShadingModeLabel(const LocalizationService& localization, ViewportShadingMode mode) {
    return localization.text(viewportShadingModeLocalizationKey(mode), std::string(viewportShadingModeLabel(mode)));
}

constexpr std::array<ViewportShadingMode, 3> kViewportChromeShadingModes{{
        ViewportShadingMode::Wireframe,
        ViewportShadingMode::Shaded,
        ViewportShadingMode::Rendered,
}};

float viewportChromeComboWidthForText(const std::string& label) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 labelSize = ImGui::CalcTextSize(label.data(), label.data() + label.size());
    return std::ceil(labelSize.x + ImGui::GetFrameHeight() + style.FramePadding.x * 2.0f + 10.0f);
}

float viewportViewComboWidth(const LocalizationService& localization) {
    float width = 0.0f;
    for (const ViewportView view : kViewportChromeViews) {
        width = std::max(width, viewportChromeComboWidthForText(localizedViewportViewLabel(localization, view)));
    }
    return width;
}

float viewportShadingComboWidth(const LocalizationService& localization) {
    float width = 0.0f;
    for (const ViewportShadingMode mode : kViewportChromeShadingModes) {
        width = std::max(width, viewportChromeComboWidthForText(localizedViewportShadingModeLabel(localization, mode)));
    }
    return width;
}

/**
 * Stores the scene-visible counters presented by the View > Show Info viewport overlay.
 */
struct ViewportInfoSceneStats {
    std::uint64_t objects = 0;
    std::uint64_t primitives = 0;
    std::uint64_t drawCalls = 0;
};

ViewportInfoSceneStats collectViewportInfoSceneStats(const SceneDocument& document, const AssetRegistry& assets) {
    ViewportInfoSceneStats stats;
    for (const EntityId entityId : document.entities()) {
        if (!document.isEffectivelyVisible(entityId)) {
            continue;
        }

        const MeshComponent* meshComponent = document.mesh(entityId);
        if (meshComponent == nullptr) {
            continue;
        }

        const MeshAsset* mesh = assets.mesh(meshComponent->mesh);
        if (mesh == nullptr || mesh->vertices.empty() || mesh->indices.empty()) {
            continue;
        }

        const std::vector<MeshPrimitiveSpan> spans = coalescedMeshPrimitiveSpans(*mesh);
        if (spans.empty()) {
            continue;
        }

        ++stats.objects;
        stats.drawCalls += spans.size();
        for (const MeshPrimitiveSpan& span : spans) {
            stats.primitives += span.indexCount / 3u;
        }
    }
    return stats;
}

std::string formatViewportInfoNumber(std::uint64_t value) {
    return std::to_string(value);
}

std::string formatViewportInfoMilliseconds(double value) {
    if (!(value >= 0.0) || !std::isfinite(value)) {
        return "--";
    }
    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "%.2f ms", value);
    return buffer;
}

std::string formatViewportInfoFramesPerSecond(double value) {
    if (!(value >= 0.0) || !std::isfinite(value)) {
        return "--";
    }
    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "%.1f", value);
    return buffer;
}

std::string viewportInfoLine(std::string_view label, const std::string& value) {
    std::string line{label};
    line.append(": ");
    line.append(value);
    return line;
}

void drawViewportInfoText(ImDrawList& drawList, ImVec2 itemMin, ImVec2 itemSize, std::span<const std::string> lines) {
    if (lines.empty() || itemSize.x <= 32.0f || itemSize.y <= 32.0f) {
        return;
    }

    const float padding = 10.0f;
    const float lineAdvance = ImGui::GetTextLineHeightWithSpacing();
    float width = 0.0f;
    for (const std::string& line : lines) {
        width = std::max(width, ImGui::CalcTextSize(line.c_str(), nullptr, false).x);
    }
    const float height = ImGui::GetTextLineHeight() + lineAdvance * static_cast<float>(lines.size() - 1u);
    const float x = std::max(itemMin.x + padding, itemMin.x + itemSize.x - width - padding);
    const float y = std::max(itemMin.y + padding, itemMin.y + itemSize.y - height - padding);
    const ImU32 shadow = IM_COL32(0, 0, 0, 180);
    const ImU32 foreground = IM_COL32(235, 238, 244, 230);

    for (std::size_t index = 0; index < lines.size(); ++index) {
        const std::string& line = lines[index];
        const ImVec2 position{x, y + lineAdvance * static_cast<float>(index)};
        drawList.AddText({position.x + 1.0f, position.y + 1.0f}, shadow, line.c_str());
        drawList.AddText(position, foreground, line.c_str());
    }
}

std::optional<int> hitDrawnPlaneCapturedPoint(
        const std::array<QVec3, 3>& points,
        int capturedPointCount,
        QVec2 mouseLocal,
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
        if (viewportDistanceSquared(mouse, *screen) <= kPointHitRadiusPixels * kPointHitRadiusPixels) {
            return index;
        }
    }
    return std::nullopt;
}

bool hitDrawnPlaneLine(
        const std::array<QVec3, 3>& points,
        int capturedPointCount,
        bool hasLine,
        QVec2 mouseLocal,
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
    return viewportDistanceToSegmentSquared(mouse, *start, *end) <= kLineHitRadiusPixels * kLineHitRadiusPixels;
}

ModelingBoxToolEvent modelingBoxEvent(
        const ViewportCameraController& camera,
        const ViewportInputFrame& input,
        float gridSize,
        bool snapEnabled) {
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
            .snapEnabled = snapEnabled,
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

void logCommandFailure(std::string_view source, const QuaderApiVoidResult& result) {
    if (result.ok) {
        return;
    }
    LOG_ERROR("{} failed: {}", source, result.message.empty() ? "Command failed." : result.message);
}

void markNavigationPressed(ViewportNavigationInput& navigation, ViewportInputActionKind action) {
    switch (action) {
    case ViewportInputActionKind::Orbit:
        navigation.orbitPressed = true;
        navigation.orbitDown = true;
        break;
    case ViewportInputActionKind::OrbitSnap:
        navigation.orbitSnapPressed = true;
        navigation.orbitSnapDown = true;
        break;
    case ViewportInputActionKind::Pan:
        navigation.panPressed = true;
        navigation.panDown = true;
        break;
    case ViewportInputActionKind::Fly:
        navigation.flyPressed = true;
        navigation.flyDown = true;
        break;
    case ViewportInputActionKind::ResetFlySpeed:
        navigation.resetFlySpeedPressed = true;
        break;
    case ViewportInputActionKind::CancelNavigation:
        navigation.cancelNavigationPressed = true;
        break;
    case ViewportInputActionKind::FlyForward:
        navigation.flyForwardDown = true;
        break;
    case ViewportInputActionKind::FlyBackward:
        navigation.flyBackwardDown = true;
        break;
    case ViewportInputActionKind::FlyRight:
        navigation.flyRightDown = true;
        break;
    case ViewportInputActionKind::FlyLeft:
        navigation.flyLeftDown = true;
        break;
    default:
        break;
    }
}

void markResolvedInputActionPressed(ViewportInputFrame& input, ViewportInputActionKind action) {
    switch (action) {
    case ViewportInputActionKind::ModalToolFinalize:
        input.modalToolFinalizePressed = true;
        break;
    case ViewportInputActionKind::ModalToolModeCycle:
        input.modalToolModeCyclePressed = true;
        break;
    default:
        markNavigationPressed(input.navigationBindings, action);
        break;
    }
}

void markNavigationDown(ViewportNavigationInput& navigation, ViewportInputActionKind action) {
    switch (action) {
    case ViewportInputActionKind::Orbit:
        navigation.orbitDown = true;
        break;
    case ViewportInputActionKind::OrbitSnap:
        navigation.orbitSnapDown = true;
        break;
    case ViewportInputActionKind::Pan:
        navigation.panDown = true;
        break;
    case ViewportInputActionKind::Fly:
        navigation.flyDown = true;
        break;
    case ViewportInputActionKind::Zoom:
        navigation.zoomWheel = true;
        break;
    case ViewportInputActionKind::ZoomToCursor:
        navigation.zoomToCursorWheel = true;
        break;
    case ViewportInputActionKind::FlyForward:
        navigation.flyForwardDown = true;
        break;
    case ViewportInputActionKind::FlyBackward:
        navigation.flyBackwardDown = true;
        break;
    case ViewportInputActionKind::FlyRight:
        navigation.flyRightDown = true;
        break;
    case ViewportInputActionKind::FlyLeft:
        navigation.flyLeftDown = true;
        break;
    default:
        break;
    }
}

void markNavigationReleased(ViewportNavigationInput& navigation, ViewportInputActionKind action) {
    switch (action) {
    case ViewportInputActionKind::Orbit:
        navigation.orbitReleased = true;
        break;
    case ViewportInputActionKind::OrbitSnap:
        navigation.orbitSnapReleased = true;
        break;
    case ViewportInputActionKind::Pan:
        navigation.panReleased = true;
        break;
    case ViewportInputActionKind::Fly:
        navigation.flyReleased = true;
        break;
    default:
        break;
    }
}

void syncFlyDirectionFromNavigationBindings(ViewportInputFrame& input) {
    input.flyLocalDirection = {};
    if (input.navigationBindings.flyForwardDown) {
        input.flyLocalDirection.z += 1.0f;
    }
    if (input.navigationBindings.flyBackwardDown) {
        input.flyLocalDirection.z -= 1.0f;
    }
    if (input.navigationBindings.flyRightDown) {
        input.flyLocalDirection.x += 1.0f;
    }
    if (input.navigationBindings.flyLeftDown) {
        input.flyLocalDirection.x -= 1.0f;
    }
}

QuaderViewportKeybindingContext keybindingContextFromInput(const ViewportInputFrame& input) {
    return {
            .viewportHovered = input.viewportHovered,
            .navigationActive = input.navigationActive,
            .flyNavigationActive = input.flyNavigationActive,
            .orbitNavigationActive = input.orbitNavigationActive,
            .panNavigationActive = input.panNavigationActive,
            .modalToolActive = input.modalToolActive,
            .activeToolOwnsViewport = input.activeToolOwnsViewport,
            .operationPreviewActive = input.operationPreviewActive,
    };
}

bool resolveViewportInputActionForChord(
        QuaderEditorApi& editor,
        const ViewportToolState& state,
        const ViewportInputFrame& input,
        const QuaderInputChord& chord,
        ViewportInputActionKind& action) {
    return editor.commands().resolveViewportInputAction(
            chord,
            state,
            input.viewportFocused,
            input.textInputActive,
            action,
            keybindingContextFromInput(input));
}

bool applyViewportKeybindingDispatch(
        QuaderEditorApi& editor,
        const QuaderViewportKeybindingDispatch& dispatch,
        ViewportInputFrame& input,
        bool* commandShortcutConsumed) {
    switch (dispatch.kind) {
    case QuaderViewportKeybindingDispatch::Kind::PendingChord:
        input.resolvedShortcuts.push_back(ViewportShortcut{});
        return true;
    case QuaderViewportKeybindingDispatch::Kind::ViewportShortcut:
        input.resolvedShortcuts.push_back(dispatch.shortcut);
        return true;
    case QuaderViewportKeybindingDispatch::Kind::Command:
        logCommandFailure("Keybinding command", editor.commands().execute(dispatch.commandId));
        if (commandShortcutConsumed != nullptr) {
            *commandShortcutConsumed = true;
        }
        input.resolvedShortcuts.push_back(ViewportShortcut{});
        return true;
    case QuaderViewportKeybindingDispatch::Kind::NoMatch:
        return false;
    }
    return false;
}

void resolveKeybindingsForFrame(
        QuaderEditorApi& editor,
        const ViewportToolState& state,
        ViewportInputFrame& input,
        bool* commandShortcutConsumed) {
    input.resolvedShortcuts.clear();
    input.navigationBindings = {};
    if (!input.viewportFocused || input.textInputActive) {
        editor.commands().resetPendingKeybindingChord();
        return;
    }

    for (const QuaderInputChord& chord : input.activeInputChords) {
        ViewportInputActionKind action = ViewportInputActionKind::None;
        if (resolveViewportInputActionForChord(editor, state, input, chord, action)) {
            markNavigationDown(input.navigationBindings, action);
        }
    }
    for (const QuaderInputChord& chord : input.releasedInputChords) {
        ViewportInputActionKind action = ViewportInputActionKind::None;
        if (resolveViewportInputActionForChord(editor, state, input, chord, action)) {
            markNavigationReleased(input.navigationBindings, action);
        }
    }
    for (const QuaderInputChord& chord : input.wheelInputChords) {
        ViewportInputActionKind action = ViewportInputActionKind::None;
        if (resolveViewportInputActionForChord(editor, state, input, chord, action)) {
            markNavigationDown(input.navigationBindings, action);
        }
    }

    for (const QuaderInputChord& chord : input.pressedInputChords) {
        ViewportInputActionKind action = ViewportInputActionKind::None;
        if (resolveViewportInputActionForChord(editor, state, input, chord, action)) {
            markResolvedInputActionPressed(input, action);
            continue;
        }

        const QuaderViewportKeybindingDispatch dispatch = editor.commands().dispatchViewportInputBinding(
                chord,
                state,
                input.viewportFocused,
                input.textInputActive,
                keybindingContextFromInput(input));
        if (applyViewportKeybindingDispatch(editor, dispatch, input, commandShortcutConsumed)) {
            return;
        }
    }

    syncFlyDirectionFromNavigationBindings(input);
}

QuaderApiVoidResult executeDeletePopupCommand(
        QuaderEditorApi& editor,
        ViewportInputRouter& inputRouter,
        QuaderCommandId commandId) {
    const QuaderApiVoidResult result = editor.commands().execute(commandId);
    if (!result.ok) {
        LOG_ERROR(
                "Delete popup command '{}' failed: {}",
                commandId.string(),
                result.message.empty() ? "Operation failed." : result.message);
    }
    inputRouter.setSelectionMode(appSelectionModeToViewport(toModelingSelectionMode(editor.selection().summary().mode)));
    return result;
}

bool drawViewportDeletePopup(
        bool& objectPopupOpen,
        bool& objectPopupPositionPending,
        ImVec2& objectPopupPosition,
        QuaderEditorApi& editor,
        ViewportInputRouter& inputRouter,
        LocalizationService& localization) {
    bool changed = false;

    if (objectPopupOpen) {
        if (objectPopupPositionPending) {
            ImGui::SetNextWindowPos(objectPopupPosition, ImGuiCond_Always, ImVec2(0.5f, 0.0f));
            objectPopupPositionPending = false;
        }
        const std::string windowTitle =
                localization.text("ui.viewport.delete_popup.window_title", "Delete") + "##ViewportDeleteObjectsWindow";
        bool open = objectPopupOpen;
        const ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoDocking |
                ImGuiWindowFlags_NoSavedSettings;
        if (ImGui::Begin(windowTitle.c_str(), &open, flags)) {
            const std::string title = localization.text("ui.viewport.delete_popup.title", "Delete Selected Objects");
            ImGui::TextUnformatted(title.c_str());
            const std::string deleteLabel = localization.text("ui.viewport.delete_popup.delete", "Delete");
            if (ImGui::Button(deleteLabel.c_str())) {
                const QuaderApiVoidResult result =
                        executeDeletePopupCommand(editor, inputRouter, QuaderCommand::DeleteSelection);
                changed = result.changed;
                open = false;
            }
            ImGui::SameLine();
            const std::string cancelLabel = localization.text("ui.viewport.delete_popup.cancel", "Cancel");
            if (ImGui::Button(cancelLabel.c_str())) {
                open = false;
            }
        }
        ImGui::End();
        objectPopupOpen = open;
    }

    if (ImGui::BeginPopup("ViewportDeleteElementsPopup")) {
        const ModelingSelectionSummary summary = toModelingSelectionSummary(editor.selection().summary());
        /**
         * Represents a Popup Action value used by the ImGui editor presentation layer.
         */
        struct PopupAction {
            const char* labelKey;
            const char* fallback;
            QuaderCommandId commandId;
            bool enabled;
        };
        const PopupAction actions[] = {
                {"ui.viewport.delete_vertices", "Delete Vertices", QuaderCommand::DeleteSelection,
                        summary.mode == ModelingSelectionMode::Vertex && summary.selectedVertexCount > 0u},
                {"ui.viewport.delete_face", "Delete Face", QuaderCommand::DeleteSelection,
                        summary.mode == ModelingSelectionMode::Face && summary.selectedFaceCount > 0u},
                {"ui.viewport.delete_edges", "Delete Edges", QuaderCommand::DeleteSelection,
                        summary.mode == ModelingSelectionMode::Edge && summary.selectedEdgeCount > 0u},
                {"ui.viewport.dissolve_edges", "Dissolve Edges", QuaderCommand::ModelingDissolveEdges,
                        summary.mode == ModelingSelectionMode::Edge && summary.selectedEdgeCount > 0u},
                {"ui.viewport.dissolve_vertex", "Dissolve Vertex", QuaderCommand::ModelingDissolveVertices,
                        summary.mode == ModelingSelectionMode::Vertex && summary.selectedVertexCount > 0u},
        };
        for (const PopupAction& action : actions) {
            const std::string label = localization.text(action.labelKey, action.fallback);
            if (ImGui::MenuItem(label.c_str(), nullptr, false, action.enabled)) {
                const QuaderApiVoidResult result = executeDeletePopupCommand(editor, inputRouter, action.commandId);
                changed = changed || result.changed;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }

    return changed;
}

} // namespace

ViewportPanel::ViewportPanel() {
    panes_[viewportPaneIndex(ViewportPaneId::Perspective)].id = ViewportPaneId::Perspective;
    panes_[viewportPaneIndex(ViewportPaneId::Top)].id = ViewportPaneId::Top;
    panes_[viewportPaneIndex(ViewportPaneId::Front)].id = ViewportPaneId::Front;
    panes_[viewportPaneIndex(ViewportPaneId::Side)].id = ViewportPaneId::Side;
}

ViewportPanel::~ViewportPanel() {
    shutdown();
}

void ViewportPanel::initialize(RenderSystem& renderSystem) {
    shutdownComplete_ = false;
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
    if (shutdownComplete_) {
        return;
    }

    shutdownComplete_ = true;
    viewportInfoCachedDocument_ = nullptr;
    viewportInfoCachedAssets_ = nullptr;
    viewportInfoSceneStatsValid_ = false;
    for (Pane& paneState : panes_) {
        paneState.renderer.shutdown();
        paneState.initialized = false;
    }
    if (workspace_ != nullptr) {
        workspace_->clearPaneVisibility();
        workspace_ = nullptr;
    }
    activeRenderPanes_.clear();
}

void ViewportPanel::prepareViewports(
        ViewportWorkspaceService& workspace,
        SceneRenderBridge& renderBridge,
        const SceneDocument& document,
        const AssetRegistry& assets) {
    QDR_PROFILE_FUNCTION();
    workspace_ = &workspace;
    const float gridWorldSize = viewportGridWorldSizeForPreset(workspace.gridPreset());
    const ViewportSettings& settings = activeViewportSettings();
    const bool gridVisible = settings.boolValue(ViewportBoolSettingId::ShowGrid);
    const bool gizmosVisible = settings.boolValue(ViewportBoolSettingId::ShowGizmos);
    const bool overlaysVisible = settings.boolValue(ViewportBoolSettingId::ShowOverlays);
    for (ViewportPaneId paneId : activeRenderPanes_) {
        QDR_PROFILE_SCOPE("Viewport prepare pane");
        Pane& paneState = pane(paneId);
        ViewportWorkspacePaneState& paneWorkspace = workspace.pane(paneId);
        if (!paneState.initialized || !paneWorkspace.visibleThisFrame) {
            continue;
        }
        if (const EntityId root = document.sceneRoot(); root != InvalidEntityId) {
            if (const SceneSettingsComponent* settings = document.sceneSettings(root)) {
                paneState.renderer.applySceneSettings(
                        *settings,
                        paneWorkspace.shading == ViewportShadingMode::Rendered);
            }
        }
        paneState.renderer.updateCamera(paneWorkspace.camera);
        paneState.renderer.updateGroundGrid(paneWorkspace.camera, gridWorldSize, settings, gridVisible);
        paneState.renderer.setSceneLayerMask(paneWorkspace.shading == ViewportShadingMode::Rendered
                        ? kViewportRenderedSceneLayerMask
                        : kViewportShadedSceneLayerMask);
        paneState.renderer.setShadowingEnabled(paneWorkspace.shading == ViewportShadingMode::Rendered);
        paneState.renderer.setSceneVisible(paneWorkspace.shading != ViewportShadingMode::Wireframe);
        paneState.renderer.setOverlayVisible(overlaysVisible ||
                gizmosVisible ||
                paneWorkspace.shading == ViewportShadingMode::Wireframe);
        const bool selectionOverlaySuppressed = selectionOverlaySuppressedForInteraction();
        ViewportOverlayFrame overlayFrame = overlaysVisible && !selectionOverlaySuppressed && selectionOverlayCache_.valid()
                ? selectionOverlayCache_.frame()
                : ViewportOverlayFrame{};
        const ModelingSelectionMode overlayMode = selectionOverlayCache_.valid()
                ? selectionOverlayCache_.key().selectionMode
                : viewportSelectionModeToApp(inputRouter_.state().selectionMode);
        if (paneWorkspace.shading == ViewportShadingMode::Wireframe) {
            QDR_PROFILE_SCOPE("Viewport build wireframe overlay");
            overlayFrame = ViewportSceneOverlayBuilder::buildWireframeFrame(
                    std::move(overlayFrame),
                    overlayMode,
                    document,
                    assets);
        }
        if (paneId == interactionPaneId_) {
            const ViewportOverlayCamera overlayCamera =
                    ViewportCameraAdapter::overlayCamera(paneWorkspace.camera, paneWorkspace.renderSize);
            if (const quader_editor_core::TransformGizmoFrame* gizmoFrame =
                        interactionController_.transformGizmoFrame()) {
                if (gizmosVisible) {
                    ViewportTransformGizmoOverlayBuilder::append(overlayFrame, *gizmoFrame, overlayCamera);
                }
            }
            if (overlaysVisible) {
                ViewportSelectionMarqueeOverlayBuilder::append(
                        overlayFrame,
                        overlayCamera,
                        interactionController_.selectionMarquee());
            }
        }
        ViewportOverlayStyleResolver::apply(
                overlayFrame,
                settings,
                ViewportOverlayStyleContext{.selectionMode = overlaySelectionContext(overlayMode)});
        {
            QDR_PROFILE_SCOPE("Viewport submit overlay frame");
            renderBridge.setOverlayFrame(
                    viewportPaneIndex(paneId),
                    viewportGridLayerMask(viewportPaneIndex(paneId)),
                    std::move(overlayFrame),
                    ViewportCameraAdapter::overlayCamera(paneWorkspace.camera, paneWorkspace.renderSize));
        }
    }
}

void ViewportPanel::renderViewports(filament::Renderer& renderer) {
    QDR_PROFILE_FUNCTION();
    for (ViewportPaneId paneId : activeRenderPanes_) {
        QDR_PROFILE_SCOPE("Viewport render pane");
        Pane& paneState = pane(paneId);
        if (!paneState.initialized || workspace_ == nullptr || !workspace_->paneVisible(paneId)) {
            continue;
        }
        paneState.renderer.render(renderer);
    }
}

bool ViewportPanel::activateToolFromMenu(
        ViewportToolId tool,
        QuaderEditorApi& editor,
        ModelingSessionService& modeling) {
    LOG_INFO("Tools menu requested viewport tool {}", viewportToolId(tool));
    const ViewportTool* toolDefinition = findViewportTool(tool);
    const QuaderApiVoidResult command = toolDefinition != nullptr ?
            editor.tools().activate(tool) :
            QuaderApiVoidResult{
                    .ok = false,
                    .changed = false,
                    .message = "Unknown viewport tool.",
            };
    if (!command.ok) {
        LOG_ERROR("Viewport tool activation failed: {}", command.message.empty() ? "Command failed." : command.message);
        return false;
    }
    const ViewportToolId activeTool = viewportToolForModeling(modeling.toolStatus().activeTool);
    LOG_INFO("Viewport tool activated from menu: requested {}, active {}", viewportToolId(tool), viewportToolId(activeTool));
    inputRouter_.setActiveTool(activeTool);
    inputRouter_.setSelectionMode(appSelectionModeToViewport(toModelingSelectionMode(editor.selection().summary().mode)));
    inputRouter_.clearModalToolState();
    interactionController_.reset(&editor);
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

void ViewportPanel::syncActiveToolFromEditor(QuaderEditorApi& editor, ModelingSessionService& modeling) {
    const ViewportToolId activeTool = viewportToolForModeling(modeling.toolStatus().activeTool);
    if (inputRouter_.state().activeTool == activeTool) {
        return;
    }

    LOG_INFO("Viewport tool synchronized from editor: {}", viewportToolId(activeTool));
    inputRouter_.setActiveTool(activeTool);
    inputRouter_.clearModalToolState();
    interactionController_.reset(&editor);
    hoverController_.invalidate();
    selectionOverlayCache_.markDirty();
}

bool ViewportPanel::drawPaneChrome(
        Pane& paneState,
        ImVec2 itemMin,
        ImVec2 itemSize,
        const LocalizationService& localization) {
    ViewportWorkspaceService& viewportWorkspace = workspace();
    ViewportWorkspacePaneState& paneWorkspace = viewportWorkspace.pane(paneState.id);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 itemMax{itemMin.x + itemSize.x, itemMin.y + itemSize.y};
    const ImU32 dividerColor = IM_COL32(19, 19, 20, 255);
    const ImU32 focusedColor = IM_COL32(46, 46, 48, 242);
    drawList->AddRect(itemMin,
            itemMax,
            paneState.id == viewportWorkspace.focusedPane() ? focusedColor : dividerColor,
            0.0f,
            0,
            1.0f);

    constexpr float kComboHeight = 28.0f;
    constexpr float kComboGap = 6.0f;
    ImGui::PushID(static_cast<int>(viewportPaneIndex(paneState.id)));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(16, 18, 21, 210));

    const float viewDesiredWidth = viewportViewComboWidth(localization);
    const float shadingDesiredWidth = viewportShadingComboWidth(localization);
    const float availableWidth = std::max(0.0f, itemSize.x - 16.0f);
    const bool compact = availableWidth < viewDesiredWidth + shadingDesiredWidth + kComboGap;
    const float shadingWidth = compact ? std::min(shadingDesiredWidth, availableWidth) : shadingDesiredWidth;
    const float viewWidth = compact
            ? std::max(0.0f, availableWidth - shadingWidth - kComboGap)
            : viewDesiredWidth;
    const float comboGap = viewWidth >= 44.0f ? kComboGap : 0.0f;
    const ImVec2 comboPos{
            itemMax.x - 8.0f - viewWidth - comboGap - shadingWidth,
            itemMin.y + 7.0f,
    };
    const ImRect chromeRect(comboPos, ImVec2(itemMax.x - 8.0f, comboPos.y + kComboHeight));
    bool consumesInput = chromeRect.Contains(ImGui::GetMousePos());
    (void)imguiCloseComboPopupForClickInside(itemMin, itemMax, chromeRect.Min, chromeRect.Max);

    ImGui::SetCursorScreenPos(comboPos);
    if (viewWidth >= 44.0f) {
        ImGui::SetNextItemWidth(viewWidth);
        const ViewportView currentView = viewportWorkspace.view(paneState.id);
        const std::string preview = localizedViewportViewLabel(localization, currentView);
        if (quaderBeginCombo("##ViewportView", preview.c_str(), ImGuiComboFlags_HeightSmall)) {
            consumesInput = true;
            for (const ViewportView view : kViewportChromeViews) {
                const bool selected = currentView == view;
                const std::string label = localizedViewportViewLabel(localization, view);
                if (ImGui::Selectable(label.c_str(), selected)) {
                    setPaneView(paneState, view);
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine(0.0f, comboGap);
    }
    ImGui::SetNextItemWidth(shadingWidth);
    const std::string shadingPreview = localizedViewportShadingModeLabel(localization, paneWorkspace.shading);
    if (quaderBeginCombo("##ViewportShading", shadingPreview.c_str(), ImGuiComboFlags_HeightSmall)) {
        consumesInput = true;
        for (const ViewportShadingMode mode : kViewportChromeShadingModes) {
            const bool selected = paneWorkspace.shading == mode;
            const std::string label = localizedViewportShadingModeLabel(localization, mode);
            if (ImGui::Selectable(label.c_str(), selected)) {
                if (viewportWorkspace.setShadingMode(paneState.id, mode)) {
                    selectionOverlayCache_.markDirty();
                }
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    ImGui::PopID();
    return consumesInput;
}

void ViewportPanel::setPaneView(Pane& paneState, ViewportView view) {
    if (!workspace().setView(paneState.id, view)) {
        return;
    }
    hoverController_.invalidate();
    selectionOverlayCache_.markDirty();
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
        workspace().setSplitHandlePosition(activeSplitHandle_.id, normalizedPosition);
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

const ViewportSettings& ViewportPanel::activeViewportSettings() const {
    return viewportSettings_ != nullptr ? *viewportSettings_ : fallbackViewportSettings_;
}

void ViewportPanel::draw(UIContext& context) {
    viewportSettings_ = &context.viewportSettings;
    draw(context.renderBridge,
            context.document,
            context.assets,
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
        ModelingSessionService& modeling,
        AssetId defaultMaterial,
        QVec2 capturedMouseDelta,
        bool* viewportCursorCaptureRequested,
        QVec2* viewportCursorCapturePoint,
        const UIContext& uiContext) {
    QDR_PROFILE_FUNCTION();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    const std::string windowTitle =
            uiContext.localization.imguiLabel("ui.panel.viewport", uiPanelKey(UIPanelId::Viewport), "Viewport");
    const bool visible = ImGui::Begin(
            windowTitle.c_str(),
            nullptr,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(2);
    ViewportWorkspaceService& viewportWorkspace =
            QuaderViewportApiRuntimeAccess::workspace(uiContext.editor.viewports());
    workspace_ = &viewportWorkspace;
    inputRouter_.syncGridPresetSnapshot(viewportWorkspace.gridPreset());
    syncActiveToolFromEditor(uiContext.editor, modeling);
    if (!visible) {
        for (Pane& paneState : panes_) {
            renderBridge.setOverlayFrame(
                    viewportPaneIndex(paneState.id),
                    viewportGridLayerMask(viewportPaneIndex(paneState.id)),
                    {},
                    ViewportCameraAdapter::overlayCamera(viewportWorkspace.camera(paneState.id), {1.0f, 1.0f}));
        }
        lastFlyNavigationActive_ = false;
        viewportWorkspace.clearPaneVisibility();
        activeRenderPanes_.clear();
        ImGui::End();
        return;
    }

    ImVec2 available = ImGui::GetContentRegionAvail();
    available.x = std::max(available.x, 1.0f);
    available.y = std::max(available.y, 1.0f);
    const ImVec2 canvasMin = ImGui::GetCursorScreenPos();
    ImGui::Dummy(available);

    viewportWorkspace.clearPaneVisibility();
    activeRenderPanes_.clear();

    /**
     * Represents a Draw Pane value used by the ImGui editor presentation layer.
     */
    struct DrawPane {
        Pane* pane = nullptr;
        ImVec2 itemMin = {};
        ImVec2 itemSize = {};
        bool hovered = false;
        bool focusHovered = false;
        bool chromeConsumesInput = false;
    };
    std::vector<DrawPane> drawPanes;
    const std::vector<ViewportPanePlacement> placements = viewportWorkspace.activePanePlacements();
    drawPanes.reserve(placements.size());
    for (const ViewportPanePlacement& placement : placements) {
        QDR_PROFILE_SCOPE("Viewport draw pane image");
        Pane& paneState = pane(placement.paneId);
        const ImVec2 itemMin = ViewportPaneController::paneRectMin(canvasMin, available, placement.normalizedRect);
        const ImVec2 itemSize = ViewportPaneController::paneRectSize(available, placement.normalizedRect);
        const QVec2 renderSize = {std::max(itemSize.x, 1.0f), std::max(itemSize.y, 1.0f)};
        viewportWorkspace.setPaneRenderSize(placement.paneId, renderSize);
        viewportWorkspace.setPaneVisible(placement.paneId, true);
        if (!uiContext.viewportRenderResizeSuspended) {
            paneState.renderer.resize(
                    static_cast<std::uint32_t>(renderSize.x),
                    static_cast<std::uint32_t>(renderSize.y));
        }
        activeRenderPanes_.push_back(placement.paneId);

        ImGui::SetCursorScreenPos(itemMin);
        ImGui::PushID(static_cast<int>(viewportPaneIndex(placement.paneId)));
        ImGui::SetNextItemAllowOverlap();
        ImGui::Image(static_cast<ImTextureID>(paneState.renderer.textureHandle()), itemSize, {0.0f, 1.0f}, {1.0f, 0.0f});
        ImGui::GetWindowDrawList()->AddImage(
                static_cast<ImTextureID>(paneState.renderer.overlayTextureHandle()),
                itemMin,
                {itemMin.x + itemSize.x, itemMin.y + itemSize.y},
                {0.0f, 1.0f},
                {1.0f, 0.0f});
        DrawPane drawPane{
                .pane = &paneState,
                .itemMin = ImGui::GetItemRectMin(),
                .itemSize = {ImGui::GetItemRectSize().x, ImGui::GetItemRectSize().y},
                .hovered = ImGui::IsItemHovered(),
                .focusHovered = ImGui::IsItemHovered(),
        };
        drawPanes.push_back(drawPane);
        ImGui::PopID();
    }

    const std::vector<ViewportSplitHandle> splitHandles = viewportWorkspace.activeSplitHandles();
    const bool splitConsumed = handleSplitInput(canvasMin, available, splitHandles);
    DrawPane* inputPane = nullptr;
    const bool transformDragLocksInput = interactionController_.dragging();
    const bool navigationGizmoDragLocksInput = navigationGizmo_.inputLocked();
    for (DrawPane& drawPane : drawPanes) {
        drawPane.chromeConsumesInput =
                drawPaneChrome(*drawPane.pane, drawPane.itemMin, drawPane.itemSize, uiContext.localization);
    }
    for (DrawPane& drawPane : drawPanes) {
        if (!transformDragLocksInput && !navigationGizmoDragLocksInput) {
            if (drawPane.focusHovered && !drawPane.chromeConsumesInput && !splitConsumed) {
                viewportWorkspace.focusPane(drawPane.pane->id);
            } else if (drawPane.focusHovered && !drawPane.chromeConsumesInput && focusController_.inputActive()) {
                viewportWorkspace.focusPane(drawPane.pane->id);
            }
        }
        if (transformDragLocksInput && drawPane.pane->id == interactionPaneId_) {
            inputPane = &drawPane;
        } else if (navigationGizmoDragLocksInput && drawPane.pane->id == navigationGizmo_.lockedPaneId()) {
            inputPane = &drawPane;
        } else if (!transformDragLocksInput &&
                !navigationGizmoDragLocksInput &&
                drawPane.hovered &&
                !drawPane.chromeConsumesInput &&
                !splitConsumed) {
            inputPane = &drawPane;
        }
    }
    if (inputPane == nullptr) {
        for (DrawPane& drawPane : drawPanes) {
            if (drawPane.pane->id == viewportWorkspace.focusedPane()) {
                inputPane = &drawPane;
                break;
            }
        }
    }
    if (inputPane != nullptr) {
        QDR_PROFILE_SCOPE("Viewport draw input pane contents");
        const bool inputOwnsActiveGizmoDrag =
                transformDragLocksInput && inputPane->pane->id == interactionPaneId_;
        const bool inputOwnsActiveNavigationGizmoDrag =
                navigationGizmoDragLocksInput && inputPane->pane->id == navigationGizmo_.lockedPaneId();
        drawPaneContents(*inputPane->pane,
                document,
                assets,
                modeling,
                defaultMaterial,
                capturedMouseDelta,
                viewportCursorCaptureRequested,
                viewportCursorCapturePoint,
                uiContext,
                inputPane->itemMin,
                inputPane->itemSize,
                (inputPane->hovered && !inputPane->chromeConsumesInput && !splitConsumed) ||
                        inputOwnsActiveGizmoDrag ||
                        inputOwnsActiveNavigationGizmoDrag,
                (inputPane->focusHovered && !inputPane->chromeConsumesInput && !splitConsumed) ||
                        inputOwnsActiveGizmoDrag ||
                        inputOwnsActiveNavigationGizmoDrag);
    }
    for (DrawPane& drawPane : drawPanes) {
        if (&drawPane == inputPane) {
            continue;
        }
        drawPaneDecorations(
                *drawPane.pane,
                document,
                assets,
                modeling,
                uiContext.selection,
                uiContext.editor,
                drawPane.itemMin,
                drawPane.itemSize);
    }
    if (activeViewportSettings().boolValue(ViewportBoolSettingId::ShowInfo)) {
        for (const DrawPane& drawPane : drawPanes) {
            if (drawPane.pane->id != viewportWorkspace.focusedPane()) {
                continue;
            }
            drawViewportInfoOverlay(document, assets, uiContext, drawPane.itemMin, drawPane.itemSize);
            break;
        }
    }
    refreshCachedSelectionOverlayFrame(document, assets, modeling);
    ImGui::SetCursorScreenPos({canvasMin.x, canvasMin.y + available.y});
    ImGui::End();
}

void ViewportPanel::drawPaneDecorations(
        Pane& paneState,
        SceneDocument& document,
        const AssetRegistry& assets,
        ModelingSessionService& modeling,
        EditorSelectionService& selection,
        QuaderEditorApi& editor,
        ImVec2 itemMin,
        ImVec2 itemSize) {
    ViewportCameraController& camera = workspace().camera(paneState.id);
    navigationGizmo_.draw(camera, paneState.id, itemMin, itemSize, {}, nullptr, nullptr, false);
    (void)document;
    (void)assets;
    (void)modeling;
    (void)selection;
    (void)editor;
}

void ViewportPanel::drawViewportInfoOverlay(
        const SceneDocument& document,
        const AssetRegistry& assets,
        const UIContext& uiContext,
        ImVec2 itemMin,
        ImVec2 itemSize) {
    const std::uint64_t sceneRevision = document.revision();
    const std::uint64_t assetRevision = assets.revision();
    const bool cacheHit = viewportInfoSceneStatsValid_ &&
            viewportInfoCachedDocument_ == &document &&
            viewportInfoCachedAssets_ == &assets &&
            viewportInfoCachedSceneRevision_ == sceneRevision &&
            viewportInfoCachedAssetRevision_ == assetRevision;
    ViewportInfoSceneStats sceneStats;
    if (cacheHit) {
        sceneStats.objects = viewportInfoCachedObjects_;
        sceneStats.primitives = viewportInfoCachedPrimitives_;
        sceneStats.drawCalls = viewportInfoCachedDrawCalls_;
    } else {
        QDR_PROFILE_SCOPE("Viewport info collect scene stats");
        sceneStats = collectViewportInfoSceneStats(document, assets);
        viewportInfoCachedDocument_ = &document;
        viewportInfoCachedAssets_ = &assets;
        viewportInfoCachedSceneRevision_ = sceneRevision;
        viewportInfoCachedAssetRevision_ = assetRevision;
        viewportInfoCachedObjects_ = sceneStats.objects;
        viewportInfoCachedPrimitives_ = sceneStats.primitives;
        viewportInfoCachedDrawCalls_ = sceneStats.drawCalls;
        viewportInfoSceneStatsValid_ = true;
    }
    const RenderFramePerformanceStats& renderStats = uiContext.renderPerformance;

    const bool fpsAvailable = uiContext.frameTiming.rawDeltaSeconds > 0.0001;
    const double fps = fpsAvailable ? 1.0 / uiContext.frameTiming.rawDeltaSeconds : 0.0;

    const std::string unavailable = uiContext.localization.text("ui.viewport.info.unavailable", "--");
    const std::string cpuValue = renderStats.cpuFrameMsAvailable
            ? formatViewportInfoMilliseconds(renderStats.cpuFrameMs)
            : unavailable;
    const std::string gpuValue = renderStats.gpuFrameMsAvailable
            ? formatViewportInfoMilliseconds(renderStats.gpuFrameMs)
            : unavailable;
    const std::string fpsValue = fpsAvailable ? formatViewportInfoFramesPerSecond(fps) : unavailable;

    const std::array<std::string, 6> lines{{
            viewportInfoLine(uiContext.localization.text("ui.viewport.info.fps", "FPS"), fpsValue),
            viewportInfoLine(uiContext.localization.text("ui.viewport.info.cpu", "CPU"), cpuValue),
            viewportInfoLine(uiContext.localization.text("ui.viewport.info.gpu", "GPU"), gpuValue),
            viewportInfoLine(
                    uiContext.localization.text("ui.viewport.info.objects", "Objects"),
                    formatViewportInfoNumber(sceneStats.objects)),
            viewportInfoLine(
                    uiContext.localization.text("ui.viewport.info.primitives", "Primitives"),
                    formatViewportInfoNumber(sceneStats.primitives)),
            viewportInfoLine(
                    uiContext.localization.text("ui.viewport.info.draw_calls", "Draw Calls"),
                    formatViewportInfoNumber(sceneStats.drawCalls)),
    }};
    drawViewportInfoText(
            *ImGui::GetWindowDrawList(),
            itemMin,
            itemSize,
            std::span<const std::string>(lines.data(), lines.size()));
}

void ViewportPanel::refreshCachedSelectionOverlayFrame(
        const SceneDocument& document,
        const AssetRegistry& assets,
        ModelingSessionService& modeling) {
    if (selectionOverlaySuppressedForInteraction()) {
        return;
    }
    const ModelingSelectionSummary overlaySummary = [&]() {
        QDR_PROFILE_SCOPE("Viewport selection overlay summary");
        return modeling.selectionSummary();
    }();
    const std::uint64_t overlaySignature = [&]() {
        QDR_PROFILE_SCOPE("Viewport selection overlay signature");
        return modeling.selectionOverlaySignature();
    }();
    const ViewportPaneOverlayCacheKey key{
            .contentRevision = overlaySummary.contentRevision,
            .selectionRevision = overlaySummary.selectionRevision,
            .sceneRevision = document.revision(),
            .assetRevision = assets.revision(),
            .overlaySignature = overlaySignature,
            .selectionMode = overlaySummary.mode,
    };
    const ViewportPaneOverlayCacheRefreshReason refreshReason = [&]() {
        QDR_PROFILE_SCOPE("Viewport selection overlay cache check");
        return selectionOverlayCache_.refreshReason(key);
    }();
    if (refreshReason == ViewportPaneOverlayCacheRefreshReason::None) {
        return;
    }

    auto rebuildCache = [&]() {
        ViewportOverlayFrame sourceFrame = [&]() {
            QDR_PROFILE_SCOPE("Viewport selection overlay source frame");
            return modeling.selectionOverlayFrame();
        }();
        ViewportOverlayFrame frame = [&]() {
            QDR_PROFILE_SCOPE("Viewport selection overlay project frame");
            return ViewportSceneOverlayBuilder::buildSelectionFrame(
                    std::move(sourceFrame),
                    document,
                    assets);
        }();
        selectionOverlayCache_.update(std::move(frame), key);
    };

    switch (refreshReason) {
    case ViewportPaneOverlayCacheRefreshReason::Dirty: {
        QDR_PROFILE_SCOPE("Viewport selection overlay cache refresh dirty");
        rebuildCache();
        break;
    }
    case ViewportPaneOverlayCacheRefreshReason::Invalid: {
        QDR_PROFILE_SCOPE("Viewport selection overlay cache refresh invalid");
        rebuildCache();
        break;
    }
    case ViewportPaneOverlayCacheRefreshReason::ContentRevision: {
        QDR_PROFILE_SCOPE("Viewport selection overlay cache refresh content revision");
        rebuildCache();
        break;
    }
    case ViewportPaneOverlayCacheRefreshReason::SelectionRevision: {
        QDR_PROFILE_SCOPE("Viewport selection overlay cache refresh selection revision");
        rebuildCache();
        break;
    }
    case ViewportPaneOverlayCacheRefreshReason::SceneRevision: {
        QDR_PROFILE_SCOPE("Viewport selection overlay cache refresh scene revision");
        rebuildCache();
        break;
    }
    case ViewportPaneOverlayCacheRefreshReason::AssetRevision: {
        QDR_PROFILE_SCOPE("Viewport selection overlay cache refresh asset revision");
        rebuildCache();
        break;
    }
    case ViewportPaneOverlayCacheRefreshReason::OverlaySignature: {
        QDR_PROFILE_SCOPE("Viewport selection overlay cache refresh overlay signature");
        rebuildCache();
        break;
    }
    case ViewportPaneOverlayCacheRefreshReason::SelectionMode: {
        QDR_PROFILE_SCOPE("Viewport selection overlay cache refresh selection mode");
        rebuildCache();
        break;
    }
    case ViewportPaneOverlayCacheRefreshReason::None:
        break;
    }
}

bool ViewportPanel::selectionOverlaySuppressedForInteraction() const {
    return inputRouter_.navigating() ||
            interactionController_.dragging() ||
            navigationGizmo_.inputLocked() ||
            splitHandleDragging_;
}

ViewportWorkspaceService& ViewportPanel::workspace() {
    if (workspace_ == nullptr) {
        throw std::logic_error("ViewportPanel requires an attached ViewportWorkspaceService.");
    }
    return *workspace_;
}

const ViewportWorkspaceService& ViewportPanel::workspace() const {
    if (workspace_ == nullptr) {
        throw std::logic_error("ViewportPanel requires an attached ViewportWorkspaceService.");
    }
    return *workspace_;
}

void ViewportPanel::drawPaneContents(Pane& paneState,
        SceneDocument& document,
        AssetRegistry& assets,
        ModelingSessionService& modeling,
        AssetId defaultMaterial,
        QVec2 capturedMouseDelta,
        bool* viewportCursorCaptureRequested,
        QVec2* viewportCursorCapturePoint,
        const UIContext& uiContext,
        ImVec2 itemMin,
        ImVec2 itemSize,
        bool hovered,
        bool focusHovered) {
    QDR_PROFILE_FUNCTION();
    ViewportWorkspaceService& viewportWorkspace = workspace();
    ViewportCameraController& camera = viewportWorkspace.camera(paneState.id);
    const ImVec2 itemMax{itemMin.x + itemSize.x, itemMin.y + itemSize.y};
    const ViewportFocusState focusState = focusController_.resolve(focusHovered, uiContext, UIPanelId::Viewport);
    auto modalToolOwnsViewport = [&]() {
        return inputRouter_.activeToolOwnsViewport() ||
                inputRouter_.hasActiveModalPreview() ||
                modeling.modalToolOwnsViewport();
    };

    const bool useCapturedMouseDelta = inputRouter_.flyCursorCaptureActive();
    ViewportInputFrame input;
    {
        QDR_PROFILE_SCOPE("Viewport build input frame");
        input = inputFrameBuilder_.build({
                .hovered = hovered,
                .focused = focusState.focused,
                .itemMin = itemMin,
                .itemSize = itemSize,
                .routeActiveNavigationInput = inputRouter_.navigating(),
                .useCapturedMouseDelta = useCapturedMouseDelta,
                .capturedMouseDelta = capturedMouseDelta,
                .deltaSeconds = static_cast<float>(std::max(uiContext.frameTiming.deltaSeconds, 0.0)),
        });
    }
    const ModelingToolSettingsTarget toolSettingsTarget = modeling.toolSettingsTarget();
    input.navigationActive = inputRouter_.navigating();
    input.flyNavigationActive = inputRouter_.flyNavigating();
    input.orbitNavigationActive = inputRouter_.orbitNavigating();
    input.panNavigationActive = inputRouter_.panNavigating();
    input.modalToolActive = inputRouter_.hasActiveModalPreview() || modeling.modalToolOwnsViewport();
    input.activeToolOwnsViewport = inputRouter_.activeToolOwnsViewport();
    input.operationPreviewActive = toolSettingsTarget.operationPreviewActive;
    input.gridPreset = viewportWorkspace.gridPreset();
    {
        QDR_PROFILE_SCOPE("Viewport resolve keybindings");
        resolveKeybindingsForFrame(
                uiContext.editor,
                inputRouter_.state(),
                input,
                uiContext.commandShortcutConsumed);
    }
    const ViewportInputResult result = [&]() {
        QDR_PROFILE_SCOPE("Viewport route input frame");
        return inputRouter_.handleFrame(input, camera);
    }();
    const ViewportShortcutControllerResult operationPreviewShortcut =
            shortcutController_.handleOperationPreviewShortcut(result, toolSettingsTarget, uiContext.editor);
    const bool operationPreviewShortcutConsumed = operationPreviewShortcut.consumed;
    if (operationPreviewShortcut.invalidateHover) {
        hoverController_.invalidate();
    }
    if (operationPreviewShortcut.markOverlayDirty) {
        selectionOverlayCache_.markDirty();
    }
    if (result.maximizedToggleRequested && !operationPreviewShortcutConsumed) {
        if (hovered) {
            viewportWorkspace.focusPane(paneState.id);
        }
        viewportWorkspace.toggleFocusedPaneMaximized();
    }
    if (result.gridPresetChanged) {
        viewportWorkspace.setGridPreset(result.requestedGridPreset);
        inputRouter_.syncGridPresetSnapshot(viewportWorkspace.gridPreset());
    }
    if (result.viewChanged) {
        (void)viewportWorkspace.setView(paneState.id, result.requestedView);
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
    QVec2 pendingContextMenuLocalMouse = {};
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
    const bool selectionModeShortcutHandled =
            result.shortcutHandled &&
            result.shortcut.kind == ViewportShortcutKind::SetSelectionMode;
    if (selectionModeShortcutHandled) {
        const QuaderApiVoidResult command =
                uiContext.editor.selection().setMode(toApiSelectionMode(viewportSelectionModeToApp(result.shortcut.selectionMode)));
        if (!command.ok) {
            LOG_ERROR("Selection mode change failed: {}", command.message);
        }
        inputRouter_.setSelectionMode(appSelectionModeToViewport(toModelingSelectionMode(uiContext.editor.selection().summary().mode)));
        inputRouter_.clearModalDragState();
        interactionController_.reset(&uiContext.editor);
        hoverController_.invalidate();
        selectionOverlayCache_.markDirty();
    }
    if (result.toolChanged) {
        LOG_INFO("Viewport tool changed to {}", viewportToolId(inputRouter_.state().activeTool));
        inputRouter_.clearModalDragState();
        interactionController_.reset(&uiContext.editor);
        hoverController_.invalidate();
        selectionOverlayCache_.markDirty();
        const ViewportTool* activeTool = inputRouter_.activeTool();
        const QuaderApiVoidResult command = activeTool != nullptr ?
                uiContext.editor.tools().activate(inputRouter_.state().activeTool) :
                QuaderApiVoidResult{
                        .ok = false,
                        .changed = false,
                        .message = "No active viewport tool.",
                };
        if (!command.ok) {
            LOG_ERROR("Viewport tool activation failed: {}", command.message);
            inputRouter_.setActiveTool(ViewportToolId::Select);
            (void)uiContext.editor.tools().activate(ViewportToolId::Select);
        } else {
            inputRouter_.setSelectionMode(appSelectionModeToViewport(toModelingSelectionMode(uiContext.editor.selection().summary().mode)));
        }
        inputRouter_.clearModalToolState();
    }

    if (!operationPreviewShortcutConsumed) {
        const ViewportShortcutControllerResult shortcut =
                shortcutController_.handleModelingShortcut(result, uiContext.editor);
        if (shortcut.commandExecuted) {
            inputRouter_.setSelectionMode(appSelectionModeToViewport(toModelingSelectionMode(uiContext.editor.selection().summary().mode)));
        }
        if (shortcut.invalidateHover) {
            hoverController_.invalidate();
        }
        if (shortcut.markOverlayDirty) {
            selectionOverlayCache_.markDirty();
        }
    }
    if (!operationPreviewShortcutConsumed &&
            result.shortcutHandled &&
            result.shortcut.kind == ViewportShortcutKind::DeletePopup) {
        const ModelingSelectionSummary deleteSummary = toModelingSelectionSummary(uiContext.editor.selection().summary());
        if (deleteSummary.mode == ModelingSelectionMode::Object ||
                deleteSummary.mode == ModelingSelectionMode::Group ||
                sceneAwareDeleteHasSelectedRoot(document)) {
            deleteObjectsPopupOpen_ = true;
            deleteObjectsPopupPositionPending_ = true;
            deleteObjectsPopupPosition_ = ImVec2(itemMin.x + itemSize.x * 0.5f, itemMin.y + itemSize.y * 0.25f);
        } else {
            ImGui::OpenPopup("ViewportDeleteElementsPopup");
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
        interactionController_.reset(&uiContext.editor);
        hoverController_.invalidate();
        selectionOverlayCache_.markDirty();
    };

    bool cancelConsumed = false;
    if (!operationPreviewShortcutConsumed && result.cancelRequested) {
        if (interactionController_.dragging()) {
            (void)interactionController_.cancelActiveDrag(uiContext.editor);
            selectionOverlayCache_.markDirty();
            cancelConsumed = true;
        }

        if (modalToolOwnsViewport()) {
            cancelActiveModalTool(true);
            cancelConsumed = true;
        }
    }

    const bool suppressHoverWork = result.navigationActive || result.cameraChanged;
    const bool navigationGizmoConsumed = [&]() {
        QDR_PROFILE_SCOPE("Viewport draw navigation gizmo");
        return navigationGizmo_.draw(
                camera,
                paneState.id,
                itemMin,
                itemSize,
                capturedMouseDelta,
                viewportCursorCaptureRequested,
                viewportCursorCapturePoint);
    }();
    if (suppressHoverWork || navigationGizmoConsumed || result.gridPresetChanged) {
        hoverController_.invalidate();
    }
    const ImGuiIO& io = ImGui::GetIO();
    const bool leftClicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const bool leftDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    const bool leftReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    const ImVec2 leftClickScreen = io.MouseClickedPos[ImGuiMouseButton_Left];
    const QVec2 leftClickLocal = leftClicked ?
            QVec2{leftClickScreen.x - itemMin.x, leftClickScreen.y - itemMin.y} :
            input.mouseLocal;
    const int leftClickCount = std::max(1, ImGui::GetMouseClickedCount(ImGuiMouseButton_Left));
    const ViewportInteractionFrame interactionFrame{
            .inputRouter = inputRouter_,
            .camera = camera,
            .settings = activeViewportSettings(),
            .input = input,
            .inputResult = result,
            .leftClickLocal = leftClickLocal,
            .leftClickCount = leftClickCount,
            .leftPressed = leftClicked,
            .leftDown = leftDown,
            .leftReleased = leftReleased,
    };
    const ViewportBeforeToolInteractionResult beforeTool = [&]() {
        QDR_PROFILE_SCOPE("Viewport before tool interaction");
        return cancelConsumed ? ViewportBeforeToolInteractionResult{} :
                interactionController_.handleBeforeActiveTool(uiContext.editor, interactionFrame);
    }();
    if (interactionController_.dragging() || beforeTool.gizmoConsumed) {
        interactionPaneId_ = paneState.id;
    }
    const bool gizmoConsumed = cancelConsumed || navigationGizmoConsumed || beforeTool.gizmoConsumed;
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

    const ModelingSelectionSummary hoverSelectionSummary = toModelingSelectionSummary(uiContext.editor.selection().summary());
    auto makeViewportHoverWorkKey = [&](bool consumedByGizmo) {
        const ViewportSettings& settings = activeViewportSettings();
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
                .vertexRadius = settings.floatValue(ViewportFloatSettingId::PickVertexRadius),
                .edgeRadius = settings.floatValue(ViewportFloatSettingId::PickEdgeRadius),
                .sceneRevision = document.revision(),
                .assetRevision = assets.revision(),
                .contentRevision = hoverSelectionSummary.contentRevision,
                .selectionRevision = hoverSelectionSummary.selectionRevision,
        });
    };
    auto makeModelingToolEventAt = [&](QVec2 mouseLocal, bool dragging = false) {
        const ViewportSettings& settings = activeViewportSettings();
        const ViewportRay worldRay = camera.rayFromViewportPoint(mouseLocal);
        return ModelingToolRayEvent{
                .screenPosition = mouseLocal,
                .ray = {
                        .origin = worldRay.origin,
                        .direction = worldRay.direction,
                },
                .camera = ViewportCameraAdapter::modelingCamera(camera, {itemSize.x, itemSize.y}),
                .gridSize = std::max(static_cast<double>(gridSizeForPreset(inputRouter_.state().gridPreset)), 0.0001),
                .vertexRadius = settings.floatValue(ViewportFloatSettingId::PickVertexRadius),
                .edgeRadius = settings.floatValue(ViewportFloatSettingId::PickEdgeRadius),
                .snapEnabled = viewportGridSnapActive(settings, input.modifiers.primary),
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
    const bool finalizePressed = input.modalToolFinalizePressed;
    const bool modeCyclePressed = input.modalToolModeCyclePressed;

    ViewportBoxToolContext boxToolContext{
            .defaultMaterial = defaultMaterial,
    };
    boxToolContext.makeEvent = [&]() {
        const float gridSize = gridSizeForPreset(inputRouter_.state().gridPreset);
        return modelingBoxEvent(
                camera,
                input,
                gridSize,
                viewportGridSnapActive(activeViewportSettings(), input.modifiers.primary));
    };
    boxToolContext.commitPreview = [&](std::string name, AssetId material) {
        const QuaderApiResult<QuaderModelingObjectRef> result =
                uiContext.editor.modeling().commitBoxPreview(std::move(name), material);
        ModelingCreateBoxResult committed{
                .ok = result.ok,
                .changed = result.changed,
                .message = result.message,
        };
        if (result.ok && result.changed) {
            committed.object = {
                    .modelingObjectId = result.value.modelingObjectId,
                    .entity = result.value.entity,
                    .mesh = result.value.mesh,
            };
        }
        return committed;
    };
    boxToolContext.nextMeshName = [&]() {
        return SceneEntityNaming::nextNameForKind(document, SceneEntityKind::Mesh);
    };

    ViewportPointToolContext pointToolContext;
    pointToolContext.makeEventAt = makeModelingToolEventAt;
    pointToolContext.hitDrawnPlaneCapturedPoint = [&](const std::array<QVec3, 3>& points, int capturedPointCount) {
        return hitDrawnPlaneCapturedPoint(points, capturedPointCount, leftClickLocal, camera, itemMin, itemSize);
    };
    pointToolContext.hitDrawnPlaneLine = [&](const std::array<QVec3, 3>& points, int capturedPointCount, bool hasLine) {
        return hitDrawnPlaneLine(points, capturedPointCount, hasLine, leftClickLocal, camera, itemMin, itemSize);
    };

    ViewportToolContext toolContext{
            .state = const_cast<ViewportToolState&>(inputRouter_.state()),
            .camera = camera,
            .modeling = modeling,
            .document = &document,
            .settings = &activeViewportSettings(),
            .box = &boxToolContext,
            .point = &pointToolContext,
    };
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
    toolContext.setAppSelectionMode = [&](ModelingSelectionMode mode) {
        const QuaderApiVoidResult command = uiContext.editor.selection().setMode(toApiSelectionMode(mode));
        if (!command.ok) {
            LOG_ERROR("Selection mode change failed: {}", command.message.empty() ? "Command failed." : command.message);
        }
        inputRouter_.setSelectionMode(appSelectionModeToViewport(toModelingSelectionMode(uiContext.editor.selection().summary().mode)));
    };
    toolContext.handleToolCommandResult = handleToolCommandResult;

    const bool canInteractWithActiveTool = hovered && !gizmoConsumed && !result.navigationActive;
    const ViewportToolResult activeToolFrameResult = [&]() {
        QDR_PROFILE_SCOPE("Viewport active tool frame");
        return inputRouter_.handleActiveToolEvent(toolContext, ViewportToolEvent{
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
    }();
    const ModelingBoxPreview& boxPreview = inputRouter_.boxPreview();
    if (boxPreview.dragState != ModelingBoxDragState::Height) {
        modalToolOverlayRenderer_.drawBoxPreview(ImGui::GetWindowDrawList(), boxPreview, camera, itemMin, itemSize);
    }

    {
        QDR_PROFILE_SCOPE("Viewport after tool interaction");
        interactionController_.handleAfterActiveTool(
                uiContext.editor,
                interactionFrame,
                activeToolFrameResult,
                beforeTool);
    }

    const bool selectionFallbackActive = inputRouter_.activeToolAllowsSelectionFallback();
    if (!activeToolFrameResult.handled &&
            selectionFallbackActive &&
            pendingContextMenuClick &&
            !gizmoConsumed) {
        ViewportInputFrame contextInput = input;
        contextInput.mouseLocal = pendingContextMenuLocalMouse;
        const ViewportInteractionFrame contextFrame{
                .inputRouter = inputRouter_,
                .camera = camera,
                .settings = activeViewportSettings(),
                .input = contextInput,
                .inputResult = result,
        };
        const QuaderSelectionSummary summary =
                interactionController_.pickContextMenuTarget(uiContext.editor, contextFrame);
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
    }

    {
        QDR_PROFILE_SCOPE("Viewport refresh selection overlay");
        refreshCachedSelectionOverlayFrame(document, assets, modeling);
    }
    modalToolOverlayRenderer_.drawFlyNavigationOverlay(
            ImGui::GetWindowDrawList(),
            itemMin,
            itemSize,
            result.flyHudActive,
            result.flyHudActive && !camera.flySpeedIsDefault(),
            result.flyHudActive,
            camera.flySpeed(),
            uiContext.localization);
    contextMenuPresenter_.drawOperationsPopup(uiContext.editor, modeling, inputRouter_, uiContext.localization, modalToolOwnsViewport());
    if (drawViewportDeletePopup(
                deleteObjectsPopupOpen_,
                deleteObjectsPopupPositionPending_,
                deleteObjectsPopupPosition_,
                uiContext.editor,
                inputRouter_,
                uiContext.localization)) {
        hoverController_.invalidate();
        selectionOverlayCache_.markDirty();
    }
}

} // namespace quader
