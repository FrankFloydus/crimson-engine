////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "viewport/modes/ViewportToolRuntime.h"

#include "modeling/services/ModelingSessionService.h"
#include "viewport/HostViewportContractAdapter.h"
#include "viewport/modes/ViewportToolCatalog.h"

namespace quader {

std::span<const ViewportTool* const> ViewportToolRegistry::tools() const {
    return viewportToolCatalog();
}

const ViewportTool* ViewportToolRegistry::find(ViewportToolId tool) const {
    return findViewportTool(tool);
}

const ViewportTool* ViewportToolRegistry::find(std::string_view id) const {
    return findViewportTool(id);
}

bool ViewportToolRegistry::available(ViewportToolId tool) const {
    const ViewportTool* item = find(tool);
    return item != nullptr && viewportToolHasNativeController(item->viewportTool());
}

bool ViewportToolHost::available(const ViewportTool& tool) const {
    return viewportToolHasNativeController(tool.viewportTool());
}

bool ViewportToolHost::ownsViewport(const ViewportTool& tool) const {
    return tool.ownsViewport();
}

bool ViewportToolHost::blocksViewportFocusShortcuts(const ViewportTool& tool) const {
    return tool.blocksViewportFocusShortcuts();
}

bool ViewportToolHost::allowsSelectionFallback(const ViewportTool& tool) const {
    return tool.allowsSelectionFallback();
}

ModelingCommandResult ViewportToolHost::activateTool(
        const ViewportTool& tool,
        ModelingSessionService& modeling) const {
    return tool.activate(modeling);
}

ViewportToolEvent ViewportToolEventBuilder::activationEvent(
        ViewportToolId tool,
        const ViewportShortcut* shortcut) const {
    ViewportToolEvent event;
    event.tool = tool;
    if (shortcut != nullptr) {
        event.shortcut = *shortcut;
        event.fromShortcut = true;
    }
    return event;
}

bool ViewportToolController::activateTool(
        const ViewportToolRegistry& registry,
        const ViewportToolHost& host,
        ViewportToolState& state,
        ViewportToolId tool,
        const ViewportTool*& activeTool,
        ViewportInputResult* result) const {
    const ViewportTool* nextTool = registry.find(tool);
    if (nextTool == nullptr || !host.available(*nextTool)) {
        return false;
    }

    const bool changed = state.activeTool != tool;
    state.activeTool = tool;
    activeTool = nextTool;
    if (result != nullptr) {
        result->toolChanged = changed;
    }
    return true;
}

bool ViewportModalToolController::handles(const ViewportTool& tool) const {
    return dynamic_cast<const ModalViewportTool*>(&tool) != nullptr;
}

bool ViewportModalToolController::ownsViewport(
        const ViewportToolHost& host,
        const ViewportTool& tool) const {
    return handles(tool) && host.ownsViewport(tool);
}

bool ViewportModalToolController::blocksViewportFocusShortcuts(
        const ViewportToolHost& host,
        const ViewportTool& tool) const {
    return handles(tool) && host.blocksViewportFocusShortcuts(tool);
}

bool ViewportTransformToolController::handles(const ViewportTool& tool) const {
    return dynamic_cast<const TransformViewportTool*>(&tool) != nullptr;
}

ModelingTransformTool ViewportTransformToolController::transformTool(const ViewportTool& tool) const {
    return handles(tool) ? tool.transformTool() : ModelingTransformTool::Move;
}

ViewportToolRuntime::ViewportToolRuntime() {
    syncActiveTool(ViewportToolId::Select);
}

bool ViewportToolRuntime::activeToolOwnsViewport() const {
    return activeTool_ != nullptr && modalController_.ownsViewport(host_, *activeTool_);
}

bool ViewportToolRuntime::activeToolBlocksViewportFocusShortcuts() const {
    return activeTool_ != nullptr && modalController_.blocksViewportFocusShortcuts(host_, *activeTool_);
}

bool ViewportToolRuntime::toolAvailable(ViewportToolId tool) const {
    return registry_.available(tool);
}

bool ViewportToolRuntime::toolAllowsSelectionFallback(ViewportToolId tool) const {
    const ViewportTool* item = registry_.find(tool);
    return item != nullptr && host_.allowsSelectionFallback(*item);
}

ModelingTransformTool ViewportToolRuntime::transformTool(ViewportToolId tool) const {
    const ViewportTool* item = registry_.find(tool);
    return item != nullptr ? transformController_.transformTool(*item) : ModelingTransformTool::Move;
}

bool ViewportToolRuntime::setActiveTool(
        ViewportToolState& state,
        ViewportToolId tool,
        ViewportInputResult* result) {
    return controller_.activateTool(registry_, host_, state, tool, activeTool_, result);
}

void ViewportToolRuntime::syncActiveTool(ViewportToolId tool) {
    activeTool_ = registry_.find(tool);
}

bool ViewportToolRuntime::hasActiveModalPreview() const {
    return modalState_.boxPreview.valid || modalState_.boxPreview.dragState != ModelingBoxDragState::Idle;
}

const ModelingBoxPreview& ViewportToolRuntime::boxPreview() const {
    return modalState_.boxPreview;
}

void ViewportToolRuntime::clearModalToolState() {
    modalState_ = {};
}

void ViewportToolRuntime::clearModalDragState() {
    modalState_.insertEdgeLoopDragActive = false;
    modalState_.cutDragActive = false;
    modalState_.mirrorDragActive = false;
}

bool ViewportToolRuntime::handleShortcut(
        const ViewportShortcut& shortcut,
        ViewportToolState& state,
        ViewportCameraController& camera,
        ViewportInputResult& result) {
    (void)eventBuilder_.activationEvent(shortcut.tool, &shortcut);

    switch (shortcut.kind) {
    case ViewportShortcutKind::ActivateTool:
        return setActiveTool(state, shortcut.tool, &result);
    case ViewportShortcutKind::SetSelectionMode:
        result.selectionModeChanged = state.selectionMode != shortcut.selectionMode;
        state.selectionMode = shortcut.selectionMode;
        return true;
    case ViewportShortcutKind::FocusDocument:
        if (activeToolBlocksViewportFocusShortcuts()) {
            return false;
        }
        result.focusDocumentRequested = true;
        result.cameraChanged = true;
        return true;
    case ViewportShortcutKind::FocusOrigin:
        camera.focusOrigin();
        result.cameraChanged = true;
        return true;
    case ViewportShortcutKind::ToggleMaximized:
        if (activeToolBlocksViewportFocusShortcuts()) {
            return false;
        }
        state.maximized = !state.maximized;
        return true;
    case ViewportShortcutKind::SetGridPreset:
        result.gridPresetChanged = state.gridPreset != shortcut.gridPreset;
        state.gridPreset = shortcut.gridPreset;
        return true;
    case ViewportShortcutKind::CancelActiveTool:
        result.cancelRequested = true;
        return true;
    case ViewportShortcutKind::DeletePopup:
        return !activeToolBlocksViewportFocusShortcuts();
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
    case ViewportShortcutKind::FlipMeshNormals:
        if (activeToolBlocksViewportFocusShortcuts()) {
            return false;
        }
        return state.selectionMode == ViewportSelectionMode::Object;
    case ViewportShortcutKind::FlipHorizontal:
    case ViewportShortcutKind::FlipVertical:
        return state.selectionMode == ViewportSelectionMode::Object;
    case ViewportShortcutKind::ApplyMaterial:
        return true;
    case ViewportShortcutKind::None:
        return false;
    }
    return false;
}

ViewportToolResult ViewportToolRuntime::handleActiveToolEvent(
        ViewportToolState& state,
        ViewportToolContext& context,
        const ViewportToolEvent& event) {
    if (activeTool_ == nullptr) {
        syncActiveTool(state.activeTool);
    }
    if (activeTool_ == nullptr) {
        return {};
    }
    context.modalState = &modalState_;
    ViewportToolResult result = activeTool_->handleEvent(context, event);
    if (result.syncActiveToolFromModeling) {
        const ViewportToolId nextTool = viewportToolForModeling(context.modeling.toolStatus().activeTool);
        const bool changed = state.activeTool != nextTool;
        if (setActiveTool(state, nextTool)) {
            result.activeToolChanged = result.activeToolChanged || changed;
        }
    }
    return result;
}

ModelingCommandResult ViewportToolRuntime::activateActiveTool(ModelingSessionService& modeling) const {
    if (activeTool_ == nullptr) {
        return {
                .ok = false,
                .changed = false,
                .message = "No active viewport tool.",
        };
    }
    return host_.activateTool(*activeTool_, modeling);
}

} // namespace quader
