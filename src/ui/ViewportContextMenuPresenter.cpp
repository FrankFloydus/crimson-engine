////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/ViewportContextMenuPresenter.h"

#include "api/QuaderCommandIds.h"
#include "api/QuaderEditorApi.h"
#include "core/LocalizationService.h"
#include "core/Logger.h"
#include "editor/modeling/services/ModelingActionCatalog.h"
#include "editor/modeling/services/ModelingSessionService.h"
#include "editor/modeling/services/SelectionModeAdapter.h"
#include "editor/commands/ModelingCommandToolCatalog.h"
#include "viewport/ViewportInputRouter.h"

#include <imgui.h>

#include <string>
#include <string_view>
#include <vector>

namespace quader {
namespace {

bool isGenericContextAction(std::string_view actionId) {
    return actionId == "delete_selection" || actionId == "duplicate_meshes";
}

std::string contextMenuCommandId(std::string_view actionId) {
    if (actionId == "delete_selection") {
        return std::string(QuaderCommand::DeleteSelection.string());
    }
    if (actionId == "duplicate_meshes") {
        return std::string(QuaderCommand::DuplicateSelection.string());
    }

    std::string commandId = "modeling.action.";
    commandId.append(actionId);
    return commandId;
}

bool shouldCloseOperationsPopupForMouseClick() {
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        return true;
    }
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        return false;
    }
    return !ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByPopup);
}

} // namespace

void ViewportContextMenuPresenter::drawOperationsPopup(
        QuaderEditorApi& editor,
        ModelingSessionService& modeling,
        ViewportInputRouter& inputRouter,
        LocalizationService& localization,
        bool modalToolOwnsViewport) const {
    if (!ImGui::BeginPopup("ViewportOperationsContext")) {
        return;
    }

    if (shouldCloseOperationsPopupForMouseClick()) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    const ModelingActionQuery query{
            .surface = ModelingActionSurface::ViewportContextMenu,
            .modalToolOwnsViewport = modalToolOwnsViewport,
            .includeDisabled = true,
            .includeHidden = false,
    };
    const std::vector<ModelingActionItem> actions = modeling.contextActions(query);
    bool separatedGenericActions = false;
    for (const ModelingActionItem& action : actions) {
        if (!separatedGenericActions && isGenericContextAction(action.actionId)) {
            ImGui::Separator();
            separatedGenericActions = true;
        }
        const bool enabled = action.visibility == ModelingActionVisibility::Enabled;
        std::string labelKey = "modeling.action.";
        labelKey.append(action.actionId);
        labelKey.append(".label");
        std::string stableId = "modeling.action.";
        stableId.append(action.actionId);
        const std::string label = localization.imguiLabel(labelKey, stableId, action.label);
        if (ImGui::MenuItem(label.c_str(), nullptr, false, enabled)) {
            const std::string commandId = contextMenuCommandId(action.actionId);
            const QuaderApiVoidResult result = editor.commands().execute(commandId);
            if (!result.ok) {
                LOG_ERROR("Operation '{}' failed: {}", action.actionId, result.message.empty() ? "Operation failed." : result.message);
            }
            inputRouter.setSelectionMode(appSelectionModeToViewport(editor.selection().summary().mode));
            ImGui::CloseCurrentPopup();
        }
        if (!enabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !action.disabledReason.empty()) {
            ImGui::SetTooltip("%s", action.disabledReason.c_str());
        }
    }

    ImGui::EndPopup();
}

} // namespace quader
