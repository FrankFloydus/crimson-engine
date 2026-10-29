////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"
#include "assets/default_material_catalog.hpp"
#include "editor/assets/project_asset_deletion_service.hpp"
#include "editor/assets/project_asset_service.hpp"
#include "editor/material/project_asset_dependency_service.hpp"
#include "editor/material/standard_material_edit_session_service.hpp"
#include "io/project/qmat_codec.hpp"
#include "io/qdr/qdr_binary_container.hpp"
#include "platform/project_asset_recycle.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <QImage>
#include <QString>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <KnownFolders.h>
#include <ShObjIdl.h>
#include <ShlObj.h>
#include <ShlGuid.h>
#include <propsys.h>
#include <wrl/client.h>
#endif

namespace quader {
namespace {

ProjectRelativePath asset_delete_path(std::string_view text) {
  auto path = ProjectRelativePath::parse_portable(text);
  EXPECT_TRUE(path.ok()) << path.error().message;
  return path.ok() ? path.take_value() : ProjectRelativePath{};
}

std::string asset_delete_read(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), {}};
}

void asset_delete_write(const std::filesystem::path &path, std::string_view bytes) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  output.close();
  ASSERT_TRUE(output.good());
}

std::uint64_t asset_delete_hash(std::string_view bytes) {
  return quader_io::qdr_binary_content_hash(
      std::as_bytes(std::span(bytes.data(), bytes.size())));
}

#ifdef _WIN32
/// Balances the opt-in shell inspection's apartment even when an assertion fails.
class RecycleInspectionApartment final {
public:
  RecycleInspectionApartment()
      : status_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
  ~RecycleInspectionApartment() {
    if (SUCCEEDED(status_)) {
      CoUninitialize();
    }
  }
  [[nodiscard]] HRESULT status() const { return status_; }

private:
  HRESULT status_;
};

std::filesystem::path recycled_restore_directory(
    const std::filesystem::path &recycled_path) {
  PROPERTYKEY key{};
  Microsoft::WRL::ComPtr<IShellItem> bin;
  Microsoft::WRL::ComPtr<IEnumShellItems> entries;
  if (recycled_path.empty() ||
      FAILED(PSGetPropertyKeyFromName(L"System.Recycle.DeletedFrom", &key)) ||
      FAILED(SHGetKnownFolderItem(FOLDERID_RecycleBinFolder, KF_FLAG_DEFAULT,
                                 nullptr, IID_PPV_ARGS(bin.GetAddressOf()))) ||
      FAILED(bin->BindToHandler(nullptr, BHID_EnumItems,
                                IID_PPV_ARGS(entries.GetAddressOf())))) {
    return {};
  }
  while (true) {
    Microsoft::WRL::ComPtr<IShellItem> entry;
    ULONG fetched = 0u;
    if (entries->Next(1u, entry.GetAddressOf(), &fetched) != S_OK || fetched != 1u) {
      return {};
    }
    PWSTR entry_path = nullptr;
    const HRESULT named = entry->GetDisplayName(SIGDN_FILESYSPATH, &entry_path);
    const bool matches = SUCCEEDED(named) && entry_path != nullptr &&
                         _wcsicmp(recycled_path.c_str(), entry_path) == 0;
    CoTaskMemFree(entry_path);
    if (!matches) {
      continue;
    }
    Microsoft::WRL::ComPtr<IShellItem2> properties;
    PWSTR restore_folder = nullptr;
    std::filesystem::path result;
    if (SUCCEEDED(entry.As(&properties)) &&
        SUCCEEDED(properties->GetString(key, &restore_folder)) &&
        restore_folder != nullptr) {
      result = restore_folder;
    }
    CoTaskMemFree(restore_folder);
    return result;
  }
}
#endif

/// Exercises mixed deletion with the actual project owners and an injected shell.
class ProjectAssetDeletionTests : public ::testing::Test {
protected:
  void SetUp() override {
    std::filesystem::create_directories(root_);
    project_.set_resource_root(std::filesystem::path(QUADER_SOURCE_DIR) /
                               "resources" / "modeling_assets");
    const auto created = project_.create(root_);
    ASSERT_TRUE(created.ok) << created.message;
    manifest_path_ = project_.manifest_path();
    ASSERT_TRUE(project_.close(true).ok);
    std::filesystem::create_directories(root_ / "Content");
    QImage image(2, 2, QImage::Format_RGBA8888);
    image.fill(0xffa06030u);
    ASSERT_TRUE(image.save(QString::fromStdWString((root_ / "Content/image.png").wstring()), "PNG"));
    ProjectMaterialDocument document{
        .id = material_id_, .name = "Surface",
        .payload = ProjectStandardMaterialDefinition{
            .roughness = 0.3f,
            .base_color_texture = ProjectMaterialTextureSlot{
                .id = texture_id_, .project_relative_path = "Content/image.png"}}};
    const auto encoded = QmatCodec::encode(document);
    ASSERT_TRUE(encoded.ok()) << encoded.error().message;
    asset_delete_write(root_ / "Content/surface.qmat", encoded.value());
    const auto opened = project_.open(manifest_path_);
    ASSERT_TRUE(opened.ok) << opened.message;
  }

  void TearDown() override {
    project_.project_assets().set_mutation_prepare({});
    (void)project_.close(true);
    std::error_code ignored;
    std::filesystem::remove_all(root_, ignored);
  }

  ProjectAssetRecyclePort recycle_all() {
    return [this](std::span<const std::filesystem::path> paths) {
      ProjectAssetRecycleResult result{.ok = true};
      for (const auto &path : paths) {
        recycled_paths_.push_back(path);
        EXPECT_EQ(path.parent_path().lexically_relative(root_).native().find(L".quader"),
                  std::wstring::npos);
        std::filesystem::remove_all(path);
        result.items.push_back({.original_path = path, .recycled = true});
      }
      return result;
    };
  }

  SceneSessionId save_scene(std::string_view path) {
    const auto scene = project_.new_scene();
    EXPECT_TRUE(scene.ok) << scene.message;
    const auto id = project_.active_scene_id();
    const auto saved = project_.save_as(id, asset_delete_path(path));
    EXPECT_TRUE(saved.ok) << saved.message;
    return id;
  }

  std::filesystem::path root_ = std::filesystem::temp_directory_path() /
      ("quader-asset-deletion-" + std::string(ProjectAssetId::generate().str()));
  ProjectSession project_{root_ / "recent-scenes.json"};
  QuaderEditorApi editor_{project_};
  std::filesystem::path manifest_path_;
  ProjectAssetId material_id_ = *ProjectAssetId::parse("48b82075-9ee6-4bec-8b8d-bdbbe41f21cd");
  ProjectAssetId texture_id_ = *ProjectAssetId::parse("179f551c-1298-420f-82ae-9fc014652c4b");
  std::vector<std::filesystem::path> recycled_paths_;
};

TEST_F(ProjectAssetDeletionTests, SceneDeletionDoesNotRebuildUnrelatedDependenciesOrMaterialImages) {
  const auto scene = project_.project_assets().create_scene({}, "Disposable");
  ASSERT_TRUE(scene.ok) << scene.message;
  const auto material = *ProjectAssetId::parse(kDefaultProjectMaterialId);
  const auto image = project_.material_texture_image(material, StandardMaterialTextureSlot::Albedo);
  ASSERT_NE(image, nullptr);
  const auto texture_revision = project_.material_texture_revision();
  const auto work = project_.dependencies().work_stats();
  ProjectAssetDeletionService deletion(project_, recycle_all());
  const auto preview = deletion.preview(scene.paths);
  ASSERT_TRUE(preview.complete) << testing::PrintToString(preview.errors);
  const auto erased = deletion.erase(scene.paths, preview.revision);
  ASSERT_TRUE(erased.ok) << erased.message;
  EXPECT_EQ(project_.dependencies().work_stats(), work);
  EXPECT_EQ(project_.material_texture_revision(), texture_revision);
  EXPECT_EQ(project_.material_texture_image(material, StandardMaterialTextureSlot::Albedo), image);
  const auto scans = project_.project_assets().work_stats().scans;
  const auto retained = project_.project_assets().snapshot();
  ASSERT_TRUE(retained.ok());
  EXPECT_EQ(project_.project_assets().work_stats().scans, scans);
  EXPECT_EQ(retained.value().revision, project_.project_assets().refresh().value().revision);
}

TEST_F(ProjectAssetDeletionTests, DeletingRegisteredSceneRestoresExternalCopyIdentityInRetainedBrowser) {
  auto &assets = project_.project_assets();
  const auto created = assets.create_scene({}, "Registered");
  ASSERT_TRUE(created.ok) << created.message;
  const auto scene_id = project_.manifest()->find_scene(created.paths.front())->id;
  const auto duplicate = asset_delete_path("Duplicate.qdr");
  std::filesystem::copy_file(root_ / "Registered.qdr", root_ / duplicate.native_path());
  const auto before = assets.refresh();
  ASSERT_TRUE(before.ok());
  const auto copied = std::ranges::find(before.value().entries, duplicate, &ProjectAssetEntry::path);
  ASSERT_NE(copied, before.value().entries.end());
  EXPECT_FALSE(copied->scene_id.valid());
  ProjectAssetDeletionService deletion(project_, recycle_all());
  const auto preview = deletion.preview(created.paths);
  ASSERT_TRUE(preview.complete) << testing::PrintToString(preview.errors);
  const auto erased = deletion.erase(created.paths, preview.revision);
  ASSERT_TRUE(erased.ok) << erased.message;
  const auto retained = assets.snapshot();
  ASSERT_TRUE(retained.ok());
  const auto remaining = std::ranges::find(retained.value().entries, duplicate, &ProjectAssetEntry::path);
  ASSERT_NE(remaining, retained.value().entries.end());
  EXPECT_EQ(remaining->scene_id, scene_id);
  EXPECT_EQ(retained.value().revision, assets.refresh().value().revision);
}

TEST_F(ProjectAssetDeletionTests, TextureDeletionClearsSavedAndDraftSlotsWithoutSavingUnrelatedDraft) {
  auto &material_editor = project_.standard_material_editor();
  ASSERT_TRUE(material_editor.open(material_id_).ok);
  ASSERT_TRUE(material_editor.set_roughness(0.73f).ok);
  const auto session = material_editor.current()->session_id;
  ProjectAssetDeletionService deletion(project_, recycle_all());
  const std::array paths{asset_delete_path("Content/image.png")};
  const auto preview = deletion.preview(paths);
  ASSERT_TRUE(preview.complete) << ::testing::PrintToString(preview.errors);
  ASSERT_EQ(preview.affected_materials.size(), 2u);
  const auto result = deletion.erase(paths, preview.revision);
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_FALSE(std::filesystem::exists(root_ / "Content/image.png"));
  EXPECT_TRUE(project_.manifest()->is_texture_deleted(texture_id_));
  ASSERT_NE(material_editor.current(), nullptr);
  EXPECT_EQ(material_editor.current()->session_id, session);
  EXPECT_TRUE(material_editor.current()->dirty);
  EXPECT_FLOAT_EQ(material_editor.current()->document.standard_material()->roughness, 0.73f);
  EXPECT_FALSE(material_editor.current()->document.standard_material()->base_color_texture);
  const auto saved = QmatCodec::read(project_.workspace()->file_system,
                                    asset_delete_path("Content/surface.qmat"));
  ASSERT_TRUE(saved.ok()) << saved.error().message;
  EXPECT_FLOAT_EQ(saved.value().material.standard_material()->roughness, 0.3f);
  EXPECT_FALSE(saved.value().material.standard_material()->base_color_texture);
  ASSERT_EQ(recycled_paths_.size(), 1u);
  EXPECT_EQ(recycled_paths_.front(), root_ / "Content/image.png");
}

TEST_F(ProjectAssetDeletionTests, MixedFolderRecyclesOriginalFolderAndClosesSavedSceneAfterCommit) {
  const auto id = save_scene("Content/Scene.qdr");
  ASSERT_NE(project_.find(id), nullptr);
  const auto scene_id = project_.find(id)->scene_id;
  ProjectAssetDeletionService deletion(project_, recycle_all());
  const std::array paths{asset_delete_path("Content")};
  const auto preview = deletion.preview(paths);
  ASSERT_TRUE(preview.complete) << ::testing::PrintToString(preview.errors);
  ASSERT_EQ(preview.scene_ids.size(), 1u);
  ASSERT_EQ(preview.material_ids.size(), 1u);
  const auto result = deletion.erase(paths, preview.revision);
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_EQ(project_.find(id), nullptr);
  EXPECT_EQ(project_.manifest()->find_scene(scene_id), nullptr);
  EXPECT_TRUE(project_.manifest()->is_material_deleted(material_id_));
  EXPECT_TRUE(project_.manifest()->is_texture_deleted(texture_id_));
  ASSERT_EQ(recycled_paths_.size(), 1u);
  EXPECT_EQ(recycled_paths_.front(), root_ / "Content");
}

TEST_F(ProjectAssetDeletionTests, PartialRecyclingRestoresOriginalFilesManifestAndDirtyDraft) {
  const auto manifest = asset_delete_read(manifest_path_);
  const auto texture = asset_delete_read(root_ / "Content/image.png");
  const auto material = asset_delete_read(root_ / "Content/surface.qmat");
  auto &material_editor = project_.standard_material_editor();
  ASSERT_TRUE(material_editor.open(material_id_).ok);
  ASSERT_TRUE(material_editor.set_metallic(0.8f).ok);
  const auto draft = *material_editor.current();
  ProjectAssetDeletionService deletion(project_, [](std::span<const std::filesystem::path> paths) {
    ProjectAssetRecycleResult result{.message = "Injected partial recycle."};
    std::filesystem::remove(paths.front());
    result.items.push_back({.original_path = paths.front(), .recycled = true});
    result.items.push_back({.original_path = paths.back(), .recycled = false});
    return result;
  });
  const std::array paths{asset_delete_path("Content/image.png"), asset_delete_path("Content/surface.qmat")};
  const auto preview = deletion.preview(paths);
  ASSERT_TRUE(preview.complete) << ::testing::PrintToString(preview.errors);
  const auto result = deletion.erase(paths, preview.revision);
  EXPECT_FALSE(result.ok);
  EXPECT_FALSE(result.warnings.empty());
  EXPECT_EQ(asset_delete_read(manifest_path_), manifest);
  EXPECT_EQ(asset_delete_read(root_ / "Content/image.png"), texture);
  EXPECT_EQ(asset_delete_read(root_ / "Content/surface.qmat"), material);
  EXPECT_EQ(material_editor.current()->document, draft.document);
  EXPECT_EQ(material_editor.current()->draft_revision, draft.draft_revision);
  EXPECT_FALSE(project_.manifest()->is_texture_deleted(texture_id_));
}

TEST_F(ProjectAssetDeletionTests, PostRecycleFailureRestoresEmptyFoldersAndLeavesOpenScene) {
  std::filesystem::create_directories(root_ / "Content/Empty/Nested");
  const auto id = save_scene("Content/Scene.qdr");
  const auto baseline = asset_delete_read(root_ / "Content/Scene.qdr");
  ProjectAssetDeletionService deletion(project_, recycle_all(), {
      .after_recycle = [] { throw std::runtime_error("Injected publication failure."); }});
  const std::array paths{asset_delete_path("Content")};
  const auto preview = deletion.preview(paths);
  ASSERT_TRUE(preview.complete) << ::testing::PrintToString(preview.errors);
  const auto result = deletion.erase(paths, preview.revision);
  EXPECT_FALSE(result.ok);
  ASSERT_NE(project_.find(id), nullptr);
  EXPECT_TRUE(std::filesystem::is_directory(root_ / "Content/Empty/Nested"));
  EXPECT_EQ(asset_delete_read(root_ / "Content/Scene.qdr"), baseline);
}

TEST_F(ProjectAssetDeletionTests, ArbitraryFolderContentsParticipateInPhysicalDeletion) {
  asset_delete_write(root_/"Content/notes.native","Ordinary native source");bool saw_raw=false;
  ProjectAssetDeletionService deletion(project_,[&](std::span<const std::filesystem::path> paths){saw_raw=asset_delete_read(root_/"Content/notes.native")=="Ordinary native source";return recycle_all()(paths);});
  const std::array paths{asset_delete_path("Content")};const auto preview=deletion.preview(paths);ASSERT_TRUE(preview.complete)<<::testing::PrintToString(preview.errors);
  const auto erased=deletion.erase(paths,preview.revision);ASSERT_TRUE(erased.ok)<<erased.message;EXPECT_TRUE(saw_raw);EXPECT_FALSE(std::filesystem::exists(root_/"Content"));
}

TEST_F(ProjectAssetDeletionTests, FileChangeInvalidatesConfirmationBeforeRecycling) {
  ProjectAssetDeletionService deletion(project_, recycle_all());
  const std::array paths{asset_delete_path("Content/image.png")};
  const auto preview = deletion.preview(paths);
  ASSERT_TRUE(preview.complete) << ::testing::PrintToString(preview.errors);
  QImage image(4, 4, QImage::Format_RGBA8888);
  image.fill(0xff2266aau);
  ASSERT_TRUE(image.save(QString::fromStdWString((root_ / "Content/image.png").wstring()), "PNG"));
  const auto result = deletion.erase(paths, preview.revision);
  EXPECT_FALSE(result.ok);
  EXPECT_TRUE(recycled_paths_.empty());
  EXPECT_TRUE(std::filesystem::exists(root_ / "Content/image.png"));
}

TEST_F(ProjectAssetDeletionTests, DirtySceneRequiresExplicitDiscardAndRemainsOnFailure) {
  const auto id = save_scene("Content/Scene.qdr");
  const auto box = editor_.modeling().create_box({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      .name = "Unsaved object"});
  ASSERT_TRUE(box.ok()) << box.error_message();
  ProjectAssetDeletionService deletion(project_, recycle_all());
  const std::array paths{asset_delete_path("Content/Scene.qdr")};
  const auto preview = deletion.preview(paths);
  ASSERT_TRUE(preview.complete) << ::testing::PrintToString(preview.errors);
  ASSERT_EQ(preview.dirty_scene_sessions, std::vector<SceneSessionId>{id});
  EXPECT_FALSE(deletion.erase(paths, preview.revision).ok);
  EXPECT_TRUE(recycled_paths_.empty());
  ASSERT_NE(project_.find(id), nullptr);
  const std::array approved{id};
  const auto result = deletion.erase(paths, preview.revision, approved);
  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(project_.find(id), nullptr);
}

TEST_F(ProjectAssetDeletionTests, ReservedDefaultAssetsCannotBeDeleted) {
  ProjectAssetDeletionService deletion(project_, recycle_all());
  const std::array material{asset_delete_path(kDefaultProjectMaterialPath)};
  const std::array texture{asset_delete_path(kDefaultProjectAlbedoTexturePath)};
  EXPECT_FALSE(deletion.preview(material).complete);
  EXPECT_FALSE(deletion.preview(texture).complete);
  EXPECT_TRUE(recycled_paths_.empty());
}

TEST_F(ProjectAssetDeletionTests, UnregisteredSceneCopyDeletionPreservesOriginalSceneIdentity) {
  const auto id = save_scene("Content/Original.qdr");
  const auto scene_id = project_.find(id)->scene_id;
  std::filesystem::copy_file(root_ / "Content/Original.qdr", root_ / "Content/ExternalCopy.qdr");
  ProjectAssetDeletionService deletion(project_, recycle_all());
  const std::array paths{asset_delete_path("Content/ExternalCopy.qdr")};
  const auto preview = deletion.preview(paths);
  ASSERT_TRUE(preview.complete) << ::testing::PrintToString(preview.errors);
  EXPECT_TRUE(preview.scene_ids.empty());
  const auto result = deletion.erase(paths, preview.revision);
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_NE(project_.find(id), nullptr);
  EXPECT_NE(project_.manifest()->find_scene(scene_id), nullptr);
  EXPECT_TRUE(std::filesystem::exists(root_ / "Content/Original.qdr"));
  EXPECT_FALSE(std::filesystem::exists(root_ / "Content/ExternalCopy.qdr"));
}

TEST_F(ProjectAssetDeletionTests, NewAssetAtRecycledPathIsNeverOverwrittenByRollback) {
  const auto baseline = asset_delete_read(root_ / "Content/image.png");
  ProjectAssetDeletionService deletion(project_, recycle_all(), {
      .after_recycle = [this] {
        asset_delete_write(root_ / "Content/image.png", "Concurrent new asset");
        throw std::runtime_error("Injected post-recycle failure.");
      }});
  const std::array paths{asset_delete_path("Content/image.png")};
  const auto preview = deletion.preview(paths);
  ASSERT_TRUE(preview.complete) << ::testing::PrintToString(preview.errors);
  const auto result = deletion.erase(paths, preview.revision);
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(asset_delete_read(root_ / "Content/image.png"), "Concurrent new asset");
  EXPECT_TRUE(std::filesystem::is_directory(root_ / ".quader/asset-deletions"));
  EXPECT_NE(result.message.find("Recovery backups remain"), std::string::npos);
  bool found_backup = false;
  for (const auto &entry : std::filesystem::recursive_directory_iterator(root_ / ".quader/asset-deletions")) {
    if (entry.is_regular_file() && entry.path().extension() == ".old") {
      found_backup |= asset_delete_read(entry.path()) == baseline;
    }
  }
  EXPECT_TRUE(found_backup);
}

TEST_F(ProjectAssetDeletionTests, InterruptedRecyclingRecoveryRestoresOriginalAndManifest) {
  const auto original = asset_delete_read(root_ / "Content/image.png");
  const auto manifest_original = asset_delete_read(manifest_path_);
  auto candidate = *project_.manifest();
  const std::array ids{texture_id_};
  ASSERT_TRUE(candidate.mark_textures_deleted(ids).ok());
  const auto manifest_replacement = ProjectManifestStore::serialize(candidate);
  ASSERT_TRUE(project_.close(true).ok);
  const auto directory = root_ / ".quader/asset-deletions/interrupted";
  asset_delete_write(directory / "file-0.old", original);
  asset_delete_write(directory / "file-1.old", manifest_original);
  std::filesystem::remove(root_ / "Content/image.png");
  asset_delete_write(manifest_path_, manifest_replacement);
  const nlohmann::json journal{
      {"version", 1}, {"project", candidate.id().str()}, {"state", "recycling"},
      {"directories", nlohmann::json::array()},
      {"files", nlohmann::json::array({
          {{"path", "Content/image.png"}, {"backup", "file-0.old"},
           {"size", original.size()}, {"hash", asset_delete_hash(original)}},
          {{"path", manifest_path_.filename().generic_string()}, {"backup", "file-1.old"},
           {"size", manifest_original.size()}, {"hash", asset_delete_hash(manifest_original)},
           {"replacement_size", manifest_replacement.size()},
           {"replacement_hash", asset_delete_hash(manifest_replacement)}}})}};
  asset_delete_write(directory / "journal.json", journal.dump());
  auto workspace = ProjectManifestStore::open(manifest_path_);
  ASSERT_TRUE(workspace.ok()) << workspace.error().message;
  const auto recovered = ProjectAssetDeletionService::recover(workspace.value());
  EXPECT_TRUE(recovered.ok()) << recovered.error().message;
  EXPECT_FALSE(recovered.warning().empty());
  EXPECT_EQ(asset_delete_read(root_ / "Content/image.png"), original);
  EXPECT_EQ(asset_delete_read(manifest_path_), manifest_original);
  EXPECT_FALSE(workspace.value().manifest.is_texture_deleted(texture_id_));
  EXPECT_FALSE(std::filesystem::exists(directory));
}

TEST_F(ProjectAssetDeletionTests, WindowsRecycleBinKeepsOriginalProjectRestoreLocation) {
#ifdef _WIN32
  const char *enabled = std::getenv("QUADER_RUN_RECYCLE_BIN_TESTS");
  if (enabled == nullptr || std::string_view(enabled) != "1") {
    GTEST_SKIP() << "Set QUADER_RUN_RECYCLE_BIN_TESTS=1 for the real Windows shell test.";
  }
  // Recycle only this fixture's disposable asset. Never empty the user's bin.
  const std::array paths{root_ / "Content/image.png"};
  const auto recycle_start = std::chrono::steady_clock::now();
  const auto result = recycle_project_asset_paths(paths);
  const auto recycle_end = std::chrono::steady_clock::now();
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_FALSE(result.aborted);
  ASSERT_EQ(result.items.size(), 1u);
  EXPECT_TRUE(result.items.front().recycled);
  EXPECT_EQ(result.items.front().original_path, paths.front());
  EXPECT_FALSE(std::filesystem::exists(paths.front()));
  ASSERT_FALSE(result.items.front().recycled_path.empty())
      << "The Windows shell did not expose the actual recycled destination.";
  // Virtual-bin enumeration proves restore semantics here, without adding work
  // proportional to the user's entire bin to every production asset deletion.
  const auto inspection_start = std::chrono::steady_clock::now();
  RecycleInspectionApartment apartment;
  ASSERT_TRUE(SUCCEEDED(apartment.status()));
  const auto restore_directory = recycled_restore_directory(result.items.front().recycled_path);
  const auto inspection_end = std::chrono::steady_clock::now();
  std::cout << "WINDOWS_RECYCLE production_ms="
            << std::chrono::duration<double, std::milli>(recycle_end - recycle_start).count()
            << " metadata_lookup_ms="
            << std::chrono::duration<double, std::milli>(inspection_end - inspection_start).count()
            << '\n';
  ASSERT_FALSE(restore_directory.empty())
      << "The matching Recycle Bin item did not expose its original-location metadata.";
  EXPECT_EQ(restore_directory.lexically_normal(),
            paths.front().parent_path().lexically_normal());
  if (!result.items.front().restore_directory.empty()) {
    EXPECT_EQ(result.items.front().restore_directory.lexically_normal(),
              restore_directory.lexically_normal());
  }
#else
  GTEST_SKIP() << "Windows Recycle Bin verification requires Windows.";
#endif
}

} // namespace
TEST_F(ProjectAssetDeletionTests, TypedMutationRejectionPreventsAnyRecycling){
  asset_delete_write(root_/"Content/typed.native","typed source");unsigned prepares=0;
  project_.project_assets().set_mutation_prepare([&](const ProjectAssetMutation& mutation){++prepares;EXPECT_EQ(mutation.kind,ProjectAssetMutationKind::Delete);if(!mutation.paths.empty()){EXPECT_EQ(mutation.paths.front().source,asset_delete_path("Content/typed.native"));EXPECT_FALSE(mutation.paths.front().destination);}return ProjectIoResult<ProjectAssetMutationPreparation>::failure(ProjectIoErrorCode::InvalidWorkspaceState,"Typed asset has retained references");});
  ProjectAssetDeletionService deletion(project_,recycle_all());const std::array paths{asset_delete_path("Content/typed.native")};const auto preview=deletion.preview(paths);EXPECT_FALSE(preview.complete);EXPECT_FALSE(deletion.erase(paths,preview.revision).ok);EXPECT_GE(prepares,1u);EXPECT_TRUE(recycled_paths_.empty());EXPECT_EQ(asset_delete_read(root_/"Content/typed.native"),"typed source");
}
TEST_F(ProjectAssetDeletionTests, PostRecycleFailureRollsBackExistingAndPreviouslyAbsentMetadata){
  asset_delete_write(root_/"Content/typed.native","typed source");asset_delete_write(root_/".crimson/asset-locations.json","before");unsigned commits=0;
  project_.project_assets().set_mutation_prepare([&](const ProjectAssetMutation& mutation){EXPECT_EQ(mutation.kind,ProjectAssetMutationKind::Delete);ProjectAssetMutationPreparation prepared;prepared.revision=9;prepared.writes.push_back({asset_delete_path(".crimson/asset-locations.json"),"before","after"});prepared.writes.push_back({.path=asset_delete_path(".crimson/references.json"),.after="created",.create_new=true});prepared.validate=[](){return ProjectIoResult<void>::success();};prepared.committed=[&]{++commits;};return ProjectIoResult<ProjectAssetMutationPreparation>::success(std::move(prepared));});
  ProjectAssetDeletionService deletion(project_,recycle_all(),{.after_recycle=[&]{EXPECT_EQ(asset_delete_read(root_/".crimson/asset-locations.json"),"after");EXPECT_TRUE(std::filesystem::exists(root_/".crimson/references.json"));throw std::runtime_error("Injected post-recycle publication failure");}});
  const auto manifest=asset_delete_read(manifest_path_);const std::array paths{asset_delete_path("Content/typed.native")};const auto preview=deletion.preview(paths);ASSERT_TRUE(preview.complete)<<::testing::PrintToString(preview.errors);const auto erased=deletion.erase(paths,preview.revision);EXPECT_FALSE(erased.ok);EXPECT_EQ(asset_delete_read(root_/"Content/typed.native"),"typed source");EXPECT_EQ(asset_delete_read(root_/".crimson/asset-locations.json"),"before");EXPECT_FALSE(std::filesystem::exists(root_/".crimson/references.json"));EXPECT_EQ(asset_delete_read(manifest_path_),manifest);EXPECT_EQ(commits,0u);
}

} // namespace quader
