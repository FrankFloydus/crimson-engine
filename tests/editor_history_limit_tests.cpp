////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api.hpp"
#include "api/quader_api_session_internal_access.hpp"
#include "core/undo_redo_manager.hpp"
#include "editor/commands/document_command_service.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/selection/editor_selection_service.hpp"
#include "editor/session/project_session.hpp"
#include "editor/session/scene_session.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quader {
namespace {

/// Tracks both replay and retained payload lifetime for bounded history tests.
class RetainedValueCommand final : public IUndoableCommand {
public:
  RetainedValueCommand(int &value, int after,
                       std::shared_ptr<int> retained = {})
      : value_(value), before_(value), after_(after),
        retained_(std::move(retained)) {
    if (retained_) {
      ++*retained_;
    }
  }

  ~RetainedValueCommand() override {
    if (retained_) {
      --*retained_;
    }
  }

  const std::string &label() const override { return label_; }

  CommandResult undo() override {
    if (fail_undo) {
      return command_invalid_result("Injected undo failure");
    }
    value_ = before_;
    return command_changed_result();
  }

  CommandResult redo() override {
    if (fail_redo) {
      return command_invalid_result("Injected redo failure");
    }
    if (before_ == after_) {
      return command_unchanged_result();
    }
    value_ = after_;
    return command_changed_result();
  }

  bool merge_with(const IUndoableCommand &newer,
                  std::string_view key) override {
    const auto *command = dynamic_cast<const RetainedValueCommand *>(&newer);
    if (key != "gesture" || !command || &command->value_ != &value_) {
      return false;
    }
    after_ = command->after_;
    return true;
  }

  bool fail_undo = false;
  bool fail_redo = false;

private:
  int &value_;
  int before_;
  int after_;
  std::shared_ptr<int> retained_;
  std::string label_ = "Change value";
};

void set_value(EditorHistoryService &history, int &value, int next,
               std::shared_ptr<int> retained = {}) {
  const CommandResult result = history.execute(
      std::make_unique<RetainedValueCommand>(value, next, std::move(retained)));
  ASSERT_TRUE(result.ok) << result.message;
}

/// Removes only this test's uniquely named temporary project.
class HistoryProjectDirectory final {
public:
  HistoryProjectDirectory()
      : path_(std::filesystem::temp_directory_path() /
              ("quader-history-limit-" +
               std::to_string(std::chrono::steady_clock::now()
                                  .time_since_epoch().count()))) {
    std::filesystem::create_directory(path_);
  }
  ~HistoryProjectDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }
  const std::filesystem::path &path() const { return path_; }

private:
  std::filesystem::path path_;
};

TEST(EditorHistoryLimitTests, BlenderDefaultRetainsExactly32CommittedEdits) {
  EditorHistoryService history;
  auto retained = std::make_shared<int>(0);
  int value = 0;
  EXPECT_EQ(history.max_steps(), 32u);
  for (int next = 1; next <= 40; ++next) {
    set_value(history, value, next, retained);
  }
  EXPECT_EQ(history.undo_count(), 32u);
  EXPECT_EQ(*retained, 32);
  for (int step = 0; step < 32; ++step) {
    ASSERT_TRUE(history.undo().ok);
  }
  EXPECT_EQ(value, 8);
  EXPECT_FALSE(history.can_undo());
  EXPECT_EQ(history.redo_count(), 32u);
  for (int step = 0; step < 32; ++step) {
    ASSERT_TRUE(history.redo().ok);
  }
  EXPECT_EQ(value, 40);
}

TEST(EditorHistoryLimitTests, ShrinkingPreservesContiguousRedoAndReleasesPayloads) {
  EditorHistoryService history;
  auto retained = std::make_shared<int>(0);
  int value = 0;
  for (int next = 1; next <= 6; ++next) {
    set_value(history, value, next, retained);
  }
  for (int step = 0; step < 4; ++step) {
    ASSERT_TRUE(history.undo().ok);
  }
  ASSERT_EQ(value, 2);
  history.set_max_steps(3);
  EXPECT_EQ(value, 2);
  EXPECT_EQ(*retained, 3);
  EXPECT_EQ(history.undo_count(), 0u);
  EXPECT_EQ(history.redo_count(), 3u);
  for (int expected = 3; expected <= 5; ++expected) {
    ASSERT_TRUE(history.redo().ok);
    EXPECT_EQ(value, expected);
  }
  EXPECT_FALSE(history.can_redo());
  for (int expected = 4; expected >= 2; --expected) {
    ASSERT_TRUE(history.undo().ok);
    EXPECT_EQ(value, expected);
  }
  EXPECT_FALSE(history.can_undo());
}

TEST(EditorHistoryLimitTests, BranchDiscardsRedoWithoutResurrectingEvictedEdits) {
  EditorHistoryService history;
  history.set_max_steps(2);
  int value = 0;
  set_value(history, value, 1);
  set_value(history, value, 2);
  set_value(history, value, 3);
  ASSERT_TRUE(history.undo().ok);
  set_value(history, value, 9);
  EXPECT_FALSE(history.can_redo());
  ASSERT_TRUE(history.undo().ok);
  EXPECT_EQ(value, 2);
  ASSERT_TRUE(history.undo().ok);
  EXPECT_EQ(value, 1);
  EXPECT_FALSE(history.can_undo());
  history.set_max_steps(10);
  EXPECT_FALSE(history.can_undo());
  ASSERT_TRUE(history.redo().ok);
  ASSERT_TRUE(history.redo().ok);
  EXPECT_EQ(value, 9);
}

TEST(EditorHistoryLimitTests, ZeroDropsHistoryButKeepsEditsAndTransactionRollback) {
  EditorHistoryService history;
  int value = 0;
  set_value(history, value, 1);
  EditorTransaction transaction = history.begin_transaction("Provisional edit");
  ASSERT_TRUE(transaction.execute(
      std::make_unique<RetainedValueCommand>(value, 2)).ok);
  history.set_max_steps(0);
  EXPECT_EQ(value, 2);
  ASSERT_TRUE(transaction.cancel().ok);
  EXPECT_EQ(value, 1);
  EXPECT_EQ(history.undo_count(), 0u);
  set_value(history, value, 3);
  EXPECT_EQ(value, 3);
  EXPECT_FALSE(history.can_undo());
  EXPECT_FALSE(history.can_redo());
  history.set_max_steps(1);
  set_value(history, value, 4);
  ASSERT_TRUE(history.undo().ok);
  EXPECT_EQ(value, 3);
  EXPECT_FALSE(history.can_undo());
}

TEST(EditorHistoryLimitTests, CompoundEditsCountOnceAndFailedCancelKeepsChildren) {
  EditorHistoryService history;
  history.set_max_steps(1);
  int value = 0;
  set_value(history, value, 1);
  EditorTransaction transaction = history.begin_transaction("Two changes");
  auto first = std::make_unique<RetainedValueCommand>(value, 2);
  auto *first_ptr = first.get();
  first_ptr->fail_undo = true;
  ASSERT_TRUE(transaction.execute(std::move(first)).ok);
  ASSERT_TRUE(transaction.execute(
      std::make_unique<RetainedValueCommand>(value, 3)).ok);
  EXPECT_FALSE(transaction.cancel().ok);
  EXPECT_EQ(value, 3);
  EXPECT_TRUE(transaction.active());
  first_ptr->fail_undo = false;
  ASSERT_TRUE(transaction.commit().ok);
  EXPECT_EQ(history.undo_count(), 1u);
  ASSERT_TRUE(history.undo().ok);
  EXPECT_EQ(value, 1);
  EXPECT_FALSE(history.can_undo());
  ASSERT_TRUE(history.redo().ok);
  EXPECT_EQ(value, 3);
}

TEST(EditorHistoryLimitTests, FailureAndNoopPreserveRetainedHistoryAtCapacity) {
  EditorHistoryService history;
  history.set_max_steps(1);
  int value = 0;
  auto command = std::make_unique<RetainedValueCommand>(value, 1);
  auto *command_ptr = command.get();
  ASSERT_TRUE(history.execute(std::move(command)).ok);
  command_ptr->fail_undo = true;
  EXPECT_FALSE(history.undo().ok);
  EXPECT_EQ(history.undo_count(), 1u);
  EXPECT_EQ(value, 1);
  command_ptr->fail_undo = false;
  ASSERT_TRUE(history.undo().ok);
  command_ptr->fail_redo = true;
  EXPECT_FALSE(history.redo().ok);
  EXPECT_EQ(history.redo_count(), 1u);
  auto failed = std::make_unique<RetainedValueCommand>(value, 5);
  failed->fail_redo = true;
  EXPECT_FALSE(history.execute(std::move(failed)).ok);
  EXPECT_FALSE(history.execute(
      std::make_unique<RetainedValueCommand>(value, 0)).changed);
  EXPECT_EQ(history.redo_count(), 1u);
  EXPECT_EQ(value, 0);
  command_ptr->fail_redo = false;
  ASSERT_TRUE(history.redo().ok);
  EXPECT_EQ(value, 1);
}

TEST(EditorHistoryLimitTests, ExplicitMergesAndRecordedCommandsRespectLimit) {
  EditorHistoryService history;
  history.set_max_steps(1);
  int value = 0;
  for (int next = 1; next <= 5; ++next) {
    ASSERT_TRUE(history.execute(
        std::make_unique<RetainedValueCommand>(value, next),
        {.merge_key = "gesture"}).ok);
  }
  EXPECT_EQ(history.undo_count(), 1u);
  ASSERT_TRUE(history.undo().ok);
  EXPECT_EQ(value, 0);
  ASSERT_TRUE(history.redo().ok);
  auto recorded = std::make_unique<RetainedValueCommand>(value, 9);
  ASSERT_TRUE(recorded->redo().ok);
  ASSERT_TRUE(history.record_executed(std::move(recorded)).ok);
  ASSERT_TRUE(history.undo().ok);
  EXPECT_EQ(value, 5);
  EXPECT_FALSE(history.can_undo());
  history.clear();
  EXPECT_EQ(history.max_steps(), 1u);
  EXPECT_FALSE(history.can_redo());
}

TEST(EditorHistoryLimitTests, PruningDoesNotChangeContentRevisionOrSavedState) {
  SceneSession session;
  QuaderApi api(QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto entity = api.scene().create_entity({.name = "Original"});
  ASSERT_TRUE(entity.ok()) << entity.error_message();
  session.mark_clean();
  const SceneContentRevisionStamp saved_stamp = session.content_revision_stamp();
  session.set_history_max_steps(0);
  EXPECT_EQ(session.content_revision_stamp(), saved_stamp);
  EXPECT_FALSE(session.dirty());
  ASSERT_TRUE(api.scene().rename(entity.value().id, "Changed").ok());
  const SceneContentRevisionStamp dirty_stamp = session.content_revision_stamp();
  ASSERT_TRUE(session.dirty());
  session.set_history_max_steps(2);
  EXPECT_EQ(session.content_revision_stamp(), dirty_stamp);
  EXPECT_TRUE(session.dirty());
  ASSERT_TRUE(api.scene().rename(entity.value().id, "Again").ok());
  ASSERT_TRUE(api.history().undo().ok());
  ASSERT_NE(session.scene().name(entity.value().id), nullptr);
  EXPECT_EQ(session.scene().name(entity.value().id)->name, "Changed");
  EXPECT_FALSE(api.history().can_undo());
}

TEST(EditorHistoryLimitTests, SceneReplacementKeepsConfiguredLimit) {
  SceneSession session;
  session.set_history_max_steps(7);
  session.replace_document(SceneContentDocument{});
  EXPECT_EQ(session.history().max_steps(), 7u);
  EXPECT_FALSE(session.dirty());
  session.set_history_max_steps(1000);
  EXPECT_EQ(session.history().max_steps(), 256u);
}

TEST(EditorHistoryLimitTests, ProjectPolicyReachesExistingFutureAndReopenedScenes) {
  HistoryProjectDirectory directory;
  ProjectSession project;
  project.set_history_max_steps(5);
  EXPECT_EQ(project.fallback_scene().history().max_steps(), 5u);
  const auto created = project.create(directory.path());
  ASSERT_TRUE(created.ok) << created.message;
  ASSERT_TRUE(project.new_scene().ok);
  ASSERT_TRUE(project.new_scene().ok);
  for (const auto &scene : project.open_scenes()) {
    ASSERT_NE(scene->session, nullptr);
    EXPECT_EQ(scene->session->history().max_steps(), 5u);
  }
  project.set_history_max_steps(2);
  for (const auto &scene : project.open_scenes()) {
    EXPECT_EQ(scene->session->history().max_steps(), 2u);
  }
  ASSERT_TRUE(project.new_scene().ok);
  ASSERT_NE(project.active_scene(), nullptr);
  EXPECT_EQ(project.active_scene()->session->history().max_steps(), 2u);
  const auto path = ProjectRelativePath::parse_portable("Scenes/history.qdr");
  ASSERT_TRUE(path.ok());
  const auto saved = project.save_as(project.active_scene_id(), path.value());
  ASSERT_TRUE(saved.ok) << saved.message;
  const auto manifest_path = project.manifest_path();
  ASSERT_TRUE(project.close(true).ok);
  const auto reopened = project.open(manifest_path);
  ASSERT_TRUE(reopened.ok) << reopened.message;
  ASSERT_NE(project.active_scene(), nullptr);
  EXPECT_EQ(project.active_scene()->session->history().max_steps(), 2u);
}

TEST(EditorHistoryLimitTests,
     ResolvedDepthCorrectionKeepsOneSelectionHistoryEntryAndOriginalBaseline) {
  SceneSession session;
  const EntityId before = session.scene().create_entity("Before");
  const EntityId front = session.scene().create_entity("Front");
  const EntityId behind = session.scene().create_entity("Behind");
  ASSERT_TRUE(session.selection().set_scene_selection({&before, 1u}, before).ok);
  session.history().clear();
  const auto baseline = session.selection().capture_box_selection_baseline();
  ResolvedSelectionClickRequest click;
  click.winner = ResolvedSelectionTarget{
      .domain = ResolvedSelectionDomain::SceneEntity, .entity = front};
  click.gesture_merge_key = 1;
  ASSERT_TRUE(session.selection().apply_resolved_click(click).command.ok);
  ASSERT_EQ(session.scene().selected_entities(), std::vector<EntityId>{front});
  click.winner->entity = behind;
  click.baseline = baseline;
  click.expected_history_revision =
      session.selection().selection_history_revision();
  ASSERT_TRUE(session.selection().apply_resolved_click(click).command.ok);
  EXPECT_EQ(session.scene().selected_entities(), std::vector<EntityId>{behind});
  EXPECT_EQ(session.history().undo_count(), 1u);
  const auto revision = session.scene().selection_revision();
  ASSERT_TRUE(session.history().undo().ok);
  EXPECT_EQ(session.scene().selected_entities(), std::vector<EntityId>{before});
  EXPECT_GT(session.scene().selection_revision(), revision);
  EXPECT_FALSE(session.history().can_undo());
  ASSERT_TRUE(session.history().redo().ok);
  EXPECT_EQ(session.scene().selected_entities(), std::vector<EntityId>{behind});
}

TEST(EditorHistoryLimitTests,
     SelectionGesturePublicationFailurePreservesHistoryPayloadAndRevisions) {
  SceneDocument scene;
  EditorHistoryService history;
  DocumentCommandService commands(scene, history);
  const EntityId before = scene.create_entity("Before");
  const EntityId front = scene.create_entity("Front");
  const EntityId behind = scene.create_entity("Behind");
  ASSERT_TRUE(scene.replace_selection({&before, 1u}, before));
  bool reject = false;
  std::vector<EntityId> published{before};
  commands.set_selection_publication([&] {
    if (reject) return command_invalid_result("Injected selection rejection");
    published = scene.selected_entities();
    return command_changed_result();
  });
  const auto select = [&](EntityId entity,
                          std::optional<std::uint64_t> revision = {}) {
    return commands.select_entities(
        {entity}, entity, SceneSelectionApplicationMode::UndoableCommand, {},
        {.merge_key = "selection-click/2"}, revision);
  };
  ASSERT_TRUE(select(front).ok);
  const auto first_revision = history.revision();
  const auto scene_revision = scene.revision();
  const auto selection_revision = scene.selection_revision();
  reject = true;
  const auto rejected = select(behind, first_revision);
  EXPECT_FALSE(rejected.ok);
  EXPECT_FALSE(rejected.changed);
  EXPECT_EQ(scene.selected_entities(), std::vector<EntityId>{front});
  EXPECT_EQ(published, std::vector<EntityId>{front});
  EXPECT_EQ(scene.revision(), scene_revision);
  EXPECT_EQ(scene.selection_revision(), selection_revision);
  EXPECT_EQ(history.revision(), first_revision);
  EXPECT_EQ(history.undo_count(), 1u);

  reject = false;
  ASSERT_TRUE(select(behind, first_revision).ok);
  EXPECT_EQ(history.undo_count(), 1u);
  const auto merged_revision = scene.revision();
  const auto merged_selection_revision = scene.selection_revision();
  reject = true;
  EXPECT_FALSE(history.undo().ok);
  EXPECT_EQ(scene.selected_entities(), std::vector<EntityId>{behind});
  EXPECT_EQ(published, std::vector<EntityId>{behind});
  EXPECT_EQ(scene.revision(), merged_revision);
  EXPECT_EQ(scene.selection_revision(), merged_selection_revision);
  EXPECT_EQ(history.undo_count(), 1u);
  reject = false;
  ASSERT_TRUE(history.undo().ok);
  EXPECT_EQ(scene.selected_entities(), std::vector<EntityId>{before});
  EXPECT_EQ(published, std::vector<EntityId>{before});
  const auto undone_revision = scene.revision();
  const auto undone_selection_revision = scene.selection_revision();
  reject = true;
  EXPECT_FALSE(history.redo().ok);
  EXPECT_EQ(scene.selected_entities(), std::vector<EntityId>{before});
  EXPECT_EQ(scene.revision(), undone_revision);
  EXPECT_EQ(scene.selection_revision(), undone_selection_revision);
  EXPECT_EQ(history.redo_count(), 1u);
  reject = false;
  ASSERT_TRUE(history.redo().ok);
  EXPECT_EQ(published, std::vector<EntityId>{behind});
}

TEST(EditorHistoryLimitTests,
     SelectionGestureDoesNotMergeOrdinaryClicksOrAcrossHistoryBoundaries) {
  for (int scenario = 0; scenario < 4; ++scenario) {
    SCOPED_TRACE(scenario);
    SceneDocument scene;
    EditorHistoryService history;
    DocumentCommandService commands(scene, history);
    const EntityId front = scene.create_entity("Front");
    const EntityId behind = scene.create_entity("Behind");
    const std::string first_key = scenario == 0 ? "" : "selection-click/3";
    ASSERT_TRUE(commands.select_entities(
        {front}, front, SceneSelectionApplicationMode::UndoableCommand, {},
        {.merge_key = first_key}).ok);
    const auto first_revision = history.revision();
    if (scenario == 2) {
      ASSERT_TRUE(commands.rename_entity(front, "Renamed").ok);
    } else if (scenario == 3) {
      ASSERT_TRUE(history.undo().ok);
      ASSERT_TRUE(history.redo().ok);
    }
    const std::string next_key = scenario == 1 ? "selection-click/4" : first_key;
    ASSERT_TRUE(commands.select_entities(
        {behind}, behind, SceneSelectionApplicationMode::UndoableCommand, {},
        {.merge_key = next_key}, first_revision).ok);
    EXPECT_EQ(history.undo_count(), scenario == 2 ? 3u : 2u);
    ASSERT_TRUE(history.undo().ok);
    EXPECT_EQ(scene.selected_entities(), std::vector<EntityId>{front});
  }
}

TEST(EditorHistoryLimitTests, SelectionGestureCannotMergeAcrossDocuments) {
  SceneDocument first_scene;
  SceneDocument second_scene;
  EditorHistoryService history;
  DocumentCommandService first(first_scene, history);
  DocumentCommandService second(second_scene, history);
  const EntityId first_entity = first_scene.create_entity("First");
  const EntityId second_entity = second_scene.create_entity("Second");
  first.set_selection_publication([] { return command_changed_result(); });
  second.set_selection_publication([] { return command_changed_result(); });
  ASSERT_TRUE(first.select_entities(
      {first_entity}, first_entity, SceneSelectionApplicationMode::UndoableCommand,
      {}, {.merge_key = "selection-click/5"}).ok);
  ASSERT_TRUE(second.select_entities(
      {second_entity}, second_entity,
      SceneSelectionApplicationMode::UndoableCommand, {},
      {.merge_key = "selection-click/5"}, history.revision()).ok);
  EXPECT_EQ(history.undo_count(), 2u);
  ASSERT_TRUE(history.undo().ok);
  EXPECT_TRUE(second_scene.selected_entities().empty());
  EXPECT_EQ(first_scene.selected_entities(), std::vector<EntityId>{first_entity});
  ASSERT_TRUE(history.undo().ok);
  EXPECT_TRUE(first_scene.selected_entities().empty());
}

} // namespace
} // namespace quader
