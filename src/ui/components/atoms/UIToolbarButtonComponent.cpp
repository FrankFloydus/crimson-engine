////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/components/atoms/UIToolbarButtonComponent.h"

#include <cctype>
#include <string_view>
#include <utility>

namespace quader {
namespace {

bool isIconOnly(const UIToolbarButtonModel& model) {
    return model.label.empty() && model.secondaryText.empty();
}

std::string uppercaseAscii(std::string value) {
    for (char& character : value) {
        character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    }
    return value;
}

UIElement toolbarIcon(UIIconId icon, std::string_view spacingClass) {
    UIElement element = UIIconRegistry::instance().renderIcon(icon);
    element.addClass("toolbar-button-icon");
    element.addClassIf(!spacingClass.empty(), spacingClass);
    return element;
}

} // namespace

UIToolbarButtonComponent::UIToolbarButtonComponent(UIToolbarButtonModel model)
    : UIButtonComponent(model.elementId)
    , model_(std::move(model)) {
    setActive(model_.active);
    setToggleButton(model_.toggleButton);
    setToggled(model_.toggled);
    setDisabled(model_.disabled);
}

void UIToolbarButtonComponent::appendButtonClasses(UIElement& button) const {
    const bool iconOnly = isIconOnly(model_);
    button.addClass("toolbar-button")
            .addClass("bg-gray-950")
            .addClass("border-1")
            .addClass("border-gray-700")
            .addClass("rounded-6")
            .addClass("min-w-8")
            .addClass("h-8")
            .addClass("m-0")
            .addClass("text-gray-100")
            .addClass("hover-bg-purple-900")
            .addClass("active-bg-purple-900")
            .addClass("hover-border-purple-700")
            .addClass("active-border-purple-700")
            .addClassIf(model_.active, "bg-purple-900")
            .addClassIf(model_.active, "border-purple-700")
            .addClassIf(model_.active, "toolbar-button-active")
            .addClassIf(!iconOnly, "px-3")
            .addClassIf(iconOnly, "w-8")
            .addClassIf(iconOnly, "px-0")
            .addClassIf(iconOnly, "toolbar-button-icon-only");
}

void UIToolbarButtonComponent::appendButtonAttributes(UIElement& button) const {
    UIButtonComponent::appendButtonAttributes(button);
}

void UIToolbarButtonComponent::appendButtonContent(UIElement& button) const {
    if (model_.iconPlacement == UIButtonIconPlacement::Decoration && model_.icon) {
        button.child(toolbarIcon(*model_.icon, {}));
        return;
    }

    const bool hasTextContent = !model_.label.empty() || !model_.secondaryText.empty();
    if (model_.iconPlacement == UIButtonIconPlacement::Left && model_.icon) {
        button.child(toolbarIcon(*model_.icon, hasTextContent ? "mr-2" : std::string_view()));
    }
    if (!model_.label.empty()) {
        button.child(UIElement::span()
                        .addClass("toolbar-button-label")
                        .addClass("text-md")
                        .addClass("leading-md")
                        .addClass("text-gray-100")
                        .text(model_.label));
    }
    if (!model_.secondaryText.empty()) {
        button.child(UIElement::span()
                        .addClass("toolbar-button-secondary")
                        .addClass("font-alt")
                        .addClass("text-sm")
                        .addClass("leading-sm")
                        .addClass("ml-3_5")
                        .addClass("text-gray-300")
                        .text(uppercaseAscii(model_.secondaryText)));
    }
    if (model_.iconPlacement == UIButtonIconPlacement::Right && model_.icon) {
        button.child(toolbarIcon(*model_.icon, hasTextContent ? "ml-2" : std::string_view()));
    }
}

} // namespace quader
