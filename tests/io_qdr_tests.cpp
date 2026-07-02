////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "assets/material_document.hpp"
#include "assets/texture_asset.hpp"
#include "scene/document/project_document.hpp"
#include "io/qdr/qdr_document_reader.hpp"
#include "io/qdr/qdr_document_writer.hpp"
#include "io/qdr/qdr_document.hpp"
#include "io/qdr/qdr_format.hpp"
#include "io/qdr/qdr_project_codecs.hpp"
#include "support/guardrail_file_scan.hpp"

#include <gtest/gtest.h>
#include <mesh/polygon/document.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

using namespace quader;

namespace {

using quader::test::guardrail::read_file;
using quader::test::guardrail::read_source_file;
using quader::test::guardrail::source_files_under;
using quader::test::guardrail::source_path;

MeshAsset make_triangle_mesh(std::string name) {
  MeshAsset mesh;
  mesh.name = std::move(name);
  mesh.vertices = {
      MeshVertex{.position = {0.0f, 0.0f, 0.0f},
                 .normal = {0.0f, 1.0f, 0.0f},
                 .uv = {0.0f, 0.0f}},
      MeshVertex{.position = {1.0f, 0.0f, 0.0f},
                 .normal = {0.0f, 1.0f, 0.0f},
                 .uv = {1.0f, 0.0f}},
      MeshVertex{.position = {0.0f, 1.0f, 0.0f},
                 .normal = {0.0f, 1.0f, 0.0f},
                 .uv = {0.0f, 1.0f}},
  };
  mesh.indices = {0u, 1u, 2u};
  mesh.primitives = {MeshPrimitiveSpan{
      .index_offset = 0u, .index_count = 3u, .material_slot = 2u}};
  return mesh;
}

quader_poly::Document make_authored_polygon_document() {
  quader_poly::Document document;
  document.next_vertex_id = 13u;
  document.next_face_id = 21u;
  document.vertices = {
      quader_poly::Vertex{.id = 10u, .position = {0.0f, 0.0f, 0.0f}},
      quader_poly::Vertex{.id = 11u, .position = {1.0f, 0.0f, 0.0f}},
      quader_poly::Vertex{.id = 12u, .position = {0.0f, 1.0f, 0.0f}},
  };
  quader_poly::Face face;
  face.id = 20u;
  face.vertices = {10u, 11u, 12u};
  face.uvs = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}};
  face.material_slot = 2u;
  face.normal_shading = quader_poly::SurfaceShading::Authored;
  document.faces.push_back(std::move(face));
  document.hard_normal_edges.push_back(quader_poly::make_edge(10u, 11u));
  return document;
}

TextureAsset make_texture_asset(std::string name, TextureUsage usage,
                                TextureColorSpace color_space,
                                std::uint32_t width, std::uint32_t height,
                                std::string source_path,
                                bool generated = false) {
  TextureAsset texture;
  texture.name = std::move(name);
  texture.usage = usage;
  texture.color_space = color_space;
  texture.width = width;
  texture.height = height;
  texture.source_path = std::filesystem::path(std::move(source_path));
  texture.generated = generated;
  return texture;
}

void write_text_file(const std::filesystem::path &path, std::string_view text) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  ASSERT_TRUE(file.is_open()) << path.string();
  file.write(text.data(), static_cast<std::streamsize>(text.size()));
  file.close();
  ASSERT_TRUE(file.good()) << path.string();
}

std::filesystem::path unique_temp_directory(std::string_view label) {
  const auto tick = static_cast<std::uint64_t>(
      std::chrono::steady_clock::now().time_since_epoch().count());
  return std::filesystem::temp_directory_path() /
         ("quader-qdr-" + std::string(label) + "-" + std::to_string(tick));
}

/**
 * Owns a temporary QDR test directory and removes it when the test scope exits.
 */
class ScopedTempDirectory {
public:
  explicit ScopedTempDirectory(std::string_view label)
      : path_(unique_temp_directory(label)) {
    std::error_code error;
    std::filesystem::create_directories(path_, error);
    EXPECT_FALSE(error) << error.message();
  }

    ~ScopedTempDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

bool contains_temp_qdr_files(const std::filesystem::path &directory) {
  for (const std::filesystem::directory_entry &entry :
       std::filesystem::directory_iterator(directory)) {
    if (entry.path().filename().string().find(".tmp") != std::string::npos) {
      return true;
    }
  }
  return false;
}

std::string replace_once(std::string text, std::string_view from,
                         std::string_view to) {
  const std::size_t position = text.find(from);
  EXPECT_NE(position, std::string::npos);
  if (position == std::string::npos) {
    return text;
  }
  text.replace(position, from.size(), to);
  return text;
}

} // namespace

TEST(QdrDocumentIoTests, WritesAndReadsProjectDocumentPayloadWithStableIdsAndBindings) {
    ProjectDocument document(ProjectDocumentRecord{.id = 77u, .name = "Projection io"});

    const AssetId mesh_id =
        document.assets().add_mesh(12u, make_triangle_mesh("Triangle Mesh"));
    const AssetId base_color_texture_id = document.assets().add_texture(
        31u, make_texture_asset("Brick Albedo", TextureUsage::BaseColor,
                                TextureColorSpace::Srgb, 16u, 16u,
                                "textures/brick_albedo.png"));
    const AssetId roughness_texture_id = document.assets().add_texture(
        32u,
        make_texture_asset("Brick Roughness", TextureUsage::Roughness,
                           TextureColorSpace::Linear, 8u, 8u,
                           "textures/generated/brick_roughness.png", true));
    MaterialDocument material = make_default_quader_material(
        "Brick", base_color_texture_id, roughness_texture_id);
    material.base_color = {0.8f, 0.2f, 0.1f};
    material.roughness = 0.45f;
    const AssetId material_id =
        document.assets().add_material(22u, std::move(material));
    MaterialDocument accent_material = make_default_quader_material("Accent");
    accent_material.base_color = {0.1f, 0.8f, 0.2f};
    const AssetId accent_material_id =
        document.assets().add_material(23u, std::move(accent_material));

    const EntityId scene_root = document.scene().scene_root();
    ASSERT_NE(scene_root, kInvalidEntityId);
    SceneSettingsComponent scene_settings;
    scene_settings.environment.environment_id = "pillars_2k";
    scene_settings.environment.clear_color = {0.25f, 0.5f, 0.75f};
    scene_settings.environment.rotation_y = 1.25f;
    scene_settings.camera.near_plane = 0.2f;
    scene_settings.camera.far_plane = 2500.0f;
    scene_settings.post_process.dithering = false;
    scene_settings.post_process.tone_mapping = SceneToneMapping::DisplayRange;
    scene_settings.post_process.bloom = true;
    scene_settings.post_process.bloom_strength = 0.75f;
    ASSERT_TRUE(
        document.scene().set_scene_settings(scene_root, scene_settings));

    const EntityId group = document.scene().create_entity_with_id(
        100u, "Group", EntityKind::Empty);
    const EntityId wall =
        document.scene().create_entity_with_id(200u, "Wall", EntityKind::Mesh);
    const std::string group_uuid = document.scene().uuid(group);
    const std::string wall_uuid = document.scene().uuid(wall);
    const EditorObjectId group_object =
        document.scene().editor_object_id(group);
    document.scene().set_parent(wall, group, false);
    document.scene().set_transform(
        wall, TransformComponent{.position = {2.0f, 3.0f, 4.0f}});
    document.scene().set_modeling_object(
        wall, ModelingObjectComponent{.object_id = 42, .material_slot = 2u});
    document.scene().set_mesh(wall, MeshComponent{.mesh = mesh_id});
    document.scene().set_material(
        wall, MaterialComponent{
                  .material = material_id,
                  .material_slots = {{2u, accent_material_id}},
              });
    document.scene().set_selected(wall, true);
    document.scene().set_active_selection(wall);
    document.upsert_object_record(ProjectObjectRecord{
        .id = 900u,
        .scene_entity = wall,
        .mesh = mesh_id,
        .material = material_id,
        .modeling_object = 42,
        .authored_name = "Wall",
        .visible = true,
        .material_slot = 2u,
    });
    const EditorObjectId wall_object = document.scene().editor_object_id(wall);
    ASSERT_EQ(wall_object, 900u);

    const EntityId light = document.scene().create_entity_with_id(
        300u, "Key Light", EntityKind::Light);
    const std::string light_uuid = document.scene().uuid(light);
    document.scene().set_transform(
        light, TransformComponent{.position = {-2.0f, 5.0f, 1.0f}});
    document.scene().set_light(light, LightComponent{
                                          .type = LightType::Point,
                                          .color = {1.0f, 0.75f, 0.5f},
                                          .intensity = 950.0f,
                                          .range = 14.0f,
                                          .casts_shadows = true,
                                      });

    const std::string text =
        QdrDocumentWriter::serialize_project_document(document);
    EXPECT_NE(text.find("\"kind\": \"quader.project-document\""), std::string::npos);
    EXPECT_NE(text.find("\"uuid\": \"" + group_uuid + "\""), std::string::npos);
    EXPECT_NE(text.find("\"uuid\": \"" + wall_uuid + "\""), std::string::npos);
    EXPECT_NE(text.find("\"uuid\": \"" + light_uuid + "\""), std::string::npos);
    EXPECT_NE(text.find("\"editorObjectId\": " + std::to_string(group_object)),
              std::string::npos);
    EXPECT_NE(text.find("\"editorObjectId\": 900"), std::string::npos);
    EXPECT_NE(text.find("\"type\": \"point\""), std::string::npos);
    EXPECT_NE(text.find("\"range\": 14"), std::string::npos);
    EXPECT_NE(text.find("\"castsShadows\": true"), std::string::npos);
    EXPECT_NE(text.find("\"textures\": ["), std::string::npos);
    EXPECT_NE(text.find("\"baseColorTexture\": 31"), std::string::npos);
    EXPECT_NE(text.find("\"roughnessTexture\": 32"), std::string::npos);
    EXPECT_NE(text.find("\"environmentId\": \"pillars_2k\""), std::string::npos);
    EXPECT_NE(text.find("\"clearColor\""), std::string::npos);
    EXPECT_NE(text.find("\"nearPlane\": 0.2"), std::string::npos);
    EXPECT_NE(text.find("\"farPlane\": 2500"), std::string::npos);
    EXPECT_NE(text.find("\"dithering\": false"), std::string::npos);
    EXPECT_NE(text.find("\"toneMapping\": \"displayRange\""), std::string::npos);
    EXPECT_EQ(text.find("\"session\""), std::string::npos);

    QdrProjectReadResult result =
        QdrDocumentReader::deserialize_project_document(text);
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_FALSE(result.migrated_legacy_session);

    const ProjectDocument& loaded = result.document;
    EXPECT_EQ(loaded.id(), 77u);
    EXPECT_EQ(loaded.name(), "Projection io");
    EXPECT_TRUE(loaded.scene().exists(group));
    EXPECT_TRUE(loaded.scene().exists(wall));
    EXPECT_TRUE(loaded.scene().exists(light));
    EXPECT_EQ(loaded.scene().uuid(group), group_uuid);
    EXPECT_EQ(loaded.scene().uuid(wall), wall_uuid);
    EXPECT_EQ(loaded.scene().uuid(light), light_uuid);
    EXPECT_EQ(loaded.scene().entity_for_uuid(group_uuid), group);
    EXPECT_EQ(loaded.scene().entity_for_uuid(wall_uuid), wall);
    EXPECT_EQ(loaded.scene().editor_object_id(group), group_object);
    EXPECT_EQ(loaded.scene().editor_object_id(wall), wall_object);
    ASSERT_NE(loaded.scene().scene_settings(scene_root), nullptr);
    EXPECT_EQ(
        loaded.scene().scene_settings(scene_root)->environment.environment_id,
        "pillars_2k");
    EXPECT_FLOAT_EQ(
        loaded.scene().scene_settings(scene_root)->environment.clear_color.x,
        0.25f);
    EXPECT_FLOAT_EQ(
        loaded.scene().scene_settings(scene_root)->environment.clear_color.y,
        0.5f);
    EXPECT_FLOAT_EQ(
        loaded.scene().scene_settings(scene_root)->environment.clear_color.z,
        0.75f);
    EXPECT_FLOAT_EQ(
        loaded.scene().scene_settings(scene_root)->environment.rotation_y,
        1.25f);
    EXPECT_FLOAT_EQ(
        loaded.scene().scene_settings(scene_root)->camera.near_plane, 0.2f);
    EXPECT_FLOAT_EQ(loaded.scene().scene_settings(scene_root)->camera.far_plane,
                    2500.0f);
    EXPECT_FALSE(
        loaded.scene().scene_settings(scene_root)->post_process.dithering);
    EXPECT_EQ(
        loaded.scene().scene_settings(scene_root)->post_process.tone_mapping,
        SceneToneMapping::DisplayRange);
    EXPECT_TRUE(loaded.scene().scene_settings(scene_root)->post_process.bloom);
    EXPECT_FLOAT_EQ(
        loaded.scene().scene_settings(scene_root)->post_process.bloom_strength,
        0.75f);
    EXPECT_EQ(loaded.scene().parent(wall), group);
    ASSERT_NE(loaded.scene().transform(wall), nullptr);
    EXPECT_FLOAT_EQ(loaded.scene().transform(wall)->position.x, 2.0f);
    ASSERT_NE(loaded.scene().transform(light), nullptr);
    EXPECT_FLOAT_EQ(loaded.scene().transform(light)->position.y, 5.0f);
    ASSERT_NE(loaded.scene().light(light), nullptr);
    EXPECT_EQ(loaded.scene().light(light)->type, LightType::Point);
    EXPECT_FLOAT_EQ(loaded.scene().light(light)->color.y, 0.75f);
    EXPECT_FLOAT_EQ(loaded.scene().light(light)->intensity, 950.0f);
    EXPECT_FLOAT_EQ(loaded.scene().light(light)->range, 14.0f);
    EXPECT_TRUE(loaded.scene().light(light)->casts_shadows);
    ASSERT_NE(loaded.scene().mesh(wall), nullptr);
    EXPECT_EQ(loaded.scene().mesh(wall)->mesh, mesh_id);
    ASSERT_NE(loaded.scene().material(wall), nullptr);
    EXPECT_EQ(loaded.scene().material(wall)->material, material_id);
    EXPECT_EQ(loaded.scene().material(wall)->material_for_slot(2u),
              accent_material_id);
    ASSERT_NE(loaded.scene().modeling_object(wall), nullptr);
    EXPECT_EQ(loaded.scene().modeling_object(wall)->object_id, 42);
    EXPECT_EQ(loaded.scene().primary_selection(), wall);
    ASSERT_NE(loaded.assets().mesh(mesh_id), nullptr);
    EXPECT_EQ(loaded.assets().mesh(mesh_id)->indices.size(), 3u);
    ASSERT_NE(loaded.assets().material(material_id), nullptr);
    EXPECT_FLOAT_EQ(loaded.assets().material(material_id)->roughness, 0.45f);
    ASSERT_NE(loaded.assets().material(accent_material_id), nullptr);
    EXPECT_FLOAT_EQ(loaded.assets().material(accent_material_id)->base_color.y,
                    0.8f);
    ASSERT_NE(loaded.assets().texture(base_color_texture_id), nullptr);
    EXPECT_EQ(loaded.assets().texture(base_color_texture_id)->name,
              "Brick Albedo");
    EXPECT_EQ(loaded.assets().texture(base_color_texture_id)->usage,
              TextureUsage::BaseColor);
    EXPECT_EQ(loaded.assets().texture(base_color_texture_id)->color_space,
              TextureColorSpace::Srgb);
    EXPECT_EQ(loaded.assets().texture(base_color_texture_id)->width, 16u);
    EXPECT_EQ(loaded.assets().texture(base_color_texture_id)->height, 16u);
    EXPECT_EQ(loaded.assets()
                  .texture(base_color_texture_id)
                  ->source_path.generic_string(),
              "textures/brick_albedo.png");
    ASSERT_NE(loaded.assets().texture(roughness_texture_id), nullptr);
    EXPECT_EQ(loaded.assets().texture(roughness_texture_id)->usage,
              TextureUsage::Roughness);
    EXPECT_EQ(loaded.assets().texture(roughness_texture_id)->color_space,
              TextureColorSpace::Linear);
    EXPECT_TRUE(loaded.assets().texture(roughness_texture_id)->generated);
    ASSERT_TRUE(
        loaded.assets()
            .material(material_id)
            ->texture_slots.contains(material_texture_slot::kBaseColor));
    EXPECT_EQ(loaded.assets()
                  .material(material_id)
                  ->texture_slots.at(material_texture_slot::kBaseColor),
              base_color_texture_id);
    ASSERT_TRUE(
        loaded.assets()
            .material(material_id)
            ->texture_slots.contains(material_texture_slot::kRoughness));
    EXPECT_EQ(loaded.assets()
                  .material(material_id)
                  ->texture_slots.at(material_texture_slot::kRoughness),
              roughness_texture_id);
    const std::optional<ProjectObjectRecord> loaded_record =
        loaded.object_record(900u);
    ASSERT_TRUE(loaded_record.has_value());
    EXPECT_EQ(loaded_record->scene_entity, wall);
    const ProjectDocumentIdentityReport identity =
        loaded.validate_object_identity_invariants();
    EXPECT_TRUE(identity.ok()) << (identity.issues.empty() ? std::string{} : identity.issues.front().message);
}

TEST(QdrDocumentIoTests, WritesAndReadsUnreferencedProjectAssets) {
    ProjectDocument document(ProjectDocumentRecord{.id = 88u, .name = "Unreferenced Assets"});
    const AssetId mesh_id =
        document.assets().add_mesh(12u, make_triangle_mesh("Standalone Mesh"));
    const AssetId texture_id = document.assets().add_texture(
        31u, make_texture_asset("Unassigned Albedo", TextureUsage::BaseColor,
                                TextureColorSpace::Srgb, 4u, 4u,
                                "textures/unassigned.png"));
    const AssetId material_id = document.assets().add_material(
        22u, make_default_quader_material("Unassigned Material", texture_id,
                                          texture_id));
    ASSERT_NE(mesh_id, kInvalidAssetId);
    ASSERT_NE(texture_id, kInvalidAssetId);
    ASSERT_NE(material_id, kInvalidAssetId);

    const std::string text =
        QdrDocumentWriter::serialize_project_document(document);
    EXPECT_NE(text.find("\"id\": 12"), std::string::npos);
    EXPECT_NE(text.find("\"id\": 22"), std::string::npos);
    EXPECT_NE(text.find("\"id\": 31"), std::string::npos);

    QdrProjectReadResult result =
        QdrDocumentReader::deserialize_project_document(text);
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_NE(result.document.assets().mesh(mesh_id), nullptr);
    EXPECT_NE(result.document.assets().material(material_id), nullptr);
    EXPECT_NE(result.document.assets().texture(texture_id), nullptr);
}

TEST(QdrDocumentIoTests, AtomicProjectWriteUsesCompactJsonWhileDebugSerializerStaysPretty) {
    ProjectDocument document(ProjectDocumentRecord{.id = 90u, .name = "Compact Write"});
    const AssetId mesh_id =
        document.assets().add_mesh(12u, make_triangle_mesh("Triangle Mesh"));
    const EntityId mesh =
        document.scene().create_entity_with_id(200u, "Mesh", EntityKind::Mesh);
    ASSERT_NE(mesh, kInvalidEntityId);
    document.scene().set_mesh(mesh, MeshComponent{.mesh = mesh_id});

    const std::string pretty_text =
        QdrDocumentWriter::serialize_project_document(document);
    EXPECT_NE(pretty_text.find('\n'), std::string::npos);
    EXPECT_NE(pretty_text.find("\"kind\": \"quader.project-document\""),
              std::string::npos);

    ScopedTempDirectory directory("compact-project-write");
    const std::filesystem::path path = directory.path() / "compact.qdr";
    const QdrProjectWriteResult write =
        QdrDocumentWriter::write_project_document_atomic(path, document);
    ASSERT_TRUE(write.ok) << write.message;

    std::ifstream file(path, std::ios::binary);
    ASSERT_TRUE(file.is_open()) << path.string();
    const std::string saved_text((std::istreambuf_iterator<char>(file)),
                                 std::istreambuf_iterator<char>());
    EXPECT_EQ(saved_text.find('\n'), std::string::npos);
    EXPECT_NE(saved_text.find("\"kind\":\"quader.project-document\""),
              std::string::npos);

    const QdrProjectReadResult result =
        QdrDocumentReader::deserialize_project_document(saved_text);
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_NE(result.document.assets().mesh(mesh_id), nullptr);
    EXPECT_TRUE(result.document.scene().exists(mesh));
}

TEST(QdrDocumentIoTests, MissingMeshDoubleSidedFieldDefaultsToSingleSided) {
    ProjectDocument document(ProjectDocumentRecord{.id = 89u, .name = "Mesh Defaults"});
    const AssetId mesh_id =
        document.assets().add_mesh(12u, make_triangle_mesh("Triangle Mesh"));
    const EntityId wall =
        document.scene().create_entity_with_id(200u, "Wall", EntityKind::Mesh);
    document.scene().set_mesh(wall, MeshComponent{
                                        .mesh = mesh_id,
                                        .double_sided = true,
                                    });

    std::string text = QdrDocumentWriter::serialize_project_document(document);
    const std::size_t double_sided_key = text.find("\"doubleSided\": true");
    ASSERT_NE(double_sided_key, std::string::npos);
    const std::size_t line_begin = text.rfind('\n', double_sided_key);
    const std::size_t line_end = text.find('\n', double_sided_key);
    ASSERT_NE(line_begin, std::string::npos);
    ASSERT_NE(line_end, std::string::npos);
    text.erase(line_begin + 1u, line_end - line_begin);

    QdrProjectReadResult result =
        QdrDocumentReader::deserialize_project_document(text);
    ASSERT_TRUE(result.ok) << result.message;
    ASSERT_NE(result.document.scene().mesh(wall), nullptr);
    EXPECT_FALSE(result.document.scene().mesh(wall)->double_sided);
}

TEST(QdrDocumentIoTests, MissingEnvironmentClearColorDefaultsToSceneEnvironmentDefault) {
    ProjectDocument document(ProjectDocumentRecord{.id = 92u, .name = "Environment Defaults"});
    const EntityId scene_root = document.scene().scene_root();
    ASSERT_NE(scene_root, kInvalidEntityId);

    SceneSettingsComponent scene_settings;
    scene_settings.environment.clear_color = {0.25f, 0.5f, 0.75f};
    ASSERT_TRUE(
        document.scene().set_scene_settings(scene_root, scene_settings));

    std::string text = QdrDocumentWriter::serialize_project_document(document);
    const std::size_t clear_color_key = text.find("\"clearColor\"");
    ASSERT_NE(clear_color_key, std::string::npos);
    const std::size_t block_begin = text.rfind('\n', clear_color_key);
    ASSERT_NE(block_begin, std::string::npos);
    const std::size_t value_begin = text.find('{', clear_color_key);
    ASSERT_NE(value_begin, std::string::npos);
    int depth = 0;
    std::size_t block_end = value_begin;
    for (; block_end < text.size(); ++block_end) {
      if (text[block_end] == '{') {
        ++depth;
      } else if (text[block_end] == '}') {
        --depth;
        if (depth == 0) {
          ++block_end;
          break;
        }
      }
    }
    ASSERT_EQ(depth, 0);
    if (block_end < text.size() && text[block_end] == ',') {
      ++block_end;
    }
    if (block_end < text.size() && text[block_end] == '\n') {
      ++block_end;
    }
    text.erase(block_begin + 1u, block_end - (block_begin + 1u));

    QdrProjectReadResult result =
        QdrDocumentReader::deserialize_project_document(text);
    ASSERT_TRUE(result.ok) << result.message;
    ASSERT_NE(result.document.scene().scene_settings(scene_root), nullptr);
    const SceneSettingsComponent defaults;
    EXPECT_FLOAT_EQ(result.document.scene()
                        .scene_settings(scene_root)
                        ->environment.clear_color.x,
                    defaults.environment.clear_color.x);
    EXPECT_FLOAT_EQ(result.document.scene()
                        .scene_settings(scene_root)
                        ->environment.clear_color.y,
                    defaults.environment.clear_color.y);
    EXPECT_FLOAT_EQ(result.document.scene()
                        .scene_settings(scene_root)
                        ->environment.clear_color.z,
                    defaults.environment.clear_color.z);
}

TEST(QdrDocumentIoTests, ImportsLegacySessionPayloadButWritesProjectDocumentPayload) {
  const std::string text = read_source_file(
      {"tests", "fixtures", "qdr", "legacy_session_payload.qdr"});

  QdrProjectReadResult result =
      QdrDocumentReader::deserialize_project_document(text);
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_TRUE(result.migrated_legacy_session);

  const std::vector<EntityId> entities = result.document.scene().entities();
  ASSERT_EQ(entities.size(), 2u);
  const EntityId scene_root = result.document.scene().scene_root();
  ASSERT_NE(scene_root, kInvalidEntityId);
  const auto imported =
      std::find_if(entities.begin(), entities.end(),
                   [&](EntityId entity) { return entity != scene_root; });
  ASSERT_NE(imported, entities.end());
  const EntityId entity = *imported;
  EXPECT_NE(entity, static_cast<EntityId>(42u));
  ASSERT_NE(result.document.scene().modeling_object(entity), nullptr);
  EXPECT_EQ(result.document.scene().modeling_object(entity)->object_id, 42);
  EXPECT_EQ(result.document.scene().modeling_object(entity)->material_slot, 7u);
  ASSERT_NE(result.document.scene().mesh(entity), nullptr);
  const MeshAsset *mesh =
      result.document.assets().mesh(result.document.scene().mesh(entity)->mesh);
  ASSERT_NE(mesh, nullptr);
  EXPECT_GE(mesh->vertices.size(), 3u);
  EXPECT_TRUE(mesh->has_authored_polygon_document);
  ASSERT_TRUE(mesh->authored_polygon_document);
  EXPECT_EQ(mesh->authored_polygon_document.get()->faces.size(), 1u);

  const std::string rewritten =
      QdrDocumentWriter::serialize_project_document(result.document);
  EXPECT_NE(rewritten.find("\"kind\": \"quader.project-document\""),
            std::string::npos);
  EXPECT_EQ(rewritten.find("\"session\""), std::string::npos);

  QdrProjectReadResult round_trip =
      QdrDocumentReader::deserialize_project_document(rewritten);
  ASSERT_TRUE(round_trip.ok) << round_trip.message;
  EXPECT_FALSE(round_trip.migrated_legacy_session);
}

TEST(QdrDocumentIoTests, RejectsUnknownCanonicalProjectPayloadKindWithoutLegacyMigration) {
    const std::string text = R"json({
  "format": "quader.qdr",
  "version": 4,
  "document": {
    "kind": "quader.legacy-session"
  },
  "session": {
    "nextObjectId": 2,
    "activeObjectId": -1,
    "selection_mode": "object",
    "objects": []
  }
})json";

    QdrProjectReadResult result =
        QdrDocumentReader::deserialize_project_document(text);
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.migrated_legacy_session);
    EXPECT_NE(result.message.find("Unsupported QDR project document payload kind"), std::string::npos);
}

TEST(QdrDocumentIoTests, RootHeaderParsingUsesSharedHelperAcrossReaderSurfaces) {
  QdrProjectReadResult missing_root_project =
      QdrDocumentReader::deserialize_project_document("[]");
  EXPECT_FALSE(missing_root_project.ok);
  EXPECT_NE(missing_root_project.message.find("root must be an object"),
            std::string::npos);

  const std::string oversized_version = R"json({
  "format": "quader.qdr",
  "version": 2147483648,
  "document": {}
})json";
  QdrProjectReadResult oversized_project =
      QdrDocumentReader::deserialize_project_document(oversized_version);
  EXPECT_FALSE(oversized_project.ok);
  EXPECT_NE(
      oversized_project.message.find("outside the supported integer range"),
      std::string::npos);

  const quader_io::QdrReadResult missing_root_legacy =
      quader_io::deserialize_document("[]");
  EXPECT_FALSE(missing_root_legacy.ok);
  EXPECT_NE(missing_root_legacy.message.find("root must be an object"),
            std::string::npos);

  const quader_io::QdrReadResult oversized_legacy =
      quader_io::deserialize_document(oversized_version);
  EXPECT_FALSE(oversized_legacy.ok);
  EXPECT_NE(
      oversized_legacy.message.find("outside the supported integer range"),
      std::string::npos);

  const std::string qdr_root_header = read_source_file("src/io/qdr/qdr_root.hpp");
  const std::string legacy_reader =
      read_source_file("src/io/qdr/qdr_document.cpp");
  const std::string project_reader =
      read_source_file("src/io/qdr/qdr_document_reader.cpp");
  EXPECT_NE(qdr_root_header.find("QdrRootHeader"), std::string::npos);
  EXPECT_NE(legacy_reader.find("read_qdr_root_header(root)"),
            std::string::npos);
  EXPECT_NE(project_reader.find("read_qdr_root_header(root)"),
            std::string::npos);
  EXPECT_EQ(legacy_reader.find("read_version_header"), std::string::npos);
  EXPECT_EQ(project_reader.find("readVersionHeader"), std::string::npos);
}

TEST(QdrDocumentIoTests, RejectsCanonicalProjectPayloadWithDanglingObjectRecordEntity) {
    ProjectDocument document(ProjectDocumentRecord{.id = 78u, .name = "Dangling Record"});
    const EntityId entity = document.scene().create_entity_with_id(
        200u, "Object", EntityKind::Mesh);
    document.upsert_object_record(ProjectObjectRecord{
        .id = 900u,
        .scene_entity = entity,
        .authored_name = "Object",
    });

    std::string text = QdrDocumentWriter::serialize_project_document(document);
    text = replace_once(text, "\"sceneEntity\": 200", "\"sceneEntity\": 999");

    QdrProjectReadResult result =
        QdrDocumentReader::deserialize_project_document(text);
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.migrated_legacy_session);
    EXPECT_NE(result.message.find("object record points at a missing scene entity"), std::string::npos);
}

TEST(QdrDocumentIoTests, RejectsCanonicalProjectPayloadWithMismatchedEditorObjectRecordId) {
    ProjectDocument document(ProjectDocumentRecord{.id = 80u, .name = "Conflicting Record"});
    const EntityId first =
        document.scene().create_entity_with_id(100u, "First", EntityKind::Mesh);
    const EntityId second = document.scene().create_entity_with_id(
        200u, "Second", EntityKind::Mesh);
    const EditorObjectId first_object =
        document.scene().editor_object_id(first);
    const EditorObjectId second_object =
        document.scene().editor_object_id(second);
    ASSERT_NE(first_object, kInvalidEditorObjectId);
    ASSERT_NE(second_object, kInvalidEditorObjectId);
    ASSERT_NE(first_object, second_object);
    document.upsert_object_record(ProjectObjectRecord{
        .id = first_object,
        .scene_entity = first,
        .authored_name = "First",
    });
    document.upsert_object_record(ProjectObjectRecord{
        .id = second_object,
        .scene_entity = second,
        .authored_name = "Second",
    });

    std::string text = QdrDocumentWriter::serialize_project_document(document);
    text =
        replace_once(text,
                     "\"editorObjectId\": " + std::to_string(second_object) +
                         ",\n        \"sceneEntity\": 200",
                     "\"editorObjectId\": 999,\n        \"sceneEntity\": 200");

    QdrProjectReadResult result =
        QdrDocumentReader::deserialize_project_document(text);
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.migrated_legacy_session);
    EXPECT_NE(result.message.find("does not match the scene editor object id"), std::string::npos);
}

TEST(QdrDocumentIoTests, RejectsCanonicalProjectPayloadWithMissingMeshAssetReference) {
    const std::string text = R"json({
  "format": "quader.qdr",
  "version": 4,
  "document": {
    "kind": "quader.project-document",
    "project": {
      "id": 79,
      "name": "Missing Mesh"
    },
    "assets": {
      "meshes": [],
      "materials": []
    },
    "entities": [
      {
        "id": 200,
        "name": "Object",
        "kind": "mesh",
        "parent": 0,
        "mesh": {
          "mesh": 404
        }
      }
    ],
    "objects": []
  }
})json";

    QdrProjectReadResult result =
        QdrDocumentReader::deserialize_project_document(text);
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.migrated_legacy_session);
    EXPECT_NE(result.message.find("entity references a missing mesh asset"), std::string::npos);
}

TEST(QdrDocumentIoTests, RejectsCanonicalProjectPayloadWithDanglingMaterialTextureReference) {
    const std::string text = R"json({
  "format": "quader.qdr",
  "version": 4,
  "document": {
    "kind": "quader.project-document",
    "project": {
      "id": 81,
      "name": "Missing Texture"
    },
    "assets": {
      "meshes": [],
      "materials": [
        {
          "id": 22,
          "name": "Textured Material",
          "baseColor": {
            "x": 1,
            "y": 1,
            "z": 1
          },
          "roughness": 1,
          "metallic": 0,
          "textureSlots": {
            "baseColorTexture": 404
          },
          "nodeGraphReserved": true
        }
      ],
      "textures": []
    },
    "entities": [],
    "objects": []
  }
})json";

    QdrProjectReadResult result =
        QdrDocumentReader::deserialize_project_document(text);
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.migrated_legacy_session);
    EXPECT_NE(result.message.find("Texture asset does not exist"), std::string::npos);
}

TEST(QdrDocumentIoTests, MigratesLegacyMaterialTextureSlotAliasesToCanonicalSlots) {
    const std::string text = R"json({
  "format": "quader.qdr",
  "version": 4,
  "document": {
    "kind": "quader.project-document",
    "project": {
      "id": 83,
      "name": "Aliased Texture Slot"
    },
    "assets": {
      "meshes": [],
      "materials": [
        {
          "id": 22,
          "name": "Textured Material",
          "baseColor": {
            "x": 1,
            "y": 1,
            "z": 1
          },
          "roughness": 1,
          "metallic": 0,
          "textureSlots": {
            "baseColor": 31
          },
          "nodeGraphReserved": true
        }
      ],
      "textures": [
        {
          "id": 31,
          "name": "Base Texture",
          "usage": "baseColor",
          "colorSpace": "srgb",
          "width": 1,
          "height": 1,
          "sourcePath": "",
          "generated": true
        }
      ]
    },
    "entities": [
      {
        "id": 200,
        "name": "Material Entity",
        "kind": "generic",
        "parent": 0,
        "material": {
          "material": 22
        }
      }
    ],
    "objects": []
  }
})json";

    QdrProjectReadResult result =
        QdrDocumentReader::deserialize_project_document(text);
    ASSERT_TRUE(result.ok) << result.message;
    const MaterialDocument *material = result.document.assets().material(22u);
    ASSERT_NE(material, nullptr);
    ASSERT_TRUE(
        material->texture_slots.contains(material_texture_slot::kBaseColor));
    EXPECT_EQ(material->texture_slots.at(material_texture_slot::kBaseColor),
              31u);
    EXPECT_FALSE(material->texture_slots.contains("baseColor"));

    const std::string rewritten =
        QdrDocumentWriter::serialize_project_document(result.document);
    EXPECT_NE(rewritten.find("\"baseColorTexture\": 31"), std::string::npos);
    EXPECT_EQ(rewritten.find("\"baseColor\": 31"), std::string::npos);
}

TEST(QdrDocumentIoTests, RejectsCanonicalProjectPayloadWithUnsupportedMaterialTextureSlot) {
    const std::string text = R"json({
  "format": "quader.qdr",
  "version": 4,
  "document": {
    "kind": "quader.project-document",
    "project": {
      "id": 84,
      "name": "Unsupported Texture Slot"
    },
    "assets": {
      "meshes": [],
      "materials": [
        {
          "id": 22,
          "name": "Textured Material",
          "baseColor": {
            "x": 1,
            "y": 1,
            "z": 1
          },
          "roughness": 1,
          "metallic": 0,
          "textureSlots": {
            "normalTexture": 31
          },
          "nodeGraphReserved": true
        }
      ],
      "textures": [
        {
          "id": 31,
          "name": "Normal Texture",
          "usage": "normal",
          "colorSpace": "linear",
          "width": 1,
          "height": 1,
          "sourcePath": "",
          "generated": true
        }
      ]
    },
    "entities": [],
    "objects": []
  }
})json";

    QdrProjectReadResult result =
        QdrDocumentReader::deserialize_project_document(text);
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.migrated_legacy_session);
    EXPECT_NE(result.message.find("Unsupported material texture slot"), std::string::npos);
}

TEST(QdrDocumentIoTests, RegistryRejectsDanglingMaterialTextureBeforeProjectWrite) {
    ProjectDocument document(ProjectDocumentRecord{.id = 82u, .name = "Bad Texture Ref"});
    MaterialDocument material = make_default_quader_material("Broken Material");
    material.texture_slots[material_texture_slot::kBaseColor] = 404u;
    const AssetId material_id =
        document.assets().add_material(22u, std::move(material));
    EXPECT_EQ(material_id, kInvalidAssetId);
    EXPECT_EQ(document.assets().material_count(), 0u);
}

TEST(QdrDocumentIoTests, RejectsCanonicalProjectPayloadWithDuplicateMeshAssetIds) {
    const std::string text = R"json({
  "format": "quader.qdr",
  "version": 4,
  "document": {
    "kind": "quader.project-document",
    "project": {
      "id": 85,
      "name": "Duplicate Mesh"
    },
    "assets": {
      "meshes": [
        {
          "id": 12,
          "name": "First",
          "has_stable_cpu_topology": true,
          "vertices": [],
          "indices": [],
          "primitives": []
        },
        {
          "id": 12,
          "name": "Second",
          "has_stable_cpu_topology": true,
          "vertices": [],
          "indices": [],
          "primitives": []
        }
      ],
      "materials": [],
      "textures": []
    },
    "entities": [],
    "objects": []
  }
})json";

    QdrProjectReadResult result =
        QdrDocumentReader::deserialize_project_document(text);
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.message.find("duplicate mesh asset ids"), std::string::npos);
}

TEST(QdrDocumentIoTests, RejectsCanonicalProjectPayloadWithDuplicateMaterialAssetIds) {
    const std::string text = R"json({
  "format": "quader.qdr",
  "version": 4,
  "document": {
    "kind": "quader.project-document",
    "project": {
      "id": 86,
      "name": "Duplicate Material"
    },
    "assets": {
      "meshes": [],
      "materials": [
        {
          "id": 22,
          "name": "First",
          "baseColor": {"x": 1, "y": 1, "z": 1},
          "roughness": 1,
          "metallic": 0,
          "textureSlots": {},
          "nodeGraphReserved": true
        },
        {
          "id": 22,
          "name": "Second",
          "baseColor": {"x": 1, "y": 1, "z": 1},
          "roughness": 1,
          "metallic": 0,
          "textureSlots": {},
          "nodeGraphReserved": true
        }
      ],
      "textures": []
    },
    "entities": [],
    "objects": []
  }
})json";

    QdrProjectReadResult result =
        QdrDocumentReader::deserialize_project_document(text);
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.message.find("duplicate material asset ids"), std::string::npos);
}

TEST(QdrDocumentIoTests, NormalizesMeshPrimitiveSpansAtQdrBoundary) {
    ProjectDocument document(ProjectDocumentRecord{.id = 87u, .name = "Primitive Normalize"});
    MeshAsset mesh = make_triangle_mesh("Invalid Primitive Mesh");
    mesh.primitives = {MeshPrimitiveSpan{
        .index_offset = 12u, .index_count = 3u, .material_slot = 7u}};
    const AssetId mesh_id = document.assets().add_mesh(12u, std::move(mesh));
    ASSERT_NE(mesh_id, kInvalidAssetId);

    const std::string text =
        QdrDocumentWriter::serialize_project_document(document);
    EXPECT_NE(text.find("\"indexOffset\": 0"), std::string::npos);
    EXPECT_NE(text.find("\"indexCount\": 3"), std::string::npos);
    EXPECT_NE(text.find("\"materialSlot\": 0"), std::string::npos);
    EXPECT_EQ(text.find("\"indexOffset\": 12"), std::string::npos);

    QdrProjectReadResult result =
        QdrDocumentReader::deserialize_project_document(text);
    ASSERT_TRUE(result.ok) << result.message;
    const MeshAsset *loaded = result.document.assets().mesh(mesh_id);
    ASSERT_NE(loaded, nullptr);
    ASSERT_EQ(loaded->primitives.size(), 1u);
    EXPECT_EQ(loaded->primitives.front().index_offset, 0u);
    EXPECT_EQ(loaded->primitives.front().index_count, 3u);
    EXPECT_EQ(loaded->primitives.front().material_slot, 0u);
}

TEST(QdrDocumentIoTests, SceneSelectablePersistsThroughQdrUsingSceneOwnedMutation) {
    ProjectDocument document(ProjectDocumentRecord{.id = 90u, .name = "Selectable Persistence"});
    const EntityId entity = document.scene().create_entity_with_id(
        200u, "Pick Masked", EntityKind::Mesh);
    ASSERT_NE(entity, kInvalidEntityId);
    const std::uint64_t before = document.scene().revision();
    EXPECT_TRUE(document.scene().set_selectable(entity, false));
    EXPECT_GT(document.scene().revision(), before);

    const std::string text =
        QdrDocumentWriter::serialize_project_document(document);
    EXPECT_NE(text.find("\"selectable\": false"), std::string::npos);

    QdrProjectReadResult result =
        QdrDocumentReader::deserialize_project_document(text);
    ASSERT_TRUE(result.ok) << result.message;
    ASSERT_TRUE(result.document.scene().exists(entity));
    const VisibilityComponent* visibility = result.document.scene().visibility(entity);
    ASSERT_NE(visibility, nullptr);
    EXPECT_TRUE(visibility->visible);
    EXPECT_FALSE(visibility->selectable);
}

TEST(QdrDocumentIoTests, SharedPolygonDocumentCodecPreservesAuthoredNormalShadingAcrossProjectAndLegacyQdr) {
  const quader_poly::Document polygon = make_authored_polygon_document();

  ProjectDocument project(
      ProjectDocumentRecord{.id = 91u, .name = "Authored Normals"});
  MeshAsset authored_mesh = make_triangle_mesh("Authored Polygon Mesh");
  authored_mesh.name = "Authored Polygon Mesh";
  authored_mesh.has_authored_polygon_document = true;
  authored_mesh.authored_polygon_document =
      std::make_shared<quader_poly::Document>(polygon);
  const AssetId mesh_id =
      project.assets().add_mesh(12u, std::move(authored_mesh));
  ASSERT_NE(mesh_id, kInvalidAssetId);

  const std::string project_text =
      QdrDocumentWriter::serialize_project_document(project);
  EXPECT_NE(project_text.find("\"vertices\": []"), std::string::npos);
  EXPECT_NE(project_text.find("\"indices\": []"), std::string::npos);
  EXPECT_NE(project_text.find("\"primitives\": []"), std::string::npos);
  EXPECT_NE(project_text.find("\"normalShading\": \"authored\""),
            std::string::npos);
  QdrProjectReadResult project_round_trip =
      QdrDocumentReader::deserialize_project_document(project_text);
  ASSERT_TRUE(project_round_trip.ok) << project_round_trip.message;
  const MeshAsset *loaded_mesh =
      project_round_trip.document.assets().mesh(mesh_id);
  ASSERT_NE(loaded_mesh, nullptr);
  EXPECT_FALSE(loaded_mesh->vertices.empty());
  EXPECT_FALSE(loaded_mesh->indices.empty());
  ASSERT_TRUE(loaded_mesh->authored_polygon_document);
  ASSERT_EQ(loaded_mesh->authored_polygon_document->faces.size(), 1u);
  EXPECT_EQ(
      loaded_mesh->authored_polygon_document->faces.front().normal_shading,
      quader_poly::SurfaceShading::Authored);

  quader_io::QdrObject object;
  object.id = 1;
  object.material_id = 2;
  object.selected = true;
  object.document = polygon;
  object.selection.mode = quader_poly::SelectionMode::Face;
  object.selection.faces = {20u};
  object.selection.has_active = true;
  object.selection.active_kind = quader_poly::ElementKind::Face;
  object.selection.active_face = 20u;

  quader_io::QdrDocument legacy;
  legacy.objects.push_back(std::move(object));
  legacy.next_object_id = 2;
  legacy.active_object_id = 1;
  legacy.selection_mode = quader_io::QdrSelectionMode::Face;

  const std::string legacy_text = quader_io::serialize_document(legacy);
  EXPECT_NE(legacy_text.find("\"normalShading\": \"authored\""),
            std::string::npos);
  const quader_io::QdrReadResult legacy_round_trip =
      quader_io::deserialize_document(legacy_text);
  ASSERT_TRUE(legacy_round_trip.ok) << legacy_round_trip.message;
  ASSERT_EQ(legacy_round_trip.document.objects.front().document.faces.size(),
            1u);
  EXPECT_EQ(legacy_round_trip.document.objects.front()
                .document.faces.front()
                .normal_shading,
            quader_poly::SurfaceShading::Authored);
}

TEST(QdrDocumentIoTests, QdrPolygonDocumentCodecAndAtomicWriteHaveSingleOwners) {
  const std::string reader =
      read_source_file({"src", "io", "qdr", "qdr_document_reader.cpp"});
  const std::string writer =
      read_source_file({"src", "io", "qdr", "qdr_document_writer.cpp"});
  const std::string legacy =
      read_source_file({"src", "io", "qdr", "qdr_document.cpp"});
  const std::string codec =
      read_source_file({"src", "io", "qdr", "qdr_polygon_document_codec.cpp"});
  const std::string atomic =
      read_source_file({"src", "io", "qdr", "qdr_atomic_write.cpp"});

  EXPECT_NE(reader.find("#include \"io/qdr/qdr_polygon_document_codec.hpp\""),
            std::string::npos);
  EXPECT_NE(writer.find("#include \"io/qdr/qdr_polygon_document_codec.hpp\""),
            std::string::npos);
  EXPECT_NE(legacy.find("#include \"io/qdr/qdr_polygon_document_codec.hpp\""),
            std::string::npos);
  EXPECT_NE(writer.find("#include \"io/qdr/qdr_atomic_write.hpp\""),
            std::string::npos);
  EXPECT_NE(legacy.find("#include \"io/qdr/qdr_atomic_write.hpp\""),
            std::string::npos);

  EXPECT_NE(codec.find("qdr_polygon_document_to_json"), std::string::npos);
  EXPECT_NE(codec.find("qdr_polygon_document_from_json"), std::string::npos);
  EXPECT_NE(atomic.find("write_qdr_text_atomic"), std::string::npos);
  EXPECT_NE(atomic.find("make_temp_path_for_atomic_write"), std::string::npos);

  EXPECT_EQ(reader.find("polygonDocumentFromJson("), std::string::npos);
  EXPECT_EQ(reader.find("polygonVec2FromJson("), std::string::npos);
  EXPECT_EQ(reader.find("parseSurfaceShading("), std::string::npos);
  EXPECT_EQ(writer.find("polygonDocumentToJson("), std::string::npos);
  EXPECT_EQ(writer.find("polygonVec2ToJson("), std::string::npos);
  EXPECT_EQ(writer.find("surfaceShadingName("), std::string::npos);
  EXPECT_EQ(legacy.find("\njson document_to_json(const quader_poly::Document&"),
            std::string::npos);
  EXPECT_EQ(
      legacy.find(
          "\nquader_poly::Document document_from_json(const json& value)"),
      std::string::npos);

  const char *atomic_owner_tokens[] = {
      "unique_temp_suffix(",
      "make_temp_path_for_atomic_write(",
      "replace_with_temp_file(",
      "MoveFileExW(",
  };
  for (const char *token : atomic_owner_tokens) {
    EXPECT_EQ(writer.find(token), std::string::npos) << token;
    EXPECT_EQ(legacy.find(token), std::string::npos) << token;
    EXPECT_NE(atomic.find(token), std::string::npos) << token;
  }
}

TEST(QdrDocumentIoTests, QdrFormatConstantsAndProjectCodecsHaveSingleOwners) {
  EXPECT_EQ(quader_io::kQdrFormatName, kQdrFormatName);
  EXPECT_EQ(quader_io::kQdrDocumentPayloadKey, kQdrDocumentPayloadKey);
  EXPECT_EQ(quader_io::kQdrLegacySessionPayloadKey,
            kQdrLegacySessionPayloadKey);
  EXPECT_EQ(quader_io::kQdrMinSupportedFormatVersion,
            kQdrMinSupportedFormatVersion);
  EXPECT_EQ(quader_io::kQdrFormatVersion, kQdrLegacySessionFormatVersion);

  EXPECT_EQ(qdr_entity_kind_name(EntityKind::Mesh), std::string_view("mesh"));
  EXPECT_EQ(qdr_entity_kind_from_name("light"), EntityKind::Light);
  EXPECT_EQ(qdr_light_type_name(LightType::Spot), std::string_view("spot"));
  EXPECT_EQ(qdr_light_type_from_name("point"), LightType::Point);
  EXPECT_EQ(qdr_mesh_collision_type_name(MeshCollisionType::ConvexHull),
            std::string_view("convexHull"));
  EXPECT_EQ(qdr_mesh_collision_type_from_name("mesh"), MeshCollisionType::Mesh);
  EXPECT_EQ(qdr_mesh_physics_body_type_name(MeshPhysicsBodyType::RigidBody),
            std::string_view("rigidBody"));
  EXPECT_EQ(qdr_mesh_physics_body_type_from_name("missing"),
            MeshPhysicsBodyType::StaticBody);
  EXPECT_EQ(qdr_texture_usage_name(TextureUsage::BaseColor),
            std::string_view("baseColor"));
  EXPECT_EQ(qdr_texture_usage_from_name("roughness"), TextureUsage::Roughness);
  EXPECT_EQ(qdr_texture_color_space_name(TextureColorSpace::Linear),
            std::string_view("linear"));
  EXPECT_EQ(qdr_texture_color_space_from_name("srgb"), TextureColorSpace::Srgb);

  const std::string cmake = read_source_file("CMakeLists.txt");
  const std::string legacy_header =
      read_source_file({"src", "io", "qdr", "qdr_document.hpp"});
  const std::string reader =
      read_source_file({"src", "io", "qdr", "qdr_document_reader.cpp"});
  const std::string writer =
      read_source_file({"src", "io", "qdr", "qdr_document_writer.cpp"});
  const std::string codec_header =
      read_source_file({"src", "io", "qdr", "qdr_project_codecs.hpp"});
  const std::string codec =
      read_source_file({"src", "io", "qdr", "qdr_project_codecs.cpp"});

  EXPECT_NE(cmake.find("src/io/qdr/qdr_project_codecs.cpp"), std::string::npos);
  EXPECT_NE(legacy_header.find("kQdrFormatName = quader::kQdrFormatName"),
            std::string::npos);
  EXPECT_NE(legacy_header.find("kQdrDocumentPayloadKey"), std::string::npos);
  EXPECT_NE(legacy_header.find("quader::kQdrDocumentPayloadKey"),
            std::string::npos);
  EXPECT_NE(legacy_header.find(
                "kQdrFormatVersion = quader::kQdrLegacySessionFormatVersion"),
            std::string::npos);

  EXPECT_NE(reader.find("#include \"io/qdr/qdr_project_codecs.hpp\""),
            std::string::npos);
  EXPECT_NE(writer.find("#include \"io/qdr/qdr_project_codecs.hpp\""),
            std::string::npos);
  EXPECT_NE(reader.find("qdr_entity_kind_from_name("), std::string::npos);
  EXPECT_NE(reader.find("qdr_texture_usage_from_name("), std::string::npos);
  EXPECT_NE(writer.find("qdr_entity_kind_name("), std::string::npos);
  EXPECT_NE(writer.find("qdr_texture_usage_name("), std::string::npos);

  EXPECT_NE(codec_header.find("qdr_entity_kind_name("), std::string::npos);
  EXPECT_NE(codec.find("qdr_entity_kind_name("), std::string::npos);
  EXPECT_EQ(reader.find("parseEntityKind("), std::string::npos);
  EXPECT_EQ(reader.find("parseTextureUsage("), std::string::npos);
  EXPECT_EQ(reader.find("parseTextureColorSpace("), std::string::npos);
  EXPECT_EQ(writer.find("entityKindName("), std::string::npos);
  EXPECT_EQ(writer.find("textureUsageName("), std::string::npos);
  EXPECT_EQ(writer.find("textureColorSpaceName("), std::string::npos);
}

TEST(QdrDocumentIoTests, SceneSelectableMutationStaysSceneOwned) {
  const std::string scene_header =
      read_source_file({"src", "scene", "scene_document.hpp"});
  const std::string scene_source =
      read_source_file({"src", "scene", "scene_document.cpp"});
  const std::string qdr_reader =
      read_source_file({"src", "io", "qdr", "qdr_document_reader.cpp"});

  EXPECT_NE(
      scene_header.find("bool set_selectable(EntityId id, bool selectable);"),
      std::string::npos);
  EXPECT_NE(scene_source.find("bool SceneDocument::set_selectable"),
            std::string::npos);
  EXPECT_NE(qdr_reader.find("document.scene().set_selectable("),
            std::string::npos);
  EXPECT_EQ(qdr_reader.find("->selectable ="), std::string::npos);

  const std::filesystem::path source_root = source_path("src");
  for (const std::filesystem::path &file : source_files_under(source_root)) {
    if (file.filename() == "scene_document.cpp") {
      continue;
    }
    const std::string source = read_file(file);
    EXPECT_EQ(source.find("->selectable ="), std::string::npos)
        << file.string();
  }
}

TEST(ProjectionOwnershipGuardrailTests, ProjectionIsReadOnlyAndModelingOwnsSceneSync) {
    const std::string cmake = read_source_file("CMakeLists.txt");
    EXPECT_NE(cmake.find("src/editor/projection/mesh_projection_service.cpp"), std::string::npos);
    EXPECT_EQ(cmake.find("src/editor/projection/modeling_scene_reconcile_service.cpp"), std::string::npos);
    EXPECT_NE(cmake.find("src/editor/projection/picking_cache_projection.cpp"), std::string::npos);
    EXPECT_NE(cmake.find("src/editor/projection/selection_overlay_projection.cpp"), std::string::npos);
    EXPECT_NE(cmake.find("src/editor/modeling/services/modeling_mesh_sync_service.cpp"), std::string::npos);

    const std::string services =
        read_source_file({"src", "editor", "modeling", "services",
                          "modeling_session_services.cpp"});
    EXPECT_EQ(services.find("ModelingSceneReconcileService::reconcile_modeling_object_scene_nodes"), std::string::npos);
    EXPECT_EQ(services.find("MeshProjectionService::project_modeling_meshes_to_scene"), std::string::npos);
    EXPECT_NE(services.find("PickingCacheProjection::scene_transformed_objects_for_picking"), std::string::npos);
    EXPECT_NE(services.find("PickingCacheProjection::object_face_geometry_signature"), std::string::npos);

    const std::string mesh_sync =
        read_source_file({"src", "editor", "modeling", "services",
                          "modeling_mesh_sync_service.cpp"});
    EXPECT_NE(mesh_sync.find("void reconcile_modeling_object_scene_nodes"),
              std::string::npos);
    EXPECT_NE(mesh_sync.find("void project_modeling_meshes_to_scene"),
              std::string::npos);
    EXPECT_NE(mesh_sync.find("scene.create_entity"), std::string::npos);
    EXPECT_NE(mesh_sync.find("scene.destroy_entity"), std::string::npos);
    EXPECT_NE(mesh_sync.find("assets.add_mesh"), std::string::npos);
    EXPECT_NE(mesh_sync.find("assets.replace_mesh"), std::string::npos);
    EXPECT_NE(mesh_sync.find("projected_mesh_signature_known(object.id)"),
              std::string::npos);
    EXPECT_NE(mesh_sync.find("Modeling mesh sync replace changed mesh asset"),
              std::string::npos);

    const char *forbidden_projection_mutations[] = {
        "AssetRegistry",
        "SceneModelingSyncService",
        "create_entity(",
        "create_entity_with_id(",
        "destroy_entity(",
        "set_modeling_object(",
        "set_mesh(",
        "set_material(",
        "add_mesh(",
        "replace_mesh(",
        ".bind(",
        ".unbind_object(",
        "mark_scene_objects_synced(",
        "clear_selection(",
        "set_selected(",
    };
    const std::filesystem::path projection_root =
        source_path({"src", "editor", "projection"});
    for (const std::filesystem::path &file :
         source_files_under(projection_root)) {
      const std::string source = read_file(file);
      for (const char *forbidden : forbidden_projection_mutations) {
        EXPECT_EQ(source.find(forbidden), std::string::npos)
            << file.string() << " contains projection-owned mutation token "
            << forbidden;
      }
    }

    const std::string facade =
        read_source_file({"src", "editor", "modeling", "services",
                          "modeling_session_service.cpp"});
    const std::string overlay_service =
        read_source_file({"src", "editor", "modeling", "services",
                          "modeling_session_services.cpp"});
    EXPECT_EQ(facade.find("SelectionOverlayProjection::project_selection_overlay_frame"), std::string::npos);
    EXPECT_NE(overlay_service.find(
                  "SelectionOverlayProjection::project_selection_overlay_frame"),
              std::string::npos);
}

TEST(ProjectionOwnershipGuardrailTests, RenderDoesNotDependOnProjectionOrModelingOwners) {
  const std::filesystem::path render_root = source_path({"src", "render"});
  for (const std::filesystem::path &file : source_files_under(render_root)) {
    const std::string source = read_file(file);
    EXPECT_EQ(source.find("editor/projection/"), std::string::npos)
        << file.string();
    EXPECT_EQ(source.find("#include \"modeling/"), std::string::npos)
        << file.string();
  }
}
