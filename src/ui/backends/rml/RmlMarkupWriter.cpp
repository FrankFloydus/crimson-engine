////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/backends/rml/RmlMarkupWriter.h"

#include <sstream>

namespace quader {
namespace {

std::string styleText(const UIElement& element) {
    std::string style;
    for (const UIElement::Attribute& pair : element.styles()) {
        if (!style.empty()) {
            style += ' ';
        }
        style += pair.first;
        style += ": ";
        style += pair.second;
        style += ';';
    }
    return style;
}

void writeElement(std::ostringstream& rml, const UIElement& element) {
    rml << '<' << element.tag();
    if (!element.id().empty()) {
        rml << " id=\"" << escapeRmlText(element.id()) << '"';
    }
    if (!element.classes().empty()) {
        rml << " class=\"" << escapeRmlText(element.classes().toString()) << '"';
    }
    for (const UIElement::Attribute& pair : element.attributes()) {
        rml << ' ' << pair.first << "=\"" << escapeRmlText(pair.second) << '"';
    }
    const std::string styles = styleText(element);
    if (!styles.empty()) {
        rml << " style=\"" << escapeRmlText(styles) << '"';
    }

    if (element.selfClosing() && element.text().empty() && element.children().empty()) {
        rml << "/>";
        return;
    }

    rml << '>';
    if (!element.text().empty()) {
        rml << escapeRmlText(element.text());
    }
    for (const UIElement& child : element.children()) {
        writeElement(rml, child);
    }
    rml << "</" << element.tag() << '>';
}

} // namespace

std::string escapeRmlText(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char character : value) {
        switch (character) {
        case '&':
            escaped += "&amp;";
            break;
        case '<':
            escaped += "&lt;";
            break;
        case '>':
            escaped += "&gt;";
            break;
        case '"':
            escaped += "&quot;";
            break;
        case '\'':
            escaped += "&#39;";
            break;
        default:
            escaped.push_back(character);
            break;
        }
    }
    return escaped;
}

std::string writeRmlMarkup(const UIElement& element) {
    std::ostringstream rml;
    writeElement(rml, element);
    return rml.str();
}

} // namespace quader
