////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "core/localization_service.hpp"
#include "editor/metadata/host_contract.hpp"
#include "editor/tools/viewport_tool_descriptor_catalog.hpp"
#include "support/guardrail_file_scan.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace quader;

namespace {

using quader::test::guardrail::filesWithExtension;
using quader::test::guardrail::readFile;
using quader::test::guardrail::sourceFilesUnder;
using quader::test::guardrail::sourcePath;

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
    const std::filesystem::path root = sourcePath({"resources", "locales"});
    std::vector<std::pair<std::string, LocalizationService::Catalog>> catalogs;

    for (const std::filesystem::path& file : filesWithExtension(root, ".lang")) {
        catalogs.emplace_back(
                file.stem().string(),
                LocalizationService::parseLangText(readFile(file)));
    }

    std::sort(catalogs.begin(), catalogs.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.first < rhs.first;
    });
    return catalogs;
}

std::set<std::string> localizationPlaceholderTokens(std::string_view text) {
    const std::regex tokenPattern(R"regex(\{([A-Za-z_][A-Za-z0-9_]*)\})regex");
    const std::string source(text);
    std::set<std::string> tokens;
    for (std::sregex_iterator it(source.begin(), source.end(), tokenPattern), end; it != end; ++it) {
        tokens.insert((*it)[1].str());
    }
    return tokens;
}

std::set<std::string> literalLocalizationKeysReferencedByUISource() {
    const std::filesystem::path uiRoot = sourcePath({"src", "ui"});
    const std::regex directLocalizationCall(
            R"regex((?:[A-Za-z_][A-Za-z0-9_]*\.)?localization_?\.(?:text|format|imguiLabel)\(\s*"([^"]+)")regex");
    std::set<std::string> keys;

    for (const std::filesystem::path& file : sourceFilesUnder(uiRoot)) {
        const std::string source = readFile(file);
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

TEST(LocalizationResourceTests, ShellRuntimeStringsRouteThroughLocaleKeys) {
    const std::string uiSystem = readFile(sourcePath("src/ui/ui_system.cpp"));
    const std::string shortcutController = readFile(sourcePath("src/ui/viewport_shortcut_controller.cpp"));

    EXPECT_NE(uiSystem.find("ui.shell.status.ready"), std::string::npos);
    EXPECT_NE(uiSystem.find("ui.runtime.unknown_panel"), std::string::npos);
    EXPECT_NE(uiSystem.find("ui.runtime.unknown_locale"), std::string::npos);
    EXPECT_NE(uiSystem.find("ui.runtime.preferences_saved"), std::string::npos);
    EXPECT_NE(uiSystem.find("ui.runtime.viewport_panel_unavailable"), std::string::npos);
    EXPECT_EQ(uiSystem.find("runtimeFailure(\"Unknown UI panel."), std::string::npos);
    EXPECT_EQ(uiSystem.find("runtimeFailure(\"Unknown locale."), std::string::npos);
    EXPECT_EQ(uiSystem.find("runtimeStatus(false, \"Preferences saved."), std::string::npos);
    EXPECT_EQ(uiSystem.find("runtimeFailure(\"Viewport panel is not available."), std::string::npos);
    EXPECT_EQ(shortcutController.find("Apply Tool Preview"), std::string::npos);
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

TEST(LocalizationServiceTests, FormatterReplacesRepeatedTokensAndLeavesUnknownTokens) {
    const std::filesystem::path root = tempLocaleRoot("quader_localization_placeholder_format_test");
    writeTextFile(
            root / "en-US.lang",
            "locale.name=English\n"
            "debug.message=Hello {name}, {name}. Missing {missing}\n"
            "message=Selected {count} of {count}; missing {unknown}\n");

    LocalizationService localization;
    ASSERT_TRUE(localization.load(root, "en-US"));

    EXPECT_EQ(
            localization.format("debug.message", "", {{"name", "Quader"}}),
            "Hello Quader, Quader. Missing {missing}");
    EXPECT_EQ(
            localization.format("message", "", {{"count", "3"}}),
            "Selected 3 of 3; missing {unknown}");
    EXPECT_EQ(
            localization.format("missing", "Speed {speed} {speed} {unit}", {{"speed", "8"}}),
            "Speed 8 8 {unit}");

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

TEST(LocalizationResourceTests, ViewportToolCatalogHasLocaleKeys) {
    const std::vector<std::pair<std::string, LocalizationService::Catalog>> catalogs = loadResourceLocaleCatalogs();

    ASSERT_GE(catalogs.size(), 2u);
    for (const ToolDescriptor& tool : viewportToolDescriptors()) {
        std::string key("viewport.tool.");
        key += tool.id;
        key += ".label";
        for (const auto& [locale, catalog] : catalogs) {
            const auto found = catalog.find(key);
            ASSERT_NE(found, catalog.end()) << locale << " missing " << key;
            if (locale == LocalizationService::DefaultLocale) {
                EXPECT_EQ(found->second, tool.label) << key;
            }
        }
    }
}

TEST(LocalizationResourceTests, ModelingActionCatalogHasLocaleKeys) {
    const std::vector<std::pair<std::string, LocalizationService::Catalog>> catalogs = loadResourceLocaleCatalogs();

    ASSERT_GE(catalogs.size(), 2u);
    for (const quader_host_contract::HostActionDescriptor& action : quader_host_contract::action_catalog()) {
        std::string key("modeling.action.");
        key += action.id;
        key += ".label";
        for (const auto& [locale, catalog] : catalogs) {
            const auto found = catalog.find(key);
            ASSERT_NE(found, catalog.end()) << locale << " missing " << key;
            if (locale == LocalizationService::DefaultLocale) {
                EXPECT_EQ(found->second, action.label) << key;
            }
        }
    }
}

TEST(LocalizationResourceTests, ModelingShelfLocaleKeysDoNotMirrorActionLabels) {
    const std::vector<std::pair<std::string, LocalizationService::Catalog>> catalogs = loadResourceLocaleCatalogs();
    constexpr std::string_view stalePrefix = "ui.modeling_shelf.command.";
    constexpr std::string_view allowedKey = "ui.modeling_shelf.command.disabled.modal_tool";

    ASSERT_GE(catalogs.size(), 2u);
    for (const auto& [locale, catalog] : catalogs) {
        for (const auto& [key, value] : catalog) {
            (void)value;
            if (key.rfind(stalePrefix, 0) == 0) {
                EXPECT_EQ(key, allowedKey) << locale << " has stale modeling shelf command label " << key;
            }
        }
    }
}

TEST(LocalizationResourceTests, ModelingActionDisabledReasonsUseLocaleKeys) {
    const std::vector<std::pair<std::string, LocalizationService::Catalog>> catalogs = loadResourceLocaleCatalogs();
    const std::vector<std::string_view> disabledReasonKeys = {
            "modeling.action.disabled.any_selection",
            "modeling.action.disabled.delete_selection",
            "modeling.action.disabled.minimum_vertex",
            "modeling.action.disabled.minimum_vertices",
            "modeling.action.disabled.minimum_edge",
            "modeling.action.disabled.minimum_edges",
            "modeling.action.disabled.exact_edges",
            "modeling.action.disabled.minimum_face",
            "modeling.action.disabled.minimum_faces",
            "modeling.action.disabled.minimum_object",
            "modeling.action.disabled.minimum_objects",
            "modeling.action.disabled.active_element",
            "modeling.action.disabled.single_object",
    };

    ASSERT_GE(catalogs.size(), 2u);
    for (const auto& [locale, catalog] : catalogs) {
        for (std::string_view key : disabledReasonKeys) {
            EXPECT_NE(catalog.find(std::string(key)), catalog.end()) << locale << " missing " << key;
        }
    }

    const std::string source = readFile(sourcePath("src/editor/modeling/services/modeling_action_catalog.cpp"));
    EXPECT_EQ(source.find("Select something to delete."), std::string::npos);
    EXPECT_EQ(source.find("Select something to use this action."), std::string::npos);
    EXPECT_EQ(source.find("Set an active selection element."), std::string::npos);
    EXPECT_EQ(source.find("Select elements from one object."), std::string::npos);
    EXPECT_NE(source.find("disabledReasonKey"), std::string::npos);
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

TEST(LocalizationResourceTests, ResourceLocaleCatalogsKeepPlaceholderSetsInSync) {
    const std::vector<std::pair<std::string, LocalizationService::Catalog>> catalogs = loadResourceLocaleCatalogs();

    ASSERT_GE(catalogs.size(), 2u);
    const auto referenceIt = std::find_if(catalogs.begin(), catalogs.end(), [](const auto& entry) {
        return entry.first == LocalizationService::DefaultLocale;
    });
    ASSERT_NE(referenceIt, catalogs.end());
    const LocalizationService::Catalog& reference = referenceIt->second;

    for (const auto& [key, referenceValue] : reference) {
        const std::set<std::string> expectedTokens = localizationPlaceholderTokens(referenceValue);
        for (const auto& [locale, catalog] : catalogs) {
            const auto localized = catalog.find(key);
            ASSERT_NE(localized, catalog.end()) << locale << " missing " << key;
            EXPECT_EQ(localizationPlaceholderTokens(localized->second), expectedTokens)
                    << locale << " placeholder mismatch for " << key;
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
