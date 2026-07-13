////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "io/qdr/qdr_document_reader.hpp"

#include "assets/material_document.hpp"
#include "diagnostics/profile.hpp"
#include "editor/projection/mesh_projection_service.hpp"
#include "io/qdr/qdr_format.hpp"
#include "io/qdr/qdr_parallel_tasks.hpp"
#include "io/qdr/qdr_polygon_document_codec.hpp"
#include "io/qdr/qdr_project_codecs.hpp"
#include "io/qdr/qdr_root.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <io/qdr/qdr_document.hpp>
#include <mesh/polygon/document.hpp>

namespace quader {
namespace {

using Json = nlohmann::ordered_json;

/**
 * Carries a decoded mesh and its target asset id before it is installed into the asset registry.
 */
struct DecodedMeshAsset {
  AssetId id = kInvalidAssetId;
  MeshAsset mesh;
};

[[nodiscard]] std::string
required_project_enum_string(const Json &value, const char *key,
                             std::string_view label);

QdrProjectReadResult project_error_result(
    quader_io::QdrDiagnostic diagnostic) {
  const std::string message = diagnostic.message;
  return {false, ProjectDocument{}, message, false, {std::move(diagnostic)}};
}

QdrProjectReadResult project_error_result(std::string_view code,
                                          std::string_view path,
                                          std::string message) {
  return project_error_result(
      quader_io::qdr_make_error(code, path, std::move(message)));
}

QdrProjectReadResult project_read_result_with_source_path(
    QdrProjectReadResult result, const std::filesystem::path &path) {
  for (quader_io::QdrDiagnostic &diagnostic : result.diagnostics) {
    if (!diagnostic.source_path.has_value()) {
      diagnostic.source_path = path;
    }
  }
  return result;
}

void require_object(const Json &value, std::string_view label) {
  if (!value.is_object()) {
    throw std::runtime_error(std::string(label) + " must be an object.");
  }
}

QVec3 from_runtime(quader::QVec3 value) { return {value.x, value.y, value.z}; }

MeshAsset mesh_from_polygon_document(const quader_poly::Document &document,
                                     std::string name) {
  QDR_PROFILE_FUNCTION();
  MeshAsset mesh =
      MeshProjectionService::compile_polygon_document_mesh(document);
  mesh.name = std::move(name);
  mesh.has_authored_polygon_document = true;
  mesh.authored_polygon_document =
      std::make_shared<quader_poly::Document>(document);
  return mesh;
}

MeshAsset mesh_from_json(const Json &value) {
  QDR_PROFILE_FUNCTION();
  MeshAsset mesh;
  mesh.name = value.value("name", std::string("Mesh"));
  mesh.has_stable_cpu_topology = value.value("hasStableCpuTopology", true);
  if (const auto vertices_json = value.find("vertices");
      vertices_json != value.end() && vertices_json->is_array()) {
    mesh.vertices.reserve(vertices_json->size());
    for (const Json &vertex_json : *vertices_json) {
      mesh.vertices.push_back(MeshVertex{
          .position =
              qdr_vec3_from_json(vertex_json.value("position", Json::object())),
          .normal = qdr_vec3_from_json(
              vertex_json.value("normal", Json::object()), {0.0f, 1.0f, 0.0f}),
          .uv = qdr_vec2_from_json(vertex_json.value("uv", Json::object())),
          .rgba = vertex_json.value("rgba", 0xffffffffu),
      });
    }
  }
  mesh.indices = value.value("indices", std::vector<std::uint32_t>{});
  if (const auto primitives_json = value.find("primitives");
      primitives_json != value.end() && primitives_json->is_array()) {
    mesh.primitives.reserve(primitives_json->size());
    for (const Json &primitive_json : *primitives_json) {
      mesh.primitives.push_back(MeshPrimitiveSpan{
          .index_offset = primitive_json.value("indexOffset", 0u),
          .index_count = primitive_json.value("indexCount", 0u),
          .material_slot = primitive_json.value("materialSlot", 0u),
      });
    }
  }
  if (const auto polygon = value.find("polygonDocument");
      polygon != value.end() && polygon->is_object()) {
    mesh.has_authored_polygon_document = true;
    mesh.authored_polygon_document = std::make_shared<quader_poly::Document>(
        quader_io::qdr_polygon_document_from_json(
            *polygon,
            quader_io::QdrPolygonDocumentParseMode::StrictCurrentSchema));
    if (mesh.vertices.empty() && mesh.indices.empty()) {
      mesh = mesh_from_polygon_document(*mesh.authored_polygon_document,
                                        mesh.name);
    }
  }
  mesh.primitives = normalized_mesh_primitive_spans(mesh);
  return mesh;
}

TextureAsset texture_from_json(const Json &value) {
  TextureAsset texture;
  texture.name = value.value("name", std::string("Texture"));
  texture.usage = qdr_texture_usage_from_name(
      required_project_enum_string(value, "usage", "texture usage"),
      QdrProjectEnumParseMode::StrictCurrentSchema);
  texture.color_space = qdr_texture_color_space_from_name(
      required_project_enum_string(value, "colorSpace",
                                   "texture color space"),
      QdrProjectEnumParseMode::StrictCurrentSchema);
  texture.width = value.value("width", 0u);
  texture.height = value.value("height", 0u);
  texture.source_path =
      std::filesystem::path(value.value("source_path", std::string{}));
  texture.generated = value.value("generated", false);
  return texture;
}

MaterialDocument material_from_json(const Json &value) {
  MaterialDocument material;
  material.name = value.value("name", std::string("Material"));
  material.base_color = qdr_vec3_from_json(
      value.value("baseColor", Json::object()), {1.0f, 1.0f, 1.0f});
  material.roughness = value.value("roughness", 1.0f);
  material.metallic = value.value("metallic", 0.0f);
  material.node_graph_reserved = value.value("nodeGraphReserved", true);
  if (const auto slots = value.find("textureSlots");
      slots != value.end() && slots->is_object()) {
    for (const auto &[slot, textureId] : slots->items()) {
      if (!set_material_texture_slot(material, slot,
                                     textureId.get<AssetId>())) {
        throw std::runtime_error("QDR project document material is invalid: "
                                 "Unsupported material texture slot.");
      }
    }
  }
  return material;
}

EditorObjectId editor_object_id_from_json(const Json &value) {
  if (const auto editor_object_id = value.find("editorObjectId");
      editor_object_id != value.end() &&
      editor_object_id->is_number_unsigned()) {
    return editor_object_id->get<EditorObjectId>();
  }
  return value.value("id", kInvalidEditorObjectId);
}

ProjectObjectRecord object_record_from_json(const Json &value) {
  return ProjectObjectRecord{
      .id = editor_object_id_from_json(value),
      .scene_entity = value.value("sceneEntity", kInvalidEntityId),
      .mesh = value.value("mesh", kInvalidAssetId),
      .material = value.value("material", kInvalidAssetId),
      .modeling_object =
          value.value("modelingObject", kInvalidModelingObjectId),
      .authored_name = value.value("authoredName", std::string{}),
      .visible = value.value("visible", true),
      .material_slot = value.value("materialSlot", 0u),
  };
}

std::string asset_kind_label(std::string_view kind) {
  return std::string(kind.data(), kind.size());
}

AssetId qdr_asset_id_from_json(const Json &value, std::string_view kind) {
  const AssetId id = value.value("id", kInvalidAssetId);
  if (id == kInvalidAssetId) {
    throw std::runtime_error("QDR project document contains an invalid " +
                             asset_kind_label(kind) + " asset id.");
  }
  return id;
}

void require_unique_asset_id(std::set<AssetId> &ids, AssetId id,
                             std::string_view kind) {
  if (!ids.insert(id).second) {
    throw std::runtime_error("QDR project document contains duplicate " +
                             asset_kind_label(kind) + " asset ids.");
  }
}

void validate_material_asset_for_qdr(const AssetRegistry &assets,
                                     const MaterialDocument &material) {
  const AssetRegistryValidationResult validation =
      assets.validate_material(material);
  if (!validation.ok) {
    throw std::runtime_error("QDR project document material is invalid: " +
                             validation.message + ".");
  }
}

void require_valid_asset_references_for_qdr(
    const ProjectDocumentAssetReferenceReport &report) {
  if (!report.ok()) {
    throw std::runtime_error("QDR project document " +
                             report.issues.front().message);
  }
}

const Json &field_or_default(const Json &value, const char *key,
                             const Json &fallback) {
  const auto found = value.find(key);
  return found == value.end() ? fallback : *found;
}

std::string required_project_enum_string(const Json &value, const char *key,
                                         std::string_view label) {
  const auto field = value.find(key);
  if (field == value.end()) {
    throw quader_io::QdrDiagnosticException(quader_io::qdr_make_error(
        "qdr.missing_enum", "",
        "QDR project document is missing " + std::string(label) +
            " enum field."));
  }
  if (!field->is_string()) {
    throw quader_io::QdrDiagnosticException(quader_io::qdr_make_error(
        "qdr.invalid_enum", "",
        "QDR project document " + std::string(label) +
            " enum field must be a string."));
  }
  return field->get<std::string>();
}

ProjectDocument project_document_from_app_payload(const Json &payload) {
  QDR_PROFILE_FUNCTION();
  require_object(payload, "QDR project document payload");
  const Json empty_object = Json::object();
  const Json empty_array = Json::array();
  const Json &project = field_or_default(payload, "project", empty_object);
  ProjectDocument document(
      ProjectDocumentRecord{
          .id = project.value("id", 0ull),
          .name = project.value("name", std::string("Untitled Project")),
      },
      ProjectDocumentRootPolicy::DeferSceneRoot);

  const Json &assets_json = field_or_default(payload, "assets", empty_object);
  std::set<AssetId> mesh_asset_ids;
  const Json &meshes_json =
      field_or_default(assets_json, "meshes", empty_array);
  std::vector<const Json *> mesh_jsons;
  mesh_jsons.reserve(meshes_json.size());
  std::vector<DecodedMeshAsset> decoded_meshes;
  decoded_meshes.reserve(meshes_json.size());
  for (const Json &mesh_json : meshes_json) {
    const AssetId id = qdr_asset_id_from_json(mesh_json, "mesh");
    require_unique_asset_id(mesh_asset_ids, id, "mesh");
    mesh_jsons.push_back(&mesh_json);
    decoded_meshes.push_back(DecodedMeshAsset{.id = id});
  }
  qdr_detail::run_parallel_index_tasks(
      mesh_jsons.size(), "QDR mesh decode", [&](std::size_t index) {
        decoded_meshes[index].mesh = mesh_from_json(*mesh_jsons[index]);
      });
  for (DecodedMeshAsset &decoded : decoded_meshes) {
    if (document.assets().add_mesh(decoded.id, std::move(decoded.mesh)) ==
        kInvalidAssetId) {
      throw std::runtime_error(
          "QDR project document contains duplicate mesh asset ids.");
    }
  }
  std::set<AssetId> texture_asset_ids;
  for (const Json &texture_json :
       field_or_default(assets_json, "textures", empty_array)) {
    const AssetId id = qdr_asset_id_from_json(texture_json, "texture");
    require_unique_asset_id(texture_asset_ids, id, "texture");
    if (document.assets().add_texture(id, texture_from_json(texture_json)) ==
        kInvalidAssetId) {
      throw std::runtime_error(
          "QDR project document contains duplicate texture asset ids.");
    }
  }
  std::set<AssetId> material_asset_ids;
  for (const Json &material_json :
       field_or_default(assets_json, "materials", empty_array)) {
    const AssetId id = qdr_asset_id_from_json(material_json, "material");
    require_unique_asset_id(material_asset_ids, id, "material");
    MaterialDocument material = material_from_json(material_json);
    validate_material_asset_for_qdr(document.assets(), material);
    if (document.assets().add_material(id, std::move(material)) ==
        kInvalidAssetId) {
      throw std::runtime_error("QDR project document contains duplicate or "
                               "invalid material asset ids.");
    }
  }

  std::vector<std::pair<EntityId, EntityId>> parent_links;
  std::set<EntityId> entities_with_explicit_editor_object_ids;
  EntityId active_selection = kInvalidEntityId;
  bool active_selection_seen = false;
  for (const Json &entity_json :
       field_or_default(payload, "entities", empty_array)) {
    const EntityId requested_id = entity_json.value("id", kInvalidEntityId);
    const bool has_explicit_editor_object_id =
        entity_json.contains("editorObjectId");
    const EditorObjectId editor_object_id =
        has_explicit_editor_object_id
            ? entity_json.value("editorObjectId", kInvalidEditorObjectId)
            : kInvalidEditorObjectId;
    const std::string name = entity_json.value("name", std::string("Entity"));
    const EntityKind kind = qdr_entity_kind_from_name(
        required_project_enum_string(entity_json, "kind", "entity kind"),
        QdrProjectEnumParseMode::StrictCurrentSchema);
    const EntityId entity = document.scene().create_entity_with_id(
        requested_id, name, kind, editor_object_id);
    if (entity == kInvalidEntityId) {
      throw std::runtime_error(
          "QDR document contains a duplicate or invalid entity id.");
    }
    if (const auto uuid_json = entity_json.find("uuid");
        uuid_json != entity_json.end() && uuid_json->is_string() &&
        !document.scene().set_entity_uuid(entity,
                                          uuid_json->get<std::string>())) {
      throw std::runtime_error(
          "QDR document contains a duplicate or invalid entity uuid.");
    }
    if (has_explicit_editor_object_id) {
      entities_with_explicit_editor_object_ids.insert(entity);
    }

    document.scene().set_transform(
        entity, qdr_transform_component_from_json(
                    entity_json.value("transform", Json::object())));
    if (const auto visibility_json = entity_json.find("visibility");
        visibility_json != entity_json.end() && visibility_json->is_object()) {
      document.scene().set_visible(entity,
                                   visibility_json->value("visible", true));
      document.scene().set_selectable(
          entity, visibility_json->value("selectable", true));
    }
    document.scene().set_locked(entity, entity_json.value("locked", false));
    if (const auto object_json = entity_json.find("modelingObject");
        object_json != entity_json.end() && object_json->is_object()) {
      document.scene().set_modeling_object(
          entity, ModelingObjectComponent{
                      .object_id = object_json->value("objectId",
                                                      kInvalidModelingObjectId),
                      .material_slot = object_json->value("materialSlot", 0u),
                  });
    }
    if (const auto mesh_json = entity_json.find("mesh");
        mesh_json != entity_json.end() && mesh_json->is_object()) {
      document.scene().set_mesh(
          entity, qdr_mesh_component_from_json(
                      *mesh_json,
                      QdrProjectEnumParseMode::StrictCurrentSchema));
    }
    if (const auto material_json = entity_json.find("material");
        material_json != entity_json.end() && material_json->is_object()) {
      MaterialComponent material =
          qdr_material_component_from_json(*material_json);
      if (material.material != kInvalidAssetId) {
        document.scene().set_material(entity, std::move(material));
      }
    }
    if (const auto light_json = entity_json.find("light");
        light_json != entity_json.end() && light_json->is_object()) {
      document.scene().set_light(
          entity, qdr_light_component_from_json(
                      *light_json,
                      QdrProjectEnumParseMode::StrictCurrentSchema));
    }
    if (entity_json.value("sceneRoot", false) ||
        entity_json.find("sceneSettings") != entity_json.end()) {
      const Json settings_json =
          entity_json.value("sceneSettings", Json::object());
      if (!document.scene().set_scene_settings(
              entity,
              qdr_scene_settings_component_from_json(
                  settings_json,
                  QdrProjectEnumParseMode::StrictCurrentSchema))) {
        throw std::runtime_error(
            "QDR project document contains multiple scene root entities.");
      }
    }

    if (entity_json.value("selected", false)) {
      document.scene().set_selected(entity, true);
    }
    if (entity_json.value("active", false)) {
      if (active_selection_seen) {
        throw std::runtime_error(
            "QDR project document contains multiple active selections.");
      }
      active_selection_seen = true;
      active_selection = entity;
    }
    parent_links.emplace_back(entity,
                              entity_json.value("parent", kInvalidEntityId));
  }

  for (const auto &[child, parent] : parent_links) {
    if (parent != kInvalidEntityId) {
      if (!document.scene().exists(parent)) {
        throw std::runtime_error(
            "QDR project document entity references a missing parent.");
      }
      if (!document.scene().set_parent(child, parent, false)) {
        throw std::runtime_error(
            "QDR project document contains an invalid entity hierarchy.");
      }
    }
  }
  if (active_selection != kInvalidEntityId) {
    if (!document.scene().set_active_selection(active_selection)) {
      throw std::runtime_error("QDR project document active selection must "
                               "reference a selected entity.");
    }
  }

  if (document.ensure_scene_root() == kInvalidEntityId) {
    throw std::runtime_error(
        "QDR project document could not create a scene root.");
  }

  require_valid_asset_references_for_qdr(document.validate_asset_references());

  std::set<EditorObjectId> object_record_ids;
  std::set<EntityId> object_record_entities;
  const Json &objects_json = field_or_default(payload, "objects", empty_array);
  std::vector<ProjectObjectRecord> object_records;
  object_records.reserve(objects_json.size());
  for (const Json &record_json : objects_json) {
    const ProjectObjectRecord record = object_record_from_json(record_json);
    require_valid_asset_references_for_qdr(
        document.validate_asset_references(record));
    if (!object_record_ids.insert(record.id).second) {
      throw std::runtime_error(
          "QDR project document contains duplicate object record ids.");
    }
    if (!object_record_entities.insert(record.scene_entity).second) {
      throw std::runtime_error("QDR project document contains duplicate object "
                               "record scene entities.");
    }
    if (entities_with_explicit_editor_object_ids.contains(
            record.scene_entity) &&
        document.scene().editor_object_id(record.scene_entity) != record.id) {
      throw std::runtime_error("QDR project object record id does not match "
                               "the scene editor object id.");
    }
    object_records.push_back(record);
  }
  const std::size_t object_record_count = object_records.size();
  if (document.upsert_object_records(std::move(object_records)).size() !=
      object_record_count) {
    throw std::runtime_error("QDR project object record conflicts with an "
                             "existing editor object id.");
  }

  const ProjectDocumentIdentityReport identity =
      document.validate_object_identity_invariants();
  if (!identity.ok()) {
    throw std::runtime_error(
        "QDR project document identity invariant failed: " +
        identity.issues.front().message);
  }

  return document;
}

QVec3 color_from_abgr(std::uint32_t color) {
  const float r = static_cast<float>(color & 0xffu) / 255.0f;
  const float g = static_cast<float>((color >> 8u) & 0xffu) / 255.0f;
  const float b = static_cast<float>((color >> 16u) & 0xffu) / 255.0f;
  return {r, g, b};
}

LightType light_type_from_legacy(quader_io::QdrLightType type) {
  switch (type) {
  case quader_io::QdrLightType::Point:
    return LightType::Point;
  case quader_io::QdrLightType::Spot:
    return LightType::Spot;
  case quader_io::QdrLightType::Directional:
    return LightType::Directional;
  }
  return LightType::Point;
}

ProjectDocument
project_document_from_legacy_qdr(const quader_io::QdrDocument &legacy) {
  QDR_PROFILE_FUNCTION();
  ProjectDocument document(ProjectDocumentRecord{.name = "Imported QDR"},
                           ProjectDocumentRootPolicy::DeferSceneRoot);
  std::unordered_map<std::uint32_t, AssetId> material_by_slot;
  std::unordered_map<int, EntityId> entity_by_legacy_object;

  const auto material_for_slot = [&](std::uint32_t slot) {
    if (const auto existing = material_by_slot.find(slot);
        existing != material_by_slot.end()) {
      return existing->second;
    }
    MaterialDocument material =
        make_default_quader_material("QDR Material " + std::to_string(slot));
    const AssetId id = document.assets().add_material(std::move(material));
    material_by_slot[slot] = id;
    return id;
  };

  for (const quader_io::QdrObject &object : legacy.objects) {
    const std::string name = "Imported Object " + std::to_string(object.id);
    MeshAsset mesh = mesh_from_polygon_document(object.document, name);
    const AssetId mesh_id = document.assets().add_mesh(std::move(mesh));
    const AssetId material_id =
        material_for_slot(static_cast<std::uint32_t>(object.material_id));
    const EntityId entity =
        document.scene().create_entity(name, EntityKind::Mesh);
    document.scene().set_modeling_object(
        entity,
        ModelingObjectComponent{
            .object_id = object.id,
            .material_slot = static_cast<std::uint32_t>(object.material_id),
        });
    document.scene().set_mesh(entity, MeshComponent{.mesh = mesh_id});
    document.scene().set_material(entity,
                                  MaterialComponent{.material = material_id});
    if (object.selected) {
      document.scene().set_selected(entity, true);
    }
    document.create_object_record(ProjectObjectRecord{
        .scene_entity = entity,
        .mesh = mesh_id,
        .material = material_id,
        .modeling_object = object.id,
        .authored_name = name,
        .visible = true,
        .material_slot = static_cast<std::uint32_t>(object.material_id),
    });
    entity_by_legacy_object[object.id] = entity;
  }

  if (const auto active = entity_by_legacy_object.find(legacy.active_object_id);
      active != entity_by_legacy_object.end()) {
    document.scene().set_selected(active->second, true);
    document.scene().set_active_selection(active->second);
  }

  for (const quader_io::QdrLight &legacy_light : legacy.lights) {
    const std::string name =
        legacy_light.name.empty()
            ? "Imported Light " + std::to_string(legacy_light.id)
            : legacy_light.name;
    const EntityId entity =
        document.scene().create_entity(name, EntityKind::Light);
    document.scene().set_transform(
        entity,
        TransformComponent{.position = from_runtime(legacy_light.position)});
    document.scene().set_visible(entity, legacy_light.enabled);
    document.scene().set_light(
        entity, LightComponent{
                    .type = light_type_from_legacy(legacy_light.type),
                    .color = color_from_abgr(legacy_light.color_abgr),
                    .intensity = legacy_light.intensity,
                    .range = legacy_light.range,
                    .casts_shadows = legacy_light.shadow_mode !=
                                     quader_io::QdrLightShadowMode::None,
                });
    if (legacy_light.selected) {
      document.scene().set_selected(entity, true);
    }
  }

  (void)document.ensure_scene_root();
  return document;
}

QdrProjectReadResult legacy_read_result(std::string_view text) {
  QDR_PROFILE_FUNCTION();
  quader_io::QdrReadResult legacy = quader_io::deserialize_document(text);
  if (!legacy.ok) {
    return {false, ProjectDocument{}, legacy.message, false,
            std::move(legacy.diagnostics)};
  }
  return {true, project_document_from_legacy_qdr(legacy.document), {}, true};
}

QdrProjectReadResult read_from_root(const Json &root, std::string_view text) {
  QDR_PROFILE_FUNCTION();
  quader_io::require_qdr_root_object(root);
  const quader_io::QdrRootHeader header = quader_io::read_qdr_root_header(root);
  if (header.format != quader_io::qdr_string_from_view(kQdrFormatName)) {
    return project_error_result(
        "qdr.unsupported_format", "/format",
        "Unsupported Quader file format: " + header.format);
  }

  const int version = header.version;
  if (version < kQdrMinSupportedFormatVersion ||
      version > kQdrProjectFormatVersion) {
    return project_error_result(
        "qdr.unsupported_version", "/version",
        "Unsupported Quader file version: " + std::to_string(version));
  }

  const auto document_payload =
      root.find(quader_io::qdr_string_from_view(kQdrDocumentPayloadKey));
  if (document_payload != root.end()) {
    if (!document_payload->is_object()) {
      return project_error_result(
          "qdr.invalid_payload", "/document",
          "QDR document payload must be an object.");
    }

    const auto kind = document_payload->find("kind");
    if (kind != document_payload->end()) {
      if (!kind->is_string()) {
        return project_error_result(
            "qdr.invalid_payload_kind", "/document/kind",
            "QDR project document payload kind must be a string.");
      }
      const std::string kind_text = kind->get<std::string>();
      if (kind_text !=
          quader_io::qdr_string_from_view(kQdrProjectDocumentKind)) {
        return project_error_result(
            "qdr.unsupported_payload_kind", "/document/kind",
            "Unsupported QDR project document payload kind: " + kind_text);
      }
      if (version != kQdrProjectFormatVersion) {
        return project_error_result(
            "qdr.payload_version_mismatch", "/version",
            "QDR project document payload requires version " +
                std::to_string(kQdrProjectFormatVersion) + ".");
      }
      return {true,
              project_document_from_app_payload(*document_payload),
              {},
              false};
    }

    if (version == kQdrProjectFormatVersion) {
      return project_error_result(
          "qdr.missing_payload_kind", "/document/kind",
          "QDR project document payload is missing its kind.");
    }
    return legacy_read_result(text);
  }

  if (root.find(quader_io::qdr_string_from_view(kQdrLegacySessionPayloadKey)) !=
      root.end()) {
    return legacy_read_result(text);
  }

  return project_error_result("qdr.missing_payload", "",
                              "QDR document is missing its document payload.");
}

} // namespace

QdrProjectReadResult
QdrDocumentReader::deserialize_project_document(std::string_view text) {
  QDR_PROFILE_FUNCTION();
  try {
    const Json root = [&]() {
      QDR_PROFILE_SCOPE("QDR parse project JSON");
      return Json::parse(text.begin(), text.end());
    }();
    return read_from_root(root, text);
  } catch (const quader_io::QdrDiagnosticException &exception) {
    return project_error_result(exception.diagnostic());
  } catch (const nlohmann::json::parse_error &exception) {
    const quader_io::QdrTextLocation location =
        quader_io::qdr_text_location_from_parse_byte(text, exception.byte);
    return project_error_result(quader_io::qdr_make_error(
        "qdr.json_parse_error", "", exception.what(), location.line,
        location.column));
  } catch (const nlohmann::json::exception &exception) {
    return project_error_result("qdr.json_shape_error", "", exception.what());
  } catch (const std::exception &exception) {
    return project_error_result("qdr.read_failed", "", exception.what());
  } catch (...) {
    return project_error_result("qdr.unknown_error", "",
                                "Unknown error reading QDR document.");
  }
}

QdrProjectReadResult
QdrDocumentReader::read_project_document(const std::filesystem::path &path) {
  QDR_PROFILE_FUNCTION();
  std::ifstream stream(path, std::ios::binary);
  if (!stream.is_open()) {
    return project_error_result(quader_io::qdr_make_error(
        "qdr.file_open_failed", "",
        "Could not open QDR document: " + path.string(), std::nullopt,
        std::nullopt, path));
  }
  const std::string text = [&]() {
    QDR_PROFILE_SCOPE("QDR read project file bytes");
    return std::string{std::istreambuf_iterator<char>(stream),
                       std::istreambuf_iterator<char>()};
  }();
  return project_read_result_with_source_path(deserialize_project_document(text),
                                             path);
}

} // namespace quader
