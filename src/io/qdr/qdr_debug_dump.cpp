////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "io/qdr/qdr_debug_dump.hpp"

#include "assets/material_document.hpp"
#include "io/qdr/qdr_project_write_snapshot.hpp"
#include "mesh/polygon/document_topology.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quader {
namespace {

using Json = nlohmann::ordered_json;

[[nodiscard]] Json vec2_json(QVec2 value) {
  return Json{{"x", value.x}, {"y", value.y}};
}

[[nodiscard]] Json vec3_json(QVec3 value) {
  return Json{{"x", value.x}, {"y", value.y}, {"z", value.z}};
}

[[nodiscard]] std::string_view
surface_shading_name(quader_poly::SurfaceShading shading) {
  switch (shading) {
  case quader_poly::SurfaceShading::Flat:
    return "flat";
  case quader_poly::SurfaceShading::Smooth:
    return "smooth";
  case quader_poly::SurfaceShading::Authored:
    return "authored";
  }
  return "unknown";
}

[[nodiscard]] Json
authored_polygon_json(const quader_poly::Document &document) {
  Json vertices = Json::array();
  for (const quader_poly::Vertex &vertex : document.vertices) {
    vertices.push_back({
        {"id", vertex.id},
        {"position", vec3_json(vertex.position)},
    });
  }

  Json faces = Json::array();
  for (const quader_poly::Face &face : document.faces) {
    Json uvs = Json::array();
    for (QVec2 uv : face.uvs) {
      uvs.push_back(vec2_json(uv));
    }
    faces.push_back({
        {"id", face.id},
        {"vertices", face.vertices},
        {"uvs", std::move(uvs)},
        {"materialSlot", face.material_slot},
        {"normalShading", surface_shading_name(face.normal_shading)},
    });
  }

  Json hard_normal_edges = Json::array();
  const std::vector<quader_poly::Edge> document_edges =
      quader_poly::document_edges(document);
  std::set<std::pair<quader_poly::ElementId, quader_poly::ElementId>>
      emitted_edges;
  for (quader_poly::Edge edge : document.hard_normal_edges) {
    edge = quader_poly::make_edge(edge.a, edge.b);
    if (std::find(document_edges.begin(), document_edges.end(), edge) ==
            document_edges.end() ||
        !emitted_edges.insert({edge.a, edge.b}).second) {
      continue;
    }
    hard_normal_edges.push_back({{"a", edge.a}, {"b", edge.b}});
  }

  return {
      {"nextVertexId", document.next_vertex_id},
      {"nextFaceId", document.next_face_id},
      {"vertices", std::move(vertices)},
      {"faces", std::move(faces)},
      {"hardNormalEdges", std::move(hard_normal_edges)},
  };
}

[[nodiscard]] std::string_view entity_kind_name(EntityKind kind) {
  switch (kind) {
  case EntityKind::Empty:
    return "empty";
  case EntityKind::Mesh:
    return "mesh";
  case EntityKind::Light:
    return "light";
  case EntityKind::Generic:
    return "generic";
  }
  return "unknown";
}

[[nodiscard]] std::string_view light_type_name(LightType type) {
  switch (type) {
  case LightType::Directional:
    return "directional";
  case LightType::Point:
    return "point";
  case LightType::Spot:
    return "spot";
  }
  return "unknown";
}

[[nodiscard]] std::string_view collision_type_name(MeshCollisionType type) {
  switch (type) {
  case MeshCollisionType::Box:
    return "box";
  case MeshCollisionType::Sphere:
    return "sphere";
  case MeshCollisionType::Cylinder:
    return "cylinder";
  case MeshCollisionType::ConvexHull:
    return "convexHull";
  case MeshCollisionType::Mesh:
    return "mesh";
  }
  return "unknown";
}

[[nodiscard]] std::string_view body_type_name(MeshPhysicsBodyType type) {
  switch (type) {
  case MeshPhysicsBodyType::StaticBody:
    return "staticBody";
  case MeshPhysicsBodyType::RigidBody:
    return "rigidBody";
  }
  return "unknown";
}

[[nodiscard]] std::string_view texture_usage_name(TextureUsage usage) {
  switch (usage) {
  case TextureUsage::Unknown:
    return "unknown";
  case TextureUsage::BaseColor:
    return "baseColor";
  case TextureUsage::Normal:
    return "normal";
  case TextureUsage::Roughness:
    return "roughness";
  case TextureUsage::Metallic:
    return "metallic";
  case TextureUsage::Curvature:
    return "curvature";
  case TextureUsage::Thickness:
    return "thickness";
  case TextureUsage::AmbientOcclusion:
    return "ambientOcclusion";
  case TextureUsage::Position:
    return "position";
  case TextureUsage::Id:
    return "id";
  case TextureUsage::Mask:
    return "mask";
  }
  return "unknown";
}

[[nodiscard]] std::string_view
texture_color_space_name(TextureColorSpace color_space) {
  switch (color_space) {
  case TextureColorSpace::Srgb:
    return "srgb";
  case TextureColorSpace::Linear:
    return "linear";
  }
  return "unknown";
}

[[nodiscard]] std::string_view tone_mapping_name(SceneToneMapping value) {
  switch (value) {
  case SceneToneMapping::Linear:
    return "linear";
  case SceneToneMapping::AcesLegacy:
    return "acesLegacy";
  case SceneToneMapping::Aces:
    return "aces";
  case SceneToneMapping::Filmic:
    return "filmic";
  case SceneToneMapping::DisplayRange:
    return "displayRange";
  }
  return "unknown";
}

[[nodiscard]] Json transform_json(const TransformComponent &transform) {
  return {
      {"position", vec3_json(transform.position)},
      {"rotationEuler", vec3_json(transform.rotation_euler)},
      {"scale", vec3_json(transform.scale)},
      {"customPivot", transform.custom_pivot},
  };
}

[[nodiscard]] Json mesh_component_json(const MeshComponent &mesh) {
  return {
      {"mesh", mesh.mesh},
      {"editorOnly", mesh.editor_only},
      {"castShadows", mesh.cast_shadows},
      {"doubleSided", mesh.double_sided},
      {"generateCollisions", mesh.generate_collisions},
      {"collisionType", collision_type_name(mesh.collision_type)},
      {"displayCollision", mesh.display_collision},
      {"bodyType", body_type_name(mesh.body_type)},
  };
}

[[nodiscard]] Json material_component_json(const MaterialComponent &material) {
  Json result{{"material", material.material}};
  std::vector<std::pair<std::uint32_t, AssetId>> slots(
      material.material_slots.begin(), material.material_slots.end());
  std::sort(slots.begin(), slots.end());
  if (!slots.empty()) {
    Json slot_json = Json::object();
    for (const auto &[slot, asset] : slots) {
      slot_json[std::to_string(slot)] = asset;
    }
    result["slots"] = std::move(slot_json);
  }
  return result;
}

[[nodiscard]] Json light_json(const LightComponent &light) {
  return {
      {"type", light_type_name(light.type)},
      {"color", vec3_json(light.color)},
      {"intensity", light.intensity},
      {"range", light.range},
      {"castsShadows", light.casts_shadows},
  };
}

[[nodiscard]] Json
scene_settings_json(const SceneSettingsComponent &settings) {
  return {
      {"environment",
       {
           {"environmentId", settings.environment.environment_id},
           {"enabled", settings.environment.enabled},
           {"showBackground", settings.environment.show_background},
           {"clearColor", vec3_json(settings.environment.clear_color)},
           {"intensity", settings.environment.intensity},
           {"backgroundIntensity", settings.environment.background_intensity},
           {"rotationY", settings.environment.rotation_y},
       }},
      {"camera",
       {
           {"aperture", settings.camera.aperture},
           {"shutterSpeed", settings.camera.shutter_speed},
           {"sensitivity", settings.camera.sensitivity},
           {"nearPlane", settings.camera.near_plane},
           {"farPlane", settings.camera.far_plane},
       }},
      {"postProcess",
       {
           {"enabled", settings.post_process.enabled},
           {"antiAliasing", settings.post_process.anti_aliasing},
           {"ambientOcclusion", settings.post_process.ambient_occlusion},
           {"ambientOcclusionIntensity",
            settings.post_process.ambient_occlusion_intensity},
           {"ambientOcclusionRadius",
            settings.post_process.ambient_occlusion_radius},
           {"bloom", settings.post_process.bloom},
           {"bloomStrength", settings.post_process.bloom_strength},
           {"dithering", settings.post_process.dithering},
           {"toneMapping", tone_mapping_name(
                               settings.post_process.tone_mapping)},
       }},
  };
}

[[nodiscard]] std::string uuid_hex(
    const std::array<std::byte, 16> &uuid) {
  std::ostringstream stream;
  stream << std::hex << std::setfill('0');
  for (const std::byte value : uuid) {
    stream << std::setw(2)
           << static_cast<unsigned int>(std::to_integer<std::uint8_t>(value));
  }
  return stream.str();
}

[[nodiscard]] std::string hex_u32(std::uint32_t value) {
  std::ostringstream stream;
  stream << "0x" << std::hex << std::setfill('0') << std::setw(8) << value;
  return stream.str();
}

[[nodiscard]] std::string hex_u64(std::uint64_t value) {
  std::ostringstream stream;
  stream << "0x" << std::hex << std::setfill('0') << std::setw(16) << value;
  return stream.str();
}

[[nodiscard]] std::string_view chunk_type_name(std::uint32_t type) {
  switch (static_cast<quader_io::QdrBinaryChunkType>(type)) {
  case quader_io::QdrBinaryChunkType::SceneMetadata:
    return "sceneMetadata";
  case quader_io::QdrBinaryChunkType::Geometry:
    return "geometry";
  }
  return "unknown";
}

[[nodiscard]] std::string_view
chunk_codec_name(quader_io::QdrBinaryChunkCodec codec) {
  switch (codec) {
  case quader_io::QdrBinaryChunkCodec::None:
    return "none";
  case quader_io::QdrBinaryChunkCodec::Zstd:
    return "zstd";
  }
  return "unknown";
}

[[nodiscard]] Json mesh_json(
    const QdrProjectMeshAssetWriteSnapshot &mesh) {
  Json geometry;
  if (mesh.has_authored_polygon_document &&
      mesh.authored_polygon_document.has_value()) {
    geometry = {
        {"kind", "authoredPolygon"},
        {"polygonDocument",
         authored_polygon_json(*mesh.authored_polygon_document)},
    };
  } else {
    Json vertices = Json::array();
    for (const MeshVertex &vertex : mesh.vertices) {
      vertices.push_back({
          {"position", vec3_json(vertex.position)},
          {"normal", vec3_json(vertex.normal)},
          {"uv", vec2_json(vertex.uv)},
          {"rgba", vertex.rgba},
      });
    }
    Json primitives = Json::array();
    for (const MeshPrimitiveSpan &primitive : mesh.primitives) {
      primitives.push_back({
          {"indexOffset", primitive.index_offset},
          {"indexCount", primitive.index_count},
          {"materialSlot", primitive.material_slot},
      });
    }
    geometry = {
        {"kind", "compiledMesh"},
        {"vertices", std::move(vertices)},
        {"indices", mesh.indices},
        {"primitives", std::move(primitives)},
    };
  }
  return {
      {"id", mesh.id},
      {"name", mesh.name},
      {"hasStableCpuTopology", mesh.has_stable_cpu_topology},
      {"geometry", std::move(geometry)},
  };
}

[[nodiscard]] Json material_json(
    const QdrProjectMaterialAssetWriteSnapshot &snapshot) {
  const MaterialDocument &material = snapshot.material;
  std::vector<std::pair<std::string, AssetId>> sorted_slots(
      material.texture_slots.begin(), material.texture_slots.end());
  std::sort(sorted_slots.begin(), sorted_slots.end());
  Json slots = Json::object();
  for (const auto &[slot, texture] : sorted_slots) {
    slots[slot] = texture;
  }
  return {
      {"id", snapshot.id},
      {"name", material.name},
      {"baseColor", vec3_json(material.base_color)},
      {"roughness", material.roughness},
      {"metallic", material.metallic},
      {"textureSlots", std::move(slots)},
      {"nodeGraphReserved", material.node_graph_reserved},
  };
}

[[nodiscard]] Json texture_json(
    const QdrProjectTextureAssetWriteSnapshot &snapshot) {
  const TextureAsset &texture = snapshot.texture;
  return {
      {"id", snapshot.id},
      {"name", texture.name},
      {"usage", texture_usage_name(texture.usage)},
      {"colorSpace", texture_color_space_name(texture.color_space)},
      {"width", texture.width},
      {"height", texture.height},
      {"sourcePath", texture.source_path.generic_string()},
      {"generated", texture.generated},
  };
}

[[nodiscard]] Json entity_json(const QdrProjectEntityWriteSnapshot &entity) {
  Json value = {
      {"id", entity.id},
      {"uuid", entity.uuid},
      {"editorObjectId", entity.editor_object_id},
      {"name", entity.name},
      {"kind", entity_kind_name(entity.kind)},
      {"parent", entity.parent},
  };
  if (entity.transform) {
    value["transform"] = transform_json(*entity.transform);
  }
  if (entity.visibility) {
    value["visibility"] = {
        {"visible", entity.visibility->visible},
        {"selectable", entity.visibility->selectable},
    };
  }
  if (entity.lock) {
    value["locked"] = entity.lock->locked;
  }
  if (entity.modeling_object) {
    value["modelingObject"] = {
        {"objectId", entity.modeling_object->object_id},
        {"materialSlot", entity.modeling_object->material_slot},
    };
  }
  if (entity.mesh) {
    value["mesh"] = mesh_component_json(*entity.mesh);
  }
  if (entity.material) {
    value["material"] = material_component_json(*entity.material);
  }
  if (entity.light) {
    value["light"] = light_json(*entity.light);
  }
  if (entity.scene_settings) {
    value["sceneRoot"] = entity.scene_root;
    value["sceneSettings"] = scene_settings_json(*entity.scene_settings);
  }
  return value;
}

[[nodiscard]] Json object_json(const ProjectObjectRecord &object) {
  return {
      {"editorObjectId", object.id},
      {"sceneEntity", object.scene_entity},
      {"mesh", object.mesh},
      {"material", object.material},
      {"modelingObject", object.modeling_object},
      {"authoredName", object.authored_name},
      {"visible", object.visible},
      {"materialSlot", object.material_slot},
  };
}

} // namespace

std::string qdr_scene_debug_json(
    const SceneContentDocument &document,
    const quader_io::QdrBinaryContainerReadResult &container) {
  QdrProjectWriteSnapshot snapshot =
      qdr_project_write_snapshot_from_document(document);

  Json chunks = Json::array();
  for (const quader_io::QdrBinaryChunk &chunk : container.chunks) {
    chunks.push_back({
        {"type", chunk_type_name(chunk.type)},
        {"typeId", chunk.type},
        {"id", chunk.id},
        {"schemaVersion", chunk.schema_version},
        {"critical", quader_io::qdr_binary_has_flag(
                         chunk.flags,
                         quader_io::QdrBinaryChunkFlags::Critical)},
        {"derivedCache", quader_io::qdr_binary_has_flag(
                             chunk.flags,
                             quader_io::QdrBinaryChunkFlags::DerivedCache)},
        {"codec", chunk_codec_name(chunk.codec)},
        {"storedSize", chunk.stored_size},
        {"rawSize", chunk.raw_size},
        {"crc32", hex_u32(chunk.checksum)},
        {"contentHash", hex_u64(chunk.content_hash)},
    });
  }

  Json meshes = Json::array();
  for (const QdrProjectMeshAssetWriteSnapshot &mesh : snapshot.meshes) {
    meshes.push_back(mesh_json(mesh));
  }
  Json materials = Json::array();
  for (const QdrProjectMaterialAssetWriteSnapshot &material :
       snapshot.materials) {
    materials.push_back(material_json(material));
  }
  Json textures = Json::array();
  for (const QdrProjectTextureAssetWriteSnapshot &texture : snapshot.textures) {
    textures.push_back(texture_json(texture));
  }
  Json entities = Json::array();
  for (const QdrProjectEntityWriteSnapshot &entity : snapshot.entities) {
    entities.push_back(entity_json(entity));
  }
  Json objects = Json::array();
  for (const ProjectObjectRecord &object : snapshot.object_records) {
    objects.push_back(object_json(object));
  }

  return Json{
      {"format", "quader.qdr.debug"},
      {"warning", "Developer inspection output; not a persistence format."},
      {"container",
       {
           {"magic", "QDRB"},
           {"major", quader_io::kQdrBinaryContainerMajor},
           {"minor", quader_io::kQdrBinaryContainerMinor},
           {"sceneUuid", uuid_hex(container.scene_uuid)},
           {"chunks", std::move(chunks)},
       }},
      {"scene", {{"id", snapshot.project_id}, {"name", snapshot.project_name}}},
      {"assets",
       {
           {"meshes", std::move(meshes)},
           {"materials", std::move(materials)},
           {"textures", std::move(textures)},
       }},
      {"entities", std::move(entities)},
      {"objects", std::move(objects)},
  }.dump(2);
}

} // namespace quader
