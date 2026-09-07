////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_api_type_conversions.hpp"
#include "api/quader_editor_api.hpp"
#include "core/localization_service.hpp"
#include "editor/session/scene_session.hpp"
#include "ui/inspector_panel.hpp"
#include "ui/qt_application_theme.hpp"
#include "ui/qt_ui_scale_metrics.hpp"
#include "ui/ui_context.hpp"
#include "ui/ui_settings.hpp"
#include "viewport/viewport_settings.hpp"

#include <QApplication>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QPointer>
#include <QPushButton>
#include <QStyle>
#include <QTreeWidget>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <optional>
#include <string_view>
#include <utility>

#ifndef QUADER_SOURCE_DIR
#define QUADER_SOURCE_DIR "."
#endif

namespace quader {
namespace {

LocalizationService inspector_localization() {
  LocalizationService localization;
  EXPECT_TRUE(localization.load(
      std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "locales",
      "en-US"));
  return localization;
}

QTreeWidgetItem *find_top_level_item(QTreeWidget &tree,
                                     const QString &stable_id) {
  for (int index = 0; index < tree.topLevelItemCount(); ++index) {
    QTreeWidgetItem *item = tree.topLevelItem(index);
    if (item != nullptr &&
        item->data(0, Qt::UserRole).toString() == stable_id) {
      return item;
    }
  }
  return nullptr;
}

QtMaterialPresentationItem material_item(QuaderMaterialReference reference,
                                          QString stable_id, QString name,
                                          bool editable) {
  return {
      .reference = reference,
      .stable_id = std::move(stable_id),
      .name = std::move(name),
      .fallback_color = QColor::fromRgb(93, 137, 181),
      .available = true,
      .editable = editable,
  };
}

void process_inspector_events() {
  QCoreApplication::processEvents();
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

/// Runs the real retained panel against a public-API selected-light scene.
struct InspectorLightScenario {
  SceneSession session;
  QuaderEditorApi editor{
      QuaderApiSessionInternalAccess::from_scene_session(session)};
  UISettings ui_settings = UISettings::defaults();
  ViewportSettings viewport_settings = ViewportSettings::defaults();
  LocalizationService localization = inspector_localization();
  UIContext context{
      .intent = {.editor = editor},
      .scene = {.document = session.scene(),
                .assets = session.assets(),
                .modeling = session.modeling(),
                .selection = session.selection()},
      .preferences = {.ui = ui_settings, .viewport = viewport_settings},
      .localization = localization,
  };
  InspectorPanel panel;

  InspectorLightScenario() {
    panel.setAttribute(Qt::WA_DontShowOnScreen, true);
    panel.resize(360, 780);
    panel.show();
  }

  void refresh() {
    panel.refresh(context);
    process_inspector_events();
  }

  QDoubleSpinBox *range_field() {
    return panel.findChild<QDoubleSpinBox *>(
        QStringLiteral("inspector.light.range"));
  }
};

TEST(InspectorMaterialTests, DISABLED_PointLightRangeRefreshPerformance) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-inspector-light-performance";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));
  apply_qt_obsidian_application_theme(
      QApplication::style()->standardPalette());
  application.setQuitOnLastWindowClosed(false);

  InspectorLightScenario scenario;
  QuaderCreatePointLightArgs args;
  args.light.range = 2.0f;
  args.select_created = true;
  const auto created = scenario.editor.scene().create_point_light(args);
  ASSERT_TRUE(created.ok()) << created.error_message();
  scenario.refresh();

  constexpr int kWarmupSamples = 12;
  constexpr int kMeasuredSamples = 120;
  std::uint64_t material_revision = 0;
  for (const bool full_rebuild : {true, false}) {
    const auto edit = scenario.editor.scene().begin_point_light_range_edit(
        created.value().id);
    ASSERT_TRUE(edit.ok()) << edit.error_message();
    std::array<double, kMeasuredSamples> samples{};
    for (int index = 0; index < kWarmupSamples + kMeasuredSamples; ++index) {
      const auto update = scenario.editor.scene().update_point_light_range_edit(
          edit.value(), 2.0f + static_cast<float>(index + 1) * 0.025f);
      ASSERT_TRUE(update.ok()) << update.error_message();
      if (full_rebuild) {
        // Existing owner invalidation exercises the original full rebuild in
        // this same executable without adding a production benchmark switch.
        scenario.panel.set_material_presentation(
            {.revision = ++material_revision});
      }
      const auto start = std::chrono::steady_clock::now();
      scenario.refresh();
      const double elapsed_ms =
          std::chrono::duration<double, std::milli>(
              std::chrono::steady_clock::now() - start)
              .count();
      if (index >= kWarmupSamples) {
        samples[static_cast<std::size_t>(index - kWarmupSamples)] = elapsed_ms;
      }
    }
    ASSERT_TRUE(scenario.editor.scene()
                    .finish_point_light_range_edit(edit.value(), true)
                    .ok());
    scenario.refresh();
    const double mean_ms =
        std::accumulate(samples.begin(), samples.end(), 0.0) / samples.size();
    std::sort(samples.begin(), samples.end());
    std::cout << "Inspector point-light radius "
              << (full_rebuild ? "full-rebuild" : "normal-refresh")
              << ": samples=" << samples.size() << " mean_ms=" << mean_ms
              << " p95_ms=" << samples[samples.size() * 95 / 100] << '\n';
  }
}

TEST(InspectorMaterialTests,
     RetainsLightRangeFieldDuringPreviewCancelAndHistory) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-inspector-light-range-retained";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  InspectorLightScenario scenario;
  QuaderCreatePointLightArgs args;
  args.light.range = 2.0f;
  args.select_created = true;
  const auto created = scenario.editor.scene().create_point_light(args);
  ASSERT_TRUE(created.ok()) << created.error_message();
  scenario.refresh();
  QPointer<QDoubleSpinBox> field = scenario.range_field();
  ASSERT_NE(field, nullptr);
  int value_change_signals = 0;
  QObject::connect(field.data(), &QDoubleSpinBox::valueChanged,
                   [&value_change_signals](double) { ++value_change_signals; });

  const auto edit = scenario.editor.scene().begin_point_light_range_edit(
      created.value().id);
  ASSERT_TRUE(edit.ok()) << edit.error_message();
  for (const float range : {3.125f, 0.001f, 125.125f}) {
    ASSERT_TRUE(scenario.editor.scene()
                    .update_point_light_range_edit(edit.value(), range)
                    .ok());
    scenario.refresh();
    ASSERT_NE(field, nullptr)
        << "radius samples must retain the existing property widgets";
    EXPECT_EQ(scenario.range_field(), field.data());
    EXPECT_NEAR(field->value(), range, 0.00005);
  }
  ASSERT_TRUE(scenario.editor.scene()
                  .finish_point_light_range_edit(edit.value(), true)
                  .ok());
  scenario.refresh();
  ASSERT_NE(field, nullptr);
  EXPECT_DOUBLE_EQ(field->value(), 2.0);

  const auto committed = scenario.editor.scene().begin_point_light_range_edit(
      created.value().id);
  ASSERT_TRUE(committed.ok());
  ASSERT_TRUE(scenario.editor.scene()
                  .update_point_light_range_edit(committed.value(), 4.5f)
                  .ok());
  scenario.refresh();
  ASSERT_TRUE(scenario.editor.scene()
                  .finish_point_light_range_edit(committed.value())
                  .ok());
  scenario.refresh();
  ASSERT_NE(field, nullptr);
  EXPECT_DOUBLE_EQ(field->value(), 4.5);
  ASSERT_TRUE(scenario.editor.history().undo().ok());
  scenario.refresh();
  ASSERT_NE(field, nullptr);
  EXPECT_DOUBLE_EQ(field->value(), 2.0);
  ASSERT_TRUE(scenario.editor.history().redo().ok());
  scenario.refresh();
  ASSERT_NE(field, nullptr);
  EXPECT_DOUBLE_EQ(field->value(), 4.5);
  EXPECT_EQ(value_change_signals, 0)
      << "owner refreshes must not emit Inspector mutations";

  scenario.refresh();
  EXPECT_EQ(scenario.range_field(), field.data());
}

TEST(InspectorMaterialTests,
     LightRangeRetentionInvalidatesOtherPropertiesSelectionAndLocale) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-inspector-light-range-invalidation";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  InspectorLightScenario scenario;
  QuaderCreatePointLightArgs args;
  args.light.range = 2.0f;
  args.select_created = true;
  const auto created = scenario.editor.scene().create_point_light(args);
  ASSERT_TRUE(created.ok());
  scenario.refresh();
  ASSERT_NE(scenario.range_field(), nullptr);

  // A range sample coalesced with another scene edit must not mask that edit.
  const auto edit = scenario.editor.scene().begin_point_light_range_edit(
      created.value().id);
  ASSERT_TRUE(edit.ok());
  ASSERT_TRUE(scenario.editor.scene()
                  .update_point_light_range_edit(edit.value(), 3.0f)
                  .ok());
  ASSERT_TRUE(scenario.editor.scene().rename(created.value().id, "Renamed Light")
                  .ok());
  scenario.refresh();
  ASSERT_NE(scenario.range_field(), nullptr);
  EXPECT_DOUBLE_EQ(scenario.range_field()->value(), 3.0);
  auto *name = scenario.panel.findChild<QLineEdit *>(
      QStringLiteral("inspector.general.name"));
  ASSERT_NE(name, nullptr);
  EXPECT_EQ(name->text(), QStringLiteral("Renamed Light"));
  ASSERT_TRUE(scenario.editor.scene()
                  .finish_point_light_range_edit(edit.value(), true)
                  .ok());
  scenario.refresh();

  ASSERT_NE(scenario.range_field(), nullptr);
  EXPECT_DOUBLE_EQ(scenario.range_field()->value(), 2.0);
  auto light = to_api_light_component(
      *scenario.session.scene().light(created.value().id));
  light.intensity = 3.0f;
  ASSERT_TRUE(scenario.editor.scene()
                  .set_light_component(created.value().id, light)
                  .ok());
  scenario.refresh();
  auto *intensity = scenario.panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("inspector.light.intensity"));
  ASSERT_NE(intensity, nullptr);
  EXPECT_DOUBLE_EQ(intensity->value(), 3.0);

  light.type = QuaderLightType::Directional;
  ASSERT_TRUE(scenario.editor.scene()
                  .set_light_component(created.value().id, light)
                  .ok());
  scenario.refresh();
  EXPECT_EQ(scenario.range_field(), nullptr);
  light.type = QuaderLightType::Point;
  ASSERT_TRUE(scenario.editor.scene()
                  .set_light_component(created.value().id, light)
                  .ok());
  scenario.refresh();
  ASSERT_NE(scenario.range_field(), nullptr);
  EXPECT_DOUBLE_EQ(scenario.range_field()->value(), 2.0);

  ASSERT_TRUE(scenario.localization.select_locale("it-IT"));
  scenario.refresh();
  auto *tree = scenario.panel.findChild<QTreeWidget *>(
      QStringLiteral("inspector.tree"));
  ASSERT_NE(tree, nullptr);
  EXPECT_EQ(tree->headerItem()->text(0),
            QString::fromStdString(std::string(scenario.localization.text(
                "ui.inspector.property", "Property"))));

  scenario.panel.set_material_presentation(
      {.revision = 1u, .section_visible = true});
  scenario.refresh();
  EXPECT_NE(find_top_level_item(*tree, QStringLiteral("material")), nullptr);

  args.light.range = 8.0f;
  const auto second = scenario.editor.scene().create_point_light(args);
  ASSERT_TRUE(second.ok());
  scenario.refresh();
  ASSERT_NE(scenario.range_field(), nullptr);
  EXPECT_DOUBLE_EQ(scenario.range_field()->value(), 8.0);

  scenario.panel.abandon_runtime();
  scenario.refresh();
  ASSERT_NE(scenario.range_field(), nullptr);
  EXPECT_DOUBLE_EQ(scenario.range_field()->value(), 8.0);
}

TEST(InspectorMaterialTests, AppliesInterfaceScaleToRetainedTreeMetrics) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-inspector-scale-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  InspectorPanel panel;
  auto *tree = panel.findChild<QTreeWidget *>(QStringLiteral("inspector.tree"));
  ASSERT_NE(tree, nullptr);

  panel.apply_ui_scale(QtUiScaleMetrics(2.0F, 1U));
  EXPECT_EQ(tree->header()->minimumSectionSize(), 128);

  panel.apply_ui_scale(QtUiScaleMetrics(1.0F, 2U));
  EXPECT_EQ(tree->header()->minimumSectionSize(), 64);
}

TEST(InspectorMaterialTests,
     RefreshesOnlyPaletteBakedMaterialActionIconsOnPaletteChanges) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-inspector-material-palette-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));
  application.setQuitOnLastWindowClosed(false);

  QPalette palette = QApplication::palette();
  palette.setColor(QPalette::ButtonText, QColor(211, 37, 53));
  palette.setColor(QPalette::Mid, QColor(113, 23, 41));
  QApplication::setPalette(palette);

  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto box = editor.modeling().create_box(
      QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                   .max = {1.0f, 1.0f, 1.0f}},
      "Inspector Palette Material");
  ASSERT_TRUE(box.ok()) << box.error_message();
  ASSERT_TRUE(editor.selection().select_object(box.value()).ok());

  UISettings ui_settings = UISettings::defaults();
  ViewportSettings viewport_settings = ViewportSettings::defaults();
  LocalizationService localization = inspector_localization();
  UIContext context{
      .intent = {.editor = editor},
      .scene = {.document = session.scene(),
                .assets = session.assets(),
                .modeling = session.modeling(),
                .selection = session.selection()},
      .preferences = {.ui = ui_settings, .viewport = viewport_settings},
      .localization = localization,
  };

  InspectorPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.set_material_presentation({
      .revision = 1u,
      .section_visible = true,
      .materials = {material_item(
          QuaderMaterialReference::project(ProjectAssetId::generate()),
          QStringLiteral("project:palette-material"),
          QStringLiteral("Palette Material"), true)},
  });
  panel.refresh(context);
  panel.show();
  QCoreApplication::processEvents();

  auto *preview = panel.findChild<QLabel *>(
      QStringLiteral("inspector.material.0.preview"));
  auto *edit = panel.findChild<QPushButton *>(
      QStringLiteral("inspector.material.0.edit"));
  auto *replace = panel.findChild<QPushButton *>(
      QStringLiteral("inspector.material.0.replace"));
  ASSERT_NE(preview, nullptr);
  ASSERT_NE(edit, nullptr);
  ASSERT_NE(replace, nullptr);
  const qint64 preview_key = preview->pixmap().cacheKey();
  const qint64 initial_edit_key = edit->icon().cacheKey();
  const qint64 initial_replace_key = replace->icon().cacheKey();

  palette.setColor(QPalette::ButtonText, QColor(29, 197, 83));
  palette.setColor(QPalette::Mid, QColor(17, 97, 47));
  QApplication::setPalette(palette);
  QCoreApplication::processEvents();
  const qint64 application_edit_key = edit->icon().cacheKey();
  const qint64 application_replace_key = replace->icon().cacheKey();
  EXPECT_NE(application_edit_key, initial_edit_key);
  EXPECT_NE(application_replace_key, initial_replace_key);
  EXPECT_EQ(preview->pixmap().cacheKey(), preview_key)
      << "palette changes must not rebuild material preview rasters";

  palette.setColor(QPalette::ButtonText, QColor(37, 89, 223));
  palette.setColor(QPalette::Mid, QColor(21, 47, 127));
  panel.setPalette(palette);
  EXPECT_NE(edit->icon().cacheKey(), application_edit_key);
  EXPECT_NE(replace->icon().cacheKey(), application_replace_key);
  EXPECT_EQ(preview->pixmap().cacheKey(), preview_key);
}

TEST(InspectorMaterialTests,
     SelectionModesPresentOnlyTheirApprovedPropertyMatrix) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-inspector-material-mode-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto first = editor.modeling().create_box(
      QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                   .max = {1.0f, 1.0f, 1.0f}},
      "Inspector Material First");
  const auto second = editor.modeling().create_box(
      QuaderBounds{.min = {2.0f, -1.0f, -1.0f},
                   .max = {4.0f, 1.0f, 1.0f}},
      "Inspector Material Second");
  ASSERT_TRUE(first.ok()) << first.error_message();
  ASSERT_TRUE(second.ok()) << second.error_message();
  ASSERT_TRUE(editor.selection().select_object(first.value()).ok());

  UISettings ui_settings = UISettings::defaults();
  ViewportSettings viewport_settings = ViewportSettings::defaults();
  LocalizationService localization = inspector_localization();
  UIInspectorPreviewContext preview{.document = session.scene(),
                                    .assets = session.assets()};
  UIContext context{
      .intent = {.editor = editor},
      .scene = {.document = session.scene(),
                .assets = session.assets(),
                .modeling = session.modeling(),
                .selection = session.selection()},
      .inspector_preview = &preview,
      .preferences = {.ui = ui_settings, .viewport = viewport_settings},
      .localization = localization,
  };

  InspectorPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  const auto set_presentation = [&panel](std::uint64_t revision) {
    panel.set_material_presentation({
        .revision = revision,
        .section_visible = true,
        .materials = {material_item(
            QuaderMaterialReference::project(ProjectAssetId::generate()),
            QStringLiteral("project:painted-metal"),
            QStringLiteral("Painted Metal"), true)},
    });
  };

  set_presentation(1u);
  panel.refresh(context);
  auto *tree = panel.findChild<QTreeWidget *>(
      QStringLiteral("inspector.tree"));
  ASSERT_NE(tree, nullptr);
  EXPECT_NE(find_top_level_item(*tree, QStringLiteral("general")), nullptr);
  auto *material =
      find_top_level_item(*tree, QStringLiteral("material"));
  ASSERT_NE(material, nullptr);
  EXPECT_TRUE(material->isExpanded());

  auto *material_preview = panel.findChild<QLabel *>(
      QStringLiteral("inspector.material.0.preview"));
  auto *material_edit = panel.findChild<QPushButton *>(
      QStringLiteral("inspector.material.0.edit"));
  ASSERT_NE(material_preview, nullptr);
  ASSERT_NE(material_edit, nullptr);
  panel.apply_ui_scale(QtUiScaleMetrics(2.0F, 1U));
  EXPECT_EQ(material_preview->size(), QSize(112, 112));
  EXPECT_EQ(material_edit->size(), QSize(48, 48));
  EXPECT_EQ(material_edit->iconSize(), QSize(32, 32));
  panel.apply_ui_scale(QtUiScaleMetrics(1.0F, 2U));
  EXPECT_EQ(material_preview->size(), QSize(56, 56));
  EXPECT_EQ(material_edit->size(), QSize(24, 24));
  EXPECT_EQ(material_edit->iconSize(), QSize(16, 16));

  ASSERT_TRUE(editor.selection()
                  .select_objects({first.value().entity, second.value().entity},
                                  first.value().entity)
                  .ok());
  set_presentation(2u);
  panel.refresh(context);
  EXPECT_NE(find_top_level_item(*tree, QStringLiteral("general")), nullptr);
  EXPECT_NE(find_top_level_item(*tree, QStringLiteral("material")), nullptr);

  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Group).ok());
  const auto group = editor.scene().group_selection("Inspector Material Group");
  ASSERT_TRUE(group.ok()) << group.error_message();
  ASSERT_EQ(editor.selection().summary().mode, QuaderSelectionMode::Group);
  set_presentation(3u);
  panel.refresh(context);
  EXPECT_NE(find_top_level_item(*tree, QStringLiteral("general")), nullptr);
  EXPECT_NE(find_top_level_item(*tree, QStringLiteral("material")), nullptr);

  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(first.value(), 1u).ok());
  ASSERT_EQ(editor.selection().summary().selected_face_count, 1u);
  set_presentation(4u);
  panel.refresh(context);
  ASSERT_EQ(tree->topLevelItemCount(), 1);
  EXPECT_NE(find_top_level_item(*tree, QStringLiteral("material")), nullptr);

  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Vertex).ok());
  set_presentation(5u);
  panel.refresh(context);
  EXPECT_EQ(tree->topLevelItemCount(), 0);

  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Edge).ok());
  set_presentation(6u);
  panel.refresh(context);
  EXPECT_EQ(tree->topLevelItemCount(), 0);
}

TEST(InspectorMaterialTests,
     MaterialRowsDeduplicateUseSquareControlsAndDispatchTypedCallbacks) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-inspector-material-row-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto box = editor.modeling().create_box(
      QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                   .max = {1.0f, 1.0f, 1.0f}},
      "Inspector Material Rows");
  ASSERT_TRUE(box.ok()) << box.error_message();
  ASSERT_TRUE(editor.selection().select_object(box.value()).ok());

  UISettings ui_settings = UISettings::defaults();
  ViewportSettings viewport_settings = ViewportSettings::defaults();
  LocalizationService localization = inspector_localization();
  UIInspectorPreviewContext preview{.document = session.scene(),
                                    .assets = session.assets()};
  UIContext context{
      .intent = {.editor = editor},
      .scene = {.document = session.scene(),
                .assets = session.assets(),
                .modeling = session.modeling(),
                .selection = session.selection()},
      .inspector_preview = &preview,
      .preferences = {.ui = ui_settings, .viewport = viewport_settings},
      .localization = localization,
  };

  const QuaderMaterialReference editable_reference =
      QuaderMaterialReference::project(ProjectAssetId::generate());
  const QuaderMaterialReference embedded_reference =
      QuaderMaterialReference::embedded(201u);
  const QuaderMaterialReference graph_reference =
      QuaderMaterialReference::project(ProjectAssetId::generate());
  const QuaderMaterialReference missing_reference =
      QuaderMaterialReference::project(ProjectAssetId::generate());
  QtMaterialPresentationItem missing_material = material_item(
      missing_reference, QStringLiteral("project:missing"), {}, true);
  missing_material.available = false;
  std::optional<QuaderMaterialReference> edited;
  std::optional<QuaderMaterialReference> replaced;
  int edit_callback_version = 0;

  InspectorPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.set_material_presentation({
      .revision = 1u,
      .section_visible = true,
      .materials = {
          material_item(editable_reference, QStringLiteral("project:standard"),
                        QStringLiteral("Editable Material"), true),
          material_item(editable_reference,
                        QStringLiteral("project:standard-duplicate"),
                        QStringLiteral("Duplicate Material"), true),
          material_item(embedded_reference, QStringLiteral("embedded:201"),
                        QStringLiteral("Embedded Material"), true),
          material_item(graph_reference, QStringLiteral("project:graph"),
                        QStringLiteral("Graph Material"), false),
          missing_material,
      },
      .edit_material = [&edited, &edit_callback_version](
                           QuaderMaterialReference reference) {
        edited = reference;
        edit_callback_version = 1;
      },
      .replace_material = [&replaced](QuaderMaterialReference reference) {
        replaced = reference;
      },
  });
  panel.refresh(context);

  auto *tree = panel.findChild<QTreeWidget *>(
      QStringLiteral("inspector.tree"));
  ASSERT_NE(tree, nullptr);
  auto *material =
      find_top_level_item(*tree, QStringLiteral("material"));
  ASSERT_NE(material, nullptr);
  EXPECT_TRUE(material->isExpanded());
  EXPECT_EQ(material->childCount(), 4)
      << "owner snapshots remain protected from duplicate presentation rows";
  ASSERT_TRUE(material->child(0)->isFirstColumnSpanned());
  ASSERT_TRUE(material->child(1)->isFirstColumnSpanned());
  ASSERT_TRUE(material->child(2)->isFirstColumnSpanned());
  ASSERT_TRUE(material->child(3)->isFirstColumnSpanned());

  auto *preview_label = panel.findChild<QLabel *>(
      QStringLiteral("inspector.material.0.preview"));
  auto *edit_button = panel.findChild<QPushButton *>(
      QStringLiteral("inspector.material.0.edit"));
  auto *replace_button = panel.findChild<QPushButton *>(
      QStringLiteral("inspector.material.0.replace"));
  auto *embedded_edit = panel.findChild<QPushButton *>(
      QStringLiteral("inspector.material.1.edit"));
  auto *graph_edit = panel.findChild<QPushButton *>(
      QStringLiteral("inspector.material.2.edit"));
  auto *missing_edit = panel.findChild<QPushButton *>(
      QStringLiteral("inspector.material.3.edit"));
  auto *missing_replace = panel.findChild<QPushButton *>(
      QStringLiteral("inspector.material.3.replace"));
  ASSERT_NE(preview_label, nullptr);
  ASSERT_NE(edit_button, nullptr);
  ASSERT_NE(replace_button, nullptr);
  ASSERT_NE(embedded_edit, nullptr);
  ASSERT_NE(graph_edit, nullptr);
  ASSERT_NE(missing_edit, nullptr);
  ASSERT_NE(missing_replace, nullptr);
  EXPECT_EQ(preview_label->width(), 56);
  EXPECT_EQ(preview_label->height(), 56);
  EXPECT_FALSE(preview_label->pixmap().isNull());
  EXPECT_EQ(edit_button->width(), edit_button->height());
  EXPECT_EQ(replace_button->width(), replace_button->height());
  const QList<QSize> edit_icon_sizes = edit_button->icon().availableSizes(
      QIcon::Normal, QIcon::Off);
  EXPECT_TRUE(edit_icon_sizes.contains(QSize(16, 16)));
  EXPECT_TRUE(edit_icon_sizes.contains(QSize(384, 384)))
      << "programmatic Inspector glyphs retain a true high-resolution source";
  EXPECT_TRUE(edit_button->isEnabled());
  EXPECT_TRUE(replace_button->isEnabled());
  EXPECT_FALSE(embedded_edit->isEnabled());
  EXPECT_FALSE(graph_edit->isEnabled());
  EXPECT_FALSE(missing_edit->isEnabled());
  EXPECT_TRUE(missing_replace->isEnabled());

  const qint64 preview_cache_key = preview_label->pixmap().cacheKey();
  QEvent device_pixel_ratio_event(QEvent::DevicePixelRatioChange);
  QCoreApplication::sendEvent(&panel, &device_pixel_ratio_event);
  EXPECT_EQ(preview_label->pixmap().cacheKey(), preview_cache_key)
      << "an unchanged effective DPR must not rebuild the retained raster";
  QEvent screen_change_event(QEvent::ScreenChangeInternal);
  QCoreApplication::sendEvent(&panel, &screen_change_event);
  EXPECT_EQ(preview_label->pixmap().cacheKey(), preview_cache_key)
      << "screen notifications re-evaluate DPR behind the same cache gate";

  edit_button->click();
  replace_button->click();
  ASSERT_TRUE(edited.has_value());
  ASSERT_TRUE(replaced.has_value());
  EXPECT_EQ(*edited, editable_reference);
  EXPECT_EQ(*replaced, editable_reference);
  EXPECT_EQ(edit_callback_version, 1);

  // An unchanged presentation revision must not rebuild or enumerate data.
  panel.set_material_presentation({
      .revision = 1u,
      .section_visible = true,
      .materials = {material_item(
          editable_reference, QStringLiteral("project:standard"),
          QStringLiteral("Should Not Rebuild"), true)},
      .edit_material = [&edited, &edit_callback_version](
                           QuaderMaterialReference reference) {
        edited = reference;
        edit_callback_version = 2;
      },
      .replace_material = [&replaced](QuaderMaterialReference reference) {
        replaced = reference;
      },
  });
  panel.refresh(context);
  auto *name = panel.findChild<QLabel *>(
      QStringLiteral("inspector.material.0.name"));
  ASSERT_NE(name, nullptr);
  EXPECT_EQ(name->accessibleName(), QStringLiteral("Editable Material"));
  edit_button->click();
  EXPECT_EQ(edit_callback_version, 2);
}

} // namespace
} // namespace quader
