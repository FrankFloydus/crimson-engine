////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"
#include "assets/default_material_catalog.hpp"
#include "editor/session/project_session.hpp"
#include "io/project/qmat_codec.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

using namespace quader;

namespace {

std::filesystem::path source_resource_root() {
  return std::filesystem::path(QUADER_SOURCE_DIR) / "resources" /
         "modeling_assets";
}

ProjectRelativePath project_path(std::string_view portable) {
  ProjectIoResult<ProjectRelativePath> parsed =
      ProjectRelativePath::parse_portable(portable);
  EXPECT_TRUE(parsed.ok()) << parsed.error().message;
  return parsed.take_value();
}

std::vector<std::byte> read_bytes(const std::filesystem::path &path) {
  std::ifstream stream(path, std::ios::binary);
  EXPECT_TRUE(stream.is_open()) << path.string();
  const std::string bytes{std::istreambuf_iterator<char>(stream),
                          std::istreambuf_iterator<char>()};
  std::vector<std::byte> result(bytes.size());
  for (std::size_t index = 0u; index < bytes.size(); ++index) {
    result[index] = static_cast<std::byte>(
        static_cast<unsigned char>(bytes[index]));
  }
  return result;
}

void write_bytes(const std::filesystem::path &path,
                 const std::vector<std::byte> &bytes) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  ASSERT_TRUE(stream.is_open()) << path.string();
  stream.write(reinterpret_cast<const char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  ASSERT_TRUE(stream.good()) << path.string();
}

std::size_t regular_file_count(const std::filesystem::path &root) {
  std::error_code error;
  std::size_t count = 0u;
  for (std::filesystem::recursive_directory_iterator iterator(root, error), end;
       !error && iterator != end; iterator.increment(error)) {
    if (iterator->is_regular_file(error) && !error) {
      ++count;
    }
  }
  EXPECT_FALSE(error) << error.message();
  return count;
}

/// Creates an isolated folder-backed project for public material API tests.
class StandardMaterialDocumentsHarness final {
public:
  explicit StandardMaterialDocumentsHarness(std::string_view label)
      : root_(std::filesystem::temp_directory_path() /
              ("quader-standard-material-documents-" + std::string(label) +
               "-" + std::to_string(static_cast<std::uint64_t>(
                         std::chrono::steady_clock::now()
                             .time_since_epoch()
                             .count())))),
        editor_(project_) {
    std::error_code error;
    std::filesystem::create_directory(root_, error);
    EXPECT_FALSE(error) << error.message();
    project_.set_resource_root(source_resource_root());
    const QuaderApiVoidResult created = editor_.project().create(root_);
    EXPECT_TRUE(created.ok()) << created.error_message();
  }

  ~StandardMaterialDocumentsHarness() {
    if (project_.has_project()) {
      (void)project_.close(true);
    }
    std::error_code ignored;
    std::filesystem::remove_all(root_, ignored);
  }

  ProjectSession &project() { return project_; }
  QuaderEditorApi &editor() { return editor_; }
  const std::filesystem::path &root() const { return root_; }

private:
  std::filesystem::path root_;
  ProjectSession project_;
  QuaderEditorApi editor_;
};

TEST(StandardMaterialDocumentsTests,
     EditableDefaultSavesTypedV2WithoutDirtyingTheScene) {
  StandardMaterialDocumentsHarness harness("default-save");
  const std::optional<ProjectAssetId> default_id =
      ProjectAssetId::parse(kDefaultProjectMaterialId);
  ASSERT_TRUE(default_id.has_value());

  const auto opened =
      harness.editor().standard_material_documents().open(*default_id);
  ASSERT_TRUE(opened.ok()) << opened.error_message();
  ASSERT_TRUE(harness.editor()
                  .standard_material_documents()
                  .set_name("Edited Default")
                  .ok());
  ASSERT_TRUE(harness.editor()
                  .standard_material_documents()
                  .set_metallic(0.65f)
                  .ok());
  ASSERT_TRUE(harness.editor()
                  .standard_material_documents()
                  .set_normal_strength(-8.0f)
                  .ok());
  ASSERT_TRUE(harness.editor()
                  .standard_material_documents()
                  .set_normal_convention(
                      QuaderNormalMapConvention::DirectX)
                  .ok());
  const QuaderStandardMaterialTextureTransform texture_transform{
      .center = {0.25f, 0.75f},
      .scale = {2.0f, -3.0f},
      .offset = {-0.5f, 1.25f},
      .rotation_degrees = 45.0f,
  };
  ASSERT_TRUE(harness.editor()
                  .standard_material_documents()
                  .set_texture_transform(texture_transform)
                  .ok());
  EXPECT_TRUE(harness.project().has_dirty_material());
  EXPECT_FALSE(harness.project().has_dirty_scenes());

  const QuaderApiVoidResult saved =
      harness.editor().standard_material_documents().save();
  ASSERT_TRUE(saved.ok()) << saved.error_message();
  EXPECT_FALSE(harness.project().has_dirty_material());
  EXPECT_FALSE(harness.project().has_dirty_scenes());

  const auto current =
      harness.editor().standard_material_documents().current();
  ASSERT_TRUE(current.ok());
  EXPECT_EQ(current.value().id, *default_id);
  ASSERT_TRUE(current.value().path.has_value());
  EXPECT_EQ(current.value().path->portable_string(),
            kDefaultProjectMaterialPath);
  EXPECT_EQ(current.value().name, "Edited Default");
  EXPECT_FLOAT_EQ(current.value().material.metallic, 0.65f);
  EXPECT_FLOAT_EQ(current.value().material.normal_strength, -8.0f);
  EXPECT_EQ(current.value().material.normal_convention,
            QuaderNormalMapConvention::DirectX);
  EXPECT_EQ(current.value().material.texture_transform, texture_transform);

  const std::string encoded = [&]() {
    std::ifstream stream(harness.root() / kDefaultProjectMaterialPath,
                         std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(stream),
                       std::istreambuf_iterator<char>());
  }();
  EXPECT_NE(encoded.find("\"version\": 2"), std::string::npos);
  EXPECT_NE(encoded.find("\"materialType\": \"standard\""),
            std::string::npos);
  EXPECT_NE(encoded.find("\"textureTransform\""), std::string::npos);
}

TEST(StandardMaterialDocumentsTests,
     NewSaveAsActivatesNewAssetAndPreservesImportedSourceBytes) {
  StandardMaterialDocumentsHarness harness("new-save-as");
  int opened_events = 0;
  int dirty_events = 0;
  int saved_events = 0;
  int assets_events = 0;
  int active_events = 0;
  std::optional<QuaderEvent> saved_event;
  std::optional<QuaderEvent> assets_event;
  std::optional<QuaderEvent> active_event;
  const auto opened_subscription = harness.editor().events().subscribe(
      quader_event::kStandardMaterialOpened,
      [&opened_events](const QuaderEvent &) { ++opened_events; });
  const auto dirty_subscription = harness.editor().events().subscribe(
      quader_event::kStandardMaterialDirtyChanged,
      [&dirty_events](const QuaderEvent &) { ++dirty_events; });
  const auto saved_subscription = harness.editor().events().subscribe(
      quader_event::kStandardMaterialSaved,
      [&saved_events, &saved_event](const QuaderEvent &event) {
        ++saved_events;
        saved_event = event;
      });
  const auto assets_subscription = harness.editor().events().subscribe(
      quader_event::kProjectAssetsChanged,
      [&assets_events, &assets_event](const QuaderEvent &event) {
        ++assets_events;
        assets_event = event;
      });
  const auto active_subscription = harness.editor().events().subscribe(
      quader_event::kActiveProjectMaterialChanged,
      [&active_events, &active_event](const QuaderEvent &event) {
        ++active_events;
        active_event = event;
      });
  ASSERT_TRUE(opened_subscription.valid());
  ASSERT_TRUE(dirty_subscription.valid());
  ASSERT_TRUE(saved_subscription.valid());
  ASSERT_TRUE(assets_subscription.valid());
  ASSERT_TRUE(active_subscription.valid());
  const auto created =
      harness.editor().standard_material_documents().new_material();
  ASSERT_TRUE(created.ok()) << created.error_message();
  ASSERT_TRUE(harness.editor()
                  .standard_material_documents()
                  .set_name("Painted Metal")
                  .ok());

  const std::filesystem::path source =
      source_resource_root() / "materials" / "default" /
      "default_albedo.png";
  const std::vector<std::byte> original = read_bytes(source);
  ASSERT_FALSE(original.empty());
  ASSERT_TRUE(harness.editor()
                  .standard_material_documents()
                  .import_texture(
                      QuaderStandardMaterialTextureSlot::Albedo, source)
                  .ok());
  ASSERT_TRUE(
      harness.editor()
          .standard_material_documents()
          .import_texture(QuaderStandardMaterialTextureSlot::Roughness,
                          source)
          .ok());
  ASSERT_TRUE(
      harness.editor()
          .standard_material_documents()
          .import_texture(QuaderStandardMaterialTextureSlot::Metallic,
                          source)
          .ok());
  ASSERT_TRUE(harness.editor()
                  .standard_material_documents()
                  .import_texture(
                      QuaderStandardMaterialTextureSlot::Normal, source)
                  .ok());
  const auto first_preview =
      harness.editor().standard_material_documents().preview();
  ASSERT_TRUE(first_preview.ok());
  for (const auto &texture : first_preview.value().textures) {
    ASSERT_TRUE(texture.has_value());
    EXPECT_NE(texture->rgba8, nullptr);
    EXPECT_GT(texture->width, 0u);
    EXPECT_GT(texture->height, 0u);
    EXPECT_EQ(texture->detected_format, "png");
    EXPECT_EQ(texture->encoded_byte_size, original.size());
  }
  ASSERT_TRUE(harness.editor()
                  .standard_material_documents()
                  .set_metallic(0.8f)
                  .ok());
  const auto scalar_preview =
      harness.editor().standard_material_documents().preview();
  ASSERT_TRUE(scalar_preview.ok());
  EXPECT_EQ(first_preview.value().textures[0]->rgba8,
            scalar_preview.value().textures[0]->rgba8)
      << "scalar edits must keep decoded texture storage stable";

  const QuaderApiVoidResult saved =
      harness.editor().standard_material_documents().save_as(
          project_path("Materials/painted_metal.qmat"));
  ASSERT_TRUE(saved.ok()) << saved.error_message();
  EXPECT_EQ(opened_events, 1);
  EXPECT_EQ(dirty_events, 2)
      << "the first edit dirties the draft and Save As cleans it";
  EXPECT_EQ(saved_events, 1);
  EXPECT_EQ(assets_events, 1);
  EXPECT_EQ(active_events, 1);
  const auto current =
      harness.editor().standard_material_documents().current();
  ASSERT_TRUE(current.ok());
  ASSERT_TRUE(current.value().path.has_value());
  EXPECT_EQ(current.value().path->portable_string(),
            "Materials/painted_metal.qmat");
  EXPECT_FALSE(current.value().dirty);
  ASSERT_TRUE(saved_event.has_value());
  ASSERT_TRUE(assets_event.has_value());
  ASSERT_TRUE(active_event.has_value());
  EXPECT_EQ(saved_event->project_asset_id, current.value().id);
  EXPECT_EQ(assets_event->project_asset_id, current.value().id);
  EXPECT_EQ(active_event->project_asset_id, current.value().id);
  EXPECT_TRUE(saved_event->project_id.has_value());
  EXPECT_TRUE(assets_event->project_id.has_value());
  EXPECT_TRUE(active_event->project_id.has_value());
  ASSERT_TRUE(current.value().material.base_color_texture.has_value());
  ASSERT_TRUE(current.value().material.roughness_texture.has_value());
  ASSERT_TRUE(current.value().material.metallic_texture.has_value());
  ASSERT_TRUE(current.value().material.normal_texture.has_value());
  EXPECT_EQ(current.value().material.base_color_texture->color_space,
            QuaderProjectTextureColorSpace::Srgb);
  EXPECT_EQ(current.value().material.roughness_texture->color_space,
            QuaderProjectTextureColorSpace::Linear);

  const auto active = harness.editor().project_assets().active_material();
  ASSERT_TRUE(active.ok());
  EXPECT_EQ(active.value().id, current.value().id);
  EXPECT_EQ(harness.editor().project_assets().materials().value().size(), 2u);
  EXPECT_FALSE(harness.project().has_dirty_scenes());

  for (const QuaderStandardMaterialTextureReference *texture :
       {&*current.value().material.base_color_texture,
        &*current.value().material.roughness_texture,
        &*current.value().material.metallic_texture,
        &*current.value().material.normal_texture}) {
    EXPECT_EQ(read_bytes(harness.root() / texture->project_relative_path),
              original);
  }

  std::error_code error;
  std::filesystem::remove(
      harness.root() /
          current.value().material.roughness_texture->project_relative_path,
      error);
  ASSERT_FALSE(error) << error.message();
  const auto missing_preview =
      harness.editor().standard_material_documents().preview();
  ASSERT_TRUE(missing_preview.ok());
  EXPECT_FALSE(missing_preview.value().textures[1].has_value());
  EXPECT_FALSE(missing_preview.value().texture_diagnostics[1].empty());
  write_bytes(
      harness.root() /
          current.value().material.roughness_texture->project_relative_path,
      original);
  const auto cached_missing_preview =
      harness.editor().standard_material_documents().preview();
  ASSERT_TRUE(cached_missing_preview.ok());
  EXPECT_FALSE(cached_missing_preview.value().textures[1].has_value());
  EXPECT_EQ(cached_missing_preview.value().texture_diagnostics[1],
            missing_preview.value().texture_diagnostics[1])
      << "a missing dependency diagnostic must be cached for the draft";

  std::optional<QuaderEvent> closed_event;
  const auto closed_subscription = harness.editor().events().subscribe(
      quader_event::kStandardMaterialClosed,
      [&closed_event](const QuaderEvent &event) { closed_event = event; });
  ASSERT_TRUE(closed_subscription.valid());
  const MaterialEditSessionId closing_session = current.value().session_id;
  ASSERT_TRUE(harness.editor()
                  .standard_material_documents()
                  .close()
                  .ok());
  ASSERT_TRUE(closed_event.has_value());
  EXPECT_EQ(closed_event->project_asset_id, current.value().id);
  EXPECT_EQ(closed_event->material_edit_session_id, closing_session);
  EXPECT_EQ(closed_event->material_name, current.value().name);
  EXPECT_EQ(closed_event->material_path,
            current.value().path->portable_string());
  EXPECT_EQ(closed_event->material_dirty, false);
}

TEST(StandardMaterialDocumentsTests,
     DirtyDraftBlocksHeadlessProjectCloseInsteadOfBeingDiscarded) {
  StandardMaterialDocumentsHarness harness("close-preflight");
  ASSERT_TRUE(
      harness.editor().standard_material_documents().new_material().ok());
  ASSERT_TRUE(harness.editor()
                  .standard_material_documents()
                  .set_roughness(0.25f)
                  .ok());

  const QuaderApiVoidResult blocked = harness.editor().project().close();
  EXPECT_FALSE(blocked.ok());
  EXPECT_TRUE(harness.editor().project().has_project());
  EXPECT_TRUE(harness.project().has_dirty_material());

  ASSERT_TRUE(harness.editor()
                  .standard_material_documents()
                  .close(true)
                  .ok());
  EXPECT_TRUE(harness.editor().project().close().ok());
  EXPECT_FALSE(harness.editor().project().has_project());
}

TEST(StandardMaterialDocumentsTests,
     SaveDetectsExternalQmatRevisionAndKeepsTheDraftIntact) {
  StandardMaterialDocumentsHarness harness("external-conflict");
  const std::optional<ProjectAssetId> default_id =
      ProjectAssetId::parse(kDefaultProjectMaterialId);
  ASSERT_TRUE(default_id.has_value());
  ASSERT_TRUE(harness.editor()
                  .standard_material_documents()
                  .open(*default_id)
                  .ok());
  ASSERT_TRUE(harness.editor()
                  .standard_material_documents()
                  .set_name("Draft Name")
                  .ok());

  const ProjectFileSystem &file_system =
      harness.project().workspace()->file_system;
  const ProjectRelativePath path = project_path(kDefaultProjectMaterialPath);
  ProjectIoResult<QmatDecodedFile> external =
      QmatCodec::read(file_system, path);
  ASSERT_TRUE(external.ok()) << external.error().message;
  ProjectMaterialDocument changed = external.value().material;
  changed.name = "External Name";
  ASSERT_TRUE(QmatCodec::write_atomic(file_system, path, changed).ok());

  const QuaderApiVoidResult saved =
      harness.editor().standard_material_documents().save();
  EXPECT_FALSE(saved.ok());
  EXPECT_NE(saved.error_message().find("changed on disk"), std::string::npos);
  const auto current =
      harness.editor().standard_material_documents().current();
  ASSERT_TRUE(current.ok());
  EXPECT_TRUE(current.value().dirty);
  EXPECT_EQ(current.value().name, "Draft Name");
  ProjectIoResult<QmatDecodedFile> persisted =
      QmatCodec::read(file_system, path);
  ASSERT_TRUE(persisted.ok());
  EXPECT_EQ(persisted.value().material.name, "External Name");
}

TEST(StandardMaterialDocumentsTests,
     SaveAsFromExistingMaterialCreatesNewIdentityAndProtectsDestinations) {
  StandardMaterialDocumentsHarness harness("existing-save-as");
  const std::optional<ProjectAssetId> default_id =
      ProjectAssetId::parse(kDefaultProjectMaterialId);
  ASSERT_TRUE(default_id.has_value());
  ASSERT_TRUE(harness.editor()
                  .standard_material_documents()
                  .open(*default_id)
                  .ok());
  const std::filesystem::path default_path =
      harness.root() / kDefaultProjectMaterialPath;
  const std::vector<std::byte> original = read_bytes(default_path);

  const ProjectRelativePath copy_path =
      project_path("Materials/default_material_copy.qmat");
  ASSERT_TRUE(harness.editor()
                  .standard_material_documents()
                  .save_as(copy_path)
                  .ok());
  const auto current = harness.editor().standard_material_documents().current();
  ASSERT_TRUE(current.ok());
  EXPECT_NE(current.value().id, *default_id);
  EXPECT_EQ(current.value().path, copy_path);
  EXPECT_EQ(read_bytes(default_path), original);
  const auto active = harness.editor().project_assets().active_material();
  ASSERT_TRUE(active.ok());
  EXPECT_EQ(active.value().id, current.value().id);
  EXPECT_EQ(harness.editor().project_assets().materials().value().size(), 2u);

  EXPECT_FALSE(harness.editor()
                   .standard_material_documents()
                   .save_as(copy_path)
                   .ok());
  EXPECT_FALSE(harness.editor()
                   .standard_material_documents()
                   .save_as(project_path("Scenes/not_a_material.qmat"))
                   .ok());
  const auto unchanged =
      harness.editor().standard_material_documents().current();
  ASSERT_TRUE(unchanged.ok());
  EXPECT_EQ(unchanged.value().id, current.value().id);
  EXPECT_EQ(unchanged.value().path, current.value().path);
}

TEST(StandardMaterialDocumentsTests,
     FailedCatalogInstallRestoresQmatAndRemovesStagedTextureWrites) {
  StandardMaterialDocumentsHarness harness("transaction-rollback");
  const std::optional<ProjectAssetId> default_id =
      ProjectAssetId::parse(kDefaultProjectMaterialId);
  ASSERT_TRUE(default_id.has_value());
  ASSERT_TRUE(harness.editor()
                  .standard_material_documents()
                  .open(*default_id)
                  .ok());
  ASSERT_TRUE(harness.editor()
                  .standard_material_documents()
                  .set_roughness(0.42f)
                  .ok());
  const std::filesystem::path source =
      source_resource_root() / "materials" / "default" /
      "default_albedo.png";
  ASSERT_TRUE(harness.editor()
                  .standard_material_documents()
                  .import_texture(QuaderStandardMaterialTextureSlot::Normal,
                                  source)
                  .ok());

  const std::filesystem::path default_path =
      harness.root() / kDefaultProjectMaterialPath;
  const std::vector<std::byte> original = read_bytes(default_path);
  const std::filesystem::path textures = harness.root() / "Textures";
  const std::size_t texture_count = regular_file_count(textures);
  const std::filesystem::path duplicate =
      harness.root() / "Materials" / "duplicate_default.qmat";
  write_bytes(duplicate, original);

  const QuaderApiVoidResult failed =
      harness.editor().standard_material_documents().save();
  EXPECT_FALSE(failed.ok());
  EXPECT_EQ(read_bytes(default_path), original);
  EXPECT_EQ(regular_file_count(textures), texture_count);
  const auto draft = harness.editor().standard_material_documents().current();
  ASSERT_TRUE(draft.ok());
  EXPECT_TRUE(draft.value().dirty);
  EXPECT_TRUE(draft.value().pending_texture_sources[3].has_value());

  std::error_code error;
  std::filesystem::remove(duplicate, error);
  ASSERT_FALSE(error) << error.message();
  ASSERT_TRUE(harness.editor().standard_material_documents().save().ok());
  EXPECT_EQ(regular_file_count(textures), texture_count + 1u);
}

TEST(StandardMaterialDocumentsTests,
     InvalidPublicEnumsAreRejectedWithoutMutatingTheDraft) {
  StandardMaterialDocumentsHarness harness("invalid-enums");
  const std::optional<ProjectAssetId> default_id =
      ProjectAssetId::parse(kDefaultProjectMaterialId);
  ASSERT_TRUE(default_id.has_value());
  ASSERT_TRUE(harness.editor()
                  .standard_material_documents()
                  .open(*default_id)
                  .ok());
  const auto before = harness.editor().standard_material_documents().current();
  ASSERT_TRUE(before.ok());

  const auto invalid_convention = static_cast<QuaderNormalMapConvention>(255);
  const auto invalid_slot =
      static_cast<QuaderStandardMaterialTextureSlot>(255);
  EXPECT_FALSE(harness.editor()
                   .standard_material_documents()
                   .set_normal_convention(invalid_convention)
                   .ok());
  EXPECT_FALSE(harness.editor()
                   .standard_material_documents()
                   .clear_texture(invalid_slot)
                   .ok());
  EXPECT_FALSE(harness.editor()
                   .standard_material_documents()
                   .import_texture(invalid_slot,
                                   harness.root() / "does-not-exist.png")
                   .ok());
  EXPECT_FALSE(harness.editor()
                   .standard_material_documents()
                   .set_normal_strength(16.01f)
                   .ok());
  QuaderStandardMaterialTextureTransform invalid_transform;
  invalid_transform.rotation_degrees = 360.01f;
  EXPECT_FALSE(harness.editor()
                   .standard_material_documents()
                   .set_texture_transform(invalid_transform)
                   .ok());

  const auto after = harness.editor().standard_material_documents().current();
  ASSERT_TRUE(after.ok());
  EXPECT_EQ(after.value().draft_revision, before.value().draft_revision);
  EXPECT_EQ(after.value().material.normal_convention,
            before.value().material.normal_convention);
  EXPECT_EQ(after.value().material.texture_transform,
            before.value().material.texture_transform);
  EXPECT_FALSE(after.value().dirty);
}

TEST(StandardMaterialDocumentsTests,
     CleanRetargetPublishesClosedIdentityBeforeTheReplacementOpens) {
  StandardMaterialDocumentsHarness harness("retarget-events");
  const std::optional<ProjectAssetId> default_id =
      ProjectAssetId::parse(kDefaultProjectMaterialId);
  ASSERT_TRUE(default_id.has_value());
  std::vector<QuaderEvent> lifecycle;
  const auto closed_subscription = harness.editor().events().subscribe(
      quader_event::kStandardMaterialClosed,
      [&lifecycle](const QuaderEvent &event) { lifecycle.push_back(event); });
  const auto opened_subscription = harness.editor().events().subscribe(
      quader_event::kStandardMaterialOpened,
      [&lifecycle](const QuaderEvent &event) { lifecycle.push_back(event); });
  ASSERT_TRUE(closed_subscription.valid());
  ASSERT_TRUE(opened_subscription.valid());

  ASSERT_TRUE(harness.editor()
                  .standard_material_documents()
                  .open(*default_id)
                  .ok());
  const auto opened_default =
      harness.editor().standard_material_documents().current();
  ASSERT_TRUE(opened_default.ok());
  ASSERT_TRUE(harness.editor()
                  .standard_material_documents()
                  .new_material()
                  .ok());

  ASSERT_EQ(lifecycle.size(), 3u);
  EXPECT_EQ(lifecycle[0].name, quader_event::kStandardMaterialOpened);
  EXPECT_EQ(lifecycle[1].name, quader_event::kStandardMaterialClosed);
  EXPECT_EQ(lifecycle[1].project_asset_id, *default_id);
  EXPECT_EQ(lifecycle[1].material_edit_session_id,
            opened_default.value().session_id);
  EXPECT_EQ(lifecycle[2].name, quader_event::kStandardMaterialOpened);
  EXPECT_NE(lifecycle[2].project_asset_id, *default_id);
}

} // namespace
