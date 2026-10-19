////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <quader/modeling/ids.hpp>
#include <quader/modeling/types.hpp>

#include <cstdint>
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace quader::modeling {

/**
 * Groups SDK element references by component kind.
 */
struct ElementDelta {
  std::vector<VertexId> vertices;
  std::vector<EdgeKey> edges;
  std::vector<FaceId> faces;

  [[nodiscard]] bool empty() const noexcept {
    return vertices.empty() && edges.empty() && faces.empty();
  }
};

/**
 * Identifies which SDK payload families changed.
 */
struct DirtyFlags {
  bool topology = false;
  bool geometry = false;
  bool selection = false;
  bool materials = false;
  bool overlays = false;
};

/**
 * Captures source-to-destination selection remap data after topology changes.
 */
struct SelectionRemap {
  ElementDelta source;
  ElementDelta destination;
};

/**
 * Stores one renderer-neutral compiled mesh vertex.
 */
/** Live polar sampling data, with local covectors and row-major (radius, angle)
 * to UV coefficients. Authored records use base UV; compiled records include
 * the face texture rotation and signed scale. */
/// Defines live local polar sampling and its row-major UV coefficients.
struct CircularUvInterpolation {
  Vec3 center{};
  Vec3 axis_u{1.0F, 0.0F, 0.0F};
  Vec3 axis_v{0.0F, 0.0F, 1.0F};
  std::array<float, 4> uv_from_polar{};
};

/// Stores one renderer-neutral vertex, including a sparse circular field index.
struct MeshVertexPayload {
  Vec3 position{};
  Vec3 normal{0.0F, 1.0F, 0.0F};
  Vec2 uv0{};
  std::uint32_t color = 0xff9f9f9fU;
  VertexId source_vertex{};
  std::array<float, 4> uv_carrier{};
  Vec3 uv_geometric_normal{0.0F, 1.0F, 0.0F};
  std::uint32_t circular_uv_index = 0U;
};

/**
 * Assigns one authored vertex an exact local-space position.
 *
 * This payload is intentionally topology-neutral so transform history can
 * replay sparse position deltas without rebuilding an authored mesh.
 */
struct VertexPositionUpdate {
  VertexId vertex{};
  Vec3 position{};
};

/**
 * Stores one renderer-neutral primitive range and material reference.
 */
struct MeshPrimitivePayload {
  std::uint32_t index_offset = 0;
  std::uint32_t index_count = 0;
  MaterialId material{};
};

/**
 * Stores renderer-neutral mesh buffers with explicit owned lifetime.
 */
struct MeshPayload {
  std::vector<MeshVertexPayload> vertices;
  std::vector<std::uint32_t> indices;
  std::vector<MeshPrimitivePayload> primitives;
  std::uint64_t content_revision = 0;
  std::vector<CircularUvInterpolation> circular_uv_interpolations;
};

/**
 * Stores one authored polygon face record.
 */
struct AuthoredPolygonFacePayload {
  FaceId id{};
  std::vector<VertexId> vertices;
  MaterialId material{};
};

/**
 * Stores authored polygon topology independent of render backends.
 */
struct AuthoredPolygonPayload {
  std::vector<VertexId> vertices;
  std::vector<Vec3> positions;
  std::vector<AuthoredPolygonFacePayload> faces;
  std::uint64_t content_revision = 0;
};

/**
 * Enumerates portable face-normal output modes. Authored uses the connected
 * edge-normal fans by default; Flat explicitly outputs the face normal.
 */
enum class AuthoredSurfaceShading {
  Flat,
  Smooth,
  Authored,
};

/**
 * Stores one exact authored vertex record for import/export.
 */
struct AuthoredTopologyVertex {
  VertexId id{};
  Vec3 position{};
};

/**
 * Transforms base face UVs around the projection origin: rotate, divide by
 * the signed scale, then add the normalized shift. Base UVs remain authored
 * or procedural independently of these persistent settings.
 */
struct FaceTextureTransform {
  Vec2 scale{1.0F, 1.0F};
  Vec2 shift_uv{};
  float rotation_degrees = 0.0F;

  friend bool operator==(const FaceTextureTransform &left,
                         const FaceTextureTransform &right) noexcept {
    return left.scale.x == right.scale.x && left.scale.y == right.scale.y &&
           left.shift_uv.x == right.shift_uv.x &&
           left.shift_uv.y == right.shift_uv.y &&
           left.rotation_degrees == right.rotation_degrees;
  }
};

/**
 * Reads one face's texture settings and material slot without exporting its
 * authored topology or compiled mesh.
 */
struct FaceTextureSettings {
  FaceId face{};
  FaceTextureTransform texture_transform{};
  std::uint32_t material_slot = 0;
};

/**
 * Patches only the supplied properties on one typed face. Shift values use
 * normalized UVs; absent fields retain their current values.
 */
struct FaceTextureTransformUpdate {
  FaceId face{};
  std::optional<float> scale_x;
  std::optional<float> scale_y;
  std::optional<float> shift_x;
  std::optional<float> shift_y;
  std::optional<float> rotation_degrees;
};

/**
 * Stores one exact authored face record for import/export.
 */
struct AuthoredTopologyFace {
  FaceId id{};
  std::vector<VertexId> vertices;
  std::vector<Vec2> uvs;
  std::uint32_t material_slot = 0;
  AuthoredSurfaceShading shading = AuthoredSurfaceShading::Authored;
  FaceTextureTransform texture_transform{};
  std::optional<CircularUvInterpolation> circular_uv_interpolation;
};

/**
 * Owns an exact renderer-neutral authored topology snapshot.
 *
 * Allocation cursors are part of the payload so round trips preserve the
 * document lineage's monotonic non-reuse boundary.
 */
struct AuthoredTopology {
  std::vector<AuthoredTopologyVertex> vertices;
  std::vector<AuthoredTopologyFace> faces;
  /// Validated authoring uses canonical live edges explicitly authored Hard;
  /// absence from both mode sets means Default. Persistence also retains raw
  /// invalid endpoints, ordering and duplicate/conflicting records.
  std::vector<EdgeKey> hard_normal_edges;
  std::uint32_t next_vertex_id = 1U;
  std::uint32_t next_face_id = 1U;
  /// Validated authoring uses canonical live edges explicitly authored Soft,
  /// disjoint from Hard. Persistence retains the raw records without cleanup.
  std::vector<EdgeKey> soft_normal_edges;
};

/**
 * Enumerates semantic overlay record kinds.
 */
enum class SemanticOverlayKind {
  Wire,
  VertexHandle,
  FaceHighlight,
  ToolPreview,
};

/**
 * Stores one semantic overlay record before renderer projection.
 */
struct SemanticOverlayRecord {
  SemanticOverlayKind kind = SemanticOverlayKind::Wire;
  std::vector<Vec3> points;
};

/**
 * Stores semantic overlay records with the selection revision they describe.
 */
struct SemanticOverlayPayload {
  std::vector<SemanticOverlayRecord> records;
  std::uint64_t selection_revision = 0;
};

/**
 * Stores renderer-neutral PBR material values.
 */
struct MaterialPayload {
  MaterialId id{};
  std::string name;
  Vec3 base_color{0.8F, 0.8F, 0.8F};
  float roughness = 0.7F;
  float metallic = 0.0F;
};

} // namespace quader::modeling
