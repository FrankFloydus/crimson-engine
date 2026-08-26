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
#include "ui/qt_application_theme.hpp"
#include "ui/qt_viewport_context_menu.hpp"
#include "viewport/viewport_input_router.hpp"

#include <QAction>
#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QImage>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMenu>
#include <QPalette>
#include <QPoint>
#include <QStyle>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#ifndef QUADER_SOURCE_DIR
#define QUADER_SOURCE_DIR "."
#endif

namespace {

struct ContextActionSnapshot final {
  QString text;
  QString object_name;
  QString submenu_object_name;
  bool separator = false;
  bool enabled = false;
  bool shortcut_empty = true;
  std::vector<ContextActionSnapshot> children;
};

struct ContextMenuSnapshot final {
  bool opened = false;
  std::vector<ContextActionSnapshot> actions;
  QString style_sheet;
  std::size_t separator_count = 0u;
  std::size_t visible_separator_count = 0u;
};

quader::LocalizationService loaded_localization() {
  quader::LocalizationService localization;
  EXPECT_TRUE(localization.load(
      std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "locales",
      "en-US"));
  return localization;
}

ContextActionSnapshot snapshot_action(const QAction &action) {
  ContextActionSnapshot snapshot{
      .text = action.text(),
      .object_name = action.objectName(),
      .separator = action.isSeparator(),
      .enabled = action.isEnabled(),
      .shortcut_empty = action.shortcut().isEmpty(),
  };
  if (const QMenu *submenu = action.menu(); submenu != nullptr) {
    snapshot.submenu_object_name = submenu->objectName();
    for (const QAction *child : submenu->actions()) {
      if (child != nullptr) {
        snapshot.children.push_back(snapshot_action(*child));
      }
    }
  }
  return snapshot;
}

ContextMenuSnapshot show_and_snapshot(
    QWidget &owner, quader::QtViewportContextMenuPresenter &presenter,
    quader::QuaderEditorApi &editor,
    quader::ViewportInputRouter &input_router,
    quader::LocalizationService &localization,
    const quader::QuaderActionSet &action_set,
    quader::QuaderSelectionMode selection_mode,
    std::optional<quader::ProjectAssetId> material_under_mouse = std::nullopt,
    std::function<void(quader::ProjectAssetId)> open_material_editor = {},
    bool trigger_material_editor = false) {
  ContextMenuSnapshot snapshot;
  QTimer::singleShot(0, &owner, [&snapshot, trigger_material_editor]() {
    auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
    if (menu == nullptr ||
        menu->objectName() !=
            QStringLiteral("viewport_operations_context_menu")) {
      ADD_FAILURE() << "Expected the viewport context menu to open.";
      if (QWidget *popup = QApplication::activePopupWidget()) {
        popup->close();
      }
      return;
    }

    snapshot.opened = true;
    snapshot.style_sheet = menu->styleSheet();
    QImage rendered(menu->size(), QImage::Format_ARGB32_Premultiplied);
    rendered.fill(Qt::transparent);
    menu->render(&rendered);
    const QColor separator_color =
        menu->palette().color(QPalette::Active, QPalette::Mid);
    for (QAction *action : menu->actions()) {
      if (action != nullptr) {
        snapshot.actions.push_back(snapshot_action(*action));
        if (action->isSeparator()) {
          ++snapshot.separator_count;
          const QRect region =
              menu->actionGeometry(action).intersected(rendered.rect());
          bool visible = false;
          for (int y = region.top(); y <= region.bottom() && !visible; ++y) {
            for (int x = region.left(); x <= region.right(); ++x) {
              if (rendered.pixelColor(x, y).rgb() == separator_color.rgb()) {
                visible = true;
                break;
              }
            }
          }
          if (visible) {
            ++snapshot.visible_separator_count;
          }
        }
      }
    }
    if (trigger_material_editor) {
      QAction *edit_action = nullptr;
      for (QAction *action : menu->actions()) {
        if (action != nullptr &&
            action->objectName() == QStringLiteral(
                                        "viewport.material.edit_under_mouse")) {
          edit_action = action;
          break;
        }
      }
      if (edit_action == nullptr) {
        ADD_FAILURE() << "Expected the material-editor action to exist.";
      } else {
        menu->setActiveAction(edit_action);
        QKeyEvent accept_action(QEvent::KeyPress, Qt::Key_Return,
                                Qt::NoModifier);
        QApplication::sendEvent(menu, &accept_action);
        if (menu->isVisible()) {
          menu->close();
        }
        return;
      }
    }
    menu->close();
  });

  presenter.show(owner, owner.mapToGlobal(owner.rect().center()), editor,
                 input_router, localization, action_set, selection_mode,
                 material_under_mouse, open_material_editor);
  return snapshot;
}

quader::QuaderActionItem enabled_action(std::string action_id,
                                        std::string label) {
  return quader::QuaderActionItem{
      .action_id = std::move(action_id),
      .label = std::move(label),
      .visibility = quader::QuaderActionVisibility::Enabled,
  };
}

const ContextActionSnapshot *find_child(
    const ContextActionSnapshot &submenu, const QString &object_name) {
  const auto found = std::find_if(
      submenu.children.begin(), submenu.children.end(),
      [&object_name](const ContextActionSnapshot &action) {
        return action.object_name == object_name;
      });
  return found == submenu.children.end() ? nullptr : &*found;
}

} // namespace

TEST(QtViewportContextMenuTests,
     StructuresModeObjectAndUtilityCommandsWithDisplayOnlyShortcuts) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-viewport-context-menu-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));
  QApplication::setPalette(quader::qt_obsidian_application_palette(
      QApplication::style()->standardPalette()));
  application.setQuitOnLastWindowClosed(false);

  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  quader::ViewportInputRouter input_router;
  quader::LocalizationService localization = loaded_localization();
  quader::QtViewportContextMenuPresenter presenter;

  QWidget owner;
  owner.setAttribute(Qt::WA_DontShowOnScreen, true);
  owner.resize(320, 240);
  owner.show();
  QCoreApplication::processEvents();

  quader::QuaderActionSet object_actions;
  object_actions.surface = quader::QuaderActionSurface::ViewportContextMenu;
  object_actions.panels.push_back(quader::QuaderActionPanel{
      .panel_id = "mesh",
      .label = "Mesh",
      .selection_mode_id = "object",
      .visibility = quader::QuaderActionVisibility::Enabled,
      .actions = {enabled_action("duplicate_meshes", "Duplicate"),
                  enabled_action("invert_mesh", "Flip Mesh Normals"),
                  enabled_action("copy_pivot_from_active",
                                 "Copy Pivot From Active")},
  });

  const std::optional<quader::ProjectAssetId> hovered_material =
      quader::ProjectAssetId::parse(
          "11111111-1111-4111-8111-111111111111");
  ASSERT_TRUE(hovered_material.has_value());
  std::optional<quader::ProjectAssetId> opened_material;

  const ContextMenuSnapshot object_menu = show_and_snapshot(
      owner, presenter, editor, input_router, localization, object_actions,
      quader::QuaderSelectionMode::Object, hovered_material,
      [&opened_material](quader::ProjectAssetId id) { opened_material = id; },
      true);
  ASSERT_TRUE(object_menu.opened);
  ASSERT_EQ(object_menu.actions.size(), 10u);
  EXPECT_EQ(opened_material, hovered_material);
  EXPECT_EQ(object_menu.separator_count, 2u);
  EXPECT_EQ(object_menu.visible_separator_count, 2u);
  EXPECT_TRUE(object_menu.style_sheet.contains(QStringLiteral(
      "QMenu#viewport_operations_context_menu::separator")));
  EXPECT_TRUE(
      object_menu.style_sheet.contains(QStringLiteral("palette(mid)")));

  const ContextActionSnapshot &mesh_submenu = object_menu.actions[0];
  const ContextActionSnapshot &object_submenu = object_menu.actions[1];
  EXPECT_EQ(mesh_submenu.text, QStringLiteral("Mesh"));
  EXPECT_EQ(mesh_submenu.submenu_object_name,
            QStringLiteral("viewport_context_modeling_submenu"));
  EXPECT_EQ(object_submenu.text, QStringLiteral("Object"));
  EXPECT_EQ(object_submenu.submenu_object_name,
            QStringLiteral("viewport_context_object_submenu"));
  EXPECT_TRUE(object_menu.actions[2].separator);

  const ContextActionSnapshot *mesh_duplicate =
      find_child(mesh_submenu, QStringLiteral("duplicate_meshes"));
  const ContextActionSnapshot *object_duplicate =
      find_child(object_submenu, QStringLiteral("edit.duplicate"));
  ASSERT_NE(mesh_duplicate, nullptr);
  ASSERT_NE(object_duplicate, nullptr);
  EXPECT_EQ(mesh_duplicate->text, QStringLiteral("Duplicate"));
  EXPECT_TRUE(object_duplicate->text.startsWith(
      QStringLiteral("Duplicate Object")));
  EXPECT_NE(mesh_duplicate->object_name, object_duplicate->object_name);
  EXPECT_NE(find_child(mesh_submenu, QStringLiteral("invert_mesh")), nullptr);
  EXPECT_NE(find_child(object_submenu,
                       QStringLiteral("copy_pivot_from_active")),
            nullptr);

  for (const ContextActionSnapshot &top_level : object_menu.actions) {
    EXPECT_NE(top_level.object_name, QStringLiteral("duplicate_meshes"));
    EXPECT_NE(top_level.object_name, QStringLiteral("invert_mesh"));
    EXPECT_NE(top_level.object_name,
              QStringLiteral("copy_pivot_from_active"));
  }

  EXPECT_EQ(object_menu.actions[3].object_name,
            QStringLiteral("viewport.frame_selection"));
  EXPECT_EQ(object_menu.actions[4].object_name,
            QStringLiteral("viewport.material.sample_face"));
  EXPECT_EQ(object_menu.actions[4].text,
            QStringLiteral(
                "Set Material under Mouse as Active\tShift+RMouse"));
  EXPECT_TRUE(object_menu.actions[4].enabled);
  EXPECT_TRUE(object_menu.actions[4].shortcut_empty);
  EXPECT_EQ(object_menu.actions[5].object_name,
            QStringLiteral("viewport.material.edit_under_mouse"));
  EXPECT_TRUE(object_menu.actions[5].enabled);
  EXPECT_TRUE(object_menu.actions[6].separator);

  const std::vector<std::pair<QString, QString>> clipboard_actions = {
      {QStringLiteral("edit.cut"), QStringLiteral("Cut\tCtrl+X")},
      {QStringLiteral("edit.copy"), QStringLiteral("Copy\tCtrl+C")},
      {QStringLiteral("edit.paste"), QStringLiteral("Paste\tCtrl+V")},
  };
  for (std::size_t index = 0; index < clipboard_actions.size(); ++index) {
    const ContextActionSnapshot &action = object_menu.actions[7u + index];
    EXPECT_EQ(action.object_name, clipboard_actions[index].first);
    EXPECT_EQ(action.text, clipboard_actions[index].second);
    EXPECT_TRUE(action.shortcut_empty)
        << "Context-menu labels display shortcuts without installing a "
           "second QAction shortcut route.";
  }

  quader::QuaderActionSet face_actions;
  face_actions.surface = quader::QuaderActionSurface::ViewportContextMenu;
  face_actions.panels.push_back(quader::QuaderActionPanel{
      .panel_id = "face",
      .label = "Face",
      .selection_mode_id = "face",
      .visibility = quader::QuaderActionVisibility::Enabled,
      .actions = {enabled_action("invert_faces", "Invert Face")},
  });

  const ContextMenuSnapshot face_menu = show_and_snapshot(
      owner, presenter, editor, input_router, localization, face_actions,
      quader::QuaderSelectionMode::Face);
  ASSERT_TRUE(face_menu.opened);
  ASSERT_EQ(face_menu.actions.size(), 9u);
  EXPECT_EQ(face_menu.actions[0].text, QStringLiteral("Face"));
  EXPECT_EQ(face_menu.actions[0].submenu_object_name,
            QStringLiteral("viewport_context_modeling_submenu"));
  EXPECT_NE(find_child(face_menu.actions[0], QStringLiteral("invert_faces")),
            nullptr);
  EXPECT_TRUE(face_menu.actions[1].separator);
  for (const ContextActionSnapshot &top_level : face_menu.actions) {
    EXPECT_NE(top_level.text, QStringLiteral("Mesh"));
    EXPECT_NE(top_level.text, QStringLiteral("Object"));
    EXPECT_NE(top_level.object_name, QStringLiteral("invert_faces"));
  }
}
