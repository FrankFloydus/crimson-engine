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
#include "ui/qt_ui_scale_metrics.hpp"
#include "ui/qt_viewport_context_menu.hpp"
#include "ui/viewport_context_menu_policy.hpp"
#include "viewport/viewport_input_router.hpp"

#include <quader/modeling/neutral/polygon/document.hpp>

#include <QAction>
#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
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
#include <functional>
#include <initializer_list>
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
  bool inherits_application_style = false;
  std::vector<ContextActionSnapshot> actions;
  QString style_sheet;
  std::size_t separator_count = 0u;
  std::size_t on_screen_separator_count = 0u;
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
    QString trigger_action_name = {}, QString capture_filename = {},
    std::function<void()> before_trigger = {}) {
  ContextMenuSnapshot snapshot;
  QTimer snapshot_timer;
  snapshot_timer.setSingleShot(true);
  QObject::connect(&snapshot_timer, &QTimer::timeout, &owner,
                   [&snapshot, trigger_action_name, capture_filename, before_trigger]() {
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
    snapshot.inherits_application_style = menu->style() == QApplication::style();
    snapshot.style_sheet = menu->styleSheet();
    QImage rendered(menu->size(), QImage::Format_ARGB32_Premultiplied);
    rendered.fill(Qt::transparent);
    menu->render(&rendered);
    const QString capture_directory =
        qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
    if (!capture_directory.isEmpty() && !capture_filename.isEmpty()) {
      EXPECT_TRUE(QDir().mkpath(capture_directory));
      EXPECT_TRUE(rendered.save(
          QDir(capture_directory).filePath(capture_filename)));
    }
    const QColor separator_color =
        menu->palette().color(QPalette::Active, QPalette::Mid);
    for (QAction *action : menu->actions()) {
      if (action != nullptr) {
        snapshot.actions.push_back(snapshot_action(*action));
        if (action->isSeparator()) {
          ++snapshot.separator_count;
          const QRect region =
              menu->actionGeometry(action).intersected(rendered.rect());
          if (!region.isEmpty()) {
            ++snapshot.on_screen_separator_count;
          }
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
        if (before_trigger) {
          before_trigger();
        }
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

const ContextActionSnapshot *find_top_level(const ContextMenuSnapshot &menu,
                                            const QString &object_name) {
  const auto found =
      std::find_if(menu.actions.begin(), menu.actions.end(),
                   [&object_name](const ContextActionSnapshot &action) {
                     return action.object_name == object_name;
                   });
  return found == menu.actions.end() ? nullptr : &*found;
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

void expect_only_pointer_actions(const ContextMenuSnapshot &menu) {
  for (const auto &action : menu.actions) {
    if (!action.separator) {
      EXPECT_TRUE(
          action.object_name == QStringLiteral("curve.solidify.apply") ||
          action.object_name ==
              QStringLiteral("viewport.material.apply_selected_face_to_face") ||
          action.object_name ==
              QStringLiteral("viewport.material.apply_face_to_selection") ||
          action.object_name ==
              QStringLiteral("viewport.material.transfer_face_texture"))
          << action.object_name.toStdString();
    }
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
     MeshOmissionsPreserveGroupAndComponentCommandPolicy) {
  using Mode = quader::QuaderSelectionMode;
  namespace command = quader::quader_command;
  quader::ViewportContextMenuScope scope{
      .mode = Mode::Object,
      .has_selection = true,
      .polygon_targets = true,
      .groupable_targets = true,
      .has_group_target = true,
      .clipboard_targets = true,
  };
  for (const auto id : {
           command::kModelingHalfCut, command::kModelingDuplicateMeshes,
           command::kCutSelection, command::kCopySelection,
           command::kDeleteSelection, command::kGroupSelection,
           command::kModelingRemoveDoubles,
           command::kModelingRecalculateFaceNormals}) {
    EXPECT_FALSE(quader::viewport_context_menu_allows_command(scope, id.string()))
        << id.string();
  }
  for (const auto id : {
           command::kEditCreateInstance, command::kModelingInvertMesh,
           command::kModelingSnapMeshesToFloor, command::kModelingSetOriginToCenter,
           command::kModelingSetOriginToPivot, command::kModelingMirrorSelectionX,
           command::kModelingMirrorSelectionY, command::kModelingMirrorSelectionZ,
           command::kModelingFlipHorizontal, command::kModelingFlipVertical,
           command::kModelingBakeLocation, command::kModelingBakeRotation,
           command::kModelingBakeScale, command::kModelingBakeTransforms,
           command::kDuplicateSelection, command::kPasteSelection}) {
    EXPECT_TRUE(quader::viewport_context_menu_allows_command(scope, id.string()))
        << id.string();
  }
  scope.mode = Mode::Group;
  EXPECT_TRUE(quader::viewport_context_menu_allows_command(
      scope, command::kGroupSelection.string()));
  EXPECT_TRUE(quader::viewport_context_menu_allows_command(
      scope, command::kDeleteSelection.string()));
  for (const Mode mode : {Mode::Vertex, Mode::Edge, Mode::Face}) {
    scope.mode = mode;
    EXPECT_TRUE(quader::viewport_context_menu_allows_command(
        scope, command::kDeleteSelection.string()));
    EXPECT_EQ(quader::viewport_context_menu_allows_command(
                  scope, command::kModelingRecalculateFaceNormals.string()),
              mode == Mode::Face);
  }
}

TEST(QtViewportContextMenuTests,
     GenericOperationsStayInQForObjectComponentInstanceAndEmptySelections) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-viewport-context-q-ownership-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const auto temporary_root = std::filesystem::path(temporary.path().toStdWString());
  const auto project_root = temporary_root / "Q Context Project";
  ASSERT_TRUE(std::filesystem::create_directory(project_root));
  quader::ProjectSession project(temporary_root / "recent-scenes.json");
  const auto created = project.create(project_root);
  ASSERT_TRUE(created.ok) << created.message;
  quader::QuaderEditorApi editor(project);
  quader::ViewportInputRouter input_router;
  auto localization = loaded_localization();
  quader::QtViewportContextMenuPresenter presenter;
  QWidget owner;
  owner.setAttribute(Qt::WA_DontShowOnScreen, true);
  owner.resize(320, 240);
  owner.show();
  QCoreApplication::processEvents();

  const auto mesh = editor.modeling().create_box(
      {.min = {-1.0F, -1.0F, -1.0F}, .max = {1.0F, 1.0F, 1.0F}},
      "Q Operations Mesh");
  ASSERT_TRUE(mesh.ok()) << mesh.error_message();
  ASSERT_TRUE(editor.selection().select_object(mesh.value()).ok());
  const auto verify_ownership = [&](std::initializer_list<std::string_view> ids,
                                    bool target_hit = true) {
    const auto selection = editor.selection().summary();
    SCOPED_TRACE(static_cast<int>(selection.mode));
    SCOPED_TRACE(selection.active_object_id);
    const auto q_commands =
        quader::qt_viewport_context_commands(editor, selection, localization);
    for (const auto id : ids) {
      EXPECT_TRUE(std::ranges::any_of(q_commands, [id](const auto &command) {
        return command.command_id == id;
      })) << id;
    }
    const auto actions = editor.modeling().available_action_set({
        .surface = quader::QuaderActionSurface::ViewportContextMenu,
        .include_disabled = true,
    });
    const auto menu = show_and_snapshot(owner, presenter, editor, input_router,
                                       localization, actions, selection, target_hit);
    EXPECT_FALSE(menu.opened);
    EXPECT_TRUE(menu.actions.empty());
  };
  verify_ownership({"edit.duplicate", "edit.create_instance",
                    "modeling.bake_rotation", "modeling.set_origin_to_center",
                    "modeling.set_origin_to_pivot"});
  const auto mesh_commands = quader::qt_viewport_context_commands(
      editor, editor.selection().summary(), localization);
  for (const std::string_view id : {
           "modeling.half_cut", "modeling.duplicate_meshes", "edit.cut",
           "edit.copy", "edit.delete", "edit.group", "modeling.remove_doubles",
           "modeling.recalculate_face_normals"}) {
    EXPECT_FALSE(std::ranges::any_of(mesh_commands, [id](const auto &entry) {
      return entry.command_id == id;
    })) << id;
    EXPECT_TRUE(editor.commands().descriptor(id).has_value()) << id;
  }

  const auto expect_no_origin_commands = [&]() {
    const auto commands = quader::qt_viewport_context_commands(
        editor, editor.selection().summary(), localization);
    for (const auto id : {
             quader::quader_command::kModelingSetOriginToCenter,
             quader::quader_command::kModelingSetOriginToPivot}) {
      EXPECT_FALSE(std::ranges::any_of(commands, [id](const auto &command) {
        return command.command_id == id.string();
      })) << id.string();
    }
  };

  struct ComponentCase final {
    quader::QuaderSelectionMode mode;
    std::string_view operation;
  };
  const std::array cases{
      ComponentCase{quader::QuaderSelectionMode::Vertex, "modeling.bevel_vertices"},
      ComponentCase{quader::QuaderSelectionMode::Edge, "modeling.bevel_edges"},
      ComponentCase{quader::QuaderSelectionMode::Face, "modeling.extract_faces"},
  };
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
  for (const auto &component : cases) {
    SCOPED_TRACE(component.operation);
    ASSERT_TRUE(editor.selection().set_mode(component.mode).ok());
    if (component.mode == quader::QuaderSelectionMode::Vertex) {
      ASSERT_TRUE(editor.selection().select_vertices(mesh.value(), {1, 2}).ok());
    } else if (component.mode == quader::QuaderSelectionMode::Edge) {
      ASSERT_TRUE(editor.selection().select_edge(mesh.value(), 1, 2).ok());
    } else {
      ASSERT_TRUE(editor.selection().select_face(mesh.value(), 1).ok());
    }
    verify_ownership({"edit.delete", component.operation});
    expect_no_origin_commands();
    verify_ownership({}, false);
  }

  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Object).ok());
  ASSERT_TRUE(editor.selection().select_object(mesh.value()).ok());
  ASSERT_TRUE(editor.modeling().create_instance().ok());
  verify_ownership({"edit.flatten_instance", "edit.duplicate"});
  ASSERT_TRUE(editor.commands().execute("edit.copy").ok());
  ASSERT_TRUE(editor.selection().clear().ok());
  verify_ownership({"edit.paste"}, false);
  expect_no_origin_commands();

  const auto second = editor.modeling().create_box(
      {.min = {2.0F, -1.0F, -1.0F}, .max = {4.0F, 1.0F, 1.0F}},
      "Q Group Mesh");
  ASSERT_TRUE(second.ok()) << second.error_message();
  const auto third = editor.modeling().create_box(
      {.min = {5.0F, -1.0F, -1.0F}, .max = {7.0F, 1.0F, 1.0F}},
      "Q Group Second Mesh");
  ASSERT_TRUE(third.ok()) << third.error_message();
  ASSERT_TRUE(editor.selection().select_objects({second.value(), third.value()}).ok());
  const auto group = editor.scene().group_selection("Q Group");
  ASSERT_TRUE(group.ok()) << group.error_message();
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Group).ok());
  ASSERT_TRUE(editor.selection().select_objects({group.value().id}).ok());
  verify_ownership({"edit.ungroup", "edit.duplicate", "edit.delete"});
  expect_no_origin_commands();
}

TEST(QtViewportContextMenuTests, SelectedCurveKeepsSolidifyWhileGenericOperationsStayInQ) {
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

  const auto actions = editor.modeling().available_action_set({
      .surface = quader::QuaderActionSurface::ViewportContextMenu,
      .include_disabled = true,
  });
  const auto q_commands = quader::qt_viewport_context_commands(
      editor, editor.selection().summary(), localization);
  for (const std::string_view id : {"edit.duplicate"}) {
    EXPECT_TRUE(std::ranges::any_of(q_commands, [id](const auto &command) {
      return command.command_id == id;
    })) << id;
  }
  const auto menu = show_and_snapshot(
      owner, presenter, editor, input_router, localization, actions,
      editor.selection().summary(), true);
  EXPECT_FALSE(menu.opened);
  EXPECT_TRUE(menu.actions.empty());

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
  expect_only_pointer_actions(solidify_menu);
  ASSERT_EQ(solidify_menu.actions.size(), 1u);
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
  EXPECT_FALSE(control_menu.opened);
  EXPECT_TRUE(control_menu.actions.empty());
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
  ASSERT_TRUE(editor.selection().select_objects({source.value(), target.value()}).ok());
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
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
  const QFont baseline_font = application.font();
  const QPalette baseline_palette = application.palette();
  struct PresentationCase final {
    float scale;
    Qt::LayoutDirection direction;
    bool high_contrast;
    const char *capture;
  };
  const std::array presentations{
      PresentationCase{1.0F, Qt::LeftToRight, false, "context-pointer-100.png"},
      PresentationCase{2.0F, Qt::LeftToRight, false, "context-pointer-200.png"},
      PresentationCase{1.0F, Qt::LeftToRight, false, "context-pointer-down-100.png"},
      PresentationCase{1.0F, Qt::RightToLeft, false, "context-pointer-rtl.png"},
      PresentationCase{1.0F, Qt::LeftToRight, true, "context-pointer-high-contrast.png"},
  };
  for (const auto &presentation : presentations) {
    SCOPED_TRACE(presentation.capture);
    const quader::QtUiScaleMetrics metrics(presentation.scale);
    QApplication::setStyle(new quader::QtUiScaleProxyStyle(metrics));
    application.setFont(metrics.font(baseline_font));
    application.setLayoutDirection(presentation.direction);
    quader::apply_qt_application_theme_for_contrast_preference(
        baseline_palette, presentation.high_contrast ? Qt::ContrastPreference::HighContrast
                                                    : Qt::ContrastPreference::NoPreference);
    const auto menu = show_and_snapshot(
        owner, presenter, editor, input_router, localization, {},
        editor.selection().summary(), true, clicked_face, {},
        QString::fromLatin1(presentation.capture));
    expect_flat_available_menu(menu);
    expect_only_pointer_actions(menu);
    EXPECT_TRUE(menu.inherits_application_style);
    EXPECT_TRUE(menu.style_sheet.isEmpty());
    EXPECT_EQ(menu.actions.size(), 3u);
  }
  application.setFont(baseline_font);
  application.setLayoutDirection(Qt::LeftToRight);
  quader::apply_qt_application_theme_for_contrast_preference(
      baseline_palette, Qt::ContrastPreference::NoPreference);

  for (const auto &transfer : transfers) {
    const auto &action_id = transfer.first;
    SCOPED_TRACE(action_id.toStdString());
    const auto selection_before_transfer = editor.selection().summary();
    const ContextMenuSnapshot menu = show_and_snapshot(
        owner, presenter, editor, input_router, localization, {},
        editor.selection().summary(), true, clicked_face, action_id);
    expect_flat_available_menu(menu);
    expect_only_pointer_actions(menu);
    EXPECT_EQ(menu.actions.size(), 3u);
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
    EXPECT_EQ(selection_after.selection_revision,
              selection_before_transfer.selection_revision);
    ASSERT_TRUE(editor.history().undo().ok());
    EXPECT_EQ(session.history().undo_count(), history_count);
    EXPECT_EQ(read_material(source.value()), quader::MaterialReference{amber.value().id});
    EXPECT_EQ(read_material(target.value()), quader::MaterialReference{blue.value().id});
    EXPECT_EQ(face_uv_coordinates(read_face(target.value())),
              face_uv_coordinates(target_before));
    EXPECT_EQ(read_face(target.value()).texture_transform,
              target_before.texture_transform);
  }

  std::size_t history_after_selection = 0u;
  const auto stale_selection = show_and_snapshot(
      owner, presenter, editor, input_router, localization, {},
      editor.selection().summary(), true, clicked_face,
      QStringLiteral("viewport.material.apply_selected_face_to_face"), {}, [&] {
        EXPECT_TRUE(editor.selection().select_face(source.value(), 2).ok());
        history_after_selection = session.history().undo_count();
      });
  expect_flat_available_menu(stale_selection);
  EXPECT_EQ(session.history().undo_count(), history_after_selection);
  EXPECT_EQ(read_material(source.value()), quader::MaterialReference{amber.value().id});
  EXPECT_EQ(read_material(target.value()), quader::MaterialReference{blue.value().id});
  EXPECT_EQ(face_uv_coordinates(read_face(target.value())),
            face_uv_coordinates(target_before));
  ASSERT_TRUE(editor.selection().select_face(source.value(), 1).ok());

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
  ASSERT_TRUE(editor.scene().set_selectable(target.value().entity, false).ok());
  const std::size_t locked_history_count = session.history().undo_count();
  const ContextMenuSnapshot locked_source = show_and_snapshot(
      owner, presenter, editor, input_router, localization, {},
      editor.selection().summary(), true, clicked_face);
  EXPECT_FALSE(locked_source.opened);
  EXPECT_TRUE(locked_source.actions.empty());
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
  ASSERT_TRUE(editor.scene().set_selectable(target.value().entity, true).ok());

  ASSERT_TRUE(editor.selection().select_face(target.value(), 1).ok());
  const ContextMenuSnapshot same_face = show_and_snapshot(
      owner, presenter, editor, input_router, localization, {},
      editor.selection().summary(), true, clicked_face);
  EXPECT_FALSE(same_face.opened);
  EXPECT_TRUE(same_face.actions.empty());
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Object).ok());
  ASSERT_TRUE(editor.selection().select_object(source.value()).ok());
  const ContextMenuSnapshot object_mode = show_and_snapshot(
      owner, presenter, editor, input_router, localization, {},
      editor.selection().summary(), true, clicked_face);
  EXPECT_FALSE(object_mode.opened);
  EXPECT_TRUE(object_mode.actions.empty());
  for (const auto &transfer : transfers) {
    EXPECT_EQ(find_top_level(same_face, transfer.first), nullptr);
    EXPECT_EQ(find_top_level(object_mode, transfer.first), nullptr);
  }
}
