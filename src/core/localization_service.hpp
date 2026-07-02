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
    std::string display_name;
};

/**
 * Coordinates Localization Service responsibilities for the shared core service layer.
 */
class LocalizationService {
public:
    using Catalog = std::unordered_map<std::string, std::string>;

    static constexpr std::string_view kDefaultLocale = "en-US";

    static std::unordered_map<std::string, std::string>
    parse_lang_text(std::string_view text);

    bool load(const std::filesystem::path &locales_directory,
              std::string selected_locale,
              std::string fallback_locale = std::string(kDefaultLocale));

    [[nodiscard]] const std::string &current_locale() const {
      return current_locale_;
    }
    [[nodiscard]] const std::string &fallback_locale() const {
      return fallback_locale_;
    }
    [[nodiscard]] const std::vector<LocaleInfo> &available_locales() const {
      return available_locales_;
    }

    [[nodiscard]] bool has_locale(std::string_view locale) const;
    bool select_locale(std::string_view locale);
    [[nodiscard]] std::string
    locale_display_name(std::string_view locale) const;
    [[nodiscard]] std::string text(std::string_view key, std::string_view fallback) const;
    [[nodiscard]] std::string imgui_label(std::string_view key,
                                          std::string_view stable_id,
                                          std::string_view fallback) const;
    [[nodiscard]] std::string format(std::string_view key,
            std::string_view fallback,
            std::initializer_list<std::pair<std::string_view, std::string_view>> replacements) const;

private:
  static Catalog load_catalog_file(const std::filesystem::path &path);
  static std::string display_name_for(std::string_view locale,
                                      const Catalog &catalog);

  std::string current_locale_ = std::string(kDefaultLocale);
  std::string fallback_locale_ = std::string(kDefaultLocale);
  std::unordered_map<std::string, Catalog> locale_catalogs_;
  Catalog active_catalog_;
  Catalog fallback_catalog_;
  std::vector<LocaleInfo> available_locales_;
};

} // namespace quader
