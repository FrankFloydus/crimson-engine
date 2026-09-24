////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "../tools/qdr_retired_schema10_converter.hpp"

#include "api/quader_api_type_conversions.hpp"
#include "api/quader_editor_api.hpp"
#include "editor/session/project_session.hpp"
#include "io/qdr/qdr_document_reader.hpp"
#include "io/qdr/qdr_document_writer.hpp"
#include "io/shared_file_read.hpp"
#include "qdr_scene_generated.h"
#include "qdr_scene_usage_schema10_generated.h"

#include <quader/modeling/io/qdr_document.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace quader {
namespace {

namespace retired = ::quader::qdr::usage_schema10::fb;
namespace current = ::quader::qdr::fb;
constexpr const char *kMaterialId = "44444444-4444-4444-8444-444444444444";

/** Selects real malformed/unsupported input domains of the isolated converter. */
struct RetiredMigrationFixtureOptions final {
  bool missing_root = false;
  bool duplicate_root = false;
  bool parented = false;
  bool embedded_material = false;
  bool corrupt_topology = false;
  bool nonfinite_transform = false;
  bool compiled_geometry = false;
  bool invalid_index = false;
};

std::vector<std::byte> flatbuffer_bytes(const flatbuffers::FlatBufferBuilder &builder) {
  const auto *begin = reinterpret_cast<const std::byte *>(builder.GetBufferPointer());
  return {begin, begin + builder.GetSize()};
}

std::vector<std::byte> retired_geometry(RetiredMigrationFixtureOptions options) {
  flatbuffers::FlatBufferBuilder builder;
  if (options.compiled_geometry) {
    const std::array vertices{
        retired::MeshVertex({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}, 0xff112233u),
        retired::MeshVertex({1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}, 0xff445566u),
        retired::MeshVertex({0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}, 0xff778899u)};
    const std::array<std::uint32_t, 3> indices{0u, 1u, options.invalid_index ? 99u : 2u};
    const std::array primitives{retired::MeshPrimitive(0u, 3u, 3u)};
    const auto stored_vertices = builder.CreateVectorOfStructs(vertices.data(), vertices.size());
    const auto stored_indices = builder.CreateVector(indices.data(), indices.size());
    const auto stored_primitives = builder.CreateVectorOfStructs(primitives.data(), primitives.size());
    const auto root = retired::CreateGeometryPayload(builder, retired::GeometryKind::CompiledMesh,
                                                    stored_vertices, stored_indices, stored_primitives);
    builder.Finish(root, "QDRG");
  } else {
    auto document = modeling::PolygonDocument::make_box({}).or_throw();
    (void)document.assign_face_material_slot(document.face_ids().front(), 3u).or_throw();
    auto bytes = modeling::serialize_qdr_authored_topology(document).or_throw();
    if (options.corrupt_topology) {
      bytes.front() = std::byte{0};
    }
    const auto payload = builder.CreateVector(reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size());
    const auto root = retired::CreateGeometryPayload(builder, retired::GeometryKind::AuthoredPolygon,
                                                    {}, {}, {}, payload);
    builder.Finish(root, "QDRG");
  }
  return flatbuffer_bytes(builder);
}

quader_io::QdrBinaryContainerReadResult retired_scene(
    RetiredMigrationFixtureOptions options = {},
    ProductRelease minimum_product_release = kInternalFullRelease) {
  flatbuffers::FlatBufferBuilder builder;
  const auto material_id = builder.CreateString(kMaterialId);
  const std::array slots{retired::CreateProjectMaterialSlotBinding(builder, 3u, material_id)};
  const auto material = retired::CreateMaterialComponent(
      builder, options.embedded_material ? 77u : 0u, {}, material_id,
      builder.CreateVector(slots.data(), slots.size()));
  const auto mesh = retired::CreateMeshComponent(builder, 50u, true, true, false,
                                               retired::MeshCollisionType::Sphere, true,
                                               retired::MeshPhysicsBodyType::RigidBody);
  const retired::Vec3 position(options.nonfinite_transform ? std::numeric_limits<float>::infinity() : 3.0f,
                               -4.0f, 5.0f);
  const retired::Vec3 rotation(0.2f, 0.3f, 0.4f);
  const retired::Vec3 scale(2.0f, 3.0f, 4.0f);
  const auto transform = retired::CreateTransformComponent(builder, &position, &rotation, &scale, true);
  const auto visibility = retired::CreateVisibilityComponent(builder, false, false);
  const auto lock = retired::CreateLockComponent(builder, true);
  const auto modeling = retired::CreateModelingObjectComponent(builder, 11, 3u, true, 1u);
  const std::array<float, 16> matrix{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                      0.0f, 0.0f, 1.0f, 0.0f, 7.0f, 8.0f, 9.0f, 1.0f};
  const auto mapping = retired::CreateTextureMappingComponent(builder, builder.CreateVector(matrix.data(), matrix.size()));
  std::vector<flatbuffers::Offset<retired::Entity>> entities;
  for (std::uint64_t index = 0u; index < 2u; ++index) {
    const auto uuid = builder.CreateString(index == 0u ? "11111111-1111-4111-8111-111111111111"
                                                     : "22222222-2222-4222-8222-222222222222");
    const auto name = builder.CreateString(index == 0u ? "First mesh" : "Second mesh");
    retired::EntityBuilder entity(builder);
    entity.add_id(101u + index);
    entity.add_uuid(uuid);
    entity.add_editor_object_id(201u + index);
    entity.add_name(name);
    entity.add_kind(retired::EntityKind::Mesh);
    entity.add_parent(options.parented && index == 1u ? 101u : 0u);
    entity.add_transform(transform);
    entity.add_visibility(visibility);
    entity.add_lock(lock);
    if (index == 0u && !options.compiled_geometry) {
      entity.add_modeling_object(modeling);
    }
    entity.add_mesh(mesh);
    entity.add_material(material);
    entity.add_layer_id(7u);
    entity.add_texture_mapping(mapping);
    entities.push_back(entity.Finish());
  }
  const std::array meshes{retired::CreateMeshAsset(builder, 50u, builder.CreateString("Shared geometry"),
                                                 true, !options.compiled_geometry, 1u)};
  std::vector<std::uint64_t> roots{102u, 101u};
  if (options.missing_root) {
    roots.pop_back();
  }
  if (options.duplicate_root) {
    roots[1] = roots[0];
  }
  const retired::Vec3 color(0.1f, 0.2f, 0.3f);
  const std::array layers{retired::CreateSceneLayer(builder, 7u, builder.CreateString("Preserved layer"), false, &color, true)};
  const std::array dependencies{retired::CreateProjectAssetDependency(
      builder, material_id, builder.CreateString("materials/shared.qmat"), retired::ProjectAssetKind::Material)};
  const retired::Vec3 eye(5.0f, 6.0f, 7.0f), target(0.0f, 0.0f, 0.0f), up(0.0f, 1.0f, 0.0f);
  const auto camera = retired::CreateViewportCameraPose(builder, &eye, &target, &up);
  const auto scene = retired::CreateScenePayload(
      builder, 31u, builder.CreateString("Retired migration fixture"),
      builder.CreateVector(meshes.data(), meshes.size()), {}, {}, builder.CreateVector(entities),
      builder.CreateVector(roots), builder.CreateVector(dependencies.data(), dependencies.size()),
      builder.CreateVector(layers.data(), layers.size()), camera);
  retired::FinishScenePayloadBuffer(builder, scene);
  auto metadata = flatbuffer_bytes(builder);
  auto geometry = retired_geometry(options);
  std::array<std::byte, 16> uuid{};
  uuid.front() = std::byte{0x42};
  return quader_io::read_qdr_binary_container(quader_io::build_qdr_binary_container(uuid, {
      {.type = quader_io::QdrBinaryChunkType::SceneMetadata, .schema_version = 10u, .id = 0u,
       .content_hash = quader_io::qdr_binary_content_hash(metadata), .bytes = std::move(metadata)},
      {.type = quader_io::QdrBinaryChunkType::Geometry, .schema_version = 2u, .id = 1u,
       .content_hash = quader_io::qdr_binary_content_hash(geometry), .bytes = std::move(geometry)}},
      minimum_product_release));
}

TEST(QdrMigrationTests, RetiredConversionPreservesMinimumProductRelease) {
  for (const ProductRelease required :
       {kPublicRelease01, product_release(0u, 2u), kInternalFullRelease}) {
    const auto source = retired_scene({}, required);
    ASSERT_TRUE(source.ok) << source.message;
    const auto snapshot = migration::decode_retired_schema10(source);
    EXPECT_EQ(snapshot.minimum_product_release, required);
    const auto converted = QdrDocumentReader::deserialize_project_document(
        QdrDocumentWriter::serialize_project_document_bytes(snapshot));
    ASSERT_TRUE(converted.ok) << converted.message;
    EXPECT_EQ(converted.minimum_product_release, required);
  }
}

TEST(QdrMigrationTests, RetiredAuthoredScenePreservesIdentityOrderComponentsAndDependencies) {
  const auto source = retired_scene();
  ASSERT_TRUE(source.ok) << source.message;
  const auto snapshot = migration::decode_retired_schema10(source);
  EXPECT_EQ(snapshot.project_id, 31u);
  EXPECT_EQ(snapshot.project_name, "Retired migration fixture");
  EXPECT_EQ(snapshot.root_entities, (std::vector<EntityId>{102u, 101u}));
  ASSERT_EQ(snapshot.entities.size(), 2u);
  const auto &entity = snapshot.entities[1];
  EXPECT_EQ(entity.id, 101u);
  EXPECT_EQ(entity.editor_object_id, 201u);
  EXPECT_EQ(entity.uuid, "11111111-1111-4111-8111-111111111111");
  EXPECT_EQ(entity.parent, kInvalidEntityId);
  EXPECT_EQ(entity.sibling_ordinal, 1u);
  EXPECT_EQ(entity.transform.position.x, 3.0f);
  EXPECT_EQ(entity.transform.rotation_euler.z, 0.4f);
  EXPECT_EQ(entity.transform.scale.y, 3.0f);
  EXPECT_TRUE(entity.transform.custom_pivot);
  EXPECT_FALSE(entity.visibility.visible);
  EXPECT_FALSE(entity.visibility.selectable);
  EXPECT_TRUE(entity.lock.locked);
  ASSERT_TRUE(entity.mesh.has_value());
  EXPECT_TRUE(entity.mesh->editor_only);
  EXPECT_TRUE(entity.mesh->cast_shadows);
  EXPECT_TRUE(entity.mesh->double_sided);
  EXPECT_FALSE(entity.mesh->generate_collisions);
  EXPECT_TRUE(entity.mesh->display_collision);
  EXPECT_EQ(entity.mesh->collision_type, MeshCollisionType::Sphere);
  EXPECT_EQ(entity.mesh->body_type, MeshPhysicsBodyType::RigidBody);
  ASSERT_TRUE(entity.modeling_object.has_value());
  EXPECT_EQ(entity.modeling_object->object_id, 11);
  EXPECT_TRUE(entity.modeling_object->sdk_owned);
  EXPECT_EQ(entity.modeling_object->local_frame_version, 1u);
  ASSERT_TRUE(entity.texture_mapping.has_value());
  EXPECT_EQ(entity.texture_mapping->sampling_from_local.values[12], 7.0f);
  ASSERT_TRUE(entity.material.has_value());
  EXPECT_EQ(entity.material->material.project_asset().str(), kMaterialId);
  EXPECT_EQ(entity.material->material_slots.at(3u).project_asset().str(), kMaterialId);
  ASSERT_EQ(snapshot.layers.size(), 1u);
  EXPECT_EQ(snapshot.layers.front().id, 7u);
  EXPECT_FALSE(snapshot.layers.front().visible);
  EXPECT_TRUE(snapshot.layers.front().locked);
  ASSERT_EQ(snapshot.project_asset_dependencies.size(), 1u);
  EXPECT_EQ(snapshot.project_asset_dependencies.front().path.portable_string(), "materials/shared.qmat");
  ASSERT_TRUE(snapshot.viewport_camera_pose.has_value());
  EXPECT_EQ(snapshot.viewport_camera_pose->eye.z, 7.0f);
  EXPECT_EQ(snapshot.scene_settings, SceneSettings{});

  const auto bytes = QdrDocumentWriter::serialize_project_document_bytes(snapshot, source.scene_uuid);
  const auto read = QdrDocumentReader::deserialize_project_document(bytes);
  ASSERT_TRUE(read.ok) << read.message;
  EXPECT_EQ(read.scene_uuid, source.scene_uuid);
  EXPECT_EQ(read.document.scene().entities().size(), 2u);
  auto replay = qdr_project_write_snapshot_from_document(read.document);
  ASSERT_TRUE(replay.ok());
  replay.value().project_asset_dependencies = read.project_asset_dependencies;
  replay.value().viewport_camera_pose = read.viewport_camera_pose;
  EXPECT_EQ(QdrDocumentWriter::serialize_project_document_bytes(replay.value(), read.scene_uuid), bytes);
}

TEST(QdrMigrationTests, RetiredCompiledGeometryPreservesVerticesIndicesAndMaterialSlots) {
  const auto source = retired_scene({.compiled_geometry = true});
  const auto snapshot = migration::decode_retired_schema10(source);
  ASSERT_EQ(snapshot.meshes.size(), 1u);
  const auto &bytes = *snapshot.meshes.front().encoded_geometry;
  const auto *geometry = flatbuffers::GetRoot<current::GeometryPayload>(bytes.data());
  ASSERT_EQ(geometry->mesh_vertices()->size(), 3u);
  EXPECT_EQ(geometry->mesh_vertices()->Get(1u)->position().x(), 1.0f);
  EXPECT_EQ(geometry->mesh_vertices()->Get(2u)->rgba(), 0xff778899u);
  EXPECT_EQ(geometry->mesh_indices()->Get(2u), 2u);
  EXPECT_EQ(geometry->mesh_primitives()->Get(0u)->material_slot(), 3u);
}

TEST(QdrMigrationTests, RejectsMissingAndDuplicatedRootIds) {
  EXPECT_THROW((void)migration::decode_retired_schema10(retired_scene({.missing_root = true})), std::runtime_error);
  EXPECT_THROW((void)migration::decode_retired_schema10(retired_scene({.duplicate_root = true})), std::runtime_error);
}

TEST(QdrMigrationTests, RejectsUnsupportedHierarchyAndEmbeddedMaterialWithoutLosingData) {
  EXPECT_THROW((void)migration::decode_retired_schema10(retired_scene({.parented = true})), std::runtime_error);
  EXPECT_THROW((void)migration::decode_retired_schema10(retired_scene({.embedded_material = true})), std::runtime_error);
}

TEST(QdrMigrationTests, RejectsInvalidGeometryAndNonfiniteTransforms) {
  EXPECT_THROW((void)migration::decode_retired_schema10(retired_scene({.corrupt_topology = true})), std::runtime_error);
  EXPECT_THROW((void)migration::decode_retired_schema10(retired_scene({.nonfinite_transform = true})), std::runtime_error);
  EXPECT_THROW((void)migration::decode_retired_schema10(retired_scene({.compiled_geometry = true, .invalid_index = true})), std::runtime_error);
}

TEST(QdrMigrationTests, RejectsOtherMetadataOrGeometrySchemaVersions) {
  auto source = retired_scene();
  source.chunks.front().schema_version = 15u;
  EXPECT_THROW((void)migration::decode_retired_schema10(source), std::runtime_error);
  source = retired_scene();
  source.chunks.back().schema_version = 1u;
  EXPECT_THROW((void)migration::decode_retired_schema10(source), std::runtime_error);
}

/** Owns only the newly allocated test directory, never the supplied project. */
class ScopedMigrationProjectCopy final {
public:
  ScopedMigrationProjectCopy()
      : parent_(std::filesystem::canonical(std::filesystem::temp_directory_path())),
        path_(parent_ / ("quader-rootless-migration-" + std::string(SceneId::generate().str()))) {
    if (!std::filesystem::create_directory(path_)) {
      throw std::runtime_error("Could not allocate the migration verification directory.");
    }
  }

  ~ScopedMigrationProjectCopy() {
    std::error_code ignored;
    if (path_.parent_path() == parent_ &&
        path_.filename().string().starts_with("quader-rootless-migration-")) {
      std::filesystem::remove_all(path_, ignored);
    }
  }

  [[nodiscard]] const std::filesystem::path &path() const { return path_; }

private:
  std::filesystem::path parent_;
  std::filesystem::path path_;
};

TEST(QdrMigrationTests, ConvertedProjectScenesOpenThroughPublicApis) {
  const char *configured = std::getenv("QUADER_ROOTLESS_MIGRATION_PROJECT");
  if (configured == nullptr || *configured == '\0') {
    GTEST_SKIP() << "Set QUADER_ROOTLESS_MIGRATION_PROJECT to verify a converted .qproj on a disposable copy.";
  }
  const auto source_manifest = std::filesystem::canonical(std::filesystem::path(configured));
  ASSERT_TRUE(std::filesystem::is_regular_file(source_manifest));
  const auto source_root = source_manifest.parent_path();
  ScopedMigrationProjectCopy temporary;
  const auto copied_root = temporary.path() / source_manifest.stem();
  ASSERT_TRUE(std::filesystem::create_directory(copied_root));

  std::vector<std::pair<std::filesystem::path, std::uint64_t>> original_hashes;
  std::vector<std::filesystem::path> scene_paths;
  for (const auto &entry : std::filesystem::recursive_directory_iterator(source_root)) {
    ASSERT_FALSE(entry.is_symlink()) << "Fixture copy refuses symbolic links: " << entry.path().string();
    const auto relative = entry.path().lexically_relative(source_root);
    const auto destination = copied_root / relative;
    if (entry.is_directory()) {
      std::filesystem::create_directories(destination);
    } else if (entry.is_regular_file()) {
      const auto original = quader_io::read_file_bytes_shared(entry.path());
      ASSERT_TRUE(original.ok) << original.message;
      original_hashes.emplace_back(entry.path(), quader_io::qdr_binary_content_hash(original.bytes));
      std::filesystem::create_directories(destination.parent_path());
      ASSERT_TRUE(std::filesystem::copy_file(entry.path(), destination));
      if (relative.begin() != relative.end() && *relative.begin() == "Scenes" &&
          relative.extension() == ".qdr") {
        scene_paths.push_back(relative);
      }
    }
  }
  ASSERT_FALSE(scene_paths.empty()) << "The supplied project has no Scenes/*.qdr files.";
  std::sort(scene_paths.begin(), scene_paths.end());

  ProjectSession project(temporary.path() / "recent-scenes.json");
  QuaderEditorApi editor(project);
  const auto opened_project = editor.project().open(copied_root / source_manifest.filename());
  ASSERT_TRUE(opened_project.ok()) << opened_project.error_message();
  for (const auto &relative : scene_paths) {
    SCOPED_TRACE(relative.string());
    const auto expected = QdrDocumentReader::read_project_document(source_root / relative);
    ASSERT_TRUE(expected.ok) << expected.message;
    const auto portable = ProjectRelativePath::parse(relative);
    ASSERT_TRUE(portable.ok()) << portable.error().message;
    const auto opened_scene = editor.scene_documents().open(portable.value());
    ASSERT_TRUE(opened_scene.ok()) << opened_scene.error_message();
    const auto activated = editor.scene_documents().activate(opened_scene.value());
    ASSERT_TRUE(activated.ok()) << activated.error_message();
    const auto active = editor.scene_documents().active_scene();
    ASSERT_TRUE(active.has_value());
    EXPECT_EQ(active->scene_id.bytes(), expected.scene_uuid);
    EXPECT_EQ(active->path, portable.value());
    EXPECT_FALSE(active->dirty);
    EXPECT_EQ(to_scene_settings(editor.query().scene_settings()), expected.document.scene().scene_settings());

    std::vector<EntityId> expected_public_order;
    std::vector<EntityId> expected_root_order;
    const auto &scene = expected.document.scene();
    for (const EntityId id : scene.entity_tree_order()) {
      const auto actual = editor.query().entity(id);
      ASSERT_TRUE(actual.has_value()) << "Missing entity " << id;
      EXPECT_EQ(actual->uuid, scene.uuid(id));
      EXPECT_EQ(actual->editor_object_id, scene.editor_object_id(id));
      EXPECT_EQ(actual->parent, scene.parent(id));
      EXPECT_EQ(actual->kind, to_api_entity_kind(scene.kind(id)));
      if (!scene.is_instance_definition_entity(id) && !scene.is_instance_definition_member(id)) {
        expected_public_order.push_back(id);
        if (scene.parent(id) == kInvalidEntityId) {
          expected_root_order.push_back(id);
        }
      }
    }
    const auto actual_entities = editor.query().entities();
    EXPECT_EQ(actual_entities.size(), expected_public_order.size());
    std::vector<EntityId> actual_tree_order;
    std::vector<EntityId> actual_root_order;
    for (const auto &entity : editor.query().entity_tree()) {
      actual_tree_order.push_back(entity.id);
      if (entity.parent == kInvalidEntityId) {
        actual_root_order.push_back(entity.id);
      }
    }
    EXPECT_EQ(actual_tree_order, expected_public_order);
    EXPECT_EQ(actual_root_order, expected_root_order);
    EXPECT_EQ(editor.query().assets().mesh_count, expected.document.assets().mesh_ids().size());
    RecordProperty(relative.stem().string() + "_entities", std::to_string(scene.entities().size()));
    RecordProperty(relative.stem().string() + "_roots", std::to_string(scene.children(kInvalidEntityId).size()));
  }
  const auto closed = editor.project().close();
  ASSERT_TRUE(closed.ok()) << closed.error_message();
  for (const auto &[path, hash] : original_hashes) {
    const auto original = quader_io::read_file_bytes_shared(path);
    ASSERT_TRUE(original.ok) << original.message;
    EXPECT_EQ(quader_io::qdr_binary_content_hash(original.bytes), hash)
        << "Verification modified the supplied project: " << path.string();
  }
}

} // namespace
} // namespace quader
