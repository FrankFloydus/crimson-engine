////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/history/EditorHistoryService.h"
#include "editor/commands/ModelingCommandDispatcher.h"

#include <string>
#include <string_view>
#include <utility>

namespace quader {

class EditorSelectionService;

/**
 * Coordinates Modeling Command Execution Service responsibilities for the editor coordination layer.
 */
class ModelingCommandExecutionService final {
public:
    ModelingCommandResult execute(
            EditorHistoryService& history,
            ModelingSessionService& modeling,
            EditorSelectionService& selection,
            std::string_view toolId);

    ModelingCommandResult executeUntracked(
            ModelingSessionService& modeling,
            std::string_view toolId,
            EditorSelectionService* selection = nullptr);

    ModelingCommandResult beginPreview(
            EditorHistoryService& history,
            ModelingSessionService& modeling,
            EditorSelectionService& selection,
            std::string_view actionId);

    ModelingCommandResult beginPreviewUntracked(
            ModelingSessionService& modeling,
            std::string_view actionId);

    ModelingCommandResult commitPreview(
            EditorHistoryService& history,
            ModelingSessionService& modeling,
            std::string label);

    ModelingCommandResult cancelPreview(ModelingSessionService& modeling);

    [[nodiscard]] ModelingCommandAvailability availability(
            ModelingSessionService& modeling,
            std::string_view toolId,
            EditorSelectionService* selection = nullptr) const;

    [[nodiscard]] ModelingCommandHistoryPolicy historyPolicy(std::string_view toolId) const;

private:
    ModelingCommandDispatcher dispatcher_;
};

} // namespace quader
