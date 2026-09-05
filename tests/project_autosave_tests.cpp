////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"
#include "core/session_preferences.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/session/project_autosave_service.hpp"
#include "editor/session/project_session.hpp"
#include "io/qdr/qdr_document_reader.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace quader;
using namespace std::chrono_literals;

namespace {

ProjectRelativePath autosave_test_path(std::string_view path) {
  auto parsed = ProjectRelativePath::parse_portable(path);
  EXPECT_TRUE(parsed.ok()) << parsed.error().message;
  return parsed.ok() ? parsed.take_value() : ProjectRelativePath{};
}

std::vector<char> autosave_test_bytes(const std::filesystem::path &path) {
  std::ifstream file(path, std::ios::binary);
  EXPECT_TRUE(file.is_open()) << path.string();
  return {std::istreambuf_iterator<char>(file),
          std::istreambuf_iterator<char>()};
}

class ProjectAutosave : public ::testing::Test {
protected:
  ProjectAutosave()
      : root_(std::filesystem::temp_directory_path() /
              ("quader-autosave-" + std::string(ProjectId::generate().str()))),
        editor_(project_) {}

  void SetUp() override {
    std::error_code error;
    std::filesystem::create_directory(root_, error);
    ASSERT_FALSE(error) << error.message();
    const auto created = editor_.project().create(root_);
    ASSERT_TRUE(created.ok()) << created.error_message();
    ASSERT_NE(project_.active_scene(), nullptr);
  }

  void TearDown() override {
    std::error_code ignored;
    std::filesystem::remove_all(root_, ignored);
  }

  QuaderModelingObjectRef create_box(std::string name = "Recovery Box") {
    const auto created = editor_.modeling().create_box(
        QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                     .max = {1.0f, 1.0f, 1.0f}},
        std::move(name));
    EXPECT_TRUE(created.ok()) << created.error_message();
    return created.ok() ? created.value() : QuaderModelingObjectRef{};
  }

  ProjectAutosaveResult poll(std::chrono::seconds elapsed,
                            bool external_edit_active = false) {
    return project_.autosave().poll(project_, settings_, epoch_ + elapsed,
                                    external_edit_active);
  }

  std::vector<QuaderAutosaveInfo> entries() {
    const auto found = editor_.scene_documents().autosaves();
    EXPECT_TRUE(found.ok()) << found.error_message();
    return found.ok() ? found.value() : std::vector<QuaderAutosaveInfo>{};
  }

  std::filesystem::path native(const ProjectRelativePath &path) const {
    return root_ / path.native_path();
  }

  ProjectRelativePath move_to_legacy_layout(const QuaderAutosaveInfo &entry) {
    const auto legacy_path = autosave_test_path(
        ".quader/autosave/" + std::string(entry.scene_id.str()) + "/" +
        std::string(SceneId::generate().str()) + ".qdr");
    auto old_metadata = native(entry.path);
    old_metadata.replace_extension(".json");
    nlohmann::ordered_json metadata;
    {
      std::ifstream input(old_metadata, std::ios::binary);
      metadata = nlohmann::ordered_json::parse(input);
    }
    metadata["version"] = 1;
    metadata.erase("snapshot_id");
    std::filesystem::create_directories(native(legacy_path).parent_path());
    std::filesystem::rename(native(entry.path), native(legacy_path));
    auto new_metadata = native(legacy_path);
    new_metadata.replace_extension(".json");
    {
      std::ofstream output(new_metadata, std::ios::binary);
      output << metadata.dump(2);
    }
    std::filesystem::remove(old_metadata);
    return legacy_path;
  }

  std::filesystem::path root_;
  ProjectSession project_;
  QuaderEditorApi editor_;
  SessionPreferences settings_;
  std::chrono::steady_clock::time_point epoch_{};
};

TEST_F(ProjectAutosave, DefaultIntervalWaitsTwoMinutesAndSkipsUnchangedContent) {
  EXPECT_TRUE(settings_.autosave_enabled);
  EXPECT_EQ(settings_.autosave_interval_minutes, 2);
  EXPECT_EQ(settings_.autosave_max_files, 3);
  create_box();
  EXPECT_EQ(poll(0s).written, 0u);
  EXPECT_TRUE(entries().empty());
  EXPECT_EQ(poll(119s).written, 0u);
  const auto due = poll(120s);
  ASSERT_TRUE(due.ok);
  EXPECT_EQ(due.written, 1u);
  const auto saved = entries();
  ASSERT_EQ(saved.size(), 1u);
  EXPECT_FALSE(saved.front().source_path.has_value());
  EXPECT_TRUE(project_.active_scene()->dirty());
  EXPECT_EQ(poll(240s).written, 0u);
  const auto unchanged = entries();
  ASSERT_EQ(unchanged.size(), 1u);
  EXPECT_EQ(unchanged.front().path, saved.front().path);
}

TEST_F(ProjectAutosave, RecoveryDirectoryIsAvailableBeforeFirstCopyWhenDisabled) {
  settings_.autosave_enabled = false;
  create_box();
  EXPECT_EQ(poll(0s).written, 0u);
  EXPECT_FALSE(std::filesystem::exists(root_ / ".quader/autosave"));
  const auto directory = editor_.scene_documents().autosave_directory();
  ASSERT_TRUE(directory.ok()) << directory.error_message();
  EXPECT_EQ(directory.value(), root_ / ".quader/autosave");
  EXPECT_TRUE(std::filesystem::is_directory(directory.value()));
  EXPECT_TRUE(entries().empty());
  EXPECT_EQ(poll(600s).written, 0u);
  EXPECT_TRUE(std::filesystem::is_empty(directory.value()));
  ProjectSession closed;
  EXPECT_FALSE(closed.autosave().directory(closed).ok());
}

TEST_F(ProjectAutosave, RecoveryDirectoryRejectsAFileInPlaceOfTheFolder) {
  const auto storage = root_ / ".quader/autosave";
  std::filesystem::create_directories(storage.parent_path());
  {
    std::ofstream blocker(storage, std::ios::binary);
    ASSERT_TRUE(blocker.is_open());
    blocker << "Existing file must remain unchanged";
  }
  const auto before = autosave_test_bytes(storage);
  EXPECT_FALSE(editor_.scene_documents().autosave_directory().ok());
  EXPECT_EQ(autosave_test_bytes(storage), before);
}

TEST_F(ProjectAutosave, BrowserNamesShowSceneAndUtcTimeTogetherInTheRootFolder) {
  create_box();
  ASSERT_TRUE(editor_.scene_documents()
                  .save_as(project_.active_scene_id(),
                           autosave_test_path("Scenes/Robot Head.qdr"))
                  .ok());
  create_box("Later Edit");
  EXPECT_EQ(poll(0s).written, 0u);
  ASSERT_EQ(poll(120s).written, 1u);
  const auto saved = entries();
  ASSERT_EQ(saved.size(), 1u);
  EXPECT_EQ(saved.front().name, "Robot Head");
  EXPECT_EQ(saved.front().path.native_path().parent_path(),
            std::filesystem::path(".quader/autosave"));
  EXPECT_TRUE(std::regex_match(saved.front().path.native_path().filename().string(),
      std::regex("Robot Head_[0-9]{4}-[0-9]{2}-[0-9]{2}_[0-9]{2}-[0-9]{2}-[0-9]{2}-[0-9]{3}Z_"
                 "[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\\.qdr")));
}

TEST_F(ProjectAutosave, BrowserNamesSanitizeWindowsCharactersAndBoundLongNames) {
  const auto box = create_box();
  // Inject hostile display names at the storage boundary; public Save As already
  // rejects these path characters and cannot be used to create this fixture.
  project_.active_scene()->display_name = "CON.bad<>:\"/\\|?*\n";
  EXPECT_EQ(poll(0s).written, 0u);
  ASSERT_EQ(poll(120s).written, 1u);
  auto saved = entries();
  ASSERT_EQ(saved.size(), 1u);
  auto filename = saved.front().path.native_path().filename().string();
  EXPECT_TRUE(filename.starts_with("CON_bad"));
  EXPECT_EQ(filename.find_first_of("<>:\"/\\|?*\n"), std::string::npos);
  EXPECT_EQ(saved.front().name, project_.active_scene()->display_name);
  project_.active_scene()->display_name = std::string(300u, 'A');
  ASSERT_TRUE(editor_.scene().rename(box.entity, "Long Name Edit").ok());
  ASSERT_EQ(poll(240s).written, 1u);
  saved = entries();
  ASSERT_EQ(saved.size(), 2u);
  filename = saved.front().path.native_path().filename().string();
  EXPECT_TRUE(filename.starts_with(std::string(64u, 'A') + "_"));
  EXPECT_LT(filename.size(), 150u);
}

TEST_F(ProjectAutosave, UnicodeSavedSceneNamesRemainReadableAndRecoverByNativePath) {
  const auto box = create_box("Unicode Authored Geometry");
  const std::filesystem::path authored_native(L"Scenes/Caf\u00e9 \u6771\u4eac.qdr");
  const std::string scene_name = "Caf\xc3\xa9 \xe6\x9d\xb1\xe4\xba\xac";
  const auto authored = ProjectRelativePath::parse(authored_native);
  ASSERT_TRUE(authored.ok()) << authored.error().message;
  QuaderApiVoidResult save;
  ASSERT_NO_THROW(save = editor_.scene_documents().save_as_native_path(
      project_.active_scene_id(), root_ / authored_native));
  ASSERT_TRUE(save.ok()) << save.error_message();
  ASSERT_EQ(project_.active_scene()->display_name, scene_name);
  const auto authored_bytes = autosave_test_bytes(root_ / authored_native);
  ASSERT_TRUE(editor_.scene().rename(box.entity, "Unicode Recovered Edit").ok());
  EXPECT_EQ(poll(0s).written, 0u);
  // An unrelated Unicode folder must be ignored by the legacy UUID-folder scan.
  std::filesystem::create_directories(
      root_ / ".quader/autosave" / std::filesystem::path(L"\u6771\u4eac"));
  ProjectAutosaveResult copied;
  ASSERT_NO_THROW(copied = poll(120s));
  ASSERT_TRUE(copied.ok);
  ASSERT_EQ(copied.written, 1u);
  std::vector<QuaderAutosaveInfo> saved;
  ASSERT_NO_THROW(saved = entries());
  ASSERT_EQ(saved.size(), 1u);
  EXPECT_EQ(saved.front().name, scene_name);
  EXPECT_EQ(saved.front().source_path, authored.value());
  EXPECT_TRUE(saved.front().path.native_path().filename().wstring().starts_with(
      L"Caf\u00e9 \u6771\u4eac_"));

  ProjectSession reopened;
  ProjectSessionResult opened;
  ASSERT_NO_THROW(opened = reopened.open(project_.manifest_path()));
  ASSERT_TRUE(opened.ok) << opened.message;
  QuaderEditorApi recovered_editor(reopened);
  QuaderApiResult<std::vector<QuaderAutosaveInfo>> discovered;
  ASSERT_NO_THROW(discovered = recovered_editor.scene_documents().autosaves());
  ASSERT_TRUE(discovered.ok()) << discovered.error_message();
  ASSERT_EQ(discovered.value().size(), 1u);
  EXPECT_EQ(discovered.value().front().name, scene_name);
  QuaderApiResult<SceneSessionId> recovered;
  ASSERT_NO_THROW(recovered = recovered_editor.scene_documents().recover_autosave_native_path(
      native(saved.front().path)));
  ASSERT_TRUE(recovered.ok()) << recovered.error_message();
  EXPECT_TRUE(reopened.active_scene()->dirty());
  EXPECT_FALSE(reopened.active_scene()->path.has_value());
  const auto objects = recovered_editor.query().modeling_objects();
  ASSERT_EQ(objects.size(), 1u);
  const auto entity = recovered_editor.query().entity(objects.front().entity);
  ASSERT_TRUE(entity.has_value());
  EXPECT_EQ(entity->name, "Unicode Recovered Edit");
  EXPECT_EQ(autosave_test_bytes(root_ / authored_native), authored_bytes);
}

TEST_F(ProjectAutosave, LegacyCopiesRemainDiscoverableAndRecoverableAfterReopen) {
  create_box("Legacy Recovery Geometry");
  EXPECT_EQ(poll(0s).written, 0u);
  ASSERT_EQ(poll(120s).written, 1u);
  const auto saved = entries();
  ASSERT_EQ(saved.size(), 1u);
  const auto legacy_path = move_to_legacy_layout(saved.front());
  ProjectSession reopened;
  ASSERT_TRUE(reopened.open(project_.manifest_path()).ok);
  QuaderEditorApi recovered_editor(reopened);
  const auto found = recovered_editor.scene_documents().autosaves();
  ASSERT_TRUE(found.ok()) << found.error_message();
  ASSERT_EQ(found.value().size(), 1u);
  EXPECT_EQ(found.value().front().path, legacy_path);
  const auto recovered = recovered_editor.scene_documents().recover_autosave(legacy_path);
  ASSERT_TRUE(recovered.ok()) << recovered.error_message();
  EXPECT_TRUE(reopened.active_scene()->dirty());
  EXPECT_FALSE(reopened.active_scene()->path.has_value());
  const auto objects = recovered_editor.query().modeling_objects();
  ASSERT_EQ(objects.size(), 1u);
  const auto entity = recovered_editor.query().entity(objects.front().entity);
  ASSERT_TRUE(entity.has_value());
  EXPECT_EQ(entity->name, "Legacy Recovery Geometry");
}

TEST_F(ProjectAutosave, RetentionCountsLegacyAndFlatCopiesByTheirSceneIdentity) {
  const auto box = create_box();
  EXPECT_EQ(poll(0s).written, 0u);
  ASSERT_EQ(poll(120s).written, 1u);
  const auto saved = entries();
  ASSERT_EQ(saved.size(), 1u);
  const auto legacy_path = move_to_legacy_layout(saved.front());
  settings_.autosave_max_files = 1;
  ASSERT_TRUE(editor_.scene().rename(box.entity, "New Flat Copy").ok());
  ASSERT_EQ(poll(240s).written, 1u);
  const auto retained = entries();
  ASSERT_EQ(retained.size(), 1u);
  EXPECT_EQ(retained.front().scene_id, saved.front().scene_id);
  EXPECT_EQ(retained.front().path.native_path().parent_path(),
            std::filesystem::path(".quader/autosave"));
  EXPECT_FALSE(std::filesystem::exists(native(legacy_path)));
  auto legacy_metadata = native(legacy_path);
  legacy_metadata.replace_extension(".json");
  EXPECT_FALSE(std::filesystem::exists(legacy_metadata));
}

TEST_F(ProjectAutosave, RenamedOrMismatchedMetadataCannotBeRecovered) {
  create_box();
  EXPECT_EQ(poll(0s).written, 0u);
  ASSERT_EQ(poll(120s).written, 1u);
  const auto saved = entries();
  ASSERT_EQ(saved.size(), 1u);
  auto metadata_path = native(saved.front().path);
  metadata_path.replace_extension(".json");
  nlohmann::ordered_json metadata;
  {
    std::ifstream input(metadata_path, std::ios::binary);
    metadata = nlohmann::ordered_json::parse(input);
  }
  const auto original_metadata = metadata;
  metadata["snapshot_id"] = SceneId::generate().str();
  {
    std::ofstream output(metadata_path, std::ios::binary | std::ios::trunc);
    output << metadata.dump(2);
  }
  const auto id = project_.active_scene_id();
  const auto stamp = project_.active_scene()->session->content_revision_stamp();
  EXPECT_TRUE(entries().empty());
  EXPECT_FALSE(editor_.scene_documents()
                   .recover_autosave_native_path(native(saved.front().path)).ok());
  EXPECT_EQ(project_.active_scene_id(), id);
  EXPECT_EQ(project_.active_scene()->session->content_revision_stamp(), stamp);
  {
    std::ofstream output(metadata_path, std::ios::binary | std::ios::trunc);
    output << original_metadata.dump(2);
  }
  ASSERT_EQ(entries().size(), 1u);
  auto renamed = native(saved.front().path).parent_path() /
                 ("Other Scene_" + native(saved.front().path).filename().string());
  std::filesystem::rename(native(saved.front().path), renamed);
  auto renamed_metadata = renamed;
  renamed_metadata.replace_extension(".json");
  std::filesystem::rename(metadata_path, renamed_metadata);
  EXPECT_TRUE(entries().empty());
  EXPECT_FALSE(editor_.scene_documents().recover_autosave_native_path(renamed).ok());
  EXPECT_EQ(project_.active_scene_id(), id);
  EXPECT_EQ(project_.active_scene()->session->content_revision_stamp(), stamp);
}

TEST_F(ProjectAutosave, CleanEmptyAndAuthoredScenesDoNotProduceRecoveryCopies) {
  EXPECT_FALSE(project_.active_scene()->dirty());
  EXPECT_EQ(poll(0s).written, 0u);
  EXPECT_EQ(poll(120s).written, 0u);
  create_box();
  ASSERT_TRUE(editor_.scene_documents()
                  .save_as(project_.active_scene_id(),
                           autosave_test_path("Scenes/Clean.qdr"))
                  .ok());
  ASSERT_FALSE(project_.active_scene()->dirty());
  EXPECT_EQ(poll(240s).written, 0u);
  EXPECT_TRUE(entries().empty());
}

TEST_F(ProjectAutosave, CopiesDirtyInactiveAndUnsavedScenesWithoutMutatingEither) {
  const auto first_box = create_box("Authored Box");
  const auto first_id = project_.active_scene_id();
  const auto authored = autosave_test_path("Scenes/Authored.qdr");
  ASSERT_TRUE(editor_.scene_documents().save_as(first_id, authored).ok());
  const auto authored_bytes = autosave_test_bytes(native(authored));
  ASSERT_TRUE(editor_.scene().rename(first_box.entity, "Uncommitted Name").ok());
  ASSERT_TRUE(editor_.selection().select_object(first_box).ok());
  auto *first = project_.find(first_id);
  ASSERT_NE(first, nullptr);
  const auto first_stamp = first->session->content_revision_stamp();
  const auto first_undo = first->session->history().undo_count();
  const auto first_redo = first->session->history().redo_count();
  const auto first_selection = editor_.query().selection();

  const auto second_id = editor_.scene_documents().new_scene();
  ASSERT_TRUE(second_id.ok());
  create_box("Unsaved Box");
  auto *second = project_.active_scene();
  ASSERT_NE(second, nullptr);
  const auto second_stamp = second->session->content_revision_stamp();
  const auto second_undo = second->session->history().undo_count();
  const auto generation = project_.activation_generation();
  EXPECT_EQ(poll(0s).written, 0u);
  const auto copied = poll(120s);
  ASSERT_TRUE(copied.ok);
  EXPECT_EQ(copied.written, 2u);
  const auto saved = entries();
  ASSERT_EQ(saved.size(), 2u);
  EXPECT_EQ(project_.active_scene_id(), second_id.value());
  EXPECT_EQ(project_.activation_generation(), generation);
  EXPECT_EQ(first->session->content_revision_stamp(), first_stamp);
  EXPECT_EQ(second->session->content_revision_stamp(), second_stamp);
  EXPECT_EQ(first->session->history().undo_count(), first_undo);
  EXPECT_EQ(first->session->history().redo_count(), first_redo);
  EXPECT_EQ(second->session->history().undo_count(), second_undo);
  EXPECT_TRUE(first->dirty());
  EXPECT_TRUE(second->dirty());
  EXPECT_EQ(first->path, authored);
  EXPECT_FALSE(second->path.has_value());
  EXPECT_EQ(autosave_test_bytes(native(authored)), authored_bytes);
  for (const auto &entry : saved) {
    const auto recovered =
        QdrDocumentReader::read_project_document(native(entry.path));
    ASSERT_TRUE(recovered.ok) << recovered.message;
    const auto &scene = recovered.document.scene();
    const auto entities = scene.entities();
    const auto expected_name = entry.scene_id == first->scene_id
                                   ? "Uncommitted Name"
                                   : "Unsaved Box";
    EXPECT_TRUE(std::any_of(entities.begin(), entities.end(), [&](EntityId id) {
      const auto *name = scene.name(id);
      return name != nullptr && name->name == expected_name;
    }));
    EXPECT_EQ(entry.source_path.has_value(), entry.scene_id == first->scene_id);
  }
  ASSERT_TRUE(editor_.scene_documents().activate(first_id).ok());
  const auto after_selection = editor_.query().selection();
  EXPECT_EQ(after_selection.active_object_id, first_selection.active_object_id);
  EXPECT_EQ(after_selection.selected_object_count,
            first_selection.selected_object_count);
  EXPECT_EQ(after_selection.selection_revision,
            first_selection.selection_revision);
}

TEST_F(ProjectAutosave, PausedTransformPreviewRetriesAfterItEndsWithoutCommitting) {
  const auto box = create_box();
  ASSERT_TRUE(editor_.selection().select_object(box).ok());
  EXPECT_EQ(poll(0s).written, 0u);
  ASSERT_TRUE(editor_.modeling().begin_transform_preview().ok());
  ASSERT_TRUE(editor_.modeling().update_transform_translate({3.0f, 0.0f, 0.0f}).ok());
  const auto stamp = project_.active_scene()->session->content_revision_stamp();
  const auto undo = project_.active_scene()->session->history().undo_count();
  const auto deferred = poll(120s);
  EXPECT_EQ(deferred.written, 0u);
  EXPECT_EQ(deferred.deferred, 1u);
  EXPECT_TRUE(entries().empty());
  EXPECT_TRUE(project_.active_scene()->session->modeling()
                  .can_cancel_active_preview_for_undo());
  EXPECT_EQ(project_.active_scene()->session->content_revision_stamp(), stamp);
  EXPECT_EQ(project_.active_scene()->session->history().undo_count(), undo);
  ASSERT_TRUE(editor_.modeling().cancel_transform_preview().ok());
  EXPECT_EQ(poll(120s).written, 0u);
  EXPECT_EQ(poll(121s).written, 1u);
}

TEST_F(ProjectAutosave, ExternalBusyDefersThenRetriesWithoutWaitingAnotherInterval) {
  create_box();
  EXPECT_EQ(poll(0s).written, 0u);
  const auto busy = poll(120s, true);
  EXPECT_EQ(busy.written, 0u);
  EXPECT_GE(busy.deferred, 1u);
  EXPECT_TRUE(entries().empty());
  EXPECT_EQ(poll(120s).written, 0u);
  EXPECT_EQ(poll(121s).written, 1u);
}

TEST_F(ProjectAutosave, DisabledAndChangedIntervalsResetTheSchedule) {
  create_box();
  settings_.autosave_enabled = false;
  EXPECT_EQ(poll(0s).written, 0u);
  EXPECT_EQ(poll(600s).written, 0u);
  settings_.autosave_enabled = true;
  EXPECT_EQ(poll(600s).written, 0u);
  EXPECT_EQ(poll(719s).written, 0u);
  settings_.autosave_interval_minutes = 1;
  EXPECT_EQ(poll(719s).written, 0u);
  EXPECT_EQ(poll(778s).written, 0u);
  EXPECT_EQ(poll(779s).written, 1u);
}

TEST_F(ProjectAutosave, RetentionKeepsLatestCopiesAtMinimumDefaultAndMaximumLimits) {
  const auto box = create_box();
  std::chrono::seconds elapsed{0};
  for (const int maximum : {1, 3, 20}) {
    SCOPED_TRACE(maximum);
    settings_.autosave_max_files = maximum;
    EXPECT_EQ(poll(elapsed).written, 0u);
    ProjectRelativePath previous;
    for (int index = 0; index <= maximum; ++index) {
      ASSERT_TRUE(editor_.scene()
                      .rename(box.entity, "Revision " + std::to_string(maximum) +
                                              ":" + std::to_string(index))
                      .ok());
      elapsed += 120s;
      ASSERT_EQ(poll(elapsed).written, 1u);
      const auto saved = entries();
      ASSERT_FALSE(saved.empty());
      EXPECT_LE(saved.size(), static_cast<std::size_t>(maximum));
      if (index == 0) {
        previous = saved.front().path;
      }
    }
    const auto retained = entries();
    EXPECT_EQ(retained.size(), static_cast<std::size_t>(maximum));
    EXPECT_FALSE(std::filesystem::exists(native(previous)));
    auto removed_metadata = native(previous);
    removed_metadata.replace_extension(".json");
    EXPECT_FALSE(std::filesystem::exists(removed_metadata));
    EXPECT_TRUE(std::is_sorted(
        retained.begin(), retained.end(), [](const auto &left, const auto &right) {
          return left.saved_at_unix_ms > right.saved_at_unix_ms;
        }));
  }
}

TEST_F(ProjectAutosave, LoweredRetentionPrunesUnchangedCopiesAtNextDueTime) {
  const auto box = create_box();
  EXPECT_EQ(poll(0s).written, 0u);
  for (int revision = 1; revision <= 3; ++revision) {
    ASSERT_TRUE(editor_.scene()
                    .rename(box.entity, "Revision " + std::to_string(revision))
                    .ok());
    ASSERT_EQ(poll(std::chrono::seconds(revision * 120)).written, 1u);
  }
  const auto before = entries();
  ASSERT_EQ(before.size(), 3u);
  settings_.autosave_max_files = 1;
  EXPECT_EQ(poll(400s).written, 0u);
  EXPECT_EQ(entries().size(), 3u);
  EXPECT_EQ(poll(479s).written, 0u);
  EXPECT_EQ(entries().size(), 3u);
  EXPECT_EQ(poll(480s).written, 0u);
  const auto after = entries();
  ASSERT_EQ(after.size(), 1u);
  EXPECT_EQ(after.front().path, before.front().path);
}

TEST_F(ProjectAutosave, RecoverySurvivesProjectReopenAndCreatesIndependentUnsavedTab) {
  const auto box = create_box("Authored");
  const auto source_id = project_.active_scene_id();
  const auto source_path = autosave_test_path("Scenes/Source.qdr");
  ASSERT_TRUE(editor_.scene_documents().save_as(source_id, source_path).ok());
  const auto source_scene_id = project_.active_scene()->scene_id;
  const auto authored_bytes = autosave_test_bytes(native(source_path));
  ASSERT_TRUE(editor_.scene().rename(box.entity, "Recovered Edit").ok());
  EXPECT_EQ(poll(0s).written, 0u);
  ASSERT_EQ(poll(120s).written, 1u);
  const auto saved = entries();
  ASSERT_EQ(saved.size(), 1u);

  ProjectSession reopened;
  const auto opened = reopened.open(project_.manifest_path());
  ASSERT_TRUE(opened.ok) << opened.message;
  QuaderEditorApi recovered_editor(reopened);
  const auto discovered = recovered_editor.scene_documents().autosaves();
  ASSERT_TRUE(discovered.ok()) << discovered.error_message();
  ASSERT_EQ(discovered.value().size(), 1u);
  EXPECT_EQ(discovered.value().front().path, saved.front().path);
  const auto tabs_before = reopened.open_scenes().size();
  const auto recovered = recovered_editor.scene_documents().recover_autosave_native_path(
      native(saved.front().path));
  ASSERT_TRUE(recovered.ok()) << recovered.error_message();
  EXPECT_EQ(reopened.open_scenes().size(), tabs_before + 1u);
  const auto info = recovered_editor.scene_documents().active_scene();
  ASSERT_TRUE(info.has_value());
  EXPECT_EQ(info->session_id, recovered.value());
  EXPECT_NE(info->scene_id, source_scene_id);
  EXPECT_TRUE(info->dirty);
  EXPECT_FALSE(info->path.has_value());
  const auto objects = recovered_editor.query().modeling_objects();
  ASSERT_EQ(objects.size(), 1u);
  const auto recovered_entity =
      recovered_editor.query().entity(objects.front().entity);
  ASSERT_TRUE(recovered_entity.has_value());
  EXPECT_EQ(recovered_entity->name, "Recovered Edit");
  EXPECT_FALSE(recovered_editor.query().history().can_undo);
  EXPECT_EQ(autosave_test_bytes(native(source_path)), authored_bytes);
  EXPECT_EQ(project_.active_scene_id(), source_id);
  EXPECT_TRUE(project_.active_scene()->dirty());
}

TEST_F(ProjectAutosave, InvalidRecoveryAndCorruptMetadataPreserveLiveScene) {
  const auto box = create_box();
  const auto authored = autosave_test_path("Scenes/Source.qdr");
  ASSERT_TRUE(editor_.scene_documents()
                  .save_as(project_.active_scene_id(), authored)
                  .ok());
  ASSERT_TRUE(editor_.scene().rename(box.entity, "Recoverable Edit").ok());
  EXPECT_EQ(poll(0s).written, 0u);
  ASSERT_EQ(poll(120s).written, 1u);
  const auto saved = entries();
  ASSERT_EQ(saved.size(), 1u);
  const auto id = project_.active_scene_id();
  const auto stamp = project_.active_scene()->session->content_revision_stamp();
  const auto undo = project_.active_scene()->session->history().undo_count();
  const auto tabs = project_.open_scenes().size();

  EXPECT_FALSE(editor_.scene_documents().recover_autosave(authored).ok());
  EXPECT_FALSE(editor_.scene_documents().recover_autosave_native_path(native(authored)).ok());
  EXPECT_FALSE(editor_.scene_documents()
                   .recover_autosave_native_path(root_.parent_path() / "outside.qdr").ok());
  EXPECT_FALSE(editor_.scene_documents()
                   .recover_autosave(autosave_test_path(".quader/autosave/missing.qdr"))
                   .ok());
  auto metadata = native(saved.front().path);
  metadata.replace_extension(".json");
  {
    std::ofstream invalid(metadata, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(invalid.is_open());
    invalid << "{ corrupt recovery metadata";
  }
  EXPECT_FALSE(editor_.scene_documents().recover_autosave(saved.front().path).ok());
  EXPECT_FALSE(editor_.scene_documents()
                   .recover_autosave_native_path(native(saved.front().path)).ok());
  const auto listed = editor_.scene_documents().autosaves();
  EXPECT_TRUE(!listed.ok() || listed.value().empty());
  EXPECT_EQ(project_.active_scene_id(), id);
  EXPECT_EQ(project_.open_scenes().size(), tabs);
  EXPECT_EQ(project_.active_scene()->session->content_revision_stamp(), stamp);
  EXPECT_EQ(project_.active_scene()->session->history().undo_count(), undo);
  EXPECT_TRUE(project_.active_scene()->dirty());
}

TEST_F(ProjectAutosave, CorruptSnapshotAndAuthoredAccessCannotReplaceLiveState) {
  create_box();
  EXPECT_EQ(poll(0s).written, 0u);
  ASSERT_EQ(poll(120s).written, 1u);
  const auto saved = entries();
  ASSERT_EQ(saved.size(), 1u);
  const auto id = project_.active_scene_id();
  const auto stamp = project_.active_scene()->session->content_revision_stamp();
  const auto tabs = project_.open_scenes().size();
  EXPECT_FALSE(editor_.scene_documents().open(saved.front().path).ok());
  EXPECT_FALSE(editor_.scene_documents().save_as(id, saved.front().path).ok());
  {
    std::ofstream corrupt(native(saved.front().path),
                          std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(corrupt.is_open());
    corrupt << "not a valid scene";
  }
  EXPECT_FALSE(editor_.scene_documents().recover_autosave(saved.front().path).ok());
  EXPECT_EQ(project_.active_scene_id(), id);
  EXPECT_EQ(project_.open_scenes().size(), tabs);
  EXPECT_EQ(project_.active_scene()->session->content_revision_stamp(), stamp);
}

TEST_F(ProjectAutosave, FailedWriteRetainsPreviousRecoveryAndRetriesNextInterval) {
  settings_.autosave_max_files = 1;
  const auto box = create_box();
  EXPECT_EQ(poll(0s).written, 0u);
  ASSERT_EQ(poll(120s).written, 1u);
  const auto saved = entries();
  ASSERT_EQ(saved.size(), 1u);
  const auto previous_bytes = autosave_test_bytes(native(saved.front().path));
  ASSERT_TRUE(editor_.scene().rename(box.entity, "Later Edit").ok());
  const auto stamp = project_.active_scene()->session->content_revision_stamp();
  const auto directory = native(saved.front().path).parent_path();
  auto backup = directory;
  backup += ".blocked-backup";
  std::error_code error;
  std::filesystem::rename(directory, backup, error);
  ASSERT_FALSE(error) << error.message();
  {
    std::ofstream blocker(directory, std::ios::binary);
    ASSERT_TRUE(blocker.is_open());
    blocker << "Recovery directory temporarily unavailable";
  }
  const auto failed = poll(240s);
  EXPECT_FALSE(failed.ok);
  EXPECT_EQ(failed.written, 0u);
  EXPECT_FALSE(failed.warnings.empty());
  EXPECT_EQ(project_.active_scene()->session->content_revision_stamp(), stamp);
  std::filesystem::remove(directory, error);
  ASSERT_FALSE(error) << error.message();
  std::filesystem::rename(backup, directory, error);
  ASSERT_FALSE(error) << error.message();
  EXPECT_EQ(autosave_test_bytes(native(saved.front().path)), previous_bytes);
  EXPECT_EQ(poll(240s).written, 0u);
  EXPECT_EQ(poll(241s).written, 0u);
  const auto retry = poll(360s);
  ASSERT_TRUE(retry.ok);
  EXPECT_EQ(retry.written, 1u);
  const auto after = entries();
  ASSERT_EQ(after.size(), 1u);
  EXPECT_NE(after.front().path, saved.front().path);
}

} // namespace
