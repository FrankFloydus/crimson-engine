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
#include "editor/actions/ModelingCommandToolCatalog.h"
#include "viewport/modes/ViewportToolCatalog.h"

#include <functional>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

namespace quader {
namespace {

std::string commandIdForModelingTool(std::string_view toolId) {
    if (toolId == "delete_selection") {
        return std::string(QuaderCommand::DeleteSelection.string());
    }
    if (toolId == "copy_selection") {
        return std::string(QuaderCommand::CopySelection.string());
    }
    if (toolId == "paste_selection") {
        return std::string(QuaderCommand::PasteSelection.string());
    }
    if (toolId == "duplicate_meshes") {
        return std::string(QuaderCommand::DuplicateSelection.string());
    }
    if (toolId == "repeat_last_action") {
        return std::string(QuaderCommand::EditRepeatLastAction.string());
    }
    if (toolId == "assign_material" || toolId == "apply_active_material") {
        return std::string(QuaderCommand::ApplyMaterial.string());
    }

    std::string commandId = "modeling.";
    commandId.append(toolId);
    return commandId;
}

} // namespace

QuaderCommandsApi::QuaderCommandsApi(QuaderApi& api, QuaderViewportApi& viewports, QuaderToolsApi& tools)
    : api_(api)
    , viewports_(viewports)
    , tools_(tools) {
    registerCommands();
}

QuaderApiVoidResult QuaderCommandsApi::execute(QuaderCommandId commandId) {
    return execute(commandId.string());
}

QuaderApiVoidResult QuaderCommandsApi::execute(std::string_view commandId) {
    if (std::optional<QuaderApiVoidResult> result = registry_.execute(commandId)) {
        return *result;
    }
    return failure("Unknown command id.");
}

std::vector<QuaderCommandDescriptor> QuaderCommandsApi::available() const {
    return registry_.available();
}

std::string QuaderCommandsApi::shortcutFor(std::string_view commandId) const {
    return registry_.shortcutFor(commandId);
}

std::string QuaderCommandsApi::shortcutFor(QuaderCommandId commandId) const {
    return shortcutFor(commandId.string());
}

void QuaderCommandsApi::registerCommands() {
    registerCommand(
            "edit.undo",
            "Undo",
            [this] { return api_.history().undo(); },
            "Ctrl+Z",
            [this] { return api_.history().canUndo(); });
    registerCommand(
            "edit.redo",
            "Redo",
            [this] { return api_.history().redo(); },
            "Ctrl+Y",
            [this] { return api_.history().canRedo(); });
    registerCommand(
            "edit.repeat_last_action",
            "Repeat Last Action",
            [this] { return api_.history().repeatLastAction(); },
            "Ctrl+R");
    registerCommand(
            "edit.clear_selection",
            "Clear Selection",
            [this] { return api_.selection().clear(); });
    registerCommand(
            "edit.select_all",
            "Select All",
            [this] { return api_.selection().selectAll(); },
            "Ctrl+A");
    registerCommand(
            "edit.invert_selection",
            "Invert Selection",
            [this] { return api_.selection().invert(); },
            "Ctrl+I");
    registerCommand(
            "edit.delete",
            "Delete Selection",
            [this] { return api_.modeling().executeTool("delete_selection"); });
    registerCommand(
            "edit.copy",
            "Copy Selection",
            [this] { return api_.modeling().executeTool("copy_selection"); },
            "Ctrl+C");
    registerCommand(
            "edit.paste",
            "Paste Selection",
            [this] { return api_.modeling().executeTool("paste_selection"); },
            "Ctrl+V");
    registerCommand(
            "edit.duplicate",
            "Duplicate Selection",
            [this] { return api_.modeling().executeTool("duplicate_meshes"); },
            "Ctrl+D");
    registerCommand(
            "edit.group",
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
            "edit.ungroup",
            "Ungroup Selection",
            [this] { return api_.scene().ungroupSelection(); },
            "Shift+Ctrl+G");
    registerCommand(
            "edit.apply_material",
            "Apply Material",
            [this] { return api_.modeling().executeTool("assign_material"); });
    registerCommand(
            "viewport.frame_selection",
            "Frame Selection",
            [this] { return viewports_.frameSelection(); },
            "Home");
    registerCommand(
            "viewport.focus_origin",
            "Focus Origin",
            [this] { return viewports_.focusOrigin(); },
            "Shift+Home");

    registerAlias("history.undo", "edit.undo");
    registerAlias("history.redo", "edit.redo");
    registerAlias("history.repeat_last_action", "edit.repeat_last_action");
    registerAlias("selection.clear", "edit.clear_selection");
    registerAlias("selection.select_all", "edit.select_all");
    registerAlias("selection.invert", "edit.invert_selection");
    registerAlias("modeling.delete_selection", "edit.delete");
    registerAlias("modeling.copy_selection", "edit.copy");
    registerAlias("modeling.paste_selection", "edit.paste");
    registerAlias("modeling.duplicate_meshes", "edit.duplicate");
    registerAlias("scene.group_selection", "edit.group");
    registerAlias("scene.ungroup_selection", "edit.ungroup");
    registerAlias("modeling.repeat_last_action", "edit.repeat_last_action");
    registerAlias("modeling.assign_material", "edit.apply_material");
    registerAlias("modeling.apply_active_material", "edit.apply_material");

    const std::unordered_set<std::string_view> neutralModelingTools{
            "delete_selection",
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
        registerAlias("modeling.action." + std::string(tool->id()), commandIdForModelingTool(tool->id()));
        if (neutralModelingTools.contains(tool->id())) {
            continue;
        }
        std::string id = commandIdForModelingTool(tool->id());
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
        std::string id = "tools.activate.";
        id.append(tool->id());
        registerCommand(
                id,
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

} // namespace quader
