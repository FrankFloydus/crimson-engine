////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "assets/mesh_asset_payload.hpp"
#include "core/localization_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/session/project_session.hpp"
#include "editor/session/scene_session.hpp"
#include "ui/qt_application_theme.hpp"
#include "ui/qt_viewport_context_menu.hpp"
#include "viewport/viewport_input_router.hpp"

#include <quader/modeling/neutral/polygon/document.hpp>

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
#include <QTemporaryDir>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
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
  QString tool_tip;
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
  EXPECT_TRUE(localization.load(std::filesystem::path(QUADER_SOURCE_DIR) /
                                    "resources" / "locales",
                                "en-US"));
  return localization;
}

ContextActionSnapshot snapshot_action(const QAction &action) {
  ContextActionSnapshot snapshot{
      .text = action.text(),
      .object_name = action.objectName(),
      .tool_tip = action.toolTip(),
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
    quader::QuaderEditorApi &editor, quader::ViewportInputRouter &input_router,
    quader::LocalizationService &localization,
    const quader::QuaderActionSet &action_set,
    const quader::QuaderSelectionSummary &selection, bool target_hit,
    std::optional<quader::QuaderHoverTarget> material_face_target = std::nullopt,
    QString trigger_action_name = {}) {
  ContextMenuSnapshot snapshot;
  QTimer snapshot_timer;
  snapshot_timer.setSingleShot(true);
  QObject::connect(&snapshot_timer, &QTimer::timeout, &owner,
                   [&snapshot, trigger_action_name]() {
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
    if (!trigger_action_name.isEmpty()) {
      QAction *selected_action = nullptr;
      for (QAction *action : menu->actions()) {
        if (action != nullptr &&
            action->objectName() == trigger_action_name) {
          selected_action = action;
          break;
        }
      }
      if (selected_action == nullptr) {
        ADD_FAILURE() << "Expected context action: "
                      << trigger_action_name.toStdString();
      } else {
        menu->setActiveAction(selected_action);
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
  snapshot_timer.start(0);

  presenter.show(owner, owner.mapToGlobal(owner.rect().center()), editor,
                 input_router, localization, action_set, selection, target_hit,
                 material_face_target);
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

quader::QuaderActionItem
disabled_action(std::string action_id, std::string label, std::string reason) {
  return quader::QuaderActionItem{
      .action_id = std::move(action_id),
      .label = std::move(label),
      .visibility = quader::QuaderActionVisibility::Disabled,
      .disabled_reason = std::move(reason),
  };
}

quader::QuaderActionItem enabled_tool(std::string tool_id, std::string label) {
  return quader::QuaderActionItem{
      .action_id = tool_id,
      .tool_id = std::move(tool_id),
      .label = std::move(label),
      .kind = quader::QuaderActionKind::Tool,
      .visibility = quader::QuaderActionVisibility::Enabled,
  };
}

const ContextActionSnapshot *find_top_level(const ContextMenuSnapshot &menu,
                                            const QString &object_name) {
  const auto found =
      std::find_if(menu.actions.begin(), menu.actions.end(),
                   [&object_name](const ContextActionSnapshot &action) {
                     return action.object_name == object_name;
                   });
  return found == menu.actions.end() ? nullptr : &*found;
}

std::optional<std::size_t> top_level_index(const ContextMenuSnapshot &menu,
                                           const QString &object_name) {
  const auto found =
      std::find_if(menu.actions.begin(), menu.actions.end(),
                   [&object_name](const ContextActionSnapshot &action) {
                     return action.object_name == object_name;
                   });
  return found == menu.actions.end()
             ? std::nullopt
             : std::optional<std::size_t>(static_cast<std::size_t>(
                   std::distance(menu.actions.begin(), found)));
}

void expect_flat_available_menu(const ContextMenuSnapshot &menu) {
  ASSERT_TRUE(menu.opened);
  bool previous_separator = true;
  for (const ContextActionSnapshot &action : menu.actions) {
    SCOPED_TRACE(action.object_name.toStdString());
    EXPECT_TRUE(action.children.empty());
    EXPECT_TRUE(action.submenu_object_name.isEmpty());
    EXPECT_TRUE(action.shortcut_empty)
        << "Shortcuts must remain display text without a second QAction route.";
    if (action.separator) {
      EXPECT_FALSE(previous_separator)
          << "Separators must not lead the menu or follow another separator.";
    } else {
      EXPECT_TRUE(action.enabled);
      EXPECT_FALSE(action.object_name.startsWith(QStringLiteral("tools.activate.")))
          << "Viewport tool launchers belong to the toolbar.";
      EXPECT_EQ(std::count_if(menu.actions.begin(), menu.actions.end(),
                              [&action](const ContextActionSnapshot &other) {
                                return other.object_name == action.object_name;
                              }),
                1)
          << "Each command must occur only once in the flattened menu.";
    }
    previous_separator = action.separator;
  }
  if (!menu.actions.empty()) {
    EXPECT_FALSE(menu.actions.back().separator)
        << "The last available section must not leave a trailing separator.";
  }
}

void expect_no_supplied_tool_actions(const ContextMenuSnapshot &menu) {
  for (const QString &command_id : {QStringLiteral("move"),
                                    QStringLiteral("rotate"),
                                    QStringLiteral("scale"),
                                    QStringLiteral("pivot"),
                                    QStringLiteral("mirror"),
                                    QStringLiteral("knife"),
                                    QStringLiteral("insert_edge_loop"),
                                    QStringLiteral("cut"),
                                    QStringLiteral("linear_array"),
                                    QStringLiteral("radial_array"),
                                    QStringLiteral("path_array"),
                                    QStringLiteral("curve"),
                                    QStringLiteral("create_primitive")}) {
    EXPECT_EQ(find_top_level(menu, command_id), nullptr)
        << command_id.toStdString();
  }
}

void expect_no_whole_object_operations(const ContextMenuSnapshot &menu) {
  for (const QString &command_id : {
           QStringLiteral("edit.group"), QStringLiteral("edit.ungroup"),
           QStringLiteral("edit.duplicate"),
           QStringLiteral("edit.create_instance"),
           QStringLiteral("edit.flatten_instance"),
           QStringLiteral("edit.exit_instance_edit"),
           QStringLiteral("tools.activate.mirror"),
           QStringLiteral("mirror"),
           QStringLiteral("tools.activate.linear_array"),
           QStringLiteral("tools.activate.radial_array"),
           QStringLiteral("tools.activate.path_array"),
           QStringLiteral("modeling.mirror_selection_x"),
           QStringLiteral("modeling.mirror_selection_y"),
           QStringLiteral("modeling.mirror_selection_z"),
           QStringLiteral("modeling.flip_horizontal"),
           QStringLiteral("modeling.flip_vertical"),
           QStringLiteral("modeling.remove_doubles"),
           QStringLiteral("remove_doubles"),
           QStringLiteral("edit.cut"), QStringLiteral("edit.copy"),
           QStringLiteral("edit.paste"), QStringLiteral("duplicate_meshes"),
           QStringLiteral("combine_mesh"), QStringLiteral("half_cut"),
           QStringLiteral("create_outer_hull"), QStringLiteral("invert_mesh"),
           QStringLiteral("shade_smooth_mesh"),
           QStringLiteral("shade_flat_mesh"),
           QStringLiteral("modeling.duplicate_meshes"),
           QStringLiteral("modeling.combine_mesh"),
           QStringLiteral("modeling.half_cut"),
           QStringLiteral("modeling.create_outer_hull"),
           QStringLiteral("modeling.invert_mesh"),
           QStringLiteral("modeling.shade_smooth_mesh"),
           QStringLiteral("modeling.shade_flat_mesh"),
           QStringLiteral("snap_meshes_to_floor"),
           QStringLiteral("copy_pivot_from_active")}) {
    EXPECT_EQ(find_top_level(menu, command_id), nullptr)
        << command_id.toStdString();
  }
  for (const ContextActionSnapshot &action : menu.actions) {
    EXPECT_FALSE(action.object_name.startsWith(QStringLiteral("modeling.align.")))
        << action.object_name.toStdString();
    EXPECT_FALSE(
        action.object_name.startsWith(QStringLiteral("modeling.distribute.")))
        << action.object_name.toStdString();
  }
}

void expect_no_unrelated_catalog_commands(const ContextMenuSnapshot &menu) {
  expect_no_supplied_tool_actions(menu);
  for (const ContextActionSnapshot &action : menu.actions) {
    SCOPED_TRACE(action.object_name.toStdString());
    EXPECT_FALSE(action.object_name.startsWith(QStringLiteral("modeling.texture.")));
    EXPECT_FALSE(action.object_name.startsWith(QStringLiteral("select.")));
    EXPECT_NE(action.object_name, QStringLiteral("modeling.uv_peel"));
    EXPECT_NE(action.object_name, QStringLiteral("uv_peel"));
    EXPECT_NE(action.object_name, QStringLiteral("viewport.toggle_texture_lock"));
    EXPECT_NE(action.object_name,
              QStringLiteral("viewport.toggle_texture_lock_scale"));
    EXPECT_NE(action.object_name, QStringLiteral("viewport.frame_selection"));
    EXPECT_NE(action.object_name, QStringLiteral("viewport.focus_all"));
    EXPECT_NE(action.object_name, QStringLiteral("scene.layers.remove_selection"));
    EXPECT_NE(action.object_name, QStringLiteral("viewport.material.edit_under_mouse"));
    EXPECT_NE(action.object_name, QStringLiteral("viewport.material.sample_face"));
  }
}

std::vector<std::array<double, 2>> face_uv_coordinates(const quader_poly::Face &face) {
  std::vector<std::array<double, 2>> coordinates;
  coordinates.reserve(face.uvs.size());
  for (const auto &uv : face.uvs) {
    coordinates.push_back({uv.x, uv.y});
  }
  return coordinates;
}

} // namespace

TEST(QtViewportContextMenuTests,
     BuildsFlatEmptyObjectGroupAndComponentMenus) {
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
  const auto context_mesh = editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      "Context Menu Mesh");
  ASSERT_TRUE(context_mesh.ok()) << context_mesh.error_message();
  ASSERT_TRUE(editor.selection().select_object(context_mesh.value()).ok());
  quader::ViewportInputRouter input_router;
  quader::LocalizationService localization = loaded_localization();
  quader::QtViewportContextMenuPresenter presenter;

  QWidget owner;
  owner.setAttribute(Qt::WA_DontShowOnScreen, true);
  owner.resize(320, 240);
  owner.show();
  QCoreApplication::processEvents();

  const auto action_set = [](std::string mode,
                             std::vector<quader::QuaderActionItem> actions) {
    quader::QuaderActionSet result;
    result.surface = quader::QuaderActionSurface::ViewportContextMenu;
    result.panels.push_back(quader::QuaderActionPanel{
        .panel_id = "viewport_context_menu",
        .label = "Context Actions",
        .selection_mode_id = std::move(mode),
        .visibility = quader::QuaderActionVisibility::Enabled,
        .actions = std::move(actions),
    });
    return result;
  };
  const ContextMenuSnapshot empty_menu =
      show_and_snapshot(owner, presenter, editor, input_router, localization,
                        {}, quader::QuaderSelectionSummary{}, false);
  EXPECT_FALSE(empty_menu.opened);
  EXPECT_TRUE(empty_menu.actions.empty());
  EXPECT_EQ(empty_menu.separator_count, 0u);
  EXPECT_EQ(find_top_level(empty_menu, QStringLiteral("select.all")), nullptr);
  EXPECT_EQ(find_top_level(empty_menu, QStringLiteral("viewport.focus_all")),
            nullptr);
  EXPECT_EQ(find_top_level(empty_menu, QStringLiteral("edit.cut")), nullptr);
  EXPECT_EQ(find_top_level(empty_menu, QStringLiteral("edit.copy")), nullptr);
  EXPECT_EQ(find_top_level(empty_menu, QStringLiteral("edit.duplicate")),
            nullptr);
  EXPECT_EQ(
      find_top_level(empty_menu, QStringLiteral("viewport.frame_selection")),
      nullptr);
  for (const ContextActionSnapshot &action : empty_menu.actions) {
    EXPECT_TRUE(action.enabled) << action.text.toStdString();
    EXPECT_TRUE(action.children.empty());
  }

  quader::QuaderActionSet object_actions;
  object_actions = action_set(
      "object",
      {enabled_action("duplicate_meshes", "Duplicate Selected Meshes"),
       enabled_action("invert_mesh", "Flip Selected Mesh Normals"),
       enabled_action("shade_smooth_mesh",
                      "Set Selected Meshes to Smooth Shading"),
       enabled_action("copy_pivot_from_active", "Copy Pivot From Active"),
       enabled_action("snap_meshes_to_floor", "Snap Meshes to Floor"),
       enabled_action("combine_mesh", "Combine Selected Meshes"),
       enabled_action("half_cut", "Half-Cut Selected Meshes"),
       enabled_action("bevel_vertices", "Bevel Vertices"),
       enabled_action("bevel_edges", "Bevel Edges"),
       enabled_action("extract_faces", "Extract Faces"),
       enabled_tool("move", "Move"),
       enabled_tool("pivot", "Pivot"),
       enabled_tool("mirror", "Mirror"),
       enabled_tool("cut", "Cut"),
       enabled_tool("linear_array", "Linear Array"),
       enabled_tool("radial_array", "Radial Array"),
       enabled_tool("create_primitive", "Create Primitive"),
       enabled_action("delete_selection", "Delete Selection")});

  presenter.apply_ui_scale(quader::QtUiScaleMetrics{2.0F, 1U});
  const ContextMenuSnapshot object_menu = show_and_snapshot(
      owner, presenter, editor, input_router, localization, object_actions,
      editor.selection().summary(), true);
  ASSERT_TRUE(object_menu.opened);
  EXPECT_GT(object_menu.separator_count, 0u);
  EXPECT_EQ(object_menu.visible_separator_count, object_menu.separator_count);
  EXPECT_TRUE(object_menu.style_sheet.contains(
      QStringLiteral("QMenu#viewport_operations_context_menu::separator")));
  EXPECT_TRUE(object_menu.style_sheet.contains(QStringLiteral("palette(mid)")));
  EXPECT_TRUE(
      object_menu.style_sheet.contains(QStringLiteral("margin: 8px 16px")));
  presenter.apply_ui_scale(quader::QtUiScaleMetrics{1.0F, 2U});

  ASSERT_GE(object_menu.actions.size(), 4u);
  EXPECT_EQ(object_menu.actions[0].object_name,
            QStringLiteral("edit.duplicate"));
  EXPECT_TRUE(object_menu.actions[0].text.startsWith(
      QStringLiteral("Duplicate Object")));
  EXPECT_EQ(object_menu.actions[1].object_name,
            QStringLiteral("edit.create_instance"));
  EXPECT_EQ(object_menu.actions[2].object_name, QStringLiteral("edit.group"));
  EXPECT_TRUE(object_menu.actions[3].separator);

  const ContextActionSnapshot *mesh_duplicate =
      find_top_level(object_menu, QStringLiteral("duplicate_meshes"));
  ASSERT_NE(mesh_duplicate, nullptr);
  EXPECT_EQ(mesh_duplicate->text, QStringLiteral("Duplicate Mesh Data"));
  EXPECT_NE(find_top_level(object_menu, QStringLiteral("invert_mesh")), nullptr);
  EXPECT_NE(find_top_level(object_menu, QStringLiteral("shade_smooth_mesh")),
            nullptr);
  EXPECT_EQ(find_top_level(object_menu, QStringLiteral("bevel_vertices")),
            nullptr);
  EXPECT_EQ(find_top_level(object_menu, QStringLiteral("bevel_edges")), nullptr);
  EXPECT_EQ(find_top_level(object_menu, QStringLiteral("extract_faces")),
            nullptr);
  EXPECT_NE(find_top_level(object_menu,
                           QStringLiteral("modeling.remove_doubles")), nullptr);
  EXPECT_EQ(find_top_level(object_menu, QStringLiteral("tools.activate.mirror")),
            nullptr);
  EXPECT_EQ(find_top_level(object_menu,
                           QStringLiteral("tools.activate.insert_edge_loop")),
            nullptr);
  expect_no_unrelated_catalog_commands(object_menu);
  EXPECT_EQ(find_top_level(object_menu,
                           QStringLiteral("modeling.shade_smooth_mesh")),
            nullptr);

  EXPECT_NE(find_top_level(object_menu, QStringLiteral("snap_meshes_to_floor")),
            nullptr);
  EXPECT_NE(find_top_level(object_menu, QStringLiteral("combine_mesh")),
            nullptr);
  EXPECT_NE(find_top_level(object_menu, QStringLiteral("half_cut")), nullptr);
  EXPECT_EQ(find_top_level(object_menu, QStringLiteral("edit.apply_material")),
            nullptr);
  EXPECT_EQ(find_top_level(object_menu,
                           QStringLiteral("viewport.material.sample_face")),
            nullptr);
  EXPECT_NE(find_top_level(object_menu, QStringLiteral("edit.cut")), nullptr);
  EXPECT_NE(find_top_level(object_menu, QStringLiteral("edit.copy")), nullptr);
  ASSERT_FALSE(object_menu.actions.empty());
  EXPECT_EQ(object_menu.actions.back().object_name,
            QStringLiteral("delete_selection"));
  EXPECT_EQ(object_menu.actions.back().text,
            QStringLiteral("Delete Objects\tDelete"));
  ASSERT_TRUE(editor.commands().execute("edit.copy").ok());
  const auto paste_only = show_and_snapshot(
      owner, presenter, editor, input_router, localization, {},
      quader::QuaderSelectionSummary{}, false);
  expect_flat_available_menu(paste_only);
  ASSERT_EQ(paste_only.actions.size(), 1u);
  EXPECT_EQ(paste_only.actions.front().object_name, QStringLiteral("edit.paste"));

  const quader::QuaderActionSet group_actions = action_set(
      "group", {enabled_action("delete_selection", "Delete Selection")});
  const auto context_group = editor.scene().group_selection("Context Menu Group");
  ASSERT_TRUE(context_group.ok()) << context_group.error_message();
  ASSERT_TRUE(
      editor.selection().set_mode(quader::QuaderSelectionMode::Group).ok());
  ASSERT_TRUE(editor.selection().select_object(context_group.value().id).ok());
  const auto actual_group_selection = editor.selection().summary();
  ASSERT_TRUE(actual_group_selection.has_selection);
  ASSERT_EQ(actual_group_selection.selected_object_count, 1u);
  const ContextMenuSnapshot group_menu = show_and_snapshot(
      owner, presenter, editor, input_router, localization, group_actions,
      actual_group_selection, true);
  ASSERT_TRUE(group_menu.opened);
  ASSERT_FALSE(group_menu.actions.empty());
  EXPECT_TRUE(group_menu.actions.front().text.startsWith(
      QStringLiteral("Duplicate Group")));
  EXPECT_EQ(group_menu.actions.front().object_name,
            QStringLiteral("edit.duplicate"));
  EXPECT_NE(find_top_level(group_menu, QStringLiteral("edit.ungroup")),
            nullptr);
  EXPECT_EQ(find_top_level(
                group_menu,
                QStringLiteral("edit.apply_material")),
            nullptr);
  EXPECT_EQ(group_menu.actions.back().text,
            QStringLiteral("Delete Groups\tDelete"));
  EXPECT_EQ(find_top_level(group_menu,
                           QStringLiteral("modeling.remove_doubles")), nullptr);
  EXPECT_EQ(find_top_level(group_menu, QStringLiteral("edit.cut")), nullptr);
  EXPECT_EQ(find_top_level(group_menu, QStringLiteral("edit.copy")), nullptr);
  EXPECT_EQ(find_top_level(group_menu, QStringLiteral("edit.paste")), nullptr);
  expect_no_unrelated_catalog_commands(group_menu);

  const quader::QuaderActionSet vertex_actions = action_set(
      "vertex",
      {enabled_action("radial_align_vertices",
                      "Radially Align Selected Vertices"),
       enabled_action("dissolve_vertices", "Dissolve Selected Vertices"),
       enabled_action("connect_vertices", "Connect Selected Vertices"),
       enabled_action("snap_vertices_to_active",
                      "Snap Selected Vertices to Active Vertex"),
       enabled_action("merge_vertices", "Merge Vertices to Active"),
       enabled_action("bevel_vertices", "Bevel Selected Vertices"),
       enabled_action("merge_vertices_at_center",
                      "Merge Selected Vertices at Center"),
       enabled_action("merge_by_distance",
                      "Merge Selected Vertices by Distance"),
       enabled_action("snap_vertices_to_floor", "Snap Vertices to Floor"),
       enabled_action("duplicate_meshes", "Duplicate Mesh Data"),
       enabled_action("invert_mesh", "Flip Mesh Normals"),
       enabled_action("bevel_edges", "Bevel Edges"),
       enabled_action("extract_faces", "Extract Faces"),
       enabled_tool("rotate", "Rotate"),
       enabled_tool("mirror", "Mirror"),
       enabled_action("delete_selection", "Delete Selection")});
  ASSERT_TRUE(
      editor.selection().set_mode(quader::QuaderSelectionMode::Vertex).ok());
  ASSERT_TRUE(editor.selection().select_vertices(context_mesh.value(),
                                                {1, 2, 3, 4}).ok());
  const ContextMenuSnapshot vertex_menu = show_and_snapshot(
      owner, presenter, editor, input_router, localization, vertex_actions,
      editor.selection().summary(), true);
  ASSERT_TRUE(vertex_menu.opened);
  ASSERT_GE(vertex_menu.actions.size(), 5u);
  EXPECT_EQ(vertex_menu.actions[0].object_name,
            QStringLiteral("merge_vertices_at_center"));
  EXPECT_EQ(vertex_menu.actions[1].object_name,
            QStringLiteral("bevel_vertices"));
  EXPECT_EQ(vertex_menu.actions[2].object_name,
            QStringLiteral("connect_vertices"));
  EXPECT_NE(find_top_level(vertex_menu, QStringLiteral("radial_align_vertices")),
            nullptr);
  EXPECT_NE(find_top_level(vertex_menu, QStringLiteral("snap_vertices_to_floor")),
            nullptr);
  EXPECT_EQ(find_top_level(vertex_menu, QStringLiteral("bevel_edges")), nullptr);
  EXPECT_EQ(find_top_level(vertex_menu, QStringLiteral("extract_faces")), nullptr);
  expect_no_whole_object_operations(vertex_menu);
  expect_no_unrelated_catalog_commands(vertex_menu);
  EXPECT_EQ(vertex_menu.actions.back().text,
            QStringLiteral("Delete Vertices\tDelete"));
  EXPECT_EQ(find_top_level(
                vertex_menu,
                QStringLiteral("edit.apply_material")),
            nullptr);

  const quader::QuaderActionSet edge_actions = action_set(
      "edge",
      {enabled_action("collapse_edges", "Collapse Selected Edges"),
       enabled_action("soften_edge_normals", "Soften Selected Edge Normals"),
       enabled_action("default_edge_normals",
                      "Set Selected Edge Normals to Default"),
       enabled_action("connect_edges", "Connect Selected Edges"),
       enabled_action("bridge_edges", "Bridge Selected Edges"),
       enabled_action("snap_edges_to_active",
                      "Snap Selected Edges to Active Edge"),
       disabled_action("fill_hole", "Fill Hole from Selected Edges",
                       "Select a closed boundary."),
       enabled_action("bevel_edges", "Bevel Selected Edges"),
       enabled_action("interpolated_bridge_edges",
                      "Bridge Selected Edges with Interpolation"),
       enabled_action("split_edges", "Split Selected Edges"),
       enabled_action("harden_edge_normals", "Harden Selected Edge Normals"),
       enabled_action("dissolve_edges", "Dissolve Selected Edges"),
       enabled_action("merge_edges", "Merge Selected Edges"),
       enabled_action("radial_align_edges", "Radially Align Selected Edges"),
       enabled_action("half_cut", "Half Cut"),
       enabled_action("shade_smooth_mesh", "Shade Smooth"),
       enabled_action("bevel_vertices", "Bevel Vertices"),
       enabled_action("extract_faces", "Extract Faces"),
       enabled_tool("scale", "Scale"),
       enabled_tool("mirror", "Mirror"),
       enabled_tool("knife", "Knife"),
       enabled_tool("insert_edge_loop", "Insert Edge Loop"),
       enabled_action("delete_selection", "Delete Selection")});
  ASSERT_TRUE(
      editor.selection().set_mode(quader::QuaderSelectionMode::Edge).ok());
  ASSERT_TRUE(editor.selection().select_edge(context_mesh.value(), 1, 2).ok());
  const ContextMenuSnapshot edge_menu = show_and_snapshot(
      owner, presenter, editor, input_router, localization, edge_actions,
      editor.selection().summary(), true);
  ASSERT_TRUE(edge_menu.opened);
  ASSERT_GE(edge_menu.actions.size(), 6u);
  EXPECT_EQ(edge_menu.actions[0].object_name, QStringLiteral("bevel_edges"));
  EXPECT_NE(find_top_level(edge_menu, QStringLiteral("connect_edges")), nullptr);
  EXPECT_NE(find_top_level(edge_menu,
                           QStringLiteral("interpolated_bridge_edges")), nullptr);
  EXPECT_EQ(find_top_level(edge_menu, QStringLiteral("bridge_edges")), nullptr);
  EXPECT_EQ(find_top_level(edge_menu, QStringLiteral("fill_hole")), nullptr);
  EXPECT_NE(find_top_level(edge_menu, QStringLiteral("split_edges")), nullptr);
  EXPECT_NE(find_top_level(edge_menu, QStringLiteral("radial_align_edges")),
            nullptr);
  EXPECT_EQ(find_top_level(edge_menu, QStringLiteral("bevel_vertices")), nullptr);
  EXPECT_EQ(find_top_level(edge_menu, QStringLiteral("extract_faces")), nullptr);
  expect_no_whole_object_operations(edge_menu);
  expect_no_unrelated_catalog_commands(edge_menu);
  EXPECT_EQ(find_top_level(edge_menu, QStringLiteral("modeling.bevel_edges")),
            nullptr);
  const auto default_normals_index =
      top_level_index(edge_menu, QStringLiteral("default_edge_normals"));
  ASSERT_TRUE(default_normals_index.has_value());
  ASSERT_LT(*default_normals_index + 2u, edge_menu.actions.size());
  EXPECT_EQ(edge_menu.actions[*default_normals_index + 1u].object_name,
            QStringLiteral("harden_edge_normals"));
  EXPECT_EQ(edge_menu.actions[*default_normals_index + 2u].object_name,
            QStringLiteral("soften_edge_normals"));
  ASSERT_GT(*default_normals_index, 0u);
  EXPECT_TRUE(edge_menu.actions[*default_normals_index - 1u].separator);
  ASSERT_LT(*default_normals_index + 3u, edge_menu.actions.size());
  EXPECT_TRUE(edge_menu.actions[*default_normals_index + 3u].separator);
  EXPECT_EQ(edge_menu.actions.back().text,
            QStringLiteral("Delete Edges\tDelete"));
  EXPECT_EQ(find_top_level(
                edge_menu,
                QStringLiteral("edit.apply_material")),
            nullptr);

  const quader::QuaderActionSet face_actions = action_set(
      "face",
      {enabled_action("detach_faces", "Detach Selected Faces"),
       enabled_action("invert_faces", "Flip Selected Face Normals"),
       enabled_action("extract_faces", "Extract Selected Faces"),
       enabled_action("combine_faces", "Combine Selected Faces"),
       enabled_action("bridge_faces", "Bridge Selected Faces"),
       enabled_action("slice_quad", "Slice Selected Quad Face"),
       enabled_action("thicken_faces", "Thicken Selected Faces"),
       enabled_action("interpolated_bridge_faces",
                      "Bridge Selected Faces with Interpolation"),
       enabled_action("collapse_faces", "Collapse Selected Faces"),
       enabled_action("radial_align_faces", "Radially Align Selected Faces"),
       enabled_action("combine_mesh", "Combine Meshes"),
       enabled_action("snap_meshes_to_floor", "Snap Meshes to Floor"),
       enabled_action("remove_doubles", "Remove Doubles"),
       enabled_action("bevel_vertices", "Bevel Vertices"),
       enabled_action("bevel_edges", "Bevel Edges"),
       enabled_tool("move", "Move"),
       enabled_tool("rotate", "Rotate"),
       enabled_tool("scale", "Scale"),
       enabled_tool("mirror", "Mirror"),
       enabled_action("delete_selection", "Delete Selection")});

  ASSERT_TRUE(
      editor.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(context_mesh.value(), 1).ok());

  const ContextMenuSnapshot face_menu = show_and_snapshot(
      owner, presenter, editor, input_router, localization, face_actions,
      editor.selection().summary(), true);
  ASSERT_TRUE(face_menu.opened);
  ASSERT_GE(face_menu.actions.size(), 4u);
  EXPECT_EQ(face_menu.actions[0].object_name,
            QStringLiteral("thicken_faces"));
  EXPECT_NE(find_top_level(face_menu,
                           QStringLiteral("interpolated_bridge_faces")), nullptr);
  EXPECT_EQ(find_top_level(face_menu, QStringLiteral("bridge_faces")), nullptr);
  EXPECT_NE(find_top_level(face_menu, QStringLiteral("combine_faces")), nullptr);
  EXPECT_NE(find_top_level(face_menu, QStringLiteral("extract_faces")), nullptr);
  EXPECT_NE(find_top_level(face_menu, QStringLiteral("detach_faces")), nullptr);
  EXPECT_NE(find_top_level(face_menu, QStringLiteral("invert_faces")), nullptr);
  EXPECT_NE(find_top_level(face_menu,
                           QStringLiteral("modeling.recalculate_face_normals")),
            nullptr);
  EXPECT_EQ(find_top_level(face_menu, QStringLiteral("bevel_vertices")), nullptr);
  EXPECT_EQ(find_top_level(face_menu, QStringLiteral("bevel_edges")), nullptr);
  expect_no_whole_object_operations(face_menu);
  expect_no_unrelated_catalog_commands(face_menu);
  EXPECT_EQ(find_top_level(face_menu, QStringLiteral("modeling.extract_faces")),
            nullptr);
  EXPECT_EQ(find_top_level(face_menu, QStringLiteral("edit.apply_material")),
            nullptr);
  EXPECT_EQ(find_top_level(face_menu,
                           QStringLiteral("viewport.material.sample_face")),
            nullptr);
  EXPECT_EQ(face_menu.actions.back().text,
            QStringLiteral("Delete Faces\tDelete"));

  const auto light = editor.scene().create_point_light();
  ASSERT_TRUE(light.ok()) << light.error_message();
  ASSERT_TRUE(
      editor.selection().set_mode(quader::QuaderSelectionMode::Object).ok());
  ASSERT_TRUE(editor.selection().clear().ok());
  ASSERT_TRUE(editor.selection().select_object(light.value().id).ok());
  const quader::QuaderActionSet light_actions =
      editor.modeling().available_action_set({
          .surface = quader::QuaderActionSurface::ViewportContextMenu,
          .include_disabled = true,
      });
  const ContextMenuSnapshot light_only_menu = show_and_snapshot(
      owner, presenter, editor, input_router, localization, light_actions,
      editor.selection().summary(), true);
  expect_no_unrelated_catalog_commands(light_only_menu);
  EXPECT_NE(find_top_level(light_only_menu, QStringLiteral("edit.duplicate")),
            nullptr);
  EXPECT_EQ(find_top_level(light_only_menu, QStringLiteral("combine_mesh")),
            nullptr);
  EXPECT_EQ(find_top_level(light_only_menu, QStringLiteral("half_cut")), nullptr);
  EXPECT_EQ(find_top_level(light_only_menu,
                           QStringLiteral("tools.activate.curve")), nullptr);
  EXPECT_EQ(find_top_level(light_only_menu,
                           QStringLiteral("edit.apply_material")),
            nullptr);
  EXPECT_EQ(find_top_level(light_only_menu, QStringLiteral("edit.cut")), nullptr);
  EXPECT_EQ(find_top_level(light_only_menu, QStringLiteral("edit.copy")), nullptr);
  EXPECT_EQ(find_top_level(light_only_menu, QStringLiteral("edit.paste")), nullptr);

  const std::array<const ContextMenuSnapshot *, 7> menus{
      &paste_only,  &object_menu, &group_menu,
      &vertex_menu, &edge_menu,   &face_menu, &light_only_menu};
  for (const ContextMenuSnapshot *snapshot : menus) {
    ASSERT_NE(snapshot, nullptr);
    expect_flat_available_menu(*snapshot);
  }

  const auto first = editor.modeling().create_box(
      quader::QuaderBounds{.min = {-2.0f, -1.0f, -1.0f},
                           .max = {0.0f, 1.0f, 1.0f}},
      "Context Fence First");
  const auto second = editor.modeling().create_box(
      quader::QuaderBounds{.min = {1.0f, -1.0f, -1.0f},
                           .max = {3.0f, 1.0f, 1.0f}},
      "Context Fence Second");
  ASSERT_TRUE(first.ok()) << first.error_message();
  ASSERT_TRUE(second.ok()) << second.error_message();
  ASSERT_TRUE(editor.selection().select_object(first.value()).ok());
  const quader::QuaderSelectionSummary presented_selection =
      editor.selection().summary();
  const std::uint64_t presented_selection_revision =
      presented_selection.selection_revision;
  const std::uint64_t command_status_revision =
      editor.commands().last_status_revision();
  bool copy_action_activated = false;

  QTimer::singleShot(0, &owner, [&]() {
    auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
    if (menu == nullptr) {
      ADD_FAILURE() << "Expected the viewport context menu to open.";
      if (QWidget *popup = QApplication::activePopupWidget()) {
        popup->close();
      }
      return;
    }
    QAction *copy_action = nullptr;
    for (QAction *action : menu->actions()) {
      if (action != nullptr &&
          action->objectName() == QStringLiteral("edit.copy")) {
        copy_action = action;
        break;
      }
    }
    if (copy_action == nullptr || !copy_action->isEnabled()) {
      ADD_FAILURE() << "Expected enabled Copy context action.";
      menu->close();
      return;
    }
    QObject::connect(copy_action, &QAction::triggered, &owner,
                     [&copy_action_activated]() {
                       copy_action_activated = true;
                     });

    const quader::QuaderApiVoidResult changed_selection =
        editor.selection().select_object(second.value());
    if (!changed_selection.ok()) {
      ADD_FAILURE() << changed_selection.error_message();
      menu->close();
      return;
    }
    EXPECT_GT(editor.selection().summary().selection_revision,
              presented_selection_revision);

    menu->setActiveAction(copy_action);
    QKeyEvent accept_action(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(menu, &accept_action);
    if (menu->isVisible()) {
      menu->close();
    }
  });

  presenter.show(owner, owner.mapToGlobal(owner.rect().center()), editor,
                 input_router, localization, {}, presented_selection, true,
                 std::nullopt);
  EXPECT_TRUE(copy_action_activated);
  EXPECT_EQ(editor.commands().last_status_revision(), command_status_revision)
      << "A context-menu action must not execute after the selection used to "
         "build the menu changes inside QMenu's nested event loop.";
}

TEST(QtViewportContextMenuTests,
     RealSelectionMenusExcludeCommandsForOtherEditingScopes) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-viewport-context-scope-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const auto temporary_root =
      std::filesystem::path(temporary.path().toStdWString());
  const auto project_root = temporary_root / "Context Menu Project";
  ASSERT_TRUE(std::filesystem::create_directory(project_root));
  quader::ProjectSession project(temporary_root / "recent-scenes.json");
  const auto created = project.create(project_root);
  ASSERT_TRUE(created.ok) << created.message;
  quader::QuaderEditorApi editor(project);
  quader::ViewportInputRouter input_router;
  quader::LocalizationService localization = loaded_localization();
  quader::QtViewportContextMenuPresenter presenter;
  QWidget owner;
  owner.setAttribute(Qt::WA_DontShowOnScreen, true);
  owner.resize(320, 240);
  owner.show();
  QCoreApplication::processEvents();

  const auto mesh = editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      "Real Menu Selection");
  ASSERT_TRUE(mesh.ok()) << mesh.error_message();
  ASSERT_TRUE(editor.selection().select_object(mesh.value()).ok());
  ASSERT_TRUE(editor.commands().execute("edit.copy").ok());
  const auto paste = editor.commands().descriptor("edit.paste");
  ASSERT_TRUE(paste.has_value());
  ASSERT_TRUE(paste->enabled);

  const auto snapshot = [&]() {
    const auto actions = editor.modeling().available_action_set({
        .surface = quader::QuaderActionSurface::ViewportContextMenu,
        .include_disabled = true,
    });
    return show_and_snapshot(owner, presenter, editor, input_router,
                             localization, actions,
                             editor.selection().summary(), true);
  };
  const ContextMenuSnapshot mesh_menu = snapshot();
  expect_flat_available_menu(mesh_menu);
  expect_no_unrelated_catalog_commands(mesh_menu);
  for (const QString &id : {QStringLiteral("edit.group"),
                            QStringLiteral("edit.cut"),
                            QStringLiteral("edit.copy"),
                            QStringLiteral("edit.paste"),
                            QStringLiteral("modeling.recalculate_face_normals"),
                            QStringLiteral("modeling.remove_doubles")}) {
    EXPECT_NE(find_top_level(mesh_menu, id), nullptr) << id.toStdString();
  }

  struct ComponentCase final {
    quader::QuaderSelectionMode mode;
    const char *operation;
  };
  const std::array components{
      ComponentCase{quader::QuaderSelectionMode::Vertex, "bevel_vertices"},
      ComponentCase{quader::QuaderSelectionMode::Edge, "bevel_edges"},
      ComponentCase{quader::QuaderSelectionMode::Face, "extract_faces"},
  };
  for (const ComponentCase &component : components) {
    SCOPED_TRACE(component.operation);
    ASSERT_TRUE(editor.selection().set_mode(component.mode).ok());
    switch (component.mode) {
    case quader::QuaderSelectionMode::Vertex:
      ASSERT_TRUE(editor.selection().select_vertices(mesh.value(), {1, 2}).ok());
      break;
    case quader::QuaderSelectionMode::Edge:
      ASSERT_TRUE(editor.selection().select_edge(mesh.value(), 1, 2).ok());
      break;
    case quader::QuaderSelectionMode::Face:
      ASSERT_TRUE(editor.selection().select_face(mesh.value(), 1).ok());
      break;
    default:
      FAIL() << "Expected a component selection.";
    }
    const ContextMenuSnapshot menu = snapshot();
    expect_flat_available_menu(menu);
    expect_no_unrelated_catalog_commands(menu);
    expect_no_whole_object_operations(menu);
    EXPECT_NE(find_top_level(menu, QString::fromLatin1(component.operation)),
              nullptr);
    EXPECT_NE(find_top_level(menu, QStringLiteral("delete_selection")), nullptr);
    const auto empty_space = show_and_snapshot(
        owner, presenter, editor, input_router, localization, {},
        editor.selection().summary(), false);
    EXPECT_FALSE(empty_space.opened);
    EXPECT_TRUE(empty_space.actions.empty());
    EXPECT_EQ(find_top_level(menu,
                            QStringLiteral("modeling.recalculate_face_normals")) !=
                  nullptr,
              component.mode == quader::QuaderSelectionMode::Face);
    for (const ComponentCase &other : components) {
      if (other.mode != component.mode) {
        EXPECT_EQ(find_top_level(menu, QString::fromLatin1(other.operation)),
                  nullptr);
      }
    }
  }

  ASSERT_TRUE(
      editor.selection().set_mode(quader::QuaderSelectionMode::Object).ok());
  ASSERT_TRUE(editor.selection().select_object(mesh.value()).ok());
  ASSERT_TRUE(editor.modeling().create_instance().ok());
  for (const std::string_view command : {"edit.cut", "edit.copy"}) {
    const auto descriptor = editor.commands().descriptor(command);
    ASSERT_TRUE(descriptor.has_value()) << command;
    ASSERT_TRUE(descriptor->enabled) << command;
  }
  const auto copied_instance = editor.commands().execute("edit.copy");
  ASSERT_TRUE(copied_instance.ok()) << copied_instance.error_message();
  const auto paste_instance = editor.commands().descriptor("edit.paste");
  ASSERT_TRUE(paste_instance.has_value());
  ASSERT_TRUE(paste_instance->enabled);
  const ContextMenuSnapshot instance_menu = snapshot();
  expect_flat_available_menu(instance_menu);
  expect_no_unrelated_catalog_commands(instance_menu);
  for (const QString &id : {QStringLiteral("edit.cut"),
                            QStringLiteral("edit.copy"),
                            QStringLiteral("edit.paste"),
                            QStringLiteral("edit.flatten_instance")}) {
    EXPECT_NE(find_top_level(instance_menu, id), nullptr) << id.toStdString();
  }
}

TEST(QtViewportContextMenuTests,
     FacingFacesExposeOneBridgeAndStartItsPreviewFromTheMenu) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-viewport-bridge-context-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
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

  const auto source = editor.modeling().create_box(
      {.min = {-3.0f, -1.0f, -1.0f}, .max = {-1.0f, 1.0f, 1.0f}},
      "Bridge Source");
  const auto target = editor.modeling().create_box(
      {.min = {1.0f, -1.0f, -1.0f}, .max = {3.0f, 1.0f, 1.0f}},
      "Bridge Target");
  ASSERT_TRUE(source.ok()) << source.error_message();
  ASSERT_TRUE(target.ok()) << target.error_message();
  ASSERT_TRUE(
      editor.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
  const auto source_pick = editor.selection().pick_select({
      .ray = {.origin = {0.0f, 0.0f, 0.0f},
              .direction = {-1.0f, 0.0f, 0.0f}},
      .click_count = 1,
  });
  ASSERT_TRUE(source_pick.ok()) << source_pick.error_message();
  ASSERT_EQ(editor.selection().summary().selected_face_count, 1u);
  ASSERT_EQ(editor.selection().summary().active_object_id,
            source.value().modeling_object_id);

  const auto actions = [&]() {
    return editor.modeling().available_action_set({
        .surface = quader::QuaderActionSurface::ViewportContextMenu,
        .include_disabled = true,
    });
  };
  const auto single_face = show_and_snapshot(
      owner, presenter, editor, input_router, localization, actions(),
      editor.selection().summary(), true);
  expect_flat_available_menu(single_face);
  EXPECT_EQ(find_top_level(single_face, QStringLiteral("bridge_faces")), nullptr);
  EXPECT_EQ(find_top_level(single_face,
                           QStringLiteral("interpolated_bridge_faces")), nullptr);

  const auto target_pick = editor.selection().pick_select({
      .ray = {.origin = {0.0f, 0.0f, 0.0f},
              .direction = {1.0f, 0.0f, 0.0f}},
      .click_count = 1,
      .shift_pressed = true,
  });
  ASSERT_TRUE(target_pick.ok()) << target_pick.error_message();
  ASSERT_EQ(editor.selection().summary().selected_face_count, 2u);
  ASSERT_EQ(editor.selection().summary().active_object_id,
            target.value().modeling_object_id);
  const auto bridge = editor.commands().descriptor(
      editor.commands().command_id_for_modeling_action("interpolated_bridge_faces"));
  ASSERT_TRUE(bridge.has_value());
  ASSERT_TRUE(bridge->enabled);

  const auto supplemental = show_and_snapshot(
      owner, presenter, editor, input_router, localization, {},
      editor.selection().summary(), true);
  expect_flat_available_menu(supplemental);
  EXPECT_EQ(find_top_level(supplemental,
                           QStringLiteral("modeling.bridge_faces")), nullptr);
  EXPECT_NE(find_top_level(supplemental,
                           QStringLiteral("modeling.interpolated_bridge_faces")),
            nullptr);

  EXPECT_FALSE(editor.modeling().can_cancel_active_tool_or_preview());
  const auto bridged = show_and_snapshot(
      owner, presenter, editor, input_router, localization, actions(),
      editor.selection().summary(), true, std::nullopt,
      QStringLiteral("interpolated_bridge_faces"));
  expect_flat_available_menu(bridged);
  EXPECT_EQ(find_top_level(bridged, QStringLiteral("bridge_faces")), nullptr);
  EXPECT_NE(find_top_level(bridged,
                           QStringLiteral("interpolated_bridge_faces")), nullptr);
  EXPECT_EQ(std::ranges::count_if(bridged.actions, [](const auto &action) {
              return action.text.section(QLatin1Char('\t'), 0, 0) ==
                     QStringLiteral("Bridge Faces");
            }),
            1);
  EXPECT_TRUE(editor.modeling().can_cancel_active_tool_or_preview());
  EXPECT_TRUE(editor.tools().active_settings().preview_active);
  const auto canceled = editor.modeling().cancel_preview();
  ASSERT_TRUE(canceled.ok()) << canceled.error_message();
  EXPECT_FALSE(editor.modeling().can_cancel_active_tool_or_preview());
  EXPECT_EQ(editor.selection().summary().selected_face_count, 2u);
}

TEST(QtViewportContextMenuTests,
     EdgeNormalActionsOnlyAppearWithApplicableSelection) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-viewport-edge-normal-actions-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
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

  const auto box = editor.modeling().create_box(
      quader::QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                           .max = {1.0f, 1.0f, 1.0f}},
      "Edge Normal Availability Box");
  ASSERT_TRUE(box.ok()) << box.error_message();

  const auto context_actions = [&]() {
    return editor.modeling().available_action_set({
        .surface = quader::QuaderActionSurface::ViewportContextMenu,
        .include_disabled = true,
    });
  };
  const auto expect_normal_actions = [](const ContextMenuSnapshot &menu) {
    expect_flat_available_menu(menu);
    for (const QString &action_id :
         {QStringLiteral("default_edge_normals"),
          QStringLiteral("harden_edge_normals"),
          QStringLiteral("soften_edge_normals")}) {
      const ContextActionSnapshot *action = find_top_level(menu, action_id);
      ASSERT_NE(action, nullptr) << action_id.toStdString();
      EXPECT_TRUE(action->enabled) << action_id.toStdString();
    }
  };

  ASSERT_TRUE(
      editor.selection().set_mode(quader::QuaderSelectionMode::Edge).ok());
  ASSERT_TRUE(editor.selection().clear().ok());
  quader::QuaderSelectionSummary empty_edge_summary =
      editor.selection().summary();
  empty_edge_summary.has_selection = true;
  empty_edge_summary.selected_object_count = 1;
  const ContextMenuSnapshot empty_edge_menu = show_and_snapshot(
      owner, presenter, editor, input_router, localization, context_actions(),
      empty_edge_summary, true);
  if (empty_edge_menu.opened) {
    expect_flat_available_menu(empty_edge_menu);
  } else {
    EXPECT_TRUE(empty_edge_menu.actions.empty());
  }
  EXPECT_EQ(find_top_level(empty_edge_menu,
                           QStringLiteral("default_edge_normals")), nullptr);
  EXPECT_EQ(find_top_level(empty_edge_menu,
                           QStringLiteral("harden_edge_normals")), nullptr);
  EXPECT_EQ(find_top_level(empty_edge_menu,
                           QStringLiteral("soften_edge_normals")), nullptr);

  ASSERT_TRUE(
      editor.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
  quader::QuaderSelectionSummary wrong_mode_summary =
      editor.selection().summary();
  wrong_mode_summary.has_selection = true;
  wrong_mode_summary.selected_object_count = 1;
  const ContextMenuSnapshot wrong_mode_menu = show_and_snapshot(
      owner, presenter, editor, input_router, localization, context_actions(),
      wrong_mode_summary, true);
  if (wrong_mode_menu.opened) {
    expect_flat_available_menu(wrong_mode_menu);
  } else {
    EXPECT_TRUE(wrong_mode_menu.actions.empty());
  }
  EXPECT_EQ(find_top_level(wrong_mode_menu,
                           QStringLiteral("default_edge_normals")), nullptr);
  EXPECT_EQ(find_top_level(wrong_mode_menu,
                           QStringLiteral("harden_edge_normals")), nullptr);
  EXPECT_EQ(find_top_level(wrong_mode_menu,
                           QStringLiteral("soften_edge_normals")), nullptr);

  ASSERT_TRUE(
      editor.selection().set_mode(quader::QuaderSelectionMode::Edge).ok());
  ASSERT_TRUE(editor.selection().select_edge(box.value(), 1, 2).ok());
  const ContextMenuSnapshot selected_edge_menu = show_and_snapshot(
      owner, presenter, editor, input_router, localization, context_actions(),
      editor.selection().summary(), true);
  ASSERT_TRUE(selected_edge_menu.opened);
  expect_normal_actions(selected_edge_menu);
}

TEST(QtViewportContextMenuTests, SelectedCurveHasDirectApplicableOperations) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-viewport-curve-context-menu-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  quader::ViewportInputRouter input_router;
  quader::LocalizationService localization = loaded_localization();
  quader::QtViewportContextMenuPresenter presenter;
  const auto curve = editor.modeling().create_curve({
      .points = {{.position = {0.0f, 0.0f, 0.0f}},
                 {.position = {2.0f, 1.0f, 0.0f}},
                 {.position = {4.0f, 0.0f, 0.0f}}},
      .name = "Curve Context Actions",
  });
  ASSERT_TRUE(curve.ok()) << curve.error_message();
  ASSERT_TRUE(
      editor.selection().set_mode(quader::QuaderSelectionMode::Object).ok());
  ASSERT_TRUE(editor.selection().select_object(curve.value()).ok());

  QWidget owner;
  owner.setAttribute(Qt::WA_DontShowOnScreen, true);
  owner.resize(320, 240);
  owner.show();
  QCoreApplication::processEvents();

  quader::QuaderActionSet actions =
      editor.modeling().available_action_set({
          .surface = quader::QuaderActionSurface::ViewportContextMenu,
          .include_disabled = true,
      });
  ASSERT_FALSE(actions.panels.empty());
  actions.panels.front().actions.push_back(enabled_tool("curve", "Curve"));
  actions.panels.front().actions.push_back(
      enabled_tool("path_array", "Path Array"));
  const ContextMenuSnapshot menu = show_and_snapshot(
      owner, presenter, editor, input_router, localization, actions,
      editor.selection().summary(), true);
  expect_flat_available_menu(menu);
  expect_no_unrelated_catalog_commands(menu);
  for (const QString &command_id :
       {QStringLiteral("edit.duplicate"),
        QStringLiteral("delete_selection")}) {
    EXPECT_NE(find_top_level(menu, command_id), nullptr)
        << command_id.toStdString();
  }
  for (const QString &action_id :
       {QStringLiteral("combine_mesh"), QStringLiteral("half_cut"),
        QStringLiteral("create_outer_hull"), QStringLiteral("invert_mesh"),
        QStringLiteral("shade_smooth_mesh"),
        QStringLiteral("modeling.combine_meshes"),
        QStringLiteral("modeling.texture.project_cube"),
        QStringLiteral("tools.activate.extrude"),
        QStringLiteral("edit.group"), QStringLiteral("edit.ungroup"),
        QStringLiteral("edit.cut"), QStringLiteral("edit.copy"),
        QStringLiteral("edit.paste")}) {
    EXPECT_EQ(find_top_level(menu, action_id), nullptr)
        << action_id.toStdString();
  }
  EXPECT_EQ(find_top_level(menu, QStringLiteral("curve.solidify.apply")), nullptr);

  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Curve).ok());
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  ASSERT_TRUE(editor.tools().set_setting("curve", "curve.solidify", true).ok());
  const auto settings = editor.tools().active_settings();
  ASSERT_EQ(settings.tool_id, "curve");
  const auto apply = std::ranges::find_if(settings.controls, [](const auto &control) {
    return control.id == "curve.solidify.apply";
  });
  ASSERT_NE(apply, settings.controls.end());
  ASSERT_TRUE(apply->enabled);
  const auto solidify_menu = show_and_snapshot(
      owner, presenter, editor, input_router, localization, actions,
      editor.selection().summary(), true, std::nullopt,
      QStringLiteral("curve.solidify.apply"));
  expect_flat_available_menu(solidify_menu);
  EXPECT_NE(find_top_level(solidify_menu,
                           QStringLiteral("curve.solidify.apply")), nullptr);
  const auto source_after = editor.query().entity(curve.value().entity);
  ASSERT_TRUE(source_after.has_value());
  EXPECT_TRUE(source_after->has_curve);
  const auto generated = editor.query().entities();
  EXPECT_EQ(std::ranges::count_if(generated, [](const auto &entity) {
              return entity.has_mesh && !entity.has_curve;
            }),
            1);
  ASSERT_TRUE(editor.history().undo().ok());
  const auto restored = editor.query().entities();
  EXPECT_FALSE(std::ranges::any_of(restored, [](const auto &entity) {
    return entity.has_mesh && !entity.has_curve;
  }));

  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Curve).ok());
  ASSERT_TRUE(editor.selection().select_all().ok());
  const auto controls = editor.selection().summary();
  ASSERT_EQ(controls.sub_selection_kind,
            quader::QuaderSubSelectionKind::CurveControl);
  ASSERT_GT(controls.selected_sub_element_count, 0u);
  const auto control_actions = editor.modeling().available_action_set({
      .surface = quader::QuaderActionSurface::ViewportContextMenu,
      .include_disabled = true,
  });
  const ContextMenuSnapshot control_menu = show_and_snapshot(
      owner, presenter, editor, input_router, localization, control_actions,
      controls, true);
  expect_flat_available_menu(control_menu);
  expect_no_unrelated_catalog_commands(control_menu);
  expect_no_whole_object_operations(control_menu);
  ASSERT_EQ(control_menu.actions.size(), 1u);
  EXPECT_TRUE(control_menu.actions.front().text.startsWith(
      QStringLiteral("Delete Control Points")));
}

TEST(QtViewportContextMenuTests,
     MaterialTransfersUseClickedFaceAndPreserveSourceSelection) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-viewport-material-transfer-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  quader::ViewportInputRouter input_router;
  quader::LocalizationService localization = loaded_localization();
  quader::QtViewportContextMenuPresenter presenter;
  std::vector<quader::QuaderNotification> notifications;
  editor.ui().set_runtime_hooks({.notify = [&](quader::QuaderNotification value) {
    notifications.push_back(std::move(value));
    return quader::QuaderApiVoidResult{.ok = true};
  }});

  QWidget owner;
  owner.setAttribute(Qt::WA_DontShowOnScreen, true);
  owner.resize(320, 240);
  owner.show();
  QCoreApplication::processEvents();

  const auto amber = editor.assets().add_material(
      quader::QuaderMaterialArgs{.name = "Transfer Amber"});
  const auto blue = editor.assets().add_material(
      quader::QuaderMaterialArgs{.name = "Transfer Blue"});
  ASSERT_TRUE(amber.ok()) << amber.error_message();
  ASSERT_TRUE(blue.ok()) << blue.error_message();
  const auto source = editor.modeling().create_box({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Selected Source",
      .material = quader::QuaderMaterialReference::embedded(amber.value().id),
  });
  const auto target = editor.modeling().create_box({
      .bounds = {.min = {1.0f, -1.0f, -1.0f}, .max = {3.0f, 1.0f, 1.0f}},
      .name = "Clicked Target",
      .material = quader::QuaderMaterialReference::embedded(blue.value().id),
  });
  ASSERT_TRUE(source.ok()) << source.error_message();
  ASSERT_TRUE(target.ok()) << target.error_message();
  ASSERT_TRUE(
      editor.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(source.value(), 1).ok());
  ASSERT_TRUE(editor.modeling().set_face_texture_setting({
      .field = quader::QuaderFaceTextureField::ScaleX,
      .value = 1.25,
      .selection_token = editor.modeling().face_texture_settings().selection_token,
  }).ok());
  const quader::QuaderHoverTarget clicked_face{
      .kind = quader::QuaderActiveSelectionKind::Face,
      .object_id = target.value().modeling_object_id,
      .face_id = 1,
  };
  const auto read_face = [&](quader::QuaderModelingObjectRef object) {
    const auto mesh =
        session.modeling().compile_object_mesh(object.modeling_object_id);
    EXPECT_TRUE(mesh.has_value());
    if (!mesh.has_value() || !mesh->authored_polygon_document) {
      ADD_FAILURE() << "Expected a published polygon face.";
      return quader_poly::Face{};
    }
    const auto *face =
        quader_poly::find_face(*mesh->authored_polygon_document, 1);
    EXPECT_NE(face, nullptr);
    return face != nullptr ? *face : quader_poly::Face{};
  };
  const auto read_material = [&](quader::QuaderModelingObjectRef object) {
    const auto *materials = session.scene().material(object.entity);
    EXPECT_NE(materials, nullptr);
    return materials != nullptr
               ? materials->material_for_slot(read_face(object).material_slot)
               : quader::MaterialReference{};
  };
  const auto source_before = read_face(source.value());
  const auto target_before = read_face(target.value());
  const quader::QuaderSelectionSummary selected = editor.selection().summary();
  const std::size_t history_count = session.history().undo_count();
  const std::array transfers{
      std::pair{QStringLiteral("viewport.material.apply_selected_face_to_face"),
                QStringLiteral("Shift+RMouse")},
      std::pair{QStringLiteral("viewport.material.apply_face_to_selection"),
                QStringLiteral("Ctrl+RMouse")},
      std::pair{QStringLiteral("viewport.material.transfer_face_texture"),
                QStringLiteral("Alt+RMouse")},
  };
  for (const auto &transfer : transfers) {
    const auto &action_id = transfer.first;
    SCOPED_TRACE(action_id.toStdString());
    const ContextMenuSnapshot menu = show_and_snapshot(
        owner, presenter, editor, input_router, localization, {},
        editor.selection().summary(), true, clicked_face, action_id);
    expect_flat_available_menu(menu);
    expect_no_unrelated_catalog_commands(menu);
    for (const auto &[transfer_id, transfer_shortcut] : transfers) {
      const auto *action = find_top_level(menu, transfer_id);
      ASSERT_NE(action, nullptr) << transfer_id.toStdString();
      EXPECT_TRUE(action->text.endsWith(QLatin1Char('\t') + transfer_shortcut))
          << transfer_id.toStdString() << " text: " << action->text.toStdString()
          << "; canonical shortcut: "
          << editor.commands().shortcut_for(transfer_id.toStdString());
    }
    EXPECT_TRUE(notifications.empty());
    EXPECT_EQ(session.history().undo_count(), history_count + 1u);
    const bool samples_target =
        action_id == QStringLiteral("viewport.material.apply_face_to_selection");
    const bool transfers_texture =
        action_id == QStringLiteral("viewport.material.transfer_face_texture");
    EXPECT_EQ(read_material(source.value()),
              quader::MaterialReference{samples_target ? blue.value().id
                                                       : amber.value().id});
    EXPECT_EQ(read_material(target.value()),
              quader::MaterialReference{samples_target ? blue.value().id
                                                       : amber.value().id});
    EXPECT_EQ(face_uv_coordinates(read_face(source.value())),
              face_uv_coordinates(source_before));
    EXPECT_EQ(read_face(source.value()).texture_transform,
              source_before.texture_transform);
    const auto target_after = read_face(target.value());
    if (transfers_texture) {
      EXPECT_EQ(target_after.texture_transform, source_before.texture_transform);
      EXPECT_NE(face_uv_coordinates(target_after), face_uv_coordinates(target_before));
    } else {
      EXPECT_EQ(face_uv_coordinates(target_after), face_uv_coordinates(target_before));
      EXPECT_EQ(target_after.texture_transform, target_before.texture_transform);
    }
    const auto selection_after = editor.selection().summary();
    EXPECT_EQ(selection_after.active_object_id, selected.active_object_id);
    EXPECT_EQ(selection_after.active_face_id, selected.active_face_id);
    EXPECT_EQ(selection_after.selected_face_count, 1u);
    EXPECT_EQ(selection_after.selection_revision, selected.selection_revision);
    ASSERT_TRUE(editor.history().undo().ok());
    EXPECT_EQ(session.history().undo_count(), history_count);
    EXPECT_EQ(read_material(source.value()), quader::MaterialReference{amber.value().id});
    EXPECT_EQ(read_material(target.value()), quader::MaterialReference{blue.value().id});
    EXPECT_EQ(face_uv_coordinates(read_face(target.value())),
              face_uv_coordinates(target_before));
    EXPECT_EQ(read_face(target.value()).texture_transform,
              target_before.texture_transform);
  }

  ASSERT_TRUE(editor.selection().select_face(source.value(), 2,
                                            quader::QuaderSelectionEdit::Add).ok());
  const ContextMenuSnapshot multiple_faces = show_and_snapshot(
      owner, presenter, editor, input_router, localization, {},
      editor.selection().summary(), true, clicked_face);
  expect_flat_available_menu(multiple_faces);
  EXPECT_NE(find_top_level(multiple_faces,
                           QStringLiteral("viewport.material.apply_face_to_selection")),
            nullptr);
  EXPECT_EQ(find_top_level(multiple_faces,
                           QStringLiteral("viewport.material.apply_selected_face_to_face")),
            nullptr);
  EXPECT_EQ(find_top_level(multiple_faces,
                           QStringLiteral("viewport.material.transfer_face_texture")),
            nullptr);

  ASSERT_TRUE(editor.selection().select_face(source.value(), 1).ok());
  ASSERT_TRUE(editor.scene().set_locked(target.value().entity, true).ok());
  const std::size_t locked_history_count = session.history().undo_count();
  const ContextMenuSnapshot locked_source = show_and_snapshot(
      owner, presenter, editor, input_router, localization, {},
      editor.selection().summary(), true, clicked_face);
  expect_flat_available_menu(locked_source);
  EXPECT_EQ(find_top_level(locked_source,
                           QStringLiteral("viewport.material.apply_face_to_selection")),
            nullptr);
  EXPECT_EQ(find_top_level(locked_source,
                           QStringLiteral("viewport.material.apply_selected_face_to_face")),
            nullptr);
  EXPECT_EQ(find_top_level(locked_source,
                           QStringLiteral("viewport.material.transfer_face_texture")),
            nullptr);
  EXPECT_EQ(read_material(source.value()), quader::MaterialReference{amber.value().id});
  EXPECT_EQ(read_material(target.value()), quader::MaterialReference{blue.value().id});
  EXPECT_EQ(session.history().undo_count(), locked_history_count);
  EXPECT_TRUE(notifications.empty());
  ASSERT_TRUE(editor.scene().set_locked(target.value().entity, false).ok());

  ASSERT_TRUE(editor.selection().select_face(target.value(), 1).ok());
  const ContextMenuSnapshot same_face = show_and_snapshot(
      owner, presenter, editor, input_router, localization, {},
      editor.selection().summary(), true, clicked_face);
  expect_flat_available_menu(same_face);
  ASSERT_TRUE(
      editor.selection().set_mode(quader::QuaderSelectionMode::Object).ok());
  ASSERT_TRUE(editor.selection().select_object(source.value()).ok());
  const ContextMenuSnapshot object_mode = show_and_snapshot(
      owner, presenter, editor, input_router, localization, {},
      editor.selection().summary(), true, clicked_face);
  expect_flat_available_menu(object_mode);
  for (const auto &transfer : transfers) {
    EXPECT_EQ(find_top_level(same_face, transfer.first), nullptr);
    EXPECT_EQ(find_top_level(object_mode, transfer.first), nullptr);
  }
}
