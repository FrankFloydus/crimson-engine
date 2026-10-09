////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "support/guardrail_file_scan.hpp"

#include <regex>
#include <string>
#include <string_view>

namespace {

using quader::test::guardrail::read_source_file;
using quader::test::guardrail::source_slice;

void expect_before(std::string_view source, std::string_view first,
                   std::string_view second) {
  const auto first_position = source.find(first);
  const auto second_position = source.find(second);
  ASSERT_NE(first_position, std::string_view::npos) << first;
  ASSERT_NE(second_position, std::string_view::npos) << second;
  EXPECT_LT(first_position, second_position) << first << " before " << second;
}

TEST(SessionSchedulingGuardrailTests,
     AutosaveUsesASeparateCoarseTimerOutsideFrameWork) {
  const std::string app = read_source_file("src/app/app.cpp");
  const std::string ui = read_source_file("src/ui/ui_system.cpp");
  const std::string initialize =
      source_slice(app, "bool App::initialize(", "bool App::prepare_composed_shell()");
  ASSERT_FALSE(initialize.empty());
  EXPECT_NE(initialize.find("frame_timer_ = std::make_unique<QTimer>()"),
            std::string::npos);
  EXPECT_NE(initialize.find("autosave_timer_ = std::make_unique<QTimer>()"),
            std::string::npos);
  EXPECT_NE(initialize.find("autosave_timer_->setTimerType(Qt::CoarseTimer)"),
            std::string::npos);
  EXPECT_NE(initialize.find("QObject::connect(autosave_timer_.get()"),
            std::string::npos);
  EXPECT_NE(initialize.find("[this]() { poll_autosave(); }"),
            std::string::npos);
  std::smatch interval;
  ASSERT_TRUE(std::regex_search(
      initialize, interval,
      std::regex{R"(autosave_timer_->setInterval\(\s*([0-9]+)\s*\))"}));
  const int interval_ms = std::stoi(interval[1].str());
  EXPECT_GE(interval_ms, 1000) << "Autosave must not wake at rendering cadence.";
  EXPECT_LE(interval_ms, 60000) << "Respect the minimum one-minute save interval.";

  const std::string render_frame = source_slice(
      app, "bool App::render_frame(", "void App::schedule_next_frame(");
  const std::string ui_refresh = source_slice(
      ui, "void UISystem::refresh(", "void UISystem::process_viewport_input(");
  ASSERT_FALSE(render_frame.empty());
  ASSERT_FALSE(ui_refresh.empty());
  for (const std::string_view body : {std::string_view{render_frame},
                                     std::string_view{ui_refresh}}) {
    for (const std::string_view forbidden : {
             "poll_autosave(", ".autosave()", "write_scene_recovery(",
             "write_project_document_atomic("}) {
      EXPECT_EQ(body.find(forbidden), std::string_view::npos) << forbidden;
    }
  }
  const std::string run = source_slice(app, "int App::run()", "void App::shutdown()");
  expect_before(run, "autosave_timer_->start()", "application_->exec()");
}

TEST(SessionSchedulingGuardrailTests,
     AutosaveDefersNestedFrameModalPopupAndPointerInteractions) {
  const std::string app = read_source_file("src/app/app.cpp");
  const std::string poll =
      source_slice(app, "void App::poll_autosave()", "int App::run()");
  ASSERT_FALSE(poll.empty());
  expect_before(
      poll, "if (!running_ || shutdown_complete_ || shell_replacement_pending_)",
      "project_session_.autosave().poll(");
  expect_before(poll, "return;", "project_session_.autosave().poll(");
  for (const std::string_view interaction : {
           "rendering_frame_", "QApplication::activeModalWidget()",
           "QApplication::activePopupWidget()", "QApplication::mouseButtons()"}) {
    expect_before(poll, interaction, "project_session_.autosave().poll(");
  }
  EXPECT_NE(poll.find("std::chrono::steady_clock::now(), external_edit_active"),
            std::string::npos);
  EXPECT_NE(poll.find("ui_.session_settings()"), std::string::npos);
}

TEST(SessionSchedulingGuardrailTests,
     ShutdownStopsAutosaveBeforeDestroyingItsUiAndEditorOwners) {
  const std::string app = read_source_file("src/app/app.cpp");
  const std::string shutdown = source_slice(
      app, "void App::shutdown()", "void App::retire_shell()");
  ASSERT_FALSE(shutdown.empty());
  expect_before(shutdown, "running_ = false", "autosave_timer_->stop()");
  expect_before(shutdown, "autosave_timer_->stop()", "retire_shell()");
  expect_before(shutdown, "autosave_timer_->stop()", "autosave_timer_.reset()");
  expect_before(shutdown, "autosave_timer_.reset()", "editor_.reset()");
  expect_before(shutdown, "retire_shell()", "editor_.reset()");
  const std::string retire_shell = source_slice(
      app, "void App::retire_shell()", "void App::replace_shell()");
  ASSERT_FALSE(retire_shell.empty());
  expect_before(retire_shell, "set_session_settings_changed_callback({})",
                 "ui_.shutdown()");
}

TEST(SessionSchedulingGuardrailTests,
     ApiCommitsSessionPreferencesBeforeIrreversibleHistoryNotification) {
  const std::string ui = read_source_file("src/ui/ui_system.cpp");
  const std::string setter = source_slice(
      ui, ".set_session_settings = [this]", ".save =");
  ASSERT_FALSE(setter.empty());
  expect_before(setter, "if (!save_settings())", "session_settings_changed_(value)");
  const std::string failure = source_slice(
      setter, "if (!save_settings())", "if (session_settings_changed_)");
  EXPECT_NE(failure.find("settings_.set_session_settings(previous)"),
            std::string::npos);
  EXPECT_NE(failure.find("return runtime_failure("), std::string::npos);
  EXPECT_EQ(failure.find("session_settings_changed_(value)"), std::string::npos);

  const std::string app = read_source_file("src/app/app.cpp");
  const std::string binding = source_slice(
      app, "bool App::initialize_shell()", "void App::poll_autosave()");
  ASSERT_FALSE(binding.empty());
  EXPECT_NE(binding.find("ui_.set_session_settings_changed_callback("),
            std::string::npos);
  EXPECT_NE(binding.find("project_session_.set_history_max_steps("),
            std::string::npos);
  EXPECT_NE(binding.find("settings.undo_steps"), std::string::npos);
  EXPECT_NE(binding.find("ui_.session_settings().undo_steps"),
            std::string::npos);
}

TEST(SessionSchedulingGuardrailTests,
     DialogPersistsOrRollsBackBeforePublishingSessionCommit) {
  const std::string dialog = read_source_file("src/ui/qt_settings_dialog.cpp");
  const std::string apply =
      source_slice(dialog, "bool apply_changes()", "void discard_changes()");
  ASSERT_FALSE(apply.empty());
  expect_before(apply, "!persist_interface_settings()",
                 "QtSettingsChangeKind::SessionCommit");
  const std::string failure = source_slice(
      apply, "!persist_interface_settings()",
      "if (viewport_changed && settings_changed)");
  EXPECT_NE(failure.find("*target_ui_settings = previous_ui_settings"),
            std::string::npos);
  EXPECT_NE(failure.find("return false;"), std::string::npos);
  EXPECT_EQ(failure.find("QtSettingsChangeKind::SessionCommit"),
            std::string::npos);
  const std::string stage = source_slice(
      dialog, "void stage_session_field(", "void mark_color_dirty(");
  ASSERT_FALSE(stage.empty());
  EXPECT_EQ(stage.find("QtSettingsChangeKind::SessionCommit"),
            std::string::npos);
  EXPECT_EQ(stage.find("target_ui_settings->set_session_settings("),
            std::string::npos);
}

TEST(SessionSchedulingGuardrailTests,
     RecoveryBrowserUsesOwnerFolderAndRejectsCancelledOrStaleSelections) {
  const auto ui = read_source_file("src/ui/ui_system.cpp");
  const auto recovery = source_slice(
      ui, "void UISystem::show_autosave_recovery()",
      "void UISystem::show_settings_dialog()");
  ASSERT_FALSE(recovery.empty());
  expect_before(recovery, "autosave_directory()",
                "platform_open_scene_recovery_file_dialog(options)");
  EXPECT_NE(recovery.find("options.initial_directory = directory.value()"),
            std::string::npos);
  expect_before(recovery, "!selected", "recover_autosave_native_path(*selected)");
  expect_before(recovery, "binding_generation != runtime_binding_generation_",
                "recover_autosave_native_path(*selected)");
  EXPECT_EQ(ui.find("QtAutosaveRecoveryDialog"), std::string::npos);

  const auto platform = read_source_file("src/platform/platform_file_dialog.cpp");
  const auto browser = source_slice(platform, "choose_filesystem_entry(",
                                   "namespace platform_file_dialog_detail");
  EXPECT_NE(browser.find("dialog->SetFolder(initial)"), std::string::npos);
  EXPECT_NE(browser.find("folders_only || folder_selected"), std::string::npos);
  EXPECT_NE(browser.find("FOS_FILEMUSTEXIST"), std::string::npos);
  EXPECT_NE(browser.find("L\"*.qdr\""), std::string::npos);
}

} // namespace
