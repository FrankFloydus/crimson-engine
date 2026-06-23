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

std::filesystem::path appUtilityTokenPath(const std::filesystem::path& executableDir) {
    return executableDir / "ui" / "tokens" / "app-utility.tokens.json";
}

std::vector<UIUtilityToken> loadUIUtilityTokens(const std::filesystem::path& utilityFile) {
    std::ifstream file(utilityFile);
    if (!file.is_open()) {
        LOG_WARN("Unable to open UI utility token file {}", utilityFile.string());
        return {};
    }

    try {
        const nlohmann::ordered_json tokens = nlohmann::ordered_json::parse(file);
        if (!tokens.is_object()) {
            LOG_WARN("UI utility token file {} must contain a flat object", utilityFile.string());
            return {};
        }

        std::vector<UIUtilityToken> result;
        for (auto iterator = tokens.begin(); iterator != tokens.end(); ++iterator) {
            const std::string value = jsonValueToTokenString(*iterator);
            if (!value.empty()) {
                result.push_back({
                        .name = iterator.key(),
                        .value = value,
                });
            }
        }
        return result;
    } catch (const std::exception& error) {
        LOG_WARN("Unable to parse UI utility token file {}: {}", utilityFile.string(), error.what());
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
