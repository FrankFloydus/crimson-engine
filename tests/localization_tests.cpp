////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "core/LocalizationService.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

using namespace quader;

#ifndef QUADER_SOURCE_DIR
#define QUADER_SOURCE_DIR "."
#endif

namespace {

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
    EXPECT_EQ(localization.imguiLabel("ui.menu.edit", "ui.menu.edit", "Edit"), "Modifica###ui.menu.edit");

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
    EXPECT_TRUE(localization.selectLocale("en-US"));
    EXPECT_EQ(localization.text("ui.menu.edit", "Edit"), "Edit");
}
