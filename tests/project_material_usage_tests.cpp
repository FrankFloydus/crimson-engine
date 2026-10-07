////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"
#include "assets/default_material_catalog.hpp"
#include "editor/material/project_material_usage_service.hpp"
#include "editor/session/project_session.hpp"
#include "io/project/qmat_codec.hpp"
#include "io/qdr/qdr_document_reader.hpp"
#include "io/qdr/qdr_document_writer.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <set>
#include <string>

namespace quader {
namespace {

/// Keeps fixture files alive until the project and all pinned snapshots expire.
class UsageTestDirectory final {
public:
  UsageTestDirectory()
      : path_(std::filesystem::temp_directory_path() /
              ("quader-material-usage-" + std::to_string(
                  std::chrono::steady_clock::now().time_since_epoch().count()))) {
    std::filesystem::create_directories(path_ / "Project");
  }
  ~UsageTestDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }
  const std::filesystem::path &path() const { return path_; }

private:
  std::filesystem::path path_;
};

ProjectRelativePath usage_path(std::string_view path) {
  auto parsed = ProjectRelativePath::parse_portable(path);
  EXPECT_TRUE(parsed.ok()) << parsed.error().message;
  return parsed.take_value();
}

std::set<ProjectAssetId> project_ids(const QuaderProjectMaterialUsageSnapshot &snapshot) {
  std::set<ProjectAssetId> result;
  for (const auto &reference : snapshot.used_materials) {
    if (reference.project_owned()) result.insert(reference.project_asset);
  }
  return result;
}

/// Builds actual modeled faces through public APIs and inspects derived usage.
class ProjectMaterialUsageTests : public testing::Test {
protected:
  void SetUp() override {
    project_.set_resource_root(std::filesystem::path(QUADER_SOURCE_DIR) /
                              "resources" / "modeling_assets");
    const auto created = project_.create(directory_.path() / "Project");
    ASSERT_TRUE(created.ok) << created.message;
    editor_ = std::make_unique<QuaderEditorApi>(project_);
  }

  ProjectAssetId add_material(std::string name) {
    ProjectMaterialDocument document{.id = ProjectAssetId::generate(), .name = name};
    const auto written = QmatCodec::write_atomic(project_.workspace()->file_system,
        usage_path("Materials/" + name + ".qmat"), document);
    EXPECT_TRUE(written.ok()) << written.error().message;
    auto catalog = ProjectMaterialCatalog::scan(project_.workspace()->file_system);
    EXPECT_TRUE(catalog.ok()) << catalog.error().message;
    if (catalog.ok()) EXPECT_TRUE(project_.install_material_catalog(catalog.take_value()).ok);
    return document.id;
  }

  QuaderModelingObjectRef box(ProjectAssetId material) {
    const auto created = editor_->modeling().create_box({
        .bounds = {.min = {0.0f, 0.0f, 0.0f}, .max = {16.0f, 16.0f, 16.0f}},
        .name = "Usage box",
        .material = QuaderMaterialReference::project(material),
    });
    EXPECT_TRUE(created.ok()) << created.error_message();
    return created.ok() ? created.value() : QuaderModelingObjectRef{};
  }

  void assign_all(QuaderModelingObjectRef object, ProjectAssetId material) {
    if (editor_->mode().current() == QuaderEditorMode::Object) {
      ASSERT_TRUE(editor_->selection().select_object(object).ok());
      ASSERT_TRUE(editor_->mode().set(QuaderEditorMode::Edit).ok());
    }
    EXPECT_TRUE(editor_->selection().set_mode(QuaderSelectionMode::Face).ok());
    EXPECT_TRUE(editor_->selection().select_faces(object, {1, 2, 3, 4, 5, 6}).ok());
    const auto selection = editor_->modeling().face_texture_settings();
    const auto assigned = editor_->modeling().apply_material_to_selected_faces(
        QuaderMaterialReference::project(material), object.scene_session_id,
        selection.selection_token);
    EXPECT_TRUE(assigned.ok()) << assigned.error_message();
  }

  QuaderProjectMaterialUsageSnapshot usage(bool refresh = false) {
    const auto snapshot = editor_->project_assets().material_usage(refresh);
    EXPECT_TRUE(snapshot.ok()) << snapshot.error_message();
    return snapshot.ok() ? snapshot.value() : QuaderProjectMaterialUsageSnapshot{};
  }

  UsageTestDirectory directory_;
  ProjectSession project_{directory_.path() / "recent.json"};
  std::unique_ptr<QuaderEditorApi> editor_;
};

TEST_F(ProjectMaterialUsageTests, EmptyAndUnchangedQueriesDoNoRepeatedDerivedWork) {
  const auto empty = usage();
  EXPECT_TRUE(empty.complete);
  EXPECT_TRUE(empty.used_materials.empty());
  const auto retained = project_.material_usage().snapshot();
  const auto work = project_.material_usage().work_stats();
  for (int index = 0; index < 12; ++index) {
    const auto revision = editor_->project_assets().material_usage_revision();
    ASSERT_TRUE(revision.ok());
    EXPECT_EQ(revision.value().revision, empty.revision);
    EXPECT_EQ(project_.material_usage().snapshot(), retained);
  }
  EXPECT_EQ(project_.material_usage().work_stats(), work);
}

TEST_F(ProjectMaterialUsageTests, LiveAndUnsavedScenesReplaceSavedAssignments) {
  const auto saved = add_material("Saved");
  const auto live = add_material("Live");
  const auto unsaved = add_material("Unsaved");
  const auto dormant = add_material("Dormant");
  const auto object = box(saved);
  auto &scene = project_.active_scene()->session->scene();
  auto material = *scene.material(object.entity);
  material.material_slots.emplace(73u, MaterialReference::project(dormant));
  ASSERT_TRUE(scene.set_material(object.entity, material));
  ASSERT_TRUE(editor_->scene_documents().save_as(object.scene_session_id,
                                                usage_path("Scenes/Saved.qdr")).ok());
  assign_all(object, live);
  ASSERT_TRUE(editor_->scene_documents().new_scene().ok());
  (void)box(unsaved);
  EXPECT_EQ(project_ids(usage()), (std::set<ProjectAssetId>{live, unsaved}));
  ASSERT_TRUE(project_.close_scene(object.scene_session_id, true).ok);
  const auto closed = usage();
  EXPECT_TRUE(closed.complete);
  EXPECT_EQ(project_ids(closed), (std::set<ProjectAssetId>{saved, unsaved}));
  EXPECT_TRUE(closed.issues.empty());
}

TEST_F(ProjectMaterialUsageTests, PassiveAssetDiscoveryUpdatesExternallyEditedClosedSceneUsage) {
  const auto first = add_material("First");
  const auto second = add_material("Second");
  const auto object = box(first);
  ASSERT_TRUE(editor_->scene_documents().save_as(object.scene_session_id,
                                                usage_path("Scenes/Closed.qdr")).ok());
  assign_all(object, second);
  ASSERT_TRUE(editor_->scene_documents().save(object.scene_session_id).ok());
  const auto file_path = project_.root() / "Scenes" / "Closed.qdr";
  std::ifstream replacement(file_path, std::ios::binary);
  ASSERT_TRUE(replacement.is_open());
  const std::string replacement_bytes((std::istreambuf_iterator<char>(replacement)), {});
  replacement.close();
  ASSERT_FALSE(replacement_bytes.empty());
  assign_all(object, first);
  ASSERT_TRUE(editor_->scene_documents().save(object.scene_session_id).ok());
  ASSERT_TRUE(editor_->scene_documents().new_scene().ok());
  ASSERT_TRUE(project_.close_scene(object.scene_session_id).ok);
  ASSERT_TRUE(editor_->project_assets().assets_if_changed().ok());
  ASSERT_EQ(project_ids(usage()), (std::set<ProjectAssetId>{first}));

  const auto default_material = *ProjectAssetId::parse(kDefaultProjectMaterialId);
  const auto image = project_.material_texture_image(
      default_material, StandardMaterialTextureSlot::Albedo);
  ASSERT_NE(image, nullptr);
  const auto texture_revision = project_.material_texture_revision();
  const auto before_edit = std::filesystem::last_write_time(file_path);
  {
    std::ofstream changed(file_path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(changed.is_open());
    changed.write(replacement_bytes.data(),
                  static_cast<std::streamsize>(replacement_bytes.size()));
    ASSERT_TRUE(changed.good());
  }
  std::filesystem::last_write_time(file_path, before_edit + std::chrono::seconds(2));

  ASSERT_TRUE(editor_->project_assets().assets_if_changed().ok());
  const auto changed_usage = usage();
  EXPECT_TRUE(changed_usage.complete);
  EXPECT_EQ(project_ids(changed_usage), (std::set<ProjectAssetId>{second}));
  EXPECT_EQ(project_.material_texture_revision(), texture_revision);
  EXPECT_EQ(project_.material_texture_image(
      default_material, StandardMaterialTextureSlot::Albedo), image);
}

TEST_F(ProjectMaterialUsageTests, LastFaceAssignmentNoOpUndoAndRedoUpdateActualUsage) {
  const auto first = add_material("First");
  const auto second = add_material("Second");
  const auto object = box(first);
  ASSERT_TRUE(editor_->selection().select_object(object).ok());
  ASSERT_TRUE(editor_->mode().set(QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(editor_->selection().set_mode(QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor_->selection().select_faces(object, {1, 2, 3, 4, 5}).ok());
  auto token = editor_->modeling().face_texture_settings().selection_token;
  ASSERT_TRUE(editor_->modeling().apply_material_to_selected_faces(
      QuaderMaterialReference::project(second), object.scene_session_id, token).ok());
  EXPECT_EQ(project_ids(usage()), (std::set<ProjectAssetId>{first, second}));
  ASSERT_TRUE(editor_->selection().select_face(object, 6).ok());
  token = editor_->modeling().face_texture_settings().selection_token;
  ASSERT_TRUE(editor_->modeling().apply_material_to_selected_faces(
      QuaderMaterialReference::project(second), object.scene_session_id, token).ok());
  EXPECT_EQ(project_ids(usage()), (std::set<ProjectAssetId>{second}));
  ASSERT_TRUE(editor_->history().undo().ok());
  EXPECT_EQ(project_ids(usage()), (std::set<ProjectAssetId>{first, second}));
  const auto work = project_.material_usage().work_stats();
  token = editor_->modeling().face_texture_settings().selection_token;
  const auto noop = editor_->modeling().apply_material_to_selected_faces(
      QuaderMaterialReference::project(first), object.scene_session_id, token);
  ASSERT_TRUE(noop.ok()) << noop.error_message();
  EXPECT_FALSE(noop.changed());
  EXPECT_TRUE(editor_->history().can_redo());
  EXPECT_EQ(project_ids(usage()), (std::set<ProjectAssetId>{first, second}));
  EXPECT_EQ(project_.material_usage().work_stats(), work);
  ASSERT_TRUE(editor_->history().redo().ok());
  EXPECT_EQ(project_ids(usage()), (std::set<ProjectAssetId>{second}));
}

TEST_F(ProjectMaterialUsageTests, HiddenLockedGeometryCountsWithoutRescanningVisibilityChanges) {
  const auto used = add_material("Hidden");
  const auto object = box(used);
  EXPECT_EQ(project_ids(usage()), (std::set<ProjectAssetId>{used}));
  const auto work = project_.material_usage().work_stats();
  auto &scene = project_.active_scene()->session->scene();
  ASSERT_TRUE(scene.set_visible(object.entity, false));
  ASSERT_TRUE(scene.set_locked(object.entity, true));
  EXPECT_EQ(project_ids(usage()), (std::set<ProjectAssetId>{used}));
  EXPECT_EQ(project_.material_usage().work_stats(), work);
}

TEST_F(ProjectMaterialUsageTests, CorruptClosedSceneKeepsConfirmedUsageAndDoesNotFailAssignment) {
  const auto disk_material = add_material("Disk");
  const auto live_material = add_material("Current");
  const auto next_material = add_material("Next");
  const auto disk = box(disk_material);
  ASSERT_TRUE(editor_->scene_documents().save_as(disk.scene_session_id,
                                                usage_path("Scenes/Disk.qdr")).ok());
  ASSERT_TRUE(editor_->scene_documents().new_scene().ok());
  const auto live = box(live_material);
  ASSERT_TRUE(project_.close_scene(disk.scene_session_id).ok);
  const auto file_path = project_.root() / "Scenes" / "Disk.qdr";
  std::ifstream original(file_path, std::ios::binary);
  const std::string bytes((std::istreambuf_iterator<char>(original)), {});
  original.close();
  { std::ofstream broken(file_path, std::ios::binary | std::ios::trunc); broken << "invalid"; }
  const auto incomplete = usage(true);
  ASSERT_FALSE(incomplete.complete);
  ASSERT_EQ(incomplete.issues.size(), 1u);
  EXPECT_EQ(incomplete.issues.front().project_relative_path, "Scenes/Disk.qdr");
  EXPECT_EQ(project_ids(incomplete), (std::set<ProjectAssetId>{live_material}));
  const auto before_assignment = project_.material_usage().work_stats();
  assign_all(live, next_material);
  const auto after = usage();
  EXPECT_FALSE(after.complete);
  EXPECT_EQ(after.issues, incomplete.issues);
  EXPECT_EQ(project_ids(after), (std::set<ProjectAssetId>{next_material}));
  EXPECT_EQ(project_.material_usage().work_stats().saved_scene_read_count,
            before_assignment.saved_scene_read_count);
  EXPECT_EQ(project_.material_usage().work_stats().file_stamp_read_count,
            before_assignment.file_stamp_read_count);
  ASSERT_TRUE(editor_->history().undo().ok());
  EXPECT_EQ(project_ids(usage()), (std::set<ProjectAssetId>{live_material}));
  { std::ofstream restored(file_path, std::ios::binary | std::ios::trunc); restored.write(bytes.data(), static_cast<std::streamsize>(bytes.size())); }
  project_.material_usage().invalidate();
  const auto complete = usage();
  EXPECT_TRUE(complete.complete);
  EXPECT_TRUE(complete.issues.empty());
  EXPECT_EQ(project_ids(complete), (std::set<ProjectAssetId>{disk_material, live_material}));
}

TEST_F(ProjectMaterialUsageTests, MaterialBindingChangesReuseSavedFilesAndMeshSlotProjections) {
  const auto first = add_material("First");
  const auto second = add_material("Second");
  const auto object = box(first);
  ASSERT_TRUE(editor_->scene_documents().save_as(object.scene_session_id,
                                                usage_path("Scenes/Closed.qdr")).ok());
  ASSERT_TRUE(editor_->scene_documents().new_scene().ok());
  const auto current = box(first);
  ASSERT_TRUE(project_.close_scene(object.scene_session_id).ok);
  EXPECT_EQ(project_ids(usage()), (std::set<ProjectAssetId>{first}));
  const auto before = project_.material_usage().work_stats();
  auto &scene = project_.active_scene()->session->scene();
  auto binding = *scene.material(current.entity);
  binding.material = MaterialReference::project(second);
  ASSERT_TRUE(scene.set_material(current.entity, std::move(binding)));
  EXPECT_EQ(project_ids(usage()), (std::set<ProjectAssetId>{first, second}));
  const auto after = project_.material_usage().work_stats();
  EXPECT_EQ(after.saved_scene_read_count, before.saved_scene_read_count);
  EXPECT_EQ(after.file_stamp_read_count, before.file_stamp_read_count);
  EXPECT_EQ(after.geometry_slot_scan_count, before.geometry_slot_scan_count);
}

TEST_F(ProjectMaterialUsageTests, OnlySolidCurvesContributeTheirEffectiveSurfaceMaterial) {
  const auto solid = add_material("Solid");
  const auto dormant = add_material("Dormant");
  const auto path_material = add_material("Path");
  const auto make_curve = [&] {
    return editor_->modeling().create_curve({
        .points = {{{0.0, 0.0, 0.0}}, {{10.0, 0.0, 0.0}}},
    });
  };
  const auto tube = make_curve();
  ASSERT_TRUE(tube.ok()) << tube.error_message();
  const auto path = make_curve();
  ASSERT_TRUE(path.ok()) << path.error_message();
  auto &scene = project_.active_scene()->session->scene();
  MaterialComponent tube_material{.material = MaterialReference::project(dormant)};
  tube_material.material_slots.emplace(0u, MaterialReference::project(solid));
  ASSERT_TRUE(scene.set_material(tube.value().entity, std::move(tube_material)));
  ASSERT_TRUE(scene.set_material(path.value().entity,
                                 {.material = MaterialReference::project(path_material)}));
  auto settings = *scene.curve(tube.value().entity);
  settings.solidify = true;
  ASSERT_TRUE(scene.set_curve(tube.value().entity, settings));
  EXPECT_EQ(project_ids(usage()), (std::set<ProjectAssetId>{solid}));
  const auto session = project_.active_scene_id();
  ASSERT_TRUE(editor_->scene_documents().save_as(session, usage_path("Scenes/Curves.qdr")).ok());
  ASSERT_TRUE(editor_->scene_documents().new_scene().ok());
  ASSERT_TRUE(project_.close_scene(session).ok);
  const auto disk = usage();
  EXPECT_TRUE(disk.complete);
  EXPECT_EQ(project_ids(disk), (std::set<ProjectAssetId>{solid}));
}

TEST_F(ProjectMaterialUsageTests, RetainedSnapshotSurvivesRuntimeReplacementAndProjectClose) {
  const auto used = add_material("Used");
  (void)box(used);
  const auto before = project_.material_usage().snapshot();
  ASSERT_EQ(before->material_ids, (std::vector<ProjectAssetId>{used}));
  project_.active_scene()->session->replace_document(SceneContentDocument{});
  const auto empty = usage();
  EXPECT_TRUE(empty.complete);
  EXPECT_TRUE(empty.used_materials.empty());
  EXPECT_EQ(before->material_ids, (std::vector<ProjectAssetId>{used}));
  ASSERT_TRUE(project_.close(true).ok);
  EXPECT_FALSE(editor_->project_assets().material_usage_revision().ok());
  EXPECT_EQ(before->material_ids, (std::vector<ProjectAssetId>{used}));
}

TEST_F(ProjectMaterialUsageTests, ExplicitRefreshRetriesFailedDeferredGeometryWithoutHydration) {
  const auto ordinary = add_material("Ordinary");
  const auto deferred = add_material("Deferred");
  const auto original = box(ordinary);
  auto &session = *project_.active_scene()->session;
  SceneContentDocument deferred_source;
  const auto *original_mesh = session.assets().mesh(original.mesh);
  ASSERT_NE(original_mesh, nullptr);
  ASSERT_EQ(deferred_source.assets().add_mesh(original.mesh, *original_mesh), original.mesh);
  // The native reader defers unreferenced assets, while visible scene meshes hydrate.
  const auto bytes = QdrDocumentWriter::serialize_project_document_bytes(deferred_source);
  const auto read = QdrDocumentReader::deserialize_project_document(bytes);
  ASSERT_TRUE(read.ok) << read.message;
  ASSERT_FALSE(read.document.assets().mesh_materialized(original.mesh));
  const auto encoded_source = read.document.assets().mesh_write_source(original.mesh);
  ASSERT_TRUE(encoded_source.ok) << encoded_source.message;
  ASSERT_NE(encoded_source.source.encoded_geometry, nullptr);
  bool available = false;
  int encoded_reads = 0;
  int hydrations = 0;
  const auto encoded = encoded_source.source.encoded_geometry;
  const AssetId deferred_id = 9'999u;
  ASSERT_EQ(session.assets().add_deferred_mesh(deferred_id, "Deferred", true, true,
      [&] { ++hydrations; return MeshAsset{}; },
      [&]() -> std::shared_ptr<const std::vector<std::byte>> {
        ++encoded_reads;
        return available ? encoded : std::make_shared<const std::vector<std::byte>>();
      }), deferred_id);
  const auto entity = session.scene().create_entity("Deferred surface");
  ASSERT_TRUE(session.scene().set_mesh(entity, {.mesh = deferred_id}));
  ASSERT_TRUE(session.scene().set_material(entity,
                                          {.material = MaterialReference::project(deferred)}));
  const auto failed = usage();
  EXPECT_FALSE(failed.complete);
  EXPECT_EQ(project_ids(failed), (std::set<ProjectAssetId>{ordinary}));
  EXPECT_EQ(encoded_reads, 1);
  available = true;
  EXPECT_FALSE(usage().complete);
  EXPECT_EQ(encoded_reads, 1);
  project_.material_usage().invalidate();
  const auto recovered = usage();
  EXPECT_TRUE(recovered.complete);
  EXPECT_EQ(project_ids(recovered), (std::set<ProjectAssetId>{ordinary, deferred}));
  EXPECT_EQ(encoded_reads, 2);
  EXPECT_EQ(hydrations, 0);
  EXPECT_FALSE(session.assets().mesh_materialized(deferred_id));
}

TEST_F(ProjectMaterialUsageTests, ActualRetiredSavedSceneDoesNotPoisonAssignment) {
  const char *configured = std::getenv("QUADER_TEST_LEGACY_MATERIAL_USAGE_SCENE");
  if (configured == nullptr || *configured == '\0') {
    GTEST_SKIP() << "Set QUADER_TEST_LEGACY_MATERIAL_USAGE_SCENE for the historical project repro.";
  }
  const auto first = add_material("First");
  const auto second = add_material("Second");
  ASSERT_TRUE(editor_->scene_documents().save_as(project_.active_scene_id(),
                                                usage_path("Scenes/Current.qdr")).ok());
  const std::filesystem::path source(configured);
  const auto legacy = QdrDocumentReader::read_project_material_usage(source);
  ASSERT_TRUE(legacy.ok) << legacy.message;
  ASSERT_TRUE(legacy.complete) << legacy.message;
  const auto relative = usage_path("Scenes/LegacyUsage.qdr");
  std::filesystem::copy_file(source, project_.root() / "Scenes" / "LegacyUsage.qdr");
  auto manifest = *project_.manifest();
  ASSERT_TRUE(manifest.register_scene({SceneId::from_bytes(legacy.scene_uuid), relative}).ok());
  const auto manifest_path = project_.manifest_path();
  ASSERT_TRUE(ProjectManifestStore::save_atomic(project_.workspace()->file_system,
                                               manifest_path, manifest).ok());
  editor_.reset();
  ASSERT_TRUE(project_.open(manifest_path, true).ok);
  editor_ = std::make_unique<QuaderEditorApi>(project_);
  if (!project_.has_active_scene()) ASSERT_TRUE(editor_->scene_documents().new_scene().ok());
  const auto object = box(first);
  const auto before = usage();
  ASSERT_TRUE(before.complete);
  EXPECT_TRUE(before.issues.empty());
  assign_all(object, second);
  const auto assigned = usage();
  ASSERT_TRUE(assigned.complete);
  EXPECT_TRUE(assigned.issues.empty());
  EXPECT_TRUE(project_ids(assigned).contains(second));
  const auto work = project_.material_usage().work_stats();
  const auto settings = editor_->modeling().face_texture_settings();
  const auto noop = editor_->modeling().apply_material_to_selected_faces(
      QuaderMaterialReference::project(second), object.scene_session_id,
      settings.selection_token);
  ASSERT_TRUE(noop.ok()) << noop.error_message();
  EXPECT_FALSE(noop.changed());
  (void)usage();
  EXPECT_EQ(project_.material_usage().work_stats(), work);
  ASSERT_TRUE(editor_->history().undo().ok());
  EXPECT_EQ(project_ids(usage()), project_ids(before));
  ASSERT_TRUE(editor_->history().redo().ok());
  EXPECT_EQ(project_ids(usage()), project_ids(assigned));
}

TEST_F(ProjectMaterialUsageTests, DeletedRawReferencesResolveToDefaultInLiveAndClosedScenes) {
  const auto deleted = add_material("Deleted");
  const auto object = box(deleted);
  ASSERT_TRUE(editor_->scene_documents().save_as(object.scene_session_id,
                                                usage_path("Scenes/Deleted.qdr")).ok());
  auto manifest = *project_.manifest();
  ASSERT_TRUE(manifest.mark_materials_deleted(std::array{deleted}).ok());
  const auto manifest_path = project_.manifest_path();
  ASSERT_TRUE(ProjectManifestStore::save_atomic(project_.workspace()->file_system,
                                               manifest_path, manifest).ok());
  editor_.reset();
  ASSERT_TRUE(project_.open(manifest_path, true).ok);
  editor_ = std::make_unique<QuaderEditorApi>(project_);
  ASSERT_NE(project_.active_scene(), nullptr);
  auto &scene = project_.active_scene()->session->scene();
  const auto entities = scene.entities();
  const auto found = std::ranges::find_if(entities, [&](EntityId entity) {
    return scene.mesh(entity) != nullptr;
  });
  ASSERT_NE(found, entities.end());
  ASSERT_TRUE(scene.set_material(*found, {.material = MaterialReference::project(deleted)}));
  const auto expected = std::set<ProjectAssetId>{*ProjectAssetId::parse(kDefaultProjectMaterialId)};
  EXPECT_EQ(project_ids(usage()), expected);
  const auto scene_id = project_.active_scene_id();
  ASSERT_TRUE(editor_->scene_documents().new_scene().ok());
  ASSERT_TRUE(project_.close_scene(scene_id, true).ok);
  const auto disk = usage();
  EXPECT_TRUE(disk.complete);
  EXPECT_EQ(project_ids(disk), expected);
}

TEST_F(ProjectMaterialUsageTests, EmbeddedUsageIsScopedToTheActiveSceneAcrossTabSwitches) {
  const auto first_scene = project_.active_scene_id();
  const auto first_material = editor_->assets().add_material(QuaderMaterialArgs{.name = "First local"});
  ASSERT_TRUE(first_material.ok()) << first_material.error_message();
  const auto first_reference = QuaderMaterialReference::embedded(first_material.value().id);
  const auto created = editor_->modeling().create_box({
      .bounds = {.min = {0.0f, 0.0f, 0.0f}, .max = {4.0f, 4.0f, 4.0f}},
      .material = first_reference,
  });
  ASSERT_TRUE(created.ok()) << created.error_message();
  EXPECT_EQ(usage().used_materials, (std::vector<QuaderMaterialReference>{first_reference}));
  ASSERT_TRUE(editor_->scene_documents().new_scene().ok());
  const auto second_material = editor_->assets().add_material(QuaderMaterialArgs{.name = "Second local"});
  ASSERT_TRUE(second_material.ok()) << second_material.error_message();
  EXPECT_TRUE(usage().used_materials.empty());
  ASSERT_TRUE(editor_->scene_documents().activate(first_scene).ok());
  EXPECT_EQ(usage().used_materials, (std::vector<QuaderMaterialReference>{first_reference}));
}

} // namespace
} // namespace quader
