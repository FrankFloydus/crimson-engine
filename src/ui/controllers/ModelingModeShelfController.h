////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/ModelingTypes.h"
#include "ui/components/organisms/ModelingModeShelfComponent.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace quader {

class LocalizationService;
class QuaderEditorApi;

/**
 * Owns transient modeling shelf UI state and projects editor actions into shelf state.
 */
class ModelingModeShelfController final {
public:
    [[nodiscard]] ModelingModeShelfState buildState(
            const LocalizationService& localization) const;
    bool syncEditorState(
            const QuaderEditorApi& editor,
            const LocalizationService& localization);

    [[nodiscard]] std::vector<ModelingModeShelfControl> controls(
            const LocalizationService& localization) const;

    [[nodiscard]] std::optional<std::string_view> controlIdForSelectionMode(
            ModelingSelectionMode mode) const;
    [[nodiscard]] std::optional<ModelingSelectionMode> selectionModeForControlId(
            std::string_view controlId) const;
    [[nodiscard]] std::optional<std::string> activeControlId() const;

    [[nodiscard]] const std::vector<std::string>& overflowControlIds() const {
        return overflowControlIds_;
    }

    bool setOverflowControlIds(std::vector<std::string> controlIds);
    bool activateControl(std::string_view controlId);
    void activateCommand(std::string_view commandId);
    bool syncSelectionSummary(ModelingSelectionSummary summary);

    [[nodiscard]] std::optional<ModelingSelectionMode> takePendingSelectionMode();
    [[nodiscard]] std::optional<std::string> takePendingCommandId();

private:
    ModelingSelectionSummary selectionSummary_;
    std::vector<ModelingModeShelfCommand> activeStripCommands_;
    std::vector<std::string> overflowControlIds_;
    std::optional<ModelingSelectionMode> pendingSelectionMode_;
    std::optional<std::string> pendingCommandId_;
    bool overflowMenuOpen_ = false;
};

} // namespace quader
