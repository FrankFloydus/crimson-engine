////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"
#include "api/quader_polygon_document_internal_access.hpp"
#include "assets/default_material_catalog.hpp"
#include "editor/material/project_material_persistence.hpp"
#include "editor/modeling/modeling_types.hpp"
#include "editor/session/project_session.hpp"
#include "io/project/project_manifest.hpp"
#include "io/project/qmat_codec.hpp"
#include "io/qdr/qdr_binary_container.hpp"
#include "io/qdr/qdr_document_reader.hpp"
#include "support/project_material_fixture.hpp"

#include <gtest/gtest.h>
#include <quader/modeling/neutral/polygon/document.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <QString>

namespace quader {
namespace {

constexpr std::string_view kAmberMaterialId =
    "347a0324-6c66-4ce8-bd46-d97a4970c50d";
constexpr std::string_view kBlueMaterialId =
    "64dba180-d7bd-426e-aa8d-2ebffb8bc697";
constexpr std::string_view kAmberTextureId =
    "cf59bc97-79a3-4ef9-a555-f50f18317675";

std::filesystem::path source_resource_root() {
  return std::filesystem::path(QUADER_SOURCE_DIR) / "resources" /
         "modeling_assets";
}

ProjectAssetId project_asset_id(std::string_view text) {
  const std::optional<ProjectAssetId> parsed = ProjectAssetId::parse(text);
  EXPECT_TRUE(parsed.has_value()) << text;
  return parsed.value_or(ProjectAssetId{});
}

ProjectRelativePath project_path(std::string_view text) {
  ProjectIoResult<ProjectRelativePath> parsed =
      ProjectRelativePath::parse_portable(text);
  EXPECT_TRUE(parsed.ok()) << parsed.error().message;
  return parsed.take_value();
}

std::vector<std::byte> bytes_from_text(std::string_view text) {
  const std::span<const char> characters(text.data(), text.size());
  const std::span<const std::byte> bytes = std::as_bytes(characters);
  return {bytes.begin(), bytes.end()};
}

std::vector<std::byte> read_bytes(const std::filesystem::path &path) {
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  EXPECT_TRUE(stream.is_open()) << path.string();
  if (!stream.is_open()) {
    return {};
  }
  const std::streampos end = stream.tellg();
  EXPECT_GE(end, std::streampos{0});
  if (end < std::streampos{0}) {
    return {};
  }
  std::vector<std::byte> result(static_cast<std::size_t>(end));
  stream.seekg(0, std::ios::beg);
  stream.read(reinterpret_cast<char *>(result.data()),
              static_cast<std::streamsize>(result.size()));
  EXPECT_TRUE(stream.good() || stream.eof());
  return result;
}

void write_bytes(const std::filesystem::path &path,
                 std::span<const std::byte> bytes) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  ASSERT_TRUE(stream.is_open()) << path.string();
  stream.write(reinterpret_cast<const char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  stream.close();
  ASSERT_TRUE(stream.good());
}

void write_text(const std::filesystem::path &path, std::string_view text) {
  write_bytes(path, std::as_bytes(std::span(text.data(), text.size())));
}

std::vector<std::filesystem::path>
regular_files_below(const std::filesystem::path &root) {
  std::error_code error;
  if (!std::filesystem::exists(root, error) || error) {
    return {};
  }
  std::vector<std::filesystem::path> files;
  std::filesystem::recursive_directory_iterator iterator(
      root, std::filesystem::directory_options::none, error);
  const std::filesystem::recursive_directory_iterator end;
  for (; !error && iterator != end; iterator.increment(error)) {
    if (iterator->is_regular_file(error) && !error) {
      files.push_back(std::filesystem::relative(iterator->path(), root));
    }
  }
  EXPECT_FALSE(error) << error.message();
  std::ranges::sort(files);
  return files;
}

bool has_uuid8_asset_path_shape(std::string_view portable_path,
                                std::string_view prefix,
                                std::string_view extension) {
  if (!portable_path.starts_with(prefix) ||
      !portable_path.ends_with(extension) ||
      portable_path.size() != prefix.size() + 8u + extension.size()) {
    return false;
  }
  const std::string_view suffix = portable_path.substr(prefix.size(), 8u);
  return std::ranges::all_of(suffix, [](unsigned char character) {
    return (character >= '0' && character <= '9') ||
           (character >= 'a' && character <= 'f');
  });
}

bool contains_bytes(std::span<const std::byte> haystack,
                    std::span<const std::byte> needle) {
  if (needle.empty()) {
    return true;
  }
  return std::search(haystack.begin(), haystack.end(), needle.begin(),
                     needle.end()) != haystack.end();
}

bool contains_text(std::span<const std::byte> haystack,
                   std::string_view needle) {
  return contains_bytes(
      haystack, std::as_bytes(std::span(needle.data(), needle.size())));
}

ProjectMaterialDocument project_material(
    std::string_view id, std::string name,
    std::array<float, 3> base_color,
    std::optional<ProjectMaterialTextureSlot> preview = std::nullopt) {
  ProjectStandardMaterialDefinition standard{
      .base_color = base_color,
      .roughness = 0.65f,
      .metallic = 0.15f,
      .base_color_texture = std::move(preview),
  };
  return ProjectMaterialDocument{
      .id = project_asset_id(id),
      .name = std::move(name),
      .kind = ProjectMaterialKind::Standard,
      .material_version = kStandardMaterialVersion,
      .payload = std::move(standard),
  };
}

quader_poly::Document triangle_document() {
  quader_poly::Document document;
  const quader_poly::ElementId first =
      quader_poly::add_vertex(document, {0.0f, 0.0f, 0.0f});
  const quader_poly::ElementId second =
      quader_poly::add_vertex(document, {1.0f, 0.0f, 0.0f});
  const quader_poly::ElementId third =
      quader_poly::add_vertex(document, {0.0f, 1.0f, 0.0f});
  const std::array vertices{first, second, third};
  EXPECT_NE(quader_poly::add_face(document, vertices),
            quader_poly::kInvalidElementId);
  return document;
}

/// Owns one disposable project root used by project-material behavior tests.
class ScopedProjectMaterialDirectory final {
public:
  explicit ScopedProjectMaterialDirectory(std::string_view label) {
    const auto tick = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    base_ = std::filesystem::temp_directory_path() /
            ("quader-project-material-behavior-" + std::string(label) + "-" +
             std::to_string(tick));
    std::error_code error;
    std::filesystem::create_directories(base_, error);
    EXPECT_FALSE(error) << error.message();
  }

  ~ScopedProjectMaterialDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(base_, ignored);
  }

  std::filesystem::path make_empty_project(std::string_view name) const {
    const std::filesystem::path root = base_ / std::string(name);
    std::error_code error;
    std::filesystem::create_directory(root, error);
    EXPECT_FALSE(error) << error.message();
    return root;
  }

private:
  std::filesystem::path base_;
};

/// Records a QMAT installed into the behavior-test project.
struct InstalledMaterial final {
  ProjectRelativePath path;
  ProjectMaterialDocument material;
  std::vector<std::byte> preview_bytes;
  std::string additive_qmat_marker;
};

/// Drives public project-material workflows against one real ProjectSession.
class ProjectMaterialBehaviorHarness final {
public:
  explicit ProjectMaterialBehaviorHarness(std::string_view label)
      : temporary_(label),
        root_(temporary_.make_empty_project("MaterialBehavior")),
        editor_(project_) {
    project_.set_resource_root(source_resource_root());
  }

  void create() {
    const QuaderApiVoidResult created = editor_.project().create(root_);
    ASSERT_TRUE(created.ok()) << created.error_message();
    manifest_path_ = editor_.project().manifest_path();
  }

  void install_and_reopen(std::span<const InstalledMaterial> materials) {
    ASSERT_TRUE(editor_.project().close().ok());
    ProjectIoResult<ProjectWorkspaceStorage> workspace =
        ProjectManifestStore::open(manifest_path_);
    ASSERT_TRUE(workspace.ok()) << workspace.error().message;

    for (const InstalledMaterial &entry : materials) {
      const ProjectStandardMaterialDefinition *standard =
          entry.material.standard_material();
      ASSERT_NE(standard, nullptr);
      if (standard->base_color_texture.has_value()) {
        ProjectIoResult<ProjectRelativePath> relative =
            ProjectRelativePath::parse_portable(
                standard->base_color_texture->project_relative_path);
        ASSERT_TRUE(relative.ok()) << relative.error().message;
        ProjectIoResult<std::filesystem::path> preview_path =
            workspace.value().file_system.resolve_for_write(
                relative.value());
        ASSERT_TRUE(preview_path.ok()) << preview_path.error().message;
        write_bytes(preview_path.value(), entry.preview_bytes);
      }

      ProjectIoResult<std::string> encoded = QmatCodec::encode(entry.material);
      ASSERT_TRUE(encoded.ok()) << encoded.error().message;
      std::string qmat = encoded.take_value();
      if (!entry.additive_qmat_marker.empty()) {
        const std::size_t object = qmat.find('{');
        ASSERT_NE(object, std::string::npos);
        qmat.insert(object + 1u, "\n  \"testPayload\": \"" +
                                     entry.additive_qmat_marker + "\",");
      }
      ProjectIoResult<std::filesystem::path> qmat_path =
          workspace.value().file_system.resolve_for_write(entry.path);
      ASSERT_TRUE(qmat_path.ok()) << qmat_path.error().message;
      write_text(qmat_path.value(), qmat);
    }

    const QuaderApiVoidResult opened = editor_.project().open(manifest_path_);
    ASSERT_TRUE(opened.ok()) << opened.error_message();
    if (!editor_.scene_documents().has_active_scene()) {
      const QuaderApiResult<SceneSessionId> scene =
          editor_.scene_documents().new_scene();
      ASSERT_TRUE(scene.ok()) << scene.error_message();
    }
  }

  [[nodiscard]] ProjectSession &project() { return project_; }
  [[nodiscard]] QuaderEditorApi &editor() { return editor_; }
  [[nodiscard]] const std::filesystem::path &root() const { return root_; }
  [[nodiscard]] const std::filesystem::path &manifest_path() const {
    return manifest_path_;
  }

private:
  ScopedProjectMaterialDirectory temporary_;
  std::filesystem::path root_;
  std::filesystem::path manifest_path_;
  ProjectSession project_;
  QuaderEditorApi editor_;
};

const MaterialComponent *scene_material(ProjectSession &project,
                                        EntityId entity) {
  OpenSceneSession *scene = project.active_scene();
  EXPECT_NE(scene, nullptr);
  EXPECT_NE(scene != nullptr ? scene->session.get() : nullptr, nullptr);
  return scene != nullptr && scene->session != nullptr
             ? scene->session->scene().material(entity)
             : nullptr;
}

const quader_poly::Document *authored_document(ProjectSession &project,
                                               EntityId entity) {
  OpenSceneSession *scene = project.active_scene();
  EXPECT_NE(scene, nullptr);
  if (scene == nullptr || scene->session == nullptr) {
    return nullptr;
  }
  const MeshComponent *component = scene->session->scene().mesh(entity);
  EXPECT_NE(component, nullptr);
  if (component == nullptr) {
    return nullptr;
  }
  const MeshAsset *mesh = scene->session->assets().mesh(component->mesh);
  EXPECT_NE(mesh, nullptr);
  if (mesh == nullptr || !mesh->has_authored_polygon_document) {
    return nullptr;
  }
  return mesh->authored_polygon_document.get();
}

void expect_primary_material(ProjectSession &project, EntityId entity,
                             ProjectAssetId expected) {
  const MaterialComponent *material = scene_material(project, entity);
  ASSERT_NE(material, nullptr);
  ASSERT_TRUE(material->material.project_owned());
  EXPECT_EQ(material->material.project_asset(), expected);
}

void expect_all_face_slots(ProjectSession &project, EntityId entity,
                           std::uint32_t expected) {
  const quader_poly::Document *document = authored_document(project, entity);
  ASSERT_NE(document, nullptr);
  ASSERT_FALSE(document->faces.empty());
  for (const quader_poly::Face &face : document->faces) {
    EXPECT_EQ(face.material_slot, expected) << face.id;
  }
}

std::vector<std::pair<quader_poly::ElementId, std::uint32_t>>
face_slot_snapshot(ProjectSession &project, EntityId entity) {
  const quader_poly::Document *document = authored_document(project, entity);
  EXPECT_NE(document, nullptr);
  std::vector<std::pair<quader_poly::ElementId, std::uint32_t>> result;
  if (document == nullptr) {
    return result;
  }
  result.reserve(document->faces.size());
  for (const quader_poly::Face &face : document->faces) {
    result.emplace_back(face.id, face.material_slot);
  }
  return result;
}

ModelingSelectionExport exact_selection(ProjectSession &project) {
  return ProjectMaterialFixture(project).selection();
}

void expect_same_selection_identity(const ModelingSelectionExport &actual,
                                    const ModelingSelectionExport &expected) {
  EXPECT_EQ(actual.summary.mode, expected.summary.mode);
  EXPECT_EQ(actual.summary.has_selection, expected.summary.has_selection);
  EXPECT_EQ(actual.summary.active_kind, expected.summary.active_kind);
  EXPECT_EQ(actual.summary.active_object_id, expected.summary.active_object_id);
  EXPECT_EQ(actual.summary.active_vertex_id, expected.summary.active_vertex_id);
  EXPECT_EQ(actual.summary.active_edge_a, expected.summary.active_edge_a);
  EXPECT_EQ(actual.summary.active_edge_b, expected.summary.active_edge_b);
  EXPECT_EQ(actual.summary.active_face_id, expected.summary.active_face_id);
  EXPECT_EQ(actual.summary.selected_object_count,
            expected.summary.selected_object_count);
  EXPECT_EQ(actual.summary.selected_vertex_count,
            expected.summary.selected_vertex_count);
  EXPECT_EQ(actual.summary.selected_edge_count,
            expected.summary.selected_edge_count);
  EXPECT_EQ(actual.summary.selected_face_count,
            expected.summary.selected_face_count);
  ASSERT_EQ(actual.objects.size(), expected.objects.size());
  for (std::size_t index = 0; index < actual.objects.size(); ++index) {
    const ModelingSelectionExportObject &actual_object = actual.objects[index];
    const ModelingSelectionExportObject &expected_object =
        expected.objects[index];
    EXPECT_EQ(actual_object.modeling_object_id,
              expected_object.modeling_object_id);
    EXPECT_EQ(actual_object.entity, expected_object.entity);
    EXPECT_EQ(actual_object.selected, expected_object.selected);
    EXPECT_EQ(actual_object.selection.mode, expected_object.selection.mode);
    EXPECT_EQ(actual_object.selection.vertices,
              expected_object.selection.vertices);
    EXPECT_EQ(actual_object.selection.edges, expected_object.selection.edges);
    EXPECT_EQ(actual_object.selection.faces, expected_object.selection.faces);
    EXPECT_EQ(actual_object.selection.has_active,
              expected_object.selection.has_active);
    EXPECT_EQ(actual_object.selection.active_kind,
              expected_object.selection.active_kind);
    EXPECT_EQ(actual_object.selection.active_vertex,
              expected_object.selection.active_vertex);
    EXPECT_EQ(actual_object.selection.active_edge,
              expected_object.selection.active_edge);
    EXPECT_EQ(actual_object.selection.active_face,
              expected_object.selection.active_face);
  }
}

void expect_same_history(const QuaderHistorySnapshot &actual,
                         const QuaderHistorySnapshot &expected) {
  EXPECT_EQ(actual.can_undo, expected.can_undo);
  EXPECT_EQ(actual.can_redo, expected.can_redo);
  EXPECT_EQ(actual.undo_label, expected.undo_label);
  EXPECT_EQ(actual.redo_label, expected.redo_label);
}

TEST(ProjectMaterialPublicApiTests,
     CatalogPreviewActivationEventsAndPersistenceUseProjectIdentity) {
  ProjectMaterialBehaviorHarness harness("public-api");
  std::vector<QuaderEvent> catalog_events;
  std::vector<QuaderEvent> active_events;
  const QuaderEventSubscription catalog_subscription =
      harness.editor().events().subscribe(
          quader_event::kProjectAssetsChanged,
          [&](const QuaderEvent &event) { catalog_events.push_back(event); });
  const QuaderEventSubscription active_subscription =
      harness.editor().events().subscribe(
          quader_event::kActiveProjectMaterialChanged,
          [&](const QuaderEvent &event) { active_events.push_back(event); });
  ASSERT_TRUE(catalog_subscription.valid());
  ASSERT_TRUE(active_subscription.valid());

  harness.create();
  ASSERT_EQ(catalog_events.size(), 1u);
  ASSERT_TRUE(catalog_events.back().project_asset_id.has_value());
  EXPECT_EQ(catalog_events.back().project_asset_id,
            ProjectAssetId::parse(kDefaultProjectMaterialId));

  const std::string preview_text =
      "ALBEDO_PREVIEW_BYTES_FOR_PUBLIC_PROJECT_MATERIAL_API";
  const InstalledMaterial amber{
      .path = project_path("Materials/amber.qmat"),
      .material = project_material(
          kAmberMaterialId, "Amber", {1.0f, 0.5f, 0.1f},
          ProjectMaterialTextureSlot{
              .id = project_asset_id(kAmberTextureId),
              .project_relative_path = "Textures/amber_albedo.png",
              .color_space = ProjectTextureColorSpace::Srgb,
          }),
      .preview_bytes = bytes_from_text(preview_text),
  };
  const std::array installed{amber};
  harness.install_and_reopen(installed);
  ASSERT_EQ(catalog_events.size(), 3u)
      << "create, close, and reopen each publish one catalog event";

  const QuaderApiResult<std::vector<QuaderProjectMaterialInfo>> materials =
      harness.editor().project_assets().materials();
  ASSERT_TRUE(materials.ok()) << materials.error_message();
  ASSERT_EQ(materials.value().size(), 2u);
  EXPECT_EQ(materials.value()[0].name, "Amber");
  EXPECT_EQ(materials.value()[0].id, amber.material.id);
  EXPECT_EQ(materials.value()[0].project_relative_path,
            "Materials/amber.qmat");
  EXPECT_GT(materials.value()[0].qmat_size_bytes, 0u);
  EXPECT_EQ(materials.value()[1].name, kDefaultProjectMaterialName);

  const QuaderApiResult<QuaderProjectMaterialInfo> initial_active =
      harness.editor().project_assets().active_material();
  ASSERT_TRUE(initial_active.ok()) << initial_active.error_message();
  EXPECT_EQ(initial_active.value().id,
            project_asset_id(kDefaultProjectMaterialId));
  EXPECT_FALSE(harness.editor().project().dirty());

  const QuaderApiVoidResult activated =
      harness.editor().project_assets().set_active_material(amber.material.id);
  ASSERT_TRUE(activated.ok()) << activated.error_message();
  EXPECT_TRUE(activated.changed());
  EXPECT_FALSE(harness.editor().project().dirty());
  ASSERT_EQ(active_events.size(), 1u);
  EXPECT_EQ(active_events.back().project_asset_id, amber.material.id);
  EXPECT_EQ(active_events.back().project_root,
            std::optional<std::string>(harness.root().string()));

  const QuaderApiVoidResult unchanged =
      harness.editor().project_assets().set_active_material(amber.material.id);
  ASSERT_TRUE(unchanged.ok()) << unchanged.error_message();
  EXPECT_FALSE(unchanged.changed());
  EXPECT_EQ(active_events.size(), 1u);

  const QuaderApiResult<QuaderProjectMaterialPreview> preview =
      harness.editor().project_assets().material_preview(amber.material.id);
  ASSERT_TRUE(preview.ok()) << preview.error_message();
  EXPECT_EQ(preview.value().encoded_bytes, amber.preview_bytes);
  EXPECT_NE(preview.value().content_revision, 0u);

  const ProjectAssetId missing = ProjectAssetId::generate();
  const QuaderApiVoidResult rejected =
      harness.editor().project_assets().set_active_material(missing);
  EXPECT_FALSE(rejected.ok());
  EXPECT_EQ(active_events.size(), 1u);

  ASSERT_TRUE(harness.editor().project().close().ok());
  ASSERT_TRUE(
      harness.editor().project().open(harness.manifest_path()).ok());
  const QuaderApiResult<QuaderProjectMaterialInfo> restored =
      harness.editor().project_assets().active_material();
  ASSERT_TRUE(restored.ok()) << restored.error_message();
  EXPECT_EQ(restored.value().id, amber.material.id);

  QuaderStandaloneEditor standalone = create_standalone_editor();
  EXPECT_FALSE(standalone.editor().project_assets().materials().ok());
  EXPECT_FALSE(standalone.editor().project_assets().active_material().ok());
  EXPECT_FALSE(standalone.editor()
                   .project_assets()
                   .set_active_material(amber.material.id)
                   .ok());
}

TEST(ProjectMaterialPublicApiTests,
     TextureBatchImportKeepsPartialSuccessesAndPreservesEditorState) {
  ProjectMaterialBehaviorHarness harness("texture-batch-import");
  harness.create();

  ASSERT_TRUE(
      harness.editor().standard_material_documents().new_material().ok());
  ASSERT_TRUE(harness.editor()
                  .standard_material_documents()
                  .set_name("Unrelated Dirty Draft")
                  .ok());
  const QuaderApiResult<QuaderStandardMaterialDocumentInfo> draft_before =
      harness.editor().standard_material_documents().current();
  ASSERT_TRUE(draft_before.ok()) << draft_before.error_message();
  ASSERT_TRUE(draft_before.value().dirty);
  const QuaderHistorySnapshot history_before =
      harness.editor().query().history();
  const QuaderApiResult<QuaderProjectMaterialInfo> active_before =
      harness.editor().project_assets().active_material();
  ASSERT_TRUE(active_before.ok()) << active_before.error_message();
  const bool dirty_before = harness.editor().project().dirty();

  const std::vector<std::byte> original =
      read_bytes(source_resource_root() / "materials" / "default" /
                 "default_albedo.png");
  ASSERT_FALSE(original.empty());
  const std::filesystem::path first_directory = harness.root() / "ImportA";
  const std::filesystem::path second_directory = harness.root() / "ImportB";
  std::error_code directory_error;
  std::filesystem::create_directories(first_directory, directory_error);
  ASSERT_FALSE(directory_error) << directory_error.message();
  std::filesystem::create_directories(second_directory, directory_error);
  ASSERT_FALSE(directory_error) << directory_error.message();
  const std::filesystem::path first_source = first_directory / "same.png";
  const std::filesystem::path second_source = second_directory / "same.png";
  const std::filesystem::path invalid_source = first_directory / "broken.png";
  write_bytes(first_source, original);
  write_bytes(second_source, original);
  write_text(invalid_source, "not an encoded image");

  std::vector<QuaderEvent> assets_events;
  std::vector<QuaderEvent> active_events;
  const QuaderEventSubscription assets_subscription =
      harness.editor().events().subscribe(
          quader_event::kProjectAssetsChanged,
          [&](const QuaderEvent &event) { assets_events.push_back(event); });
  const QuaderEventSubscription active_subscription =
      harness.editor().events().subscribe(
          quader_event::kActiveProjectMaterialChanged,
          [&](const QuaderEvent &event) { active_events.push_back(event); });
  ASSERT_TRUE(assets_subscription.valid());
  ASSERT_TRUE(active_subscription.valid());

  const std::array sources{first_source, invalid_source, second_source,
                           first_source};
  const QuaderApiResult<QuaderProjectMaterialImportReport> imported =
      harness.editor().project_assets().import_textures_as_materials(sources);
  ASSERT_TRUE(imported.ok()) << imported.error_message();
  ASSERT_TRUE(imported.changed());
  ASSERT_EQ(imported.value().imported.size(), 3u);
  ASSERT_EQ(imported.value().failed.size(), 1u);
  EXPECT_EQ(imported.value().failed.front().source_path, invalid_source);
  EXPECT_FALSE(imported.value().failed.front().error_message.empty());
  ASSERT_EQ(assets_events.size(), 1u);
  EXPECT_EQ(assets_events.front().project_asset_id, active_before.value().id);
  EXPECT_TRUE(active_events.empty());

  std::vector<ProjectAssetId> imported_ids;
  imported_ids.reserve(imported.value().imported.size());
  for (const QuaderProjectMaterialImportSuccess &success :
       imported.value().imported) {
    imported_ids.push_back(success.material.id);
    EXPECT_EQ(success.material.name, "same");
    EXPECT_EQ(success.material.kind, QuaderProjectMaterialKind::Standard);
    EXPECT_NE(success.material.project_relative_path.find("Materials/"),
              std::string::npos);
    EXPECT_TRUE(has_uuid8_asset_path_shape(
        success.material.project_relative_path, "Materials/same_", ".qmat"));

    const std::filesystem::path qmat_path =
        harness.root() /
        project_path(success.material.project_relative_path).native_path();
    const std::vector<std::byte> qmat_bytes = read_bytes(qmat_path);
    ASSERT_FALSE(qmat_bytes.empty());
    const std::string qmat_text(
        reinterpret_cast<const char *>(qmat_bytes.data()), qmat_bytes.size());
    const ProjectIoResult<ProjectMaterialDocument> document =
        QmatCodec::decode(qmat_text);
    ASSERT_TRUE(document.ok()) << document.error().message;
    EXPECT_EQ(document.value().id, success.material.id);
    const ProjectStandardMaterialDefinition *standard =
        document.value().standard_material();
    ASSERT_NE(standard, nullptr);
    EXPECT_EQ(standard->base_color,
              (std::array<float, 3>{1.0f, 1.0f, 1.0f}));
    EXPECT_FLOAT_EQ(standard->roughness, 1.0f);
    EXPECT_FLOAT_EQ(standard->metallic, 0.0f);
    ASSERT_TRUE(standard->base_color_texture.has_value());
    EXPECT_EQ(standard->base_color_texture->color_space,
              ProjectTextureColorSpace::Srgb);
    EXPECT_TRUE(has_uuid8_asset_path_shape(
        standard->base_color_texture->project_relative_path,
        "Textures/same_", ".png"));
    EXPECT_EQ(read_bytes(harness.root() /
                         std::filesystem::path(standard->base_color_texture
                                                   ->project_relative_path)),
              original);
  }
  ASSERT_EQ(imported_ids.size(), 3u);
  EXPECT_NE(imported_ids[0], imported_ids[1]);
  EXPECT_NE(imported_ids[0], imported_ids[2]);
  EXPECT_NE(imported_ids[1], imported_ids[2]);
  EXPECT_NE(imported.value().imported[0].material.project_relative_path,
            imported.value().imported[1].material.project_relative_path);
  EXPECT_NE(imported.value().imported[0].material.project_relative_path,
            imported.value().imported[2].material.project_relative_path);

  const QuaderApiResult<QuaderProjectMaterialInfo> active_after =
      harness.editor().project_assets().active_material();
  ASSERT_TRUE(active_after.ok()) << active_after.error_message();
  EXPECT_EQ(active_after.value().id, active_before.value().id);
  const QuaderApiResult<QuaderStandardMaterialDocumentInfo> draft_after =
      harness.editor().standard_material_documents().current();
  ASSERT_TRUE(draft_after.ok()) << draft_after.error_message();
  EXPECT_EQ(draft_after.value().session_id, draft_before.value().session_id);
  EXPECT_EQ(draft_after.value().id, draft_before.value().id);
  EXPECT_EQ(draft_after.value().name, draft_before.value().name);
  EXPECT_EQ(draft_after.value().draft_revision,
            draft_before.value().draft_revision);
  EXPECT_EQ(draft_after.value().clean_revision,
            draft_before.value().clean_revision);
  EXPECT_EQ(draft_after.value().pending_texture_sources,
            draft_before.value().pending_texture_sources);
  EXPECT_EQ(draft_after.value().dirty, draft_before.value().dirty);
  EXPECT_EQ(harness.editor().project().dirty(), dirty_before);
  expect_same_history(harness.editor().query().history(), history_before);

  ASSERT_TRUE(harness.editor()
                  .standard_material_documents()
                  .close(true)
                  .ok());
  ASSERT_TRUE(harness.editor().project().close().ok());
  ASSERT_TRUE(harness.editor().project().open(harness.manifest_path()).ok());
  for (ProjectAssetId material_id : imported_ids) {
    const ProjectMaterialCatalogEntry *entry =
        harness.project().material_catalog().find(material_id);
    ASSERT_NE(entry, nullptr);
    const QuaderApiResult<QuaderProjectMaterialPreview> preview =
        harness.editor().project_assets().material_preview(material_id);
    ASSERT_TRUE(preview.ok()) << preview.error_message();
    EXPECT_EQ(preview.value().encoded_bytes, original);
  }
  const QuaderApiResult<QuaderProjectMaterialInfo> active_after_reopen =
      harness.editor().project_assets().active_material();
  ASSERT_TRUE(active_after_reopen.ok()) << active_after_reopen.error_message();
  EXPECT_EQ(active_after_reopen.value().id, active_before.value().id);
}

TEST(ProjectMaterialPublicApiTests,
     TextureImportPreservesUnicodeSourceAndDisplayName) {
  ProjectMaterialBehaviorHarness harness("texture-import-unicode");
  harness.create();
  const std::vector<std::byte> original =
      read_bytes(source_resource_root() / "materials" / "default" /
                 "default_albedo.png");
  ASSERT_FALSE(original.empty());

  const std::filesystem::path source_directory =
      harness.root() / std::filesystem::path(L"trame-β");
  std::error_code error;
  std::filesystem::create_directories(source_directory, error);
  ASSERT_FALSE(error) << error.message();
  const std::filesystem::path source =
      source_directory / std::filesystem::path(L"albedo-β.png");
  write_bytes(source, original);

  const std::array sources{source};
  const QuaderApiResult<QuaderProjectMaterialImportReport> imported =
      harness.editor().project_assets().import_textures_as_materials(sources);

  ASSERT_TRUE(imported.ok()) << imported.error_message();
  ASSERT_TRUE(imported.changed());
  ASSERT_EQ(imported.value().imported.size(), 1u);
  EXPECT_TRUE(imported.value().failed.empty());
  EXPECT_EQ(imported.value().imported.front().source_path, source);
  EXPECT_EQ(imported.value().imported.front().material.name,
            QStringLiteral("albedo-β").toUtf8().toStdString());
  const QuaderApiResult<QuaderProjectMaterialPreview> preview =
      harness.editor().project_assets().material_preview(
          imported.value().imported.front().material.id);
  ASSERT_TRUE(preview.ok()) << preview.error_message();
  EXPECT_EQ(preview.value().encoded_bytes, original);
}

TEST(ProjectMaterialPublicApiTests,
     MaterialPairPublicationRollsBackTextureWhenQmatCreationFails) {
  ProjectMaterialBehaviorHarness harness("texture-pair-rollback");
  harness.create();
  const std::filesystem::path texture =
      harness.root() / "Textures" / "pair_deadbeef.png";
  const std::filesystem::path existing_qmat =
      harness.root() / kDefaultProjectMaterialPath;
  const std::vector<std::byte> original_qmat = read_bytes(existing_qmat);
  ASSERT_FALSE(original_qmat.empty());
  const std::vector<std::byte> texture_bytes = bytes_from_text("texture");

  ProjectMaterialFileRollback publication;
  ASSERT_TRUE(publication.create_bytes(texture, texture_bytes).ok());
  const ProjectMaterialDocument competing = project_material(
      "6d0f4ab6-969c-402c-ad93-d72c627e4e69", "Competing",
      {0.25f, 0.5f, 0.75f});
  const ProjectIoResult<void> qmat =
      publication.create_qmat(existing_qmat, competing);
  EXPECT_FALSE(qmat.ok());

  const std::string rollback_message = publication.rollback();
  EXPECT_TRUE(rollback_message.empty()) << rollback_message;
  EXPECT_FALSE(std::filesystem::exists(texture));
  EXPECT_EQ(read_bytes(existing_qmat), original_qmat);
}

TEST(ProjectMaterialPublicApiTests,
     TextureBatchImportReportsAllRejectedWithoutMutationOrEvents) {
  QuaderStandaloneEditor standalone = create_standalone_editor();
  const std::array<std::filesystem::path, 1> absent{
      std::filesystem::path("missing.png")};
  const QuaderApiResult<QuaderProjectMaterialImportReport> no_project =
      standalone.editor().project_assets().import_textures_as_materials(
          absent);
  EXPECT_FALSE(no_project.ok());
  EXPECT_FALSE(no_project.changed());

  ProjectMaterialBehaviorHarness harness("texture-batch-all-rejected");
  harness.create();
  const QuaderApiResult<QuaderProjectMaterialImportReport> empty =
      harness.editor().project_assets().import_textures_as_materials({});
  EXPECT_FALSE(empty.ok());
  EXPECT_FALSE(empty.changed());
  const std::size_t catalog_size_before =
      harness.project().material_catalog().entries().size();
  const QuaderApiResult<QuaderProjectMaterialInfo> active_before =
      harness.editor().project_assets().active_material();
  ASSERT_TRUE(active_before.ok()) << active_before.error_message();
  const QuaderHistorySnapshot history_before =
      harness.editor().query().history();

  std::size_t assets_events = 0u;
  const QuaderEventSubscription subscription =
      harness.editor().events().subscribe(
          quader_event::kProjectAssetsChanged,
          [&](const QuaderEvent &) { ++assets_events; });
  ASSERT_TRUE(subscription.valid());
  const std::filesystem::path invalid = harness.root() / "invalid.webp";
  write_text(invalid, "invalid webp payload");
  const std::array sources{invalid};
  const QuaderApiResult<QuaderProjectMaterialImportReport> result =
      harness.editor().project_assets().import_textures_as_materials(sources);
  ASSERT_TRUE(result.ok()) << result.error_message();
  EXPECT_FALSE(result.changed());
  EXPECT_TRUE(result.value().imported.empty());
  ASSERT_EQ(result.value().failed.size(), 1u);
  EXPECT_EQ(result.value().failed.front().source_path, invalid);
  EXPECT_FALSE(result.value().failed.front().error_message.empty());
  EXPECT_EQ(assets_events, 0u);
  EXPECT_EQ(harness.project().material_catalog().entries().size(),
            catalog_size_before);
  const QuaderApiResult<QuaderProjectMaterialInfo> active_after =
      harness.editor().project_assets().active_material();
  ASSERT_TRUE(active_after.ok()) << active_after.error_message();
  EXPECT_EQ(active_after.value().id, active_before.value().id);
  EXPECT_FALSE(harness.editor().project().dirty());
  expect_same_history(harness.editor().query().history(), history_before);
}

TEST(ProjectMaterialPublicApiTests,
     TextureBatchGlobalCatalogFailureRollsBackCreatedFiles) {
  ProjectMaterialBehaviorHarness harness("texture-batch-global-rollback");
  harness.create();
  ASSERT_TRUE(
      harness.editor().standard_material_documents().new_material().ok());
  ASSERT_TRUE(harness.editor()
                  .standard_material_documents()
                  .set_name("Preserved Rollback Draft")
                  .ok());
  const QuaderApiResult<QuaderStandardMaterialDocumentInfo> draft_before =
      harness.editor().standard_material_documents().current();
  ASSERT_TRUE(draft_before.ok()) << draft_before.error_message();
  ASSERT_TRUE(draft_before.value().dirty);
  const QuaderApiResult<QuaderProjectMaterialInfo> active_before =
      harness.editor().project_assets().active_material();
  ASSERT_TRUE(active_before.ok()) << active_before.error_message();
  const QuaderHistorySnapshot history_before =
      harness.editor().query().history();
  const bool dirty_before = harness.editor().project().dirty();

  const std::filesystem::path default_qmat =
      harness.root() / kDefaultProjectMaterialPath;
  const std::vector<std::byte> default_qmat_bytes = read_bytes(default_qmat);
  ASSERT_FALSE(default_qmat_bytes.empty());
  const std::filesystem::path duplicate_qmat =
      harness.root() / "Materials" / "duplicate_default.qmat";
  write_bytes(duplicate_qmat, default_qmat_bytes);
  const std::vector<std::filesystem::path> materials_before =
      regular_files_below(harness.root() / "Materials");
  const std::vector<std::filesystem::path> textures_before =
      regular_files_below(harness.root() / "Textures");

  std::size_t assets_events = 0u;
  std::size_t active_events = 0u;
  const QuaderEventSubscription assets_subscription =
      harness.editor().events().subscribe(
          quader_event::kProjectAssetsChanged,
          [&](const QuaderEvent &) { ++assets_events; });
  const QuaderEventSubscription active_subscription =
      harness.editor().events().subscribe(
          quader_event::kActiveProjectMaterialChanged,
          [&](const QuaderEvent &) { ++active_events; });
  ASSERT_TRUE(assets_subscription.valid());
  ASSERT_TRUE(active_subscription.valid());

  const std::filesystem::path source =
      source_resource_root() / "materials" / "default" /
      "default_albedo.png";
  const std::array sources{source};
  const QuaderApiResult<QuaderProjectMaterialImportReport> result =
      harness.editor().project_assets().import_textures_as_materials(sources);
  EXPECT_FALSE(result.ok()) << result.error_message();
  EXPECT_FALSE(result.changed());
  EXPECT_TRUE(result.value().imported.empty());
  EXPECT_TRUE(result.value().failed.empty());
  EXPECT_EQ(assets_events, 0u);
  EXPECT_EQ(active_events, 0u);
  EXPECT_EQ(regular_files_below(harness.root() / "Materials"),
            materials_before);
  EXPECT_EQ(regular_files_below(harness.root() / "Textures"),
            textures_before);
  EXPECT_EQ(read_bytes(default_qmat), default_qmat_bytes);

  const QuaderApiResult<QuaderProjectMaterialInfo> active_after =
      harness.editor().project_assets().active_material();
  ASSERT_TRUE(active_after.ok()) << active_after.error_message();
  EXPECT_EQ(active_after.value().id, active_before.value().id);
  const QuaderApiResult<QuaderStandardMaterialDocumentInfo> draft_after =
      harness.editor().standard_material_documents().current();
  ASSERT_TRUE(draft_after.ok()) << draft_after.error_message();
  EXPECT_EQ(draft_after.value().session_id, draft_before.value().session_id);
  EXPECT_EQ(draft_after.value().id, draft_before.value().id);
  EXPECT_EQ(draft_after.value().name, draft_before.value().name);
  EXPECT_EQ(draft_after.value().draft_revision,
            draft_before.value().draft_revision);
  EXPECT_EQ(draft_after.value().clean_revision,
            draft_before.value().clean_revision);
  EXPECT_EQ(draft_after.value().pending_texture_sources,
            draft_before.value().pending_texture_sources);
  EXPECT_EQ(draft_after.value().dirty, draft_before.value().dirty);
  EXPECT_EQ(harness.editor().project().dirty(), dirty_before);
  expect_same_history(harness.editor().query().history(), history_before);

  std::error_code remove_error;
  EXPECT_TRUE(std::filesystem::remove(duplicate_qmat, remove_error));
  EXPECT_FALSE(remove_error) << remove_error.message();
}

TEST(ProjectMaterialBehaviorTests,
     NewBoxAndMeshResolveTheActiveProjectMaterialAtInvocation) {
  ProjectMaterialBehaviorHarness harness("new-object-material");
  harness.create();
  const InstalledMaterial amber{
      .path = project_path("Materials/amber.qmat"),
      .material = project_material(kAmberMaterialId, "Amber",
                                   {1.0f, 0.5f, 0.1f}),
  };
  const InstalledMaterial blue{
      .path = project_path("Materials/blue.qmat"),
      .material = project_material(kBlueMaterialId, "Blue",
                                   {0.1f, 0.2f, 1.0f}),
  };
  const std::array installed{amber, blue};
  harness.install_and_reopen(installed);

  ASSERT_TRUE(harness.editor()
                  .project_assets()
                  .set_active_material(amber.material.id)
                  .ok());
  const QuaderApiResult<QuaderModelingObjectRef> box =
      harness.editor().modeling().create_box(
          {.min = {-1.0f, -1.0f, -1.0f},
           .max = {1.0f, 1.0f, 1.0f}},
          "Amber Box");
  ASSERT_TRUE(box.ok()) << box.error_message();
  expect_primary_material(harness.project(), box.value().entity,
                          amber.material.id);

  const QuaderApiResult<QuaderModelingObjectRef> mesh =
      harness.editor().modeling().create_mesh(
          QuaderPolygonDocumentInternalAccess::from_backend(
              triangle_document()),
          {.name = "Amber Mesh"});
  ASSERT_TRUE(mesh.ok()) << mesh.error_message();
  expect_primary_material(harness.project(), mesh.value().entity,
                          amber.material.id);

  ASSERT_TRUE(harness.editor()
                  .project_assets()
                  .set_active_material(blue.material.id)
                  .ok());
  const QuaderApiResult<QuaderModelingObjectRef> second_box =
      harness.editor().modeling().create_box(
          {.min = {2.0f, -1.0f, -1.0f},
           .max = {4.0f, 1.0f, 1.0f}},
          "Blue Box");
  ASSERT_TRUE(second_box.ok()) << second_box.error_message();
  expect_primary_material(harness.project(), second_box.value().entity,
                          blue.material.id);
}

TEST(ProjectMaterialBehaviorTests,
     ObjectApplyIsOneUndoTransactionAndRedoCapturesOriginalMaterial) {
  ProjectMaterialBehaviorHarness harness("object-apply");
  harness.create();
  const InstalledMaterial amber{
      .path = project_path("Materials/amber.qmat"),
      .material = project_material(kAmberMaterialId, "Amber",
                                   {1.0f, 0.5f, 0.1f}),
  };
  const InstalledMaterial blue{
      .path = project_path("Materials/blue.qmat"),
      .material = project_material(kBlueMaterialId, "Blue",
                                   {0.1f, 0.2f, 1.0f}),
  };
  const std::array installed{amber, blue};
  harness.install_and_reopen(installed);
  ASSERT_TRUE(harness.editor()
                  .project_assets()
                  .set_active_material(amber.material.id)
                  .ok());

  const QuaderApiResult<QuaderModelingObjectRef> first =
      harness.editor().modeling().create_box(
          {.min = {-3.0f, -1.0f, -1.0f},
           .max = {-1.0f, 1.0f, 1.0f}},
          "First");
  const QuaderApiResult<QuaderModelingObjectRef> second =
      harness.editor().modeling().create_box(
          {.min = {1.0f, -1.0f, -1.0f},
           .max = {3.0f, 1.0f, 1.0f}},
          "Second");
  ASSERT_TRUE(first.ok()) << first.error_message();
  ASSERT_TRUE(second.ok()) << second.error_message();

  ASSERT_TRUE(harness.editor()
                  .project_assets()
                  .set_active_material(blue.material.id)
                  .ok());
  ASSERT_TRUE(harness.editor()
                  .selection()
                  .set_mode(QuaderSelectionMode::Object)
                  .ok());
  ASSERT_TRUE(harness.editor()
                  .selection()
                  .select_objects({first.value(), second.value()})
                  .ok());
  const QuaderApiVoidResult applied =
      harness.editor().modeling().apply_active_material();
  ASSERT_TRUE(applied.ok()) << applied.error_message();
  EXPECT_TRUE(applied.changed());
  for (const QuaderModelingObjectRef object : {first.value(), second.value()}) {
    expect_primary_material(harness.project(), object.entity,
                            blue.material.id);
    const MaterialComponent *material =
        scene_material(harness.project(), object.entity);
    ASSERT_NE(material, nullptr);
    EXPECT_TRUE(material->material_slots.empty());
    expect_all_face_slots(harness.project(), object.entity, 0u);
  }

  EXPECT_EQ(harness.editor().query().history().undo_label, "Apply Material");
  ASSERT_TRUE(harness.editor().history().undo().ok());
  expect_primary_material(harness.project(), first.value().entity,
                          amber.material.id);
  expect_primary_material(harness.project(), second.value().entity,
                          amber.material.id);

  ASSERT_TRUE(harness.editor()
                  .project_assets()
                  .set_active_material(
                      project_asset_id(kDefaultProjectMaterialId))
                  .ok());
  ASSERT_TRUE(harness.editor().history().redo().ok());
  expect_primary_material(harness.project(), first.value().entity,
                          blue.material.id);
  expect_primary_material(harness.project(), second.value().entity,
                          blue.material.id);
}

TEST(ProjectMaterialBehaviorTests,
     PublicApplyMaterialCommandIsAvailableInObjectMode) {
  ProjectMaterialBehaviorHarness harness("public-object-apply");
  harness.create();
  const QuaderApiResult<QuaderModelingObjectRef> box =
      harness.editor().modeling().create_box(
          {.min = {-1.0f, -1.0f, -1.0f},
           .max = {1.0f, 1.0f, 1.0f}},
          "Command Box");
  ASSERT_TRUE(box.ok()) << box.error_message();
  ASSERT_TRUE(harness.editor().selection().select_object(box.value()).ok());

  const std::vector<QuaderCommandDescriptor> commands =
      harness.editor().commands().available();
  const auto descriptor = std::find_if(
      commands.begin(), commands.end(), [](const QuaderCommandDescriptor &item) {
        return item.id == quader_command::kApplyMaterial.string();
      });
  ASSERT_NE(descriptor, commands.end());
  EXPECT_TRUE(descriptor->enabled);

  const QuaderApiVoidResult applied =
      harness.editor().commands().execute(quader_command::kApplyMaterial);
  EXPECT_TRUE(applied.ok()) << applied.error_message();
}

TEST(ProjectMaterialBehaviorTests,
     FaceApplyReusesLowestSlotAndObjectApplyClearsFaceOverrides) {
  ProjectMaterialBehaviorHarness harness("face-apply");
  harness.create();
  const InstalledMaterial amber{
      .path = project_path("Materials/amber.qmat"),
      .material = project_material(kAmberMaterialId, "Amber",
                                   {1.0f, 0.5f, 0.1f}),
  };
  const InstalledMaterial blue{
      .path = project_path("Materials/blue.qmat"),
      .material = project_material(kBlueMaterialId, "Blue",
                                   {0.1f, 0.2f, 1.0f}),
  };
  const std::array installed{amber, blue};
  harness.install_and_reopen(installed);
  ASSERT_TRUE(harness.editor()
                  .project_assets()
                  .set_active_material(amber.material.id)
                  .ok());
  const QuaderApiResult<QuaderModelingObjectRef> box =
      harness.editor().modeling().create_box(
          {.min = {-1.0f, -1.0f, -1.0f},
           .max = {1.0f, 1.0f, 1.0f}},
          "Face Material Box");
  ASSERT_TRUE(box.ok()) << box.error_message();
  const quader_poly::Document *initial =
      authored_document(harness.project(), box.value().entity);
  ASSERT_NE(initial, nullptr);
  ASSERT_GE(initial->faces.size(), 2u);
  const quader_poly::ElementId first_face = initial->faces[0].id;
  const quader_poly::ElementId second_face = initial->faces[1].id;

  ASSERT_TRUE(harness.editor()
                  .project_assets()
                  .set_active_material(blue.material.id)
                  .ok());
  ASSERT_TRUE(harness.editor()
                  .selection()
                  .set_mode(QuaderSelectionMode::Face)
                  .ok());
  ASSERT_TRUE(harness.editor()
                  .selection()
                  .select_face(box.value(), first_face)
                  .ok());
  ASSERT_TRUE(harness.editor().modeling().apply_active_material().ok());

  const MaterialComponent *after_first =
      scene_material(harness.project(), box.value().entity);
  ASSERT_NE(after_first, nullptr);
  EXPECT_EQ(after_first->material.project_asset(), amber.material.id);
  ASSERT_EQ(after_first->material_slots.size(), 1u);
  ASSERT_TRUE(after_first->material_slots.contains(1u));
  EXPECT_EQ(after_first->material_slots.at(1u).project_asset(),
            blue.material.id);
  const quader_poly::Document *first_document =
      authored_document(harness.project(), box.value().entity);
  ASSERT_NE(first_document, nullptr);
  ASSERT_NE(quader_poly::find_face(*first_document, first_face), nullptr);
  EXPECT_EQ(quader_poly::find_face(*first_document, first_face)->material_slot,
            1u);

  ASSERT_TRUE(harness.editor()
                  .selection()
                  .select_face(box.value(), second_face)
                  .ok());
  ASSERT_TRUE(harness.editor().modeling().apply_active_material().ok());
  const MaterialComponent *after_second =
      scene_material(harness.project(), box.value().entity);
  ASSERT_NE(after_second, nullptr);
  EXPECT_EQ(after_second->material_slots.size(), 1u)
      << "the existing lowest matching slot must be reused";
  const quader_poly::Document *second_document =
      authored_document(harness.project(), box.value().entity);
  ASSERT_NE(second_document, nullptr);
  ASSERT_NE(quader_poly::find_face(*second_document, first_face), nullptr);
  ASSERT_NE(quader_poly::find_face(*second_document, second_face), nullptr);
  EXPECT_EQ(quader_poly::find_face(*second_document, first_face)->material_slot,
            1u);
  EXPECT_EQ(
      quader_poly::find_face(*second_document, second_face)->material_slot,
      1u);

  const ProjectAssetId built_in =
      project_asset_id(kDefaultProjectMaterialId);
  ASSERT_TRUE(harness.editor()
                  .project_assets()
                  .set_active_material(built_in)
                  .ok());
  ASSERT_TRUE(harness.editor()
                  .selection()
                  .set_mode(QuaderSelectionMode::Object)
                  .ok());
  ASSERT_TRUE(harness.editor().selection().select_object(box.value()).ok());
  ASSERT_TRUE(harness.editor().modeling().apply_active_material().ok());
  expect_primary_material(harness.project(), box.value().entity, built_in);
  const MaterialComponent *object_applied =
      scene_material(harness.project(), box.value().entity);
  ASSERT_NE(object_applied, nullptr);
  EXPECT_TRUE(object_applied->material_slots.empty());
  expect_all_face_slots(harness.project(), box.value().entity, 0u);

  ASSERT_TRUE(harness.editor().history().undo().ok());
  const MaterialComponent *undone =
      scene_material(harness.project(), box.value().entity);
  ASSERT_NE(undone, nullptr);
  EXPECT_EQ(undone->material.project_asset(), amber.material.id);
  ASSERT_TRUE(undone->material_slots.contains(1u));
  EXPECT_EQ(undone->material_slots.at(1u).project_asset(), blue.material.id);

  ASSERT_TRUE(harness.editor()
                  .project_assets()
                  .set_active_material(amber.material.id)
                  .ok());
  ASSERT_TRUE(harness.editor().history().redo().ok());
  expect_primary_material(harness.project(), box.value().entity, built_in);

  ASSERT_TRUE(harness.editor()
                  .selection()
                  .set_mode(QuaderSelectionMode::Vertex)
                  .ok());
  const QuaderApiVoidResult vertex_rejected =
      harness.editor().modeling().apply_active_material();
  EXPECT_FALSE(vertex_rejected.ok());
  EXPECT_NE(vertex_rejected.error_message().find("object or face"),
            std::string::npos);
  ASSERT_TRUE(harness.editor()
                  .selection()
                  .set_mode(QuaderSelectionMode::Edge)
                  .ok());
  EXPECT_FALSE(harness.editor().modeling().apply_active_material().ok());
}

TEST(ProjectMaterialBehaviorTests,
     FaceMaterialCanNormalizeOnlyItsMeshWithExactUndoAndRedo) {
  ProjectMaterialBehaviorHarness harness("face-material-to-mesh");
  harness.create();
  const InstalledMaterial amber{
      .path = project_path("Materials/amber.qmat"),
      .material = project_material(kAmberMaterialId, "Amber",
                                   {1.0f, 0.5f, 0.1f}),
  };
  const InstalledMaterial blue{
      .path = project_path("Materials/blue.qmat"),
      .material = project_material(kBlueMaterialId, "Blue",
                                   {0.1f, 0.2f, 1.0f}),
  };
  const std::array installed{amber, blue};
  harness.install_and_reopen(installed);
  ASSERT_TRUE(harness.editor()
                  .project_assets()
                  .set_active_material(amber.material.id)
                  .ok());

  const QuaderApiResult<QuaderModelingObjectRef> target =
      harness.editor().modeling().create_box(
          {.min = {-3.0f, -1.0f, -1.0f},
           .max = {-1.0f, 1.0f, 1.0f}},
          "Target");
  const QuaderApiResult<QuaderModelingObjectRef> other =
      harness.editor().modeling().create_box(
          {.min = {1.0f, -1.0f, -1.0f},
           .max = {3.0f, 1.0f, 1.0f}},
          "Other");
  ASSERT_TRUE(target.ok()) << target.error_message();
  ASSERT_TRUE(other.ok()) << other.error_message();
  const quader_poly::Document *target_document =
      authored_document(harness.project(), target.value().entity);
  const quader_poly::Document *other_document =
      authored_document(harness.project(), other.value().entity);
  ASSERT_NE(target_document, nullptr);
  ASSERT_NE(other_document, nullptr);
  ASSERT_GE(target_document->faces.size(), 2u);
  ASSERT_FALSE(other_document->faces.empty());
  const quader_poly::ElementId sampled_face = target_document->faces[0].id;
  const quader_poly::ElementId other_face = other_document->faces[0].id;

  ASSERT_TRUE(harness.editor()
                  .project_assets()
                  .set_active_material(blue.material.id)
                  .ok());
  ASSERT_TRUE(harness.editor()
                  .selection()
                  .set_mode(QuaderSelectionMode::Face)
                  .ok());
  ASSERT_TRUE(harness.editor()
                  .selection()
                  .select_face(target.value(), sampled_face)
                  .ok());
  ASSERT_TRUE(harness.editor().modeling().apply_active_material().ok());
  target_document =
      authored_document(harness.project(), target.value().entity);
  ASSERT_NE(target_document, nullptr);
  ASSERT_NE(quader_poly::find_face(*target_document, sampled_face), nullptr);
  EXPECT_NE(quader_poly::find_face(*target_document, sampled_face)
                ->material_slot,
            0u)
      << "the sampled material must resolve through a nonzero override slot";

  ASSERT_TRUE(harness.editor()
                  .project_assets()
                  .set_active_material(amber.material.id)
                  .ok());
  ASSERT_TRUE(harness.editor()
                  .selection()
                  .select_face(other.value(), other_face)
                  .ok());
  const ModelingSelectionExport selection_before =
      exact_selection(harness.project());
  const QuaderApiResult<QuaderFaceProjectMaterial> sampled =
      harness.editor().modeling().project_material_for_face(
          target.value().modeling_object_id, sampled_face);
  ASSERT_TRUE(sampled.ok()) << sampled.error_message();
  EXPECT_FALSE(sampled.changed());
  EXPECT_EQ(sampled.value().id, blue.material.id);
  const QuaderApiResult<QuaderFaceProjectMaterial> sampled_from_facet =
      harness.editor()
          .modeling()
          .mesh(target.value())
          .materials()
          .project_material_for_face(sampled_face);
  ASSERT_TRUE(sampled_from_facet.ok())
      << sampled_from_facet.error_message();
  EXPECT_EQ(sampled_from_facet.value().id, blue.material.id);

  const MaterialComponent *target_component =
      scene_material(harness.project(), target.value().entity);
  const MaterialComponent *other_component =
      scene_material(harness.project(), other.value().entity);
  const quader_poly::Document *target_topology =
      authored_document(harness.project(), target.value().entity);
  ASSERT_NE(target_component, nullptr);
  ASSERT_NE(other_component, nullptr);
  ASSERT_NE(target_topology, nullptr);
  const MaterialComponent target_component_before = *target_component;
  const MaterialComponent other_component_before = *other_component;
  const quader_poly::Document target_topology_before = *target_topology;
  const std::vector<std::pair<quader_poly::ElementId, std::uint32_t>>
      target_slots_before =
      face_slot_snapshot(harness.project(), target.value().entity);
  const std::vector<std::pair<quader_poly::ElementId, std::uint32_t>>
      other_slots_before =
      face_slot_snapshot(harness.project(), other.value().entity);

  const QuaderApiVoidResult applied =
      harness.editor().modeling().apply_face_material_to_mesh(
          target.value().modeling_object_id, sampled_face);
  ASSERT_TRUE(applied.ok()) << applied.error_message();
  EXPECT_TRUE(applied.changed());
  expect_primary_material(harness.project(), target.value().entity,
                          blue.material.id);
  const MaterialComponent *normalized =
      scene_material(harness.project(), target.value().entity);
  ASSERT_NE(normalized, nullptr);
  EXPECT_TRUE(normalized->material_slots.empty());
  expect_all_face_slots(harness.project(), target.value().entity, 0u);
  ASSERT_NE(scene_material(harness.project(), other.value().entity), nullptr);
  EXPECT_EQ(*scene_material(harness.project(), other.value().entity),
            other_component_before);
  EXPECT_EQ(face_slot_snapshot(harness.project(), other.value().entity),
            other_slots_before);
  const ModelingSelectionExport selection_after_apply =
      exact_selection(harness.project());
  expect_same_selection_identity(selection_after_apply, selection_before);
  EXPECT_EQ(selection_after_apply.summary.selection_revision,
            selection_before.summary.selection_revision);
  const QuaderApiResult<QuaderProjectMaterialInfo> active_after_apply =
      harness.editor().project_assets().active_material();
  ASSERT_TRUE(active_after_apply.ok()) << active_after_apply.error_message();
  EXPECT_EQ(active_after_apply.value().id, amber.material.id);
  EXPECT_EQ(harness.editor().query().history().undo_label,
            "Apply Face Material to Mesh");

  const QuaderApiResult<QuaderModelingObjectRef> transient =
      harness.editor().modeling().create_box(
          {.min = {5.0f, -1.0f, -1.0f},
           .max = {7.0f, 1.0f, 1.0f}},
          "Transient Redo Guard");
  ASSERT_TRUE(transient.ok()) << transient.error_message();
  ASSERT_TRUE(harness.editor().history().undo().ok());
  EXPECT_FALSE(harness.project()
                   .active_scene()
                   ->session->scene()
                   .exists(transient.value().entity));
  expect_same_selection_identity(exact_selection(harness.project()),
                                 selection_before);
  const QuaderHistorySnapshot history_before_no_op =
      harness.editor().query().history();
  ASSERT_TRUE(history_before_no_op.can_undo);
  ASSERT_TRUE(history_before_no_op.can_redo);
  const ModelingSelectionExport selection_before_no_op =
      exact_selection(harness.project());

  const QuaderApiVoidResult unchanged =
      harness.editor()
          .modeling()
          .mesh(target.value())
          .materials()
          .apply_face_material_to_mesh(sampled_face);
  ASSERT_TRUE(unchanged.ok()) << unchanged.error_message();
  EXPECT_FALSE(unchanged.changed());
  expect_same_history(harness.editor().query().history(),
                      history_before_no_op);
  const ModelingSelectionExport selection_after_no_op =
      exact_selection(harness.project());
  expect_same_selection_identity(selection_after_no_op,
                                 selection_before_no_op);
  EXPECT_EQ(selection_after_no_op.summary.selection_revision,
            selection_before_no_op.summary.selection_revision);

  ASSERT_TRUE(harness.editor().history().undo().ok());
  const MaterialComponent *undone =
      scene_material(harness.project(), target.value().entity);
  ASSERT_NE(undone, nullptr);
  EXPECT_EQ(*undone, target_component_before);
  EXPECT_EQ(face_slot_snapshot(harness.project(), target.value().entity),
            target_slots_before);
  const quader_poly::Document *undone_document =
      authored_document(harness.project(), target.value().entity);
  ASSERT_NE(undone_document, nullptr);
  EXPECT_TRUE(
      quader_poly::has_same_geometry(*undone_document, target_topology_before));
  expect_same_selection_identity(exact_selection(harness.project()),
                                 selection_before);

  const ProjectAssetId built_in = project_asset_id(kDefaultProjectMaterialId);
  ASSERT_TRUE(harness.editor()
                  .project_assets()
                  .set_active_material(built_in)
                  .ok());
  ASSERT_TRUE(harness.editor().history().redo().ok());
  expect_primary_material(harness.project(), target.value().entity,
                          blue.material.id);
  expect_all_face_slots(harness.project(), target.value().entity, 0u);
  const MaterialComponent *redone =
      scene_material(harness.project(), target.value().entity);
  ASSERT_NE(redone, nullptr);
  EXPECT_TRUE(redone->material_slots.empty());
  const QuaderApiResult<QuaderProjectMaterialInfo> active_after_redo =
      harness.editor().project_assets().active_material();
  ASSERT_TRUE(active_after_redo.ok()) << active_after_redo.error_message();
  EXPECT_EQ(active_after_redo.value().id, built_in);
  expect_same_selection_identity(exact_selection(harness.project()),
                                 selection_before);
}

TEST(ProjectMaterialBehaviorTests,
     FaceProjectMaterialRejectsNonProjectAndNonEditableTargetsWithoutHistory) {
  ProjectMaterialBehaviorHarness harness("face-material-rejections");
  harness.create();
  const QuaderApiResult<QuaderModelingObjectRef> box =
      harness.editor().modeling().create_box(
          {.min = {-1.0f, -1.0f, -1.0f},
           .max = {1.0f, 1.0f, 1.0f}},
          "Rejected Target");
  ASSERT_TRUE(box.ok()) << box.error_message();
  const quader_poly::Document *document =
      authored_document(harness.project(), box.value().entity);
  ASSERT_NE(document, nullptr);
  ASSERT_FALSE(document->faces.empty());
  const quader_poly::ElementId face = document->faces.front().id;
  const std::vector<std::pair<quader_poly::ElementId, std::uint32_t>>
      slots_before =
      face_slot_snapshot(harness.project(), box.value().entity);
  const QuaderApiResult<QuaderModelingObjectRef> transient =
      harness.editor().modeling().create_box(
          {.min = {4.0f, -1.0f, -1.0f},
           .max = {6.0f, 1.0f, 1.0f}},
          "Rejected Redo Guard");
  ASSERT_TRUE(transient.ok()) << transient.error_message();
  ASSERT_TRUE(harness.editor().history().undo().ok());
  const QuaderHistorySnapshot history_before =
      harness.editor().query().history();
  ASSERT_TRUE(history_before.can_undo);
  ASSERT_TRUE(history_before.can_redo);
  const ModelingSelectionExport selection_before =
      exact_selection(harness.project());

  const QuaderApiResult<QuaderFaceProjectMaterial> initial =
      harness.editor().modeling().project_material_for_face(
          box.value().modeling_object_id, face);
  ASSERT_TRUE(initial.ok()) << initial.error_message();
  EXPECT_EQ(initial.value().id,
            project_asset_id(kDefaultProjectMaterialId));
  const QuaderApiResult<QuaderFaceProjectMaterial> missing_object =
      harness.editor().modeling().project_material_for_face(
          kInvalidQuaderModelingObjectId, face);
  EXPECT_FALSE(missing_object.ok());
  EXPECT_FALSE(missing_object.changed());
  const QuaderApiResult<QuaderFaceProjectMaterial> missing_face =
      harness.editor().modeling().project_material_for_face(
          box.value().modeling_object_id, kInvalidQuaderElementId);
  EXPECT_FALSE(missing_face.ok());
  EXPECT_FALSE(missing_face.changed());

  const ProjectMaterialFixture fixture(harness.project());
  const MaterialComponent project_component =
      *scene_material(harness.project(), box.value().entity);

  ASSERT_TRUE(fixture.set_material(
      box.value().entity,
      MaterialComponent{.material = static_cast<AssetId>(7331u)}));
  const QuaderApiResult<QuaderFaceProjectMaterial> embedded =
      harness.editor().modeling().project_material_for_face(
          box.value().modeling_object_id, face);
  EXPECT_FALSE(embedded.ok());
  EXPECT_NE(embedded.error_message().find("project material"),
            std::string::npos);

  ASSERT_TRUE(fixture.clear_material(box.value().entity));
  const QuaderApiResult<QuaderFaceProjectMaterial> absent =
      harness.editor().modeling().project_material_for_face(
          box.value().modeling_object_id, face);
  EXPECT_FALSE(absent.ok());
  EXPECT_NE(absent.error_message().find("no material"), std::string::npos);
  ASSERT_TRUE(fixture.set_material(box.value().entity, project_component));

  ASSERT_TRUE(fixture.set_visible(box.value().entity, false));
  EXPECT_FALSE(harness.editor()
                   .modeling()
                   .project_material_for_face(box.value().modeling_object_id,
                                              face)
                   .ok());
  EXPECT_FALSE(harness.editor()
                   .modeling()
                   .apply_face_material_to_mesh(
                       box.value().modeling_object_id, face)
                   .ok());
  ASSERT_TRUE(fixture.set_visible(box.value().entity, true));

  ASSERT_TRUE(fixture.set_locked(box.value().entity, true));
  EXPECT_FALSE(harness.editor()
                   .modeling()
                   .project_material_for_face(box.value().modeling_object_id,
                                              face)
                   .ok());
  EXPECT_FALSE(harness.editor()
                   .modeling()
                   .apply_face_material_to_mesh(
                       box.value().modeling_object_id, face)
                   .ok());
  ASSERT_TRUE(fixture.set_locked(box.value().entity, false));

  EXPECT_EQ(face_slot_snapshot(harness.project(), box.value().entity),
            slots_before);
  expect_same_history(harness.editor().query().history(), history_before);
  expect_same_selection_identity(exact_selection(harness.project()),
                                 selection_before);

  const QuaderMeshHandle stale_handle =
      harness.editor().modeling().mesh(box.value());
  ASSERT_TRUE(stale_handle.valid());
  const QuaderApiResult<SceneSessionId> replacement =
      harness.editor().scene_documents().new_scene();
  ASSERT_TRUE(replacement.ok()) << replacement.error_message();
  const QuaderApiResult<QuaderFaceProjectMaterial> stale =
      stale_handle.materials().project_material_for_face(face);
  EXPECT_FALSE(stale.ok());
  EXPECT_NE(stale.error_message().find("stale"), std::string::npos);
}

TEST(ProjectMaterialBehaviorTests,
     FaceMaterialMeshNormalizationSupportsEmbeddedMaterialReferences) {
  ProjectMaterialBehaviorHarness harness("embedded-face-material-to-mesh");
  harness.create();
  const QuaderApiResult<QuaderAssetRef> embedded_asset =
      harness.editor().assets().add_material(
          QuaderMaterialArgs{.name = "Embedded Face Material"});
  ASSERT_TRUE(embedded_asset.ok()) << embedded_asset.error_message();
  const QuaderApiResult<QuaderModelingObjectRef> box =
      harness.editor().modeling().create_box(
          {.min = {-1.0f, -1.0f, -1.0f},
           .max = {1.0f, 1.0f, 1.0f}},
          "Embedded Material Target");
  ASSERT_TRUE(box.ok()) << box.error_message();
  const quader_poly::Document *document =
      authored_document(harness.project(), box.value().entity);
  ASSERT_NE(document, nullptr);
  ASSERT_GE(document->faces.size(), 2u);
  const quader_poly::ElementId sampled_face = document->faces.front().id;

  const ProjectMaterialFixture fixture(harness.project());
  MaterialComponent mixed;
  mixed.material =
      MaterialReference::project(project_asset_id(kDefaultProjectMaterialId));
  mixed.material_slots.emplace(7u, embedded_asset.value().id);
  ASSERT_TRUE(fixture.set_material(box.value().entity, mixed));
  ASSERT_TRUE(harness.editor()
                  .modeling()
                  .mesh(box.value())
                  .materials()
                  .assign_to_face(sampled_face, 7u)
                  .ok());
  const MaterialComponent *baseline_component =
      scene_material(harness.project(), box.value().entity);
  ASSERT_NE(baseline_component, nullptr);
  const MaterialComponent baseline = *baseline_component;
  const std::vector<std::pair<quader_poly::ElementId, std::uint32_t>>
      baseline_slots =
          face_slot_snapshot(harness.project(), box.value().entity);
  const ModelingSelectionExport selection_before =
      exact_selection(harness.project());
  const QuaderHistorySnapshot history_before_query =
      harness.editor().query().history();

  const QuaderApiResult<QuaderFaceProjectMaterial> project_query =
      harness.editor().modeling().project_material_for_face(
          box.value().modeling_object_id, sampled_face);
  EXPECT_FALSE(project_query.ok());
  EXPECT_NE(project_query.error_message().find("project material"),
            std::string::npos);
  expect_same_history(harness.editor().query().history(),
                      history_before_query);

  const QuaderApiVoidResult applied =
      harness.editor().modeling().apply_face_material_to_mesh(
          box.value().modeling_object_id, sampled_face);
  ASSERT_TRUE(applied.ok()) << applied.error_message();
  EXPECT_TRUE(applied.changed());
  const MaterialComponent *normalized =
      scene_material(harness.project(), box.value().entity);
  ASSERT_NE(normalized, nullptr);
  EXPECT_TRUE(normalized->material.embedded());
  EXPECT_EQ(normalized->material.embedded_asset(), embedded_asset.value().id);
  EXPECT_TRUE(normalized->material_slots.empty());
  expect_all_face_slots(harness.project(), box.value().entity, 0u);
  expect_same_selection_identity(exact_selection(harness.project()),
                                 selection_before);

  ASSERT_TRUE(harness.editor().history().undo().ok());
  ASSERT_NE(scene_material(harness.project(), box.value().entity), nullptr);
  EXPECT_EQ(*scene_material(harness.project(), box.value().entity), baseline);
  EXPECT_EQ(face_slot_snapshot(harness.project(), box.value().entity),
            baseline_slots);
  ASSERT_TRUE(harness.editor().history().redo().ok());
  normalized = scene_material(harness.project(), box.value().entity);
  ASSERT_NE(normalized, nullptr);
  EXPECT_EQ(normalized->material.embedded_asset(), embedded_asset.value().id);
  EXPECT_TRUE(normalized->material_slots.empty());
  expect_all_face_slots(harness.project(), box.value().entity, 0u);
}

TEST(ProjectMaterialBehaviorTests,
     FaceProjectMaterialRequiresExplicitInstanceDefinitionEditScope) {
  ProjectMaterialBehaviorHarness harness("face-material-instance-scope");
  harness.create();
  const QuaderApiResult<QuaderModelingObjectRef> member =
      harness.editor().modeling().create_box(
          {.min = {-1.0f, -1.0f, -1.0f},
           .max = {1.0f, 1.0f, 1.0f}},
          "Instance Material Member");
  ASSERT_TRUE(member.ok()) << member.error_message();
  const quader_poly::Document *document =
      authored_document(harness.project(), member.value().entity);
  ASSERT_NE(document, nullptr);
  ASSERT_FALSE(document->faces.empty());
  const quader_poly::ElementId face = document->faces.front().id;

  ASSERT_TRUE(harness.editor()
                  .selection()
                  .set_mode(QuaderSelectionMode::Object)
                  .ok());
  ASSERT_TRUE(harness.editor().selection().select_object(member.value()).ok());
  const QuaderApiResult<QuaderInstanceCreateResult> instance =
      harness.editor().modeling().create_instance_from_selection();
  ASSERT_TRUE(instance.ok()) << instance.error_message();
  ASSERT_EQ(instance.value().definition.members.size(), 1u);
  EXPECT_EQ(instance.value().definition.members.front().id,
            member.value().entity);

  const QuaderApiResult<QuaderFaceProjectMaterial> ambiguous =
      harness.editor().modeling().project_material_for_face(
          member.value().modeling_object_id, face);
  EXPECT_FALSE(ambiguous.ok());
  EXPECT_NE(ambiguous.error_message().find("atomic instance"),
            std::string::npos);
  EXPECT_FALSE(harness.editor()
                   .modeling()
                   .apply_face_material_to_mesh(
                       member.value().modeling_object_id, face)
                   .ok());

  const EntityId occurrence = instance.value().occurrence.root.id;
  ASSERT_TRUE(harness.editor().selection().select_object(occurrence).ok());
  ASSERT_TRUE(harness.editor().modeling().enter_instance_edit(occurrence).ok());
  const QuaderApiResult<QuaderFaceProjectMaterial> editable =
      harness.editor().modeling().project_material_for_face(
          member.value().modeling_object_id, face);
  ASSERT_TRUE(editable.ok()) << editable.error_message();
  EXPECT_EQ(editable.value().id,
            project_asset_id(kDefaultProjectMaterialId));
  const ProjectMaterialFixture fixture(harness.project());
  const QuaderHistorySnapshot history_before_unselectable =
      harness.editor().query().history();
  ASSERT_TRUE(fixture.set_selectable(occurrence, false));
  EXPECT_FALSE(harness.editor()
                   .modeling()
                   .project_material_for_face(
                       member.value().modeling_object_id, face)
                   .ok());
  EXPECT_FALSE(harness.editor()
                   .modeling()
                   .apply_face_material_to_mesh(
                       member.value().modeling_object_id, face)
                   .ok());
  expect_same_history(harness.editor().query().history(),
                      history_before_unselectable);
  ASSERT_TRUE(fixture.set_selectable(occurrence, true));
  const QuaderApiVoidResult unchanged =
      harness.editor().modeling().apply_face_material_to_mesh(
          member.value().modeling_object_id, face);
  ASSERT_TRUE(unchanged.ok()) << unchanged.error_message();
  EXPECT_FALSE(unchanged.changed());

  ASSERT_TRUE(harness.editor().modeling().exit_instance_edit().ok());
  EXPECT_FALSE(harness.editor()
                   .modeling()
                   .project_material_for_face(
                       member.value().modeling_object_id, face)
                   .ok());
}

TEST(ProjectMaterialPersistenceTests,
     Qdr11StoresStableReferencesAndDependenciesWithoutMaterialPayloads) {
  ProjectMaterialBehaviorHarness harness("qdr-project-reference");
  harness.create();
  constexpr std::string_view kQmatMarker =
      "QMAT_PAYLOAD_MUST_NEVER_BE_EMBEDDED_IN_A_SCENE_7f156";
  constexpr std::string_view kPreviewMarker =
      "PNG_PAYLOAD_MUST_NEVER_BE_EMBEDDED_IN_A_SCENE_943ac";
  const InstalledMaterial amber{
      .path = project_path("Materials/amber.qmat"),
      .material = project_material(
          kAmberMaterialId, "Amber", {1.0f, 0.5f, 0.1f},
          ProjectMaterialTextureSlot{
              .id = project_asset_id(kAmberTextureId),
              .project_relative_path = "Textures/amber_albedo.png",
              .color_space = ProjectTextureColorSpace::Srgb,
          }),
      .preview_bytes = bytes_from_text(kPreviewMarker),
      .additive_qmat_marker = std::string(kQmatMarker),
  };
  const std::array installed{amber};
  harness.install_and_reopen(installed);
  ASSERT_TRUE(harness.editor()
                  .project_assets()
                  .set_active_material(amber.material.id)
                  .ok());

  const QuaderApiResult<QuaderModelingObjectRef> box =
      harness.editor().modeling().create_box(
          {.min = {-1.0f, -1.0f, -1.0f},
           .max = {1.0f, 1.0f, 1.0f}},
          "Persisted Project Material");
  ASSERT_TRUE(box.ok()) << box.error_message();
  const SceneSessionId scene_id =
      harness.editor().scene_documents().active_scene_id().value();
  const ProjectRelativePath scene_path =
      project_path("Scenes/project_material.qdr");
  ASSERT_TRUE(
      harness.editor().scene_documents().save_as(scene_id, scene_path).ok());

  const std::filesystem::path scene_file =
      harness.root() / scene_path.native_path();
  const std::vector<std::byte> scene_bytes = read_bytes(scene_file);
  ASSERT_GE(scene_bytes.size(), quader_io::kQdrBinaryHeaderSize);
  EXPECT_EQ(scene_bytes[6], std::byte{1});
  EXPECT_EQ(scene_bytes[7], std::byte{0});
  EXPECT_FALSE(contains_text(scene_bytes, kQmatMarker));
  EXPECT_FALSE(contains_text(scene_bytes, kPreviewMarker));

  const QdrProjectReadResult read =
      QdrDocumentReader::deserialize_project_document(scene_bytes);
  ASSERT_TRUE(read.ok) << read.message;
  const MaterialComponent *material = read.document.scene().material(
      box.value().entity);
  ASSERT_NE(material, nullptr);
  ASSERT_TRUE(material->material.project_owned());
  EXPECT_EQ(material->material.project_asset(), amber.material.id);
  ASSERT_EQ(read.project_asset_dependencies.size(), 1u);
  EXPECT_EQ(read.project_asset_dependencies.front().stable_id,
            amber.material.id);
  EXPECT_EQ(read.project_asset_dependencies.front().kind,
            ProjectAssetKind::Material);
  EXPECT_EQ(read.project_asset_dependencies.front().path, amber.path);

  ASSERT_TRUE(harness.editor().project().close().ok());
  std::error_code error;
  ASSERT_TRUE(std::filesystem::remove(
      harness.root() / amber.path.native_path(), error));
  ASSERT_FALSE(error) << error.message();

  ProjectSession reopened;
  reopened.set_resource_root(source_resource_root());
  const ProjectSessionResult restored = reopened.open(harness.manifest_path());
  ASSERT_TRUE(restored.ok) << restored.message;
  ASSERT_FALSE(restored.warnings.empty());
  EXPECT_NE(restored.warnings.front().find("missing project material"),
            std::string::npos);
  EXPECT_TRUE(reopened.open_scenes().empty());

  const ProjectSessionResult direct = reopened.open_scene(scene_path);
  EXPECT_FALSE(direct.ok);
  EXPECT_NE(direct.message.find("missing project material"),
            std::string::npos);
}

} // namespace
} // namespace quader
