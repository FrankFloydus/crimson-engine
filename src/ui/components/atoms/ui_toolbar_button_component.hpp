////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/components/atoms/ui_button_component.hpp"

#include <optional>
#include <string>

namespace quader {

/**
 * Describes one compact toolbar button with optional icon, primary label, and
 * secondary command text.
 */
struct UIToolbarButtonModel {
    std::string elementId;
    std::optional<UIIconId> icon;
    UIButtonIconPlacement iconPlacement = UIButtonIconPlacement::Left;
    std::string label;
    std::string secondaryText;
    bool active = false;
    bool toggleButton = false;
    bool toggled = false;
    bool disabled = false;
};

/**
 * Retained compact toolbar button used by tool strips and toolbar-like rows.
 */
class UIToolbarButtonComponent : public UIButtonComponent {
public:
    explicit UIToolbarButtonComponent(UIToolbarButtonModel model);

protected:
    void appendButtonClasses(UIElement& button) const override;
    void appendButtonAttributes(UIElement& button) const override;
    void appendButtonContent(UIElement& button) const override;

    [[nodiscard]] const UIToolbarButtonModel& model() const { return model_; }

private:
    UIToolbarButtonModel model_;
};

} // namespace quader
