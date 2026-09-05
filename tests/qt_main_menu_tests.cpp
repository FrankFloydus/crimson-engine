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
#include "ui/qt_main_menu.hpp"

#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QMenu>
#include <QMenuBar>
#include <QMetaObject>
#include <QPointer>
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
  ASSERT_TRUE(localization_.select_locale("it-IT"));
  menu_->rebuild();
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
