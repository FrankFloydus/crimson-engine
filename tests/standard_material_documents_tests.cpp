////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"
#include "assets/default_material_catalog.hpp"
#include "editor/render/standard_material_render_snapshot_adapter.hpp"
#include "editor/session/project_session.hpp"
#include "io/project/qmat_codec.hpp"
#include "ui/standard_material_preview_render_snapshot_adapter.hpp"

#include <gtest/gtest.h>

#include <algorithm>
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
     RevisionSnapshotAndAtomicPropertyIntentsAvoidUiReadModifyWrite) {
  StandardMaterialDocumentsHarness harness("atomic-intents");
  const std::optional<ProjectAssetId> default_id =
      ProjectAssetId::parse(kDefaultProjectMaterialId);
  ASSERT_TRUE(default_id.has_value());
  ASSERT_TRUE(harness.editor()
                  .standard_material_documents()
                  .open(*default_id)
                  .ok());

  const auto before =
      harness.editor().standard_material_documents().current_revision();
  ASSERT_TRUE(before.ok());
  EXPECT_EQ(before.value().id, *default_id);
  EXPECT_FALSE(before.value().dirty);

  ASSERT_TRUE(harness.editor()
                  .standard_material_documents()
                  .set_scalar(QuaderStandardMaterialScalar::Metallic, 0.42f)
                  .ok());
  ASSERT_TRUE(
      harness.editor()
          .standard_material_documents()
          .set_texture_transform_component(
              QuaderStandardMaterialTextureTransformComponent::ScaleU, 2.5f)
          .ok());
  ASSERT_TRUE(
      harness.editor()
          .standard_material_documents()
          .set_texture_transform_component(
              QuaderStandardMaterialTextureTransformComponent::OffsetV,
              -1.25f)
          .ok());

  const auto changed =
      harness.editor().standard_material_documents().current_revision();
  ASSERT_TRUE(changed.ok());
  EXPECT_EQ(changed.value().session_id, before.value().session_id);
  EXPECT_EQ(changed.value().id, before.value().id);
  EXPECT_EQ(changed.value().draft_revision,
            before.value().draft_revision + 3u);
  EXPECT_EQ(changed.value().clean_revision, before.value().clean_revision);
  EXPECT_TRUE(changed.value().dirty);

  const auto document =
      harness.editor().standard_material_documents().current();
  ASSERT_TRUE(document.ok());
  EXPECT_FLOAT_EQ(document.value().material.metallic, 0.42f);
  EXPECT_FLOAT_EQ(document.value().material.texture_transform.scale[0], 2.5f);
  EXPECT_FLOAT_EQ(document.value().material.texture_transform.scale[1], 1.0f);
  EXPECT_FLOAT_EQ(document.value().material.texture_transform.offset[0],
                  0.0f);
  EXPECT_FLOAT_EQ(document.value().material.texture_transform.offset[1],
                  -1.25f);

  const QuaderApiVoidResult unchanged =
      harness.editor().standard_material_documents().set_scalar(
          QuaderStandardMaterialScalar::Metallic, 0.42f);
  ASSERT_TRUE(unchanged.ok());
  EXPECT_FALSE(unchanged.changed());
  EXPECT_EQ(harness.editor()
                .standard_material_documents()
                .current_revision()
                .value()
                .draft_revision,
            changed.value().draft_revision);
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
  const auto invalid_scalar =
      static_cast<QuaderStandardMaterialScalar>(255);
  const auto invalid_component =
      static_cast<QuaderStandardMaterialTextureTransformComponent>(255);
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
  EXPECT_FALSE(harness.editor()
                   .standard_material_documents()
                   .set_scalar(invalid_scalar, 0.5f)
                   .ok());
  EXPECT_FALSE(harness.editor()
                   .standard_material_documents()
                   .set_texture_transform_component(invalid_component, 1.0f)
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

TEST(StandardMaterialDocumentsTests,
     PreviewRenderAdapterProjectsOneCanonicalMaterialPayload) {
  QuaderStandardMaterialDocumentInfo document;
  document.session_id = MaterialEditSessionId::from_generation(7u);
  document.id = ProjectAssetId::generate();
  document.draft_revision = 19u;
  document.material.base_color = {0.15f, 0.35f, 0.55f};
  document.material.roughness = 0.25f;
  document.material.metallic = 0.75f;
  document.material.normal_strength = -2.5f;
  document.material.normal_convention = QuaderNormalMapConvention::DirectX;
  document.material.texture_transform.center = {0.2f, 0.8f};
  document.material.texture_transform.scale = {2.0f, -3.0f};
  document.material.texture_transform.offset = {-0.5f, 1.5f};
  document.material.texture_transform.rotation_degrees = 47.0f;

  QuaderStandardMaterialPreviewInfo preview;
  preview.session_id = document.session_id;
  preview.id = document.id;
  preview.draft_revision = document.draft_revision;
  preview.material = document.material;
  const ProjectAssetId texture_id = ProjectAssetId::generate();
  auto pixels = std::make_shared<std::vector<std::byte>>(16u, std::byte{0x7f});
  preview.textures[2] = QuaderStandardMaterialPreviewTexture{
      .id = texture_id,
      .color_space = QuaderProjectTextureColorSpace::Linear,
      .content_revision = 23u,
      .rgba8 = pixels,
      .width = 2u,
      .height = 2u,
  };

  const RenderMaterialEvaluationSnapshot rendered =
      standard_material_preview_render_snapshot(document, &preview);
  const RenderStandardMaterialEvaluationSnapshot *standard =
      rendered.standard_material();
  ASSERT_NE(standard, nullptr);
  EXPECT_EQ(rendered.project_material_id, document.id);
  EXPECT_EQ(rendered.content_revision, document.draft_revision);
  EXPECT_FLOAT_EQ(standard->base_color.x, 0.15f);
  EXPECT_FLOAT_EQ(standard->base_color.y, 0.35f);
  EXPECT_FLOAT_EQ(standard->base_color.z, 0.55f);
  EXPECT_FLOAT_EQ(standard->roughness, 0.25f);
  EXPECT_FLOAT_EQ(standard->metallic, 0.75f);
  EXPECT_FLOAT_EQ(standard->normal_strength, -2.5f);
  EXPECT_TRUE(standard->directx_normal);
  EXPECT_FLOAT_EQ(standard->texture_transform.center.x, 0.2f);
  EXPECT_FLOAT_EQ(standard->texture_transform.center.y, 0.8f);
  EXPECT_FLOAT_EQ(standard->texture_transform.scale.x, 2.0f);
  EXPECT_FLOAT_EQ(standard->texture_transform.scale.y, -3.0f);
  EXPECT_FLOAT_EQ(standard->texture_transform.offset.x, -0.5f);
  EXPECT_FLOAT_EQ(standard->texture_transform.offset.y, 1.5f);
  EXPECT_FLOAT_EQ(standard->texture_transform.rotation_degrees, 47.0f);
  const RenderMaterialTextureEvaluationSnapshot &texture =
      standard->texture(RenderStandardMaterialTextureSlot::Metallic);
  EXPECT_EQ(texture.project_texture_id, texture_id);
  EXPECT_EQ(texture.content_revision, 23u);
  EXPECT_EQ(texture.color_space, TextureColorSpace::Linear);
  EXPECT_EQ(texture.decoded_rgba8, pixels);
  EXPECT_TRUE(texture.decoded_present());
}

TEST(StandardMaterialDocumentsTests,
     PreviewRenderAdapterRejectsMismatchedPayloadAtomically) {
  QuaderStandardMaterialDocumentInfo document;
  document.session_id = MaterialEditSessionId::from_generation(4u);
  document.id = ProjectAssetId::generate();
  document.draft_revision = 11u;
  document.material.roughness = 0.4f;

  QuaderStandardMaterialPreviewInfo stale;
  stale.session_id = document.session_id;
  stale.id = document.id;
  stale.draft_revision = document.draft_revision - 1u;
  stale.material.roughness = 0.9f;
  stale.textures[0] = QuaderStandardMaterialPreviewTexture{
      .id = ProjectAssetId::generate(),
      .content_revision = 8u,
      .rgba8 = std::make_shared<std::vector<std::byte>>(4u),
      .width = 1u,
      .height = 1u,
  };

  const auto expect_rejected =
      [&document](const QuaderStandardMaterialPreviewInfo &candidate) {
    const RenderMaterialEvaluationSnapshot rendered =
        standard_material_preview_render_snapshot(document, &candidate);
    const RenderStandardMaterialEvaluationSnapshot *standard =
        rendered.standard_material();
    ASSERT_NE(standard, nullptr);
    EXPECT_FLOAT_EQ(standard->roughness, 0.4f);
    EXPECT_FALSE(standard->texture(RenderStandardMaterialTextureSlot::Albedo)
                     .present());
  };

  expect_rejected(stale);
  stale.draft_revision = document.draft_revision;
  stale.session_id = MaterialEditSessionId::from_generation(5u);
  expect_rejected(stale);
  stale.session_id = document.session_id;
  stale.id = ProjectAssetId::generate();
  expect_rejected(stale);
}

TEST(StandardMaterialDocumentsTests,
     DraftAndPersistedAdaptersProjectIdenticalCanonicalMaterialValues) {
  StandardMaterialDocumentsHarness harness("render-adapter-parity");
  ASSERT_TRUE(
      harness.editor().standard_material_documents().new_material().ok());
  ASSERT_TRUE(harness.editor()
                  .standard_material_documents()
                  .set_base_color({0.12f, 0.34f, 0.56f})
                  .ok());
  ASSERT_TRUE(harness.editor()
                  .standard_material_documents()
                  .set_roughness(0.27f)
                  .ok());
  ASSERT_TRUE(harness.editor()
                  .standard_material_documents()
                  .set_metallic(0.81f)
                  .ok());
  ASSERT_TRUE(harness.editor()
                  .standard_material_documents()
                  .set_normal_strength(-3.5f)
                  .ok());
  ASSERT_TRUE(harness.editor()
                  .standard_material_documents()
                  .set_normal_convention(
                      QuaderNormalMapConvention::DirectX)
                  .ok());
  ASSERT_TRUE(
      harness.editor()
          .standard_material_documents()
          .set_texture_transform({
              .center = {0.25f, 0.75f},
              .scale = {2.0f, -3.0f},
              .offset = {-0.5f, 1.25f},
              .rotation_degrees = 47.0f,
          })
          .ok());

  const auto draft_document =
      harness.editor().standard_material_documents().current();
  ASSERT_TRUE(draft_document.ok()) << draft_document.error_message();
  const RenderMaterialEvaluationSnapshot draft =
      standard_material_preview_render_snapshot(draft_document.value());
  const RenderStandardMaterialEvaluationSnapshot *draft_standard =
      draft.standard_material();
  ASSERT_NE(draft_standard, nullptr);

  const QuaderApiVoidResult saved =
      harness.editor().standard_material_documents().save_as(
          project_path("Materials/render_adapter_parity.qmat"));
  ASSERT_TRUE(saved.ok()) << saved.error_message();
  const auto persisted_document =
      harness.editor().standard_material_documents().current();
  ASSERT_TRUE(persisted_document.ok())
      << persisted_document.error_message();
  EXPECT_NE(persisted_document.value().id, draft_document.value().id)
      << "Save As owns allocation of the persisted project asset identity";
  ProjectSessionRenderMaterialResolver resolver(harness.project());
  const std::optional<RenderProjectMaterialSnapshot> persisted =
      resolver.resolve(persisted_document.value().id);
  ASSERT_TRUE(persisted.has_value());
  const RenderStandardMaterialEvaluationSnapshot *persisted_standard =
      persisted->evaluation.standard_material();
  ASSERT_NE(persisted_standard, nullptr);

  EXPECT_EQ(persisted->evaluation.project_material_id,
            persisted_document.value().id);
  EXPECT_EQ(*persisted_standard, *draft_standard);
}

TEST(StandardMaterialDocumentsTests,
     ProjectResolverUsesTheSameCanonicalMaterialProjection) {
  StandardMaterialDocumentsHarness harness("render-resolver");
  const std::optional<ProjectAssetId> default_id =
      ProjectAssetId::parse(kDefaultProjectMaterialId);
  ASSERT_TRUE(default_id.has_value());
  const ProjectMaterialCatalogEntry *entry =
      harness.project().material_catalog().find(*default_id);
  ASSERT_NE(entry, nullptr);
  const ProjectStandardMaterialDefinition *source =
      entry->material.standard_material();
  ASSERT_NE(source, nullptr);

  ProjectSessionRenderMaterialResolver resolver(harness.project());
  const std::optional<RenderProjectMaterialSnapshot> rendered =
      resolver.resolve(*default_id);
  ASSERT_TRUE(rendered.has_value());
  const RenderStandardMaterialEvaluationSnapshot *standard =
      rendered->evaluation.standard_material();
  ASSERT_NE(standard, nullptr);
  EXPECT_EQ(rendered->catalog_revision, resolver.revision());
  EXPECT_EQ(rendered->evaluation.content_revision, entry->content_revision);
  EXPECT_FLOAT_EQ(standard->base_color.x, source->base_color[0]);
  EXPECT_FLOAT_EQ(standard->base_color.y, source->base_color[1]);
  EXPECT_FLOAT_EQ(standard->base_color.z, source->base_color[2]);
  EXPECT_FLOAT_EQ(standard->roughness, source->roughness);
  EXPECT_FLOAT_EQ(standard->metallic, source->metallic);
  EXPECT_FLOAT_EQ(standard->normal_strength, source->normal_strength);
  EXPECT_EQ(standard->directx_normal,
            source->normal_convention == NormalMapConvention::DirectX);
}

TEST(StandardMaterialDocumentsTests,
     MaterialPresentationSnapshotCombinesProjectAndActiveSceneOwners) {
  StandardMaterialDocumentsHarness harness("presentation-snapshot");
  const auto revision =
      harness.editor().project_assets().material_presentation_revision();
  const auto snapshot =
      harness.editor().project_assets().material_presentations();
  const auto project_materials = harness.editor().project_assets().materials();
  ASSERT_TRUE(revision.ok()) << revision.error_message();
  ASSERT_TRUE(snapshot.ok()) << snapshot.error_message();
  ASSERT_TRUE(project_materials.ok()) << project_materials.error_message();
  EXPECT_EQ(snapshot.value().revision, revision.value());

  const std::size_t project_count = static_cast<std::size_t>(std::ranges::count_if(
      snapshot.value().materials,
      [](const QuaderMaterialPresentationItem &item) {
        return item.reference.project_owned();
      }));
  const std::size_t embedded_count =
      static_cast<std::size_t>(std::ranges::count_if(
          snapshot.value().materials,
          [](const QuaderMaterialPresentationItem &item) {
            return item.reference.embedded();
          }));
  EXPECT_EQ(project_count, project_materials.value().size());
  ASSERT_NE(harness.project().active_scene(), nullptr);
  EXPECT_EQ(embedded_count,
            harness.project().active_scene()->session->assets().material_count());
  for (const QuaderMaterialPresentationItem &item :
       snapshot.value().materials) {
    EXPECT_TRUE(item.reference.valid());
    EXPECT_FALSE(item.name.empty());
    EXPECT_TRUE(item.available);
    if (item.reference.project_owned()) {
      EXPECT_FALSE(item.project_relative_path.empty());
      EXPECT_EQ(item.editable,
                item.kind == QuaderMaterialPresentationKind::Standard);
    } else {
      EXPECT_EQ(item.kind, QuaderMaterialPresentationKind::Embedded);
      EXPECT_FALSE(item.editable);
    }
  }
}

} // namespace
