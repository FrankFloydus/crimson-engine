////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/components/common/ui_tab_rail_component.hpp"

#include <utility>

namespace quader {
namespace {

void appendClasses(UIElement& element, const std::vector<std::string>& classes) {
    for (const std::string& className : classes) {
        element.addClass(className);
    }
}

} // namespace

UITabRailItem::UITabRailItem(std::string itemId, UIElement itemContent, bool itemActive)
    : id(std::move(itemId))
    , content(std::move(itemContent))
    , active(itemActive) {}

UITabRailComponent::UITabRailComponent(UITabRailModel model)
    : model_(std::move(model)) {}

UIElement UITabRailComponent::render() const {
    UIElement rail = UIElement::div()
            .id(model_.elementId)
            .addClass("ui-tab-rail");
    appendClasses(rail, model_.railClasses);

    for (const UITabRailItem& item : model_.items) {
        UIElement cell = UIElement::div()
                .addClass("ui-tab-rail-cell")
                .addClassIf(item.active, "ui-tab-rail-cell-active")
                .data("ui-tab-rail-item", item.id);
        appendClasses(cell, model_.cellClasses);
        if (item.active) {
            appendClasses(cell, model_.activeCellClasses);
        }
        cell.child(item.content);
        rail.child(std::move(cell));
    }

    return rail;
}

} // namespace quader
