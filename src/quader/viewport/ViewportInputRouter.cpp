#include "quader/viewport/ViewportInputRouter.h"

#include "quader/viewport/HostViewportContractAdapter.h"

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

ViewportShortcut commandShortcut(ViewportShortcutKind kind, ViewportShortcutContext context) {
    return {
        .kind = kind,
        .context = context,
    };
}

ViewportShortcut toolShortcut(ViewportTool tool) {
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

ViewportInputRouter::ViewportInputRouter() {
    state_.gridPreset = viewportDefaultGridPreset();
}

ViewportInputResult ViewportInputRouter::handleFrame(const ViewportInputFrame& input, ViewportCameraController& camera) {
    ViewportInputResult result;
    camera.setViewportSize(input.viewportSize.x, input.viewportSize.y);

    const bool acceptsMouse = input.viewportHovered || navigating();
    const bool hasFlyMovementInput =
            std::abs(input.flyLocalDirection.x) > 0.000001f ||
            std::abs(input.flyLocalDirection.y) > 0.000001f ||
            std::abs(input.flyLocalDirection.z) > 0.000001f;
    bool flyStartedThisFrame = false;
    if (acceptsMouse &&
            input.rightPressed &&
            !input.modifiers.primary &&
            !input.modifiers.shift &&
            !input.modifiers.alt) {
        navigationMode_ = camera.state().projection == ViewportProjection::Orthographic
                ? ViewportDragMode::Pan
                : ViewportDragMode::Fly;
        navigationUsesRightButton_ = true;
        flyCursorCaptureActive_ = navigationMode_ == ViewportDragMode::Fly;
        flyHudActive_ = false;
        camera.beginDrag(input.mouseScreen, navigationMode_);
        flyStartedThisFrame = navigationMode_ == ViewportDragMode::Fly;
        result.consumed = true;
    }

    if (navigationMode_ == ViewportDragMode::Fly) {
        if (input.middlePressed) {
            camera.resetFlySpeed();
            result.consumed = true;
            result.flySpeedChanged = true;
            result.flySpeedReset = true;
            result.flySpeed = camera.flySpeed();
        }

        if (input.mouseWheel != 0.0f) {
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

        if (input.rightDown && !cancelFly && !flyStartedThisFrame) {
            if (hasFlyMovementInput) {
                flyHudActive_ = true;
            }
            camera.updateDragDelta(input.mouseDelta);
            camera.flyMove(input.flyLocalDirection, input.deltaSeconds);
            result.consumed = true;
            result.cameraChanged = true;
        }

        if (input.rightReleased || !input.rightDown || cancelFly) {
            navigationMode_ = ViewportDragMode::None;
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

    if (acceptsMouse && input.middlePressed) {
        navigationMode_ = input.modifiers.shift
                ? ViewportDragMode::Pan
                : (input.modifiers.alt ? ViewportDragMode::OrbitSnap : ViewportDragMode::Orbit);
        navigationUsesRightButton_ = false;
        camera.beginDrag(input.mouseScreen, navigationMode_);
        result.consumed = true;
    }

    if (navigating()) {
        const bool dragDown = navigationUsesRightButton_ ? input.rightDown : input.middleDown;
        const bool dragReleased = navigationUsesRightButton_ ? input.rightReleased : input.middleReleased;
        if (dragDown) {
            const ViewportDragMode activeNavigationMode =
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
            navigationUsesRightButton_ = false;
            flyCursorCaptureActive_ = false;
            flyHudActive_ = false;
            camera.endDrag();
            result.consumed = true;
        }
    }

    if (input.viewportHovered && input.mouseWheel != 0.0f) {
        if (input.modifiers.primary) {
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
    navigationUsesRightButton_ = false;
    flyCursorCaptureActive_ = false;
    flyHudActive_ = false;
}

void ViewportInputRouter::setActiveTool(ViewportTool tool) {
    if (!viewportToolHasNativeController(tool)) {
        return;
    }
    state_.activeTool = tool;
}

void ViewportInputRouter::setSelectionMode(ViewportSelectionMode mode) {
    state_.selectionMode = mode;
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
    if (key == ViewportKey::F && unmodified(modifiers)) {
        shortcut.kind = ViewportShortcutKind::FocusDocument;
        return true;
    }

    if (hasPrimaryOrAlt(modifiers)) {
        return false;
    }

    if (modifiers.shift) {
        if (key == ViewportKey::R) {
            shortcut = toolShortcut(ViewportTool::InsertEdgeLoop);
            return true;
        }
        if (key == ViewportKey::C) {
            shortcut = toolShortcut(ViewportTool::Cut);
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
        shortcut = toolShortcut(ViewportTool::Select);
        return true;
    case ViewportKey::W:
        shortcut = toolShortcut(ViewportTool::Move);
        return true;
    case ViewportKey::R:
        shortcut = toolShortcut(ViewportTool::Rotate);
        return true;
    case ViewportKey::S:
        shortcut = toolShortcut(ViewportTool::Scale);
        return true;
    case ViewportKey::B:
        shortcut = toolShortcut(ViewportTool::Box);
        return true;
    case ViewportKey::C:
        shortcut = toolShortcut(ViewportTool::Knife);
        return true;
    case ViewportKey::M:
        shortcut = commandShortcut(ViewportShortcutKind::MergeActive, ViewportShortcutContext::Viewport);
        return true;
    case ViewportKey::P:
        shortcut = toolShortcut(ViewportTool::Poly);
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
    switch (shortcut.kind) {
    case ViewportShortcutKind::ActivateTool:
        if (!viewportToolHasNativeController(shortcut.tool)) {
            return false;
        }
        result.toolChanged = state_.activeTool != shortcut.tool;
        state_.activeTool = shortcut.tool;
        return true;
    case ViewportShortcutKind::SetSelectionMode:
        result.selectionModeChanged = state_.selectionMode != shortcut.selectionMode;
        state_.selectionMode = shortcut.selectionMode;
        return true;
    case ViewportShortcutKind::FocusDocument:
        result.focusDocumentRequested = true;
        result.cameraChanged = true;
        return true;
    case ViewportShortcutKind::FocusOrigin:
        camera.focusOrigin();
        result.cameraChanged = true;
        return true;
    case ViewportShortcutKind::ToggleMaximized:
        state_.maximized = !state_.maximized;
        return true;
    case ViewportShortcutKind::SetGridPreset:
        result.gridPresetChanged = state_.gridPreset != shortcut.gridPreset;
        state_.gridPreset = shortcut.gridPreset;
        return true;
    case ViewportShortcutKind::CancelActiveTool:
        result.cancelRequested = true;
        return true;
    case ViewportShortcutKind::DeleteSelection:
    case ViewportShortcutKind::SelectAll:
    case ViewportShortcutKind::InvertSelection:
    case ViewportShortcutKind::Copy:
    case ViewportShortcutKind::Paste:
    case ViewportShortcutKind::Duplicate:
    case ViewportShortcutKind::Undo:
    case ViewportShortcutKind::Redo:
    case ViewportShortcutKind::RepeatLastAction:
    case ViewportShortcutKind::MergeActive:
    case ViewportShortcutKind::MergeCenter:
    case ViewportShortcutKind::MirrorAxis:
        return true;
    case ViewportShortcutKind::FlipHorizontal:
    case ViewportShortcutKind::FlipVertical:
        return state_.selectionMode == ViewportSelectionMode::Object;
    case ViewportShortcutKind::ApplyMaterial:
        return true;
    case ViewportShortcutKind::None:
        return false;
    }
    return false;
}

const char* viewportToolId(ViewportTool tool) {
    switch (tool) {
    case ViewportTool::Select:
        return "select";
    case ViewportTool::Move:
        return "move";
    case ViewportTool::Rotate:
        return "rotate";
    case ViewportTool::Scale:
        return "scale";
    case ViewportTool::Box:
        return "box";
    case ViewportTool::InsertEdgeLoop:
        return "insert_edge_loop";
    case ViewportTool::Knife:
        return "knife";
    case ViewportTool::Cut:
        return "cut";
    case ViewportTool::Poly:
        return "poly";
    case ViewportTool::Mirror:
        return "mirror";
    }
    return "unknown";
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
    }
    return "unknown";
}

float viewportGridWorldSizeForPreset(int preset) {
    return viewportGridWorldSizeFromContract(preset);
}

} // namespace quader
