////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <quader/modeling/ids.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace quader::modeling {

/**
 * SDK-owned two-dimensional vector value.
 */
struct Vec2 {
  float x = 0.0F;
  float y = 0.0F;
};

/**
 * SDK-owned three-dimensional vector value.
 */
struct Vec3 {
  float x = 0.0F;
  float y = 0.0F;
  float z = 0.0F;
};

/**
 * Renderer-neutral affine transform axes and origin.
 */
struct Transform3 {
  Vec3 x_axis{1.0F, 0.0F, 0.0F};
  Vec3 y_axis{0.0F, 1.0F, 0.0F};
  Vec3 z_axis{0.0F, 0.0F, 1.0F};
  Vec3 origin{};
};

/** Selects connected-region, vertex-normal, or independent-face extrusion. */
enum class FaceExtrudeMode {
  Region,
  AlongNormals,
  Individual,
};

/** Defines signed face displacement in an affine operation-space metric. */
struct FaceExtrudeOptions {
  FaceExtrudeMode mode = FaceExtrudeMode::Region;
  float amount = 1.0F;
  // An explicit Region direction is normalized in operation space.
  std::optional<Vec3> region_direction;
  Transform3 extrusion_from_local{};
};

/**
 * Summarizes selected face geometry in operation space. The center averages unique
 * selected vertices; summed_normal adds unit face normals without normalizing the sum.
 */
struct PolygonFaceExtrudeFrame {
  Vec3 center{};
  Vec3 summed_normal{};
  Vec3 active_normal{};
  std::size_t face_count = 0;
  std::size_t vertex_count = 0;
};

/**
 * SDK ray value used by portable picking and tool contracts.
 */
struct Ray {
  Vec3 origin{};
  Vec3 direction{0.0F, 0.0F, 1.0F};
};

/**
 * Identifies which component family a public selection value addresses.
 */
enum class SelectionKind {
  Object,
  Vertex,
  Edge,
  Face,
};

/**
 * Describes how a public selection value should be committed to session state.
 */
enum class SelectionEdit {
  Replace,
  Add,
  Remove,
  Toggle,
};

/**
 * Identifies how a knife point resolves against polygon topology.
 */
enum class KnifeTargetKind {
  ExistingVertex,
  ExistingEdge,
  InsertedVertex,
  FacePoint,
};

/**
 * Stores a resolved SDK knife point target without exposing polygon internals.
 */
struct KnifeTarget {
  KnifeTargetKind kind = KnifeTargetKind::ExistingVertex;
  VertexId vertex{};
  EdgeKey edge{};
  float edge_factor = 0.5F;
  FaceId face{};
  Vec3 position{};
};

/**
 * Connects two knife point indices in a stroke.
 */
struct KnifeStrokeSegment {
  std::uint32_t first_point = 0;
  std::uint32_t second_point = 0;
};

/**
 * Stores a typed union of public selection IDs.
 */
struct SelectionUnion {
  SelectionKind kind = SelectionKind::Object;
  ObjectId object{};
  std::vector<ObjectId> objects;
  std::vector<VertexId> vertices;
  std::vector<EdgeKey> edges;
  std::vector<FaceId> faces;
};

/**
 * Summarizes session selection state without exposing owner internals.
 */
struct SelectionSummary {
  std::vector<ObjectId> objects;
  std::size_t vertex_count = 0;
  std::size_t edge_count = 0;
  std::size_t face_count = 0;
  std::uint64_t selection_revision = 0;
};

} // namespace quader::modeling
