////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/components/atoms/UIToolbarButtonComponent.h"
#include "ui/core/UIComponent.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace quader {

/**
 * Identifies how one toolbar control interacts with the shelf content strip.
 */
enum class ModelingModeShelfControlBehavior {
    OpensTabContent,
    Action,
};

/**
 * Describes when a modeling shelf tab should project its label alongside other
 * content such as icons.
 */
enum class ModelingModeShelfControlLabelVisibility {
    Hidden,
    Active,
    Always,
};

/**
 * Describes one normal toolbar button rendered by the modeling shelf.
 */
struct ModelingModeShelfControl {
    std::string id;
    std::string label;
    std::optional<UIIconId> icon;
    ModelingModeShelfControlLabelVisibility labelVisibility = ModelingModeShelfControlLabelVisibility::Always;
    ModelingModeShelfControlBehavior behavior = ModelingModeShelfControlBehavior::Action;
};

/**
 * Describes one command button rendered inside the active shelf strip.
 */
struct ModelingModeShelfCommand {
    std::string id;
    std::string actionId;
    std::string commandId;
    std::string label;
    std::string secondaryText;
    std::string disabledReason;
    std::optional<UIIconId> icon;
    bool disabled = false;
};

/**
 * Stores the complete retained state needed to render the shelf.
 */
struct ModelingModeShelfState {
    std::vector<ModelingModeShelfControl> controls;
    std::vector<std::string> overflowControlIds;
    std::vector<ModelingModeShelfCommand> activeStripCommands;
    std::optional<std::string> activeTabControlId;
    std::string overflowButtonLabel = "More";
    bool overflowMenuOpen = false;
};

[[nodiscard]] bool modelingModeShelfControlOpensTabContent(const ModelingModeShelfControl& control);
[[nodiscard]] std::string modelingModeShelfControlButtonElementId(std::string_view controlId);
[[nodiscard]] std::string modelingModeShelfControlMeasureElementId(std::string_view controlId);
[[nodiscard]] std::string_view modelingModeShelfOverflowToggleControlId();
[[nodiscard]] std::string_view modelingModeShelfOverflowButtonElementId();
[[nodiscard]] std::string_view modelingModeShelfOverflowMeasureElementId();

/**
 * Retained toolbar row for normal buttons that may open shelf tab content.
 */
class ModelingModeShelfToolbarComponent final : public UIComponent {
public:
    explicit ModelingModeShelfToolbarComponent(ModelingModeShelfState state);

    [[nodiscard]] UIElement render() const override;

private:
    ModelingModeShelfState state_;
};

/**
 * Composes the fixed modeling shelf mounted above the viewport column.
 */
class ModelingModeShelfComponent final : public UIComponent {
public:
    explicit ModelingModeShelfComponent(ModelingModeShelfState state);

    [[nodiscard]] UIElement render() const override;

private:
    ModelingModeShelfState state_;
};

[[nodiscard]] std::string buildModelingModeShelfRml(ModelingModeShelfState state);

} // namespace quader
