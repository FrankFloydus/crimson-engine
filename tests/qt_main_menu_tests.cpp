////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"
#include "core/localization_service.hpp"
#include "editor/session/project_session.hpp"
#include "editor/session/quader_editor_api_session_access.hpp"
#include "ui/qt_application_theme.hpp"
#include "ui/qt_main_menu.hpp"
#include "ui/qt_ui_scale_metrics.hpp"

#include <gtest/gtest.h>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFontMetrics>
#include <QImage>
#include <QMenu>
#include <QMenuBar>
#include <QMetaObject>
#include <QPointer>
#include <QStyle>
#include <QTemporaryDir>

#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#ifndef QUADER_SOURCE_DIR
#define QUADER_SOURCE_DIR "."
#endif

namespace quader {
namespace {

class QtFileMenuTests : public testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(QCoreApplication::instance(), nullptr);
    application_ = std::make_unique<QApplication>(argc_, argv_);
    application_->setQuitOnLastWindowClosed(false);
    ASSERT_TRUE(temporary_.isValid());
    root_ = std::filesystem::path(temporary_.path().toStdWString());
    project_ = std::make_unique<ProjectSession>(root_ / "recent-scenes.json");
    const std::filesystem::path project_root = root_ / "Project & One";
    ASSERT_TRUE(std::filesystem::create_directory(project_root));
    const ProjectSessionResult created = project_->create(project_root);
    ASSERT_TRUE(created.ok) << created.message;
    editor_ = std::make_unique<QuaderEditorApi>(*project_);
    ASSERT_TRUE(localization_.load(
        std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "locales",
        "en-US"));
    menu_bar_ = std::make_unique<QMenuBar>();
    menu_ = std::make_unique<QtMainMenu>();
    menu_->initialize(*menu_bar_, QtMainMenuContext{
        .editor = *editor_,
        .modeling = project_->active_scene()->session->modeling(),
        .viewport_settings = viewport_settings_,
        .localization = localization_,
        .show_autosave_recovery = [this] { ++recovery_requests_; },
        .autosave_recovery_available = [this] { return recovery_available_; },
    });
    QuaderEditorApiSessionAccess::set_scene_binding_changed_callback(
        *editor_, [this](SceneSession &replacement) {
          ++runtime_rebinds_;
          if (menu_) {
            menu_->rebind_runtime(*editor_, replacement.modeling());
          }
        });
  }

  void save_active_scene(const std::string &name) {
    auto path = ProjectRelativePath::parse_portable("Scenes/" + name + ".qdr");
    ASSERT_TRUE(path.ok()) << path.error().message;
    const QuaderApiVoidResult saved = editor_->scene_documents().save_as(
        project_->active_scene_id(), path.take_value());
    ASSERT_TRUE(saved.ok()) << saved.error_message();
  }

  QMenu *recent_menu() const {
    return menu_bar_->findChild<QMenu *>(QStringLiteral("menu_file_open_recent"));
  }

  void show_recent_menu() {
    ASSERT_NE(recent_menu(), nullptr);
    ASSERT_TRUE(QMetaObject::invokeMethod(recent_menu(), "aboutToShow",
                                          Qt::DirectConnection));
  }

  int argc_ = 1;
  char application_name_[23] = "quader-file-menu-tests";
  char *argv_[2] = {application_name_, nullptr};
  std::unique_ptr<QApplication> application_;
  QTemporaryDir temporary_;
  std::filesystem::path root_;
  ViewportSettings viewport_settings_ = ViewportSettings::defaults();
  LocalizationService localization_;
  std::unique_ptr<ProjectSession> project_;
  std::unique_ptr<QuaderEditorApi> editor_;
  std::unique_ptr<QMenuBar> menu_bar_;
  std::unique_ptr<QtMainMenu> menu_;
  bool recovery_available_ = false;
  int recovery_requests_ = 0;
  int runtime_rebinds_ = 0;
};

TEST_F(QtFileMenuTests, RecoveryRoutesThroughTheFileMenuAndTracksAvailability) {
  auto *recover = menu_bar_->findChild<QMenu *>(QStringLiteral("menu_file_recover"));
  auto *action = menu_bar_->findChild<QAction *>(
      QStringLiteral("action_recover_autosave"));
  ASSERT_NE(recover, nullptr);
  ASSERT_NE(action, nullptr);
  EXPECT_EQ(recover->title(), QStringLiteral("Recover"));
  EXPECT_EQ(action->text(), QStringLiteral("Auto Save..."));
  EXPECT_EQ(recover->actions(), QList<QAction *>{action});
  EXPECT_FALSE(action->isEnabled());
  EXPECT_TRUE(action->shortcut().isEmpty());
  action->trigger();
  EXPECT_EQ(recovery_requests_, 0);

  recovery_available_ = true;
  ASSERT_TRUE(QMetaObject::invokeMethod(recover, "aboutToShow",
                                        Qt::DirectConnection));
  EXPECT_TRUE(action->isEnabled());
  action->trigger();
  EXPECT_EQ(recovery_requests_, 1);

  recovery_available_ = false;
  menu_->refresh();
  EXPECT_FALSE(action->isEnabled());
  action->trigger();
  EXPECT_EQ(recovery_requests_, 1);
}

TEST_F(QtFileMenuTests, CompactMenusKeepFocusCommandsDirectlyUnderView) {
  const auto top_level = menu_bar_->actions();
  ASSERT_GE(top_level.size(), 3);
  ASSERT_NE(top_level[0]->menu(), nullptr);
  ASSERT_NE(top_level[1]->menu(), nullptr);
  ASSERT_NE(top_level[2]->menu(), nullptr);
  EXPECT_EQ(top_level[0]->menu()->objectName(), QStringLiteral("menu_file"));
  EXPECT_EQ(top_level[1]->menu()->objectName(), QStringLiteral("menu_edit"));
  EXPECT_EQ(top_level[2]->menu()->objectName(), QStringLiteral("menu_view"));

  auto *view = menu_bar_->findChild<QMenu *>(QStringLiteral("menu_view"));
  ASSERT_NE(view, nullptr);
  const auto actions = view->actions();
  ASSERT_GE(actions.size(), 6);
  EXPECT_EQ(actions[0]->property("quaderCommandId").toString(),
            QStringLiteral("viewport.toggle_local_visibility"));
  EXPECT_TRUE(actions[1]->isSeparator());
  EXPECT_EQ(actions[2]->property("quaderCommandId").toString(),
            QStringLiteral("viewport.frame_selection"));
  EXPECT_EQ(actions[3]->property("quaderCommandId").toString(),
            QStringLiteral("viewport.focus_all"));
  EXPECT_EQ(actions[4]->property("quaderCommandId").toString(),
            QStringLiteral("viewport.focus_origin"));
  EXPECT_TRUE(actions[5]->isSeparator());
  for (const char *removed : {"menu_view_viewpoint", "menu_view_camera",
                              "menu_view_shading", "menu_object_transform",
                              "menu_object_snapping"}) {
    EXPECT_EQ(menu_bar_->findChild<QMenu *>(QString::fromLatin1(removed)),
              nullptr) << removed;
  }
  EXPECT_EQ(menu_bar_->findChild<QAction *>(
                QStringLiteral("action_keyboard_shortcuts")), nullptr);
  EXPECT_EQ(menu_bar_->findChild<QAction *>(
                QStringLiteral("command_select_toggle_backface_selection")),
            nullptr);
  auto *object = menu_bar_->findChild<QMenu *>(QStringLiteral("menu_object"));
  auto *display = menu_bar_->findChild<QMenu *>(QStringLiteral("menu_view_display"));
  ASSERT_NE(object, nullptr);
  ASSERT_NE(display, nullptr);
  for (const char *name : {"command_scene_hide_selection",
                           "command_scene_unhide_hidden_objects"}) {
    auto *action = menu_bar_->findChild<QAction *>(QString::fromLatin1(name));
    ASSERT_NE(action, nullptr);
    EXPECT_TRUE(view->actions().contains(action));
    EXPECT_FALSE(object->actions().contains(action));
  }
  EXPECT_FALSE(display->actions().contains(actions[0]));
  for (const char *name : {"command_viewport_toggle_transform_space",
                           "command_viewport_spawn_window"}) {
    EXPECT_EQ(menu_bar_->findChild<QAction *>(QString::fromLatin1(name)), nullptr);
  }
  EXPECT_TRUE(editor_->commands()
                  .descriptor(quader_command::kViewportToggleTransformSpace)
                  .has_value());
  EXPECT_TRUE(editor_->commands()
                  .descriptor(quader_command::kViewportSpawnWindow)
                  .has_value());
}

TEST_F(QtFileMenuTests, SelectMenuSeparatesDistinctSelectionFamilies) {
  auto *select = menu_bar_->findChild<QMenu *>(QStringLiteral("menu_select"));
  ASSERT_NE(select, nullptr);
  const auto actions = select->actions();
  for (const char *name : {"command_edit_clear_selection",
                           "command_select_inverse", "command_select_edge_ring"}) {
    auto *action = select->findChild<QAction *>(QString::fromLatin1(name));
    ASSERT_NE(action, nullptr) << name;
    const qsizetype index = actions.indexOf(action);
    ASSERT_GE(index, 0);
    ASSERT_LT(index + 1, actions.size());
    EXPECT_TRUE(actions[index + 1]->isSeparator()) << name;
  }
  ASSERT_FALSE(actions.empty());
  EXPECT_FALSE(actions.front()->isSeparator());
  EXPECT_FALSE(actions.back()->isSeparator());
  for (qsizetype index = 1; index < actions.size(); ++index) {
    EXPECT_FALSE(actions[index - 1]->isSeparator() && actions[index]->isSeparator());
  }
}

TEST_F(QtFileMenuTests, ObjectMenuFlattensFormerGroupsAndOmitsDelete) {
  QApplication::setStyle(QStringLiteral("Fusion"));
  apply_qt_obsidian_application_theme(QApplication::style()->standardPalette());
  QtUiScaleApplicationController scale_controller(*application_);
  auto *object = menu_bar_->findChild<QMenu *>(QStringLiteral("menu_object"));
  ASSERT_NE(object, nullptr);
  const auto actions = object->actions();
  ASSERT_FALSE(actions.empty());
  EXPECT_FALSE(actions.front()->isSeparator());
  EXPECT_FALSE(actions.back()->isSeparator());
  QStringList group_starts;
  bool starts_group = true;
  for (auto *action : actions) {
    EXPECT_EQ(action->menu(), nullptr);
    EXPECT_NE(action->objectName(), QStringLiteral("command_edit_delete"));
    if (action->isSeparator()) {
      EXPECT_FALSE(starts_group);
      starts_group = true;
    } else if (starts_group) {
      group_starts.append(action->objectName());
      starts_group = false;
    }
  }
  EXPECT_EQ(group_starts, (QStringList{
      QStringLiteral("command_edit_create_instance"),
      QStringLiteral("command_edit_group"),
      QStringLiteral("command_modeling_align_x_min"),
      QStringLiteral("command_modeling_align_y_min"),
      QStringLiteral("command_modeling_align_z_min"),
      QStringLiteral("command_modeling_distribute_centers_x"),
      QStringLiteral("command_modeling_distribute_gaps_x"),
      QStringLiteral("command_edit_duplicate"),
      QStringLiteral("action_material_find_replace"),
  }));
  EXPECT_TRUE(editor_->commands().descriptor(quader_command::kDeleteSelection)
                  .has_value());
  EXPECT_EQ(editor_->commands().shortcut_for(quader_command::kDeleteSelection),
            "Delete");

  const auto review_menu = [&](QMenu &reviewed, const QString &filename,
                               int &row_height) {
    reviewed.setAttribute(Qt::WA_DontShowOnScreen, true);
    reviewed.popup(QPoint(20, 20));
    QCoreApplication::processEvents();
    ASSERT_TRUE(reviewed.isVisible());
    reviewed.setActiveAction(reviewed.actions().front());
    row_height = reviewed.actionGeometry(reviewed.actions().front()).height();
    EXPECT_GE(row_height, scale_controller.metrics().px(28));
    const QString directory = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
    if (!directory.isEmpty()) {
      ASSERT_TRUE(QDir().mkpath(directory));
      QImage image(reviewed.size(), QImage::Format_ARGB32_Premultiplied);
      image.fill(Qt::transparent);
      reviewed.render(&image);
      EXPECT_TRUE(image.save(QDir(directory).filePath(filename)));
    }
    reviewed.close();
    QCoreApplication::processEvents();
  };
  auto *view = menu_bar_->findChild<QMenu *>(QStringLiteral("menu_view"));
  auto *window = menu_bar_->findChild<QMenu *>(QStringLiteral("menu_window"));
  ASSERT_NE(view, nullptr);
  ASSERT_NE(window, nullptr);
  const auto review_roots = [&](const QString &suffix) {
    int row_height = 0;
    review_menu(*view, QStringLiteral("view-menu-full-") + suffix + ".png",
                row_height);
    review_menu(*window, QStringLiteral("window-menu-full-") + suffix + ".png",
                row_height);
  };
  int row_100 = 0;
  review_menu(*object, QStringLiteral("object-menu-full-100.png"), row_100);
  review_roots(QStringLiteral("100"));
  ASSERT_TRUE(scale_controller.apply_factor(2.0F));
  int row_200 = 0;
  review_menu(*object, QStringLiteral("object-menu-full-200.png"), row_200);
  EXPECT_GT(row_200, row_100);
  review_roots(QStringLiteral("200"));
  ASSERT_TRUE(scale_controller.apply_factor(1.0F));
  int row_down = 0;
  review_menu(*object, QStringLiteral("object-menu-full-scale-down.png"), row_down);
  EXPECT_EQ(row_down, row_100);
  review_roots(QStringLiteral("scale-down"));
}

TEST_F(QtFileMenuTests,
       ViewportControlsFollowFocusWithoutMutatingApplicationDefaults) {
  auto &viewports = editor_->viewports();
  ASSERT_TRUE(viewports.set_layout(ViewportLayoutId::TwoVertical).ok());
  constexpr ViewportPaneId kFirst = ViewportPaneId::Perspective;
  constexpr ViewportPaneId kSecond = ViewportPaneId::Top;
  ASSERT_TRUE(viewports.focus_pane(kFirst).ok());

  struct ControlAction {
    const char *object_name;
    QuaderViewportControl control;
    ViewportBoolSettingId preference;
  };
  constexpr ControlAction kControls[] = {
      {"command_viewport_toggle_grid", QuaderViewportControl::ShowGrid,
       ViewportBoolSettingId::ShowGrid},
      {"command_viewport_toggle_gizmos", QuaderViewportControl::ShowGizmos,
       ViewportBoolSettingId::ShowGizmos},
      {"command_viewport_toggle_overlays", QuaderViewportControl::ShowOverlays,
       ViewportBoolSettingId::ShowOverlays},
      {"command_viewport_toggle_mesh_grid", QuaderViewportControl::ShowMeshGrid,
       ViewportBoolSettingId::ShowMeshGrid},
  };
  for (const auto &entry : kControls) {
    SCOPED_TRACE(entry.object_name);
    auto *action = menu_bar_->findChild<QAction *>(
        QString::fromLatin1(entry.object_name));
    ASSERT_NE(action, nullptr);
    const bool default_value = viewport_settings_.bool_value(entry.preference);
    const bool original = viewports.control(kFirst, entry.control);
    const bool second_original = viewports.control(kSecond, entry.control);
    ASSERT_TRUE(viewports.focus_pane(kFirst).ok());
    menu_->refresh();
    ASSERT_EQ(action->isChecked(), original);
    action->trigger();
    EXPECT_EQ(viewports.control(kFirst, entry.control), !original);
    EXPECT_EQ(viewports.control(kSecond, entry.control), second_original);
    EXPECT_EQ(viewport_settings_.bool_value(entry.preference), default_value);
    EXPECT_TRUE(viewports.control_overridden(kFirst, entry.control));
    EXPECT_FALSE(viewports.control_overridden(kSecond, entry.control));

    ASSERT_TRUE(viewports.focus_pane(kSecond).ok());
    menu_->refresh();
    EXPECT_EQ(action->isChecked(), second_original);
    ASSERT_TRUE(viewports.focus_pane(kFirst).ok());
    menu_->refresh();
    EXPECT_EQ(action->isChecked(), !original);
  }
}

TEST_F(QtFileMenuTests, RemovedMenuEntriesRetainTheirPublicCommands) {
  for (const QuaderCommandId id : {
           quader_command::kToolsActivateMove,
           quader_command::kToolsActivateRotate,
           quader_command::kToolsActivateScale,
           quader_command::kToolsActivateCreateBox,
           quader_command::kToolsActivateCreateCylinder,
           quader_command::kToolsActivateCreateSphere,
           quader_command::kToolsActivateCreateCone,
           quader_command::kToolsActivateCreateTorus,
           quader_command::kViewportViewTop,
           quader_command::kViewportShadingShaded,
           quader_command::kSelectToggleBackfaceSelection,
       }) {
    SCOPED_TRACE(std::string(id.string()));
    EXPECT_TRUE(editor_->commands().descriptor(id).has_value());
    for (const auto *action : menu_bar_->findChildren<QAction *>()) {
      EXPECT_NE(action->property("quaderCommandId").toString(),
                QString::fromUtf8(id.string().data(),
                                  static_cast<qsizetype>(id.string().size())));
    }
  }
  ASSERT_TRUE(editor_->commands().execute(
      quader_command::kViewportShadingSolid).ok());
  EXPECT_EQ(editor_->viewports().shading_mode(ViewportPaneId::Perspective),
            QuaderShadingMode::Solid);
}

TEST_F(QtFileMenuTests, ComponentMenusPresentOnlyTheInterpolatedBridge) {
  for (const char *operand : {"edge", "face"}) {
    const QString component = QString::fromLatin1(operand);
    auto *menu = menu_bar_->findChild<QMenu *>(QStringLiteral("menu_") + component);
    ASSERT_NE(menu, nullptr);
    const QString suffix = component + QStringLiteral("s");
    EXPECT_EQ(menu->findChild<QAction *>(
                  QStringLiteral("command_modeling_bridge_") + suffix), nullptr);
    auto *interpolated = menu->findChild<QAction *>(
        QStringLiteral("command_modeling_interpolated_bridge_") + suffix);
    ASSERT_NE(interpolated, nullptr);
    EXPECT_TRUE(menu->actions().contains(interpolated));
    int bridge_entries = 0;
    for (const QAction *action : menu->actions()) {
      if (action->property("quaderCommandId").toString().contains(
              QStringLiteral("bridge"))) {
        ++bridge_entries;
      }
    }
    EXPECT_EQ(bridge_entries, 1);
  }
}

TEST_F(QtFileMenuTests,
       DisplayDimensionsToggleTracksPreferencesAndLocalizesInPlace) {
  QApplication::setStyle(QStringLiteral("Fusion"));
  apply_qt_obsidian_application_theme(QApplication::style()->standardPalette());
  QtUiScaleApplicationController scale_controller(*application_);
  const auto preferences_path = root_ / "viewport.ini";
  editor_->preferences().set_runtime_hooks({
      .dimensions_visible = [this] {
        return viewport_settings_.bool_value(ViewportBoolSettingId::ShowDimensions);
      },
      .set_dimensions_visible = [this, &preferences_path](bool visible) {
        const bool changed = viewport_settings_.set_bool_value(
            ViewportBoolSettingId::ShowDimensions, visible);
        if (changed) {
          EXPECT_TRUE(viewport_settings_.save(preferences_path));
        }
        return QuaderApiVoidResult{
            .ok = true,
            .changed = changed,
            .mutation_kind = QuaderMutationKind::Runtime,
        };
      },
  });
  auto *display =
      menu_bar_->findChild<QMenu *>(QStringLiteral("menu_view_display"));
  auto *action = menu_bar_->findChild<QAction *>(
      QStringLiteral("command_viewport_toggle_dimensions"));
  ASSERT_NE(display, nullptr);
  ASSERT_NE(action, nullptr);
  EXPECT_TRUE(display->actions().contains(action));
  menu_->refresh();
  EXPECT_EQ(action->text(), QStringLiteral("Toggle Dimensions"));
  EXPECT_TRUE(action->isCheckable());
  EXPECT_TRUE(action->isChecked());
  EXPECT_TRUE(action->shortcut().isEmpty());

  const auto review_display = [&](const QString &filename, QRect &row_bounds) {
    display->setAttribute(Qt::WA_DontShowOnScreen, true);
    display->popup(QPoint(20, 20));
    QCoreApplication::processEvents();
    ASSERT_TRUE(display->isVisible());
    display->setActiveAction(action);
    EXPECT_TRUE(action->isCheckable());
    EXPECT_TRUE(action->isChecked());
    row_bounds = display->actionGeometry(action);
    EXPECT_TRUE(display->rect().contains(row_bounds));
    EXPECT_GE(row_bounds.height(), scale_controller.metrics().px(28));
    EXPECT_GT(row_bounds.height(), display->fontMetrics().height());
    const int indicator_width = display->style()->pixelMetric(
        QStyle::PM_IndicatorWidth, nullptr, display);
    EXPECT_GE(row_bounds.width(),
              display->fontMetrics().horizontalAdvance(action->text()) +
                  indicator_width + scale_controller.metrics().px(12));
    const QString review_directory =
        qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
    if (!review_directory.isEmpty()) {
      ASSERT_TRUE(QDir().mkpath(review_directory));
      QImage image(display->size(), QImage::Format_ARGB32_Premultiplied);
      image.fill(Qt::transparent);
      display->render(&image);
      EXPECT_TRUE(image.save(QDir(review_directory).filePath(filename)));
    }
    display->close();
    QCoreApplication::processEvents();
  };
  QRect english_100;
  review_display(QStringLiteral("dimensions-display-menu-en-100.png"),
                 english_100);
  ASSERT_TRUE(scale_controller.apply_factor(2.0F));
  QRect english_200;
  review_display(QStringLiteral("dimensions-display-menu-en-200.png"),
                 english_200);
  EXPECT_GT(english_200.height(), english_100.height());
  EXPECT_GT(english_200.width(), english_100.width());

  action->trigger();
  menu_->refresh();
  EXPECT_FALSE(action->isChecked());
  EXPECT_FALSE(editor_->preferences().dimensions_visible());
  EXPECT_FALSE(ViewportSettings::load(preferences_path)
                   .bool_value(ViewportBoolSettingId::ShowDimensions));
  EXPECT_TRUE(viewport_settings_.bool_value(ViewportBoolSettingId::ShowOverlays));
  EXPECT_TRUE(
      viewport_settings_.bool_value(ViewportBoolSettingId::ShowHoverDimensions));

  ASSERT_TRUE(editor_->commands()
                  .execute(quader_command::kViewportToggleDimensions)
                  .changed());
  menu_->refresh();
  EXPECT_TRUE(action->isChecked());
  EXPECT_TRUE(ViewportSettings::load(preferences_path)
                  .bool_value(ViewportBoolSettingId::ShowDimensions));
  EXPECT_FALSE(editor_->preferences().set_dimensions_visible(true).changed());

  ASSERT_TRUE(localization_.select_locale("it-IT"));
  menu_->rebuild();
  display = menu_bar_->findChild<QMenu *>(QStringLiteral("menu_view_display"));
  action = menu_bar_->findChild<QAction *>(
      QStringLiteral("command_viewport_toggle_dimensions"));
  ASSERT_NE(display, nullptr);
  ASSERT_NE(action, nullptr);
  menu_->refresh();
  EXPECT_EQ(action->text(), QStringLiteral("Attiva o disattiva quote"));
  EXPECT_TRUE(action->isChecked());
  QRect italian_200;
  review_display(QStringLiteral("dimensions-display-menu-it-200.png"),
                 italian_200);
  ASSERT_TRUE(scale_controller.apply_factor(1.0F));
  QRect italian_restored;
  review_display(QStringLiteral("dimensions-display-menu-it-restored-100.png"),
                 italian_restored);
  EXPECT_EQ(italian_restored.height(), english_100.height());
  EXPECT_LT(italian_restored.width(), italian_200.width());
}

TEST_F(QtFileMenuTests, RecentScenesRefreshOnlyWhenOpenedAndKeepFiveEntries) {
  show_recent_menu();
  ASSERT_EQ(recent_menu()->actions().size(), 1);
  EXPECT_EQ(recent_menu()->actions().front()->text(),
            QStringLiteral("No recent scenes"));
  EXPECT_FALSE(recent_menu()->actions().front()->isEnabled());

  for (int index = 0; index < 6; ++index) {
    if (index != 0) {
      ASSERT_TRUE(editor_->scene_documents().new_scene().ok());
    }
    save_active_scene(index == 5 ? "Scene & Six" : "Scene " + std::to_string(index));
  }
  menu_->refresh();
  ASSERT_EQ(recent_menu()->actions().size(), 1);
  EXPECT_EQ(recent_menu()->actions().front()->text(),
            QStringLiteral("No recent scenes"));

  show_recent_menu();
  const QList<QAction *> recent = recent_menu()->actions();
  ASSERT_EQ(recent.size(), 5);
  EXPECT_EQ(recent.front()->text(),
            QStringLiteral("Scene && Six (Project && One)"));
  EXPECT_EQ(recent.back()->text(),
            QStringLiteral("Scene 1 (Project && One)"));
  const std::filesystem::path latest_scene =
      project_->root() / "Scenes" / "Scene & Six.qdr";
  EXPECT_EQ(recent.front()->toolTip(),
            QDir::toNativeSeparators(QString::fromStdWString(latest_scene.wstring())));
  EXPECT_TRUE(recent.front()->isEnabled());
  EXPECT_TRUE(recent.front()->shortcut().isEmpty());

  ASSERT_TRUE(std::filesystem::remove(latest_scene));
  menu_->refresh();
  EXPECT_TRUE(recent.front()->isEnabled());
  show_recent_menu();
  ASSERT_EQ(recent_menu()->actions().size(), 5);
  EXPECT_FALSE(recent_menu()->actions().front()->isEnabled());

  auto *file = menu_bar_->findChild<QMenu *>(QStringLiteral("menu_file"));
  auto *open = menu_bar_->findChild<QAction *>(
      QStringLiteral("command_scene_document_open"));
  ASSERT_NE(file, nullptr);
  ASSERT_NE(open, nullptr);
  const qsizetype open_index = file->actions().indexOf(open);
  ASSERT_GE(open_index, 0);
  ASSERT_LT(open_index + 1, file->actions().size());
  EXPECT_EQ(file->actions()[open_index + 1], recent_menu()->menuAction());
}

TEST_F(QtFileMenuTests, RecentSceneActionRebindsInPlaceAndReportsOpenFailure) {
  save_active_scene("First");
  const SceneSessionId first_scene = project_->active_scene_id();
  ASSERT_TRUE(editor_->scene_documents().new_scene().ok());
  save_active_scene("Second");
  show_recent_menu();
  ASSERT_EQ(recent_menu()->actions().size(), 2);
  QPointer<QAction> first_action(recent_menu()->actions()[1]);
  QPointer<QMenu> retained_recent(recent_menu());
  const int rebinds_before = runtime_rebinds_;
  first_action->trigger();
  EXPECT_EQ(project_->active_scene_id(), first_scene);
  EXPECT_EQ(runtime_rebinds_, rebinds_before + 1);
  EXPECT_FALSE(first_action.isNull());
  EXPECT_EQ(retained_recent.data(), recent_menu());

  std::vector<QuaderNotification> notifications;
  editor_->ui().set_runtime_hooks({
      .notify = [&](QuaderNotification notification) {
        notifications.push_back(std::move(notification));
        return QuaderApiVoidResult{.ok = true};
      },
  });
  const SceneSessionId second_scene = project_->open_scenes()[1]->session_id;
  ASSERT_TRUE(editor_->scene_documents().close(second_scene).ok());
  show_recent_menu();
  ASSERT_EQ(recent_menu()->actions().size(), 2);
  QAction *second_action = recent_menu()->actions()[1];
  ASSERT_TRUE(second_action->text().startsWith(QStringLiteral("Second")));
  ASSERT_TRUE(second_action->isEnabled());
  ASSERT_TRUE(std::filesystem::remove(project_->root() / "Scenes" / "Second.qdr"));
  second_action->trigger();
  EXPECT_EQ(project_->active_scene_id(), first_scene);
  ASSERT_EQ(notifications.size(), 1u);
  EXPECT_EQ(notifications.front().level, QuaderNotificationLevel::Error);
  EXPECT_FALSE(notifications.front().message.empty());
}

TEST_F(QtFileMenuTests, RebuildLocalizesTheEmptyRecentAndRecoveryMenus) {
  menu_->refresh();
  auto *project_return = menu_bar_->findChild<QAction *>(
      QStringLiteral("command_project_close"));
  ASSERT_NE(project_return, nullptr);
  EXPECT_EQ(project_return->text(), QStringLiteral("Quit to Project List"));

  ASSERT_TRUE(localization_.select_locale("it-IT"));
  menu_->rebuild();
  menu_->refresh();
  project_return = menu_bar_->findChild<QAction *>(
      QStringLiteral("command_project_close"));
  ASSERT_NE(project_return, nullptr);
  EXPECT_EQ(project_return->text(),
            QStringLiteral("Torna all'elenco dei progetti"));
  show_recent_menu();
  ASSERT_EQ(recent_menu()->actions().size(), 1);
  EXPECT_EQ(recent_menu()->title(), QString::fromStdString(localization_.text(
      "ui.menu.file.open_recent", "Open Recent")));
  EXPECT_EQ(recent_menu()->actions().front()->text(),
            QString::fromStdString(localization_.text(
                "ui.menu.file.no_recent_scenes", "No recent scenes")));
  auto *recover = menu_bar_->findChild<QAction *>(
      QStringLiteral("action_recover_autosave"));
  ASSERT_NE(recover, nullptr);
  EXPECT_EQ(recover->text(), QString::fromStdString(localization_.text(
      "ui.menu.file.recover_autosave", "Auto Save...")));
}

} // namespace
} // namespace quader
