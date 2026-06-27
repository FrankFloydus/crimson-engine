////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/core/ui_component.hpp"

#include <string>

namespace quader {

/**
 * Composes the current minimize, restore, and close chrome buttons.
 */
class WindowChromeControlsComponent final : public UIComponent {
public:
    [[nodiscard]] UIElement render() const override;
};

[[nodiscard]] std::string buildWindowChromeControlsRml();
[[nodiscard]] std::string buildWindowChromeControlsInnerRml();

} // namespace quader
