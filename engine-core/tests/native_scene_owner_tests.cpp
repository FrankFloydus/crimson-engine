#include "scene/document/scene_entity_state.hpp"
#include "scene/document/scene_content_document.hpp"
#include "editor/commands/document/duplicate_selected_hierarchy_command.hpp"
#include "io/qdr/qdr_binary_container.hpp"
#include "io/qdr/qdr_document_reader.hpp"
#include "io/qdr/qdr_document_writer.hpp"

#include <gtest/gtest.h>
#include <array>
#include <vector>

namespace quader {
namespace {

TEST(NativeEngineSceneOwner, ComponentInventorySharesImmutableBytesAndRejectsInvalidPublication) {
  SceneDocument scene;
  const auto entity = scene.create_empty("Actor");
  EngineComponentsComponent component{1u, R"({"movement":{"enabled":true}})"};
  ASSERT_TRUE(scene.set_engine_components(entity, component));
  const auto revision = scene.content_revision();
  ASSERT_TRUE(scene.set_engine_components(entity, EngineComponentsComponent{1u, std::string(component.text())}));
  EXPECT_EQ(scene.content_revision(), revision);
  const auto captured = capture_scene_entity_state(scene, entity);
  ASSERT_TRUE(captured && captured->engine_components);
  EXPECT_EQ(captured->engine_components->serialized_components, component.serialized_components);
  EXPECT_FALSE(scene.set_engine_components(entity, EngineComponentsComponent{2u, "{}"}));
  auto invalid = component;
  invalid.entity_references.push_back({"cameraFollow.target", "not-a-uuid"});
  EXPECT_FALSE(scene.set_engine_components(entity, invalid));
  EXPECT_FALSE(engine_components_metadata_is_valid(1u, kMaximumEngineComponentsBytes + 1u, {}));
  invalid = component;
  invalid.asset_references_complete = true;
  invalid.asset_references = {"not-a-uuid"};
  EXPECT_FALSE(scene.set_engine_components(entity, invalid));
  invalid.asset_references = {"27b6b7a0-660a-4c2e-8ce5-c553f7922984", "27B6B7A0-660A-4C2E-8CE5-C553F7922984"};
  EXPECT_FALSE(scene.set_engine_components(entity, invalid));
  EXPECT_EQ(scene.content_revision(), revision);
  ASSERT_TRUE(scene.clear_engine_components(entity));
  ASSERT_TRUE(apply_scene_entity_state(scene, entity, *captured));
  EXPECT_EQ(scene.engine_components(entity)->serialized_components, component.serialized_components);
}

TEST(NativeEngineSceneOwner, StructuralRollbackRestoresComponentsAndPreservesDocumentSettings) {
  SceneDocument scene;
  const auto entity = scene.create_empty("Player");
  EngineComponentsComponent original{1u, R"({"playerInput":{"enabled":true}})"};
  ASSERT_TRUE(scene.set_engine_components(entity, original));
  const auto checkpoint = scene.capture_modeling_projection_checkpoint(
      std::array{entity}, SceneModelingProjectionDomain::Structure);
  ASSERT_TRUE(checkpoint.valid());
  ASSERT_TRUE(scene.set_engine_components(entity, EngineComponentsComponent{1u, "{}"}));
  EngineSceneSettings settings;
  settings.serialized_settings = "{}";
  settings.player_uuid = scene.uuid(entity);
  ASSERT_TRUE(scene.set_engine_settings(settings));
  const auto settings_revision = scene.scene_settings_revision();
  const auto fence = scene.capture_modeling_projection_mutation_fence(
      std::array{entity}, SceneModelingProjectionDomain::Structure);
  ASSERT_TRUE(scene.restore_modeling_projection_checkpoint_preserving_concurrent_changes(checkpoint, fence));
  ASSERT_NE(scene.engine_components(entity), nullptr);
  EXPECT_EQ(scene.engine_components(entity)->serialized_components, original.serialized_components);
  ASSERT_NE(scene.engine_settings(), nullptr);
  EXPECT_EQ(*scene.engine_settings(), settings);
  EXPECT_EQ(scene.scene_settings_revision(), settings_revision);
}

TEST(NativeEngineSceneOwner, DuplicateRemapsWholeBatchReferencesAndRedoPreservesNativeIdentity) {
  SceneDocument scene;
  const auto camera = scene.create_empty("Eye");
  const auto player = scene.create_empty("Player");
  const auto external = scene.create_empty("Sun");
  EngineComponentsComponent camera_data{1u, R"({"camera":{"fov":75}})"};
  camera_data.entity_references = {{"cameraFollow.target", scene.uuid(player)}, {"environment.sun", scene.uuid(external)}};
  ASSERT_TRUE(scene.set_engine_components(camera, camera_data));
  EngineSceneSettings settings{.serialized_settings="{}", .active_camera_uuid=scene.uuid(camera), .player_uuid=scene.uuid(player)};
  ASSERT_TRUE(scene.set_engine_settings(settings));
  DuplicateSelectedHierarchyCommand command(scene,
      std::vector<SceneEntityStateTree>{*capture_scene_entity_state_tree(scene, camera), *capture_scene_entity_state_tree(scene, player)}, {});
  const auto duplicated = command.redo();
  ASSERT_TRUE(duplicated.ok) << duplicated.message;
  ASSERT_EQ(command.duplicate_roots().size(), 2u);
  const auto copy_camera = command.duplicate_roots()[0], copy_player = command.duplicate_roots()[1];
  const auto copy_uuid = scene.uuid(copy_camera), player_uuid = scene.uuid(copy_player);
  const auto *copy = scene.engine_components(copy_camera);
  ASSERT_NE(copy, nullptr);
  EXPECT_EQ(copy->serialized_components, camera_data.serialized_components);
  EXPECT_EQ(copy->entity_references[0].entity_uuid, player_uuid);
  EXPECT_EQ(copy->entity_references[1].entity_uuid, scene.uuid(external));
  EXPECT_EQ(scene.engine_settings()->active_camera_uuid, scene.uuid(camera));
  ASSERT_TRUE(command.undo().ok);
  EXPECT_FALSE(scene.exists(copy_camera));
  ASSERT_TRUE(command.redo().ok);
  EXPECT_EQ(scene.uuid(command.duplicate_roots()[0]), copy_uuid);
  EXPECT_EQ(scene.engine_components(command.duplicate_roots()[0])->entity_references[0].entity_uuid, player_uuid);
}

TEST(NativeEngineSceneOwner, QdrOwnsSameEntitiesComponentsSelectorsAndReferenceMetadata) {
  SceneContentDocument document;
  auto &scene = document.scene();
  const auto actor = scene.create_empty("Actor"), camera = scene.create_empty("Eye");
  ASSERT_TRUE(scene.set_parent(camera, actor, false));
  EngineComponentsComponent component{1u, R"({"camera":{"verticalFov":75}})"};
  component.entity_references.push_back({"cameraFollow.target", scene.uuid(actor)});
  ASSERT_TRUE(scene.set_engine_components(camera, component));
  EngineSceneSettings settings{.serialized_settings=R"({"schema":25})", .active_camera_uuid=scene.uuid(camera), .player_uuid=scene.uuid(actor)};
  ASSERT_TRUE(scene.set_engine_settings(settings));
  const auto bytes = QdrDocumentWriter::serialize_project_document_bytes(document);
  const auto reopened = QdrDocumentReader::deserialize_project_document(bytes);
  ASSERT_TRUE(reopened.ok) << reopened.message;
  EXPECT_EQ(reopened.document.scene().entities().size(), 2u);
  EXPECT_EQ(reopened.document.scene().uuid(camera), scene.uuid(camera));
  EXPECT_EQ(reopened.document.scene().parent(camera), actor);
  ASSERT_NE(reopened.document.scene().engine_components(camera), nullptr);
  EXPECT_EQ(*reopened.document.scene().engine_components(camera), component);
  ASSERT_NE(reopened.document.scene().engine_settings(), nullptr);
  EXPECT_EQ(*reopened.document.scene().engine_settings(), settings);
  EXPECT_TRUE(QdrDocumentReader::deserialize_project_dependencies(bytes).ok);
  auto container = quader_io::read_qdr_binary_container(bytes);
  ASSERT_TRUE(container.ok) << container.message;
  std::vector<quader_io::QdrBinaryChunkInput> older;
  for (const auto &chunk : container.chunks) {
    const auto data = chunk.bytes();
    older.push_back({.type=static_cast<quader_io::QdrBinaryChunkType>(chunk.type),
        .schema_version=chunk.type==static_cast<std::uint32_t>(quader_io::QdrBinaryChunkType::SceneMetadata)?std::uint16_t{20u}:chunk.schema_version,
        .flags=chunk.flags, .id=chunk.id, .content_hash=chunk.content_hash,
        .bytes=std::vector<std::byte>(data.begin(), data.end())});
  }
  const auto invalid = quader_io::build_qdr_binary_container(container.scene_uuid, std::move(older));
  EXPECT_FALSE(QdrDocumentReader::deserialize_project_document(invalid).ok);
  EXPECT_FALSE(QdrDocumentReader::deserialize_project_dependencies(invalid).ok);
}

TEST(NativeEngineSceneOwner, EngineDependencyInventoryRoundTripsWithoutReadingGeometryChunks) {
  SceneContentDocument document;
  const auto entity = document.scene().create_empty("Engine owner");
  const std::string asset = "27b6b7a0-660a-4c2e-8ce5-c553f7922984";
  EngineComponentsComponent component{1u, "{}"};
  component.asset_references = {asset};
  component.asset_references_complete = true;
  ASSERT_TRUE(document.scene().set_engine_components(entity, component));
  const auto bytes = QdrDocumentWriter::serialize_project_document_bytes(document);
  const auto references = QdrDocumentReader::deserialize_project_engine_asset_dependencies(bytes);
  ASSERT_TRUE(references.ok) << references.message;
  ASSERT_TRUE(references.engine_asset_ids_complete);
  ASSERT_EQ(references.engine_asset_ids.size(), 1u);
  EXPECT_EQ(references.engine_asset_ids.front().str(), asset);
  const auto reopened = QdrDocumentReader::deserialize_project_document(bytes);
  ASSERT_TRUE(reopened.ok) << reopened.message;
  EXPECT_EQ(*reopened.document.scene().engine_components(entity), component);
  auto container = quader_io::read_qdr_binary_container(bytes);
  ASSERT_TRUE(container.ok);
  std::vector<quader_io::QdrBinaryChunkInput> chunks;
  for (const auto& chunk : container.chunks) {
    const auto contents = chunk.bytes();
    chunks.push_back({.type=static_cast<quader_io::QdrBinaryChunkType>(chunk.type),
        .schema_version=chunk.schema_version, .flags=chunk.flags, .id=chunk.id,
        .content_hash=chunk.content_hash, .bytes={contents.begin(), contents.end()}});
  }
  // An unsupported geometry dialect is deliberately opaque to this projection.
  // A broader scene/dependency read still rejects it.
  chunks.push_back({.type=quader_io::QdrBinaryChunkType::Geometry,
      .schema_version=999u, .id=99u, .bytes={std::byte{0x11}, std::byte{0x22}}});
  const auto opaque_geometry = quader_io::build_qdr_binary_container(container.scene_uuid, std::move(chunks));
  const auto metadata_only = QdrDocumentReader::deserialize_project_engine_asset_dependencies(opaque_geometry);
  ASSERT_TRUE(metadata_only.ok) << metadata_only.message;
  EXPECT_EQ(metadata_only.engine_asset_ids, references.engine_asset_ids);
  EXPECT_EQ(metadata_only.stats.unique_geometry_decoded_count, 0u);
  EXPECT_EQ(metadata_only.stats.unique_geometry_compiled_count, 0u);
  EXPECT_EQ(metadata_only.stats.geometry_decode_ms, 0.0);
  EXPECT_FALSE(QdrDocumentReader::deserialize_project_dependencies(opaque_geometry).ok);
  component.asset_references.clear();
  ASSERT_TRUE(document.scene().set_engine_components(entity, component));
  const auto empty = QdrDocumentReader::deserialize_project_engine_asset_dependencies(
      QdrDocumentWriter::serialize_project_document_bytes(document));
  EXPECT_TRUE(empty.ok);
  EXPECT_TRUE(empty.engine_asset_ids_complete);
  EXPECT_TRUE(empty.engine_asset_ids.empty());
  component.asset_references_complete = false;
  ASSERT_TRUE(document.scene().set_engine_components(entity, component));
  const auto unknown_bytes = QdrDocumentWriter::serialize_project_document_bytes(document);
  const auto unknown = QdrDocumentReader::deserialize_project_engine_asset_dependencies(unknown_bytes);
  EXPECT_TRUE(unknown.ok);
  EXPECT_FALSE(unknown.engine_asset_ids_complete);
  const auto unknown_document = QdrDocumentReader::deserialize_project_document(unknown_bytes);
  ASSERT_TRUE(unknown_document.ok);
  EXPECT_FALSE(unknown_document.document.scene().engine_components(entity)->asset_references_complete);
}

TEST(NativeEngineSceneOwner, PublicEngineEnvelopeDoesNotAuthorizeNativeFullFeatures) {
  SceneContentDocument document;
  auto& scene = document.scene();
  const auto entity = scene.create_empty("Engine player");
  EngineComponentsComponent component{1u, R"({"playerInput":{"enabled":true}})"};
  component.asset_references_complete = true;
  ASSERT_TRUE(scene.set_engine_components(entity, component));
  ASSERT_TRUE(scene.set_engine_settings(EngineSceneSettings{
      .serialized_settings = "{}", .player_uuid = scene.uuid(entity)}));
  const auto encoded = [&] {
    auto snapshot = qdr_project_write_snapshot_from_document(document);
    EXPECT_TRUE(snapshot.ok());
    if (!snapshot) return std::vector<std::byte>{};
    snapshot.value().minimum_product_release = kPublicRelease01;
    return QdrDocumentWriter::serialize_project_document_bytes(snapshot.value());
  };
  const auto accepted = QdrDocumentReader::deserialize_project_document(encoded(), kPublicRelease01);
  ASSERT_TRUE(accepted.ok) << accepted.message;
  EXPECT_EQ(accepted.minimum_product_release, kPublicRelease01);
  ASSERT_NE(accepted.document.scene().engine_settings(), nullptr);
  EXPECT_EQ(accepted.document.scene().engine_settings()->player_uuid, scene.uuid(entity));
  auto settings = scene.scene_settings();
  settings.fog.enabled = true;
  ASSERT_TRUE(scene.set_scene_settings(settings));
  const auto forbidden_environment = encoded();
  EXPECT_FALSE(QdrDocumentReader::deserialize_project_document(forbidden_environment, kPublicRelease01).ok);
  EXPECT_TRUE(QdrDocumentReader::deserialize_project_document(forbidden_environment, kInternalFullRelease).ok);
  settings.fog.enabled = false;
  ASSERT_TRUE(scene.set_scene_settings(settings));
  ASSERT_TRUE(scene.set_light(entity, LightComponent{}));
  const auto forbidden_light = encoded();
  EXPECT_FALSE(QdrDocumentReader::deserialize_project_document(forbidden_light, kPublicRelease01).ok);
  EXPECT_TRUE(QdrDocumentReader::deserialize_project_document(forbidden_light, kInternalFullRelease).ok);
}

} // namespace
} // namespace quader
