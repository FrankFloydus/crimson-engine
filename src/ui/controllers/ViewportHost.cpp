////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/controllers/ViewportHost.h"

#include "api/QuaderEditorApi.h"
#include "core/Logger.h"
#include "editor/modeling/services/ModelingSessionService.h"
#include "editor/modeling/services/SelectionModeAdapter.h"
#include "editor/tools/ViewportToolCatalog.h"
#include "editor/viewport/QuaderViewportApiRuntimeAccess.h"
#include "platform/PlatformWindow.h"
#include "render/RenderLayers.h"
#include "render/ViewportOverlayFrame.h"
#include "render/overlay/ViewportBoxOverlayBuilder.h"
#include "render/overlay/ViewportTransformGizmoOverlayBuilder.h"
#include "scene/SceneBounds.h"
#include "scene/SceneEntityNaming.h"
#include "ui/ViewportShortcutController.h"
#include "viewport/ViewportCameraAdapter.h"
#include "viewport/ViewportWorkspaceService.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_video.h>

#include <algorithm>
#include <cmath>
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

std::optional<QuaderKeyCode> quaderKeyCodeFromSdlKey(SDL_Keycode key) {
    switch (key) {
    case SDLK_A:
        return QuaderKeyCode::A;
    case SDLK_B:
        return QuaderKeyCode::B;
    case SDLK_C:
        return QuaderKeyCode::C;
    case SDLK_D:
        return QuaderKeyCode::D;
    case SDLK_E:
        return QuaderKeyCode::E;
    case SDLK_F:
        return QuaderKeyCode::F;
    case SDLK_G:
        return QuaderKeyCode::G;
    case SDLK_H:
        return QuaderKeyCode::H;
    case SDLK_I:
        return QuaderKeyCode::I;
    case SDLK_J:
        return QuaderKeyCode::J;
    case SDLK_K:
        return QuaderKeyCode::K;
    case SDLK_L:
        return QuaderKeyCode::L;
    case SDLK_M:
        return QuaderKeyCode::M;
    case SDLK_N:
        return QuaderKeyCode::N;
    case SDLK_O:
        return QuaderKeyCode::O;
    case SDLK_P:
        return QuaderKeyCode::P;
    case SDLK_Q:
        return QuaderKeyCode::Q;
    case SDLK_R:
        return QuaderKeyCode::R;
    case SDLK_S:
        return QuaderKeyCode::S;
    case SDLK_T:
        return QuaderKeyCode::T;
    case SDLK_U:
        return QuaderKeyCode::U;
    case SDLK_V:
        return QuaderKeyCode::V;
    case SDLK_W:
        return QuaderKeyCode::W;
    case SDLK_X:
        return QuaderKeyCode::X;
    case SDLK_Y:
        return QuaderKeyCode::Y;
    case SDLK_Z:
        return QuaderKeyCode::Z;
    case SDLK_0:
        return QuaderKeyCode::Digit0;
    case SDLK_1:
        return QuaderKeyCode::Digit1;
    case SDLK_2:
        return QuaderKeyCode::Digit2;
    case SDLK_3:
        return QuaderKeyCode::Digit3;
    case SDLK_4:
        return QuaderKeyCode::Digit4;
    case SDLK_5:
        return QuaderKeyCode::Digit5;
    case SDLK_6:
        return QuaderKeyCode::Digit6;
    case SDLK_7:
        return QuaderKeyCode::Digit7;
    case SDLK_8:
        return QuaderKeyCode::Digit8;
    case SDLK_9:
        return QuaderKeyCode::Digit9;
    case SDLK_KP_0:
        return QuaderKeyCode::Numpad0;
    case SDLK_KP_1:
        return QuaderKeyCode::Numpad1;
    case SDLK_KP_2:
        return QuaderKeyCode::Numpad2;
    case SDLK_KP_3:
        return QuaderKeyCode::Numpad3;
    case SDLK_KP_4:
        return QuaderKeyCode::Numpad4;
    case SDLK_KP_5:
        return QuaderKeyCode::Numpad5;
    case SDLK_KP_6:
        return QuaderKeyCode::Numpad6;
    case SDLK_KP_7:
        return QuaderKeyCode::Numpad7;
    case SDLK_KP_8:
        return QuaderKeyCode::Numpad8;
    case SDLK_KP_9:
        return QuaderKeyCode::Numpad9;
    case SDLK_KP_PLUS:
        return QuaderKeyCode::NumpadAdd;
    case SDLK_KP_MINUS:
        return QuaderKeyCode::NumpadSubtract;
    case SDLK_KP_MULTIPLY:
        return QuaderKeyCode::NumpadMultiply;
    case SDLK_KP_DIVIDE:
        return QuaderKeyCode::NumpadDivide;
    case SDLK_KP_PERIOD:
        return QuaderKeyCode::NumpadDecimal;
    case SDLK_HOME:
        return QuaderKeyCode::Home;
    case SDLK_END:
        return QuaderKeyCode::End;
    case SDLK_PAGEUP:
        return QuaderKeyCode::PageUp;
    case SDLK_PAGEDOWN:
        return QuaderKeyCode::PageDown;
    case SDLK_LEFT:
        return QuaderKeyCode::Left;
    case SDLK_RIGHT:
        return QuaderKeyCode::Right;
    case SDLK_UP:
        return QuaderKeyCode::Up;
    case SDLK_DOWN:
        return QuaderKeyCode::Down;
    case SDLK_COMMA:
        return QuaderKeyCode::Comma;
    case SDLK_PERIOD:
        return QuaderKeyCode::Period;
    case SDLK_SLASH:
        return QuaderKeyCode::Slash;
    case SDLK_BACKSLASH:
        return QuaderKeyCode::Backslash;
    case SDLK_SEMICOLON:
        return QuaderKeyCode::Semicolon;
    case SDLK_APOSTROPHE:
        return QuaderKeyCode::Apostrophe;
    case SDLK_LEFTBRACKET:
        return QuaderKeyCode::LeftBracket;
    case SDLK_RIGHTBRACKET:
        return QuaderKeyCode::RightBracket;
    case SDLK_MINUS:
        return QuaderKeyCode::Minus;
    case SDLK_EQUALS:
        return QuaderKeyCode::Equal;
    case SDLK_GRAVE:
        return QuaderKeyCode::Grave;
    case SDLK_SPACE:
        return QuaderKeyCode::Space;
    case SDLK_INSERT:
        return QuaderKeyCode::Insert;
    case SDLK_F1:
        return QuaderKeyCode::F1;
    case SDLK_F2:
        return QuaderKeyCode::F2;
    case SDLK_F3:
        return QuaderKeyCode::F3;
    case SDLK_F4:
        return QuaderKeyCode::F4;
    case SDLK_F5:
        return QuaderKeyCode::F5;
    case SDLK_F6:
        return QuaderKeyCode::F6;
    case SDLK_F7:
        return QuaderKeyCode::F7;
    case SDLK_F8:
        return QuaderKeyCode::F8;
    case SDLK_F9:
        return QuaderKeyCode::F9;
    case SDLK_F10:
        return QuaderKeyCode::F10;
    case SDLK_F11:
        return QuaderKeyCode::F11;
    case SDLK_F12:
        return QuaderKeyCode::F12;
    case SDLK_F13:
        return QuaderKeyCode::F13;
    case SDLK_F14:
        return QuaderKeyCode::F14;
    case SDLK_F15:
        return QuaderKeyCode::F15;
    case SDLK_F16:
        return QuaderKeyCode::F16;
    case SDLK_F17:
        return QuaderKeyCode::F17;
    case SDLK_F18:
        return QuaderKeyCode::F18;
    case SDLK_F19:
        return QuaderKeyCode::F19;
    case SDLK_F20:
        return QuaderKeyCode::F20;
    case SDLK_F21:
        return QuaderKeyCode::F21;
    case SDLK_F22:
        return QuaderKeyCode::F22;
    case SDLK_F23:
        return QuaderKeyCode::F23;
    case SDLK_F24:
        return QuaderKeyCode::F24;
    case SDLK_ESCAPE:
        return QuaderKeyCode::Escape;
    case SDLK_DELETE:
        return QuaderKeyCode::Delete;
    case SDLK_BACKSPACE:
        return QuaderKeyCode::Backspace;
    case SDLK_RETURN:
    case SDLK_RETURN2:
        return QuaderKeyCode::Enter;
    case SDLK_KP_ENTER:
        return QuaderKeyCode::NumpadEnter;
    case SDLK_TAB:
        return QuaderKeyCode::Tab;
    default:
        return std::nullopt;
    }
}

std::optional<ViewportKey> viewportKeyFromKeyCode(QuaderKeyCode key) {
    switch (key) {
    case QuaderKeyCode::A:
        return ViewportKey::A;
    case QuaderKeyCode::B:
        return ViewportKey::B;
    case QuaderKeyCode::C:
        return ViewportKey::C;
    case QuaderKeyCode::D:
        return ViewportKey::D;
    case QuaderKeyCode::E:
        return ViewportKey::E;
    case QuaderKeyCode::F:
        return ViewportKey::F;
    case QuaderKeyCode::G:
        return ViewportKey::G;
    case QuaderKeyCode::H:
        return ViewportKey::H;
    case QuaderKeyCode::I:
        return ViewportKey::I;
    case QuaderKeyCode::J:
        return ViewportKey::J;
    case QuaderKeyCode::K:
        return ViewportKey::K;
    case QuaderKeyCode::L:
        return ViewportKey::L;
    case QuaderKeyCode::M:
        return ViewportKey::M;
    case QuaderKeyCode::N:
        return ViewportKey::N;
    case QuaderKeyCode::O:
        return ViewportKey::O;
    case QuaderKeyCode::P:
        return ViewportKey::P;
    case QuaderKeyCode::Q:
        return ViewportKey::Q;
    case QuaderKeyCode::R:
        return ViewportKey::R;
    case QuaderKeyCode::S:
        return ViewportKey::S;
    case QuaderKeyCode::T:
        return ViewportKey::T;
    case QuaderKeyCode::U:
        return ViewportKey::U;
    case QuaderKeyCode::V:
        return ViewportKey::V;
    case QuaderKeyCode::W:
        return ViewportKey::W;
    case QuaderKeyCode::X:
        return ViewportKey::X;
    case QuaderKeyCode::Y:
        return ViewportKey::Y;
    case QuaderKeyCode::Z:
        return ViewportKey::Z;
    case QuaderKeyCode::Digit0:
    case QuaderKeyCode::Numpad0:
        return ViewportKey::Number0;
    case QuaderKeyCode::Digit1:
    case QuaderKeyCode::Numpad1:
        return ViewportKey::Number1;
    case QuaderKeyCode::Digit2:
    case QuaderKeyCode::Numpad2:
        return ViewportKey::Number2;
    case QuaderKeyCode::Digit3:
    case QuaderKeyCode::Numpad3:
        return ViewportKey::Number3;
    case QuaderKeyCode::Digit4:
    case QuaderKeyCode::Numpad4:
        return ViewportKey::Number4;
    case QuaderKeyCode::Digit5:
    case QuaderKeyCode::Numpad5:
        return ViewportKey::Number5;
    case QuaderKeyCode::Digit6:
    case QuaderKeyCode::Numpad6:
        return ViewportKey::Number6;
    case QuaderKeyCode::Digit7:
    case QuaderKeyCode::Numpad7:
        return ViewportKey::Number7;
    case QuaderKeyCode::Digit8:
    case QuaderKeyCode::Numpad8:
        return ViewportKey::Number8;
    case QuaderKeyCode::Digit9:
    case QuaderKeyCode::Numpad9:
        return ViewportKey::Number9;
    case QuaderKeyCode::Home:
        return ViewportKey::Home;
    case QuaderKeyCode::End:
        return ViewportKey::End;
    case QuaderKeyCode::PageUp:
        return ViewportKey::PageUp;
    case QuaderKeyCode::PageDown:
        return ViewportKey::PageDown;
    case QuaderKeyCode::Left:
        return ViewportKey::Left;
    case QuaderKeyCode::Right:
        return ViewportKey::Right;
    case QuaderKeyCode::Up:
        return ViewportKey::Up;
    case QuaderKeyCode::Down:
        return ViewportKey::Down;
    case QuaderKeyCode::Space:
        return ViewportKey::Space;
    case QuaderKeyCode::F1:
        return ViewportKey::F1;
    case QuaderKeyCode::F2:
        return ViewportKey::F2;
    case QuaderKeyCode::F3:
        return ViewportKey::F3;
    case QuaderKeyCode::F4:
        return ViewportKey::F4;
    case QuaderKeyCode::F5:
        return ViewportKey::F5;
    case QuaderKeyCode::F6:
        return ViewportKey::F6;
    case QuaderKeyCode::F7:
        return ViewportKey::F7;
    case QuaderKeyCode::F8:
        return ViewportKey::F8;
    case QuaderKeyCode::F9:
        return ViewportKey::F9;
    case QuaderKeyCode::F10:
        return ViewportKey::F10;
    case QuaderKeyCode::F11:
        return ViewportKey::F11;
    case QuaderKeyCode::F12:
        return ViewportKey::F12;
    case QuaderKeyCode::Escape:
        return ViewportKey::Escape;
    case QuaderKeyCode::Delete:
        return ViewportKey::Delete;
    case QuaderKeyCode::Backspace:
        return ViewportKey::Backspace;
    case QuaderKeyCode::Enter:
    case QuaderKeyCode::NumpadEnter:
        return ViewportKey::Enter;
    case QuaderKeyCode::Tab:
        return ViewportKey::Tab;
    default:
        return std::nullopt;
    }
}

std::optional<QuaderMouseButton> mouseButtonFromSdlButton(std::uint8_t button) {
    switch (button) {
    case SDL_BUTTON_LEFT:
        return QuaderMouseButton::Left;
    case SDL_BUTTON_MIDDLE:
        return QuaderMouseButton::Middle;
    case SDL_BUTTON_RIGHT:
        return QuaderMouseButton::Right;
    case SDL_BUTTON_X1:
        return QuaderMouseButton::Back;
    case SDL_BUTTON_X2:
        return QuaderMouseButton::Forward;
    default:
        return std::nullopt;
    }
}

void appendActiveMouseDragChords(
        const std::unordered_map<QuaderMouseButton, QuaderInputChord>& dragChordsDown,
        std::vector<QuaderInputChord>& chords) {
    for (const auto& [button, chord] : dragChordsDown) {
        (void)button;
        chords.push_back(chord);
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

bool inputKeyPressed(const ViewportInputFrame& input, ViewportKey key) {
    return std::find(input.pressedKeys.begin(), input.pressedKeys.end(), key) != input.pressedKeys.end();
}

bool viewportOverlayFrameVisible(const ViewportOverlayFrame& frame) {
    return !frame.lines.empty() || !frame.triangles.empty() || !frame.points.empty();
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

void syncInputRouterFromEditorState(
        ViewportInputRouter& inputRouter,
        QuaderEditorApi& editor,
        ModelingSessionService& modeling) {
    const ViewportToolId activeTool = viewportToolForModeling(modeling.toolStatus().activeTool);
    if (inputRouter.state().activeTool != activeTool) {
        inputRouter.setActiveTool(activeTool);
        inputRouter.clearModalToolState();
    }
    inputRouter.setSelectionMode(appSelectionModeToViewport(editor.selection().summary().mode));
}

void activateViewportToolFromRouter(
        QuaderEditorApi& editor,
        SceneDocument& document,
        ViewportInputRouter& inputRouter) {
    LOG_INFO("Viewport tool changed to {}", viewportToolId(inputRouter.state().activeTool));
    const bool preserveGroupSelection =
            inputRouter.state().selectionMode == ViewportSelectionMode::Group ||
            editor.selection().summary().mode == ModelingSelectionMode::Group;
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
        (void)editor.selection().setMode(ModelingSelectionMode::Group);
        inputRouter.setSelectionMode(ViewportSelectionMode::Group);
    } else {
        inputRouter.setSelectionMode(appSelectionModeToViewport(editor.selection().summary().mode));
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

    ViewportToolContext toolContext{
            .state = const_cast<ViewportToolState&>(inputRouter.state()),
            .camera = camera,
            .modeling = modeling,
            .document = &document,
            .settings = &settings,
            .box = &boxToolContext,
    };
    toolContext.claimHover = [](ModelingBoxDragState) {
        return true;
    };
    toolContext.invalidateHover = []() {};
    toolContext.markOverlayDirty = []() {};
    toolContext.setActiveTool = [&](ViewportToolId tool) {
        inputRouter.setActiveTool(tool);
    };
    toolContext.setSelectionMode = [&](ViewportSelectionMode mode) {
        inputRouter.setSelectionMode(mode);
    };
    toolContext.setAppSelectionMode = [&](ModelingSelectionMode mode) {
        const QuaderApiVoidResult command = editor.selection().setMode(mode);
        if (!command.ok) {
            LOG_ERROR("Selection mode change failed: {}", command.message.empty() ? "Command failed." : command.message);
        }
        inputRouter.setSelectionMode(appSelectionModeToViewport(editor.selection().summary().mode));
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
                    .finalizePressed =
                            inputKeyPressed(input, ViewportKey::Space) ||
                            inputKeyPressed(input, ViewportKey::Enter),
                    .modeCyclePressed = inputKeyPressed(input, ViewportKey::F),
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

std::optional<SDL_WindowID> eventWindowId(const SDL_Event& event) {
    switch (event.type) {
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        return event.window.windowID;
    case SDL_EVENT_MOUSE_MOTION:
        return event.motion.windowID;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        return event.button.windowID;
    case SDL_EVENT_MOUSE_WHEEL:
        return event.wheel.windowID;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        return event.key.windowID;
    default:
        return std::nullopt;
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
    default:
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
            QuaderViewportKeybindingContext{
                    .viewportHovered = input.viewportHovered,
                    .navigationActive = input.navigationActive,
                    .flyNavigationActive = input.flyNavigationActive,
                    .orbitNavigationActive = input.orbitNavigationActive,
                    .panNavigationActive = input.panNavigationActive,
                    .modalToolActive = input.modalToolActive,
                    .activeToolOwnsViewport = input.activeToolOwnsViewport,
            });
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

void resolveKeybindingsForFrame(
        QuaderEditorApi& editor,
        const ViewportToolState& state,
        ViewportInputFrame& input) {
    input.shortcutResolverActive = true;
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
            markNavigationPressed(input.navigationBindings, action);
            continue;
        }

        const QuaderViewportKeybindingContext keybindingContext = keybindingContextFromInput(input);
        const QuaderViewportKeybindingDispatch dispatch = editor.commands().dispatchViewportInputBinding(
                chord,
                state,
                input.viewportFocused,
                input.textInputActive,
                keybindingContext);
        switch (dispatch.kind) {
        case QuaderViewportKeybindingDispatch::Kind::PendingChord:
            input.resolvedShortcuts.push_back(ViewportShortcut{});
            return;
        case QuaderViewportKeybindingDispatch::Kind::ViewportShortcut:
            input.resolvedShortcuts.push_back(dispatch.shortcut);
            return;
        case QuaderViewportKeybindingDispatch::Kind::Command:
            logCommandFailure(
                    "Keybinding command",
                    editor.commands().execute(dispatch.commandId));
            input.resolvedShortcuts.push_back(ViewportShortcut{});
            return;
        case QuaderViewportKeybindingDispatch::Kind::NoMatch:
            break;
        }
    }

    if (input.pressedInputChords.empty()) {
        for (ViewportKey key : input.pressedKeys) {
            const QuaderViewportKeybindingContext keybindingContext = keybindingContextFromInput(input);
            const QuaderViewportKeybindingDispatch dispatch = editor.commands().dispatchViewportKeybinding(
                    key,
                    input.modifiers,
                    state,
                    input.viewportFocused,
                    input.textInputActive,
                    keybindingContext);
            switch (dispatch.kind) {
            case QuaderViewportKeybindingDispatch::Kind::PendingChord:
                input.resolvedShortcuts.push_back(ViewportShortcut{});
                return;
            case QuaderViewportKeybindingDispatch::Kind::ViewportShortcut:
                input.resolvedShortcuts.push_back(dispatch.shortcut);
                return;
            case QuaderViewportKeybindingDispatch::Kind::Command:
                logCommandFailure(
                        "Keybinding command",
                        editor.commands().execute(dispatch.commandId));
                input.resolvedShortcuts.push_back(ViewportShortcut{});
                return;
            case QuaderViewportKeybindingDispatch::Kind::NoMatch:
                break;
            }
        }
    }
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
    ViewportCameraController camera;
    ViewportInputRouter inputRouter;
    FrameTiming frameTiming = {};
    SDL_WindowID windowId = 0u;
    std::uint32_t framebufferWidth = 1u;
    std::uint32_t framebufferHeight = 1u;
    int windowWidth = 1;
    int windowHeight = 1;
    QVec2 mouseWindow = {};
    QVec2 mouseDeltaWindow = {};
    QVec2 relativeMouseDelta = {};
    bool middleDown = false;
    bool middlePressed = false;
    bool middleReleased = false;
    bool leftDown = false;
    bool leftPressed = false;
    bool leftReleased = false;
    QVec2 leftClickWindow = {};
    int leftClickCount = 1;
    bool rightDown = false;
    bool rightPressed = false;
    bool rightReleased = false;
    float wheelY = 0.0f;
    std::unordered_set<ViewportKey> keysDown;
    std::vector<ViewportKey> pressedKeys;
    std::unordered_set<QuaderMouseButton> mouseButtonsDown;
    std::unordered_map<QuaderMouseButton, QuaderInputChord> mouseDragChordsDown;
    std::vector<QuaderInputChord> pressedInputChords;
    std::vector<QuaderInputChord> activeInputChords;
    std::vector<QuaderInputChord> releasedInputChords;
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

    if (event.type == SDL_EVENT_KEY_DOWN &&
            event.key.key == SDLK_F12 &&
            !event.key.repeat &&
            (SDL_GetModState() & (SDL_KMOD_CTRL | SDL_KMOD_GUI | SDL_KMOD_SHIFT | SDL_KMOD_ALT)) == 0) {
        spawnWindowedViewport();
        return true;
    }

    if (processWindowedViewportEvent(event)) {
        return true;
    }

    const std::optional<SDL_WindowID> windowId = eventWindowId(event);
    if (windowId && *windowId != SDL_GetWindowID(window_)) {
        return false;
    }

    switch (event.type) {
    case SDL_EVENT_MOUSE_MOTION:
        mouseWindow_ = {event.motion.x, event.motion.y};
        mouseDeltaWindow_ = mouseDeltaWindow_ + QVec2{event.motion.xrel, event.motion.yrel};
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        mouseWindow_ = {event.button.x, event.button.y};
        const bool down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        const bool startedInMainViewport = down && mainViewportContainsWindowPoint(event.button.x, event.button.y);
        const std::optional<QuaderMouseButton> mouseButton = mouseButtonFromSdlButton(event.button.button);
        const ViewportModifiers modifiers = currentModifiers();
        if (event.button.button == SDL_BUTTON_MIDDLE) {
            if (down) {
                middleDown_ = startedInMainViewport;
                middlePressed_ = middlePressed_ || startedInMainViewport;
            } else {
                middleReleased_ = middleReleased_ || middleDown_;
                middleDown_ = false;
            }
        } else if (event.button.button == SDL_BUTTON_LEFT) {
            if (down) {
                leftDown_ = startedInMainViewport;
                leftPressed_ = leftPressed_ || startedInMainViewport;
                if (startedInMainViewport) {
                    leftClickWindow_ = {event.button.x, event.button.y};
                    leftClickCount_ = std::max(1, static_cast<int>(event.button.clicks));
                }
            } else {
                leftReleased_ = leftReleased_ || leftDown_;
                leftDown_ = false;
            }
        } else if (event.button.button == SDL_BUTTON_RIGHT) {
            if (down) {
                rightDown_ = startedInMainViewport;
                rightPressed_ = rightPressed_ || startedInMainViewport;
            } else {
                rightReleased_ = rightReleased_ || rightDown_;
                rightDown_ = false;
            }
        }
        if (mouseButton) {
            if (down && startedInMainViewport) {
                const QuaderInputChord dragChord = mouseInputChord(*mouseButton, QuaderInputGesture::Drag, modifiers);
                mouseButtonsDown_.insert(*mouseButton);
                mouseDragChordsDown_[*mouseButton] = dragChord;
                pressedInputChords_.push_back(mouseInputChord(*mouseButton, QuaderInputGesture::Press, modifiers));
                pressedInputChords_.push_back(dragChord);
            } else if (!down) {
                const bool wasDown = mouseButtonsDown_.contains(*mouseButton);
                const auto dragChord = mouseDragChordsDown_.find(*mouseButton);
                const std::optional<QuaderInputChord> releasedDragChord =
                        dragChord == mouseDragChordsDown_.end() ? std::nullopt : std::optional<QuaderInputChord>(dragChord->second);
                mouseButtonsDown_.erase(*mouseButton);
                mouseDragChordsDown_.erase(*mouseButton);
                if (wasDown && releasedDragChord) {
                    releasedInputChords_.push_back(*releasedDragChord);
                }
            }
        }
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL: {
        mouseWindow_ = {event.wheel.mouse_x, event.wheel.mouse_y};
        const float direction = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
        wheelY_ += event.wheel.y * direction;
        break;
    }
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        const std::optional<QuaderKeyCode> keyCode = quaderKeyCodeFromSdlKey(event.key.key);
        if (keyCode) {
            const std::optional<ViewportKey> key = viewportKeyFromKeyCode(*keyCode);
            if (event.key.down) {
                if (key) {
                    keysDown_.insert(*key);
                }
                if (!event.key.repeat) {
                    if (key) {
                        pressedKeys_.push_back(*key);
                    }
                    pressedInputChords_.push_back(keyboardInputChord(*keyCode, currentModifiers()));
                }
            } else {
                if (key) {
                    keysDown_.erase(*key);
                }
            }
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
    relativeMouseDelta_ = {};
    if (relativeMouseCaptured_) {
        float deltaX = 0.0f;
        float deltaY = 0.0f;
        (void)SDL_GetRelativeMouseState(&deltaX, &deltaY);
        const float scaleX = windowWidth_ > 0 ? static_cast<float>(framebufferWidth_) / static_cast<float>(windowWidth_) : 1.0f;
        const float scaleY = windowHeight_ > 0 ? static_cast<float>(framebufferHeight_) / static_cast<float>(windowHeight_) : 1.0f;
        relativeMouseDelta_ = {deltaX * scaleX, deltaY * scaleY};
    }

    for (const std::unique_ptr<ViewportWindowInstance>& viewport : windowedViewports_) {
        ViewportWindowInstance& instance = *viewport;
        instance.frameTiming = frameTiming;
        instance.relativeMouseDelta = {};
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
        instance.relativeMouseDelta = {deltaX * scaleX, deltaY * scaleY};
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
    workspace.focusPane(mainViewport_.pane);
    workspace.clearPaneVisibility();
    workspace.setPaneVisible(mainViewport_.pane, true);
    workspace.setPaneRenderSize(mainViewport_.pane, {
            static_cast<float>(mainViewportFrame_.width),
            static_cast<float>(mainViewportFrame_.height),
    });

    if (inputSuppressed) {
        setCursorCaptured(false);
        (void)viewportInteractions_.cancelActiveDrag(editor);
        viewportInteractions_.reset();
        clearTransientInput();
        handleWindowedViewportFrames(editor, workspace, document, assets, modeling, defaultMaterial);
        return;
    }

    syncInputRouterFromEditorState(inputRouter_, editor, modeling);
    inputRouter_.setGridPreset(workspace.gridPreset());
    ViewportCameraController& camera = workspace.camera(mainViewport_.pane);
    ViewportInputFrame input = buildViewportInputFrame();
    resolveKeybindingsForFrame(editor, inputRouter_.state(), input);
    syncInputRouterFromEditorState(inputRouter_, editor, modeling);
    const ViewportInputResult result = inputRouter_.handleFrame(input, camera);
    if (result.toolChanged) {
        activateViewportToolFromRouter(editor, document, inputRouter_);
    }
    if (result.gridPresetChanged) {
        workspace.setGridPreset(inputRouter_.state().gridPreset);
    }
    if (result.focusDocumentRequested) {
        focusDocument(document, assets, camera);
    }
    if (result.maximizedToggleRequested) {
        (void)workspace.toggleFocusedPaneMaximized();
    }
    if (result.cancelRequested) {
        (void)viewportInteractions_.cancelActiveDrag(editor);
        logCommandFailure("Cancel active tool", editor.tools().cancelModalTool());
        inputRouter_.clearModalToolState();
    }
    const ViewportShortcutController shortcutController;
    (void)shortcutController.handleModelingShortcut(result, editor);
    ViewportInteractionFrame interactionFrame{
            .inputRouter = inputRouter_,
            .camera = camera,
            .settings = settings_,
            .input = input,
            .inputResult = result,
            .leftClickLocal = mainViewportWindowPointToLocal(leftClickWindow_),
            .leftClickCount = leftClickCount_,
            .leftPressed = leftPressed_,
            .leftDown = leftDown_,
            .leftReleased = leftReleased_,
    };
    const ViewportBeforeToolInteractionResult beforeTool =
            viewportInteractions_.handleBeforeActiveTool(editor, interactionFrame);
    const ViewportToolResult toolResult = dispatchActiveViewportToolFrame(
            editor,
            modeling,
            document,
            inputRouter_,
            camera,
            input,
            result,
            settings_,
            defaultMaterial,
            leftPressed_,
            leftDown_,
            leftReleased_,
            beforeTool.gizmoConsumed,
            interactionFrame.leftClickLocal);
    viewportInteractions_.handleAfterActiveTool(editor, interactionFrame, toolResult, beforeTool);

    setCursorCaptured(result.flyCursorCaptureActive);
    handleWindowedViewportFrames(editor, workspace, document, assets, modeling, defaultMaterial);
    clearTransientInput();
}

void ViewportHost::prepareViewports(
        QuaderEditorApi& editor,
        SceneRenderBridge& renderBridge,
        const SceneDocument& document,
        const AssetRegistry& assets) {
    (void)document;
    (void)assets;
    ViewportWorkspaceService& workspace = QuaderViewportApiRuntimeAccess::workspace(editor.viewports());
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
    logCommandFailure("Sync tool previews", editor.render().syncToolPreviews(renderBridge));
    ViewportOverlayFrame toolOverlayFrame;
    const ModelingBoxPreview& boxPreview = inputRouter_.boxPreview();
    if (boxPreview.dragState != ModelingBoxDragState::Height) {
        ViewportBoxOverlayBuilder::append(toolOverlayFrame, boxPreview);
    }
    const quader_editor_core::TransformGizmoFrame* transformGizmoFrame =
            viewportInteractions_.transformGizmoFrame();
    const ViewportOverlayCamera overlayCamera = ViewportCameraAdapter::overlayCamera(pane.camera, pane.renderSize);
    if (transformGizmoFrame != nullptr) {
        ViewportTransformGizmoOverlayBuilder::append(toolOverlayFrame, *transformGizmoFrame, overlayCamera);
    }
    const bool hasToolOverlay = viewportOverlayFrameVisible(toolOverlayFrame);
    const QuaderApiResult<QuaderRenderOverlaySubmission> overlaySubmission =
            editor.render().syncSelectionOverlay(
                    renderBridge,
                    viewportPaneIndex(mainViewport_.pane),
                    viewportGridLayerMask(mainViewport_.layerIndex),
                    overlayCamera,
                    hasToolOverlay ? &toolOverlayFrame : nullptr);
    logCommandFailure("Sync selection overlay", overlaySubmission);
    renderer_.setOverlayVisible(overlaySubmission.ok && overlaySubmission.value.visible);
    prepareWindowedViewports(workspace);
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
    relativeMouseDelta_ = {};
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
    mouseWindow_ = {mouseX, mouseY};
    if ((buttons & SDL_BUTTON_MMASK) == 0u) {
        middleDown_ = false;
    }
    if ((buttons & SDL_BUTTON_LMASK) == 0u) {
        leftDown_ = false;
    }
    if ((buttons & SDL_BUTTON_RMASK) == 0u) {
        rightDown_ = false;
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

ViewportInputFrame ViewportHost::buildViewportInputFrame() const {
    const float scaleX = windowWidth_ > 0 ? static_cast<float>(framebufferWidth_) / static_cast<float>(windowWidth_) : 1.0f;
    const float scaleY = windowHeight_ > 0 ? static_cast<float>(framebufferHeight_) / static_cast<float>(windowHeight_) : 1.0f;
    const QVec2 mousePixel = {
            mouseWindow_.x * scaleX,
            mouseWindow_.y * scaleY,
    };
    const QVec2 mouseLocal = {
            mousePixel.x - static_cast<float>(mainViewportFrame_.left),
            mousePixel.y - static_cast<float>(mainViewportFrame_.top),
    };
    const QVec2 mouseDelta = relativeMouseCaptured_
            ? relativeMouseDelta_
            : QVec2{mouseDeltaWindow_.x * scaleX, mouseDeltaWindow_.y * scaleY};
    const bool hovered = mouseLocal.x >= 0.0f &&
            mouseLocal.y >= 0.0f &&
            mouseLocal.x <= static_cast<float>(mainViewportFrame_.width) &&
            mouseLocal.y <= static_cast<float>(mainViewportFrame_.height);
    ViewportInputFrame input;
    input.viewportHovered = hovered || inputRouter_.navigating();
    input.viewportFocused = true;
    input.textInputActive = false;
    input.navigationActive = inputRouter_.navigating();
    input.flyNavigationActive = inputRouter_.flyNavigating();
    input.orbitNavigationActive = inputRouter_.orbitNavigating();
    input.panNavigationActive = inputRouter_.panNavigating();
    input.modalToolActive = inputRouter_.hasActiveModalPreview();
    input.activeToolOwnsViewport = inputRouter_.activeToolOwnsViewport();
    input.middlePressed = middlePressed_ && input.viewportHovered;
    input.middleDown = middleDown_;
    input.middleReleased = middleReleased_;
    input.rightPressed = rightPressed_ && input.viewportHovered;
    input.rightDown = rightDown_;
    input.rightReleased = rightReleased_;
    input.mouseWheel = input.viewportHovered ? wheelY_ : 0.0f;
    input.deltaSeconds = static_cast<float>(std::max(currentFrameTiming_.deltaSeconds, 0.0));
    input.mouseScreen = mousePixel;
    input.mouseLocal = mouseLocal;
    input.mouseDelta = mouseDelta;
    input.viewportSize = {
            static_cast<float>(mainViewportFrame_.width),
            static_cast<float>(mainViewportFrame_.height),
    };
    const ViewportModifiers modifiers = currentModifiers();
    input.modifiers = modifiers;
    input.pressedInputChords = pressedInputChords_;
    input.releasedInputChords = releasedInputChords_;
    appendActiveMouseDragChords(mouseDragChordsDown_, input.activeInputChords);
    appendWheelChords(input.mouseWheel, modifiers, input.wheelInputChords);
    if (keysDown_.contains(ViewportKey::W)) {
        input.flyLocalDirection.z += 1.0f;
    }
    if (keysDown_.contains(ViewportKey::S)) {
        input.flyLocalDirection.z -= 1.0f;
    }
    if (keysDown_.contains(ViewportKey::D)) {
        input.flyLocalDirection.x += 1.0f;
    }
    if (keysDown_.contains(ViewportKey::A)) {
        input.flyLocalDirection.x -= 1.0f;
    }
    input.pressedKeys = pressedKeys_;
    return input;
}

ViewportModifiers ViewportHost::currentModifiers() const {
    const SDL_Keymod mod = SDL_GetModState();
    return {
            .primary = (mod & SDL_KMOD_CTRL) != 0 || (mod & SDL_KMOD_GUI) != 0,
            .shift = (mod & SDL_KMOD_SHIFT) != 0,
            .alt = (mod & SDL_KMOD_ALT) != 0,
    };
}

void ViewportHost::clearTransientInput() {
    middlePressed_ = false;
    middleReleased_ = false;
    leftPressed_ = false;
    leftReleased_ = false;
    rightPressed_ = false;
    rightReleased_ = false;
    wheelY_ = 0.0f;
    mouseDeltaWindow_ = {};
    pressedKeys_.clear();
    pressedInputChords_.clear();
    activeInputChords_.clear();
    releasedInputChords_.clear();
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
    const std::string title = "Quader Viewport " + std::to_string(static_cast<int>(layerIndex) + 1);
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
    instance->camera.focusOrigin(64.0f);
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
    const std::optional<SDL_WindowID> windowId = eventWindowId(event);
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
        shutdownWindowedViewport(instance);
        windowedViewports_.erase(found);
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
        instance.mouseWindow = {-1.0f, -1.0f};
        return true;
    case SDL_EVENT_MOUSE_MOTION:
        instance.mouseWindow = {event.motion.x, event.motion.y};
        instance.mouseDeltaWindow = instance.mouseDeltaWindow + QVec2{event.motion.xrel, event.motion.yrel};
        return true;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        instance.mouseWindow = {event.button.x, event.button.y};
        const bool down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        const std::optional<QuaderMouseButton> mouseButton = mouseButtonFromSdlButton(event.button.button);
        const ViewportModifiers modifiers = currentModifiers();
        if (event.button.button == SDL_BUTTON_MIDDLE) {
            instance.middleDown = down;
            instance.middlePressed = instance.middlePressed || down;
            instance.middleReleased = instance.middleReleased || !down;
        } else if (event.button.button == SDL_BUTTON_LEFT) {
            instance.leftDown = down;
            instance.leftPressed = instance.leftPressed || down;
            instance.leftReleased = instance.leftReleased || !down;
            if (down) {
                instance.leftClickWindow = {event.button.x, event.button.y};
                instance.leftClickCount = std::max(1, static_cast<int>(event.button.clicks));
            }
        } else if (event.button.button == SDL_BUTTON_RIGHT) {
            instance.rightDown = down;
            instance.rightPressed = instance.rightPressed || down;
            instance.rightReleased = instance.rightReleased || !down;
        }
        if (mouseButton) {
            if (down) {
                const QuaderInputChord dragChord = mouseInputChord(*mouseButton, QuaderInputGesture::Drag, modifiers);
                instance.mouseButtonsDown.insert(*mouseButton);
                instance.mouseDragChordsDown[*mouseButton] = dragChord;
                instance.pressedInputChords.push_back(mouseInputChord(*mouseButton, QuaderInputGesture::Press, modifiers));
                instance.pressedInputChords.push_back(dragChord);
            } else {
                const bool wasDown = instance.mouseButtonsDown.contains(*mouseButton);
                const auto dragChord = instance.mouseDragChordsDown.find(*mouseButton);
                const std::optional<QuaderInputChord> releasedDragChord =
                        dragChord == instance.mouseDragChordsDown.end() ? std::nullopt : std::optional<QuaderInputChord>(dragChord->second);
                instance.mouseButtonsDown.erase(*mouseButton);
                instance.mouseDragChordsDown.erase(*mouseButton);
                if (wasDown && releasedDragChord) {
                    instance.releasedInputChords.push_back(*releasedDragChord);
                }
            }
        }
        return true;
    }
    case SDL_EVENT_MOUSE_WHEEL: {
        instance.mouseWindow = {event.wheel.mouse_x, event.wheel.mouse_y};
        const float direction = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
        instance.wheelY += event.wheel.y * direction;
        return true;
    }
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        const std::optional<QuaderKeyCode> keyCode = quaderKeyCodeFromSdlKey(event.key.key);
        if (keyCode) {
            const std::optional<ViewportKey> key = viewportKeyFromKeyCode(*keyCode);
            if (event.key.down) {
                if (key) {
                    instance.keysDown.insert(*key);
                }
                if (!event.key.repeat) {
                    if (key) {
                        instance.pressedKeys.push_back(*key);
                    }
                    instance.pressedInputChords.push_back(keyboardInputChord(*keyCode, currentModifiers()));
                }
            } else {
                if (key) {
                    instance.keysDown.erase(*key);
                }
            }
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
    for (const std::unique_ptr<ViewportWindowInstance>& viewport : windowedViewports_) {
        ViewportWindowInstance& instance = *viewport;
        syncInputRouterFromEditorState(instance.inputRouter, editor, modeling);
        instance.inputRouter.setGridPreset(workspace.gridPreset());
        ViewportInputFrame input = buildWindowedViewportInputFrame(instance);
        resolveKeybindingsForFrame(editor, instance.inputRouter.state(), input);
        syncInputRouterFromEditorState(instance.inputRouter, editor, modeling);
        const ViewportInputResult result = instance.inputRouter.handleFrame(input, instance.camera);
        if (result.toolChanged) {
            activateViewportToolFromRouter(editor, document, instance.inputRouter);
        }
        if (result.gridPresetChanged) {
            workspace.setGridPreset(instance.inputRouter.state().gridPreset);
        }
        if (result.focusDocumentRequested) {
            focusDocument(document, assets, instance.camera);
        }
        if (result.maximizedToggleRequested) {
            (void)workspace.toggleFocusedPaneMaximized();
        }
        if (result.cancelRequested) {
            (void)instance.interactions.cancelActiveDrag(editor);
            logCommandFailure("Cancel active tool", editor.tools().cancelModalTool());
            instance.inputRouter.clearModalToolState();
        }
        const ViewportShortcutController shortcutController;
        (void)shortcutController.handleModelingShortcut(result, editor);
        const float scaleX = instance.windowWidth > 0
                ? static_cast<float>(instance.framebufferWidth) / static_cast<float>(instance.windowWidth)
                : 1.0f;
        const float scaleY = instance.windowHeight > 0
                ? static_cast<float>(instance.framebufferHeight) / static_cast<float>(instance.windowHeight)
                : 1.0f;
        const QVec2 leftClickLocal = {
                instance.leftClickWindow.x * scaleX,
                instance.leftClickWindow.y * scaleY,
        };
        ViewportInteractionFrame interactionFrame{
                .inputRouter = instance.inputRouter,
                .camera = instance.camera,
                .settings = settings_,
                .input = input,
                .inputResult = result,
                .leftClickLocal = leftClickLocal,
                .leftClickCount = instance.leftClickCount,
                .leftPressed = instance.leftPressed,
                .leftDown = instance.leftDown,
                .leftReleased = instance.leftReleased,
        };
        const ViewportBeforeToolInteractionResult beforeTool =
                instance.interactions.handleBeforeActiveTool(editor, interactionFrame);
        const ViewportToolResult toolResult = dispatchActiveViewportToolFrame(
                editor,
                modeling,
                document,
                instance.inputRouter,
                instance.camera,
                input,
                result,
                settings_,
                defaultMaterial,
                instance.leftPressed,
                instance.leftDown,
                instance.leftReleased,
                beforeTool.gizmoConsumed,
                interactionFrame.leftClickLocal);
        instance.interactions.handleAfterActiveTool(editor, interactionFrame, toolResult, beforeTool);
        setWindowedViewportCursorCaptured(instance, result.flyCursorCaptureActive);
        clearWindowedViewportTransientInput(instance);
    }
}

void ViewportHost::prepareWindowedViewports(ViewportWorkspaceService& workspace) {
    for (const std::unique_ptr<ViewportWindowInstance>& viewport : windowedViewports_) {
        ViewportWindowInstance& instance = *viewport;
        instance.window.refreshPixelSize();
        instance.framebufferWidth = instance.window.widthPixels();
        instance.framebufferHeight = instance.window.heightPixels();
        instance.renderer.resize(instance.framebufferWidth, instance.framebufferHeight);
        instance.renderer.updateCamera(instance.camera);
        instance.renderer.updateGroundGrid(
                instance.camera,
                viewportGridWorldSizeForPreset(workspace.gridPreset()),
                settings_,
                true);
        instance.renderer.setSceneVisible(true);
        instance.renderer.setOverlayVisible(false);
    }
}

ViewportInputFrame ViewportHost::buildWindowedViewportInputFrame(const ViewportWindowInstance& instance) const {
    const float scaleX = instance.windowWidth > 0
            ? static_cast<float>(instance.framebufferWidth) / static_cast<float>(instance.windowWidth)
            : 1.0f;
    const float scaleY = instance.windowHeight > 0
            ? static_cast<float>(instance.framebufferHeight) / static_cast<float>(instance.windowHeight)
            : 1.0f;
    const QVec2 mousePixel = {
            instance.mouseWindow.x * scaleX,
            instance.mouseWindow.y * scaleY,
    };
    const QVec2 mouseDelta = instance.relativeMouseCaptured
            ? instance.relativeMouseDelta
            : QVec2{instance.mouseDeltaWindow.x * scaleX, instance.mouseDeltaWindow.y * scaleY};
    const bool hovered = mousePixel.x >= 0.0f &&
            mousePixel.y >= 0.0f &&
            mousePixel.x <= static_cast<float>(instance.framebufferWidth) &&
            mousePixel.y <= static_cast<float>(instance.framebufferHeight);
    ViewportInputFrame input;
    input.viewportHovered = hovered || instance.inputRouter.navigating();
    input.viewportFocused = true;
    input.textInputActive = false;
    input.navigationActive = instance.inputRouter.navigating();
    input.flyNavigationActive = instance.inputRouter.flyNavigating();
    input.orbitNavigationActive = instance.inputRouter.orbitNavigating();
    input.panNavigationActive = instance.inputRouter.panNavigating();
    input.modalToolActive = instance.inputRouter.hasActiveModalPreview();
    input.activeToolOwnsViewport = instance.inputRouter.activeToolOwnsViewport();
    input.middlePressed = instance.middlePressed && input.viewportHovered;
    input.middleDown = instance.middleDown;
    input.middleReleased = instance.middleReleased;
    input.rightPressed = instance.rightPressed && input.viewportHovered;
    input.rightDown = instance.rightDown;
    input.rightReleased = instance.rightReleased;
    input.mouseWheel = input.viewportHovered ? instance.wheelY : 0.0f;
    input.deltaSeconds = static_cast<float>(std::max(instance.frameTiming.deltaSeconds, 0.0));
    input.mouseScreen = mousePixel;
    input.mouseLocal = mousePixel;
    input.mouseDelta = mouseDelta;
    input.viewportSize = {
            static_cast<float>(instance.framebufferWidth),
            static_cast<float>(instance.framebufferHeight),
    };
    const ViewportModifiers modifiers = currentModifiers();
    input.modifiers = modifiers;
    input.pressedInputChords = instance.pressedInputChords;
    input.releasedInputChords = instance.releasedInputChords;
    appendActiveMouseDragChords(instance.mouseDragChordsDown, input.activeInputChords);
    appendWheelChords(input.mouseWheel, modifiers, input.wheelInputChords);
    if (instance.keysDown.contains(ViewportKey::W)) {
        input.flyLocalDirection.z += 1.0f;
    }
    if (instance.keysDown.contains(ViewportKey::S)) {
        input.flyLocalDirection.z -= 1.0f;
    }
    if (instance.keysDown.contains(ViewportKey::D)) {
        input.flyLocalDirection.x += 1.0f;
    }
    if (instance.keysDown.contains(ViewportKey::A)) {
        input.flyLocalDirection.x -= 1.0f;
    }
    input.pressedKeys = instance.pressedKeys;
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
    instance.relativeMouseDelta = {};
}

void ViewportHost::clearWindowedViewportTransientInput(ViewportWindowInstance& instance) {
    instance.middlePressed = false;
    instance.middleReleased = false;
    instance.leftPressed = false;
    instance.leftReleased = false;
    instance.rightPressed = false;
    instance.rightReleased = false;
    instance.wheelY = 0.0f;
    instance.mouseDeltaWindow = {};
    instance.pressedKeys.clear();
    instance.pressedInputChords.clear();
    instance.activeInputChords.clear();
    instance.releasedInputChords.clear();
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
