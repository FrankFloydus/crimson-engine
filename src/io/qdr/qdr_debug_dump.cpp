////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "io/qdr/qdr_debug_dump.hpp"

#include "assets/material_document.hpp"
#include "io/qdr/qdr_project_write_snapshot.hpp"
#include "quader/modeling/neutral/polygon/document_topology.hpp"
#include <quader/modeling/curves/curve_document.hpp>

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
        {"textureTransform",
         {{"scale", vec2_json(face.texture_transform.scale)},
          {"shiftUv", vec2_json(face.texture_transform.shift_uv)},
          {"rotationDegrees", face.texture_transform.rotation_degrees}}},
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
  case EntityKind::Instance:
    return "instance";
  case EntityKind::Curve:
    return "curve";
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
  const auto reference_json = [](MaterialReference reference) -> Json {
    if (reference.project_owned()) {
      return {{"kind", "project"},
              {"id", std::string(reference.project_asset().str())}};
    }
    if (reference.embedded()) {
      return {{"kind", "embedded"},
              {"id", reference.embedded_asset()}};
    }
    return {{"kind", "none"}};
  };
  Json result{{"material", reference_json(material.material)}};
  std::vector<std::pair<std::uint32_t, MaterialReference>> slots(
      material.material_slots.begin(), material.material_slots.end());
  std::sort(slots.begin(), slots.end(), [](const auto &left,
                                           const auto &right) {
    return left.first < right.first;
  });
  if (!slots.empty()) {
    Json slot_json = Json::object();
    for (const auto &[slot, reference] : slots) {
      slot_json[std::to_string(slot)] = reference_json(reference);
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
      {"distanceAttenuation", light.distance_attenuation},
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
           {"useExposure", settings.camera.use_exposure},
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
  if (mesh.encoded_geometry) {
    geometry = {
        {"kind", mesh.has_authored_polygon_document
                     ? "encodedAuthoredPolygon"
                     : "encodedCompiledMesh"},
        {"encodedBytes", mesh.encoded_geometry->size()},
    };
  } else if (mesh.has_authored_polygon_document &&
      mesh.authored_polygon_document) {
    geometry = {
        {"kind", "authoredPolygon"},
        {"polygonDocument",
         authored_polygon_json(*mesh.authored_polygon_document)},
    };
  } else {
    static const MeshGeometryPayload empty_geometry;
    const MeshGeometryPayload &payload =
        mesh.geometry ? *mesh.geometry : empty_geometry;
    Json vertices = Json::array();
    for (const MeshVertex &vertex : payload.vertices) {
      vertices.push_back({
          {"position", vec3_json(vertex.position)},
          {"normal", vec3_json(vertex.normal)},
          {"uv", vec2_json(vertex.uv)},
          {"uvCarrier", vertex.uv_carrier},
          {"uvGeometricNormal", vec3_json(vertex.uv_geometric_normal)},
          {"rgba", vertex.rgba},
      });
    }
    Json primitives = Json::array();
    for (const MeshPrimitiveSpan &primitive : payload.primitives) {
      primitives.push_back({
          {"indexOffset", primitive.index_offset},
          {"indexCount", primitive.index_count},
          {"materialSlot", primitive.material_slot},
      });
    }
    geometry = {
        {"kind", "compiledMesh"},
        {"vertices", std::move(vertices)},
        {"indices", payload.indices},
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
      {"transform", transform_json(entity.transform)},
      {"visibility",
       {{"visible", entity.visibility.visible},
        {"selectable", entity.visibility.selectable}}},
      {"locked", entity.lock.locked},
  };
  if (entity.texture_mapping) {
    value["textureMapping"] = {
        {"samplingFromLocal",
         entity.texture_mapping->sampling_from_local.values},
    };
  }
  if (entity.modeling_object) {
    value["modelingObject"] = {
        {"objectId", entity.modeling_object->object_id},
        {"materialSlot", entity.modeling_object->material_slot},
        {"sdkOwned", entity.modeling_object->sdk_owned},
        {"localFrameVersion", entity.modeling_object->local_frame_version},
    };
  }
  if (entity.mesh) {
    value["mesh"] = mesh_component_json(*entity.mesh);
  }
  if (entity.curve) {
    value["curve"] = {
        {"curve", entity.curve->curve},
        {"solidify", entity.curve->solidify},
        {"solidifyResolution", entity.curve->solidify_resolution},
        {"solidifyRadius", entity.curve->solidify_radius},
    };
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
  if (entity.layer_id != 0u) {
    value["layerId"] = entity.layer_id;
  }
  return value;
}

[[nodiscard]] Json
layer_json(const QdrProjectSceneLayerWriteSnapshot &layer) {
  return {
      {"id", layer.id},
      {"name", layer.name},
      {"visible", layer.visible},
      {"wireframeColor",
       {layer.wireframe_color.x, layer.wireframe_color.y,
        layer.wireframe_color.z}},
      {"locked", layer.locked},
  };
}

} // namespace

std::string qdr_scene_debug_json(
    const SceneContentDocument &document,
    const quader_io::QdrBinaryContainerReadResult &container) {
  QdrProjectWriteSnapshotResult snapshot_result =
      qdr_project_write_snapshot_from_document(document);
  if (!snapshot_result.ok()) {
    return Json{{"format", "quader.qdr.debug"},
                {"error", snapshot_result.error().message}}
        .dump(2);
  }
  QdrProjectWriteSnapshot snapshot = snapshot_result.take_value();

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
  Json curves = Json::array();
  for (const QdrProjectCurveAssetWriteSnapshot &curve : snapshot.curves) {
    Json points = Json::array();
    Json segments = Json::array();
    if (curve.document) {
      for (const modeling::CurveControlPoint &point : curve.document->points()) {
        points.push_back({
            {"id", point.id.index},
            {"position", vec3_json({point.position.x, point.position.y,
                                    point.position.z})},
            {"incomingHandle", vec3_json({point.incoming_handle.x,
                                          point.incoming_handle.y,
                                          point.incoming_handle.z})},
            {"outgoingHandle", vec3_json({point.outgoing_handle.x,
                                          point.outgoing_handle.y,
                                          point.outgoing_handle.z})},
        });
      }
      for (const modeling::CurveSegmentId segment : curve.document->segments()) {
        segments.push_back(segment.index);
      }
    }
    curves.push_back({
        {"id", curve.id},
        {"name", curve.name},
        {"closed", curve.document && curve.document->closed()},
        {"points", std::move(points)},
        {"segments", std::move(segments)},
    });
  }
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
  Json layers = Json::array();
  for (const QdrProjectSceneLayerWriteSnapshot &layer : snapshot.layers) {
    layers.push_back(layer_json(layer));
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
           {"curves", std::move(curves)},
           {"materials", std::move(materials)},
           {"textures", std::move(textures)},
      }},
      {"entities", std::move(entities)},
      {"layers", std::move(layers)},
  }.dump(2);
}

} // namespace quader
