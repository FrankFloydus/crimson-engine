////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_mesh_arrangement_catalog.hpp"
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

using quader::test::guardrail::files_with_extension;
using quader::test::guardrail::read_file;
using quader::test::guardrail::source_files_under;
using quader::test::guardrail::source_path;

void write_text_file(const std::filesystem::path &path, std::string_view text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  ASSERT_TRUE(file.is_open()) << path.string();
  file << text;
}

std::filesystem::path temp_locale_root(std::string_view name) {
  std::filesystem::path path = std::filesystem::temp_directory_path();
  path /= std::string(name);
  std::filesystem::remove_all(path);
  std::filesystem::create_directories(path);
  return path;
}

std::vector<std::pair<std::string, LocalizationService::Catalog>>
load_resource_locale_catalogs() {
  const std::filesystem::path root = source_path({"resources", "locales"});
  std::vector<std::pair<std::string, LocalizationService::Catalog>> catalogs;

  for (const std::filesystem::path &file :
       files_with_extension(root, ".lang")) {
    catalogs.emplace_back(
        file.stem().string(),
        LocalizationService::parse_lang_text(read_file(file)));
  }

  std::sort(
      catalogs.begin(), catalogs.end(),
      [](const auto &lhs, const auto &rhs) { return lhs.first < rhs.first; });
  return catalogs;
}

std::set<std::string> localization_placeholder_tokens(std::string_view text) {
  const std::regex token_pattern(R"regex(\{([A-Za-z_][A-Za-z0-9_]*)\})regex");
  const std::string source(text);
  std::set<std::string> tokens;
  for (std::sregex_iterator it(source.begin(), source.end(), token_pattern),
       end;
       it != end; ++it) {
    tokens.insert((*it)[1].str());
  }
  return tokens;
}

std::set<std::string> literal_localization_keys_referenced_by_ui_source() {
  const std::filesystem::path ui_root = source_path({"src", "ui"});
  const std::regex direct_localization_call(
      R"regex((?:[A-Za-z_][A-Za-z0-9_]*\.)?localization_?\.(?:text|format)\(\s*"([^"]+)")regex");
  std::set<std::string> keys;

  for (const std::filesystem::path &file : source_files_under(ui_root)) {
    const std::string source = read_file(file);
    for (std::sregex_iterator
             it(source.begin(), source.end(), direct_localization_call),
         end;
         it != end; ++it) {
      std::size_t next =
          static_cast<std::size_t>(it->position() + it->length());
      while (next < source.size() &&
             std::isspace(static_cast<unsigned char>(source[next])) != 0) {
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
  const std::string ui_system = read_file(source_path("src/ui/ui_system.cpp"));
  const std::string shortcut_controller =
      read_file(source_path("src/ui/viewport_shortcut_controller.cpp"));

  EXPECT_NE(ui_system.find("ui.runtime.unknown_panel"), std::string::npos);
  EXPECT_NE(ui_system.find("ui.runtime.unknown_locale"), std::string::npos);
  EXPECT_NE(ui_system.find("ui.runtime.preferences_saved"), std::string::npos);
  EXPECT_NE(ui_system.find("ui.runtime.viewport_panel_unavailable"),
            std::string::npos);
  EXPECT_EQ(ui_system.find("runtime_failure(\"Unknown UI panel."),
            std::string::npos);
  EXPECT_EQ(ui_system.find("runtime_failure(\"Unknown locale."),
            std::string::npos);
  EXPECT_EQ(ui_system.find("runtime_status(false, \"Preferences saved."),
            std::string::npos);
  EXPECT_EQ(ui_system.find("runtime_failure(\"Viewport panel is not available."),
            std::string::npos);
  EXPECT_EQ(shortcut_controller.find("Apply Tool Preview"), std::string::npos);
}

} // namespace

TEST(LocalizationServiceTests, ParsesFlatLangFilesWithEscapesAndLastValueWins) {
  const auto catalog =
      LocalizationService::parse_lang_text("\xef\xbb\xbf# comment\n"
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
  const std::filesystem::path root =
      temp_locale_root("quader_localization_switch_test");
  write_text_file(root / "en-US.lang", "locale.name=English\n"
                                       "ui.menu.edit=Edit\n"
                                       "ui.menu.file=File\n"
                                       "fallback.only=Only English\n"
                                       "blank.value=\n");
  write_text_file(root / "it-IT.lang", "locale.name=Italiano\n"
                                       "ui.menu.edit=Modifica\n");

  LocalizationService localization;
  ASSERT_TRUE(localization.load(root, "it-IT"));

  EXPECT_EQ(localization.current_locale(), "it-IT");
  EXPECT_TRUE(localization.has_locale("en-US"));
  EXPECT_TRUE(localization.has_locale("it-IT"));
  EXPECT_EQ(localization.locale_display_name("it-IT"), "Italiano");
  EXPECT_EQ(localization.text("ui.menu.edit", "Edit"), "Modifica");
  EXPECT_EQ(localization.text("fallback.only", "Fallback"), "Only English");
  EXPECT_EQ(localization.text("blank.value", "Fallback"), "Fallback");

  ASSERT_TRUE(localization.select_locale("en-US"));
  EXPECT_EQ(localization.current_locale(), "en-US");
  EXPECT_EQ(localization.text("ui.menu.edit", "Edit"), "Edit");
  EXPECT_FALSE(localization.select_locale("missing"));
  EXPECT_EQ(localization.current_locale(), "en-US");

  std::filesystem::remove_all(root);
}

TEST(LocalizationServiceTests, MissingSelectedLocaleFallsBackToEnglish) {
  const std::filesystem::path root =
      temp_locale_root("quader_localization_missing_selected_test");
  write_text_file(root / "en-US.lang", "locale.name=English\n"
                                       "ui.menu.file=File\n");

  LocalizationService localization;
  ASSERT_TRUE(localization.load(root, "it-IT"));

  EXPECT_EQ(localization.current_locale(), "en-US");
  EXPECT_EQ(localization.text("ui.menu.file", "Fallback"), "File");

  std::filesystem::remove_all(root);
}

TEST(LocalizationServiceTests, FormatterReplacesRepeatedTokensAndLeavesUnknownTokens) {
  const std::filesystem::path root =
      temp_locale_root("quader_localization_placeholder_format_test");
  write_text_file(root / "en-US.lang",
                  "locale.name=English\n"
                  "debug.message=Hello {name}, {name}. Missing {missing}\n"
                  "message=Selected {count} of {count}; missing {unknown}\n");

  LocalizationService localization;
  ASSERT_TRUE(localization.load(root, "en-US"));

  EXPECT_EQ(localization.format("debug.message", "", {{"name", "Quader"}}),
            "Hello Quader, Quader. Missing {missing}");
  EXPECT_EQ(localization.format("message", "", {{"count", "3"}}),
            "Selected 3 of 3; missing {unknown}");
  EXPECT_EQ(localization.format("missing", "Speed {speed} {speed} {unit}",
                                {{"speed", "8"}}),
            "Speed 8 8 {unit}");

  std::filesystem::remove_all(root);
}

TEST(LocalizationResourceTests, ShipsEnglishAndItalianCatalogs) {
    const std::filesystem::path root = std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "locales";

    LocalizationService localization;
    ASSERT_TRUE(localization.load(root, "it-IT"));

    EXPECT_EQ(localization.current_locale(), "it-IT");
    EXPECT_EQ(localization.text("ui.menu.edit", "Edit"), "Modifica");
    EXPECT_EQ(localization.text("viewport.tool.move.label", "Move"), "Sposta");
    EXPECT_EQ(localization.text("modeling.action.bridge_faces.label", "Bridge Faces"), "Bridge facce");
    EXPECT_EQ(localization.text("modeling.action.copy_pivot_from_active.label",
                                "Copy Pivot From Active"),
              "Copia pivot dall'elemento attivo");
    EXPECT_EQ(localization.text(
                  "modeling.action.disabled.copy_pivot_from_active_selection",
                  "Select an active mesh and at least one other editable mesh."),
              "Seleziona una mesh attiva e almeno un'altra mesh modificabile.");
    EXPECT_EQ(localization.text("ui.viewport.delete_popup.window_title", "Delete"), "Elimina");
    EXPECT_EQ(localization.text("ui.panel.tool_settings", "Tool Settings"),
              "Impostazioni strumento");
    EXPECT_EQ(localization.text("ui.action.apply", "Apply"), "Applica");
    EXPECT_EQ(localization.text("ui.action.cancel", "Cancel"), "Annulla");
    EXPECT_TRUE(localization.select_locale("en-US"));
    EXPECT_EQ(localization.text("ui.menu.edit", "Edit"), "Edit");
    EXPECT_EQ(localization.text("modeling.action.copy_pivot_from_active.label",
                                "Copy Pivot From Active"),
              "Copy Pivot From Active");
    EXPECT_EQ(localization.text(
                  "modeling.action.disabled.copy_pivot_from_active_selection",
                  "Select an active mesh and at least one other editable mesh."),
              "Select an active mesh and at least one other editable mesh.");
    EXPECT_EQ(localization.text("ui.viewport.delete_popup.window_title", "Delete"), "Delete");
    EXPECT_EQ(localization.text("ui.panel.tool_settings", "Tool Settings"),
              "Tool Settings");
    EXPECT_EQ(localization.text("ui.action.apply", "Apply"), "Apply");
    EXPECT_EQ(localization.text("ui.action.cancel", "Cancel"), "Cancel");
}

TEST(LocalizationResourceTests, ViewportToolCatalogHasLocaleKeys) {
  const std::vector<std::pair<std::string, LocalizationService::Catalog>>
      catalogs = load_resource_locale_catalogs();

  ASSERT_GE(catalogs.size(), 2u);
  for (const ToolDescriptor &tool : viewport_tool_descriptors()) {
    std::string key("viewport.tool.");
    key += tool.id;
    key += ".label";
    for (const auto &[locale, catalog] : catalogs) {
      const auto found = catalog.find(key);
      ASSERT_NE(found, catalog.end()) << locale << " missing " << key;
      if (locale == LocalizationService::kDefaultLocale) {
        EXPECT_EQ(found->second, tool.label) << key;
      }
    }
  }
}

TEST(LocalizationResourceTests, MeshArrangementCatalogHasLocaleKeys) {
  const std::vector<std::pair<std::string, LocalizationService::Catalog>>
      catalogs = load_resource_locale_catalogs();

  ASSERT_GE(catalogs.size(), 2u);
  for (const QuaderMeshArrangementDescriptor &action :
       quader_mesh_arrangement_catalog()) {
    for (const auto &[locale, catalog] : catalogs) {
      const auto found = catalog.find(std::string(action.localization_key));
      ASSERT_NE(found, catalog.end())
          << locale << " missing " << action.localization_key;
      if (locale == LocalizationService::kDefaultLocale) {
        EXPECT_EQ(found->second, action.fallback_label)
            << action.localization_key;
      }
    }
  }
}

TEST(LocalizationResourceTests, ModelingActionCatalogHasLocaleKeys) {
  const std::vector<std::pair<std::string, LocalizationService::Catalog>>
      catalogs = load_resource_locale_catalogs();

  ASSERT_GE(catalogs.size(), 2u);
  for (const quader_host_contract::HostActionDescriptor &action :
       quader_host_contract::action_catalog()) {
    std::string key("modeling.action.");
    key += action.id;
    key += ".label";
    for (const auto &[locale, catalog] : catalogs) {
      const auto found = catalog.find(key);
      ASSERT_NE(found, catalog.end()) << locale << " missing " << key;
      if (locale == LocalizationService::kDefaultLocale) {
        EXPECT_EQ(found->second, action.label) << key;
      }
    }
    }
}

TEST(LocalizationResourceTests, ModelingShelfLocaleKeysDoNotMirrorActionLabels) {
  const std::vector<std::pair<std::string, LocalizationService::Catalog>>
      catalogs = load_resource_locale_catalogs();
  constexpr std::string_view kStalePrefix = "ui.modeling_shelf.command.";
  constexpr std::string_view kAllowedKey =
      "ui.modeling_shelf.command.disabled.modal_tool";

  ASSERT_GE(catalogs.size(), 2u);
  for (const auto &[locale, catalog] : catalogs) {
    for (const auto &[key, value] : catalog) {
      (void)value;
      if (key.rfind(kStalePrefix, 0) == 0) {
        EXPECT_EQ(key, kAllowedKey)
            << locale << " has stale modeling shelf command label " << key;
      }
    }
    }
}

TEST(LocalizationResourceTests, ModelingActionDisabledReasonsUseLocaleKeys) {
  const std::vector<std::pair<std::string, LocalizationService::Catalog>>
      catalogs = load_resource_locale_catalogs();
  const std::vector<std::string_view> disabled_reason_keys = {
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
      "modeling.action.disabled.copy_pivot_from_active_selection",
  };

  ASSERT_GE(catalogs.size(), 2u);
  for (const auto &[locale, catalog] : catalogs) {
    for (std::string_view key : disabled_reason_keys) {
      EXPECT_NE(catalog.find(std::string(key)), catalog.end())
          << locale << " missing " << key;
    }
    }

    const std::string source = read_file(source_path(
        "src/editor/modeling/services/modeling_action_catalog.cpp"));
    EXPECT_EQ(source.find("Select something to delete."), std::string::npos);
    EXPECT_EQ(source.find("Select something to use this action."), std::string::npos);
    EXPECT_EQ(source.find("Set an active selection element."), std::string::npos);
    EXPECT_EQ(source.find("Select elements from one object."), std::string::npos);
    EXPECT_NE(source.find("disabled_reason_key"), std::string::npos);
}

TEST(LocalizationResourceTests, ResourceLocaleCatalogsExposeTheSameKeys) {
  const std::vector<std::pair<std::string, LocalizationService::Catalog>>
      catalogs = load_resource_locale_catalogs();

  ASSERT_GE(catalogs.size(), 2u);
  for (const auto &[locale, catalog] : catalogs) {
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
  const std::vector<std::pair<std::string, LocalizationService::Catalog>>
      catalogs = load_resource_locale_catalogs();

  ASSERT_GE(catalogs.size(), 2u);
  const auto reference_it =
      std::find_if(catalogs.begin(), catalogs.end(), [](const auto &entry) {
        return entry.first == LocalizationService::kDefaultLocale;
      });
  ASSERT_NE(reference_it, catalogs.end());
  const LocalizationService::Catalog &reference = reference_it->second;

  for (const auto &[key, referenceValue] : reference) {
    const std::set<std::string> expected_tokens =
        localization_placeholder_tokens(referenceValue);
    for (const auto &[locale, catalog] : catalogs) {
      const auto localized = catalog.find(key);
      ASSERT_NE(localized, catalog.end()) << locale << " missing " << key;
      EXPECT_EQ(localization_placeholder_tokens(localized->second),
                expected_tokens)
          << locale << " placeholder mismatch for " << key;
    }
    }
}

TEST(LocalizationResourceTests, UISourceLiteralLocalizationKeysExistInEveryCatalog) {
  const std::set<std::string> referenced_keys =
      literal_localization_keys_referenced_by_ui_source();
  const std::vector<std::pair<std::string, LocalizationService::Catalog>>
      catalogs = load_resource_locale_catalogs();

  ASSERT_GE(catalogs.size(), 2u);
  if (referenced_keys.empty()) {
    return;
  }

  for (const std::string &key : referenced_keys) {
    for (const auto &[locale, catalog] : catalogs) {
      EXPECT_NE(catalog.find(key), catalog.end())
          << locale << " missing UI localization key " << key;
    }
  }
}
