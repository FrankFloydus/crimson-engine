////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/core/ui_component.hpp"
#include "ui/core/ui_icon_registry.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace quader {

/// Describes where a button projects its optional icon.
enum class UIButtonIconPlacement {
    None,
    Left,
    Right,
    Decoration,
};

/**
 * Base retained button component.
 *
 * Derived button classes own visual variants and behavior attributes while this
 * base class owns the common button element, optional label/icon content, and
 * semantic state attributes.
 */
class UIButtonComponent : public UIComponent {
public:
    [[nodiscard]] UIElement render() const override;

protected:
    explicit UIButtonComponent(std::string elementId);

    void setLabel(std::string label);
    void setIcon(UIIconId icon, UIButtonIconPlacement placement);
    void setDisabled(bool disabled);
    void setHovered(bool hovered);
    void setFocused(bool focused);
    void setPressed(bool pressed);
    void setActive(bool active);
    void setToggled(bool toggled);
    void setToggleButton(bool toggleButton);

    [[nodiscard]] std::string_view elementId() const { return elementId_; }

    virtual void appendButtonClasses(UIElement& button) const = 0;
    virtual void appendButtonAttributes(UIElement& button) const;
    virtual void appendButtonContent(UIElement& button) const;

private:
    std::string elementId_;
    std::string label_;
    std::optional<UIIconId> icon_;
    UIButtonIconPlacement iconPlacement_ = UIButtonIconPlacement::None;
    bool disabled_ = false;
    bool hovered_ = false;
    bool focused_ = false;
    bool pressed_ = false;
    bool active_ = false;
    bool toggled_ = false;
    bool toggleButton_ = false;
};

} // namespace quader
