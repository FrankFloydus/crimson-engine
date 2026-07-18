////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "assets/material_document.hpp"
#include "io/qdr/qdr_atomic_write.hpp"
#include "io/qdr/qdr_document_reader.hpp"
#include "io/qdr/qdr_document_writer.hpp"
#include "io/qdr/qdr_project_write_snapshot.hpp"
#include "scene/document/scene_content_document.hpp"
#include "support/guardrail_file_scan.hpp"

#include <gtest/gtest.h>
#include <quader/modeling/neutral/polygon/document.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace quader;

namespace {

using quader::test::guardrail::read_file;

[[nodiscard]] MeshAsset make_triangle_mesh(std::string name) {
  MeshAsset mesh;
  mesh.name = std::move(name);
  MeshGeometryPayload &geometry = mesh.edit_geometry();
  geometry.vertices = {
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
  geometry.indices = {0u, 1u, 2u};
  geometry.primitives = {{.index_offset = 0u,
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
    const std::string name = entry.path().filename().string();
    if (name.find(".tmp") != std::string::npos ||
        name.find(".stage") != std::string::npos ||
        name.find(".rollback") != std::string::npos ||
        name.find(".quarantine") != std::string::npos) {
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
  document.scene().set_modeling_object(
      entity, {.object_id = 42, .material_slot = 3u, .sdk_owned = true});

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
  const ModelingObjectComponent *modeling =
      read.document.scene().modeling_object(entity);
  ASSERT_NE(modeling, nullptr);
  EXPECT_EQ(modeling->object_id, 42);
  EXPECT_EQ(modeling->material_slot, 3u);
  EXPECT_TRUE(modeling->sdk_owned);
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

TEST(QdrDocumentIoTests, AtomicWriteCatchesThrowingValidatorAndRemovesTemp) {
  ScopedTempDirectory directory("throwing-atomic-validator");
  const std::filesystem::path target = directory.path() / "scene.qdr";
  write_text_file(target, "original scene");

  const quader_io::QdrAtomicWriteResult written =
      quader_io::write_qdr_text_atomic(
          target, "candidate bytes", [](const std::filesystem::path &) {
            throw std::runtime_error("validator failure");
            return quader_io::QdrAtomicWriteResult{true, {}};
          });

  EXPECT_FALSE(written.ok);
  EXPECT_NE(written.message.find("validator failure"), std::string::npos);
  EXPECT_EQ(read_file(target), "original scene");
  EXPECT_FALSE(contains_temp_qdr_files(directory.path()));
}

TEST(QdrAtomicFileTransactionTests,
     RollbackPreservesExistingDestinationIdentityAndHardLinks) {
  ScopedTempDirectory directory("staged-rollback-hard-link");
  const std::filesystem::path target = directory.path() / "scene.qdr";
  const std::filesystem::path alias = directory.path() / "scene-alias.qdr";
  constexpr std::string_view kOriginal = "original scene bytes";
  write_text_file(target, kOriginal);

  std::error_code hard_link_error;
  std::filesystem::create_hard_link(target, alias, hard_link_error);
  if (hard_link_error) {
    GTEST_SKIP() << "Hard links unavailable: " << hard_link_error.message();
  }

  quader_io::QdrAtomicFileTransaction transaction;
  const quader_io::QdrAtomicWriteResult prepared = transaction.prepare(target);
  ASSERT_TRUE(prepared.ok) << prepared.message;
  const std::filesystem::path staging = transaction.staging_path();
  ASSERT_TRUE(std::filesystem::equivalent(target, alias));

  const quader_io::QdrAtomicWriteResult staged =
      quader_io::write_qdr_text_atomic(staging, "replacement scene bytes");
  ASSERT_TRUE(staged.ok) << staged.message;
  EXPECT_EQ(read_file(target), kOriginal);

  const quader_io::QdrAtomicWriteResult published = transaction.publish();
  ASSERT_TRUE(published.ok) << published.message;
  EXPECT_EQ(read_file(target), "replacement scene bytes");
  EXPECT_EQ(read_file(alias), kOriginal);
  EXPECT_FALSE(std::filesystem::equivalent(target, alias));

  const quader_io::QdrAtomicWriteResult rolled_back = transaction.rollback();
  ASSERT_TRUE(rolled_back.ok) << rolled_back.message;
  EXPECT_FALSE(std::filesystem::exists(staging));
  EXPECT_EQ(read_file(target), kOriginal);
  EXPECT_TRUE(std::filesystem::equivalent(target, alias));
}

TEST(QdrAtomicFileTransactionTests,
     PublishNeverReplacesAConcurrentCreatorForAnInitiallyAbsentTarget) {
  ScopedTempDirectory directory("staged-publish-conflict");
  const std::filesystem::path target = directory.path() / "scene.qdr";

  quader_io::QdrAtomicFileTransaction transaction;
  const quader_io::QdrAtomicWriteResult prepared = transaction.prepare(target);
  ASSERT_TRUE(prepared.ok) << prepared.message;
  const std::filesystem::path staging = transaction.staging_path();
  const quader_io::QdrAtomicWriteResult staged =
      quader_io::write_qdr_text_atomic(staging, "our staged scene");
  ASSERT_TRUE(staged.ok) << staged.message;

  constexpr std::string_view kConflict = "another writer's scene";
  write_text_file(target, kConflict);
  const quader_io::QdrAtomicWriteResult published = transaction.publish();
  EXPECT_FALSE(published.ok);
  EXPECT_EQ(read_file(target), kConflict);

  const quader_io::QdrAtomicWriteResult rolled_back = transaction.rollback();
  ASSERT_TRUE(rolled_back.ok) << rolled_back.message;

  EXPECT_FALSE(std::filesystem::exists(staging));
  EXPECT_EQ(read_file(target), kConflict);
}

TEST(QdrAtomicFileTransactionTests,
     PublishedSceneExistsBeforeAManifestCanReferenceIt) {
  ScopedTempDirectory directory("publish-before-manifest");
  const std::filesystem::path target = directory.path() / "scene.qdr";

  quader_io::QdrAtomicFileTransaction transaction;
  const quader_io::QdrAtomicWriteResult prepared = transaction.prepare(target);
  ASSERT_TRUE(prepared.ok) << prepared.message;
  const std::filesystem::path staging = transaction.staging_path();
  const quader_io::QdrAtomicWriteResult staged =
      quader_io::write_qdr_text_atomic(staging, "durable scene bytes");
  ASSERT_TRUE(staged.ok) << staged.message;

  const quader_io::QdrAtomicWriteResult published = transaction.publish();
  ASSERT_TRUE(published.ok) << published.message;
  EXPECT_TRUE(transaction.published());
  EXPECT_FALSE(std::filesystem::exists(staging));
  EXPECT_EQ(read_file(target), "durable scene bytes");

  const quader_io::QdrAtomicWriteResult rolled_back = transaction.rollback();
  ASSERT_TRUE(rolled_back.ok) << rolled_back.message;
  EXPECT_FALSE(std::filesystem::exists(target));
}

#ifdef _WIN32
TEST(QdrAtomicFileTransactionTests,
     PublishedGuardRejectsReplacementAcrossTheManifestWindow) {
  ScopedTempDirectory directory("published-replacement-guard");
  const std::filesystem::path target = directory.path() / "scene.qdr";

  quader_io::QdrAtomicFileTransaction transaction;
  ASSERT_TRUE(transaction.prepare(target).ok);
  ASSERT_TRUE(quader_io::write_qdr_text_atomic(transaction.staging_path(),
                                                 "our scene")
                  .ok);
  ASSERT_TRUE(transaction.publish().ok);
  const quader_io::QdrAtomicWriteResult conflicting =
      quader_io::write_qdr_text_atomic(target, "conflicting scene");
  EXPECT_FALSE(conflicting.ok);
  EXPECT_TRUE(transaction.verify_published().ok);
  EXPECT_EQ(read_file(target), "our scene");

  const quader_io::QdrAtomicWriteResult rolled_back = transaction.rollback();
  ASSERT_TRUE(rolled_back.ok) << rolled_back.message;
  EXPECT_FALSE(std::filesystem::exists(target));
}

TEST(QdrAtomicFileTransactionTests,
     GuardAcquisitionFailureStillAllowsExactRollback) {
  ScopedTempDirectory directory("published-guard-acquisition-failure");
  const std::filesystem::path target = directory.path() / "scene.qdr";

  quader_io::QdrAtomicFileTransaction transaction;
  ASSERT_TRUE(transaction.prepare(target).ok);
  ASSERT_TRUE(quader_io::write_qdr_text_atomic(transaction.staging_path(),
                                                "our scene")
                  .ok);
  const HANDLE staging_writer = ::CreateFileW(
      transaction.staging_path().c_str(), GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  ASSERT_NE(staging_writer, INVALID_HANDLE_VALUE);

  const quader_io::QdrAtomicWriteResult published = transaction.publish();
  EXPECT_FALSE(published.ok);
  EXPECT_TRUE(transaction.active());
  EXPECT_TRUE(transaction.published());
  EXPECT_FALSE(transaction.verify_published().ok);
  EXPECT_EQ(read_file(target), "our scene");

  ASSERT_NE(::CloseHandle(staging_writer), 0);
  const quader_io::QdrAtomicWriteResult rolled_back = transaction.rollback();
  ASSERT_TRUE(rolled_back.ok) << rolled_back.message;
  EXPECT_FALSE(std::filesystem::exists(target));
}

TEST(QdrAtomicFileTransactionTests,
     RestoreFailureRetainsStateAndSecondRollbackSucceeds) {
  ScopedTempDirectory directory("rollback-restore-retry");
  const std::filesystem::path target = directory.path() / "scene.qdr";
  write_text_file(target, "original scene");

  quader_io::QdrAtomicFileTransaction transaction;
  ASSERT_TRUE(transaction.prepare(target).ok);
  ASSERT_TRUE(quader_io::write_qdr_text_atomic(transaction.staging_path(),
                                                "replacement scene")
                  .ok);
  ASSERT_TRUE(transaction.publish().ok);
  const std::filesystem::path backup = transaction.backup_path();
  ASSERT_FALSE(backup.empty());
  const HANDLE backup_guard =
      ::CreateFileW(backup.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  ASSERT_NE(backup_guard, INVALID_HANDLE_VALUE);

  const quader_io::QdrAtomicWriteResult first_rollback = transaction.rollback();
  EXPECT_FALSE(first_rollback.ok);
  EXPECT_TRUE(transaction.active());
  EXPECT_TRUE(transaction.published());
  EXPECT_EQ(transaction.backup_path(), backup);
  EXPECT_EQ(read_file(target), "replacement scene");

  ASSERT_NE(::CloseHandle(backup_guard), 0);
  const quader_io::QdrAtomicWriteResult second_rollback =
      transaction.rollback();
  ASSERT_TRUE(second_rollback.ok) << second_rollback.message;
  EXPECT_FALSE(transaction.active());
  EXPECT_EQ(read_file(target), "original scene");
  EXPECT_FALSE(std::filesystem::exists(backup));
}

TEST(QdrAtomicFileTransactionTests,
     CommitCleanupFailureRetainsBackupForDestructorRetry) {
  ScopedTempDirectory directory("commit-cleanup-retry");
  const std::filesystem::path target = directory.path() / "scene.qdr";
  write_text_file(target, "original scene");

  quader_io::QdrAtomicFileTransaction transaction;
  ASSERT_TRUE(transaction.prepare(target).ok);
  ASSERT_TRUE(quader_io::write_qdr_text_atomic(transaction.staging_path(),
                                                "committed scene")
                  .ok);
  ASSERT_TRUE(transaction.publish().ok);
  const std::filesystem::path backup = transaction.backup_path();
  ASSERT_FALSE(backup.empty());

  const HANDLE backup_guard =
      ::CreateFileW(backup.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  ASSERT_NE(backup_guard, INVALID_HANDLE_VALUE);

  const quader_io::QdrAtomicWriteResult committed = transaction.commit();
  EXPECT_TRUE(committed.ok);
  EXPECT_TRUE(transaction.active());
  EXPECT_FALSE(transaction.published());
  EXPECT_EQ(transaction.backup_path(), backup);
  EXPECT_NE(committed.message.find(backup.string()), std::string::npos);
  EXPECT_EQ(read_file(target), "committed scene");
  EXPECT_FALSE(
      quader_io::write_qdr_text_atomic(target, "conflicting scene").ok);
  EXPECT_EQ(read_file(target), "committed scene");

  ASSERT_NE(::CloseHandle(backup_guard), 0);
  const quader_io::QdrAtomicWriteResult cleanup = transaction.rollback();
  ASSERT_TRUE(cleanup.ok) << cleanup.message;
  EXPECT_FALSE(transaction.active());
  EXPECT_FALSE(std::filesystem::exists(backup));
  EXPECT_EQ(read_file(target), "committed scene");
}
#endif

TEST(QdrAtomicFileTransactionTests,
     ExplicitRecoveryPreservationDisarmsDestructorRollback) {
  ScopedTempDirectory directory("explicit-recovery-preservation");
  const std::filesystem::path target = directory.path() / "scene.qdr";
  write_text_file(target, "original scene");
  std::filesystem::path backup;

  {
    quader_io::QdrAtomicFileTransaction transaction;
    ASSERT_TRUE(transaction.prepare(target).ok);
    EXPECT_FALSE(transaction.preserve_published_for_recovery());
    EXPECT_TRUE(transaction.active());
    ASSERT_TRUE(quader_io::write_qdr_text_atomic(transaction.staging_path(),
                                                  "published scene")
                    .ok);
    ASSERT_TRUE(transaction.publish().ok);
    backup = transaction.backup_path();
    ASSERT_FALSE(backup.empty());

    ASSERT_TRUE(transaction.preserve_published_for_recovery());
    EXPECT_FALSE(transaction.active());
  }

  EXPECT_EQ(read_file(target), "published scene");
  EXPECT_EQ(read_file(backup), "original scene");
  std::error_code cleanup_error;
  EXPECT_TRUE(std::filesystem::remove(backup, cleanup_error));
  EXPECT_FALSE(cleanup_error) << cleanup_error.message();
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

  QdrProjectWriteSnapshotResult snapshot_result =
      qdr_project_write_snapshot_from_document(document);
  ASSERT_TRUE(snapshot_result.ok()) << snapshot_result.error().message;
  const QdrProjectWriteSnapshot snapshot = snapshot_result.take_value();
  document.rename("Mutated Scene");
  ASSERT_TRUE(document.scene().rename_entity(entity, "Mutated Entity"));
  ASSERT_TRUE(document.assets().update_mesh(mesh_id, [](MeshAsset &mesh) {
    mesh.name = "Mutated Mesh";
    mesh.edit_authored_polygon_document().faces.front().material_slot = 9u;
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
