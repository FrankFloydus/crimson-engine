////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"
#include "api/quader_polygon_document_internal_access.hpp"
#include "api/quader_project_assets_api_internal_access.hpp"
#include "assets/default_material_catalog.hpp"
#include "editor/material/project_material_persistence.hpp"
#include "editor/material/project_material_texture_snapshot.hpp"
#include "editor/modeling/modeling_types.hpp"
#include "editor/projection/mesh_projection_service.hpp"
#include "editor/session/project_session.hpp"
#include "io/project/project_manifest.hpp"
#include "io/project/qmat_codec.hpp"
#include "io/qdr/qdr_binary_container.hpp"
#include "io/qdr/qdr_document_reader.hpp"
#include "scene/scene_texture_mapping.hpp"
#include "render/mesh_quad_uv_interpolation.hpp"
#include "support/project_material_fixture.hpp"

#include <gtest/gtest.h>
#include <quader/modeling/neutral/polygon/document.hpp>
#include <quader/modeling/neutral/polygon/document_texture.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <QCoreApplication>
#include <QImage>
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

void write_texture_png(const std::filesystem::path &path, int width, int height) {
  QImage image(width, height, QImage::Format_RGBA8888);
  image.fill(0xffb47828u);
  ASSERT_TRUE(image.save(QString::fromStdWString(path.wstring()), "PNG"));
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

quader_poly::FaceTextureTransform face_texture_transform(
    ProjectSession &project, EntityId entity, quader_poly::ElementId face_id) {
  const quader_poly::Document *document = authored_document(project, entity);
  EXPECT_NE(document, nullptr);
  const quader_poly::Face *face =
      document != nullptr ? quader_poly::find_face(*document, face_id) : nullptr;
  EXPECT_NE(face, nullptr);
  return face != nullptr ? face->texture_transform
                         : quader_poly::FaceTextureTransform{};
}

QuaderApiVoidResult edit_selected_face_texture(
    QuaderEditorApi &editor, QuaderFaceTextureField field, double value) {
  const QuaderFaceTextureSettings settings =
      editor.modeling().face_texture_settings();
  return editor.modeling().set_face_texture_setting({
      .field = field,
      .value = value,
      .selection_token = settings.selection_token,
  });
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

void expect_primitive_project_material(ProjectSession &project,
                                       QuaderEditorApi &editor,
                                       QuaderModelingObjectId object,
                                       ProjectAssetId expected) {
  const auto reference = editor.query().modeling_object_for_id(object);
  ASSERT_TRUE(reference.has_value());
  expect_primary_material(project, reference->entity, expected);
  expect_all_face_slots(project, reference->entity, 0u);
  const auto *document = authored_document(project, reference->entity);
  ASSERT_NE(document, nullptr);
  for (const auto &face : document->faces) {
    const auto material = editor.modeling().project_material_for_face(object, face.id);
    ASSERT_TRUE(material.ok()) << material.error_message();
    EXPECT_EQ(material.value().id, expected);
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

std::vector<std::pair<EntityId, bool>>
scene_selection_identity(QuaderEditorApi &editor) {
  std::vector<std::pair<EntityId, bool>> result;
  for (const auto &entity : editor.query().entities()) {
    if (entity.selected) {
      result.emplace_back(entity.id, entity.active);
    }
  }
  std::ranges::sort(result);
  return result;
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

quader_poly::Document texture_transfer_quad(bool folded = false) {
  quader_poly::Document document;
  const std::array<QVec3, 4> positions =
      folded ? std::array<QVec3, 4>{{
                   {2.0f, 0.0f, 0.0f}, {2.0f, 0.0f, -2.0f},
                   {2.0f, 2.0f, -2.0f}, {2.0f, 2.0f, 0.0f}}}
             : std::array<QVec3, 4>{{
                   {0.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f},
                   {2.0f, 2.0f, 0.0f}, {0.0f, 2.0f, 0.0f}}};
  std::vector<quader_poly::ElementId> vertices;
  for (const QVec3 position : positions) {
    vertices.push_back(quader_poly::add_vertex(document, position));
  }
  EXPECT_NE(quader_poly::add_face(document, vertices), quader_poly::kInvalidElementId);
  document.faces.front().uvs = {
      {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
  return document;
}

quader_poly::Document sloped_texture_continuation_document() {
  // Captured sloped wall, its tapered side faces, and the separated thin cap.
  // Stable sparse IDs retain the original edge relationships without a scene file.
  quader_poly::Document document;
  constexpr float kLeft = 0.16870377957820892F;
  constexpr float kRight = 1.5481683015823364F;
  constexpr float kLowerSeam = 2.270589828491211F;
  constexpr float kUpperSeam = 0.6056923866271973F;
  constexpr float kCapFront = 0.5279415249824524F;
  constexpr float kCapBack = 0.45275720953941345F;
  document.vertices = {
      {26, {kRight, 1.5F, 0.5F}}, {32, {kLeft, 1.5F, 0.5F}},
      {53, {kRight, 1.5F, kLowerSeam}}, {55, {kLeft, 1.5F, kLowerSeam}},
      {1381, {kRight, 3.75F, kCapFront}}, {1382, {kRight, 3.75F, kCapBack}},
      {1383, {kLeft, 3.75F, kCapBack}}, {1384, {kLeft, 3.75F, kCapFront}},
      {1385, {kLeft, 3.75F, kUpperSeam}}, {1386, {kRight, 3.75F, kUpperSeam}}};
  document.faces = {
      {.id = 68, .vertices = {1381, 1382, 1383, 1384}},
      {.id = 1381, .vertices = {32, 55, 1385, 1384},
       .uvs = {{5.375F, 0.125F}, {5.817647457122803F, 0.125F},
               {5.40142297744751F, -0.4375F}, {5.381985187530518F, -0.4375F}},
       .texture_transform = {.scale = {0.125F, 0.125F}}},
      {.id = 1382, .vertices = {55, 53, 1386, 1385},
       .uvs = {{-0.1562500149011612F, 0.019437480717897415F},
               {-0.1131417453289032F, 0.019437480717897415F},
               {-0.1131417453289032F, -0.0680311769247055F},
               {-0.1562500149011612F, -0.0680311769247055F}},
       .material_slot = 1,
       .texture_transform = {.scale = {0.125F, 0.125F}}},
      {.id = 1383, .vertices = {53, 26, 1381, 1386}}};
  document.next_vertex_id = 1387;
  document.next_face_id = 1384;
  return document;
}

quader_poly::Document tapered_side_texture_continuation_document(float rotation) {
  auto document = sloped_texture_continuation_document();
  auto &source = *quader_poly::find_face(document, 1381);
  source.uvs = {{0.4863211214542389F, 0.3792925179004669F},
                {0.5307994484901428F, 0.41220441460609436F},
                {0.5307994484901428F, 0.32473576068878174F},
                {0.528846263885498F, 0.32329052686691284F}};
  source.material_slot = 1;
  source.texture_transform.rotation_degrees = rotation;
  document.vertices.insert(document.vertices.end(), {
      {15, {0.0F, 1.5F, 0.5F}},
      {38, {-0.16089105606079102F, 1.5F, 0.5F}},
      {73, {0.0F, 1.5F, -1.2121429443359375F}},
      {75, {-0.16089105606079102F, 1.5F, -1.2121429443359375F}},
      {1377, {-0.16089105606079102F, 1.75F, 0.5F}},
      {1380, {-0.8606553077697754F, 1.75F, 0.5F}},
      {1387, {-0.8606553077697754F, 1.75F, 2.270589828491211F}},
      {1388, {-0.16089105606079102F, 1.75F, 2.270589828491211F}}});
  document.faces.insert(document.faces.end(), {
      {.id = 35, .vertices = {1377, 1380, 1387, 1388},
       .uvs = {{-0.16654984652996063F, 0.6126062273979187F},
               {-0.1884174793958664F, 0.6126062273979187F},
               {-0.1884174793958664F, 0.667937159538269F},
               {-0.16654984652996063F, 0.667937159538269F}},
       .texture_transform = {.scale = {0.125F, 0.125F}}},
      {.id = 72, .vertices = {15, 73, 75, 38},
       .uvs = {{-0.1615220010280609F, 0.6165440678596497F},
               {-0.1615220010280609F, 0.5630396008491516F},
               {-0.16654984652996063F, 0.5630396008491516F},
               {-0.16654984652996063F, 0.6165440678596497F}},
       .texture_transform = {.scale = {0.125F, 0.125F}}}});
  document.next_vertex_id = 1389;
  return document;
}

std::optional<QVec2> rendered_face_uv_at(ProjectSession &project, EntityId entity,
                                        quader_poly::ElementId face_id, QVec3 point) {
  const auto &scene = project.active_scene()->session->scene();
  const auto *binding = scene.mesh(entity);
  if (binding == nullptr) return std::nullopt;
  const auto *asset = project.active_scene()->session->assets().mesh(binding->mesh);
  const auto sampling = effective_texture_sampling_from_local(scene, entity);
  if (asset == nullptr || !asset->authored_polygon_document || !sampling) return std::nullopt;
  const auto *face = quader_poly::find_face(*asset->authored_polygon_document, face_id);
  if (face == nullptr) return std::nullopt;
  MeshAsset mesh = *asset;
  MeshProjectionService::apply_mesh_texture_mapping(mesh, *sampling);
  const auto &geometry = mesh.geometry();
  const auto quad = build_mesh_quad_uv_interpolations(
      geometry.vertices, geometry.indices, geometry.primitives);
  for (std::size_t index = 0; index + 2U < geometry.indices.size(); index += 3U) {
    const std::array indices{geometry.indices[index], geometry.indices[index + 1U],
                             geometry.indices[index + 2U]};
    if (!std::ranges::all_of(indices, [&](std::uint32_t corner) {
          return std::ranges::find(face->vertices, geometry.vertices[corner].source_vertex_id) !=
                 face->vertices.end();
        })) continue;
    const auto &a = geometry.vertices[indices[0]];
    const auto &b = geometry.vertices[indices[1]];
    const auto &c = geometry.vertices[indices[2]];
    const auto ab = b.position - a.position;
    const auto ac = c.position - a.position;
    const auto relative = point - a.position;
    const float aa = dot(ab, ab), cc = dot(ac, ac), mixed = dot(ab, ac);
    const float determinant = aa * cc - mixed * mixed;
    if (determinant <= 1.0e-10F) continue;
    const float wb = (dot(relative, ab) * cc - dot(relative, ac) * mixed) / determinant;
    const float wc = (dot(relative, ac) * aa - dot(relative, ab) * mixed) / determinant;
    const float wa = 1.0F - wb - wc;
    if (wa < -0.0001F || wb < -0.0001F || wc < -0.0001F ||
        length_squared(relative - ab * wb - ac * wc) > 1.0e-8F) continue;
    const QVec2 linear = a.uv * wa + b.uv * wb + c.uv * wc;
    auto interpolation = quad[indices[0]];
    interpolation.coordinates = quad[indices[0]].coordinates * wa +
                                quad[indices[1]].coordinates * wb +
                                quad[indices[2]].coordinates * wc;
    return evaluate_mesh_quad_uv_interpolation(linear, interpolation);
  }
  return std::nullopt;
}

void expect_same_authored_document(const quader_poly::Document &actual,
                                   const quader_poly::Document &expected) {
  ASSERT_EQ(actual.vertices.size(), expected.vertices.size());
  ASSERT_EQ(actual.faces.size(), expected.faces.size());
  EXPECT_EQ(actual.next_vertex_id, expected.next_vertex_id);
  EXPECT_EQ(actual.next_face_id, expected.next_face_id);
  EXPECT_EQ(actual.hard_normal_edges, expected.hard_normal_edges);
  EXPECT_EQ(actual.soft_normal_edges, expected.soft_normal_edges);
  for (std::size_t index = 0; index < actual.vertices.size(); ++index) {
    EXPECT_EQ(actual.vertices[index].id, expected.vertices[index].id);
    EXPECT_EQ(actual.vertices[index].position.x, expected.vertices[index].position.x);
    EXPECT_EQ(actual.vertices[index].position.y, expected.vertices[index].position.y);
    EXPECT_EQ(actual.vertices[index].position.z, expected.vertices[index].position.z);
  }
  for (std::size_t index = 0; index < actual.faces.size(); ++index) {
    const auto &face = actual.faces[index];
    const auto &baseline = expected.faces[index];
    EXPECT_EQ(face.id, baseline.id);
    EXPECT_EQ(face.vertices, baseline.vertices);
    EXPECT_EQ(face.material_slot, baseline.material_slot);
    EXPECT_EQ(face.normal_shading, baseline.normal_shading);
    EXPECT_EQ(face.texture_transform, baseline.texture_transform);
    ASSERT_EQ(face.uvs.size(), baseline.uvs.size());
    for (std::size_t corner = 0; corner < face.uvs.size(); ++corner) {
      EXPECT_EQ(face.uvs[corner].x, baseline.uvs[corner].x);
      EXPECT_EQ(face.uvs[corner].y, baseline.uvs[corner].y);
    }
  }
}

std::vector<QVec2> effective_face_corner_uvs(
    ProjectSession &project, EntityId entity, quader_poly::ElementId face_id,
    EntityId occurrence = kInvalidEntityId) {
  const auto *document = authored_document(project, entity);
  EXPECT_NE(document, nullptr);
  if (document == nullptr) {
    return {};
  }
  const auto *face = quader_poly::find_face(*document, face_id);
  EXPECT_NE(face, nullptr);
  if (face == nullptr) {
    return {};
  }
  const auto sampling = effective_texture_sampling_from_local(
      project.active_scene()->session->scene(), entity, occurrence);
  EXPECT_TRUE(sampling.has_value());
  if (!sampling.has_value()) {
    return {};
  }
  const auto &matrix = sampling->values;
  const quader_poly::Transform3 correction{
      .x_axis = {matrix[0], matrix[1], matrix[2]},
      .y_axis = {matrix[4], matrix[5], matrix[6]},
      .z_axis = {matrix[8], matrix[9], matrix[10]},
      .origin = {matrix[12], matrix[13], matrix[14]},
  };
  const QVec3 normal = quader_poly::face_normal(*document, *face);
  const auto carrier = quader_poly::face_texture_carrier(*document, *face, normal);
  std::vector<QVec2> result;
  for (std::size_t corner = 0; corner < face->vertices.size(); ++corner) {
    const auto *vertex = quader_poly::find_vertex(*document, face->vertices[corner]);
    EXPECT_NE(vertex, nullptr);
    if (vertex == nullptr) {
      return {};
    }
    const QVec2 base = face->uvs.empty()
                          ? quader_poly::generated_texture_uv(vertex->position, normal)
                          : face->uvs[corner];
    result.push_back(quader_poly::evaluate_texture_mapping(
        quader_poly::apply_face_texture_transform(base, face->texture_transform),
        vertex->position, normal, carrier, correction));
  }
  return result;
}

void expect_same_revisions(const QuaderRevisionStamp &actual,
                           const QuaderRevisionStamp &expected) {
  EXPECT_EQ(actual.project_revision, expected.project_revision);
  EXPECT_EQ(actual.scene_revision, expected.scene_revision);
  EXPECT_EQ(actual.scene_layer_revision, expected.scene_layer_revision);
  EXPECT_EQ(actual.asset_revision, expected.asset_revision);
  EXPECT_EQ(actual.modeling_content_revision, expected.modeling_content_revision);
  EXPECT_EQ(actual.modeling_selection_revision, expected.modeling_selection_revision);
}

TEST(ProjectMaterialPublicApiTests,
     CatalogPreviewEventsAndPersistenceUseProjectIdentity) {
  ProjectMaterialBehaviorHarness harness("public-api");
  std::vector<QuaderEvent> catalog_events;
  const QuaderEventSubscription catalog_subscription =
      harness.editor().events().subscribe(
          quader_event::kProjectAssetsChanged,
          [&](const QuaderEvent &event) { catalog_events.push_back(event); });
  ASSERT_TRUE(catalog_subscription.valid());

  harness.create();
  ASSERT_EQ(catalog_events.size(), 1u);
  EXPECT_FALSE(catalog_events.back().project_asset_id.has_value());

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

  EXPECT_FALSE(harness.editor().project().dirty());
  const std::filesystem::path canonical_root =
      std::filesystem::canonical(harness.root());
  EXPECT_EQ(catalog_events.back().project_root,
            std::optional<std::string>(canonical_root.string()));
  const auto manifest_before = read_bytes(harness.manifest_path());

  const QuaderApiResult<QuaderProjectMaterialPreview> preview =
      harness.editor().project_assets().material_preview(amber.material.id);
  ASSERT_TRUE(preview.ok()) << preview.error_message();
  EXPECT_EQ(preview.value().encoded_bytes, amber.preview_bytes);
  EXPECT_NE(preview.value().content_revision, 0u);

  const ProjectAssetId missing = ProjectAssetId::generate();
  EXPECT_FALSE(harness.editor().project_assets().material_preview(missing).ok());
  EXPECT_EQ(read_bytes(harness.manifest_path()), manifest_before);
  EXPECT_EQ(catalog_events.size(), 3u);

  ASSERT_TRUE(harness.editor().project().close().ok());
  ASSERT_TRUE(harness.editor().project().open(harness.manifest_path()).ok());
  const auto restored = harness.editor().project_assets().materials();
  ASSERT_TRUE(restored.ok()) << restored.error_message();
  ASSERT_EQ(restored.value().size(), 2u);
  EXPECT_EQ(restored.value().front().id, amber.material.id);

  QuaderStandaloneEditor standalone = create_standalone_editor();
  EXPECT_FALSE(standalone.editor().project_assets().materials().ok());
  EXPECT_FALSE(standalone.editor().project_assets()
                   .material_preview(amber.material.id).ok());
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
  const auto manifest_before = ProjectManifestStore::open(harness.manifest_path());
  ASSERT_TRUE(manifest_before.ok()) << manifest_before.error().message;
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
  const QuaderEventSubscription assets_subscription =
      harness.editor().events().subscribe(
          quader_event::kProjectAssetsChanged,
          [&](const QuaderEvent &event) { assets_events.push_back(event); });
  ASSERT_TRUE(assets_subscription.valid());

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
  EXPECT_FALSE(assets_events.front().project_asset_id.has_value());
  const auto manifest_after = ProjectManifestStore::open(harness.manifest_path());
  ASSERT_TRUE(manifest_after.ok()) << manifest_after.error().message;

  std::vector<ProjectAssetId> imported_ids;
  imported_ids.reserve(imported.value().imported.size());
  for (const QuaderProjectMaterialImportSuccess &success :
       imported.value().imported) {
    imported_ids.push_back(success.material.id);
    EXPECT_EQ(success.material.name, "same");
    EXPECT_EQ(success.material.kind, QuaderProjectMaterialKind::Standard);
    EXPECT_NE(success.material.project_relative_path.find("Materials/"),
              std::string::npos);
    EXPECT_EQ(success.material.project_relative_path,
              imported_ids.size() == 1u
                  ? "Materials/same.qmat"
                  : "Materials/same_" + std::to_string(imported_ids.size()) +
                        ".qmat");

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
    const auto *persisted_texture = manifest_after.value().manifest.find_texture(
        standard->base_color_texture->id);
    ASSERT_NE(persisted_texture, nullptr);
    EXPECT_EQ(persisted_texture->path.portable_string(),
              standard->base_color_texture->project_relative_path);
    EXPECT_EQ(standard->base_color_texture->color_space,
              ProjectTextureColorSpace::Srgb);
    EXPECT_EQ(project_path(standard->base_color_texture->project_relative_path)
                  .native_path().filename(), "same.png");
    if (imported_ids.size() == 1u) {
      EXPECT_EQ(success.material.project_relative_path, "Materials/same.qmat");
      EXPECT_EQ(standard->base_color_texture->project_relative_path,
                "Textures/same.png");
    }
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

  EXPECT_EQ(manifest_after.value().manifest.id(), manifest_before.value().manifest.id());
  EXPECT_TRUE(std::ranges::equal(manifest_after.value().manifest.scenes(),
                                manifest_before.value().manifest.scenes()));
  EXPECT_TRUE(std::ranges::equal(manifest_after.value().manifest.open_scene_paths(),
                                manifest_before.value().manifest.open_scene_paths()));
  EXPECT_EQ(manifest_after.value().manifest.active_scene_path(),
            manifest_before.value().manifest.active_scene_path());
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
  EXPECT_NE(harness.project().material_catalog().find(
                project_asset_id(kDefaultProjectMaterialId)), nullptr);
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
      source_directory / std::filesystem::path(L"albedo β_deadbeef.PNG");
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
            QStringLiteral("albedo β_deadbeef").toUtf8().toStdString());
  EXPECT_EQ(imported.value().imported.front().material.project_relative_path,
            QStringLiteral("Materials/albedo β_deadbeef.qmat")
                .toUtf8().toStdString());
  const auto *entry = harness.project().material_catalog().find(
      imported.value().imported.front().material.id);
  ASSERT_NE(entry, nullptr);
  ASSERT_NE(entry->material.standard_material(), nullptr);
  ASSERT_TRUE(entry->material.standard_material()->base_color_texture);
  const auto &texture = *entry->material.standard_material()->base_color_texture;
  EXPECT_EQ(texture.project_relative_path,
            QStringLiteral("Textures/albedo β_deadbeef.PNG")
                .toUtf8().toStdString());
  EXPECT_EQ(read_bytes(harness.root() /
                      project_path(texture.project_relative_path).native_path()),
            original);
  const QuaderApiResult<QuaderProjectMaterialPreview> preview =
      harness.editor().project_assets().material_preview(
          imported.value().imported.front().material.id);
  ASSERT_TRUE(preview.ok()) << preview.error_message();
  EXPECT_EQ(preview.value().encoded_bytes, original);
}

TEST(ProjectMaterialPublicApiTests,
     CaseInsensitiveNameCollisionsNumberMaterialsAndSeparateTextureAssets) {
  ProjectMaterialBehaviorHarness harness("texture-case-collision");
  harness.create();
  const auto original = read_bytes(source_resource_root() / "materials" /
                                   "default" / "default_albedo.png");
  const auto first = harness.root() / "First" / "Brick.PNG";
  const auto second = harness.root() / "Second" / "brick.png";
  std::filesystem::create_directory(first.parent_path());
  std::filesystem::create_directory(second.parent_path());
  write_bytes(first, original);
  write_bytes(second, original);
  const std::array sources{first, second};
  const auto imported = harness.editor().project_assets()
                            .import_textures_as_materials(sources);
  ASSERT_TRUE(imported.ok()) << imported.error_message();
  ASSERT_EQ(imported.value().imported.size(), 2u);
  const auto &one = imported.value().imported[0].material;
  const auto &two = imported.value().imported[1].material;
  EXPECT_EQ(one.name, "Brick");
  EXPECT_EQ(two.name, "brick");
  EXPECT_EQ(one.project_relative_path, "Materials/Brick.qmat");
  const auto second_qmat = project_path(two.project_relative_path).native_path();
  EXPECT_EQ(second_qmat.filename(), "brick_2.qmat");
  EXPECT_EQ(second_qmat.parent_path(), std::filesystem::path("Materials"));
  EXPECT_NE(one.id, two.id);
  const auto *first_entry = harness.project().material_catalog().find(one.id);
  const auto *second_entry = harness.project().material_catalog().find(two.id);
  ASSERT_NE(first_entry, nullptr);
  ASSERT_NE(second_entry, nullptr);
  const auto &first_texture =
      *first_entry->material.standard_material()->base_color_texture;
  const auto &second_texture =
      *second_entry->material.standard_material()->base_color_texture;
  EXPECT_NE(first_texture.id, second_texture.id);
  EXPECT_EQ(first_texture.project_relative_path, "Textures/Brick.PNG");
  const auto second_texture_path =
      project_path(second_texture.project_relative_path).native_path();
  EXPECT_EQ(second_texture_path.filename(), "brick.png");
  EXPECT_NE(second_texture_path.parent_path(), std::filesystem::path("Textures"));
  EXPECT_EQ(read_bytes(harness.root() / second_texture_path), original);
  EXPECT_EQ(read_bytes(harness.root() / "Textures/Brick.PNG"), original);
}

TEST(ProjectMaterialPublicApiTests,
     TextureBatchFallbackNamesAdvanceWithinTheSameImport) {
  ProjectMaterialBehaviorHarness harness("texture-fallback-sequence");
  harness.create();
  const auto original = read_bytes(source_resource_root() / "materials" /
                                   "default" / "default_albedo.png");
  const auto first = harness.root() / "First" / " .png";
  const auto second = harness.root() / "Second" / " .png";
  std::filesystem::create_directory(first.parent_path());
  std::filesystem::create_directory(second.parent_path());
  write_bytes(first, original);
  write_bytes(second, original);
  const std::array sources{first, second};
  const auto imported = harness.editor().project_assets()
                            .import_textures_as_materials(sources);
  ASSERT_TRUE(imported.ok()) << imported.error_message();
  ASSERT_EQ(imported.value().imported.size(), 2u);
  EXPECT_EQ(imported.value().imported[0].material.name, "Material_1");
  EXPECT_EQ(imported.value().imported[1].material.name, "Material_2");
  EXPECT_EQ(imported.value().imported[0].material.project_relative_path,
            "Materials/Material_1.qmat");
  EXPECT_EQ(imported.value().imported[1].material.project_relative_path,
            "Materials/Material_2.qmat");
}

TEST(ProjectMaterialPublicApiTests,
     GeneratedDestinationsSafelyRepresentReservedAndUnsafeNames) {
  ProjectMaterialBehaviorHarness harness("asset-safe-names");
  harness.create();
  const auto &file_system = harness.project().workspace()->file_system;
  const auto unsafe =
      make_unique_material_destination(file_system, "../paint:<red>?*");
  ASSERT_TRUE(unsafe.ok()) << unsafe.error().message;
  EXPECT_EQ(unsafe.value().path.native_path().parent_path(),
            std::filesystem::path("Materials"));
  EXPECT_EQ(unsafe.value().path.native_path().filename(),
            ".._paint__red___.qmat");
  const auto reserved =
      make_unique_texture_destination(file_system, "CON", ".PNG");
  ASSERT_TRUE(reserved.ok()) << reserved.error().message;
  EXPECT_EQ(reserved.value().path.portable_string(), "Textures/_CON.PNG");
}

TEST(ProjectMaterialPublicApiTests,
     CollisionDirectoryRollsBackWithoutChangingTheExistingTexture) {
  ProjectMaterialBehaviorHarness harness("texture-directory-rollback");
  harness.create();
  const auto &file_system = harness.project().workspace()->file_system;
  const auto original = bytes_from_text("existing texture");
  const auto existing_texture = harness.root() / "Textures/shared.PNG";
  write_bytes(existing_texture, original);
  const auto allocated =
      make_unique_texture_destination(file_system, "shared", ".PNG");
  ASSERT_TRUE(allocated.ok()) << allocated.error().message;
  const auto collision_parent =
      harness.root() / allocated.value().path.native_path().parent_path();
  EXPECT_FALSE(std::filesystem::exists(collision_parent));
  const auto existing_qmat = harness.root() / kDefaultProjectMaterialPath;
  const auto qmat_before = read_bytes(existing_qmat);
  {
    ProjectMaterialFileRollback publication;
    const auto prepared =
        publication.prepare_destination(file_system, allocated.value().path);
    ASSERT_TRUE(prepared.ok()) << prepared.error().message;
    EXPECT_TRUE(std::filesystem::is_directory(collision_parent));
    ASSERT_TRUE(publication.create_bytes(prepared.value(),
                                        bytes_from_text("new texture")).ok());
    const auto competing = project_material(
        "6d0f4ab6-969c-402c-ad93-d72c627e4e69", "Competing",
        {0.25f, 0.5f, 0.75f});
    EXPECT_FALSE(publication.create_qmat(existing_qmat, competing).ok());
    // Abandoning an uncommitted request must roll back directories as well as
    // files without touching the source that caused the naming collision.
  }
  EXPECT_FALSE(std::filesystem::exists(collision_parent));
  EXPECT_EQ(read_bytes(existing_texture), original);
  EXPECT_EQ(read_bytes(existing_qmat), qmat_before);
}

TEST(ProjectMaterialPublicApiTests,
     CompetingFileAfterDestinationAllocationIsNeverOverwritten) {
  ProjectMaterialBehaviorHarness harness("texture-create-new-race");
  harness.create();
  const auto &file_system = harness.project().workspace()->file_system;
  const auto allocated =
      make_unique_texture_destination(file_system, "raced", ".png");
  ASSERT_TRUE(allocated.ok()) << allocated.error().message;
  EXPECT_EQ(allocated.value().path.portable_string(), "Textures/raced.png");
  const auto absolute = file_system.resolve(allocated.value().path);
  ASSERT_TRUE(absolute.ok());
  const auto competing = bytes_from_text("competing import");
  write_bytes(absolute.value(), competing);
  ProjectMaterialFileRollback publication;
  EXPECT_FALSE(publication.create_bytes(absolute.value(),
                                       bytes_from_text("late import")).ok());
  EXPECT_TRUE(publication.rollback().empty());
  EXPECT_EQ(read_bytes(absolute.value()), competing);
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
  const auto manifest_before = read_bytes(harness.manifest_path());
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
  EXPECT_FALSE(result.ok());
  EXPECT_NE(result.error_message().find("No materials were imported"),
            std::string::npos);
  EXPECT_FALSE(result.changed());
  EXPECT_TRUE(result.value().imported.empty());
  ASSERT_EQ(result.value().failed.size(), 1u);
  EXPECT_EQ(result.value().failed.front().source_path, invalid);
  EXPECT_FALSE(result.value().failed.front().error_message.empty());
  EXPECT_EQ(assets_events, 0u);
  EXPECT_EQ(harness.project().material_catalog().entries().size(),
            catalog_size_before);
  EXPECT_EQ(read_bytes(harness.manifest_path()), manifest_before);
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
  const auto manifest_before = read_bytes(harness.manifest_path());
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
  const QuaderEventSubscription assets_subscription =
      harness.editor().events().subscribe(
          quader_event::kProjectAssetsChanged,
          [&](const QuaderEvent &) { ++assets_events; });
  ASSERT_TRUE(assets_subscription.valid());

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
  EXPECT_EQ(regular_files_below(harness.root() / "Materials"),
            materials_before);
  EXPECT_EQ(regular_files_below(harness.root() / "Textures"),
            textures_before);
  EXPECT_EQ(read_bytes(default_qmat), default_qmat_bytes);

  EXPECT_EQ(read_bytes(harness.manifest_path()), manifest_before);
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
     NewBoxAndMeshUseExplicitMaterialsWithoutChangingCreationDefaults) {
  ProjectAssetId requested_material_id = project_asset_id(kDefaultProjectMaterialId);
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

  requested_material_id = amber.material.id;
  const QuaderApiResult<QuaderModelingObjectRef> box =
      harness.editor().modeling().create_box({.bounds = {.min = {-1.0f, -1.0f, -1.0f},
           .max = {1.0f, 1.0f, 1.0f}}, .name = "Amber Box",
           .material = QuaderMaterialReference::project(requested_material_id)});
  ASSERT_TRUE(box.ok()) << box.error_message();
  expect_primary_material(harness.project(), box.value().entity,
                          amber.material.id);

  const QuaderApiResult<QuaderModelingObjectRef> mesh =
      harness.editor().modeling().create_mesh(QuaderPolygonDocumentInternalAccess::from_backend(
              triangle_document()),
          {.name = "Amber Mesh",
           .material = QuaderMaterialReference::project(requested_material_id)});
  ASSERT_TRUE(mesh.ok()) << mesh.error_message();
  expect_primary_material(harness.project(), mesh.value().entity,
                          amber.material.id);

  requested_material_id = blue.material.id;
  const QuaderApiResult<QuaderModelingObjectRef> second_box =
      harness.editor().modeling().create_box({.bounds = {.min = {2.0f, -1.0f, -1.0f},
           .max = {4.0f, 1.0f, 1.0f}}, .name = "Blue Box",
           .material = QuaderMaterialReference::project(requested_material_id)});
  ASSERT_TRUE(second_box.ok()) << second_box.error_message();
  expect_primary_material(harness.project(), second_box.value().entity,
                          blue.material.id);
  const auto default_box = harness.editor().modeling().create_box(
      {.min = {5.0f, -1.0f, -1.0f}, .max = {7.0f, 1.0f, 1.0f}},
      "Default Box");
  ASSERT_TRUE(default_box.ok()) << default_box.error_message();
  expect_primary_material(harness.project(), default_box.value().entity,
                          project_asset_id(kDefaultProjectMaterialId));
  const auto default_mesh = harness.editor().modeling().create_mesh(
      QuaderPolygonDocumentInternalAccess::from_backend(triangle_document()));
  ASSERT_TRUE(default_mesh.ok()) << default_mesh.error_message();
  expect_primary_material(harness.project(), default_mesh.value().entity,
                          project_asset_id(kDefaultProjectMaterialId));
}

TEST(ProjectMaterialBehaviorTests,
     ObjectApplyIsOneUndoTransactionAndRedoCapturesOriginalMaterial) {
  ProjectAssetId requested_material_id = project_asset_id(kDefaultProjectMaterialId);
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
  requested_material_id = amber.material.id;

  const QuaderApiResult<QuaderModelingObjectRef> first =
      harness.editor().modeling().create_box({.bounds = {.min = {-3.0f, -1.0f, -1.0f},
           .max = {-1.0f, 1.0f, 1.0f}}, .name = "First",
           .material = QuaderMaterialReference::project(requested_material_id)});
  const QuaderApiResult<QuaderModelingObjectRef> second =
      harness.editor().modeling().create_box({.bounds = {.min = {1.0f, -1.0f, -1.0f},
           .max = {3.0f, 1.0f, 1.0f}}, .name = "Second",
           .material = QuaderMaterialReference::project(requested_material_id)});
  ASSERT_TRUE(first.ok()) << first.error_message();
  ASSERT_TRUE(second.ok()) << second.error_message();

  requested_material_id = blue.material.id;
  ASSERT_TRUE(harness.editor()
                  .selection()
                  .set_mode(QuaderSelectionMode::Object)
                  .ok());
  ASSERT_TRUE(harness.editor()
                  .selection()
                  .select_objects({first.value(), second.value()})
                  .ok());
  const QuaderApiVoidResult applied =
      harness.editor().modeling().apply_material(QuaderMaterialReference::project(requested_material_id));
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

  EXPECT_EQ(harness.editor().query().history().undo_label, "Assign Material");
  ASSERT_TRUE(harness.editor().history().undo().ok());
  expect_primary_material(harness.project(), first.value().entity,
                          amber.material.id);
  expect_primary_material(harness.project(), second.value().entity,
                          amber.material.id);

  requested_material_id = project_asset_id(kDefaultProjectMaterialId);
  ASSERT_TRUE(harness.editor().history().redo().ok());
  expect_primary_material(harness.project(), first.value().entity,
                          blue.material.id);
  expect_primary_material(harness.project(), second.value().entity,
                          blue.material.id);
}

TEST(ProjectMaterialBehaviorTests,
     ExplicitMaterialAddsAndReplacesSolidifiedCurveMaterialsWithMeshes) {
  ProjectAssetId requested_material_id = project_asset_id(kDefaultProjectMaterialId);
  ProjectMaterialBehaviorHarness harness("solid-curve-object-apply");
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
  auto &editor = harness.editor();
  requested_material_id = amber.material.id;

  const auto mesh = editor.modeling().create_box({.bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}}, .name = "Pipe Companion",
           .material = QuaderMaterialReference::project(requested_material_id)});
  ASSERT_TRUE(mesh.ok()) << mesh.error_message();
  const auto curve = editor.modeling().create_curve({
      .points = {{.position = {0.0f, 0.0f, 0.0f}},
                 {.position = {4.0f, 0.0f, 0.0f}}},
      .name = "Material Pipe",
  });
  ASSERT_TRUE(curve.ok()) << curve.error_message();
  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Object).ok());
  ASSERT_TRUE(editor.selection().select_object(curve.value()).ok());
  const QuaderHistorySnapshot history_before_solidify = editor.query().history();
  const QuaderApiVoidResult dormant_apply =
      editor.modeling().apply_material(QuaderMaterialReference::project(requested_material_id));
  EXPECT_FALSE(dormant_apply.ok());
  EXPECT_FALSE(dormant_apply.changed());
  EXPECT_EQ(scene_material(harness.project(), curve.value().entity), nullptr);
  expect_same_history(editor.query().history(), history_before_solidify);
  ASSERT_TRUE(
      editor.tools().set_setting("curve", "curve.solidify", true).ok());
  OpenSceneSession *active_scene = harness.project().active_scene();
  ASSERT_NE(active_scene, nullptr);
  ASSERT_NE(active_scene->session, nullptr);
  const CurveComponent *curve_component =
      active_scene->session->scene().curve(curve.value().entity);
  ASSERT_NE(curve_component, nullptr);
  const CurveComponent curve_settings = *curve_component;
  const QuaderApiResult<QuaderCurveSnapshot> curve_before =
      editor.modeling().curve_snapshot(curve.value());
  ASSERT_TRUE(curve_before.ok()) << curve_before.error_message();
  EXPECT_EQ(scene_material(harness.project(), curve.value().entity), nullptr);

  const QuaderApiVoidResult added =
      editor.modeling().apply_material(QuaderMaterialReference::project(requested_material_id));
  ASSERT_TRUE(added.ok()) << added.error_message();
  ASSERT_TRUE(added.changed());
  expect_primary_material(harness.project(), curve.value().entity,
                          amber.material.id);
  EXPECT_EQ(editor.query().history().undo_label, "Assign Material");
  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_EQ(scene_material(harness.project(), curve.value().entity), nullptr);
  ASSERT_TRUE(editor.history().redo().ok());
  expect_primary_material(harness.project(), curve.value().entity,
                          amber.material.id);

  requested_material_id = blue.material.id;
  ASSERT_TRUE(editor.selection().select_objects(
      {mesh.value().entity, curve.value().entity}, curve.value().entity).ok());
  const ModelingSelectionExport selection_before =
      exact_selection(harness.project());
  const auto scene_selection_before = scene_selection_identity(editor);
  const QuaderApiVoidResult replaced =
      editor.modeling().apply_material(QuaderMaterialReference::project(requested_material_id));
  ASSERT_TRUE(replaced.ok()) << replaced.error_message();
  ASSERT_TRUE(replaced.changed());
  expect_primary_material(harness.project(), mesh.value().entity,
                          blue.material.id);
  expect_primary_material(harness.project(), curve.value().entity,
                          blue.material.id);
  EXPECT_EQ(scene_selection_identity(editor), scene_selection_before);
  expect_same_selection_identity(exact_selection(harness.project()),
                                 selection_before);
  EXPECT_EQ(*active_scene->session->scene().curve(curve.value().entity),
            curve_settings);
  const QuaderApiResult<QuaderCurveSnapshot> curve_after =
      editor.modeling().curve_snapshot(curve.value());
  ASSERT_TRUE(curve_after.ok()) << curve_after.error_message();
  EXPECT_TRUE(curve_after.value().document.has_same_geometry(
      curve_before.value().document));

  ASSERT_TRUE(editor.history().undo().ok());
  expect_primary_material(harness.project(), mesh.value().entity,
                          amber.material.id);
  expect_primary_material(harness.project(), curve.value().entity,
                          amber.material.id);
  expect_same_selection_identity(exact_selection(harness.project()),
                                 selection_before);
  requested_material_id = project_asset_id(kDefaultProjectMaterialId);
  ASSERT_TRUE(editor.history().redo().ok());
  expect_primary_material(harness.project(), mesh.value().entity,
                          blue.material.id);
  expect_primary_material(harness.project(), curve.value().entity,
                          blue.material.id);
  EXPECT_EQ(*active_scene->session->scene().curve(curve.value().entity),
            curve_settings);
}

TEST(ProjectMaterialBehaviorTests,
     GroupApplyIncludesSolidifiedCurvesAndRejectsProtectedDescendants) {
  ProjectAssetId requested_material_id = project_asset_id(kDefaultProjectMaterialId);
  ProjectMaterialBehaviorHarness harness("solid-curve-group-apply");
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
  auto &editor = harness.editor();
  requested_material_id = amber.material.id;
  const auto mesh = editor.modeling().create_box({.bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}}, .name = "Grouped Pipe Mesh",
           .material = QuaderMaterialReference::project(requested_material_id)});
  const auto curve = editor.modeling().create_curve({
      .points = {{.position = {0.0f, 2.0f, 0.0f}},
                 {.position = {4.0f, 2.0f, 0.0f}}},
      .name = "Grouped Pipe Curve",
  });
  ASSERT_TRUE(mesh.ok()) << mesh.error_message();
  ASSERT_TRUE(curve.ok()) << curve.error_message();
  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Object).ok());
  ASSERT_TRUE(editor.selection().select_object(curve.value()).ok());
  ASSERT_TRUE(
      editor.tools().set_setting("curve", "curve.solidify", true).ok());
  ASSERT_TRUE(editor.modeling().apply_material(QuaderMaterialReference::project(requested_material_id)).ok());
  ASSERT_TRUE(editor.selection().select_objects(
      {mesh.value().entity, curve.value().entity}, curve.value().entity).ok());
  const auto group = editor.scene().group_selection("Pipe Material Group");
  ASSERT_TRUE(group.ok()) << group.error_message();
  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Group).ok());
  ASSERT_TRUE(editor.selection().select_object(group.value().id).ok());
  const ProjectMaterialFixture fixture(harness.project());
  ASSERT_TRUE(fixture.set_visible(curve.value().entity, false));
  requested_material_id = blue.material.id;

  ASSERT_TRUE(fixture.set_locked(curve.value().entity, true));
  const QuaderHistorySnapshot history_before_locked = editor.query().history();
  const QuaderApiVoidResult locked =
      editor.modeling().apply_material(QuaderMaterialReference::project(requested_material_id));
  EXPECT_FALSE(locked.ok());
  EXPECT_FALSE(locked.changed());
  expect_primary_material(harness.project(), mesh.value().entity,
                          amber.material.id);
  expect_primary_material(harness.project(), curve.value().entity,
                          amber.material.id);
  expect_same_history(editor.query().history(), history_before_locked);
  ASSERT_TRUE(fixture.set_locked(curve.value().entity, false));

  const ModelingSelectionExport selection_before =
      exact_selection(harness.project());
  const auto scene_selection_before = scene_selection_identity(editor);
  const QuaderApiVoidResult applied =
      editor.modeling().apply_material(QuaderMaterialReference::project(requested_material_id));
  ASSERT_TRUE(applied.ok()) << applied.error_message();
  ASSERT_TRUE(applied.changed());
  expect_primary_material(harness.project(), mesh.value().entity,
                          blue.material.id);
  expect_primary_material(harness.project(), curve.value().entity,
                          blue.material.id);
  EXPECT_FALSE(editor.query().entity(curve.value().entity)->visible);
  EXPECT_EQ(scene_selection_identity(editor), scene_selection_before);
  expect_same_selection_identity(exact_selection(harness.project()),
                                 selection_before);

  ASSERT_TRUE(editor.history().undo().ok());
  expect_primary_material(harness.project(), mesh.value().entity,
                          amber.material.id);
  expect_primary_material(harness.project(), curve.value().entity,
                          amber.material.id);
  expect_same_selection_identity(exact_selection(harness.project()),
                                 selection_before);
  ASSERT_TRUE(editor.history().redo().ok());
  expect_primary_material(harness.project(), mesh.value().entity,
                          blue.material.id);
  expect_primary_material(harness.project(), curve.value().entity,
                          blue.material.id);
  EXPECT_FALSE(editor.query().entity(curve.value().entity)->visible);
}

TEST(ProjectMaterialBehaviorTests,
     GroupApplyIncludesNestedHiddenMeshesAndRestoresExactSelection) {
  ProjectAssetId requested_material_id = project_asset_id(kDefaultProjectMaterialId);
  // Nested, disjoint and overlapping group selections restore their exact
  // scene-owned selection through explicit material assignment.
  for (int route = 0; route < 3; ++route) {
    SCOPED_TRACE(route);
    ProjectMaterialBehaviorHarness harness("group-apply-history");
    harness.create();
    const InstalledMaterial blue{
        .path = project_path("Materials/blue.qmat"),
        .material = project_material(kBlueMaterialId, "Blue", {0.1f, 0.2f, 1.0f}),
    };
    const std::array installed{blue};
    harness.install_and_reopen(installed);
    auto &editor = harness.editor();
    const auto unrelated = editor.modeling().create_box({.bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}}, .name = "Unrelated Mesh",
           .material = QuaderMaterialReference::project(requested_material_id)});
    ASSERT_TRUE(unrelated.ok()) << unrelated.error_message();
    const auto unrelated_before =
        *authored_document(harness.project(), unrelated.value().entity);
    auto seed = unrelated_before;
    for (std::size_t index = 0; index < seed.faces.size(); ++index) {
      auto &face = seed.faces[index];
      ASSERT_EQ(face.vertices.size(), 4u);
      face.material_slot = static_cast<std::uint32_t>(index % 3u + 1u);
      face.uvs = {{0.1f, 0.2f}, {2.1f, 0.2f}, {2.1f, 1.4f}, {0.1f, 1.4f}};
      face.texture_transform = {
          .scale = {-1.25f, 0.75f}, .shift_uv = {0.2f, -0.4f},
          .rotation_degrees = 23.0f};
    }
    const auto first = editor.modeling().create_mesh(QuaderPolygonDocumentInternalAccess::from_backend(seed),
          {.name = "First Group Mesh",
           .material = QuaderMaterialReference::project(requested_material_id)});
    const auto second = editor.modeling().create_mesh(QuaderPolygonDocumentInternalAccess::from_backend(seed),
          {.name = "Hidden Group Mesh",
           .material = QuaderMaterialReference::project(requested_material_id)});
    ASSERT_TRUE(first.ok()) << first.error_message();
    ASSERT_TRUE(second.ok()) << second.error_message();
    ASSERT_TRUE(editor.selection().select_objects({first.value(), second.value()}).ok());
    const auto inner = editor.scene().group_selection("Inner Material Group");
    ASSERT_TRUE(inner.ok()) << inner.error_message();
    const auto outer = editor.scene().group_selection("Outer Material Group");
    ASSERT_TRUE(outer.ok()) << outer.error_message();
    ASSERT_TRUE(editor.selection().clear().ok());
    const auto third = editor.modeling().create_mesh(QuaderPolygonDocumentInternalAccess::from_backend(seed),
          {.name = "Second Group Mesh",
           .material = QuaderMaterialReference::project(requested_material_id)});
    ASSERT_TRUE(third.ok()) << third.error_message();
    const auto other_group = editor.scene().group_selection("Second Material Group");
    ASSERT_TRUE(other_group.ok()) << other_group.error_message();
    const ProjectMaterialFixture fixture(harness.project());
    ASSERT_TRUE(fixture.set_visible(second.value().entity, false));
    const MaterialComponent initial_material{
        .material = MaterialReference::project(project_asset_id(kDefaultProjectMaterialId)),
        .material_slots = {{1u, MaterialReference::project(blue.material.id)}}};
    for (const auto object : {first.value(), second.value(), third.value()}) {
      ASSERT_TRUE(fixture.set_material(object.entity, initial_material));
    }

    ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Group).ok());
    const std::vector<EntityId> groups = route == 0
        ? std::vector<EntityId>{outer.value().id}
        : (route == 1
            ? std::vector<EntityId>{outer.value().id, other_group.value().id}
            : std::vector<EntityId>{inner.value().id, outer.value().id,
                                    other_group.value().id});
    const EntityId primary = groups.back();
    const auto selected = route == 2
        ? editor.selection().select_objects_for_inspection(groups, primary)
        : editor.selection().select_objects(groups, primary);
    ASSERT_TRUE(selected.ok()) << selected.error_message();
    ASSERT_TRUE(editor.query().entity(primary)->active);
    ASSERT_EQ(editor.selection().summary().selected_object_count, groups.size());
    requested_material_id = blue.material.id;
    const auto selection_before = exact_selection(harness.project());
    const auto scene_selection_before = scene_selection_identity(editor);
    const auto selection_revision = editor.selection().summary().selection_revision;
    const auto history_before = editor.query().history();
    const auto applied = editor.modeling().apply_material(
        QuaderMaterialReference::project(requested_material_id));
    ASSERT_TRUE(applied.ok()) << applied.error_message();
    ASSERT_TRUE(applied.changed());
    EXPECT_EQ(editor.query().history().undo_label, "Assign Material");
    EXPECT_EQ(scene_selection_identity(editor), scene_selection_before);
    EXPECT_EQ(editor.selection().summary().selection_revision, selection_revision);
    expect_same_selection_identity(exact_selection(harness.project()), selection_before);

    auto normalized = seed;
    for (auto &face : normalized.faces) {
      face.material_slot = 0u;
    }
    const auto expect_meshes = [&](bool changed) {
      for (const auto object : {first.value(), second.value(), third.value()}) {
        const bool target_changed = changed &&
            (route != 0 || object.entity != third.value().entity);
        expect_same_authored_document(
            *authored_document(harness.project(), object.entity),
            target_changed ? normalized : seed);
        EXPECT_EQ(*scene_material(harness.project(), object.entity),
                  target_changed
                      ? MaterialComponent{.material = MaterialReference::project(blue.material.id)}
                      : initial_material);
      }
      expect_same_authored_document(
          *authored_document(harness.project(), unrelated.value().entity), unrelated_before);
      expect_primary_material(harness.project(), unrelated.value().entity,
                              project_asset_id(kDefaultProjectMaterialId));
      EXPECT_FALSE(editor.query().entity(second.value().entity)->visible);
      EXPECT_EQ(editor.query().entity(first.value().entity)->parent, inner.value().id);
      EXPECT_EQ(editor.query().entity(inner.value().id)->parent, outer.value().id);
    };
    expect_meshes(true);

    ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Object).ok());
    ASSERT_TRUE(editor.history().undo().ok());
    EXPECT_EQ(editor.query().history().undo_label, history_before.undo_label);
    EXPECT_EQ(editor.selection().summary().mode, QuaderSelectionMode::Group);
    EXPECT_EQ(scene_selection_identity(editor), scene_selection_before);
    expect_meshes(false);

    requested_material_id = project_asset_id(kDefaultProjectMaterialId);
    ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Face).ok());
    ASSERT_TRUE(editor.history().redo().ok());
    EXPECT_EQ(editor.selection().summary().mode, QuaderSelectionMode::Group);
    EXPECT_EQ(scene_selection_identity(editor), scene_selection_before);
    expect_meshes(true);
  }
}

TEST(ProjectMaterialBehaviorTests,
     GroupApplyNoOpAndProtectedTargetsPreserveRedoAndRevisions) {
  ProjectAssetId requested_material_id = project_asset_id(kDefaultProjectMaterialId);
  ProjectMaterialBehaviorHarness harness("group-apply-protected");
  harness.create();
  const InstalledMaterial blue{
      .path = project_path("Materials/blue.qmat"),
      .material = project_material(kBlueMaterialId, "Blue", {0.1f, 0.2f, 1.0f}),
  };
  const std::array installed{blue};
  harness.install_and_reopen(installed);
  auto &editor = harness.editor();
  const auto first = editor.modeling().create_box({.bounds = {.min = {-3.0f, -1.0f, -1.0f}, .max = {-1.0f, 1.0f, 1.0f}}, .name = "First",
           .material = QuaderMaterialReference::project(requested_material_id)});
  ASSERT_TRUE(first.ok()) << first.error_message();
  const auto first_group = editor.scene().group_selection("First Group");
  ASSERT_TRUE(first_group.ok()) << first_group.error_message();
  ASSERT_TRUE(editor.selection().clear().ok());
  const auto second = editor.modeling().create_box({.bounds = {.min = {1.0f, -1.0f, -1.0f}, .max = {3.0f, 1.0f, 1.0f}}, .name = "Second",
           .material = QuaderMaterialReference::project(requested_material_id)});
  ASSERT_TRUE(second.ok()) << second.error_message();
  const auto second_group = editor.scene().group_selection("Second Group");
  ASSERT_TRUE(second_group.ok()) << second_group.error_message();
  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Group).ok());
  ASSERT_TRUE(editor.selection().select_objects(
      {first_group.value().id, second_group.value().id}, second_group.value().id).ok());
  requested_material_id = blue.material.id;
  const auto applied = editor.modeling().apply_material(QuaderMaterialReference::project(requested_material_id));
  ASSERT_TRUE(applied.ok()) << applied.error_message();
  ASSERT_TRUE(applied.changed());
  ASSERT_TRUE(editor.history().undo().ok());
  ASSERT_TRUE(editor.query().history().can_redo);
  requested_material_id = project_asset_id(kDefaultProjectMaterialId);
  const auto first_before = *authored_document(harness.project(), first.value().entity);
  const auto second_before = *authored_document(harness.project(), second.value().entity);
  const auto expect_unchanged_apply = [&](bool success) {
    const auto revisions = editor.query().revisions();
    const auto history = editor.query().history();
    const auto selection = exact_selection(harness.project());
    const auto scene_selection = scene_selection_identity(editor);
    const auto result = editor.modeling().apply_material(
        QuaderMaterialReference::project(requested_material_id));
    EXPECT_EQ(result.ok(), success) << result.error_message();
    EXPECT_FALSE(result.changed());
    expect_same_revisions(editor.query().revisions(), revisions);
    expect_same_history(editor.query().history(), history);
    expect_same_selection_identity(exact_selection(harness.project()), selection);
    EXPECT_EQ(scene_selection_identity(editor), scene_selection);
    expect_same_authored_document(
        *authored_document(harness.project(), first.value().entity), first_before);
    expect_same_authored_document(
        *authored_document(harness.project(), second.value().entity), second_before);
    for (const auto object : {first.value(), second.value()}) {
      expect_primary_material(harness.project(), object.entity,
                              project_asset_id(kDefaultProjectMaterialId));
    }
  };
  expect_unchanged_apply(true);

  requested_material_id = blue.material.id;
  const ProjectMaterialFixture fixture(harness.project());
  // Setting flags after selection exercises execution preflight, including a
  // protected root that the shared selected-root helper would otherwise skip.
  for (const EntityId target : {first_group.value().id, second.value().entity}) {
    ASSERT_TRUE(fixture.set_locked(target, true));
    expect_unchanged_apply(false);
    ASSERT_TRUE(fixture.set_locked(target, false));
    ASSERT_TRUE(fixture.set_selectable(target, false));
    expect_unchanged_apply(false);
    ASSERT_TRUE(fixture.set_selectable(target, true));
  }
  ASSERT_TRUE(editor.query().history().can_redo);


  const auto read_only = editor.scene().create_entity({
      .name = "Read-only Mesh", .kind = QuaderEntityKind::Mesh,
      .parent = first_group.value().id});
  ASSERT_TRUE(read_only.ok()) << read_only.error_message();
  ASSERT_TRUE(fixture.bind_shared_mesh(read_only.value().id,
      editor.query().entity(first.value().entity)->mesh));
  ASSERT_TRUE(editor.selection().select_objects(
      {first_group.value().id, second_group.value().id}, second_group.value().id).ok());
  expect_unchanged_apply(false);

  const auto empty = editor.scene().create_entity(
      {.name = "Empty Material Group", .kind = QuaderEntityKind::Empty});
  ASSERT_TRUE(empty.ok()) << empty.error_message();
  ASSERT_TRUE(editor.selection().select_object(empty.value().id).ok());
  expect_unchanged_apply(false);
  ASSERT_TRUE(editor.selection().clear().ok());
  expect_unchanged_apply(false);
}

TEST(ProjectMaterialBehaviorTests,
     GroupApplyPublicationFailureRollsBackAllDescendantsAtomically) {
  ProjectAssetId requested_material_id = project_asset_id(kDefaultProjectMaterialId);
  ProjectMaterialBehaviorHarness harness("group-apply-rollback");
  harness.create();
  auto &editor = harness.editor();
  auto document = texture_transfer_quad();
  document.faces.front().material_slot = 3u;
  const auto first = editor.modeling().create_mesh(QuaderPolygonDocumentInternalAccess::from_backend(document),
          {.name = "First",
           .material = QuaderMaterialReference::project(requested_material_id)});
  ASSERT_TRUE(first.ok()) << first.error_message();
  const auto first_group = editor.scene().group_selection("First Group");
  ASSERT_TRUE(first_group.ok()) << first_group.error_message();
  ASSERT_TRUE(editor.selection().clear().ok());
  const auto second = editor.modeling().create_mesh(QuaderPolygonDocumentInternalAccess::from_backend(document),
          {.name = "Second",
           .material = QuaderMaterialReference::project(requested_material_id)});
  ASSERT_TRUE(second.ok()) << second.error_message();
  const auto second_group = editor.scene().group_selection("Second Group");
  ASSERT_TRUE(second_group.ok()) << second_group.error_message();
  const auto embedded = editor.assets().add_material(QuaderMaterialArgs{.name = "Before"});
  ASSERT_TRUE(embedded.ok()) << embedded.error_message();
  const MaterialComponent material_before{
      .material = embedded.value().id,
      .material_slots = {{3u, MaterialReference(embedded.value().id)}}};
  const ProjectMaterialFixture fixture(harness.project());
  ASSERT_TRUE(fixture.set_material(first.value().entity, material_before));
  ASSERT_TRUE(fixture.set_material(second.value().entity, material_before));
  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Group).ok());
  ASSERT_TRUE(editor.selection().select_objects(
      {first_group.value().id, second_group.value().id}, second_group.value().id).ok());
  const auto revisions = editor.query().revisions();
  const auto history = editor.query().history();
  const auto selection = exact_selection(harness.project());
  const auto scene_selection = scene_selection_identity(editor);
  using ProjectionFailurePoint = ProjectMaterialFixture::ProjectionFailurePoint;
  constexpr std::array kFailures{
      ProjectionFailurePoint::AfterSdkMutation,
      ProjectionFailurePoint::CommittedSceneNotify,
      ProjectionFailurePoint::BeforeCommit};
  for (const auto failure : kFailures) {
    SCOPED_TRACE(static_cast<int>(failure));
    ASSERT_TRUE(fixture.set_projection_failure(failure));
    const auto result = editor.modeling().apply_material(QuaderMaterialReference::project(requested_material_id));
    EXPECT_FALSE(result.ok());
    EXPECT_FALSE(result.changed());
    ASSERT_TRUE(fixture.set_projection_failure(ProjectionFailurePoint::None));
    expect_same_revisions(editor.query().revisions(), revisions);
    expect_same_history(editor.query().history(), history);
    expect_same_selection_identity(exact_selection(harness.project()), selection);
    EXPECT_EQ(scene_selection_identity(editor), scene_selection);
    for (const auto object : {first.value(), second.value()}) {
      expect_same_authored_document(
          *authored_document(harness.project(), object.entity), document);
      EXPECT_EQ(*scene_material(harness.project(), object.entity), material_before);
    }
  }
  const auto applied = editor.modeling().apply_material(QuaderMaterialReference::project(requested_material_id));
  ASSERT_TRUE(applied.ok()) << applied.error_message();
  ASSERT_TRUE(applied.changed());
  for (const auto object : {first.value(), second.value()}) {
    expect_all_face_slots(harness.project(), object.entity, 0u);
    expect_primary_material(harness.project(), object.entity,
                            project_asset_id(kDefaultProjectMaterialId));
  }
  EXPECT_EQ(scene_selection_identity(editor), scene_selection);
}

TEST(ProjectMaterialBehaviorTests,
     GroupApplyRequiresExplicitInstanceMeshEditScope) {
  ProjectAssetId requested_material_id = project_asset_id(kDefaultProjectMaterialId);
  ProjectMaterialBehaviorHarness harness("group-apply-instance-scope");
  harness.create();
  const InstalledMaterial blue{
      .path = project_path("Materials/blue.qmat"),
      .material = project_material(kBlueMaterialId, "Blue", {0.1f, 0.2f, 1.0f}),
  };
  const std::array installed{blue};
  harness.install_and_reopen(installed);
  auto &editor = harness.editor();
  const auto member = editor.modeling().create_box({.bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}}, .name = "Instance Member",
           .material = QuaderMaterialReference::project(requested_material_id)});
  ASSERT_TRUE(member.ok()) << member.error_message();
  const auto instance = editor.modeling().create_instance_from_selection();
  ASSERT_TRUE(instance.ok()) << instance.error_message();
  const EntityId occurrence = instance.value().occurrence.root.id;
  ASSERT_TRUE(editor.selection().clear().ok());
  const auto ordinary = editor.modeling().create_box({.bounds = {.min = {3.0f, -1.0f, -1.0f}, .max = {5.0f, 1.0f, 1.0f}}, .name = "Ordinary Member",
           .material = QuaderMaterialReference::project(requested_material_id)});
  ASSERT_TRUE(ordinary.ok()) << ordinary.error_message();
  ASSERT_TRUE(editor.selection().select_objects(
      {ordinary.value().entity, occurrence}, ordinary.value().entity).ok());
  const auto group = editor.scene().group_selection("Mixed Instance Group");
  ASSERT_TRUE(group.ok()) << group.error_message();
  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Group).ok());
  ASSERT_TRUE(editor.selection().select_object(group.value().id).ok());
  requested_material_id = blue.material.id;
  const auto member_before = *authored_document(harness.project(), member.value().entity);
  const auto ordinary_before = *authored_document(harness.project(), ordinary.value().entity);
  const auto revisions = editor.query().revisions();
  const auto history = editor.query().history();
  const auto selection = scene_selection_identity(editor);
  const auto rejected = editor.modeling().apply_material(QuaderMaterialReference::project(requested_material_id));
  EXPECT_FALSE(rejected.ok());
  EXPECT_FALSE(rejected.changed());
  expect_same_revisions(editor.query().revisions(), revisions);
  expect_same_history(editor.query().history(), history);
  EXPECT_EQ(scene_selection_identity(editor), selection);
  expect_same_authored_document(
      *authored_document(harness.project(), member.value().entity), member_before);
  expect_same_authored_document(
      *authored_document(harness.project(), ordinary.value().entity), ordinary_before);
  for (const auto object : {member.value(), ordinary.value()}) {
    expect_primary_material(harness.project(), object.entity,
                            project_asset_id(kDefaultProjectMaterialId));
  }

  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Object).ok());
  ASSERT_TRUE(editor.selection().select_object(occurrence).ok());
  const auto entered = editor.modeling().enter_instance_edit(occurrence);
  ASSERT_TRUE(entered.ok()) << entered.error_message();
  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Group).ok());
  const auto scope_revisions = editor.query().revisions();
  const auto scope_history = editor.query().history();
  const auto scope_selection = scene_selection_identity(editor);
  const auto in_scope_group = editor.modeling().apply_material(QuaderMaterialReference::project(requested_material_id));
  EXPECT_FALSE(in_scope_group.ok());
  EXPECT_FALSE(in_scope_group.changed());
  expect_same_revisions(editor.query().revisions(), scope_revisions);
  expect_same_history(editor.query().history(), scope_history);
  EXPECT_EQ(scene_selection_identity(editor), scope_selection);

  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Object).ok());
  const auto selected = editor.selection().select_object(member.value());
  ASSERT_TRUE(selected.ok()) << selected.error_message();
  const auto applied = editor.modeling().apply_material(QuaderMaterialReference::project(requested_material_id));
  ASSERT_TRUE(applied.ok()) << applied.error_message();
  ASSERT_TRUE(applied.changed());
  EXPECT_TRUE(editor.modeling().can_exit_instance_edit());
  expect_primary_material(harness.project(), member.value().entity, blue.material.id);
  expect_primary_material(harness.project(), ordinary.value().entity,
                          project_asset_id(kDefaultProjectMaterialId));
  expect_same_authored_document(
      *authored_document(harness.project(), member.value().entity), member_before);
  ASSERT_TRUE(editor.modeling().exit_instance_edit().ok());
}

TEST(ProjectMaterialBehaviorTests,
     DirectHoverTargetsAssignFaceOrWholeMeshWithoutChangingSelection) {
  ProjectAssetId requested_material_id = project_asset_id(kDefaultProjectMaterialId);
  ProjectMaterialBehaviorHarness harness("direct-hover-material-targets");
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
  requested_material_id = amber.material.id;
  const QuaderApiResult<QuaderModelingObjectRef> target =
      harness.editor().modeling().create_box({.bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}}, .name = "Hover Target",
           .material = QuaderMaterialReference::project(requested_material_id)});
  const QuaderApiResult<QuaderModelingObjectRef> selected =
      harness.editor().modeling().create_box({.bounds = {.min = {3.0f, -1.0f, -1.0f}, .max = {5.0f, 1.0f, 1.0f}}, .name = "Unrelated Selection",
           .material = QuaderMaterialReference::project(requested_material_id)});
  ASSERT_TRUE(target.ok()) << target.error_message();
  ASSERT_TRUE(selected.ok()) << selected.error_message();
  const quader_poly::Document *document =
      authored_document(harness.project(), target.value().entity);
  ASSERT_NE(document, nullptr);
  ASSERT_GE(document->faces.size(), 2u);
  const QuaderElementId first_face = document->faces[0].id;
  const QuaderElementId second_face = document->faces[1].id;
  ASSERT_TRUE(harness.editor()
                  .selection()
                  .set_mode(QuaderSelectionMode::Object)
                  .ok());
  ASSERT_TRUE(
      harness.editor().selection().select_object(selected.value()).ok());
  const ModelingSelectionExport selection_before =
      exact_selection(harness.project());
  requested_material_id = blue.material.id;

  const QuaderApiVoidResult face_applied =
      harness.editor().modeling().apply_material_to_face(
          target.value().modeling_object_id, first_face, QuaderMaterialReference::project(requested_material_id));
  ASSERT_TRUE(face_applied.ok()) << face_applied.error_message();
  EXPECT_TRUE(face_applied.changed());
  EXPECT_EQ(harness.editor().query().history().undo_label,
            "Apply Material to Face");
  const auto first_material =
      harness.editor().modeling().project_material_for_face(
          target.value().modeling_object_id, first_face);
  const auto second_material =
      harness.editor().modeling().project_material_for_face(
          target.value().modeling_object_id, second_face);
  ASSERT_TRUE(first_material.ok()) << first_material.error_message();
  ASSERT_TRUE(second_material.ok()) << second_material.error_message();
  EXPECT_EQ(first_material.value().id, blue.material.id);
  EXPECT_EQ(second_material.value().id, amber.material.id);
  expect_same_selection_identity(exact_selection(harness.project()),
                                 selection_before);

  ASSERT_TRUE(harness.editor().history().undo().ok());
  ASSERT_TRUE(harness.editor().history().redo().ok());
  ASSERT_TRUE(harness.editor().history().undo().ok());
  const QuaderApiVoidResult mesh_applied =
      harness.editor().modeling().apply_material_to_mesh(
          target.value().modeling_object_id, first_face, QuaderMaterialReference::project(requested_material_id));
  ASSERT_TRUE(mesh_applied.ok()) << mesh_applied.error_message();
  EXPECT_TRUE(mesh_applied.changed());
  EXPECT_EQ(harness.editor().query().history().undo_label,
            "Apply Material to Mesh");
  expect_primary_material(harness.project(), target.value().entity,
                          blue.material.id);
  expect_all_face_slots(harness.project(), target.value().entity, 0u);
  expect_primary_material(harness.project(), selected.value().entity,
                          amber.material.id);
  expect_same_selection_identity(exact_selection(harness.project()),
                                 selection_before);
  ASSERT_TRUE(harness.editor().history().undo().ok());
  expect_primary_material(harness.project(), target.value().entity,
                          amber.material.id);
  expect_same_selection_identity(exact_selection(harness.project()),
                                 selection_before);
}

TEST(ProjectMaterialBehaviorTests,
     SelectedFaceMaterialCopiesToFaceOrMeshWithoutChangingMappingOrSelection) {
  for (const bool embedded : {false, true}) {
    SCOPED_TRACE(embedded ? "embedded" : "project");
    ProjectMaterialBehaviorHarness harness(
        embedded ? "selected-embedded-material-copy" : "selected-project-material-copy");
    harness.create();
    const InstalledMaterial amber{
        .path = project_path("Materials/amber.qmat"),
        .material = project_material(kAmberMaterialId, "Amber", {1.0f, 0.5f, 0.1f}),
    };
    harness.install_and_reopen(std::array{amber});
    auto &editor = harness.editor();
    const auto embedded_asset = editor.assets().add_material(
        QuaderMaterialArgs{.name = "Embedded Copy Source"});
    ASSERT_TRUE(embedded_asset.ok());
    const auto material = embedded
                              ? QuaderMaterialReference::embedded(embedded_asset.value().id)
                              : QuaderMaterialReference::project(amber.material.id);
    const auto expected_material = embedded
                                       ? MaterialReference(embedded_asset.value().id)
                                       : MaterialReference::project(amber.material.id);
    const auto source = editor.modeling().create_box(
        {.bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
         .name = "Selected Source", .material = material});
    const auto target = editor.modeling().create_box(
        {.min = {3.0f, -1.0f, -1.0f}, .max = {5.0f, 1.0f, 1.0f}}, "Clicked Target");
    ASSERT_TRUE(source.ok());
    ASSERT_TRUE(target.ok());
    const auto source_face = static_cast<QuaderElementId>(
        authored_document(harness.project(), source.value().entity)->faces.front().id);
    const auto target_face = static_cast<QuaderElementId>(
        authored_document(harness.project(), target.value().entity)->faces.front().id);
    ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Face).ok());
    ASSERT_TRUE(editor.selection().select_face(target.value(), target_face).ok());
    ASSERT_TRUE(edit_selected_face_texture(editor, QuaderFaceTextureField::ShiftX, 19).ok());
    ASSERT_TRUE(edit_selected_face_texture(editor, QuaderFaceTextureField::ScaleY, -0.75).ok());
    ASSERT_TRUE(editor.selection().select_face(source.value(), source_face).ok());
    const auto source_before = *authored_document(harness.project(), source.value().entity);
    const auto target_before = *authored_document(harness.project(), target.value().entity);
    const auto material_before = *scene_material(harness.project(), target.value().entity);
    const auto selection_before = exact_selection(harness.project());

    for (const bool whole_mesh : {false, true}) {
      SCOPED_TRACE(whole_mesh ? "whole mesh" : "single face");
      const auto apply = [&] {
        return whole_mesh
                   ? editor.modeling().apply_selected_face_material_to_mesh(
                         target.value().modeling_object_id, target_face)
                   : editor.modeling().apply_selected_face_material_to_face(
                         target.value().modeling_object_id, target_face);
      };
      const auto applied = apply();
      ASSERT_TRUE(applied.ok()) << applied.error_message();
      ASSERT_TRUE(applied.changed());
      EXPECT_EQ(editor.query().history().undo_label,
                whole_mesh ? "Apply Material to Mesh" : "Apply Material to Face");
      const auto after = *authored_document(harness.project(), target.value().entity);
      const auto after_material = *scene_material(harness.project(), target.value().entity);
      auto expected_document = target_before;
      for (std::size_t index = 0; index < after.faces.size(); ++index) {
        const auto &face = after.faces[index];
        const auto expected_reference = whole_mesh || face.id == target_face
                                            ? expected_material
                                            : material_before.material_for_slot(
                                                  target_before.faces[index].material_slot);
        EXPECT_EQ(after_material.material_for_slot(face.material_slot), expected_reference);
        expected_document.faces[index].material_slot = face.material_slot;
      }
      expect_same_authored_document(after, expected_document);
      expect_same_authored_document(
          *authored_document(harness.project(), source.value().entity), source_before);
      expect_same_selection_identity(exact_selection(harness.project()), selection_before);
      const auto applied_history = editor.query().history();
      const auto applied_revisions = editor.query().revisions();
      const auto repeated = apply();
      ASSERT_TRUE(repeated.ok()) << repeated.error_message();
      EXPECT_FALSE(repeated.changed());
      expect_same_history(editor.query().history(), applied_history);
      expect_same_revisions(editor.query().revisions(), applied_revisions);

      ASSERT_TRUE(editor.history().undo().ok());
      expect_same_authored_document(
          *authored_document(harness.project(), target.value().entity), target_before);
      EXPECT_EQ(*scene_material(harness.project(), target.value().entity), material_before);
      expect_same_selection_identity(exact_selection(harness.project()), selection_before);
      ASSERT_TRUE(editor.history().redo().ok());
      expect_same_authored_document(
          *authored_document(harness.project(), target.value().entity), after);
      EXPECT_EQ(*scene_material(harness.project(), target.value().entity), after_material);
      ASSERT_TRUE(editor.history().undo().ok());
    }
  }
}

TEST(ProjectMaterialBehaviorTests,
     ClickedFaceMaterialCopiesToSelectedFacesAcrossMeshesInOneUndo) {
  ProjectMaterialBehaviorHarness harness("clicked-material-to-selected-faces");
  harness.create();
  const InstalledMaterial amber{
      .path = project_path("Materials/amber.qmat"),
      .material = project_material(kAmberMaterialId, "Amber", {1.0f, 0.5f, 0.1f}),
  };
  harness.install_and_reopen(std::array{amber});
  auto &editor = harness.editor();
  const auto source = editor.modeling().create_box(
      {.bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
       .name = "Clicked Source", .material = QuaderMaterialReference::project(amber.material.id)});
  const auto first = editor.modeling().create_box(
      {.min = {3.0f, -1.0f, -1.0f}, .max = {5.0f, 1.0f, 1.0f}}, "First Selected Mesh");
  const auto second = editor.modeling().create_box(
      {.min = {6.0f, -1.0f, -1.0f}, .max = {8.0f, 1.0f, 1.0f}}, "Second Selected Mesh");
  ASSERT_TRUE(source.ok());
  ASSERT_TRUE(first.ok());
  ASSERT_TRUE(second.ok());
  const auto source_before = *authored_document(harness.project(), source.value().entity);
  const auto first_initial = *authored_document(harness.project(), first.value().entity);
  const auto second_initial = *authored_document(harness.project(), second.value().entity);
  const auto source_face = static_cast<QuaderElementId>(source_before.faces.front().id);
  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(
      first.value(), static_cast<QuaderElementId>(first_initial.faces[0].id)).ok());
  const auto texture_edit =
      edit_selected_face_texture(editor, QuaderFaceTextureField::ShiftY, -23);
  ASSERT_TRUE(texture_edit.ok()) << texture_edit.error_message();
  ASSERT_TRUE(editor.selection().select_faces(
      first.value(), {static_cast<QuaderElementId>(first_initial.faces[0].id),
                      static_cast<QuaderElementId>(first_initial.faces[1].id)}).ok());
  ASSERT_TRUE(editor.selection().select_face(
      second.value(), static_cast<QuaderElementId>(second_initial.faces[0].id),
      QuaderSelectionEdit::Add).ok());
  const std::array objects{first.value(), second.value()};
  const std::array before{
      *authored_document(harness.project(), first.value().entity),
      *authored_document(harness.project(), second.value().entity)};
  const std::array before_material{
      *scene_material(harness.project(), first.value().entity),
      *scene_material(harness.project(), second.value().entity)};
  const auto selection_before = exact_selection(harness.project());
  const auto applied = editor.modeling().apply_face_material_to_selected_faces(
      source.value().modeling_object_id, source_face);
  ASSERT_TRUE(applied.ok()) << applied.error_message();
  ASSERT_TRUE(applied.changed());
  for (std::size_t object_index = 0; object_index < objects.size(); ++object_index) {
    const auto &object = objects[object_index];
    const auto &after = *authored_document(harness.project(), object.entity);
    const auto &material = *scene_material(harness.project(), object.entity);
    auto expected_document = before[object_index];
    for (std::size_t face_index = 0; face_index < after.faces.size(); ++face_index) {
      const auto &face = after.faces[face_index];
      const bool selected = face_index == 0u || (object_index == 0u && face_index == 1u);
      EXPECT_EQ(material.material_for_slot(face.material_slot),
                selected ? MaterialReference::project(amber.material.id)
                         : before_material[object_index].material_for_slot(
                               before[object_index].faces[face_index].material_slot));
      expected_document.faces[face_index].material_slot = face.material_slot;
    }
    expect_same_authored_document(after, expected_document);
  }
  expect_same_authored_document(
      *authored_document(harness.project(), source.value().entity), source_before);
  expect_same_selection_identity(exact_selection(harness.project()), selection_before);
  const std::array after{
      *authored_document(harness.project(), first.value().entity),
      *authored_document(harness.project(), second.value().entity)};
  const auto applied_history = editor.query().history();
  EXPECT_FALSE(editor.modeling().apply_face_material_to_selected_faces(
      source.value().modeling_object_id, source_face).changed());
  expect_same_history(editor.query().history(), applied_history);
  ASSERT_TRUE(editor.history().undo().ok());
  for (std::size_t index = 0; index < objects.size(); ++index) {
    expect_same_authored_document(
        *authored_document(harness.project(), objects[index].entity), before[index]);
    EXPECT_EQ(*scene_material(harness.project(), objects[index].entity), before_material[index]);
  }
  expect_same_selection_identity(exact_selection(harness.project()), selection_before);
  ASSERT_TRUE(editor.history().redo().ok());
  for (std::size_t index = 0; index < objects.size(); ++index) {
    expect_same_authored_document(
        *authored_document(harness.project(), objects[index].entity), after[index]);
  }
}

TEST(ProjectMaterialBehaviorTests,
     FaceMaterialCopyRejectsInvalidSourcesAndRollsBackFailedPublication) {
  ProjectMaterialBehaviorHarness harness("face-material-copy-validation");
  harness.create();
  auto &editor = harness.editor();
  const auto embedded = editor.assets().add_material(QuaderMaterialArgs{.name = "Copy Source"});
  ASSERT_TRUE(embedded.ok());
  const auto source = editor.modeling().create_mesh(
      QuaderPolygonDocumentInternalAccess::from_backend(texture_transfer_quad()),
      {.material = QuaderMaterialReference::embedded(embedded.value().id)});
  const auto target = editor.modeling().create_mesh(
      QuaderPolygonDocumentInternalAccess::from_backend(texture_transfer_quad(true)));
  ASSERT_TRUE(source.ok());
  ASSERT_TRUE(target.ok());
  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(source.value(), 1).ok());
  const ProjectMaterialFixture fixture(harness.project());
  const auto source_material = *scene_material(harness.project(), source.value().entity);
  auto target_material = *scene_material(harness.project(), target.value().entity);
  target_material.material_slots.emplace(3u, target_material.material);
  ASSERT_TRUE(fixture.set_material(target.value().entity, target_material));
  // A nonzero face slot makes whole-mesh normalization cross the SDK mutation
  // boundary exercised by the publication failure injection below.
  ASSERT_TRUE(editor.modeling().assign_material_slot_to_face(target.value(), 1, 3u).ok());
  const auto expect_rejected = [&](auto action, QuaderModelingObjectId object_id,
                                   QuaderElementId face_id) {
    const auto history = editor.query().history();
    const auto revisions = editor.query().revisions();
    const auto selection = exact_selection(harness.project());
    const auto before = *authored_document(harness.project(), target.value().entity);
    const auto material = *scene_material(harness.project(), target.value().entity);
    const auto result = (editor.modeling().*action)(object_id, face_id);
    EXPECT_FALSE(result.ok());
    EXPECT_FALSE(result.changed());
    expect_same_history(editor.query().history(), history);
    expect_same_revisions(editor.query().revisions(), revisions);
    expect_same_selection_identity(exact_selection(harness.project()), selection);
    expect_same_authored_document(
        *authored_document(harness.project(), target.value().entity), before);
    EXPECT_EQ(*scene_material(harness.project(), target.value().entity), material);
  };
  for (const auto action : {&QuaderModelingApi::apply_selected_face_material_to_face,
                            &QuaderModelingApi::apply_selected_face_material_to_mesh}) {
    SCOPED_TRACE(action == &QuaderModelingApi::apply_selected_face_material_to_face
                     ? "copy to face" : "copy to mesh");
    ASSERT_TRUE(editor.selection().clear().ok());
    expect_rejected(action, target.value().modeling_object_id, 1);
    ASSERT_TRUE(editor.selection().select_face(source.value(), 1).ok());
    ASSERT_TRUE(editor.selection().select_face(target.value(), 1, QuaderSelectionEdit::Add).ok());
    expect_rejected(action, target.value().modeling_object_id, 1);
    ASSERT_TRUE(editor.selection().select_face(source.value(), 1).ok());
    expect_rejected(action, kInvalidQuaderModelingObjectId, 1);
    expect_rejected(action, target.value().modeling_object_id, kInvalidQuaderElementId);
    ASSERT_TRUE(fixture.clear_material(source.value().entity));
    expect_rejected(action, target.value().modeling_object_id, 1);
    ASSERT_TRUE(fixture.set_material(source.value().entity, source_material));
    ASSERT_TRUE(editor.modeling().begin_transform_preview().ok());
    expect_rejected(action, target.value().modeling_object_id, 1);
    ASSERT_TRUE(editor.modeling().cancel_transform_preview().ok());
    ASSERT_TRUE(fixture.set_projection_failure(
        ProjectMaterialFixture::ProjectionFailurePoint::AfterSdkMutation));
    expect_rejected(action, target.value().modeling_object_id, 1);
    ASSERT_TRUE(fixture.set_projection_failure(
        ProjectMaterialFixture::ProjectionFailurePoint::None));
  }
  const auto action = &QuaderModelingApi::apply_face_material_to_selected_faces;
  ASSERT_TRUE(editor.selection().clear().ok());
  expect_rejected(action, source.value().modeling_object_id, 1);
  ASSERT_TRUE(editor.selection().select_face(target.value(), 1).ok());
  expect_rejected(action, source.value().modeling_object_id, kInvalidQuaderElementId);
  ASSERT_TRUE(fixture.clear_material(source.value().entity));
  expect_rejected(action, source.value().modeling_object_id, 1);
  ASSERT_TRUE(fixture.set_material(source.value().entity, source_material));
  ASSERT_TRUE(fixture.set_projection_failure(
      ProjectMaterialFixture::ProjectionFailurePoint::AfterSdkMutation));
  expect_rejected(action, source.value().modeling_object_id, 1);
  ASSERT_TRUE(fixture.set_projection_failure(
      ProjectMaterialFixture::ProjectionFailurePoint::None));
}

TEST(ProjectMaterialBehaviorTests,
     FaceTextureTransferContinuesMaterialAndAttributesWithExactHistory) {
  ProjectAssetId requested_material_id = project_asset_id(kDefaultProjectMaterialId);
  ProjectMaterialBehaviorHarness harness("face-texture-transfer");
  harness.create();
  const InstalledMaterial amber{
      .path = project_path("Materials/amber.qmat"),
      .material = project_material(kAmberMaterialId, "Amber", {1.0f, 0.5f, 0.1f}),
  };
  const InstalledMaterial blue{
      .path = project_path("Materials/blue.qmat"),
      .material = project_material(kBlueMaterialId, "Blue", {0.1f, 0.2f, 1.0f}),
  };
  const std::array installed{amber, blue};
  harness.install_and_reopen(installed);
  auto &editor = harness.editor();
  requested_material_id = blue.material.id;
  const auto source = editor.modeling().create_mesh(QuaderPolygonDocumentInternalAccess::from_backend(texture_transfer_quad()),
          {.name = "Texture Source",
           .material = QuaderMaterialReference::project(requested_material_id)});
  auto target_topology = texture_transfer_quad(true);
  const std::array extra_vertices{
      quader_poly::add_vertex(target_topology, {5.0f, 0.0f, 0.0f}),
      quader_poly::add_vertex(target_topology, {6.0f, 0.0f, 0.0f}),
      quader_poly::add_vertex(target_topology, {5.0f, 1.0f, 0.0f}),
  };
  ASSERT_NE(quader_poly::add_face(target_topology, extra_vertices),
            quader_poly::kInvalidElementId);
  const auto target = editor.modeling().create_mesh(QuaderPolygonDocumentInternalAccess::from_backend(target_topology),
          {.name = "Unselected Texture Target",
           .material = QuaderMaterialReference::project(requested_material_id)});
  ASSERT_TRUE(source.ok()) << source.error_message();
  ASSERT_TRUE(target.ok()) << target.error_message();
  const QuaderElementId source_face = 1;
  const QuaderElementId target_face = 1;
  requested_material_id = amber.material.id;
  ASSERT_TRUE(editor.modeling().apply_material_to_face(
      source.value().modeling_object_id, source_face, QuaderMaterialReference::project(requested_material_id)).ok());
  requested_material_id = blue.material.id;
  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(source.value(), source_face).ok());
  ASSERT_TRUE(edit_selected_face_texture(editor, QuaderFaceTextureField::ScaleX, 1.25).ok());
  ASSERT_TRUE(edit_selected_face_texture(editor, QuaderFaceTextureField::ScaleY, -0.75).ok());
  ASSERT_TRUE(edit_selected_face_texture(editor, QuaderFaceTextureField::ShiftX, 13).ok());
  ASSERT_TRUE(edit_selected_face_texture(editor, QuaderFaceTextureField::ShiftY, -7).ok());
  ASSERT_TRUE(edit_selected_face_texture(editor, QuaderFaceTextureField::Rotate, 31).ok());
  const auto source_before = *authored_document(harness.project(), source.value().entity);
  const auto target_before = *authored_document(harness.project(), target.value().entity);
  const auto material_before = *scene_material(harness.project(), target.value().entity);
  const auto selection_before = exact_selection(harness.project());
  const auto history_before = editor.query().history();
  const auto source_uv = effective_face_corner_uvs(
      harness.project(), source.value().entity, source_face);
  ASSERT_EQ(source_uv.size(), 4u);

  const auto applied = editor.modeling().transfer_face_texture_to_face(
      target.value().modeling_object_id, target_face);
  ASSERT_TRUE(applied.ok()) << applied.error_message();
  ASSERT_TRUE(applied.changed());
  EXPECT_EQ(editor.query().history().undo_label, "Transfer Face Texture");
  const auto target_after = *authored_document(harness.project(), target.value().entity);
  const auto material_after = *scene_material(harness.project(), target.value().entity);
  const auto &transferred = target_after.faces.front();
  EXPECT_EQ(transferred.texture_transform, source_before.faces.front().texture_transform);
  EXPECT_EQ(material_after.material_for_slot(transferred.material_slot),
            MaterialReference::project(amber.material.id));
  EXPECT_EQ(material_after.material, material_before.material);
  auto expected_target = target_before;
  expected_target.faces.front().uvs = transferred.uvs;
  expected_target.faces.front().texture_transform = transferred.texture_transform;
  expected_target.faces.front().material_slot = transferred.material_slot;
  expect_same_authored_document(target_after, expected_target);
  expect_same_authored_document(
      *authored_document(harness.project(), source.value().entity), source_before);
  expect_same_selection_identity(exact_selection(harness.project()), selection_before);
  EXPECT_EQ(editor.query().revisions().modeling_selection_revision,
            selection_before.summary.selection_revision);
  const auto target_uv = effective_face_corner_uvs(
      harness.project(), target.value().entity, target_face);
  ASSERT_EQ(target_uv.size(), 4u);
  const std::array<QVec2, 4> unfolded_base{{
      {1.0f, 0.0f}, {2.0f, 0.0f}, {2.0f, 1.0f}, {1.0f, 1.0f}}};
  for (std::size_t corner = 0; corner < target_uv.size(); ++corner) {
    // Numeric edits can author the effective chart independently of its scalar
    // settings. Continue the measured source basis by the known unfolded width.
    const QVec2 expected = source_uv[0] +
        (source_uv[1] - source_uv[0]) * unfolded_base[corner].x +
        (source_uv[3] - source_uv[0]) * unfolded_base[corner].y;
    EXPECT_NEAR(target_uv[corner].x, expected.x, 2.0e-5f);
    EXPECT_NEAR(target_uv[corner].y, expected.y, 2.0e-5f);
  }
  EXPECT_NEAR(target_uv.front().x, source_uv[1].x, 2.0e-5f);
  EXPECT_NEAR(target_uv.back().y, source_uv[2].y, 2.0e-5f);

  ASSERT_TRUE(editor.history().undo().ok());
  expect_same_authored_document(
      *authored_document(harness.project(), target.value().entity), target_before);
  EXPECT_EQ(*scene_material(harness.project(), target.value().entity), material_before);
  EXPECT_EQ(editor.query().history().undo_label, history_before.undo_label);
  expect_same_selection_identity(exact_selection(harness.project()), selection_before);
  requested_material_id = project_asset_id(kDefaultProjectMaterialId);
  ASSERT_TRUE(editor.history().redo().ok());
  expect_same_authored_document(
      *authored_document(harness.project(), target.value().entity), target_after);
  EXPECT_EQ(*scene_material(harness.project(), target.value().entity), material_after);
  expect_same_selection_identity(exact_selection(harness.project()), selection_before);

  const auto transient = editor.modeling().create_box({.bounds = {.min = {8.0f, 0.0f, 0.0f}, .max = {9.0f, 1.0f, 1.0f}}, .name = "Redo Guard",
           .material = QuaderMaterialReference::project(requested_material_id)});
  ASSERT_TRUE(transient.ok());
  ASSERT_TRUE(editor.history().undo().ok());
  const auto history_before_no_op = editor.query().history();
  const auto revisions_before_no_op = editor.query().revisions();
  ASSERT_TRUE(history_before_no_op.can_redo);
  for (const auto &face : std::array{source.value(), target.value()}) {
    const auto unchanged = editor.modeling().transfer_face_texture_to_face(
        face.modeling_object_id, source_face);
    ASSERT_TRUE(unchanged.ok()) << unchanged.error_message();
    EXPECT_FALSE(unchanged.changed());
    expect_same_history(editor.query().history(), history_before_no_op);
    expect_same_revisions(editor.query().revisions(), revisions_before_no_op);
  }
  ASSERT_TRUE(editor.history().redo().ok());
  EXPECT_TRUE(editor.query().entity(transient.value().entity).has_value());
}

TEST(ProjectMaterialBehaviorTests,
     FaceTextureTransferMatchesHammerObtuseWrapWithExactHistory) {
  ProjectMaterialBehaviorHarness harness("face-transfer-hammer-obtuse");
  harness.create();
  auto &editor = harness.editor();
  const auto source = editor.modeling().create_mesh(
      QuaderPolygonDocumentInternalAccess::from_backend(texture_transfer_quad()));
  auto target_document = texture_transfer_quad(true);
  constexpr float kRootThree = 1.7320508075688772F;
  target_document.vertices[1].position = {1.0F, 0.0F, -kRootThree};
  target_document.vertices[2].position = {1.0F, 2.0F, -kRootThree};
  const auto target = editor.modeling().create_mesh(
      QuaderPolygonDocumentInternalAccess::from_backend(target_document));
  ASSERT_TRUE(source.ok()) << source.error_message();
  ASSERT_TRUE(target.ok()) << target.error_message();
  const auto material = editor.assets().add_material(
      QuaderMaterialArgs{.name = "Wrapped Material"});
  ASSERT_TRUE(material.ok());
  const ProjectMaterialFixture fixture(harness.project());
  ASSERT_TRUE(fixture.set_material(source.value().entity,
      MaterialComponent{.material = material.value().id}));
  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(source.value(), 1).ok());
  ASSERT_TRUE(edit_selected_face_texture(editor, QuaderFaceTextureField::ScaleX, -1.25).ok());
  ASSERT_TRUE(edit_selected_face_texture(editor, QuaderFaceTextureField::Rotate, 31.0).ok());
  ASSERT_TRUE(edit_selected_face_texture(editor, QuaderFaceTextureField::ShiftY, 7.0).ok());
  const auto source_before = *authored_document(harness.project(), source.value().entity);
  const auto source_uv = effective_face_corner_uvs(harness.project(), source.value().entity, 1);
  ASSERT_EQ(source_uv.size(), 4U);
  const auto before = *authored_document(harness.project(), target.value().entity);
  const auto material_before = *scene_material(harness.project(), target.value().entity);
  const auto selection_before = exact_selection(harness.project());
  const auto history_before = editor.query().history();

  const auto applied = editor.modeling().transfer_face_texture_to_face(
      target.value().modeling_object_id, 1);
  ASSERT_TRUE(applied.ok()) << applied.error_message();
  ASSERT_TRUE(applied.changed());
  const auto after = *authored_document(harness.project(), target.value().entity);
  const auto material_after = *scene_material(harness.project(), target.value().entity);
  EXPECT_EQ(material_after.material_for_slot(after.faces.front().material_slot),
            material.value().id);
  auto expected_document = before;
  expected_document.faces.front() = after.faces.front();
  expected_document.faces.front().vertices = before.faces.front().vertices;
  expect_same_authored_document(after, expected_document);
  expect_same_authored_document(
      *authored_document(harness.project(), source.value().entity), source_before);
  expect_same_selection_identity(exact_selection(harness.project()), selection_before);
  EXPECT_EQ(after.faces.front().texture_transform, source_before.faces.front().texture_transform);
  const auto uv = effective_face_corner_uvs(harness.project(), target.value().entity, 1);
  ASSERT_EQ(uv.size(), 4U);
  // Hammer folds the 120-degree normal angle to 60 degrees. The target's
  // transverse UV advance is one half, while both seam endpoints stay fixed.
  constexpr std::array<QVec2, 4> kExpectedBase{{
      {1.0F, 0.0F}, {1.5F, 0.0F}, {1.5F, 1.0F}, {1.0F, 1.0F}}};
  for (std::size_t corner = 0; corner < uv.size(); ++corner) {
    const QVec2 expected = source_uv[0] +
        (source_uv[1] - source_uv[0]) * kExpectedBase[corner].x +
        (source_uv[3] - source_uv[0]) * kExpectedBase[corner].y;
    EXPECT_NEAR(uv[corner].x, expected.x, 2.0e-5F);
    EXPECT_NEAR(uv[corner].y, expected.y, 2.0e-5F);
  }
  const auto history_after = editor.query().history();
  const auto repeated = editor.modeling().transfer_face_texture_to_face(
      target.value().modeling_object_id, 1);
  ASSERT_TRUE(repeated.ok()) << repeated.error_message();
  EXPECT_FALSE(repeated.changed());
  expect_same_history(editor.query().history(), history_after);
  ASSERT_TRUE(editor.history().undo().ok());
  expect_same_authored_document(
      *authored_document(harness.project(), target.value().entity), before);
  EXPECT_EQ(*scene_material(harness.project(), target.value().entity), material_before);
  EXPECT_EQ(editor.query().history().undo_label, history_before.undo_label);
  ASSERT_TRUE(editor.history().redo().ok());
  expect_same_authored_document(
      *authored_document(harness.project(), target.value().entity), after);
  EXPECT_EQ(*scene_material(harness.project(), target.value().entity), material_after);
  expect_same_selection_identity(exact_selection(harness.project()), selection_before);
}

TEST(ProjectMaterialBehaviorTests,
     FaceTextureTransferCommitsMaterialOnlyAndHonorsSlotZeroOverride) {
  ProjectMaterialBehaviorHarness harness("face-transfer-material-only");
  harness.create();
  auto &editor = harness.editor();
  const auto embedded = editor.assets().add_material(
      QuaderMaterialArgs{.name = "Source Material"});
  ASSERT_TRUE(embedded.ok());
  const auto source = editor.modeling().create_mesh(
      QuaderPolygonDocumentInternalAccess::from_backend(texture_transfer_quad()));
  auto target_document = texture_transfer_quad();
  target_document.faces.front().material_slot = 1u;
  const auto target = editor.modeling().create_mesh(
      QuaderPolygonDocumentInternalAccess::from_backend(target_document));
  ASSERT_TRUE(source.ok());
  ASSERT_TRUE(target.ok());
  const ProjectMaterialFixture fixture(harness.project());
  const MaterialReference source_reference = embedded.value().id;
  ASSERT_TRUE(fixture.set_material(
      source.value().entity, MaterialComponent{.material = source_reference}));
  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(source.value(), 1).ok());
  const auto before = *authored_document(harness.project(), target.value().entity);
  ASSERT_EQ(before.faces.front().material_slot, 1u);
  const auto material_before = *scene_material(harness.project(), target.value().entity);
  const auto content_before = editor.query().revisions().modeling_content_revision;
  const auto applied = editor.modeling().transfer_face_texture_to_face(
      target.value().modeling_object_id, 1);
  ASSERT_TRUE(applied.ok()) << applied.error_message();
  ASSERT_TRUE(applied.changed());
  EXPECT_EQ(editor.query().revisions().modeling_content_revision, content_before);
  expect_same_authored_document(
      *authored_document(harness.project(), target.value().entity), before);
  EXPECT_EQ(scene_material(harness.project(), target.value().entity)->material_for_slot(1u),
            source_reference);
  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_EQ(*scene_material(harness.project(), target.value().entity), material_before);
  ASSERT_TRUE(editor.history().redo().ok());
  EXPECT_EQ(scene_material(harness.project(), target.value().entity)->material_for_slot(1u),
            source_reference);

  MaterialComponent overridden{.material = source_reference};
  overridden.material_slots.emplace(0u, material_before.material);
  ASSERT_TRUE(fixture.set_material(target.value().entity, overridden));
  ASSERT_TRUE(editor.modeling().assign_material_slot_to_face(target.value(), 1, 0u).ok());
  const auto overridden_before = *authored_document(harness.project(), target.value().entity);
  const auto assigned = editor.modeling().transfer_face_texture_to_face(
      target.value().modeling_object_id, 1);
  ASSERT_TRUE(assigned.ok()) << assigned.error_message();
  ASSERT_TRUE(assigned.changed());
  const auto *after = authored_document(harness.project(), target.value().entity);
  const auto *material = scene_material(harness.project(), target.value().entity);
  EXPECT_NE(after->faces.front().material_slot, 0u);
  EXPECT_EQ(material->material_for_slot(after->faces.front().material_slot), source_reference);
  EXPECT_EQ(material->material_for_slot(0u), material_before.material);
  ASSERT_TRUE(editor.history().undo().ok());
  expect_same_authored_document(
      *authored_document(harness.project(), target.value().entity), overridden_before);
  EXPECT_EQ(*scene_material(harness.project(), target.value().entity), overridden);
}

TEST(ProjectMaterialBehaviorTests,
     FaceTextureTransferRejectsInvalidSourcesTargetsAndPreviewsWithoutHistory) {
  ProjectMaterialBehaviorHarness harness("face-transfer-validation");
  harness.create();
  auto &editor = harness.editor();
  const auto source = editor.modeling().create_mesh(
      QuaderPolygonDocumentInternalAccess::from_backend(texture_transfer_quad()));
  const auto target = editor.modeling().create_mesh(
      QuaderPolygonDocumentInternalAccess::from_backend(texture_transfer_quad(true)));
  ASSERT_TRUE(source.ok());
  ASSERT_TRUE(target.ok());
  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(source.value(), 1).ok());
  const auto transient = editor.modeling().create_box(
      {.min = {8.0f, 0.0f, 0.0f}, .max = {9.0f, 1.0f, 1.0f}}, "Redo Guard");
  ASSERT_TRUE(transient.ok());
  ASSERT_TRUE(editor.history().undo().ok());
  ASSERT_TRUE(editor.query().history().can_redo);
  const auto source_before = *authored_document(harness.project(), source.value().entity);
  const auto target_before = *authored_document(harness.project(), target.value().entity);
  const auto material_before = *scene_material(harness.project(), target.value().entity);
  const auto expect_rejected = [&](QuaderModelingObjectId object_id, QuaderElementId face) {
    const auto revisions = editor.query().revisions();
    const auto history = editor.query().history();
    const auto selection = exact_selection(harness.project());
    const auto result = editor.modeling().transfer_face_texture_to_face(object_id, face);
    EXPECT_FALSE(result.ok());
    EXPECT_FALSE(result.changed());
    EXPECT_FALSE(result.error_message().empty());
    expect_same_revisions(editor.query().revisions(), revisions);
    expect_same_history(editor.query().history(), history);
    expect_same_selection_identity(exact_selection(harness.project()), selection);
    expect_same_authored_document(
        *authored_document(harness.project(), source.value().entity), source_before);
    expect_same_authored_document(
        *authored_document(harness.project(), target.value().entity), target_before);
    EXPECT_EQ(*scene_material(harness.project(), target.value().entity), material_before);
  };
  expect_rejected(kInvalidQuaderModelingObjectId, 1);
  expect_rejected(target.value().modeling_object_id, kInvalidQuaderElementId);
  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Object).ok());
  expect_rejected(target.value().modeling_object_id, 1);
  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().clear().ok());
  expect_rejected(target.value().modeling_object_id, 1);
  ASSERT_TRUE(editor.selection().select_face(source.value(), 1).ok());
  ASSERT_TRUE(editor.selection().select_face(target.value(), 1, QuaderSelectionEdit::Add).ok());
  expect_rejected(target.value().modeling_object_id, 1);
  ASSERT_TRUE(editor.selection().select_face(source.value(), 1).ok());

  const ProjectMaterialFixture fixture(harness.project());
  ASSERT_TRUE(fixture.set_locked(target.value().entity, true));
  expect_rejected(target.value().modeling_object_id, 1);
  ASSERT_TRUE(fixture.set_locked(target.value().entity, false));
  ASSERT_TRUE(fixture.set_visible(target.value().entity, false));
  expect_rejected(target.value().modeling_object_id, 1);
  ASSERT_TRUE(fixture.set_visible(target.value().entity, true));
  ASSERT_TRUE(fixture.set_selectable(target.value().entity, false));
  expect_rejected(target.value().modeling_object_id, 1);
  ASSERT_TRUE(fixture.set_selectable(target.value().entity, true));
  ASSERT_TRUE(fixture.set_locked(source.value().entity, true));
  expect_rejected(target.value().modeling_object_id, 1);
  ASSERT_TRUE(fixture.set_locked(source.value().entity, false));
  const auto source_material = *scene_material(harness.project(), source.value().entity);
  ASSERT_TRUE(fixture.clear_material(source.value().entity));
  expect_rejected(target.value().modeling_object_id, 1);
  ASSERT_TRUE(fixture.set_material(source.value().entity, source_material));

  ASSERT_TRUE(editor.modeling().begin_transform_preview().ok());
  expect_rejected(target.value().modeling_object_id, 1);
  ASSERT_TRUE(editor.modeling().cancel_transform_preview().ok());
  ASSERT_TRUE(editor.query().history().can_redo);
}

TEST(ProjectMaterialBehaviorTests,
     FaceTextureTransferPublicationFailureDoesNotInstallStagedMaterial) {
  ProjectMaterialBehaviorHarness harness("face-transfer-atomic-failure");
  harness.create();
  auto &editor = harness.editor();
  const auto embedded = editor.assets().add_material(
      QuaderMaterialArgs{.name = "Staged Source Material"});
  ASSERT_TRUE(embedded.ok());
  const auto source = editor.modeling().create_mesh(
      QuaderPolygonDocumentInternalAccess::from_backend(texture_transfer_quad()));
  const auto target = editor.modeling().create_mesh(
      QuaderPolygonDocumentInternalAccess::from_backend(texture_transfer_quad(true)));
  ASSERT_TRUE(source.ok());
  ASSERT_TRUE(target.ok());
  const ProjectMaterialFixture fixture(harness.project());
  ASSERT_TRUE(fixture.set_material(source.value().entity,
      MaterialComponent{.material = embedded.value().id}));
  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(source.value(), 1).ok());
  const auto target_before = *authored_document(harness.project(), target.value().entity);
  const auto material_before = *scene_material(harness.project(), target.value().entity);
  const auto revisions_before = editor.query().revisions();
  const auto history_before = editor.query().history();
  const auto selection_before = exact_selection(harness.project());
  using ProjectionFailurePoint = ProjectMaterialFixture::ProjectionFailurePoint;
  constexpr std::array kFailures{
      ProjectionFailurePoint::AfterSdkMutation,
      ProjectionFailurePoint::CommittedSceneNotify,
      ProjectionFailurePoint::BeforeCommit,
  };
  for (const auto point : kFailures) {
    SCOPED_TRACE(static_cast<int>(point));
    ASSERT_TRUE(fixture.set_projection_failure(point));
    const auto result = editor.modeling().transfer_face_texture_to_face(
        target.value().modeling_object_id, 1);
    EXPECT_FALSE(result.ok());
    EXPECT_FALSE(result.changed());
    ASSERT_TRUE(fixture.set_projection_failure(ProjectionFailurePoint::None));
    expect_same_authored_document(
        *authored_document(harness.project(), target.value().entity), target_before);
    EXPECT_EQ(*scene_material(harness.project(), target.value().entity), material_before);
    expect_same_revisions(editor.query().revisions(), revisions_before);
    expect_same_history(editor.query().history(), history_before);
    expect_same_selection_identity(exact_selection(harness.project()), selection_before);
  }
}

TEST(ProjectMaterialBehaviorTests,
     FaceTextureTransferUsesWorldFramesAndDetachesUnrelatedSharedMeshes) {
  ProjectMaterialBehaviorHarness harness("face-transfer-world-shared");
  harness.create();
  auto &editor = harness.editor();
  const auto source = editor.modeling().create_mesh(
      QuaderPolygonDocumentInternalAccess::from_backend(texture_transfer_quad()),
      {.name = "World Source"});
  const auto target = editor.modeling().create_mesh(
      QuaderPolygonDocumentInternalAccess::from_backend(texture_transfer_quad()),
      {.name = "Rotated World Target"});
  ASSERT_TRUE(source.ok());
  ASSERT_TRUE(target.ok());
  constexpr float kQuarterTurn = 1.5707963267948966f;
  const auto rotated = editor.scene().set_transform(target.value().entity,
      {.position = {2.0f, 0.0f, 0.0f},
       .rotation_euler = {0.0f, kQuarterTurn, 0.0f}});
  ASSERT_TRUE(rotated.ok()) << rotated.error_message();
  const auto consumer = editor.scene().create_entity(
      {.name = "Unrelated Shared Consumer", .kind = QuaderEntityKind::Mesh});
  ASSERT_TRUE(consumer.ok()) << consumer.error_message();
  const AssetId shared_mesh = editor.query().entity(target.value().entity)->mesh;
  // A raw nonmodeling consumer is fixture setup. The public component setter
  // edits an existing component and deliberately cannot install its first one.
  ASSERT_TRUE(ProjectMaterialFixture(harness.project()).bind_shared_mesh(
      consumer.value().id, shared_mesh));
  const auto shared_before = *authored_document(harness.project(), consumer.value().id);
  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(source.value(), 1).ok());
  const auto selection_before = exact_selection(harness.project());
  const auto source_before = *authored_document(harness.project(), source.value().entity);
  const auto applied = editor.modeling().transfer_face_texture_to_face(
      target.value().modeling_object_id, 1);
  ASSERT_TRUE(applied.ok()) << applied.error_message();
  ASSERT_TRUE(applied.changed());
  EXPECT_NE(editor.query().entity(target.value().entity)->mesh, shared_mesh);
  EXPECT_EQ(editor.query().entity(consumer.value().id)->mesh, shared_mesh);
  expect_same_authored_document(
      *authored_document(harness.project(), consumer.value().id), shared_before);
  expect_same_authored_document(
      *authored_document(harness.project(), source.value().entity), source_before);
  const auto actual = effective_face_corner_uvs(
      harness.project(), target.value().entity, 1);
  ASSERT_EQ(actual.size(), 4u);
  constexpr std::array<QVec2, 4> kExpected{{
      {1.0f, 0.0f}, {2.0f, 0.0f}, {2.0f, 1.0f}, {1.0f, 1.0f}}};
  for (std::size_t corner = 0; corner < actual.size(); ++corner) {
    EXPECT_NEAR(actual[corner].x, kExpected[corner].x, 2.0e-5f);
    EXPECT_NEAR(actual[corner].y, kExpected[corner].y, 2.0e-5f);
  }
  expect_same_selection_identity(exact_selection(harness.project()), selection_before);
  const auto target_after = *authored_document(harness.project(), target.value().entity);
  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_EQ(editor.query().entity(target.value().entity)->mesh, shared_mesh);
  expect_same_authored_document(
      *authored_document(harness.project(), consumer.value().id), shared_before);
  ASSERT_TRUE(editor.history().redo().ok());
  EXPECT_EQ(editor.query().entity(consumer.value().id)->mesh, shared_mesh);
  expect_same_authored_document(
      *authored_document(harness.project(), target.value().entity), target_after);
}

TEST(ProjectMaterialBehaviorTests,
     FaceTextureTransferUsesDisplayedOccurrenceSamplingAndExplicitEditScope) {
  ProjectMaterialBehaviorHarness harness("face-transfer-instance-sampling");
  harness.create();
  auto &editor = harness.editor();
  const auto outside = editor.modeling().create_mesh(
      QuaderPolygonDocumentInternalAccess::from_backend(texture_transfer_quad()),
      {.name = "Outside Scope"});
  auto source_topology = texture_transfer_quad();
  source_topology.faces.front().texture_transform = {
      .scale = {1.25f, -0.75f}, .shift_uv = {0.125f, -0.25f},
      .rotation_degrees = 23.0f};
  const auto source = editor.modeling().create_mesh(
      QuaderPolygonDocumentInternalAccess::from_backend(source_topology),
      {.name = "Occurrence Source"});
  const auto target = editor.modeling().create_mesh(
      QuaderPolygonDocumentInternalAccess::from_backend(texture_transfer_quad(true)),
      {.name = "Occurrence Target"});
  ASSERT_TRUE(outside.ok());
  ASSERT_TRUE(source.ok());
  ASSERT_TRUE(target.ok());
  ASSERT_TRUE(editor.selection().select_objects({source.value(), target.value()}).ok());
  const auto instance = editor.modeling().create_instance_from_selection();
  ASSERT_TRUE(instance.ok()) << instance.error_message();
  const EntityId occurrence = instance.value().occurrence.root.id;
  auto transform = editor.query().entity(occurrence)->transform;
  transform.position = {7.0f, -3.0f, 5.0f};
  transform.rotation_euler = {0.17f, 0.31f, -0.42f};
  transform.scale = {2.0f, 3.0f, 4.0f};
  const auto transformed = editor.scene().set_transform(occurrence, transform);
  ASSERT_TRUE(transformed.ok()) << transformed.error_message();
  const auto occurrence_before = editor.query().entity(occurrence);
  ASSERT_TRUE(occurrence_before->texture_sampling_from_local.has_value());
  const auto occurrence_sampling = occurrence_before->texture_sampling_from_local->values;
  ASSERT_NE(occurrence_sampling, QMat4{}.values);
  const auto entered = editor.modeling().enter_instance_edit(occurrence);
  ASSERT_TRUE(entered.ok()) << entered.error_message();
  const auto selected_object = editor.selection().select_object(source.value());
  ASSERT_TRUE(selected_object.ok()) << selected_object.error_message();
  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Face).ok());
  // Component picking is the public occurrence-aware route; direct scene-scope
  // selection deliberately accepts definition members only in Object mode.
  const ProjectMaterialFixture fixture(harness.project());
  const auto source_world = fixture.instance_member_world_transform(
      source.value().entity, occurrence);
  ASSERT_TRUE(source_world.has_value());
  const auto selected_face = editor.selection().pick_select({.ray = {
      .origin = transform_point(*source_world, {1.0f, 1.0f, 4.0f}),
      .direction = normalize(transform_vector(*source_world, {0.0f, 0.0f, -1.0f}))}});
  ASSERT_TRUE(selected_face.ok()) << selected_face.error_message();
  ASSERT_EQ(selected_face.value().selected_face_count, 1u);
  ASSERT_EQ(selected_face.value().active_object_id, source.value().modeling_object_id);
  ASSERT_EQ(selected_face.value().active_face_id, 1);
  const auto source_before = *authored_document(harness.project(), source.value().entity);
  const auto target_before = *authored_document(harness.project(), target.value().entity);
  const auto selection_before = exact_selection(harness.project());
  const auto source_uv = effective_face_corner_uvs(
      harness.project(), source.value().entity, 1, occurrence);
  ASSERT_EQ(source_uv.size(), 4u);
  const auto revisions_before_failure = editor.query().revisions();
  const auto history_before_failure = editor.query().history();
  EXPECT_FALSE(editor.modeling().transfer_face_texture_to_face(
      outside.value().modeling_object_id, 1).ok());
  expect_same_revisions(editor.query().revisions(), revisions_before_failure);
  expect_same_history(editor.query().history(), history_before_failure);
  ASSERT_TRUE(fixture.set_locked(occurrence, true));
  const auto revisions_while_locked = editor.query().revisions();
  EXPECT_FALSE(editor.modeling().transfer_face_texture_to_face(
      target.value().modeling_object_id, 1).ok());
  expect_same_revisions(editor.query().revisions(), revisions_while_locked);
  ASSERT_TRUE(fixture.set_locked(occurrence, false));

  const auto applied = editor.modeling().transfer_face_texture_to_face(
      target.value().modeling_object_id, 1);
  ASSERT_TRUE(applied.ok()) << applied.error_message();
  ASSERT_TRUE(applied.changed());
  const auto target_uv = effective_face_corner_uvs(
      harness.project(), target.value().entity, 1, occurrence);
  ASSERT_EQ(target_uv.size(), 4u);
  // The folded edge is twice as long in displayed world space as the source's
  // U edge (occurrence Z scale 4 versus X scale 2), including sampling C.
  const QVec2 continued = (source_uv[1] - source_uv[0]) * 2.0f;
  const std::array<QVec2, 4> expected{
      source_uv[1], source_uv[1] + continued,
      source_uv[2] + continued, source_uv[2]};
  for (std::size_t corner = 0; corner < expected.size(); ++corner) {
    EXPECT_NEAR(target_uv[corner].x, expected[corner].x, 2.0e-4f);
    EXPECT_NEAR(target_uv[corner].y, expected[corner].y, 2.0e-4f);
  }
  EXPECT_EQ(editor.query().entity(occurrence)->texture_sampling_from_local->values,
            occurrence_sampling);
  expect_same_authored_document(
      *authored_document(harness.project(), source.value().entity), source_before);
  expect_same_selection_identity(exact_selection(harness.project()), selection_before);
  const auto target_after = *authored_document(harness.project(), target.value().entity);
  const auto history_before_no_op = editor.query().history();
  const auto revisions_before_no_op = editor.query().revisions();
  const auto repeated = editor.modeling().transfer_face_texture_to_face(
      target.value().modeling_object_id, 1);
  ASSERT_TRUE(repeated.ok()) << repeated.error_message();
  EXPECT_FALSE(repeated.changed());
  expect_same_history(editor.query().history(), history_before_no_op);
  expect_same_revisions(editor.query().revisions(), revisions_before_no_op);
  ASSERT_TRUE(editor.history().undo().ok());
  expect_same_authored_document(
      *authored_document(harness.project(), target.value().entity), target_before);
  ASSERT_TRUE(editor.history().redo().ok());
  expect_same_authored_document(
      *authored_document(harness.project(), target.value().entity), target_after);
  EXPECT_EQ(editor.query().entity(occurrence)->texture_sampling_from_local->values,
            occurrence_sampling);
  ASSERT_TRUE(editor.modeling().exit_instance_edit().ok());
}

TEST(ProjectMaterialBehaviorTests,
     FaceTextureTransferWithinOneMeshKeepsSourceAndIndependentClickHistory) {
  ProjectMaterialBehaviorHarness harness("face-transfer-same-mesh");
  harness.create();
  auto &editor = harness.editor();
  const auto box = editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}});
  ASSERT_TRUE(box.ok());
  const auto *document = authored_document(harness.project(), box.value().entity);
  ASSERT_GE(document->faces.size(), 3u);
  const QuaderElementId source_face = document->faces[0].id;
  const QuaderElementId first_target = document->faces[1].id;
  const QuaderElementId second_target = document->faces[2].id;
  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(box.value(), source_face).ok());
  ASSERT_TRUE(edit_selected_face_texture(editor, QuaderFaceTextureField::ScaleX, 1.5).ok());
  const auto before = *authored_document(harness.project(), box.value().entity);
  const auto selection_before = exact_selection(harness.project());
  const auto first = editor.modeling().transfer_face_texture_to_face(
      box.value().modeling_object_id, first_target);
  ASSERT_TRUE(first.ok()) << first.error_message();
  ASSERT_TRUE(first.changed());
  const auto after_first = *authored_document(harness.project(), box.value().entity);
  const auto second = editor.modeling().transfer_face_texture_to_face(
      box.value().modeling_object_id, second_target);
  ASSERT_TRUE(second.ok()) << second.error_message();
  ASSERT_TRUE(second.changed());
  const auto after_second = *authored_document(harness.project(), box.value().entity);
  auto unchanged_faces = after_second;
  unchanged_faces.faces[1] = before.faces[1];
  unchanged_faces.faces[2] = before.faces[2];
  expect_same_authored_document(unchanged_faces, before);
  expect_same_selection_identity(exact_selection(harness.project()), selection_before);
  ASSERT_TRUE(editor.history().undo().ok());
  expect_same_authored_document(
      *authored_document(harness.project(), box.value().entity), after_first);
  ASSERT_TRUE(editor.history().undo().ok());
  expect_same_authored_document(
      *authored_document(harness.project(), box.value().entity), before);
  ASSERT_TRUE(editor.history().redo().ok());
  expect_same_authored_document(
      *authored_document(harness.project(), box.value().entity), after_first);
  ASSERT_TRUE(editor.history().redo().ok());
  expect_same_authored_document(
      *authored_document(harness.project(), box.value().entity), after_second);
  expect_same_selection_identity(exact_selection(harness.project()), selection_before);
}

TEST(ProjectMaterialBehaviorTests,
     FaceTextureTransferContinuesTranslatedSlopeOntoTaperedSidesAndThinCap) {
  ProjectMaterialBehaviorHarness harness("sloped-face-continuation");
  harness.create();
  const InstalledMaterial beige{
      .path = project_path("Materials/beige.qmat"),
      .material = project_material(kAmberMaterialId, "Beige", {0.8F, 0.7F, 0.5F})};
  const InstalledMaterial blue{
      .path = project_path("Materials/blue.qmat"),
      .material = project_material(kBlueMaterialId, "Blue", {0.1F, 0.2F, 1.0F})};
  const std::array materials{beige, blue};
  harness.install_and_reopen(materials);
  auto &editor = harness.editor();
  const auto original = sloped_texture_continuation_document();
  const auto created = editor.modeling().create_mesh(
      QuaderPolygonDocumentInternalAccess::from_backend(original),
      {.name = "Sloped texture continuation"});
  ASSERT_TRUE(created.ok()) << created.error_message();
  const auto object = created.value();
  editor.preferences().set_runtime_hooks({
      .texture_lock_enabled = [] { return false; },
      .texture_lock_scale_enabled = [] { return false; }});
  ASSERT_TRUE(editor.scene().set_transform(object.entity,
      {.position = {5.0F, 0.5F, -21.5F}}).ok());
  const auto placed = editor.query().entity(object.entity);
  ASSERT_TRUE(placed.has_value());
  ASSERT_TRUE(placed->texture_sampling_from_local.has_value());
  QMat4 expected_sampling;
  expected_sampling.values[12] = 5.0F;
  expected_sampling.values[13] = 0.5F;
  expected_sampling.values[14] = -21.5F;
  EXPECT_EQ(placed->texture_sampling_from_local->values, expected_sampling.values);
  auto &session = *harness.project().active_scene()->session;
  const ProjectMaterialFixture fixture(harness.project());
  ASSERT_TRUE(fixture.set_material(object.entity, {
      .material = MaterialReference::project(blue.material.id),
      .material_slots = {{1U, MaterialReference::project(beige.material.id)}}}));
  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(object, 1382).ok());
  const auto before = *authored_document(harness.project(), object.entity);
  expect_same_authored_document(before, original);
  const auto selected = exact_selection(harness.project());
  const auto source_uv = effective_face_corner_uvs(harness.project(), object.entity, 1382);
  ASSERT_EQ(source_uv.size(), 4U);
  const auto a = quader_poly::find_vertex(before, 55)->position;
  const auto b = quader_poly::find_vertex(before, 53)->position;
  const auto d = quader_poly::find_vertex(before, 1385)->position;
  const float width = b.x - a.x;
  const float slope_length = std::sqrt(length_squared(d - a));
  const auto along_seam = (d - a) * (1.0F / slope_length);
  const QVec3 side_tangent{0, -along_seam.z, along_seam.y};
  const auto across_uv = source_uv[1] - source_uv[0];
  const auto along_uv = source_uv[3] - source_uv[0];
  const auto expected_uv = [&](QuaderElementId target, QVec3 point) {
    if (target == 68) {
      // The cap lies beyond the source's upper plane intersection. Its gap
      // and depth extend the source's slope distance without changing density.
      return source_uv[3] + across_uv * ((point.x - d.x) / width) +
             along_uv * ((d.z - point.z) / slope_length);
    }
    const bool left = target == 1381;
    const auto relative = point - (left ? a : b);
    return (left ? source_uv[0] : source_uv[1]) +
           along_uv * (dot(relative, along_seam) / slope_length) +
           across_uv * ((left ? 1.0F : -1.0F) * dot(relative, side_tangent) / width);
  };
  std::vector<quader_poly::Document> documents{before};
  std::vector<MaterialComponent> bindings{*scene_material(harness.project(), object.entity)};
  const auto history_depth = session.history().undo_count();
  for (const QuaderElementId target : {1381, 1383, 68}) {
    SCOPED_TRACE(target);
    const auto history_before = editor.query().history();
    const auto revisions_before = editor.query().revisions();
    ASSERT_TRUE(fixture.set_projection_failure(
        ProjectMaterialFixture::ProjectionFailurePoint::BeforeCommit));
    const auto failed = editor.modeling().transfer_face_texture_to_face(
        object.modeling_object_id, target);
    ASSERT_TRUE(fixture.set_projection_failure(
        ProjectMaterialFixture::ProjectionFailurePoint::None));
    EXPECT_FALSE(failed.ok());
    EXPECT_FALSE(failed.changed());
    expect_same_authored_document(*authored_document(harness.project(), object.entity),
                                  documents.back());
    EXPECT_EQ(*scene_material(harness.project(), object.entity), bindings.back());
    expect_same_selection_identity(exact_selection(harness.project()), selected);
    expect_same_history(editor.query().history(), history_before);
    expect_same_revisions(editor.query().revisions(), revisions_before);

    const auto applied = editor.modeling().transfer_face_texture_to_face(
        object.modeling_object_id, target);
    ASSERT_TRUE(applied.ok()) << applied.error_message();
    ASSERT_TRUE(applied.changed());
    const auto after = *authored_document(harness.project(), object.entity);
    const auto *face = quader_poly::find_face(after, static_cast<quader_poly::ElementId>(target));
    ASSERT_NE(face, nullptr);
    const auto material = *scene_material(harness.project(), object.entity);
    EXPECT_EQ(material.material_for_slot(face->material_slot),
              MaterialReference::project(beige.material.id));
    EXPECT_EQ(face->texture_transform, quader_poly::find_face(before, 1382)->texture_transform);
    auto unchanged_faces = after;
    *quader_poly::find_face(unchanged_faces, static_cast<quader_poly::ElementId>(target)) =
        *quader_poly::find_face(documents.back(), static_cast<quader_poly::ElementId>(target));
    expect_same_authored_document(unchanged_faces, documents.back());
    expect_same_selection_identity(exact_selection(harness.project()), selected);
    const auto corners = effective_face_corner_uvs(harness.project(), object.entity,
        static_cast<quader_poly::ElementId>(target));
    ASSERT_EQ(corners.size(), 4U);
    QVec3 interior{};
    constexpr std::array kWeights{0.12F, 0.28F, 0.42F, 0.18F};
    for (std::size_t corner = 0; corner < corners.size(); ++corner) {
      const auto position = quader_poly::find_vertex(after, face->vertices[corner])->position;
      const auto expected = expected_uv(target, position);
      EXPECT_NEAR(corners[corner].x, expected.x, 2.0e-5F) << corner;
      EXPECT_NEAR(corners[corner].y, expected.y, 2.0e-5F) << corner;
      interior += position * kWeights[corner];
    }
    const auto rendered = rendered_face_uv_at(harness.project(), object.entity,
        static_cast<quader_poly::ElementId>(target), interior);
    ASSERT_TRUE(rendered.has_value());
    const auto expected_interior = expected_uv(target, interior);
    EXPECT_NEAR(rendered->x, expected_interior.x, 2.0e-5F);
    EXPECT_NEAR(rendered->y, expected_interior.y, 2.0e-5F);
    documents.push_back(after);
    bindings.push_back(material);
    EXPECT_EQ(session.history().undo_count(), history_depth + documents.size() - 1U);
    EXPECT_EQ(editor.query().history().undo_label, "Transfer Face Texture");
    const auto no_op_history = editor.query().history();
    const auto no_op_revisions = editor.query().revisions();
    const auto repeated = editor.modeling().transfer_face_texture_to_face(
        object.modeling_object_id, target);
    ASSERT_TRUE(repeated.ok()) << repeated.error_message();
    EXPECT_FALSE(repeated.changed());
    expect_same_authored_document(*authored_document(harness.project(), object.entity), after);
    expect_same_history(editor.query().history(), no_op_history);
    expect_same_revisions(editor.query().revisions(), no_op_revisions);
  }
  for (std::size_t index = documents.size() - 1U; index > 0U; --index) {
    ASSERT_TRUE(editor.history().undo().ok());
    expect_same_authored_document(*authored_document(harness.project(), object.entity),
                                  documents[index - 1U]);
    EXPECT_EQ(*scene_material(harness.project(), object.entity), bindings[index - 1U]);
    expect_same_selection_identity(exact_selection(harness.project()), selected);
  }
  EXPECT_EQ(session.history().undo_count(), history_depth);
  for (std::size_t index = 1U; index < documents.size(); ++index) {
    ASSERT_TRUE(editor.history().redo().ok());
    expect_same_authored_document(*authored_document(harness.project(), object.entity),
                                  documents[index]);
    EXPECT_EQ(*scene_material(harness.project(), object.entity), bindings[index]);
    expect_same_selection_identity(exact_selection(harness.project()), selected);
  }
}

TEST(ProjectMaterialBehaviorTests,
     FaceTextureTransferContinuesTaperedSideOntoHorizontalLips) {
  for (const float rotation : {0.0F, 31.0F}) {
    SCOPED_TRACE(rotation);
    ProjectMaterialBehaviorHarness harness("tapered-side-lip-continuation");
    harness.create();
    const InstalledMaterial beige{
        .path = project_path("Materials/beige.qmat"),
        .material = project_material(kAmberMaterialId, "Beige", {0.8F, 0.7F, 0.5F})};
    const InstalledMaterial blue{
        .path = project_path("Materials/blue.qmat"),
        .material = project_material(kBlueMaterialId, "Blue", {0.1F, 0.2F, 1.0F})};
    const std::array materials{beige, blue};
    harness.install_and_reopen(materials);
    auto &editor = harness.editor();
    const auto original = tapered_side_texture_continuation_document(rotation);
    const auto created = editor.modeling().create_mesh(
        QuaderPolygonDocumentInternalAccess::from_backend(original),
        {.name = "Tapered side and horizontal lips"});
    ASSERT_TRUE(created.ok()) << created.error_message();
    const auto object = created.value();
    editor.preferences().set_runtime_hooks({
        .texture_lock_enabled = [] { return false; },
        .texture_lock_scale_enabled = [] { return false; }});
    ASSERT_TRUE(editor.scene().set_transform(object.entity,
        {.position = {5.0F, 0.5F, -21.5F}}).ok());
    const ProjectMaterialFixture fixture(harness.project());
    ASSERT_TRUE(fixture.set_material(object.entity, {
        .material = MaterialReference::project(blue.material.id),
        .material_slots = {{1U, MaterialReference::project(beige.material.id)}}}));
    ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Face).ok());
    ASSERT_TRUE(editor.selection().select_face(object, 1381).ok());
    expect_same_authored_document(*authored_document(harness.project(), object.entity),
                                  original);
    const auto selected = exact_selection(harness.project());
    const auto source_uv = effective_face_corner_uvs(harness.project(), object.entity, 1381);
    ASSERT_EQ(source_uv.size(), 4U);
    const auto a = quader_poly::find_vertex(original, 32)->position;
    const auto b = quader_poly::find_vertex(original, 55)->position;
    const auto d = quader_poly::find_vertex(original, 1384)->position;
    const auto along_z = (source_uv[1] - source_uv[0]) * (1.0F / (b.z - a.z));
    const auto along_y = (source_uv[3] - source_uv[0] - along_z * (d.z - a.z)) *
                         (1.0F / (d.y - a.y));
    const auto expected_uv = [&](QVec3 point) {
      // Unfold a +Y lip into the -X source plane through their intersection:
      // horizontal +X distance becomes source +Y distance. Retain the phase
      // across the gap as well as the diagonal chart's two independent axes.
      return source_uv[0] + along_y * (point.y + point.x - a.x - a.y) +
             along_z * (point.z - a.z);
    };
    const auto source_edge = rendered_face_uv_at(harness.project(), object.entity,
                                                1381, (a + b) * 0.5F);
    ASSERT_TRUE(source_edge.has_value());
    EXPECT_NEAR(source_edge->x, (source_uv[0].x + source_uv[1].x) * 0.5F, 2.0e-5F);
    EXPECT_NEAR(source_edge->y, (source_uv[0].y + source_uv[1].y) * 0.5F, 2.0e-5F);
    auto &session = *harness.project().active_scene()->session;
    for (const QuaderElementId target : {35, 72}) {
      SCOPED_TRACE(target);
      const auto before = *authored_document(harness.project(), object.entity);
      const auto material_before = *scene_material(harness.project(), object.entity);
      const auto history_before = editor.query().history();
      const auto revisions_before = editor.query().revisions();
      const auto depth_before = session.history().undo_count();
      ASSERT_TRUE(fixture.set_projection_failure(
          ProjectMaterialFixture::ProjectionFailurePoint::BeforeCommit));
      const auto failed = editor.modeling().transfer_face_texture_to_face(
          object.modeling_object_id, target);
      ASSERT_TRUE(fixture.set_projection_failure(
          ProjectMaterialFixture::ProjectionFailurePoint::None));
      EXPECT_FALSE(failed.ok());
      EXPECT_FALSE(failed.changed());
      expect_same_authored_document(*authored_document(harness.project(), object.entity), before);
      EXPECT_EQ(*scene_material(harness.project(), object.entity), material_before);
      expect_same_selection_identity(exact_selection(harness.project()), selected);
      expect_same_history(editor.query().history(), history_before);
      expect_same_revisions(editor.query().revisions(), revisions_before);

      const auto applied = editor.modeling().transfer_face_texture_to_face(
          object.modeling_object_id, target);
      ASSERT_TRUE(applied.ok()) << applied.error_message();
      ASSERT_TRUE(applied.changed());
      const auto after = *authored_document(harness.project(), object.entity);
      const auto *face = quader_poly::find_face(after, static_cast<quader_poly::ElementId>(target));
      ASSERT_NE(face, nullptr);
      const auto material_after = *scene_material(harness.project(), object.entity);
      EXPECT_EQ(material_after.material_for_slot(face->material_slot),
                MaterialReference::project(beige.material.id));
      EXPECT_EQ(face->texture_transform, quader_poly::find_face(original, 1381)->texture_transform);
      auto unchanged_faces = after;
      *quader_poly::find_face(unchanged_faces, static_cast<quader_poly::ElementId>(target)) =
          *quader_poly::find_face(before, static_cast<quader_poly::ElementId>(target));
      expect_same_authored_document(unchanged_faces, before);
      expect_same_selection_identity(exact_selection(harness.project()), selected);
      const auto corners = effective_face_corner_uvs(harness.project(), object.entity,
          static_cast<quader_poly::ElementId>(target));
      ASSERT_EQ(corners.size(), 4U);
      QVec3 interior{};
      constexpr std::array kWeights{0.12F, 0.28F, 0.42F, 0.18F};
      for (std::size_t corner = 0; corner < corners.size(); ++corner) {
        const auto position = quader_poly::find_vertex(after, face->vertices[corner])->position;
        const auto expected = expected_uv(position);
        EXPECT_NEAR(corners[corner].x, expected.x, 2.0e-5F) << corner;
        EXPECT_NEAR(corners[corner].y, expected.y, 2.0e-5F) << corner;
        interior += position * kWeights[corner];
      }
      // Sample both the visible interior and the edge facing the source plane.
      // These lips are disjoint, so continuation extrapolates across the gap.
      const auto edge_start = quader_poly::find_vertex(after, face->vertices[0])->position;
      const auto edge_end = quader_poly::find_vertex(after,
          face->vertices[target == 35 ? 3U : 1U])->position;
      for (const auto sample : {interior, (edge_start + edge_end) * 0.5F}) {
        const auto rendered = rendered_face_uv_at(harness.project(), object.entity,
            static_cast<quader_poly::ElementId>(target), sample);
        ASSERT_TRUE(rendered.has_value());
        const auto expected = expected_uv(sample);
        EXPECT_NEAR(rendered->x, expected.x, 2.0e-5F);
        EXPECT_NEAR(rendered->y, expected.y, 2.0e-5F);
      }
      EXPECT_EQ(session.history().undo_count(), depth_before + 1U);
      EXPECT_EQ(editor.query().history().undo_label, "Transfer Face Texture");
      const auto no_op_history = editor.query().history();
      const auto no_op_revisions = editor.query().revisions();
      const auto repeated = editor.modeling().transfer_face_texture_to_face(
          object.modeling_object_id, target);
      ASSERT_TRUE(repeated.ok()) << repeated.error_message();
      EXPECT_FALSE(repeated.changed());
      expect_same_authored_document(*authored_document(harness.project(), object.entity), after);
      expect_same_history(editor.query().history(), no_op_history);
      expect_same_revisions(editor.query().revisions(), no_op_revisions);
      ASSERT_TRUE(editor.history().undo().ok());
      expect_same_authored_document(*authored_document(harness.project(), object.entity), before);
      EXPECT_EQ(*scene_material(harness.project(), object.entity), material_before);
      EXPECT_EQ(session.history().undo_count(), depth_before);
      expect_same_selection_identity(exact_selection(harness.project()), selected);
      ASSERT_TRUE(editor.history().redo().ok());
      expect_same_authored_document(*authored_document(harness.project(), object.entity), after);
      EXPECT_EQ(*scene_material(harness.project(), object.entity), material_after);
      EXPECT_EQ(session.history().undo_count(), depth_before + 1U);
      expect_same_selection_identity(exact_selection(harness.project()), selected);
    }
  }
}

TEST(ProjectMaterialBehaviorTests,
     DirectHoverTargetsCanAssignWhenTheFaceHasNoCurrentMaterial) {
  ProjectAssetId requested_material_id = project_asset_id(kDefaultProjectMaterialId);
  ProjectMaterialBehaviorHarness harness("direct-hover-missing-material");
  harness.create();
  const QuaderApiResult<QuaderModelingObjectRef> box =
      harness.editor().modeling().create_box({.bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}}, .name = "Hover Target Without Material",
           .material = QuaderMaterialReference::project(requested_material_id)});
  ASSERT_TRUE(box.ok()) << box.error_message();
  const quader_poly::Document *document =
      authored_document(harness.project(), box.value().entity);
  ASSERT_NE(document, nullptr);
  ASSERT_FALSE(document->faces.empty());
  const QuaderElementId face = document->faces.front().id;
  const auto slots_before =
      face_slot_snapshot(harness.project(), box.value().entity);

  const ProjectMaterialFixture fixture(harness.project());
  ASSERT_TRUE(fixture.clear_material(box.value().entity));

  const QuaderApiVoidResult face_applied =
      harness.editor().modeling().apply_material_to_face(
          box.value().modeling_object_id, face, QuaderMaterialReference::project(requested_material_id));
  ASSERT_TRUE(face_applied.ok()) << face_applied.error_message();
  ASSERT_TRUE(face_applied.changed());
  const auto face_material =
      harness.editor().modeling().project_material_for_face(
          box.value().modeling_object_id, face);
  ASSERT_TRUE(face_material.ok()) << face_material.error_message();
  EXPECT_EQ(face_material.value().id,
            project_asset_id(kDefaultProjectMaterialId));
  ASSERT_TRUE(harness.editor().history().undo().ok());
  EXPECT_EQ(scene_material(harness.project(), box.value().entity), nullptr);
  EXPECT_EQ(face_slot_snapshot(harness.project(), box.value().entity),
            slots_before);

  const QuaderApiVoidResult mesh_applied =
      harness.editor().modeling().apply_material_to_mesh(
          box.value().modeling_object_id, face, QuaderMaterialReference::project(requested_material_id));
  ASSERT_TRUE(mesh_applied.ok()) << mesh_applied.error_message();
  ASSERT_TRUE(mesh_applied.changed());
  expect_primary_material(harness.project(), box.value().entity,
                          project_asset_id(kDefaultProjectMaterialId));
  expect_all_face_slots(harness.project(), box.value().entity, 0u);
  ASSERT_TRUE(harness.editor().history().undo().ok());
  EXPECT_EQ(scene_material(harness.project(), box.value().entity), nullptr);
  EXPECT_EQ(face_slot_snapshot(harness.project(), box.value().entity),
            slots_before);
}

TEST(ProjectMaterialBehaviorTests,
     PrimitiveAcceptanceUsesExplicitMaterialPerShapeWithExactHistory) {
  ProjectMaterialBehaviorHarness harness("primitive-explicit-material");
  harness.create();
  const InstalledMaterial amber{
      .path = project_path("Materials/amber.qmat"),
      .material = project_material(kAmberMaterialId, "Amber", {1.0f, 0.5f, 0.1f}),
  };
  const InstalledMaterial blue{
      .path = project_path("Materials/blue.qmat"),
      .material = project_material(kBlueMaterialId, "Blue", {0.1f, 0.2f, 1.0f}),
  };
  harness.install_and_reopen(std::array{amber, blue});
  auto &editor = harness.editor();
  auto &session = *harness.project().active_scene()->session;
  constexpr std::array kShapes{
      QuaderPrimitiveCreationShape::Box,
      QuaderPrimitiveCreationShape::Cylinder,
      QuaderPrimitiveCreationShape::Cone,
      QuaderPrimitiveCreationShape::Sphere,
      QuaderPrimitiveCreationShape::Torus,
  };
  for (const auto shape : kShapes) {
    SCOPED_TRACE(static_cast<int>(shape));
    ASSERT_TRUE(editor.tools().begin_primitive_creation(shape).ok());
    ASSERT_TRUE(ProjectMaterialFixture(harness.project()).prepare_primitive_footprint());
    ASSERT_TRUE(editor.tools().accept_active_primitive(
        "Amber Primitive", QuaderMaterialReference::project(amber.material.id)).ok());
    const auto first = editor.tools().live_primitive_creation();
    ASSERT_TRUE(first.has_value());
    ASSERT_NO_FATAL_FAILURE(expect_primitive_project_material(
        harness.project(), editor, first->object_id, amber.material.id));
    EXPECT_EQ(session.history().undo_count(), 0u);

    // A different choice is consumed only by the next accepted footprint.
    ASSERT_TRUE(ProjectMaterialFixture(harness.project()).prepare_primitive_footprint(4.0f));
    ASSERT_TRUE(editor.tools().accept_active_primitive(
        "Blue Primitive", QuaderMaterialReference::project(blue.material.id)).ok());
    const auto second = editor.tools().live_primitive_creation();
    ASSERT_TRUE(second.has_value());
    ASSERT_NO_FATAL_FAILURE(expect_primitive_project_material(
        harness.project(), editor, first->object_id, amber.material.id));
    ASSERT_NO_FATAL_FAILURE(expect_primitive_project_material(
        harness.project(), editor, second->object_id, blue.material.id));
    EXPECT_EQ(session.history().undo_count(), 0u);

    ASSERT_TRUE(editor.history().undo().ok());
    ASSERT_EQ(editor.query().modeling_objects().size(), 1u);
    ASSERT_NO_FATAL_FAILURE(expect_primitive_project_material(
        harness.project(), editor, first->object_id, amber.material.id));
    ASSERT_TRUE(editor.history().redo().ok());
    ASSERT_EQ(editor.query().modeling_objects().size(), 2u);
    ASSERT_NO_FATAL_FAILURE(expect_primitive_project_material(
        harness.project(), editor, second->object_id, blue.material.id));

    ASSERT_TRUE(editor.tools().commit_active_tool_preview().ok());
    EXPECT_EQ(session.history().undo_count(), 2u);
    ASSERT_TRUE(editor.history().undo().ok());
    ASSERT_EQ(editor.query().modeling_objects().size(), 1u);
    ASSERT_NO_FATAL_FAILURE(expect_primitive_project_material(
        harness.project(), editor, first->object_id, amber.material.id));
    ASSERT_TRUE(editor.history().undo().ok());
    EXPECT_TRUE(editor.query().modeling_objects().empty());
    ASSERT_TRUE(editor.history().redo().ok());
    ASSERT_TRUE(editor.history().redo().ok());
    ASSERT_NO_FATAL_FAILURE(expect_primitive_project_material(
        harness.project(), editor, first->object_id, amber.material.id));
    ASSERT_NO_FATAL_FAILURE(expect_primitive_project_material(
        harness.project(), editor, second->object_id, blue.material.id));
    ASSERT_TRUE(editor.history().undo().ok());
    ASSERT_TRUE(editor.history().undo().ok());
  }
}

TEST(ProjectMaterialBehaviorTests,
     PrimitiveAcceptanceFallsBackForInvalidMissingAndDeletedMaterialReferences) {
  ProjectMaterialBehaviorHarness harness("primitive-material-fallback");
  harness.create();
  const InstalledMaterial amber{
      .path = project_path("Materials/amber.qmat"),
      .material = project_material(kAmberMaterialId, "Amber", {1.0f, 0.5f, 0.1f}),
  };
  harness.install_and_reopen(std::array{amber});
  auto &editor = harness.editor();
  QuaderProjectAssetsApiInternalAccess::set_recycle_port(editor.project_assets(),
      [&harness](std::span<const std::filesystem::path> paths) {
        const auto bin = harness.root() / ".quader/test-recycle";
        std::filesystem::create_directories(bin);
        ProjectAssetRecycleResult result{.ok = true};
        for (const auto &path : paths) {
          std::error_code error;
          std::filesystem::rename(path, bin / path.filename(), error);
          result.items.push_back({.original_path = path, .recycled = !error,
                                   .message = error.message()});
          if (error) {
            result.ok = false;
            result.message = error.message();
            break;
          }
        }
        return result;
      });
  const std::array removed{amber.material.id};
  const auto deletion = editor.project_assets().preview_delete_materials(removed);
  ASSERT_TRUE(deletion.ok()) << deletion.error_message();
  const auto deleted = editor.project_assets().delete_materials(
      removed, deletion.value().revision);
  ASSERT_TRUE(deleted.ok()) << deleted.error_message();
  const auto history_before =
      harness.project().active_scene()->session->history().undo_count();
  const std::array references{
      QuaderMaterialReference{},
      QuaderMaterialReference::project(project_asset_id(kBlueMaterialId)),
      QuaderMaterialReference::project(amber.material.id),
      QuaderMaterialReference::embedded(std::numeric_limits<AssetId>::max()),
  };
  ASSERT_TRUE(editor.tools().begin_primitive_creation(
      QuaderPrimitiveCreationShape::Box).ok());
  for (const auto reference : references) {
    ASSERT_TRUE(ProjectMaterialFixture(harness.project()).prepare_primitive_footprint());
    const auto created = editor.tools().accept_active_primitive("Fallback Primitive", reference);
    ASSERT_TRUE(created.ok()) << created.error_message();
    const auto state = editor.tools().live_primitive_creation();
    ASSERT_TRUE(state.has_value());
    ASSERT_NO_FATAL_FAILURE(expect_primitive_project_material(
        harness.project(), editor, state->object_id,
        project_asset_id(kDefaultProjectMaterialId)));
  }
  ASSERT_TRUE(editor.tools().commit_active_tool_preview().ok());
  EXPECT_EQ(harness.project().active_scene()->session->history().undo_count(),
            history_before + references.size());
}

TEST(ProjectMaterialBehaviorTests,
     ImplicitApplyMaterialCommandIsRetiredWhileExplicitAssignmentWorks) {
  ProjectAssetId requested_material_id = project_asset_id(kDefaultProjectMaterialId);
  ProjectMaterialBehaviorHarness harness("public-object-apply");
  harness.create();
  const QuaderApiResult<QuaderModelingObjectRef> box =
      harness.editor().modeling().create_box({.bounds = {.min = {-1.0f, -1.0f, -1.0f},
           .max = {1.0f, 1.0f, 1.0f}}, .name = "Command Box",
           .material = QuaderMaterialReference::project(requested_material_id)});
  ASSERT_TRUE(box.ok()) << box.error_message();
  ASSERT_TRUE(harness.editor().selection().select_object(box.value()).ok());

  const std::vector<QuaderCommandDescriptor> commands =
      harness.editor().commands().available();
  const auto descriptor = std::find_if(
      commands.begin(), commands.end(), [](const QuaderCommandDescriptor &item) {
        return item.id == "edit.apply_material";
      });
  EXPECT_EQ(descriptor, commands.end());

  const QuaderApiVoidResult applied =
      harness.editor().modeling().apply_material(QuaderMaterialReference::project(requested_material_id));
  EXPECT_TRUE(applied.ok()) << applied.error_message();
}

TEST(ProjectMaterialBehaviorTests,
     SelectedFaceAssignmentRejectsStaleTargetsAndPreservesNoOpRedo) {
  ProjectMaterialBehaviorHarness harness("selected-face-assignment");
  harness.create();
  const InstalledMaterial amber{
      .path = project_path("Materials/amber.qmat"),
      .material = project_material(kAmberMaterialId, "Amber", {1.0f, 0.5f, 0.1f}),
  };
  harness.install_and_reopen(std::array{amber});
  auto &editor = harness.editor();
  const auto reference = QuaderMaterialReference::project(amber.material.id);
  const auto box = editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}}, "Faces");
  ASSERT_TRUE(box.ok()) << box.error_message();
  const auto initial = *authored_document(harness.project(), box.value().entity);
  ASSERT_GE(initial.faces.size(), 3u);
  const auto first_face = initial.faces[0].id;
  const auto second_face = initial.faces[1].id;
  const auto scene_id = editor.modeling().face_texture_settings_revision().scene_session_id;
  const auto object_history = editor.query().history();
  EXPECT_FALSE(editor.modeling().apply_material_to_selected_faces(
      reference, scene_id, editor.modeling().face_texture_settings().selection_token).ok());
  expect_same_history(editor.query().history(), object_history);
  expect_primary_material(harness.project(), box.value().entity,
                          project_asset_id(kDefaultProjectMaterialId));

  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(box.value(), first_face).ok());
  const auto first_token = editor.modeling().face_texture_settings().selection_token;
  ASSERT_TRUE(editor.selection().select_face(box.value(), second_face).ok());
  const auto history_before_stale = editor.query().history();
  const auto selection_before_stale = exact_selection(harness.project());
  const auto material_before_stale = *scene_material(harness.project(), box.value().entity);
  EXPECT_FALSE(editor.modeling().apply_material_to_selected_faces(
      reference, scene_id, first_token).ok());
  expect_same_history(editor.query().history(), history_before_stale);
  expect_same_authored_document(
      *authored_document(harness.project(), box.value().entity), initial);
  EXPECT_EQ(*scene_material(harness.project(), box.value().entity), material_before_stale);
  expect_same_selection_identity(exact_selection(harness.project()), selection_before_stale);

  ASSERT_TRUE(editor.selection().select_face(
      box.value(), first_face, QuaderSelectionEdit::Add).ok());
  const auto selection = editor.modeling().face_texture_settings();
  ASSERT_EQ(selection.selected_face_count, 2u);
  const ProjectMaterialFixture fixture(harness.project());
  ASSERT_TRUE(fixture.set_locked(box.value().entity, true));
  const auto locked_history = editor.query().history();
  EXPECT_FALSE(editor.modeling().apply_material_to_selected_faces(
      reference, scene_id, selection.selection_token).ok());
  expect_same_history(editor.query().history(), locked_history);
  expect_same_authored_document(
      *authored_document(harness.project(), box.value().entity), initial);
  ASSERT_TRUE(fixture.set_locked(box.value().entity, false));
  const auto unlocked_selection = editor.modeling().face_texture_settings();
  const auto assigned = editor.modeling().apply_material_to_selected_faces(
      reference, scene_id, unlocked_selection.selection_token);
  ASSERT_TRUE(assigned.ok()) << assigned.error_message();
  ASSERT_TRUE(assigned.changed());
  EXPECT_EQ(editor.query().history().undo_label, "Assign Material");
  for (const auto face : {first_face, second_face}) {
    const auto material = editor.modeling().project_material_for_face(
        box.value().modeling_object_id, face);
    ASSERT_TRUE(material.ok()) << material.error_message();
    EXPECT_EQ(material.value().id, amber.material.id);
  }
  const auto untouched = editor.modeling().project_material_for_face(
      box.value().modeling_object_id, initial.faces[2].id);
  ASSERT_TRUE(untouched.ok()) << untouched.error_message();
  EXPECT_EQ(untouched.value().id, project_asset_id(kDefaultProjectMaterialId));
  ASSERT_TRUE(editor.history().undo().ok());
  expect_same_authored_document(
      *authored_document(harness.project(), box.value().entity), initial);
  const auto redo_before = editor.query().history();
  ASSERT_TRUE(redo_before.can_redo);
  const auto unchanged = editor.modeling().apply_material_to_selected_faces(
      QuaderMaterialReference::project(project_asset_id(kDefaultProjectMaterialId)),
      scene_id, editor.modeling().face_texture_settings().selection_token);
  ASSERT_TRUE(unchanged.ok()) << unchanged.error_message();
  EXPECT_FALSE(unchanged.changed());
  expect_same_history(editor.query().history(), redo_before);
  ASSERT_TRUE(editor.history().redo().ok());
  EXPECT_EQ(editor.modeling().project_material_for_face(
      box.value().modeling_object_id, first_face).value().id, amber.material.id);

  const auto captured_token = editor.modeling().face_texture_settings().selection_token;
  ASSERT_TRUE(editor.scene_documents().new_scene().ok());
  const auto replacement_history = editor.query().history();
  EXPECT_FALSE(editor.modeling().apply_material_to_selected_faces(
      reference, scene_id, captured_token).ok());
  expect_same_history(editor.query().history(), replacement_history);
  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Face).ok());
  const auto replacement_id = editor.modeling().face_texture_settings_revision().scene_session_id;
  EXPECT_FALSE(editor.modeling().apply_material_to_selected_faces(
      reference, replacement_id, editor.modeling().face_texture_settings().selection_token).ok());
}

TEST(ProjectMaterialBehaviorTests,
     SameFaceMaterialKeepsExistingAliasSlotAndRedoHistory) {
  ProjectMaterialBehaviorHarness harness("face-material-alias-no-op");
  harness.create();
  const InstalledMaterial amber{
      .path = project_path("Materials/amber.qmat"),
      .material = project_material(kAmberMaterialId, "Amber", {1.0f, 0.5f, 0.1f}),
  };
  const InstalledMaterial blue{
      .path = project_path("Materials/blue.qmat"),
      .material = project_material(kBlueMaterialId, "Blue", {0.1f, 0.2f, 1.0f}),
  };
  harness.install_and_reopen(std::array{amber, blue});
  auto &editor = harness.editor();
  const auto box = editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}}, "Alias Face");
  ASSERT_TRUE(box.ok());
  const auto face = authored_document(harness.project(), box.value().entity)->faces.front().id;
  MaterialComponent aliases{
      .material = MaterialReference::project(project_asset_id(kDefaultProjectMaterialId)),
      .material_slots = {{3u, MaterialReference::project(amber.material.id)},
                         {7u, MaterialReference::project(amber.material.id)}}};
  const ProjectMaterialFixture fixture(harness.project());
  ASSERT_TRUE(fixture.set_material(box.value().entity, aliases));
  ASSERT_TRUE(editor.modeling().assign_material_slot_to_face(box.value(), face, 7u).ok());
  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(box.value(), face).ok());
  ASSERT_TRUE(editor.modeling().apply_material(
      QuaderMaterialReference::project(blue.material.id)).ok());
  ASSERT_TRUE(editor.history().undo().ok());
  const auto document_before = *authored_document(harness.project(), box.value().entity);
  const auto history_before = editor.query().history();
  const auto revisions_before = editor.query().revisions();
  ASSERT_TRUE(history_before.can_redo);
  const auto scene_id = editor.modeling().face_texture_settings_revision().scene_session_id;
  const auto unchanged = editor.modeling().apply_material_to_selected_faces(
      QuaderMaterialReference::project(amber.material.id), scene_id,
      editor.modeling().face_texture_settings().selection_token);
  ASSERT_TRUE(unchanged.ok()) << unchanged.error_message();
  EXPECT_FALSE(unchanged.changed());
  const auto direct = editor.modeling().apply_material_to_face(
      box.value().modeling_object_id, face, QuaderMaterialReference::project(amber.material.id));
  ASSERT_TRUE(direct.ok()) << direct.error_message();
  EXPECT_FALSE(direct.changed());
  expect_same_history(editor.query().history(), history_before);
  expect_same_revisions(editor.query().revisions(), revisions_before);
  expect_same_authored_document(
      *authored_document(harness.project(), box.value().entity), document_before);
  EXPECT_EQ(*scene_material(harness.project(), box.value().entity), aliases);
}

TEST(ProjectMaterialBehaviorTests,
     FaceTexturePixelShiftUsesImageAxesAndPreservesMappingOnReplacement) {
  ProjectAssetId requested_material_id = project_asset_id(kDefaultProjectMaterialId);
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-texture-shift-test";
  char *argv[] = {application_name, nullptr};
  QCoreApplication application(argc, argv);
  QCoreApplication::addLibraryPath(QCoreApplication::applicationDirPath());
  ProjectMaterialBehaviorHarness harness("face-texture-pixel-shift");
  harness.create();
  const InstalledMaterial untextured{
      .path = project_path("Materials/untextured.qmat"),
      .material = project_material(kBlueMaterialId, "Untextured",
                                   {0.1f, 0.25f, 1.0f}),
  };
  const std::array installed{untextured};
  harness.install_and_reopen(installed);
  const std::filesystem::path wide_source = harness.root() / "wide.png";
  const std::filesystem::path replacement_source = harness.root() / "replacement.png";
  write_texture_png(wide_source, 128, 32);
  write_texture_png(replacement_source, 256, 96);
  const std::array sources{wide_source, replacement_source};
  const auto imported =
      harness.editor().project_assets().import_textures_as_materials(sources);
  ASSERT_TRUE(imported.ok()) << imported.error_message();
  ASSERT_EQ(imported.value().imported.size(), 2u);
  ASSERT_TRUE(imported.value().failed.empty());
  const ProjectAssetId wide = imported.value().imported[0].material.id;
  const ProjectAssetId replacement = imported.value().imported[1].material.id;
  requested_material_id = wide;
  const auto box = harness.editor().modeling().create_box({.bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}}, .name = "Pixel Shift Face",
           .material = QuaderMaterialReference::project(requested_material_id)});
  ASSERT_TRUE(box.ok()) << box.error_message();
  const quader_poly::Document *initial =
      authored_document(harness.project(), box.value().entity);
  ASSERT_NE(initial, nullptr);
  ASSERT_FALSE(initial->faces.empty());
  const quader_poly::ElementId face_id = initial->faces.front().id;
  ASSERT_TRUE(harness.editor().selection().set_mode(QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(harness.editor().selection().select_face(box.value(), face_id).ok());
  ASSERT_TRUE(edit_selected_face_texture(
                  harness.editor(), QuaderFaceTextureField::ShiftX, 32).ok());
  ASSERT_TRUE(edit_selected_face_texture(
                  harness.editor(), QuaderFaceTextureField::ShiftY, -8).ok());
  const auto original =
      face_texture_transform(harness.project(), box.value().entity, face_id);
  EXPECT_FLOAT_EQ(original.shift_uv.x, 0.25f);
  EXPECT_FLOAT_EQ(original.shift_uv.y, -0.25f);
  auto settings = harness.editor().modeling().face_texture_settings();
  EXPECT_DOUBLE_EQ(settings.shift_x, 32);
  EXPECT_DOUBLE_EQ(settings.shift_y, -8);

  ASSERT_TRUE(harness.editor().materials().replace_material_references(
                  QuaderMaterialReference::project(wide),
                  QuaderMaterialReference::project(replacement),
                  QuaderMaterialUsageScope::Selection).changed());
  EXPECT_EQ(face_texture_transform(harness.project(), box.value().entity, face_id),
            original);
  settings = harness.editor().modeling().face_texture_settings();
  EXPECT_DOUBLE_EQ(settings.shift_x, 64);
  EXPECT_DOUBLE_EQ(settings.shift_y, -24);
  ASSERT_TRUE(harness.editor().history().undo().ok());
  settings = harness.editor().modeling().face_texture_settings();
  EXPECT_DOUBLE_EQ(settings.shift_x, 32);
  EXPECT_DOUBLE_EQ(settings.shift_y, -8);
  ASSERT_TRUE(harness.editor().history().redo().ok());
  EXPECT_EQ(face_texture_transform(harness.project(), box.value().entity, face_id),
            original);

  ASSERT_TRUE(harness.editor().materials().replace_material_references(
                  QuaderMaterialReference::project(replacement),
                  QuaderMaterialReference::project(untextured.material.id),
                  QuaderMaterialUsageScope::Selection).changed());
  EXPECT_EQ(harness.project().material_texture_image(
                untextured.material.id, StandardMaterialTextureSlot::Albedo),
            nullptr);
  EXPECT_EQ(face_texture_transform(harness.project(), box.value().entity, face_id),
            original);
  settings = harness.editor().modeling().face_texture_settings();
  EXPECT_DOUBLE_EQ(settings.shift_x, 16);
  EXPECT_DOUBLE_EQ(settings.shift_y, -16);
  ASSERT_TRUE(edit_selected_face_texture(
                  harness.editor(), QuaderFaceTextureField::ShiftX, 8).ok());
  ASSERT_TRUE(edit_selected_face_texture(
                  harness.editor(), QuaderFaceTextureField::ShiftY, -4).ok());
  const auto fallback =
      face_texture_transform(harness.project(), box.value().entity, face_id);
  EXPECT_FLOAT_EQ(fallback.shift_uv.x, 0.125f);
  EXPECT_FLOAT_EQ(fallback.shift_uv.y, -0.0625f);
}

TEST(ProjectMaterialBehaviorTests,
     TextureImagesShareImmutableSnapshotsAndRefreshAfterMaterialSaveAndReopen) {
  ProjectAssetId requested_material_id = project_asset_id(kDefaultProjectMaterialId);
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-texture-cache-test";
  char *argv[] = {application_name, nullptr};
  QCoreApplication application(argc, argv);
  QCoreApplication::addLibraryPath(QCoreApplication::applicationDirPath());
  ProjectMaterialBehaviorHarness harness("shared-texture-snapshot");
  harness.create();
  const std::filesystem::path source = harness.root() / "original.png";
  const std::filesystem::path replacement_source = harness.root() / "larger.png";
  write_texture_png(source, 128, 32);
  write_texture_png(replacement_source, 256, 96);
  const std::array sources{source};
  const auto imported =
      harness.editor().project_assets().import_textures_as_materials(sources);
  ASSERT_TRUE(imported.ok()) << imported.error_message();
  ASSERT_EQ(imported.value().imported.size(), 1u);
  const ProjectAssetId material = imported.value().imported.front().material.id;
  const auto first = harness.project().material_texture_image(
      material, StandardMaterialTextureSlot::Albedo);
  ASSERT_NE(first, nullptr);
  EXPECT_EQ(first->width, 128u);
  EXPECT_EQ(first->height, 32u);
  ASSERT_NE(first->encoded, nullptr);
  ASSERT_NE(first->decoded, nullptr);
  EXPECT_FALSE(first->encoded->empty());
  EXPECT_EQ(first->decoded->size(), 128u * 32u * 4u);
  EXPECT_NE(first->content_revision, 0u);
  EXPECT_TRUE(first->texture_id.valid());
  EXPECT_EQ(first->project_relative_path, "Textures/original.png");
  EXPECT_EQ(first->detected_format, "png");
  EXPECT_EQ(first->encoded_byte_size, first->encoded->size());
  const auto albedo_info =
      harness.project().material_albedo_texture_info(material);
  ASSERT_TRUE(albedo_info.ok()) << albedo_info.error().message;
  EXPECT_EQ(albedo_info.value().material_id, material);
  EXPECT_EQ(albedo_info.value().texture, first);
  EXPECT_EQ(albedo_info.value().project_relative_path.portable_string(),
            "Textures/original.png");
  EXPECT_EQ(albedo_info.value().absolute_path,
            harness.root() / "Textures" / "original.png");

  requested_material_id = material;
  const auto box = harness.editor().modeling().create_box({.bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}}, .name = "Texture Cache Face",
           .material = QuaderMaterialReference::project(requested_material_id)});
  ASSERT_TRUE(box.ok()) << box.error_message();
  const quader_poly::Document *document =
      authored_document(harness.project(), box.value().entity);
  ASSERT_NE(document, nullptr);
  ASSERT_FALSE(document->faces.empty());
  const quader_poly::ElementId face_id = document->faces.front().id;
  ASSERT_TRUE(harness.editor().selection().set_mode(QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(harness.editor().selection().select_face(box.value(), face_id).ok());
  ASSERT_TRUE(edit_selected_face_texture(
                  harness.editor(), QuaderFaceTextureField::ShiftX, 32).ok());
  ASSERT_TRUE(edit_selected_face_texture(
                  harness.editor(), QuaderFaceTextureField::ShiftY, -8).ok());
  const auto mapping =
      face_texture_transform(harness.project(), box.value().entity, face_id);
  const auto scene = harness.editor().scene_documents().active_scene_id();
  ASSERT_TRUE(scene.has_value());
  ASSERT_TRUE(harness.editor().scene_documents().save_as(
                  *scene, project_path("Scenes/texture_cache.qdr")).ok());
  const auto texture_revision = harness.project().material_texture_revision();
  const auto face_revision = harness.editor().modeling().face_texture_settings_revision();
  const auto history = harness.editor().query().history();
  const bool dirty = harness.editor().project().dirty();
  for (int frame = 0; frame < 64; ++frame) {
    EXPECT_EQ(harness.project().material_texture_image(
                  material, StandardMaterialTextureSlot::Albedo), first);
    const auto settings = harness.editor().modeling().face_texture_settings();
    EXPECT_DOUBLE_EQ(settings.shift_x, 32);
    EXPECT_DOUBLE_EQ(settings.shift_y, -8);
  }
  EXPECT_EQ(harness.project().material_texture_revision(), texture_revision);
  EXPECT_EQ(harness.editor().modeling().face_texture_settings_revision(), face_revision);
  expect_same_history(harness.editor().query().history(), history);
  EXPECT_EQ(harness.editor().project().dirty(), dirty);

  auto &material_editor = harness.editor().standard_material_documents();
  ASSERT_TRUE(material_editor.open(material).ok());
  ASSERT_TRUE(material_editor.import_texture(
                  QuaderStandardMaterialTextureSlot::Albedo, replacement_source).ok());
  ASSERT_TRUE(material_editor.save().ok());
  EXPECT_GT(harness.project().material_texture_revision(), texture_revision);
  const auto updated = harness.project().material_texture_image(
      material, StandardMaterialTextureSlot::Albedo);
  ASSERT_NE(updated, nullptr);
  EXPECT_NE(updated, first);
  EXPECT_EQ(updated->width, 256u);
  EXPECT_EQ(updated->height, 96u);
  EXPECT_EQ(first->width, 128u);
  EXPECT_EQ(first->height, 32u);
  EXPECT_EQ(first->decoded->size(), 128u * 32u * 4u);
  const auto settings = harness.editor().modeling().face_texture_settings();
  EXPECT_DOUBLE_EQ(settings.shift_x, 64);
  EXPECT_DOUBLE_EQ(settings.shift_y, -24);
  EXPECT_EQ(face_texture_transform(harness.project(), box.value().entity, face_id), mapping);
  const auto updated_revision = harness.project().material_texture_revision();

  ASSERT_TRUE(harness.editor().project().close().ok());
  EXPECT_EQ(harness.project().material_texture_image(
                material, StandardMaterialTextureSlot::Albedo), nullptr);
  ASSERT_TRUE(harness.editor().project().open(harness.manifest_path()).ok());
  EXPECT_GT(harness.project().material_texture_revision(), updated_revision);
  const auto reopened = harness.project().material_texture_image(
      material, StandardMaterialTextureSlot::Albedo);
  ASSERT_NE(reopened, nullptr);
  EXPECT_NE(reopened, updated);
  EXPECT_EQ(reopened->width, 256u);
  EXPECT_EQ(reopened->height, 96u);
  EXPECT_EQ(harness.project().material_texture_image(
                material, StandardMaterialTextureSlot::Albedo), reopened);
}

TEST(ProjectMaterialBehaviorTests,
     FaceApplyReusesLowestSlotAndObjectApplyClearsFaceOverrides) {
  ProjectAssetId requested_material_id = project_asset_id(kDefaultProjectMaterialId);
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
  requested_material_id = amber.material.id;
  const QuaderApiResult<QuaderModelingObjectRef> box =
      harness.editor().modeling().create_box({.bounds = {.min = {-1.0f, -1.0f, -1.0f},
           .max = {1.0f, 1.0f, 1.0f}}, .name = "Face Material Box",
           .material = QuaderMaterialReference::project(requested_material_id)});
  ASSERT_TRUE(box.ok()) << box.error_message();
  const quader_poly::Document *initial =
      authored_document(harness.project(), box.value().entity);
  ASSERT_NE(initial, nullptr);
  ASSERT_GE(initial->faces.size(), 2u);
  const quader_poly::ElementId first_face = initial->faces[0].id;
  const quader_poly::ElementId second_face = initial->faces[1].id;

  requested_material_id = blue.material.id;
  ASSERT_TRUE(harness.editor()
                  .selection()
                  .set_mode(QuaderSelectionMode::Face)
                  .ok());
  ASSERT_TRUE(harness.editor()
                  .selection()
                  .select_face(box.value(), first_face)
                  .ok());
  ASSERT_TRUE(harness.editor().modeling().apply_material(QuaderMaterialReference::project(requested_material_id)).ok());

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
  ASSERT_TRUE(harness.editor().modeling().apply_material(QuaderMaterialReference::project(requested_material_id)).ok());
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
  requested_material_id = built_in;
  ASSERT_TRUE(harness.editor()
                  .selection()
                  .set_mode(QuaderSelectionMode::Object)
                  .ok());
  ASSERT_TRUE(harness.editor().selection().select_object(box.value()).ok());
  ASSERT_TRUE(harness.editor().modeling().apply_material(QuaderMaterialReference::project(requested_material_id)).ok());
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

  requested_material_id = amber.material.id;
  ASSERT_TRUE(harness.editor().history().redo().ok());
  expect_primary_material(harness.project(), box.value().entity, built_in);

  ASSERT_TRUE(harness.editor()
                  .selection()
                  .set_mode(QuaderSelectionMode::Vertex)
                  .ok());
  const QuaderApiVoidResult vertex_rejected =
      harness.editor().modeling().apply_material(QuaderMaterialReference::project(requested_material_id));
  EXPECT_FALSE(vertex_rejected.ok());
  EXPECT_NE(vertex_rejected.error_message().find("object, group, or face"),
            std::string::npos);
  ASSERT_TRUE(harness.editor()
                  .selection()
                  .set_mode(QuaderSelectionMode::Edge)
                  .ok());
  EXPECT_FALSE(harness.editor().modeling().apply_material(QuaderMaterialReference::project(requested_material_id)).ok());
}

TEST(ProjectMaterialBehaviorTests,
     FaceMaterialCanNormalizeOnlyItsMeshWithExactUndoAndRedo) {
  ProjectAssetId requested_material_id = project_asset_id(kDefaultProjectMaterialId);
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
  requested_material_id = amber.material.id;

  const QuaderApiResult<QuaderModelingObjectRef> target =
      harness.editor().modeling().create_box({.bounds = {.min = {-3.0f, -1.0f, -1.0f},
           .max = {-1.0f, 1.0f, 1.0f}}, .name = "Target",
           .material = QuaderMaterialReference::project(requested_material_id)});
  const QuaderApiResult<QuaderModelingObjectRef> other =
      harness.editor().modeling().create_box({.bounds = {.min = {1.0f, -1.0f, -1.0f},
           .max = {3.0f, 1.0f, 1.0f}}, .name = "Other",
           .material = QuaderMaterialReference::project(requested_material_id)});
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

  requested_material_id = blue.material.id;
  ASSERT_TRUE(harness.editor()
                  .selection()
                  .set_mode(QuaderSelectionMode::Face)
                  .ok());
  ASSERT_TRUE(harness.editor()
                  .selection()
                  .select_face(target.value(), sampled_face)
                  .ok());
  ASSERT_TRUE(harness.editor().modeling().apply_material(QuaderMaterialReference::project(requested_material_id)).ok());
  target_document =
      authored_document(harness.project(), target.value().entity);
  ASSERT_NE(target_document, nullptr);
  ASSERT_NE(quader_poly::find_face(*target_document, sampled_face), nullptr);
  EXPECT_NE(quader_poly::find_face(*target_document, sampled_face)
                ->material_slot,
            0u)
      << "the sampled material must resolve through a nonzero override slot";

  requested_material_id = amber.material.id;
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
  EXPECT_EQ(harness.editor().query().history().undo_label,
            "Apply Face Material to Mesh");

  const QuaderApiResult<QuaderModelingObjectRef> transient =
      harness.editor().modeling().create_box({.bounds = {.min = {5.0f, -1.0f, -1.0f},
           .max = {7.0f, 1.0f, 1.0f}}, .name = "Transient Redo Guard",
           .material = QuaderMaterialReference::project(requested_material_id)});
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
  requested_material_id = built_in;
  ASSERT_TRUE(harness.editor().history().redo().ok());
  expect_primary_material(harness.project(), target.value().entity,
                          blue.material.id);
  expect_all_face_slots(harness.project(), target.value().entity, 0u);
  const MaterialComponent *redone =
      scene_material(harness.project(), target.value().entity);
  ASSERT_NE(redone, nullptr);
  EXPECT_TRUE(redone->material_slots.empty());
  expect_same_selection_identity(exact_selection(harness.project()),
                                 selection_before);
}

TEST(ProjectMaterialBehaviorTests,
     FaceProjectMaterialRejectsNonProjectAndNonEditableTargetsWithoutHistory) {
  ProjectAssetId requested_material_id = project_asset_id(kDefaultProjectMaterialId);
  ProjectMaterialBehaviorHarness harness("face-material-rejections");
  harness.create();
  const QuaderApiResult<QuaderModelingObjectRef> box =
      harness.editor().modeling().create_box({.bounds = {.min = {-1.0f, -1.0f, -1.0f},
           .max = {1.0f, 1.0f, 1.0f}}, .name = "Rejected Target",
           .material = QuaderMaterialReference::project(requested_material_id)});
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
      harness.editor().modeling().create_box({.bounds = {.min = {4.0f, -1.0f, -1.0f},
           .max = {6.0f, 1.0f, 1.0f}}, .name = "Rejected Redo Guard",
           .material = QuaderMaterialReference::project(requested_material_id)});
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
  EXPECT_FALSE(harness.editor()
                   .modeling()
                   .apply_material_to_mesh(
                       box.value().modeling_object_id, face, QuaderMaterialReference::project(requested_material_id))
                   .ok());
  EXPECT_FALSE(harness.editor()
                   .modeling()
                   .apply_material_to_face(
                       box.value().modeling_object_id, face, QuaderMaterialReference::project(requested_material_id))
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
  EXPECT_FALSE(harness.editor()
                   .modeling()
                   .apply_material_to_mesh(
                       box.value().modeling_object_id, face, QuaderMaterialReference::project(requested_material_id))
                   .ok());
  EXPECT_FALSE(harness.editor()
                   .modeling()
                   .apply_material_to_face(
                       box.value().modeling_object_id, face, QuaderMaterialReference::project(requested_material_id))
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

TEST(ProjectMaterialReferenceBehaviorTests,
     SelectionAndActiveSceneReplacementDeduplicateAndRestoreExactState) {
  ProjectMaterialBehaviorHarness harness("material-reference-selection");
  harness.create();
  const InstalledMaterial amber{
      .path = project_path("Materials/amber.qmat"),
      .material = project_material(kAmberMaterialId, "Amber",
                                   {1.0f, 0.5f, 0.1f}),
  };
  const InstalledMaterial blue{
      .path = project_path("Materials/blue.qmat"),
      .material = project_material(kBlueMaterialId, "Blue",
                                   {0.1f, 0.25f, 1.0f}),
  };
  const std::array installed{amber, blue};
  harness.install_and_reopen(installed);

  const QuaderApiResult<QuaderModelingObjectRef> first =
      harness.editor().modeling().create_box({
          .bounds = {.min = {-3.0f, -1.0f, -1.0f}, .max = {-1.0f, 1.0f, 1.0f}},
          .name = "First Material Reference Mesh",
          .material = QuaderMaterialReference::project(amber.material.id),
      });
  const QuaderApiResult<QuaderModelingObjectRef> second =
      harness.editor().modeling().create_box({
          .bounds = {.min = {1.0f, -1.0f, -1.0f}, .max = {3.0f, 1.0f, 1.0f}},
          .name = "Second Material Reference Mesh",
          .material = QuaderMaterialReference::project(amber.material.id),
      });
  ASSERT_TRUE(first.ok()) << first.error_message();
  ASSERT_TRUE(second.ok()) << second.error_message();

  const quader_poly::Document *first_document =
      authored_document(harness.project(), first.value().entity);
  ASSERT_NE(first_document, nullptr);
  ASSERT_FALSE(first_document->faces.empty());
  const quader_poly::ElementId blue_face = first_document->faces.front().id;
  MaterialComponent first_component;
  first_component.material = MaterialReference::project(amber.material.id);
  first_component.material_slots.emplace(
      7u, MaterialReference::project(blue.material.id));
  first_component.material_slots.emplace(
      9u, MaterialReference::project(amber.material.id));
  constexpr AssetId kUnusedMissingMaterial = 0x7130u;
  first_component.material_slots.emplace(
      11u, MaterialReference(kUnusedMissingMaterial));
  const ProjectMaterialFixture fixture(harness.project());
  ASSERT_TRUE(fixture.set_material(first.value().entity, first_component));
  ASSERT_TRUE(harness.editor()
                  .modeling()
                  .mesh(first.value())
                  .materials()
                  .assign_to_face(blue_face, 7u)
                  .ok());
  ASSERT_TRUE(harness.editor().selection().set_mode(QuaderSelectionMode::Object)
                  .ok());
  ASSERT_TRUE(harness.editor()
                  .selection()
                  .select_objects({first.value(), second.value()},
                                  QuaderSelectionEdit::Replace)
                  .ok());

  const QuaderApiResult<std::vector<QuaderMaterialReference>> used =
      harness.editor().materials().used_materials(
          QuaderMaterialUsageScope::Selection);
  ASSERT_TRUE(used.ok()) << used.error_message();
  ASSERT_EQ(used.value().size(), 2u);
  EXPECT_NE(std::ranges::find(
                used.value(),
                QuaderMaterialReference::project(amber.material.id)),
            used.value().end());
  EXPECT_NE(std::ranges::find(
                used.value(),
                QuaderMaterialReference::project(blue.material.id)),
            used.value().end());

  const auto opened_draft =
      harness.editor().standard_material_documents().open(amber.material.id);
  ASSERT_TRUE(opened_draft.ok()) << opened_draft.error_message();
  ASSERT_TRUE(harness.editor()
                  .standard_material_documents()
                  .set_roughness(0.37f)
                  .ok());
  const auto draft_before =
      harness.editor().standard_material_documents().current_revision();
  ASSERT_TRUE(draft_before.ok()) << draft_before.error_message();
  ASSERT_TRUE(draft_before.value().dirty);

  const MaterialComponent first_before =
      *scene_material(harness.project(), first.value().entity);
  const MaterialComponent second_before =
      *scene_material(harness.project(), second.value().entity);
  const std::vector<std::pair<quader_poly::ElementId, std::uint32_t>>
      first_slots_before =
          face_slot_snapshot(harness.project(), first.value().entity);
  const ModelingSelectionExport selection_before =
      exact_selection(harness.project());
  const auto manifest_before = read_bytes(harness.manifest_path());

  const QuaderApiVoidResult replaced =
      harness.editor().materials().replace_material_references(
          QuaderMaterialReference::project(amber.material.id),
          QuaderMaterialReference::project(blue.material.id),
          QuaderMaterialUsageScope::Selection);
  ASSERT_TRUE(replaced.ok()) << replaced.error_message();
  ASSERT_TRUE(replaced.changed());
  ASSERT_NE(scene_material(harness.project(), first.value().entity), nullptr);
  ASSERT_NE(scene_material(harness.project(), second.value().entity), nullptr);
  EXPECT_EQ(scene_material(harness.project(), first.value().entity)->material,
            MaterialReference::project(blue.material.id));
  EXPECT_EQ(scene_material(harness.project(), second.value().entity)->material,
            MaterialReference::project(blue.material.id));
  EXPECT_EQ(scene_material(harness.project(), first.value().entity)
                ->material_slots.at(9u),
            MaterialReference::project(blue.material.id));
  EXPECT_EQ(scene_material(harness.project(), first.value().entity)
                ->material_slots.at(11u),
            MaterialReference(kUnusedMissingMaterial));
  EXPECT_EQ(face_slot_snapshot(harness.project(), first.value().entity),
            first_slots_before);
  expect_same_selection_identity(exact_selection(harness.project()),
                                 selection_before);
  EXPECT_EQ(read_bytes(harness.manifest_path()), manifest_before);
  const auto draft_after_replace =
      harness.editor().standard_material_documents().current_revision();
  ASSERT_TRUE(draft_after_replace.ok())
      << draft_after_replace.error_message();
  EXPECT_EQ(draft_after_replace.value().session_id,
            draft_before.value().session_id);
  EXPECT_EQ(draft_after_replace.value().id, draft_before.value().id);
  EXPECT_EQ(draft_after_replace.value().draft_revision,
            draft_before.value().draft_revision);
  EXPECT_EQ(draft_after_replace.value().clean_revision,
            draft_before.value().clean_revision);
  EXPECT_EQ(draft_after_replace.value().dirty, draft_before.value().dirty);
  const MaterialComponent first_after =
      *scene_material(harness.project(), first.value().entity);
  const MaterialComponent second_after =
      *scene_material(harness.project(), second.value().entity);

  ASSERT_TRUE(harness.editor().history().undo().ok());
  EXPECT_EQ(*scene_material(harness.project(), first.value().entity),
            first_before);
  EXPECT_EQ(*scene_material(harness.project(), second.value().entity),
            second_before);
  EXPECT_EQ(face_slot_snapshot(harness.project(), first.value().entity),
            first_slots_before);
  expect_same_selection_identity(exact_selection(harness.project()),
                                 selection_before);
  ASSERT_TRUE(harness.editor().history().redo().ok());
  EXPECT_EQ(*scene_material(harness.project(), first.value().entity),
            first_after);
  EXPECT_EQ(*scene_material(harness.project(), second.value().entity),
            second_after);
  const auto draft_after_redo =
      harness.editor().standard_material_documents().current_revision();
  ASSERT_TRUE(draft_after_redo.ok()) << draft_after_redo.error_message();
  EXPECT_EQ(draft_after_redo.value().session_id,
            draft_before.value().session_id);
  EXPECT_EQ(draft_after_redo.value().draft_revision,
            draft_before.value().draft_revision);
  EXPECT_EQ(draft_after_redo.value().dirty, draft_before.value().dirty);

  const QuaderHistorySnapshot history_before_same =
      harness.editor().query().history();
  const QuaderApiVoidResult same =
      harness.editor().materials().replace_material_references(
          QuaderMaterialReference::project(blue.material.id),
          QuaderMaterialReference::project(blue.material.id),
          QuaderMaterialUsageScope::Selection);
  ASSERT_TRUE(same.ok()) << same.error_message();
  EXPECT_FALSE(same.changed());
  expect_same_history(harness.editor().query().history(), history_before_same);

  const QuaderHistorySnapshot history_before_missing =
      harness.editor().query().history();
  const QuaderApiVoidResult missing =
      harness.editor().materials().replace_material_references(
          QuaderMaterialReference::project(ProjectAssetId::generate()),
          QuaderMaterialReference::project(blue.material.id),
          QuaderMaterialUsageScope::Selection);
  ASSERT_TRUE(missing.ok()) << missing.error_message();
  EXPECT_FALSE(missing.changed());
  expect_same_history(harness.editor().query().history(),
                      history_before_missing);

  ASSERT_TRUE(harness.editor().history().undo().ok());
  ASSERT_TRUE(fixture.set_locked(second.value().entity, true));
  const QuaderHistorySnapshot history_before_locked =
      harness.editor().query().history();
  const QuaderApiVoidResult locked =
      harness.editor().materials().replace_material_references(
          QuaderMaterialReference::project(amber.material.id),
          QuaderMaterialReference::project(blue.material.id),
          QuaderMaterialUsageScope::Selection);
  EXPECT_FALSE(locked.ok());
  EXPECT_FALSE(locked.changed());
  EXPECT_EQ(*scene_material(harness.project(), first.value().entity),
            first_before);
  EXPECT_EQ(*scene_material(harness.project(), second.value().entity),
            second_before);
  expect_same_history(harness.editor().query().history(),
                      history_before_locked);
  ASSERT_TRUE(fixture.set_locked(second.value().entity, false));

  ASSERT_TRUE(fixture.set_selectable(second.value().entity, false));
  const QuaderHistorySnapshot history_before_unselectable =
      harness.editor().query().history();
  const QuaderApiVoidResult unselectable =
      harness.editor().materials().replace_material_references(
          QuaderMaterialReference::project(amber.material.id),
          QuaderMaterialReference::project(blue.material.id),
          QuaderMaterialUsageScope::Selection);
  EXPECT_FALSE(unselectable.ok());
  EXPECT_FALSE(unselectable.changed());
  EXPECT_EQ(*scene_material(harness.project(), first.value().entity),
            first_before);
  EXPECT_EQ(*scene_material(harness.project(), second.value().entity),
            second_before);
  expect_same_history(harness.editor().query().history(),
                      history_before_unselectable);
  ASSERT_TRUE(fixture.set_selectable(second.value().entity, true));

  ASSERT_TRUE(fixture.set_visible(second.value().entity, false));
  ASSERT_TRUE(harness.editor().selection().select_object(first.value()).ok());
  const QuaderApiVoidResult scene_replaced =
      harness.editor().materials().replace_material_references(
          QuaderMaterialReference::project(amber.material.id),
          QuaderMaterialReference::project(blue.material.id),
          QuaderMaterialUsageScope::ActiveScene);
  ASSERT_TRUE(scene_replaced.ok()) << scene_replaced.error_message();
  ASSERT_TRUE(scene_replaced.changed());
  EXPECT_EQ(scene_material(harness.project(), first.value().entity)->material,
            MaterialReference::project(blue.material.id));
  EXPECT_EQ(scene_material(harness.project(), second.value().entity)->material,
            MaterialReference::project(blue.material.id));
}

TEST(ProjectMaterialReferenceBehaviorTests,
     SolidifiedCurvesExposeOnlyTheirPrimaryMaterialBinding) {
  ProjectMaterialBehaviorHarness harness("material-reference-solid-curve");
  harness.create();
  const InstalledMaterial amber{
      .path = project_path("Materials/amber.qmat"),
      .material = project_material(kAmberMaterialId, "Amber",
                                   {1.0f, 0.5f, 0.1f}),
  };
  const InstalledMaterial blue{
      .path = project_path("Materials/blue.qmat"),
      .material = project_material(kBlueMaterialId, "Blue",
                                   {0.1f, 0.25f, 1.0f}),
  };
  const std::array installed{amber, blue};
  harness.install_and_reopen(installed);

  const QuaderApiResult<QuaderModelingObjectRef> solid_curve =
      harness.editor().modeling().create_curve({
          .points = {{.position = {0.0f, 0.0f, 0.0f}},
                     {.position = {3.0f, 0.0f, 0.0f}}},
          .name = "Solid Material Curve",
      });
  ASSERT_TRUE(solid_curve.ok()) << solid_curve.error_message();
  ASSERT_TRUE(harness.editor().selection().set_mode(QuaderSelectionMode::Object)
                  .ok());
  ASSERT_TRUE(
      harness.editor().selection().select_object(solid_curve.value()).ok());
  ASSERT_TRUE(harness.editor()
                  .tools()
                  .set_setting("curve", "curve.solidify", true)
                  .ok());

  const QuaderApiResult<QuaderModelingObjectRef> dormant_curve =
      harness.editor().modeling().create_curve({
          .points = {{.position = {0.0f, 2.0f, 0.0f}},
                     {.position = {3.0f, 2.0f, 0.0f}}},
          .name = "Dormant Material Curve",
      });
  ASSERT_TRUE(dormant_curve.ok()) << dormant_curve.error_message();
  const ProjectMaterialFixture fixture(harness.project());
  const MaterialComponent solid_before{
      .material = MaterialReference::project(amber.material.id),
      .material_slots = {
          {7u, MaterialReference::project(blue.material.id)}},
  };
  ASSERT_TRUE(
      fixture.set_material(solid_curve.value().entity, solid_before));
  ASSERT_TRUE(fixture.set_material(
      dormant_curve.value().entity,
      MaterialComponent{
          .material = MaterialReference::project(blue.material.id)}));
  ASSERT_TRUE(
      harness.editor().selection().select_object(solid_curve.value()).ok());

  const QuaderApiResult<std::vector<QuaderMaterialReference>> selected_used =
      harness.editor().materials().used_materials(
          QuaderMaterialUsageScope::Selection);
  ASSERT_TRUE(selected_used.ok()) << selected_used.error_message();
  ASSERT_EQ(selected_used.value().size(), 1u);
  EXPECT_EQ(selected_used.value().front(),
            QuaderMaterialReference::project(amber.material.id));
  const QuaderApiResult<std::vector<QuaderMaterialReference>> scene_used =
      harness.editor().materials().used_materials(
          QuaderMaterialUsageScope::ActiveScene);
  ASSERT_TRUE(scene_used.ok()) << scene_used.error_message();
  ASSERT_EQ(scene_used.value().size(), 1u);
  EXPECT_EQ(scene_used.value().front(),
            QuaderMaterialReference::project(amber.material.id));

  const QuaderHistorySnapshot history_before_slot_only =
      harness.editor().query().history();
  const QuaderApiVoidResult slot_only =
      harness.editor().materials().replace_material_references(
          QuaderMaterialReference::project(blue.material.id),
          QuaderMaterialReference::project(amber.material.id),
          QuaderMaterialUsageScope::Selection);
  ASSERT_TRUE(slot_only.ok()) << slot_only.error_message();
  EXPECT_FALSE(slot_only.changed());
  EXPECT_EQ(*scene_material(harness.project(), solid_curve.value().entity),
            solid_before);
  expect_same_history(harness.editor().query().history(),
                      history_before_slot_only);

  const QuaderApiVoidResult replaced =
      harness.editor().materials().replace_material_references(
          QuaderMaterialReference::project(amber.material.id),
          QuaderMaterialReference::project(blue.material.id),
          QuaderMaterialUsageScope::Selection);
  ASSERT_TRUE(replaced.ok()) << replaced.error_message();
  ASSERT_TRUE(replaced.changed());
  const MaterialComponent *solid_after =
      scene_material(harness.project(), solid_curve.value().entity);
  ASSERT_NE(solid_after, nullptr);
  EXPECT_EQ(solid_after->material,
            MaterialReference::project(blue.material.id));
  EXPECT_EQ(solid_after->material_slots, solid_before.material_slots);
  ASSERT_TRUE(harness.editor().history().undo().ok());
  EXPECT_EQ(*scene_material(harness.project(), solid_curve.value().entity),
            solid_before);
  ASSERT_TRUE(harness.editor().history().redo().ok());
  EXPECT_EQ(scene_material(harness.project(), solid_curve.value().entity)
                ->material,
            MaterialReference::project(blue.material.id));
}

TEST(ProjectMaterialReferenceBehaviorTests,
     SolidifiedCurveGroupScopePreservesLockPolicy) {
  ProjectMaterialBehaviorHarness harness(
      "material-reference-solid-curve-group");
  harness.create();
  const InstalledMaterial amber{
      .path = project_path("Materials/amber.qmat"),
      .material = project_material(kAmberMaterialId, "Amber",
                                   {1.0f, 0.5f, 0.1f}),
  };
  const InstalledMaterial blue{
      .path = project_path("Materials/blue.qmat"),
      .material = project_material(kBlueMaterialId, "Blue",
                                   {0.1f, 0.25f, 1.0f}),
  };
  const std::array installed{amber, blue};
  harness.install_and_reopen(installed);
  const QuaderApiResult<QuaderModelingObjectRef> curve =
      harness.editor().modeling().create_curve({
          .points = {{.position = {0.0f, 0.0f, 0.0f}},
                     {.position = {3.0f, 0.0f, 0.0f}}},
          .name = "Grouped Solid Material Curve",
      });
  ASSERT_TRUE(curve.ok()) << curve.error_message();
  ASSERT_TRUE(harness.editor().selection().set_mode(QuaderSelectionMode::Object)
                  .ok());
  ASSERT_TRUE(harness.editor().selection().select_object(curve.value()).ok());
  ASSERT_TRUE(harness.editor()
                  .tools()
                  .set_setting("curve", "curve.solidify", true)
                  .ok());
  const ProjectMaterialFixture fixture(harness.project());
  ASSERT_TRUE(fixture.set_material(
      curve.value().entity,
      MaterialComponent{
          .material = MaterialReference::project(amber.material.id)}));
  const QuaderApiResult<QuaderEntityRef> group =
      harness.editor().scene().group_selection("Solid Material Curve Group");
  ASSERT_TRUE(group.ok()) << group.error_message();
  ASSERT_TRUE(harness.editor().selection().set_mode(QuaderSelectionMode::Group)
                  .ok());
  ASSERT_TRUE(harness.editor().selection().select_object(group.value().id)
                  .ok());

  const QuaderApiResult<std::vector<QuaderMaterialReference>> used =
      harness.editor().materials().used_materials(
          QuaderMaterialUsageScope::Selection);
  ASSERT_TRUE(used.ok()) << used.error_message();
  ASSERT_EQ(used.value().size(), 1u);
  EXPECT_EQ(used.value().front(),
            QuaderMaterialReference::project(amber.material.id));

  ASSERT_TRUE(fixture.set_locked(group.value().id, true));
  const QuaderHistorySnapshot history_before_locked =
      harness.editor().query().history();
  const QuaderApiVoidResult locked =
      harness.editor().materials().replace_material_references(
          QuaderMaterialReference::project(amber.material.id),
          QuaderMaterialReference::project(blue.material.id),
          QuaderMaterialUsageScope::Selection);
  EXPECT_FALSE(locked.ok());
  EXPECT_FALSE(locked.changed());
  EXPECT_EQ(scene_material(harness.project(), curve.value().entity)->material,
            MaterialReference::project(amber.material.id));
  expect_same_history(harness.editor().query().history(),
                      history_before_locked);
  ASSERT_TRUE(fixture.set_locked(group.value().id, false));

  const QuaderApiVoidResult replaced =
      harness.editor().materials().replace_material_references(
          QuaderMaterialReference::project(amber.material.id),
          QuaderMaterialReference::project(blue.material.id),
          QuaderMaterialUsageScope::Selection);
  ASSERT_TRUE(replaced.ok()) << replaced.error_message();
  ASSERT_TRUE(replaced.changed());
  EXPECT_EQ(scene_material(harness.project(), curve.value().entity)->material,
            MaterialReference::project(blue.material.id));
  ASSERT_TRUE(harness.editor().history().undo().ok());
  EXPECT_EQ(scene_material(harness.project(), curve.value().entity)->material,
            MaterialReference::project(amber.material.id));
}

TEST(ProjectMaterialReferenceBehaviorTests,
     FaceScopeIncludesMissingReferencesAndChangesOnlyMatchingSelectedFaces) {
  ProjectMaterialBehaviorHarness harness("material-reference-face");
  harness.create();
  const InstalledMaterial blue{
      .path = project_path("Materials/blue.qmat"),
      .material = project_material(kBlueMaterialId, "Blue",
                                   {0.1f, 0.25f, 1.0f}),
  };
  const std::array installed{blue};
  harness.install_and_reopen(installed);
  const QuaderApiResult<QuaderModelingObjectRef> box =
      harness.editor().modeling().create_box(
          {.min = {-1.0f, -1.0f, -1.0f},
           .max = {1.0f, 1.0f, 1.0f}},
          "Missing Material Face Mesh");
  ASSERT_TRUE(box.ok()) << box.error_message();
  const quader_poly::Document *document =
      authored_document(harness.project(), box.value().entity);
  ASSERT_NE(document, nullptr);
  ASSERT_GE(document->faces.size(), 3u);
  const QuaderElementId first_face = document->faces[0].id;
  const QuaderElementId second_face = document->faces[1].id;
  const QuaderElementId unselected_face = document->faces[2].id;
  constexpr AssetId kMissingEmbeddedMaterial = 0x7f31u;
  const ProjectMaterialFixture fixture(harness.project());
  MaterialComponent missing_component{
      .material = MaterialReference(kMissingEmbeddedMaterial),
  };
  missing_component.material_slots.emplace(
      7u, MaterialReference::project(blue.material.id));
  ASSERT_TRUE(fixture.set_material(box.value().entity, missing_component));
  ASSERT_TRUE(harness.editor()
                  .modeling()
                  .mesh(box.value())
                  .materials()
                  .assign_to_face(second_face, 7u)
                  .ok());
  ASSERT_TRUE(harness.editor().selection().set_mode(QuaderSelectionMode::Face)
                  .ok());
  ASSERT_TRUE(harness.editor()
                  .selection()
                  .select_faces(box.value(), {first_face, second_face})
                  .ok());

  const QuaderApiResult<std::vector<QuaderMaterialReference>> used =
      harness.editor().materials().used_materials(
          QuaderMaterialUsageScope::Selection);
  ASSERT_TRUE(used.ok()) << used.error_message();
  ASSERT_EQ(used.value().size(), 2u);
  EXPECT_NE(std::ranges::find(
                used.value(), QuaderMaterialReference::embedded(
                                  kMissingEmbeddedMaterial)),
            used.value().end());
  EXPECT_NE(std::ranges::find(
                used.value(),
                QuaderMaterialReference::project(blue.material.id)),
            used.value().end());
  const MaterialComponent component_before =
      *scene_material(harness.project(), box.value().entity);
  const std::vector<std::pair<quader_poly::ElementId, std::uint32_t>>
      slots_before = face_slot_snapshot(harness.project(), box.value().entity);
  const ModelingSelectionExport selection_before =
      exact_selection(harness.project());

  const ProjectAssetId invalid_destination = ProjectAssetId::generate();
  const QuaderHistorySnapshot history_before_invalid =
      harness.editor().query().history();
  const QuaderApiVoidResult invalid =
      harness.editor().materials().replace_material_references(
          QuaderMaterialReference::embedded(kMissingEmbeddedMaterial),
          QuaderMaterialReference::project(invalid_destination),
          QuaderMaterialUsageScope::Selection);
  EXPECT_FALSE(invalid.ok());
  EXPECT_FALSE(invalid.changed());
  expect_same_history(harness.editor().query().history(),
                      history_before_invalid);

  const QuaderApiVoidResult replaced =
      harness.editor().materials().replace_material_references(
          QuaderMaterialReference::embedded(kMissingEmbeddedMaterial),
          QuaderMaterialReference::project(blue.material.id),
          QuaderMaterialUsageScope::Selection);
  ASSERT_TRUE(replaced.ok()) << replaced.error_message();
  ASSERT_TRUE(replaced.changed());
  const MaterialComponent *changed =
      scene_material(harness.project(), box.value().entity);
  ASSERT_NE(changed, nullptr);
  EXPECT_EQ(changed->material,
            MaterialReference(kMissingEmbeddedMaterial));
  ASSERT_EQ(changed->material_slots.size(), 1u);
  EXPECT_EQ(changed->material_slots.begin()->second,
            MaterialReference::project(blue.material.id));
  const quader_poly::Document *changed_document =
      authored_document(harness.project(), box.value().entity);
  ASSERT_NE(changed_document, nullptr);
  const quader_poly::Face *first_changed =
      quader_poly::find_face(*changed_document, first_face);
  const quader_poly::Face *second_changed =
      quader_poly::find_face(*changed_document, second_face);
  const quader_poly::Face *unselected =
      quader_poly::find_face(*changed_document, unselected_face);
  ASSERT_NE(first_changed, nullptr);
  ASSERT_NE(second_changed, nullptr);
  ASSERT_NE(unselected, nullptr);
  EXPECT_NE(first_changed->material_slot, 0u);
  EXPECT_EQ(second_changed->material_slot, 7u);
  EXPECT_EQ(second_changed->material_slot, first_changed->material_slot);
  EXPECT_EQ(unselected->material_slot, 0u);
  expect_same_selection_identity(exact_selection(harness.project()),
                                 selection_before);
  const MaterialComponent component_after = *changed;
  const auto slots_after =
      face_slot_snapshot(harness.project(), box.value().entity);

  ASSERT_TRUE(harness.editor().history().undo().ok());
  EXPECT_EQ(*scene_material(harness.project(), box.value().entity),
            component_before);
  EXPECT_EQ(face_slot_snapshot(harness.project(), box.value().entity),
            slots_before);
  expect_same_selection_identity(exact_selection(harness.project()),
                                 selection_before);
  ASSERT_TRUE(harness.editor().history().redo().ok());
  changed_document = authored_document(harness.project(), box.value().entity);
  ASSERT_NE(changed_document, nullptr);
  EXPECT_EQ(*scene_material(harness.project(), box.value().entity),
            component_after);
  EXPECT_EQ(face_slot_snapshot(harness.project(), box.value().entity),
            slots_after);
  expect_same_selection_identity(exact_selection(harness.project()),
                                 selection_before);
}

TEST(ProjectMaterialReferenceBehaviorTests,
     GroupScopeTraversesNestedGroupsAndDeduplicatesLinkedDefinitions) {
  ProjectMaterialBehaviorHarness harness("material-reference-instances");
  harness.create();
  const InstalledMaterial amber{
      .path = project_path("Materials/amber.qmat"),
      .material = project_material(kAmberMaterialId, "Amber",
                                   {1.0f, 0.5f, 0.1f}),
  };
  const InstalledMaterial blue{
      .path = project_path("Materials/blue.qmat"),
      .material = project_material(kBlueMaterialId, "Blue",
                                   {0.1f, 0.25f, 1.0f}),
  };
  const std::array installed{amber, blue};
  harness.install_and_reopen(installed);
  const QuaderApiResult<QuaderModelingObjectRef> member =
      harness.editor().modeling().create_box({
          .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
          .name = "Nested Instance Material Member",
          .material = QuaderMaterialReference::project(amber.material.id),
      });
  ASSERT_TRUE(member.ok()) << member.error_message();
  const QuaderApiResult<QuaderEntityRef> inner_group =
      harness.editor().scene().group_selection("Inner Material Group");
  ASSERT_TRUE(inner_group.ok()) << inner_group.error_message();
  const QuaderApiResult<QuaderEntityRef> outer_group =
      harness.editor().scene().group_selection("Outer Material Group");
  ASSERT_TRUE(outer_group.ok()) << outer_group.error_message();
  const QuaderApiResult<QuaderInstanceCreateResult> instance =
      harness.editor().modeling().create_instance_from_selection();
  ASSERT_TRUE(instance.ok()) << instance.error_message();
  const EntityId first_occurrence = instance.value().occurrence.root.id;
  ASSERT_TRUE(harness.editor()
                  .modeling()
                  .duplicate_selected_entity_hierarchy()
                  .ok());
  OpenSceneSession *active_scene = harness.project().active_scene();
  ASSERT_NE(active_scene, nullptr);
  ASSERT_NE(active_scene->session, nullptr);
  const std::vector<EntityId> selected_after_duplicate =
      active_scene->session->scene().selected_entities();
  ASSERT_EQ(selected_after_duplicate.size(), 1u);
  const EntityId second_occurrence = selected_after_duplicate.front();
  EXPECT_NE(second_occurrence, first_occurrence);
  ASSERT_TRUE(harness.editor()
                  .selection()
                  .select_objects({first_occurrence, second_occurrence},
                                  second_occurrence)
                  .ok());
  const QuaderApiResult<QuaderEntityRef> linked_group =
      harness.editor().scene().group_selection("Linked Material Group");
  ASSERT_TRUE(linked_group.ok()) << linked_group.error_message();
  ASSERT_TRUE(harness.editor().selection().set_mode(QuaderSelectionMode::Group)
                  .ok());
  ASSERT_TRUE(harness.editor()
                  .selection()
                  .select_object(linked_group.value().id)
                  .ok());

  const QuaderApiResult<std::vector<QuaderMaterialReference>> used =
      harness.editor().materials().used_materials(
          QuaderMaterialUsageScope::Selection);
  ASSERT_TRUE(used.ok()) << used.error_message();
  ASSERT_EQ(used.value().size(), 1u);
  EXPECT_EQ(used.value().front(),
            QuaderMaterialReference::project(amber.material.id));
  const QuaderApiVoidResult replaced =
      harness.editor().materials().replace_material_references(
          QuaderMaterialReference::project(amber.material.id),
          QuaderMaterialReference::project(blue.material.id),
          QuaderMaterialUsageScope::Selection);
  ASSERT_TRUE(replaced.ok()) << replaced.error_message();
  ASSERT_TRUE(replaced.changed());
  EXPECT_EQ(scene_material(harness.project(), member.value().entity)->material,
            MaterialReference::project(blue.material.id));
  ASSERT_TRUE(harness.editor().history().undo().ok());
  EXPECT_EQ(scene_material(harness.project(), member.value().entity)->material,
            MaterialReference::project(amber.material.id));

  const ProjectMaterialFixture fixture(harness.project());
  ASSERT_TRUE(fixture.set_locked(second_occurrence, true));
  const QuaderHistorySnapshot history_before_locked =
      harness.editor().query().history();
  const QuaderApiVoidResult locked =
      harness.editor().materials().replace_material_references(
          QuaderMaterialReference::project(amber.material.id),
          QuaderMaterialReference::project(blue.material.id),
          QuaderMaterialUsageScope::Selection);
  EXPECT_FALSE(locked.ok());
  EXPECT_FALSE(locked.changed());
  EXPECT_EQ(scene_material(harness.project(), member.value().entity)->material,
            MaterialReference::project(amber.material.id));
  expect_same_history(harness.editor().query().history(),
                      history_before_locked);
  ASSERT_TRUE(fixture.set_locked(second_occurrence, false));

  ASSERT_TRUE(fixture.set_selectable(linked_group.value().id, false));
  const QuaderHistorySnapshot history_before_unselectable_ancestor =
      harness.editor().query().history();
  const QuaderApiVoidResult unselectable_ancestor =
      harness.editor().materials().replace_material_references(
          QuaderMaterialReference::project(amber.material.id),
          QuaderMaterialReference::project(blue.material.id),
          QuaderMaterialUsageScope::Selection);
  EXPECT_FALSE(unselectable_ancestor.ok());
  EXPECT_FALSE(unselectable_ancestor.changed());
  EXPECT_EQ(scene_material(harness.project(), member.value().entity)->material,
            MaterialReference::project(amber.material.id));
  expect_same_history(harness.editor().query().history(),
                      history_before_unselectable_ancestor);
  ASSERT_TRUE(fixture.set_selectable(linked_group.value().id, true));
}

TEST(ProjectMaterialReferenceBehaviorTests,
     FaceScopeRejectsWhenAnyLinkedOccurrenceIsLocked) {
  ProjectMaterialBehaviorHarness harness("material-reference-face-instance");
  harness.create();
  const InstalledMaterial amber{
      .path = project_path("Materials/amber.qmat"),
      .material = project_material(kAmberMaterialId, "Amber",
                                   {1.0f, 0.5f, 0.1f}),
  };
  const InstalledMaterial blue{
      .path = project_path("Materials/blue.qmat"),
      .material = project_material(kBlueMaterialId, "Blue",
                                   {0.1f, 0.25f, 1.0f}),
  };
  const std::array installed{amber, blue};
  harness.install_and_reopen(installed);
  const QuaderApiResult<QuaderModelingObjectRef> member =
      harness.editor().modeling().create_box({
          .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
          .name = "Linked Face Material Member",
          .material = QuaderMaterialReference::project(amber.material.id),
      });
  ASSERT_TRUE(member.ok()) << member.error_message();
  const QuaderApiResult<QuaderInstanceCreateResult> instance =
      harness.editor().modeling().create_instance_from_selection();
  ASSERT_TRUE(instance.ok()) << instance.error_message();
  const EntityId first_occurrence = instance.value().occurrence.root.id;
  ASSERT_TRUE(harness.editor()
                  .modeling()
                  .duplicate_selected_entity_hierarchy()
                  .ok());
  OpenSceneSession *active_scene = harness.project().active_scene();
  ASSERT_NE(active_scene, nullptr);
  ASSERT_NE(active_scene->session, nullptr);
  const std::vector<EntityId> duplicate_selection =
      active_scene->session->scene().selected_entities();
  ASSERT_EQ(duplicate_selection.size(), 1u);
  const EntityId second_occurrence = duplicate_selection.front();
  ASSERT_NE(second_occurrence, first_occurrence);
  ASSERT_TRUE(
      harness.editor().selection().select_object(first_occurrence).ok());
  ASSERT_TRUE(
      harness.editor().modeling().enter_instance_edit(first_occurrence).ok());
  ASSERT_TRUE(harness.editor().selection().select_object(member.value()).ok());
  ASSERT_TRUE(harness.editor().selection().set_mode(QuaderSelectionMode::Face)
                  .ok());
  QuaderPickSelectRequest pick_request;
  pick_request.ray = {
      .origin = {0.0f, 0.0f, 5.0f},
      .direction = {0.0f, 0.0f, -1.0f},
  };
  const QuaderApiResult<QuaderSelectionSummary> select_face =
      harness.editor().selection().pick_select(pick_request);
  ASSERT_TRUE(select_face.ok()) << select_face.error_message();
  ASSERT_EQ(select_face.value().selected_face_count, 1u);
  const ModelingSelectionExport selection_before =
      exact_selection(harness.project());
  const quader_poly::ElementId selected_face =
      selection_before.summary.active_face_id;
  ASSERT_NE(selected_face, quader_poly::kInvalidElementId);
  const MaterialComponent component_before =
      *scene_material(harness.project(), member.value().entity);
  const auto slots_before =
      face_slot_snapshot(harness.project(), member.value().entity);

  const ProjectMaterialFixture fixture(harness.project());
  ASSERT_TRUE(fixture.set_locked(second_occurrence, true));
  const QuaderHistorySnapshot history_before =
      harness.editor().query().history();
  const QuaderApiVoidResult locked =
      harness.editor().materials().replace_material_references(
          QuaderMaterialReference::project(amber.material.id),
          QuaderMaterialReference::project(blue.material.id),
          QuaderMaterialUsageScope::Selection);
  EXPECT_FALSE(locked.ok());
  EXPECT_FALSE(locked.changed());
  EXPECT_EQ(*scene_material(harness.project(), member.value().entity),
            component_before);
  EXPECT_EQ(face_slot_snapshot(harness.project(), member.value().entity),
            slots_before);
  expect_same_selection_identity(exact_selection(harness.project()),
                                 selection_before);
  expect_same_history(harness.editor().query().history(), history_before);

  ASSERT_TRUE(fixture.set_locked(second_occurrence, false));
  ASSERT_TRUE(fixture.set_selectable(second_occurrence, false));
  const QuaderHistorySnapshot history_before_unselectable =
      harness.editor().query().history();
  const QuaderApiVoidResult unselectable =
      harness.editor().materials().replace_material_references(
          QuaderMaterialReference::project(amber.material.id),
          QuaderMaterialReference::project(blue.material.id),
          QuaderMaterialUsageScope::Selection);
  EXPECT_FALSE(unselectable.ok());
  EXPECT_FALSE(unselectable.changed());
  EXPECT_EQ(*scene_material(harness.project(), member.value().entity),
            component_before);
  EXPECT_EQ(face_slot_snapshot(harness.project(), member.value().entity),
            slots_before);
  expect_same_selection_identity(exact_selection(harness.project()),
                                 selection_before);
  expect_same_history(harness.editor().query().history(),
                      history_before_unselectable);
  ASSERT_TRUE(fixture.set_selectable(second_occurrence, true));

  const QuaderApiVoidResult replaced =
      harness.editor().materials().replace_material_references(
          QuaderMaterialReference::project(amber.material.id),
          QuaderMaterialReference::project(blue.material.id),
          QuaderMaterialUsageScope::Selection);
  ASSERT_TRUE(replaced.ok()) << replaced.error_message();
  ASSERT_TRUE(replaced.changed());
  const MaterialComponent *changed =
      scene_material(harness.project(), member.value().entity);
  ASSERT_NE(changed, nullptr);
  const quader_poly::Document *document =
      authored_document(harness.project(), member.value().entity);
  ASSERT_NE(document, nullptr);
  const quader_poly::Face *changed_face =
      quader_poly::find_face(*document, selected_face);
  ASSERT_NE(changed_face, nullptr);
  EXPECT_EQ(changed->material_for_slot(changed_face->material_slot),
            MaterialReference::project(blue.material.id));
}

TEST(ProjectMaterialReferenceBehaviorTests,
     MissingProjectSourceRemainsDiscoverableAndReplaceable) {
  ProjectMaterialBehaviorHarness harness("material-reference-missing-project");
  harness.create();
  const InstalledMaterial blue{
      .path = project_path("Materials/blue.qmat"),
      .material = project_material(kBlueMaterialId, "Blue",
                                   {0.1f, 0.25f, 1.0f}),
  };
  const std::array installed{blue};
  harness.install_and_reopen(installed);
  const QuaderApiResult<QuaderModelingObjectRef> box =
      harness.editor().modeling().create_box(
          {.min = {-1.0f, -1.0f, -1.0f},
           .max = {1.0f, 1.0f, 1.0f}},
          "Missing Project Material Mesh");
  ASSERT_TRUE(box.ok()) << box.error_message();
  const ProjectAssetId missing_project = ProjectAssetId::generate();
  const ProjectMaterialFixture fixture(harness.project());
  ASSERT_TRUE(fixture.set_material(
      box.value().entity,
      MaterialComponent{.material = MaterialReference::project(
                            missing_project)}));
  ASSERT_TRUE(harness.editor().selection().set_mode(QuaderSelectionMode::Object)
                  .ok());
  ASSERT_TRUE(harness.editor().selection().select_object(box.value()).ok());

  const QuaderApiResult<std::vector<QuaderMaterialReference>> used =
      harness.editor().materials().used_materials(
          QuaderMaterialUsageScope::Selection);
  ASSERT_TRUE(used.ok()) << used.error_message();
  ASSERT_EQ(used.value().size(), 1u);
  EXPECT_EQ(used.value().front(),
            QuaderMaterialReference::project(missing_project));
  const QuaderApiVoidResult replaced =
      harness.editor().materials().replace_material_references(
          QuaderMaterialReference::project(missing_project),
          QuaderMaterialReference::project(blue.material.id),
          QuaderMaterialUsageScope::Selection);
  ASSERT_TRUE(replaced.ok()) << replaced.error_message();
  ASSERT_TRUE(replaced.changed());
  EXPECT_EQ(scene_material(harness.project(), box.value().entity)->material,
            MaterialReference::project(blue.material.id));
  ASSERT_TRUE(harness.editor().history().undo().ok());
  EXPECT_EQ(scene_material(harness.project(), box.value().entity)->material,
            MaterialReference::project(missing_project));
}

TEST(ProjectMaterialReferenceBehaviorTests,
     EmbeddedReferencesRoundTripThroughPublicReplacement) {
  ProjectMaterialBehaviorHarness harness("material-reference-embedded");
  harness.create();
  const QuaderApiResult<QuaderAssetRef> source =
      harness.editor().assets().add_material(
          QuaderMaterialArgs{.name = "Embedded Replacement Source"});
  const QuaderApiResult<QuaderAssetRef> replacement =
      harness.editor().assets().add_material(
          QuaderMaterialArgs{.name = "Embedded Replacement Destination"});
  ASSERT_TRUE(source.ok()) << source.error_message();
  ASSERT_TRUE(replacement.ok()) << replacement.error_message();
  const QuaderApiResult<QuaderModelingObjectRef> box =
      harness.editor().modeling().create_box(
          {.min = {-1.0f, -1.0f, -1.0f},
           .max = {1.0f, 1.0f, 1.0f}},
          "Embedded Reference Mesh");
  ASSERT_TRUE(box.ok()) << box.error_message();
  const ProjectMaterialFixture fixture(harness.project());
  ASSERT_TRUE(fixture.set_material(
      box.value().entity,
      MaterialComponent{.material = MaterialReference(source.value().id)}));
  ASSERT_TRUE(harness.editor().selection().set_mode(QuaderSelectionMode::Object)
                  .ok());
  ASSERT_TRUE(harness.editor().selection().select_object(box.value()).ok());

  const QuaderApiResult<std::vector<QuaderMaterialReference>> used =
      harness.editor().materials().used_materials(
          QuaderMaterialUsageScope::Selection);
  ASSERT_TRUE(used.ok()) << used.error_message();
  ASSERT_EQ(used.value().size(), 1u);
  EXPECT_EQ(used.value().front(),
            QuaderMaterialReference::embedded(source.value().id));
  const QuaderApiVoidResult replaced =
      harness.editor().materials().replace_material_references(
          QuaderMaterialReference::embedded(source.value().id),
          QuaderMaterialReference::embedded(replacement.value().id),
          QuaderMaterialUsageScope::Selection);
  ASSERT_TRUE(replaced.ok()) << replaced.error_message();
  ASSERT_TRUE(replaced.changed());
  EXPECT_EQ(scene_material(harness.project(), box.value().entity)->material,
            MaterialReference(replacement.value().id));
  ASSERT_TRUE(harness.editor().history().undo().ok());
  EXPECT_EQ(scene_material(harness.project(), box.value().entity)->material,
            MaterialReference(source.value().id));
  ASSERT_TRUE(harness.editor().history().redo().ok());
  EXPECT_EQ(scene_material(harness.project(), box.value().entity)->material,
            MaterialReference(replacement.value().id));
}

TEST(ProjectMaterialPersistenceTests,
     FaceOnlyMaterialsSurvivePublicSaveReopenAndResave) {
  ProjectMaterialBehaviorHarness harness("face-only-material-roundtrip");
  harness.create();
  const InstalledMaterial amber{
      .path = project_path("Materials/amber.qmat"),
      .material = project_material(kAmberMaterialId, "Amber",
                                   {1.0f, 0.5f, 0.1f}),
  };
  const InstalledMaterial blue{
      .path = project_path("Materials/blue.qmat"),
      .material = project_material(kBlueMaterialId, "Blue",
                                   {0.1f, 0.25f, 1.0f}),
  };
  const std::array installed{amber, blue};
  harness.install_and_reopen(installed);
  QuaderEditorApi &editor = harness.editor();
  const auto box = editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      "Face Materials Without Primary");
  ASSERT_TRUE(box.ok()) << box.error_message();
  const EntityId entity = box.value().entity;
  const auto original_slots = face_slot_snapshot(harness.project(), entity);
  ASSERT_EQ(original_slots.size(), 6u);

  // Only the unassigned starting state needs low-level fixture setup. All
  // assignments and persistence below exercise the public editor workflow.
  ASSERT_TRUE(ProjectMaterialFixture(harness.project()).clear_material(entity));
  for (std::size_t index = 0u; index < installed.size(); ++index) {
    const auto assigned = editor.modeling().apply_material_to_face(
        box.value().modeling_object_id, original_slots[index].first,
        QuaderMaterialReference::project(installed[index].material.id));
    ASSERT_TRUE(assigned.ok()) << assigned.error_message();
    ASSERT_TRUE(assigned.changed());
  }
  const MaterialComponent *material = scene_material(harness.project(), entity);
  ASSERT_NE(material, nullptr);
  const MaterialComponent expected_material = *material;
  EXPECT_FALSE(expected_material.material.valid());
  ASSERT_EQ(expected_material.material_slots.size(), 2u);
  const auto expected_slots = face_slot_snapshot(harness.project(), entity);
  EXPECT_NE(expected_slots[0].second, 0u);
  EXPECT_NE(expected_slots[1].second, 0u);
  EXPECT_NE(expected_slots[0].second, expected_slots[1].second);
  EXPECT_EQ(expected_material.material_for_slot(expected_slots[0].second),
            MaterialReference::project(amber.material.id));
  EXPECT_EQ(expected_material.material_for_slot(expected_slots[1].second),
            MaterialReference::project(blue.material.id));
  for (std::size_t index = installed.size(); index < expected_slots.size();
       ++index) {
    EXPECT_EQ(expected_slots[index], original_slots[index]);
    EXPECT_FALSE(expected_material.material_for_slot(expected_slots[index].second)
                     .valid());
  }

  const auto scene_id = editor.scene_documents().active_scene_id();
  ASSERT_TRUE(scene_id.has_value());
  const ProjectRelativePath path = project_path("Scenes/face_materials.qdr");
  const auto saved = editor.scene_documents().save_as(*scene_id, path);
  ASSERT_TRUE(saved.ok()) << saved.error_message();
  ASSERT_TRUE(editor.scene_documents().close(*scene_id).ok());

  // The second pass reads bytes produced by saving an already reopened scene.
  // Both the component bindings and every authored face-slot ID must survive.
  for (int pass = 0; pass < 2; ++pass) {
    SCOPED_TRACE(pass);
    const auto reopened = editor.scene_documents().open(path);
    ASSERT_TRUE(reopened.ok()) << reopened.error_message();
    material = scene_material(harness.project(), entity);
    ASSERT_NE(material, nullptr);
    EXPECT_EQ(*material, expected_material);
    EXPECT_EQ(face_slot_snapshot(harness.project(), entity), expected_slots);
    for (std::size_t index = 0u; index < installed.size(); ++index) {
      const auto resolved = editor.modeling().project_material_for_face(
          box.value().modeling_object_id, expected_slots[index].first);
      ASSERT_TRUE(resolved.ok()) << resolved.error_message();
      EXPECT_EQ(resolved.value().id, installed[index].material.id);
    }
    for (std::size_t index = installed.size(); index < expected_slots.size();
         ++index) {
      EXPECT_FALSE(material->material_for_slot(expected_slots[index].second)
                       .valid());
    }
    const auto resaved = editor.scene_documents().save(reopened.value());
    ASSERT_TRUE(resaved.ok()) << resaved.error_message();
    ASSERT_TRUE(editor.scene_documents().close(reopened.value()).ok());
  }
}

TEST(ProjectMaterialPersistenceTests,
     Qdr12StoresStableReferencesAndDependenciesWithoutMaterialPayloads) {
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

  const QuaderApiResult<QuaderModelingObjectRef> box =
      harness.editor().modeling().create_box({
          .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
          .name = "Persisted Project Material",
          .material = QuaderMaterialReference::project(amber.material.id),
      });
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
  EXPECT_EQ(scene_bytes[6], std::byte{2});
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
