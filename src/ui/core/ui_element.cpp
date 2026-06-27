////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/core/ui_element.hpp"

#include <algorithm>

namespace quader {

UIElement::UIElement(std::string tag)
        : tag_(std::move(tag)) {}

UIElement UIElement::div() {
    return UIElement("div");
}

UIElement UIElement::span() {
    return UIElement("span");
}

UIElement UIElement::button() {
    return UIElement("button");
}

UIElement UIElement::img() {
    return UIElement("img").selfClosing();
}

UIElement& UIElement::id(std::string_view value) {
    id_ = std::string(value);
    return *this;
}

UIElement& UIElement::attr(std::string_view name, std::string_view value) {
    setPair(attributes_, name, value);
    return *this;
}

UIElement& UIElement::data(std::string_view name, std::string_view value) {
    std::string attributeName = "data-";
    attributeName += name;
    return attr(attributeName, value);
}

UIElement& UIElement::style(std::string_view name, std::string_view value) {
    setPair(styles_, name, value);
    return *this;
}

UIElement& UIElement::addClass(std::string_view className) {
    classes_.add(className);
    return *this;
}

UIElement& UIElement::addClassIf(bool condition, std::string_view className) {
    classes_.addIf(condition, className);
    return *this;
}

UIElement& UIElement::text(std::string_view value) {
    text_ = std::string(value);
    return *this;
}

UIElement& UIElement::child(UIElement element) {
    children_.push_back(std::move(element));
    return *this;
}

UIElement& UIElement::selfClosing(bool value) {
    selfClosing_ = value;
    return *this;
}

void UIElement::setPair(std::vector<Attribute>& pairs, std::string_view name, std::string_view value) {
    const auto existing = std::find_if(pairs.begin(), pairs.end(), [name](const Attribute& pair) {
        return pair.first == name;
    });
    if (existing != pairs.end()) {
        existing->second = std::string(value);
        return;
    }
    pairs.emplace_back(name, value);
}

} // namespace quader
