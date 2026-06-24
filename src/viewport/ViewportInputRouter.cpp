////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "viewport/ViewportInputRouter.h"

#include "viewport/HostViewportContractAdapter.h"
#include "editor/tools/ViewportToolCatalog.h"
#include "editor/tools/ViewportToolRuntime.h"

#include <algorithm>
#include <cmath>

namespace quader {
namespace {

bool hasPrimaryOrAlt(ViewportModifiers modifiers) {
    return modifiers.primary || modifiers.alt;
}

bool unmodified(ViewportModifiers modifiers) {
    return !modifiers.primary && !modifiers.alt && !modifiers.shift;
}

bool shiftOnly(ViewportModifiers modifiers) {
    return !modifiers.primary && !modifiers.alt && modifiers.shift;
}

bool primaryOnly(ViewportModifiers modifiers) {
    return modifiers.primary && !modifiers.alt && !modifiers.shift;
}

bool primaryShiftOnly(ViewportModifiers modifiers) {
    return modifiers.primary && !modifiers.alt && modifiers.shift;
}

bool usesResolvedInputBindings(const ViewportInputFrame& input) {
    return input.shortcutResolverActive;
}

ViewportInputActionKind pressedNavigationAction(const ViewportInputFrame& input) {
    if (usesResolvedInputBindings(input)) {
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

    if (input.rightPressed &&
            !input.modifiers.primary &&
            !input.modifiers.shift &&
            !input.modifiers.alt) {
        return ViewportInputActionKind::Fly;
    }
    if (!input.middlePressed) {
        return ViewportInputActionKind::None;
    }
    if (input.modifiers.shift) {
        return ViewportInputActionKind::Pan;
    }
    if (input.modifiers.alt) {
        return ViewportInputActionKind::OrbitSnap;
    }
    return ViewportInputActionKind::Orbit;
}

bool navigationActionDown(const ViewportInputFrame& input, ViewportInputActionKind action, bool legacyRightButton) {
    if (!usesResolvedInputBindings(input)) {
        return legacyRightButton ? input.rightDown : input.middleDown;
    }

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

bool navigationActionReleased(const ViewportInputFrame& input, ViewportInputActionKind action, bool legacyRightButton) {
    if (!usesResolvedInputBindings(input)) {
        return legacyRightButton ? input.rightReleased : input.middleReleased;
    }

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

ViewportShortcut commandShortcut(ViewportShortcutKind kind, ViewportShortcutContext context) {
    return {
        .kind = kind,
        .context = context,
    };
}

ViewportShortcut toolShortcut(ViewportToolId tool) {
    return {
        .kind = ViewportShortcutKind::ActivateTool,
        .context = ViewportShortcutContext::Viewport,
        .tool = tool,
    };
}

ViewportShortcut selectionShortcut(ViewportSelectionMode mode) {
    return {
        .kind = ViewportShortcutKind::SetSelectionMode,
        .context = ViewportShortcutContext::Viewport,
        .selectionMode = mode,
    };
}

ViewportShortcut gridShortcut(int preset) {
    return {
        .kind = ViewportShortcutKind::SetGridPreset,
        .context = ViewportShortcutContext::Viewport,
        .gridPreset = preset,
    };
}

ViewportShortcut mirrorShortcut(ViewportMirrorAxis axis) {
    return {
        .kind = ViewportShortcutKind::MirrorAxis,
        .context = ViewportShortcutContext::Viewport,
        .mirrorAxis = axis,
    };
}

} // namespace

ViewportInputRouter::ViewportInputRouter()
    : toolRuntime_(std::make_unique<ViewportToolRuntime>()) {
    state_.gridPreset = viewportDefaultGridPreset();
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
        navigationUsesRightButton_ = !usesResolvedInputBindings(input) && pressedAction == ViewportInputActionKind::Fly;
        flyCursorCaptureActive_ = navigationMode_ == ViewportDragMode::Fly;
        flyHudActive_ = false;
        camera.beginDrag(input.mouseScreen, navigationMode_);
        flyStartedThisFrame = navigationMode_ == ViewportDragMode::Fly;
        result.consumed = true;
    }

    if (navigationMode_ == ViewportDragMode::Fly) {
        const bool resetFlySpeedPressed = usesResolvedInputBindings(input)
                ? input.navigationBindings.resetFlySpeedPressed
                : input.middlePressed;
        if (resetFlySpeedPressed) {
            camera.resetFlySpeed();
            result.consumed = true;
            result.flySpeedChanged = true;
            result.flySpeedReset = true;
            result.flySpeed = camera.flySpeed();
        }

        const bool flySpeedWheelActive = !usesResolvedInputBindings(input) ||
                input.navigationBindings.zoomWheel ||
                input.navigationBindings.zoomToCursorWheel;
        if (input.mouseWheel != 0.0f && flySpeedWheelActive) {
            camera.adjustFlySpeed(input.mouseWheel);
            result.consumed = true;
            result.flySpeedChanged = true;
            result.flySpeed = camera.flySpeed();
        }

        bool cancelFly = false;
        for (ViewportKey key : input.pressedKeys) {
            if (key == ViewportKey::Escape) {
                cancelFly = true;
                break;
            }
        }

        const bool flyDown = navigationActionDown(input, activeNavigationAction_, navigationUsesRightButton_);
        if (flyDown && !cancelFly && !flyStartedThisFrame) {
            if (hasFlyMovementInput) {
                flyHudActive_ = true;
            }
            camera.updateDragDelta(input.mouseDelta);
            camera.flyMove(input.flyLocalDirection, input.deltaSeconds);
            result.consumed = true;
            result.cameraChanged = true;
        }

        if (navigationActionReleased(input, activeNavigationAction_, navigationUsesRightButton_) || !flyDown || cancelFly) {
            navigationMode_ = ViewportDragMode::None;
            activeNavigationAction_ = ViewportInputActionKind::None;
            navigationUsesRightButton_ = false;
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
        const bool dragDown = navigationActionDown(input, activeNavigationAction_, navigationUsesRightButton_);
        const bool dragReleased = navigationActionReleased(input, activeNavigationAction_, navigationUsesRightButton_);
        if (dragDown) {
            const ViewportDragMode activeNavigationMode =
                    !usesResolvedInputBindings(input) &&
                    !navigationUsesRightButton_ &&
                            (navigationMode_ == ViewportDragMode::Orbit || navigationMode_ == ViewportDragMode::OrbitSnap)
                    ? (input.modifiers.alt ? ViewportDragMode::OrbitSnap : ViewportDragMode::Orbit)
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
            navigationUsesRightButton_ = false;
            flyCursorCaptureActive_ = false;
            flyHudActive_ = false;
            camera.endDrag();
            result.consumed = true;
        }
    }

    const bool wheelZoomActive = usesResolvedInputBindings(input)
            ? input.navigationBindings.zoomWheel || input.navigationBindings.zoomToCursorWheel
            : true;
    if (input.viewportHovered && input.mouseWheel != 0.0f && wheelZoomActive) {
        if (usesResolvedInputBindings(input) ? input.navigationBindings.zoomToCursorWheel : input.modifiers.primary) {
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

    if (acceptsShortcuts && !result.shortcutHandled && !input.shortcutResolverActive) {
        for (ViewportKey key : input.pressedKeys) {
            ViewportShortcut shortcut;
            if (!resolveShortcut(key, input.modifiers, shortcut)) {
                continue;
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
    navigationUsesRightButton_ = false;
    flyCursorCaptureActive_ = false;
    flyHudActive_ = false;
}

void ViewportInputRouter::setActiveTool(ViewportToolId tool) {
    (void)toolRuntime_->setActiveTool(state_, tool);
}

void ViewportInputRouter::setSelectionMode(ViewportSelectionMode mode) {
    state_.selectionMode = mode;
}

void ViewportInputRouter::setGridPreset(int preset) {
    state_.gridPreset = preset;
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

bool ViewportInputRouter::resolveShortcut(ViewportKey key, ViewportModifiers modifiers, ViewportShortcut& shortcut) {
    if (resolveViewportShortcutFromHostContract(key, modifiers, shortcut)) {
        return true;
    }

    if (primaryOnly(modifiers)) {
        switch (key) {
        case ViewportKey::A:
            shortcut = commandShortcut(ViewportShortcutKind::SelectAll, ViewportShortcutContext::Global);
            return true;
        case ViewportKey::C:
            shortcut = commandShortcut(ViewportShortcutKind::Copy, ViewportShortcutContext::Global);
            return true;
        case ViewportKey::D:
            shortcut = commandShortcut(ViewportShortcutKind::Duplicate, ViewportShortcutContext::Global);
            return true;
        case ViewportKey::I:
            shortcut = commandShortcut(ViewportShortcutKind::InvertSelection, ViewportShortcutContext::Global);
            return true;
        case ViewportKey::R:
            shortcut = commandShortcut(ViewportShortcutKind::RepeatLastAction, ViewportShortcutContext::Global);
            return true;
        case ViewportKey::V:
            shortcut = commandShortcut(ViewportShortcutKind::Paste, ViewportShortcutContext::Global);
            return true;
        case ViewportKey::Y:
            shortcut = commandShortcut(ViewportShortcutKind::Redo, ViewportShortcutContext::Global);
            return true;
        case ViewportKey::Z:
            shortcut = commandShortcut(ViewportShortcutKind::Undo, ViewportShortcutContext::Global);
            return true;
        default:
            return false;
        }
    }

    if (primaryShiftOnly(modifiers)) {
        if (key == ViewportKey::Z) {
            shortcut = commandShortcut(ViewportShortcutKind::Redo, ViewportShortcutContext::Global);
            return true;
        }
        return false;
    }

    if (key == ViewportKey::Escape && unmodified(modifiers)) {
        shortcut.kind = ViewportShortcutKind::CancelActiveTool;
        return true;
    }
    if ((key == ViewportKey::Delete || key == ViewportKey::Backspace) && unmodified(modifiers)) {
        shortcut = commandShortcut(ViewportShortcutKind::DeleteSelection, ViewportShortcutContext::Global);
        return true;
    }
    if (key == ViewportKey::Home && unmodified(modifiers)) {
        shortcut.kind = ViewportShortcutKind::FocusDocument;
        return true;
    }
    if (key == ViewportKey::Home && shiftOnly(modifiers)) {
        shortcut.kind = ViewportShortcutKind::FocusOrigin;
        return true;
    }
    if (key == ViewportKey::Space && unmodified(modifiers)) {
        shortcut.kind = ViewportShortcutKind::ToggleMaximized;
        return true;
    }
    if (key == ViewportKey::X && unmodified(modifiers)) {
        shortcut = commandShortcut(ViewportShortcutKind::DeletePopup, ViewportShortcutContext::Viewport);
        return true;
    }
    if (key == ViewportKey::F && unmodified(modifiers)) {
        shortcut = commandShortcut(ViewportShortcutKind::FlipMeshNormals, ViewportShortcutContext::Viewport);
        return true;
    }

    if (hasPrimaryOrAlt(modifiers)) {
        return false;
    }

    if (modifiers.shift) {
        if (key == ViewportKey::R) {
            shortcut = toolShortcut(ViewportToolId::InsertEdgeLoop);
            return true;
        }
        if (key == ViewportKey::C) {
            shortcut = toolShortcut(ViewportToolId::Cut);
            return true;
        }
        if (key == ViewportKey::M) {
            shortcut = commandShortcut(ViewportShortcutKind::MergeCenter, ViewportShortcutContext::Viewport);
            return true;
        }
        if (key == ViewportKey::T) {
            shortcut = commandShortcut(ViewportShortcutKind::ApplyMaterial, ViewportShortcutContext::Viewport);
            return true;
        }
        if (key == ViewportKey::X) {
            shortcut = mirrorShortcut(ViewportMirrorAxis::X);
            return true;
        }
        if (key == ViewportKey::Y) {
            shortcut = mirrorShortcut(ViewportMirrorAxis::Y);
            return true;
        }
        if (key == ViewportKey::Z) {
            shortcut = mirrorShortcut(ViewportMirrorAxis::Z);
            return true;
        }
        if (key == ViewportKey::H) {
            shortcut = commandShortcut(ViewportShortcutKind::FlipHorizontal, ViewportShortcutContext::Viewport);
            return true;
        }
        if (key == ViewportKey::V) {
            shortcut = commandShortcut(ViewportShortcutKind::FlipVertical, ViewportShortcutContext::Viewport);
            return true;
        }
        return false;
    }

    switch (key) {
    case ViewportKey::Q:
        shortcut = toolShortcut(ViewportToolId::Select);
        return true;
    case ViewportKey::W:
        shortcut = toolShortcut(ViewportToolId::Move);
        return true;
    case ViewportKey::R:
        shortcut = toolShortcut(ViewportToolId::Rotate);
        return true;
    case ViewportKey::S:
        shortcut = toolShortcut(ViewportToolId::Scale);
        return true;
    case ViewportKey::B:
        shortcut = toolShortcut(ViewportToolId::Box);
        return true;
    case ViewportKey::C:
        shortcut = toolShortcut(ViewportToolId::Knife);
        return true;
    case ViewportKey::M:
        shortcut = commandShortcut(ViewportShortcutKind::MergeActive, ViewportShortcutContext::Viewport);
        return true;
    case ViewportKey::P:
        shortcut = toolShortcut(ViewportToolId::Poly);
        return true;
    case ViewportKey::Number1:
        shortcut = selectionShortcut(ViewportSelectionMode::Vertex);
        return true;
    case ViewportKey::Number2:
        shortcut = selectionShortcut(ViewportSelectionMode::Edge);
        return true;
    case ViewportKey::Number3:
        shortcut = selectionShortcut(ViewportSelectionMode::Face);
        return true;
    case ViewportKey::Number4:
        shortcut = selectionShortcut(ViewportSelectionMode::Object);
        return true;
    case ViewportKey::Number5:
        shortcut = selectionShortcut(ViewportSelectionMode::Group);
        return true;
    case ViewportKey::F1:
        shortcut = gridShortcut(1);
        return true;
    case ViewportKey::F2:
        shortcut = gridShortcut(2);
        return true;
    case ViewportKey::F3:
        shortcut = gridShortcut(3);
        return true;
    case ViewportKey::F4:
        shortcut = gridShortcut(4);
        return true;
    case ViewportKey::F5:
        shortcut = gridShortcut(5);
        return true;
    case ViewportKey::F6:
        shortcut = gridShortcut(6);
        return true;
    case ViewportKey::F7:
        shortcut = gridShortcut(7);
        return true;
    case ViewportKey::F8:
        shortcut = gridShortcut(8);
        return true;
    case ViewportKey::F9:
        shortcut = gridShortcut(9);
        return true;
    case ViewportKey::F10:
        shortcut = gridShortcut(10);
        return true;
    default:
        return false;
    }
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
