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
#include "scene/light_properties.hpp"
#include "scene/scene_texture_mapping.hpp"
#include "scene/scene_transform.hpp"

#include <quader/modeling/io/qdr_document.hpp>
#include <quader/modeling/curves/curve_document.hpp>
#include <quader/modeling/neutral/polygon/document_topology.hpp>
#include <quader/modeling/neutral/polygon/document_validation.hpp>

#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/verifier.h>

#include "qdr_scene_generated.h"
#include "qdr_scene_usage_schema10_generated.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
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
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace quader {
namespace {

namespace fb = ::quader::qdr::fb;

constexpr std::uint16_t kMinimumSceneChunkSchemaVersion = 1u;
constexpr std::uint16_t kSceneChunkSchemaVersion = 19u;
constexpr std::uint16_t kGeometryChunkSchemaVersion = 1u;

/**
 * Dependency-only view of the incompatible schema-10 dialect written by the
 * retired bgfx branch. The fields inspected here retained their current wire
 * positions; later entity fields deliberately remain opaque.
 */
struct RetiredSchema10DependencyEntity final : private flatbuffers::Table {
  // Keep the generated FlatBuffers offset names for legacy schema comparison.
  // NOLINTBEGIN(readability-identifier-naming)
  enum FlatBuffersVTableOffset : flatbuffers::voffset_t {
    VT_ID = 4,
    VT_EDITOR_OBJECT_ID = 8,
    VT_MATERIAL = 26,
  };
  // NOLINTEND(readability-identifier-naming)

  [[nodiscard]] std::uint64_t id() const {
    return GetField<std::uint64_t>(VT_ID, 0u);
  }

  [[nodiscard]] const fb::MaterialComponent *material() const {
    return GetPointer<const fb::MaterialComponent *>(VT_MATERIAL);
  }

  // FlatBuffers verifier templates require this exact method name.
  template <bool B = false>
  [[nodiscard]] bool
  Verify(flatbuffers::VerifierTemplate<B> &verifier) const { // NOLINT(readability-identifier-naming)
    return VerifyTableStart(verifier) &&
           VerifyField<std::uint64_t>(verifier, VT_ID, 8u) &&
           VerifyField<std::uint64_t>(verifier, VT_EDITOR_OBJECT_ID, 8u) &&
           VerifyOffset(verifier, VT_MATERIAL) &&
           verifier.VerifyTable(material()) && verifier.EndTable();
  }
};

struct RetiredSchema10DependencyPayload final : private flatbuffers::Table {
  // Keep the generated FlatBuffers offset names for legacy schema comparison.
  // NOLINTBEGIN(readability-identifier-naming)
  enum FlatBuffersVTableOffset : flatbuffers::voffset_t {
    VT_SCENE_ID = 4,
    VT_SCENE_NAME = 6,
    VT_ENTITIES = 14,
    VT_ROOT_ENTITIES = 16,
    VT_PROJECT_ASSET_DEPENDENCIES = 18,
  };
  // NOLINTEND(readability-identifier-naming)

  [[nodiscard]] std::uint64_t scene_id() const {
    return GetField<std::uint64_t>(VT_SCENE_ID, 0u);
  }

  [[nodiscard]] const flatbuffers::String *scene_name() const {
    return GetPointer<const flatbuffers::String *>(VT_SCENE_NAME);
  }

  [[nodiscard]] const flatbuffers::Vector<
      flatbuffers::Offset<RetiredSchema10DependencyEntity>> *
  entities() const {
    return GetPointer<const flatbuffers::Vector<
        flatbuffers::Offset<RetiredSchema10DependencyEntity>> *>(VT_ENTITIES);
  }

  [[nodiscard]] const flatbuffers::Vector<std::uint64_t> *
  root_entities() const {
    return GetPointer<const flatbuffers::Vector<std::uint64_t> *>(
        VT_ROOT_ENTITIES);
  }

  [[nodiscard]] const flatbuffers::Vector<
      flatbuffers::Offset<fb::ProjectAssetDependency>> *
  project_asset_dependencies() const {
    return GetPointer<const flatbuffers::Vector<
        flatbuffers::Offset<fb::ProjectAssetDependency>> *>(
        VT_PROJECT_ASSET_DEPENDENCIES);
  }

  // FlatBuffers verifier templates require this exact method name.
  template <bool B = false>
  [[nodiscard]] bool
  Verify(flatbuffers::VerifierTemplate<B> &verifier) const { // NOLINT(readability-identifier-naming)
    return VerifyTableStart(verifier) &&
           VerifyField<std::uint64_t>(verifier, VT_SCENE_ID, 8u) &&
           VerifyOffset(verifier, VT_SCENE_NAME) &&
           verifier.VerifyString(scene_name()) &&
           VerifyOffset(verifier, VT_ENTITIES) &&
           verifier.VerifyVector(entities()) &&
           verifier.VerifyVectorOfTables(entities()) &&
           VerifyOffsetRequired(verifier, VT_ROOT_ENTITIES) &&
           verifier.VerifyVector(root_entities()) &&
           VerifyOffset(verifier, VT_PROJECT_ASSET_DEPENDENCIES) &&
           verifier.VerifyVector(project_asset_dependencies()) &&
           verifier.VerifyVectorOfTables(project_asset_dependencies()) &&
           verifier.EndTable();
  }
};

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
  case EntityKind::Curve:
    return fb::EntityKind::Curve;
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
  case fb::EntityKind::Curve:
    return EntityKind::Curve;
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
  case SceneToneMapping::Agx:
    return fb::SceneToneMapping::Agx;
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
  case fb::SceneToneMapping::Agx:
    return SceneToneMapping::Agx;
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

enum class EncodedGeometryWriteDisposition {
  Preserve,
  CanonicalizeLegacyAuthored,
};

template <typename Element>
[[nodiscard]] bool flatbuffer_vector_is_empty(
    const flatbuffers::Vector<Element> *values) {
  return values == nullptr || values->size() == 0u;
}

[[nodiscard]] bool
legacy_authored_geometry_fields_are_empty(const fb::GeometryPayload &payload) {
  return payload.next_vertex_id() == 1u && payload.next_face_id() == 1u &&
         flatbuffer_vector_is_empty(payload.polygon_vertices()) &&
         flatbuffer_vector_is_empty(payload.face_ids()) &&
         flatbuffer_vector_is_empty(payload.face_vertex_offsets()) &&
         flatbuffer_vector_is_empty(payload.face_vertices()) &&
         flatbuffer_vector_is_empty(payload.face_uv_offsets()) &&
         flatbuffer_vector_is_empty(payload.face_uvs()) &&
         flatbuffer_vector_is_empty(payload.face_material_slots()) &&
         flatbuffer_vector_is_empty(payload.face_shading()) &&
         flatbuffer_vector_is_empty(payload.hard_normal_edges());
}

[[nodiscard]] EncodedGeometryWriteDisposition
encoded_geometry_write_disposition(const fb::GeometryPayload &payload,
                                   std::uint16_t scene_schema_version) {
  switch (payload.kind()) {
  case fb::GeometryKind::CompiledMesh:
    return EncodedGeometryWriteDisposition::Preserve;
  case fb::GeometryKind::AuthoredPolygon: {
    const flatbuffers::Vector<std::uint8_t> *authored =
        payload.authored_payload();
    const bool has_sdk_payload = authored != nullptr && authored->size() != 0u;
    const bool has_legacy_fields =
        !legacy_authored_geometry_fields_are_empty(payload);
    if (has_sdk_payload && !has_legacy_fields) {
      // SDK decoding owns migration and full validation. Only inspect the
      // stable header here so deferred pre-1.3 topology cannot bypass migration
      // by retaining its encoded bytes for the next save.
      if (authored->size() >= 8u && authored->Get(0u) == 'Q' &&
          authored->Get(1u) == 'G' && authored->Get(2u) == 'E' &&
          authored->Get(3u) == 'O' && authored->Get(4u) == 1u &&
          authored->Get(5u) == 0u && authored->Get(6u) < 3u &&
          authored->Get(7u) == 0u) {
        return EncodedGeometryWriteDisposition::CanonicalizeLegacyAuthored;
      }
      return EncodedGeometryWriteDisposition::Preserve;
    }
    // Repository writers used the SDK authored payload from the first binary
    // schema onward. The CSR-only allowance is intentionally restricted to
    // the separately supported schema 1-3 compatibility surface; schemas 4-7
    // already required the same SDK payload as the current schema.
    if (scene_schema_version <= 3u && (has_sdk_payload || has_legacy_fields)) {
      return EncodedGeometryWriteDisposition::CanonicalizeLegacyAuthored;
    }
    if (!has_sdk_payload) {
      throw std::runtime_error(
          "QDR authored geometry is missing its SDK topology payload.");
    }
    throw std::runtime_error(
        "QDR authored geometry contains retired CSR topology fields.");
  }
  default:
    throw std::runtime_error("QDR geometry contains an unknown payload kind.");
  }
}

[[nodiscard]] std::vector<std::byte> encode_geometry(
    const QdrProjectMeshAssetWriteSnapshot &mesh) {
  QDR_PROFILE_FUNCTION();
  std::optional<std::vector<std::byte>> upgraded_authored;
  if (mesh.encoded_geometry) {
    const fb::GeometryPayload *payload =
        verify_geometry_payload(*mesh.encoded_geometry);
    if (encoded_geometry_write_disposition(*payload, kSceneChunkSchemaVersion) ==
        EncodedGeometryWriteDisposition::Preserve) {
      return *mesh.encoded_geometry;
    }
    const auto *authored = payload->authored_payload();
    const auto decoded = modeling::deserialize_qdr_authored_topology(
        std::as_bytes(std::span(authored->data(), authored->size())));
    if (!decoded.ok()) {
      throw std::runtime_error("SDK authored-topology decoding failed: " +
                               decoded.error_message());
    }
    auto encoded = modeling::serialize_qdr_authored_topology(decoded.value());
    if (!encoded.ok()) {
      throw std::runtime_error("SDK authored-topology encoding failed: " +
                               encoded.error_message());
    }
    upgraded_authored = std::move(encoded).value();
  }
  if (!mesh.geometry && !upgraded_authored) {
    throw std::runtime_error("QDR mesh snapshot has no geometry payload.");
  }
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
  if (upgraded_authored) {
    kind = fb::GeometryKind::AuthoredPolygon;
    authored_payload = builder.CreateVector(
        reinterpret_cast<const std::uint8_t *>(upgraded_authored->data()),
        upgraded_authored->size());
  } else if (mesh.has_authored_polygon_document &&
      mesh.authored_polygon_document) {
    kind = fb::GeometryKind::AuthoredPolygon;
    quader::modeling::Result<quader::modeling::PolygonDocument> imported =
        MeshProjectionService::sdk_document_from_polygon_document(
            *mesh.authored_polygon_document);
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
    const MeshGeometryPayload &geometry = *mesh.geometry;
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
                      const SceneSettings &settings) {
  if (!scene_fog_settings_is_valid(settings.fog)) {
    throw std::runtime_error("QDR scene contains invalid fog settings.");
  }
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
      settings.camera.far_plane, settings.camera.use_exposure);
  const auto post_process = fb::CreateScenePostProcessSettings(
      builder, settings.post_process.enabled,
      settings.post_process.anti_aliasing,
      settings.post_process.ambient_occlusion,
      settings.post_process.ambient_occlusion_intensity,
      settings.post_process.ambient_occlusion_radius,
      settings.post_process.bloom, settings.post_process.bloom_strength,
      settings.post_process.dithering,
      to_fb(settings.post_process.tone_mapping));
  const fb::Vec3 fog_color = to_fb(settings.fog.color);
  const auto fog = fb::CreateSceneFogSettings(
      builder, settings.fog.enabled, settings.fog.distance,
      settings.fog.cut_off_distance, settings.fog.maximum_opacity,
      settings.fog.height, settings.fog.height_falloff, &fog_color,
      settings.fog.density, settings.fog.in_scattering_start,
      settings.fog.in_scattering_size, settings.fog.color_from_environment);
  return fb::CreateSceneSettingsComponent(builder, environment, camera,
                                          post_process, fog);
}

void validate_write_snapshot_roots(const QdrProjectWriteSnapshot &snapshot) {
  std::set<EntityId> expected;
  for (const auto &entity : snapshot.entities) {
    if (entity.parent == kInvalidEntityId) {
      if (entity.id == kInvalidEntityId || !expected.insert(entity.id).second) {
        throw std::runtime_error("QDR snapshot contains invalid parentless entities.");
      }
    }
  }
  for (const EntityId root : snapshot.root_entities) {
    if (expected.erase(root) != 1u) {
      throw std::runtime_error("QDR snapshot contains invalid ordered roots.");
    }
  }
  if (!expected.empty()) {
    throw std::runtime_error("QDR snapshot ordered roots are incomplete.");
  }
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

  std::set<AssetId> curve_ids;
  std::vector<flatbuffers::Offset<fb::CurveAsset>> curves;
  curves.reserve(snapshot.curves.size());
  for (const QdrProjectCurveAssetWriteSnapshot &curve : snapshot.curves) {
    if (curve.id == kInvalidAssetId || !curve.document ||
        curve.document->point_count() < 2u ||
        !curve_ids.insert(curve.id).second) {
      throw std::runtime_error("QDR curve asset is missing or duplicated.");
    }
    const auto encoded = modeling::serialize_curve_document(*curve.document);
    if (!encoded.ok()) {
      throw std::runtime_error("QDR curve asset is invalid: " +
                               encoded.error().message);
    }
    const auto &bytes = encoded.value();
    curves.push_back(fb::CreateCurveAsset(
        builder, curve.id, builder.CreateString(curve.name),
        builder.CreateVector(
            reinterpret_cast<const std::uint8_t *>(bytes.data()),
            bytes.size())));
  }

  std::vector<flatbuffers::Offset<fb::MaterialAsset>> materials;
  materials.reserve(snapshot.materials.size());
  for (const QdrProjectMaterialAssetWriteSnapshot &snapshot_material :
       snapshot.materials) {
    const MaterialDocument &material = snapshot_material.material;
    if (!is_valid_material_diffuse_model(material.diffuse_model)) {
      throw std::runtime_error("QDR material diffuse model is unsupported.");
    }
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
        builder.CreateVector(slots), material.node_graph_reserved,
        static_cast<fb::MaterialDiffuseModel>(material.diffuse_model)));
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
    flatbuffers::Offset<fb::CurveComponent> curve;
    if (entity.curve.has_value()) {
      if (!curve_component_is_valid(*entity.curve) ||
          entity.kind != EntityKind::Curve ||
          !curve_ids.contains(entity.curve->curve) ||
          entity.mesh.has_value() || entity.light.has_value() ||
          entity.instance_definition.has_value() ||
          entity.instance_occurrence.has_value() ||
          entity.texture_mapping.has_value()) {
        throw std::runtime_error("QDR entity curve binding is invalid.");
      }
      curve = fb::CreateCurveComponent(
          builder, entity.curve->curve, entity.curve->solidify,
          entity.curve->solidify_resolution, entity.curve->solidify_radius);
    } else if (entity.kind == EntityKind::Curve) {
      throw std::runtime_error("QDR curve entity is missing its curve binding.");
    }
    // The legacy position slot stores the authored Location channel.
    const fb::Vec3 location = to_fb(entity.transform.location);
    const fb::Vec3 rotation = to_fb(entity.transform.rotation_euler);
    const fb::Vec3 scale = to_fb(entity.transform.scale);
    if (!scene_matrix_is_affine(entity.transform.parenting_offset)) {
      throw std::runtime_error("QDR entity parenting offset is not finite affine data.");
    }
    const auto &offset_values = entity.transform.parenting_offset.values;
    const flatbuffers::Offset<fb::TransformComponent> transform =
        fb::CreateTransformComponent(builder, &location, &rotation, &scale,
                                     entity.transform.custom_pivot,
                                     builder.CreateVector(offset_values.data(),
                                                          offset_values.size()));
    const flatbuffers::Offset<fb::VisibilityComponent> visibility =
        fb::CreateVisibilityComponent(builder, entity.visibility.visible,
                                      entity.visibility.selectable);
    // Historical wire slots retain their inverse locked convention.
    const flatbuffers::Offset<fb::LockComponent> lock =
        fb::CreateLockComponent(builder, !entity.selectable.selectable);
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
      if (!mesh_wireframe_color_is_valid(entity.mesh->wireframe_on_shaded_color)) {
        throw std::runtime_error("Cannot encode invalid mesh wireframe color.");
      }
      std::optional<fb::Vec3> wireframe_color;
      if (entity.mesh->wireframe_on_shaded_color.has_value()) {
        wireframe_color = to_fb(*entity.mesh->wireframe_on_shaded_color);
      }
      mesh = fb::CreateMeshComponent(
          builder, entity.mesh->mesh, entity.mesh->editor_only,
          entity.mesh->cast_shadows, entity.mesh->double_sided,
          entity.mesh->generate_collisions,
          to_fb(entity.mesh->collision_type),
          entity.mesh->display_collision, to_fb(entity.mesh->body_type),
          entity.mesh->wireframe_on_shaded,
          wireframe_color.has_value() ? &*wireframe_color : nullptr);
    }
    flatbuffers::Offset<fb::MaterialComponent> material;
    if (entity.material.has_value()) {
      material = encode_material_component(builder, *entity.material);
    }
    flatbuffers::Offset<fb::LightComponent> light;
    if (entity.light.has_value()) {
      if (!light_component_metadata_is_valid(*entity.light)) {
        throw std::runtime_error("Cannot encode invalid light metadata.");
      }
      const fb::Vec3 color = to_fb(entity.light->color);
      light = fb::CreateLightComponent(
          builder, to_fb(entity.light->type), &color, entity.light->intensity,
          entity.light->range, entity.light->casts_shadows,
          entity.light->distance_attenuation,
          static_cast<fb::LightUnit>(entity.light->light_unit),
          static_cast<fb::LightColorMode>(entity.light->color_mode),
          entity.light->color_temperature_kelvin,
          static_cast<fb::LightSourcePreset>(entity.light->source_preset));
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
        modeling_object, mesh, material, light, false, {},
        instance_definition, instance_occurrence, entity.layer_id,
        texture_mapping, curve));
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
      {}, builder.CreateVector(dependencies),
      builder.CreateVector(layers), viewport_camera_pose,
      builder.CreateVector(curves),
      encode_scene_settings(builder, snapshot.scene_settings),
      builder.CreateVector(snapshot.root_entities));
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
    std::span<const std::byte> bytes, std::uint16_t schema_version) {
  flatbuffers::Verifier verifier(
      reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size());
  if (!fb::VerifyScenePayloadBuffer(verifier)) {
    throw std::runtime_error("QDR scene metadata FlatBuffer is invalid.");
  }
  const auto *payload = fb::GetScenePayload(bytes.data());
  if (schema_version >= 19u && payload->entities() != nullptr) {
    for (const auto *entity : *payload->entities()) {
      if (const auto *mesh = entity->mesh();
          mesh != nullptr && mesh->wireframe_on_shaded_color() != nullptr &&
          !mesh_wireframe_color_is_valid(from_fb(mesh->wireframe_on_shaded_color()))) {
        throw std::runtime_error("QDR scene contains an invalid mesh wireframe color.");
      }
    }
  }
  return payload;
}

[[nodiscard]] const RetiredSchema10DependencyPayload *
verify_retired_schema_10_dependency_payload(
    std::span<const std::byte> bytes) {
  flatbuffers::Verifier verifier(
      reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size());
  if (!verifier.VerifyBuffer<RetiredSchema10DependencyPayload>("QDRS")) {
    return nullptr;
  }
  return flatbuffers::GetRoot<RetiredSchema10DependencyPayload>(bytes.data());
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

void migrate_legacy_csr_edge_normal_modes(quader_poly::Document &document) {
  std::set<std::pair<quader_poly::ElementId, quader_poly::ElementId>>
      hard_edges;
  for (const quader_poly::Edge edge : document.hard_normal_edges) {
    const quader_poly::Edge canonical = quader_poly::make_edge(edge.a, edge.b);
    hard_edges.emplace(canonical.a, canonical.b);
  }
  document.hard_normal_edges.clear();
  document.hard_normal_edges.reserve(hard_edges.size());
  for (const auto &[a, b] : hard_edges) {
    document.hard_normal_edges.push_back(quader_poly::make_edge(a, b));
  }

  std::map<std::pair<quader_poly::ElementId, quader_poly::ElementId>,
           std::size_t>
      smooth_face_count_by_edge;
  for (const quader_poly::Face &face : document.faces) {
    if (face.normal_shading != quader_poly::SurfaceShading::Smooth ||
        face.vertices.size() < 3u) {
      continue;
    }
    std::set<std::pair<quader_poly::ElementId, quader_poly::ElementId>>
        face_edges;
    for (std::size_t corner = 0u; corner < face.vertices.size(); ++corner) {
      const quader_poly::Edge edge = quader_poly::make_edge(
          face.vertices[corner],
          face.vertices[(corner + 1u) % face.vertices.size()]);
      face_edges.emplace(edge.a, edge.b);
    }
    for (const auto &edge : face_edges) {
      ++smooth_face_count_by_edge[edge];
    }
  }

  document.soft_normal_edges.clear();
  for (const auto &[edge, smooth_face_count] : smooth_face_count_by_edge) {
    if (smooth_face_count >= 2u && !hard_edges.contains(edge)) {
      document.soft_normal_edges.push_back(
          quader_poly::make_edge(edge.first, edge.second));
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

  // CSR scenes predate explicit Soft overrides. Their renderer joined every
  // shared edge between Smooth faces unless the edge was authored Hard.
  migrate_legacy_csr_edge_normal_modes(document);
  // CSR predates the distinction between implicit automatic shading and an
  // explicit Flat output override. Preserve its Smooth-derived Soft rows first.
  for (quader_poly::Face &face : document.faces) {
    if (face.normal_shading == quader_poly::SurfaceShading::Flat) {
      face.normal_shading = quader_poly::SurfaceShading::Authored;
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
                std::uint16_t scene_schema_version,
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
    const EncodedGeometryWriteDisposition write_disposition =
        encoded_geometry_write_disposition(payload, scene_schema_version);
    const flatbuffers::Vector<std::uint8_t> *authored =
        payload.authored_payload();
    if (authored != nullptr && authored->size() != 0u) {
      const auto *begin =
          reinterpret_cast<const std::byte *>(authored->data());
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
      stats.geometry_decode_ms += std::chrono::duration<double, std::milli>(
                                      std::chrono::steady_clock::now() -
                                      decode_started)
                                      .count();
      const auto compile_started = std::chrono::steady_clock::now();
      std::optional<MeshAsset> mesh =
          MeshProjectionService::compile_sdk_document_mesh(sdk_document);
      stats.geometry_compile_ms += std::chrono::duration<double, std::milli>(
                                       std::chrono::steady_clock::now() -
                                       compile_started)
                                       .count();
      if (!mesh.has_value()) {
        throw std::runtime_error(
            "SDK authored-topology projection failed.");
      }
      return std::move(*mesh);
    }

    if (write_disposition !=
        EncodedGeometryWriteDisposition::CanonicalizeLegacyAuthored) {
      throw std::runtime_error(
          "QDR authored geometry is missing its SDK topology payload.");
    }

    // Schemas 1-3 may store authored topology only in the CSR vectors. They
    // stay reader-only for compatibility; current writers emit the SDK payload
    // and leave these retired fields empty.
    quader_poly::Document legacy_document = decode_polygon_document(payload);
    stats.geometry_decode_ms += std::chrono::duration<double, std::milli>(
                                    std::chrono::steady_clock::now() -
                                    decode_started)
                                    .count();
    const auto compile_started = std::chrono::steady_clock::now();
    MeshAsset mesh =
        MeshProjectionService::compile_polygon_document_mesh(legacy_document);
    stats.geometry_compile_ms += std::chrono::duration<double, std::milli>(
                                     std::chrono::steady_clock::now() -
                                     compile_started)
                                     .count();
    mesh.replace_authored_polygon_document(std::move(legacy_document));
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
  DeferredGeometryDecoder(
      quader_io::QdrBinaryChunk chunk, std::uint16_t scene_schema_version,
      EncodedGeometryWriteDisposition write_disposition)
      : chunk_(std::move(chunk)), scene_schema_version_(scene_schema_version),
        write_disposition_(write_disposition) {}

  [[nodiscard]] bool preserves_encoded_geometry() const noexcept {
    return write_disposition_ == EncodedGeometryWriteDisposition::Preserve;
  }

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
        decoded_.emplace(
            decode_geometry(*payload, scene_schema_version_, deferred_stats));
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
  std::uint16_t scene_schema_version_ = 0u;
  EncodedGeometryWriteDisposition write_disposition_ =
      EncodedGeometryWriteDisposition::Preserve;
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

template <typename StoredMaterialComponent>
[[nodiscard]] MaterialComponent
decode_material_component(const StoredMaterialComponent &value) {
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
      const auto *slot = slots->Get(index);
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
      const auto *slot = slots->Get(index);
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

/**
 * Compatibility-only projection of the retired ScenePayload.objects vector.
 * Entity components remain authoritative; legacy records may supply only a
 * missing editor object id after their identities and references are checked.
 */
struct LegacyQdrObjectRecord final {
  EditorObjectId editor_object_id = kInvalidEditorObjectId;
  EntityId scene_entity = kInvalidEntityId;
  AssetId mesh = kInvalidAssetId;
  MaterialReference material;
};

[[nodiscard]] MaterialReference
decode_legacy_material_reference(const fb::ObjectRecord &stored) {
  MaterialReference material(stored.material());
  if (const flatbuffers::String *project_id = stored.project_material_id();
      project_id != nullptr && !project_id->str().empty()) {
    const std::optional<ProjectAssetId> parsed =
        ProjectAssetId::parse(project_id->string_view());
    if (!parsed.has_value() || material.valid()) {
      throw std::runtime_error(
          "QDR legacy object record contains an invalid or ambiguous "
          "material reference.");
    }
    material = MaterialReference::project(*parsed);
  }
  return material;
}

[[nodiscard]] std::vector<LegacyQdrObjectRecord>
decode_legacy_object_records(const SceneContentDocument &document,
                             const fb::ScenePayload &payload) {
  std::vector<LegacyQdrObjectRecord> records;
  const auto *objects = payload.objects();
  if (objects == nullptr) {
    return records;
  }

  std::set<EditorObjectId> object_ids;
  std::set<EntityId> object_entities;
  records.reserve(objects->size());
  for (std::size_t index = 0u; index < objects->size(); ++index) {
    const fb::ObjectRecord *stored = objects->Get(index);
    LegacyQdrObjectRecord record{
        .editor_object_id = stored->editor_object_id(),
        .scene_entity = stored->scene_entity(),
        .mesh = stored->mesh(),
        .material = decode_legacy_material_reference(*stored),
    };
    if (record.editor_object_id == kInvalidEditorObjectId ||
        record.scene_entity == kInvalidEntityId ||
        !object_ids.insert(record.editor_object_id).second ||
        !object_entities.insert(record.scene_entity).second) {
      throw std::runtime_error(
          "QDR scene contains duplicate or invalid legacy object records.");
    }
    if (record.mesh != kInvalidAssetId &&
        !document.assets().has_mesh(record.mesh)) {
      throw std::runtime_error(
          "QDR legacy object record references a missing mesh asset.");
    }
    if (record.material.embedded() &&
        document.assets().material(record.material.embedded_asset()) ==
            nullptr) {
      throw std::runtime_error(
          "QDR legacy object record references a missing material asset.");
    }
    records.push_back(std::move(record));
  }
  return records;
}

[[nodiscard]] SceneSettings
decode_scene_settings(const fb::SceneSettingsComponent &value,
                      std::uint16_t schema_version) {
  const fb::SceneEnvironmentSettings *environment = value.environment();
  const fb::SceneCameraSettings *camera = value.camera();
  const fb::ScenePostProcessSettings *post_process = value.post_process();
  if (!environment || !camera || !post_process) {
    throw std::runtime_error(
        "QDR scene settings payload is missing required fields.");
  }
  SceneSettings settings;
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
  settings.camera.use_exposure =
      schema_version < 11u || camera->use_exposure();
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
  if (schema_version < 13u &&
      post_process->tone_mapping() == fb::SceneToneMapping::Agx) {
    throw std::runtime_error(
        "QDR scene contains AgX tone mapping before schema 13.");
  }
  settings.post_process.tone_mapping =
      tone_mapping_from_fb(post_process->tone_mapping());
  if (schema_version >= 15u && value.fog() != nullptr) {
    const fb::SceneFogSettings &fog = *value.fog();
    settings.fog.enabled = fog.enabled();
    settings.fog.distance = fog.distance();
    settings.fog.cut_off_distance = fog.cut_off_distance();
    settings.fog.maximum_opacity = fog.maximum_opacity();
    settings.fog.height = fog.height();
    settings.fog.height_falloff = fog.height_falloff();
    settings.fog.color = from_fb(fog.color(), settings.fog.color);
    settings.fog.density = fog.density();
    settings.fog.in_scattering_start = fog.in_scattering_start();
    settings.fog.in_scattering_size = fog.in_scattering_size();
    settings.fog.color_from_environment = fog.color_from_environment();
    if (!scene_fog_settings_is_valid(settings.fog)) {
      throw std::runtime_error("QDR scene contains invalid fog settings.");
    }
  }
  return settings;
}

// Legacy wrappers exist only while decoding an unpublished candidate. They never
// escape into the live scene or a write snapshot.
struct DecodedSceneDocumentMetadata final {
  SceneSettings settings;
  EntityId legacy_root = kInvalidEntityId;
  std::vector<EntityId> roots;
};

[[nodiscard]] DecodedSceneDocumentMetadata decode_scene_document_metadata(
    const fb::ScenePayload &payload, std::uint16_t schema_version) {
  DecodedSceneDocumentMetadata result;
  std::map<EntityId, const fb::Entity *> entities;
  std::vector<EntityId> encoded_roots;
  if (const auto *stored_entities = payload.entities()) {
    for (const fb::Entity *entity : *stored_entities) {
      if (entity->id() == kInvalidEntityId ||
          !entities.emplace(entity->id(), entity).second) {
        throw std::runtime_error("QDR scene contains a duplicate or invalid entity id.");
      }
      if (entity->parent() == kInvalidEntityId) encoded_roots.push_back(entity->id());
      if (!entity->scene_root() && entity->scene_settings() == nullptr) continue;
      if (schema_version >= 16u ||
          entity->scene_settings() == nullptr ||
          result.legacy_root != kInvalidEntityId ||
          entity->parent() != kInvalidEntityId ||
          entity->modeling_object() != nullptr || entity->mesh() != nullptr ||
          entity->material() != nullptr || entity->light() != nullptr ||
          entity->instance_definition() != nullptr ||
          entity->instance_occurrence() != nullptr ||
          entity->texture_mapping() != nullptr || entity->curve() != nullptr ||
          entity->layer_id() != kInvalidSceneLayerId) {
        throw std::runtime_error("QDR scene contains invalid legacy scene-root settings or payload.");
      }
      result.legacy_root = entity->id();
      result.settings = decode_scene_settings(*entity->scene_settings(), schema_version);
    }
  }
  if (schema_version >= 16u) {
    if (payload.scene_settings() == nullptr || payload.root_entities() == nullptr) {
      throw std::runtime_error("QDR scene is missing document settings or ordered roots.");
    }
    result.settings = decode_scene_settings(*payload.scene_settings(), schema_version);
    result.roots.assign(payload.root_entities()->begin(), payload.root_entities()->end());
    std::set<EntityId> unique_roots;
    for (const EntityId root : result.roots) {
      const auto entity = entities.find(root);
      if (!unique_roots.insert(root).second || entity == entities.end() ||
          entity->second->parent() != kInvalidEntityId) {
        throw std::runtime_error("QDR scene contains invalid ordered roots.");
      }
    }
    if (result.roots.size() != encoded_roots.size()) {
      throw std::runtime_error("QDR scene ordered roots are incomplete.");
    }
  } else {
    for (const EntityId root : encoded_roots) {
      if (root != result.legacy_root) {
        result.roots.push_back(root);
      } else if (const auto *stored_entities = payload.entities()) {
        for (const fb::Entity *entity : *stored_entities) {
          if (entity->parent() == root) result.roots.push_back(entity->id());
        }
      }
    }
  }
  SceneDocument settings_validator;
  if (!settings_validator.set_scene_settings(result.settings)) {
    throw std::runtime_error("QDR scene contains invalid document settings.");
  }
  return result;
}

void apply_scene_document_metadata(
    SceneDocument &scene, const DecodedSceneDocumentMetadata &metadata) {
  if (metadata.legacy_root != kInvalidEntityId) {
    const EntityId root = metadata.legacy_root;
    const auto children = scene.children(root);
    const bool root_selectable = scene.visibility(root)->selectable;
    for (const EntityId child : children) {
      const QMat4 world_transform = scene.world_transform_matrix(child);
      const bool visible = scene.is_effectively_visible(child);
      const bool selection_allowed = scene.is_effectively_selectable(child);
      const bool selectable = root_selectable && scene.visibility(child)->selectable;
      // Preserve exact authored TRS when the removed wrapper has no placement.
      const bool transform_changes =
          scene.local_transform_matrix(root).values != QMat4{}.values;
      if (!scene.set_parent(child, kInvalidEntityId, transform_changes) ||
          !scene_matrix_near_equal(scene.world_transform_matrix(child), world_transform) ||
          !scene.set_visible(child, visible) ||
          !scene.set_selectable(child, selection_allowed) ||
          !scene.set_selectability(child, selectable)) {
        throw std::runtime_error("QDR legacy scene-root promotion cannot preserve object state.");
      }
    }
    if (!scene.destroy_entity(root)) {
      throw std::runtime_error("QDR legacy scene root could not be removed.");
    }
  }
  if (scene.root_entities().size() != metadata.roots.size()) {
    throw std::runtime_error("QDR scene ordered roots are incomplete.");
  }
  for (std::size_t index = 0u; index < metadata.roots.size(); ++index) {
    if (!scene.set_sibling_ordinal(metadata.roots[index], index)) {
      throw std::runtime_error("QDR scene contains invalid ordered roots.");
    }
  }
  if (!scene.set_scene_settings(metadata.settings)) {
    throw std::runtime_error("QDR scene contains invalid document settings.");
  }
}

template <typename Payload>
[[nodiscard]] std::vector<ProjectAssetReference>
decode_project_asset_dependencies(const Payload &payload) {
  std::vector<ProjectAssetReference> dependencies;
  std::set<std::string> stable_ids;
  const auto *stored_dependencies = payload.project_asset_dependencies();
  if (stored_dependencies == nullptr) {
    return dependencies;
  }
  dependencies.reserve(stored_dependencies->size());
  for (std::size_t index = 0u; index < stored_dependencies->size(); ++index) {
    const auto *stored = stored_dependencies->Get(index);
    const std::optional<ProjectAssetId> stable_id =
        ProjectAssetId::parse(string_from_fb(stored->stable_id()));
    const ProjectAssetKind kind = project_asset_kind_from_fb(
        static_cast<fb::ProjectAssetKind>(stored->kind()));
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

void decode_material_and_texture_assets(
    SceneContentDocument &document, const fb::ScenePayload &payload,
    std::uint16_t scene_schema_version) {
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
      if (scene_schema_version < 14u &&
          stored_material->diffuse_model() != fb::MaterialDiffuseModel::Burley) {
        throw std::runtime_error(
            "QDR material diffuse model is unsupported before schema 14.");
      }
      material.diffuse_model =
          static_cast<MaterialDiffuseModel>(stored_material->diffuse_model());
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

void decode_assets(
    SceneContentDocument &document, const fb::ScenePayload &payload,
    const std::unordered_map<std::uint64_t,
                             const quader_io::QdrBinaryChunk *> &geometry_chunks,
    std::uint16_t scene_schema_version,
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
          descriptor.geometry_id,
          decode_geometry(*geometry_payload, scene_schema_version, stats));
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
          const fb::GeometryPayload *geometry_payload =
              verify_geometry_payload(chunk.bytes());
          const EncodedGeometryWriteDisposition write_disposition =
              encoded_geometry_write_disposition(*geometry_payload,
                                                 scene_schema_version);
          deferred = deferred_geometry
                         .emplace(
                             geometry_id,
                             std::make_shared<DeferredGeometryDecoder>(
                                 chunk, scene_schema_version,
                                 write_disposition))
                         .first;
        }
        const std::shared_ptr<DeferredGeometryDecoder> loader =
            deferred->second;
        std::function<std::shared_ptr<const std::vector<std::byte>>()>
            encoded_geometry_loader;
        if (loader->preserves_encoded_geometry()) {
          encoded_geometry_loader =
              [loader]() { return loader->encoded_geometry(); };
        }
        if (document.assets().add_deferred_mesh(
                descriptor.id, descriptor.name,
                descriptor.has_stable_cpu_topology,
                descriptor.has_authored_polygon_document,
                [loader]() { return loader->materialize(); },
                std::move(encoded_geometry_loader)) ==
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

  decode_material_and_texture_assets(document, payload, scene_schema_version);
}

void decode_curve_assets(SceneContentDocument &document,
                         const fb::ScenePayload &payload,
                         std::uint16_t schema_version) {
  const auto *curves = payload.curves();
  if (curves == nullptr || curves->size() == 0u) {
    return;
  }
  if (schema_version < 8u) {
    throw std::runtime_error("QDR curve assets require scene schema 8.");
  }
  std::set<AssetId> ids;
  for (const fb::CurveAsset *stored : *curves) {
    if (stored->id() == kInvalidAssetId || !ids.insert(stored->id()).second) {
      throw std::runtime_error(
          "QDR scene contains an invalid or duplicate curve asset id.");
    }
    const auto *payload_bytes = stored->authored_payload();
    if (payload_bytes == nullptr || payload_bytes->size() == 0u) {
      throw std::runtime_error("QDR curve asset has no authored payload.");
    }
    auto decoded = modeling::deserialize_curve_document(std::span<const std::byte>(
        reinterpret_cast<const std::byte *>(payload_bytes->data()),
        payload_bytes->size()));
    if (!decoded.ok()) {
      throw std::runtime_error("QDR curve asset is invalid: " +
                               decoded.error().message);
    }
    CurveAsset curve;
    curve.name = string_from_fb(stored->name(), "Curve");
    curve.document = std::make_shared<const modeling::CurveDocument>(
        std::move(decoded).value());
    if (document.assets().add_curve(stored->id(), std::move(curve)) ==
        kInvalidAssetId) {
      throw std::runtime_error("QDR scene could not install a curve asset.");
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

[[nodiscard]] TransformComponent
decode_transform_component(const fb::TransformComponent &stored,
                           std::uint16_t schema_version) {
  TransformComponent transform{
      .location = from_fb(stored.position()),
      .rotation_euler = from_fb(stored.rotation_euler()),
      .scale = from_fb(stored.scale(), {1.0f, 1.0f, 1.0f}),
      .custom_pivot = stored.custom_pivot(),
  };
  if (schema_version >= 17u) {
    if (const auto *values = stored.parenting_offset()) {
      if (values->size() != transform.parenting_offset.values.size()) {
        throw std::runtime_error("QDR entity parenting offset has an invalid size.");
      }
      std::copy(values->begin(), values->end(),
                transform.parenting_offset.values.begin());
      if (!scene_matrix_is_affine(transform.parenting_offset)) {
        throw std::runtime_error("QDR entity parenting offset is not finite affine data.");
      }
    }
  }
  return transform;
}

[[nodiscard]] SceneEntityState
decode_entity_state(const SceneContentDocument &document,
                    const fb::Entity &stored,
                    std::uint16_t schema_version) {
  const bool is_curve = stored.kind() == fb::EntityKind::Curve;
  if (is_curve || stored.curve() != nullptr) {
    if (schema_version < 8u || !is_curve || stored.curve() == nullptr ||
        stored.mesh() != nullptr || stored.light() != nullptr ||
        stored.instance_definition() != nullptr ||
        stored.instance_occurrence() != nullptr || stored.scene_root() ||
        stored.scene_settings() != nullptr ||
        stored.texture_mapping() != nullptr) {
      throw std::runtime_error("QDR scene contains an invalid curve entity.");
    }
  }

  SceneEntityState state;
  state.id = stored.id();
  state.uuid = string_from_fb(stored.uuid());
  state.editor_object_id = stored.editor_object_id();
  state.name = string_from_fb(stored.name(), "Entity");
  state.kind = from_fb(stored.kind());
  state.parent = stored.parent();
  if (const fb::TransformComponent *value = stored.transform()) {
    state.transform = decode_transform_component(*value, schema_version);
  }
  if (const fb::VisibilityComponent *value = stored.visibility()) {
    state.visibility = {
        .visible = value->visible(),
        .selectable = value->selectable(),
    };
  }
  if (const fb::LockComponent *value = stored.lock()) {
    state.selectable = {.selectable = !value->locked()};
  }
  if (const fb::ModelingObjectComponent *value = stored.modeling_object()) {
    state.modeling_object = ModelingObjectComponent{
        .object_id = value->object_id(),
        .material_slot = value->material_slot(),
        .sdk_owned = value->sdk_owned(),
        .local_frame_version =
            schema_version >= 6u
                ? value->local_frame_version()
                : kLegacyUnknownModelingObjectLocalFrameVersion,
    };
  }
  if (const fb::MeshComponent *value = stored.mesh()) {
    state.mesh = MeshComponent{
        .mesh = value->mesh(),
        .editor_only = value->editor_only(),
        .cast_shadows = value->cast_shadows(),
        .double_sided = value->double_sided(),
        .generate_collisions = value->generate_collisions(),
        .collision_type = from_fb(value->collision_type()),
        .display_collision = value->display_collision(),
        .body_type = mesh_body_type_from_fb(value->body_type()),
        .wireframe_on_shaded = schema_version < 18u || value->wireframe_on_shaded(),
        .wireframe_on_shaded_color =
            schema_version >= 19u && value->wireframe_on_shaded_color() != nullptr
                ? std::optional{from_fb(value->wireframe_on_shaded_color())}
                : std::nullopt,
    };
  }
  if (const fb::MaterialComponent *value = stored.material()) {
    MaterialComponent material = decode_material_component(*value);
    if (material.material.valid() || !material.material_slots.empty()) {
      state.material = std::move(material);
    }
  }
  if (const fb::CurveComponent *value = stored.curve()) {
    if (!document.assets().has_curve(value->curve())) {
      throw std::runtime_error("QDR entity references a missing curve asset.");
    }
    CurveComponent curve{.curve = value->curve()};
    if (schema_version >= 9u) {
      curve.solidify = value->solidify();
      curve.solidify_resolution = value->solidify_resolution();
      curve.solidify_radius = value->solidify_radius();
    }
    if (!curve_component_is_valid(curve)) {
      throw std::runtime_error(
          "QDR scene contains an invalid curve component.");
    }
    state.curve = curve;
  }
  if (const fb::LightComponent *value = stored.light()) {
    const float distance_attenuation =
        schema_version >= 10u ? value->distance_attenuation() : 2.0f;
    if (!std::isfinite(distance_attenuation)) {
      throw std::runtime_error(
          "QDR scene contains invalid light distance attenuation.");
    }
    state.light = LightComponent{
        .type = light_type_from_fb(value->type()),
        .color = from_fb(value->color(), {1.0f, 1.0f, 1.0f}),
        .intensity = value->intensity(),
        .range = value->range(),
        .casts_shadows = value->casts_shadows(),
        .distance_attenuation = distance_attenuation,
    };
    if (schema_version >= 12u) {
      state.light->light_unit = static_cast<LightUnit>(value->light_unit());
      state.light->color_mode =
          static_cast<LightColorMode>(value->color_mode());
      state.light->color_temperature_kelvin =
          value->color_temperature_kelvin();
      state.light->source_preset =
          static_cast<LightSourcePreset>(value->source_preset());
      if (!light_component_metadata_is_valid(*state.light)) {
        throw std::runtime_error("QDR scene contains invalid light metadata.");
      }
    }
  }
  if (stored.instance_definition() != nullptr) {
    state.instance_definition = InstanceDefinitionComponent{};
  }
  if (const fb::InstanceOccurrenceComponent *value =
          stored.instance_occurrence()) {
    state.instance_occurrence =
        InstanceOccurrenceComponent{.definition = value->definition()};
  }
  if (schema_version >= 7u && stored.texture_mapping() != nullptr) {
    const auto *values =
        stored.texture_mapping()->sampling_from_local();
    TextureMappingComponent mapping;
    if (values == nullptr ||
        values->size() != mapping.sampling_from_local.values.size()) {
      throw std::runtime_error(
          "QDR entity texture mapping must contain one affine matrix.");
    }
    std::copy(values->begin(), values->end(),
              mapping.sampling_from_local.values.begin());
    state.texture_mapping = mapping;
  }
  if (schema_version >= 4u) {
    state.layer_id = stored.layer_id();
  }
  return state;
}

void decode_entities(SceneContentDocument &document,
                     const fb::ScenePayload &payload,
                     std::uint16_t schema_version) {
  const auto scene_metadata = decode_scene_document_metadata(payload, schema_version);
  const std::vector<LegacyQdrObjectRecord> legacy_object_records =
      decode_legacy_object_records(document, payload);
  std::unordered_map<EntityId, const LegacyQdrObjectRecord *>
      legacy_record_by_entity;
  legacy_record_by_entity.reserve(legacy_object_records.size());
  for (const LegacyQdrObjectRecord &record : legacy_object_records) {
    legacy_record_by_entity.emplace(record.scene_entity, &record);
  }

  std::vector<SceneParentAssignment> parent_links;
  std::vector<EntityId> instance_definitions;
  std::vector<std::pair<EntityId, EntityId>> instance_occurrences;
  std::vector<SceneLayerAssignment> layer_assignments;
  std::vector<SceneTextureMappingAssignment> texture_mappings;
  if (const auto *entities = payload.entities()) {
    parent_links.reserve(entities->size());
    for (std::size_t index = 0u; index < entities->size(); ++index) {
      const fb::Entity *stored = entities->Get(index);
      SceneEntityState state =
          decode_entity_state(document, *stored, schema_version);
      const auto legacy_record = legacy_record_by_entity.find(state.id);
      if (legacy_record != legacy_record_by_entity.end()) {
        if (state.editor_object_id != kInvalidEditorObjectId &&
            state.editor_object_id !=
                legacy_record->second->editor_object_id) {
          throw std::runtime_error(
              "QDR legacy object record id conflicts with its canonical "
              "scene entity.");
        }
        if (state.editor_object_id == kInvalidEditorObjectId) {
          state.editor_object_id =
              legacy_record->second->editor_object_id;
        }
      }

      const EntityId entity = document.scene().create_entity_with_id(
          state.id, state.name, state.kind, state.editor_object_id);
      if (entity == kInvalidEntityId) {
        throw std::runtime_error(
            "QDR scene contains a duplicate or invalid entity id.");
      }
      if (!state.uuid.empty() &&
          !document.scene().set_entity_uuid(entity, state.uuid)) {
        throw std::runtime_error(
            "QDR scene contains a duplicate or invalid entity UUID.");
      }

      if (state.texture_mapping.has_value()) {
        texture_mappings.push_back(
            {.entity = entity, .mapping = state.texture_mapping});
      }
      if (state.layer_id != kInvalidSceneLayerId) {
        layer_assignments.push_back(
            {.entity = entity, .layer_id = state.layer_id});
      }
      if (!apply_scene_entity_state(
              document.scene(), entity, state,
              SceneEntityStateApplyOptions{
                  .include_selection = false,
                  .include_instance_roles = false,
                  .include_texture_mapping = false,
                  .include_layer_membership = false,
              })) {
        throw std::runtime_error(
            "QDR scene entity component state is invalid.");
      }

      if (state.instance_definition.has_value() &&
          state.instance_occurrence.has_value()) {
        throw std::runtime_error(
            "QDR entity cannot be both an instance definition and an "
            "occurrence.");
      }
      if (state.instance_definition.has_value()) {
        instance_definitions.push_back(entity);
      }
      if (state.instance_occurrence.has_value()) {
        if (state.kind != EntityKind::Instance) {
          throw std::runtime_error(
              "QDR instance occurrence has a non-instance entity kind.");
        }
        instance_occurrences.emplace_back(
            entity, state.instance_occurrence->definition);
      } else if (state.kind == EntityKind::Instance) {
        throw std::runtime_error(
            "QDR instance entity is missing its occurrence component.");
      }
      parent_links.push_back(
          SceneParentAssignment{.child = entity, .parent = state.parent});
    }
  }
  if (!document.scene().replace_hierarchy(parent_links)) {
    throw std::runtime_error(
        "QDR scene contains an invalid entity hierarchy.");
  }
  // Historical object projections also included the Scene wrapper. Validate
  // every identity before migration removes that compatibility-only entity.
  for (const LegacyQdrObjectRecord &record : legacy_object_records) {
    if (!document.scene().exists(record.scene_entity) ||
        document.scene().editor_object_id(record.scene_entity) !=
            record.editor_object_id) {
      throw std::runtime_error(
          "QDR legacy object record points at a missing or conflicting scene "
          "entity.");
    }
  }
  apply_scene_document_metadata(document.scene(), scene_metadata);

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

}

[[nodiscard]] std::optional<QdrSceneBinaryDependencyDecodeResult>
decode_retired_schema_10_dependency_projection(
    const quader_io::QdrBinaryContainerReadResult &container,
    const quader_io::QdrBinaryChunk &metadata,
    std::string validation_error) {
  if (metadata.schema_version != 10u) {
    return std::nullopt;
  }

  bool has_retired_geometry = false;
  for (const quader_io::QdrBinaryChunk &chunk : container.chunks) {
    if (chunk.type !=
        static_cast<std::uint32_t>(quader_io::QdrBinaryChunkType::Geometry)) {
      continue;
    }
    if (chunk.schema_version != 2u) {
      return std::nullopt;
    }
    (void)chunk.bytes();
    has_retired_geometry = true;
  }
  if (!has_retired_geometry) {
    return std::nullopt;
  }

  const RetiredSchema10DependencyPayload *payload =
      verify_retired_schema_10_dependency_payload(metadata.bytes());
  if (payload == nullptr) {
    return std::nullopt;
  }

  std::set<EntityId> entity_ids;
  std::set<ProjectAssetId> material_ids;
  std::vector<ProjectAssetReference> project_asset_dependencies =
      decode_project_asset_dependencies(*payload);
  for (const ProjectAssetReference &reference : project_asset_dependencies) {
    if (reference.kind == ProjectAssetKind::Material) {
      material_ids.insert(reference.stable_id);
    }
  }

  if (const auto *entities = payload->entities()) {
    for (const RetiredSchema10DependencyEntity *entity : *entities) {
      if (entity->id() == kInvalidEntityId ||
          !entity_ids.insert(entity->id()).second) {
        throw std::runtime_error(
            "Retired QDR scene contains an invalid or duplicate entity id.");
      }
      if (const fb::MaterialComponent *stored = entity->material()) {
        const MaterialComponent material = decode_material_component(*stored);
        if (material.material.project_owned()) {
          material_ids.insert(material.material.project_asset());
        }
        for (const auto &[slot, reference] : material.material_slots) {
          (void)slot;
          if (reference.project_owned()) {
            material_ids.insert(reference.project_asset());
          }
        }
      }
    }
  }

  std::set<EntityId> root_ids;
  for (const std::uint64_t root : *payload->root_entities()) {
    if (root == kInvalidEntityId || !entity_ids.contains(root) ||
        !root_ids.insert(root).second) {
      throw std::runtime_error(
          "Retired QDR scene contains an invalid root-entity list.");
    }
  }

  return QdrSceneBinaryDependencyDecodeResult{
      .fully_validated = false,
      .validation_error = std::move(validation_error),
      .scene_name = string_from_fb(payload->scene_name(), "Untitled Scene"),
      .material_ids = {material_ids.begin(), material_ids.end()},
      .project_asset_dependencies = std::move(project_asset_dependencies),
  };
}

/// Reads slot identities from validated topology without constructing a render mesh.
template <typename GeometryPayload>
[[nodiscard]] std::vector<std::uint32_t>
geometry_material_slots(const GeometryPayload &payload,
                        std::uint16_t scene_schema_version) {
  std::set<std::uint32_t> slots;
  const auto kind = static_cast<fb::GeometryKind>(payload.kind());
  if (kind == fb::GeometryKind::AuthoredPolygon) {
    if constexpr (std::is_same_v<GeometryPayload, fb::GeometryPayload>) {
      (void)encoded_geometry_write_disposition(payload, scene_schema_version);
    }
    const auto *authored = payload.authored_payload();
    if (authored != nullptr && authored->size() != 0u) {
      auto decoded = modeling::deserialize_qdr_authored_topology(
          std::as_bytes(std::span(authored->data(), authored->size())));
      if (!decoded.ok()) {
        throw std::runtime_error("SDK authored-topology decoding failed: " +
                                 decoded.error_message());
      }
      const auto topology =
          MeshProjectionService::polygon_document_from_sdk_document(decoded.value());
      if (!topology.has_value()) {
        throw std::runtime_error("QDR authored-topology export failed.");
      }
      for (const auto &face : topology->faces) {
        slots.insert(face.material_slot);
      }
    } else if constexpr (std::is_same_v<GeometryPayload, fb::GeometryPayload>) {
      for (const auto &face : decode_polygon_document(payload).faces) {
        slots.insert(face.material_slot);
      }
    } else {
      throw std::runtime_error("QDR authored geometry has no SDK topology payload.");
    }
  } else if (kind == fb::GeometryKind::CompiledMesh) {
    const auto *vertices = payload.mesh_vertices();
    const auto *indices = payload.mesh_indices();
    const std::size_t vertex_count = vertices ? vertices->size() : 0u;
    const std::size_t index_count = indices ? indices->size() : 0u;
    if (indices != nullptr) {
      for (const std::uint32_t index : *indices) {
        if (index >= vertex_count) {
          throw std::runtime_error("QDR compiled geometry index references a missing vertex.");
        }
      }
    }
    if (vertices != nullptr) {
      for (const auto *vertex : *vertices) {
        const auto &position = vertex->position();
        const auto &normal = vertex->normal();
        const auto &uv = vertex->uv();
        if (!std::isfinite(position.x()) || !std::isfinite(position.y()) ||
            !std::isfinite(position.z()) || !std::isfinite(normal.x()) ||
            !std::isfinite(normal.y()) || !std::isfinite(normal.z()) ||
            !std::isfinite(uv.x()) || !std::isfinite(uv.y())) {
          throw std::runtime_error("QDR compiled geometry attributes must be finite.");
        }
      }
    }
    if (const auto *mappings = payload.mesh_texture_mapping();
        mappings != nullptr && mappings->size() != 0u) {
      if (mappings->size() != vertex_count) {
        throw std::runtime_error("QDR compiled geometry has an invalid texture mapping count.");
      }
      for (const auto *mapping : *mappings) {
        const auto &normal = mapping->geometric_normal();
        if (!std::isfinite(mapping->carrier_00()) ||
            !std::isfinite(mapping->carrier_01()) ||
            !std::isfinite(mapping->carrier_10()) ||
            !std::isfinite(mapping->carrier_11()) ||
            !std::isfinite(normal.x()) || !std::isfinite(normal.y()) ||
            !std::isfinite(normal.z())) {
          throw std::runtime_error("QDR compiled geometry texture mapping must be finite.");
        }
      }
    }
    const auto *primitives = payload.mesh_primitives();
    if (primitives != nullptr && primitives->size() != 0u) {
      for (const auto *primitive : *primitives) {
        const std::uint64_t end =
            static_cast<std::uint64_t>(primitive->index_offset()) +
            primitive->index_count();
        if (primitive->index_offset() > index_count || end > index_count ||
            primitive->index_count() % 3u != 0u) {
          throw std::runtime_error("QDR compiled geometry contains an invalid primitive span.");
        }
        if (primitive->index_count() != 0u) {
          slots.insert(primitive->material_slot());
        }
      }
    } else if (index_count != 0u) {
      if (index_count % 3u != 0u) {
        throw std::runtime_error("QDR compiled geometry has incomplete triangles.");
      }
      slots.insert(0u);
    }
  } else {
    throw std::runtime_error("QDR geometry contains an unknown payload kind.");
  }
  return {slots.begin(), slots.end()};
}

namespace usage10 = ::quader::qdr::usage_schema10::fb;

template <typename Payload>
[[nodiscard]] QdrSceneBinaryMaterialUsageDecodeResult
collect_scene_material_usage(
    const Payload &payload,
    const quader_io::QdrBinaryContainerReadResult &container,
    std::uint16_t scene_schema_version) {
  constexpr bool kRetired = std::is_same_v<Payload, usage10::ScenePayload>;
  QdrSceneBinaryMaterialUsageDecodeResult result;
  result.scene_name = string_from_fb(payload.scene_name(), "Untitled Scene");
  (void)decode_project_asset_dependencies(payload);

  std::set<AssetId> texture_ids;
  if (const auto *textures = payload.textures()) {
    for (const auto *texture : *textures) {
      if (texture->id() == kInvalidAssetId ||
          !texture_ids.insert(texture->id()).second ||
          static_cast<unsigned>(texture->color_space()) > 1u) {
        throw std::runtime_error("QDR scene contains an invalid or duplicate texture asset.");
      }
    }
  }
  std::set<AssetId> material_ids;
  if (const auto *materials = payload.materials()) {
    for (const auto *material : *materials) {
      if (material->id() == kInvalidAssetId ||
          !material_ids.insert(material->id()).second) {
        throw std::runtime_error("QDR scene contains an invalid or duplicate material asset.");
      }
      if constexpr (kRetired) {
        if (material->texture() != kInvalidAssetId &&
            !texture_ids.contains(material->texture())) {
          throw std::runtime_error("Retired QDR material references a missing texture.");
        }
        if (const auto *transform = material->texture_transform()) {
          const auto finite_pair = [](const auto *value) {
            return value == nullptr ||
                   (std::isfinite(value->x()) && std::isfinite(value->y()));
          };
          if (!finite_pair(transform->center()) ||
              !finite_pair(transform->scale()) ||
              !finite_pair(transform->offset()) ||
              !std::isfinite(transform->rotation_degrees())) {
            throw std::runtime_error("Retired QDR material has an invalid texture transform.");
          }
        }
      }
    }
  }

  const auto geometry_started = std::chrono::steady_clock::now();
  std::unordered_map<std::uint64_t, const quader_io::QdrBinaryChunk *> chunks;
  for (const auto &chunk : container.chunks) {
    (void)chunk.bytes(); // Check every checksum, including unreferenced payloads.
    if (chunk.type == static_cast<std::uint32_t>(quader_io::QdrBinaryChunkType::Geometry)) {
      if (chunk.schema_version != (kRetired ? 2u : kGeometryChunkSchemaVersion) ||
          !chunks.emplace(chunk.id, &chunk).second) {
        throw std::runtime_error("QDR scene contains unsupported or duplicate geometry chunks.");
      }
    }
  }
  std::map<std::uint64_t, std::vector<std::uint32_t>> slots_by_chunk;
  std::map<AssetId, std::uint64_t> geometry_by_mesh;
  if (const auto *meshes = payload.meshes()) {
    result.stats.logical_geometry_count = meshes->size();
    for (const auto *mesh : *meshes) {
      if (mesh->id() == kInvalidAssetId ||
          !geometry_by_mesh.emplace(mesh->id(), mesh->geometry_chunk_id()).second) {
        throw std::runtime_error("QDR scene contains an invalid or duplicate mesh asset id.");
      }
      const auto &chunk = require_geometry_chunk(chunks, mesh->geometry_chunk_id());
      if (!slots_by_chunk.contains(chunk.id)) {
        const auto bytes = chunk.bytes();
        if constexpr (kRetired) {
          flatbuffers::Verifier verifier(
              reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size());
          if (!verifier.VerifyBuffer<usage10::GeometryPayload>("QDRG")) {
            throw std::runtime_error("Retired QDR geometry FlatBuffer is invalid.");
          }
          const auto *geometry = flatbuffers::GetRoot<usage10::GeometryPayload>(bytes.data());
          slots_by_chunk.emplace(chunk.id, geometry_material_slots(*geometry, scene_schema_version));
        } else {
          slots_by_chunk.emplace(chunk.id, geometry_material_slots(
              *verify_geometry_payload(bytes), scene_schema_version));
        }
        ++result.stats.unique_geometry_decoded_count;
      }
    }
  }
  std::map<AssetId, bool> renderable_curves;
  if (const auto *curves = payload.curves()) {
    if (curves->size() != 0u && scene_schema_version < 8u) {
      throw std::runtime_error("QDR curve assets require scene schema 8.");
    }
    for (const auto *curve : *curves) {
      const auto *bytes = curve->authored_payload();
      if (curve->id() == kInvalidAssetId || bytes == nullptr || bytes->size() == 0u ||
          renderable_curves.contains(curve->id())) {
        throw std::runtime_error("QDR scene contains an invalid or duplicate curve asset.");
      }
      auto decoded = modeling::deserialize_curve_document(
          std::as_bytes(std::span(bytes->data(), bytes->size())));
      if (!decoded.ok()) {
        throw std::runtime_error("QDR curve asset is invalid: " + decoded.error_message());
      }
      renderable_curves.emplace(curve->id(), decoded.value().points().size() >= 2u);
    }
  }
  result.stats.geometry_decode_ms = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - geometry_started).count();

  std::set<ProjectAssetId> used_project;
  std::set<AssetId> used_embedded;
  const auto check_reference = [&](const MaterialReference &reference, bool slot) {
    if ((slot && !reference.valid()) ||
        (reference.embedded() && !material_ids.contains(reference.embedded_asset()))) {
      throw std::runtime_error("QDR scene material binding references a missing material asset.");
    }
  };
  const auto collect_reference = [&](const MaterialReference &reference) {
    if (reference.project_owned()) {
      used_project.insert(reference.project_asset());
    } else if (reference.embedded()) {
      used_embedded.insert(reference.embedded_asset());
    }
  };
  std::map<EntityId, EntityId> parents;
  std::set<EditorObjectId> editor_ids;
  std::set<std::string> uuids;
  std::set<EntityId> definitions;
  std::vector<std::pair<EntityId, EntityId>> occurrences;
  if (const auto *entities = payload.entities()) {
    for (const auto *entity : *entities) {
      const std::string uuid = string_from_fb(entity->uuid());
      if (entity->id() == kInvalidEntityId ||
          !parents.emplace(entity->id(), entity->parent()).second ||
          (!uuid.empty() && (!ProjectAssetId::parse(uuid).has_value() ||
                            !uuids.insert(uuid).second)) ||
          (entity->editor_object_id() != kInvalidEditorObjectId &&
           !editor_ids.insert(entity->editor_object_id()).second)) {
        throw std::runtime_error("QDR scene contains an invalid or duplicate entity identity.");
      }
      if (static_cast<unsigned>(entity->kind()) > (kRetired ? 4u : 5u)) {
        throw std::runtime_error("QDR scene contains an unknown entity kind.");
      }
      if (entity->instance_definition() != nullptr) {
        definitions.insert(entity->id());
      }
      if (const auto *instance = entity->instance_occurrence()) {
        occurrences.emplace_back(entity->id(), instance->definition());
      }
      MaterialComponent material;
      if (const auto *stored = entity->material()) {
        material = decode_material_component(*stored);
        check_reference(material.material, false);
        for (const auto &[slot, reference] : material.material_slots) {
          (void)slot;
          check_reference(reference, true);
        }
      }
      if (const auto *mesh = entity->mesh()) {
        const auto geometry = geometry_by_mesh.find(mesh->mesh());
        if (geometry == geometry_by_mesh.end()) {
          throw std::runtime_error("QDR entity references a missing mesh asset.");
        }
        for (const auto slot : slots_by_chunk.at(geometry->second)) {
          collect_reference(material.material_for_slot(slot));
        }
      }
      if (const auto *curve = entity->curve()) {
        const auto found = renderable_curves.find(curve->curve());
        if (found == renderable_curves.end() || !curve_component_is_valid({
                .curve = curve->curve(), .solidify = curve->solidify(),
                .solidify_resolution = curve->solidify_resolution(),
                .solidify_radius = curve->solidify_radius()})) {
          throw std::runtime_error("QDR entity contains an invalid curve reference.");
        }
        if (curve->solidify() && found->second) {
          collect_reference(material.material_for_slot(0u));
        }
      }
    }
  }
  std::set<EntityId> checked;
  for (const auto &[entity, parent] : parents) {
    (void)parent;
    std::set<EntityId> path;
    EntityId current = entity;
    while (current != kInvalidEntityId && !checked.contains(current)) {
      const auto found = parents.find(current);
      if (found == parents.end() || !path.insert(current).second) {
        throw std::runtime_error("QDR scene contains an invalid entity hierarchy.");
      }
      current = found->second;
    }
    checked.insert(path.begin(), path.end());
  }
  for (const auto &[entity, definition] : occurrences) {
    if (entity == definition || !definitions.contains(definition)) {
      throw std::runtime_error("QDR scene contains an invalid instance reference.");
    }
  }
  if constexpr (kRetired) {
    std::set<EntityId> roots;
    for (const EntityId root : *payload.root_entities()) {
      const auto found = parents.find(root);
      if (found == parents.end() || found->second != kInvalidEntityId ||
          !roots.insert(root).second) {
        throw std::runtime_error("Retired QDR scene contains an invalid root-entity list.");
      }
    }
    for (const auto &[entity, parent] : parents) {
      if (parent == kInvalidEntityId && !roots.contains(entity)) {
        throw std::runtime_error("Retired QDR scene root-entity list is incomplete.");
      }
    }
  }
  result.material_ids.assign(used_project.begin(), used_project.end());
  result.embedded_material_ids.assign(used_embedded.begin(), used_embedded.end());
  return result;
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
  validate_write_snapshot_roots(snapshot);
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
          std::move(chunks), snapshot.minimum_product_release);
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
  const fb::ScenePayload *payload = verify_scene_payload(metadata.bytes(), metadata.schema_version);
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
      });
  std::vector<ProjectAssetReference> project_asset_dependencies =
      decode_project_asset_dependencies(*payload);
  std::optional<QdrSceneViewportCameraPose> viewport_camera_pose =
      decode_viewport_camera_pose(*payload, metadata.schema_version);
  decode_scene_layers(document, *payload, metadata.schema_version);
  decode_assets(document, *payload, geometry_chunks, metadata.schema_version,
                stats);
  decode_curve_assets(document, *payload, metadata.schema_version);
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

QdrSceneBinaryDependencyDecodeResult decode_qdr_scene_binary_dependencies(
    const quader_io::QdrBinaryContainerReadResult &container) {
  QDR_PROFILE_FUNCTION();
  if (!container.ok) {
    throw std::runtime_error(container.message.empty()
                                 ? "QDR binary container is invalid."
                                 : container.message);
  }
  const auto metadata_started = std::chrono::steady_clock::now();
  const quader_io::QdrBinaryChunk &metadata =
      require_scene_metadata_chunk(container);
  const fb::ScenePayload *payload = nullptr;
  try {
    payload = verify_scene_payload(metadata.bytes(), metadata.schema_version);
  } catch (const std::runtime_error &error) {
    std::optional<QdrSceneBinaryDependencyDecodeResult> recovered =
        decode_retired_schema_10_dependency_projection(container, metadata,
                                                       error.what());
    if (!recovered.has_value()) {
      throw;
    }
    recovered->stats.metadata_access_ms =
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - metadata_started)
            .count();
    return std::move(*recovered);
  }
  QdrSceneBinaryDependencyDecodeResult result;
  result.scene_name = string_from_fb(payload->scene_name(), "Untitled Scene");
  result.project_asset_dependencies = decode_project_asset_dependencies(*payload);

  std::set<ProjectAssetId> material_ids;
  for (const ProjectAssetReference &reference :
       result.project_asset_dependencies) {
    if (reference.kind == ProjectAssetKind::Material) {
      material_ids.insert(reference.stable_id);
    }
  }

  // Geometry remains opaque. Accessing the bytes verifies lazy checksums but
  // never constructs topology or invokes the modeling compiler.
  std::unordered_map<std::uint64_t, const quader_io::QdrBinaryChunk *>
      geometry_chunks;
  for (const quader_io::QdrBinaryChunk &chunk : container.chunks) {
    (void)chunk.bytes();
    if (chunk.type ==
        static_cast<std::uint32_t>(quader_io::QdrBinaryChunkType::Geometry)) {
      if (chunk.schema_version != kGeometryChunkSchemaVersion ||
          !geometry_chunks.emplace(chunk.id, &chunk).second) {
        throw std::runtime_error(
            "QDR scene contains unsupported or duplicate geometry chunks.");
      }
    }
  }
  std::set<AssetId> mesh_ids;
  if (const auto *meshes = payload->meshes()) {
    result.stats.logical_geometry_count = meshes->size();
    for (const fb::MeshAsset *mesh : *meshes) {
      if (mesh->id() == kInvalidAssetId ||
          !mesh_ids.insert(mesh->id()).second) {
        throw std::runtime_error(
            "QDR scene contains an invalid or duplicate mesh asset id.");
      }
      (void)require_geometry_chunk(geometry_chunks, mesh->geometry_chunk_id());
    }
  }

  SceneContentDocument metadata_document(
      SceneContentDocumentRecord{.id = payload->scene_id(),
                                 .name = result.scene_name});
  decode_material_and_texture_assets(metadata_document, *payload,
                                     metadata.schema_version);
  const auto collect_material = [&](const MaterialReference &reference,
                                    bool slot) {
    if ((slot && !reference.valid()) ||
        (reference.embedded() &&
         metadata_document.assets().material(reference.embedded_asset()) ==
             nullptr)) {
      throw std::runtime_error(
          "QDR scene material binding references a missing material asset.");
    }
    if (reference.project_owned()) {
      material_ids.insert(reference.project_asset());
    }
  };

  std::map<EntityId, const fb::Entity *> entities;
  std::set<EditorObjectId> editor_object_ids;
  if (const auto *stored_entities = payload->entities()) {
    for (const fb::Entity *entity : *stored_entities) {
      if (entity->id() == kInvalidEntityId ||
          !entities.emplace(entity->id(), entity).second ||
          (entity->editor_object_id() != kInvalidEditorObjectId &&
           !editor_object_ids.insert(entity->editor_object_id()).second)) {
        throw std::runtime_error(
            "QDR scene contains a duplicate or invalid entity identity.");
      }
      if (const fb::MeshComponent *mesh = entity->mesh();
          mesh != nullptr && !mesh_ids.contains(mesh->mesh())) {
        throw std::runtime_error("QDR entity references a missing mesh asset.");
      }
      if (const fb::MaterialComponent *stored = entity->material()) {
        const MaterialComponent material = decode_material_component(*stored);
        collect_material(material.material, false);
        for (const auto &[slot, reference] : material.material_slots) {
          (void)slot;
          collect_material(reference, true);
        }
      }
    }
  }

  std::set<EditorObjectId> legacy_object_ids;
  std::set<EntityId> legacy_entity_ids;
  std::map<EntityId, EditorObjectId> legacy_identity_by_entity;
  if (const auto *objects = payload->objects()) {
    for (const fb::ObjectRecord *object : *objects) {
      const auto entity = entities.find(object->scene_entity());
      if (object->editor_object_id() == kInvalidEditorObjectId ||
          !legacy_object_ids.insert(object->editor_object_id()).second ||
          !legacy_entity_ids.insert(object->scene_entity()).second ||
          entity == entities.end() ||
          (entity->second->editor_object_id() != kInvalidEditorObjectId &&
           entity->second->editor_object_id() != object->editor_object_id()) ||
          (entity->second->editor_object_id() == kInvalidEditorObjectId &&
           editor_object_ids.contains(object->editor_object_id()))) {
        throw std::runtime_error(
            "QDR legacy object record has an invalid or conflicting identity.");
      }
      if (object->mesh() != kInvalidAssetId &&
          !mesh_ids.contains(object->mesh())) {
        throw std::runtime_error(
            "QDR legacy object record references a missing mesh asset.");
      }
      legacy_identity_by_entity.emplace(object->scene_entity(),
                                        object->editor_object_id());
      collect_material(decode_legacy_material_reference(*object), false);
    }
  }

  const auto scene_metadata =
      decode_scene_document_metadata(*payload, metadata.schema_version);
  std::vector<SceneParentAssignment> parent_links;
  if (const auto *stored_entities = payload->entities()) {
    parent_links.reserve(stored_entities->size());
    for (const fb::Entity *stored : *stored_entities) {
      EditorObjectId object_id = stored->editor_object_id();
      if (object_id == kInvalidEditorObjectId) {
        if (const auto legacy = legacy_identity_by_entity.find(stored->id());
            legacy != legacy_identity_by_entity.end()) {
          object_id = legacy->second;
        }
      }
      const EntityId entity = metadata_document.scene().create_entity_with_id(
          stored->id(), string_from_fb(stored->name()), from_fb(stored->kind()),
          object_id);
      const std::string uuid = string_from_fb(stored->uuid());
      if (entity == kInvalidEntityId ||
          (!uuid.empty() &&
           !metadata_document.scene().set_entity_uuid(entity, uuid))) {
        throw std::runtime_error(
            "QDR scene contains a duplicate or invalid entity identity.");
      }
      // The dependency projection needs placement and inherited presentation,
      // but must not decode geometry or hydrate asset-backed components.
      SceneEntityState metadata_state{
          .id = entity,
          .kind = from_fb(stored->kind()),
      };
      if (const auto *transform = stored->transform()) {
        metadata_state.transform =
            decode_transform_component(*transform, metadata.schema_version);
      }
      if (const auto *visibility = stored->visibility()) {
        metadata_state.visibility = {
            .visible = visibility->visible(),
            .selectable = visibility->selectable(),
        };
      }
      if (const auto *lock = stored->lock()) {
        metadata_state.selectable.selectable = !lock->locked();
      }
      if (!apply_scene_entity_state(
              metadata_document.scene(), entity, metadata_state,
              SceneEntityStateApplyOptions{
                  .include_modeling_object = false,
                  .include_selection = false,
                  .include_instance_roles = false,
                  .include_texture_mapping = false,
                  .include_layer_membership = false,
              })) {
        throw std::runtime_error(
            "QDR scene contains invalid metadata entity state.");
      }
      parent_links.push_back({.child = entity, .parent = stored->parent()});
    }
  }
  if (!metadata_document.scene().replace_hierarchy(parent_links)) {
    throw std::runtime_error("QDR scene contains an invalid entity hierarchy.");
  }
  apply_scene_document_metadata(metadata_document.scene(), scene_metadata);
  const SceneContentDocumentIdentityReport identity =
      metadata_document.validate_object_identity_invariants();
  if (!identity.ok()) {
    throw std::runtime_error("QDR scene identity invariant failed: " +
                             identity.issues.front().message);
  }

  result.material_ids.assign(material_ids.begin(), material_ids.end());
  result.stats.metadata_access_ms = std::chrono::duration<double, std::milli>(
                                        std::chrono::steady_clock::now() -
                                        metadata_started)
                                        .count();
  return result;
}

QdrSceneBinaryMaterialUsageDecodeResult decode_qdr_scene_binary_material_usage(
    const quader_io::QdrBinaryContainerReadResult &container) {
  QDR_PROFILE_FUNCTION();
  if (!container.ok) {
    throw std::runtime_error(container.message.empty()
                                 ? "QDR binary container is invalid."
                                 : container.message);
  }
  const auto started = std::chrono::steady_clock::now();
  const auto &metadata = require_scene_metadata_chunk(container);
  bool retired_geometry = false;
  for (const auto &chunk : container.chunks) {
    retired_geometry = retired_geometry ||
        (chunk.type == static_cast<std::uint32_t>(quader_io::QdrBinaryChunkType::Geometry) &&
         chunk.schema_version == 2u);
  }
  QdrSceneBinaryMaterialUsageDecodeResult result;
  if (metadata.schema_version == 10u && retired_geometry) {
    const auto bytes = metadata.bytes();
    flatbuffers::Verifier verifier(
        reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size());
    if (!usage10::VerifyScenePayloadBuffer(verifier)) {
      throw std::runtime_error("Retired QDR scene metadata FlatBuffer is invalid.");
    }
    result = collect_scene_material_usage(
        *usage10::GetScenePayload(bytes.data()), container, metadata.schema_version);
  } else {
    // Keep the established current-format metadata, identity, hierarchy and
    // dormant-reference validation while extracting a narrower set of usage.
    const auto validation = decode_qdr_scene_binary_dependencies(container);
    if (!validation.fully_validated) {
      throw std::runtime_error(validation.validation_error);
    }
    result = collect_scene_material_usage(
        *verify_scene_payload(metadata.bytes(), metadata.schema_version), container, metadata.schema_version);
  }
  result.stats.metadata_access_ms = std::max(0.0,
      std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - started).count() -
      result.stats.geometry_decode_ms);
  return result;
}

std::vector<std::uint32_t> decode_qdr_geometry_material_slots(
    std::span<const std::byte> bytes, std::uint16_t scene_schema_version) {
  return geometry_material_slots(*verify_geometry_payload(bytes),
                                  scene_schema_version);
}

} // namespace quader
