////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "api/quader_mesh_arrangement_catalog.hpp"
#include "core/localization_service.hpp"
#include "editor/metadata/host_contract.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/tools/viewport_tool_descriptor_catalog.hpp"
#include "support/guardrail_file_scan.hpp"

#include <gtest/gtest.h>

#include <QKeySequence>

#include <algorithm>
#include <array>
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
    EXPECT_EQ(localization.text("ui.menu.edit", "Edit"), "&Modifica");
    EXPECT_EQ(localization.text("ui.menu.edit.find_replace", "Find/Replace..."),
              "Trova/Sostituisci…");
    EXPECT_EQ(localization.text("ui.material_browser.accept", "Accept"),
              "Accetta");
    EXPECT_EQ(localization.text("ui.material_find_replace.title",
                                "Find/Replace Materials"),
              "Trova/Sostituisci materiali");
    EXPECT_EQ(localization.text("ui.inspector.material", "Material"),
              "Materiale");
    EXPECT_EQ(localization.text("viewport.tool.move.label", "Move"), "Sposta");
    EXPECT_EQ(localization.text("modeling.action.bridge_faces.label",
                                "Bridge Selected Faces"),
              "Crea bridge tra facce selezionate");
    EXPECT_EQ(localization.text("modeling.action.copy_pivot_from_active.label",
                                "Copy Pivot From Active"),
              "Copia pivot dall'oggetto attivo");
    EXPECT_EQ(localization.text("modeling.action.snap_meshes_to_floor.label",
                                "Snap Meshes to Floor"),
              "Aggancia mesh al pavimento");
    EXPECT_EQ(localization.text(
                  "modeling.action.snap_vertices_to_floor.label",
                  "Snap Vertices to Floor"),
              "Aggancia vertici al pavimento");
    EXPECT_EQ(localization.text(
                  "modeling.action.disabled.copy_pivot_from_active_selection",
                  "Select an active mesh and at least one other editable mesh."),
              "Seleziona una mesh attiva e almeno un'altra mesh modificabile.");
    EXPECT_EQ(localization.text("ui.viewport.delete_popup.window_title", "Delete"), "Elimina");
    EXPECT_EQ(localization.text("ui.panel.tool_settings", "Tool Settings"),
              "Impostazioni strumento");
    EXPECT_EQ(localization.text("ui.panel.layers", "Layers"), "Livelli");
    EXPECT_EQ(localization.text("ui.layers.add_selection",
                                "Add Selection to Layer"),
              "Aggiungi selezione al livello");
    EXPECT_EQ(localization.text("ui.layers.locked", "Locked"),
              "Bloccato");
    EXPECT_EQ(localization.text("ui.layers.lock_state_unlocked",
                                "Layer unlocked"),
              "Livello sbloccato");
    EXPECT_EQ(localization.text("viewport.tool.radial_array.label",
                                "Radial Array"),
              "Serie radiale");
    EXPECT_EQ(localization.text("radial_array.copies", "Number of Copies"),
              "Numero di copie");
    EXPECT_EQ(localization.text("radial_array.repeat_spacing",
                                "Repeat Spacing"),
              "Distanza tra ripetizioni");
    EXPECT_EQ(localization.text("ui.action.apply", "Apply"), "Applica");
    EXPECT_EQ(localization.text("ui.action.cancel", "Cancel"), "Annulla");
    EXPECT_TRUE(localization.select_locale("en-US"));
    EXPECT_EQ(localization.text("ui.menu.edit", "Edit"), "&Edit");
    EXPECT_EQ(localization.text("ui.menu.edit.find_replace", "Find/Replace..."),
              "Find/Replace…");
    EXPECT_EQ(localization.text("ui.material_browser.accept", "Accept"),
              "Accept");
    EXPECT_EQ(localization.text("ui.material_find_replace.title",
                                "Find/Replace Materials"),
              "Find/Replace Materials");
    EXPECT_EQ(localization.text("ui.inspector.material", "Material"),
              "Material");
    EXPECT_EQ(localization.text("modeling.action.copy_pivot_from_active.label",
                                "Copy Pivot From Active"),
              "Copy Pivot from Active Object");
    EXPECT_EQ(localization.text("modeling.action.snap_meshes_to_floor.label",
                                "Snap Meshes to Floor"),
              "Snap Meshes to Floor");
    EXPECT_EQ(localization.text(
                  "modeling.action.snap_vertices_to_floor.label",
                  "Snap Vertices to Floor"),
              "Snap Vertices to Floor");
    EXPECT_EQ(localization.text(
                  "modeling.action.disabled.copy_pivot_from_active_selection",
                  "Select an active mesh and at least one other editable mesh."),
              "Select an active mesh and at least one other editable mesh.");
    EXPECT_EQ(localization.text("ui.viewport.delete_popup.window_title", "Delete"), "Delete");
    EXPECT_EQ(localization.text("ui.panel.tool_settings", "Tool Settings"),
              "Tool Settings");
    EXPECT_EQ(localization.text("viewport.tool.radial_array.label",
                                "Radial Array"),
              "Radial Array");
    EXPECT_EQ(localization.text("radial_array.copies", "Number of Copies"),
              "Number of Copies");
    EXPECT_EQ(localization.text("radial_array.repeat_spacing",
                                "Repeat Spacing"),
              "Repeat Spacing");
    EXPECT_EQ(localization.text("ui.action.apply", "Apply"), "Apply");
    EXPECT_EQ(localization.text("ui.action.cancel", "Cancel"), "Cancel");
}

TEST(LocalizationResourceTests, TopLevelMenusExposeUniqueQtMnemonics) {
  constexpr std::array<std::string_view, 9> kTopLevelMenuKeys = {
      "ui.menu.file",    "ui.menu.edit",   "ui.menu.select",
      "ui.menu.align",   "ui.menu.cleanup", "ui.menu.tools",
      "ui.menu.create",  "ui.menu.view",   "ui.menu.layout",
  };

  for (const auto &[locale, catalog] : load_resource_locale_catalogs()) {
    SCOPED_TRACE(locale);
    std::set<std::string> mnemonics;
    for (const std::string_view key : kTopLevelMenuKeys) {
      const auto found = catalog.find(std::string(key));
      ASSERT_NE(found, catalog.end()) << key;

      const QKeySequence mnemonic = QKeySequence::mnemonic(
          QString::fromUtf8(found->second.data(),
                            static_cast<qsizetype>(found->second.size())));
      ASSERT_FALSE(mnemonic.isEmpty())
          << key << " must expose a Qt mnemonic";
      const std::string portable =
          mnemonic.toString(QKeySequence::PortableText)
              .toCaseFolded()
              .toUtf8()
              .toStdString();
      EXPECT_TRUE(mnemonics.insert(portable).second)
          << "duplicate top-level mnemonic '" << portable << "' for " << key;
    }
  }
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

TEST(LocalizationResourceTests,
     ViewportContextMenuLabelsStayCompactAndPopupScoped) {
  /// One concise popup label expected in both shipped locale catalogs.
  struct LabelExpectation {
    std::string_view action_id;
    std::string_view english;
    std::string_view italian;
  };
  constexpr std::array<LabelExpectation, 42> kExpectedLabels{{
      {"delete_selection", "Delete", "Elimina"},
      {"duplicate_meshes", "Duplicate", "Duplica"},
      {"snap_meshes_to_floor", "Snap to Floor", "Aggancia al pavimento"},
      {"invert_mesh", "Flip Mesh Normals", "Inverti normali mesh"},
      {"shade_smooth_mesh", "Shade Smooth", "Ombreggiatura morbida"},
      {"shade_flat_mesh", "Shade Flat", "Ombreggiatura piatta"},
      {"combine_mesh", "Combine Mesh", "Combina mesh"},
      {"half_cut", "Half-Cut", "Taglio a metà"},
      {"create_outer_hull", "Create Outer Hull", "Crea guscio esterno"},
      {"copy_pivot_from_active", "Copy Pivot From Active",
       "Copia pivot dall'elemento attivo"},
      {"snap_vertices_to_active", "Snap to Vertex", "Aggancia al vertice"},
      {"snap_vertices_to_floor", "Snap to Floor",
       "Aggancia al pavimento"},
      {"merge_vertices", "Merge", "Unisci"},
      {"merge_vertices_at_center", "Merge at Center", "Unisci al centro"},
      {"merge_by_distance", "Merge by Distance", "Unisci per distanza"},
      {"bevel_vertices", "Bevel Vertex", "Smussa vertice"},
      {"connect_vertices", "Connect", "Connetti"},
      {"dissolve_vertices", "Dissolve Vertex", "Dissolvi vertice"},
      {"radial_align_vertices", "Radial Align", "Allinea radialmente"},
      {"snap_edges_to_active", "Snap to Edge", "Aggancia allo spigolo"},
      {"connect_edges", "Connect Edges", "Connetti spigoli"},
      {"split_edges", "Edge Split", "Dividi spigolo"},
      {"harden_edge_normals", "Hard Normal", "Normale dura"},
      {"soften_edge_normals", "Soft Normal", "Normale morbida"},
      {"bevel_edges", "Bevel Edges", "Smussa spigoli"},
      {"bridge_edges", "Bridge", "Bridge"},
      {"interpolated_bridge_edges", "Interpolated Bridge",
       "Bridge interpolato"},
      {"dissolve_edges", "Dissolve", "Dissolvi"},
      {"merge_edges", "Merge Edges", "Unisci spigoli"},
      {"collapse_edges", "Collapse", "Collassa"},
      {"fill_hole", "Fill Hole", "Riempi foro"},
      {"radial_align_edges", "Radial Align", "Allinea radialmente"},
      {"bridge_faces", "Bridge Faces", "Bridge facce"},
      {"interpolated_bridge_faces", "Interpolated Bridge",
       "Bridge interpolato"},
      {"invert_faces", "Invert Face", "Inverti faccia"},
      {"combine_faces", "Combine Faces", "Combina facce"},
      {"collapse_faces", "Collapse Faces", "Collassa facce"},
      {"radial_align_faces", "Radial Align", "Allinea radialmente"},
      {"slice_quad", "Slice Quad", "Suddividi quad"},
      {"thicken_faces", "Thicken Faces", "Ispessisci facce"},
      {"extract_faces", "Extract Faces", "Estrai facce"},
      {"detach_faces", "Detach Faces", "Stacca facce"},
  }};

  const std::vector<std::pair<std::string, LocalizationService::Catalog>>
      catalogs = load_resource_locale_catalogs();
  const auto find_catalog = [&](std::string_view locale)
      -> const LocalizationService::Catalog * {
    const auto found = std::find_if(
        catalogs.begin(), catalogs.end(), [&](const auto &entry) {
          return entry.first == locale;
        });
    return found == catalogs.end() ? nullptr : &found->second;
  };
  const LocalizationService::Catalog *english = find_catalog("en-US");
  const LocalizationService::Catalog *italian = find_catalog("it-IT");
  ASSERT_NE(english, nullptr);
  ASSERT_NE(italian, nullptr);

  constexpr std::string_view kPrefix =
      "ui.viewport.context_menu.action.";
  for (const LabelExpectation &expected : kExpectedLabels) {
    std::string key(kPrefix);
    key.append(expected.action_id);
    key.append(".label");

    const auto english_label = english->find(key);
    ASSERT_NE(english_label, english->end()) << key;
    EXPECT_EQ(english_label->second, expected.english) << key;
    const auto italian_label = italian->find(key);
    ASSERT_NE(italian_label, italian->end()) << key;
    EXPECT_EQ(italian_label->second, expected.italian) << key;
  }

  for (const auto &[locale, catalog] : catalogs) {
    const std::size_t popup_label_count =
        static_cast<std::size_t>(std::count_if(
            catalog.begin(), catalog.end(), [&](const auto &entry) {
              return entry.first.rfind(kPrefix, 0) == 0;
            }));
    EXPECT_EQ(popup_label_count, kExpectedLabels.size())
        << locale << " must expose exactly the popup action label set";
  }

  EXPECT_EQ(english->at("modeling.action.snap_edges_to_active.label"),
            "Snap Selected Edges to Active Edge");
  EXPECT_EQ(italian->at("modeling.action.snap_edges_to_active.label"),
            "Aggancia spigoli selezionati allo spigolo attivo");

  EXPECT_EQ(english->at("ui.viewport.context_menu.section.mesh"), "Mesh");
  EXPECT_EQ(italian->at("ui.viewport.context_menu.section.mesh"), "Mesh");
  EXPECT_EQ(english->at("ui.viewport.context_menu.section.object"),
            "Object");
  EXPECT_EQ(italian->at("ui.viewport.context_menu.section.object"),
            "Oggetto");
  EXPECT_EQ(english->at("ui.viewport.context_menu.section.face"), "Face");
  EXPECT_EQ(italian->at("ui.viewport.context_menu.section.face"), "Faccia");
  EXPECT_EQ(english->at("ui.viewport.context_menu.section.edge"), "Edge");
  EXPECT_EQ(italian->at("ui.viewport.context_menu.section.edge"), "Spigolo");
  EXPECT_EQ(english->at("ui.viewport.context_menu.section.vertex"),
            "Vertex");
  EXPECT_EQ(italian->at("ui.viewport.context_menu.section.vertex"),
            "Vertice");
  EXPECT_EQ(
      english->at("ui.viewport.context_menu.command.center_selection"),
      "Center View on Selection");
  EXPECT_EQ(
      italian->at("ui.viewport.context_menu.command.center_selection"),
      "Centra la vista sulla selezione");
  EXPECT_EQ(english->at(
                "ui.viewport.context_menu.command.set_material_under_mouse"),
            "Set Material under Mouse as Active");
  EXPECT_EQ(italian->at(
                "ui.viewport.context_menu.command.set_material_under_mouse"),
            "Imposta come attivo il materiale sotto il mouse");
  EXPECT_EQ(english->at(
                "ui.viewport.context_menu.command.edit_material_under_mouse"),
            "Edit Material under Mouse");
  EXPECT_EQ(italian->at(
                "ui.viewport.context_menu.command.edit_material_under_mouse"),
            "Modifica il materiale sotto il mouse");
  EXPECT_EQ(english->at("ui.menu.edit.cut"), "Cut");
  EXPECT_EQ(italian->at("ui.menu.edit.cut"), "Taglia");
  EXPECT_EQ(english->at("keybindings.target.edit.cut.label"),
            "Cut Selection");
  EXPECT_EQ(italian->at("keybindings.target.edit.cut.label"),
            "Taglia selezione");
  EXPECT_EQ(english->at(
                "keybindings.target.viewport.material."
                "apply_active_to_selection.label"),
            "Apply Active Material under Mouse");
  EXPECT_EQ(italian->at(
                "keybindings.target.viewport.material."
                "apply_active_to_selection.label"),
            "Applica il materiale attivo sotto il mouse");
}

TEST(LocalizationResourceTests,
     ProjectHomeUsesTerseRailAndLocalizedRecentActions) {
  const std::vector<std::pair<std::string, LocalizationService::Catalog>>
      catalogs = load_resource_locale_catalogs();
  const auto find_catalog = [&](std::string_view locale)
      -> const LocalizationService::Catalog * {
    const auto found = std::find_if(
        catalogs.begin(), catalogs.end(), [&](const auto &entry) {
          return entry.first == locale;
        });
    return found == catalogs.end() ? nullptr : &found->second;
  };
  const LocalizationService::Catalog *english = find_catalog("en-US");
  const LocalizationService::Catalog *italian = find_catalog("it-IT");
  ASSERT_NE(english, nullptr);
  ASSERT_NE(italian, nullptr);

  EXPECT_EQ(english->at("ui.home.new_project"), "New");
  EXPECT_EQ(english->at("ui.home.open_project"), "Open");
  EXPECT_EQ(english->at("ui.home.recent_projects"), "Recent");
  EXPECT_EQ(english->at("ui.home.recent_last_modified"), "Last modified");
  EXPECT_EQ(english->at("ui.home.recent_size"), "Size");
  EXPECT_EQ(english->at("ui.home.recent_actions"), "Actions");
  EXPECT_EQ(english->at("ui.home.recent_action.open"), "Open");
  EXPECT_EQ(english->at("ui.home.recent_action.open_file_location"),
            "Open file location");
  EXPECT_EQ(english->at("ui.home.recent_action.copy_path"), "Copy path");
  EXPECT_EQ(english->at("ui.home.recent_action.remove"),
            "Remove from recent");

  EXPECT_EQ(italian->at("ui.home.new_project"), "Nuovo");
  EXPECT_EQ(italian->at("ui.home.open_project"), "Apri");
  EXPECT_EQ(italian->at("ui.home.recent_projects"), "Recenti");
  EXPECT_EQ(italian->at("ui.home.recent_last_modified"), "Ultima modifica");
  EXPECT_EQ(italian->at("ui.home.recent_size"), "Dimensione");
  EXPECT_EQ(italian->at("ui.home.recent_actions"), "Azioni");
  EXPECT_EQ(italian->at("ui.home.recent_action.open"), "Apri");
  EXPECT_EQ(italian->at("ui.home.recent_action.open_file_location"),
            "Apri percorso file");
  EXPECT_EQ(italian->at("ui.home.recent_action.copy_path"), "Copia percorso");
  EXPECT_EQ(italian->at("ui.home.recent_action.remove"),
            "Rimuovi dai recenti");
  EXPECT_FALSE(english->contains("ui.home.recent_location"));
  EXPECT_FALSE(italian->contains("ui.home.recent_location"));
  EXPECT_FALSE(english->contains("ui.home.remove_recent_project"));
  EXPECT_FALSE(italian->contains("ui.home.remove_recent_project"));
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
      "modeling.action.disabled.snap_meshes_to_floor_selection",
      "modeling.action.disabled.snap_vertices_to_floor_selection",
      "modeling.action.disabled.snap_vertices_to_floor_instance_edit",
      "modeling.action.disabled.snap_to_floor_active_tool",
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

TEST(LocalizationResourceTests,
     KeybindingCatalogLabelsExistInEveryLocale) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const std::filesystem::path keybindings =
      std::filesystem::temp_directory_path() /
      "quader-localization-keybinding-catalog.json";
  std::error_code ignored;
  std::filesystem::remove(keybindings, ignored);
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path(keybindings).ok());

  const std::vector<QuaderKeybindingCatalogEntry> entries =
      editor.commands().keybinding_catalog();
  const auto catalogs = load_resource_locale_catalogs();
  ASSERT_FALSE(entries.empty());
  ASSERT_GE(catalogs.size(), 2u);
  for (const QuaderKeybindingCatalogEntry &entry : entries) {
    ASSERT_FALSE(entry.localization_key.empty()) << entry.target_id;
    ASSERT_FALSE(entry.scope_localization_key.empty()) << entry.target_id;
    for (const auto &[locale, catalog] : catalogs) {
      EXPECT_NE(catalog.find(entry.localization_key), catalog.end())
          << locale << " missing keybinding label " << entry.localization_key
          << " for " << entry.target_id;
      EXPECT_NE(catalog.find(entry.scope_localization_key), catalog.end())
          << locale << " missing keybinding scope "
          << entry.scope_localization_key << " for " << entry.target_id;
    }
  }
  std::filesystem::remove(keybindings, ignored);
}

TEST(LocalizationResourceTests,
     KeybindingCatalogLabelsAreRecognizableAndUniqueInEveryLocale) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const std::filesystem::path keybindings =
      std::filesystem::temp_directory_path() /
      "quader-localization-recognizable-keybindings.json";
  std::error_code ignored;
  std::filesystem::remove(keybindings, ignored);
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path(keybindings).ok());

  const std::vector<QuaderKeybindingCatalogEntry> entries =
      editor.commands().keybinding_catalog();
  const auto catalogs = load_resource_locale_catalogs();
  const std::set<std::string> obscure_english_labels{
      "Back",     "Bridge",  "Center",   "Collapse", "Connect",
      "Copy",     "Cut",     "Delete",   "Dissolve", "Duplicate",
      "Front",    "Knife",   "Left",     "Max",      "Merge",
      "Min",      "Mirror",  "Move",     "Paste",    "Poly",
      "Radial Align",         "Rendered", "Right",    "Rotate",
      "Scale",    "Select",  "Shaded",   "Top",      "Wireframe",
      "X",        "Y",       "Z",
  };

  for (const auto &[locale, catalog] : catalogs) {
    std::set<std::string> visible_labels;
    for (const QuaderKeybindingCatalogEntry &entry : entries) {
      const auto localized = catalog.find(entry.localization_key);
      ASSERT_NE(localized, catalog.end()) << locale << " missing "
                                           << entry.localization_key;
      EXPECT_TRUE(visible_labels.insert(localized->second).second)
          << locale << " duplicates visible command label \""
          << localized->second << "\" for " << entry.action_id;
      if (locale == LocalizationService::kDefaultLocale) {
        EXPECT_FALSE(obscure_english_labels.contains(localized->second))
            << entry.action_id << " still renders as the generic label \""
            << localized->second << "\"";
      }
    }
  }
  std::filesystem::remove(keybindings, ignored);
}

TEST(LocalizationResourceTests,
     MeshArrangementKeybindingLabelsAreFullAndUniqueInEveryLocale) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const std::filesystem::path keybindings =
      std::filesystem::temp_directory_path() /
      "quader-localization-arrangement-keybindings.json";
  std::error_code ignored;
  std::filesystem::remove(keybindings, ignored);
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path(keybindings).ok());

  const std::vector<QuaderKeybindingCatalogEntry> entries =
      editor.commands().keybinding_catalog();
  const auto catalogs = load_resource_locale_catalogs();
  /// Describes the expected keybinding labels for one resource locale.
  struct LocaleExpectation {
    std::string_view locale;
    std::array<std::string_view, 15> labels;
  };
  const std::array<LocaleExpectation, 2> expectations{{
      {
          "en-US",
          {
              "Align X Minimum",
              "Align X Center",
              "Align X Maximum",
              "Align Y Minimum",
              "Align Y Center",
              "Align Y Maximum",
              "Align Z Minimum",
              "Align Z Center",
              "Align Z Maximum",
              "Distribute Centers Along X",
              "Distribute Centers Along Y",
              "Distribute Centers Along Z",
              "Distribute Gaps Along X",
              "Distribute Gaps Along Y",
              "Distribute Gaps Along Z",
          },
      },
      {
          "it-IT",
          {
              "Allinea X al minimo",
              "Allinea X al centro",
              "Allinea X al massimo",
              "Allinea Y al minimo",
              "Allinea Y al centro",
              "Allinea Y al massimo",
              "Allinea Z al minimo",
              "Allinea Z al centro",
              "Allinea Z al massimo",
              "Distribuisci centri lungo X",
              "Distribuisci centri lungo Y",
              "Distribuisci centri lungo Z",
              "Distribuisci spazi lungo X",
              "Distribuisci spazi lungo Y",
              "Distribuisci spazi lungo Z",
          },
      },
  }};

  const auto arrangements = quader_mesh_arrangement_catalog();
  ASSERT_EQ(arrangements.size(), expectations.front().labels.size());
  for (const LocaleExpectation &expectation : expectations) {
    const auto locale = std::find_if(
        catalogs.begin(), catalogs.end(), [&](const auto &candidate) {
          return candidate.first == expectation.locale;
        });
    ASSERT_NE(locale, catalogs.end()) << expectation.locale;

    std::set<std::string> localized_labels;
    std::set<std::string> fallback_labels;
    for (std::size_t index = 0; index < arrangements.size(); ++index) {
      const QuaderMeshArrangementDescriptor &arrangement = arrangements[index];
      const auto entry = std::find_if(
          entries.begin(), entries.end(), [&](const auto &candidate) {
            return std::find(candidate.target_ids.begin(),
                             candidate.target_ids.end(),
                             arrangement.command_id.string()) !=
                   candidate.target_ids.end();
          });
      ASSERT_NE(entry, entries.end()) << arrangement.command_id.string();
      EXPECT_EQ(entry->localization_key,
                arrangement.keybinding_localization_key);
      if (expectation.locale == "en-US") {
        EXPECT_EQ(entry->label, expectation.labels[index]);
      }
      EXPECT_TRUE(fallback_labels.insert(entry->label).second) << entry->label;

      const auto localized = locale->second.find(entry->localization_key);
      ASSERT_NE(localized, locale->second.end()) << entry->localization_key;
      EXPECT_EQ(localized->second, expectation.labels[index]);
      EXPECT_TRUE(localized_labels.insert(localized->second).second)
          << expectation.locale << " duplicates " << localized->second;
    }
  }
  std::filesystem::remove(keybindings, ignored);
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
