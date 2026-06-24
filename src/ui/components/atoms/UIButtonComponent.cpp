////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/components/atoms/UIButtonComponent.h"

#include <utility>

namespace quader {

UIButtonComponent::UIButtonComponent(std::string elementId)
        : elementId_(std::move(elementId)) {}

UIElement UIButtonComponent::render() const {
    UIElement button = UIElement::button().id(elementId_);
    appendButtonClasses(button);
    appendButtonAttributes(button);
    appendButtonContent(button);
    return button;
}

void UIButtonComponent::setLabel(std::string label) {
    label_ = std::move(label);
}

void UIButtonComponent::setIcon(UIIconId icon, UIButtonIconPlacement placement) {
    icon_ = icon;
    iconPlacement_ = placement;
}

void UIButtonComponent::setDisabled(bool disabled) {
    disabled_ = disabled;
}

void UIButtonComponent::setHovered(bool hovered) {
    hovered_ = hovered;
}

void UIButtonComponent::setFocused(bool focused) {
    focused_ = focused;
}

void UIButtonComponent::setPressed(bool pressed) {
    pressed_ = pressed;
}

void UIButtonComponent::setActive(bool active) {
    active_ = active;
}

void UIButtonComponent::setToggled(bool toggled) {
    toggled_ = toggled;
}

void UIButtonComponent::setToggleButton(bool toggleButton) {
    toggleButton_ = toggleButton;
}

void UIButtonComponent::appendButtonAttributes(UIElement& button) const {
    if (disabled_) {
        button.attr("disabled", "disabled");
    }
    if (hovered_) {
        button.data("ui-hover", "true");
    }
    if (focused_) {
        button.data("ui-focus", "true");
    }
    if (pressed_) {
        button.data("ui-pressed", "true");
    }
    if (active_) {
        button.data("ui-active", "true");
    }
    if (toggleButton_) {
        button.attr("aria-pressed", toggled_ ? "true" : "false");
        button.data("ui-toggle", toggled_ ? "on" : "off");
    }
}

void UIButtonComponent::appendButtonContent(UIElement& button) const {
    if (iconPlacement_ == UIButtonIconPlacement::Decoration && icon_) {
        button.child(UIIconRegistry::instance().renderIcon(*icon_, "ui-button-icon"));
        return;
    }

    if (iconPlacement_ == UIButtonIconPlacement::Left && icon_) {
        button.child(UIIconRegistry::instance().renderIcon(*icon_, "ui-button-icon"));
    }
    if (!label_.empty()) {
        button.child(UIElement::span().addClass("ui-button-label").text(label_));
    }
    if (iconPlacement_ == UIButtonIconPlacement::Right && icon_) {
        button.child(UIIconRegistry::instance().renderIcon(*icon_, "ui-button-icon"));
    }
}

} // namespace quader
