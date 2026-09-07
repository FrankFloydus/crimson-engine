////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "assets/material_document.hpp"
#include "editor/material/standard_material_edit_session_service.hpp"
#include "editor/session/project_session.hpp"
#include "io/bridge/quader_bridge_reader.hpp"
#include "io/project/project_manifest.hpp"
#include "io/project/qmat_codec.hpp"
#include "io/qdr/qdr_document_writer.hpp"
#include "io/qdr/qdr_binary_container.hpp"
#include "io/qdr/qdr_project_write_snapshot.hpp"
#include "scene/document/scene_content_document.hpp"

#include <gtest/gtest.h>
#include <quader/modeling/neutral/polygon/document.hpp>
#include <QImage>
#include <QColor>

#include <array>
#include <atomic>
#include <chrono>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

using namespace quader;

namespace {

constexpr char kFirstScene[] = "11111111-1111-4111-8111-111111111111";
constexpr char kSecondScene[] = "22222222-2222-4222-8222-222222222222";
constexpr char kProjectMaterial[] = "33333333-3333-4333-8333-333333333333";
constexpr char kTexture[] = "44444444-4444-4444-8444-444444444444";
constexpr char kMissingMaterial[] = "55555555-5555-4555-8555-555555555555";

std::string utf8_path(const std::filesystem::path &path) {
  const auto text = path.u8string();
  return {reinterpret_cast<const char *>(text.data()), text.size()};
}

ProjectRelativePath relative_path(std::string_view text) {
  auto parsed = ProjectRelativePath::parse_portable(text);
  EXPECT_TRUE(parsed.ok()) << parsed.error().message;
  return parsed.take_value();
}

SceneId scene_id(std::string_view text) {
  auto parsed = SceneId::parse(text);
  EXPECT_TRUE(parsed.ok()) << parsed.error().message;
  return parsed.take_value();
}

void write_bytes(const std::filesystem::path &path, std::string_view bytes) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  ASSERT_TRUE(stream.is_open()) << path.string();
  stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  stream.close();
  ASSERT_TRUE(stream.good()) << path.string();
}

QuaderBridgeSourceFingerprint fingerprint_file(const std::filesystem::path &path) {
  std::ifstream stream(path, std::ios::binary);
  EXPECT_TRUE(stream.is_open()) << path.string();
  const std::string bytes((std::istreambuf_iterator<char>(stream)), {});
  return {.byte_count = bytes.size(),
          .content_hash = quader_io::qdr_binary_content_hash(std::as_bytes(
              std::span(bytes.data(), bytes.size())))};
}

MeshAsset make_two_material_mesh() {
  MeshAsset mesh;
  mesh.name = "Two material triangles";
  auto &geometry = mesh.edit_geometry();
  geometry.vertices = {
      {.position = {0.0f, 0.0f, 0.0f}, .normal = {0.0f, 0.0f, 1.0f},
       .uv = {0.0f, 0.0f}},
      {.position = {1.0f, 0.0f, 0.0f}, .normal = {0.0f, 0.0f, 1.0f},
       .uv = {1.0f, 0.0f}},
      {.position = {0.0f, 1.0f, 0.0f}, .normal = {0.0f, 0.0f, 1.0f},
       .uv = {0.0f, 1.0f}},
      {.position = {1.0f, 0.0f, 0.0f}, .normal = {0.0f, 0.0f, 1.0f},
       .uv = {0.25f, 0.0f}},
      {.position = {1.0f, 1.0f, 0.0f}, .normal = {0.0f, 0.0f, 1.0f},
       .uv = {1.0f, 1.0f}},
      {.position = {0.0f, 1.0f, 0.0f}, .normal = {0.0f, 0.0f, 1.0f},
       .uv = {0.0f, 0.75f}},
  };
  geometry.indices = {0u, 1u, 2u, 3u, 4u, 5u};
  geometry.primitives = {{.index_offset = 0u, .index_count = 3u,
                          .material_slot = 0u},
                         {.index_offset = 3u, .index_count = 3u,
                          .material_slot = 1u}};
  auto authored = std::make_shared<quader_poly::Document>();
  authored->next_vertex_id = 5u;
  authored->next_face_id = 3u;
  authored->vertices = {{.id = 1u, .position = {0.0f, 0.0f, 0.0f}},
                        {.id = 2u, .position = {1.0f, 0.0f, 0.0f}},
                        {.id = 3u, .position = {0.0f, 1.0f, 0.0f}},
                        {.id = 4u, .position = {1.0f, 1.0f, 0.0f}}};
  quader_poly::Face first;
  first.id = 1u;
  first.vertices = {1u, 2u, 3u};
  first.uvs = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}};
  first.normal_shading = quader_poly::SurfaceShading::Authored;
  quader_poly::Face second;
  second.id = 2u;
  second.vertices = {2u, 4u, 3u};
  second.uvs = {{0.25f, 0.0f}, {1.0f, 1.0f}, {0.0f, 0.75f}};
  second.material_slot = 1u;
  second.normal_shading = quader_poly::SurfaceShading::Authored;
  authored->faces = {std::move(first), std::move(second)};
  mesh.has_authored_polygon_document = true;
  mesh.authored_polygon_document = std::move(authored);
  return mesh;
}

SceneContentDocument make_document(bool mixed_materials = false,
                                   const char *material_id = kProjectMaterial) {
  SceneContentDocument document({.id = 7u, .name = "Bridge fixture"});
  EXPECT_EQ(document.assets().add_mesh(10u, make_two_material_mesh()), 10u);
  auto material = make_default_quader_material("Embedded blue");
  material.base_color = {0.1f, 0.2f, 0.8f};
  material.roughness = 0.35f;
  material.metallic = 0.6f;
  EXPECT_EQ(document.assets().add_material(20u, std::move(material)), 20u);
  const EntityId entity = document.scene().create_entity_with_id(
      100u, "Fixture mesh", EntityKind::Mesh);
  EXPECT_EQ(entity, 100u);
  EXPECT_TRUE(document.scene().set_mesh(
      entity, MeshComponent{.mesh = 10u, .collision_type = MeshCollisionType::Mesh}));
  MaterialComponent binding{.material = 20u};
  if (mixed_materials) {
    binding.material_slots.emplace(
        1u, MaterialReference::project(*ProjectAssetId::parse(material_id)));
  }
  EXPECT_TRUE(document.scene().set_material(entity, std::move(binding)));
  return document;
}

/// Exercises the public reader ABI against disposable canonical native files.
class BridgeReaderTests : public testing::Test {
protected:
  void SetUp() override {
    api_ = quader_bridge_get_api(QUADER_BRIDGE_ABI_VERSION);
    ASSERT_NE(api_, nullptr);
    static std::atomic<std::uint64_t> sequence{0u};
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    root_ = std::filesystem::temp_directory_path() /
            ("quader-bridge-" + std::to_string(tick) + "-" +
             std::to_string(sequence.fetch_add(1u)));
    ASSERT_TRUE(std::filesystem::create_directory(root_));
    auto created = ProjectManifestStore::create_in_empty_directory(root_);
    ASSERT_TRUE(created.ok()) << created.error().message;
    workspace_ = created.take_value();
    auto manifest = ProjectManifest::create(
        ProjectId::parse("66666666-6666-4666-8666-666666666666").take_value(),
        root_.filename().string());
    ASSERT_TRUE(manifest.ok()) << manifest.error().message;
    workspace_.manifest = manifest.take_value();
    save_manifest();
  }

  void TearDown() override {
    if (api_ != nullptr) {
      api_->release_blob(blob_);
      api_->release_scene(scene_);
      api_->release_project(project_);
    }
    if (!root_.empty() && root_.parent_path() ==
                              std::filesystem::temp_directory_path() &&
        root_.filename().string().starts_with("quader-bridge-")) {
      std::error_code ignored;
      std::filesystem::remove_all(root_, ignored);
    }
  }

  void save_manifest() {
    const auto saved = ProjectManifestStore::save_atomic(workspace_);
    ASSERT_TRUE(saved.ok()) << saved.error().message;
  }

  void add_scene(const char *id, std::string_view path,
                const SceneContentDocument &document,
                const char *project_material = nullptr) {
    auto snapshot = qdr_project_write_snapshot_from_document(document);
    ASSERT_TRUE(snapshot.ok()) << snapshot.error().message;
    if (project_material != nullptr) {
      snapshot.value().project_asset_dependencies.push_back({
          .stable_id = *ProjectAssetId::parse(project_material),
          .kind = ProjectAssetKind::Material,
          .path = relative_path(std::string_view(project_material) == kMissingMaterial
                                    ? "Materials/missing.qmat"
                                    : "Materials/surface.qmat"),
      });
    }
    const auto relative = relative_path(path);
    const auto resolved = workspace_.file_system.resolve_for_write(relative);
    ASSERT_TRUE(resolved.ok()) << resolved.error().message;
    const auto written = QdrDocumentWriter::write_project_document_atomic(
        resolved.value(), snapshot.value(), scene_id(id).bytes());
    ASSERT_TRUE(written.ok) << written.message;
    const auto registered = workspace_.manifest.register_scene(
        {.id = scene_id(id), .path = relative});
    ASSERT_TRUE(registered.ok()) << registered.error().message;
    save_manifest();
  }

  void open_project() {
    ASSERT_EQ(api_->open_project(utf8_path(workspace_.manifest_path).c_str(),
                                &project_, &error_), 1)
        << error_.message;
  }

  void open_scene(const char *id = kFirstScene) {
    ASSERT_NE(project_, nullptr);
    ASSERT_EQ(api_->open_scene(project_, id, &scene_, &error_), 1)
        << error_.message;
  }

  std::uint32_t material_index(std::uint32_t kind) {
    QuaderBridgeSceneView scene_view{};
    EXPECT_EQ(api_->scene_view(scene_, &scene_view, &error_), 1);
    for (std::uint32_t index = 0u; index < scene_view.material_count; ++index) {
      QuaderBridgeMaterialIdentity identity{};
      EXPECT_EQ(api_->material_identity(scene_, index, &identity, &error_), 1);
      if (identity.kind == kind) {
        return index;
      }
    }
    ADD_FAILURE() << "Expected material kind " << kind;
    return QUADER_BRIDGE_NO_MATERIAL;
  }

  void write_project_material(const char *id = kProjectMaterial,
                            std::string_view texture = "Textures/albedo.PNG",
                            bool all_textures = false) {
    std::filesystem::create_directories(root_ / "Materials");
    std::filesystem::create_directories(root_ / "Textures");
    ProjectMaterialDocument material;
    material.id = *ProjectAssetId::parse(id);
    material.name = "Project surface";
    auto &standard = *material.standard_material();
    standard.base_color = {0.2f, 0.4f, 0.6f};
    standard.roughness = 0.45f;
    standard.metallic = 0.7f;
    standard.normal_strength = 0.8f;
    standard.normal_convention = NormalMapConvention::DirectX;
    standard.texture_transform = {.center = {0.25f, 0.75f},
                                  .scale = {2.0f, -1.0f},
                                  .offset = {0.2f, -0.3f},
                                  .rotation_degrees = 35.0f};
    standard.base_color_texture = ProjectMaterialTextureSlot{
        .id = *ProjectAssetId::parse(kTexture),
        .project_relative_path = std::string(texture),
        .color_space = ProjectTextureColorSpace::Srgb,
    };
    if (all_textures) {
      auto linear = *standard.base_color_texture;
      linear.color_space = ProjectTextureColorSpace::Linear;
      standard.roughness_texture = linear;
      standard.metallic_texture = linear;
      standard.normal_texture = linear;
    }
    const auto written = QmatCodec::write_atomic(
        workspace_.file_system,
        relative_path(std::string_view(id) == kMissingMaterial
                          ? "Materials/missing.qmat" : "Materials/surface.qmat"),
        material);
    ASSERT_TRUE(written.ok()) << written.error().message;
  }

  const QuaderBridgeApi *api_ = nullptr;
  QuaderBridgeProject *project_ = nullptr;
  QuaderBridgeScene *scene_ = nullptr;
  QuaderBridgeBlob *blob_ = nullptr;
  QuaderBridgeError error_{};
  std::filesystem::path root_;
  ProjectWorkspaceStorage workspace_;
};

TEST_F(BridgeReaderTests, RejectsUnsupportedAbiAndInvalidArguments) {
  EXPECT_EQ(quader_bridge_get_api(0u), nullptr);
  EXPECT_EQ(quader_bridge_get_api(1u), nullptr);
  EXPECT_EQ(quader_bridge_get_api(2u), nullptr);
  EXPECT_EQ(quader_bridge_get_api(QUADER_BRIDGE_ABI_VERSION + 1u), nullptr);
  EXPECT_EQ(api_->abi_version, QUADER_BRIDGE_ABI_VERSION);
  EXPECT_EQ(api_->struct_size, sizeof(QuaderBridgeApi));
  EXPECT_EQ(api_->open_project(nullptr, &project_, &error_), 0);
  EXPECT_EQ(project_, nullptr);
  EXPECT_NE(error_.message[0], '\0');
  EXPECT_EQ(api_->open_project(utf8_path(workspace_.manifest_path).c_str(),
                              nullptr, nullptr), 0);
  QuaderBridgeProjectView project_view{};
  EXPECT_EQ(api_->project_view(nullptr, &project_view, &error_), 0);
  EXPECT_EQ(api_->scene_view(nullptr, nullptr, nullptr), 0);
  EXPECT_EQ(api_->mesh_view(nullptr, 0u, nullptr, nullptr), 0);
  EXPECT_EQ(api_->material_identity(nullptr, 0u, nullptr, nullptr), 0);
  EXPECT_EQ(api_->material_view(nullptr, 0u, nullptr, nullptr), 0);
  EXPECT_EQ(api_->read_texture(nullptr, 0u, 0u, &blob_, nullptr), 0);
  EXPECT_EQ(api_->blob_view(nullptr, nullptr, nullptr), 0);
  EXPECT_EQ(api_->scene_source_fingerprint(nullptr, nullptr, nullptr), 0);
  EXPECT_EQ(api_->material_source_fingerprint(nullptr, 0u, nullptr, nullptr), 0);
  api_->release_project(nullptr);
  api_->release_scene(nullptr);
  api_->release_blob(nullptr);
}

TEST_F(BridgeReaderTests, FingerprintsIdentifyDecodedBytesAcrossSourceReplacement) {
  write_project_material();
  add_scene(kFirstScene, "Scenes/first.qdr", make_document(true), kProjectMaterial);
  const auto saved_scene = fingerprint_file(root_ / "Scenes" / "first.qdr");
  const auto saved_material = fingerprint_file(root_ / "Materials" / "surface.qmat");
  open_project();
  open_scene();
  QuaderBridgeSourceFingerprint scene_fingerprint{};
  ASSERT_EQ(api_->scene_source_fingerprint(scene_, &scene_fingerprint, &error_), 1)
      << error_.message;
  EXPECT_EQ(scene_fingerprint.byte_count, saved_scene.byte_count);
  EXPECT_EQ(scene_fingerprint.content_hash, saved_scene.content_hash);
  QuaderBridgeSceneView scene_view{};
  ASSERT_EQ(api_->scene_view(scene_, &scene_view, &error_), 1);
  EXPECT_STREQ(scene_view.relative_path, "Scenes/first.qdr");
  const auto project = material_index(QUADER_BRIDGE_MATERIAL_PROJECT);
  const auto embedded = material_index(QUADER_BRIDGE_MATERIAL_EMBEDDED);
  QuaderBridgeSourceFingerprint material_fingerprint{};
  ASSERT_EQ(api_->material_source_fingerprint(scene_, project,
      &material_fingerprint, &error_), 1) << error_.message;
  EXPECT_EQ(material_fingerprint.byte_count, saved_material.byte_count);
  EXPECT_EQ(material_fingerprint.content_hash, saved_material.content_hash);
  QuaderBridgeMaterialView material_view{};
  ASSERT_EQ(api_->material_view(scene_, project, &material_view, &error_), 1);
  EXPECT_STREQ(material_view.source_relative_path, "Materials/surface.qmat");

  // Exact source snapshots must not turn into whichever file is on disk later.
  write_bytes(root_ / "Scenes" / "first.qdr", "replaced scene bytes");
  write_bytes(root_ / "Materials" / "surface.qmat", "replaced material bytes");
  ASSERT_EQ(api_->scene_source_fingerprint(scene_, &scene_fingerprint, &error_), 1);
  EXPECT_EQ(scene_fingerprint.content_hash, saved_scene.content_hash);
  ASSERT_EQ(api_->material_source_fingerprint(scene_, project,
      &material_fingerprint, &error_), 1);
  EXPECT_EQ(material_fingerprint.content_hash, saved_material.content_hash);
  ASSERT_EQ(api_->material_source_fingerprint(scene_, embedded,
      &material_fingerprint, &error_), 1);
  EXPECT_EQ(material_fingerprint.byte_count, saved_scene.byte_count);
  EXPECT_EQ(material_fingerprint.content_hash, saved_scene.content_hash);
  ASSERT_EQ(api_->material_view(scene_, embedded, &material_view, &error_), 1);
  EXPECT_STREQ(material_view.source_relative_path, "Scenes/first.qdr");
  EXPECT_EQ(api_->scene_source_fingerprint(scene_, nullptr, &error_), 0);
  EXPECT_EQ(api_->material_source_fingerprint(scene_, UINT32_MAX,
      &material_fingerprint, &error_), 0);
  EXPECT_EQ(api_->material_source_fingerprint(scene_, project, nullptr, &error_), 0);
}

TEST_F(BridgeReaderTests, LegacyMaterialFingerprintsUseCatalogDecodedBytes) {
  write_project_material();
  add_scene(kFirstScene, "Scenes/first.qdr", make_document(true));
  const auto expected = fingerprint_file(root_ / "Materials" / "surface.qmat");
  open_project();
  open_scene();
  const auto index = material_index(QUADER_BRIDGE_MATERIAL_PROJECT);
  QuaderBridgeSourceFingerprint fingerprint{};
  ASSERT_EQ(api_->material_source_fingerprint(scene_, index, &fingerprint, &error_), 1)
      << error_.message;
  EXPECT_EQ(fingerprint.byte_count, expected.byte_count);
  EXPECT_EQ(fingerprint.content_hash, expected.content_hash);
  QuaderBridgeMaterialView material{};
  ASSERT_EQ(api_->material_view(scene_, index, &material, &error_), 1);
  EXPECT_STREQ(material.source_relative_path, "Materials/surface.qmat");
}

TEST_F(BridgeReaderTests, EmptyCatalogHasNoDefaultScene) {
  open_project();
  QuaderBridgeProjectView view{};
  ASSERT_EQ(api_->project_view(project_, &view, &error_), 1) << error_.message;
  EXPECT_EQ(view.scene_count, 0u);
  EXPECT_EQ(view.default_scene_index, QUADER_BRIDGE_NO_SCENE);
  EXPECT_EQ(std::string(view.name), root_.filename().string());
  QuaderBridgeSceneEntry entry{};
  EXPECT_EQ(api_->scene_entry(project_, 0u, &entry, &error_), 0);
  EXPECT_EQ(api_->open_scene(project_, kFirstScene, &scene_, &error_), 0);
  EXPECT_EQ(scene_, nullptr);
}

TEST_F(BridgeReaderTests, CatalogUsesSavedActiveSceneAndFallsBackToFirst) {
  const auto document = make_document();
  add_scene(kFirstScene, "Scenes/first.qdr", document);
  add_scene(kSecondScene, "Scenes/second.qdr", document);
  open_project();
  QuaderBridgeProjectView view{};
  ASSERT_EQ(api_->project_view(project_, &view, &error_), 1);
  EXPECT_EQ(view.scene_count, 2u);
  EXPECT_EQ(view.default_scene_index, 0u);
  QuaderBridgeSceneEntry entry{};
  ASSERT_EQ(api_->scene_entry(project_, 1u, &entry, &error_), 1);
  EXPECT_STREQ(entry.id, kSecondScene);
  EXPECT_STREQ(entry.relative_path, "Scenes/second.qdr");
  api_->release_project(std::exchange(project_, nullptr));
  const auto active = relative_path("Scenes/second.qdr");
  ASSERT_TRUE(workspace_.manifest.set_workspace({active}, active).ok());
  save_manifest();
  open_project();
  ASSERT_EQ(api_->project_view(project_, &view, &error_), 1);
  EXPECT_EQ(view.default_scene_index, 1u);
}

TEST_F(BridgeReaderTests, SceneStorageSurvivesProjectRelease) {
  add_scene(kFirstScene, "Scenes/first.qdr", make_document());
  open_project();
  open_scene();
  api_->release_project(std::exchange(project_, nullptr));
  QuaderBridgeSceneView scene_view{};
  ASSERT_EQ(api_->scene_view(scene_, &scene_view, &error_), 1);
  EXPECT_STREQ(scene_view.id, kFirstScene);
  EXPECT_EQ(scene_view.mesh_count, 1u);
  EXPECT_EQ(scene_view.material_count, 1u);
  QuaderBridgeMeshView mesh{};
  ASSERT_EQ(api_->mesh_view(scene_, 0u, &mesh, &error_), 1) << error_.message;
  EXPECT_STREQ(mesh.name, "Fixture mesh");
  EXPECT_EQ(mesh.display_entity, 100u);
  EXPECT_EQ(mesh.geometry_entity, 100u);
  EXPECT_EQ(mesh.visible, 1u);
  EXPECT_EQ(mesh.index_count, 6u);
  ASSERT_GE(mesh.vertex_count, 4u);
  ASSERT_NE(mesh.vertices, nullptr);
  ASSERT_NE(mesh.indices, nullptr);
  EXPECT_EQ(mesh.collision_status, QUADER_BRIDGE_COLLISION_TRIMESH_STATIC);
  ASSERT_EQ(mesh.collision_point_count, 6u);
  ASSERT_NE(mesh.collision_points, nullptr);
  const auto *retained_collision = mesh.collision_points;
  for (std::uint32_t index = 0u; index < mesh.index_count; ++index) {
    EXPECT_LT(mesh.indices[index], mesh.vertex_count);
  }
  EXPECT_FLOAT_EQ(mesh.transform[0], 1.0f);
  EXPECT_FLOAT_EQ(mesh.transform[5], 1.0f);
  EXPECT_FLOAT_EQ(mesh.transform[10], 1.0f);
  EXPECT_FLOAT_EQ(mesh.transform[15], 1.0f);
  QuaderBridgeMaterialView material{};
  ASSERT_EQ(api_->material_view(scene_, 0u, &material, &error_), 1);
  EXPECT_STREQ(material.name, "Embedded blue");
  EXPECT_FLOAT_EQ(material.base_color[2], 0.8f);
  EXPECT_FLOAT_EQ(material.roughness, 0.35f);
  EXPECT_FLOAT_EQ(material.metallic, 0.6f);
  EXPECT_FLOAT_EQ(retained_collision[0].position[0], 0.0f);
  EXPECT_FLOAT_EQ(retained_collision[1].position[0], 1.0f);
  EXPECT_FLOAT_EQ(retained_collision[2].position[1], 1.0f);
  QuaderBridgeMeshView reread{};
  ASSERT_EQ(api_->mesh_view(scene_, 0u, &reread, &error_), 1);
  EXPECT_EQ(reread.collision_points, retained_collision);
  EXPECT_EQ(api_->mesh_view(scene_, 1u, &mesh, &error_), 0);
  EXPECT_EQ(api_->material_identity(scene_, 1u, nullptr, &error_), 0);
}

TEST_F(BridgeReaderTests, CollisionEligibilityPreservesSavedUnsupportedMetadata) {
  auto document = make_document();
  auto &source = document.scene();
  const std::array<MeshComponent, 5> components = {{
      {.mesh = 10u, .collision_type = MeshCollisionType::Mesh},
      {.mesh = 10u, .generate_collisions = false,
       .collision_type = MeshCollisionType::Mesh, .display_collision = true},
      {.mesh = 10u, .collision_type = MeshCollisionType::Box},
      {.mesh = 10u, .collision_type = MeshCollisionType::Mesh,
       .body_type = MeshPhysicsBodyType::RigidBody},
      {.mesh = 10u, .generate_collisions = false,
       .collision_type = MeshCollisionType::ConvexHull},
  }};
  for (std::size_t index = 0u; index < components.size(); ++index) {
    const EntityId entity = 100u + index;
    if (index != 0u) {
      ASSERT_EQ(source.create_entity_with_id(entity, "Collision metadata",
                                             EntityKind::Mesh), entity);
      ASSERT_TRUE(source.set_material(entity, *source.material(100u)));
    }
    ASSERT_TRUE(source.set_mesh(entity, components[index]));
    ASSERT_TRUE(source.set_visible(entity, false));
  }
  add_scene(kFirstScene, "Scenes/first.qdr", document);
  open_project();
  open_scene();
  QuaderBridgeSceneView view{};
  ASSERT_EQ(api_->scene_view(scene_, &view, &error_), 1);
  ASSERT_EQ(view.mesh_count, components.size());
  for (std::uint32_t index = 0u; index < view.mesh_count; ++index) {
    QuaderBridgeMeshView mesh{};
    ASSERT_EQ(api_->mesh_view(scene_, index, &mesh, &error_), 1) << error_.message;
    ASSERT_GE(mesh.display_entity, 100u);
    ASSERT_LT(mesh.display_entity, 105u);
    const auto &metadata = components[mesh.display_entity - 100u];
    const auto expected = !metadata.generate_collisions
        ? QUADER_BRIDGE_COLLISION_DISABLED
        : mesh.display_entity == 100u ? QUADER_BRIDGE_COLLISION_TRIMESH_STATIC
                                     : QUADER_BRIDGE_COLLISION_UNSUPPORTED;
    EXPECT_EQ(mesh.collision_status, expected);
    EXPECT_EQ(mesh.visible, 0u);
    EXPECT_EQ(mesh.index_count, 6u);
    EXPECT_EQ(mesh.collision_point_count,
              expected == QUADER_BRIDGE_COLLISION_TRIMESH_STATIC ? 6u : 0u);
    EXPECT_EQ(mesh.collision_points == nullptr,
              expected != QUADER_BRIDGE_COLLISION_TRIMESH_STATIC);
  }
}

TEST_F(BridgeReaderTests, CollisionUsesRenderedPrimitiveSpansAndSnappedLocalFaces) {
  SceneContentDocument document({.id = 7u, .name = "Collision primitive spans"});
  MeshAsset asset;
  auto &geometry = asset.edit_geometry();
  geometry.vertices = {
      {.position = {0.000149f, 0, 0}, .normal = {0, 0, 1}},
      {.position = {1, 0, 0}, .normal = {0, 0, 1}},
      {.position = {0, 1, 0}, .normal = {0, 0, 1}},
      {.position = {2, 2, 0}, .normal = {0, 0, 1}},
  };
  // Native asset storage requires a complete triangle index buffer. Its unused
  // prefix/suffix reference a fourth vertex, while the rendered overlapping
  // spans both select only vertices 0, 1 and 2 at an unaligned offset.
  geometry.indices = {3u, 0u, 1u, 2u, 1u, 3u};
  geometry.primitives = {{.index_offset = 1u, .index_count = 3u},
                         {.index_offset = 1u, .index_count = 3u}};
  ASSERT_EQ(document.assets().add_mesh(10u, std::move(asset)), 10u);
  ASSERT_EQ(document.scene().create_entity_with_id(100u, "Span mesh",
                                                  EntityKind::Mesh), 100u);
  ASSERT_TRUE(document.scene().set_mesh(100u,
      MeshComponent{.mesh = 10u, .collision_type = MeshCollisionType::Mesh}));
  add_scene(kFirstScene, "Scenes/first.qdr", document);
  open_project();
  open_scene();
  QuaderBridgeMeshView mesh{};
  ASSERT_EQ(api_->mesh_view(scene_, 0u, &mesh, &error_), 1) << error_.message;
  EXPECT_EQ(mesh.collision_status, QUADER_BRIDGE_COLLISION_TRIMESH_STATIC);
  ASSERT_EQ(mesh.collision_point_count, 6u);
  EXPECT_EQ(mesh.index_count, 6u);
  for (std::uint32_t first : {0u, 3u}) {
    EXPECT_FLOAT_EQ(mesh.collision_points[first].position[0], 0.0001f);
    EXPECT_FLOAT_EQ(mesh.collision_points[first + 1u].position[0], 1.0f);
    EXPECT_FLOAT_EQ(mesh.collision_points[first + 2u].position[1], 1.0f);
  }
}

TEST_F(BridgeReaderTests, EmptySupportedMeshHasNoCollisionFaces) {
  SceneContentDocument document({.id = 7u, .name = "Empty collision"});
  ASSERT_EQ(document.assets().add_mesh(10u, MeshAsset{}), 10u);
  ASSERT_EQ(document.scene().create_entity_with_id(100u, "Empty mesh",
                                                  EntityKind::Mesh), 100u);
  ASSERT_TRUE(document.scene().set_mesh(100u,
      MeshComponent{.mesh = 10u, .collision_type = MeshCollisionType::Mesh}));
  add_scene(kFirstScene, "Scenes/first.qdr", document);
  open_project();
  open_scene();
  QuaderBridgeMeshView mesh{};
  ASSERT_EQ(api_->mesh_view(scene_, 0u, &mesh, &error_), 1) << error_.message;
  EXPECT_EQ(mesh.collision_status, QUADER_BRIDGE_COLLISION_TRIMESH_STATIC);
  EXPECT_EQ(mesh.collision_point_count, 0u);
  EXPECT_EQ(mesh.index_count, 0u);
}

TEST_F(BridgeReaderTests, MeshSurfacesExposeIdentitiesBeforeReadingMissingQmat) {
  add_scene(kFirstScene, "Scenes/first.qdr", make_document(true),
           kProjectMaterial);
  open_project();
  open_scene();
  QuaderBridgeMeshView mesh{};
  ASSERT_EQ(api_->mesh_view(scene_, 0u, &mesh, &error_), 1) << error_.message;
  ASSERT_EQ(mesh.surface_count, 2u);
  EXPECT_NE(mesh.surfaces[0].material_index, mesh.surfaces[1].material_index);
  EXPECT_EQ(mesh.surfaces[0].index_count, 3u);
  EXPECT_EQ(mesh.surfaces[1].index_count, 3u);
  const auto embedded = material_index(QUADER_BRIDGE_MATERIAL_EMBEDDED);
  const auto project = material_index(QUADER_BRIDGE_MATERIAL_PROJECT);
  QuaderBridgeMaterialIdentity identity{};
  ASSERT_EQ(api_->material_identity(scene_, embedded, &identity, &error_), 1);
  EXPECT_STREQ(identity.id, "20");
  ASSERT_EQ(api_->material_identity(scene_, project, &identity, &error_), 1);
  EXPECT_STREQ(identity.id, kProjectMaterial);
  QuaderBridgeMaterialView material{};
  EXPECT_EQ(api_->material_view(scene_, project, &material, &error_), 0);
  EXPECT_NE(std::string(error_.message).find("surface.qmat"), std::string::npos)
      << error_.message;
  EXPECT_EQ(api_->read_texture(scene_, project, QUADER_BRIDGE_TEXTURE_ALBEDO,
                              &blob_, &error_), 0);
  EXPECT_EQ(blob_, nullptr);
  ASSERT_EQ(api_->material_identity(scene_, project, &identity, &error_), 1);
  EXPECT_STREQ(identity.id, kProjectMaterial);
}

TEST_F(BridgeReaderTests, MaterialMetadataDoesNotRequireTextureAndBlobOwnsBytes) {
  write_project_material();
  add_scene(kFirstScene, "Scenes/first.qdr", make_document(true),
           kProjectMaterial);
  open_project();
  open_scene();
  const auto index = material_index(QUADER_BRIDGE_MATERIAL_PROJECT);
  QuaderBridgeMaterialView material{};
  ASSERT_EQ(api_->material_view(scene_, index, &material, &error_), 1)
      << error_.message;
  EXPECT_FLOAT_EQ(material.roughness, 0.45f);
  EXPECT_FLOAT_EQ(material.metallic, 0.7f);
  EXPECT_FLOAT_EQ(material.normal_strength, 0.8f);
  EXPECT_EQ(material.normal_directx, 1u);
  EXPECT_FLOAT_EQ(material.uv_center[0], 0.25f);
  EXPECT_FLOAT_EQ(material.uv_scale[1], -1.0f);
  EXPECT_FLOAT_EQ(material.uv_offset[1], -0.3f);
  EXPECT_FLOAT_EQ(material.uv_rotation_degrees, 35.0f);
  EXPECT_EQ(material.textures[QUADER_BRIDGE_TEXTURE_ALBEDO].present, 1u);
  EXPECT_EQ(material.textures[QUADER_BRIDGE_TEXTURE_ALBEDO].srgb, 1u);
  EXPECT_STREQ(material.textures[QUADER_BRIDGE_TEXTURE_ALBEDO].id, kTexture);
  EXPECT_EQ(api_->read_texture(scene_, index, QUADER_BRIDGE_TEXTURE_ALBEDO,
                              &blob_, &error_), 0);
  EXPECT_NE(std::string(error_.message).find("albedo.PNG"), std::string::npos)
      << error_.message;
  const std::string bytes("\x89PNG\r\n\x1a\n\0fixture", 16u);
  write_bytes(root_ / "Textures" / "albedo.PNG", bytes);
  ASSERT_EQ(api_->read_texture(scene_, index, QUADER_BRIDGE_TEXTURE_ALBEDO,
                              &blob_, &error_), 1) << error_.message;
  api_->release_scene(std::exchange(scene_, nullptr));
  api_->release_project(std::exchange(project_, nullptr));
  QuaderBridgeBlobView blob{};
  ASSERT_EQ(api_->blob_view(blob_, &blob, &error_), 1);
  EXPECT_STREQ(blob.extension, ".png");
  EXPECT_EQ(blob.byte_count, bytes.size());
  ASSERT_NE(blob.bytes, nullptr);
  EXPECT_EQ(std::string(reinterpret_cast<const char *>(blob.bytes),
                        static_cast<std::size_t>(blob.byte_count)), bytes);
}

#if defined(_WIN32)
TEST_F(BridgeReaderTests, TextureReadsShareDeleteAccessAndReleaseSourceHandle) {
  write_project_material();
  add_scene(kFirstScene, "Scenes/first.qdr", make_document(true), kProjectMaterial);
  const auto texture_path = root_ / "Textures" / "albedo.PNG";
  write_bytes(texture_path, "original encoded texture");
  open_project();
  open_scene();
  const auto index = material_index(QUADER_BRIDGE_MATERIAL_PROJECT);
  const auto close_handle = [](void *handle) { ::CloseHandle(handle); };
  std::unique_ptr<void, decltype(close_handle)> delete_access(
      ::CreateFileW(texture_path.c_str(), DELETE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr),
      close_handle);
  ASSERT_NE(delete_access.get(), INVALID_HANDLE_VALUE);
  // Reciprocal sharing makes this fail deterministically if the reader does
  // not permit deletion, as required by concurrent atomic native saves.
  ASSERT_EQ(api_->read_texture(scene_, index, QUADER_BRIDGE_TEXTURE_ALBEDO,
                               &blob_, &error_), 1) << error_.message;
  delete_access.reset();
  const auto replacement = root_ / "Textures" / "replacement.PNG";
  write_bytes(replacement, "replacement encoded texture");
  ASSERT_TRUE(::MoveFileExW(replacement.c_str(), texture_path.c_str(),
                           MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH));
  QuaderBridgeBlobView view{};
  ASSERT_EQ(api_->blob_view(blob_, &view, &error_), 1) << error_.message;
  EXPECT_EQ(std::string(reinterpret_cast<const char *>(view.bytes),
                        static_cast<std::size_t>(view.byte_count)),
            "original encoded texture");
}
#endif

TEST_F(BridgeReaderTests, IncludesHiddenMeshesAndMirroredInstancePlacementsOnce) {
  auto document = make_document();
  auto &source = document.scene();
  ASSERT_TRUE(source.set_visible(100u, false));
  const auto definition = source.create_entity_with_id(
      200u, "Stored definition", EntityKind::Generic);
  const auto member = source.create_entity_with_id(
      201u, "Definition member", EntityKind::Mesh);
  ASSERT_TRUE(source.set_mesh(member, MeshComponent{.mesh = 10u}));
  ASSERT_TRUE(source.set_material(member, MaterialComponent{.material = 20u}));
  ASSERT_TRUE(source.set_parent(member, definition, false));
  ASSERT_TRUE(source.set_transform(member,
                                  TransformComponent{.position = {2, 0, 0}}));
  ASSERT_TRUE(source.set_instance_definition(definition));
  for (const EntityId id : {202u, 203u}) {
    ASSERT_EQ(source.create_entity_with_id(id, "Placed instance",
                                          EntityKind::Instance), id);
    ASSERT_TRUE(source.set_instance_occurrence(
        id, InstanceOccurrenceComponent{.definition = definition}));
    ASSERT_TRUE(source.set_transform(
        id, TransformComponent{.position = {10, 3, 4}, .scale = {-1, 2, 1}}));
  }
  ASSERT_TRUE(source.set_visible(203u, false));
  add_scene(kFirstScene, "Scenes/first.qdr", document);
  open_project();
  open_scene();
  QuaderBridgeSceneView scene_view{};
  ASSERT_EQ(api_->scene_view(scene_, &scene_view, &error_), 1);
  ASSERT_EQ(scene_view.mesh_count, 3u);
  std::array<bool, 3> found{};
  const QuaderBridgeCollisionPoint *shared_collision = nullptr;
  for (std::uint32_t index = 0u; index < scene_view.mesh_count; ++index) {
    QuaderBridgeMeshView mesh{};
    ASSERT_EQ(api_->mesh_view(scene_, index, &mesh, &error_), 1);
    EXPECT_EQ(mesh.collision_status, QUADER_BRIDGE_COLLISION_TRIMESH_STATIC);
    ASSERT_EQ(mesh.collision_point_count, 6u);
    ASSERT_NE(mesh.collision_points, nullptr);
    if (shared_collision == nullptr) {
      shared_collision = mesh.collision_points;
    }
    EXPECT_EQ(mesh.collision_points, shared_collision);
    EXPECT_FLOAT_EQ(mesh.collision_points[0].position[0], 0.0f);
    EXPECT_FLOAT_EQ(mesh.collision_points[1].position[0], 1.0f);
    if (mesh.display_entity == 100u) {
      found[0] = true;
      EXPECT_EQ(mesh.visible, 0u);
      continue;
    }
    ASSERT_TRUE(mesh.display_entity == 202u || mesh.display_entity == 203u);
    found[mesh.display_entity == 202u ? 1u : 2u] = true;
    EXPECT_EQ(mesh.geometry_entity, member);
    EXPECT_EQ(mesh.definition_entity, definition);
    EXPECT_EQ(mesh.visible, mesh.display_entity == 202u ? 1u : 0u);
    EXPECT_FLOAT_EQ(mesh.transform[0], -1.0f);
    EXPECT_FLOAT_EQ(mesh.transform[5], 2.0f);
    EXPECT_FLOAT_EQ(mesh.transform[12], 8.0f);
    EXPECT_FLOAT_EQ(mesh.transform[13], 3.0f);
    EXPECT_FLOAT_EQ(mesh.transform[14], 4.0f);
  }
  EXPECT_EQ(found, (std::array<bool, 3>{true, true, true}));
}

TEST_F(BridgeReaderTests, ComposesMemberAndOccurrenceUvMappings) {
  auto document = make_document(true);
  auto &source = document.scene();
  ASSERT_EQ(source.create_entity_with_id(200u, "Definition",
                                         EntityKind::Generic), 200u);
  ASSERT_TRUE(source.set_parent(100u, 200u, false));
  ASSERT_TRUE(source.set_instance_definition(200u));
  ASSERT_EQ(source.create_entity_with_id(202u, "Mapped instance",
                                         EntityKind::Instance), 202u);
  ASSERT_TRUE(source.set_instance_occurrence(
      202u, InstanceOccurrenceComponent{.definition = 200u}));
  TextureMappingComponent member_mapping;
  member_mapping.sampling_from_local.values[12] = 1.0f;
  TextureMappingComponent occurrence_mapping;
  occurrence_mapping.sampling_from_local.values[12] = 2.0f;
  ASSERT_TRUE(source.set_texture_mapping(100u, member_mapping));
  ASSERT_TRUE(source.set_texture_mapping(202u, occurrence_mapping));
  add_scene(kFirstScene, "Scenes/first.qdr", document, kProjectMaterial);
  open_project();
  open_scene();
  QuaderBridgeMeshView mesh{};
  ASSERT_EQ(api_->mesh_view(scene_, 0u, &mesh, &error_), 1) << error_.message;
  ASSERT_EQ(mesh.surface_count, 2u);
  ASSERT_EQ(mesh.collision_point_count, 6u);
  const std::array<std::array<float, 3>, 6> expected_collision = {{
      {0, 0, 0}, {1, 0, 0}, {0, 1, 0},
      {1, 0, 0}, {1, 1, 0}, {0, 1, 0},
  }};
  for (std::size_t corner = 0u; corner < expected_collision.size(); ++corner) {
    for (std::size_t axis = 0u; axis < 3u; ++axis) {
      EXPECT_FLOAT_EQ(mesh.collision_points[corner].position[axis],
                      expected_collision[corner][axis]);
    }
  }
  const auto embedded = material_index(QUADER_BRIDGE_MATERIAL_EMBEDDED);
  const QuaderBridgeSurface *triangle = nullptr;
  for (std::uint32_t index = 0u; index < mesh.surface_count; ++index) {
    if (mesh.surfaces[index].material_index == embedded) {
      triangle = &mesh.surfaces[index];
    }
  }
  ASSERT_NE(triangle, nullptr);
  ASSERT_EQ(triangle->index_count, 3u);
  for (std::uint32_t corner = 0u; corner < triangle->index_count; ++corner) {
    const auto &vertex = mesh.vertices[mesh.indices[triangle->index_offset + corner]];
    EXPECT_NEAR(vertex.uv[0], vertex.position[0] + 3.0f, 1e-5f);
    EXPECT_NEAR(vertex.uv[1], vertex.position[1], 1e-5f);
    EXPECT_NEAR(vertex.normal[2], 1.0f, 1e-5f);
  }
  const auto &a = mesh.vertices[mesh.indices[triangle->index_offset]];
  const auto &b = mesh.vertices[mesh.indices[triangle->index_offset + 1u]];
  const auto &c = mesh.vertices[mesh.indices[triangle->index_offset + 2u]];
  EXPECT_GT((b.position[0] - a.position[0]) * (c.position[1] - a.position[1]) -
                (b.position[1] - a.position[1]) * (c.position[0] - a.position[0]),
            0.0f);
}

TEST_F(BridgeReaderTests, RejectsSceneIdentityMismatchAndMalformedScene) {
  add_scene(kFirstScene, "Scenes/first.qdr", make_document());
  open_project();
  const auto replaced = QdrDocumentWriter::write_project_document_atomic(
      root_ / "Scenes" / "first.qdr", make_document(),
      scene_id(kSecondScene).bytes());
  ASSERT_TRUE(replaced.ok) << replaced.message;
  EXPECT_EQ(api_->open_scene(project_, kFirstScene, &scene_, &error_), 0);
  EXPECT_EQ(scene_, nullptr);
  EXPECT_NE(error_.message[0], '\0');
  write_bytes(root_ / "Scenes" / "first.qdr", "not a binary QDR document");
  EXPECT_EQ(api_->open_scene(project_, kFirstScene, &scene_, &error_), 0);
  EXPECT_EQ(scene_, nullptr);
  EXPECT_NE(error_.message[0], '\0');
}

TEST_F(BridgeReaderTests, RevalidatesManifestBeforeOpeningScene) {
  add_scene(kFirstScene, "Scenes/first.qdr", make_document());
  open_project();
  auto replacement = ProjectManifest::create(ProjectId::generate(),
                                             root_.filename().string());
  ASSERT_TRUE(replacement.ok());
  workspace_.manifest = replacement.take_value();
  ASSERT_TRUE(workspace_.manifest.register_scene(
      {.id = scene_id(kFirstScene),
       .path = relative_path("Scenes/first.qdr")}).ok());
  save_manifest();
  EXPECT_EQ(api_->open_scene(project_, kFirstScene, &scene_, &error_), 0);
  EXPECT_EQ(scene_, nullptr);
  write_bytes(workspace_.manifest_path, "{ invalid manifest");
  EXPECT_EQ(api_->open_scene(project_, kFirstScene, &scene_, &error_), 0);
  api_->release_project(std::exchange(project_, nullptr));
  EXPECT_EQ(api_->open_project(utf8_path(workspace_.manifest_path).c_str(),
                              &project_, &error_), 0);
  EXPECT_EQ(project_, nullptr);
}

TEST_F(BridgeReaderTests, ExportGodotAcceptanceFixtures) {
  write_project_material(kProjectMaterial, "Textures/albedo.PNG", true);
  write_project_material(kMissingMaterial, "Textures/missing.PNG");
  constexpr std::uint8_t kPng[] = {
      0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
      0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
      0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
      0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0x70, 0x68, 0x38, 0xf0,
      0x1f, 0x00, 0x05, 0x04, 0x02, 0x80, 0x3c, 0x9e, 0x03, 0x80, 0x00, 0x00,
      0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
  };
  write_bytes(root_ / "Textures" / "albedo.PNG",
              {reinterpret_cast<const char *>(kPng), sizeof(kPng)});
  auto document = make_document(true);
  auto &source = document.scene();
  ASSERT_TRUE(source.set_mesh(100u,
      MeshComponent{.mesh = 10u, .collision_type = MeshCollisionType::Mesh,
                    .display_collision = true}));
  ASSERT_EQ(source.create_entity_with_id(101u, "Hidden mesh", EntityKind::Mesh),
            101u);
  ASSERT_TRUE(source.set_mesh(101u, MeshComponent{.mesh = 10u}));
  ASSERT_TRUE(source.set_material(101u, *source.material(100u)));
  ASSERT_TRUE(source.set_visible(101u, false));
  ASSERT_TRUE(source.set_transform(101u,
      TransformComponent{.position = {3, 0, 0}, .scale = {1.5f, 2.0f, 0.75f}}));
  ASSERT_EQ(source.create_entity_with_id(200u, "Stored definition",
                                         EntityKind::Generic), 200u);
  ASSERT_EQ(source.create_entity_with_id(201u, "Definition member",
                                         EntityKind::Mesh), 201u);
  ASSERT_TRUE(source.set_mesh(201u, MeshComponent{.mesh = 10u}));
  ASSERT_TRUE(source.set_material(201u, *source.material(100u)));
  ASSERT_TRUE(source.set_parent(201u, 200u, false));
  ASSERT_TRUE(source.set_instance_definition(200u));
  ASSERT_EQ(source.create_entity_with_id(202u, "Mirrored instance",
                                         EntityKind::Instance), 202u);
  ASSERT_TRUE(source.set_instance_occurrence(
      202u, InstanceOccurrenceComponent{.definition = 200u}));
  ASSERT_TRUE(source.set_transform(
      202u, TransformComponent{.position = {6, 0, 0}, .scale = {-1, 2, 1}}));
  add_scene(kFirstScene, "Scenes/mixed_materials.qdr", document,
           kProjectMaterial);
  add_scene(kSecondScene, "Scenes/missing_texture.qdr",
           make_document(true, kMissingMaterial), kMissingMaterial);
  open_project();
  open_scene();
  QuaderBridgeSceneView view{};
  ASSERT_EQ(api_->scene_view(scene_, &view, &error_), 1) << error_.message;
  ASSERT_EQ(view.mesh_count, 3u);
  ASSERT_EQ(view.material_count, 2u);
  ASSERT_EQ(api_->read_texture(scene_, material_index(QUADER_BRIDGE_MATERIAL_PROJECT),
                              QUADER_BRIDGE_TEXTURE_ALBEDO, &blob_, &error_), 1)
      << error_.message;

  const char *configured = std::getenv("QUADER_BRIDGE_TEST_FIXTURE_DIR");
  if (configured == nullptr || *configured == '\0') {
    return;
  }
  const auto destination = std::filesystem::absolute(
      std::filesystem::path(configured)).lexically_normal();
  ASSERT_FALSE(destination.filename().empty());
  ASSERT_NE(destination, root_);
  ASSERT_NE(destination, destination.root_path());
  std::filesystem::create_directories(destination);
  ASSERT_TRUE(std::filesystem::is_empty(destination))
      << "Fixture export requires a new or empty output directory: "
      << destination.string();
  for (const char *directory : {"Scenes", "Materials", "Textures"}) {
    std::filesystem::copy(root_ / directory, destination / directory,
                          std::filesystem::copy_options::recursive);
  }
  auto exported_fs = ProjectFileSystem::open(destination);
  ASSERT_TRUE(exported_fs.ok()) << exported_fs.error().message;
  const auto catalog = workspace_.manifest.scenes();
  ProjectId exported_project_id = workspace_.manifest.id();
  if (const char *configured_id = std::getenv("QUADER_BRIDGE_TEST_PROJECT_ID");
      configured_id != nullptr && *configured_id != '\0') {
    const auto parsed = ProjectId::parse(configured_id);
    ASSERT_TRUE(parsed.ok()) << parsed.error().message;
    exported_project_id = parsed.value();
  }
  auto exported_manifest = ProjectManifest::hydrate(
      exported_project_id, utf8_path(destination.filename()),
      std::vector<ProjectSceneCatalogEntry>(catalog.begin(), catalog.end()),
      {}, std::nullopt);
  ASSERT_TRUE(exported_manifest.ok()) << exported_manifest.error().message;
  auto manifest_filename = destination.filename();
  manifest_filename += ".qproj";
  const auto saved = ProjectManifestStore::save_atomic(
      exported_fs.value(), destination / manifest_filename,
      exported_manifest.value());
  ASSERT_TRUE(saved.ok()) << saved.error().message;
  const auto reopened = ProjectManifestStore::open(destination / manifest_filename);
  ASSERT_TRUE(reopened.ok()) << reopened.error().message;
  {
    // Produce completion records through the same persistence owner as Ctrl+S.
    ProjectSession native_project(root_ / "export-recent-scenes.json");
    const auto opened = native_project.open(destination / manifest_filename);
    ASSERT_TRUE(opened.ok) << opened.message;
    for (const auto &entry : reopened.value().manifest.scenes()) {
      const auto opened_scene = native_project.open_scene(entry.path);
      ASSERT_TRUE(opened_scene.ok) << opened_scene.message;
      ASSERT_TRUE(opened_scene.scene_session_id.has_value());
      const auto committed = native_project.save(*opened_scene.scene_session_id);
      ASSERT_TRUE(committed.ok) << committed.message;
      EXPECT_TRUE(committed.warnings.empty()) << committed.message;
    }
  }
  write_bytes(destination / "expected.json",
      "{\n"
      "  \"valid_scene_id\": \"11111111-1111-4111-8111-111111111111\",\n"
      "  \"valid_scene_path\": \"Scenes/mixed_materials.qdr\",\n"
      "  \"valid_mesh_count\": 3,\n"
      "  \"surfaces_per_mesh\": 2,\n"
      "  \"visible_mesh_entity\": 100,\n"
      "  \"hidden_mesh_entity\": 101,\n"
      "  \"mirrored_mesh_entity\": 202,\n"
      "  \"collision_mesh_count\": 3,\n"
      "  \"collision_point_count\": 6,\n"
      "  \"missing_scene_id\": \"22222222-2222-4222-8222-222222222222\",\n"
      "  \"missing_scene_path\": \"Scenes/missing_texture.qdr\",\n"
      "  \"texture_path\": \"Textures/albedo.PNG\",\n"
      "  \"project_material_id\": \"33333333-3333-4333-8333-333333333333\"\n"
      "}\n");
  RecordProperty("godot_fixture_manifest", utf8_path(destination / manifest_filename));
}

TEST_F(BridgeReaderTests, SaveGodotAcceptanceFixture) {
  const char *configured = std::getenv("QUADER_BRIDGE_MUTATE_FIXTURE_DIR");
  if (configured == nullptr || *configured == '\0') {
    GTEST_SKIP() << "Only runs when an owned Godot acceptance fixture is supplied.";
  }
  const auto destination = std::filesystem::absolute(
      std::filesystem::path(configured)).lexically_normal();
  ASSERT_NE(destination, destination.root_path());
  ASSERT_TRUE(std::filesystem::is_regular_file(destination / "expected.json"))
      << "Mutations require an exported acceptance fixture.";
  auto manifest_filename = destination.filename();
  manifest_filename += ".qproj";
  const auto catalog = ProjectManifestStore::open(destination / manifest_filename);
  ASSERT_TRUE(catalog.ok()) << catalog.error().message;
  ASSERT_NE(catalog.value().manifest.find_scene(scene_id(kFirstScene)), nullptr);
  const char *configured_mode = std::getenv("QUADER_BRIDGE_MUTATION");
  const std::string mode = configured_mode == nullptr ? "scene" : configured_mode;
  unsigned revision = 1u;
  if (const char *configured_revision = std::getenv("QUADER_BRIDGE_MUTATION_REVISION");
      configured_revision != nullptr && *configured_revision != '\0') {
    const std::string_view text(configured_revision);
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), revision);
    ASSERT_EQ(parsed.ec, std::errc{});
    ASSERT_EQ(parsed.ptr, text.data() + text.size());
  }
  ASSERT_GE(revision, 1u);
  ASSERT_LE(revision, 50u);
  ProjectSession native_project(root_ / "mutation-recent-scenes.json");
  const auto opened = native_project.open(destination / manifest_filename);
  ASSERT_TRUE(opened.ok) << opened.message;
  if (mode == "baseline") {
    for (const auto &entry : catalog.value().manifest.scenes()) {
      const auto opened_scene = native_project.open_scene(entry.path);
      ASSERT_TRUE(opened_scene.ok) << opened_scene.message;
      ASSERT_TRUE(opened_scene.scene_session_id.has_value());
      const auto committed = native_project.save(*opened_scene.scene_session_id);
      ASSERT_TRUE(committed.ok) << committed.message;
      ASSERT_TRUE(committed.warnings.empty()) << committed.message;
    }
    return;
  }
  if (mode == "collision_enable" || mode == "collision_disable" ||
      mode == "collision_unsupported") {
    const auto opened_scene = native_project.open_scene(
        relative_path("Scenes/mixed_materials.qdr"));
    ASSERT_TRUE(opened_scene.ok) << opened_scene.message;
    ASSERT_TRUE(opened_scene.scene_session_id.has_value());
    auto *scene = native_project.find(*opened_scene.scene_session_id);
    ASSERT_NE(scene, nullptr);
    const auto *saved_metadata = scene->session->scene().mesh(100u);
    ASSERT_NE(saved_metadata, nullptr);
    MeshComponent metadata = *saved_metadata;
    metadata.generate_collisions = mode != "collision_disable";
    metadata.collision_type = mode == "collision_unsupported"
        ? MeshCollisionType::Box : MeshCollisionType::Mesh;
    metadata.body_type = MeshPhysicsBodyType::StaticBody;
    const auto changed = scene->session->document_commands().set_mesh_component(
        100u, metadata);
    ASSERT_TRUE(changed.ok) << changed.message;
    const auto committed = native_project.save(scene->session_id);
    ASSERT_TRUE(committed.ok) << committed.message;
    ASSERT_TRUE(committed.warnings.empty()) << committed.message;
    return;
  }
  if (mode == "scene" || mode == "rapid" || mode == "save_all") {
    const auto opened_scene = native_project.open_scene(
        relative_path("Scenes/mixed_materials.qdr"));
    ASSERT_TRUE(opened_scene.ok) << opened_scene.message;
    ASSERT_TRUE(opened_scene.scene_session_id.has_value());
    auto *scene = native_project.find(*opened_scene.scene_session_id);
    ASSERT_NE(scene, nullptr);
    const unsigned count = mode == "rapid" ? 3u : 1u;
    for (unsigned offset = 0u; offset < count; ++offset) {
      const auto changed = scene->session->document_commands().apply_entity_transform(
          100u, TransformComponent{.position = {static_cast<float>(revision + offset),
                                               0.0f, 0.0f}});
      ASSERT_TRUE(changed.ok) << changed.message;
      const auto committed = mode == "save_all"
          ? native_project.save_all() : native_project.save(scene->session_id);
      ASSERT_TRUE(committed.ok) << committed.message;
      ASSERT_TRUE(committed.warnings.empty()) << committed.message;
    }
    RecordProperty("saved_entity_100_x", std::to_string(revision + count - 1u));
    return;
  }
  ASSERT_TRUE(mode == "material" || mode == "texture" || mode == "failed_texture")
      << "Unknown acceptance mutation: " << mode;
  auto &editor = native_project.standard_material_editor();
  const auto opened_material = editor.open(*ProjectAssetId::parse(kProjectMaterial));
  ASSERT_TRUE(opened_material.ok) << opened_material.message;
  ASSERT_TRUE(editor.set_roughness(0.2f + 0.01f * static_cast<float>(revision)).ok);
  ASSERT_TRUE(editor.set_base_color(
      {0.1f + 0.01f * static_cast<float>(revision), 0.4f, 0.6f}).ok);
  ASSERT_TRUE(editor.set_texture_transform_component(
      StandardMaterialTextureTransformComponent::RotationDegrees,
      35.0f + static_cast<float>(revision)).ok);
  ASSERT_TRUE(editor.set_texture_transform_component(
      StandardMaterialTextureTransformComponent::OffsetU,
      0.2f + 0.01f * static_cast<float>(revision)).ok);
  if (mode == "texture") {
    QImage image(1, 1, QImage::Format_RGBA8888);
    image.fill(QColor(static_cast<int>(revision * 4u), 64, 192, 255));
    ASSERT_TRUE(image.save(QString::fromStdWString(
        (destination / "Textures" / "albedo.PNG").wstring()), "PNG"));
  } else if (mode == "failed_texture") {
    write_bytes(destination / "Textures" / "albedo.PNG", "invalid PNG payload");
  }
  const auto committed = editor.save();
  ASSERT_TRUE(committed.ok) << committed.message;
  RecordProperty("saved_material_revision", std::to_string(revision));
}

} // namespace
