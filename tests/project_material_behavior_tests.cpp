////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"
#include "api/quader_polygon_document_internal_access.hpp"
#include "assets/default_material_catalog.hpp"
#include "editor/session/project_session.hpp"
#include "io/project/project_manifest.hpp"
#include "io/project/qmat_codec.hpp"
#include "io/qdr/qdr_binary_container.hpp"
#include "io/qdr/qdr_document_reader.hpp"

#include <gtest/gtest.h>
#include <mesh/polygon/document.hpp>

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
