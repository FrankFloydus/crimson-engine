////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "math/types/qvec2.hpp"
#include "math/types/qvec3.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace quader_poly {

using ElementId = std::uint32_t;

inline constexpr ElementId kInvalidElementId = 0;

/**
 * Represents a Transform3 value used by the polygon document and mesh editing core.
 */
struct Transform3 {
    quader::QVec3 x_axis { 1.0F, 0.0F, 0.0F };
    quader::QVec3 y_axis { 0.0F, 1.0F, 0.0F };
    quader::QVec3 z_axis { 0.0F, 0.0F, 1.0F };
    quader::QVec3 origin { 0.0F, 0.0F, 0.0F };
};

/**
 * Represents a Ray value used by the polygon document and mesh editing core.
 */
struct Ray {
    quader::QVec3 origin;
    quader::QVec3 direction { 0.0F, 0.0F, 1.0F };
};

/**
 * Represents a Vertex value used by the polygon document and mesh editing core.
 */
struct Vertex {
  ElementId id = kInvalidElementId;
  quader::QVec3 position;
};

/**
 * Represents an Edge value used by the polygon document and mesh editing core.
 */
struct Edge {
  ElementId a = kInvalidElementId;
  ElementId b = kInvalidElementId;

  friend bool operator==(const Edge &left, const Edge &right) = default;
};

/**
 * Enumerates SurfaceShading values used by the modeling layer.
 */
enum class SurfaceShading {
  Flat,
  Smooth,
  Authored,
};

/**
 * Represents a Face value used by the polygon document and mesh editing core.
 */
struct Face {
  ElementId id = kInvalidElementId;
  std::vector<ElementId> vertices;
  std::vector<quader::QVec2> uvs;
  std::uint32_t material_slot = 0;
  SurfaceShading normal_shading = SurfaceShading::Flat;
};

/**
 * Represents a Document value used by the polygon document and mesh editing core.
 */
struct Document {
    std::vector<Vertex> vertices;
    std::vector<Face> faces;
    std::vector<Edge> hard_normal_edges;
    ElementId next_vertex_id = 1;
    ElementId next_face_id = 1;
};

/**
 * Represents a Cube Primitive Spec value used by the polygon document and mesh editing core.
 */
struct CubePrimitiveSpec {
    float width = 2.0F;
    float height = 2.0F;
    float depth = 2.0F;
    int width_segments = 1;
    int height_segments = 1;
    int depth_segments = 1;
};

[[nodiscard]] Edge make_edge(ElementId a, ElementId b);

[[nodiscard]] Document make_cube(const CubePrimitiveSpec& spec);
[[nodiscard]] ElementId add_vertex(Document& document, quader::QVec3 position);
[[nodiscard]] ElementId add_face(Document& document, std::span<const ElementId> vertices, std::uint32_t material_slot = 0);
[[nodiscard]] const Vertex* find_vertex(const Document& document, ElementId id);
[[nodiscard]] Vertex* find_vertex(Document& document, ElementId id);
[[nodiscard]] const Face* find_face(const Document& document, ElementId id);
[[nodiscard]] Face* find_face(Document& document, ElementId id);

} // namespace quader_poly
