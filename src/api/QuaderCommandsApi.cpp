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
#include "editor/commands/EditorCommandRegistry.h"
#include "editor/commands/ModelingCommandToolCatalog.h"
#include "editor/input/QuaderKeybindingService.h"
#include "editor/tools/ViewportToolCatalog.h"
#include "viewport/HostViewportContractAdapter.h"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace quader {
namespace {

std::string prefixedCommandId(std::string_view prefix, std::string_view suffix) {
    std::string commandId(prefix);
    commandId.append(suffix);
    return commandId;
}

/**
 * Stores the command bridge contract for modeling action IDs exposed through the public command API.
 */
struct ModelingActionCommandBridgeDescriptor {
    std::string_view actionId;
    QuaderCommandId commandId;
    bool registersDedicatedCommand = true;
    std::string_view compatibilityAlias;
};

std::span<const ModelingActionCommandBridgeDescriptor> modelingActionCommandBridgeCatalog() {
    static constexpr std::array<ModelingActionCommandBridgeDescriptor, 21> descriptors{{
            {"delete_selection", QuaderCommand::DeleteSelection, false, "modeling.delete_selection"},
            {"clear_selection", QuaderCommand::EditClearSelection, false, {}},
            {"select_all", QuaderCommand::EditSelectAll, false, {}},
            {"invert_selection", QuaderCommand::EditInvertSelection, false, {}},
            {"copy_selection", QuaderCommand::CopySelection, false, "modeling.copy_selection"},
            {"paste_selection", QuaderCommand::PasteSelection, false, "modeling.paste_selection"},
            {"duplicate_meshes", QuaderCommand::DuplicateSelection, false, "modeling.duplicate_meshes"},
            {"repeat_last_action", QuaderCommand::EditRepeatLastAction, false, "modeling.repeat_last_action"},
            {"assign_material", QuaderCommand::ApplyMaterial, false, "modeling.assign_material"},
            {"apply_active_material", QuaderCommand::ApplyMaterial, false, "modeling.apply_active_material"},
            {"dissolve_edges", QuaderCommand::ModelingDissolveEdges, true, {}},
            {"dissolve_vertices", QuaderCommand::ModelingDissolveVertices, true, {}},
            {"combine_meshes", QuaderCommand::ModelingCombineMeshes, true, {}},
            {"merge_vertices_to_active", QuaderCommand::ModelingMergeVerticesToActive, true, {}},
            {"merge_vertices_to_center", QuaderCommand::ModelingMergeVerticesToCenter, true, {}},
            {"mirror_selection_x", QuaderCommand::ModelingMirrorSelectionX, true, {}},
            {"mirror_selection_y", QuaderCommand::ModelingMirrorSelectionY, true, {}},
            {"mirror_selection_z", QuaderCommand::ModelingMirrorSelectionZ, true, {}},
            {"invert_mesh", QuaderCommand::ModelingInvertMesh, true, {}},
            {"flip_horizontal", QuaderCommand::ModelingFlipHorizontal, true, {}},
            {"flip_vertical", QuaderCommand::ModelingFlipVertical, true, {}},
    }};
    return descriptors;
}

const ModelingActionCommandBridgeDescriptor* modelingActionCommandBridge(std::string_view actionId) {
    for (const ModelingActionCommandBridgeDescriptor& descriptor : modelingActionCommandBridgeCatalog()) {
        if (descriptor.actionId == actionId) {
            return &descriptor;
        }
    }
    return nullptr;
}

std::optional<QuaderCommandId> fixedCommandIdForModelingAction(std::string_view actionId) {
    const ModelingActionCommandBridgeDescriptor* descriptor = modelingActionCommandBridge(actionId);
    return descriptor == nullptr ? std::nullopt : std::optional<QuaderCommandId>(descriptor->commandId);
}

bool modelingActionRegistersDedicatedCommand(std::string_view actionId) {
    const ModelingActionCommandBridgeDescriptor* descriptor = modelingActionCommandBridge(actionId);
    return descriptor == nullptr || descriptor->registersDedicatedCommand;
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

} // namespace

/**
 * Owns command registration, dispatch, and keybinding adaptation for QuaderCommandsApi.
 */
class QuaderCommandsApi::Impl final {
public:
    Impl(QuaderApi& api, QuaderViewportApi& viewports, QuaderToolsApi& tools);

    QuaderApiVoidResult execute(std::string_view commandId);
    QuaderApiVoidResult execute(QuaderCommandId commandId);
    QuaderApiVoidResult executeModelingAction(std::string_view actionId);
    QuaderApiVoidResult executeToolActivation(std::string_view toolId);
    [[nodiscard]] std::vector<QuaderCommandDescriptor> available() const;
    [[nodiscard]] std::string commandIdForModelingAction(std::string_view actionId) const;
    [[nodiscard]] std::string commandIdForToolActivation(std::string_view toolId) const;
    [[nodiscard]] std::string shortcutFor(std::string_view commandId) const;
    [[nodiscard]] std::string shortcutFor(QuaderCommandId commandId) const;
    [[nodiscard]] std::vector<QuaderKeybindingRule> keybindings() const;
    [[nodiscard]] std::vector<QuaderKeybindingConflict> conflictsForShortcut(
            std::string_view shortcut,
            std::string_view when = {}) const;
    [[nodiscard]] bool resolveViewportShortcut(
            ViewportKey key,
            ViewportModifiers modifiers,
            const ViewportToolState& state,
            bool viewportFocused,
            bool textInputActive,
            ViewportShortcut& shortcut,
            QuaderViewportKeybindingContext inputContext = {});
    [[nodiscard]] bool resolveViewportShortcut(
            const QuaderInputChord& chord,
            const ViewportToolState& state,
            bool viewportFocused,
            bool textInputActive,
            ViewportShortcut& shortcut,
            QuaderViewportKeybindingContext inputContext = {});
    [[nodiscard]] bool resolveViewportInputAction(
            const QuaderInputChord& chord,
            const ViewportToolState& state,
            bool viewportFocused,
            bool textInputActive,
            ViewportInputActionKind& action,
            QuaderViewportKeybindingContext inputContext = {});
    [[nodiscard]] QuaderViewportKeybindingDispatch dispatchViewportKeybinding(
            ViewportKey key,
            ViewportModifiers modifiers,
            const ViewportToolState& state,
            bool viewportFocused,
            bool textInputActive,
            QuaderViewportKeybindingContext inputContext = {});
    [[nodiscard]] QuaderViewportKeybindingDispatch dispatchViewportInputBinding(
            const QuaderInputChord& chord,
            const ViewportToolState& state,
            bool viewportFocused,
            bool textInputActive,
            QuaderViewportKeybindingContext inputContext = {});
    void resetPendingKeybindingChord();
    [[nodiscard]] bool hasPendingKeybindingChord() const;
    QuaderApiVoidResult setShortcut(std::string_view commandId, std::string shortcut);
    QuaderApiVoidResult removeShortcut(std::string_view commandId);
    QuaderApiVoidResult resetShortcut(std::string_view commandId);
    QuaderApiVoidResult setKeybindingStoragePath(std::filesystem::path path);

private:
    void registerCommands();
    void registerDefaultKeybindings();
    void registerViewportDefaultKeybindings();
    void registerCommand(
            QuaderCommandId id,
            std::string label,
            std::function<QuaderApiVoidResult()> execute,
            std::string shortcut = {},
            std::function<bool()> enabled = {},
            bool visible = true);
    void registerCommand(
            std::string id,
            std::string label,
            std::function<QuaderApiVoidResult()> execute,
            std::string shortcut = {},
            std::function<bool()> enabled = {},
            bool visible = true);
    void registerAlias(std::string aliasId, QuaderCommandId targetId);
    void registerAlias(std::string aliasId, std::string targetId);
    [[nodiscard]] QuaderApiVoidResult failure(std::string message) const;
    [[nodiscard]] std::string canonicalTargetId(std::string_view commandId) const;

    QuaderApi& api_;
    QuaderViewportApi& viewports_;
    QuaderToolsApi& tools_;
    EditorCommandRegistry registry_;
    QuaderKeybindingService keybindings_;
};

QuaderCommandsApi::Impl::Impl(QuaderApi& api, QuaderViewportApi& viewports, QuaderToolsApi& tools)
    : api_(api)
    , viewports_(viewports)
    , tools_(tools) {
    keybindings_.setStoragePath(defaultKeybindingsPath());
    registerCommands();
    registerDefaultKeybindings();
    (void)keybindings_.loadUserKeybindings();
}

QuaderApiVoidResult QuaderCommandsApi::Impl::execute(QuaderCommandId commandId) {
    return execute(commandId.string());
}

QuaderApiVoidResult QuaderCommandsApi::Impl::executeModelingAction(std::string_view actionId) {
    return execute(commandIdForModelingAction(actionId));
}

QuaderApiVoidResult QuaderCommandsApi::Impl::executeToolActivation(std::string_view toolId) {
    return execute(commandIdForToolActivation(toolId));
}

QuaderApiVoidResult QuaderCommandsApi::Impl::execute(std::string_view commandId) {
    if (std::optional<QuaderApiVoidResult> result = registry_.execute(commandId)) {
        result->revisions = api_.revisions();
        return *result;
    }
    return failure("Unknown command id.");
}

std::vector<QuaderCommandDescriptor> QuaderCommandsApi::Impl::available() const {
    std::vector<QuaderCommandDescriptor> commands = registry_.available();
    for (QuaderCommandDescriptor& command : commands) {
        command.shortcut = keybindings_.shortcutFor(command.id);
    }
    return commands;
}

std::string QuaderCommandsApi::Impl::commandIdForModelingAction(std::string_view actionId) const {
    return quader::commandIdForModelingAction(actionId);
}

std::string QuaderCommandsApi::Impl::commandIdForToolActivation(std::string_view toolId) const {
    return quader::commandIdForToolActivation(toolId);
}

std::string QuaderCommandsApi::Impl::shortcutFor(std::string_view commandId) const {
    if (registry_.contains(commandId) &&
            registry_.canonicalCommandId(commandId) != commandId &&
            registry_.shortcutFor(commandId).empty()) {
        return {};
    }
    return keybindings_.shortcutFor(canonicalTargetId(commandId));
}

std::string QuaderCommandsApi::Impl::shortcutFor(QuaderCommandId commandId) const {
    return shortcutFor(commandId.string());
}

std::vector<QuaderKeybindingRule> QuaderCommandsApi::Impl::keybindings() const {
    return keybindings_.keybindings();
}

std::vector<QuaderKeybindingConflict> QuaderCommandsApi::Impl::conflictsForShortcut(
        std::string_view shortcut,
        std::string_view when) const {
    return keybindings_.conflictsFor(shortcut, when);
}

bool QuaderCommandsApi::Impl::resolveViewportShortcut(
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

bool QuaderCommandsApi::Impl::resolveViewportShortcut(
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

bool QuaderCommandsApi::Impl::resolveViewportInputAction(
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

QuaderViewportKeybindingDispatch QuaderCommandsApi::Impl::dispatchViewportKeybinding(
        ViewportKey key,
        ViewportModifiers modifiers,
        const ViewportToolState& state,
        bool viewportFocused,
        bool textInputActive,
        QuaderViewportKeybindingContext inputContext) {
    const std::optional<QuaderInputChord> chord = inputChordForViewportKey(key, modifiers);
    return chord ? dispatchViewportInputBinding(*chord, state, viewportFocused, textInputActive, inputContext) : QuaderViewportKeybindingDispatch{};
}

QuaderViewportKeybindingDispatch QuaderCommandsApi::Impl::dispatchViewportInputBinding(
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
    if (viewportShortcutForCommandTargetId(dispatch.targetId, shortcut)) {
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

void QuaderCommandsApi::Impl::resetPendingKeybindingChord() {
    keybindings_.resetPendingChord();
}

bool QuaderCommandsApi::Impl::hasPendingKeybindingChord() const {
    return keybindings_.hasPendingChord();
}

QuaderApiVoidResult QuaderCommandsApi::Impl::setShortcut(std::string_view commandId, std::string shortcut) {
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

QuaderApiVoidResult QuaderCommandsApi::Impl::removeShortcut(std::string_view commandId) {
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

QuaderApiVoidResult QuaderCommandsApi::Impl::resetShortcut(std::string_view commandId) {
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

QuaderApiVoidResult QuaderCommandsApi::Impl::setKeybindingStoragePath(std::filesystem::path path) {
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

void QuaderCommandsApi::Impl::registerCommands() {
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
    registerAlias("scene.group_selection", QuaderCommand::GroupSelection);
    registerAlias("scene.ungroup_selection", QuaderCommand::UngroupSelection);
    for (const ModelingActionCommandBridgeDescriptor& descriptor : modelingActionCommandBridgeCatalog()) {
        if (!descriptor.compatibilityAlias.empty()) {
            registerAlias(std::string(descriptor.compatibilityAlias), descriptor.commandId);
        }
    }

    for (const CommandTool* tool : modelingCommandTools()) {
        if (tool == nullptr) {
            continue;
        }
        registerAlias(legacyActionAlias(tool->id()), commandIdForModelingAction(tool->id()));
        if (!modelingActionRegistersDedicatedCommand(tool->id())) {
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

void QuaderCommandsApi::Impl::registerDefaultKeybindings() {
    constexpr std::string_view editableContext = "viewportFocus && !textInputActive && !navigation.active";
    for (const QuaderCommandDescriptor& command : registry_.available()) {
        keybindings_.registerTarget(command.id);
        if (!command.shortcut.empty()) {
            keybindings_.addDefaultKeybinding(command.id, command.shortcut, std::string(editableContext), 0);
        }
    }

    registerViewportDefaultKeybindings();
}

void QuaderCommandsApi::Impl::registerViewportDefaultKeybindings() {
    constexpr std::string_view viewportContext = "viewportFocus && !textInputActive";
    constexpr std::string_view viewportShortcutContext = "viewportFocus && !textInputActive && !navigation.active";

    keybindings_.addDefaultKeybinding(
            viewportInputActionTargetId("cancel_active_tool"),
            "Escape",
            std::string(viewportShortcutContext),
            10);
    keybindings_.addDefaultKeybinding(
            viewportInputActionTargetId("toggle_maximized"),
            "Space",
            std::string(viewportShortcutContext),
            10);
    keybindings_.addDefaultKeybinding(
            viewportInputActionTargetId("delete_popup"),
            "X",
            std::string(viewportShortcutContext),
            10);
    for (const ViewportInputActionBindingDescriptor& descriptor : viewportInputActionBindingCatalog()) {
        keybindings_.addDefaultKeybinding(
                descriptor.targetId,
                descriptor.shortcut,
                std::string(viewportContext),
                20);
    }

    for (const ViewportShortcutBindingDescriptor& descriptor : viewportShortcutBindingCatalog()) {
        const std::optional<std::string> targetId = viewportCommandTargetIdForContractTarget(descriptor.targetId);
        if (!targetId) {
            continue;
        }
        keybindings_.addDefaultKeybinding(
                *targetId,
                descriptor.shortcut,
                std::string(viewportShortcutContext),
                10);
    }

    keybindings_.addDefaultKeybinding(viewportSelectionModeTargetId("vertex"), "1", std::string(viewportShortcutContext), 5);
    keybindings_.addDefaultKeybinding(viewportSelectionModeTargetId("edge"), "2", std::string(viewportShortcutContext), 5);
    keybindings_.addDefaultKeybinding(viewportSelectionModeTargetId("face"), "3", std::string(viewportShortcutContext), 5);
    keybindings_.addDefaultKeybinding(viewportSelectionModeTargetId("object"), "4", std::string(viewportShortcutContext), 5);
    keybindings_.addDefaultKeybinding(viewportSelectionModeTargetId("group"), "5", std::string(viewportShortcutContext), 5);

    const std::vector<ViewportGridShortcutBindingDescriptor> presets = viewportGridShortcutBindingCatalog();
    for (const ViewportGridShortcutBindingDescriptor& preset : presets) {
        keybindings_.addDefaultKeybinding(
                viewportGridPresetTargetId(preset.preset),
                preset.shortcut,
                std::string(viewportShortcutContext),
                10);
    }

    if (presets.empty()) {
        for (int preset = 1; preset <= 10; ++preset) {
            keybindings_.addDefaultKeybinding(
                    viewportGridPresetTargetId(preset),
                    "F" + std::to_string(preset),
                    std::string(viewportShortcutContext),
                    10);
        }
    }
}

void QuaderCommandsApi::Impl::registerCommand(
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

void QuaderCommandsApi::Impl::registerCommand(
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

void QuaderCommandsApi::Impl::registerAlias(std::string aliasId, QuaderCommandId targetId) {
    registerAlias(std::move(aliasId), std::string(targetId.string()));
}

void QuaderCommandsApi::Impl::registerAlias(std::string aliasId, std::string targetId) {
    registry_.registerAlias(std::move(aliasId), std::move(targetId));
}

QuaderApiVoidResult QuaderCommandsApi::Impl::failure(std::string message) const {
    return {
            .ok = false,
            .changed = false,
            .message = std::move(message),
            .mutationKind = QuaderMutationKind::None,
            .revisions = api_.revisions(),
    };
}

std::string QuaderCommandsApi::Impl::canonicalTargetId(std::string_view commandId) const {
    if (registry_.contains(commandId)) {
        return registry_.canonicalCommandId(commandId);
    }
    return std::string(commandId);
}

QuaderCommandsApi::QuaderCommandsApi(QuaderApi& api, QuaderViewportApi& viewports, QuaderToolsApi& tools)
    : impl_(std::make_unique<Impl>(api, viewports, tools)) {}

QuaderCommandsApi::~QuaderCommandsApi() = default;

QuaderApiVoidResult QuaderCommandsApi::execute(QuaderCommandId commandId) {
    return impl_->execute(commandId);
}

QuaderApiVoidResult QuaderCommandsApi::executeModelingAction(std::string_view actionId) {
    return impl_->executeModelingAction(actionId);
}

QuaderApiVoidResult QuaderCommandsApi::executeToolActivation(std::string_view toolId) {
    return impl_->executeToolActivation(toolId);
}

QuaderApiVoidResult QuaderCommandsApi::execute(std::string_view commandId) {
    return impl_->execute(commandId);
}

std::vector<QuaderCommandDescriptor> QuaderCommandsApi::available() const {
    return impl_->available();
}

std::string QuaderCommandsApi::commandIdForModelingAction(std::string_view actionId) const {
    return impl_->commandIdForModelingAction(actionId);
}

std::string QuaderCommandsApi::commandIdForToolActivation(std::string_view toolId) const {
    return impl_->commandIdForToolActivation(toolId);
}

std::string QuaderCommandsApi::shortcutFor(std::string_view commandId) const {
    return impl_->shortcutFor(commandId);
}

std::string QuaderCommandsApi::shortcutFor(QuaderCommandId commandId) const {
    return impl_->shortcutFor(commandId);
}

std::vector<QuaderKeybindingRule> QuaderCommandsApi::keybindings() const {
    return impl_->keybindings();
}

std::vector<QuaderKeybindingConflict> QuaderCommandsApi::conflictsForShortcut(
        std::string_view shortcut,
        std::string_view when) const {
    return impl_->conflictsForShortcut(shortcut, when);
}

bool QuaderCommandsApi::resolveViewportShortcut(
        ViewportKey key,
        ViewportModifiers modifiers,
        const ViewportToolState& state,
        bool viewportFocused,
        bool textInputActive,
        ViewportShortcut& shortcut,
        QuaderViewportKeybindingContext inputContext) {
    return impl_->resolveViewportShortcut(key, modifiers, state, viewportFocused, textInputActive, shortcut, inputContext);
}

bool QuaderCommandsApi::resolveViewportShortcut(
        const QuaderInputChord& chord,
        const ViewportToolState& state,
        bool viewportFocused,
        bool textInputActive,
        ViewportShortcut& shortcut,
        QuaderViewportKeybindingContext inputContext) {
    return impl_->resolveViewportShortcut(chord, state, viewportFocused, textInputActive, shortcut, inputContext);
}

bool QuaderCommandsApi::resolveViewportInputAction(
        const QuaderInputChord& chord,
        const ViewportToolState& state,
        bool viewportFocused,
        bool textInputActive,
        ViewportInputActionKind& action,
        QuaderViewportKeybindingContext inputContext) {
    return impl_->resolveViewportInputAction(chord, state, viewportFocused, textInputActive, action, inputContext);
}

QuaderViewportKeybindingDispatch QuaderCommandsApi::dispatchViewportKeybinding(
        ViewportKey key,
        ViewportModifiers modifiers,
        const ViewportToolState& state,
        bool viewportFocused,
        bool textInputActive,
        QuaderViewportKeybindingContext inputContext) {
    return impl_->dispatchViewportKeybinding(key, modifiers, state, viewportFocused, textInputActive, inputContext);
}

QuaderViewportKeybindingDispatch QuaderCommandsApi::dispatchViewportInputBinding(
        const QuaderInputChord& chord,
        const ViewportToolState& state,
        bool viewportFocused,
        bool textInputActive,
        QuaderViewportKeybindingContext inputContext) {
    return impl_->dispatchViewportInputBinding(chord, state, viewportFocused, textInputActive, inputContext);
}

void QuaderCommandsApi::resetPendingKeybindingChord() {
    impl_->resetPendingKeybindingChord();
}

bool QuaderCommandsApi::hasPendingKeybindingChord() const {
    return impl_->hasPendingKeybindingChord();
}

QuaderApiVoidResult QuaderCommandsApi::setShortcut(std::string_view commandId, std::string shortcut) {
    return impl_->setShortcut(commandId, std::move(shortcut));
}

QuaderApiVoidResult QuaderCommandsApi::removeShortcut(std::string_view commandId) {
    return impl_->removeShortcut(commandId);
}

QuaderApiVoidResult QuaderCommandsApi::resetShortcut(std::string_view commandId) {
    return impl_->resetShortcut(commandId);
}

QuaderApiVoidResult QuaderCommandsApi::setKeybindingStoragePath(std::filesystem::path path) {
    return impl_->setKeybindingStoragePath(std::move(path));
}

} // namespace quader
