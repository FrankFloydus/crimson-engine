////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "io/qdr/qdr_document_writer.hpp"

#include "assets/material_document.hpp"
#include "diagnostics/profile.hpp"
#include "io/qdr/qdr_atomic_write.hpp"
#include "io/qdr/qdr_format.hpp"
#include "io/qdr/qdr_parallel_tasks.hpp"
#include "io/qdr/qdr_polygon_document_codec.hpp"
#include "io/qdr/qdr_project_codecs.hpp"
#include "io/qdr/qdr_project_write_snapshot.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quader {
namespace {

using Json = nlohmann::ordered_json;

std::string string_from_view(std::string_view value) {
  return std::string(value.data(), value.size());
}

std::string project_payload_kind_name() {
  return string_from_view(kQdrProjectDocumentKind);
}

std::vector<MeshPrimitiveSpan>
normalized_mesh_primitive_spans(const QdrProjectMeshAssetWriteSnapshot &mesh) {
  std::vector<MeshPrimitiveSpan> spans;
  spans.reserve(mesh.primitives.empty() ? 1u : mesh.primitives.size());
  for (const MeshPrimitiveSpan &primitive : mesh.primitives) {
    const std::uint64_t offset = primitive.index_offset;
    const std::uint64_t count = primitive.index_count;
    if (count == 0u || offset >= mesh.indices.size() ||
        offset + count > mesh.indices.size()) {
      continue;
    }
    spans.push_back(primitive);
  }
  if (spans.empty() && !mesh.indices.empty()) {
    spans.push_back(MeshPrimitiveSpan{
        .index_offset = 0,
        .index_count = static_cast<std::uint32_t>(mesh.indices.size()),
        .material_slot = 0,
    });
  }
  return spans;
}

Json mesh_to_json(const QdrProjectMeshAssetWriteSnapshot &mesh) {
  QDR_PROFILE_FUNCTION();
  Json vertices = Json::array();
  Json indices = Json::array();
  Json primitives = Json::array();
  if (!mesh.has_authored_polygon_document ||
      !mesh.authored_polygon_document.has_value()) {
    vertices.get_ref<Json::array_t &>().reserve(mesh.vertices.size());
    for (const MeshVertex &vertex : mesh.vertices) {
      vertices.push_back(Json{
          {"position", qdr_vec3_to_json(vertex.position)},
          {"normal", qdr_vec3_to_json(vertex.normal)},
          {"uv", qdr_vec2_to_json(vertex.uv)},
          {"rgba", vertex.rgba},
      });
    }

    indices = mesh.indices;
    const std::vector<MeshPrimitiveSpan> normalized_primitives =
        normalized_mesh_primitive_spans(mesh);
    primitives.get_ref<Json::array_t &>().reserve(normalized_primitives.size());
    for (const MeshPrimitiveSpan &primitive : normalized_primitives) {
      primitives.push_back(Json{
          {"indexOffset", primitive.index_offset},
          {"indexCount", primitive.index_count},
          {"materialSlot", primitive.material_slot},
      });
    }
  }

  Json value{
      {"id", mesh.id},
      {"name", mesh.name},
      {"hasStableCpuTopology", mesh.has_stable_cpu_topology},
      {"vertices", vertices},
      {"indices", indices},
      {"primitives", primitives},
  };
  if (mesh.has_authored_polygon_document &&
      mesh.authored_polygon_document.has_value()) {
    value["polygonDocument"] = quader_io::qdr_polygon_document_to_json(
        *mesh.authored_polygon_document);
  }
  return value;
}

Json texture_to_json(const QdrProjectTextureAssetWriteSnapshot &snapshot) {
  const TextureAsset &texture = snapshot.texture;
  return Json{
      {"id", snapshot.id},
      {"name", texture.name},
      {"usage", string_from_view(qdr_texture_usage_name(texture.usage))},
      {"colorSpace",
       string_from_view(qdr_texture_color_space_name(texture.color_space))},
      {"width", texture.width},
      {"height", texture.height},
      {"source_path", texture.source_path.generic_string()},
      {"generated", texture.generated},
  };
}

Json material_to_json(const QdrProjectMaterialAssetWriteSnapshot &snapshot) {
  const MaterialDocument &material = snapshot.material;
  Json texture_slots = Json::object();
  for (const MaterialTextureSlotDescriptor &descriptor :
       material_texture_slot_descriptors()) {
    if (const std::optional<AssetId> texture_id =
            material_texture_slot_asset(material, descriptor.id)) {
      texture_slots[descriptor.id] = *texture_id;
    }
  }
  return Json{
      {"id", snapshot.id},
      {"name", material.name},
      {"baseColor", qdr_vec3_to_json(material.base_color)},
      {"roughness", material.roughness},
      {"metallic", material.metallic},
      {"textureSlots", texture_slots},
      {"nodeGraphReserved", material.node_graph_reserved},
  };
}

Json entity_to_json(const QdrProjectEntityWriteSnapshot &entity) {
  QDR_PROFILE_FUNCTION();
  Json value{
      {"id", entity.id},
      {"uuid", entity.uuid},
      {"editorObjectId", entity.editor_object_id},
      {"name", entity.name},
      {"kind", string_from_view(qdr_entity_kind_name(entity.kind))},
      {"parent", entity.parent},
  };
  if (entity.transform.has_value()) {
    value["transform"] = qdr_transform_component_to_json(*entity.transform);
  }
  if (entity.visibility.has_value()) {
    value["visibility"] = Json{
        {"visible", entity.visibility->visible},
        {"selectable", entity.visibility->selectable},
    };
  }
  if (entity.lock.has_value()) {
    value["locked"] = entity.lock->locked;
  }
  if (entity.selection.has_value()) {
    value["selected"] = entity.selection->selected;
    value["active"] = entity.active;
  }
  if (entity.modeling_object.has_value()) {
    value["modelingObject"] = Json{
        {"objectId", entity.modeling_object->object_id},
        {"materialSlot", entity.modeling_object->material_slot},
    };
  }
  if (entity.mesh.has_value()) {
    value["mesh"] = qdr_mesh_component_to_json(*entity.mesh);
  }
  if (entity.material.has_value()) {
    value["material"] = qdr_material_component_to_json(*entity.material);
  }
  if (entity.light.has_value()) {
    value["light"] = qdr_light_component_to_json(*entity.light);
  }
  if (entity.scene_settings.has_value()) {
    value["sceneRoot"] = entity.scene_root;
    value["sceneSettings"] =
        qdr_scene_settings_component_to_json(*entity.scene_settings);
  }
  return value;
}

Json object_record_to_json(const ProjectObjectRecord &record) {
  return Json{
      {"editorObjectId", record.id},
      {"sceneEntity", record.scene_entity},
      {"mesh", record.mesh},
      {"material", record.material},
      {"modelingObject", record.modeling_object},
      {"authoredName", record.authored_name},
      {"visible", record.visible},
      {"materialSlot", record.material_slot},
  };
}

Json project_payload_to_json(const QdrProjectWriteSnapshot &snapshot) {
  QDR_PROFILE_FUNCTION();

  Json meshes = Json::array();
  std::vector<Json> mesh_entries(snapshot.meshes.size());
  qdr_detail::run_parallel_index_tasks(
      mesh_entries.size(), "QDR mesh encode", [&](std::size_t index) {
        mesh_entries[index] = mesh_to_json(snapshot.meshes[index]);
      });
  meshes.get_ref<Json::array_t &>().reserve(mesh_entries.size());
  for (Json &entry : mesh_entries) {
    meshes.push_back(std::move(entry));
  }

  Json materials = Json::array();
  materials.get_ref<Json::array_t &>().reserve(snapshot.materials.size());
  for (const QdrProjectMaterialAssetWriteSnapshot &material :
       snapshot.materials) {
    materials.push_back(material_to_json(material));
  }

  Json textures = Json::array();
  textures.get_ref<Json::array_t &>().reserve(snapshot.textures.size());
  for (const QdrProjectTextureAssetWriteSnapshot &texture :
       snapshot.textures) {
    textures.push_back(texture_to_json(texture));
  }

  Json entities = Json::array();
  entities.get_ref<Json::array_t &>().reserve(snapshot.entities.size());
  for (const QdrProjectEntityWriteSnapshot &entity : snapshot.entities) {
    entities.push_back(entity_to_json(entity));
  }

  Json object_records = Json::array();
  object_records.get_ref<Json::array_t &>().reserve(
      snapshot.object_records.size());
  for (const ProjectObjectRecord &record : snapshot.object_records) {
    object_records.push_back(object_record_to_json(record));
  }

  return Json{
      {"kind", project_payload_kind_name()},
      {"project",
       Json{
           {"id", snapshot.project_id},
           {"name", snapshot.project_name},
       }},
      {"assets",
       Json{
           {"meshes", meshes},
           {"materials", materials},
           {"textures", textures},
       }},
      {"entities", entities},
      {"objects", object_records},
  };
}

Json project_root_to_json(const QdrProjectWriteSnapshot &snapshot) {
  return Json{
      {"format", string_from_view(kQdrFormatName)},
      {"version", kQdrProjectFormatVersion},
      {string_from_view(kQdrDocumentPayloadKey),
       project_payload_to_json(snapshot)},
  };
}

std::string
serialize_project_document_for_disk(const QdrProjectWriteSnapshot &snapshot) {
  QDR_PROFILE_FUNCTION();
  return project_root_to_json(snapshot).dump();
}

QdrProjectWriteResult
project_write_result_from_atomic(quader_io::QdrAtomicWriteResult result) {
  return {result.ok, std::move(result.message)};
}

} // namespace

std::string
QdrDocumentWriter::serialize_project_document(
    const QdrProjectWriteSnapshot &snapshot) {
  QDR_PROFILE_FUNCTION();
  return project_root_to_json(snapshot).dump(2);
}

std::string
QdrDocumentWriter::serialize_project_document(const ProjectDocument &document) {
  QDR_PROFILE_FUNCTION();
  return serialize_project_document(
      qdr_project_write_snapshot_from_document(document));
}

QdrProjectWriteResult QdrDocumentWriter::write_project_document_atomic(
    const std::filesystem::path &path,
    const QdrProjectWriteSnapshot &snapshot) {
  QDR_PROFILE_FUNCTION();
  const std::string serialized = [&]() {
    QDR_PROFILE_SCOPE("QDR serialize project document");
    return serialize_project_document_for_disk(snapshot);
  }();
  // Hot project saves skip temp-file read-back validation; QDR round-trip tests
  // cover the writer contract without adding save-path parse cost.
  return project_write_result_from_atomic(
      quader_io::write_qdr_text_atomic(path, serialized));
}

QdrProjectWriteResult QdrDocumentWriter::write_project_document_atomic(
    const std::filesystem::path &path, const ProjectDocument &document) {
  QDR_PROFILE_FUNCTION();
  const ProjectDocumentIdentityReport identity =
      document.validate_object_identity_invariants();
  if (!identity.ok()) {
    return {
        .ok = false,
        .message = identity.issues.empty()
                       ? "Project object identity validation failed."
                       : identity.issues.front().message,
    };
  }
  const ProjectDocumentAssetReferenceReport asset_references =
      document.validate_asset_references();
  if (!asset_references.ok()) {
    return {
        .ok = false,
        .message = asset_references.issues.empty()
                       ? "Project asset reference validation failed."
                       : asset_references.issues.front().message,
    };
  }
  return write_project_document_atomic(
      path, qdr_project_write_snapshot_from_document(document));
}

} // namespace quader
