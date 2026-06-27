////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/components/atoms/ui_toolbar_button_component.hpp"
#include "ui/core/ui_component.hpp"

#include <optional>
#include <span>
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

inline constexpr std::string_view kModelingModeShelfControlDataKey = "modeling-mode-shelf-control";
inline constexpr std::string_view kModelingModeShelfControlBehaviorDataKey = "modeling-mode-shelf-control-behavior";
inline constexpr std::string_view kModelingModeShelfOverflowToggleDataKey = "modeling-mode-shelf-overflow-toggle";
inline constexpr std::string_view kModelingModeShelfCommandDataKey = "modeling-mode-shelf-command";
inline constexpr std::string_view kModelingModeShelfActionDataKey = "modeling-mode-shelf-action";
inline constexpr std::string_view kModelingModeShelfDisabledReasonDataKey = "modeling-mode-shelf-disabled-reason";
inline constexpr std::string_view kModelingModeShelfActiveControlDataKey = "modeling-mode-shelf-active-control";
inline constexpr std::string_view kModelingModeShelfControlAttribute = "data-modeling-mode-shelf-control";
inline constexpr std::string_view kModelingModeShelfOverflowToggleAttribute = "data-modeling-mode-shelf-overflow-toggle";
inline constexpr std::string_view kModelingModeShelfCommandAttribute = "data-modeling-mode-shelf-command";

[[nodiscard]] bool modelingModeShelfControlOpensTabContent(const ModelingModeShelfControl& control);
[[nodiscard]] std::string_view modelingModeShelfControlBehaviorName(ModelingModeShelfControlBehavior behavior);
[[nodiscard]] std::string modelingModeShelfControlButtonElementId(std::string_view controlId);
[[nodiscard]] std::string modelingModeShelfControlMeasureElementId(std::string_view controlId);
[[nodiscard]] std::string_view modelingModeShelfOverflowToggleControlId();
[[nodiscard]] std::string_view modelingModeShelfOverflowButtonElementId();
[[nodiscard]] std::string_view modelingModeShelfOverflowMeasureElementId();
[[nodiscard]] std::span<const std::string_view> modelingModeShelfPointerBypassElementIds();

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
