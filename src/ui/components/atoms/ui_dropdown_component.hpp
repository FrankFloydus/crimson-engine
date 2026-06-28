////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/core/ui_component.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace quader {

inline constexpr std::string_view kUIDropdownDataKey = "ui-dropdown";
inline constexpr std::string_view kUIDropdownToggleDataKey = "ui-dropdown-toggle";
inline constexpr std::string_view kUIDropdownOptionDataKey = "ui-dropdown-option";

inline constexpr std::string_view kUIDropdownAttribute = "data-ui-dropdown";
inline constexpr std::string_view kUIDropdownToggleAttribute = "data-ui-dropdown-toggle";
inline constexpr std::string_view kUIDropdownOptionAttribute = "data-ui-dropdown-option";

/**
 * Stores one selected option event emitted by a retained dropdown.
 */
struct UIDropdownSelection {
    std::string dropdownId;
    std::string optionId;
};

/**
 * Stores one retained dropdown option.
 */
struct UIDropdownOption {
    std::string id;
    std::string label;
    std::string keybinding;
    bool selected = false;
    bool disabled = false;
};

/**
 * Stores the retained dropdown presentation model.
 */
struct UIDropdownModel {
    std::string elementId;
    std::string selectedLabel;
    std::vector<std::string> classes;
    std::vector<UIDropdownOption> options;
    bool open = false;
    bool disabled = false;
};

/**
 * Retained dropdown component with an input control and optional option surface.
 */
class UIDropdownComponent final : public UIComponent {
public:
    explicit UIDropdownComponent(UIDropdownModel model);

    [[nodiscard]] UIElement render() const override;

private:
    [[nodiscard]] bool hasOptionKeybindings() const;
    [[nodiscard]] UIElement renderInput() const;
    [[nodiscard]] UIElement renderSurface(bool showKeybindings) const;
    [[nodiscard]] UIElement renderOption(const UIDropdownOption& option, bool showKeybindings) const;

    UIDropdownModel model_;
};

[[nodiscard]] std::string uiDropdownInputElementId(std::string_view elementId);
[[nodiscard]] std::string uiDropdownSurfaceElementId(std::string_view elementId);
[[nodiscard]] std::string uiDropdownOptionElementId(std::string_view elementId, std::string_view optionId);

} // namespace quader
