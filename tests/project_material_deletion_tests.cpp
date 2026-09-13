////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"
#include "assets/default_material_catalog.hpp"
#include "editor/material/project_asset_dependency_service.hpp"
#include "editor/material/project_material_deletion_service.hpp"
#include "editor/session/project_autosave_service.hpp"
#include "editor/session/project_session.hpp"
#include "io/project/project_manifest.hpp"
#include "io/project/qmat_codec.hpp"
#include "io/qdr/qdr_binary_container.hpp"
#include "io/qdr/qdr_document_reader.hpp"
#include "support/project_material_fixture.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <QImage>
#include <QString>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace quader {
namespace {

ProjectRelativePath deletion_path(std::string_view text) {
  auto result = ProjectRelativePath::parse_portable(text);
  EXPECT_TRUE(result.ok()) << result.error().message;
  return result.ok() ? result.take_value() : ProjectRelativePath{};
}

ProjectAssetId deletion_material_id() {
  return *ProjectAssetId::parse("48b82075-9ee6-4bec-8b8d-bdbbe41f21cd");
}

ProjectAssetId surviving_material_id() {
  return *ProjectAssetId::parse("562895b1-c4ea-4581-b154-931c3fe17d03");
}

ProjectAssetId sibling_material_id() {
  return *ProjectAssetId::parse("ca42ef4a-c6d4-4d20-85ea-ae859eb97012");
}

ProjectAssetId default_material_id() {
  return *ProjectAssetId::parse(kDefaultProjectMaterialId);
}

std::string deletion_read_bytes(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  EXPECT_TRUE(input.is_open()) << path.string();
  return {std::istreambuf_iterator<char>(input), {}};
}

void deletion_write_bytes(const std::filesystem::path &path,
                          std::string_view bytes) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  ASSERT_TRUE(output.is_open()) << path.string();
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  output.close();
  ASSERT_TRUE(output.good()) << path.string();
}

void expect_deletion_material(const SceneDocument &scene, EntityId entity,
                              ProjectAssetId expected) {
  const auto *material = scene.material(entity);
  ASSERT_NE(material, nullptr);
  EXPECT_EQ(material->material, MaterialReference::project(expected));
}

void expect_deletion_history(const QuaderHistorySnapshot &actual,
                             const QuaderHistorySnapshot &expected) {
  EXPECT_EQ(actual.can_undo, expected.can_undo);
  EXPECT_EQ(actual.can_redo, expected.can_redo);
  EXPECT_EQ(actual.undo_label, expected.undo_label);
  EXPECT_EQ(actual.redo_label, expected.redo_label);
}

nlohmann::json deletion_recovery_file(std::string path, std::string backup,
                                     std::string_view baseline,
                                     std::string_view replacement) {
  const auto hash = [](std::string_view bytes) {
    return quader_io::qdr_binary_content_hash(
        std::as_bytes(std::span(bytes.data(), bytes.size())));
  };
  return {{"path", std::move(path)}, {"backup", std::move(backup)},
          {"byte_count", baseline.size()}, {"content_hash", hash(baseline)},
          {"replacement_byte_count", replacement.size()},
          {"replacement_content_hash", hash(replacement)}};
}

/// Retains two independently published sources for recovery-prevalidation checks.
struct PreparedDeletionRecovery final {
  std::filesystem::path directory;
  std::string replacement_scene;
  std::string replacement_manifest;
};

/// Exercises public folder-deletion workflows against real QMAT and QDR files.
class ProjectMaterialDeletionTests : public ::testing::Test {
protected:
  void SetUp() override {
    std::filesystem::create_directories(root_);
    project_.set_resource_root(std::filesystem::path(QUADER_SOURCE_DIR) /
                               "resources" / "modeling_assets");
    const auto created = editor_.project().create(root_);
    ASSERT_TRUE(created.ok()) << created.error_message();
    manifest_ = editor_.project().manifest_path();
    ASSERT_TRUE(editor_.project().close().ok());
    std::filesystem::create_directories(root_ / "Textures");
    QImage texture(2, 2, QImage::Format_RGBA8888);
    texture.fill(0xffb47828u);
    ASSERT_TRUE(texture.save(
        QString::fromStdWString((root_ / "Textures/shared.png").wstring()),
        "PNG"));
    ASSERT_NO_FATAL_FAILURE(write_material(
        "Materials/Delete/wood.qmat", deletion_material_id(), "Wood"));
    ASSERT_NO_FATAL_FAILURE(write_material(
        "Materials/Keep/stone.qmat", surviving_material_id(), "Stone"));
    const auto opened = editor_.project().open(manifest_);
    ASSERT_TRUE(opened.ok()) << opened.error_message();
    if (!editor_.scene_documents().has_active_scene()) {
      ASSERT_TRUE(editor_.scene_documents().new_scene().ok());
    }
  }

  void TearDown() override {
    (void)project_.close(true);
    std::error_code ignored;
    std::filesystem::remove_all(root_, ignored);
  }

  void write_material(std::string_view path, ProjectAssetId id,
                      std::string name,
                      std::string texture_path = "Textures/shared.png") {
    ProjectMaterialDocument document{
        .id = id,
        .name = std::move(name),
        .kind = ProjectMaterialKind::Standard,
        .material_version = kStandardMaterialVersion,
        .payload = ProjectStandardMaterialDefinition{
            .base_color = {0.8f, 0.4f, 0.1f},
            .roughness = 0.65f,
            .base_color_texture = ProjectMaterialTextureSlot{
                .id = *ProjectAssetId::parse(
                    "179f551c-1298-420f-82ae-9fc014652c4b"),
                .project_relative_path = std::move(texture_path),
            },
        },
    };
    auto encoded = QmatCodec::encode(document);
    ASSERT_TRUE(encoded.ok()) << encoded.error().message;
    deletion_write_bytes(root_ / deletion_path(path).native_path(),
                         encoded.value());
  }

  QuaderApiResult<QuaderModelingObjectRef>
  create_box(std::string name, ProjectAssetId material = deletion_material_id()) {
    return editor_.modeling().create_box({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f},
                   .max = {1.0f, 1.0f, 1.0f}},
        .name = std::move(name),
        .material = QuaderMaterialReference::project(material),
    });
  }

  void save_active(std::string_view path = "Scenes/Saved.qdr") {
    const auto id = editor_.scene_documents().active_scene_id();
    ASSERT_TRUE(id.has_value());
    const auto saved =
        editor_.scene_documents().save_as(*id, deletion_path(path));
    ASSERT_TRUE(saved.ok()) << saved.error_message();
  }

  void delete_folder(std::string_view folder = "Materials/Delete") {
    const auto preview =
        editor_.project_assets().preview_delete_material_folder(folder);
    ASSERT_TRUE(preview.ok()) << preview.error_message();
    ASSERT_TRUE(preview.value().complete) << ::testing::PrintToString(preview.value().errors);
    const auto deleted = editor_.project_assets().delete_material_folder(
        folder, preview.value().revision);
    ASSERT_TRUE(deleted.ok()) << deleted.error_message();
    EXPECT_TRUE(deleted.changed());
  }

  void prepare_interrupted_deletion(PreparedDeletionRecovery &recovery) {
    const auto box = create_box("Original scene");
    ASSERT_TRUE(box.ok()) << box.error_message();
    ASSERT_NO_FATAL_FAILURE(save_active());
    const auto baseline_scene = deletion_read_bytes(root_ / "Scenes/Saved.qdr");
    ASSERT_TRUE(editor_.scene().rename(box.value().entity, "Replacement scene").ok());
    ASSERT_TRUE(editor_.scene_documents().save_active().ok());
    recovery.replacement_scene = deletion_read_bytes(root_ / "Scenes/Saved.qdr");
    ASSERT_TRUE(editor_.project().close().ok());
    const auto baseline_manifest = deletion_read_bytes(manifest_);
    auto workspace = ProjectManifestStore::open(manifest_);
    ASSERT_TRUE(workspace.ok()) << workspace.error().message;
    const std::array ids{deletion_material_id()};
    ASSERT_TRUE(workspace.value().manifest.mark_materials_deleted(ids).ok());
    ASSERT_TRUE(ProjectManifestStore::save_atomic(workspace.value()).ok());
    recovery.replacement_manifest = deletion_read_bytes(manifest_);
    recovery.directory = root_ / ".quader/material-deletions" /
        "2369a965-7c2c-4b48-b1b5-80a4b93a9078";
    std::filesystem::create_directories(recovery.directory);
    ASSERT_NO_FATAL_FAILURE(deletion_write_bytes(recovery.directory / "file-0.old",
                                                 baseline_scene));
    ASSERT_NO_FATAL_FAILURE(deletion_write_bytes(recovery.directory / "file-1.old",
                                                 baseline_manifest));
    std::filesystem::rename(root_ / "Materials/Delete", recovery.directory / "folder");
    const nlohmann::json journal{
        {"version", 1}, {"project", workspace.value().manifest.id().str()},
        {"folder", "Materials/Delete"}, {"state", "prepared"},
        {"files", nlohmann::json::array({
            deletion_recovery_file("Scenes/Saved.qdr", "file-0.old",
                                   baseline_scene, recovery.replacement_scene),
            deletion_recovery_file(manifest_.filename().generic_string(), "file-1.old",
                                   baseline_manifest, recovery.replacement_manifest)})}};
    ASSERT_NO_FATAL_FAILURE(deletion_write_bytes(recovery.directory / "journal.json",
                                                 journal.dump()));
  }

  std::filesystem::path root_ = std::filesystem::temp_directory_path() /
      ("quader-material-deletion-" + std::string(ProjectAssetId::generate().str()));
  ProjectSession project_{root_ / "recent-scenes.json"};
  QuaderEditorApi editor_{project_};
  std::filesystem::path manifest_;
};

TEST_F(ProjectMaterialDeletionTests,
       SavedAndDirtyLiveScenesKeepIndependentContentHistoryAndSelection) {
  const auto saved_box = create_box("Saved name");
  ASSERT_TRUE(saved_box.ok()) << saved_box.error_message();
  ASSERT_NO_FATAL_FAILURE(save_active());
  const auto saved_session = *editor_.scene_documents().active_scene_id();
  ASSERT_TRUE(editor_.scene().rename(saved_box.value().entity, "Unsaved name").ok());
  ASSERT_TRUE(editor_.object(saved_box.value()).select().ok());
  const auto saved_history = editor_.query().history();
  const auto saved_selection = project_.active_scene()->session->scene().selected_entities();
  const auto unsaved = editor_.scene_documents().new_scene();
  ASSERT_TRUE(unsaved.ok()) << unsaved.error_message();
  const auto hidden_box = create_box("Hidden dormant slots", default_material_id());
  ASSERT_TRUE(hidden_box.ok()) << hidden_box.error_message();
  ProjectMaterialFixture fixture(project_);
  ASSERT_TRUE(fixture.set_material(hidden_box.value().entity,
      {.material = MaterialReference::project(default_material_id()),
       .material_slots = {{91u, MaterialReference::project(deletion_material_id())}}}));
  ASSERT_TRUE(fixture.set_visible(hidden_box.value().entity, false));
  ASSERT_TRUE(fixture.set_locked(hidden_box.value().entity, true));
  const auto unsaved_history = editor_.query().history();
  const auto unsaved_selection = project_.active_scene()->session->scene().selected_entities();

  const auto graph = editor_.project_assets().material_dependencies(true);
  ASSERT_TRUE(graph.ok()) << graph.error_message();
  ASSERT_TRUE(graph.value().complete) << ::testing::PrintToString(graph.value().errors);
  EXPECT_EQ(std::ranges::count_if(graph.value().scenes, [&](const auto &scene) {
    return scene.project_relative_path == "Scenes/Saved.qdr";
  }), 2);
  EXPECT_TRUE(std::ranges::any_of(graph.value().scenes, [&](const auto &scene) {
    return scene.live && scene.session_id == unsaved.value() &&
           std::ranges::find(scene.material_ids, deletion_material_id()) !=
               scene.material_ids.end();
  }));
  const auto preview = editor_.project_assets().preview_delete_material_folder("Materials/Delete");
  ASSERT_TRUE(preview.ok());
  ASSERT_TRUE(preview.value().complete) << ::testing::PrintToString(preview.value().errors);
  EXPECT_EQ(preview.value().material_ids,
            std::vector<ProjectAssetId>{deletion_material_id()});
  EXPECT_EQ(preview.value().affected_scenes.size(), 3u);
  const auto removed = editor_.project_assets().delete_material_folder(
      "Materials/Delete", preview.value().revision);
  ASSERT_TRUE(removed.ok()) << removed.error_message();

  const auto *unsaved_scene = project_.find(unsaved.value());
  ASSERT_NE(unsaved_scene, nullptr);
  EXPECT_TRUE(unsaved_scene->dirty());
  const auto &hidden_scene = unsaved_scene->session->scene();
  const auto *hidden_material = hidden_scene.material(hidden_box.value().entity);
  ASSERT_NE(hidden_material, nullptr);
  EXPECT_EQ(hidden_material->material_slots.at(91u),
            MaterialReference::project(default_material_id()));
  EXPECT_EQ(hidden_scene.selected_entities(), unsaved_selection);
  expect_deletion_history(editor_.query().history(), unsaved_history);
  const auto hidden_info = editor_.query().entity(hidden_box.value().entity);
  ASSERT_TRUE(hidden_info.has_value());
  EXPECT_FALSE(hidden_info->visible);
  EXPECT_TRUE(hidden_info->locked);

  auto on_disk = QdrDocumentReader::read_project_document(root_ / "Scenes/Saved.qdr");
  ASSERT_TRUE(on_disk.ok) << on_disk.message;
  const auto *disk_name = on_disk.document.scene().name(saved_box.value().entity);
  ASSERT_NE(disk_name, nullptr);
  EXPECT_EQ(disk_name->name, "Saved name");
  expect_deletion_material(on_disk.document.scene(), saved_box.value().entity,
                           default_material_id());
  ASSERT_TRUE(editor_.scene_documents().activate(saved_session).ok());
  EXPECT_TRUE(project_.active_scene()->dirty());
  EXPECT_EQ(editor_.query().entity(saved_box.value().entity)->name, "Unsaved name");
  EXPECT_EQ(project_.active_scene()->session->scene().selected_entities(), saved_selection);
  expect_deletion_history(editor_.query().history(), saved_history);
  expect_deletion_material(project_.active_scene()->session->scene(),
                           saved_box.value().entity, default_material_id());
  EXPECT_TRUE(std::filesystem::exists(root_ / "Textures/shared.png"));
  EXPECT_NE(project_.material_catalog().find(surviving_material_id()), nullptr);
}

TEST_F(ProjectMaterialDeletionTests,
       SameNamedScenesRemainDistinctWhileCleanSavedLivePairsCoalesce) {
  ASSERT_NE(project_.active_scene(), nullptr);
  project_.active_scene()->session->document().rename("Shared");
  const auto first_box = create_box("First shared scene");
  ASSERT_TRUE(first_box.ok()) << first_box.error_message();
  std::filesystem::create_directories(root_ / "Scenes/First");
  ASSERT_NO_FATAL_FAILURE(save_active("Scenes/First/Shared.qdr"));

  const auto second_scene = editor_.scene_documents().new_scene();
  ASSERT_TRUE(second_scene.ok()) << second_scene.error_message();
  ASSERT_NE(project_.active_scene(), nullptr);
  project_.active_scene()->session->document().rename("Shared");
  const auto second_box = create_box("Second shared scene");
  ASSERT_TRUE(second_box.ok()) << second_box.error_message();
  std::filesystem::create_directories(root_ / "Scenes/Second");
  ASSERT_NO_FATAL_FAILURE(save_active("Scenes/Second/Shared.qdr"));

  const auto preview = editor_.project_assets().preview_delete_material_folder(
      "Materials/Delete");
  ASSERT_TRUE(preview.ok()) << preview.error_message();
  ASSERT_TRUE(preview.value().complete)
      << ::testing::PrintToString(preview.value().errors);
  ASSERT_EQ(preview.value().affected_scenes.size(), 2u)
      << "clean saved/live pairs should produce one warning per scene";
  std::set<std::tuple<std::string, bool, std::string, std::uint64_t>>
      scene_records;
  for (const auto &scene : preview.value().affected_scenes) {
    scene_records.emplace(
        scene.name, scene.live, scene.project_relative_path,
        scene.session_id.has_value() ? scene.session_id->packed() : 0u);
  }
  EXPECT_EQ(scene_records.size(), 2u);
  EXPECT_EQ(std::ranges::count_if(
                preview.value().affected_scenes, [](const auto &scene) {
                  return scene.project_relative_path ==
                         "Scenes/First/Shared.qdr";
                }),
            1);
  EXPECT_EQ(std::ranges::count_if(
                preview.value().affected_scenes, [](const auto &scene) {
                  return scene.project_relative_path ==
                         "Scenes/Second/Shared.qdr";
                }),
            1);
  EXPECT_TRUE(std::ranges::all_of(
      preview.value().affected_scenes,
      [](const auto &scene) { return scene.live; }));
}

TEST_F(ProjectMaterialDeletionTests,
       UndoClipboardSaveAndRecoveryKeepDeletedIdentitiesEffectivelyDefault) {
  const auto box = create_box("Historical wood");
  ASSERT_TRUE(box.ok()) << box.error_message();
  ASSERT_TRUE(editor_.object(box.value()).select().ok());
  const SessionPreferences autosave_settings;
  const std::chrono::steady_clock::time_point autosave_epoch{};
  EXPECT_EQ(project_.autosave().poll(project_, autosave_settings, autosave_epoch).written, 0u);
  const auto autosaved = project_.autosave().poll(
      project_, autosave_settings, autosave_epoch + std::chrono::minutes(2));
  ASSERT_TRUE(autosaved.ok) << ::testing::PrintToString(autosaved.warnings);
  ASSERT_EQ(autosaved.written, 1u);
  const auto historical_copies = editor_.scene_documents().autosaves();
  ASSERT_TRUE(historical_copies.ok()) << historical_copies.error_message();
  ASSERT_EQ(historical_copies.value().size(), 1u);
  const auto historical_recovery_path = historical_copies.value().front().path;
  ASSERT_NO_FATAL_FAILURE(save_active());
  ASSERT_TRUE(editor_.modeling().copy_selection().ok());
  ASSERT_TRUE(editor_.modeling().apply_material(
      QuaderMaterialReference::project(surviving_material_id())).ok());
  const auto history = editor_.query().history();

  ASSERT_NO_FATAL_FAILURE(delete_folder());
  expect_deletion_history(editor_.query().history(), history);
  ASSERT_TRUE(editor_.history().undo().ok());
  const auto used_after_undo =
      editor_.materials().used_materials(QuaderMaterialUsageScope::Selection);
  ASSERT_TRUE(used_after_undo.ok()) << used_after_undo.error_message();
  EXPECT_EQ(used_after_undo.value(), std::vector<QuaderMaterialReference>{
      QuaderMaterialReference::project(default_material_id())});
  EXPECT_FALSE(editor_.modeling().apply_material(
      QuaderMaterialReference::project(deletion_material_id())).ok());
  EXPECT_FALSE(editor_.standard_material_documents().open(deletion_material_id()).ok());

  ASSERT_TRUE(editor_.modeling().paste_selection().ok());
  const auto used_after_paste =
      editor_.materials().used_materials(QuaderMaterialUsageScope::ActiveScene);
  ASSERT_TRUE(used_after_paste.ok()) << used_after_paste.error_message();
  EXPECT_EQ(used_after_paste.value(), std::vector<QuaderMaterialReference>{
      QuaderMaterialReference::project(default_material_id())});
  ASSERT_TRUE(editor_.scene_documents().save_active().ok());
  auto read = QdrDocumentReader::read_project_document(root_ / "Scenes/Saved.qdr");
  ASSERT_TRUE(read.ok) << read.message;
  for (const auto &dependency : read.project_asset_dependencies) {
    EXPECT_NE(dependency.stable_id, deletion_material_id());
  }
  for (EntityId entity : read.document.scene().entities()) {
    if (const auto *material = read.document.scene().material(entity)) {
      EXPECT_EQ(material->material, MaterialReference::project(default_material_id()));
    }
  }
  const auto material_list = editor_.project_assets().materials();
  ASSERT_TRUE(material_list.ok());
  EXPECT_FALSE(std::ranges::any_of(material_list.value(), [&](const auto &material) {
    return material.id == deletion_material_id();
  }));
  ASSERT_TRUE(editor_.project().close().ok());
  ASSERT_TRUE(editor_.project().open(manifest_).ok());
  const auto reopened_used =
      editor_.materials().used_materials(QuaderMaterialUsageScope::ActiveScene);
  ASSERT_TRUE(reopened_used.ok());
  EXPECT_EQ(reopened_used.value(), used_after_paste.value());

  const auto recovered = editor_.scene_documents().recover_autosave(
      historical_recovery_path);
  ASSERT_TRUE(recovered.ok()) << recovered.error_message();
  expect_deletion_material(project_.active_scene()->session->scene(),
                           box.value().entity, default_material_id());
  EXPECT_TRUE(project_.active_scene()->dirty());
}

TEST_F(ProjectMaterialDeletionTests, EmptyFoldersDeleteButRootAndDefaultAreProtected) {
  const auto surviving_bytes = deletion_read_bytes(root_ / "Materials/Keep/stone.qmat");
  ASSERT_TRUE(editor_.project_assets().create_material_folder("Materials", "Empty").ok());
  ASSERT_TRUE(editor_.project_assets().create_material_folder("Materials/Empty", "Nested").ok());
  const auto preview = editor_.project_assets().preview_delete_material_folder("Materials/Empty");
  ASSERT_TRUE(preview.ok());
  ASSERT_TRUE(preview.value().complete) << ::testing::PrintToString(preview.value().errors);
  EXPECT_TRUE(preview.value().material_ids.empty());
  EXPECT_EQ(preview.value().folder_count, 2u);
  ASSERT_NO_FATAL_FAILURE(delete_folder("Materials/Empty"));
  EXPECT_FALSE(std::filesystem::exists(root_ / "Materials/Empty"));
  EXPECT_NE(project_.material_catalog().find(surviving_material_id()), nullptr);
  EXPECT_EQ(deletion_read_bytes(root_ / "Materials/Keep/stone.qmat"), surviving_bytes);
  ASSERT_NO_FATAL_FAILURE(delete_folder());
  EXPECT_NE(project_.material_catalog().find(surviving_material_id()), nullptr);
  EXPECT_EQ(deletion_read_bytes(root_ / "Materials/Keep/stone.qmat"), surviving_bytes);
  for (const std::string_view path : {"Materials", "", "Textures"}) {
    const auto protected_preview = editor_.project_assets().preview_delete_material_folder(path);
    ASSERT_TRUE(protected_preview.ok());
    EXPECT_FALSE(protected_preview.value().complete);
    EXPECT_FALSE(editor_.project_assets().delete_material_folder(
        path, protected_preview.value().revision).ok());
  }
  const auto *reserved = project_.material_catalog().find(default_material_id());
  ASSERT_NE(reserved, nullptr);
  const auto parent = reserved->path.native_path().parent_path().generic_string();
  const auto protected_preview = editor_.project_assets().preview_delete_material_folder(parent);
  ASSERT_TRUE(protected_preview.ok());
  EXPECT_FALSE(protected_preview.value().complete);
  EXPECT_TRUE(std::filesystem::exists(root_ / reserved->path.native_path()));
}

TEST_F(ProjectMaterialDeletionTests,
       SelectedMaterialDeletionPreservesItsFolderAndSiblingFiles) {
  ASSERT_TRUE(editor_.project().close().ok());
  ASSERT_NO_FATAL_FAILURE(write_material(
      "Materials/Delete/sibling.qmat", sibling_material_id(), "Sibling"));
  ASSERT_TRUE(editor_.project().open(manifest_).ok());
  if (!editor_.scene_documents().has_active_scene()) {
    ASSERT_TRUE(editor_.scene_documents().new_scene().ok());
  }
  const auto box = create_box("Selected deletion");
  ASSERT_TRUE(box.ok()) << box.error_message();
  ASSERT_NO_FATAL_FAILURE(save_active());

  ProjectMaterialDeletionService service(project_);
  const std::array reserved{default_material_id()};
  EXPECT_FALSE(service.preview_materials(reserved).complete);
  const std::array duplicate{deletion_material_id(), deletion_material_id()};
  EXPECT_FALSE(service.preview_materials(duplicate).complete);
  const std::array selected{deletion_material_id()};
  const auto preview = service.preview_materials(selected);
  ASSERT_TRUE(preview.complete) << ::testing::PrintToString(preview.errors);
  ASSERT_EQ(preview.material_ids, std::vector<ProjectAssetId>(selected.begin(),
                                                              selected.end()));
  const auto deleted = service.delete_materials(selected, preview.revision);
  ASSERT_TRUE(deleted.ok) << deleted.message;
  EXPECT_TRUE(deleted.changed);

  EXPECT_TRUE(std::filesystem::is_directory(root_ / "Materials/Delete"));
  EXPECT_FALSE(std::filesystem::exists(root_ / "Materials/Delete/wood.qmat"));
  EXPECT_TRUE(std::filesystem::is_regular_file(
      root_ / "Materials/Delete/sibling.qmat"));
  EXPECT_EQ(project_.material_catalog().find(deletion_material_id()), nullptr);
  EXPECT_NE(project_.material_catalog().find(sibling_material_id()), nullptr);
  EXPECT_TRUE(project_.manifest()->is_material_deleted(deletion_material_id()));
  EXPECT_FALSE(project_.manifest()->is_material_deleted(sibling_material_id()));
  expect_deletion_material(project_.active_scene()->session->scene(),
                           box.value().entity, default_material_id());
  EXPECT_TRUE(std::filesystem::is_regular_file(root_ / "Textures/shared.png"));
}

TEST_F(ProjectMaterialDeletionTests, ConfirmationRejectsChangedSourcesAndAnotherFolder) {
  const auto box = create_box("Before preview");
  ASSERT_TRUE(box.ok());
  const auto preview = editor_.project_assets().preview_delete_material_folder("Materials/Delete");
  ASSERT_TRUE(preview.ok());
  ASSERT_TRUE(preview.value().complete) << ::testing::PrintToString(preview.value().errors);
  const auto wrong_folder = editor_.project_assets().delete_material_folder(
      "Materials/Keep", preview.value().revision);
  EXPECT_FALSE(wrong_folder.ok());
  EXPECT_FALSE(wrong_folder.changed());
  ASSERT_TRUE(editor_.scene().rename(box.value().entity, "Changed after preview").ok());
  const auto stale = editor_.project_assets().delete_material_folder(
      "Materials/Delete", preview.value().revision);
  EXPECT_FALSE(stale.ok());
  EXPECT_FALSE(stale.changed());
  EXPECT_TRUE(std::filesystem::exists(root_ / "Materials/Delete/wood.qmat"));
  EXPECT_TRUE(std::filesystem::exists(root_ / "Materials/Keep/stone.qmat"));
  const auto refreshed = editor_.project_assets().preview_delete_material_folder("Materials/Delete");
  ASSERT_TRUE(refreshed.ok());
  EXPECT_NE(refreshed.value().revision, preview.value().revision);
  ASSERT_NO_FATAL_FAILURE(delete_folder());
}

TEST_F(ProjectMaterialDeletionTests,
       PostQuarantineCatalogValidationRejectsConcurrentAddition) {
  const std::array ids{deletion_material_id()};
  const auto concurrent_id = *ProjectAssetId::parse(
      "fc7b386b-e2c4-48ff-8432-c4320369311d");
  ProjectMaterialDeletionService deletion(
      project_, {.after_quarantine = [&] {
        write_material("Materials/Keep/concurrent.qmat", concurrent_id,
                       "Concurrent");
      }});
  const auto preview = deletion.preview_materials(ids);
  ASSERT_TRUE(preview.complete)
      << ::testing::PrintToString(preview.errors);

  const auto deleted = deletion.delete_materials(ids, preview.revision);

  EXPECT_FALSE(deleted.ok);
  EXPECT_FALSE(deleted.changed);
  EXPECT_NE(deleted.message.find("catalog changed"), std::string::npos);
  EXPECT_TRUE(std::filesystem::is_regular_file(
      root_ / "Materials/Delete/wood.qmat"));
  EXPECT_TRUE(std::filesystem::is_regular_file(
      root_ / "Materials/Keep/concurrent.qmat"));
  EXPECT_FALSE(project_.manifest()->is_material_deleted(
      deletion_material_id()));
}

TEST_F(ProjectMaterialDeletionTests,
       PostQuarantineCatalogValidationRejectsSameCountIdentityReplacement) {
  const std::array ids{deletion_material_id()};
  const auto replacement_id = *ProjectAssetId::parse(
      "f7f69006-dad1-43a3-b18d-942219d3cb44");
  ProjectMaterialDeletionService deletion(
      project_, {.after_quarantine = [&] {
        ASSERT_TRUE(std::filesystem::remove(
            root_ / "Materials/Keep/stone.qmat"));
        write_material("Materials/Keep/replacement.qmat", replacement_id,
                       "Replacement");
      }});
  const auto preview = deletion.preview_materials(ids);
  ASSERT_TRUE(preview.complete)
      << ::testing::PrintToString(preview.errors);

  const auto deleted = deletion.delete_materials(ids, preview.revision);

  EXPECT_FALSE(deleted.ok);
  EXPECT_FALSE(deleted.changed);
  EXPECT_NE(deleted.message.find("catalog content changed"),
            std::string::npos);
  EXPECT_TRUE(std::filesystem::is_regular_file(
      root_ / "Materials/Delete/wood.qmat"));
  EXPECT_FALSE(std::filesystem::exists(
      root_ / "Materials/Keep/stone.qmat"));
  EXPECT_TRUE(std::filesystem::is_regular_file(
      root_ / "Materials/Keep/replacement.qmat"));
}

TEST_F(ProjectMaterialDeletionTests,
       PostQuarantineCatalogValidationRejectsConcurrentContentChange) {
  const std::array ids{deletion_material_id()};
  ProjectMaterialDeletionService deletion(
      project_, {.after_quarantine = [&] {
        write_material("Materials/Keep/stone.qmat", surviving_material_id(),
                       "Changed Stone");
      }});
  const auto preview = deletion.preview_materials(ids);
  ASSERT_TRUE(preview.complete)
      << ::testing::PrintToString(preview.errors);

  const auto deleted = deletion.delete_materials(ids, preview.revision);

  EXPECT_FALSE(deleted.ok);
  EXPECT_FALSE(deleted.changed);
  EXPECT_NE(deleted.message.find("catalog content changed"),
            std::string::npos);
  EXPECT_TRUE(std::filesystem::is_regular_file(
      root_ / "Materials/Delete/wood.qmat"));
  const auto changed = QmatCodec::decode(deletion_read_bytes(
      root_ / "Materials/Keep/stone.qmat"));
  ASSERT_TRUE(changed.ok()) << changed.error().message;
  EXPECT_EQ(changed.value().name, "Changed Stone");
}

TEST_F(ProjectMaterialDeletionTests, UnmanagedFilesAndReferencedTexturesPreventDeletion) {
  ASSERT_NO_FATAL_FAILURE(deletion_write_bytes(root_ / "Materials/Delete/notes.txt", "Keep me"));
  const auto unmanaged = editor_.project_assets().preview_delete_material_folder("Materials/Delete");
  ASSERT_TRUE(unmanaged.ok());
  EXPECT_FALSE(unmanaged.value().complete);
  EXPECT_FALSE(unmanaged.value().errors.empty());
  EXPECT_FALSE(editor_.project_assets().delete_material_folder(
      "Materials/Delete", unmanaged.value().revision).ok());
  EXPECT_EQ(deletion_read_bytes(root_ / "Materials/Delete/notes.txt"), "Keep me");
  std::filesystem::remove(root_ / "Materials/Delete/notes.txt");

  ASSERT_TRUE(editor_.project().close().ok());
  std::filesystem::copy_file(root_ / "Textures/shared.png", root_ / "Materials/Delete/shared.png");
  ASSERT_NO_FATAL_FAILURE(write_material("Materials/Keep/stone.qmat",
      surviving_material_id(), "Stone", "Materials/Delete/shared.png"));
  ASSERT_TRUE(editor_.project().open(manifest_).ok());
  const auto shared = editor_.project_assets().preview_delete_material_folder("Materials/Delete");
  ASSERT_TRUE(shared.ok());
  EXPECT_FALSE(shared.value().complete);
  EXPECT_FALSE(editor_.project_assets().delete_material_folder(
      "Materials/Delete", shared.value().revision).ok());
  EXPECT_TRUE(std::filesystem::exists(root_ / "Materials/Delete/shared.png"));
  EXPECT_NE(project_.material_catalog().find(surviving_material_id()), nullptr);
}

TEST_F(ProjectMaterialDeletionTests, UnreadableRegisteredSceneBlocksDeletionEvenWhenClosed) {
  const auto box = create_box("Saved dependency");
  ASSERT_TRUE(box.ok());
  ASSERT_NO_FATAL_FAILURE(save_active());
  ASSERT_TRUE(editor_.scene_documents().close_active().ok());
  ASSERT_NO_FATAL_FAILURE(deletion_write_bytes(root_ / "Scenes/Saved.qdr", "invalid QDR bytes"));
  const auto preview = editor_.project_assets().preview_delete_material_folder("Materials/Delete");
  ASSERT_TRUE(preview.ok());
  EXPECT_FALSE(preview.value().complete);
  EXPECT_FALSE(preview.value().errors.empty());
  const auto result = editor_.project_assets().delete_material_folder(
      "Materials/Delete", preview.value().revision);
  EXPECT_FALSE(result.ok());
  EXPECT_FALSE(result.changed());
  EXPECT_EQ(deletion_read_bytes(root_ / "Scenes/Saved.qdr"), "invalid QDR bytes");
  EXPECT_TRUE(std::filesystem::exists(root_ / "Materials/Delete/wood.qmat"));
  EXPECT_FALSE(project_.manifest()->is_material_deleted(deletion_material_id()));
}

TEST_F(ProjectMaterialDeletionTests, DraftRemainsUntilConfirmedSuccessfulDeletion) {
  ASSERT_TRUE(editor_.standard_material_documents().open(deletion_material_id()).ok());
  ASSERT_TRUE(editor_.standard_material_documents().set_base_color({0.2f, 0.5f, 0.8f}).ok());
  const auto before = editor_.standard_material_documents().current();
  ASSERT_TRUE(before.ok());
  const auto preview = editor_.project_assets().preview_delete_material_folder("Materials/Delete");
  ASSERT_TRUE(preview.ok());
  ASSERT_TRUE(preview.value().complete) << ::testing::PrintToString(preview.value().errors);
  EXPECT_TRUE(preview.value().discards_material_draft);
  // Dismissing the warning has no owner mutation to undo.
  const auto after_preview = editor_.standard_material_documents().current();
  ASSERT_TRUE(after_preview.ok());
  EXPECT_EQ(after_preview.value().session_id, before.value().session_id);
  EXPECT_EQ(after_preview.value().draft_revision, before.value().draft_revision);
  ASSERT_TRUE(editor_.standard_material_documents().set_roughness(0.2f).ok());
  const auto stale = editor_.project_assets().delete_material_folder(
      "Materials/Delete", preview.value().revision);
  EXPECT_FALSE(stale.ok());
  const auto after_failure = editor_.standard_material_documents().current();
  ASSERT_TRUE(after_failure.ok());
  EXPECT_EQ(after_failure.value().session_id, before.value().session_id);
  EXPECT_TRUE(after_failure.value().dirty);
  ASSERT_NO_FATAL_FAILURE(delete_folder());
  EXPECT_FALSE(editor_.standard_material_documents().current().ok());
  EXPECT_EQ(project_.material_catalog().find(deletion_material_id()), nullptr);
  EXPECT_NE(project_.material_catalog().find(default_material_id()), nullptr);
  EXPECT_TRUE(std::filesystem::exists(root_ / "Textures/shared.png"));
}

TEST_F(ProjectMaterialDeletionTests, UnchangedDependencyReadsPerformNoExpensiveWork) {
  ASSERT_TRUE(create_box("Cached dependency").ok());
  ASSERT_NO_FATAL_FAILURE(save_active());
  const auto first = editor_.project_assets().material_dependencies(true);
  ASSERT_TRUE(first.ok());
  ASSERT_TRUE(first.value().complete) << ::testing::PrintToString(first.value().errors);
  const auto stats = project_.dependencies().work_stats();
  for (int frame = 0; frame < 8; ++frame) {
    const auto unchanged = editor_.project_assets().material_dependencies();
    ASSERT_TRUE(unchanged.ok());
    EXPECT_EQ(unchanged.value().revision, first.value().revision);
  }
  EXPECT_EQ(project_.dependencies().work_stats(), stats);
}

#ifdef _WIN32
/// Denies replacing a document while retaining ordinary read access for inspection.
class ScopedDeletionManifestLock final {
public:
  explicit ScopedDeletionManifestLock(const std::filesystem::path &path)
      : handle_(::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                              nullptr)) {}
  ~ScopedDeletionManifestLock() {
    if (handle_ != INVALID_HANDLE_VALUE) {
      (void)::CloseHandle(handle_);
    }
  }
  bool valid() const { return handle_ != INVALID_HANDLE_VALUE; }

private:
  HANDLE handle_ = INVALID_HANDLE_VALUE;
};

TEST_F(ProjectMaterialDeletionTests, FailedManifestPublicationRollsBackSavedSceneAndFolder) {
  const auto box = create_box("Rollback dependency");
  ASSERT_TRUE(box.ok());
  ASSERT_NO_FATAL_FAILURE(save_active());
  ASSERT_TRUE(editor_.standard_material_documents().open(deletion_material_id()).ok());
  ASSERT_TRUE(editor_.standard_material_documents().set_roughness(0.2f).ok());
  const auto draft = editor_.standard_material_documents().current();
  ASSERT_TRUE(draft.ok());
  const auto old_scene = deletion_read_bytes(root_ / "Scenes/Saved.qdr");
  const auto old_manifest = deletion_read_bytes(manifest_);
  const auto history = editor_.query().history();
  const auto preview = editor_.project_assets().preview_delete_material_folder("Materials/Delete");
  ASSERT_TRUE(preview.ok());
  ASSERT_TRUE(preview.value().complete) << ::testing::PrintToString(preview.value().errors);
  {
    ScopedDeletionManifestLock lock(manifest_);
    ASSERT_TRUE(lock.valid());
    const auto deleted = editor_.project_assets().delete_material_folder(
        "Materials/Delete", preview.value().revision);
    EXPECT_FALSE(deleted.ok());
    EXPECT_FALSE(deleted.changed());
  }
  EXPECT_EQ(deletion_read_bytes(root_ / "Scenes/Saved.qdr"), old_scene);
  EXPECT_EQ(deletion_read_bytes(manifest_), old_manifest);
  EXPECT_TRUE(std::filesystem::exists(root_ / "Materials/Delete/wood.qmat"));
  expect_deletion_material(project_.active_scene()->session->scene(),
                           box.value().entity, deletion_material_id());
  expect_deletion_history(editor_.query().history(), history);
  EXPECT_FALSE(project_.active_scene()->dirty());
  const auto retained = editor_.standard_material_documents().current();
  ASSERT_TRUE(retained.ok());
  EXPECT_EQ(retained.value().session_id, draft.value().session_id);
  EXPECT_EQ(retained.value().draft_revision, draft.value().draft_revision);
  ASSERT_NO_FATAL_FAILURE(delete_folder());
}
#endif

TEST_F(ProjectMaterialDeletionTests, PreparedJournalRestoresManifestAndQuarantinedFolder) {
  ASSERT_TRUE(editor_.project().close().ok());
  auto workspace = ProjectManifestStore::open(manifest_);
  ASSERT_TRUE(workspace.ok()) << workspace.error().message;
  const auto baseline = deletion_read_bytes(manifest_);
  const auto transaction = root_ / ".quader/material-deletions" /
      "db26313f-6de1-4097-aa33-e2bf83589922";
  std::filesystem::create_directories(transaction);
  ASSERT_NO_FATAL_FAILURE(deletion_write_bytes(transaction / "file-0.old", baseline));
  const std::array ids{deletion_material_id()};
  ASSERT_TRUE(workspace.value().manifest.mark_materials_deleted(ids).ok());
  ASSERT_TRUE(ProjectManifestStore::save_atomic(workspace.value()).ok());
  const auto replacement = deletion_read_bytes(manifest_);
  std::filesystem::rename(root_ / "Materials/Delete", transaction / "folder");
  const auto hash = quader_io::qdr_binary_content_hash(
      std::as_bytes(std::span(baseline.data(), baseline.size())));
  const auto replacement_hash = quader_io::qdr_binary_content_hash(
      std::as_bytes(std::span(replacement.data(), replacement.size())));
  const nlohmann::json journal{
      {"version", 1}, {"project", workspace.value().manifest.id().str()},
      {"folder", "Materials/Delete"}, {"state", "prepared"},
      {"files", {{{"path", manifest_.filename().generic_string()},
                   {"backup", "file-0.old"}, {"byte_count", baseline.size()},
                   {"content_hash", hash},
                   {"replacement_byte_count", replacement.size()},
                   {"replacement_content_hash", replacement_hash}}}}};
  ASSERT_NO_FATAL_FAILURE(deletion_write_bytes(transaction / "journal.json", journal.dump()));
  const auto opened = editor_.project().open(manifest_);
  ASSERT_TRUE(opened.ok()) << opened.error_message();
  EXPECT_FALSE(project_.manifest()->is_material_deleted(deletion_material_id()));
  EXPECT_TRUE(std::filesystem::exists(root_ / "Materials/Delete/wood.qmat"));
  EXPECT_NE(project_.material_catalog().find(deletion_material_id()), nullptr);
  EXPECT_FALSE(std::filesystem::exists(transaction));
}

TEST_F(ProjectMaterialDeletionTests,
       PreparedMaterialJournalRestoresOnlyTheSelectedQmat) {
  ASSERT_TRUE(editor_.project().close().ok());
  auto workspace = ProjectManifestStore::open(manifest_);
  ASSERT_TRUE(workspace.ok()) << workspace.error().message;
  const auto baseline_manifest = deletion_read_bytes(manifest_);
  const auto material_path = root_ / "Materials/Delete/wood.qmat";
  const auto material_bytes = deletion_read_bytes(material_path);
  const std::array ids{deletion_material_id()};
  ASSERT_TRUE(workspace.value().manifest.mark_materials_deleted(ids).ok());
  ASSERT_TRUE(ProjectManifestStore::save_atomic(workspace.value()).ok());
  const auto replacement_manifest = deletion_read_bytes(manifest_);

  const auto transaction = root_ / ".quader/material-deletions" /
      "77d53503-d35a-41bf-95e6-a88265491091";
  std::filesystem::create_directories(transaction / "materials");
  ASSERT_NO_FATAL_FAILURE(deletion_write_bytes(transaction / "file-0.old",
                                               baseline_manifest));
  std::filesystem::rename(material_path,
                          transaction / "materials/material-0.qmat");
  const auto material_hash = quader_io::qdr_binary_content_hash(
      std::as_bytes(std::span(material_bytes.data(), material_bytes.size())));
  const nlohmann::json journal{
      {"version", 2}, {"kind", "materials"},
      {"project", workspace.value().manifest.id().str()},
      {"state", "prepared"},
      {"material_ids",
       nlohmann::json::array({std::string(deletion_material_id().str())})},
      {"material_files",
       {{{"path", "Materials/Delete/wood.qmat"},
         {"byte_count", material_bytes.size()},
         {"content_hash", material_hash}}}},
      {"files",
       {deletion_recovery_file(manifest_.filename().generic_string(),
                               "file-0.old", baseline_manifest,
                               replacement_manifest)}}};
  ASSERT_NO_FATAL_FAILURE(deletion_write_bytes(transaction / "journal.json",
                                               journal.dump()));

  const auto opened = editor_.project().open(manifest_);
  ASSERT_TRUE(opened.ok()) << opened.error_message();
  EXPECT_FALSE(project_.manifest()->is_material_deleted(deletion_material_id()));
  EXPECT_TRUE(std::filesystem::is_directory(root_ / "Materials/Delete"));
  EXPECT_EQ(deletion_read_bytes(material_path), material_bytes);
  EXPECT_NE(project_.material_catalog().find(deletion_material_id()), nullptr);
  EXPECT_FALSE(std::filesystem::exists(transaction));
}

TEST_F(ProjectMaterialDeletionTests,
       MaterialRecoveryPreservesUnjournaledQuarantineEntries) {
  ASSERT_TRUE(editor_.project().close().ok());
  auto workspace = ProjectManifestStore::open(manifest_);
  ASSERT_TRUE(workspace.ok()) << workspace.error().message;
  const auto baseline_manifest = deletion_read_bytes(manifest_);
  const auto material_path = root_ / "Materials/Delete/wood.qmat";
  const auto material_bytes = deletion_read_bytes(material_path);
  const std::array ids{deletion_material_id()};
  ASSERT_TRUE(workspace.value().manifest.mark_materials_deleted(ids).ok());
  ASSERT_TRUE(ProjectManifestStore::save_atomic(workspace.value()).ok());
  const auto replacement_manifest = deletion_read_bytes(manifest_);

  const auto transaction = root_ / ".quader/material-deletions" /
      "fc269461-82e6-4c7d-b535-b26153c05c63";
  std::filesystem::create_directories(transaction / "materials");
  ASSERT_NO_FATAL_FAILURE(deletion_write_bytes(transaction / "file-0.old",
                                               baseline_manifest));
  std::filesystem::rename(material_path,
                          transaction / "materials/material-0.qmat");
  ASSERT_NO_FATAL_FAILURE(deletion_write_bytes(
      transaction / "materials/unexpected.bin", "Unjournaled content"));
  const auto material_hash = quader_io::qdr_binary_content_hash(
      std::as_bytes(std::span(material_bytes.data(), material_bytes.size())));
  const nlohmann::json journal{
      {"version", 2}, {"kind", "materials"},
      {"project", workspace.value().manifest.id().str()},
      {"state", "prepared"},
      {"material_ids",
       nlohmann::json::array({std::string(deletion_material_id().str())})},
      {"material_files",
       {{{"path", "Materials/Delete/wood.qmat"},
         {"byte_count", material_bytes.size()},
         {"content_hash", material_hash}}}},
      {"files",
       {deletion_recovery_file(manifest_.filename().generic_string(),
                               "file-0.old", baseline_manifest,
                               replacement_manifest)}}};
  ASSERT_NO_FATAL_FAILURE(deletion_write_bytes(transaction / "journal.json",
                                               journal.dump()));

  const auto opened = editor_.project().open(manifest_);

  EXPECT_FALSE(opened.ok());
  EXPECT_NE(opened.error_message().find("unjournaled entry"),
            std::string::npos);
  EXPECT_EQ(deletion_read_bytes(manifest_), replacement_manifest);
  EXPECT_FALSE(std::filesystem::exists(material_path));
  EXPECT_EQ(deletion_read_bytes(transaction / "materials/material-0.qmat"),
            material_bytes);
  EXPECT_EQ(deletion_read_bytes(transaction / "materials/unexpected.bin"),
            "Unjournaled content");
  EXPECT_TRUE(std::filesystem::exists(transaction / "file-0.old"));
  EXPECT_TRUE(std::filesystem::exists(transaction / "journal.json"));
}

TEST_F(ProjectMaterialDeletionTests,
       CommittedMaterialRecoveryRequiresCurrentManifestTombstones) {
  ASSERT_TRUE(editor_.project().close().ok());
  auto workspace = ProjectManifestStore::open(manifest_);
  ASSERT_TRUE(workspace.ok()) << workspace.error().message;
  const auto baseline_manifest = deletion_read_bytes(manifest_);
  ASSERT_EQ(nlohmann::json::parse(baseline_manifest)
                .at("version")
                .get<std::uint64_t>(),
            2u);
  const std::array ids{deletion_material_id()};
  ASSERT_TRUE(workspace.value().manifest.mark_materials_deleted(ids).ok());
  ASSERT_TRUE(ProjectManifestStore::save_atomic(workspace.value()).ok());
  const auto replacement_manifest = deletion_read_bytes(manifest_);

  const auto material_path = root_ / "Materials/Delete/wood.qmat";
  const auto material_bytes = deletion_read_bytes(material_path);
  const auto transaction = root_ / ".quader/material-deletions" /
      "44428535-ce2f-4e8d-8d6d-b5d4b87ac75a";
  std::filesystem::create_directories(transaction / "materials");
  ASSERT_NO_FATAL_FAILURE(deletion_write_bytes(transaction / "file-0.old",
                                               baseline_manifest));
  std::filesystem::rename(material_path,
                          transaction / "materials/material-0.qmat");
  const auto material_hash = quader_io::qdr_binary_content_hash(
      std::as_bytes(std::span(material_bytes.data(), material_bytes.size())));
  const nlohmann::json journal{
      {"version", 2}, {"kind", "materials"},
      {"project", workspace.value().manifest.id().str()},
      {"state", "committed"},
      {"material_ids",
       nlohmann::json::array({std::string(deletion_material_id().str())})},
      {"material_files",
       {{{"path", "Materials/Delete/wood.qmat"},
         {"byte_count", material_bytes.size()},
         {"content_hash", material_hash}}}},
      {"files",
       {deletion_recovery_file(manifest_.filename().generic_string(),
                               "file-0.old", baseline_manifest,
                               replacement_manifest)}}};
  ASSERT_NO_FATAL_FAILURE(deletion_write_bytes(transaction / "journal.json",
                                               journal.dump()));
  ASSERT_NO_FATAL_FAILURE(deletion_write_bytes(manifest_, baseline_manifest));

  const auto opened = editor_.project().open(manifest_);
  EXPECT_FALSE(opened.ok());
  EXPECT_EQ(deletion_read_bytes(manifest_), baseline_manifest);
  EXPECT_FALSE(std::filesystem::exists(material_path));
  EXPECT_EQ(deletion_read_bytes(transaction / "materials/material-0.qmat"),
            material_bytes);
  EXPECT_TRUE(std::filesystem::exists(transaction / "journal.json"));
}

TEST_F(ProjectMaterialDeletionTests,
       CommittedMaterialRecoveryAcceptsNewerManifestWithTombstones) {
  ASSERT_TRUE(editor_.project().close().ok());
  auto workspace = ProjectManifestStore::open(manifest_);
  ASSERT_TRUE(workspace.ok()) << workspace.error().message;
  const auto baseline_manifest = deletion_read_bytes(manifest_);
  const std::array ids{deletion_material_id()};
  ASSERT_TRUE(workspace.value().manifest.mark_materials_deleted(ids).ok());
  ASSERT_TRUE(ProjectManifestStore::save_atomic(workspace.value()).ok());
  const auto replacement_manifest = deletion_read_bytes(manifest_);
  const auto subsequently_deleted = ProjectAssetId::generate();
  const std::array newer_ids{subsequently_deleted};
  ASSERT_TRUE(workspace.value().manifest.mark_materials_deleted(newer_ids).ok());
  ASSERT_TRUE(ProjectManifestStore::save_atomic(workspace.value()).ok());
  const auto newer_manifest = deletion_read_bytes(manifest_);
  ASSERT_NE(newer_manifest, replacement_manifest);

  const auto material_path = root_ / "Materials/Delete/wood.qmat";
  const auto material_bytes = deletion_read_bytes(material_path);
  const auto transaction = root_ / ".quader/material-deletions" /
      "3e69c90f-c534-492a-bbc9-18df3556b16c";
  std::filesystem::create_directories(transaction / "materials");
  ASSERT_NO_FATAL_FAILURE(deletion_write_bytes(transaction / "file-0.old",
                                               baseline_manifest));
  std::filesystem::rename(material_path,
                          transaction / "materials/material-0.qmat");
  const auto material_hash = quader_io::qdr_binary_content_hash(
      std::as_bytes(std::span(material_bytes.data(), material_bytes.size())));
  const nlohmann::json journal{
      {"version", 2}, {"kind", "materials"},
      {"project", workspace.value().manifest.id().str()},
      {"state", "committed"},
      {"material_ids",
       nlohmann::json::array({std::string(deletion_material_id().str())})},
      {"material_files",
       {{{"path", "Materials/Delete/wood.qmat"},
         {"byte_count", material_bytes.size()},
         {"content_hash", material_hash}}}},
      {"files",
       {deletion_recovery_file(manifest_.filename().generic_string(),
                               "file-0.old", baseline_manifest,
                               replacement_manifest)}}};
  ASSERT_NO_FATAL_FAILURE(deletion_write_bytes(transaction / "journal.json",
                                               journal.dump()));

  const auto opened = editor_.project().open(manifest_);
  ASSERT_TRUE(opened.ok()) << opened.error_message();
  ASSERT_NE(project_.manifest(), nullptr);
  EXPECT_TRUE(project_.manifest()->is_material_deleted(deletion_material_id()));
  EXPECT_TRUE(project_.manifest()->is_material_deleted(subsequently_deleted));
  EXPECT_EQ(deletion_read_bytes(manifest_), newer_manifest);
  EXPECT_FALSE(std::filesystem::exists(material_path));
  EXPECT_FALSE(std::filesystem::exists(transaction));
}

TEST_F(ProjectMaterialDeletionTests, CommittedJournalKeepsDeletionAndFinishesCleanup) {
  ASSERT_TRUE(editor_.project().close().ok());
  auto workspace = ProjectManifestStore::open(manifest_);
  ASSERT_TRUE(workspace.ok()) << workspace.error().message;
  const std::array ids{deletion_material_id()};
  ASSERT_TRUE(workspace.value().manifest.mark_materials_deleted(ids).ok());
  ASSERT_TRUE(ProjectManifestStore::save_atomic(workspace.value()).ok());
  const auto transaction = root_ / ".quader/material-deletions" /
      "2933ca87-7f87-4500-ac6f-f6e2cc3b2615";
  std::filesystem::create_directories(transaction);
  std::filesystem::rename(root_ / "Materials/Delete", transaction / "folder");
  const nlohmann::json journal{
      {"version", 1}, {"project", workspace.value().manifest.id().str()},
      {"folder", "Materials/Delete"}, {"state", "committed"},
      {"files", nlohmann::json::array()}};
  ASSERT_NO_FATAL_FAILURE(deletion_write_bytes(transaction / "journal.json", journal.dump()));
  const auto opened = editor_.project().open(manifest_);
  ASSERT_TRUE(opened.ok()) << opened.error_message();
  EXPECT_TRUE(project_.manifest()->is_material_deleted(deletion_material_id()));
  EXPECT_FALSE(std::filesystem::exists(root_ / "Materials/Delete"));
  EXPECT_EQ(project_.material_catalog().find(deletion_material_id()), nullptr);
  EXPECT_FALSE(std::filesystem::exists(transaction));
  EXPECT_TRUE(std::filesystem::exists(root_ / "Textures/shared.png"));
}

TEST_F(ProjectMaterialDeletionTests, TransactionRootAliasCannotDeleteAnotherProjectFolder) {
  const auto target = root_ / "Materials/Keep";
  const auto sentinel = target / "35073758-3e22-4823-adfa-3cc7155f095c/retained.txt";
  ASSERT_NO_FATAL_FAILURE(deletion_write_bytes(sentinel, "Preserve unrelated folder"));
  ASSERT_TRUE(editor_.project().close().ok());
  ASSERT_TRUE(editor_.project().open(manifest_).ok());
  std::filesystem::create_directories(root_ / ".quader");
  const auto alias = root_ / ".quader/material-deletions";
  std::error_code error;
  std::filesystem::create_directory_symlink(target, alias, error);
  if (error) {
    GTEST_SKIP() << "Directory symlink creation is unavailable: " << error.message();
  }
  const auto preview = editor_.project_assets().preview_delete_material_folder("Materials/Delete");
  ASSERT_TRUE(preview.ok());
  ASSERT_TRUE(preview.value().complete) << ::testing::PrintToString(preview.value().errors);
  const auto deleted = editor_.project_assets().delete_material_folder(
      "Materials/Delete", preview.value().revision);
  EXPECT_FALSE(deleted.ok());
  EXPECT_FALSE(deleted.changed());
  EXPECT_EQ(deletion_read_bytes(sentinel), "Preserve unrelated folder");
  EXPECT_TRUE(std::filesystem::exists(root_ / "Materials/Delete/wood.qmat"));
  ASSERT_TRUE(editor_.project().close().ok());
  const auto reopened = editor_.project().open(manifest_);
  EXPECT_FALSE(reopened.ok());
  EXPECT_EQ(deletion_read_bytes(sentinel), "Preserve unrelated folder");
  std::filesystem::remove(alias);
}

TEST_F(ProjectMaterialDeletionTests, PreparedRecoveryPreservesNewerTargetBeforeRestoringAnyFile) {
  PreparedDeletionRecovery recovery;
  ASSERT_NO_FATAL_FAILURE(prepare_interrupted_deletion(recovery));
  const std::string newer_manifest = recovery.replacement_manifest + "\n \n";
  ASSERT_NO_FATAL_FAILURE(deletion_write_bytes(manifest_, newer_manifest));
  const auto opened = editor_.project().open(manifest_);
  EXPECT_FALSE(opened.ok());
  EXPECT_EQ(deletion_read_bytes(manifest_), newer_manifest);
  EXPECT_EQ(deletion_read_bytes(root_ / "Scenes/Saved.qdr"), recovery.replacement_scene);
  EXPECT_FALSE(std::filesystem::exists(root_ / "Materials/Delete"));
  EXPECT_TRUE(std::filesystem::exists(recovery.directory / "folder/wood.qmat"));
  EXPECT_TRUE(std::filesystem::exists(recovery.directory / "journal.json"));
}

TEST_F(ProjectMaterialDeletionTests, PreparedRecoveryChecksLaterBackupBeforeRestoringAnyFile) {
  PreparedDeletionRecovery recovery;
  ASSERT_NO_FATAL_FAILURE(prepare_interrupted_deletion(recovery));
  ASSERT_NO_FATAL_FAILURE(deletion_write_bytes(recovery.directory / "file-1.old",
                                               "Corrupt later backup"));
  const auto opened = editor_.project().open(manifest_);
  EXPECT_FALSE(opened.ok());
  EXPECT_EQ(deletion_read_bytes(manifest_), recovery.replacement_manifest);
  EXPECT_EQ(deletion_read_bytes(root_ / "Scenes/Saved.qdr"), recovery.replacement_scene);
  EXPECT_FALSE(std::filesystem::exists(root_ / "Materials/Delete"));
  EXPECT_TRUE(std::filesystem::exists(recovery.directory / "folder/wood.qmat"));
  EXPECT_TRUE(std::filesystem::exists(recovery.directory / "journal.json"));
}

TEST_F(ProjectMaterialDeletionTests, PublishingRecoveryPreservesAmbiguousReplacementAndBackups) {
  PreparedDeletionRecovery recovery;
  ASSERT_NO_FATAL_FAILURE(prepare_interrupted_deletion(recovery));
  const auto journal_path = recovery.directory / "journal.json";
  const auto scene_backup = deletion_read_bytes(recovery.directory / "file-0.old");
  const auto manifest_backup = deletion_read_bytes(recovery.directory / "file-1.old");
  auto journal = nlohmann::json::parse(deletion_read_bytes(journal_path));
  journal["state"] = "publishing";
  journal["publishing_path"] = "Scenes/Saved.qdr";
  const auto publishing_journal = journal.dump();
  ASSERT_NO_FATAL_FAILURE(deletion_write_bytes(journal_path, publishing_journal));

  const auto opened = editor_.project().open(manifest_);
  EXPECT_FALSE(opened.ok());
  EXPECT_EQ(deletion_read_bytes(manifest_), recovery.replacement_manifest);
  EXPECT_EQ(deletion_read_bytes(root_ / "Scenes/Saved.qdr"), recovery.replacement_scene);
  EXPECT_EQ(deletion_read_bytes(recovery.directory / "file-0.old"), scene_backup);
  EXPECT_EQ(deletion_read_bytes(recovery.directory / "file-1.old"), manifest_backup);
  EXPECT_EQ(deletion_read_bytes(journal_path), publishing_journal);
  EXPECT_FALSE(std::filesystem::exists(root_ / "Materials/Delete"));
  EXPECT_TRUE(std::filesystem::exists(recovery.directory / "folder/wood.qmat"));
}

TEST_F(ProjectMaterialDeletionTests, PublishingRecoveryUsesVerifiedPrivateRollbackAndCleansArtifact) {
  PreparedDeletionRecovery recovery;
  ASSERT_NO_FATAL_FAILURE(prepare_interrupted_deletion(recovery));
  const auto journal_path = recovery.directory / "journal.json";
  const auto scene_baseline = deletion_read_bytes(recovery.directory / "file-0.old");
  const auto manifest_baseline = deletion_read_bytes(recovery.directory / "file-1.old");
  const std::string rollback_path = "Scenes/.Saved.qdr.interrupted.rollback";
  ASSERT_NO_FATAL_FAILURE(deletion_write_bytes(root_ / rollback_path, scene_baseline));
  auto journal = nlohmann::json::parse(deletion_read_bytes(journal_path));
  journal["state"] = "publishing";
  journal["publishing_path"] = "Scenes/Saved.qdr";
  journal["files"][0]["rollback_path"] = rollback_path;
  ASSERT_NO_FATAL_FAILURE(deletion_write_bytes(journal_path, journal.dump()));

  const auto opened = editor_.project().open(manifest_);
  ASSERT_TRUE(opened.ok()) << opened.error_message();
  EXPECT_EQ(deletion_read_bytes(root_ / "Scenes/Saved.qdr"), scene_baseline);
  EXPECT_EQ(deletion_read_bytes(manifest_), manifest_baseline);
  EXPECT_FALSE(project_.manifest()->is_material_deleted(deletion_material_id()));
  EXPECT_TRUE(std::filesystem::exists(root_ / "Materials/Delete/wood.qmat"));
  EXPECT_FALSE(std::filesystem::exists(root_ / rollback_path));
  EXPECT_FALSE(std::filesystem::exists(recovery.directory));
}

TEST_F(ProjectMaterialDeletionTests, CommittedRecoveryRetriesFailedSceneNotificationWithExactFingerprint) {
  ASSERT_TRUE(create_box("Bridge dependency").ok());
  ASSERT_NO_FATAL_FAILURE(save_active());
  const auto scene_id = project_.active_scene()->scene_id;
  const auto bridge_directory = root_ / ".quader/bridge/scenes";
  ASSERT_TRUE(std::filesystem::is_directory(bridge_directory));
  std::filesystem::rename(bridge_directory, root_ / ".quader/bridge/previous-scenes");
  ASSERT_NO_FATAL_FAILURE(deletion_write_bytes(bridge_directory, "Notification destination unavailable"));
  ASSERT_NO_FATAL_FAILURE(delete_folder());
  const auto transaction_root = root_ / ".quader/material-deletions";
  ASSERT_TRUE(std::filesystem::is_directory(transaction_root));
  EXPECT_FALSE(std::filesystem::is_empty(transaction_root));
  const auto committed_bytes = deletion_read_bytes(root_ / "Scenes/Saved.qdr");
  const auto committed_hash = quader_io::qdr_binary_content_hash(
      std::as_bytes(std::span(committed_bytes.data(), committed_bytes.size())));
  ASSERT_TRUE(editor_.project().close().ok());
  std::filesystem::remove(bridge_directory);
  const auto opened = editor_.project().open(manifest_);
  ASSERT_TRUE(opened.ok()) << opened.error_message();
  const auto notification_path = bridge_directory / (std::string(scene_id.str()) + ".json");
  ASSERT_TRUE(std::filesystem::is_regular_file(notification_path));
  const auto notification = nlohmann::json::parse(deletion_read_bytes(notification_path));
  EXPECT_EQ(notification.at("document_id").get<std::string>(), scene_id.str());
  EXPECT_EQ(notification.at("project_id").get<std::string>(), project_.manifest()->id().str());
  EXPECT_EQ(notification.at("relative_path"), "Scenes/Saved.qdr");
  EXPECT_EQ(std::stoull(notification.at("byte_count").get<std::string>()), committed_bytes.size());
  EXPECT_EQ(std::stoull(notification.at("content_hash").get<std::string>(), nullptr, 16), committed_hash);
  EXPECT_EQ(deletion_read_bytes(root_ / "Scenes/Saved.qdr"), committed_bytes);
  EXPECT_TRUE(std::filesystem::is_empty(transaction_root));
}

} // namespace
} // namespace quader
