////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/QuaderCommandsApi.h"

#include "api/QuaderApi.h"
#include "api/QuaderToolsApi.h"
#include "api/QuaderViewportApi.h"
#include "editor/commands/ModelingCommandToolCatalog.h"
#include "editor/tools/ViewportToolCatalog.h"
#include "viewport/HostViewportContractAdapter.h"

#include <cstdlib>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

namespace quader {
namespace {

std::string prefixedCommandId(std::string_view prefix, std::string_view suffix) {
    std::string commandId(prefix);
    commandId.append(suffix);
    return commandId;
}

std::optional<QuaderCommandId> fixedCommandIdForModelingAction(std::string_view actionId) {
    if (actionId == "delete_selection") {
        return QuaderCommand::DeleteSelection;
    }
    if (actionId == "clear_selection") {
        return QuaderCommand::EditClearSelection;
    }
    if (actionId == "select_all") {
        return QuaderCommand::EditSelectAll;
    }
    if (actionId == "invert_selection") {
        return QuaderCommand::EditInvertSelection;
    }
    if (actionId == "copy_selection") {
        return QuaderCommand::CopySelection;
    }
    if (actionId == "paste_selection") {
        return QuaderCommand::PasteSelection;
    }
    if (actionId == "duplicate_meshes") {
        return QuaderCommand::DuplicateSelection;
    }
    if (actionId == "repeat_last_action") {
        return QuaderCommand::EditRepeatLastAction;
    }
    if (actionId == "assign_material" || actionId == "apply_active_material") {
        return QuaderCommand::ApplyMaterial;
    }
    if (actionId == "dissolve_edges") {
        return QuaderCommand::ModelingDissolveEdges;
    }
    if (actionId == "dissolve_vertices") {
        return QuaderCommand::ModelingDissolveVertices;
    }
    if (actionId == "combine_meshes") {
        return QuaderCommand::ModelingCombineMeshes;
    }
    if (actionId == "merge_vertices_to_active") {
        return QuaderCommand::ModelingMergeVerticesToActive;
    }
    if (actionId == "merge_vertices_to_center") {
        return QuaderCommand::ModelingMergeVerticesToCenter;
    }
    if (actionId == "mirror_selection_x") {
        return QuaderCommand::ModelingMirrorSelectionX;
    }
    if (actionId == "mirror_selection_y") {
        return QuaderCommand::ModelingMirrorSelectionY;
    }
    if (actionId == "mirror_selection_z") {
        return QuaderCommand::ModelingMirrorSelectionZ;
    }
    if (actionId == "invert_mesh") {
        return QuaderCommand::ModelingInvertMesh;
    }
    if (actionId == "flip_horizontal") {
        return QuaderCommand::ModelingFlipHorizontal;
    }
    if (actionId == "flip_vertical") {
        return QuaderCommand::ModelingFlipVertical;
    }
    return std::nullopt;
}

std::string commandIdForModelingAction(std::string_view actionId) {
    if (const std::optional<QuaderCommandId> fixed = fixedCommandIdForModelingAction(actionId)) {
        return std::string(fixed->string());
    }

    return prefixedCommandId("modeling.", actionId);
}

std::string commandIdForToolActivation(std::string_view toolId) {
    return prefixedCommandId("tools.activate.", toolId);
}

std::string legacyActionAlias(std::string_view actionId) {
    return prefixedCommandId("modeling.action.", actionId);
}

std::filesystem::path defaultKeybindingsPath() {
    if (const char* localAppData = std::getenv("LOCALAPPDATA")) {
        return std::filesystem::path(localAppData) / "Quader" / "keybindings.json";
    }
    return std::filesystem::path("keybindings.json");
}

std::string viewportActionTargetId(std::string_view suffix) {
    return prefixedCommandId("viewport.", suffix);
}

std::string selectionModeTargetId(std::string_view mode) {
    return prefixedCommandId("viewport.selection_mode.", mode);
}

std::string gridPresetTargetId(int preset) {
    return prefixedCommandId("viewport.grid_preset.", std::to_string(preset));
}

std::optional<std::string> targetIdForViewportShortcutTarget(std::string_view targetId) {
    if (targetId == "select_all") {
        return std::string(QuaderCommand::EditSelectAll.string());
    }
    if (targetId == "copy_meshes") {
        return std::string(QuaderCommand::CopySelection.string());
    }
    if (targetId == "duplicate_meshes") {
        return std::string(QuaderCommand::DuplicateSelection.string());
    }
    if (targetId == "invert_selection") {
        return std::string(QuaderCommand::EditInvertSelection.string());
    }
    if (targetId == "repeat_last_action") {
        return std::string(QuaderCommand::EditRepeatLastAction.string());
    }
    if (targetId == "paste_meshes") {
        return std::string(QuaderCommand::PasteSelection.string());
    }
    if (targetId == "undo") {
        return std::string(QuaderCommand::EditUndo.string());
    }
    if (targetId == "redo") {
        return std::string(QuaderCommand::EditRedo.string());
    }
    if (targetId == "delete") {
        return std::string(QuaderCommand::DeleteSelection.string());
    }
    if (targetId == "merge_vertices") {
        return std::string(QuaderCommand::ModelingMergeVerticesToActive.string());
    }
    if (targetId == "merge_vertices_at_center") {
        return std::string(QuaderCommand::ModelingMergeVerticesToCenter.string());
    }
    if (targetId == "apply_active_material") {
        return std::string(QuaderCommand::ApplyMaterial.string());
    }
    if (targetId == "invert_mesh") {
        return std::string(QuaderCommand::ModelingInvertMesh.string());
    }
    if (targetId == "focus_all") {
        return std::string(QuaderCommand::ViewportFrameSelection.string());
    }
    if (targetId == "focus_origin") {
        return std::string(QuaderCommand::ViewportFocusOrigin.string());
    }
    if (targetId == "mirror_axis_x") {
        return std::string(QuaderCommand::ModelingMirrorSelectionX.string());
    }
    if (targetId == "mirror_axis_y") {
        return std::string(QuaderCommand::ModelingMirrorSelectionY.string());
    }
    if (targetId == "mirror_axis_z") {
        return std::string(QuaderCommand::ModelingMirrorSelectionZ.string());
    }
    if (targetId == "flip_horizontal") {
        return std::string(QuaderCommand::ModelingFlipHorizontal.string());
    }
    if (targetId == "flip_vertical") {
        return std::string(QuaderCommand::ModelingFlipVertical.string());
    }

    ViewportToolId tool = ViewportToolId::Select;
    if (viewportToolFromContractId(targetId, tool)) {
        return commandIdForToolActivation(viewportToolCatalogId(tool));
    }

    ViewportSelectionMode mode = ViewportSelectionMode::Object;
    if (viewportSelectionModeFromContractId(targetId, mode)) {
        return selectionModeTargetId(viewportSelectionModeId(mode));
    }

    return std::nullopt;
}

std::optional<QuaderKeyCode> keyCodeForViewportKey(ViewportKey key) {
    switch (key) {
    case ViewportKey::A:
        return QuaderKeyCode::A;
    case ViewportKey::Q:
        return QuaderKeyCode::Q;
    case ViewportKey::W:
        return QuaderKeyCode::W;
    case ViewportKey::D:
        return QuaderKeyCode::D;
    case ViewportKey::E:
        return QuaderKeyCode::E;
    case ViewportKey::F:
        return QuaderKeyCode::F;
    case ViewportKey::G:
        return QuaderKeyCode::G;
    case ViewportKey::H:
        return QuaderKeyCode::H;
    case ViewportKey::I:
        return QuaderKeyCode::I;
    case ViewportKey::J:
        return QuaderKeyCode::J;
    case ViewportKey::K:
        return QuaderKeyCode::K;
    case ViewportKey::L:
        return QuaderKeyCode::L;
    case ViewportKey::M:
        return QuaderKeyCode::M;
    case ViewportKey::N:
        return QuaderKeyCode::N;
    case ViewportKey::O:
        return QuaderKeyCode::O;
    case ViewportKey::R:
        return QuaderKeyCode::R;
    case ViewportKey::S:
        return QuaderKeyCode::S;
    case ViewportKey::B:
        return QuaderKeyCode::B;
    case ViewportKey::C:
        return QuaderKeyCode::C;
    case ViewportKey::P:
        return QuaderKeyCode::P;
    case ViewportKey::T:
        return QuaderKeyCode::T;
    case ViewportKey::U:
        return QuaderKeyCode::U;
    case ViewportKey::V:
        return QuaderKeyCode::V;
    case ViewportKey::X:
        return QuaderKeyCode::X;
    case ViewportKey::Y:
        return QuaderKeyCode::Y;
    case ViewportKey::Z:
        return QuaderKeyCode::Z;
    case ViewportKey::Number0:
        return QuaderKeyCode::Digit0;
    case ViewportKey::Number1:
        return QuaderKeyCode::Digit1;
    case ViewportKey::Number2:
        return QuaderKeyCode::Digit2;
    case ViewportKey::Number3:
        return QuaderKeyCode::Digit3;
    case ViewportKey::Number4:
        return QuaderKeyCode::Digit4;
    case ViewportKey::Number5:
        return QuaderKeyCode::Digit5;
    case ViewportKey::Number6:
        return QuaderKeyCode::Digit6;
    case ViewportKey::Number7:
        return QuaderKeyCode::Digit7;
    case ViewportKey::Number8:
        return QuaderKeyCode::Digit8;
    case ViewportKey::Number9:
        return QuaderKeyCode::Digit9;
    case ViewportKey::Home:
        return QuaderKeyCode::Home;
    case ViewportKey::End:
        return QuaderKeyCode::End;
    case ViewportKey::PageUp:
        return QuaderKeyCode::PageUp;
    case ViewportKey::PageDown:
        return QuaderKeyCode::PageDown;
    case ViewportKey::Left:
        return QuaderKeyCode::Left;
    case ViewportKey::Right:
        return QuaderKeyCode::Right;
    case ViewportKey::Up:
        return QuaderKeyCode::Up;
    case ViewportKey::Down:
        return QuaderKeyCode::Down;
    case ViewportKey::Space:
        return QuaderKeyCode::Space;
    case ViewportKey::F1:
        return QuaderKeyCode::F1;
    case ViewportKey::F2:
        return QuaderKeyCode::F2;
    case ViewportKey::F3:
        return QuaderKeyCode::F3;
    case ViewportKey::F4:
        return QuaderKeyCode::F4;
    case ViewportKey::F5:
        return QuaderKeyCode::F5;
    case ViewportKey::F6:
        return QuaderKeyCode::F6;
    case ViewportKey::F7:
        return QuaderKeyCode::F7;
    case ViewportKey::F8:
        return QuaderKeyCode::F8;
    case ViewportKey::F9:
        return QuaderKeyCode::F9;
    case ViewportKey::F10:
        return QuaderKeyCode::F10;
    case ViewportKey::F11:
        return QuaderKeyCode::F11;
    case ViewportKey::F12:
        return QuaderKeyCode::F12;
    case ViewportKey::Escape:
        return QuaderKeyCode::Escape;
    case ViewportKey::Delete:
        return QuaderKeyCode::Delete;
    case ViewportKey::Backspace:
        return QuaderKeyCode::Backspace;
    case ViewportKey::Enter:
        return QuaderKeyCode::Enter;
    case ViewportKey::Tab:
        return QuaderKeyCode::Tab;
    }

    return std::nullopt;
}

std::optional<QuaderInputChord> inputChordForViewportKey(ViewportKey key, ViewportModifiers modifiers) {
    const std::optional<QuaderKeyCode> keyCode = keyCodeForViewportKey(key);
    if (!keyCode) {
        return std::nullopt;
    }
    return QuaderInputChord{
            .primary = modifiers.primary,
            .shift = modifiers.shift,
            .alt = modifiers.alt,
            .device = QuaderInputDevice::Keyboard,
            .key = *keyCode,
    };
}

bool viewportInputActionForTargetId(std::string_view targetId, ViewportInputActionKind& action) {
    if (targetId == viewportActionTargetId("navigation.orbit")) {
        action = ViewportInputActionKind::Orbit;
        return true;
    }
    if (targetId == viewportActionTargetId("navigation.orbit_snap")) {
        action = ViewportInputActionKind::OrbitSnap;
        return true;
    }
    if (targetId == viewportActionTargetId("navigation.pan")) {
        action = ViewportInputActionKind::Pan;
        return true;
    }
    if (targetId == viewportActionTargetId("navigation.fly")) {
        action = ViewportInputActionKind::Fly;
        return true;
    }
    if (targetId == viewportActionTargetId("navigation.zoom")) {
        action = ViewportInputActionKind::Zoom;
        return true;
    }
    if (targetId == viewportActionTargetId("navigation.zoom_to_cursor")) {
        action = ViewportInputActionKind::ZoomToCursor;
        return true;
    }
    if (targetId == viewportActionTargetId("navigation.reset_fly_speed")) {
        action = ViewportInputActionKind::ResetFlySpeed;
        return true;
    }
    return false;
}

QuaderKeybindingContext viewportKeybindingContext(
        const ViewportToolState& state,
        bool viewportFocused,
        bool textInputActive,
        QuaderViewportKeybindingContext inputContext,
        bool canUndo,
        bool canRedo) {
    QuaderKeybindingContext context;
    context.set("viewportFocus", viewportFocused);
    context.set("viewport.hovered", inputContext.viewportHovered);
    context.set("textInputActive", textInputActive);
    context.set("editorTextFocus", textInputActive);
    context.set("canUndo", canUndo);
    context.set("canRedo", canRedo);
    context.set("navigation.active", inputContext.navigationActive);
    context.set("navigation.flyActive", inputContext.flyNavigationActive);
    context.set("navigation.orbitActive", inputContext.orbitNavigationActive);
    context.set("navigation.panActive", inputContext.panNavigationActive);
    context.set("modalTool.active", inputContext.modalToolActive);
    context.set("activeTool.ownsViewport", inputContext.activeToolOwnsViewport);
    context.set("activeTool." + std::string(viewportToolId(state.activeTool)), true);
    context.set("selectionMode." + std::string(viewportSelectionModeId(state.selectionMode)), true);
    return context;
}

bool viewportShortcutForTargetId(std::string_view targetId, ViewportShortcut& shortcut) {
    if (targetId == QuaderCommand::EditSelectAll.string()) {
        shortcut.kind = ViewportShortcutKind::SelectAll;
        shortcut.context = ViewportShortcutContext::Global;
        return true;
    }
    if (targetId == QuaderCommand::CopySelection.string()) {
        shortcut.kind = ViewportShortcutKind::Copy;
        shortcut.context = ViewportShortcutContext::Global;
        return true;
    }
    if (targetId == QuaderCommand::DuplicateSelection.string()) {
        shortcut.kind = ViewportShortcutKind::Duplicate;
        shortcut.context = ViewportShortcutContext::Global;
        return true;
    }
    if (targetId == QuaderCommand::EditInvertSelection.string()) {
        shortcut.kind = ViewportShortcutKind::InvertSelection;
        shortcut.context = ViewportShortcutContext::Global;
        return true;
    }
    if (targetId == QuaderCommand::EditRepeatLastAction.string()) {
        shortcut.kind = ViewportShortcutKind::RepeatLastAction;
        shortcut.context = ViewportShortcutContext::Global;
        return true;
    }
    if (targetId == QuaderCommand::PasteSelection.string()) {
        shortcut.kind = ViewportShortcutKind::Paste;
        shortcut.context = ViewportShortcutContext::Global;
        return true;
    }
    if (targetId == QuaderCommand::EditUndo.string()) {
        shortcut.kind = ViewportShortcutKind::Undo;
        shortcut.context = ViewportShortcutContext::Global;
        return true;
    }
    if (targetId == QuaderCommand::EditRedo.string()) {
        shortcut.kind = ViewportShortcutKind::Redo;
        shortcut.context = ViewportShortcutContext::Global;
        return true;
    }
    if (targetId == QuaderCommand::DeleteSelection.string()) {
        shortcut.kind = ViewportShortcutKind::DeleteSelection;
        shortcut.context = ViewportShortcutContext::Global;
        return true;
    }
    if (targetId == QuaderCommand::ModelingMergeVerticesToActive.string()) {
        shortcut.kind = ViewportShortcutKind::MergeActive;
        shortcut.context = ViewportShortcutContext::Viewport;
        return true;
    }
    if (targetId == QuaderCommand::ModelingMergeVerticesToCenter.string()) {
        shortcut.kind = ViewportShortcutKind::MergeCenter;
        shortcut.context = ViewportShortcutContext::Viewport;
        return true;
    }
    if (targetId == QuaderCommand::ModelingMirrorSelectionX.string()) {
        shortcut.kind = ViewportShortcutKind::MirrorAxis;
        shortcut.context = ViewportShortcutContext::Viewport;
        shortcut.mirrorAxis = ViewportMirrorAxis::X;
        return true;
    }
    if (targetId == QuaderCommand::ModelingMirrorSelectionY.string()) {
        shortcut.kind = ViewportShortcutKind::MirrorAxis;
        shortcut.context = ViewportShortcutContext::Viewport;
        shortcut.mirrorAxis = ViewportMirrorAxis::Y;
        return true;
    }
    if (targetId == QuaderCommand::ModelingMirrorSelectionZ.string()) {
        shortcut.kind = ViewportShortcutKind::MirrorAxis;
        shortcut.context = ViewportShortcutContext::Viewport;
        shortcut.mirrorAxis = ViewportMirrorAxis::Z;
        return true;
    }
    if (targetId == QuaderCommand::ModelingInvertMesh.string()) {
        shortcut.kind = ViewportShortcutKind::FlipMeshNormals;
        shortcut.context = ViewportShortcutContext::Viewport;
        return true;
    }
    if (targetId == QuaderCommand::ModelingFlipHorizontal.string()) {
        shortcut.kind = ViewportShortcutKind::FlipHorizontal;
        shortcut.context = ViewportShortcutContext::Viewport;
        return true;
    }
    if (targetId == QuaderCommand::ModelingFlipVertical.string()) {
        shortcut.kind = ViewportShortcutKind::FlipVertical;
        shortcut.context = ViewportShortcutContext::Viewport;
        return true;
    }
    if (targetId == QuaderCommand::ApplyMaterial.string()) {
        shortcut.kind = ViewportShortcutKind::ApplyMaterial;
        shortcut.context = ViewportShortcutContext::Viewport;
        return true;
    }
    if (targetId == QuaderCommand::ViewportFrameSelection.string()) {
        shortcut.kind = ViewportShortcutKind::FocusDocument;
        shortcut.context = ViewportShortcutContext::Viewport;
        return true;
    }
    if (targetId == QuaderCommand::ViewportFocusOrigin.string()) {
        shortcut.kind = ViewportShortcutKind::FocusOrigin;
        shortcut.context = ViewportShortcutContext::Viewport;
        return true;
    }
    if (targetId == viewportActionTargetId("cancel_active_tool")) {
        shortcut.kind = ViewportShortcutKind::CancelActiveTool;
        shortcut.context = ViewportShortcutContext::Viewport;
        return true;
    }
    if (targetId == viewportActionTargetId("toggle_maximized")) {
        shortcut.kind = ViewportShortcutKind::ToggleMaximized;
        shortcut.context = ViewportShortcutContext::Viewport;
        return true;
    }
    if (targetId == viewportActionTargetId("delete_popup")) {
        shortcut.kind = ViewportShortcutKind::DeletePopup;
        shortcut.context = ViewportShortcutContext::Viewport;
        return true;
    }

    constexpr std::string_view toolPrefix = "tools.activate.";
    if (targetId.starts_with(toolPrefix)) {
        ViewportToolId tool = ViewportToolId::Select;
        if (!viewportToolFromContractId(targetId.substr(toolPrefix.size()), tool)) {
            return false;
        }
        shortcut.kind = ViewportShortcutKind::ActivateTool;
        shortcut.context = ViewportShortcutContext::Viewport;
        shortcut.tool = tool;
        return true;
    }

    constexpr std::string_view selectionModePrefix = "viewport.selection_mode.";
    if (targetId.starts_with(selectionModePrefix)) {
        ViewportSelectionMode mode = ViewportSelectionMode::Object;
        if (!viewportSelectionModeFromContractId(targetId.substr(selectionModePrefix.size()), mode)) {
            return false;
        }
        shortcut.kind = ViewportShortcutKind::SetSelectionMode;
        shortcut.context = ViewportShortcutContext::Viewport;
        shortcut.selectionMode = mode;
        return true;
    }

    constexpr std::string_view gridPresetPrefix = "viewport.grid_preset.";
    if (targetId.starts_with(gridPresetPrefix)) {
        const std::string presetText(targetId.substr(gridPresetPrefix.size()));
        try {
            const int preset = std::stoi(presetText);
            shortcut.kind = ViewportShortcutKind::SetGridPreset;
            shortcut.context = ViewportShortcutContext::Viewport;
            shortcut.gridPreset = preset;
            return true;
        } catch (...) {
            return false;
        }
    }

    return false;
}

} // namespace

QuaderCommandsApi::QuaderCommandsApi(QuaderApi& api, QuaderViewportApi& viewports, QuaderToolsApi& tools)
    : api_(api)
    , viewports_(viewports)
    , tools_(tools) {
    keybindings_.setStoragePath(defaultKeybindingsPath());
    registerCommands();
    registerDefaultKeybindings();
    (void)keybindings_.loadUserKeybindings();
}

QuaderApiVoidResult QuaderCommandsApi::execute(QuaderCommandId commandId) {
    return execute(commandId.string());
}

QuaderApiVoidResult QuaderCommandsApi::executeModelingAction(std::string_view actionId) {
    return execute(commandIdForModelingAction(actionId));
}

QuaderApiVoidResult QuaderCommandsApi::executeToolActivation(std::string_view toolId) {
    return execute(commandIdForToolActivation(toolId));
}

QuaderApiVoidResult QuaderCommandsApi::execute(std::string_view commandId) {
    if (std::optional<QuaderApiVoidResult> result = registry_.execute(commandId)) {
        result->revisions = api_.revisions();
        return *result;
    }
    return failure("Unknown command id.");
}

std::vector<QuaderCommandDescriptor> QuaderCommandsApi::available() const {
    std::vector<QuaderCommandDescriptor> commands = registry_.available();
    for (QuaderCommandDescriptor& command : commands) {
        command.shortcut = keybindings_.shortcutFor(command.id);
    }
    return commands;
}

std::string QuaderCommandsApi::commandIdForModelingAction(std::string_view actionId) const {
    return quader::commandIdForModelingAction(actionId);
}

std::string QuaderCommandsApi::commandIdForToolActivation(std::string_view toolId) const {
    return quader::commandIdForToolActivation(toolId);
}

std::string QuaderCommandsApi::shortcutFor(std::string_view commandId) const {
    if (registry_.contains(commandId) &&
            registry_.canonicalCommandId(commandId) != commandId &&
            registry_.shortcutFor(commandId).empty()) {
        return {};
    }
    return keybindings_.shortcutFor(canonicalTargetId(commandId));
}

std::string QuaderCommandsApi::shortcutFor(QuaderCommandId commandId) const {
    return shortcutFor(commandId.string());
}

std::vector<QuaderKeybindingRule> QuaderCommandsApi::keybindings() const {
    return keybindings_.keybindings();
}

std::vector<QuaderKeybindingConflict> QuaderCommandsApi::conflictsForShortcut(
        std::string_view shortcut,
        std::string_view when) const {
    return keybindings_.conflictsFor(shortcut, when);
}

bool QuaderCommandsApi::resolveViewportShortcut(
        ViewportKey key,
        ViewportModifiers modifiers,
        const ViewportToolState& state,
        bool viewportFocused,
        bool textInputActive,
        ViewportShortcut& shortcut,
        QuaderViewportKeybindingContext inputContext) {
    const std::optional<QuaderInputChord> chord = inputChordForViewportKey(key, modifiers);
    return chord ? resolveViewportShortcut(*chord, state, viewportFocused, textInputActive, shortcut, inputContext) : false;
}

bool QuaderCommandsApi::resolveViewportShortcut(
        const QuaderInputChord& chord,
        const ViewportToolState& state,
        bool viewportFocused,
        bool textInputActive,
        ViewportShortcut& shortcut,
        QuaderViewportKeybindingContext inputContext) {
    const QuaderViewportKeybindingDispatch dispatch =
            dispatchViewportInputBinding(chord, state, viewportFocused, textInputActive, inputContext);
    if (dispatch.kind == QuaderViewportKeybindingDispatch::Kind::PendingChord) {
        shortcut.kind = ViewportShortcutKind::None;
        return true;
    }
    if (dispatch.kind != QuaderViewportKeybindingDispatch::Kind::ViewportShortcut) {
        return false;
    }
    shortcut = dispatch.shortcut;
    return true;
}

bool QuaderCommandsApi::resolveViewportInputAction(
        const QuaderInputChord& chord,
        const ViewportToolState& state,
        bool viewportFocused,
        bool textInputActive,
        ViewportInputActionKind& action,
        QuaderViewportKeybindingContext inputContext) {
    action = ViewportInputActionKind::None;
    if (chord.empty()) {
        return false;
    }

    const QuaderKeybindingContext context = viewportKeybindingContext(
            state,
            viewportFocused,
            textInputActive,
            inputContext,
            api_.history().canUndo(),
            api_.history().canRedo());
    const QuaderKeybindingDispatch dispatch = keybindings_.resolveSequence({chord}, context);
    return dispatch.kind == QuaderKeybindingDispatch::Kind::ExactMatch &&
            viewportInputActionForTargetId(dispatch.targetId, action);
}

QuaderViewportKeybindingDispatch QuaderCommandsApi::dispatchViewportKeybinding(
        ViewportKey key,
        ViewportModifiers modifiers,
        const ViewportToolState& state,
        bool viewportFocused,
        bool textInputActive,
        QuaderViewportKeybindingContext inputContext) {
    const std::optional<QuaderInputChord> chord = inputChordForViewportKey(key, modifiers);
    return chord ? dispatchViewportInputBinding(*chord, state, viewportFocused, textInputActive, inputContext) : QuaderViewportKeybindingDispatch{};
}

QuaderViewportKeybindingDispatch QuaderCommandsApi::dispatchViewportInputBinding(
        const QuaderInputChord& chord,
        const ViewportToolState& state,
        bool viewportFocused,
        bool textInputActive,
        QuaderViewportKeybindingContext inputContext) {
    if (chord.empty()) {
        keybindings_.resetPendingChord();
        return {};
    }

    const QuaderKeybindingContext context = viewportKeybindingContext(
            state,
            viewportFocused,
            textInputActive,
            inputContext,
            api_.history().canUndo(),
            api_.history().canRedo());
    const QuaderKeybindingDispatch dispatch = keybindings_.dispatch(chord, context);
    if (dispatch.kind == QuaderKeybindingDispatch::Kind::PendingChord) {
        return {
                .kind = QuaderViewportKeybindingDispatch::Kind::PendingChord,
        };
    }
    if (dispatch.kind != QuaderKeybindingDispatch::Kind::ExactMatch) {
        return {};
    }

    ViewportShortcut shortcut;
    if (viewportShortcutForTargetId(dispatch.targetId, shortcut)) {
        return {
                .kind = QuaderViewportKeybindingDispatch::Kind::ViewportShortcut,
                .shortcut = shortcut,
        };
    }

    if (registry_.contains(dispatch.targetId)) {
        return {
                .kind = QuaderViewportKeybindingDispatch::Kind::Command,
                .commandId = dispatch.targetId,
        };
    }

    return {};
}

void QuaderCommandsApi::resetPendingKeybindingChord() {
    keybindings_.resetPendingChord();
}

bool QuaderCommandsApi::hasPendingKeybindingChord() const {
    return keybindings_.hasPendingChord();
}

QuaderApiVoidResult QuaderCommandsApi::setShortcut(std::string_view commandId, std::string shortcut) {
    const std::string targetId = canonicalTargetId(commandId);
    std::string when;
    for (const QuaderKeybindingRule& rule : keybindings_.keybindings()) {
        if (rule.targetId == targetId) {
            when = rule.when;
            break;
        }
    }
    if (const std::optional<std::string> error = keybindings_.setUserShortcut(targetId, std::move(shortcut), when)) {
        return failure(*error);
    }
    return {
            .ok = true,
            .changed = true,
            .message = "Shortcut updated.",
            .mutationKind = QuaderMutationKind::None,
            .revisions = api_.revisions(),
    };
}

QuaderApiVoidResult QuaderCommandsApi::removeShortcut(std::string_view commandId) {
    const std::string targetId = canonicalTargetId(commandId);
    if (const std::optional<std::string> error = keybindings_.removeUserShortcut(targetId)) {
        return failure(*error);
    }
    return {
            .ok = true,
            .changed = true,
            .message = "Shortcut removed.",
            .mutationKind = QuaderMutationKind::None,
            .revisions = api_.revisions(),
    };
}

QuaderApiVoidResult QuaderCommandsApi::resetShortcut(std::string_view commandId) {
    const std::string targetId = canonicalTargetId(commandId);
    if (const std::optional<std::string> error = keybindings_.resetUserShortcut(targetId)) {
        return failure(*error);
    }
    return {
            .ok = true,
            .changed = true,
            .message = "Shortcut reset.",
            .mutationKind = QuaderMutationKind::None,
            .revisions = api_.revisions(),
    };
}

QuaderApiVoidResult QuaderCommandsApi::setKeybindingStoragePath(std::filesystem::path path) {
    keybindings_.setStoragePath(std::move(path));
    keybindings_.clearUserKeybindings();
    if (const std::optional<std::string> error = keybindings_.loadUserKeybindings()) {
        return failure(*error);
    }
    return {
            .ok = true,
            .changed = false,
            .message = "Shortcut storage path updated.",
            .mutationKind = QuaderMutationKind::None,
            .revisions = api_.revisions(),
    };
}

void QuaderCommandsApi::registerCommands() {
    registerCommand(
            QuaderCommand::EditUndo,
            "Undo",
            [this] { return api_.history().undo(); },
            "Ctrl+Z",
            [this] { return api_.history().canUndo(); });
    registerCommand(
            QuaderCommand::EditRedo,
            "Redo",
            [this] { return api_.history().redo(); },
            "Ctrl+Y",
            [this] { return api_.history().canRedo(); });
    registerCommand(
            QuaderCommand::EditRepeatLastAction,
            "Repeat Last Action",
            [this] { return api_.history().repeatLastAction(); },
            "Ctrl+R");
    registerCommand(
            QuaderCommand::EditClearSelection,
            "Clear Selection",
            [this] { return api_.selection().clear(); });
    registerCommand(
            QuaderCommand::EditSelectAll,
            "Select All",
            [this] { return api_.selection().selectAll(); },
            "Ctrl+A");
    registerCommand(
            QuaderCommand::EditInvertSelection,
            "Invert Selection",
            [this] { return api_.selection().invert(); },
            "Ctrl+I");
    registerCommand(
            QuaderCommand::DeleteSelection,
            "Delete Selection",
            [this] { return api_.modeling().executeTool("delete_selection"); });
    registerCommand(
            QuaderCommand::CopySelection,
            "Copy Selection",
            [this] { return api_.modeling().executeTool("copy_selection"); },
            "Ctrl+C");
    registerCommand(
            QuaderCommand::PasteSelection,
            "Paste Selection",
            [this] { return api_.modeling().executeTool("paste_selection"); },
            "Ctrl+V");
    registerCommand(
            QuaderCommand::DuplicateSelection,
            "Duplicate Selection",
            [this] { return api_.modeling().executeTool("duplicate_meshes"); },
            "Ctrl+D");
    registerCommand(
            QuaderCommand::GroupSelection,
            "Group Selection",
            [this] {
                const QuaderApiResult<QuaderEntityRef> result = api_.scene().groupSelection();
                return QuaderApiVoidResult{
                        .ok = result.ok,
                        .changed = result.changed,
                        .message = result.message,
                        .mutationKind = result.mutationKind,
                        .revisions = result.revisions,
                };
            },
            "Ctrl+G");
    registerCommand(
            QuaderCommand::UngroupSelection,
            "Ungroup Selection",
            [this] { return api_.scene().ungroupSelection(); },
            "Shift+Ctrl+G");
    registerCommand(
            QuaderCommand::ApplyMaterial,
            "Apply Material",
            [this] { return api_.modeling().executeTool("assign_material"); });
    registerCommand(
            QuaderCommand::ViewportFrameSelection,
            "Frame Selection",
            [this] { return viewports_.frameSelection(); },
            "Home");
    registerCommand(
            QuaderCommand::ViewportFocusOrigin,
            "Focus Origin",
            [this] { return viewports_.focusOrigin(); },
            "Shift+Home");

    registerAlias("history.undo", QuaderCommand::EditUndo);
    registerAlias("history.redo", QuaderCommand::EditRedo);
    registerAlias("history.repeat_last_action", QuaderCommand::EditRepeatLastAction);
    registerAlias("selection.clear", QuaderCommand::EditClearSelection);
    registerAlias("selection.select_all", QuaderCommand::EditSelectAll);
    registerAlias("selection.invert", QuaderCommand::EditInvertSelection);
    registerAlias("modeling.delete_selection", QuaderCommand::DeleteSelection);
    registerAlias("modeling.copy_selection", QuaderCommand::CopySelection);
    registerAlias("modeling.paste_selection", QuaderCommand::PasteSelection);
    registerAlias("modeling.duplicate_meshes", QuaderCommand::DuplicateSelection);
    registerAlias("scene.group_selection", QuaderCommand::GroupSelection);
    registerAlias("scene.ungroup_selection", QuaderCommand::UngroupSelection);
    registerAlias("modeling.repeat_last_action", QuaderCommand::EditRepeatLastAction);
    registerAlias("modeling.assign_material", QuaderCommand::ApplyMaterial);
    registerAlias("modeling.apply_active_material", QuaderCommand::ApplyMaterial);

    const std::unordered_set<std::string_view> neutralModelingTools{
            "delete_selection",
            "clear_selection",
            "select_all",
            "invert_selection",
            "copy_selection",
            "paste_selection",
            "duplicate_meshes",
            "repeat_last_action",
            "assign_material",
            "apply_active_material",
    };
    for (const CommandTool* tool : modelingCommandTools()) {
        if (tool == nullptr) {
            continue;
        }
        registerAlias(legacyActionAlias(tool->id()), commandIdForModelingAction(tool->id()));
        if (neutralModelingTools.contains(tool->id())) {
            continue;
        }
        std::string id = commandIdForModelingAction(tool->id());
        registerCommand(
                id,
                std::string(tool->label()),
                [this, toolId = std::string(tool->id())] {
                    return api_.modeling().executeTool(toolId);
                });
    }
    for (const ViewportTool* tool : viewportToolCatalog()) {
        if (tool == nullptr) {
            continue;
        }
        registerCommand(
                commandIdForToolActivation(tool->id()),
                std::string(tool->label()),
                [this, toolId = std::string(tool->id())] {
                    return tools_.activate(toolId);
                },
                std::string(tool->shortcut()),
                [toolKind = tool->viewportTool()] {
                    return viewportToolAvailable(toolKind);
                });
    }

    registry_.registerPrefix("tools.activate.", [this](std::string_view toolId) {
        return tools_.activate(toolId);
    });
}

void QuaderCommandsApi::registerDefaultKeybindings() {
    constexpr std::string_view editableContext = "viewportFocus && !textInputActive && !navigation.active";
    for (const QuaderCommandDescriptor& command : registry_.available()) {
        keybindings_.registerTarget(command.id);
        if (!command.shortcut.empty()) {
            keybindings_.addDefaultKeybinding(command.id, command.shortcut, std::string(editableContext), 0);
        }
    }

    registerViewportDefaultKeybindings();
}

void QuaderCommandsApi::registerViewportDefaultKeybindings() {
    constexpr std::string_view viewportContext = "viewportFocus && !textInputActive";
    constexpr std::string_view viewportShortcutContext = "viewportFocus && !textInputActive && !navigation.active";

    keybindings_.addDefaultKeybinding(
            viewportActionTargetId("cancel_active_tool"),
            "Escape",
            std::string(viewportShortcutContext),
            10);
    keybindings_.addDefaultKeybinding(
            viewportActionTargetId("toggle_maximized"),
            "Space",
            std::string(viewportShortcutContext),
            10);
    keybindings_.addDefaultKeybinding(
            viewportActionTargetId("delete_popup"),
            "X",
            std::string(viewportShortcutContext),
            10);
    keybindings_.addDefaultKeybinding(
            viewportActionTargetId("navigation.orbit"),
            "MiddleMouseDrag",
            std::string(viewportContext),
            20);
    keybindings_.addDefaultKeybinding(
            viewportActionTargetId("navigation.orbit_snap"),
            "Alt+MiddleMouseDrag",
            std::string(viewportContext),
            20);
    keybindings_.addDefaultKeybinding(
            viewportActionTargetId("navigation.pan"),
            "Shift+MiddleMouseDrag",
            std::string(viewportContext),
            20);
    keybindings_.addDefaultKeybinding(
            viewportActionTargetId("navigation.fly"),
            "RightMouseDrag",
            std::string(viewportContext),
            20);
    keybindings_.addDefaultKeybinding(
            viewportActionTargetId("navigation.reset_fly_speed"),
            "MiddleMouse",
            std::string(viewportContext),
            20);
    keybindings_.addDefaultKeybinding(
            viewportActionTargetId("navigation.zoom"),
            "Wheel",
            std::string(viewportContext),
            20);
    keybindings_.addDefaultKeybinding(
            viewportActionTargetId("navigation.zoom_to_cursor"),
            "Ctrl+Wheel",
            std::string(viewportContext),
            20);

    for (const ViewportShortcutBindingDescriptor& descriptor : viewportShortcutBindingCatalog()) {
        const std::optional<std::string> targetId = targetIdForViewportShortcutTarget(descriptor.targetId);
        if (!targetId) {
            continue;
        }
        keybindings_.addDefaultKeybinding(
                *targetId,
                descriptor.shortcut,
                std::string(viewportShortcutContext),
                10);
    }

    keybindings_.addDefaultKeybinding(selectionModeTargetId("vertex"), "1", std::string(viewportShortcutContext), 5);
    keybindings_.addDefaultKeybinding(selectionModeTargetId("edge"), "2", std::string(viewportShortcutContext), 5);
    keybindings_.addDefaultKeybinding(selectionModeTargetId("face"), "3", std::string(viewportShortcutContext), 5);
    keybindings_.addDefaultKeybinding(selectionModeTargetId("object"), "4", std::string(viewportShortcutContext), 5);
    keybindings_.addDefaultKeybinding(selectionModeTargetId("group"), "5", std::string(viewportShortcutContext), 5);

    const std::vector<ViewportGridShortcutBindingDescriptor> presets = viewportGridShortcutBindingCatalog();
    for (const ViewportGridShortcutBindingDescriptor& preset : presets) {
        keybindings_.addDefaultKeybinding(
                gridPresetTargetId(preset.preset),
                preset.shortcut,
                std::string(viewportShortcutContext),
                10);
    }

    if (presets.empty()) {
        for (int preset = 1; preset <= 10; ++preset) {
            keybindings_.addDefaultKeybinding(
                    gridPresetTargetId(preset),
                    "F" + std::to_string(preset),
                    std::string(viewportShortcutContext),
                    10);
        }
    }
}

void QuaderCommandsApi::registerCommand(
        QuaderCommandId id,
        std::string label,
        std::function<QuaderApiVoidResult()> execute,
        std::string shortcut,
        std::function<bool()> enabled,
        bool visible) {
    registerCommand(
            std::string(id.string()),
            std::move(label),
            std::move(execute),
            std::move(shortcut),
            std::move(enabled),
            visible);
}

void QuaderCommandsApi::registerCommand(
        std::string id,
        std::string label,
        std::function<QuaderApiVoidResult()> execute,
        std::string shortcut,
        std::function<bool()> enabled,
        bool visible) {
    registry_.registerCommand(EditorCommandRegistration{
            .id = std::move(id),
            .label = std::move(label),
            .shortcut = std::move(shortcut),
            .visible = visible,
            .execute = std::move(execute),
            .enabled = std::move(enabled),
    });
}

void QuaderCommandsApi::registerAlias(std::string aliasId, QuaderCommandId targetId) {
    registerAlias(std::move(aliasId), std::string(targetId.string()));
}

void QuaderCommandsApi::registerAlias(std::string aliasId, std::string targetId) {
    registry_.registerAlias(std::move(aliasId), std::move(targetId));
}

QuaderApiVoidResult QuaderCommandsApi::failure(std::string message) const {
    return {
            .ok = false,
            .changed = false,
            .message = std::move(message),
            .mutationKind = QuaderMutationKind::None,
            .revisions = api_.revisions(),
    };
}

std::string QuaderCommandsApi::canonicalTargetId(std::string_view commandId) const {
    if (registry_.contains(commandId)) {
        return registry_.canonicalCommandId(commandId);
    }
    return std::string(commandId);
}

} // namespace quader
