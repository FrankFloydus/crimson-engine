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
struct MeshVertexPayload {
  Vec3 position{};
  Vec3 normal{0.0F, 1.0F, 0.0F};
  Vec2 uv0{};
  std::uint32_t color = 0xff9f9f9fU;
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
 * Enumerates portable authored face-normal shading modes.
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
 * Stores one exact authored face record for import/export.
 */
struct AuthoredTopologyFace {
  FaceId id{};
  std::vector<VertexId> vertices;
  std::vector<Vec2> uvs;
  std::uint32_t material_slot = 0;
  AuthoredSurfaceShading shading = AuthoredSurfaceShading::Flat;
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
  std::vector<EdgeKey> hard_normal_edges;
  std::uint32_t next_vertex_id = 1U;
  std::uint32_t next_face_id = 1U;
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
