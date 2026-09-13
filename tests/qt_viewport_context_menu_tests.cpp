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
#include <array>
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
    std::optional<quader::ProjectAssetId> material_under_mouse = std::nullopt,
    std::function<void(quader::ProjectAssetId)> open_material_editor = {},
    QString trigger_action_name = {}) {
  ContextMenuSnapshot snapshot;
  QTimer::singleShot(0, &owner, [&snapshot, trigger_action_name]() {
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

  presenter.show(owner, owner.mapToGlobal(owner.rect().center()), editor,
                 input_router, localization, action_set, selection, target_hit,
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

quader::QuaderActionItem
disabled_action(std::string action_id, std::string label, std::string reason) {
  return quader::QuaderActionItem{
      .action_id = std::move(action_id),
      .label = std::move(label),
      .visibility = quader::QuaderActionVisibility::Disabled,
      .disabled_reason = std::move(reason),
  };
}

const ContextActionSnapshot *find_child(const ContextActionSnapshot &submenu,
                                        const QString &object_name) {
  const auto found =
      std::find_if(submenu.children.begin(), submenu.children.end(),
                   [&object_name](const ContextActionSnapshot &action) {
                     return action.object_name == object_name;
                   });
  return found == submenu.children.end() ? nullptr : &*found;
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

const ContextActionSnapshot *find_submenu(const ContextMenuSnapshot &menu,
                                          const QString &submenu_object_name) {
  const auto found =
      std::find_if(menu.actions.begin(), menu.actions.end(),
                   [&submenu_object_name](const ContextActionSnapshot &action) {
                     return action.submenu_object_name == submenu_object_name;
                   });
  return found == menu.actions.end() ? nullptr : &*found;
}

} // namespace

TEST(QtViewportContextMenuTests,
     BuildsDeliberateEmptyObjectGroupAndComponentMenus) {
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
  const auto selection = [](quader::QuaderSelectionMode mode) {
    quader::QuaderSelectionSummary summary;
    summary.mode = mode;
    summary.has_selection = true;
    summary.selected_object_count = 2;
    summary.selected_vertex_count = 4;
    summary.selected_edge_count = 4;
    summary.selected_face_count = 2;
    return summary;
  };

  const ContextMenuSnapshot empty_menu =
      show_and_snapshot(owner, presenter, editor, input_router, localization,
                        {}, quader::QuaderSelectionSummary{}, false);
  ASSERT_TRUE(empty_menu.opened);
  EXPECT_EQ(empty_menu.separator_count, 0u);
  EXPECT_NE(find_top_level(empty_menu, QStringLiteral("select.all")), nullptr);
  EXPECT_NE(find_top_level(empty_menu, QStringLiteral("viewport.focus_all")),
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
       enabled_action("delete_selection", "Delete Selection")});

  const std::optional<quader::ProjectAssetId> hovered_material =
      quader::ProjectAssetId::parse("11111111-1111-4111-8111-111111111111");
  ASSERT_TRUE(hovered_material.has_value());
  std::optional<quader::ProjectAssetId> opened_material;

  presenter.apply_ui_scale(quader::QtUiScaleMetrics{2.0F, 1U});
  const ContextMenuSnapshot object_menu = show_and_snapshot(
      owner, presenter, editor, input_router, localization, object_actions,
      selection(quader::QuaderSelectionMode::Object), true, hovered_material,
      [&opened_material](quader::ProjectAssetId id) { opened_material = id; },
      QStringLiteral("viewport.material.edit_under_mouse"));
  ASSERT_TRUE(object_menu.opened);
  EXPECT_EQ(opened_material, hovered_material);
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

  const ContextActionSnapshot *mesh_data = find_submenu(
      object_menu, QStringLiteral("viewport_context_mesh_data_submenu"));
  const ContextActionSnapshot *normals = find_submenu(
      object_menu, QStringLiteral("viewport_context_normals_submenu"));
  const ContextActionSnapshot *arrange = find_submenu(
      object_menu, QStringLiteral("viewport_context_arrange_submenu"));
  ASSERT_NE(mesh_data, nullptr);
  ASSERT_NE(normals, nullptr);
  ASSERT_NE(arrange, nullptr);
  const ContextActionSnapshot *mesh_duplicate =
      find_child(*mesh_data, QStringLiteral("duplicate_meshes"));
  ASSERT_NE(mesh_duplicate, nullptr);
  EXPECT_EQ(mesh_duplicate->text, QStringLiteral("Duplicate Mesh Data"));
  EXPECT_NE(find_child(*normals, QStringLiteral("invert_mesh")), nullptr);
  EXPECT_NE(find_child(*normals, QStringLiteral("shade_smooth_mesh")), nullptr);
  EXPECT_NE(find_child(*arrange, QStringLiteral("copy_pivot_from_active")),
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
  const auto edit_material_index = top_level_index(
      object_menu, QStringLiteral("viewport.material.edit_under_mouse"));
  ASSERT_TRUE(edit_material_index.has_value());
  ASSERT_GT(*edit_material_index, 0u);
  EXPECT_TRUE(object_menu.actions[*edit_material_index - 1u].separator);
  EXPECT_NE(find_top_level(object_menu, QStringLiteral("edit.cut")), nullptr);
  EXPECT_NE(find_top_level(object_menu, QStringLiteral("edit.copy")), nullptr);
  ASSERT_FALSE(object_menu.actions.empty());
  EXPECT_EQ(object_menu.actions.back().object_name,
            QStringLiteral("delete_selection"));
  EXPECT_EQ(object_menu.actions.back().text,
            QStringLiteral("Delete Objects\tDelete"));

  const quader::QuaderActionSet group_actions = action_set(
      "group", {enabled_action("delete_selection", "Delete Selection")});
  const ContextMenuSnapshot group_menu = show_and_snapshot(
      owner, presenter, editor, input_router, localization, group_actions,
      selection(quader::QuaderSelectionMode::Group), true);
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
       enabled_action("delete_selection", "Delete Selection")});
  const ContextMenuSnapshot vertex_menu = show_and_snapshot(
      owner, presenter, editor, input_router, localization, vertex_actions,
      selection(quader::QuaderSelectionMode::Vertex), true);
  ASSERT_TRUE(vertex_menu.opened);
  ASSERT_GE(vertex_menu.actions.size(), 5u);
  EXPECT_EQ(vertex_menu.actions[0].object_name,
            QStringLiteral("merge_vertices_at_center"));
  EXPECT_EQ(vertex_menu.actions[1].object_name,
            QStringLiteral("bevel_vertices"));
  EXPECT_EQ(vertex_menu.actions[2].object_name,
            QStringLiteral("connect_vertices"));
  const ContextActionSnapshot *vertex_topology = find_submenu(
      vertex_menu, QStringLiteral("viewport_context_topology_submenu"));
  const ContextActionSnapshot *vertex_merge = find_submenu(
      vertex_menu, QStringLiteral("viewport_context_merge_submenu"));
  const ContextActionSnapshot *vertex_arrange = find_submenu(
      vertex_menu, QStringLiteral("viewport_context_arrange_submenu"));
  ASSERT_NE(vertex_topology, nullptr);
  ASSERT_NE(vertex_merge, nullptr);
  ASSERT_NE(vertex_arrange, nullptr);
  EXPECT_NE(find_child(*vertex_merge, QStringLiteral("merge_vertices")),
            nullptr);
  EXPECT_NE(find_child(*vertex_merge, QStringLiteral("merge_by_distance")),
            nullptr);
  EXPECT_NE(find_child(*vertex_topology, QStringLiteral("dissolve_vertices")),
            nullptr);
  EXPECT_NE(
      find_child(*vertex_arrange, QStringLiteral("snap_vertices_to_active")),
      nullptr);
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
       enabled_action("delete_selection", "Delete Selection")});
  const ContextMenuSnapshot edge_menu = show_and_snapshot(
      owner, presenter, editor, input_router, localization, edge_actions,
      selection(quader::QuaderSelectionMode::Edge), true);
  ASSERT_TRUE(edge_menu.opened);
  ASSERT_GE(edge_menu.actions.size(), 6u);
  EXPECT_EQ(edge_menu.actions[0].object_name, QStringLiteral("bevel_edges"));
  EXPECT_EQ(edge_menu.actions[1].object_name, QStringLiteral("bridge_edges"));
  EXPECT_EQ(edge_menu.actions[2].object_name, QStringLiteral("connect_edges"));
  EXPECT_EQ(edge_menu.actions[3].object_name, QStringLiteral("fill_hole"));
  EXPECT_FALSE(edge_menu.actions[3].enabled);
  EXPECT_EQ(edge_menu.actions[3].tool_tip,
            QStringLiteral("Select a closed boundary."));
  const ContextActionSnapshot *edge_topology = find_submenu(
      edge_menu, QStringLiteral("viewport_context_topology_submenu"));
  const ContextActionSnapshot *edge_merge =
      find_submenu(edge_menu, QStringLiteral("viewport_context_merge_submenu"));
  const ContextActionSnapshot *edge_normals = find_submenu(
      edge_menu, QStringLiteral("viewport_context_normals_submenu"));
  ASSERT_NE(edge_topology, nullptr);
  ASSERT_NE(edge_merge, nullptr);
  ASSERT_NE(edge_normals, nullptr);
  ASSERT_EQ(edge_normals->children.size(), 3u);
  EXPECT_EQ(edge_normals->children[0].object_name,
            QStringLiteral("default_edge_normals"));
  EXPECT_EQ(edge_normals->children[1].object_name,
            QStringLiteral("harden_edge_normals"));
  EXPECT_EQ(edge_normals->children[2].object_name,
            QStringLiteral("soften_edge_normals"));
  EXPECT_NE(find_child(*edge_topology, QStringLiteral("split_edges")), nullptr);
  EXPECT_NE(find_child(*edge_merge, QStringLiteral("merge_edges")), nullptr);
  EXPECT_NE(find_child(*edge_normals, QStringLiteral("harden_edge_normals")),
            nullptr);
  EXPECT_NE(find_child(*edge_normals, QStringLiteral("default_edge_normals")),
            nullptr);
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
       enabled_action("delete_selection", "Delete Selection")});

  ASSERT_TRUE(
      editor.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(context_mesh.value(), 1).ok());

  const ContextMenuSnapshot face_menu = show_and_snapshot(
      owner, presenter, editor, input_router, localization, face_actions,
      selection(quader::QuaderSelectionMode::Face), true);
  ASSERT_TRUE(face_menu.opened);
  ASSERT_GE(face_menu.actions.size(), 4u);
  EXPECT_EQ(face_menu.actions[0].object_name,
            QStringLiteral("thicken_faces"));
  EXPECT_EQ(face_menu.actions[1].object_name, QStringLiteral("bridge_faces"));
  EXPECT_EQ(face_menu.actions[2].object_name, QStringLiteral("combine_faces"));
  EXPECT_EQ(face_menu.actions[3].object_name, QStringLiteral("extract_faces"));
  const ContextActionSnapshot *face_topology = find_submenu(
      face_menu, QStringLiteral("viewport_context_topology_submenu"));
  const ContextActionSnapshot *face_normals = find_submenu(
      face_menu, QStringLiteral("viewport_context_normals_submenu"));
  ASSERT_NE(face_topology, nullptr);
  ASSERT_NE(face_normals, nullptr);
  EXPECT_NE(find_child(*face_topology, QStringLiteral("detach_faces")),
            nullptr);
  EXPECT_NE(find_child(*face_normals, QStringLiteral("invert_faces")), nullptr);
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
  const ContextMenuSnapshot light_only_menu = show_and_snapshot(
      owner, presenter, editor, input_router, localization, object_actions,
      editor.selection().summary(), true);
  EXPECT_EQ(find_top_level(light_only_menu,
                           QStringLiteral("edit.apply_material")),
            nullptr);

  const std::array<const ContextMenuSnapshot *, 6> menus{
      &empty_menu,  &object_menu, &group_menu,
      &vertex_menu, &edge_menu,   &face_menu};
  const std::function<void(const std::vector<ContextActionSnapshot> &)>
      expect_display_only_shortcuts =
          [&](const std::vector<ContextActionSnapshot> &actions) {
            for (const ContextActionSnapshot &action : actions) {
              EXPECT_TRUE(action.shortcut_empty)
                  << action.object_name.toStdString()
                  << " must display shortcut text without installing a "
                     "second QAction shortcut route.";
              expect_display_only_shortcuts(action.children);
            }
          };
  for (const ContextMenuSnapshot *snapshot : menus) {
    ASSERT_NE(snapshot, nullptr);
    expect_display_only_shortcuts(snapshot->actions);
    EXPECT_EQ(find_submenu(*snapshot,
                           QStringLiteral("viewport_context_modeling_submenu")),
              nullptr);
    EXPECT_EQ(find_submenu(*snapshot,
                           QStringLiteral("viewport_context_object_submenu")),
              nullptr);
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
  bool frame_action_activated = false;

  QTimer::singleShot(0, &owner, [&]() {
    auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
    if (menu == nullptr) {
      ADD_FAILURE() << "Expected the viewport context menu to open.";
      if (QWidget *popup = QApplication::activePopupWidget()) {
        popup->close();
      }
      return;
    }
    QAction *frame_action = nullptr;
    for (QAction *action : menu->actions()) {
      if (action != nullptr &&
          action->objectName() == QStringLiteral("viewport.frame_selection")) {
        frame_action = action;
        break;
      }
    }
    if (frame_action == nullptr || !frame_action->isEnabled()) {
      ADD_FAILURE() << "Expected enabled Frame Selection context action.";
      menu->close();
      return;
    }
    QObject::connect(frame_action, &QAction::triggered, &owner,
                     [&frame_action_activated]() {
                       frame_action_activated = true;
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

    menu->setActiveAction(frame_action);
    QKeyEvent accept_action(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(menu, &accept_action);
    if (menu->isVisible()) {
      menu->close();
    }
  });

  presenter.show(owner, owner.mapToGlobal(owner.rect().center()), editor,
                 input_router, localization, {}, presented_selection, true,
                 std::nullopt, {});
  EXPECT_TRUE(frame_action_activated);
  EXPECT_EQ(editor.commands().last_status_revision(), command_status_revision)
      << "A context-menu action must not execute after the selection used to "
         "build the menu changes inside QMenu's nested event loop.";
}

TEST(QtViewportContextMenuTests,
     EdgeNormalActionsShareSelectionAvailabilityAndDisabledTooltip) {
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
  const auto expect_normal_actions = [](const ContextMenuSnapshot &menu,
                                        bool enabled,
                                        const QString &tool_tip) {
    const ContextActionSnapshot *normals = find_submenu(
        menu, QStringLiteral("viewport_context_normals_submenu"));
    ASSERT_NE(normals, nullptr);
    for (const QString &action_id :
         {QStringLiteral("default_edge_normals"),
          QStringLiteral("harden_edge_normals"),
          QStringLiteral("soften_edge_normals")}) {
      const ContextActionSnapshot *action = find_child(*normals, action_id);
      ASSERT_NE(action, nullptr) << action_id.toStdString();
      EXPECT_EQ(action->enabled, enabled) << action_id.toStdString();
      if (enabled) {
        EXPECT_FALSE(action->tool_tip.isEmpty()) << action_id.toStdString();
      } else {
        EXPECT_EQ(action->tool_tip, tool_tip) << action_id.toStdString();
      }
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
  ASSERT_TRUE(empty_edge_menu.opened);
  expect_normal_actions(empty_edge_menu, false,
                        QStringLiteral("Select at least 1 edge."));

  ASSERT_TRUE(
      editor.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
  quader::QuaderSelectionSummary wrong_mode_summary =
      editor.selection().summary();
  wrong_mode_summary.has_selection = true;
  wrong_mode_summary.selected_object_count = 1;
  const ContextMenuSnapshot wrong_mode_menu = show_and_snapshot(
      owner, presenter, editor, input_router, localization, context_actions(),
      wrong_mode_summary, true);
  ASSERT_TRUE(wrong_mode_menu.opened);
  const ContextActionSnapshot *wrong_mode_normals = find_submenu(
      wrong_mode_menu, QStringLiteral("viewport_context_normals_submenu"));
  ASSERT_NE(wrong_mode_normals, nullptr);
  EXPECT_EQ(find_child(*wrong_mode_normals,
                       QStringLiteral("default_edge_normals")),
            nullptr);
  EXPECT_EQ(find_child(*wrong_mode_normals,
                       QStringLiteral("harden_edge_normals")),
            nullptr);
  EXPECT_EQ(find_child(*wrong_mode_normals,
                       QStringLiteral("soften_edge_normals")),
            nullptr);

  ASSERT_TRUE(
      editor.selection().set_mode(quader::QuaderSelectionMode::Edge).ok());
  ASSERT_TRUE(editor.selection().select_edge(box.value(), 1, 2).ok());
  const ContextMenuSnapshot selected_edge_menu = show_and_snapshot(
      owner, presenter, editor, input_router, localization, context_actions(),
      editor.selection().summary(), true);
  ASSERT_TRUE(selected_edge_menu.opened);
  expect_normal_actions(selected_edge_menu, true, QString{});
}

TEST(QtViewportContextMenuTests, MaterialEditingDispatchesWithoutImplicitAssignment) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-viewport-material-error-tests";
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

  const std::optional<quader::ProjectAssetId> hovered_material =
      quader::ProjectAssetId::parse("11111111-1111-4111-8111-111111111111");
  ASSERT_TRUE(hovered_material.has_value());
  const std::uint64_t command_status_revision =
      editor.commands().last_status_revision();
  std::size_t edit_attempts = 0;
  for (std::size_t attempt = 1; attempt <= 2; ++attempt) {
    const ContextMenuSnapshot menu = show_and_snapshot(
        owner, presenter, editor, input_router, localization, {}, {}, true,
        hovered_material,
        [&](quader::ProjectAssetId id) {
          EXPECT_EQ(id, *hovered_material);
          ++edit_attempts;
        },
        QStringLiteral("viewport.material.edit_under_mouse"));
    ASSERT_TRUE(menu.opened);
    EXPECT_EQ(edit_attempts, attempt);
    EXPECT_TRUE(notifications.empty());
    EXPECT_EQ(find_top_level(menu,
                             QStringLiteral("viewport.material.sample_face")),
              nullptr);
    EXPECT_EQ(find_top_level(menu, QStringLiteral("edit.apply_material")),
              nullptr);
    EXPECT_EQ(editor.commands().last_status_revision(), command_status_revision);
  }
}
