////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "viewport/viewport_input_router.hpp"

#include "viewport/host_viewport_contract_adapter.hpp"
#include "editor/tools/viewport_tool_catalog.hpp"
#include "editor/tools/viewport_tool_runtime.hpp"

#include <algorithm>
#include <cmath>

namespace quader {
namespace {

ViewportInputActionKind pressedNavigationAction(const ViewportInputFrame& input) {
    if (input.navigationBindings.flyPressed) {
        return ViewportInputActionKind::Fly;
    }
    if (input.navigationBindings.panPressed) {
        return ViewportInputActionKind::Pan;
    }
    if (input.navigationBindings.orbitSnapPressed) {
        return ViewportInputActionKind::OrbitSnap;
    }
    if (input.navigationBindings.orbitPressed) {
        return ViewportInputActionKind::Orbit;
    }
    return ViewportInputActionKind::None;
}

bool navigationActionDown(const ViewportInputFrame& input, ViewportInputActionKind action) {
    switch (action) {
    case ViewportInputActionKind::Orbit:
        return input.navigationBindings.orbitDown;
    case ViewportInputActionKind::OrbitSnap:
        return input.navigationBindings.orbitSnapDown;
    case ViewportInputActionKind::Pan:
        return input.navigationBindings.panDown;
    case ViewportInputActionKind::Fly:
        return input.navigationBindings.flyDown;
    default:
        return false;
    }
}

bool navigationActionReleased(const ViewportInputFrame& input, ViewportInputActionKind action) {
    switch (action) {
    case ViewportInputActionKind::Orbit:
        return input.navigationBindings.orbitReleased;
    case ViewportInputActionKind::OrbitSnap:
        return input.navigationBindings.orbitSnapReleased;
    case ViewportInputActionKind::Pan:
        return input.navigationBindings.panReleased;
    case ViewportInputActionKind::Fly:
        return input.navigationBindings.flyReleased;
    default:
        return false;
    }
}

bool isOrbitNavigationAction(ViewportInputActionKind action) {
    return action == ViewportInputActionKind::Orbit || action == ViewportInputActionKind::OrbitSnap;
}

ViewportDragMode navigationModeForAction(ViewportInputActionKind action, const ViewportCameraController& camera) {
    switch (action) {
    case ViewportInputActionKind::Orbit:
        return ViewportDragMode::Orbit;
    case ViewportInputActionKind::OrbitSnap:
        return ViewportDragMode::OrbitSnap;
    case ViewportInputActionKind::Pan:
        return ViewportDragMode::Pan;
    case ViewportInputActionKind::Fly:
        return camera.state().projection == ViewportProjection::Orthographic
                ? ViewportDragMode::Pan
                : ViewportDragMode::Fly;
    default:
        return ViewportDragMode::None;
    }
}

} // namespace

ViewportInputRouter::ViewportInputRouter()
    : toolRuntime_(std::make_unique<ViewportToolRuntime>()) {
    syncGridPresetSnapshot(viewportDefaultGridPreset());
    toolRuntime_->syncActiveTool(state_.activeTool);
}

ViewportInputRouter::~ViewportInputRouter() = default;

ViewportInputResult ViewportInputRouter::handleFrame(const ViewportInputFrame& input, ViewportCameraController& camera) {
    ViewportInputResult result;
    camera.setViewportSize(input.viewportSize.x, input.viewportSize.y);

    const bool acceptsMouse = input.viewportHovered || navigating();
    const bool hasFlyMovementInput =
            std::abs(input.flyLocalDirection.x) > 0.000001f ||
            std::abs(input.flyLocalDirection.y) > 0.000001f ||
            std::abs(input.flyLocalDirection.z) > 0.000001f;
    bool flyStartedThisFrame = false;
    const ViewportInputActionKind pressedAction = acceptsMouse && !navigating()
            ? pressedNavigationAction(input)
            : ViewportInputActionKind::None;
    if (pressedAction != ViewportInputActionKind::None) {
        activeNavigationAction_ = pressedAction;
        navigationMode_ = navigationModeForAction(pressedAction, camera);
        orbitSnapActionUsesAltModifier_ = pressedAction == ViewportInputActionKind::OrbitSnap && input.modifiers.alt;
        flyCursorCaptureActive_ = navigationMode_ == ViewportDragMode::Fly;
        flyHudActive_ = false;
        camera.beginDrag(input.mouseScreen, navigationMode_);
        flyStartedThisFrame = navigationMode_ == ViewportDragMode::Fly;
        result.consumed = true;
    }

    if (navigationMode_ == ViewportDragMode::Fly) {
        if (input.navigationBindings.resetFlySpeedPressed) {
            camera.resetFlySpeed();
            result.consumed = true;
            result.flySpeedChanged = true;
            result.flySpeedReset = true;
            result.flySpeed = camera.flySpeed();
        }

        const bool flySpeedWheelActive = input.navigationBindings.zoomWheel ||
                input.navigationBindings.zoomToCursorWheel;
        if (input.mouseWheel != 0.0f && flySpeedWheelActive) {
            camera.adjustFlySpeed(input.mouseWheel);
            result.consumed = true;
            result.flySpeedChanged = true;
            result.flySpeed = camera.flySpeed();
        }

        const bool cancelFly = input.navigationBindings.cancelNavigationPressed;
        const bool flyDown = navigationActionDown(input, activeNavigationAction_);
        if (flyDown && !cancelFly && !flyStartedThisFrame) {
            if (hasFlyMovementInput) {
                flyHudActive_ = true;
            }
            camera.updateDragDelta(input.mouseDelta);
            camera.flyMove(input.flyLocalDirection, input.deltaSeconds);
            result.consumed = true;
            result.cameraChanged = true;
        }

        if (navigationActionReleased(input, activeNavigationAction_) || !flyDown || cancelFly) {
            navigationMode_ = ViewportDragMode::None;
            activeNavigationAction_ = ViewportInputActionKind::None;
            orbitSnapActionUsesAltModifier_ = false;
            flyCursorCaptureActive_ = false;
            flyHudActive_ = false;
            camera.endDrag();
            result.consumed = true;
        }

        result.navigationActive = navigating();
        result.flyNavigationActive = flyNavigating();
        result.flyCursorCaptureActive = flyCursorCaptureActive();
        result.flyHudActive = flyHudActive();
        return result;
    }

    if (navigating()) {
        const bool dragDown = navigationActionDown(input, activeNavigationAction_);
        const bool dragReleased = navigationActionReleased(input, activeNavigationAction_) ||
                input.navigationBindings.cancelNavigationPressed;
        if (dragDown) {
            const ViewportDragMode activeNavigationMode =
                    isOrbitNavigationAction(activeNavigationAction_)
                    ? (input.modifiers.alt ||
                                    (activeNavigationAction_ == ViewportInputActionKind::OrbitSnap &&
                                            !orbitSnapActionUsesAltModifier_)
                            ? ViewportDragMode::OrbitSnap
                            : ViewportDragMode::Orbit)
                    : navigationMode_;
            const bool lockedOrthographicOrbit =
                    camera.orthographicViewLocked() &&
                    (activeNavigationMode == ViewportDragMode::Orbit || activeNavigationMode == ViewportDragMode::OrbitSnap);
            if (!lockedOrthographicOrbit) {
                camera.updateDrag(input.mouseScreen, activeNavigationMode);
                result.cameraChanged = true;
            }
            navigationMode_ = activeNavigationMode;
            result.consumed = true;
        }
        if (dragReleased || !dragDown) {
            navigationMode_ = ViewportDragMode::None;
            activeNavigationAction_ = ViewportInputActionKind::None;
            orbitSnapActionUsesAltModifier_ = false;
            flyCursorCaptureActive_ = false;
            flyHudActive_ = false;
            camera.endDrag();
            result.consumed = true;
        }
    }

    const bool wheelZoomActive = input.navigationBindings.zoomWheel || input.navigationBindings.zoomToCursorWheel;
    if (input.viewportHovered && input.mouseWheel != 0.0f && wheelZoomActive) {
        if (input.navigationBindings.zoomToCursorWheel) {
            camera.scrollToCursor(input.mouseWheel, input.mouseLocal);
            result.zoomedToCursor = true;
        } else {
            camera.scroll(input.mouseWheel);
        }
        result.consumed = true;
        result.cameraChanged = true;
    }

    const bool acceptsShortcuts = input.viewportFocused && !input.textInputActive && !navigating();
    if (acceptsShortcuts) {
        for (const ViewportShortcut& shortcut : input.resolvedShortcuts) {
            if (shortcut.kind == ViewportShortcutKind::None) {
                result.consumed = true;
                result.shortcutHandled = true;
                result.shortcut = shortcut;
                break;
            }
            if (handleShortcut(shortcut, camera, result)) {
                result.consumed = true;
                result.shortcutHandled = true;
                result.shortcut = shortcut;
                break;
            }
        }
    }

    result.navigationActive = navigating();
    result.flyNavigationActive = flyNavigating();
    result.flyCursorCaptureActive = flyCursorCaptureActive();
    result.flyHudActive = flyHudActive();
    return result;
}

void ViewportInputRouter::reset() {
    navigationMode_ = ViewportDragMode::None;
    activeNavigationAction_ = ViewportInputActionKind::None;
    orbitSnapActionUsesAltModifier_ = false;
    flyCursorCaptureActive_ = false;
    flyHudActive_ = false;
}

void ViewportInputRouter::setActiveTool(ViewportToolId tool) {
    (void)toolRuntime_->setActiveTool(state_, tool);
}

void ViewportInputRouter::setSelectionMode(ViewportSelectionMode mode) {
    state_.selectionMode = mode;
}

void ViewportInputRouter::syncGridPresetSnapshot(int preset) {
    state_.gridPreset = viewportClampGridPreset(preset);
}

bool ViewportInputRouter::activeToolOwnsViewport() const {
    return toolRuntime_ != nullptr && toolRuntime_->activeToolOwnsViewport();
}

bool ViewportInputRouter::activeToolBlocksViewportFocusShortcuts() const {
    return toolRuntime_ != nullptr && toolRuntime_->activeToolBlocksViewportFocusShortcuts();
}

bool ViewportInputRouter::activeToolAllowsSelectionFallback() const {
    return toolRuntime_ != nullptr && toolRuntime_->toolAllowsSelectionFallback(state_.activeTool);
}

bool ViewportInputRouter::hasActiveModalPreview() const {
    return toolRuntime_ != nullptr && toolRuntime_->hasActiveModalPreview();
}

const ModelingBoxPreview& ViewportInputRouter::boxPreview() const {
    static const ModelingBoxPreview emptyPreview;
    return toolRuntime_ != nullptr ? toolRuntime_->boxPreview() : emptyPreview;
}

void ViewportInputRouter::clearModalToolState() {
    if (toolRuntime_ != nullptr) {
        toolRuntime_->clearModalToolState();
    }
}

void ViewportInputRouter::clearModalDragState() {
    if (toolRuntime_ != nullptr) {
        toolRuntime_->clearModalDragState();
    }
}

const ViewportTool* ViewportInputRouter::activeTool() const {
    return toolRuntime_ != nullptr ? toolRuntime_->activeTool() : findViewportTool(state_.activeTool);
}

bool ViewportInputRouter::handleShortcut(const ViewportShortcut& shortcut, ViewportCameraController& camera, ViewportInputResult& result) {
    return toolRuntime_ != nullptr && toolRuntime_->handleShortcut(shortcut, state_, camera, result);
}

ViewportToolResult ViewportInputRouter::handleActiveToolEvent(
        ViewportToolContext& context,
        const ViewportToolEvent& event) {
    return toolRuntime_ != nullptr ? toolRuntime_->handleActiveToolEvent(state_, context, event) : ViewportToolResult{};
}

const char* viewportToolId(ViewportToolId tool) {
    return viewportToolCatalogId(tool).data();
}

const char* viewportSelectionModeId(ViewportSelectionMode mode) {
    switch (mode) {
    case ViewportSelectionMode::Vertex:
        return "vertex";
    case ViewportSelectionMode::Edge:
        return "edge";
    case ViewportSelectionMode::Face:
        return "face";
    case ViewportSelectionMode::Object:
        return "object";
    case ViewportSelectionMode::Group:
        return "group";
    }
    return "unknown";
}

float viewportGridWorldSizeForPreset(int preset) {
    return viewportGridWorldSizeFromContract(preset);
}

} // namespace quader
