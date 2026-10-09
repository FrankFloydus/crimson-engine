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
  const std::regex qtext_localization_call(
      R"regex(\bqtext\(\s*(?:(?:[A-Za-z_][A-Za-z0-9_]*)\.)?localization_?\s*,\s*"([^"]+)")regex");
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
    for (std::sregex_iterator
             it(source.begin(), source.end(), qtext_localization_call),
         end;
         it != end; ++it) {
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
  const std::uint64_t loaded_revision = localization.revision();
  EXPECT_GT(loaded_revision, 0u);

  EXPECT_EQ(localization.current_locale(), "it-IT");
  EXPECT_TRUE(localization.has_locale("en-US"));
  EXPECT_TRUE(localization.has_locale("it-IT"));
  EXPECT_EQ(localization.locale_display_name("it-IT"), "Italiano");
  EXPECT_EQ(localization.text("ui.menu.edit", "Edit"), "Modifica");
  EXPECT_EQ(localization.text("fallback.only", "Fallback"), "Only English");
  EXPECT_EQ(localization.text("blank.value", "Fallback"), "Fallback");

  ASSERT_TRUE(localization.select_locale("en-US"));
  EXPECT_GT(localization.revision(), loaded_revision);
  const std::uint64_t english_revision = localization.revision();
  ASSERT_TRUE(localization.select_locale("en-US"));
  EXPECT_EQ(localization.revision(), english_revision);
  EXPECT_EQ(localization.current_locale(), "en-US");
  EXPECT_EQ(localization.text("ui.menu.edit", "Edit"), "Edit");
  EXPECT_FALSE(localization.select_locale("missing"));
  EXPECT_EQ(localization.revision(), english_revision);
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

TEST(LocalizationServiceTests,
     ReloadPublishesCatalogChangesWithoutChangingLocaleId) {
  const std::filesystem::path root =
      temp_locale_root("quader_localization_reload_revision_test");
  write_text_file(root / "en-US.lang", "locale.name=English\n"
                                       "ui.menu.file=File\n");

  LocalizationService localization;
  ASSERT_TRUE(localization.load(root, "en-US"));
  const std::uint64_t initial_revision = localization.revision();

  write_text_file(root / "en-US.lang", "locale.name=English\n"
                                       "ui.menu.file=Reloaded File\n");
  ASSERT_TRUE(localization.load(root, "en-US"));
  EXPECT_EQ(localization.current_locale(), "en-US");
  EXPECT_GT(localization.revision(), initial_revision);
  EXPECT_EQ(localization.text("ui.menu.file", "Fallback"), "Reloaded File");

  const std::uint64_t reloaded_revision = localization.revision();
  std::filesystem::remove_all(root);
  EXPECT_FALSE(localization.load(root, "en-US"));
  EXPECT_GT(localization.revision(), reloaded_revision);
  EXPECT_EQ(localization.text("ui.menu.file", "Fallback"), "Fallback");
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
    EXPECT_EQ(localization.text("ui.menu.object.materials.find_replace",
                                "Find and Replace Materials"),
              "Trova e sostituisci materiali");
    EXPECT_EQ(localization.text("ui.menu.mesh", "Mesh"), "Me&sh");
    EXPECT_EQ(localization.text("ui.menu.vertex", "Vertex"), "Ver&tice");
    EXPECT_EQ(localization.text("ui.menu.edge", "Edge"), "Spi&golo");
    EXPECT_EQ(localization.text("ui.menu.face", "Face"), "Facc&ia");
    EXPECT_EQ(localization.text("ui.menu.uv", "UV"), "&UV");
    EXPECT_EQ(localization.text("ui.menu.uv.align", "Align"), "Allinea");
    EXPECT_EQ(localization.text("ui.menu.uv.align.to_grid", "To Grid"),
              "Alla griglia");
    EXPECT_EQ(localization.text("ui.menu.uv.align.to_face", "To Face"),
              "Alla faccia");
    EXPECT_EQ(localization.text("ui.menu.uv.align.to_view", "To View"),
              "Alla vista");
    EXPECT_EQ(localization.text("ui.menu.uv.rotate_cw", "Rotate CW"),
              "Ruota in senso orario");
    EXPECT_EQ(localization.text("ui.menu.uv.rotate_ccw", "Rotate CCW"),
              "Ruota in senso antiorario");
    EXPECT_EQ(localization.text("ui.menu.uv.flip", "Flip"), "Ribalta");
    EXPECT_EQ(localization.text("ui.menu.uv.flip.horizontal", "Horizontal"),
              "Orizzontale");
    EXPECT_EQ(localization.text("ui.menu.uv.flip.vertical", "Vertical"),
              "Verticale");
    EXPECT_EQ(localization.text("ui.menu.uv.fit", "Fit"), "Adatta");
    EXPECT_EQ(localization.text("ui.menu.uv.fit.both", "Both"),
              "Entrambi gli assi");
    EXPECT_EQ(localization.text("ui.menu.uv.fit.x", "X"), "Asse X");
    EXPECT_EQ(localization.text("ui.menu.uv.fit.y", "Y"), "Asse Y");
    EXPECT_EQ(localization.text("ui.menu.uv.justify", "Justify"),
              "Giustifica");
    EXPECT_EQ(localization.text("ui.menu.uv.justify.left", "Left"),
              "Sinistra");
    EXPECT_EQ(localization.text("ui.menu.uv.justify.right", "Right"),
              "Destra");
    EXPECT_EQ(localization.text("ui.menu.uv.justify.top", "Top"), "Alto");
    EXPECT_EQ(localization.text("ui.menu.uv.justify.bottom", "Bottom"),
              "Basso");
    EXPECT_EQ(localization.text("ui.menu.uv.justify.center", "Center"),
              "Centro");
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
              "Bridge facce");
    EXPECT_EQ(localization.text("modeling.action.copy_pivot_from_active.label",
                                "Copy Pivot From Active"),
              "Copia pivot dall'attivo");
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
    EXPECT_EQ(localization.text("ui.viewport.settings", "Settings"),
              "Impostazioni");
    EXPECT_EQ(localization.text("ui.panel.face_texture", "Face Texture"),
              "Texture faccia");
    EXPECT_EQ(localization.text("ui.material_browser.assign", "Assign"),
              "Assegna");
    EXPECT_EQ(localization.text("ui.panel.layers", "Layers"), "Livelli");
    EXPECT_EQ(localization.text("ui.layers.add_selection",
                                "Add Selection to Layer"),
              "Aggiungi oggetti al livello");
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
    EXPECT_EQ(localization.text("curve.solidify", "Solidify"),
              "Solidifica");
    EXPECT_EQ(localization.text("curve.solidify.resolution", "Resolution"),
              "Risoluzione");
    EXPECT_EQ(localization.text("curve.solidify.radius", "Radius"),
              "Raggio");
    EXPECT_EQ(localization.text("curve.solidify.apply", "Apply Solidify"),
              "Applica solidificazione");
    EXPECT_EQ(localization.text("ui.action.apply", "Apply"), "Applica");
    EXPECT_EQ(localization.text("ui.action.cancel", "Cancel"), "Annulla");
    EXPECT_TRUE(localization.select_locale("en-US"));
    EXPECT_EQ(localization.text("ui.menu.edit", "Edit"), "&Edit");
    EXPECT_EQ(localization.text("ui.menu.object.materials.find_replace",
                                "Find and Replace Materials"),
              "Find and Replace Materials");
    EXPECT_EQ(localization.text("ui.menu.mesh", "Mesh"), "&Mesh");
    EXPECT_EQ(localization.text("ui.menu.vertex", "Vertex"), "&Vertex");
    EXPECT_EQ(localization.text("ui.menu.edge", "Edge"), "Ed&ge");
    EXPECT_EQ(localization.text("ui.menu.face", "Face"), "Fa&ce");
    EXPECT_EQ(localization.text("ui.menu.uv", "UV"), "&UV");
    EXPECT_EQ(localization.text("ui.menu.uv.align", "Align"), "Align");
    EXPECT_EQ(localization.text("ui.menu.uv.align.to_grid", "To Grid"),
              "To Grid");
    EXPECT_EQ(localization.text("ui.menu.uv.align.to_face", "To Face"),
              "To Face");
    EXPECT_EQ(localization.text("ui.menu.uv.align.to_view", "To View"),
              "To View");
    EXPECT_EQ(localization.text("ui.menu.uv.rotate_cw", "Rotate CW"),
              "Rotate CW");
    EXPECT_EQ(localization.text("ui.menu.uv.rotate_ccw", "Rotate CCW"),
              "Rotate CCW");
    EXPECT_EQ(localization.text("ui.menu.uv.flip", "Flip"), "Flip");
    EXPECT_EQ(localization.text("ui.menu.uv.flip.horizontal", "Horizontal"),
              "Horizontal");
    EXPECT_EQ(localization.text("ui.menu.uv.flip.vertical", "Vertical"),
              "Vertical");
    EXPECT_EQ(localization.text("ui.menu.uv.fit", "Fit"), "Fit");
    EXPECT_EQ(localization.text("ui.menu.uv.fit.both", "Both"), "Both");
    EXPECT_EQ(localization.text("ui.menu.uv.fit.x", "X"), "X");
    EXPECT_EQ(localization.text("ui.menu.uv.fit.y", "Y"), "Y");
    EXPECT_EQ(localization.text("ui.menu.uv.justify", "Justify"),
              "Justify");
    EXPECT_EQ(localization.text("ui.menu.uv.justify.left", "Left"), "Left");
    EXPECT_EQ(localization.text("ui.menu.uv.justify.right", "Right"),
              "Right");
    EXPECT_EQ(localization.text("ui.menu.uv.justify.top", "Top"), "Top");
    EXPECT_EQ(localization.text("ui.menu.uv.justify.bottom", "Bottom"),
              "Bottom");
    EXPECT_EQ(localization.text("ui.menu.uv.justify.center", "Center"),
              "Center");
    EXPECT_EQ(localization.text("ui.material_browser.accept", "Accept"),
              "Accept");
    EXPECT_EQ(localization.text("ui.material_find_replace.title",
                                "Find/Replace Materials"),
              "Find/Replace Materials");
    EXPECT_EQ(localization.text("ui.inspector.material", "Material"),
              "Material");
    EXPECT_EQ(localization.text("modeling.action.copy_pivot_from_active.label",
                                "Copy Pivot From Active"),
              "Copy Pivot from Active");
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
    EXPECT_EQ(localization.text("ui.viewport.settings", "Settings"),
              "Settings");
    EXPECT_EQ(localization.text("ui.panel.face_texture", "Face Texture"),
              "Face Texture");
    EXPECT_EQ(localization.text("ui.material_browser.assign", "Assign"),
              "Assign");
    EXPECT_EQ(localization.text("viewport.tool.radial_array.label",
                                "Radial Array"),
              "Radial Array");
    EXPECT_EQ(localization.text("radial_array.copies", "Number of Copies"),
              "Number of Copies");
    EXPECT_EQ(localization.text("radial_array.repeat_spacing",
                                "Repeat Spacing"),
              "Repeat Spacing");
    EXPECT_EQ(localization.text("curve.solidify", "Solidify"),
              "Solidify");
    EXPECT_EQ(localization.text("curve.solidify.resolution", "Resolution"),
              "Resolution");
    EXPECT_EQ(localization.text("curve.solidify.radius", "Radius"),
              "Radius");
    EXPECT_EQ(localization.text("curve.solidify.apply", "Apply Solidify"),
              "Apply Solidify");
    EXPECT_EQ(localization.text("ui.action.apply", "Apply"), "Apply");
    EXPECT_EQ(localization.text("ui.action.cancel", "Cancel"), "Cancel");
}

TEST(LocalizationResourceTests, TopLevelMenusExposeUniqueQtMnemonics) {
  constexpr std::array<std::string_view, 13> kTopLevelMenuKeys = {
      "ui.menu.file",   "ui.menu.edit",   "ui.menu.select",
      "ui.menu.create", "ui.menu.object", "ui.menu.mesh",
      "ui.menu.vertex", "ui.menu.edge",   "ui.menu.face",
      "ui.menu.uv",     "ui.menu.view",   "ui.menu.window",
      "ui.menu.help",
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
  constexpr std::array<LabelExpectation, 54> kExpectedLabels{{
      {"delete_selection", "Delete", "Elimina"},
      {"duplicate_meshes", "Duplicate", "Duplica"},
      {"snap_meshes_to_floor", "Snap to Floor", "Aggancia al pavimento"},
      {"invert_mesh", "Flip Normals", "Inverti normali"},
      {"mirror_selection_x", "Mirror X", "Specchia X"},
      {"mirror_selection_y", "Mirror Y", "Specchia Y"},
      {"mirror_selection_z", "Mirror Z", "Specchia Z"},
      {"flip_horizontal", "Flip Horizontally", "Ribalta orizzontalmente"},
      {"flip_vertical", "Flip Vertically", "Ribalta verticalmente"},
      {"bake_location", "Bake Location", "Applica posizione"},
      {"bake_rotation", "Bake Rotation", "Applica rotazione"},
      {"bake_scale", "Bake Scale", "Applica scala"},
      {"bake_transforms", "Bake All", "Applica tutto"},
      {"shade_smooth_mesh", "Shade Smooth", "Ombreggiatura morbida"},
      {"shade_flat_mesh", "Shade Flat", "Ombreggiatura piatta"},
      {"combine_mesh", "Combine Meshes", "Combina mesh"},
      {"half_cut", "Half-Cut", "Taglio a metà"},
      {"create_outer_hull", "Create Outer Hull", "Crea guscio esterno"},
      {"copy_pivot_from_active", "Copy Pivot From Active",
       "Copia pivot dall'oggetto attivo"},
      {"set_origin_to_center", "Set Origin to Center", "Imposta origine al centro"},
      {"set_origin_to_pivot", "Set Origin to Pivot", "Imposta origine al pivot"},
      {"snap_vertices_to_active", "Snap to Vertex", "Aggancia al vertice"},
      {"snap_vertices_to_floor", "Snap to Floor",
       "Aggancia al pavimento"},
      {"merge_vertices", "Merge", "Unisci"},
      {"merge_vertices_at_center", "Merge at Center", "Unisci al centro"},
      {"merge_by_distance", "Merge by Distance", "Unisci per distanza"},
      {"bevel_vertices", "Bevel Vertices", "Smussa vertici"},
      {"connect_vertices", "Connect", "Connetti"},
      {"dissolve_vertices", "Dissolve Vertices", "Dissolvi vertici"},
      {"radial_align_vertices", "Radial Align", "Allinea radialmente"},
      {"snap_edges_to_active", "Snap to Edge", "Aggancia allo spigolo"},
      {"connect_edges", "Connect Edges", "Connetti spigoli"},
      {"split_edges", "Edge Split", "Dividi spigoli"},
      {"default_edge_normals", "Default Normals", "Normali predefinite"},
      {"harden_edge_normals", "Harden Normals", "Indurisci normali"},
      {"soften_edge_normals", "Soften Normals", "Ammorbidisci normali"},
      {"bevel_edges", "Bevel Edges", "Smussa spigoli"},
      {"bridge_edges", "Bridge", "Bridge"},
      {"interpolated_bridge_edges", "Bridge Edges",
       "Collega spigoli"},
      {"dissolve_edges", "Dissolve", "Dissolvi"},
      {"merge_edges", "Merge Edges", "Unisci spigoli"},
      {"collapse_edges", "Collapse", "Collassa"},
      {"fill_hole", "Fill Hole", "Riempi foro"},
      {"radial_align_edges", "Radial Align", "Allinea radialmente"},
      {"bridge_faces", "Bridge Faces", "Bridge facce"},
      {"interpolated_bridge_faces", "Bridge Faces",
       "Collega facce"},
      {"invert_faces", "Flip Face Normals", "Inverti normali facce"},
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
            "Snap Edges to Active");
  EXPECT_EQ(italian->at("modeling.action.snap_edges_to_active.label"),
            "Aggancia spigoli all'attivo");
  EXPECT_EQ(english->at("modeling.action.invert_mesh.label"), "Flip Mesh Normals");
  EXPECT_EQ(english->at("modeling.action.bake_location.label"), "Bake Location");
  EXPECT_EQ(english->at("modeling.action.bake_transforms.label"), "Bake Transforms");
  EXPECT_EQ(english->at("keybindings.target.modeling.flip_horizontal.label"),
            "Flip Selection Horizontally");
  EXPECT_EQ(english->at("ui.viewport.marking_menu.submenu.mirror"), "Mirror");
  EXPECT_EQ(english->at("ui.viewport.marking_menu.submenu.flip"), "Flip");
  EXPECT_EQ(english->at("ui.viewport.marking_menu.submenu.bake"), "Bake");
  EXPECT_EQ(italian->at("ui.viewport.marking_menu.submenu.mirror"), "Specchia");
  EXPECT_EQ(italian->at("ui.viewport.marking_menu.submenu.flip"), "Ribalta");
  EXPECT_EQ(italian->at("ui.viewport.marking_menu.submenu.bake"), "Applica");

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
      "Focus Selection");
  EXPECT_EQ(
      italian->at("ui.viewport.context_menu.command.center_selection"),
      "Inquadra selezione");
  EXPECT_EQ(english->at(
                "ui.viewport.context_menu.command.edit_material_under_mouse"),
            "Edit Material");
  EXPECT_EQ(italian->at(
                "ui.viewport.context_menu.command.edit_material_under_mouse"),
            "Modifica materiale");
  EXPECT_EQ(english->at("ui.menu.edit.cut"), "Cut");
  EXPECT_EQ(italian->at("ui.menu.edit.cut"), "Taglia");
  EXPECT_EQ(english->at("keybindings.target.edit.cut.label"),
            "Cut Selection");
  EXPECT_EQ(italian->at("keybindings.target.edit.cut.label"),
            "Taglia selezione");
  for (const auto &[locale, catalog] : catalogs) {
    SCOPED_TRACE(locale);
    for (const std::string_view retired : {
             "ui.panel.active_material", "ui.active_material.browse",
             "modeling.action.apply_active_material.label",
             "keybindings.target.edit.apply_material.label",
             "keybindings.target.viewport.material.sample_face.label",
             "keybindings.target.viewport.material.apply_active_to_selection.label",
             "ui.viewport.context_menu.command.set_material_under_mouse"}) {
      EXPECT_EQ(catalog.find(std::string(retired)), catalog.end()) << retired;
    }
  }
}

TEST(LocalizationResourceTests,
     ProjectHomeUsesLocalizedProjectEntryAndRecentActions) {
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

  EXPECT_EQ(english->at("ui.home.new_project"), "New project");
  EXPECT_EQ(english->at("ui.home.open_project"), "Open project…");
  EXPECT_EQ(english->at("ui.home.recent_projects"), "Recent projects");
  EXPECT_EQ(english->at("ui.home.recent_last_modified"), "Last modified");
  EXPECT_EQ(english->at("ui.home.recent_size"), "Size");
  EXPECT_EQ(english->at("ui.home.recent_actions"), "Actions");
  EXPECT_EQ(english->at("ui.home.recent_action.open"), "Open");
  EXPECT_EQ(english->at("ui.home.recent_action.open_file_location"),
            "Open file location");
  EXPECT_EQ(english->at("ui.home.recent_action.copy_path"), "Copy path");
  EXPECT_EQ(english->at("ui.home.recent_action.remove"),
            "Remove from recent");

  EXPECT_EQ(italian->at("ui.home.new_project"), "Nuovo progetto");
  EXPECT_EQ(italian->at("ui.home.open_project"), "Apri progetto…");
  EXPECT_EQ(italian->at("ui.home.recent_projects"), "Progetti recenti");
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
  const auto align_entry = std::find_if(
      entries.begin(), entries.end(), [](const auto &candidate) {
        return std::find(candidate.target_ids.begin(), candidate.target_ids.end(),
                         quader_command::kToolsActivateAlign.string()) !=
               candidate.target_ids.end();
      });
  ASSERT_NE(align_entry, entries.end());
  EXPECT_EQ(align_entry->label, "Activate Align Tool");
  EXPECT_EQ(align_entry->localization_key,
            "keybindings.target.tools.activate.align.label");
  for (const auto &arrangement : quader_mesh_arrangement_catalog()) {
    EXPECT_TRUE(std::none_of(
        entries.begin(), entries.end(), [&](const auto &candidate) {
          return std::find(candidate.target_ids.begin(), candidate.target_ids.end(),
                           arrangement.command_id.string()) !=
                 candidate.target_ids.end();
        })) << arrangement.command_id.string();
  }
  const auto first = editor.modeling().create_box(
      {.min = {0.0F, 0.0F, 0.0F}, .max = {1.0F, 1.0F, 1.0F}});
  const auto second = editor.modeling().create_box(
      {.min = {3.0F, 3.0F, 3.0F}, .max = {4.0F, 4.0F, 4.0F}});
  ASSERT_TRUE(first.ok()) << first.error_message();
  ASSERT_TRUE(second.ok()) << second.error_message();
  ASSERT_TRUE(editor.selection()
                  .select_objects({first.value().entity, second.value().entity},
                                  second.value().entity)
                  .ok());
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::Align).ok());
  const auto target = editor.tools().active_settings();
  ASSERT_TRUE(target.active);
  ASSERT_EQ(target.tool_id, "align");
  ASSERT_EQ(target.controls.size(), 5u);

  /// Describes the visible Align activation and contextual action labels.
  struct LocaleExpectation {
    std::string_view locale;
    std::string_view tool_label;
    std::array<std::string_view, 4> row_labels;
    std::array<std::string_view, 3> alignment_labels;
  };
  const std::array<LocaleExpectation, 2> expectations{{
      {
          "en-US",
          "Activate Align Tool",
          {"Align X", "Align Y", "Align Z", "Distribute"},
          {"Min", "Center", "Max"},
      },
      {
          "it-IT",
          "Attiva strumento Allinea",
          {"Allinea X", "Allinea Y", "Allinea Z", "Distribuisci"},
          {"Min", "Centro", "Max"},
      },
  }};
  constexpr std::array<std::string_view, 4> kActionRowIds{
      "align.x", "align.y", "align.z", "align.distribute"};
  constexpr std::array<std::string_view, 3> kAlignmentOptionIds{
      "min", "center", "max"};
  constexpr std::array<std::string_view, 3> kDistributionOptionIds{"x", "y", "z"};
  constexpr std::array<std::string_view, 3> kDistributionLabels{"X", "Y", "Z"};
  for (const LocaleExpectation &expectation : expectations) {
    const auto locale = std::find_if(
        catalogs.begin(), catalogs.end(), [&](const auto &candidate) {
          return candidate.first == expectation.locale;
        });
    ASSERT_NE(locale, catalogs.end()) << expectation.locale;

    const auto activation = locale->second.find(align_entry->localization_key);
    ASSERT_NE(activation, locale->second.end()) << align_entry->localization_key;
    EXPECT_EQ(activation->second, expectation.tool_label);
    std::set<std::string> localized_labels{activation->second};
    for (std::size_t index = 0; index < kActionRowIds.size(); ++index) {
      const auto control = std::find_if(
          target.controls.begin(), target.controls.end(), [&](const auto &candidate) {
            return candidate.id == kActionRowIds[index];
          });
      ASSERT_NE(control, target.controls.end()) << kActionRowIds[index];
      EXPECT_EQ(control->kind, QuaderToolSettingControlKind::Action);
      EXPECT_EQ(control->label, expectations.front().row_labels[index]);
      ASSERT_EQ(control->choices.size(), 3u);
      const auto row_label = locale->second.find(control->label_key);
      ASSERT_NE(row_label, locale->second.end()) << control->label_key;
      EXPECT_EQ(row_label->second, expectation.row_labels[index]);
      const bool distribution = control->id == "align.distribute";
      const auto &option_ids =
          distribution ? kDistributionOptionIds : kAlignmentOptionIds;
      const auto &option_labels =
          distribution ? kDistributionLabels : expectation.alignment_labels;
      const auto &fallback_labels =
          distribution ? kDistributionLabels : expectations.front().alignment_labels;
      for (std::size_t option_index = 0; option_index < control->choices.size();
           ++option_index) {
        const auto &option = control->choices[option_index];
        EXPECT_EQ(option.id, option_ids[option_index]);
        EXPECT_EQ(option.label, fallback_labels[option_index]);
        const std::string key = control->label_key + "." + option.id;
        const auto localized = locale->second.find(key);
        ASSERT_NE(localized, locale->second.end()) << key;
        EXPECT_EQ(localized->second, option_labels[option_index]);
        const std::string full_label = row_label->second + " " + localized->second;
        EXPECT_TRUE(localized_labels.insert(full_label).second)
            << expectation.locale << " duplicates " << full_label;
      }
    }
    EXPECT_EQ(localized_labels.size(), 13u);
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
