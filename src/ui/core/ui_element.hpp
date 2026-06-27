////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/core/ui_class_list.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quader {

/**
 * Backend-neutral retained UI element snapshot.
 *
 * UI components should build these nodes and let a backend writer decide how to
 * project them to RML, DOM, or another retained UI backend.
 */
class UIElement {
public:
    using Attribute = std::pair<std::string, std::string>;

    explicit UIElement(std::string tag);

    [[nodiscard]] static UIElement div();
    [[nodiscard]] static UIElement span();
    [[nodiscard]] static UIElement button();
    [[nodiscard]] static UIElement img();

    UIElement& id(std::string_view value);
    UIElement& attr(std::string_view name, std::string_view value);
    UIElement& data(std::string_view name, std::string_view value);
    UIElement& style(std::string_view name, std::string_view value);
    UIElement& addClass(std::string_view className);
    UIElement& addClassIf(bool condition, std::string_view className);
    UIElement& text(std::string_view value);
    UIElement& child(UIElement element);
    UIElement& selfClosing(bool value = true);

    [[nodiscard]] const std::string& tag() const { return tag_; }
    [[nodiscard]] const std::string& id() const { return id_; }
    [[nodiscard]] const UIClassList& classes() const { return classes_; }
    [[nodiscard]] const std::vector<Attribute>& attributes() const { return attributes_; }
    [[nodiscard]] const std::vector<Attribute>& styles() const { return styles_; }
    [[nodiscard]] const std::string& text() const { return text_; }
    [[nodiscard]] const std::vector<UIElement>& children() const { return children_; }
    [[nodiscard]] bool selfClosing() const { return selfClosing_; }

private:
    void setPair(std::vector<Attribute>& pairs, std::string_view name, std::string_view value);

    std::string tag_;
    std::string id_;
    UIClassList classes_;
    std::vector<Attribute> attributes_;
    std::vector<Attribute> styles_;
    std::string text_;
    std::vector<UIElement> children_;
    bool selfClosing_ = false;
};

} // namespace quader
