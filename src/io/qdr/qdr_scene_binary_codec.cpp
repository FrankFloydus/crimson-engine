////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "io/qdr/qdr_scene_binary_codec.hpp"

#include "assets/asset_registry.hpp"
#include "assets/material_document.hpp"
#include "diagnostics/profile.hpp"
#include "editor/projection/mesh_projection_service.hpp"
#include "scene/scene_texture_mapping.hpp"

#include <quader/modeling/io/qdr_document.hpp>
#include <quader/modeling/neutral/polygon/document_validation.hpp>

#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/verifier.h>

#include "qdr_scene_generated.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace quader {
namespace {

namespace fb = ::quader::qdr::fb;

constexpr std::uint16_t kMinimumSceneChunkSchemaVersion = 1u;
constexpr std::uint16_t kSceneChunkSchemaVersion = 7u;
constexpr std::uint16_t kGeometryChunkSchemaVersion = 1u;

[[nodiscard]] fb::Vec2 to_fb(QVec2 value) {
  return fb::Vec2(value.x, value.y);
}

[[nodiscard]] fb::Vec3 to_fb(QVec3 value) {
  return fb::Vec3(value.x, value.y, value.z);
}

[[nodiscard]] fb::Vec3 to_fb(QdrSceneViewportCameraVector value) {
  return fb::Vec3(value.x, value.y, value.z);
}

[[nodiscard]] QVec2 from_fb(const fb::Vec2 *value) {
  return value ? QVec2{value->x(), value->y()} : QVec2{};
}

[[nodiscard]] QVec2 from_fb(const fb::Vec2 &value) {
  return {value.x(), value.y()};
}

[[nodiscard]] QVec3 from_fb(const fb::Vec3 *value,
                            QVec3 fallback = {}) {
  return value ? QVec3{value->x(), value->y(), value->z()} : fallback;
}

[[nodiscard]] QVec3 from_fb(const fb::Vec3 &value, QVec3 = {}) {
  return {value.x(), value.y(), value.z()};
}

[[nodiscard]] QdrSceneViewportCameraVector
qdr_camera_vector_from_fb(const fb::Vec3 *value) {
  return value ? QdrSceneViewportCameraVector{
                     value->x(), value->y(), value->z()}
               : QdrSceneViewportCameraVector{};
}

[[nodiscard]] quader::modeling::AuthoredSurfaceShading
sdk_shading(quader_poly::SurfaceShading shading) {
  switch (shading) {
  case quader_poly::SurfaceShading::Flat:
    return quader::modeling::AuthoredSurfaceShading::Flat;
  case quader_poly::SurfaceShading::Smooth:
    return quader::modeling::AuthoredSurfaceShading::Smooth;
  case quader_poly::SurfaceShading::Authored:
    return quader::modeling::AuthoredSurfaceShading::Authored;
  }
  return quader::modeling::AuthoredSurfaceShading::Flat;
}

[[nodiscard]] quader_poly::SurfaceShading
native_shading(quader::modeling::AuthoredSurfaceShading shading) {
  switch (shading) {
  case quader::modeling::AuthoredSurfaceShading::Flat:
    return quader_poly::SurfaceShading::Flat;
  case quader::modeling::AuthoredSurfaceShading::Smooth:
    return quader_poly::SurfaceShading::Smooth;
  case quader::modeling::AuthoredSurfaceShading::Authored:
    return quader_poly::SurfaceShading::Authored;
  }
  return quader_poly::SurfaceShading::Flat;
}

[[nodiscard]] quader::modeling::AuthoredTopology
sdk_authored_topology(const quader_poly::Document &document) {
  quader::modeling::AuthoredTopology topology;
  topology.next_vertex_id = document.next_vertex_id;
  topology.next_face_id = document.next_face_id;
  topology.vertices.reserve(document.vertices.size());
  for (const quader_poly::Vertex &vertex : document.vertices) {
    topology.vertices.push_back({
        .id = quader::modeling::make_id<quader::modeling::VertexTag>(vertex.id),
        .position = {vertex.position.x, vertex.position.y, vertex.position.z},
    });
  }
  topology.faces.reserve(document.faces.size());
  for (const quader_poly::Face &face : document.faces) {
    quader::modeling::AuthoredTopologyFace record;
    record.id =
        quader::modeling::make_id<quader::modeling::FaceTag>(face.id);
    record.material_slot = face.material_slot;
    record.shading = sdk_shading(face.normal_shading);
    record.texture_transform = {
        .scale = {face.texture_transform.scale.x,
                  face.texture_transform.scale.y},
        .shift_uv = {face.texture_transform.shift_uv.x,
                     face.texture_transform.shift_uv.y},
        .rotation_degrees = face.texture_transform.rotation_degrees,
    };
    record.vertices.reserve(face.vertices.size());
    for (quader_poly::ElementId id : face.vertices) {
      record.vertices.push_back(
          quader::modeling::make_id<quader::modeling::VertexTag>(id));
    }
    record.uvs.reserve(face.uvs.size());
    for (QVec2 uv : face.uvs) {
      record.uvs.push_back({uv.x, uv.y});
    }
    topology.faces.push_back(std::move(record));
  }
  topology.hard_normal_edges.reserve(document.hard_normal_edges.size());
  for (quader_poly::Edge edge : document.hard_normal_edges) {
    topology.hard_normal_edges.push_back(quader::modeling::make_edge_key(
        quader::modeling::make_id<quader::modeling::VertexTag>(edge.a),
        quader::modeling::make_id<quader::modeling::VertexTag>(edge.b)));
  }
  return topology;
}

[[nodiscard]] quader_poly::Document native_authored_topology(
    const quader::modeling::AuthoredTopology &topology) {
  quader_poly::Document document;
  document.next_vertex_id = topology.next_vertex_id;
  document.next_face_id = topology.next_face_id;
  document.vertices.reserve(topology.vertices.size());
  for (const quader::modeling::AuthoredTopologyVertex &vertex :
       topology.vertices) {
    document.vertices.push_back({
        .id = vertex.id.index,
        .position = {vertex.position.x, vertex.position.y, vertex.position.z},
    });
  }
  document.faces.reserve(topology.faces.size());
  for (const quader::modeling::AuthoredTopologyFace &face : topology.faces) {
    quader_poly::Face record;
    record.id = face.id.index;
    record.material_slot = face.material_slot;
    record.normal_shading = native_shading(face.shading);
    record.texture_transform = {
        .scale = {face.texture_transform.scale.x,
                  face.texture_transform.scale.y},
        .shift_uv = {face.texture_transform.shift_uv.x,
                     face.texture_transform.shift_uv.y},
        .rotation_degrees = face.texture_transform.rotation_degrees,
    };
    record.vertices.reserve(face.vertices.size());
    for (quader::modeling::VertexId id : face.vertices) {
      record.vertices.push_back(id.index);
    }
    record.uvs.reserve(face.uvs.size());
    for (quader::modeling::Vec2 uv : face.uvs) {
      record.uvs.push_back({uv.x, uv.y});
    }
    document.faces.push_back(std::move(record));
  }
  document.hard_normal_edges.reserve(topology.hard_normal_edges.size());
  for (quader::modeling::EdgeKey edge : topology.hard_normal_edges) {
    document.hard_normal_edges.push_back(
        quader_poly::make_edge(edge.a.index, edge.b.index));
  }
  return document;
}

[[nodiscard]] std::string string_from_fb(const flatbuffers::String *value,
                                         std::string fallback = {}) {
  return value ? value->str() : std::move(fallback);
}

[[nodiscard]] fb::EntityKind to_fb(EntityKind value) {
  switch (value) {
  case EntityKind::Empty:
    return fb::EntityKind::Empty;
  case EntityKind::Mesh:
    return fb::EntityKind::Mesh;
  case EntityKind::Light:
    return fb::EntityKind::Light;
  case EntityKind::Generic:
    return fb::EntityKind::Generic;
  case EntityKind::Instance:
    return fb::EntityKind::Instance;
  }
  throw std::runtime_error("Cannot encode an unknown scene entity kind.");
}

[[nodiscard]] EntityKind from_fb(fb::EntityKind value) {
  switch (value) {
  case fb::EntityKind::Empty:
    return EntityKind::Empty;
  case fb::EntityKind::Mesh:
    return EntityKind::Mesh;
  case fb::EntityKind::Light:
    return EntityKind::Light;
  case fb::EntityKind::Generic:
    return EntityKind::Generic;
  case fb::EntityKind::Instance:
    return EntityKind::Instance;
  default:
    throw std::runtime_error("QDR scene contains an unknown entity kind.");
  }
}

[[nodiscard]] fb::MeshCollisionType to_fb(MeshCollisionType value) {
  switch (value) {
  case MeshCollisionType::Box:
    return fb::MeshCollisionType::Box;
  case MeshCollisionType::Sphere:
    return fb::MeshCollisionType::Sphere;
  case MeshCollisionType::Cylinder:
    return fb::MeshCollisionType::Cylinder;
  case MeshCollisionType::ConvexHull:
    return fb::MeshCollisionType::ConvexHull;
  case MeshCollisionType::Mesh:
    return fb::MeshCollisionType::Mesh;
  }
  throw std::runtime_error("Cannot encode an unknown mesh collision type.");
}

[[nodiscard]] MeshCollisionType from_fb(fb::MeshCollisionType value) {
  switch (value) {
  case fb::MeshCollisionType::Box:
    return MeshCollisionType::Box;
  case fb::MeshCollisionType::Sphere:
    return MeshCollisionType::Sphere;
  case fb::MeshCollisionType::Cylinder:
    return MeshCollisionType::Cylinder;
  case fb::MeshCollisionType::ConvexHull:
    return MeshCollisionType::ConvexHull;
  case fb::MeshCollisionType::Mesh:
    return MeshCollisionType::Mesh;
  default:
    throw std::runtime_error(
        "QDR scene contains an unknown mesh collision type.");
  }
}

[[nodiscard]] fb::MeshPhysicsBodyType to_fb(MeshPhysicsBodyType value) {
  switch (value) {
  case MeshPhysicsBodyType::StaticBody:
    return fb::MeshPhysicsBodyType::StaticBody;
  case MeshPhysicsBodyType::RigidBody:
    return fb::MeshPhysicsBodyType::RigidBody;
  }
  throw std::runtime_error("Cannot encode an unknown mesh body type.");
}

[[nodiscard]] MeshPhysicsBodyType
mesh_body_type_from_fb(fb::MeshPhysicsBodyType value) {
  switch (value) {
  case fb::MeshPhysicsBodyType::StaticBody:
    return MeshPhysicsBodyType::StaticBody;
  case fb::MeshPhysicsBodyType::RigidBody:
    return MeshPhysicsBodyType::RigidBody;
  default:
    throw std::runtime_error("QDR scene contains an unknown mesh body type.");
  }
}

[[nodiscard]] fb::LightType to_fb(LightType value) {
  switch (value) {
  case LightType::Directional:
    return fb::LightType::Directional;
  case LightType::Point:
    return fb::LightType::Point;
  case LightType::Spot:
    return fb::LightType::Spot;
  }
  throw std::runtime_error("Cannot encode an unknown light type.");
}

[[nodiscard]] LightType light_type_from_fb(fb::LightType value) {
  switch (value) {
  case fb::LightType::Directional:
    return LightType::Directional;
  case fb::LightType::Point:
    return LightType::Point;
  case fb::LightType::Spot:
    return LightType::Spot;
  default:
    throw std::runtime_error("QDR scene contains an unknown light type.");
  }
}

[[nodiscard]] fb::SceneToneMapping to_fb(SceneToneMapping value) {
  switch (value) {
  case SceneToneMapping::Linear:
    return fb::SceneToneMapping::Linear;
  case SceneToneMapping::AcesLegacy:
    return fb::SceneToneMapping::AcesLegacy;
  case SceneToneMapping::Aces:
    return fb::SceneToneMapping::Aces;
  case SceneToneMapping::Filmic:
    return fb::SceneToneMapping::Filmic;
  case SceneToneMapping::DisplayRange:
    return fb::SceneToneMapping::DisplayRange;
  }
  throw std::runtime_error("Cannot encode an unknown tone-mapping mode.");
}

[[nodiscard]] SceneToneMapping
tone_mapping_from_fb(fb::SceneToneMapping value) {
  switch (value) {
  case fb::SceneToneMapping::Linear:
    return SceneToneMapping::Linear;
  case fb::SceneToneMapping::AcesLegacy:
    return SceneToneMapping::AcesLegacy;
  case fb::SceneToneMapping::Aces:
    return SceneToneMapping::Aces;
  case fb::SceneToneMapping::Filmic:
    return SceneToneMapping::Filmic;
  case fb::SceneToneMapping::DisplayRange:
    return SceneToneMapping::DisplayRange;
  default:
    throw std::runtime_error(
        "QDR scene contains an unknown tone-mapping mode.");
  }
}

[[nodiscard]] fb::TextureUsage to_fb(TextureUsage value) {
  switch (value) {
  case TextureUsage::Unknown:
    return fb::TextureUsage::Unknown;
  case TextureUsage::BaseColor:
    return fb::TextureUsage::BaseColor;
  case TextureUsage::Normal:
    return fb::TextureUsage::Normal;
  case TextureUsage::Roughness:
    return fb::TextureUsage::Roughness;
  case TextureUsage::Metallic:
    return fb::TextureUsage::Metallic;
  case TextureUsage::Curvature:
    return fb::TextureUsage::Curvature;
  case TextureUsage::Thickness:
    return fb::TextureUsage::Thickness;
  case TextureUsage::AmbientOcclusion:
    return fb::TextureUsage::AmbientOcclusion;
  case TextureUsage::Position:
    return fb::TextureUsage::Position;
  case TextureUsage::Id:
    return fb::TextureUsage::Id;
  case TextureUsage::Mask:
    return fb::TextureUsage::Mask;
  }
  throw std::runtime_error("Cannot encode an unknown texture usage.");
}

[[nodiscard]] TextureUsage texture_usage_from_fb(fb::TextureUsage value) {
  switch (value) {
  case fb::TextureUsage::Unknown:
    return TextureUsage::Unknown;
  case fb::TextureUsage::BaseColor:
    return TextureUsage::BaseColor;
  case fb::TextureUsage::Normal:
    return TextureUsage::Normal;
  case fb::TextureUsage::Roughness:
    return TextureUsage::Roughness;
  case fb::TextureUsage::Metallic:
    return TextureUsage::Metallic;
  case fb::TextureUsage::Curvature:
    return TextureUsage::Curvature;
  case fb::TextureUsage::Thickness:
    return TextureUsage::Thickness;
  case fb::TextureUsage::AmbientOcclusion:
    return TextureUsage::AmbientOcclusion;
  case fb::TextureUsage::Position:
    return TextureUsage::Position;
  case fb::TextureUsage::Id:
    return TextureUsage::Id;
  case fb::TextureUsage::Mask:
    return TextureUsage::Mask;
  default:
    throw std::runtime_error("QDR scene contains an unknown texture usage.");
  }
}

[[nodiscard]] fb::TextureColorSpace to_fb(TextureColorSpace value) {
  switch (value) {
  case TextureColorSpace::Linear:
    return fb::TextureColorSpace::Linear;
  case TextureColorSpace::Srgb:
    return fb::TextureColorSpace::Srgb;
  }
  throw std::runtime_error("Cannot encode an unknown texture color space.");
}

[[nodiscard]] TextureColorSpace
texture_color_space_from_fb(fb::TextureColorSpace value) {
  switch (value) {
  case fb::TextureColorSpace::Linear:
    return TextureColorSpace::Linear;
  case fb::TextureColorSpace::Srgb:
    return TextureColorSpace::Srgb;
  default:
    throw std::runtime_error(
        "QDR scene contains an unknown texture color space.");
  }
}

[[nodiscard]] fb::ProjectAssetKind to_fb(ProjectAssetKind value) {
  switch (value) {
  case ProjectAssetKind::Material:
    return fb::ProjectAssetKind::Material;
  case ProjectAssetKind::Texture:
    return fb::ProjectAssetKind::Texture;
  case ProjectAssetKind::Model:
    return fb::ProjectAssetKind::Model;
  case ProjectAssetKind::Unknown:
    return fb::ProjectAssetKind::Unknown;
  }
  return fb::ProjectAssetKind::Unknown;
}

[[nodiscard]] ProjectAssetKind
project_asset_kind_from_fb(fb::ProjectAssetKind value) {
  switch (value) {
  case fb::ProjectAssetKind::Material:
    return ProjectAssetKind::Material;
  case fb::ProjectAssetKind::Texture:
    return ProjectAssetKind::Texture;
  case fb::ProjectAssetKind::Model:
    return ProjectAssetKind::Model;
  case fb::ProjectAssetKind::Unknown:
    return ProjectAssetKind::Unknown;
  }
  return ProjectAssetKind::Unknown;
}

[[nodiscard]] fb::SurfaceShading
to_fb(quader_poly::SurfaceShading value) {
  switch (value) {
  case quader_poly::SurfaceShading::Flat:
    return fb::SurfaceShading::Flat;
  case quader_poly::SurfaceShading::Smooth:
    return fb::SurfaceShading::Smooth;
  case quader_poly::SurfaceShading::Authored:
    return fb::SurfaceShading::Authored;
  }
  throw std::runtime_error("Cannot encode an unknown polygon shading mode.");
}

[[nodiscard]] quader_poly::SurfaceShading
surface_shading_from_fb(fb::SurfaceShading value) {
  switch (value) {
  case fb::SurfaceShading::Flat:
    return quader_poly::SurfaceShading::Flat;
  case fb::SurfaceShading::Smooth:
    return quader_poly::SurfaceShading::Smooth;
  case fb::SurfaceShading::Authored:
    return quader_poly::SurfaceShading::Authored;
  default:
    throw std::runtime_error(
        "QDR geometry contains an unknown polygon shading mode.");
  }
}

[[nodiscard]] std::array<std::byte, 16>
scene_uuid_from_id(std::uint64_t scene_id) {
  std::array<std::byte, 16> uuid{};
  for (std::size_t index = 0u; index < sizeof(scene_id); ++index) {
    uuid[index] =
        static_cast<std::byte>((scene_id >> (index * 8u)) & 0xffu);
  }
  return uuid;
}

[[nodiscard]] std::vector<MeshPrimitiveSpan> normalized_primitives(
    const QdrProjectMeshAssetWriteSnapshot &mesh) {
  if (!mesh.geometry) {
    return {};
  }
  const MeshGeometryPayload &geometry = *mesh.geometry;
  std::vector<MeshPrimitiveSpan> primitives;
  primitives.reserve(geometry.primitives.empty() ? 1u
                                                 : geometry.primitives.size());
  for (const MeshPrimitiveSpan primitive : geometry.primitives) {
    const std::uint64_t end = static_cast<std::uint64_t>(primitive.index_offset) +
                              primitive.index_count;
    if (primitive.index_count != 0u &&
        primitive.index_offset < geometry.indices.size() &&
        end <= geometry.indices.size()) {
      primitives.push_back(primitive);
    }
  }
  if (primitives.empty() && !geometry.indices.empty()) {
    primitives.push_back({
        .index_offset = 0u,
        .index_count = static_cast<std::uint32_t>(geometry.indices.size()),
        .material_slot = 0u,
    });
  }
  return primitives;
}

[[nodiscard]] std::vector<std::byte> flatbuffer_bytes(
    const flatbuffers::FlatBufferBuilder &builder) {
  const auto *begin = reinterpret_cast<const std::byte *>(
      builder.GetBufferPointer());
  return {begin, begin + builder.GetSize()};
}

[[nodiscard]] const fb::GeometryPayload *
verify_geometry_payload(std::span<const std::byte> bytes);

[[nodiscard]] std::vector<std::byte> encode_geometry(
    const QdrProjectMeshAssetWriteSnapshot &mesh) {
  QDR_PROFILE_FUNCTION();
  if (mesh.encoded_geometry) {
    (void)verify_geometry_payload(*mesh.encoded_geometry);
    return *mesh.encoded_geometry;
  }
  if (!mesh.geometry) {
    throw std::runtime_error("QDR mesh snapshot has no geometry payload.");
  }
  const MeshGeometryPayload &geometry = *mesh.geometry;
  flatbuffers::FlatBufferBuilder builder;

  std::vector<fb::PolygonVertex> polygon_vertices;
  std::vector<std::uint32_t> face_ids;
  std::vector<std::uint32_t> face_vertex_offsets;
  std::vector<std::uint32_t> face_vertices;
  std::vector<std::uint32_t> face_uv_offsets;
  std::vector<fb::Vec2> face_uvs;
  std::vector<std::uint32_t> face_material_slots;
  std::vector<fb::SurfaceShading> face_shading;
  std::vector<fb::PolygonEdge> hard_normal_edges;
  std::vector<fb::MeshVertex> mesh_vertices;
  std::vector<fb::MeshTextureMappingVertex> mesh_texture_mapping;
  std::vector<std::uint32_t> mesh_indices;
  std::vector<fb::MeshPrimitive> mesh_primitives;
  flatbuffers::Offset<flatbuffers::Vector<std::uint8_t>> authored_payload;

  fb::GeometryKind kind = fb::GeometryKind::CompiledMesh;
  std::uint32_t next_vertex_id = 1u;
  std::uint32_t next_face_id = 1u;
  if (mesh.has_authored_polygon_document &&
      mesh.authored_polygon_document) {
    kind = fb::GeometryKind::AuthoredPolygon;
    quader::modeling::Result<quader::modeling::PolygonDocument> imported =
        quader::modeling::PolygonDocument::from_authored_topology(
            sdk_authored_topology(*mesh.authored_polygon_document));
    if (!imported.ok()) {
      throw std::runtime_error(
          "SDK authored-topology import failed: " +
          imported.error_message());
    }
    quader::modeling::PolygonDocument sdk_document =
        std::move(imported).value();
    quader::modeling::Result<std::vector<std::byte>> encoded =
        quader::modeling::serialize_qdr_authored_topology(sdk_document);
    if (!encoded.ok()) {
      throw std::runtime_error(
          "SDK authored-topology encoding failed: " +
          encoded.error_message());
    }
    std::vector<std::byte> bytes = std::move(encoded).value();
    authored_payload = builder.CreateVector(
        reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size());
  } else {
    mesh_vertices.reserve(geometry.vertices.size());
    mesh_texture_mapping.reserve(geometry.vertices.size());
    for (const MeshVertex &vertex : geometry.vertices) {
      mesh_vertices.emplace_back(to_fb(vertex.position), to_fb(vertex.normal),
                                 to_fb(vertex.uv), vertex.rgba);
      mesh_texture_mapping.emplace_back(
          vertex.uv_carrier[0], vertex.uv_carrier[1], vertex.uv_carrier[2],
          vertex.uv_carrier[3], to_fb(vertex.uv_geometric_normal));
    }
    mesh_indices = geometry.indices;
    const std::vector<MeshPrimitiveSpan> primitives =
        normalized_primitives(mesh);
    mesh_primitives.reserve(primitives.size());
    for (const MeshPrimitiveSpan primitive : primitives) {
      mesh_primitives.emplace_back(primitive.index_offset,
                                   primitive.index_count,
                                   primitive.material_slot);
    }
  }

  const auto root = fb::CreateGeometryPayload(
      builder, kind, next_vertex_id, next_face_id,
      builder.CreateVectorOfStructs(polygon_vertices),
      builder.CreateVector(face_ids), builder.CreateVector(face_vertex_offsets),
      builder.CreateVector(face_vertices), builder.CreateVector(face_uv_offsets),
      builder.CreateVectorOfStructs(face_uvs),
      builder.CreateVector(face_material_slots),
      builder.CreateVector(face_shading),
      builder.CreateVectorOfStructs(hard_normal_edges),
      builder.CreateVectorOfStructs(mesh_vertices),
      builder.CreateVector(mesh_indices),
      builder.CreateVectorOfStructs(mesh_primitives), authored_payload,
      builder.CreateVectorOfStructs(mesh_texture_mapping));
  builder.Finish(root, "QDRG");
  return flatbuffer_bytes(builder);
}

/// Maps logical meshes to deduplicated encoded geometry chunks.
struct EncodedGeometrySet final {
  std::vector<std::uint64_t> chunk_ids_by_mesh;
  std::vector<quader_io::QdrBinaryChunkInput> chunks;
  std::size_t encode_count = 0;
  double encode_ms = 0.0;
  double deduplication_ms = 0.0;
};

/// Identifies one pinned compiled/authored payload pair during QDR encoding.
struct PinnedGeometryIdentity final {
  const MeshGeometryPayload *geometry = nullptr;
  const quader_poly::Document *authored = nullptr;
  const std::vector<std::byte> *encoded = nullptr;

  [[nodiscard]] bool operator==(const PinnedGeometryIdentity &) const =
      default;
};

/// Hashes pinned geometry identity by immutable payload addresses.
struct PinnedGeometryIdentityHash final {
  [[nodiscard]] std::size_t
  operator()(const PinnedGeometryIdentity &identity) const noexcept {
    const std::size_t geometry =
        std::hash<const void *>{}(identity.geometry);
    const std::size_t authored = std::hash<const void *>{}(identity.authored);
    const std::size_t encoded =
        std::hash<const void *>{}(identity.encoded);
    const std::size_t geometry_authored =
        geometry ^ (authored + 0x9e3779b9U + (geometry << 6U) +
                    (geometry >> 2U));
    return geometry_authored ^
           (encoded + 0x9e3779b9U + (geometry_authored << 6U) +
            (geometry_authored >> 2U));
  }
};

[[nodiscard]] EncodedGeometrySet encode_unique_geometry(
    const QdrProjectWriteSnapshot &snapshot) {
  QDR_PROFILE_FUNCTION();
  EncodedGeometrySet result;
  result.chunk_ids_by_mesh.resize(snapshot.meshes.size());
  std::unordered_map<std::uint64_t, std::vector<std::size_t>> candidates;
  std::unordered_map<PinnedGeometryIdentity, std::uint64_t,
                     PinnedGeometryIdentityHash>
      pinned_chunks;

  for (std::size_t mesh_index = 0u; mesh_index < snapshot.meshes.size();
       ++mesh_index) {
    const QdrProjectMeshAssetWriteSnapshot &mesh =
        snapshot.meshes[mesh_index];
    const PinnedGeometryIdentity identity{
        .geometry = mesh.geometry.get(),
        .authored = mesh.has_authored_polygon_document
                        ? mesh.authored_polygon_document.get()
                        : nullptr,
        .encoded = mesh.encoded_geometry.get(),
    };
    if (identity.geometry != nullptr || identity.encoded != nullptr) {
      if (const auto found = pinned_chunks.find(identity);
          found != pinned_chunks.end()) {
        result.chunk_ids_by_mesh[mesh_index] = found->second;
        continue;
      }
    }
    const auto encode_started = std::chrono::steady_clock::now();
    std::vector<std::byte> bytes = encode_geometry(mesh);
    ++result.encode_count;
    result.encode_ms += std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - encode_started)
                            .count();
    const auto deduplication_started = std::chrono::steady_clock::now();
    const std::uint64_t hash = quader_io::qdr_binary_content_hash(bytes);
    std::uint64_t chunk_id = 0u;
    if (const auto found = candidates.find(hash); found != candidates.end()) {
      for (const std::size_t candidate : found->second) {
        if (result.chunks[candidate].bytes == bytes) {
          chunk_id = result.chunks[candidate].id;
          break;
        }
      }
    }
    if (chunk_id == 0u) {
      chunk_id = static_cast<std::uint64_t>(result.chunks.size()) + 1u;
      result.chunks.push_back(quader_io::QdrBinaryChunkInput{
          .type = quader_io::QdrBinaryChunkType::Geometry,
          .schema_version = kGeometryChunkSchemaVersion,
          .flags = quader_io::QdrBinaryChunkFlags::Critical,
          .id = chunk_id,
          .content_hash = hash,
          .bytes = std::move(bytes),
          .allow_compression = true,
      });
      candidates[hash].push_back(result.chunks.size() - 1u);
    }
    result.chunk_ids_by_mesh[mesh_index] = chunk_id;
    if (identity.geometry != nullptr || identity.encoded != nullptr) {
      pinned_chunks.emplace(identity, chunk_id);
    }
    result.deduplication_ms +=
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - deduplication_started)
            .count();
  }
  return result;
}

[[nodiscard]] flatbuffers::Offset<fb::MaterialComponent>
encode_material_component(flatbuffers::FlatBufferBuilder &builder,
                          const MaterialComponent &material) {
  std::vector<std::pair<std::uint32_t, AssetId>> sorted_slots;
  std::vector<std::pair<std::uint32_t, ProjectAssetId>>
      sorted_project_slots;
  sorted_slots.reserve(material.material_slots.size());
  sorted_project_slots.reserve(material.material_slots.size());
  for (const auto &[slot, reference] : material.material_slots) {
    if (reference.embedded()) {
      sorted_slots.emplace_back(slot, reference.embedded_asset());
    } else if (reference.project_owned()) {
      sorted_project_slots.emplace_back(slot, reference.project_asset());
    }
  }
  std::sort(sorted_slots.begin(), sorted_slots.end());
  std::sort(sorted_project_slots.begin(), sorted_project_slots.end());
  std::vector<fb::MaterialSlotBinding> slots;
  slots.reserve(sorted_slots.size());
  for (const auto [slot, asset] : sorted_slots) {
    slots.emplace_back(slot, asset);
  }
  std::vector<flatbuffers::Offset<fb::ProjectMaterialSlotBinding>>
      project_slots;
  project_slots.reserve(sorted_project_slots.size());
  for (const auto &[slot, asset] : sorted_project_slots) {
    project_slots.push_back(fb::CreateProjectMaterialSlotBinding(
        builder, slot, builder.CreateString(asset.str())));
  }
  return fb::CreateMaterialComponent(
      builder, material.material.embedded_asset(),
      builder.CreateVectorOfStructs(slots),
      material.material.project_owned()
          ? builder.CreateString(material.material.project_asset().str())
          : flatbuffers::Offset<flatbuffers::String>{},
      builder.CreateVector(project_slots));
}

[[nodiscard]] flatbuffers::Offset<fb::SceneSettingsComponent>
encode_scene_settings(flatbuffers::FlatBufferBuilder &builder,
                      const SceneSettingsComponent &settings) {
  const fb::Vec3 clear_color = to_fb(settings.environment.clear_color);
  const auto environment = fb::CreateSceneEnvironmentSettings(
      builder, builder.CreateString(settings.environment.environment_id),
      settings.environment.enabled, settings.environment.show_background,
      &clear_color, settings.environment.intensity,
      settings.environment.background_intensity,
      settings.environment.rotation_y);
  const auto camera = fb::CreateSceneCameraSettings(
      builder, settings.camera.aperture, settings.camera.shutter_speed,
      settings.camera.sensitivity, settings.camera.near_plane,
      settings.camera.far_plane);
  const auto post_process = fb::CreateScenePostProcessSettings(
      builder, settings.post_process.enabled,
      settings.post_process.anti_aliasing,
      settings.post_process.ambient_occlusion,
      settings.post_process.ambient_occlusion_intensity,
      settings.post_process.ambient_occlusion_radius,
      settings.post_process.bloom, settings.post_process.bloom_strength,
      settings.post_process.dithering,
      to_fb(settings.post_process.tone_mapping));
  return fb::CreateSceneSettingsComponent(builder, environment, camera,
                                          post_process);
}

[[nodiscard]] std::vector<std::byte> encode_scene_metadata(
    const QdrProjectWriteSnapshot &snapshot,
    const std::vector<std::uint64_t> &geometry_chunk_ids) {
  QDR_PROFILE_FUNCTION();
  flatbuffers::FlatBufferBuilder builder;

  std::vector<flatbuffers::Offset<fb::MeshAsset>> meshes;
  meshes.reserve(snapshot.meshes.size());
  for (std::size_t index = 0u; index < snapshot.meshes.size(); ++index) {
    const QdrProjectMeshAssetWriteSnapshot &mesh = snapshot.meshes[index];
    meshes.push_back(fb::CreateMeshAsset(
        builder, mesh.id, builder.CreateString(mesh.name),
        mesh.has_stable_cpu_topology,
        mesh.has_authored_polygon_document &&
            (static_cast<bool>(mesh.authored_polygon_document) ||
             static_cast<bool>(mesh.encoded_geometry)),
        geometry_chunk_ids.at(index)));
  }

  std::vector<flatbuffers::Offset<fb::MaterialAsset>> materials;
  materials.reserve(snapshot.materials.size());
  for (const QdrProjectMaterialAssetWriteSnapshot &snapshot_material :
       snapshot.materials) {
    const MaterialDocument &material = snapshot_material.material;
    std::vector<flatbuffers::Offset<fb::TextureSlotBinding>> slots;
    slots.reserve(material.texture_slots.size());
    for (const MaterialTextureSlotDescriptor &descriptor :
         material_texture_slot_descriptors()) {
      const std::optional<AssetId> texture =
          material_texture_slot_asset(material, descriptor.id);
      if (!texture.has_value()) {
        continue;
      }
      slots.push_back(fb::CreateTextureSlotBinding(
          builder, builder.CreateString(descriptor.id), *texture));
    }
    const fb::Vec3 base_color = to_fb(material.base_color);
    materials.push_back(fb::CreateMaterialAsset(
        builder, snapshot_material.id, builder.CreateString(material.name),
        &base_color, material.roughness, material.metallic,
        builder.CreateVector(slots), material.node_graph_reserved));
  }

  std::vector<flatbuffers::Offset<fb::TextureAsset>> textures;
  textures.reserve(snapshot.textures.size());
  for (const QdrProjectTextureAssetWriteSnapshot &snapshot_texture :
       snapshot.textures) {
    const TextureAsset &texture = snapshot_texture.texture;
    textures.push_back(fb::CreateTextureAsset(
        builder, snapshot_texture.id, builder.CreateString(texture.name),
        to_fb(texture.usage), to_fb(texture.color_space), texture.width,
        texture.height,
        builder.CreateString(texture.source_path.generic_string()),
        texture.generated));
  }

  std::vector<flatbuffers::Offset<fb::Entity>> entities;
  entities.reserve(snapshot.entities.size());
  for (const QdrProjectEntityWriteSnapshot &entity : snapshot.entities) {
    flatbuffers::Offset<fb::TransformComponent> transform;
    if (entity.transform.has_value()) {
      const fb::Vec3 position = to_fb(entity.transform->position);
      const fb::Vec3 rotation = to_fb(entity.transform->rotation_euler);
      const fb::Vec3 scale = to_fb(entity.transform->scale);
      transform = fb::CreateTransformComponent(
          builder, &position, &rotation, &scale,
          entity.transform->custom_pivot);
    }
    flatbuffers::Offset<fb::VisibilityComponent> visibility;
    if (entity.visibility.has_value()) {
      visibility = fb::CreateVisibilityComponent(
          builder, entity.visibility->visible, entity.visibility->selectable);
    }
    flatbuffers::Offset<fb::LockComponent> lock;
    if (entity.lock.has_value()) {
      lock = fb::CreateLockComponent(builder, entity.lock->locked);
    }
    flatbuffers::Offset<fb::ModelingObjectComponent> modeling_object;
    if (entity.modeling_object.has_value()) {
      modeling_object = fb::CreateModelingObjectComponent(
          builder, entity.modeling_object->object_id,
          entity.modeling_object->material_slot,
          entity.modeling_object->sdk_owned,
          entity.modeling_object->local_frame_version);
    }
    flatbuffers::Offset<fb::MeshComponent> mesh;
    if (entity.mesh.has_value()) {
      mesh = fb::CreateMeshComponent(
          builder, entity.mesh->mesh, entity.mesh->editor_only,
          entity.mesh->cast_shadows, entity.mesh->double_sided,
          entity.mesh->generate_collisions,
          to_fb(entity.mesh->collision_type),
          entity.mesh->display_collision, to_fb(entity.mesh->body_type));
    }
    flatbuffers::Offset<fb::MaterialComponent> material;
    if (entity.material.has_value()) {
      material = encode_material_component(builder, *entity.material);
    }
    flatbuffers::Offset<fb::LightComponent> light;
    if (entity.light.has_value()) {
      const fb::Vec3 color = to_fb(entity.light->color);
      light = fb::CreateLightComponent(
          builder, to_fb(entity.light->type), &color, entity.light->intensity,
          entity.light->range, entity.light->casts_shadows);
    }
    flatbuffers::Offset<fb::SceneSettingsComponent> settings;
    if (entity.scene_settings.has_value()) {
      settings = encode_scene_settings(builder, *entity.scene_settings);
    }
    flatbuffers::Offset<fb::InstanceDefinitionComponent>
        instance_definition;
    if (entity.instance_definition.has_value()) {
      instance_definition = fb::CreateInstanceDefinitionComponent(builder);
    }
    flatbuffers::Offset<fb::InstanceOccurrenceComponent>
        instance_occurrence;
    if (entity.instance_occurrence.has_value()) {
      instance_occurrence = fb::CreateInstanceOccurrenceComponent(
          builder, entity.instance_occurrence->definition);
    }
    flatbuffers::Offset<fb::TextureMappingComponent> texture_mapping;
    if (entity.texture_mapping.has_value()) {
      if ((entity.kind != EntityKind::Mesh &&
           entity.kind != EntityKind::Instance) ||
          !texture_mapping_is_valid(*entity.texture_mapping)) {
        throw std::runtime_error("QDR entity texture mapping is invalid.");
      }
      const auto &values = entity.texture_mapping->sampling_from_local.values;
      texture_mapping = fb::CreateTextureMappingComponent(
          builder, builder.CreateVector(values.data(), values.size()));
    }
    entities.push_back(fb::CreateEntity(
        builder, entity.id, builder.CreateString(entity.uuid),
        entity.editor_object_id, builder.CreateString(entity.name),
        to_fb(entity.kind), entity.parent, transform, visibility, lock,
        modeling_object, mesh, material, light, entity.scene_root, settings,
        instance_definition, instance_occurrence, entity.layer_id,
        texture_mapping));
  }

  std::vector<flatbuffers::Offset<fb::ObjectRecord>> objects;
  objects.reserve(snapshot.object_records.size());
  for (const ProjectObjectRecord &record : snapshot.object_records) {
    objects.push_back(fb::CreateObjectRecord(
        builder, record.id, record.scene_entity, record.mesh,
        record.material.embedded_asset(),
        record.modeling_object, builder.CreateString(record.authored_name),
        record.visible, record.material_slot,
        record.material.project_owned()
            ? builder.CreateString(record.material.project_asset().str())
            : flatbuffers::Offset<flatbuffers::String>{}));
  }

  std::vector<ProjectAssetReference> sorted_dependencies =
      snapshot.project_asset_dependencies;
  std::sort(sorted_dependencies.begin(), sorted_dependencies.end(),
            [](const ProjectAssetReference &left,
               const ProjectAssetReference &right) {
              return std::tuple(left.stable_id.str(), left.kind,
                                left.path.portable_string()) <
                     std::tuple(right.stable_id.str(), right.kind,
                                right.path.portable_string());
            });
  std::set<std::string> dependency_ids;
  std::vector<flatbuffers::Offset<fb::ProjectAssetDependency>> dependencies;
  dependencies.reserve(sorted_dependencies.size());
  for (const ProjectAssetReference &dependency : sorted_dependencies) {
    if (!dependency.stable_id.valid() || !dependency.path.valid() ||
        dependency.kind == ProjectAssetKind::Unknown ||
        !dependency_ids.insert(std::string(dependency.stable_id.str())).second) {
      throw std::runtime_error(
          "QDR project asset dependency is incomplete or duplicated.");
    }
    dependencies.push_back(fb::CreateProjectAssetDependency(
        builder, builder.CreateString(dependency.stable_id.str()),
        builder.CreateString(dependency.path.portable_string()),
        to_fb(dependency.kind)));
  }

  std::vector<QdrProjectSceneLayerWriteSnapshot> sorted_layers =
      snapshot.layers;
  std::sort(sorted_layers.begin(), sorted_layers.end(),
            [](const QdrProjectSceneLayerWriteSnapshot &left,
               const QdrProjectSceneLayerWriteSnapshot &right) {
              return left.id < right.id;
            });
  std::vector<flatbuffers::Offset<fb::SceneLayer>> layers;
  layers.reserve(sorted_layers.size());
  for (const QdrProjectSceneLayerWriteSnapshot &layer : sorted_layers) {
    const fb::Vec3 color = to_fb(layer.wireframe_color);
    layers.push_back(fb::CreateSceneLayer(
        builder, layer.id, builder.CreateString(layer.name), layer.visible,
        &color, layer.locked));
  }

  flatbuffers::Offset<fb::ViewportCameraPose> viewport_camera_pose;
  if (snapshot.viewport_camera_pose.has_value()) {
    if (!qdr_scene_viewport_camera_pose_is_valid(
            *snapshot.viewport_camera_pose)) {
      throw std::runtime_error("QDR viewport camera pose is invalid.");
    }
    const fb::Vec3 eye = to_fb(snapshot.viewport_camera_pose->eye);
    const fb::Vec3 target = to_fb(snapshot.viewport_camera_pose->target);
    const fb::Vec3 up = to_fb(snapshot.viewport_camera_pose->up);
    viewport_camera_pose =
        fb::CreateViewportCameraPose(builder, &eye, &target, &up);
  }

  const auto root = fb::CreateScenePayload(
      builder, snapshot.project_id, builder.CreateString(snapshot.project_name),
      builder.CreateVector(meshes), builder.CreateVector(materials),
      builder.CreateVector(textures), builder.CreateVector(entities),
      builder.CreateVector(objects), builder.CreateVector(dependencies),
      builder.CreateVector(layers), viewport_camera_pose);
  fb::FinishScenePayloadBuffer(builder, root);
  return flatbuffer_bytes(builder);
}

[[nodiscard]] const quader_io::QdrBinaryChunk &
require_scene_metadata_chunk(
    const quader_io::QdrBinaryContainerReadResult &container) {
  const quader_io::QdrBinaryChunk *metadata = nullptr;
  for (const quader_io::QdrBinaryChunk &chunk : container.chunks) {
    const auto type = static_cast<quader_io::QdrBinaryChunkType>(chunk.type);
    if (type == quader_io::QdrBinaryChunkType::SceneMetadata) {
      if (metadata != nullptr || chunk.id != 0u) {
        throw std::runtime_error(
            "QDR scene contains multiple or invalid metadata chunks.");
      }
      metadata = &chunk;
      continue;
    }
    if (type != quader_io::QdrBinaryChunkType::Geometry &&
        quader_io::qdr_binary_has_flag(
            chunk.flags, quader_io::QdrBinaryChunkFlags::Critical)) {
      throw std::runtime_error(
          "QDR scene contains an unknown critical chunk type.");
    }
  }
  if (metadata == nullptr) {
    throw std::runtime_error("QDR scene is missing its metadata chunk.");
  }
  if (metadata->schema_version < kMinimumSceneChunkSchemaVersion ||
      metadata->schema_version > kSceneChunkSchemaVersion) {
    throw std::runtime_error(
        "QDR scene metadata requires an unsupported schema version.");
  }
  if (metadata->codec != quader_io::QdrBinaryChunkCodec::None) {
    throw std::runtime_error(
        "QDR scene metadata chunk must remain uncompressed.");
  }
  return *metadata;
}

[[nodiscard]] const fb::ScenePayload *verify_scene_payload(
    std::span<const std::byte> bytes) {
  flatbuffers::Verifier verifier(
      reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size());
  if (!fb::VerifyScenePayloadBuffer(verifier)) {
    throw std::runtime_error("QDR scene metadata FlatBuffer is invalid.");
  }
  return fb::GetScenePayload(bytes.data());
}

[[nodiscard]] const fb::GeometryPayload *verify_geometry_payload(
    std::span<const std::byte> bytes) {
  flatbuffers::Verifier verifier(
      reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size());
  if (!verifier.VerifyBuffer<fb::GeometryPayload>("QDRG")) {
    throw std::runtime_error("QDR geometry FlatBuffer is invalid.");
  }
  return flatbuffers::GetRoot<fb::GeometryPayload>(bytes.data());
}

void require_csr_offsets(
    const flatbuffers::Vector<std::uint32_t> *offsets,
    std::size_t record_count, std::size_t element_count,
    std::string_view label) {
  if (offsets == nullptr || offsets->size() != record_count + 1u ||
      offsets->Get(0u) != 0u || offsets->Get(record_count) != element_count) {
    throw std::runtime_error("QDR polygon " + std::string(label) +
                             " CSR offsets are invalid.");
  }
  for (std::size_t index = 1u; index < offsets->size(); ++index) {
    if (offsets->Get(index) < offsets->Get(index - 1u)) {
      throw std::runtime_error("QDR polygon " + std::string(label) +
                               " CSR offsets are not monotonic.");
    }
  }
}

[[nodiscard]] quader_poly::Document
decode_polygon_document(const fb::GeometryPayload &payload) {
  const auto *vertices = payload.polygon_vertices();
  const auto *face_ids = payload.face_ids();
  const auto *vertex_offsets = payload.face_vertex_offsets();
  const auto *face_vertices = payload.face_vertices();
  const auto *uv_offsets = payload.face_uv_offsets();
  const auto *face_uvs = payload.face_uvs();
  const auto *material_slots = payload.face_material_slots();
  const auto *shading = payload.face_shading();
  const auto *hard_edges = payload.hard_normal_edges();

  const std::size_t face_count = face_ids ? face_ids->size() : 0u;
  const std::size_t face_vertex_count =
      face_vertices ? face_vertices->size() : 0u;
  const std::size_t face_uv_count = face_uvs ? face_uvs->size() : 0u;
  require_csr_offsets(vertex_offsets, face_count, face_vertex_count,
                      "vertex");
  require_csr_offsets(uv_offsets, face_count, face_uv_count, "UV");
  if (material_slots == nullptr || material_slots->size() != face_count ||
      shading == nullptr || shading->size() != face_count) {
    throw std::runtime_error(
        "QDR polygon face attribute arrays have inconsistent lengths.");
  }

  quader_poly::Document document;
  document.next_vertex_id = payload.next_vertex_id();
  document.next_face_id = payload.next_face_id();
  if (vertices != nullptr) {
    document.vertices.reserve(vertices->size());
    for (std::size_t index = 0u; index < vertices->size(); ++index) {
      const fb::PolygonVertex *vertex = vertices->Get(index);
      document.vertices.push_back({
          .id = vertex->id(),
          .position = from_fb(vertex->position()),
      });
    }
  }

  document.faces.reserve(face_count);
  for (std::size_t face_index = 0u; face_index < face_count; ++face_index) {
    const std::uint32_t vertex_begin = vertex_offsets->Get(face_index);
    const std::uint32_t vertex_end = vertex_offsets->Get(face_index + 1u);
    const std::uint32_t uv_begin = uv_offsets->Get(face_index);
    const std::uint32_t uv_end = uv_offsets->Get(face_index + 1u);
    if (uv_begin != uv_end && uv_end - uv_begin != vertex_end - vertex_begin) {
      throw std::runtime_error(
          "QDR polygon face UV count does not match its vertex count.");
    }
    quader_poly::Face face{
        .id = face_ids->Get(face_index),
        .material_slot = material_slots->Get(face_index),
        .normal_shading =
            surface_shading_from_fb(shading->Get(face_index)),
    };
    face.vertices.reserve(vertex_end - vertex_begin);
    for (std::uint32_t index = vertex_begin; index < vertex_end; ++index) {
      face.vertices.push_back(face_vertices->Get(index));
    }
    face.uvs.reserve(uv_end - uv_begin);
    for (std::uint32_t index = uv_begin; index < uv_end; ++index) {
      face.uvs.push_back(from_fb(face_uvs->Get(index)));
    }
    document.faces.push_back(std::move(face));
  }
  if (hard_edges != nullptr) {
    document.hard_normal_edges.reserve(hard_edges->size());
    for (std::size_t index = 0u; index < hard_edges->size(); ++index) {
      const fb::PolygonEdge *edge = hard_edges->Get(index);
      document.hard_normal_edges.push_back({edge->a(), edge->b()});
    }
  }

  const quader_poly::PolygonDocumentValidationReport validation =
      quader_poly::validate_polygon_document(document);
  if (!validation.ok()) {
    const std::string message = validation.diagnostics.empty()
                                    ? "Polygon validation failed."
                                    : validation.diagnostics.front().message;
    throw std::runtime_error("QDR polygon geometry is invalid: " + message);
  }

  quader_poly::ElementId max_vertex_id = 0u;
  for (const quader_poly::Vertex &vertex : document.vertices) {
    max_vertex_id = std::max(max_vertex_id, vertex.id);
  }
  quader_poly::ElementId max_face_id = 0u;
  for (const quader_poly::Face &face : document.faces) {
    max_face_id = std::max(max_face_id, face.id);
  }
  if (max_vertex_id == std::numeric_limits<quader_poly::ElementId>::max() ||
      max_face_id == std::numeric_limits<quader_poly::ElementId>::max()) {
    throw std::runtime_error("QDR polygon ids cannot be incremented safely.");
  }
  document.next_vertex_id =
      std::max(document.next_vertex_id, max_vertex_id + 1u);
  document.next_face_id = std::max(document.next_face_id, max_face_id + 1u);
  return document;
}

[[nodiscard]] MeshAsset decode_compiled_mesh(
    const fb::GeometryPayload &payload) {
  MeshAsset mesh;
  MeshGeometryPayload &geometry = mesh.edit_geometry();
  if (const auto *vertices = payload.mesh_vertices()) {
    geometry.vertices.reserve(vertices->size());
    for (std::size_t index = 0u; index < vertices->size(); ++index) {
      const fb::MeshVertex *vertex = vertices->Get(index);
      geometry.vertices.push_back({
          .position = from_fb(vertex->position()),
          .normal = from_fb(vertex->normal(), {0.0f, 1.0f, 0.0f}),
          .uv = from_fb(vertex->uv()),
          .rgba = vertex->rgba(),
      });
    }
  }
  if (const auto *indices = payload.mesh_indices()) {
    geometry.indices.assign(indices->begin(), indices->end());
  }
  if (const auto *mappings = payload.mesh_texture_mapping();
      mappings != nullptr && mappings->size() != 0u) {
    if (mappings->size() != geometry.vertices.size()) {
      throw std::runtime_error(
          "QDR compiled geometry has an invalid texture mapping count.");
    }
    for (std::size_t index = 0u; index < mappings->size(); ++index) {
      const fb::MeshTextureMappingVertex *mapping = mappings->Get(index);
      MeshVertex &vertex = geometry.vertices[index];
      vertex.uv_carrier = {mapping->carrier_00(), mapping->carrier_01(),
                           mapping->carrier_10(), mapping->carrier_11()};
      vertex.uv_geometric_normal = from_fb(mapping->geometric_normal());
      if (std::ranges::any_of(vertex.uv_carrier, [](float value) {
            return !std::isfinite(value);
          }) || !std::isfinite(vertex.uv_geometric_normal.x) ||
          !std::isfinite(vertex.uv_geometric_normal.y) ||
          !std::isfinite(vertex.uv_geometric_normal.z)) {
        throw std::runtime_error(
            "QDR compiled geometry texture mapping must be finite.");
      }
    }
  }
  for (const std::uint32_t index : geometry.indices) {
    if (index >= geometry.vertices.size()) {
      throw std::runtime_error(
          "QDR compiled geometry index references a missing vertex.");
    }
  }
  if (const auto *primitives = payload.mesh_primitives()) {
    geometry.primitives.reserve(primitives->size());
    for (std::size_t index = 0u; index < primitives->size(); ++index) {
      const fb::MeshPrimitive *primitive = primitives->Get(index);
      const std::uint64_t end =
          static_cast<std::uint64_t>(primitive->index_offset()) +
          primitive->index_count();
      if (primitive->index_count() == 0u ||
          primitive->index_offset() >= geometry.indices.size() ||
          end > geometry.indices.size()) {
        throw std::runtime_error(
            "QDR compiled geometry contains an invalid primitive span.");
      }
      geometry.primitives.push_back({
          .index_offset = primitive->index_offset(),
          .index_count = primitive->index_count(),
          .material_slot = primitive->material_slot(),
      });
    }
  }
  if (geometry.primitives.empty() && !geometry.indices.empty()) {
    geometry.primitives.push_back({
        .index_offset = 0u,
        .index_count = static_cast<std::uint32_t>(geometry.indices.size()),
        .material_slot = 0u,
    });
  }
  if (payload.mesh_texture_mapping() == nullptr ||
      payload.mesh_texture_mapping()->size() == 0u) {
    if (!MeshProjectionService::initialize_compiled_texture_mapping(mesh)) {
      throw std::runtime_error(
          "QDR compiled geometry cannot provide valid texture mapping triangles.");
    }
  }
  return mesh;
}

[[nodiscard]] MeshAsset
decode_geometry(const fb::GeometryPayload &payload,
                QdrSceneBinaryDecodeStats &stats) {
  QDR_PROFILE_FUNCTION();
  switch (payload.kind()) {
  case fb::GeometryKind::CompiledMesh: {
    const auto decode_started = std::chrono::steady_clock::now();
    MeshAsset mesh = decode_compiled_mesh(payload);
    stats.geometry_decode_ms += std::chrono::duration<double, std::milli>(
                                    std::chrono::steady_clock::now() -
                                    decode_started)
                                    .count();
    return mesh;
  }
  case fb::GeometryKind::AuthoredPolygon: {
    const auto decode_started = std::chrono::steady_clock::now();
    const flatbuffers::Vector<std::uint8_t> *authored =
        payload.authored_payload();
    if (authored == nullptr || authored->size() == 0u) {
      throw std::runtime_error(
          "QDR authored geometry is missing its SDK topology payload.");
    }
    const auto *begin = reinterpret_cast<const std::byte *>(authored->data());
    quader::modeling::Result<quader::modeling::PolygonDocument> decoded =
        quader::modeling::deserialize_qdr_authored_topology(
            std::span<const std::byte>(begin, authored->size()));
    if (!decoded.ok()) {
      throw std::runtime_error(
          "SDK authored-topology decoding failed: " +
          decoded.error_message());
    }
    quader::modeling::PolygonDocument sdk_document =
        std::move(decoded).value();
    quader::modeling::Result<quader::modeling::AuthoredTopology> exported =
        sdk_document.authored_topology();
    if (!exported.ok()) {
      throw std::runtime_error(
          "SDK authored-topology export failed: " +
          exported.error_message());
    }
    quader_poly::Document document =
        native_authored_topology(exported.value());
    stats.geometry_decode_ms += std::chrono::duration<double, std::milli>(
                                    std::chrono::steady_clock::now() -
                                    decode_started)
                                    .count();
    const auto compile_started = std::chrono::steady_clock::now();
    MeshAsset mesh =
        MeshProjectionService::compile_polygon_document_mesh(document);
    stats.geometry_compile_ms += std::chrono::duration<double, std::milli>(
                                     std::chrono::steady_clock::now() -
                                     compile_started)
                                     .count();
    mesh.has_authored_polygon_document = true;
    mesh.authored_polygon_document =
        std::make_shared<quader_poly::Document>(std::move(document));
    return mesh;
  }
  default:
    throw std::runtime_error("QDR geometry contains an unknown payload kind.");
  }
}

[[nodiscard]] const quader_io::QdrBinaryChunk &require_geometry_chunk(
    const std::unordered_map<std::uint64_t,
                             const quader_io::QdrBinaryChunk *> &chunks,
    std::uint64_t id) {
  const auto found = chunks.find(id);
  if (id == 0u || found == chunks.end()) {
    throw std::runtime_error(
        "QDR mesh asset references a missing geometry chunk.");
  }
  return *found->second;
}

/**
 * Records one logical mesh asset decoded from scene metadata before its
 * referenced geometry chunk is materialized.
 */
struct StoredMeshDescriptor final {
  AssetId id = kInvalidAssetId;
  std::string name;
  bool has_stable_cpu_topology = true;
  bool has_authored_polygon_document = false;
  std::uint64_t geometry_id = 0u;
};

/**
 * Owns one immutable geometry chunk and compiles it at most once when multiple
 * logical mesh assets share an otherwise-unreferenced topology payload.
 */
class DeferredGeometryDecoder final {
public:
  explicit DeferredGeometryDecoder(quader_io::QdrBinaryChunk chunk)
      : chunk_(std::move(chunk)) {}

  [[nodiscard]] MeshAsset materialize() {
    std::scoped_lock lock(mutex_);
    if (failure_) {
      std::rethrow_exception(failure_);
    }
    if (!decoded_.has_value()) {
      try {
        QdrSceneBinaryDecodeStats deferred_stats;
        const fb::GeometryPayload *payload =
            verify_geometry_payload(chunk_.bytes());
        decoded_.emplace(decode_geometry(*payload, deferred_stats));
      } catch (...) {
        failure_ = std::current_exception();
        throw;
      }
    }
    return *decoded_;
  }

  [[nodiscard]] std::shared_ptr<const std::vector<std::byte>>
  encoded_geometry() {
    std::scoped_lock lock(mutex_);
    if (!encoded_) {
      const std::span<const std::byte> bytes = chunk_.bytes();
      (void)verify_geometry_payload(bytes);
      encoded_ = std::make_shared<const std::vector<std::byte>>(
          bytes.begin(), bytes.end());
    }
    return encoded_;
  }

private:
  quader_io::QdrBinaryChunk chunk_;
  std::mutex mutex_;
  std::optional<MeshAsset> decoded_;
  std::shared_ptr<const std::vector<std::byte>> encoded_;
  std::exception_ptr failure_;
};

[[nodiscard]] std::set<AssetId>
referenced_mesh_asset_ids(const fb::ScenePayload &payload) {
  std::set<AssetId> references;
  if (const auto *entities = payload.entities()) {
    for (std::size_t index = 0u; index < entities->size(); ++index) {
      const fb::Entity *entity = entities->Get(index);
      if (const fb::MeshComponent *mesh = entity->mesh();
          mesh != nullptr && mesh->mesh() != kInvalidAssetId) {
        references.insert(mesh->mesh());
      }
    }
  }
  if (const auto *objects = payload.objects()) {
    for (std::size_t index = 0u; index < objects->size(); ++index) {
      const AssetId mesh = objects->Get(index)->mesh();
      if (mesh != kInvalidAssetId) {
        references.insert(mesh);
      }
    }
  }
  return references;
}

[[nodiscard]] MaterialComponent
decode_material_component(const fb::MaterialComponent &value) {
  MaterialComponent material{.material = MaterialReference(value.material())};
  if (const flatbuffers::String *project_id = value.project_material_id();
      project_id != nullptr && !project_id->str().empty()) {
    const std::optional<ProjectAssetId> parsed =
        ProjectAssetId::parse(project_id->string_view());
    if (!parsed.has_value() || material.material.valid()) {
      throw std::runtime_error(
          "QDR entity material contains an invalid or ambiguous primary "
          "material reference.");
    }
    material.material = MaterialReference::project(*parsed);
  }
  if (const auto *slots = value.material_slots()) {
    for (std::size_t index = 0u; index < slots->size(); ++index) {
      const fb::MaterialSlotBinding *slot = slots->Get(index);
      if (!material.material_slots
               .emplace(slot->slot(), slot->material_id())
               .second) {
        throw std::runtime_error(
            "QDR entity material contains duplicate slot bindings.");
      }
    }
  }
  if (const auto *slots = value.project_material_slots()) {
    for (std::size_t index = 0u; index < slots->size(); ++index) {
      const fb::ProjectMaterialSlotBinding *slot = slots->Get(index);
      const std::optional<ProjectAssetId> parsed = ProjectAssetId::parse(
          string_from_fb(slot->project_material_id()));
      if (!parsed.has_value() ||
          !material.material_slots
               .emplace(slot->slot(), MaterialReference::project(*parsed))
               .second) {
        throw std::runtime_error(
            "QDR entity material contains an invalid, ambiguous, or duplicate "
            "project slot binding.");
      }
    }
  }
  return material;
}

[[nodiscard]] SceneSettingsComponent
decode_scene_settings(const fb::SceneSettingsComponent &value) {
  const fb::SceneEnvironmentSettings *environment = value.environment();
  const fb::SceneCameraSettings *camera = value.camera();
  const fb::ScenePostProcessSettings *post_process = value.post_process();
  if (!environment || !camera || !post_process) {
    throw std::runtime_error(
        "QDR scene settings payload is missing required fields.");
  }
  SceneSettingsComponent settings;
  settings.environment.environment_id = string_from_fb(
      environment->environment_id(), settings.environment.environment_id);
  settings.environment.enabled = environment->enabled();
  settings.environment.show_background = environment->show_background();
  settings.environment.clear_color = from_fb(
      environment->clear_color(), settings.environment.clear_color);
  settings.environment.intensity = environment->intensity();
  settings.environment.background_intensity =
      environment->background_intensity();
  settings.environment.rotation_y = environment->rotation_y();
  settings.camera.aperture = camera->aperture();
  settings.camera.shutter_speed = camera->shutter_speed();
  settings.camera.sensitivity = camera->sensitivity();
  settings.camera.near_plane = camera->near_plane();
  settings.camera.far_plane = camera->far_plane();
  settings.post_process.enabled = post_process->enabled();
  settings.post_process.anti_aliasing = post_process->anti_aliasing();
  settings.post_process.ambient_occlusion =
      post_process->ambient_occlusion();
  settings.post_process.ambient_occlusion_intensity =
      post_process->ambient_occlusion_intensity();
  settings.post_process.ambient_occlusion_radius =
      post_process->ambient_occlusion_radius();
  settings.post_process.bloom = post_process->bloom();
  settings.post_process.bloom_strength = post_process->bloom_strength();
  settings.post_process.dithering = post_process->dithering();
  settings.post_process.tone_mapping =
      tone_mapping_from_fb(post_process->tone_mapping());
  return settings;
}

[[nodiscard]] std::vector<ProjectAssetReference>
decode_project_asset_dependencies(const fb::ScenePayload &payload) {
  std::vector<ProjectAssetReference> dependencies;
  std::set<std::string> stable_ids;
  const auto *stored_dependencies = payload.project_asset_dependencies();
  if (stored_dependencies == nullptr) {
    return dependencies;
  }
  dependencies.reserve(stored_dependencies->size());
  for (std::size_t index = 0u; index < stored_dependencies->size(); ++index) {
    const fb::ProjectAssetDependency *stored =
        stored_dependencies->Get(index);
    const std::optional<ProjectAssetId> stable_id =
        ProjectAssetId::parse(string_from_fb(stored->stable_id()));
    const ProjectAssetKind kind = project_asset_kind_from_fb(stored->kind());
    const ProjectIoResult<ProjectRelativePath> path =
        ProjectRelativePath::parse_portable(
            string_from_fb(stored->project_relative_path()));
    if (!stable_id.has_value() || kind == ProjectAssetKind::Unknown ||
        !path.ok() ||
        !stable_ids.insert(std::string(stable_id->str())).second) {
      throw std::runtime_error(
          "QDR project asset dependency is invalid or duplicated.");
    }
    dependencies.push_back({
        .stable_id = *stable_id,
        .kind = kind,
        .path = path.value(),
    });
  }
  return dependencies;
}

void decode_assets(
    SceneContentDocument &document, const fb::ScenePayload &payload,
    const std::unordered_map<std::uint64_t,
                             const quader_io::QdrBinaryChunk *> &geometry_chunks,
    QdrSceneBinaryDecodeStats &stats) {
  const std::set<AssetId> referenced_meshes =
      referenced_mesh_asset_ids(payload);
  std::unordered_map<std::uint64_t, MeshAsset> decoded_geometry;
  std::unordered_map<std::uint64_t, std::shared_ptr<DeferredGeometryDecoder>>
      deferred_geometry;
  std::unordered_map<std::uint64_t, AssetId> installed_geometry_source;
  std::set<AssetId> mesh_ids;
  if (const auto *meshes = payload.meshes()) {
    stats.logical_geometry_count = meshes->size();
    std::vector<StoredMeshDescriptor> descriptors;
    descriptors.reserve(meshes->size());
    for (std::size_t index = 0u; index < meshes->size(); ++index) {
      const fb::MeshAsset *stored_mesh = meshes->Get(index);
      const AssetId id = stored_mesh->id();
      if (id == kInvalidAssetId || !mesh_ids.insert(id).second) {
        throw std::runtime_error(
            "QDR scene contains an invalid or duplicate mesh asset id.");
      }
      const std::uint64_t geometry_id = stored_mesh->geometry_chunk_id();
      (void)require_geometry_chunk(geometry_chunks, geometry_id);
      descriptors.push_back({
          .id = id,
          .name = string_from_fb(stored_mesh->name(), "Mesh"),
          .has_stable_cpu_topology =
              stored_mesh->has_stable_cpu_topology(),
          .has_authored_polygon_document =
              stored_mesh->has_authored_polygon_document(),
          .geometry_id = geometry_id,
      });
    }

    for (const StoredMeshDescriptor &descriptor : descriptors) {
      if (!referenced_meshes.contains(descriptor.id) ||
          decoded_geometry.contains(descriptor.geometry_id)) {
        continue;
      }
      const quader_io::QdrBinaryChunk &chunk =
          require_geometry_chunk(geometry_chunks, descriptor.geometry_id);
      const fb::GeometryPayload *geometry_payload =
          verify_geometry_payload(chunk.bytes());
      decoded_geometry.emplace(
          descriptor.geometry_id, decode_geometry(*geometry_payload, stats));
      ++stats.unique_geometry_decoded_count;
      if (geometry_payload->kind() == fb::GeometryKind::AuthoredPolygon) {
        ++stats.unique_geometry_compiled_count;
      }
    }

    for (const StoredMeshDescriptor &descriptor : descriptors) {
      const std::uint64_t geometry_id = descriptor.geometry_id;
      auto decoded = decoded_geometry.find(geometry_id);
      if (decoded == decoded_geometry.end()) {
        auto deferred = deferred_geometry.find(geometry_id);
        if (deferred == deferred_geometry.end()) {
          const quader_io::QdrBinaryChunk &chunk =
              require_geometry_chunk(geometry_chunks, geometry_id);
          deferred = deferred_geometry
                         .emplace(
                             geometry_id,
                             std::make_shared<DeferredGeometryDecoder>(chunk))
                         .first;
        }
        const std::shared_ptr<DeferredGeometryDecoder> loader =
            deferred->second;
        if (document.assets().add_deferred_mesh(
                descriptor.id, descriptor.name,
                descriptor.has_stable_cpu_topology,
                descriptor.has_authored_polygon_document,
                [loader]() { return loader->materialize(); },
                [loader]() { return loader->encoded_geometry(); }) ==
            kInvalidAssetId) {
          throw std::runtime_error(
              "QDR scene could not install a deferred mesh asset.");
        }
        continue;
      }
      const auto installed = installed_geometry_source.find(geometry_id);
      if (installed != installed_geometry_source.end()) {
        const MeshAsset *source =
            document.assets().mesh(installed->second);
        if (source == nullptr ||
            descriptor.has_authored_polygon_document !=
                source->has_authored_polygon_document ||
            document.assets().add_mesh_payload_alias(
                descriptor.id, installed->second, descriptor.name,
                descriptor.has_stable_cpu_topology) == kInvalidAssetId) {
          throw std::runtime_error(
              "QDR scene could not install a shared mesh asset.");
        }
        continue;
      }
      MeshAsset mesh = decoded->second;
      mesh.name = descriptor.name;
      mesh.has_stable_cpu_topology = descriptor.has_stable_cpu_topology;
      if (descriptor.has_authored_polygon_document !=
          mesh.has_authored_polygon_document) {
        throw std::runtime_error(
            "QDR mesh metadata does not match its geometry kind.");
      }
      if (document.assets().add_mesh(descriptor.id, std::move(mesh)) ==
          kInvalidAssetId) {
        throw std::runtime_error("QDR scene could not install a mesh asset.");
      }
      installed_geometry_source.emplace(geometry_id, descriptor.id);
    }
  }

  std::set<AssetId> texture_ids;
  if (const auto *textures = payload.textures()) {
    for (std::size_t index = 0u; index < textures->size(); ++index) {
      const fb::TextureAsset *stored_texture = textures->Get(index);
      const AssetId id = stored_texture->id();
      if (id == kInvalidAssetId || !texture_ids.insert(id).second) {
        throw std::runtime_error(
            "QDR scene contains an invalid or duplicate texture asset id.");
      }
      TextureAsset texture;
      texture.name = string_from_fb(stored_texture->name(), "Texture");
      texture.usage = texture_usage_from_fb(stored_texture->usage());
      texture.color_space =
          texture_color_space_from_fb(stored_texture->color_space());
      texture.width = stored_texture->width();
      texture.height = stored_texture->height();
      texture.source_path = std::filesystem::path(
          string_from_fb(stored_texture->source_path()));
      texture.generated = stored_texture->generated();
      if (document.assets().add_texture(id, std::move(texture)) ==
          kInvalidAssetId) {
        throw std::runtime_error("QDR scene could not install a texture asset.");
      }
    }
  }

  std::set<AssetId> material_ids;
  if (const auto *materials = payload.materials()) {
    for (std::size_t index = 0u; index < materials->size(); ++index) {
      const fb::MaterialAsset *stored_material = materials->Get(index);
      const AssetId id = stored_material->id();
      if (id == kInvalidAssetId || !material_ids.insert(id).second) {
        throw std::runtime_error(
            "QDR scene contains an invalid or duplicate material asset id.");
      }
      MaterialDocument material;
      material.name = string_from_fb(stored_material->name(), "Material");
      material.base_color = from_fb(stored_material->base_color(),
                                    {1.0f, 1.0f, 1.0f});
      material.roughness = stored_material->roughness();
      material.metallic = stored_material->metallic();
      material.node_graph_reserved = stored_material->node_graph_reserved();
      if (const auto *slots = stored_material->texture_slots()) {
        for (std::size_t slot_index = 0u; slot_index < slots->size();
             ++slot_index) {
          const fb::TextureSlotBinding *slot = slots->Get(slot_index);
          const std::string slot_name = string_from_fb(slot->slot());
          if (!set_material_texture_slot(material, slot_name,
                                         slot->texture_id())) {
            throw std::runtime_error(
                "QDR material contains an unsupported texture slot.");
          }
        }
      }
      const AssetRegistryValidationResult validation =
          document.assets().validate_material(material);
      if (!validation.ok) {
        throw std::runtime_error("QDR material is invalid: " +
                                 validation.message);
      }
      if (document.assets().add_material(id, std::move(material)) ==
          kInvalidAssetId) {
        throw std::runtime_error("QDR scene could not install a material asset.");
      }
    }
  }
}

void decode_scene_layers(SceneContentDocument &document,
                         const fb::ScenePayload &payload,
                         std::uint16_t schema_version) {
  if (schema_version < 4u) {
    return;
  }

  SceneLayerState state;
  std::set<SceneLayerId> ids;
  std::set<std::string> names;
  if (const auto *layers = payload.layers()) {
    state.layers.reserve(layers->size());
    for (std::size_t index = 0u; index < layers->size(); ++index) {
      const fb::SceneLayer *stored = layers->Get(index);
      const SceneLayerId id = stored->id();
      const std::string name = string_from_fb(stored->name());
      const fb::Vec3 *stored_color = stored->wireframe_color();
      const QVec3 color = from_fb(stored_color);
      if (id == kInvalidSceneLayerId || name.empty() ||
          stored_color == nullptr || !std::isfinite(color.x) ||
          !std::isfinite(color.y) || !std::isfinite(color.z) ||
          color.x < 0.0f || color.x > 1.0f || color.y < 0.0f ||
          color.y > 1.0f || color.z < 0.0f || color.z > 1.0f ||
          !ids.insert(id).second || !names.insert(name).second) {
        throw std::runtime_error(
            "QDR scene contains an invalid or duplicate scene layer.");
      }
      state.layers.push_back(SceneLayerRecord{
          .id = id,
          .name = name,
          .visible = stored->visible(),
          .wireframe_color = color,
          .locked = stored->locked(),
      });
    }
  }
  if (!document.scene().replace_layer_state(std::move(state))) {
    throw std::runtime_error("QDR scene layers could not be restored.");
  }
}

[[nodiscard]] std::optional<QdrSceneViewportCameraPose>
decode_viewport_camera_pose(const fb::ScenePayload &payload,
                            std::uint16_t schema_version) {
  if (schema_version < 5u || payload.viewport_camera_pose() == nullptr) {
    return std::nullopt;
  }
  const fb::ViewportCameraPose &stored = *payload.viewport_camera_pose();
  QdrSceneViewportCameraPose pose{
      .eye = qdr_camera_vector_from_fb(stored.eye()),
      .target = qdr_camera_vector_from_fb(stored.target()),
      .up = qdr_camera_vector_from_fb(stored.up()),
  };
  if (stored.eye() == nullptr || stored.target() == nullptr ||
      stored.up() == nullptr ||
      !qdr_scene_viewport_camera_pose_is_valid(pose)) {
    throw std::runtime_error(
        "QDR scene contains an invalid viewport camera pose.");
  }
  return pose;
}

void decode_entities(SceneContentDocument &document,
                     const fb::ScenePayload &payload,
                     std::uint16_t schema_version) {
  std::vector<SceneParentAssignment> parent_links;
  std::vector<EntityId> instance_definitions;
  std::vector<std::pair<EntityId, EntityId>> instance_occurrences;
  std::vector<SceneLayerAssignment> layer_assignments;
  std::vector<SceneTextureMappingAssignment> texture_mappings;
  std::set<EntityId> explicit_object_id_entities;
  if (const auto *entities = payload.entities()) {
    parent_links.reserve(entities->size());
    for (std::size_t index = 0u; index < entities->size(); ++index) {
      const fb::Entity *stored = entities->Get(index);
      const bool has_editor_object_id =
          stored->editor_object_id() != kInvalidEditorObjectId;
      const EntityId entity = document.scene().create_entity_with_id(
          stored->id(), string_from_fb(stored->name(), "Entity"),
          from_fb(stored->kind()), stored->editor_object_id());
      if (entity == kInvalidEntityId) {
        throw std::runtime_error(
            "QDR scene contains a duplicate or invalid entity id.");
      }
      const std::string uuid = string_from_fb(stored->uuid());
      if (!uuid.empty() && !document.scene().set_entity_uuid(entity, uuid)) {
        throw std::runtime_error(
            "QDR scene contains a duplicate or invalid entity UUID.");
      }
      if (has_editor_object_id) {
        explicit_object_id_entities.insert(entity);
      }
      if (schema_version >= 7u && stored->texture_mapping() != nullptr) {
        const auto *values =
            stored->texture_mapping()->sampling_from_local();
        TextureMappingComponent mapping;
        if (values == nullptr || values->size() !=
                                     mapping.sampling_from_local.values.size()) {
          throw std::runtime_error(
              "QDR entity texture mapping must contain one affine matrix.");
        }
        std::copy(values->begin(), values->end(),
                  mapping.sampling_from_local.values.begin());
        texture_mappings.push_back({.entity = entity, .mapping = mapping});
      }
      if (schema_version >= 4u &&
          stored->layer_id() != kInvalidSceneLayerId) {
        layer_assignments.push_back(SceneLayerAssignment{
            .entity = entity,
            .layer_id = stored->layer_id(),
        });
      }
      if (const fb::TransformComponent *value = stored->transform()) {
        document.scene().set_transform(
            entity, TransformComponent{
                        .position = from_fb(value->position()),
                        .rotation_euler = from_fb(value->rotation_euler()),
                        .scale = from_fb(value->scale(), {1.0f, 1.0f, 1.0f}),
                        .custom_pivot = value->custom_pivot(),
                    });
      }
      if (const fb::VisibilityComponent *value = stored->visibility()) {
        document.scene().set_visible(entity, value->visible());
        document.scene().set_selectable(entity, value->selectable());
      }
      if (const fb::LockComponent *value = stored->lock()) {
        document.scene().set_locked(entity, value->locked());
      }
      if (const fb::ModelingObjectComponent *value = stored->modeling_object()) {
        document.scene().set_modeling_object(
            entity, {.object_id = value->object_id(),
                     .material_slot = value->material_slot(),
                     .sdk_owned = value->sdk_owned(),
                     .local_frame_version =
                         schema_version >= 6u
                             ? value->local_frame_version()
                             : kLegacyUnknownModelingObjectLocalFrameVersion});
      }
      if (const fb::MeshComponent *value = stored->mesh()) {
        document.scene().set_mesh(
            entity,
            MeshComponent{
                .mesh = value->mesh(),
                .editor_only = value->editor_only(),
                .cast_shadows = value->cast_shadows(),
                .double_sided = value->double_sided(),
                .generate_collisions = value->generate_collisions(),
                .collision_type = from_fb(value->collision_type()),
                .display_collision = value->display_collision(),
                .body_type = mesh_body_type_from_fb(value->body_type()),
            });
      }
      if (const fb::MaterialComponent *value = stored->material()) {
        MaterialComponent material = decode_material_component(*value);
        if (material.material.valid()) {
          document.scene().set_material(entity, std::move(material));
        }
      }
      if (const fb::LightComponent *value = stored->light()) {
        document.scene().set_light(
            entity,
            LightComponent{
                .type = light_type_from_fb(value->type()),
                .color = from_fb(value->color(), {1.0f, 1.0f, 1.0f}),
                .intensity = value->intensity(),
                .range = value->range(),
                .casts_shadows = value->casts_shadows(),
            });
      }
      if (stored->scene_root() || stored->scene_settings() != nullptr) {
        if (stored->scene_settings() == nullptr ||
            !document.scene().set_scene_settings(
                entity, decode_scene_settings(*stored->scene_settings()))) {
          throw std::runtime_error(
              "QDR scene contains invalid scene-root settings.");
        }
      }
      const bool has_instance_definition =
          stored->instance_definition() != nullptr;
      const fb::InstanceOccurrenceComponent *stored_occurrence =
          stored->instance_occurrence();
      if (has_instance_definition && stored_occurrence != nullptr) {
        throw std::runtime_error(
            "QDR entity cannot be both an instance definition and an "
            "occurrence.");
      }
      if (has_instance_definition) {
        instance_definitions.push_back(entity);
      }
      if (stored_occurrence != nullptr) {
        if (from_fb(stored->kind()) != EntityKind::Instance) {
          throw std::runtime_error(
              "QDR instance occurrence has a non-instance entity kind.");
        }
        instance_occurrences.emplace_back(entity,
                                          stored_occurrence->definition());
      } else if (from_fb(stored->kind()) == EntityKind::Instance) {
        throw std::runtime_error(
            "QDR instance entity is missing its occurrence component.");
      }
      parent_links.push_back(SceneParentAssignment{
          .child = entity,
          .parent = stored->parent(),
      });
    }
  }
  if (!document.scene().replace_hierarchy(parent_links)) {
    throw std::runtime_error(
        "QDR scene contains an invalid entity hierarchy.");
  }
  if (document.ensure_scene_root() == kInvalidEntityId) {
    throw std::runtime_error("QDR scene could not establish its scene root.");
  }

  const std::set<EntityId> definition_ids(instance_definitions.begin(),
                                          instance_definitions.end());
  std::unordered_map<EntityId, bool> beneath_definition;
  beneath_definition.reserve(parent_links.size() + 1u);
  for (const EntityId entity : document.scene().entity_tree_order()) {
    const EntityId parent = document.scene().parent(entity);
    const bool parent_is_or_beneath_definition =
        parent != kInvalidEntityId &&
        (definition_ids.contains(parent) || beneath_definition[parent]);
    if (definition_ids.contains(entity) &&
        parent_is_or_beneath_definition) {
      throw std::runtime_error(
          "QDR instance definitions cannot be nested.");
    }
    beneath_definition[entity] = parent_is_or_beneath_definition;
  }
  for (const EntityId definition : instance_definitions) {
    if (document.scene().children(definition).empty() ||
        !document.scene().set_instance_definition(definition)) {
      throw std::runtime_error(
          "QDR scene contains an invalid or empty instance definition.");
    }
  }
  for (const auto [entity, definition] : instance_occurrences) {
    if (!document.scene().set_instance_occurrence(
            entity, InstanceOccurrenceComponent{.definition = definition})) {
      throw std::runtime_error(
          "QDR scene contains an invalid instance occurrence.");
    }
  }

  if (!texture_mappings.empty() &&
      !document.scene().set_transforms_and_texture_mappings({},
                                                           texture_mappings)) {
    throw std::runtime_error("QDR scene contains an invalid texture mapping.");
  }

  if (schema_version >= 4u && !layer_assignments.empty()) {
    SceneLayerState layer_state = document.scene().layer_state();
    layer_state.assignments = std::move(layer_assignments);
    if (!document.scene().replace_layer_state(std::move(layer_state))) {
      throw std::runtime_error(
          "QDR scene contains dangling or forbidden layer memberships.");
    }
  }

  std::set<EditorObjectId> object_ids;
  std::set<EntityId> object_entities;
  std::vector<ProjectObjectRecord> object_records;
  if (const auto *objects = payload.objects()) {
    object_records.reserve(objects->size());
    for (std::size_t index = 0u; index < objects->size(); ++index) {
      const fb::ObjectRecord *stored = objects->Get(index);
      ProjectObjectRecord record{
          .id = stored->editor_object_id(),
          .scene_entity = stored->scene_entity(),
          .mesh = stored->mesh(),
          .material = MaterialReference(stored->material()),
          .modeling_object = stored->modeling_object(),
          .authored_name = string_from_fb(stored->authored_name()),
          .visible = stored->visible(),
          .material_slot = stored->material_slot(),
      };
      if (const flatbuffers::String *project_id =
              stored->project_material_id();
          project_id != nullptr && !project_id->str().empty()) {
        const std::optional<ProjectAssetId> parsed =
            ProjectAssetId::parse(project_id->string_view());
        if (!parsed.has_value() || record.material.valid()) {
          throw std::runtime_error(
              "QDR object record contains an invalid or ambiguous material "
              "reference.");
        }
        record.material = MaterialReference::project(*parsed);
      }
      const SceneContentDocumentAssetReferenceReport reference_report =
          document.validate_asset_references(record);
      if (!reference_report.ok()) {
        throw std::runtime_error("QDR object record has invalid references: " +
                                 reference_report.issues.front().message);
      }
      if (record.id == kInvalidEditorObjectId ||
          !object_ids.insert(record.id).second ||
          !object_entities.insert(record.scene_entity).second) {
        throw std::runtime_error(
            "QDR scene contains duplicate or invalid object records.");
      }
      if (explicit_object_id_entities.contains(record.scene_entity) &&
          document.scene().editor_object_id(record.scene_entity) != record.id) {
        throw std::runtime_error(
            "QDR object record id does not match its scene entity.");
      }
      object_records.push_back(std::move(record));
    }
  }
  const std::size_t object_count = object_records.size();
  if (document.upsert_object_records(std::move(object_records)).size() !=
      object_count) {
    throw std::runtime_error("QDR scene contains conflicting object records.");
  }
}

} // namespace

QdrSceneBinaryEncodeResult
encode_qdr_scene_binary(const QdrProjectWriteSnapshot &snapshot) {
  return encode_qdr_scene_binary(snapshot,
                                 scene_uuid_from_id(snapshot.project_id));
}

QdrSceneBinaryEncodeResult encode_qdr_scene_binary(
    const QdrProjectWriteSnapshot &snapshot,
    std::array<std::byte, 16> scene_uuid) {
  QDR_PROFILE_FUNCTION();
  EncodedGeometrySet geometry = encode_unique_geometry(snapshot);
  const auto metadata_started = std::chrono::steady_clock::now();
  std::vector<std::byte> metadata =
      encode_scene_metadata(snapshot, geometry.chunk_ids_by_mesh);
  const double metadata_ms = std::chrono::duration<double, std::milli>(
                                 std::chrono::steady_clock::now() -
                                 metadata_started)
                                 .count();
  std::vector<quader_io::QdrBinaryChunkInput> chunks;
  chunks.reserve(1u + geometry.chunks.size());
  chunks.push_back({
      .type = quader_io::QdrBinaryChunkType::SceneMetadata,
      .schema_version = kSceneChunkSchemaVersion,
      .flags = quader_io::QdrBinaryChunkFlags::Critical,
      .id = 0u,
      .content_hash = quader_io::qdr_binary_content_hash(metadata),
      .bytes = std::move(metadata),
      .allow_compression = false,
  });
  for (quader_io::QdrBinaryChunkInput &chunk : geometry.chunks) {
    chunks.push_back(std::move(chunk));
  }
  QdrSceneBinaryEncodeResult result{
      .logical_geometry_count = snapshot.meshes.size(),
      .geometry_encode_count = geometry.encode_count,
      .unique_geometry_count = geometry.chunks.size(),
      .geometry_encode_ms = geometry.encode_ms,
      .geometry_deduplication_ms = geometry.deduplication_ms,
      .metadata_encode_ms = metadata_ms,
  };
  const auto container_started = std::chrono::steady_clock::now();
  quader_io::QdrBinaryContainerBuildResult container =
      quader_io::build_qdr_binary_container_with_stats(scene_uuid,
                                                       std::move(chunks));
  result.container_build_and_compression_ms =
      std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - container_started)
          .count();
  result.compressed_geometry_count =
      container.stats.compressed_geometry_chunk_count;
  result.uncompressed_geometry_bytes =
      container.stats.uncompressed_geometry_bytes;
  result.stored_geometry_bytes = container.stats.stored_geometry_bytes;
  result.bytes = std::move(container.bytes);
  return result;
}

QdrSceneBinaryDecodeResult decode_qdr_scene_binary_with_stats(
    const quader_io::QdrBinaryContainerReadResult &container) {
  QDR_PROFILE_FUNCTION();
  if (!container.ok) {
    throw std::runtime_error(container.message.empty()
                                 ? "QDR binary container is invalid."
                                 : container.message);
  }
  QdrSceneBinaryDecodeStats stats;
  const auto metadata_started = std::chrono::steady_clock::now();
  const quader_io::QdrBinaryChunk &metadata =
      require_scene_metadata_chunk(container);
  const fb::ScenePayload *payload = verify_scene_payload(metadata.bytes());
  stats.metadata_access_ms = std::chrono::duration<double, std::milli>(
                                 std::chrono::steady_clock::now() -
                                 metadata_started)
                                 .count();
  const auto hydration_started = std::chrono::steady_clock::now();
  std::unordered_map<std::uint64_t, const quader_io::QdrBinaryChunk *>
      geometry_chunks;
  for (const quader_io::QdrBinaryChunk &chunk : container.chunks) {
    if (chunk.type ==
        static_cast<std::uint32_t>(quader_io::QdrBinaryChunkType::Geometry)) {
      if (chunk.schema_version != kGeometryChunkSchemaVersion ||
          !geometry_chunks.emplace(chunk.id, &chunk).second) {
        throw std::runtime_error(
            "QDR scene contains unsupported or duplicate geometry chunks.");
      }
    }
  }

  SceneContentDocument document(
      SceneContentDocumentRecord{
          .id = payload->scene_id(),
          .name = string_from_fb(payload->scene_name(), "Untitled Scene"),
      },
      SceneContentDocumentRootPolicy::DeferSceneRoot);
  std::vector<ProjectAssetReference> project_asset_dependencies =
      decode_project_asset_dependencies(*payload);
  std::optional<QdrSceneViewportCameraPose> viewport_camera_pose =
      decode_viewport_camera_pose(*payload, metadata.schema_version);
  decode_scene_layers(document, *payload, metadata.schema_version);
  decode_assets(document, *payload, geometry_chunks, stats);
  decode_entities(document, *payload, metadata.schema_version);

  const SceneContentDocumentAssetReferenceReport references =
      document.validate_asset_references();
  if (!references.ok()) {
    throw std::runtime_error("QDR scene has invalid asset references: " +
                             references.issues.front().message);
  }
  const SceneContentDocumentIdentityReport identity =
      document.validate_object_identity_invariants();
  if (!identity.ok()) {
    throw std::runtime_error("QDR scene identity invariant failed: " +
                             identity.issues.front().message);
  }
  stats.hydration_ms = std::chrono::duration<double, std::milli>(
                           std::chrono::steady_clock::now() -
                           hydration_started)
                           .count();
  return {
      .document = std::move(document),
      .project_asset_dependencies = std::move(project_asset_dependencies),
      .viewport_camera_pose = std::move(viewport_camera_pose),
      .stats = stats,
  };
}

SceneContentDocument decode_qdr_scene_binary(
    const quader_io::QdrBinaryContainerReadResult &container) {
  return decode_qdr_scene_binary_with_stats(container).document;
}

} // namespace quader
