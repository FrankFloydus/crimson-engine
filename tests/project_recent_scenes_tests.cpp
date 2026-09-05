////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"
#include "assets/default_material_catalog.hpp"
#include "editor/commands/document/rename_entity_command.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/modeling/services/primitive_creation_tool_service.hpp"
#include "editor/session/project_autosave_service.hpp"
#include "editor/session/project_session.hpp"
#include "io/qdr/qdr_document_reader.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <Windows.h>
#endif

using namespace quader;
using namespace std::chrono_literals;

namespace {

ProjectRelativePath recent_scene_path(std::string_view text) {
  auto parsed = ProjectRelativePath::parse_portable(text);
  EXPECT_TRUE(parsed.ok()) << parsed.error().message;
  return parsed.ok() ? parsed.take_value() : ProjectRelativePath{};
}

std::string recent_file_bytes(const std::filesystem::path &path) {
  std::ifstream stream(path, std::ios::binary);
  EXPECT_TRUE(stream.is_open()) << path.string();
  return {std::istreambuf_iterator<char>(stream),
          std::istreambuf_iterator<char>()};
}

struct SavedRecentScene final {
  SceneSessionId session_id;
  ProjectRelativePath path;
  EntityId entity = kInvalidEntityId;
};

#if defined(_WIN32)
/// Leaves the candidate readable while refusing atomic manifest replacement.
class RecentSceneManifestWriteLock final {
public:
  explicit RecentSceneManifestWriteLock(const std::filesystem::path &path)
      : handle_(::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                              nullptr)) {}

  ~RecentSceneManifestWriteLock() {
    if (ready()) {
      ::CloseHandle(handle_);
    }
  }

  [[nodiscard]] bool ready() const noexcept {
    return handle_ != INVALID_HANDLE_VALUE;
  }

private:
  HANDLE handle_ = INVALID_HANDLE_VALUE;
};
#endif

/// Exercises the public recent-scene workflows with an isolated user MRU file.
class ProjectRecentScenesTests : public ::testing::Test {
protected:
  ProjectRecentScenesTests()
      : root_(std::filesystem::temp_directory_path() /
              ("quader-recent-scenes-" + std::string(ProjectId::generate().str()))),
        storage_(root_ / "recent-scenes.json"), project_(storage_),
        editor_(project_) {}

  void SetUp() override {
    std::error_code error;
    std::filesystem::create_directories(root_ / "Current", error);
    ASSERT_FALSE(error) << error.message();
    project_.set_resource_root(std::filesystem::path(QUADER_SOURCE_DIR) /
                               "resources" / "modeling_assets");
    const auto created = editor_.project().create(root_ / "Current");
    ASSERT_TRUE(created.ok()) << created.error_message();
  }

  void TearDown() override {
    std::error_code ignored;
    std::filesystem::remove_all(root_, ignored);
  }

  SavedRecentScene save_scene(std::string name) {
    const auto scene = editor_.scene_documents().new_scene();
    EXPECT_TRUE(scene.ok()) << scene.error_message();
    const auto entity = editor_.scene().create_entity({.name = name});
    EXPECT_TRUE(entity.ok()) << entity.error_message();
    const auto path = recent_scene_path("Scenes/" + name + ".qdr");
    const auto saved = editor_.scene_documents().save_as(scene.value(), path);
    EXPECT_TRUE(saved.ok()) << saved.error_message();
    return {scene.value(), path, entity.value().id};
  }

  QuaderRecentScene closed_candidate() {
    std::error_code error;
    const auto candidate_root = root_ / "Candidate";
    std::filesystem::create_directory(candidate_root, error);
    EXPECT_FALSE(error) << error.message();
    ProjectSession candidate(storage_);
    QuaderEditorApi editor(candidate);
    EXPECT_TRUE(editor.project().create(candidate_root).ok());
    const auto target_id = candidate.active_scene_id();
    const auto target_path = recent_scene_path("Scenes/Requested.qdr");
    EXPECT_TRUE(editor.scene_documents().save_as(target_id, target_path).ok());
    const auto entries = editor.project().recent_scenes();
    EXPECT_FALSE(entries.empty());
    const auto target = entries.empty() ? QuaderRecentScene{} : entries.front();
    const auto other = editor.scene_documents().new_scene();
    EXPECT_TRUE(other.ok());
    EXPECT_TRUE(editor.scene_documents()
                    .save_as(other.value(), recent_scene_path("Scenes/Other.qdr"))
                    .ok());
    EXPECT_TRUE(editor.scene_documents().close(target_id).ok());
    return target;
  }

  std::vector<std::string> recent_paths() const {
    std::vector<std::string> paths;
    for (const auto &entry : editor_.project().recent_scenes()) {
      paths.push_back(entry.scene_path.portable_string());
    }
    return paths;
  }

  std::filesystem::path root_;
  std::filesystem::path storage_;
  ProjectSession project_;
  QuaderEditorApi editor_;
};

TEST_F(ProjectRecentScenesTests, SavesRetainFiveNewestScenesAcrossSessions) {
  for (int index = 0; index < 7; ++index) {
    save_scene("Scene" + std::to_string(index));
  }
  const std::vector<std::string> expected{
      "Scenes/Scene6.qdr", "Scenes/Scene5.qdr", "Scenes/Scene4.qdr",
      "Scenes/Scene3.qdr", "Scenes/Scene2.qdr"};
  EXPECT_EQ(recent_paths(), expected);
  ProjectSession restarted(storage_);
  QuaderEditorApi restarted_editor(restarted);
  const auto entries = restarted_editor.project().recent_scenes();
  ASSERT_EQ(entries.size(), expected.size());
  for (std::size_t index = 0; index < entries.size(); ++index) {
    EXPECT_EQ(entries[index].scene_path.portable_string(), expected[index]);
    EXPECT_TRUE(entries[index].available);
    EXPECT_EQ(entries[index].manifest_path, project_.manifest_path());
    EXPECT_FALSE(entries[index].scene_name.empty());
    EXPECT_FALSE(entries[index].project_name.empty());
  }
}

TEST_F(ProjectRecentScenesTests, ActivationDoesNotPromoteButRepeatedDirtyEditsDo) {
  const auto first = save_scene("First");
  ASSERT_TRUE(editor_.scene().rename(first.entity, "First dirty edit").ok());
  ASSERT_TRUE(project_.find(first.session_id)->dirty());
  const auto second = save_scene("Second");
  const auto before = recent_file_bytes(storage_);
  ASSERT_TRUE(editor_.scene_documents().activate(first.session_id).ok());
  EXPECT_EQ(recent_file_bytes(storage_), before);
  ASSERT_TRUE(editor_.scene().rename(first.entity, "Second dirty edit").ok());
  EXPECT_EQ(recent_paths(), (std::vector<std::string>{
                               "Scenes/First.qdr", "Scenes/Second.qdr"}));
  ASSERT_TRUE(editor_.scene_documents().activate(second.session_id).ok());
  EXPECT_EQ(recent_paths().front(), "Scenes/First.qdr");
  ASSERT_TRUE(editor_.scene().rename(second.entity, "Edited second").ok());
  EXPECT_EQ(recent_paths().front(), "Scenes/Second.qdr");
}

TEST_F(ProjectRecentScenesTests, UnsavedAndRecoveredTabsStayOutUntilExplicitSave) {
  ASSERT_TRUE(editor_.modeling()
                  .create_box({.min = {-1.0f, -1.0f, -1.0f},
                               .max = {1.0f, 1.0f, 1.0f}},
                              "Unsaved")
                  .ok());
  EXPECT_TRUE(recent_paths().empty());
  SessionPreferences preferences;
  const std::chrono::steady_clock::time_point epoch{};
  EXPECT_EQ(project_.autosave().poll(project_, preferences, epoch).written, 0u);
  ASSERT_EQ(project_.autosave().poll(project_, preferences, epoch + 120s).written,
            1u);
  const auto autosaves = editor_.scene_documents().autosaves();
  ASSERT_TRUE(autosaves.ok());
  ASSERT_EQ(autosaves.value().size(), 1u);
  EXPECT_TRUE(recent_paths().empty());
  const auto recovered = editor_.scene_documents().recover_autosave(
      autosaves.value().front().path);
  ASSERT_TRUE(recovered.ok()) << recovered.error_message();
  EXPECT_TRUE(project_.active_scene()->dirty());
  EXPECT_FALSE(project_.active_scene()->path.has_value());
  EXPECT_TRUE(recent_paths().empty());
  ASSERT_TRUE(editor_.scene_documents()
                  .save_as(recovered.value(), recent_scene_path("Scenes/Recovered.qdr"))
                  .ok());
  EXPECT_EQ(recent_paths(),
            (std::vector<std::string>{"Scenes/Recovered.qdr"}));
}

TEST_F(ProjectRecentScenesTests, OpenRecentLoadsClosedSceneAndReusesResidentTab) {
  const auto target = closed_candidate();
  const auto opened = editor_.project().open_recent_scene(target);
  ASSERT_TRUE(opened.ok()) << opened.error_message();
  ASSERT_TRUE(opened.changed());
  EXPECT_EQ(editor_.project().manifest_path(), target.manifest_path);
  ASSERT_NE(project_.active_scene(), nullptr);
  EXPECT_EQ(project_.active_scene()->path, target.scene_path);
  EXPECT_EQ(project_.active_scene()->scene_id, target.scene_id);
  const auto target_session = project_.active_scene_id();
  const auto count = project_.open_scenes().size();
  const auto other = editor_.scene_documents().new_scene();
  ASSERT_TRUE(other.ok());
  ASSERT_TRUE(editor_.project().open_recent_scene(target).ok());
  EXPECT_EQ(project_.active_scene_id(), target_session);
  EXPECT_EQ(project_.open_scenes().size(), count + 1u);
  EXPECT_EQ(recent_paths().front(), "Scenes/Requested.qdr");
}

TEST_F(ProjectRecentScenesTests, InvalidTargetPreservesWorkspaceAndActivePreview) {
  const auto target = closed_candidate();
  const auto box = editor_.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}}, "Preview");
  ASSERT_TRUE(box.ok());
  ASSERT_TRUE(editor_.selection().select_object(box.value()).ok());
  ASSERT_TRUE(editor_.modeling().begin_transform_preview().ok());
  ASSERT_TRUE(editor_.modeling().update_transform_translate({2.0f, 0.0f, 0.0f}).ok());
  auto *const active = project_.active_scene();
  const auto stamp = active->session->content_revision_stamp();
  const auto current_root = project_.root();
  const auto recent_before = recent_file_bytes(storage_);
  int prompts = 0;
  editor_.scene_documents().set_runtime_hooks({
      .confirm_dirty_scene = [&prompts](const QuaderSceneDocumentInfo &,
                                       std::string_view) {
        ++prompts;
        return QuaderDirtySceneDecision::Discard;
      }});
  const auto target_file = target.manifest_path.parent_path() /
                           target.scene_path.native_path();
  std::error_code error;
  ASSERT_TRUE(std::filesystem::remove(target_file, error));
  ASSERT_FALSE(error);
  for (const bool corrupt : {false, true}) {
    if (corrupt) {
      std::ofstream stream(target_file, std::ios::binary);
      stream << "not a QDR scene";
    }
    const auto result = editor_.project().open_recent_scene(target);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(prompts, 0);
    EXPECT_EQ(project_.root(), current_root);
    EXPECT_EQ(project_.active_scene(), active);
    EXPECT_EQ(active->session->content_revision_stamp(), stamp);
    EXPECT_TRUE(editor_.modeling().can_cancel_active_tool_or_preview());
    EXPECT_EQ(recent_file_bytes(storage_), recent_before);
  }
}

TEST_F(ProjectRecentScenesTests, CanceledReplacementAndSavePickerPreserveWorkspace) {
  const auto target = closed_candidate();
  ASSERT_TRUE(editor_.scene().create_entity({.name = "Keep unsaved"}).ok());
  auto *const active = project_.active_scene();
  const auto current_root = project_.root();
  const auto recent_before = recent_file_bytes(storage_);
  int prompts = 0;
  int save_pickers = 0;
  auto decision = QuaderDirtySceneDecision::Cancel;
  editor_.scene_documents().set_runtime_hooks({
      .choose_save_scene_as_path = [&save_pickers](SceneSessionId)
          -> std::optional<std::filesystem::path> {
        ++save_pickers;
        return std::nullopt;
      },
      .confirm_dirty_scene = [&prompts, &decision](const QuaderSceneDocumentInfo &,
                                                 std::string_view action) {
        EXPECT_EQ(action, "project.open_recent_scene");
        ++prompts;
        return decision;
      }});
  for (const auto next : {QuaderDirtySceneDecision::Cancel,
                          QuaderDirtySceneDecision::Save}) {
    decision = next;
    const auto result = editor_.project().open_recent_scene(target);
    EXPECT_TRUE(result.ok()) << result.error_message();
    EXPECT_FALSE(result.changed());
    EXPECT_EQ(project_.root(), current_root);
    EXPECT_EQ(project_.active_scene(), active);
    EXPECT_TRUE(active->dirty());
    EXPECT_EQ(recent_file_bytes(storage_), recent_before);
  }
  EXPECT_EQ(prompts, 2);
  EXPECT_EQ(save_pickers, 1);
}

TEST_F(ProjectRecentScenesTests, DirtyMaterialPreflightCanCancelRecentOpen) {
  const auto target = closed_candidate();
  const auto material = ProjectAssetId::parse(kDefaultProjectMaterialId);
  ASSERT_TRUE(material.has_value());
  ASSERT_TRUE(editor_.standard_material_documents().open(*material).ok());
  ASSERT_TRUE(editor_.standard_material_documents().set_metallic(0.63f).ok());
  ASSERT_TRUE(project_.has_dirty_material());
  const auto current_root = project_.root();
  const auto recent_before = recent_file_bytes(storage_);
  int prompts = 0;
  editor_.project().set_runtime_hooks({
      .prepare_material_document_close = [&prompts](std::string_view action) {
        EXPECT_EQ(action, "project.open_recent_scene");
        ++prompts;
        return false;
      }});
  const auto result = editor_.project().open_recent_scene(target);
  EXPECT_FALSE(result.ok());
  EXPECT_FALSE(result.changed());
  EXPECT_EQ(prompts, 1);
  EXPECT_EQ(project_.root(), current_root);
  EXPECT_TRUE(project_.has_dirty_material());
  EXPECT_EQ(recent_file_bytes(storage_), recent_before);
}

TEST_F(ProjectRecentScenesTests, RecentStoreFailureDoesNotUndoSuccessfulSceneSave) {
  std::error_code error;
  ASSERT_TRUE(std::filesystem::create_directory(storage_, error));
  ASSERT_FALSE(error);
  const auto saved = save_scene("SavedDespiteRecentStoreFailure");
  ASSERT_FALSE(project_.find(saved.session_id)->dirty());
  const auto original = editor_.query().entity(saved.entity);
  ASSERT_TRUE(original.has_value());
  const auto read = QdrDocumentReader::read_project_document(
      project_.root() / saved.path.native_path());
  ASSERT_TRUE(read.ok) << read.message;
  const auto persisted_id = read.document.scene().entity_for_uuid(original->uuid);
  ASSERT_NE(persisted_id, kInvalidEntityId);
  const auto *persisted_name = read.document.scene().name(persisted_id);
  ASSERT_NE(persisted_name, nullptr);
  EXPECT_EQ(persisted_name->name, "SavedDespiteRecentStoreFailure");
  EXPECT_TRUE(recent_paths().empty());
  ASSERT_TRUE(std::filesystem::remove(storage_, error));
  ASSERT_FALSE(error);
  const auto retried = editor_.scene_documents().save(saved.session_id);
  ASSERT_TRUE(retried.ok()) << retried.error_message();
  EXPECT_FALSE(project_.find(saved.session_id)->dirty());
  EXPECT_EQ(recent_paths(), (std::vector<std::string>{
                               "Scenes/SavedDespiteRecentStoreFailure.qdr"}));
}

TEST_F(ProjectRecentScenesTests, TransactionPromotesOnlyAfterWholeCommit) {
  const auto first = save_scene("First");
  save_scene("Second");
  ASSERT_TRUE(editor_.scene_documents().activate(first.session_id).ok());
  auto &session = *project_.active_scene()->session;
  auto &history = session.history();
  const auto before = recent_file_bytes(storage_);
  const auto count = history.undo_count();
  {
    auto transaction = history.begin_transaction("Canceled rename");
    ASSERT_TRUE(transaction.execute(std::make_unique<RenameEntityCommand>(
        session.scene(), first.entity, "First", "Provisional")).ok);
    EXPECT_EQ(recent_file_bytes(storage_), before);
    ASSERT_TRUE(transaction.cancel().ok);
    EXPECT_EQ(recent_file_bytes(storage_), before);
  }
  auto transaction = history.begin_transaction("Two renames");
  ASSERT_TRUE(transaction.execute(std::make_unique<RenameEntityCommand>(
      session.scene(), first.entity, "First", "Intermediate")).ok);
  ASSERT_TRUE(transaction.execute(std::make_unique<RenameEntityCommand>(
      session.scene(), first.entity, "Intermediate", "Committed")).ok);
  EXPECT_EQ(recent_file_bytes(storage_), before);
  ASSERT_TRUE(transaction.commit().ok);
  EXPECT_EQ(history.undo_count(), count + 1u);
  EXPECT_EQ(recent_paths(), (std::vector<std::string>{
                               "Scenes/First.qdr", "Scenes/Second.qdr"}));
  ASSERT_TRUE(editor_.query().entity(first.entity).has_value());
  EXPECT_EQ(editor_.query().entity(first.entity)->name, "Committed");
}

TEST_F(ProjectRecentScenesTests, ManifestWriteFailurePrecedesDestructivePreflight) {
#if !defined(_WIN32)
  GTEST_SKIP() << "The manifest sharing lock is Windows-specific.";
#else
  const auto target = closed_candidate();
  const auto box = editor_.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}}, "Keep preview");
  ASSERT_TRUE(box.ok());
  ASSERT_TRUE(editor_.selection().select_object(box.value()).ok());
  ASSERT_TRUE(editor_.modeling().begin_transform_preview().ok());
  ASSERT_TRUE(editor_.modeling().update_transform_translate({2.0f, 0.0f, 0.0f}).ok());
  const auto material = ProjectAssetId::parse(kDefaultProjectMaterialId);
  ASSERT_TRUE(material.has_value());
  ASSERT_TRUE(editor_.standard_material_documents().open(*material).ok());
  ASSERT_TRUE(editor_.standard_material_documents().set_metallic(0.63f).ok());
  ASSERT_TRUE(project_.has_dirty_material());
  auto *const active = project_.active_scene();
  const auto stamp = active->session->content_revision_stamp();
  const auto original_root = project_.root();
  const auto recent_before = recent_file_bytes(storage_);
  int scene_prompts = 0;
  int material_prompts = 0;
  editor_.scene_documents().set_runtime_hooks({
      .confirm_dirty_scene = [&scene_prompts](const QuaderSceneDocumentInfo &,
                                             std::string_view) {
        ++scene_prompts;
        return QuaderDirtySceneDecision::Discard;
      }});
  editor_.project().set_runtime_hooks({
      .prepare_material_document_close = [this, &material_prompts](std::string_view) {
        ++material_prompts;
        return editor_.standard_material_documents().close(true).ok();
      }});
  RecentSceneManifestWriteLock lock(target.manifest_path);
  ASSERT_TRUE(lock.ready());
  const auto result = editor_.project().open_recent_scene(target);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(scene_prompts, 0);
  EXPECT_EQ(material_prompts, 0);
  EXPECT_EQ(project_.root(), original_root);
  EXPECT_EQ(project_.active_scene(), active);
  EXPECT_EQ(active->session->content_revision_stamp(), stamp);
  EXPECT_TRUE(editor_.modeling().can_cancel_active_tool_or_preview());
  EXPECT_TRUE(project_.has_dirty_material());
  EXPECT_EQ(recent_file_bytes(storage_), recent_before);
#endif
}

TEST_F(ProjectRecentScenesTests, PrimitiveApplyPromotesWithHistoryDisabled) {
  const auto first = save_scene("First");
  save_scene("Second");
  project_.set_history_max_steps(0u);
  ASSERT_TRUE(editor_.scene_documents().activate(first.session_id).ok());
  ASSERT_TRUE(editor_.tools().activate(ViewportToolId::CreatePrimitive).ok());
  ModelingBoxPreview preview;
  preview.ok = true;
  preview.valid = true;
  preview.drag_state = ModelingBoxDragState::Height;
  preview.corners = {
      QVec3{-1.0f, 0.0f, -1.0f}, QVec3{-1.0f, 0.0f, 1.0f},
      QVec3{1.0f, 0.0f, 1.0f}, QVec3{1.0f, 0.0f, -1.0f},
      QVec3{-1.0f, 2.0f, -1.0f}, QVec3{-1.0f, 2.0f, 1.0f},
      QVec3{1.0f, 2.0f, 1.0f}, QVec3{1.0f, 2.0f, -1.0f},
  };
  preview.corner_count = preview.corners.size();
  ASSERT_TRUE(project_.active_scene()->session->primitive_creation()
                  .accept_preview(preview, "Committed primitive")
                  .ok);
  EXPECT_EQ(recent_paths().front(), "Scenes/Second.qdr");
  ASSERT_TRUE(editor_.tools().commit_active_tool_preview().ok());
  EXPECT_EQ(recent_paths().front(), "Scenes/First.qdr");
  EXPECT_EQ(project_.active_scene()->session->history().undo_count(), 0u);
  EXPECT_TRUE(project_.active_scene()->dirty());
}

TEST_F(ProjectRecentScenesTests, CanceledPreviewThenSelectionDoesNotPromoteScene) {
  const auto box = editor_.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}}, "First");
  ASSERT_TRUE(box.ok());
  const auto first = project_.active_scene_id();
  ASSERT_TRUE(editor_.scene_documents()
                  .save_as(first, recent_scene_path("Scenes/First.qdr"))
                  .ok());
  save_scene("Second");
  ASSERT_TRUE(editor_.scene_documents().activate(first).ok());
  ASSERT_TRUE(editor_.selection().select_object(box.value()).ok());
  const auto recent_before = recent_file_bytes(storage_);
  ASSERT_TRUE(editor_.modeling().begin_transform_preview().ok());
  ASSERT_TRUE(editor_.modeling().update_transform_translate({3.0f, 0.0f, 0.0f}).ok());
  EXPECT_EQ(recent_file_bytes(storage_), recent_before);
  ASSERT_TRUE(editor_.modeling().cancel_transform_preview().ok());
  EXPECT_EQ(recent_file_bytes(storage_), recent_before)
      << "Canceling the provisional transform must not promote the scene.";
  ASSERT_TRUE(editor_.selection().clear().ok());
  EXPECT_EQ(recent_file_bytes(storage_), recent_before)
      << "Clearing selection after Cancel must not promote the scene.";
  ASSERT_TRUE(editor_.selection().select_object(box.value()).ok());
  EXPECT_EQ(recent_file_bytes(storage_), recent_before)
      << "Selecting the object after Cancel must not promote the scene.";
  EXPECT_EQ(recent_paths().front(), "Scenes/Second.qdr");
}

TEST_F(ProjectRecentScenesTests, UnicodeSceneAndProjectNamesSurviveRecentReopen) {
#if !defined(_WIN32)
  GTEST_SKIP() << "Exercises native Windows UTF-16 filesystem paths.";
#else
  const auto unicode_root = root_ / L"Caf\u00e9_\u6a21\u578b";
  const std::filesystem::path native_relative =
      L"Scenes/Sc\u00e8ne_\u6a21\u578b.qdr";
  std::error_code error;
  ASSERT_TRUE(std::filesystem::create_directory(unicode_root, error));
  ASSERT_FALSE(error) << error.message();
  ProjectSession project(storage_);
  QuaderEditorApi editor(project);
  QuaderApiVoidResult created;
  ASSERT_NO_THROW(created = editor.project().create(unicode_root))
      << "Creating a project with a Unicode root.";
  ASSERT_TRUE(created.ok()) << created.error_message();
  const auto authored = editor.scene().create_entity({.name = "Persisted entity"});
  ASSERT_TRUE(authored.ok());
  const auto relative = ProjectRelativePath::parse(native_relative);
  ASSERT_TRUE(relative.ok()) << relative.error().message;
  QuaderApiVoidResult saved;
  ASSERT_NO_THROW(saved = editor.scene_documents().save_as(
                      project.active_scene_id(), relative.value()))
      << "Saving a scene with a Unicode file name.";
  ASSERT_TRUE(saved.ok()) << saved.error_message();
  std::vector<QuaderRecentScene> entries;
  ASSERT_NO_THROW(entries = editor.project().recent_scenes())
      << "Reading the persisted Unicode recent-scene entry.";
  ASSERT_EQ(entries.size(), 1u);
  const auto &entry = entries.front();
  const std::u8string project_name_utf8 = u8"Caf\u00e9_\u6a21\u578b";
  const std::u8string scene_name_utf8 = u8"Sc\u00e8ne_\u6a21\u578b";
  const std::string expected_project_name(
      reinterpret_cast<const char *>(project_name_utf8.data()),
      project_name_utf8.size());
  const std::string expected_scene_name(
      reinterpret_cast<const char *>(scene_name_utf8.data()),
      scene_name_utf8.size());
  EXPECT_EQ(entry.project_name, expected_project_name);
  EXPECT_EQ(entry.scene_name, expected_scene_name);
  EXPECT_EQ(entry.scene_path.native_path(), native_relative);
  EXPECT_EQ(entry.manifest_path, project.manifest_path());
  EXPECT_TRUE(entry.available);
  QuaderApiVoidResult closed;
  ASSERT_NO_THROW(closed = editor.project().close())
      << "Closing the project with a Unicode root.";
  ASSERT_TRUE(closed.ok()) << closed.error_message();
  ASSERT_FALSE(editor.project().has_project());
  QuaderApiVoidResult opened;
  ASSERT_NO_THROW(opened = editor.project().open_recent_scene(entry))
      << "Reopening the persisted Unicode recent-scene entry.";
  ASSERT_TRUE(opened.ok()) << opened.error_message();
  EXPECT_EQ(project.root(), unicode_root);
  ASSERT_NE(project.active_scene(), nullptr);
  EXPECT_EQ(project.active_scene()->path, relative.value());
  EXPECT_EQ(project.active_scene()->scene_id, entry.scene_id);
  EXPECT_EQ(project.active_scene()->display_name, expected_scene_name);
  EXPECT_TRUE(std::filesystem::is_regular_file(
      project.root() / project.active_scene()->path->native_path()));
  const auto &reopened_scene = project.active_scene()->session->scene();
  const auto reopened_entity = reopened_scene.entity_for_uuid(authored.value().uuid);
  ASSERT_NE(reopened_entity, kInvalidEntityId);
  const auto *reopened_name = reopened_scene.name(reopened_entity);
  ASSERT_NE(reopened_name, nullptr);
  EXPECT_EQ(reopened_name->name, "Persisted entity");
#endif
}

TEST_F(ProjectRecentScenesTests, DestroyingActiveTransformDragDetachesContentObserver) {
  int settled_callbacks = 0;
  auto project = std::make_unique<ProjectSession>(storage_);
  const auto teardown_root = root_ / "Teardown";
  std::error_code error;
  ASSERT_TRUE(std::filesystem::create_directory(teardown_root, error));
  ASSERT_FALSE(error) << error.message();
  {
    QuaderEditorApi editor(*project);
    ASSERT_TRUE(editor.project().create(teardown_root).ok());
    const auto box = editor.modeling().create_box(
        {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        "Unfinished drag");
    ASSERT_TRUE(box.ok());
    ASSERT_TRUE(editor.selection().select_object(box.value()).ok());
    const QuaderViewportCamera camera{
        .orthographic = true,
        .origin = {0.0f, 0.0f, 10.0f},
        .x_axis = {1.0f, 0.0f, 0.0f},
        .y_axis = {0.0f, 1.0f, 0.0f},
        .z_axis = {0.0f, 0.0f, 1.0f},
        .viewport_size = {100.0f, 100.0f},
        .orthographic_size = 100.0,
        .fov_degrees = 60.0,
    };
    const QuaderRay ray{
        .origin = {0.0f, 0.0f, 10.0f},
        .direction = {0.0f, 0.0f, -1.0f},
    };
    const auto drag = editor.modeling().begin_transform_drag({
        .tool = QuaderTransformTool::Move,
        .operation = QuaderTransformOperation::Transform,
        .axis = QuaderTransformAxis::X,
        .screen_position = {0.0f, 0.0f},
        .screen_pivot = {0.0f, 0.0f},
        .screen_tip = {10.0f, 0.0f},
        .has_world_direction = true,
        .world_direction = {1.0f, 0.0f, 0.0f},
        .ray = ray,
        .camera = camera,
        .grid_size = 1.0,
        .vertex_snap_radius = 0.0,
        .grid_snap_enabled = false,
    });
    ASSERT_TRUE(drag.ok()) << drag.error_message();
    ASSERT_TRUE(drag.value().active);
    const auto updated = editor.modeling().update_transform_drag({
        .screen_position = {2.0f, 0.0f},
        .ray = ray,
        .camera = camera,
        .grid_size = 1.0,
        .vertex_snap_radius = 0.0,
        .grid_snap_enabled = false,
    });
    ASSERT_TRUE(updated.ok()) << updated.error_message();
    ASSERT_TRUE(updated.value().changed);
    ASSERT_TRUE(project->active_scene()->session->modeling().transform_drag_active());
    project->active_scene()->session->set_content_settled_observer(
        [&settled_callbacks](bool) { ++settled_callbacks; });
  }
  ASSERT_TRUE(project->active_scene()->session->modeling().transform_drag_active());
  EXPECT_EQ(settled_callbacks, 0);
  project.reset();
  EXPECT_EQ(settled_callbacks, 0)
      << "Member teardown must not publish content through a destroying owner.";
}

} // namespace
