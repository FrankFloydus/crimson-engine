////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/UIUtilityTokens.h"

#include "core/Logger.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string_view>

namespace quader {
namespace {

bool isHexDigit(char value) {
    return std::isxdigit(static_cast<unsigned char>(value)) != 0;
}

bool isHexColor(const std::string& value) {
    return value.size() == 7u &&
            value[0] == '#' &&
            std::all_of(value.begin() + 1, value.end(), isHexDigit);
}

bool startsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

bool isSafeUtilityName(std::string_view value) {
    return !value.empty() && std::all_of(value.begin(), value.end(), [](char character) {
        const auto unsignedCharacter = static_cast<unsigned char>(character);
        return std::isalnum(unsignedCharacter) != 0 || character == '-' || character == '_';
    });
}

bool isSafeRcssValue(std::string_view value) {
    return !value.empty() && value.find_first_of("{};") == std::string_view::npos;
}

bool isNumericValue(std::string_view value) {
    return !value.empty() && std::all_of(value.begin(), value.end(), [](char character) {
        const auto unsignedCharacter = static_cast<unsigned char>(character);
        return std::isdigit(unsignedCharacter) != 0;
    });
}

std::string jsonValueToTokenString(const nlohmann::ordered_json& value) {
    if (value.is_string()) {
        return value.get<std::string>();
    }
    if (value.is_number_integer()) {
        return std::to_string(value.get<long long>());
    }
    if (value.is_number_unsigned()) {
        return std::to_string(value.get<unsigned long long>());
    }
    if (value.is_number_float()) {
        std::ostringstream stream;
        stream << value.get<double>();
        return stream.str();
    }
    return {};
}

const nlohmann::ordered_json* objectMember(const nlohmann::ordered_json& object, std::string_view key) {
    if (!object.is_object()) {
        return nullptr;
    }

    const auto iterator = object.find(std::string(key));
    return iterator == object.end() ? nullptr : &(*iterator);
}

void collectColorTokens(
        const nlohmann::ordered_json& node,
        const std::string& prefix,
        std::vector<DebugPaletteColorToken>& colors) {
    if (!node.is_object()) {
        return;
    }

    for (auto iterator = node.begin(); iterator != node.end(); ++iterator) {
        const std::string name = prefix.empty() ? iterator.key() : prefix + "." + iterator.key();
        if (iterator->is_string()) {
            const std::string hex = iterator->get<std::string>();
            if (isHexColor(hex)) {
                colors.push_back({
                        .name = name,
                        .hex = hex,
                });
            }
            continue;
        }

        collectColorTokens(*iterator, name, colors);
    }
}

void appendFlatUtilityTokens(std::vector<UIUtilityToken>& tokens, const nlohmann::ordered_json& node) {
    if (!node.is_object()) {
        return;
    }

    for (auto iterator = node.begin(); iterator != node.end(); ++iterator) {
        const std::string value = jsonValueToTokenString(*iterator);
        if (!value.empty()) {
            tokens.push_back({
                    .name = iterator.key(),
                    .value = value,
            });
        }
    }
}

void appendNamedUtilityGroup(
        std::vector<UIUtilityToken>& tokens,
        const nlohmann::ordered_json& theme,
        std::string_view groupName,
        std::string_view utilityPrefix) {
    const nlohmann::ordered_json* group = objectMember(theme, groupName);
    if (!group || !group->is_object()) {
        return;
    }

    for (auto iterator = group->begin(); iterator != group->end(); ++iterator) {
        const std::string value = jsonValueToTokenString(*iterator);
        if (!value.empty()) {
            tokens.push_back({
                    .name = std::string(utilityPrefix) + iterator.key(),
                    .value = value,
            });
        }
    }
}

void appendThemeTokens(UITokenConfig& result, const nlohmann::ordered_json& theme) {
    if (const nlohmann::ordered_json* colors = objectMember(theme, "colors")) {
        collectColorTokens(*colors, {}, result.colors);
    }

    appendNamedUtilityGroup(result.utilities, theme, "fontFamily", "font-");
    appendNamedUtilityGroup(result.utilities, theme, "fontWeight", "font-");
    appendNamedUtilityGroup(result.utilities, theme, "fontSize", "text-");
    appendNamedUtilityGroup(result.utilities, theme, "lineHeight", "leading-");
    appendNamedUtilityGroup(result.utilities, theme, "spacing", "space-");
    appendNamedUtilityGroup(result.utilities, theme, "spacing", "size-");
    appendNamedUtilityGroup(result.utilities, theme, "size", "size-");
    appendNamedUtilityGroup(result.utilities, theme, "borderWidth", "border-");
    appendNamedUtilityGroup(result.utilities, theme, "fontKerning", "kerning-");
    if (const nlohmann::ordered_json* utilities = objectMember(theme, "utilities")) {
        appendFlatUtilityTokens(result.utilities, *utilities);
    }
}

void appendRule(std::ostringstream& rcss, std::string_view className, std::string_view property, std::string_view value) {
    if (!isSafeUtilityName(className) || !isSafeRcssValue(value)) {
        return;
    }

    rcss << "." << className << " { " << property << ": " << value << "; }\n";
}

void appendRule2(
        std::ostringstream& rcss,
        std::string_view className,
        std::string_view propertyA,
        std::string_view valueA,
        std::string_view propertyB,
        std::string_view valueB) {
    if (!isSafeUtilityName(className) || !isSafeRcssValue(valueA) || !isSafeRcssValue(valueB)) {
        return;
    }

    rcss << "." << className << " { " << propertyA << ": " << valueA << "; " << propertyB << ": " << valueB << "; }\n";
}

void appendSpacingUtilities(std::ostringstream& rcss, std::string_view suffix, std::string_view value) {
    if (!isSafeUtilityName(suffix) || !isSafeRcssValue(value)) {
        return;
    }

    appendRule(rcss, std::string("p-") + std::string(suffix), "padding", value);
    appendRule2(rcss, std::string("px-") + std::string(suffix), "padding-left", value, "padding-right", value);
    appendRule2(rcss, std::string("py-") + std::string(suffix), "padding-top", value, "padding-bottom", value);
    appendRule(rcss, std::string("pt-") + std::string(suffix), "padding-top", value);
    appendRule(rcss, std::string("pr-") + std::string(suffix), "padding-right", value);
    appendRule(rcss, std::string("pb-") + std::string(suffix), "padding-bottom", value);
    appendRule(rcss, std::string("pl-") + std::string(suffix), "padding-left", value);

    appendRule(rcss, std::string("m-") + std::string(suffix), "margin", value);
    appendRule2(rcss, std::string("mx-") + std::string(suffix), "margin-left", value, "margin-right", value);
    appendRule2(rcss, std::string("my-") + std::string(suffix), "margin-top", value, "margin-bottom", value);
    appendRule(rcss, std::string("mt-") + std::string(suffix), "margin-top", value);
    appendRule(rcss, std::string("mr-") + std::string(suffix), "margin-right", value);
    appendRule(rcss, std::string("mb-") + std::string(suffix), "margin-bottom", value);
    appendRule(rcss, std::string("ml-") + std::string(suffix), "margin-left", value);
}

void appendSizingUtilities(std::ostringstream& rcss, std::string_view suffix, std::string_view value) {
    if (!isSafeUtilityName(suffix) || !isSafeRcssValue(value)) {
        return;
    }

    appendRule2(rcss, std::string("size-") + std::string(suffix), "width", value, "height", value);
    appendRule(rcss, std::string("w-") + std::string(suffix), "width", value);
    appendRule(rcss, std::string("h-") + std::string(suffix), "height", value);
    appendRule(rcss, std::string("min-w-") + std::string(suffix), "min-width", value);
    appendRule(rcss, std::string("max-w-") + std::string(suffix), "max-width", value);
    appendRule(rcss, std::string("min-h-") + std::string(suffix), "min-height", value);
    appendRule(rcss, std::string("max-h-") + std::string(suffix), "max-height", value);
}

void appendUtilityToken(std::ostringstream& rcss, const UIUtilityToken& token) {
    if (!isSafeUtilityName(token.name) || !isSafeRcssValue(token.value)) {
        return;
    }

    const std::string_view name = token.name;
    const std::string_view value = token.value;
    if (startsWith(name, "text-")) {
        appendRule(rcss, name, "font-size", value);
    } else if (startsWith(name, "leading-")) {
        appendRule(rcss, name, "line-height", value);
    } else if (startsWith(name, "font-")) {
        appendRule(rcss, name, isNumericValue(value) ? "font-weight" : "font-family", value);
    } else if (startsWith(name, "space-")) {
        appendSpacingUtilities(rcss, name.substr(6), value);
    } else if (startsWith(name, "size-")) {
        appendSizingUtilities(rcss, name.substr(5), value);
    } else if (startsWith(name, "w-")) {
        appendRule(rcss, name, "width", value);
    } else if (startsWith(name, "h-")) {
        appendRule(rcss, name, "height", value);
    } else if (startsWith(name, "min-w-")) {
        appendRule(rcss, name, "min-width", value);
    } else if (startsWith(name, "max-w-")) {
        appendRule(rcss, name, "max-width", value);
    } else if (startsWith(name, "min-h-")) {
        appendRule(rcss, name, "min-height", value);
    } else if (startsWith(name, "max-h-")) {
        appendRule(rcss, name, "max-height", value);
    } else if (startsWith(name, "border-")) {
        appendRule(rcss, name, "border-width", value);
    } else if (startsWith(name, "kerning-")) {
        appendRule(rcss, name, "font-kerning", value);
    }
}

} // namespace

std::filesystem::path appUITokenConfigPath(const std::filesystem::path& executableDir) {
    return executableDir / "ui" / "tokens" / "app-ui.tokens.json";
}

UITokenConfig loadUITokenConfig(const std::filesystem::path& configFile) {
    std::ifstream file(configFile);
    if (!file.is_open()) {
        LOG_WARN("Unable to open UI token config file {}", configFile.string());
        return {};
    }

    try {
        const nlohmann::ordered_json config = nlohmann::ordered_json::parse(file);
        if (!config.is_object()) {
            LOG_WARN("UI token config file {} must contain an object", configFile.string());
            return {};
        }

        UITokenConfig result;
        const nlohmann::ordered_json* theme = objectMember(config, "theme");
        if (!theme || !theme->is_object()) {
            LOG_WARN("UI token config file {} must contain a theme object", configFile.string());
            return {};
        }

        appendThemeTokens(result, *theme);
        if (const nlohmann::ordered_json* extend = objectMember(*theme, "extend")) {
            appendThemeTokens(result, *extend);
        }

        return result;
    } catch (const std::exception& error) {
        LOG_WARN("Unable to parse UI token config file {}: {}", configFile.string(), error.what());
        return {};
    }
}

std::string buildUIUtilityRcss(
        const std::vector<UIUtilityToken>& tokens,
        const std::vector<DebugPaletteColorToken>& paletteColors) {
    std::ostringstream rcss;
    for (const UIUtilityToken& token : tokens) {
        appendUtilityToken(rcss, token);
    }

    for (const DebugPaletteColorToken& color : paletteColors) {
        const std::string suffix = debugPaletteUtilityClassSuffix(color.name);
        if (suffix.empty() || !isSafeRcssValue(color.hex)) {
            continue;
        }

        appendRule(rcss, "bg-" + suffix, "background-color", color.hex);
        appendRule(rcss, "text-" + suffix, "color", color.hex);
        appendRule(rcss, "border-" + suffix, "border-color", color.hex);
    }
    return rcss.str();
}

} // namespace quader
