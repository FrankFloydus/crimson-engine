////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "core/localization_service.hpp"
#include "editor/session/scene_session.hpp"
#include "ui/inspector_panel.hpp"
#include "ui/ui_context.hpp"
#include "ui/ui_settings.hpp"
#include "viewport/viewport_settings.hpp"

#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>

#include <filesystem>
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
  EXPECT_TRUE(edit_button->isEnabled());
  EXPECT_TRUE(replace_button->isEnabled());
  EXPECT_FALSE(embedded_edit->isEnabled());
  EXPECT_FALSE(graph_edit->isEnabled());
  EXPECT_FALSE(missing_edit->isEnabled());
  EXPECT_TRUE(missing_replace->isEnabled());

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
