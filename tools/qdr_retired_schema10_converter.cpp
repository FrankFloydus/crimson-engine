////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "qdr_retired_schema10_converter.hpp"

#include "io/qdr/qdr_document_reader.hpp"
#include "io/qdr/qdr_document_writer.hpp"
#include "qdr_scene_generated.h"
#include "qdr_scene_usage_schema10_generated.h"

#include <quader/modeling/io/qdr_document.hpp>
#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/verifier.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace quader::migration {
namespace {

namespace retired = ::quader::qdr::usage_schema10::fb;
namespace current = ::quader::qdr::fb;

void require(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename T> bool empty(const T *vector) {
  return vector == nullptr || vector->size() == 0u;
}

std::string text(const flatbuffers::String *value) {
  return value == nullptr ? std::string{} : value->str();
}

QVec3 vector3(const retired::Vec3 *value, QVec3 fallback = {}) {
  if (value == nullptr) {
    return fallback;
  }
  require(std::isfinite(value->x()) && std::isfinite(value->y()) &&
              std::isfinite(value->z()),
          "Retired QDR contains a non-finite vector.");
  return {value->x(), value->y(), value->z()};
}

current::Vec3 wire_vector(const retired::Vec3 &value) {
  return {value.x(), value.y(), value.z()};
}

/** Rebuilds only the outer geometry table; the SDK owns topology migration. */
std::shared_ptr<const std::vector<std::byte>> convert_geometry(
    const quader_io::QdrBinaryChunk &chunk, bool authored) {
  const auto bytes = chunk.bytes();
  flatbuffers::Verifier verifier(
      reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size());
  require(verifier.VerifyBuffer<retired::GeometryPayload>("QDRG"),
          "Retired QDR geometry FlatBuffer is invalid.");
  const auto *source = flatbuffers::GetRoot<retired::GeometryPayload>(bytes.data());
  require(source->kind() == (authored ? retired::GeometryKind::AuthoredPolygon
                                    : retired::GeometryKind::CompiledMesh),
          "Retired QDR mesh metadata disagrees with its geometry kind.");

  flatbuffers::FlatBufferBuilder builder;
  std::vector<current::MeshVertex> vertices;
  std::vector<std::uint32_t> indices;
  std::vector<current::MeshPrimitive> primitives;
  std::vector<current::MeshTextureMappingVertex> mapping;
  flatbuffers::Offset<flatbuffers::Vector<std::uint8_t>> authored_payload;
  if (authored) {
    require(empty(source->mesh_vertices()) && empty(source->mesh_indices()) &&
                empty(source->mesh_primitives()) && empty(source->mesh_texture_mapping()),
            "Retired authored geometry also contains compiled data; conversion would discard it.");
    require(!empty(source->authored_payload()), "Retired authored geometry has no topology.");
    const auto *payload = source->authored_payload();
    auto decoded = modeling::deserialize_qdr_authored_topology(
        std::as_bytes(std::span(payload->data(), payload->size())));
    if (!decoded.ok()) {
      throw std::runtime_error("Retired topology is invalid: " + decoded.error_message());
    }
    auto encoded = modeling::serialize_qdr_authored_topology(decoded.value());
    if (!encoded.ok()) {
      throw std::runtime_error("Retired topology cannot be migrated: " + encoded.error_message());
    }
    authored_payload = builder.CreateVector(
        reinterpret_cast<const std::uint8_t *>(encoded.value().data()), encoded.value().size());
  } else {
    require(empty(source->authored_payload()),
            "Retired compiled geometry also contains authored data; conversion would discard it.");
    if (const auto *stored = source->mesh_vertices()) {
      vertices.reserve(stored->size());
      for (const auto *vertex : *stored) {
        vertices.emplace_back(wire_vector(vertex->position()), wire_vector(vertex->normal()),
                              current::Vec2(vertex->uv().x(), vertex->uv().y()), vertex->rgba());
      }
    }
    if (const auto *stored = source->mesh_indices()) {
      indices.assign(stored->begin(), stored->end());
    }
    if (const auto *stored = source->mesh_primitives()) {
      for (const auto *primitive : *stored) {
        primitives.emplace_back(primitive->index_offset(), primitive->index_count(),
                                primitive->material_slot());
      }
    }
    if (const auto *stored = source->mesh_texture_mapping()) {
      for (const auto *entry : *stored) {
        mapping.emplace_back(entry->carrier_00(), entry->carrier_01(), entry->carrier_10(),
                             entry->carrier_11(), wire_vector(entry->geometric_normal()));
      }
    }
  }
  const auto root = current::CreateGeometryPayload(
      builder, authored ? current::GeometryKind::AuthoredPolygon : current::GeometryKind::CompiledMesh,
      1u, 1u, builder.CreateVectorOfStructs(std::vector<current::PolygonVertex>{}),
      builder.CreateVector(std::vector<std::uint32_t>{}),
      builder.CreateVector(std::vector<std::uint32_t>{}),
      builder.CreateVector(std::vector<std::uint32_t>{}),
      builder.CreateVector(std::vector<std::uint32_t>{}),
      builder.CreateVectorOfStructs(std::vector<current::Vec2>{}),
      builder.CreateVector(std::vector<std::uint32_t>{}),
      builder.CreateVector(std::vector<current::SurfaceShading>{}),
      builder.CreateVectorOfStructs(std::vector<current::PolygonEdge>{}),
      builder.CreateVectorOfStructs(vertices), builder.CreateVector(indices),
      builder.CreateVectorOfStructs(primitives), authored_payload,
      builder.CreateVectorOfStructs(mapping));
  builder.Finish(root, "QDRG");
  const auto *begin = reinterpret_cast<const std::byte *>(builder.GetBufferPointer());
  return std::make_shared<const std::vector<std::byte>>(begin, begin + builder.GetSize());
}

MaterialReference material_reference(AssetId embedded, const flatbuffers::String *project) {
  require(embedded == kInvalidAssetId,
          "Retired embedded material bindings are outside this converter's supported surface.");
  if (project == nullptr || project->size() == 0u) {
    return {};
  }
  const auto parsed = ProjectAssetId::parse(project->string_view());
  require(parsed.has_value(), "Retired material UUID is invalid.");
  return MaterialReference::project(*parsed);
}

SceneEntityState convert_entity(const retired::Entity &source,
                               const std::set<AssetId> &mesh_ids) {
  require(source.kind() == retired::EntityKind::Mesh &&
              source.parent() == kInvalidEntityId && source.mesh() != nullptr &&
              source.curve() == nullptr && source.instance_definition() == nullptr &&
              source.instance_occurrence() == nullptr,
          "Retired converter supports only parentless mesh entities; unsupported content was preserved on disk.");
  SceneEntityState result;
  result.id = source.id();
  result.uuid = text(source.uuid());
  result.editor_object_id = source.editor_object_id();
  result.name = text(source.name());
  result.kind = EntityKind::Mesh;
  result.layer_id = source.layer_id();
  if (const auto *value = source.transform()) {
    result.transform = {vector3(value->position()), vector3(value->rotation_euler()),
                        vector3(value->scale(), {1.0f, 1.0f, 1.0f}), value->custom_pivot()};
  }
  if (const auto *value = source.visibility()) {
    result.visibility = {value->visible(), value->selectable()};
  }
  if (const auto *value = source.lock()) {
    result.lock = {value->locked()};
  }
  if (const auto *value = source.modeling_object()) {
    result.modeling_object = ModelingObjectComponent{
        value->object_id(), value->material_slot(), value->sdk_owned(), value->local_frame_version()};
  }
  const auto &mesh = *source.mesh();
  require(mesh_ids.contains(mesh.mesh()) && static_cast<unsigned>(mesh.collision_type()) <= 4u &&
              static_cast<unsigned>(mesh.body_type()) <= 1u,
          "Retired entity has an invalid mesh binding or physics enum.");
  result.mesh = MeshComponent{
      .mesh = mesh.mesh(), .editor_only = mesh.editor_only(), .cast_shadows = true,
      .double_sided = mesh.double_sided(), .generate_collisions = mesh.generate_collisions(),
      .collision_type = static_cast<MeshCollisionType>(mesh.collision_type()),
      .display_collision = mesh.display_collision(),
      .body_type = static_cast<MeshPhysicsBodyType>(mesh.body_type())};
  if (const auto *value = source.material()) {
    MaterialComponent material;
    material.material = material_reference(value->material(), value->project_material_id());
    require(empty(value->material_slots()), "Retired embedded material slots are unsupported.");
    if (const auto *slots = value->project_material_slots()) {
      for (const auto *slot : *slots) {
        const auto reference = material_reference(kInvalidAssetId, slot->project_material_id());
        require(reference.valid() && material.material_slots.emplace(slot->slot(), reference).second,
                "Retired material slots are invalid or duplicated.");
      }
    }
    result.material = std::move(material);
  }
  if (const auto *value = source.texture_mapping()) {
    const auto *stored = value->sampling_from_local();
    require(stored != nullptr && stored->size() == 16u, "Retired texture mapping matrix is invalid.");
    TextureMappingComponent mapping;
    std::copy(stored->begin(), stored->end(), mapping.sampling_from_local.values.begin());
    result.texture_mapping = mapping;
  }
  return result;
}

} // namespace

QdrProjectWriteSnapshot decode_retired_schema10(
    const quader_io::QdrBinaryContainerReadResult &container) {
  require(container.ok, "Cannot convert an invalid QDR container.");
  const quader_io::QdrBinaryChunk *metadata = nullptr;
  std::map<std::uint64_t, const quader_io::QdrBinaryChunk *> geometry;
  for (const auto &chunk : container.chunks) {
    (void)chunk.bytes();
    if (chunk.type == static_cast<std::uint32_t>(quader_io::QdrBinaryChunkType::SceneMetadata)) {
      require(metadata == nullptr && chunk.schema_version == 10u,
              "Retired converter requires exactly one metadata schema-10 chunk.");
      metadata = &chunk;
    } else {
      require(chunk.type == static_cast<std::uint32_t>(quader_io::QdrBinaryChunkType::Geometry) &&
                  chunk.schema_version == 2u && geometry.emplace(chunk.id, &chunk).second,
              "Retired converter encountered unsupported or duplicate chunks.");
    }
  }
  require(metadata != nullptr && !geometry.empty(), "Retired converter requires metadata and geometry.");
  const auto bytes = metadata->bytes();
  flatbuffers::Verifier verifier(reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size());
  require(retired::VerifyScenePayloadBuffer(verifier), "Retired scene FlatBuffer is invalid.");
  const auto &source = *retired::GetScenePayload(bytes.data());
  require(empty(source.materials()) && empty(source.textures()) && empty(source.curves()),
          "Retired embedded materials, textures and curves require a separate lossless converter.");
  require(source.entities() != nullptr && source.meshes() != nullptr,
          "Retired scene lacks its entity or mesh table.");

  // The independent, frozen reader validates identities, every geometry payload,
  // material bindings, hierarchy and explicit roots before conversion begins.
  (void)decode_qdr_scene_binary_material_usage(container);

  QdrProjectWriteSnapshot result;
  result.project_id = source.scene_id();
  result.project_name = text(source.scene_name());
  std::set<AssetId> mesh_ids;
  std::set<std::uint64_t> used_geometry;
  std::map<std::uint64_t, std::pair<bool, std::shared_ptr<const std::vector<std::byte>>>> converted;
  for (const auto *mesh : *source.meshes()) {
    require(mesh->id() != kInvalidAssetId && mesh_ids.insert(mesh->id()).second &&
                geometry.contains(mesh->geometry_chunk_id()),
            "Retired mesh asset identity or geometry reference is invalid.");
    const auto id = mesh->geometry_chunk_id();
    const bool authored = mesh->has_authored_polygon_document();
    auto [found, inserted] = converted.try_emplace(id);
    if (inserted) {
      found->second = {authored, convert_geometry(*geometry.at(id), authored)};
    }
    require(found->second.first == authored, "Retired aliased mesh kinds disagree.");
    result.meshes.push_back({.id = mesh->id(), .name = text(mesh->name()),
                             .has_stable_cpu_topology = mesh->has_stable_cpu_topology(),
                             .has_authored_polygon_document = authored,
                             .encoded_geometry = found->second.second});
    used_geometry.insert(id);
  }
  require(used_geometry.size() == geometry.size(),
          "Retired scene has unreferenced geometry chunks; conversion would discard them.");
  std::map<EntityId, SceneEntityState> entities;
  for (const auto *entity : *source.entities()) {
    require(entity->id() != kInvalidEntityId &&
                entities.emplace(entity->id(), convert_entity(*entity, mesh_ids)).second,
            "Retired scene entity IDs are invalid or duplicated.");
  }
  std::set<EntityId> roots;
  for (const EntityId id : *source.root_entities()) {
    require(entities.contains(id) && roots.insert(id).second, "Retired root list is invalid or duplicated.");
    result.root_entities.push_back(id);
    auto entity = std::move(entities.at(id));
    entity.sibling_ordinal = result.entities.size();
    result.entities.push_back(std::move(entity));
  }
  require(roots.size() == entities.size(), "Retired root list does not include every parentless entity.");
  if (const auto *dependencies = source.project_asset_dependencies()) {
    for (const auto *dependency : *dependencies) {
      const auto id = ProjectAssetId::parse(text(dependency->stable_id()));
      const auto path = ProjectRelativePath::parse_portable(text(dependency->project_relative_path()));
      require(id.has_value() && path.ok() && static_cast<unsigned>(dependency->kind()) >= 1u &&
                  static_cast<unsigned>(dependency->kind()) <= 3u,
              "Retired project asset dependency is invalid.");
      result.project_asset_dependencies.push_back({*id, static_cast<ProjectAssetKind>(dependency->kind()), path.value()});
    }
  }
  if (const auto *layers = source.layers()) {
    for (const auto *layer : *layers) {
      result.layers.push_back({.id = layer->id(), .name = text(layer->name()),
                               .visible = layer->visible(),
                               .wireframe_color = vector3(layer->wireframe_color(), {238.0f / 255.0f, 242.0f / 255.0f, 247.0f / 255.0f}),
                               .locked = layer->locked()});
    }
  }
  if (const auto *camera = source.viewport_camera_pose()) {
    require(camera->eye() != nullptr && camera->target() != nullptr && camera->up() != nullptr,
            "Retired viewport camera is incomplete.");
    const auto eye = vector3(camera->eye());
    const auto target = vector3(camera->target());
    const auto up = vector3(camera->up());
    result.viewport_camera_pose = QdrSceneViewportCameraPose{
        {eye.x, eye.y, eye.z}, {target.x, target.y, target.z}, {up.x, up.y, up.z}};
    require(qdr_scene_viewport_camera_pose_is_valid(*result.viewport_camera_pose),
            "Retired viewport camera is invalid.");
  }

  // Ordinary schema-16 hydration is the final authority for all current
  // geometry, component and cross-reference invariants.
  const auto encoded = QdrDocumentWriter::serialize_project_document_bytes(result, container.scene_uuid);
  const auto check = QdrDocumentReader::deserialize_project_document(encoded);
  if (!check.ok) {
    throw std::runtime_error("Converted retired scene failed canonical validation: " + check.message);
  }
  return result;
}

} // namespace quader::migration
