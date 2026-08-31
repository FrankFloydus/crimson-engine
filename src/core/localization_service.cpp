////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "core/localization_service.hpp"

#include "core/logger.hpp"
#include "core/qstr.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <sstream>

namespace quader {
namespace {

std::string strip_bom(std::string value) {
  constexpr unsigned char kBom0 = 0xef;
  constexpr unsigned char kBom1 = 0xbb;
  constexpr unsigned char kBom2 = 0xbf;
  if (value.size() >= 3U && static_cast<unsigned char>(value[0]) == kBom0 &&
      static_cast<unsigned char>(value[1]) == kBom1 &&
      static_cast<unsigned char>(value[2]) == kBom2) {
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

std::string locale_id_from_path(const std::filesystem::path &path) {
  return path.stem().string();
}

const std::string *find_value(const LocalizationService::Catalog &catalog,
                              std::string_view key) {
  const auto found = catalog.find(std::string(key));
  return found != catalog.end() && !found->second.empty() ? &found->second
                                                          : nullptr;
}

} // namespace

std::unordered_map<std::string, std::string>
LocalizationService::parse_lang_text(std::string_view text) {
  Catalog catalog;
  std::istringstream stream(strip_bom(std::string(text)));
  std::string line;
  while (std::getline(stream, line)) {
    const std::string trimmed = QStr::trim_ascii_whitespace(line);
    if (trimmed.empty() || trimmed.front() == '#' || trimmed.front() == ';') {
      continue;
    }

    const std::optional<QStrSplitOnce> split = QStr::split_once(trimmed, '=');
    if (!split) {
      continue;
    }

    const std::string key = QStr::trim_ascii_whitespace(split->before);
    if (key.empty()) {
      continue;
    }
    const std::string value =
        unescape(QStr::trim_ascii_whitespace(split->after));
    catalog[key] = value;
  }
  return catalog;
}

bool LocalizationService::load(const std::filesystem::path &locales_directory,
                               std::string selected_locale,
                               std::string fallback_locale) {
  const std::uint64_t revision_before_load = revision_;
  if (fallback_locale.empty()) {
    fallback_locale = std::string(kDefaultLocale);
  }
  if (selected_locale.empty()) {
    selected_locale = fallback_locale;
  }

  fallback_locale_ = std::move(fallback_locale);
  current_locale_ = selected_locale;
  fallback_catalog_.clear();
  active_catalog_.clear();
  locale_catalogs_.clear();
  available_locales_.clear();

  std::error_code error;
  if (std::filesystem::exists(locales_directory, error)) {
    std::vector<std::filesystem::path> locale_files;
    for (const std::filesystem::directory_entry &entry :
         std::filesystem::directory_iterator(locales_directory, error)) {
      if (entry.is_regular_file(error) && entry.path().extension() == ".lang") {
        locale_files.push_back(entry.path());
      }
    }
    std::sort(locale_files.begin(), locale_files.end());
    for (const std::filesystem::path &locale_file : locale_files) {
      const std::string locale = locale_id_from_path(locale_file);
      const Catalog catalog = load_catalog_file(locale_file);
      locale_catalogs_[locale] = catalog;
      available_locales_.push_back(LocaleInfo{
          .id = locale,
          .display_name = display_name_for(locale, catalog),
      });
    }
  }

  if (const auto found = locale_catalogs_.find(fallback_locale_);
      found != locale_catalogs_.end()) {
    fallback_catalog_ = found->second;
  } else {
    fallback_catalog_ =
        load_catalog_file(locales_directory / (fallback_locale_ + ".lang"));
    if (!fallback_catalog_.empty()) {
      locale_catalogs_[fallback_locale_] = fallback_catalog_;
    }
  }
  if (fallback_catalog_.empty()) {
    LOG_WARN("Localization fallback catalog {} is missing or empty",
             (locales_directory / (fallback_locale_ + ".lang")).string());
  }

  if (!select_locale(selected_locale)) {
    LOG_WARN("Localization catalog {} is missing or empty; falling back to {}",
             selected_locale, fallback_locale_);
    (void)select_locale(fallback_locale_);
  }

  if (available_locales_.empty()) {
    available_locales_.push_back(LocaleInfo{
        .id = fallback_locale_,
        .display_name = display_name_for(fallback_locale_, fallback_catalog_),
    });
  }

  std::sort(available_locales_.begin(), available_locales_.end(),
            [](const LocaleInfo &left, const LocaleInfo &right) {
              return left.id < right.id;
            });
  available_locales_.erase(
      std::unique(available_locales_.begin(), available_locales_.end(),
                  [](const LocaleInfo &left, const LocaleInfo &right) {
                    return left.id == right.id;
                  }),
      available_locales_.end());

  // A load publishes a complete replacement catalog, including an empty
  // result. Retained consumers must observe that publication even when the
  // selected locale identifier did not change or loading failed.
  if (revision_ == revision_before_load) {
    ++revision_;
  }

  return !fallback_catalog_.empty();
}

bool LocalizationService::has_locale(std::string_view locale) const {
  return std::any_of(
      available_locales_.begin(), available_locales_.end(),
      [locale](const LocaleInfo &info) { return info.id == locale; });
}

bool LocalizationService::select_locale(std::string_view locale) {
  const std::string locale_id(locale);
  const auto found = locale_catalogs_.find(locale_id);
  if (found == locale_catalogs_.end() || found->second.empty()) {
    if (locale_id == fallback_locale_ && !fallback_catalog_.empty()) {
      const bool changed = current_locale_ != fallback_locale_ ||
                           active_catalog_ != fallback_catalog_;
      active_catalog_ = fallback_catalog_;
      current_locale_ = fallback_locale_;
      if (changed) {
        ++revision_;
      }
      return true;
    }
    return false;
  }

  const bool changed = current_locale_ != locale_id ||
                       active_catalog_ != found->second;
  active_catalog_ = found->second;
  current_locale_ = locale_id;
  if (changed) {
    ++revision_;
  }
  return true;
}

std::string
LocalizationService::locale_display_name(std::string_view locale) const {
  const auto found = std::find_if(
      available_locales_.begin(), available_locales_.end(),
      [locale](const LocaleInfo &info) { return info.id == locale; });
  return found != available_locales_.end() ? found->display_name
                                           : std::string(locale);
}

std::string LocalizationService::text(std::string_view key, std::string_view fallback) const {
  if (const std::string *value = find_value(active_catalog_, key)) {
    return *value;
  }
  if (const std::string *value = find_value(fallback_catalog_, key)) {
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

LocalizationService::Catalog
LocalizationService::load_catalog_file(const std::filesystem::path &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    return {};
  }
  const std::string text((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
  return parse_lang_text(text);
}

std::string LocalizationService::display_name_for(std::string_view locale,
                                                  const Catalog &catalog) {
  if (const std::string *name = find_value(catalog, "locale.name")) {
    return *name;
  }
  if (locale == kDefaultLocale) {
    return "English";
  }
  return std::string(locale);
}

} // namespace quader
