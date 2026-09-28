////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/material/project_asset_dependency_service.hpp"

#include "assets/default_material_catalog.hpp"
#include "editor/assets/project_asset_deletion_service.hpp"
#include "editor/assets/project_asset_service.hpp"
#include "editor/material/standard_material_edit_session_service.hpp"
#include "editor/session/project_session.hpp"
#include "io/project/qmat_codec.hpp"

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
#include <string>
#include <string_view>

namespace quader {
namespace {

/// Releases temporary data only after the project and dependency owners die.
class DependencyTestDirectory final {
public:
  DependencyTestDirectory()
      : path_(std::filesystem::temp_directory_path() /
              ("quader-dependency-" +
               std::to_string(std::chrono::steady_clock::now()
                                  .time_since_epoch().count()))) {
    std::filesystem::create_directories(path_ / "Project");
  }
  ~DependencyTestDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }
  [[nodiscard]] const std::filesystem::path &path() const { return path_; }

private:
  std::filesystem::path path_;
};

ProjectRelativePath relative_path(std::string_view path) {
  auto parsed = ProjectRelativePath::parse_portable(path);
  EXPECT_TRUE(parsed.ok()) << parsed.error().message;
  return parsed.take_value();
}

std::set<ProjectAssetId> material_set(const ProjectSceneAssetDependencies &scene) {
  return {scene.material_ids.begin(), scene.material_ids.end()};
}

std::string dependency_read(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), {}};
}

void dependency_write(const std::filesystem::path &path, std::string_view bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  output.close();
  ASSERT_TRUE(output.good());
}

/// Exercises dependency derivation through the actual project/document owners.
class ProjectAssetDependencyTests : public ::testing::Test {
protected:
  void SetUp() override {
    project_.set_resource_root(std::filesystem::path(QUADER_SOURCE_DIR) /
                               "resources" / "modeling_assets");
    const auto created = project_.create(directory_.path() / "Project");
    ASSERT_TRUE(created.ok) << created.message;
  }

  ProjectAssetId add_material(std::string name,
                             std::optional<ProjectMaterialTextureSlot> texture = {}) {
    ProjectMaterialDocument document{.id = ProjectAssetId::generate(), .name = name};
    document.standard_material()->base_color_texture = std::move(texture);
    const auto written = QmatCodec::write_atomic(
        project_.workspace()->file_system,
        relative_path("Materials/" + name + ".qmat"), document);
    EXPECT_TRUE(written.ok()) << written.error().message;
    auto catalog = ProjectMaterialCatalog::scan(project_.workspace()->file_system);
    EXPECT_TRUE(catalog.ok()) << catalog.error().message;
    if (catalog.ok()) {
      const auto installed = project_.install_material_catalog(catalog.take_value());
      EXPECT_TRUE(installed.ok) << installed.message;
    }
    return document.id;
  }

  EntityId add_binding(ProjectAssetId primary, ProjectAssetId dormant = {}) {
    auto &scene = project_.active_scene()->session->scene();
    const auto entity = scene.create_empty("Bound object");
    MaterialComponent material{.material = MaterialReference::project(primary)};
    if (dormant.valid()) {
      material.material_slots.emplace(73u, MaterialReference::project(dormant));
    }
    EXPECT_TRUE(scene.set_material(entity, std::move(material)));
    return entity;
  }

  const ProjectSceneAssetDependencies *find_scene(
      const ProjectAssetDependencySnapshot &snapshot, SceneId id, bool live) {
    const auto found = std::ranges::find_if(snapshot.scenes, [&](const auto &scene) {
      return scene.scene_id == id && scene.live == live;
    });
    return found == snapshot.scenes.end() ? nullptr : &*found;
  }

  DependencyTestDirectory directory_;
  ProjectSession project_{directory_.path() / "recent.json"};
  ProjectAssetDependencyService dependencies_{project_};
};

TEST_F(ProjectAssetDependencyTests,
       SeparatesSavedDirtyLiveAndUnsavedBindingsIncludingDormantDefinitionSlots) {
  const auto saved_material = add_material("Saved");
  const auto dormant_material = add_material("Dormant");
  const auto live_material = add_material("Live");
  const auto unsaved_material = add_material("Unsaved");
  auto *saved_tab = project_.active_scene();
  const auto entity = add_binding(saved_material, dormant_material);
  auto &scene = saved_tab->session->scene();
  const auto definition = scene.create_entity("Definition");
  ASSERT_TRUE(scene.set_instance_definition(definition));
  ASSERT_TRUE(scene.set_parent(entity, definition));
  ASSERT_TRUE(scene.set_visible(definition, false));
  ASSERT_TRUE(scene.set_locked(definition, true));
  const auto saved = project_.save_as(saved_tab->session_id,
                                      relative_path("Scenes/Saved.qdr"));
  ASSERT_TRUE(saved.ok) << saved.message;
  const auto saved_id = saved_tab->scene_id;
  ASSERT_NE(project_.manifest()->find_scene(saved_id), nullptr);
  ASSERT_TRUE(scene.set_material(entity,
                                 {.material = MaterialReference::project(live_material)}));
  ASSERT_TRUE(saved_tab->dirty());
  ASSERT_TRUE(project_.new_scene().ok);
  const auto unsaved_id = project_.active_scene()->scene_id;
  (void)add_binding(unsaved_material);

  const auto snapshot = dependencies_.snapshot(true);
  ASSERT_TRUE(snapshot->complete)
      << (snapshot->errors.empty() ? "" : snapshot->errors.front());
  const auto *disk = find_scene(*snapshot, saved_id, false);
  const auto *live = find_scene(*snapshot, saved_id, true);
  const auto *unsaved = find_scene(*snapshot, unsaved_id, true);
  ASSERT_NE(disk, nullptr);
  ASSERT_NE(live, nullptr);
  ASSERT_NE(unsaved, nullptr);
  EXPECT_EQ(material_set(*disk),
            (std::set<ProjectAssetId>{saved_material, dormant_material}));
  EXPECT_EQ(material_set(*live), (std::set<ProjectAssetId>{live_material}));
  EXPECT_TRUE(live->dirty);
  EXPECT_EQ(material_set(*unsaved), (std::set<ProjectAssetId>{unsaved_material}));
  EXPECT_FALSE(unsaved->path.has_value());
  EXPECT_EQ(find_scene(*snapshot, unsaved_id, false), nullptr);
}

TEST_F(ProjectAssetDependencyTests,
       CanonicalizesDeletedIdsInSavedAndLiveSceneRecords) {
  const ProjectAssetId deleted = add_material("Deleted");
  (void)add_binding(deleted);
  auto *open = project_.active_scene();
  ASSERT_NE(open, nullptr);
  ASSERT_NE(open->session, nullptr);
  ASSERT_TRUE(project_.save_as(open->session_id,
                               relative_path("Scenes/Deleted.qdr"))
                  .ok);
  const SceneId scene_id = open->scene_id;

  const auto before = dependencies_.snapshot(true);
  ASSERT_TRUE(before->complete)
      << (before->errors.empty() ? "" : before->errors.front());
  const auto *saved_before = find_scene(*before, scene_id, false);
  const auto *live_before = find_scene(*before, scene_id, true);
  ASSERT_NE(saved_before, nullptr);
  ASSERT_NE(live_before, nullptr);
  EXPECT_EQ(material_set(*saved_before),
            (std::set<ProjectAssetId>{deleted}));
  EXPECT_EQ(material_set(*live_before),
            (std::set<ProjectAssetId>{deleted}));

  ProjectManifest manifest = *project_.manifest();
  ASSERT_TRUE(manifest.mark_materials_deleted(std::array{deleted}).ok());
  const std::filesystem::path manifest_path = project_.manifest_path();
  const ProjectWorkspaceStorage *workspace = project_.workspace();
  ASSERT_NE(workspace, nullptr);
  const auto saved_manifest = ProjectManifestStore::save_atomic(
      workspace->file_system, manifest_path, manifest);
  ASSERT_TRUE(saved_manifest.ok()) << saved_manifest.error().message;
  ASSERT_TRUE(project_.open(manifest_path, true).ok);

  open = project_.active_scene();
  ASSERT_NE(open, nullptr);
  ASSERT_NE(open->session, nullptr);
  auto &live_scene = open->session->scene();
  const std::vector<EntityId> entities = live_scene.entities();
  const auto bound_entity =
      std::ranges::find_if(entities, [&live_scene](EntityId candidate) {
        return live_scene.material(candidate) != nullptr;
      });
  ASSERT_NE(bound_entity, entities.end());
  const EntityId entity = *bound_entity;
  ASSERT_TRUE(live_scene.set_material(
      entity, {.material = MaterialReference::project(deleted)}));
  const MaterialComponent *raw_binding = live_scene.material(entity);
  ASSERT_NE(raw_binding, nullptr);
  ASSERT_TRUE(raw_binding->material.project_owned());
  EXPECT_EQ(raw_binding->material.project_asset(), deleted);

  const auto after = dependencies_.snapshot(true);
  ASSERT_TRUE(after->complete)
      << (after->errors.empty() ? "" : after->errors.front());
  const auto *saved_after = find_scene(*after, scene_id, false);
  const auto *live_after = find_scene(*after, scene_id, true);
  ASSERT_NE(saved_after, nullptr);
  ASSERT_NE(live_after, nullptr);
  const ProjectAssetId default_material =
      *ProjectAssetId::parse(kDefaultProjectMaterialId);
  EXPECT_EQ(material_set(*saved_after),
            (std::set<ProjectAssetId>{default_material}));
  EXPECT_EQ(material_set(*live_after),
            (std::set<ProjectAssetId>{default_material}));
}

TEST_F(ProjectAssetDependencyTests, InspectsRegisteredScenesAfterTheirTabsClose) {
  const auto id = add_material("Closed");
  (void)add_binding(id);
  const auto session_id = project_.active_scene_id();
  ASSERT_TRUE(project_.save_as(session_id, relative_path("Scenes/Closed.qdr")).ok);
  const auto scene_id = project_.active_scene()->scene_id;
  ASSERT_NE(project_.manifest()->find_scene(scene_id), nullptr);
  ASSERT_TRUE(project_.new_scene().ok);
  ASSERT_TRUE(project_.close_scene(session_id).ok);
  const auto snapshot = dependencies_.snapshot(true);
  ASSERT_TRUE(snapshot->complete);
  const auto *saved = find_scene(*snapshot, scene_id, false);
  ASSERT_NE(saved, nullptr);
  EXPECT_EQ(material_set(*saved), (std::set<ProjectAssetId>{id}));
  EXPECT_EQ(find_scene(*snapshot, scene_id, true), nullptr);
}

TEST_F(ProjectAssetDependencyTests, RetainsUnchangedSnapshotWithoutDiskOrEntityWork) {
  const auto id = add_material("Retained");
  (void)add_binding(id);
  ASSERT_TRUE(project_.save_as(project_.active_scene_id(),
                               relative_path("Scenes/Retained.qdr")).ok);
  const auto first = dependencies_.snapshot(true);
  ASSERT_TRUE(first->complete);
  const auto work = dependencies_.work_stats();
  for (int index = 0; index < 20; ++index) {
    EXPECT_EQ(dependencies_.snapshot().get(), first.get());
  }
  EXPECT_EQ(dependencies_.work_stats(), work);
  const auto refreshed = dependencies_.snapshot(true);
  EXPECT_EQ(refreshed.get(), first.get());
  EXPECT_EQ(refreshed->revision, first->revision);
  EXPECT_EQ(dependencies_.work_stats().live_scene_scan_count,
            work.live_scene_scan_count);
  EXPECT_GT(dependencies_.work_stats().saved_scene_read_count,
            work.saved_scene_read_count);
}

TEST_F(ProjectAssetDependencyTests,
       RetainsIncompleteSnapshotWhenOpenSceneDocumentRemainsUnavailable) {
  auto *open = project_.active_scene();
  ASSERT_NE(open, nullptr);
  ASSERT_NE(open->session, nullptr);
  auto unavailable = std::move(open->session);

  const auto first = dependencies_.snapshot(true);
  EXPECT_FALSE(first->complete);
  EXPECT_TRUE(std::ranges::any_of(first->errors, [](const auto &error) {
    return error.find("its document is unavailable") != std::string::npos;
  }));
  const auto work = dependencies_.work_stats();

  for (int index = 0; index < 20; ++index) {
    const auto retained = dependencies_.snapshot();
    EXPECT_EQ(retained.get(), first.get());
    EXPECT_FALSE(retained->complete);
    EXPECT_EQ(retained->errors, first->errors);
  }
  EXPECT_EQ(dependencies_.work_stats(), work);

  open->session = std::move(unavailable);
}

TEST_F(ProjectAssetDependencyTests, ReusesSavedMetadataWhenOnlyLiveBindingsChange) {
  const auto first_material = add_material("First");
  const auto second_material = add_material("Second");
  const auto entity = add_binding(first_material);
  ASSERT_TRUE(project_.save_as(project_.active_scene_id(),
                               relative_path("Scenes/Cached.qdr")).ok);
  const auto first = dependencies_.snapshot(true);
  ASSERT_TRUE(first->complete);
  const auto work = dependencies_.work_stats();
  ASSERT_TRUE(project_.active_scene()->session->scene().set_material(
      entity, {.material = MaterialReference::project(second_material)}));
  const auto changed = dependencies_.snapshot();
  ASSERT_TRUE(changed->complete);
  EXPECT_GT(changed->revision, first->revision);
  EXPECT_EQ(dependencies_.work_stats().saved_scene_read_count,
            work.saved_scene_read_count);
  EXPECT_EQ(dependencies_.work_stats().live_scene_scan_count,
            work.live_scene_scan_count + 1u);
}

TEST_F(ProjectAssetDependencyTests, StrongRefreshDetectsSameStampSourceCorruption) {
  const auto id = add_material("Fingerprint");
  (void)add_binding(id);
  ASSERT_TRUE(project_.save_as(project_.active_scene_id(),
                               relative_path("Scenes/Fingerprint.qdr")).ok);
  const auto first = dependencies_.snapshot(true);
  ASSERT_TRUE(first->complete);
  const auto path = project_.root() / "Scenes" / "Fingerprint.qdr";
  const auto modified = std::filesystem::last_write_time(path);
  {
    std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary);
    ASSERT_TRUE(file.is_open());
    file.put('X');
  }
  std::filesystem::last_write_time(path, modified);
  EXPECT_EQ(dependencies_.snapshot().get(), first.get());
  const auto changed = dependencies_.snapshot(true);
  EXPECT_FALSE(changed->complete);
  EXPECT_GT(changed->revision, first->revision);
  EXPECT_FALSE(changed->errors.empty());
}

TEST_F(ProjectAssetDependencyTests, StrongRefreshRejectsAnExternallyChangedCatalog) {
  const auto id = add_material("Changed");
  const auto first = dependencies_.snapshot(true);
  ASSERT_TRUE(first->complete);
  auto material = project_.material_catalog().find(id)->material;
  material.name = "Changed outside the editor";
  ASSERT_TRUE(QmatCodec::write_atomic(project_.workspace()->file_system,
                                      relative_path("Materials/Changed.qmat"),
                                      material).ok());
  const auto changed = dependencies_.snapshot(true);
  EXPECT_FALSE(changed->complete);
  EXPECT_GT(changed->revision, first->revision);
  EXPECT_TRUE(std::ranges::any_of(changed->errors, [](const auto &error) {
    return error.find("Refresh the material catalog") != std::string::npos;
  }));
}

TEST_F(ProjectAssetDependencyTests,
       FolderChangesPreserveMaterialDependenciesAndRetainTheirScan) {
  const auto id = add_material("FolderIndependent");
  const auto first = dependencies_.snapshot(true);
  ASSERT_TRUE(first->complete);
  ASSERT_TRUE(std::filesystem::create_directories(project_.root() / "Scenes/Nested/Empty"));
  ASSERT_TRUE(std::filesystem::create_directories(project_.root() / "Content/Empty"));
  const auto refreshed = dependencies_.snapshot(true);
  ASSERT_TRUE(refreshed->complete)
      << (refreshed->errors.empty() ? "" : refreshed->errors.front());
  const auto work = dependencies_.work_stats();
  auto &draft = project_.standard_material_editor();
  ASSERT_TRUE(draft.open(id).ok);
  ASSERT_TRUE(draft.set_roughness(0.25f).ok);
  const auto edited = dependencies_.snapshot();
  EXPECT_TRUE(edited->complete)
      << (edited->errors.empty() ? "" : edited->errors.front());
  EXPECT_EQ(dependencies_.work_stats().material_catalog_scan_count,
            work.material_catalog_scan_count);
  const std::array selected{relative_path("Content/Empty")};
  const auto preview = ProjectAssetDeletionService(project_).preview(selected);
  EXPECT_TRUE(preview.complete)
      << (preview.errors.empty() ? "" : preview.errors.front());
}

TEST_F(ProjectAssetDependencyTests,
       StrongRefreshRejectsExternallyAddedRemovedAndMovedMaterialFiles) {
  (void)add_material("Installed");
  const auto first = dependencies_.snapshot(true);
  ASSERT_TRUE(first->complete);
  ProjectMaterialDocument external{.id = ProjectAssetId::generate(), .name = "External"};
  const auto external_path = relative_path("Materials/External.qmat");
  ASSERT_TRUE(QmatCodec::write_atomic(project_.workspace()->file_system,
                                      external_path, external).ok());
  const auto added = dependencies_.snapshot(true);
  EXPECT_FALSE(added->complete);
  ASSERT_TRUE(std::filesystem::remove(project_.root() / external_path.native_path()));
  ASSERT_TRUE(dependencies_.snapshot(true)->complete);
  const auto installed_path = project_.root() / "Materials/Installed.qmat";
  const auto moved_path = project_.root() / "Materials/Moved.qmat";
  std::filesystem::rename(installed_path, moved_path);
  EXPECT_FALSE(dependencies_.snapshot(true)->complete);
  ASSERT_TRUE(std::filesystem::remove(moved_path));
  EXPECT_FALSE(dependencies_.snapshot(true)->complete);
}

TEST_F(ProjectAssetDependencyTests, IncludesSavedAndPendingDraftTextureDependencies) {
  const auto texture_id = ProjectAssetId::generate();
  const auto texture_path = project_.root() / "Textures" / "shared.png";
  QImage image(2, 2, QImage::Format_RGBA8888);
  image.fill(0xffaabbccu);
  ASSERT_TRUE(image.save(QString::fromStdWString(texture_path.wstring()), "PNG"));
  const ProjectMaterialTextureSlot texture{
      .id = texture_id,
      .project_relative_path = "Textures/shared.png",
  };
  const auto first_id = add_material("SharedOne", texture);
  const auto second_id = add_material("SharedTwo", texture);
  auto &editor = project_.standard_material_editor();
  ASSERT_TRUE(editor.open(first_id).ok);
  const auto external = directory_.path() / "pending.png";
  ASSERT_TRUE(image.save(QString::fromStdWString(external.wstring()), "PNG"));
  ASSERT_TRUE(editor.import_texture(StandardMaterialTextureSlot::Albedo, external).ok);
  const auto snapshot = dependencies_.snapshot(true);
  ASSERT_TRUE(snapshot->complete);
  const auto draft = std::ranges::find_if(snapshot->materials, [&](const auto &material) {
    return material.id == first_id && material.draft;
  });
  ASSERT_NE(draft, snapshot->materials.end());
  ASSERT_EQ(draft->textures.size(), 1u);
  EXPECT_TRUE(draft->dirty);
  EXPECT_EQ(draft->textures.front().texture_id, texture_id);
  EXPECT_EQ(draft->textures.front().path, relative_path("Textures/shared.png"));
  EXPECT_EQ(draft->textures.front().pending_source, external);
  EXPECT_EQ(std::ranges::count_if(snapshot->materials, [&](const auto &material) {
    return !material.draft && (material.id == first_id || material.id == second_id) &&
           material.textures.size() == 1u &&
           material.textures.front().texture_id == texture_id;
  }), 2);
}

TEST_F(ProjectAssetDependencyTests,
       TextureCaseRenameKeepsSavedAndDraftDependencyPathsCanonical) {
  const auto texture_id = ProjectAssetId::generate();
  const auto source = relative_path("Textures/shared.png");
  QImage image(2, 2, QImage::Format_RGBA8888);
  image.fill(0xffaabbccu);
  ASSERT_TRUE(image.save(QString::fromStdWString(
      (project_.root() / source.native_path()).wstring()), "PNG"));
  const auto material_id = add_material("RenamedTexture", ProjectMaterialTextureSlot{
      .id = texture_id, .project_relative_path = source.portable_string()});
  auto &editor = project_.standard_material_editor();
  ASSERT_TRUE(editor.open(material_id).ok);
  ASSERT_TRUE(editor.set_roughness(0.31f).ok);
  const auto initial = dependencies_.snapshot(true);
  ASSERT_TRUE(initial->complete);
  const auto renamed = project_.project_assets().rename(source, "Shared");
  ASSERT_TRUE(renamed.ok) << renamed.message;
  ASSERT_EQ(renamed.paths.size(), 1u);
  EXPECT_EQ(renamed.paths.front(), relative_path("Textures/Shared.png"));
  const auto retained = dependencies_.snapshot();
  EXPECT_GT(retained->revision, initial->revision);
  for (const auto refresh : {false, true}) {
    const auto inspected = dependencies_.snapshot(refresh);
    ASSERT_TRUE(inspected->complete)
        << (inspected->errors.empty() ? "" : inspected->errors.front());
    for (const bool draft : {false, true}) {
      const auto material = std::ranges::find_if(inspected->materials,
          [&](const auto &entry) { return entry.id == material_id && entry.draft == draft; });
      ASSERT_NE(material, inspected->materials.end());
      ASSERT_EQ(material->textures.size(), 1u);
      EXPECT_EQ(material->textures.front().texture_id, texture_id);
      EXPECT_EQ(material->textures.front().path, relative_path("Textures/Shared.png"));
    }
  }
  EXPECT_FLOAT_EQ(editor.current()->document.standard_material()->roughness, 0.31f);
  EXPECT_TRUE(editor.current()->dirty);
}

TEST_F(ProjectAssetDependencyTests,
       RecognizesResidentReseededSceneWithoutIgnoringUnexplainedIdentityMismatch) {
  const auto material_id = add_material("ReseededDependency");
  (void)add_binding(material_id);
  ASSERT_TRUE(project_.save_as(project_.active_scene_id(),
                               relative_path("Scenes/Original.qdr")).ok);
  const auto original_id = project_.active_scene()->scene_id;
  std::filesystem::copy_file(project_.root() / "Scenes/Original.qdr",
                              project_.root() / "Scenes/Copy.qdr");
  const auto opened = project_.open_scene(relative_path("Scenes/Copy.qdr"));
  ASSERT_TRUE(opened.ok) << opened.message;
  auto *copy = project_.active_scene();
  ASSERT_NE(copy, nullptr);
  EXPECT_NE(copy->scene_id, original_id);
  ASSERT_TRUE(copy->identity_dirty);
  const auto inspected = dependencies_.snapshot(true);
  ASSERT_TRUE(inspected->complete)
      << (inspected->errors.empty() ? "" : inspected->errors.front());
  const auto *saved = find_scene(*inspected, copy->scene_id, false);
  ASSERT_NE(saved, nullptr);
  EXPECT_EQ(saved->path, relative_path("Scenes/Copy.qdr"));
  EXPECT_EQ(material_set(*saved), (std::set<ProjectAssetId>{material_id}));
  copy->identity_dirty = false;
  const auto unexpected = dependencies_.snapshot(true);
  EXPECT_FALSE(unexpected->complete);
  EXPECT_TRUE(std::ranges::any_of(unexpected->errors, [](const auto &error) {
    return error.find("scene identity differs") != std::string::npos;
  }));
  copy->identity_dirty = true;
}

TEST_F(ProjectAssetDependencyTests,
       ExternalTextureMetadataChangesBlockDeletionAndPreserveExternalManifest) {
  (void)add_material("DeleteTarget");
  const auto target = relative_path("Materials/DeleteTarget.qmat");
  const std::array selected{target};
  bool recycled = false;
  ProjectAssetDeletionService deletion(project_,
      [&recycled](std::span<const std::filesystem::path>) {
        recycled = true;
        return ProjectAssetRecycleResult{};
      });
  const auto original_preview = deletion.preview(selected);
  ASSERT_TRUE(original_preview.complete)
      << (original_preview.errors.empty() ? "" : original_preview.errors.front());
  const auto manifest_path = project_.manifest_path();
  const auto original_manifest = dependency_read(manifest_path);
  const auto original_target = dependency_read(project_.root() / target.native_path());
  const auto original_json = nlohmann::json::parse(original_manifest);
  ASSERT_FALSE(original_json["textures"].empty());

  for (const auto change : {"path", "remove", "retire", "alias"}) {
    SCOPED_TRACE(change);
    auto changed = original_json;
    if (std::string_view(change) == "path") {
      changed["textures"][0]["path"] = "Textures/ExternalRename.png";
    } else if (std::string_view(change) == "remove") {
      changed["textures"].erase(changed["textures"].begin());
    } else if (std::string_view(change) == "retire") {
      changed["deletedTextureIds"].push_back(std::string(ProjectAssetId::generate().str()));
    } else {
      changed["textures"][0]["aliases"].push_back(std::string(ProjectAssetId::generate().str()));
    }
    const auto external_manifest = changed.dump(2);
    ASSERT_TRUE(ProjectManifestStore::deserialize(external_manifest).ok());
    dependency_write(manifest_path, external_manifest);
    const auto preview = deletion.preview(selected);
    EXPECT_FALSE(preview.complete);
    EXPECT_TRUE(std::ranges::any_of(preview.errors, [](const auto &error) {
      return error.find("manifest changed on disk") != std::string::npos;
    }));
    const auto erased = deletion.erase(selected, original_preview.revision);
    EXPECT_FALSE(erased.ok);
    EXPECT_FALSE(recycled);
    EXPECT_EQ(dependency_read(manifest_path), external_manifest);
    EXPECT_EQ(dependency_read(project_.root() / target.native_path()), original_target);
  }
  dependency_write(manifest_path, original_manifest);
}

TEST_F(ProjectAssetDependencyTests,
       UnsavedTextureDiscoveryAllowsDeletionAndPublishesItsMetadataOnce) {
  (void)add_material("DeleteAfterDiscovery");
  (void)add_material("DeleteAfterPublication");
  const auto manifest_path = project_.manifest_path();
  const auto original_manifest = dependency_read(manifest_path);
  const auto persisted_stamp = project_.workspace()->persisted_texture_metadata_fingerprint;
  const auto texture_path = relative_path("Textures/Discovered.png");
  QImage image(2, 2, QImage::Format_RGBA8888);
  image.fill(0xffaabbccu);
  ASSERT_TRUE(image.save(QString::fromStdWString(
      (project_.root() / texture_path.native_path()).wstring()), "PNG"));
  const auto refreshed = project_.refresh_asset_catalog();
  ASSERT_TRUE(refreshed.ok) << refreshed.message;
  ASSERT_NE(project_.manifest()->find_texture(texture_path), nullptr);
  EXPECT_EQ(dependency_read(manifest_path), original_manifest);
  EXPECT_EQ(project_.workspace()->persisted_texture_metadata_fingerprint, persisted_stamp);
  EXPECT_NE(project_.manifest()->texture_metadata_fingerprint(), persisted_stamp);
  ProjectAssetDeletionService deletion(project_,
      [](std::span<const std::filesystem::path> paths) {
        ProjectAssetRecycleResult result{.ok = true};
        for (const auto &path : paths) {
          result.items.push_back({.original_path = path, .recycled = std::filesystem::remove(path)});
        }
        return result;
      });
  const std::array selected{relative_path("Materials/DeleteAfterDiscovery.qmat")};
  const auto preview = deletion.preview(selected);
  ASSERT_TRUE(preview.complete)
      << (preview.errors.empty() ? "" : preview.errors.front());
  const auto erased = deletion.erase(selected, preview.revision);
  ASSERT_TRUE(erased.ok) << erased.message;
  const auto persisted = ProjectManifestStore::open(manifest_path);
  ASSERT_TRUE(persisted.ok()) << persisted.error().message;
  EXPECT_NE(persisted.value().manifest.find_texture(texture_path), nullptr);
  EXPECT_EQ(project_.workspace()->persisted_texture_metadata_fingerprint,
            persisted.value().persisted_texture_metadata_fingerprint);
  const auto inspected = dependencies_.snapshot(true);
  EXPECT_TRUE(inspected->complete)
      << (inspected->errors.empty() ? "" : inspected->errors.front());
  const std::array next_selected{relative_path("Materials/DeleteAfterPublication.qmat")};
  const auto next_preview = deletion.preview(next_selected);
  ASSERT_TRUE(next_preview.complete)
      << (next_preview.errors.empty() ? "" : next_preview.errors.front());
  const auto next_erased = deletion.erase(next_selected, next_preview.revision);
  EXPECT_TRUE(next_erased.ok) << next_erased.message;
}

} // namespace
} // namespace quader
