////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/core/ui_component.hpp"

#include <string>
#include <vector>

namespace quader {

/**
 * One retained item inside a tab rail. The rail owns tab-cell composition, while
 * callers provide the concrete content rendered inside each cell.
 */
struct UITabRailItem {
    UITabRailItem(std::string itemId, UIElement itemContent, bool itemActive = false);

    std::string id;
    UIElement content;
    bool active = false;
};

/**
 * Presentation classes for a reusable contiguous tab rail.
 */
struct UITabRailModel {
    std::string elementId;
    std::vector<std::string> railClasses;
    std::vector<std::string> cellClasses;
    std::vector<std::string> activeCellClasses;
    std::vector<UITabRailItem> items;
};

/**
 * Reusable retained tab rail that renders equal tab cells with exactly one
 * active cell at a time.
 */
class UITabRailComponent final : public UIComponent {
public:
    explicit UITabRailComponent(UITabRailModel model);

    [[nodiscard]] UIElement render() const override;

private:
    UITabRailModel model_;
};

} // namespace quader
