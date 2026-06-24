////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/DebugPaletteView.h"

#include <cctype>
#include <sstream>
#include <string_view>

namespace quader {
namespace {

std::string rmlEscape(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char character : value) {
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
            escaped += character;
            break;
        }
    }
    return escaped;
}

} // namespace

std::string debugPaletteUtilityClassSuffix(std::string_view tokenName) {
    std::string suffix;
    suffix.reserve(tokenName.size());
    for (char character : tokenName) {
        const auto unsignedCharacter = static_cast<unsigned char>(character);
        if (std::isalnum(unsignedCharacter) != 0) {
            suffix.push_back(static_cast<char>(std::tolower(unsignedCharacter)));
        } else if (character == '.' || character == '_' || character == '-') {
            suffix.push_back('-');
        } else {
            return {};
        }
    }
    return suffix;
}

std::string buildDebugPalettePageRml(const std::vector<DebugPaletteColorToken>& colors) {
    std::ostringstream rml;
    rml << R"RML(
    <div id="debug-palette-page" class="font-ui bg-purple-950 text-gray-50 p-6">
        <div class="debug-palette-title text-xl leading-xl font-medium mb-4">Palette</div>
        <div class="debug-palette-grid">
)RML";

    for (const DebugPaletteColorToken& color : colors) {
        const std::string name = rmlEscape(color.name);
        const std::string hex = rmlEscape(color.hex);
        const std::string swatchClass = "bg-" + debugPaletteUtilityClassSuffix(color.name);
        rml << "            <div class=\"debug-palette-token w-52 h-11 mr-4 mb-3\">\n"
            << "                <div class=\"debug-palette-swatch size-8 mr-3 border-1 border-gray-500 " << swatchClass << "\"></div>\n"
            << "                <div class=\"debug-palette-text\">\n"
            << "                    <div class=\"debug-palette-name text-sm leading-sm font-regular text-gray-100 mb-1\">" << name << "</div>\n"
            << "                    <button class=\"debug-palette-hex font-mono text-xs leading-xs font-regular kerning-normal w-24 h-6 px-2 py-1 bg-gray-700 text-gray-50 border-1 border-gray-400\" data-copy-hex=\"" << hex << "\">" << hex << "</button>\n"
            << "                </div>\n"
            << "            </div>\n";
    }

    rml << R"RML(
        </div>
    </div>
)RML";
    return rml.str();
}

} // namespace quader
