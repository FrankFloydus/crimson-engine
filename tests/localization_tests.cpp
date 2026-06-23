////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "core/LocalizationService.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace quader;

#ifndef QUADER_SOURCE_DIR
#define QUADER_SOURCE_DIR "."
#endif

namespace {

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    EXPECT_TRUE(file.is_open()) << path.string();
    if (!file.is_open()) {
        return {};
    }

    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void writeTextFile(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(file.is_open()) << path.string();
    file << text;
}

std::filesystem::path tempLocaleRoot(std::string_view name) {
    std::filesystem::path path = std::filesystem::temp_directory_path();
    path /= std::string(name);
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
    return path;
}

std::vector<std::pair<std::string, LocalizationService::Catalog>> loadResourceLocaleCatalogs() {
    const std::filesystem::path root = std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "locales";
    std::vector<std::pair<std::string, LocalizationService::Catalog>> catalogs;

    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(root)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".lang") {
            continue;
        }

        catalogs.emplace_back(
                entry.path().stem().string(),
                LocalizationService::parseLangText(readTextFile(entry.path())));
    }

    std::sort(catalogs.begin(), catalogs.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.first < rhs.first;
    });
    return catalogs;
}

std::set<std::string> literalLocalizationKeysReferencedByUISource() {
    const std::filesystem::path uiRoot = std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui";
    const std::regex directLocalizationCall(
            R"regex(localization\.(?:text|format)\(\s*"([^"]+)")regex");
    std::set<std::string> keys;

    for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(uiRoot)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        const std::string extension = entry.path().extension().string();
        if (extension != ".cpp" && extension != ".h" && extension != ".hpp") {
            continue;
        }

        const std::string source = readTextFile(entry.path());
        for (std::sregex_iterator it(source.begin(), source.end(), directLocalizationCall), end; it != end; ++it) {
            std::size_t next = static_cast<std::size_t>(it->position() + it->length());
            while (next < source.size() && std::isspace(static_cast<unsigned char>(source[next])) != 0) {
                ++next;
            }
            if (next < source.size() && source[next] == '+') {
                continue;
            }
            keys.insert((*it)[1].str());
        }
    }

    return keys;
}

} // namespace

TEST(LocalizationServiceTests, ParsesFlatLangFilesWithEscapesAndLastValueWins) {
    const auto catalog = LocalizationService::parseLangText(
            "\xef\xbb\xbf# comment\n"
            "; another comment\n"
            " greeting = Hello\\nworld \n"
            " bad line without equals\n"
            " empty = \n"
            " greeting = Ciao\\tmondo\n");

    ASSERT_EQ(catalog.size(), 2u);
    EXPECT_EQ(catalog.at("greeting"), "Ciao\tmondo");
    EXPECT_EQ(catalog.at("empty"), "");
}

TEST(LocalizationServiceTests, LoadsFallbackCatalogAndSwitchesLocaleImmediately) {
    const std::filesystem::path root = tempLocaleRoot("quader_localization_switch_test");
    writeTextFile(
            root / "en-US.lang",
            "locale.name=English\n"
            "ui.menu.edit=Edit\n"
            "ui.menu.file=File\n"
            "fallback.only=Only English\n"
            "blank.value=\n");
    writeTextFile(
            root / "it-IT.lang",
            "locale.name=Italiano\n"
            "ui.menu.edit=Modifica\n");

    LocalizationService localization;
    ASSERT_TRUE(localization.load(root, "it-IT"));

    EXPECT_EQ(localization.currentLocale(), "it-IT");
    EXPECT_TRUE(localization.hasLocale("en-US"));
    EXPECT_TRUE(localization.hasLocale("it-IT"));
    EXPECT_EQ(localization.localeDisplayName("it-IT"), "Italiano");
    EXPECT_EQ(localization.text("ui.menu.edit", "Edit"), "Modifica");
    EXPECT_EQ(localization.text("fallback.only", "Fallback"), "Only English");
    EXPECT_EQ(localization.text("blank.value", "Fallback"), "Fallback");

    ASSERT_TRUE(localization.selectLocale("en-US"));
    EXPECT_EQ(localization.currentLocale(), "en-US");
    EXPECT_EQ(localization.text("ui.menu.edit", "Edit"), "Edit");
    EXPECT_FALSE(localization.selectLocale("missing"));
    EXPECT_EQ(localization.currentLocale(), "en-US");

    std::filesystem::remove_all(root);
}

TEST(LocalizationServiceTests, MissingSelectedLocaleFallsBackToEnglish) {
    const std::filesystem::path root = tempLocaleRoot("quader_localization_missing_selected_test");
    writeTextFile(
            root / "en-US.lang",
            "locale.name=English\n"
            "ui.menu.file=File\n");

    LocalizationService localization;
    ASSERT_TRUE(localization.load(root, "it-IT"));

    EXPECT_EQ(localization.currentLocale(), "en-US");
    EXPECT_EQ(localization.text("ui.menu.file", "Fallback"), "File");

    std::filesystem::remove_all(root);
}

TEST(LocalizationResourceTests, ShipsEnglishAndItalianCatalogs) {
    const std::filesystem::path root = std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "locales";

    LocalizationService localization;
    ASSERT_TRUE(localization.load(root, "it-IT"));

    EXPECT_EQ(localization.currentLocale(), "it-IT");
    EXPECT_EQ(localization.text("ui.menu.edit", "Edit"), "Modifica");
    EXPECT_EQ(localization.text("viewport.tool.move.label", "Move"), "Sposta");
    EXPECT_EQ(localization.text("modeling.action.bridge_faces.label", "Bridge Faces"), "Bridge facce");
    EXPECT_EQ(localization.text("ui.viewport.delete_popup.window_title", "Delete"), "Elimina");
    EXPECT_TRUE(localization.selectLocale("en-US"));
    EXPECT_EQ(localization.text("ui.menu.edit", "Edit"), "Edit");
    EXPECT_EQ(localization.text("ui.viewport.delete_popup.window_title", "Delete"), "Delete");
}

TEST(LocalizationResourceTests, ResourceLocaleCatalogsExposeTheSameKeys) {
    const std::vector<std::pair<std::string, LocalizationService::Catalog>> catalogs = loadResourceLocaleCatalogs();

    ASSERT_GE(catalogs.size(), 2u);
    for (const auto& [locale, catalog] : catalogs) {
        EXPECT_FALSE(catalog.empty()) << locale;
    }

    const LocalizationService::Catalog& reference = catalogs.front().second;
    for (const auto& [locale, catalog] : catalogs) {
        for (const auto& [key, value] : reference) {
            (void)value;
            EXPECT_NE(catalog.find(key), catalog.end()) << locale << " missing " << key;
        }
        for (const auto& [key, value] : catalog) {
            (void)value;
            EXPECT_NE(reference.find(key), reference.end()) << locale << " adds unmatched key " << key;
        }
    }
}

TEST(LocalizationResourceTests, UISourceLiteralLocalizationKeysExistInEveryCatalog) {
    const std::set<std::string> referencedKeys = literalLocalizationKeysReferencedByUISource();
    const std::vector<std::pair<std::string, LocalizationService::Catalog>> catalogs = loadResourceLocaleCatalogs();

    ASSERT_GE(catalogs.size(), 2u);
    if (referencedKeys.empty()) {
        return;
    }

    for (const std::string& key : referencedKeys) {
        for (const auto& [locale, catalog] : catalogs) {
            EXPECT_NE(catalog.find(key), catalog.end()) << locale << " missing UI localization key " << key;
        }
    }
}
