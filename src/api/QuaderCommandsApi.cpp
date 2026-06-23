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
    return registry_.available();
}

std::string QuaderCommandsApi::commandIdForModelingAction(std::string_view actionId) const {
    return quader::commandIdForModelingAction(actionId);
}

std::string QuaderCommandsApi::commandIdForToolActivation(std::string_view toolId) const {
    return quader::commandIdForToolActivation(toolId);
}

std::string QuaderCommandsApi::shortcutFor(std::string_view commandId) const {
    return registry_.shortcutFor(commandId);
}

std::string QuaderCommandsApi::shortcutFor(QuaderCommandId commandId) const {
    return shortcutFor(commandId.string());
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

} // namespace quader
