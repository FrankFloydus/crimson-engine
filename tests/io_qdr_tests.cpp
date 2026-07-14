////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "assets/material_document.hpp"
#include "io/qdr/qdr_document_reader.hpp"
#include "io/qdr/qdr_document_writer.hpp"
#include "io/qdr/qdr_project_write_snapshot.hpp"
#include "scene/document/scene_content_document.hpp"
#include "support/guardrail_file_scan.hpp"

#include <gtest/gtest.h>
#include <mesh/polygon/document.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

using namespace quader;

namespace {

using quader::test::guardrail::read_file;
using quader::test::guardrail::read_source_file;
using quader::test::guardrail::source_files_under;
using quader::test::guardrail::source_path;
using quader::test::guardrail::source_slice;

[[nodiscard]] MeshAsset make_triangle_mesh(std::string name) {
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
  mesh.primitives = {{.index_offset = 0u,
                      .index_count = 3u,
                      .material_slot = 2u}};
  return mesh;
}

[[nodiscard]] quader_poly::Document make_authored_polygon_document() {
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

[[nodiscard]] std::array<std::byte, 16> test_scene_uuid() {
  std::array<std::byte, 16> uuid{};
  for (std::size_t index = 0u; index < uuid.size(); ++index) {
    uuid[index] = static_cast<std::byte>(index + 1u);
  }
  return uuid;
}

[[nodiscard]] std::filesystem::path
unique_temp_directory(std::string_view label) {
  const auto tick = static_cast<std::uint64_t>(
      std::chrono::steady_clock::now().time_since_epoch().count());
  return std::filesystem::temp_directory_path() /
         ("quader-qdr-" + std::string(label) + "-" + std::to_string(tick));
}

/// Owns an isolated temporary directory for one QDR persistence test.
class ScopedTempDirectory final {
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

  [[nodiscard]] const std::filesystem::path &path() const { return path_; }

private:
  std::filesystem::path path_;
};

void write_text_file(const std::filesystem::path &path,
                     std::string_view text) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  ASSERT_TRUE(file.is_open()) << path.string();
  file.write(text.data(), static_cast<std::streamsize>(text.size()));
  file.close();
  ASSERT_TRUE(file.good()) << path.string();
}

[[nodiscard]] bool
contains_temp_qdr_files(const std::filesystem::path &directory) {
  for (const std::filesystem::directory_entry &entry :
       std::filesystem::directory_iterator(directory)) {
    if (entry.path().filename().string().find(".tmp") != std::string::npos) {
      return true;
    }
  }
  return false;
}

} // namespace

TEST(SceneContentDocumentTests,
     SceneObjectRecordComponentFieldsStaySceneCanonical) {
  SceneContentDocument document(SceneContentDocumentRecord{
      .id = 92u,
      .name = "Canonical Object Records",
  });
  const AssetId stale_mesh =
      document.assets().add_mesh(12u, make_triangle_mesh("Stale Mesh"));
  const AssetId canonical_mesh =
      document.assets().add_mesh(13u, make_triangle_mesh("Canonical Mesh"));
  const AssetId stale_material = document.assets().add_material(
      22u, make_default_quader_material("Stale Material"));
  const AssetId canonical_material = document.assets().add_material(
      23u, make_default_quader_material("Canonical Material"));
  ASSERT_NE(stale_mesh, kInvalidAssetId);
  ASSERT_NE(canonical_mesh, kInvalidAssetId);
  ASSERT_NE(stale_material, kInvalidAssetId);
  ASSERT_NE(canonical_material, kInvalidAssetId);

  const EntityId entity = document.scene().create_entity_with_id(
      200u, "Canonical Mesh", EntityKind::Mesh);
  ASSERT_NE(entity, kInvalidEntityId);
  const EditorObjectId object_id = document.scene().editor_object_id(entity);
  ASSERT_NE(object_id, kInvalidEditorObjectId);
  document.scene().set_visible(entity, false);
  document.scene().set_modeling_object(
      entity, ModelingObjectComponent{.object_id = 42, .material_slot = 2u});
  document.scene().set_mesh(entity, MeshComponent{.mesh = canonical_mesh});
  document.scene().set_material(
      entity, MaterialComponent{.material = canonical_material});

  EXPECT_EQ(document.upsert_object_record(ProjectObjectRecord{
                .id = object_id,
                .scene_entity = entity,
                .mesh = stale_mesh,
                .material = stale_material,
                .modeling_object = 99,
                .authored_name = "Imported Authored Name",
                .visible = true,
                .material_slot = 7u,
            }),
            object_id);

  const std::optional<ProjectObjectRecord> record =
      document.object_record(object_id);
  ASSERT_TRUE(record.has_value());
  EXPECT_EQ(record->mesh, canonical_mesh);
  EXPECT_EQ(record->material, canonical_material);
  EXPECT_EQ(record->modeling_object, 42);
  EXPECT_FALSE(record->visible);
  EXPECT_EQ(record->material_slot, 2u);

  const std::uint64_t revision_after_upsert = document.revision();
  EXPECT_FALSE(document.synchronize_object_records_with_scene());
  EXPECT_EQ(document.revision(), revision_after_upsert);
}

TEST(QdrDocumentIoTests,
     AtomicSceneWriteRoundTripsBinaryAndPreservesSceneUuid) {
  SceneContentDocument document(
      SceneContentDocumentRecord{.id = 77u, .name = "Binary Scene"});
  const AssetId mesh_id =
      document.assets().add_mesh(12u, make_triangle_mesh("Triangle Mesh"));
  const EntityId entity = document.scene().create_entity_with_id(
      200u, "Triangle", EntityKind::Mesh);
  ASSERT_NE(mesh_id, kInvalidAssetId);
  ASSERT_NE(entity, kInvalidEntityId);
  document.scene().set_mesh(entity, MeshComponent{.mesh = mesh_id});

  ScopedTempDirectory directory("atomic-binary-round-trip");
  const std::filesystem::path path = directory.path() / "scene.qdr";
  const QdrProjectWriteResult write =
      QdrDocumentWriter::write_project_document_atomic(
          path, document, test_scene_uuid());
  ASSERT_TRUE(write.ok) << write.message;
  const std::string bytes = read_file(path);
  ASSERT_GE(bytes.size(), 4u);
  EXPECT_EQ(bytes.substr(0u, 4u), "QDRB");

  const QdrProjectReadResult read =
      QdrDocumentReader::read_project_document(path);
  ASSERT_TRUE(read.ok) << read.message;
  EXPECT_EQ(read.scene_uuid, test_scene_uuid());
  EXPECT_EQ(read.document.id(), 77u);
  EXPECT_EQ(read.document.name(), "Binary Scene");
  EXPECT_NE(read.document.assets().mesh(mesh_id), nullptr);
  EXPECT_TRUE(read.document.scene().exists(entity));
  EXPECT_FALSE(contains_temp_qdr_files(directory.path()));
}

TEST(QdrDocumentIoTests,
     AtomicSceneWriteRejectsInvalidReferencesWithoutReplacingTarget) {
  ScopedTempDirectory directory("invalid-binary-write");
  const std::filesystem::path path = directory.path() / "scene.qdr";
  constexpr std::string_view kOriginal = "existing valid scene bytes";
  write_text_file(path, kOriginal);

  SceneContentDocument document(
      SceneContentDocumentRecord{.id = 91u, .name = "Invalid Write"});
  const EntityId entity =
      document.scene().create_entity("Dangling Mesh", EntityKind::Mesh);
  ASSERT_NE(entity, kInvalidEntityId);
  document.scene().set_mesh(entity, MeshComponent{.mesh = 999u});
  ASSERT_FALSE(document.validate_asset_references().ok());

  const QdrProjectWriteResult write =
      QdrDocumentWriter::write_project_document_atomic(path, document);
  EXPECT_FALSE(write.ok);
  EXPECT_EQ(read_file(path), kOriginal);
  EXPECT_FALSE(contains_temp_qdr_files(directory.path()));
}

TEST(QdrDocumentIoTests, SceneWriteSnapshotOwnsImmutableCopies) {
  SceneContentDocument document(
      SceneContentDocumentRecord{.id = 93u, .name = "Snapshot Scene"});
  MeshAsset authored_mesh = make_triangle_mesh("Snapshot Mesh");
  authored_mesh.has_authored_polygon_document = true;
  authored_mesh.authored_polygon_document =
      std::make_shared<quader_poly::Document>(
          make_authored_polygon_document());
  const AssetId mesh_id =
      document.assets().add_mesh(12u, std::move(authored_mesh));
  const EntityId entity = document.scene().create_entity_with_id(
      200u, "Snapshot Entity", EntityKind::Mesh);
  ASSERT_NE(mesh_id, kInvalidAssetId);
  ASSERT_NE(entity, kInvalidEntityId);
  document.scene().set_mesh(entity, MeshComponent{.mesh = mesh_id});

  const QdrProjectWriteSnapshot snapshot =
      qdr_project_write_snapshot_from_document(document);
  document.rename("Mutated Scene");
  ASSERT_TRUE(document.scene().rename_entity(entity, "Mutated Entity"));
  ASSERT_TRUE(document.assets().update_mesh(mesh_id, [](MeshAsset &mesh) {
    mesh.name = "Mutated Mesh";
    mesh.authored_polygon_document->faces.front().material_slot = 9u;
  }));

  const std::vector<std::byte> bytes =
      QdrDocumentWriter::serialize_project_document_bytes(
          snapshot, test_scene_uuid());
  const QdrProjectReadResult read =
      QdrDocumentReader::deserialize_project_document(bytes);
  ASSERT_TRUE(read.ok) << read.message;
  EXPECT_EQ(read.stats.logical_geometry_count, 1u);
  EXPECT_EQ(read.stats.unique_geometry_decoded_count, 1u);
  EXPECT_EQ(read.stats.unique_geometry_compiled_count, 1u);
  EXPECT_EQ(read.document.name(), "Snapshot Scene");
  const NameComponent *name = read.document.scene().name(entity);
  ASSERT_NE(name, nullptr);
  EXPECT_EQ(name->name, "Snapshot Entity");
  const MeshAsset *mesh = read.document.assets().mesh(mesh_id);
  ASSERT_NE(mesh, nullptr);
  EXPECT_EQ(mesh->name, "Snapshot Mesh");
  ASSERT_TRUE(mesh->authored_polygon_document);
  EXPECT_EQ(mesh->authored_polygon_document->faces.front().material_slot, 2u);
}

TEST(QdrDocumentIoTests, NativeScenePersistenceDoesNotDispatchLegacyJson) {
  const std::string cmake = read_source_file("CMakeLists.txt");
  const std::string native_target = source_slice(
      cmake, "add_library(quader_project_qdr_io STATIC",
      "set(QUADER_QDR_GENERATED_DIR");
  ASSERT_FALSE(native_target.empty());
  EXPECT_NE(native_target.find("src/io/qdr/qdr_scene_binary_codec.cpp"),
            std::string::npos);
  EXPECT_EQ(native_target.find("src/io/qdr/qdr_project_codecs.cpp"),
            std::string::npos);
  EXPECT_EQ(native_target.find("src/io/qdr/qdr_document.cpp"),
            std::string::npos);

  const std::string reader =
      read_source_file({"src", "io", "qdr", "qdr_document_reader.cpp"});
  const std::string writer =
      read_source_file({"src", "io", "qdr", "qdr_document_writer.cpp"});
  const std::string codec = read_source_file(
      {"src", "io", "qdr", "qdr_scene_binary_codec.cpp"});
  for (const std::string *source : {&reader, &writer, &codec}) {
    EXPECT_EQ(source->find("nlohmann/json"), std::string::npos);
    EXPECT_EQ(source->find("deserialize_document("), std::string::npos);
    EXPECT_EQ(source->find("qdr_project_codecs"), std::string::npos);
  }
  EXPECT_NE(codec.find("serialize_qdr_authored_topology("),
            std::string::npos);
  EXPECT_NE(codec.find("deserialize_qdr_authored_topology("),
            std::string::npos);
}

TEST(QdrDocumentIoTests, SceneSelectableMutationStaysSceneOwned) {
  const std::string scene_header =
      read_source_file({"src", "scene", "scene_document.hpp"});
  const std::string scene_source =
      read_source_file({"src", "scene", "scene_document.cpp"});
  const std::string qdr_codec = read_source_file(
      {"src", "io", "qdr", "qdr_scene_binary_codec.cpp"});

  EXPECT_NE(
      scene_header.find("bool set_selectable(EntityId id, bool selectable);"),
      std::string::npos);
  EXPECT_NE(scene_source.find("bool SceneDocument::set_selectable"),
            std::string::npos);
  EXPECT_NE(qdr_codec.find("document.scene().set_selectable("),
            std::string::npos);
  EXPECT_EQ(qdr_codec.find("->selectable ="), std::string::npos);

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

TEST(ProjectionOwnershipGuardrailTests,
     ProjectionIsReadOnlyAndModelingOwnsSceneSync) {
  const std::string cmake = read_source_file("CMakeLists.txt");
  EXPECT_NE(cmake.find("src/editor/projection/mesh_projection_service.cpp"),
            std::string::npos);
  EXPECT_EQ(
      cmake.find("src/editor/projection/modeling_scene_reconcile_service.cpp"),
      std::string::npos);
  EXPECT_NE(cmake.find("src/editor/projection/picking_cache_projection.cpp"),
            std::string::npos);
  EXPECT_NE(
      cmake.find("src/editor/projection/selection_overlay_projection.cpp"),
      std::string::npos);
  EXPECT_NE(
      cmake.find("src/editor/modeling/services/modeling_mesh_sync_service.cpp"),
      std::string::npos);

  const std::string services = read_source_file(
      {"src", "editor", "modeling", "services",
       "modeling_session_services.cpp"});
  EXPECT_EQ(services.find("ModelingSceneReconcileService::"
                          "reconcile_modeling_object_scene_nodes"),
            std::string::npos);
  EXPECT_EQ(services.find("MeshProjectionService::"
                          "project_modeling_meshes_to_scene"),
            std::string::npos);
  EXPECT_NE(services.find("PickingCacheProjection::"
                          "scene_transformed_objects_for_picking"),
            std::string::npos);
  EXPECT_NE(services.find("PickingCacheProjection::"
                          "object_face_geometry_signature"),
            std::string::npos);

  const std::string mesh_sync = read_source_file(
      {"src", "editor", "modeling", "services",
       "modeling_mesh_sync_service.cpp"});
  EXPECT_NE(mesh_sync.find("bool reconcile_modeling_object_scene_nodes"),
            std::string::npos);
  EXPECT_NE(mesh_sync.find("bool project_modeling_meshes_to_scene"),
            std::string::npos);
  EXPECT_NE(mesh_sync.find("scene.create_entity"), std::string::npos);
  EXPECT_NE(mesh_sync.find("scene.destroy_entity"), std::string::npos);
  EXPECT_NE(mesh_sync.find("assets.add_mesh"), std::string::npos);
  EXPECT_NE(mesh_sync.find("assets.replace_mesh"), std::string::npos);
  EXPECT_NE(mesh_sync.find("build_projected_mesh_plans"), std::string::npos);
  EXPECT_NE(mesh_sync.find("install_projected_mesh_assets"),
            std::string::npos);
  EXPECT_NE(mesh_sync.find("rollback_projected_mesh_installs"),
            std::string::npos);
  EXPECT_NE(mesh_sync.find("remember_projected_mesh_signature"),
            std::string::npos);

  const char *forbidden_projection_mutations[] = {
      "AssetRegistry",          "SceneModelingSyncService",
      "create_entity(",         "create_entity_with_id(",
      "destroy_entity(",        "set_modeling_object(",
      "set_mesh(",              "set_material(",
      "add_mesh(",              "replace_mesh(",
      ".bind(",                 ".unbind_object(",
      "mark_scene_objects_synced(", "clear_selection(",
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

  const std::string facade = read_source_file(
      {"src", "editor", "modeling", "services",
       "modeling_session_service.cpp"});
  const std::string overlay_service = read_source_file(
      {"src", "editor", "modeling", "services",
       "modeling_session_services.cpp"});
  EXPECT_EQ(facade.find("SelectionOverlayProjection::"
                        "project_selection_overlay_frame"),
            std::string::npos);
  EXPECT_NE(overlay_service.find("SelectionOverlayProjection::"
                                 "project_selection_overlay_frame"),
            std::string::npos);
}

TEST(ProjectionOwnershipGuardrailTests,
     RenderDoesNotDependOnProjectionOrModelingOwners) {
  const std::filesystem::path render_root = source_path({"src", "render"});
  for (const std::filesystem::path &file : source_files_under(render_root)) {
    const std::string source = read_file(file);
    EXPECT_EQ(source.find("editor/projection/"), std::string::npos)
        << file.string();
    EXPECT_EQ(source.find("#include \"modeling/"), std::string::npos)
        << file.string();
  }
}
