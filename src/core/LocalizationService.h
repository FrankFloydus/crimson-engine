////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace quader {

/**
 * Stores the Locale Info data contract used by the shared core service layer.
 */
struct LocaleInfo {
    std::string id;
    std::string displayName;
};

/**
 * Coordinates Localization Service responsibilities for the shared core service layer.
 */
class LocalizationService {
public:
    using Catalog = std::unordered_map<std::string, std::string>;

    static constexpr std::string_view DefaultLocale = "en-US";

    static std::unordered_map<std::string, std::string> parseLangText(std::string_view text);

    bool load(const std::filesystem::path& localesDirectory,
            std::string selectedLocale,
            std::string fallbackLocale = std::string(DefaultLocale));

    [[nodiscard]] const std::string& currentLocale() const { return currentLocale_; }
    [[nodiscard]] const std::string& fallbackLocale() const { return fallbackLocale_; }
    [[nodiscard]] const std::vector<LocaleInfo>& availableLocales() const { return availableLocales_; }

    [[nodiscard]] bool hasLocale(std::string_view locale) const;
    bool selectLocale(std::string_view locale);
    [[nodiscard]] std::string localeDisplayName(std::string_view locale) const;
    [[nodiscard]] std::string text(std::string_view key, std::string_view fallback) const;
    [[nodiscard]] std::string format(std::string_view key,
            std::string_view fallback,
            std::initializer_list<std::pair<std::string_view, std::string_view>> replacements) const;

private:
    static Catalog loadCatalogFile(const std::filesystem::path& path);
    static std::string displayNameFor(std::string_view locale, const Catalog& catalog);

    std::string currentLocale_ = std::string(DefaultLocale);
    std::string fallbackLocale_ = std::string(DefaultLocale);
    std::unordered_map<std::string, Catalog> localeCatalogs_;
    Catalog activeCatalog_;
    Catalog fallbackCatalog_;
    std::vector<LocaleInfo> availableLocales_;
};

} // namespace quader
