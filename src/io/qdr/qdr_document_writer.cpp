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

#include <nlohmann/json.hpp>

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

Json mesh_to_json(AssetId id, const MeshAsset &mesh) {
  QDR_PROFILE_FUNCTION();
  Json vertices = Json::array();
  Json indices = Json::array();
  Json primitives = Json::array();
  if (!mesh.has_authored_polygon_document || !mesh.authored_polygon_document) {
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
      {"id", id},
      {"name", mesh.name},
      {"hasStableCpuTopology", mesh.has_stable_cpu_topology},
      {"vertices", vertices},
      {"indices", indices},
      {"primitives", primitives},
  };
  if (mesh.has_authored_polygon_document && mesh.authored_polygon_document) {
    value["polygonDocument"] = quader_io::qdr_polygon_document_to_json(
        *mesh.authored_polygon_document);
  }
  return value;
}

Json texture_to_json(AssetId id, const TextureAsset &texture) {
  return Json{
      {"id", id},
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

Json material_to_json(AssetId id, const MaterialDocument &material) {
  Json texture_slots = Json::object();
  for (const MaterialTextureSlotDescriptor &descriptor :
       material_texture_slot_descriptors()) {
    if (const std::optional<AssetId> texture_id =
            material_texture_slot_asset(material, descriptor.id)) {
      texture_slots[descriptor.id] = *texture_id;
    }
  }
  return Json{
      {"id", id},
      {"name", material.name},
      {"baseColor", qdr_vec3_to_json(material.base_color)},
      {"roughness", material.roughness},
      {"metallic", material.metallic},
      {"textureSlots", texture_slots},
      {"nodeGraphReserved", material.node_graph_reserved},
  };
}

Json entity_to_json(const SceneDocument &scene, EntityId entity) {
  QDR_PROFILE_FUNCTION();
  Json value{
      {"id", entity},
      {"uuid", scene.uuid(entity)},
      {"editorObjectId", scene.editor_object_id(entity)},
      {"name", scene.name(entity) ? scene.name(entity)->name : std::string{}},
      {"kind", string_from_view(qdr_entity_kind_name(scene.kind(entity)))},
      {"parent", scene.parent(entity)},
  };
  if (const TransformComponent *transform = scene.transform(entity)) {
    value["transform"] = qdr_transform_component_to_json(*transform);
  }
  if (const VisibilityComponent *visibility = scene.visibility(entity)) {
    value["visibility"] = Json{
        {"visible", visibility->visible},
        {"selectable", visibility->selectable},
    };
  }
  if (const LockComponent *lock = scene.lock(entity)) {
    value["locked"] = lock->locked;
  }
  if (const SelectionComponent *selection = scene.selection(entity)) {
    value["selected"] = selection->selected;
    value["active"] = scene.primary_selection() == entity;
  }
  if (const ModelingObjectComponent *object = scene.modeling_object(entity)) {
    value["modelingObject"] = Json{
        {"objectId", object->object_id},
        {"materialSlot", object->material_slot},
    };
  }
  if (const MeshComponent *mesh = scene.mesh(entity)) {
    value["mesh"] = qdr_mesh_component_to_json(*mesh);
  }
  if (const MaterialComponent *material = scene.material(entity)) {
    value["material"] = qdr_material_component_to_json(*material);
  }
  if (const LightComponent *light = scene.light(entity)) {
    value["light"] = qdr_light_component_to_json(*light);
  }
  if (const SceneSettingsComponent *settings = scene.scene_settings(entity)) {
    value["sceneRoot"] = scene.is_scene_root(entity);
    value["sceneSettings"] = qdr_scene_settings_component_to_json(*settings);
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

Json project_payload_to_json(const ProjectDocument &document) {
  QDR_PROFILE_FUNCTION();
  const SceneDocument &scene = document.scene();
  const AssetRegistry &assets = document.assets();

  Json meshes = Json::array();
  const std::vector<AssetId> mesh_ids = assets.mesh_ids();
  /**
   * Holds one mesh asset and its encoded JSON payload for parallel QDR
   * serialization.
   */
  struct MeshToWrite {
    AssetId id = kInvalidAssetId;
    const MeshAsset *mesh = nullptr;
    Json value;
  };
  std::vector<MeshToWrite> mesh_entries;
  mesh_entries.reserve(mesh_ids.size());
  for (const AssetId id : mesh_ids) {
    if (const MeshAsset *mesh = assets.mesh(id)) {
      mesh_entries.push_back(MeshToWrite{.id = id, .mesh = mesh});
    }
  }
  qdr_detail::run_parallel_index_tasks(
      mesh_entries.size(), "QDR mesh encode", [&](std::size_t index) {
        mesh_entries[index].value =
            mesh_to_json(mesh_entries[index].id, *mesh_entries[index].mesh);
      });
  meshes.get_ref<Json::array_t &>().reserve(mesh_entries.size());
  for (MeshToWrite &entry : mesh_entries) {
    meshes.push_back(std::move(entry.value));
  }

  Json materials = Json::array();
  const std::vector<AssetId> material_ids = assets.material_ids();
  materials.get_ref<Json::array_t &>().reserve(material_ids.size());
  for (const AssetId id : material_ids) {
    if (const MaterialDocument *material = assets.material(id)) {
      materials.push_back(material_to_json(id, *material));
    }
  }

  Json textures = Json::array();
  const std::vector<AssetId> texture_ids = assets.texture_ids();
  textures.get_ref<Json::array_t &>().reserve(texture_ids.size());
  for (const AssetId id : texture_ids) {
    if (const TextureAsset *texture = assets.texture(id)) {
      textures.push_back(texture_to_json(id, *texture));
    }
  }

  Json entities = Json::array();
  const std::vector<EntityId> entity_ids = scene.entity_tree_order();
  entities.get_ref<Json::array_t &>().reserve(entity_ids.size());
  for (const EntityId entity : entity_ids) {
    entities.push_back(entity_to_json(scene, entity));
  }

  Json object_records = Json::array();
  const std::vector<ProjectObjectRecord> records = document.object_records();
  object_records.get_ref<Json::array_t &>().reserve(records.size());
  for (const ProjectObjectRecord &record : records) {
    object_records.push_back(object_record_to_json(record));
  }

  return Json{
      {"kind", string_from_view(kQdrProjectDocumentKind)},
      {"project",
       Json{
           {"id", document.id()},
           {"name", document.name()},
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

Json project_root_to_json(const ProjectDocument &document) {
  return Json{
      {"format", string_from_view(kQdrFormatName)},
      {"version", kQdrProjectFormatVersion},
      {string_from_view(kQdrDocumentPayloadKey),
       project_payload_to_json(document)},
  };
}

std::string
serialize_project_document_for_disk(const ProjectDocument &document) {
  QDR_PROFILE_FUNCTION();
  return project_root_to_json(document).dump();
}

QdrProjectWriteResult
project_write_result_from_atomic(quader_io::QdrAtomicWriteResult result) {
  return {result.ok, std::move(result.message)};
}

} // namespace

std::string
QdrDocumentWriter::serialize_project_document(const ProjectDocument &document) {
  QDR_PROFILE_FUNCTION();
  return project_root_to_json(document).dump(2);
}

QdrProjectWriteResult QdrDocumentWriter::write_project_document_atomic(
    const std::filesystem::path &path, const ProjectDocument &document) {
  QDR_PROFILE_FUNCTION();
  const std::string serialized = [&]() {
    QDR_PROFILE_SCOPE("QDR serialize project document");
    return serialize_project_document_for_disk(document);
  }();
  return project_write_result_from_atomic(
      quader_io::write_qdr_text_atomic(path, serialized));
}

} // namespace quader
