////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/components/atoms/ui_dropdown_component.hpp"

#include "ui/core/ui_icon_registry.hpp"

#include <algorithm>
#include <utility>

namespace quader {
namespace {

std::string selectedLabelForModel(const UIDropdownModel& model) {
    if (!model.selectedLabel.empty()) {
        return model.selectedLabel;
    }

    const auto selected = std::find_if(model.options.begin(), model.options.end(), [](const UIDropdownOption& option) {
        return option.selected;
    });
    return selected == model.options.end() ? std::string{} : selected->label;
}

} // namespace

UIDropdownComponent::UIDropdownComponent(UIDropdownModel model)
    : model_(std::move(model)) {}

UIElement UIDropdownComponent::render() const {
    const bool showKeybindings = hasOptionKeybindings();
    UIElement dropdown = UIElement::div()
            .id(model_.elementId)
            .addClass("ui-dropdown")
            .data(kUIDropdownDataKey, model_.elementId);
    for (const std::string& className : model_.classes) {
        dropdown.addClass(className);
    }
    dropdown.child(renderInput());
    if (model_.open) {
        dropdown.child(renderSurface(showKeybindings));
    }
    return dropdown;
}

bool UIDropdownComponent::hasOptionKeybindings() const {
    return std::any_of(model_.options.begin(), model_.options.end(), [](const UIDropdownOption& option) {
        return !option.keybinding.empty();
    });
}

UIElement UIDropdownComponent::renderInput() const {
    UIElement input = UIElement::button()
            .id(uiDropdownInputElementId(model_.elementId))
            .addClass("ui-dropdown-input")
            .addClass("bg-gray-950")
            .addClass("border-1")
            .addClass("border-gray-700")
            .addClass("rounded-6")
            .addClass("text-sm")
            .addClass("leading-sm")
            .addClass("text-gray-100")
            .addClass("hover-bg-purple-800")
            .addClass("hover-border-purple-300")
            .data(kUIDropdownDataKey, model_.elementId)
            .data(kUIDropdownToggleDataKey, model_.elementId)
            .attr("aria-haspopup", "listbox")
            .attr("aria-expanded", model_.open ? "true" : "false");
    if (model_.disabled) {
        input.attr("disabled", "disabled");
    }

    input.child(UIElement::span()
            .addClass("ui-dropdown-input-label")
            .text(selectedLabelForModel(model_)));
    input.child(UIIconRegistry::instance().renderIcon(UIIconId::ArrowDown, "ui-dropdown-input-icon"));
    return input;
}

UIElement UIDropdownComponent::renderSurface(bool showKeybindings) const {
    UIElement surface = UIElement::div()
            .id(uiDropdownSurfaceElementId(model_.elementId))
            .addClass("ui-dropdown-surface")
            .addClass("bg-gray-950")
            .addClass("border-1")
            .addClass("border-gray-700")
            .addClass("rounded-6")
            .data(kUIDropdownDataKey, model_.elementId)
            .attr("role", "listbox");
    for (const UIDropdownOption& option : model_.options) {
        surface.child(renderOption(option, showKeybindings));
    }
    return surface;
}

UIElement UIDropdownComponent::renderOption(const UIDropdownOption& option, bool showKeybindings) const {
    UIElement row = UIElement::button()
            .id(uiDropdownOptionElementId(model_.elementId, option.id))
            .addClass("ui-dropdown-option")
            .addClass("text-sm")
            .addClass("leading-sm")
            .addClass("text-gray-100")
            .addClass("hover-bg-purple-800")
            .addClassIf(option.selected, "ui-dropdown-option-selected")
            .addClassIf(option.selected, "bg-purple-900")
            .data(kUIDropdownDataKey, model_.elementId)
            .data(kUIDropdownOptionDataKey, option.id)
            .attr("role", "option")
            .attr("aria-selected", option.selected ? "true" : "false");
    if (option.disabled) {
        row.attr("disabled", "disabled");
    }

    row.child(UIElement::span()
            .addClass("ui-dropdown-option-label")
            .text(option.label));
    if (showKeybindings) {
        row.child(UIElement::span()
                .addClass("ui-dropdown-option-keybinding")
                .addClass("text-gray-300")
                .text(option.keybinding));
    }
    return row;
}

std::string uiDropdownInputElementId(std::string_view elementId) {
    std::string result(elementId);
    result += "-input";
    return result;
}

std::string uiDropdownSurfaceElementId(std::string_view elementId) {
    std::string result(elementId);
    result += "-surface";
    return result;
}

std::string uiDropdownOptionElementId(std::string_view elementId, std::string_view optionId) {
    std::string result(elementId);
    result += "-option-";
    result += optionId;
    return result;
}

} // namespace quader
