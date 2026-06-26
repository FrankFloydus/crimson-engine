////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/controllers/ViewportHost.h"

#include "api/QuaderApiTypeConversions.h"
#include "api/QuaderEditorApi.h"
#include "core/Logger.h"
#include "editor/input/QuaderInputBinding.h"
#include "editor/modeling/services/ModelingSessionService.h"
#include "editor/render/QuaderRenderApiRuntimeAccess.h"
#include "editor/modeling/services/SelectionModeAdapter.h"
#include "editor/tools/ViewportToolCatalog.h"
#include "editor/viewport/QuaderViewportApiRuntimeAccess.h"
#include "platform/PlatformWindow.h"
#include "platform/SdlEventUtils.h"
#include "platform/SdlInputAdapter.h"
#include "render/RenderLayers.h"
#include "render/ViewportOverlayFrame.h"
#include "render/overlay/ViewportBoxOverlayBuilder.h"
#include "render/overlay/ViewportSelectionMarqueeOverlayBuilder.h"
#include "render/overlay/ViewportTransformGizmoOverlayBuilder.h"
#include "scene/SceneBounds.h"
#include "scene/SceneEntityNaming.h"
#include "ui/ViewportShortcutController.h"
#include "viewport/ViewportCameraAdapter.h"
#include "viewport/ViewportWorkspaceService.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_video.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace quader {
namespace {

QuaderInputChord keyboardInputChord(QuaderKeyCode key, ViewportModifiers modifiers) {
    return {
            .primary = modifiers.primary,
            .shift = modifiers.shift,
            .alt = modifiers.alt,
            .device = QuaderInputDevice::Keyboard,
            .key = key,
    };
}

QuaderInputChord mouseInputChord(
        QuaderMouseButton button,
        QuaderInputGesture gesture,
        ViewportModifiers modifiers) {
    return {
            .primary = modifiers.primary,
            .shift = modifiers.shift,
            .alt = modifiers.alt,
            .device = QuaderInputDevice::Mouse,
            .mouseButton = button,
            .gesture = gesture,
    };
}

QuaderInputChord wheelInputChord(QuaderInputGesture gesture, ViewportModifiers modifiers) {
    return {
            .primary = modifiers.primary,
            .shift = modifiers.shift,
            .alt = modifiers.alt,
            .device = QuaderInputDevice::Mouse,
            .gesture = gesture,
    };
}

void appendActiveMouseDragChords(
        const std::unordered_map<QuaderMouseButton, QuaderInputChord>& dragChordsDown,
        std::vector<QuaderInputChord>& chords) {
    for (const auto& [button, chord] : dragChordsDown) {
        (void)button;
        chords.push_back(chord);
    }
}

void appendActiveKeyboardChords(
        const std::unordered_set<QuaderKeyCode>& keysDown,
        ViewportModifiers modifiers,
        std::vector<QuaderInputChord>& chords) {
    for (QuaderKeyCode key : keysDown) {
        chords.push_back(keyboardInputChord(key, modifiers));
    }
}

void appendWheelChords(
        float wheelY,
        ViewportModifiers modifiers,
        std::vector<QuaderInputChord>& chords) {
    if (wheelY == 0.0f) {
        return;
    }
    chords.push_back(wheelInputChord(QuaderInputGesture::Wheel, modifiers));
    chords.push_back(wheelInputChord(
            wheelY > 0.0f ? QuaderInputGesture::WheelUp : QuaderInputGesture::WheelDown,
            modifiers));
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

QuaderApiVoidResult viewportRuntimeStatus(bool changed) {
    return {
            .ok = true,
            .changed = changed,
            .mutationKind = changed ? QuaderMutationKind::Runtime : QuaderMutationKind::None,
    };
}

void installViewportRuntimeHooks(
        QuaderEditorApi& editor,
        SceneDocument& document,
        const AssetRegistry& assets,
        std::function<QuaderApiVoidResult()> spawnWindow) {
    QuaderEditorApi* editorPtr = &editor;
    SceneDocument* documentPtr = &document;
    const AssetRegistry* assetsPtr = &assets;
    editor.viewports().setRuntimeHooks(QuaderViewportRuntimeHooks{
            .frameSelection = [editorPtr, documentPtr, assetsPtr](ViewportPaneId pane) {
                ViewportWorkspaceService& workspace = QuaderViewportApiRuntimeAccess::workspace(editorPtr->viewports());
                focusDocument(*documentPtr, *assetsPtr, workspace.camera(pane));
                return viewportRuntimeStatus(true);
            },
            .focusOrigin = [editorPtr](ViewportPaneId pane) {
                ViewportWorkspaceService& workspace = QuaderViewportApiRuntimeAccess::workspace(editorPtr->viewports());
                return viewportRuntimeStatus(workspace.focusOrigin(pane));
            },
            .spawnWindow = std::move(spawnWindow),
    });
}

bool viewportOverlayFrameVisible(const ViewportOverlayFrame& frame) {
    return !frame.lines.empty() || !frame.triangles.empty() || !frame.points.empty();
}

float distanceSquared(QVec2 from, QVec2 to) {
    const QVec2 delta = to - from;
    return delta.x * delta.x + delta.y * delta.y;
}

float distanceToSegmentSquared(QVec2 point, QVec2 start, QVec2 end) {
    const QVec2 segment = end - start;
    const float lengthSquared = segment.x * segment.x + segment.y * segment.y;
    if (lengthSquared <= 0.000001f) {
        return distanceSquared(point, start);
    }

    const QVec2 relative = point - start;
    const float t = std::clamp(
            (relative.x * segment.x + relative.y * segment.y) / lengthSquared,
            0.0f,
            1.0f);
    return distanceSquared(point, start + segment * t);
}

ViewportOverlayFrame buildToolOverlayFrame(
        const ViewportInputRouter& inputRouter,
        const ViewportInteractionController& interactions,
        const ViewportOverlayCamera& overlayCamera) {
    ViewportOverlayFrame toolOverlayFrame;
    const ModelingBoxPreview& boxPreview = inputRouter.boxPreview();
    if (boxPreview.dragState != ModelingBoxDragState::Height) {
        ViewportBoxOverlayBuilder::append(toolOverlayFrame, boxPreview);
    }
    const quader_editor_core::TransformGizmoFrame* transformGizmoFrame =
            interactions.transformGizmoFrame();
    if (transformGizmoFrame != nullptr) {
        ViewportTransformGizmoOverlayBuilder::append(toolOverlayFrame, *transformGizmoFrame, overlayCamera);
    }
    ViewportSelectionMarqueeOverlayBuilder::append(toolOverlayFrame, overlayCamera, interactions.selectionMarquee());
    return toolOverlayFrame;
}

std::optional<int> hitDrawnPlaneCapturedPoint(
        const std::array<QVec3, 3>& points,
        int capturedPointCount,
        QVec2 mouseLocal,
        const ViewportCameraController& camera) {
    constexpr float kPointHitRadiusPixels = 10.0f;
    const int count = std::min(capturedPointCount, 2);
    for (int index = count - 1; index >= 0; --index) {
        const ViewportScreenPoint screen =
                camera.projectWorldPoint(points[static_cast<std::size_t>(index)]);
        if (!screen.visible) {
            continue;
        }
        if (distanceSquared(mouseLocal, screen.position) <= kPointHitRadiusPixels * kPointHitRadiusPixels) {
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
        const ViewportCameraController& camera) {
    constexpr float kLineHitRadiusPixels = 10.0f;
    if (!hasLine || capturedPointCount < 2) {
        return false;
    }

    const ViewportScreenPoint start = camera.projectWorldPoint(points[0]);
    const ViewportScreenPoint end = camera.projectWorldPoint(points[1]);
    if (!start.visible || !end.visible) {
        return false;
    }

    return distanceToSegmentSquared(mouseLocal, start.position, end.position) <=
            kLineHitRadiusPixels * kLineHitRadiusPixels;
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

ModelingToolRayEvent modelingToolRayEvent(
        const ViewportCameraController& camera,
        const ViewportSettings& settings,
        QVec2 localMouse,
        QVec2 viewportSize,
        float gridSize,
        ViewportModifiers modifiers,
        bool dragging) {
    const ViewportRay ray = camera.rayFromViewportPoint(localMouse);
    return {
            .screenPosition = localMouse,
            .ray = {
                    .origin = ray.origin,
                    .direction = ray.direction,
            },
            .camera = ViewportCameraAdapter::modelingCamera(camera, viewportSize),
            .gridSize = std::max(static_cast<double>(gridSize), 0.0001),
            .vertexRadius = settings.floatValue(ViewportFloatSettingId::PickVertexRadius),
            .edgeRadius = settings.floatValue(ViewportFloatSettingId::PickEdgeRadius),
            .snapEnabled = !modifiers.primary,
            .constrainAxis = modifiers.shift,
            .dragging = dragging,
    };
}

void syncInputRouterFromEditorState(
        ViewportInputRouter& inputRouter,
        QuaderEditorApi& editor,
        ModelingSessionService& modeling) {
    const ViewportToolId activeTool = viewportToolForModeling(modeling.toolStatus().activeTool);
    if (inputRouter.state().activeTool != activeTool) {
        inputRouter.setActiveTool(activeTool);
        inputRouter.clearModalToolState();
    }
    inputRouter.setSelectionMode(appSelectionModeToViewport(toModelingSelectionMode(editor.selection().summary().mode)));
}

void activateViewportToolFromRouter(
        QuaderEditorApi& editor,
        SceneDocument& document,
        ViewportInputRouter& inputRouter) {
    LOG_INFO("Viewport tool changed to {}", viewportToolId(inputRouter.state().activeTool));
    const bool preserveGroupSelection =
            inputRouter.state().selectionMode == ViewportSelectionMode::Group ||
            editor.selection().summary().mode == QuaderSelectionMode::Group;
    const std::vector<EntityId> selectedBeforeToolActivation = preserveGroupSelection
            ? document.selectedEntities()
            : std::vector<EntityId>{};
    const EntityId activeBeforeToolActivation = preserveGroupSelection
            ? document.primarySelection()
            : InvalidEntityId;
    inputRouter.clearModalDragState();
    const ViewportTool* activeTool = inputRouter.activeTool();
    const QuaderApiVoidResult command = activeTool != nullptr ?
            editor.tools().activate(inputRouter.state().activeTool) :
            QuaderApiVoidResult{
                    .ok = false,
                    .changed = false,
                    .message = "No active viewport tool.",
            };
    if (!command.ok) {
        LOG_ERROR("Viewport tool activation failed: {}", command.message.empty() ? "Command failed." : command.message);
        inputRouter.setActiveTool(ViewportToolId::Select);
        (void)editor.tools().activate(ViewportToolId::Select);
    } else if (preserveGroupSelection) {
        const QuaderApiVoidResult restoreSelection =
                editor.selection().selectObjects(
                        std::span<const EntityId>(
                                selectedBeforeToolActivation.data(),
                                selectedBeforeToolActivation.size()),
                        activeBeforeToolActivation);
        if (!restoreSelection.ok) {
            LOG_ERROR("Group selection restore after tool activation failed: {}", restoreSelection.message);
        }
        (void)editor.selection().setMode(QuaderSelectionMode::Group);
        inputRouter.setSelectionMode(ViewportSelectionMode::Group);
    } else {
        inputRouter.setSelectionMode(appSelectionModeToViewport(toModelingSelectionMode(editor.selection().summary().mode)));
    }
    inputRouter.clearModalToolState();
}

ViewportToolResult dispatchActiveViewportToolFrame(
        QuaderEditorApi& editor,
        ModelingSessionService& modeling,
        SceneDocument& document,
        ViewportInputRouter& inputRouter,
        ViewportCameraController& camera,
        const ViewportInputFrame& input,
        const ViewportInputResult& result,
        const ViewportSettings& settings,
        AssetId defaultMaterial,
        bool leftPressed,
        bool leftDown,
        bool leftReleased,
        bool gizmoConsumed,
        QVec2 leftClickLocal) {
    ViewportBoxToolContext boxToolContext{
            .defaultMaterial = defaultMaterial,
    };
    boxToolContext.makeEvent = [&]() {
        return modelingBoxEvent(
                camera,
                input,
                viewportGridWorldSizeForPreset(inputRouter.state().gridPreset));
    };
    boxToolContext.commitPreview = [&](std::string name, AssetId material) {
        const QuaderApiResult<QuaderModelingObjectRef> command =
                editor.modeling().commitBoxPreview(std::move(name), material);
        ModelingCreateBoxResult committed{
                .ok = command.ok,
                .changed = command.changed,
                .message = command.message,
        };
        if (command.ok && command.changed) {
            committed.object = {
                    .modelingObjectId = command.value().modelingObjectId,
                    .entity = command.value().entity,
                    .mesh = command.value().mesh,
            };
        }
        return committed;
    };
    boxToolContext.nextMeshName = [&]() {
        return SceneEntityNaming::nextNameForKind(document, SceneEntityKind::Mesh);
    };

    ViewportPointToolContext pointToolContext;
    pointToolContext.makeEventAt = [&](QVec2 localMouse, bool dragging) {
        return modelingToolRayEvent(
                camera,
                settings,
                localMouse,
                input.viewportSize,
                viewportGridWorldSizeForPreset(inputRouter.state().gridPreset),
                input.modifiers,
                dragging);
    };
    pointToolContext.hitDrawnPlaneCapturedPoint =
            [&](const std::array<QVec3, 3>& points, int capturedPointCount) {
                return hitDrawnPlaneCapturedPoint(points, capturedPointCount, leftClickLocal, camera);
            };
    pointToolContext.hitDrawnPlaneLine =
            [&](const std::array<QVec3, 3>& points, int capturedPointCount, bool hasLine) {
                return hitDrawnPlaneLine(points, capturedPointCount, hasLine, leftClickLocal, camera);
            };

    ViewportToolContext toolContext{
            .state = const_cast<ViewportToolState&>(inputRouter.state()),
            .camera = camera,
            .modeling = modeling,
            .document = &document,
            .settings = &settings,
            .box = &boxToolContext,
            .point = &pointToolContext,
    };
    toolContext.claimHover = [](ModelingBoxDragState) {
        return true;
    };
    toolContext.invalidateHover = [&]() {
        const QuaderApiVoidResult result = editor.selection().clearHover();
        if (!result.ok) {
            LOG_ERROR("Clear hover failed: {}", result.message.empty() ? "Command failed." : result.message);
        }
    };
    toolContext.markOverlayDirty = []() {};
    toolContext.setActiveTool = [&](ViewportToolId tool) {
        inputRouter.setActiveTool(tool);
    };
    toolContext.setSelectionMode = [&](ViewportSelectionMode mode) {
        inputRouter.setSelectionMode(mode);
    };
    toolContext.setAppSelectionMode = [&](ModelingSelectionMode mode) {
        const QuaderApiVoidResult command = editor.selection().setMode(toApiSelectionMode(mode));
        if (!command.ok) {
            LOG_ERROR("Selection mode change failed: {}", command.message.empty() ? "Command failed." : command.message);
        }
        inputRouter.setSelectionMode(appSelectionModeToViewport(toModelingSelectionMode(editor.selection().summary().mode)));
    };
    toolContext.handleToolCommandResult = [](std::string_view label, const ModelingCommandResult& command) {
        if (!command.ok) {
            LOG_ERROR("{} failed: {}", label, command.message.empty() ? "Command failed." : command.message);
        }
    };

    const bool suppressHoverWork = result.navigationActive || result.cameraChanged;
    const bool canInteractWithActiveTool = input.viewportHovered && !result.navigationActive && !gizmoConsumed;
    return inputRouter.handleActiveToolEvent(toolContext, ViewportToolEvent{
            .kind = ViewportToolEventKind::Frame,
            .tool = inputRouter.state().activeTool,
            .pointer = {
                    .hovered = input.viewportHovered,
                    .leftClicked = leftPressed && input.viewportHovered,
                    .leftDown = leftDown,
                    .leftReleased = leftReleased,
                    .canInteract = canInteractWithActiveTool,
                    .finalizePressed = input.modalToolFinalizePressed,
                    .modeCyclePressed = input.modalToolModeCyclePressed,
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

QuaderViewportKeybindingContext keybindingContextFromInput(const ViewportInputFrame& input);

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

QuaderViewportKeybindingContext keybindingContextFromInput(const ViewportInputFrame& input) {
    return {
            .viewportHovered = input.viewportHovered,
            .navigationActive = input.navigationActive,
            .flyNavigationActive = input.flyNavigationActive,
            .orbitNavigationActive = input.orbitNavigationActive,
            .panNavigationActive = input.panNavigationActive,
            .modalToolActive = input.modalToolActive,
            .activeToolOwnsViewport = input.activeToolOwnsViewport,
    };
}

bool applyViewportKeybindingDispatch(
        QuaderEditorApi& editor,
        const QuaderViewportKeybindingDispatch& dispatch,
        ViewportInputFrame& input) {
    switch (dispatch.kind) {
    case QuaderViewportKeybindingDispatch::Kind::PendingChord:
        input.resolvedShortcuts.push_back(ViewportShortcut{});
        return true;
    case QuaderViewportKeybindingDispatch::Kind::ViewportShortcut:
        input.resolvedShortcuts.push_back(dispatch.shortcut);
        return true;
    case QuaderViewportKeybindingDispatch::Kind::Command:
        logCommandFailure(
                "Keybinding command",
                editor.commands().execute(dispatch.commandId));
        input.resolvedShortcuts.push_back(ViewportShortcut{});
        return true;
    case QuaderViewportKeybindingDispatch::Kind::NoMatch:
        return false;
    }
    return false;
}

std::string formatWindowedViewportTitle(std::string_view titleFormat, int index) {
    std::string title(titleFormat);
    const std::string token = "{index}";
    const std::string indexText = std::to_string(index);
    const std::size_t tokenPos = title.find(token);
    if (tokenPos != std::string::npos) {
        title.replace(tokenPos, token.size(), indexText);
        return title;
    }
    if (!title.empty()) {
        title.push_back(' ');
    }
    title += indexText;
    return title;
}

void resolveKeybindingsForFrame(
        QuaderEditorApi& editor,
        const ViewportToolState& state,
        ViewportInputFrame& input) {
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

        const QuaderViewportKeybindingContext keybindingContext = keybindingContextFromInput(input);
        const QuaderViewportKeybindingDispatch dispatch = editor.commands().dispatchViewportInputBinding(
                chord,
                state,
                input.viewportFocused,
                input.textInputActive,
                keybindingContext);
        if (applyViewportKeybindingDispatch(editor, dispatch, input)) {
            return;
        }
    }

    syncFlyDirectionFromNavigationBindings(input);
}

} // namespace

/**
 * Stores one window-backed viewport component instance.
 */
struct ViewportHost::ViewportWindowInstance {
    PlatformWindow window;
    filament::SwapChain* swapChain = nullptr;
    ViewportComponent component;
    ViewportRenderer renderer;
    ViewportInputRouter inputRouter;
    FrameTiming frameTiming = {};
    SDL_WindowID windowId = 0u;
    std::uint32_t framebufferWidth = 1u;
    std::uint32_t framebufferHeight = 1u;
    int windowWidth = 1;
    int windowHeight = 1;
    ViewportInputSurfaceState input;
    ViewportInteractionController interactions;
    bool relativeMouseCaptured = false;
    bool relativeMouseCaptureWarningIssued = false;
};

ViewportHost::ViewportHost() = default;

ViewportHost::~ViewportHost() {
    shutdown();
}

void ViewportHost::initialize(SDL_Window* window, RenderSystem& renderSystem, const ViewportSettings& settings) {
    window_ = window;
    renderSystem_ = &renderSystem;
    settings_ = settings;
    updateWindowMetrics();
    mainViewportFrame_ = {
            .left = 0,
            .top = 0,
            .width = framebufferWidth_,
            .height = framebufferHeight_,
    };
    renderer_.initialize(renderSystem.engine(),
            renderSystem.scene(),
            renderSystem.textures(),
            renderSystem.groundGridMaterial(),
            viewportGridLayerMask(mainViewport_.layerIndex));
    renderer_.resize(framebufferWidth_, framebufferHeight_);
}

void ViewportHost::shutdown() {
    shutdownWindowedViewports();
    setCursorCaptured(false);
    renderer_.shutdown();
    window_ = nullptr;
    renderSystem_ = nullptr;
}

bool ViewportHost::processEvent(const SDL_Event& event) {
    if (!window_) {
        return false;
    }

    if (processWindowedViewportEvent(event)) {
        return true;
    }

    const std::optional<SDL_WindowID> windowId = sdlEventWindowId(event);
    if (windowId && *windowId != SDL_GetWindowID(window_)) {
        return false;
    }

    switch (event.type) {
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        setCursorCaptured(false);
        inputRouter_.reset();
        clearAllInputState(mainInput_);
        break;
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        mainInput_.mouseWindow = {-1.0f, -1.0f};
        break;
    case SDL_EVENT_MOUSE_MOTION:
        accumulateMouseMotion(mainInput_, {event.motion.x, event.motion.y}, {event.motion.xrel, event.motion.yrel});
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        const bool down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        const bool startedInMainViewport = down && mainViewportContainsWindowPoint(event.button.x, event.button.y);
        accumulateMouseButton(
                mainInput_,
                event.button.button,
                down,
                startedInMainViewport,
                {event.button.x, event.button.y},
                static_cast<int>(event.button.clicks),
                currentModifiers(mainInput_));
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL: {
        const float direction = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
        accumulateMouseWheel(mainInput_, {event.wheel.mouse_x, event.wheel.mouse_y}, event.wheel.y * direction);
        break;
    }
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        accumulateModifierKey(mainInput_, event.key.scancode, event.key.mod, event.key.down);
        const std::optional<QuaderKeyCode> keyCode = quaderKeyCodeFromSdlKey(event.key.key);
        if (keyCode) {
            accumulateKey(mainInput_, *keyCode, event.key.down, event.key.repeat, currentModifiers(mainInput_));
        }
        break;
    }
    default:
        break;
    }
    return false;
}

void ViewportHost::beginFrame(const FrameTiming& frameTiming) {
    currentFrameTiming_ = frameTiming;
    updateWindowMetrics();
    mainInput_.relativeMouseDelta = {};
    if (relativeMouseCaptured_) {
        float deltaX = 0.0f;
        float deltaY = 0.0f;
        (void)SDL_GetRelativeMouseState(&deltaX, &deltaY);
        const float scaleX = windowWidth_ > 0 ? static_cast<float>(framebufferWidth_) / static_cast<float>(windowWidth_) : 1.0f;
        const float scaleY = windowHeight_ > 0 ? static_cast<float>(framebufferHeight_) / static_cast<float>(windowHeight_) : 1.0f;
        mainInput_.relativeMouseDelta = {deltaX * scaleX, deltaY * scaleY};
    }

    for (const std::unique_ptr<ViewportWindowInstance>& viewport : windowedViewports_) {
        ViewportWindowInstance& instance = *viewport;
        instance.frameTiming = frameTiming;
        instance.input.relativeMouseDelta = {};
        instance.window.refreshPixelSize();
        instance.framebufferWidth = instance.window.widthPixels();
        instance.framebufferHeight = instance.window.heightPixels();
        SDL_GetWindowSize(instance.window.sdlWindow(), &instance.windowWidth, &instance.windowHeight);
        instance.windowWidth = std::max(1, instance.windowWidth);
        instance.windowHeight = std::max(1, instance.windowHeight);
        if (!instance.relativeMouseCaptured) {
            continue;
        }

        float deltaX = 0.0f;
        float deltaY = 0.0f;
        (void)SDL_GetRelativeMouseState(&deltaX, &deltaY);
        const float scaleX = static_cast<float>(instance.framebufferWidth) / static_cast<float>(instance.windowWidth);
        const float scaleY = static_cast<float>(instance.framebufferHeight) / static_cast<float>(instance.windowHeight);
            instance.input.relativeMouseDelta = {deltaX * scaleX, deltaY * scaleY};
    }
}

void ViewportHost::resize(std::uint32_t framebufferWidth, std::uint32_t framebufferHeight) {
    framebufferWidth_ = std::max(1u, framebufferWidth);
    framebufferHeight_ = std::max(1u, framebufferHeight);
}

void ViewportHost::setMainViewportFrame(
        int left,
        int top,
        std::uint32_t width,
        std::uint32_t height) {
    mainViewportFrame_ = {
            .left = std::max(0, left),
            .top = std::max(0, top),
            .width = std::max(1u, width),
            .height = std::max(1u, height),
    };
}

void ViewportHost::handleFrame(
        QuaderEditorApi& editor,
        SceneDocument& document,
        const AssetRegistry& assets,
        ModelingSessionService& modeling,
        AssetId defaultMaterial,
        bool inputSuppressed) {
    ViewportWorkspaceService& workspace = QuaderViewportApiRuntimeAccess::workspace(editor.viewports());
    installViewportRuntimeHooks(
            editor,
            document,
            assets,
            [this] {
                spawnWindowedViewport();
                return viewportRuntimeStatus(true);
            });
    flushRemovedWindowedViewportWorkspaceState(workspace);
    workspace.focusPane(mainViewport_.pane);
    workspace.clearPaneVisibility();
    workspace.setPaneVisible(mainViewport_.pane, true);
    workspace.setPaneRenderSize(mainViewport_.pane, {
            static_cast<float>(mainViewportFrame_.width),
            static_cast<float>(mainViewportFrame_.height),
    });

    if (inputSuppressed) {
        setCursorCaptured(false);
        inputRouter_.reset();
        (void)viewportInteractions_.cancelActiveDrag(editor);
        viewportInteractions_.reset();
        clearAllInputState(mainInput_);
        for (const std::unique_ptr<ViewportWindowInstance>& viewport : windowedViewports_) {
            viewport->inputRouter.reset();
            setWindowedViewportCursorCaptured(*viewport, false);
            clearAllInputState(viewport->input);
        }
        handleWindowedViewportFrames(editor, workspace, document, assets, modeling, defaultMaterial);
        return;
    }

    syncInputRouterFromEditorState(inputRouter_, editor, modeling);
    inputRouter_.setGridPreset(workspace.gridPreset());
    ViewportCameraController& camera = workspace.camera(mainViewport_.pane);
    ViewportInputFrame input = buildViewportInputFrame();
    handleViewportFrameSurface(
            editor,
            workspace,
            document,
            modeling,
            defaultMaterial,
            mainViewport_,
            inputRouter_,
            viewportInteractions_,
            camera,
            std::move(input),
            mainInput_,
            mainViewportWindowPointToLocal(mainInput_.leftClickWindow),
            nullptr);
    handleWindowedViewportFrames(editor, workspace, document, assets, modeling, defaultMaterial);
    clearTransientInput();
}

void ViewportHost::handleViewportFrameSurface(
        QuaderEditorApi& editor,
        ViewportWorkspaceService& workspace,
        SceneDocument& document,
        ModelingSessionService& modeling,
        AssetId defaultMaterial,
        const ViewportComponent& component,
        ViewportInputRouter& inputRouter,
        ViewportInteractionController& interactions,
        ViewportCameraController& camera,
        ViewportInputFrame input,
        ViewportInputSurfaceState& inputState,
        QVec2 leftClickLocal,
        ViewportWindowInstance* windowedInstance) {
    resolveKeybindingsForFrame(editor, inputRouter.state(), input);
    syncInputRouterFromEditorState(inputRouter, editor, modeling);
    const ViewportInputResult result = inputRouter.handleFrame(input, camera);
    if (result.toolChanged) {
        activateViewportToolFromRouter(editor, document, inputRouter);
    }
    if (result.gridPresetChanged) {
        workspace.setGridPreset(inputRouter.state().gridPreset);
    }
    if (result.focusDocumentRequested) {
        logCommandFailure("Frame selection", editor.viewports().frameSelection(component.pane));
    }
    if (result.maximizedToggleRequested) {
        (void)workspace.toggleFocusedPaneMaximized();
    }
    if (result.cancelRequested) {
        (void)interactions.cancelActiveDrag(editor);
        logCommandFailure("Cancel active tool", editor.tools().cancelModalTool());
        inputRouter.clearModalToolState();
    }
    const ViewportShortcutController shortcutController;
    const ViewportShortcutControllerResult previewShortcut =
            shortcutController.handleOperationPreviewShortcut(result, modeling.toolSettingsTarget(), editor, modeling);
    if (!previewShortcut.consumed) {
        (void)shortcutController.handleModelingShortcut(result, editor, modeling);
    }
    ViewportInteractionFrame interactionFrame{
            .inputRouter = inputRouter,
            .camera = camera,
            .settings = settings_,
            .input = input,
            .inputResult = result,
            .leftClickLocal = leftClickLocal,
            .leftClickCount = inputState.leftClickCount,
            .leftPressed = inputState.leftPressed,
            .leftDown = inputState.leftDown,
            .leftReleased = inputState.leftReleased,
    };
    const ViewportBeforeToolInteractionResult beforeTool =
            interactions.handleBeforeActiveTool(editor, interactionFrame);
    const ViewportToolResult toolResult = dispatchActiveViewportToolFrame(
            editor,
            modeling,
            document,
            inputRouter,
            camera,
            input,
            result,
            settings_,
            defaultMaterial,
            inputState.leftPressed,
            inputState.leftDown,
            inputState.leftReleased,
            beforeTool.gizmoConsumed,
            interactionFrame.leftClickLocal);
    interactions.handleAfterActiveTool(editor, interactionFrame, toolResult, beforeTool);

    if (windowedInstance != nullptr) {
        setWindowedViewportCursorCaptured(*windowedInstance, result.flyCursorCaptureActive);
    } else {
        setCursorCaptured(result.flyCursorCaptureActive);
    }
}

void ViewportHost::prepareViewports(
        QuaderEditorApi& editor,
        SceneRenderBridge& renderBridge,
        const SceneDocument& document,
        const AssetRegistry& assets) {
    (void)document;
    (void)assets;
    ViewportWorkspaceService& workspace = QuaderViewportApiRuntimeAccess::workspace(editor.viewports());
    flushRemovedWindowedViewportWorkspaceState(workspace);
    workspace.setPaneVisible(mainViewport_.pane, true);
    workspace.setPaneRenderSize(mainViewport_.pane, {
            static_cast<float>(mainViewportFrame_.width),
            static_cast<float>(mainViewportFrame_.height),
    });

    ViewportWorkspacePaneState& pane = workspace.pane(mainViewport_.pane);
    renderer_.resize(mainViewportFrame_.width, mainViewportFrame_.height);
    renderer_.updateCamera(pane.camera);
    renderer_.updateGroundGrid(
            pane.camera,
            viewportGridWorldSizeForPreset(workspace.gridPreset()),
            settings_,
            true);
    renderer_.setSceneVisible(true);
    logCommandFailure("Sync tool previews", QuaderRenderApiRuntimeAccess::syncToolPreviews(editor.render(), renderBridge));
    const ViewportOverlayCamera overlayCamera = ViewportCameraAdapter::overlayCamera(pane.camera, pane.renderSize);
    ViewportOverlayFrame toolOverlayFrame =
            buildToolOverlayFrame(inputRouter_, viewportInteractions_, overlayCamera);
    const bool hasToolOverlay = viewportOverlayFrameVisible(toolOverlayFrame);
    const QuaderApiResult<QuaderRenderOverlaySubmission> overlaySubmission =
            QuaderRenderApiRuntimeAccess::syncSelectionOverlay(
                    editor.render(),
                    renderBridge,
                    mainViewport_.layerIndex,
                    viewportGridLayerMask(mainViewport_.layerIndex),
                    overlayCamera,
                    hasToolOverlay ? &toolOverlayFrame : nullptr);
    logCommandFailure("Sync selection overlay", overlaySubmission);
    renderer_.setOverlayVisible(overlaySubmission.ok && overlaySubmission.value.visible);
    prepareWindowedViewports(editor, renderBridge, workspace);
}

void ViewportHost::renderViewports(RenderSystem& renderSystem) {
    renderer_.render(renderSystem.renderer());
}

void ViewportHost::renderWindowedViewports(RenderSystem& renderSystem) {
    for (const std::unique_ptr<ViewportWindowInstance>& viewport : windowedViewports_) {
        ViewportWindowInstance& instance = *viewport;
        if (instance.swapChain == nullptr || !renderSystem.beginFrame(instance.swapChain)) {
            continue;
        }

        instance.renderer.render(renderSystem.renderer());
        renderSystem.endFrame();
    }
}

void ViewportHost::setViewportSettings(const ViewportSettings& settings) {
    settings_ = settings;
}

void ViewportHost::setCursorCaptured(bool captured) {
    if (window_ == nullptr || relativeMouseCaptured_ == captured) {
        return;
    }

    if (!SDL_SetWindowRelativeMouseMode(window_, captured)) {
        if (!relativeMouseCaptureWarningIssued_) {
            LOG_WARN("Unable to {} viewport relative mouse mode: {}",
                    captured ? "enable" : "disable",
                    SDL_GetError());
            relativeMouseCaptureWarningIssued_ = true;
        }
        return;
    }

    relativeMouseCaptured_ = captured;
    relativeMouseCaptureWarningIssued_ = false;
    float discardX = 0.0f;
    float discardY = 0.0f;
    (void)SDL_GetRelativeMouseState(&discardX, &discardY);
    mainInput_.relativeMouseDelta = {};
}

void ViewportHost::setWindowedViewportTitleFormat(std::string titleFormat) {
    if (titleFormat.empty()) {
        return;
    }
    windowedViewportTitleFormat_ = std::move(titleFormat);
}

void ViewportHost::setTextInputActive(bool active) {
    textInputActive_ = active;
}

std::string ViewportHost::textureSource() const {
    return std::string(mainViewport_.textureSource);
}

RenderTextureHandle ViewportHost::textureHandle() const {
    return renderer_.textureHandle();
}

std::uint32_t ViewportHost::textureWidth() const {
    return renderer_.width();
}

std::uint32_t ViewportHost::textureHeight() const {
    return renderer_.height();
}

void ViewportHost::updateWindowMetrics() {
    if (!window_) {
        return;
    }

    int framebufferW = 1;
    int framebufferH = 1;
    SDL_GetWindowSizeInPixels(window_, &framebufferW, &framebufferH);
    resize(static_cast<std::uint32_t>(std::max(1, framebufferW)), static_cast<std::uint32_t>(std::max(1, framebufferH)));
    SDL_GetWindowSize(window_, &windowWidth_, &windowHeight_);
    windowWidth_ = std::max(1, windowWidth_);
    windowHeight_ = std::max(1, windowHeight_);

    float mouseX = 0.0f;
    float mouseY = 0.0f;
    const SDL_MouseButtonFlags buttons = SDL_GetMouseState(&mouseX, &mouseY);
    mainInput_.mouseWindow = {mouseX, mouseY};
    if ((buttons & SDL_BUTTON_MMASK) == 0u) {
        mainInput_.middleDown = false;
        mainInput_.mouseButtonsDown.erase(QuaderMouseButton::Middle);
        mainInput_.mouseDragChordsDown.erase(QuaderMouseButton::Middle);
    }
    if ((buttons & SDL_BUTTON_LMASK) == 0u) {
        mainInput_.leftDown = false;
        mainInput_.mouseButtonsDown.erase(QuaderMouseButton::Left);
        mainInput_.mouseDragChordsDown.erase(QuaderMouseButton::Left);
    }
    if ((buttons & SDL_BUTTON_RMASK) == 0u) {
        mainInput_.rightDown = false;
        mainInput_.mouseButtonsDown.erase(QuaderMouseButton::Right);
        mainInput_.mouseDragChordsDown.erase(QuaderMouseButton::Right);
    }
}

bool ViewportHost::mainViewportContainsWindowPoint(float windowX, float windowY) const {
    const float scaleX = windowWidth_ > 0 ? static_cast<float>(framebufferWidth_) / static_cast<float>(windowWidth_) : 1.0f;
    const float scaleY = windowHeight_ > 0 ? static_cast<float>(framebufferHeight_) / static_cast<float>(windowHeight_) : 1.0f;
    const QVec2 mousePixel = {
            windowX * scaleX,
            windowY * scaleY,
    };
    const QVec2 mouseLocal = {
            mousePixel.x - static_cast<float>(mainViewportFrame_.left),
            mousePixel.y - static_cast<float>(mainViewportFrame_.top),
    };
    return mouseLocal.x >= 0.0f &&
            mouseLocal.y >= 0.0f &&
            mouseLocal.x <= static_cast<float>(mainViewportFrame_.width) &&
            mouseLocal.y <= static_cast<float>(mainViewportFrame_.height);
}

bool ViewportHost::hasActivePointerGesture() const {
    return inputStateHasActivePointerGesture(mainInput_);
}

QVec2 ViewportHost::mainViewportWindowPointToLocal(QVec2 windowPoint) const {
    const float scaleX = windowWidth_ > 0 ? static_cast<float>(framebufferWidth_) / static_cast<float>(windowWidth_) : 1.0f;
    const float scaleY = windowHeight_ > 0 ? static_cast<float>(framebufferHeight_) / static_cast<float>(windowHeight_) : 1.0f;
    const QVec2 mousePixel = {
            windowPoint.x * scaleX,
            windowPoint.y * scaleY,
    };
    return {
            mousePixel.x - static_cast<float>(mainViewportFrame_.left),
            mousePixel.y - static_cast<float>(mainViewportFrame_.top),
    };
}

void ViewportHost::accumulateMouseMotion(
        ViewportInputSurfaceState& input,
        QVec2 windowPoint,
        QVec2 windowDelta) {
    input.mouseWindow = windowPoint;
    input.mouseDeltaWindow = input.mouseDeltaWindow + windowDelta;
}

void ViewportHost::accumulateMouseButton(
        ViewportInputSurfaceState& input,
        std::uint8_t button,
        bool down,
        bool acceptPress,
        QVec2 windowPoint,
        int clickCount,
        ViewportModifiers modifiers) {
    input.mouseWindow = windowPoint;
    const std::optional<QuaderMouseButton> mouseButton = quaderMouseButtonFromSdlButton(button);
    if (button == SDL_BUTTON_MIDDLE) {
        if (down) {
            input.middleDown = acceptPress;
            input.middlePressed = input.middlePressed || acceptPress;
        } else {
            input.middleReleased = input.middleReleased || input.middleDown;
            input.middleDown = false;
        }
    } else if (button == SDL_BUTTON_LEFT) {
        if (down) {
            input.leftDown = acceptPress;
            input.leftPressed = input.leftPressed || acceptPress;
            if (acceptPress) {
                input.leftClickWindow = windowPoint;
                input.leftClickCount = std::max(1, clickCount);
            }
        } else {
            input.leftReleased = input.leftReleased || input.leftDown;
            input.leftDown = false;
        }
    } else if (button == SDL_BUTTON_RIGHT) {
        if (down) {
            input.rightDown = acceptPress;
            input.rightPressed = input.rightPressed || acceptPress;
        } else {
            input.rightReleased = input.rightReleased || input.rightDown;
            input.rightDown = false;
        }
    }

    if (!mouseButton) {
        return;
    }
    if (down && acceptPress) {
        const QuaderInputChord dragChord = mouseInputChord(*mouseButton, QuaderInputGesture::Drag, modifiers);
        input.mouseButtonsDown.insert(*mouseButton);
        input.mouseDragChordsDown[*mouseButton] = dragChord;
        input.pressedInputChords.push_back(mouseInputChord(*mouseButton, QuaderInputGesture::Press, modifiers));
        input.pressedInputChords.push_back(dragChord);
        return;
    }
    if (down) {
        return;
    }

    const bool wasDown = input.mouseButtonsDown.contains(*mouseButton);
    const auto dragChord = input.mouseDragChordsDown.find(*mouseButton);
    const std::optional<QuaderInputChord> releasedDragChord =
            dragChord == input.mouseDragChordsDown.end() ? std::nullopt : std::optional<QuaderInputChord>(dragChord->second);
    input.mouseButtonsDown.erase(*mouseButton);
    input.mouseDragChordsDown.erase(*mouseButton);
    if (wasDown && releasedDragChord) {
        input.releasedInputChords.push_back(*releasedDragChord);
    }
}

void ViewportHost::accumulateMouseWheel(ViewportInputSurfaceState& input, QVec2 windowPoint, float wheelY) {
    input.mouseWindow = windowPoint;
    input.wheelY += wheelY;
}

void ViewportHost::accumulateKey(
        ViewportInputSurfaceState& input,
        QuaderKeyCode keyCode,
        bool down,
        bool repeat,
        ViewportModifiers modifiers) {
    if (down) {
        input.keyboardKeysDown.insert(keyCode);
        if (!repeat) {
            input.pressedInputChords.push_back(keyboardInputChord(keyCode, modifiers));
        }
        return;
    }

    input.keyboardKeysDown.erase(keyCode);
}

void ViewportHost::applyModifierMask(ViewportInputSurfaceState& input, SDL_Keymod modifiers) {
    input.modifierKeys.leftControl = (modifiers & SDL_KMOD_LCTRL) != 0;
    input.modifierKeys.rightControl = (modifiers & SDL_KMOD_RCTRL) != 0;
    input.modifierKeys.leftGui = (modifiers & SDL_KMOD_LGUI) != 0;
    input.modifierKeys.rightGui = (modifiers & SDL_KMOD_RGUI) != 0;
    input.modifierKeys.leftShift = (modifiers & SDL_KMOD_LSHIFT) != 0;
    input.modifierKeys.rightShift = (modifiers & SDL_KMOD_RSHIFT) != 0;
    input.modifierKeys.leftAlt = (modifiers & SDL_KMOD_LALT) != 0;
    input.modifierKeys.rightAlt = (modifiers & SDL_KMOD_RALT) != 0;
}

void ViewportHost::accumulateModifierKey(
        ViewportInputSurfaceState& input,
        SDL_Scancode scanCode,
        SDL_Keymod modifiers,
        bool down) {
    applyModifierMask(input, modifiers);
    switch (scanCode) {
    case SDL_SCANCODE_LCTRL:
        input.modifierKeys.leftControl = down;
        break;
    case SDL_SCANCODE_RCTRL:
        input.modifierKeys.rightControl = down;
        break;
    case SDL_SCANCODE_LGUI:
        input.modifierKeys.leftGui = down;
        break;
    case SDL_SCANCODE_RGUI:
        input.modifierKeys.rightGui = down;
        break;
    case SDL_SCANCODE_LSHIFT:
        input.modifierKeys.leftShift = down;
        break;
    case SDL_SCANCODE_RSHIFT:
        input.modifierKeys.rightShift = down;
        break;
    case SDL_SCANCODE_LALT:
        input.modifierKeys.leftAlt = down;
        break;
    case SDL_SCANCODE_RALT:
        input.modifierKeys.rightAlt = down;
        break;
    default:
        break;
    }
}

bool ViewportHost::inputStateHasActivePointerGesture(const ViewportInputSurfaceState& input) {
    return input.leftDown || input.middleDown || input.rightDown || !input.mouseButtonsDown.empty();
}

void ViewportHost::clearTransientInputState(ViewportInputSurfaceState& input) {
    input.middlePressed = false;
    input.middleReleased = false;
    input.leftPressed = false;
    input.leftReleased = false;
    input.rightPressed = false;
    input.rightReleased = false;
    input.wheelY = 0.0f;
    input.mouseDeltaWindow = {};
    input.pressedInputChords.clear();
    input.releasedInputChords.clear();
}

void ViewportHost::clearHeldInputState(ViewportInputSurfaceState& input) {
    input.middleDown = false;
    input.leftDown = false;
    input.rightDown = false;
    input.keyboardKeysDown.clear();
    input.mouseButtonsDown.clear();
    input.mouseDragChordsDown.clear();
    input.modifierKeys = {};
}

void ViewportHost::clearAllInputState(ViewportInputSurfaceState& input) {
    clearHeldInputState(input);
    clearTransientInputState(input);
}

ViewportInputFrame ViewportHost::buildViewportInputFrame() const {
    const float scaleX = windowWidth_ > 0 ? static_cast<float>(framebufferWidth_) / static_cast<float>(windowWidth_) : 1.0f;
    const float scaleY = windowHeight_ > 0 ? static_cast<float>(framebufferHeight_) / static_cast<float>(windowHeight_) : 1.0f;
    const QVec2 mousePixel = {
            mainInput_.mouseWindow.x * scaleX,
            mainInput_.mouseWindow.y * scaleY,
    };
    const QVec2 mouseLocal = {
            mousePixel.x - static_cast<float>(mainViewportFrame_.left),
            mousePixel.y - static_cast<float>(mainViewportFrame_.top),
    };
    const QVec2 mouseDelta = relativeMouseCaptured_
            ? mainInput_.relativeMouseDelta
            : QVec2{mainInput_.mouseDeltaWindow.x * scaleX, mainInput_.mouseDeltaWindow.y * scaleY};
    const bool hovered = mouseLocal.x >= 0.0f &&
            mouseLocal.y >= 0.0f &&
            mouseLocal.x <= static_cast<float>(mainViewportFrame_.width) &&
            mouseLocal.y <= static_cast<float>(mainViewportFrame_.height);
    ViewportInputFrame input;
    input.viewportHovered = hovered || inputRouter_.navigating();
    input.viewportFocused = true;
    input.textInputActive = textInputActive_;
    input.navigationActive = inputRouter_.navigating();
    input.flyNavigationActive = inputRouter_.flyNavigating();
    input.orbitNavigationActive = inputRouter_.orbitNavigating();
    input.panNavigationActive = inputRouter_.panNavigating();
    input.modalToolActive = inputRouter_.hasActiveModalPreview();
    input.activeToolOwnsViewport = inputRouter_.activeToolOwnsViewport();
    input.mouseWheel = input.viewportHovered ? mainInput_.wheelY : 0.0f;
    input.deltaSeconds = static_cast<float>(std::max(currentFrameTiming_.deltaSeconds, 0.0));
    input.mouseScreen = mousePixel;
    input.mouseLocal = mouseLocal;
    input.mouseDelta = mouseDelta;
    input.viewportSize = {
            static_cast<float>(mainViewportFrame_.width),
            static_cast<float>(mainViewportFrame_.height),
    };
    const ViewportModifiers modifiers = currentModifiers(mainInput_);
    input.modifiers = modifiers;
    input.pressedInputChords = mainInput_.pressedInputChords;
    input.releasedInputChords = mainInput_.releasedInputChords;
    appendActiveMouseDragChords(mainInput_.mouseDragChordsDown, input.activeInputChords);
    appendActiveKeyboardChords(mainInput_.keyboardKeysDown, modifiers, input.activeInputChords);
    appendWheelChords(input.mouseWheel, modifiers, input.wheelInputChords);
    return input;
}

ViewportModifiers ViewportHost::currentModifiers(const ViewportInputSurfaceState& input) const {
    const SDL_Keymod mod = SDL_GetModState();
    const bool* keyboardState = SDL_GetKeyboardState(nullptr);
    const auto keyDown = [keyboardState](SDL_Scancode scancode) {
        return keyboardState != nullptr && keyboardState[scancode];
    };
    return {
            .primary = (mod & SDL_KMOD_CTRL) != 0 ||
                    (mod & SDL_KMOD_GUI) != 0 ||
                    input.modifierKeys.leftControl ||
                    input.modifierKeys.rightControl ||
                    input.modifierKeys.leftGui ||
                    input.modifierKeys.rightGui ||
                    keyDown(SDL_SCANCODE_LCTRL) ||
                    keyDown(SDL_SCANCODE_RCTRL) ||
                    keyDown(SDL_SCANCODE_LGUI) ||
                    keyDown(SDL_SCANCODE_RGUI),
            .shift = (mod & SDL_KMOD_SHIFT) != 0 ||
                    input.modifierKeys.leftShift ||
                    input.modifierKeys.rightShift ||
                    keyDown(SDL_SCANCODE_LSHIFT) ||
                    keyDown(SDL_SCANCODE_RSHIFT),
            .alt = (mod & SDL_KMOD_ALT) != 0 ||
                    input.modifierKeys.leftAlt ||
                    input.modifierKeys.rightAlt ||
                    keyDown(SDL_SCANCODE_LALT) ||
                    keyDown(SDL_SCANCODE_RALT),
    };
}

void ViewportHost::clearTransientInput() {
    clearTransientInputState(mainInput_);
}

void ViewportHost::spawnWindowedViewport() {
    if (!renderSystem_) {
        LOG_WARN("Cannot spawn a windowed viewport before the render system is available");
        return;
    }

    const std::size_t totalViewportCount = 1u + windowedViewports_.size();
    if (totalViewportCount >= kMaxViewportComponents) {
        if (!windowedViewports_.empty() && !SDL_RaiseWindow(windowedViewports_.back()->window.sdlWindow())) {
            LOG_WARN("Unable to raise latest windowed viewport: {}", SDL_GetError());
        }
        LOG_INFO("Viewport component limit reached: {}", kMaxViewportComponents);
        return;
    }

    const std::uint8_t layerIndex = nextWindowedViewportLayerIndex();
    if (layerIndex == 0u || layerIndex >= kMaxViewportComponents) {
        LOG_WARN("Unable to allocate a viewport layer for a new windowed viewport");
        return;
    }

    auto instance = std::make_unique<ViewportWindowInstance>();
    instance->component = windowedViewportComponent(layerIndex);
    const std::string title =
            formatWindowedViewportTitle(windowedViewportTitleFormat_, static_cast<int>(layerIndex) + 1);
    if (!instance->window.create(title.c_str(), 960, 540)) {
        return;
    }

    instance->windowId = SDL_GetWindowID(instance->window.sdlWindow());
    instance->swapChain = renderSystem_->createAdditionalSwapChain(
            instance->window.nativeWindowHandle(),
            SwapChainPresentationColorSpace::Srgb);
    if (!instance->swapChain) {
        LOG_WARN("Unable to create windowed viewport swapchain");
        instance->window.destroy();
        return;
    }

    instance->window.refreshPixelSize();
    instance->framebufferWidth = instance->window.widthPixels();
    instance->framebufferHeight = instance->window.heightPixels();
    SDL_GetWindowSize(instance->window.sdlWindow(), &instance->windowWidth, &instance->windowHeight);
    instance->windowWidth = std::max(1, instance->windowWidth);
    instance->windowHeight = std::max(1, instance->windowHeight);
    instance->inputRouter.setGridPreset(inputRouter_.state().gridPreset);
    instance->renderer.initialize(
            renderSystem_->engine(),
            renderSystem_->scene(),
            renderSystem_->textures(),
            renderSystem_->groundGridMaterial(),
            viewportGridLayerMask(instance->component.layerIndex),
            ViewportRenderSurface::Swapchain);
    instance->renderer.resize(instance->framebufferWidth, instance->framebufferHeight);
    LOG_INFO("Spawned windowed viewport {}", instance->component.stableId);
    windowedViewports_.push_back(std::move(instance));
}

void ViewportHost::shutdownWindowedViewports() {
    for (const std::unique_ptr<ViewportWindowInstance>& viewport : windowedViewports_) {
        shutdownWindowedViewport(*viewport);
    }
    windowedViewports_.clear();
}

bool ViewportHost::processWindowedViewportEvent(const SDL_Event& event) {
    const std::optional<SDL_WindowID> windowId = sdlEventWindowId(event);
    if (!windowId) {
        return false;
    }

    const auto found = std::find_if(windowedViewports_.begin(), windowedViewports_.end(), [&](const auto& viewport) {
        return viewport->windowId == *windowId;
    });
    if (found == windowedViewports_.end()) {
        return false;
    }

    ViewportWindowInstance& instance = **found;
    switch (event.type) {
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        removedWindowedViewportIds_.push_back(instance.component.stableId);
        shutdownWindowedViewport(instance);
        windowedViewports_.erase(found);
        return true;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        setWindowedViewportCursorCaptured(instance, false);
        instance.inputRouter.reset();
        clearAllInputState(instance.input);
        return true;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_RESIZED:
        instance.window.refreshPixelSize();
        instance.framebufferWidth = instance.window.widthPixels();
        instance.framebufferHeight = instance.window.heightPixels();
        SDL_GetWindowSize(instance.window.sdlWindow(), &instance.windowWidth, &instance.windowHeight);
        instance.windowWidth = std::max(1, instance.windowWidth);
        instance.windowHeight = std::max(1, instance.windowHeight);
        return true;
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        instance.input.mouseWindow = {-1.0f, -1.0f};
        return true;
    case SDL_EVENT_MOUSE_MOTION:
        accumulateMouseMotion(instance.input, {event.motion.x, event.motion.y}, {event.motion.xrel, event.motion.yrel});
        return true;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        const bool down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        accumulateMouseButton(
                instance.input,
                event.button.button,
                down,
                true,
                {event.button.x, event.button.y},
                static_cast<int>(event.button.clicks),
                currentModifiers(instance.input));
        return true;
    }
    case SDL_EVENT_MOUSE_WHEEL: {
        const float direction = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
        accumulateMouseWheel(instance.input, {event.wheel.mouse_x, event.wheel.mouse_y}, event.wheel.y * direction);
        return true;
    }
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        accumulateModifierKey(instance.input, event.key.scancode, event.key.mod, event.key.down);
        const std::optional<QuaderKeyCode> keyCode = quaderKeyCodeFromSdlKey(event.key.key);
        if (keyCode) {
            accumulateKey(instance.input, *keyCode, event.key.down, event.key.repeat, currentModifiers(instance.input));
        }
        return true;
    }
    default:
        return true;
    }
}

void ViewportHost::handleWindowedViewportFrames(
        QuaderEditorApi& editor,
        ViewportWorkspaceService& workspace,
        SceneDocument& document,
        const AssetRegistry& assets,
        ModelingSessionService& modeling,
        AssetId defaultMaterial) {
    (void)assets;
    for (const std::unique_ptr<ViewportWindowInstance>& viewport : windowedViewports_) {
        ViewportWindowInstance& instance = *viewport;
        ViewportWorkspaceAuxiliaryViewportState& workspaceState =
                ensureWindowedViewportWorkspaceState(workspace, instance);
        workspace.setAuxiliaryViewportVisible(workspaceState.stableId, true);
        workspace.setAuxiliaryViewportRenderSize(workspaceState.stableId, {
                static_cast<float>(instance.framebufferWidth),
                static_cast<float>(instance.framebufferHeight),
        });
        ViewportCameraController& camera = workspaceState.camera;
        syncInputRouterFromEditorState(instance.inputRouter, editor, modeling);
        instance.inputRouter.setGridPreset(workspace.gridPreset());
        ViewportInputFrame input = buildWindowedViewportInputFrame(instance);
        const float scaleX = instance.windowWidth > 0
                ? static_cast<float>(instance.framebufferWidth) / static_cast<float>(instance.windowWidth)
                : 1.0f;
        const float scaleY = instance.windowHeight > 0
                ? static_cast<float>(instance.framebufferHeight) / static_cast<float>(instance.windowHeight)
                : 1.0f;
        const QVec2 leftClickLocal = {
                instance.input.leftClickWindow.x * scaleX,
                instance.input.leftClickWindow.y * scaleY,
        };
        handleViewportFrameSurface(
                editor,
                workspace,
                document,
                modeling,
                defaultMaterial,
                instance.component,
                instance.inputRouter,
                instance.interactions,
                camera,
                std::move(input),
                instance.input,
                leftClickLocal,
                &instance);
        clearWindowedViewportTransientInput(instance);
    }
}

void ViewportHost::prepareWindowedViewports(
        QuaderEditorApi& editor,
        SceneRenderBridge& renderBridge,
        ViewportWorkspaceService& workspace) {
    for (const std::unique_ptr<ViewportWindowInstance>& viewport : windowedViewports_) {
        ViewportWindowInstance& instance = *viewport;
        ViewportWorkspaceAuxiliaryViewportState& workspaceState =
                ensureWindowedViewportWorkspaceState(workspace, instance);
        instance.window.refreshPixelSize();
        instance.framebufferWidth = instance.window.widthPixels();
        instance.framebufferHeight = instance.window.heightPixels();
        workspace.setAuxiliaryViewportVisible(workspaceState.stableId, true);
        workspace.setAuxiliaryViewportRenderSize(workspaceState.stableId, {
                static_cast<float>(instance.framebufferWidth),
                static_cast<float>(instance.framebufferHeight),
        });
        instance.renderer.resize(instance.framebufferWidth, instance.framebufferHeight);
        instance.renderer.updateCamera(workspaceState.camera);
        instance.renderer.updateGroundGrid(
                workspaceState.camera,
                viewportGridWorldSizeForPreset(workspace.gridPreset()),
                settings_,
                true);
        instance.renderer.setSceneVisible(workspace.auxiliaryViewportVisible(workspaceState.stableId));
        const ViewportOverlayCamera overlayCamera = ViewportCameraAdapter::overlayCamera(
                workspaceState.camera,
                {static_cast<float>(instance.framebufferWidth), static_cast<float>(instance.framebufferHeight)});
        ViewportOverlayFrame toolOverlayFrame =
                buildToolOverlayFrame(instance.inputRouter, instance.interactions, overlayCamera);
        const bool hasToolOverlay = viewportOverlayFrameVisible(toolOverlayFrame);
        const QuaderApiResult<QuaderRenderOverlaySubmission> overlaySubmission =
                QuaderRenderApiRuntimeAccess::syncSelectionOverlay(
                        editor.render(),
                        renderBridge,
                        instance.component.layerIndex,
                        viewportGridLayerMask(instance.component.layerIndex),
                        overlayCamera,
                        hasToolOverlay ? &toolOverlayFrame : nullptr);
        logCommandFailure("Sync windowed selection overlay", overlaySubmission);
        instance.renderer.setOverlayVisible(overlaySubmission.ok && overlaySubmission.value.visible);
    }
}

ViewportWorkspaceAuxiliaryViewportState& ViewportHost::ensureWindowedViewportWorkspaceState(
        ViewportWorkspaceService& workspace,
        const ViewportWindowInstance& instance) const {
    return workspace.ensureAuxiliaryViewport(
            instance.component.stableId,
            instance.component.layerIndex);
}

void ViewportHost::flushRemovedWindowedViewportWorkspaceState(ViewportWorkspaceService& workspace) {
    for (const std::string& stableId : removedWindowedViewportIds_) {
        (void)workspace.removeAuxiliaryViewport(stableId);
    }
    removedWindowedViewportIds_.clear();
}

ViewportInputFrame ViewportHost::buildWindowedViewportInputFrame(const ViewportWindowInstance& instance) const {
    const float scaleX = instance.windowWidth > 0
            ? static_cast<float>(instance.framebufferWidth) / static_cast<float>(instance.windowWidth)
            : 1.0f;
    const float scaleY = instance.windowHeight > 0
            ? static_cast<float>(instance.framebufferHeight) / static_cast<float>(instance.windowHeight)
            : 1.0f;
    const QVec2 mousePixel = {
            instance.input.mouseWindow.x * scaleX,
            instance.input.mouseWindow.y * scaleY,
    };
    const QVec2 mouseDelta = instance.relativeMouseCaptured
            ? instance.input.relativeMouseDelta
            : QVec2{instance.input.mouseDeltaWindow.x * scaleX, instance.input.mouseDeltaWindow.y * scaleY};
    const bool hovered = mousePixel.x >= 0.0f &&
            mousePixel.y >= 0.0f &&
            mousePixel.x <= static_cast<float>(instance.framebufferWidth) &&
            mousePixel.y <= static_cast<float>(instance.framebufferHeight);
    ViewportInputFrame input;
    input.viewportHovered = hovered || instance.inputRouter.navigating();
    input.viewportFocused = true;
    input.textInputActive = textInputActive_;
    input.navigationActive = instance.inputRouter.navigating();
    input.flyNavigationActive = instance.inputRouter.flyNavigating();
    input.orbitNavigationActive = instance.inputRouter.orbitNavigating();
    input.panNavigationActive = instance.inputRouter.panNavigating();
    input.modalToolActive = instance.inputRouter.hasActiveModalPreview();
    input.activeToolOwnsViewport = instance.inputRouter.activeToolOwnsViewport();
    input.mouseWheel = input.viewportHovered ? instance.input.wheelY : 0.0f;
    input.deltaSeconds = static_cast<float>(std::max(instance.frameTiming.deltaSeconds, 0.0));
    input.mouseScreen = mousePixel;
    input.mouseLocal = mousePixel;
    input.mouseDelta = mouseDelta;
    input.viewportSize = {
            static_cast<float>(instance.framebufferWidth),
            static_cast<float>(instance.framebufferHeight),
    };
    const ViewportModifiers modifiers = currentModifiers(instance.input);
    input.modifiers = modifiers;
    input.pressedInputChords = instance.input.pressedInputChords;
    input.releasedInputChords = instance.input.releasedInputChords;
    appendActiveMouseDragChords(instance.input.mouseDragChordsDown, input.activeInputChords);
    appendActiveKeyboardChords(instance.input.keyboardKeysDown, modifiers, input.activeInputChords);
    appendWheelChords(input.mouseWheel, modifiers, input.wheelInputChords);
    return input;
}

void ViewportHost::setWindowedViewportCursorCaptured(ViewportWindowInstance& instance, bool captured) {
    if (!instance.window.sdlWindow() || instance.relativeMouseCaptured == captured) {
        return;
    }

    if (!SDL_SetWindowRelativeMouseMode(instance.window.sdlWindow(), captured)) {
        if (!instance.relativeMouseCaptureWarningIssued) {
            LOG_WARN("Unable to {} windowed viewport relative mouse mode: {}",
                    captured ? "enable" : "disable",
                    SDL_GetError());
            instance.relativeMouseCaptureWarningIssued = true;
        }
        return;
    }

    instance.relativeMouseCaptured = captured;
    instance.relativeMouseCaptureWarningIssued = false;
    float discardX = 0.0f;
    float discardY = 0.0f;
    (void)SDL_GetRelativeMouseState(&discardX, &discardY);
    instance.input.relativeMouseDelta = {};
}

void ViewportHost::clearWindowedViewportTransientInput(ViewportWindowInstance& instance) {
    clearTransientInputState(instance.input);
}

std::uint8_t ViewportHost::nextWindowedViewportLayerIndex() const {
    for (std::uint8_t layerIndex = 1u; layerIndex < kMaxViewportComponents; ++layerIndex) {
        const bool used = std::any_of(windowedViewports_.begin(), windowedViewports_.end(), [layerIndex](const auto& viewport) {
            return viewport->component.layerIndex == layerIndex;
        });
        if (!used) {
            return layerIndex;
        }
    }
    return 0u;
}

void ViewportHost::shutdownWindowedViewport(ViewportWindowInstance& instance) {
    setWindowedViewportCursorCaptured(instance, false);
    instance.renderer.shutdown();
    if (renderSystem_ && instance.swapChain) {
        renderSystem_->destroyAdditionalSwapChain(instance.swapChain);
        instance.swapChain = nullptr;
    }
    instance.window.destroy();
}

} // namespace quader
