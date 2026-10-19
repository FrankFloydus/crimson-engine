////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

// Source-only neutral modeling core contract shared by SDK and native owners.

#include "math/types/qvec2.hpp"
#include "math/types/qvec3.hpp"

#include <array>
#include <cstdint>
#include <optional>
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
 * Describes how an authored edge controls normal sharing between its faces.
 * Default delegates to the mesh smoothing-angle rule, Hard always splits the
 * normals, and Soft always joins them. Explicit face shading controls the
 * output normal independently of the geometric fan calculation.
 */
enum class EdgeNormalMode {
  Default,
  Hard,
  Soft,
};

/**
 * Stores the persistent rotation, signed scale, and normalized shift applied
 * after a face's authored or generated base UVs.
 */
struct FaceTextureTransform {
  quader::QVec2 scale{1.0F, 1.0F};
  quader::QVec2 shift_uv{};
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
 * Represents a Face value used by the polygon document and mesh editing core.
 */
/**
 * Adds a live polar correction to corner UV interpolation. The two independent
 * local covectors project (position - center) into the polar plane. Row-major
 * coefficients map (radius, angle in radians) to base UV. The correction uses
 * exact features minus interpolated corner features, so authored corners stay
 * unchanged and no extra geometry is required.
 */
struct CircularUvInterpolation {
  quader::QVec3 center{};
  quader::QVec3 axis_u{1.0F, 0.0F, 0.0F};
  quader::QVec3 axis_v{0.0F, 0.0F, 1.0F};
  std::array<float, 4> uv_from_polar{};

  friend bool operator==(const CircularUvInterpolation &left,
                         const CircularUvInterpolation &right) noexcept {
    return left.center.x == right.center.x && left.center.y == right.center.y &&
           left.center.z == right.center.z && left.axis_u.x == right.axis_u.x &&
           left.axis_u.y == right.axis_u.y && left.axis_u.z == right.axis_u.z &&
           left.axis_v.x == right.axis_v.x && left.axis_v.y == right.axis_v.y &&
           left.axis_v.z == right.axis_v.z &&
           left.uv_from_polar == right.uv_from_polar;
  }
};

/// Retains one authored polygon loop, corner UVs and optional live polar sampling.
struct Face {
  ElementId id = kInvalidElementId;
  std::vector<ElementId> vertices;
  std::vector<quader::QVec2> uvs;
  std::uint32_t material_slot = 0;
  SurfaceShading normal_shading = SurfaceShading::Authored;
  FaceTextureTransform texture_transform{};
  std::optional<CircularUvInterpolation> circular_uv_interpolation;
};

/**
 * Represents a Document value used by the polygon document and mesh editing core.
 */
struct Document {
    std::vector<Vertex> vertices;
    std::vector<Face> faces;
    std::vector<Edge> hard_normal_edges;
    std::vector<Edge> soft_normal_edges;
    ElementId next_vertex_id = 1;
    ElementId next_face_id = 1;
};

/** Non-owning immutable geometry with independently retained topology and vertices. */
struct DocumentReadView {
  std::span<const Vertex> vertices;
  std::span<const Face> faces;
  std::span<const Edge> hard_normal_edges;
  std::span<const Edge> soft_normal_edges;
  ElementId next_vertex_id = 1;
  ElementId next_face_id = 1;
};

/** Borrows a complete document without copying its authored arrays. */
[[nodiscard]] inline DocumentReadView document_read_view(const Document &document) {
  return {.vertices = document.vertices,
          .faces = document.faces,
          .hard_normal_edges = document.hard_normal_edges,
          .soft_normal_edges = document.soft_normal_edges,
          .next_vertex_id = document.next_vertex_id,
          .next_face_id = document.next_face_id};
}

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
    bool flip_faces = false;
};

/**
 * Represents a Cylinder Primitive Spec value used by the polygon document and mesh editing core.
 */
struct CylinderPrimitiveSpec {
    float width = 2.0F;
    float height = 2.0F;
    float depth = 2.0F;
    int side_count = 32;
    bool capped = true;
    bool flip_faces = false;
};

/**
 * Represents a Cone Primitive Spec value used by the polygon document and mesh editing core.
 */
struct ConePrimitiveSpec {
    float width = 2.0F;
    float height = 2.0F;
    float depth = 2.0F;
    int side_count = 32;
    bool capped = true;
    bool flip_faces = false;
};

/**
 * Selects latitude/longitude or subdivided-cube sphere topology.
 */
enum class SphereType {
    Uv = 0,
    Quad = 1,
};

/**
 * Represents a Sphere Primitive Spec value used by the polygon document and mesh editing core.
 */
struct SpherePrimitiveSpec {
    float width = 2.0F;
    float height = 2.0F;
    float depth = 2.0F;
    int radial_segments = 32;
    int rings = 16;
    bool hemisphere = false;
    bool flip_faces = false;
    SphereType type = SphereType::Uv;
    int subdivision = 3;
};

/**
 * Represents a Torus Primitive Spec value used by the polygon document and mesh editing core.
 *
 * The width, height, and depth define the outer bounds. tube_radius_ratio is the tube radius as a
 * fraction of the outer X/Z radii and must remain below one half to preserve the center hole.
 */
struct TorusPrimitiveSpec {
    float width = 2.0F;
    float height = 0.4F;
    float depth = 2.0F;
    int major_segments = 32;
    int minor_segments = 12;
    float tube_radius_ratio = 0.2F;
    bool flip_faces = false;
};

[[nodiscard]] Edge make_edge(ElementId a, ElementId b);

[[nodiscard]] Document make_cube(const CubePrimitiveSpec& spec);
[[nodiscard]] Document make_cylinder(const CylinderPrimitiveSpec& spec);
[[nodiscard]] Document make_cone(const ConePrimitiveSpec& spec);
[[nodiscard]] Document make_sphere(const SpherePrimitiveSpec& spec);
[[nodiscard]] Document make_torus(const TorusPrimitiveSpec& spec);
[[nodiscard]] ElementId add_vertex(Document& document, quader::QVec3 position);
[[nodiscard]] ElementId add_face(Document& document, std::span<const ElementId> vertices, std::uint32_t material_slot = 0);
[[nodiscard]] const Vertex* find_vertex(const Document& document, ElementId id);
[[nodiscard]] Vertex* find_vertex(Document& document, ElementId id);
[[nodiscard]] const Face* find_face(const Document& document, ElementId id);
[[nodiscard]] Face* find_face(Document& document, ElementId id);

} // namespace quader_poly
