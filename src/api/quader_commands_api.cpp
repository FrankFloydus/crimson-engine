////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_commands_api.hpp"

#include "api/quader_api.hpp"
#include "api/quader_project_api.hpp"
#include "api/quader_tools_api.hpp"
#include "api/quader_viewport_api.hpp"
#include "core/public_command_targets.hpp"
#include "editor/commands/editor_command_registry.hpp"
#include "editor/commands/modeling_command_tool_catalog.hpp"
#include "editor/input/quader_keybinding_service.hpp"
#include "editor/input/viewport_input_binding_adapter.hpp"
#include "editor/tools/viewport_tool_catalog.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"

#include <cstdlib>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace quader {
namespace {

std::string commandIdForModelingAction(std::string_view actionId) {
    if (const CommandTool* tool = findModelingCommandTool(actionId)) {
        return tool->publicCommandTargetId();
    }

    return quaderModelingCommandTargetId(actionId);
}

bool modelingActionRegistersDedicatedCommand(const CommandTool& tool) {
    return tool.registersPublicCommand();
}

std::string commandIdForToolActivation(std::string_view toolId) {
    return quaderToolActivationCommandTargetId(toolId);
}

std::filesystem::path defaultKeybindingsPath() {
    if (const char* localAppData = std::getenv("LOCALAPPDATA")) {
        return std::filesystem::path(localAppData) / "Quader" / "keybindings.json";
    }
    return std::filesystem::path("keybindings.json");
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
    context.set("operationPreview.active", inputContext.operationPreviewActive);
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
    Impl(QuaderApi& api, QuaderProjectApi& project, QuaderViewportApi& viewports, QuaderToolsApi& tools);

    QuaderApiVoidResult execute(std::string_view commandId);
    QuaderApiVoidResult execute(QuaderCommandId commandId);
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
    QuaderApiVoidResult setShortcut(QuaderCommandId commandId, std::string shortcut);
    QuaderApiVoidResult removeShortcut(std::string_view commandId);
    QuaderApiVoidResult removeShortcut(QuaderCommandId commandId);
    QuaderApiVoidResult resetShortcut(std::string_view commandId);
    QuaderApiVoidResult resetShortcut(QuaderCommandId commandId);
    QuaderApiVoidResult setKeybindingStoragePath(std::filesystem::path path);

private:
    void registerCommands();
    void registerDefaultKeybindings();
    void registerViewportDefaultKeybindings();
    void registerViewportViewCommand(
            QuaderCommandId id,
            std::string label,
            ViewportView view,
            std::string shortcut = {});
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
    [[nodiscard]] QuaderApiVoidResult failure(std::string message) const;

    QuaderApi& api_;
    QuaderProjectApi& project_;
    QuaderViewportApi& viewports_;
    QuaderToolsApi& tools_;
    EditorCommandRegistry registry_;
    QuaderKeybindingService keybindings_;
};

QuaderCommandsApi::Impl::Impl(QuaderApi& api, QuaderProjectApi& project, QuaderViewportApi& viewports, QuaderToolsApi& tools)
    : api_(api)
    , project_(project)
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
    return keybindings_.shortcutFor(std::string(commandId));
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
    const std::optional<QuaderInputChord> chord = quaderInputChordForViewportKey(key, modifiers);
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
    const std::optional<QuaderInputChord> chord = quaderInputChordForViewportKey(key, modifiers);
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

    if (registry_.contains(dispatch.targetId) && !viewportTargetRequiresRouterDispatch(dispatch.targetId)) {
        return {
                .kind = QuaderViewportKeybindingDispatch::Kind::Command,
                .commandId = dispatch.targetId,
        };
    }

    ViewportShortcut shortcut;
    if (viewportRouterShortcutForTargetId(dispatch.targetId, shortcut)) {
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
    const std::string targetId(commandId);
    std::string when;
    for (const QuaderKeybindingRule& rule : keybindings_.keybindings()) {
        if (rule.targetId == targetId) {
            when = rule.when;
            break;
        }
    }
    const QuaderKeybindingMutationResult mutation =
            keybindings_.setUserShortcut(targetId, std::move(shortcut), when);
    if (!mutation.ok) {
        return failure(mutation.message);
    }
    return {
            .ok = true,
            .changed = mutation.changed,
            .message = mutation.message,
            .mutationKind = QuaderMutationKind::None,
            .revisions = api_.revisions(),
    };
}

QuaderApiVoidResult QuaderCommandsApi::Impl::setShortcut(QuaderCommandId commandId, std::string shortcut) {
    return setShortcut(commandId.string(), std::move(shortcut));
}

QuaderApiVoidResult QuaderCommandsApi::Impl::removeShortcut(std::string_view commandId) {
    const std::string targetId(commandId);
    const QuaderKeybindingMutationResult mutation = keybindings_.removeUserShortcut(targetId);
    if (!mutation.ok) {
        return failure(mutation.message);
    }
    return {
            .ok = true,
            .changed = mutation.changed,
            .message = mutation.message,
            .mutationKind = QuaderMutationKind::None,
            .revisions = api_.revisions(),
    };
}

QuaderApiVoidResult QuaderCommandsApi::Impl::removeShortcut(QuaderCommandId commandId) {
    return removeShortcut(commandId.string());
}

QuaderApiVoidResult QuaderCommandsApi::Impl::resetShortcut(std::string_view commandId) {
    const std::string targetId(commandId);
    const QuaderKeybindingMutationResult mutation = keybindings_.resetUserShortcut(targetId);
    if (!mutation.ok) {
        return failure(mutation.message);
    }
    return {
            .ok = true,
            .changed = mutation.changed,
            .message = mutation.message,
            .mutationKind = QuaderMutationKind::None,
            .revisions = api_.revisions(),
    };
}

QuaderApiVoidResult QuaderCommandsApi::Impl::resetShortcut(QuaderCommandId commandId) {
    return resetShortcut(commandId.string());
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
    auto registerModelingToolPublicCommand = [this](
            QuaderCommandId commandId,
            std::string_view toolId,
            std::string fallbackLabel,
            std::string shortcut = {}) {
        const CommandTool* tool = findModelingCommandTool(toolId);
        const std::string label = tool != nullptr && !tool->label().empty()
                ? std::string(tool->label())
                : std::move(fallbackLabel);
        registerCommand(
                commandId,
                label,
                [this, targetToolId = std::string(toolId)] {
                    return api_.modeling().executeTool(targetToolId);
                },
                std::move(shortcut),
                [this, targetToolId = std::string(toolId)] {
                    return api_.modeling().canExecuteTool(targetToolId);
                });
    };

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
    registerModelingToolPublicCommand(
            QuaderCommand::DeleteSelection,
            "delete_selection",
            "Delete Selection");
    registerModelingToolPublicCommand(
            QuaderCommand::CopySelection,
            "copy_selection",
            "Copy Selection",
            "Ctrl+C");
    registerModelingToolPublicCommand(
            QuaderCommand::PasteSelection,
            "paste_selection",
            "Paste Selection",
            "Ctrl+V");
    registerModelingToolPublicCommand(
            QuaderCommand::DuplicateSelection,
            "duplicate_meshes",
            "Duplicate Selection",
            "Ctrl+D");
    registerCommand(
            QuaderCommand::GroupSelection,
            "Group Selection",
            [this] {
                const QuaderApiResult<QuaderEntityRef> result = api_.scene().groupSelection();
                return quaderApiStatusFromResult(result);
            },
            "Ctrl+G");
    registerCommand(
            QuaderCommand::UngroupSelection,
            "Ungroup Selection",
            [this] { return api_.scene().ungroupSelection(); },
            "Shift+Ctrl+G");
    registerModelingToolPublicCommand(
            QuaderCommand::ApplyMaterial,
            "apply_active_material",
            "Apply Material");
    registerCommand(
            QuaderCommand::ModelingApplyActivePreview,
            "Apply Active Preview",
            [this] { return api_.modeling().commitPreview("Apply Tool Preview"); });
    registerCommand(
            QuaderCommand::ModelingCancelActivePreview,
            "Cancel Active Preview",
            [this] { return api_.modeling().cancelPreview(); });
    registerCommand(
            QuaderCommand::ProjectNew,
            "New Project",
            [this] { return project_.newProject(); },
            "Ctrl+N");
    registerCommand(
            QuaderCommand::ProjectOpen,
            "Open Project",
            [this] { return project_.open({}); },
            "Ctrl+O");
    registerCommand(
            QuaderCommand::ProjectSave,
            "Save Project",
            [this] { return project_.save(); },
            "Ctrl+S");
    registerCommand(
            QuaderCommand::ProjectSaveAs,
            "Save Project As",
            [this] { return project_.saveAs({}); });
    registerCommand(
            QuaderCommand::ViewportFrameSelection,
            "Frame Selection",
            [this] { return viewports_.frameSelection(); });
    registerCommand(
            QuaderCommand::ViewportFocusOrigin,
            "Focus Origin",
            [this] { return viewports_.focusOrigin(); });
    registerCommand(
            QuaderCommand::ViewportSpawnWindow,
            "New Viewport Window",
            [this] { return viewports_.spawnWindow(); },
            "F12");
    registerViewportViewCommand(
            QuaderCommand::ViewportViewPerspective,
            "Perspective View",
            ViewportView::Perspective,
            "P");
    registerViewportViewCommand(
            QuaderCommand::ViewportViewTop,
            "Top View",
            ViewportView::Top);
    registerViewportViewCommand(
            QuaderCommand::ViewportViewBottom,
            "Bottom View",
            ViewportView::Bottom);
    registerViewportViewCommand(
            QuaderCommand::ViewportViewLeft,
            "Left View",
            ViewportView::Left);
    registerViewportViewCommand(
            QuaderCommand::ViewportViewRight,
            "Right View",
            ViewportView::Side);
    registerViewportViewCommand(
            QuaderCommand::ViewportViewFront,
            "Front View",
            ViewportView::Front);
    registerViewportViewCommand(
            QuaderCommand::ViewportViewBack,
            "Back View",
            ViewportView::Back);

    for (const CommandTool* tool : modelingCommandTools()) {
        if (tool == nullptr) {
            continue;
        }
        const std::string commandId = commandIdForModelingAction(tool->id());
        if (!modelingActionRegistersDedicatedCommand(*tool)) {
            continue;
        }
        registerCommand(
                commandId,
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

}

void QuaderCommandsApi::Impl::registerViewportViewCommand(
        QuaderCommandId id,
        std::string label,
        ViewportView view,
        std::string shortcut) {
    registerCommand(
            id,
            std::move(label),
            [this, view] {
                return viewports_.setView(ViewportPaneId::Perspective, view);
            },
            std::move(shortcut));
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
    constexpr std::string_view operationPreviewContext =
            "viewportFocus && !textInputActive && !navigation.active && operationPreview.active";

    keybindings_.addDefaultKeybinding(
            std::string(QuaderCommand::ModelingApplyActivePreview.string()),
            "Space",
            std::string(operationPreviewContext),
            20);
    keybindings_.addDefaultKeybinding(
            std::string(QuaderCommand::ModelingCancelActivePreview.string()),
            "Escape",
            std::string(operationPreviewContext),
            20);

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
                descriptor.when.empty() ? std::string(viewportContext) : std::string(descriptor.when),
                descriptor.weight);
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

QuaderApiVoidResult QuaderCommandsApi::Impl::failure(std::string message) const {
    return {
            .ok = false,
            .changed = false,
            .message = std::move(message),
            .mutationKind = QuaderMutationKind::None,
            .revisions = api_.revisions(),
    };
}

QuaderCommandsApi::QuaderCommandsApi(QuaderApi& api, QuaderProjectApi& project, QuaderViewportApi& viewports, QuaderToolsApi& tools)
    : impl_(std::make_unique<Impl>(api, project, viewports, tools)) {}

QuaderCommandsApi::~QuaderCommandsApi() = default;

QuaderApiVoidResult QuaderCommandsApi::execute(QuaderCommandId commandId) {
    return impl_->execute(commandId);
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

QuaderApiVoidResult QuaderCommandsApi::setShortcut(QuaderCommandId commandId, std::string shortcut) {
    return impl_->setShortcut(commandId, std::move(shortcut));
}

QuaderApiVoidResult QuaderCommandsApi::removeShortcut(std::string_view commandId) {
    return impl_->removeShortcut(commandId);
}

QuaderApiVoidResult QuaderCommandsApi::removeShortcut(QuaderCommandId commandId) {
    return impl_->removeShortcut(commandId);
}

QuaderApiVoidResult QuaderCommandsApi::resetShortcut(std::string_view commandId) {
    return impl_->resetShortcut(commandId);
}

QuaderApiVoidResult QuaderCommandsApi::resetShortcut(QuaderCommandId commandId) {
    return impl_->resetShortcut(commandId);
}

QuaderApiVoidResult QuaderCommandsApi::setKeybindingStoragePath(std::filesystem::path path) {
    return impl_->setKeybindingStoragePath(std::move(path));
}

} // namespace quader
