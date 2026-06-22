////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "core/LocalizationService.h"

#include "core/Logger.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <sstream>

namespace quader {
namespace {

std::string trim(std::string_view value) {
    const auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    });
    const auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    }).base();
    if (begin >= end) {
        return {};
    }
    return {begin, end};
}

std::string stripBom(std::string value) {
    constexpr unsigned char Bom0 = 0xef;
    constexpr unsigned char Bom1 = 0xbb;
    constexpr unsigned char Bom2 = 0xbf;
    if (value.size() >= 3U &&
            static_cast<unsigned char>(value[0]) == Bom0 &&
            static_cast<unsigned char>(value[1]) == Bom1 &&
            static_cast<unsigned char>(value[2]) == Bom2) {
        value.erase(0, 3);
    }
    return value;
}

std::string unescape(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    bool escaped = false;
    for (const char c : value) {
        if (!escaped) {
            if (c == '\\') {
                escaped = true;
            } else {
                result.push_back(c);
            }
            continue;
        }

        switch (c) {
        case 'n':
            result.push_back('\n');
            break;
        case 'r':
            result.push_back('\r');
            break;
        case 't':
            result.push_back('\t');
            break;
        default:
            result.push_back(c);
            break;
        }
        escaped = false;
    }
    if (escaped) {
        result.push_back('\\');
    }
    return result;
}

std::string localeIdFromPath(const std::filesystem::path& path) {
    return path.stem().string();
}

const std::string* findValue(const LocalizationService::Catalog& catalog, std::string_view key) {
    const auto found = catalog.find(std::string(key));
    return found != catalog.end() && !found->second.empty() ? &found->second : nullptr;
}

} // namespace

std::unordered_map<std::string, std::string> LocalizationService::parseLangText(std::string_view text) {
    Catalog catalog;
    std::istringstream stream(stripBom(std::string(text)));
    std::string line;
    while (std::getline(stream, line)) {
        const std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed.front() == '#' || trimmed.front() == ';') {
            continue;
        }

        const std::size_t equals = trimmed.find('=');
        if (equals == std::string::npos) {
            continue;
        }

        const std::string key = trim(std::string_view(trimmed).substr(0, equals));
        if (key.empty()) {
            continue;
        }
        const std::string value = unescape(trim(std::string_view(trimmed).substr(equals + 1U)));
        catalog[key] = value;
    }
    return catalog;
}

bool LocalizationService::load(
        const std::filesystem::path& localesDirectory,
        std::string selectedLocale,
        std::string fallbackLocale) {
    if (fallbackLocale.empty()) {
        fallbackLocale = std::string(DefaultLocale);
    }
    if (selectedLocale.empty()) {
        selectedLocale = fallbackLocale;
    }

    fallbackLocale_ = std::move(fallbackLocale);
    currentLocale_ = selectedLocale;
    fallbackCatalog_.clear();
    activeCatalog_.clear();
    localeCatalogs_.clear();
    availableLocales_.clear();

    std::error_code error;
    if (std::filesystem::exists(localesDirectory, error)) {
        std::vector<std::filesystem::path> localeFiles;
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(localesDirectory, error)) {
            if (entry.is_regular_file(error) && entry.path().extension() == ".lang") {
                localeFiles.push_back(entry.path());
            }
        }
        std::sort(localeFiles.begin(), localeFiles.end());
        for (const std::filesystem::path& localeFile : localeFiles) {
            const std::string locale = localeIdFromPath(localeFile);
            const Catalog catalog = loadCatalogFile(localeFile);
            localeCatalogs_[locale] = catalog;
            availableLocales_.push_back(LocaleInfo{
                    .id = locale,
                    .displayName = displayNameFor(locale, catalog),
            });
        }
    }

    if (const auto found = localeCatalogs_.find(fallbackLocale_); found != localeCatalogs_.end()) {
        fallbackCatalog_ = found->second;
    } else {
        fallbackCatalog_ = loadCatalogFile(localesDirectory / (fallbackLocale_ + ".lang"));
        if (!fallbackCatalog_.empty()) {
            localeCatalogs_[fallbackLocale_] = fallbackCatalog_;
        }
    }
    if (fallbackCatalog_.empty()) {
        LOG_WARN("Localization fallback catalog {} is missing or empty", (localesDirectory / (fallbackLocale_ + ".lang")).string());
    }

    if (!selectLocale(selectedLocale)) {
        LOG_WARN("Localization catalog {} is missing or empty; falling back to {}",
                selectedLocale,
                fallbackLocale_);
        (void)selectLocale(fallbackLocale_);
    }

    if (availableLocales_.empty()) {
        availableLocales_.push_back(LocaleInfo{
                .id = fallbackLocale_,
                .displayName = displayNameFor(fallbackLocale_, fallbackCatalog_),
        });
    }

    std::sort(availableLocales_.begin(), availableLocales_.end(), [](const LocaleInfo& left, const LocaleInfo& right) {
        return left.id < right.id;
    });
    availableLocales_.erase(
            std::unique(availableLocales_.begin(), availableLocales_.end(), [](const LocaleInfo& left, const LocaleInfo& right) {
                return left.id == right.id;
            }),
            availableLocales_.end());

    return !fallbackCatalog_.empty();
}

bool LocalizationService::hasLocale(std::string_view locale) const {
    return std::any_of(availableLocales_.begin(), availableLocales_.end(), [locale](const LocaleInfo& info) {
        return info.id == locale;
    });
}

bool LocalizationService::selectLocale(std::string_view locale) {
    const std::string localeId(locale);
    const auto found = localeCatalogs_.find(localeId);
    if (found == localeCatalogs_.end() || found->second.empty()) {
        if (localeId == fallbackLocale_ && !fallbackCatalog_.empty()) {
            activeCatalog_ = fallbackCatalog_;
            currentLocale_ = fallbackLocale_;
            return true;
        }
        return false;
    }

    activeCatalog_ = found->second;
    currentLocale_ = localeId;
    return true;
}

std::string LocalizationService::localeDisplayName(std::string_view locale) const {
    const auto found = std::find_if(availableLocales_.begin(), availableLocales_.end(), [locale](const LocaleInfo& info) {
        return info.id == locale;
    });
    return found != availableLocales_.end() ? found->displayName : std::string(locale);
}

std::string LocalizationService::text(std::string_view key, std::string_view fallback) const {
    if (const std::string* value = findValue(activeCatalog_, key)) {
        return *value;
    }
    if (const std::string* value = findValue(fallbackCatalog_, key)) {
        return *value;
    }
    return fallback.empty() ? std::string(key) : std::string(fallback);
}

std::string LocalizationService::format(
        std::string_view key,
        std::string_view fallback,
        std::initializer_list<std::pair<std::string_view, std::string_view>> replacements) const {
    std::string value = text(key, fallback);
    for (const auto& replacement : replacements) {
        std::string token = "{";
        token.append(replacement.first);
        token.push_back('}');
        std::size_t offset = 0U;
        while ((offset = value.find(token, offset)) != std::string::npos) {
            value.replace(offset, token.size(), replacement.second);
            offset += replacement.second.size();
        }
    }
    return value;
}

std::string LocalizationService::imguiLabel(
        std::string_view key,
        std::string_view stableId,
        std::string_view fallback) const {
    std::string value = text(key, fallback);
    value.append("###");
    value.append(stableId);
    return value;
}

LocalizationService::Catalog LocalizationService::loadCatalogFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return {};
    }
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return parseLangText(text);
}

std::string LocalizationService::displayNameFor(std::string_view locale, const Catalog& catalog) {
    if (const std::string* name = findValue(catalog, "locale.name")) {
        return *name;
    }
    if (locale == DefaultLocale) {
        return "English";
    }
    return std::string(locale);
}

} // namespace quader
