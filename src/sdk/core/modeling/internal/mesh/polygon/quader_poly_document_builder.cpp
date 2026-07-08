////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <mesh/polygon/document_builder.hpp>

#include <mesh/polygon/internal/quader_poly_document_constants.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace quader_poly {
namespace {

Selection all_vertex_selection(const Document& document)
{
    Selection selection;
    selection.mode = SelectionMode::Vertex;
    selection.vertices.reserve(document.vertices.size());
    for (const Vertex& vertex : document.vertices) {
        selection.vertices.push_back(vertex.id);
    }
    return selection;
}

quader::QVec3 basis_transform_point(const Transform3& transform, quader::QVec3 point)
{
    return (transform.x_axis * point.x) +
        (transform.y_axis * point.y) +
        (transform.z_axis * point.z);
}

Transform3 transform_around_pivot(const Transform3& transform, quader::QVec3 pivot)
{
    Transform3 result = transform;
    result.origin = transform.origin + pivot - basis_transform_point(transform, pivot);
    return result;
}

Transform3 scale_transform(quader::QVec3 scale, quader::QVec3 pivot)
{
    Transform3 transform;
    transform.x_axis = { scale.x, 0.0F, 0.0F };
    transform.y_axis = { 0.0F, scale.y, 0.0F };
    transform.z_axis = { 0.0F, 0.0F, scale.z };
    return transform_around_pivot(transform, pivot);
}

bool finite_vec3(quader::QVec3 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

quader::QVec3 average_points(std::span<const quader::QVec3> points)
{
    quader::QVec3 center;
    for (quader::QVec3 point : points) {
        center += point;
    }
    return points.empty() ? center : center / static_cast<float>(points.size());
}

quader::QVec3 oriented_quad_area_vector(std::span<const quader::QVec3> corners, const std::array<int, 4>& indices)
{
    quader::QVec3 area;
    for (std::size_t index = 0; index < indices.size(); ++index) {
        const quader::QVec3 current = corners[static_cast<std::size_t>(indices[index])];
        const quader::QVec3 next = corners[static_cast<std::size_t>(indices[(index + 1U) % indices.size()])];
        area += cross(current, next);
    }
    return area;
}

bool add_oriented_box_quad(
    Document& document,
    const std::array<ElementId, 8>& vertex_ids,
    std::span<const quader::QVec3> corners,
    std::array<int, 4> indices,
    quader::QVec3 box_center,
    std::uint32_t material_slot)
{
    const std::array<quader::QVec3, 4> face_points {
        corners[static_cast<std::size_t>(indices[0])],
        corners[static_cast<std::size_t>(indices[1])],
        corners[static_cast<std::size_t>(indices[2])],
        corners[static_cast<std::size_t>(indices[3])],
    };
    const quader::QVec3 face_center = average_points(face_points);
    const quader::QVec3 outward = face_center - box_center;
    const quader::QVec3 area = oriented_quad_area_vector(corners, indices);
    if (length_squared(area) <=
            document_internal::kEpsilon * document_internal::kEpsilon ||
        length_squared(outward) <=
            document_internal::kEpsilon * document_internal::kEpsilon) {
      return false;
    }
    if (dot(area, outward) < 0.0F) {
        std::reverse(indices.begin(), indices.end());
    }

    const std::array face_vertices {
        vertex_ids[static_cast<std::size_t>(indices[0])],
        vertex_ids[static_cast<std::size_t>(indices[1])],
        vertex_ids[static_cast<std::size_t>(indices[2])],
        vertex_ids[static_cast<std::size_t>(indices[3])],
    };
    return add_face(document, face_vertices, material_slot) !=
           kInvalidElementId;
}

std::optional<std::uint32_t> no_material_override()
{
    return std::nullopt;
}

void append_document(
    Document& target,
    std::vector<ElementId>& target_vertex_ids,
    const Document& source,
    std::optional<std::uint32_t> material_slot_override = no_material_override())
{
    std::map<ElementId, ElementId> vertex_id_map;
    for (const Vertex& vertex : source.vertices) {
        const ElementId id = add_vertex(target, vertex.position);
        vertex_id_map.emplace(vertex.id, id);
        target_vertex_ids.push_back(id);
    }

    for (const Face& face : source.faces) {
        std::vector<ElementId> face_vertices;
        face_vertices.reserve(face.vertices.size());
        bool valid = true;
        for (const ElementId vertex_id : face.vertices) {
            const auto mapped = vertex_id_map.find(vertex_id);
            if (mapped == vertex_id_map.end()) {
                valid = false;
                break;
            }
            face_vertices.push_back(mapped->second);
        }
        if (!valid) {
            continue;
        }

        const ElementId face_id = add_face(
            target,
            face_vertices,
            material_slot_override.value_or(face.material_slot));
        Face* appended_face = find_face(target, face_id);
        if (appended_face == nullptr) {
            continue;
        }
        appended_face->uvs = face.uvs;
        appended_face->normal_shading = face.normal_shading;
    }

    for (const Edge& edge : source.hard_normal_edges) {
        const auto a = vertex_id_map.find(edge.a);
        const auto b = vertex_id_map.find(edge.b);
        if (a != vertex_id_map.end() && b != vertex_id_map.end()) {
            target.hard_normal_edges.push_back(make_edge(a->second, b->second));
        }
    }
}

bool resolve_vertex_refs(
    std::span<const DocumentBuilder::VertexRef> refs,
    const std::vector<ElementId>& vertex_ids,
    std::vector<ElementId>& resolved)
{
    resolved.clear();
    resolved.reserve(refs.size());
    for (const DocumentBuilder::VertexRef ref : refs) {
        if (ref.index >= vertex_ids.size()) {
            return false;
        }
        const ElementId vertex_id = vertex_ids[ref.index];
        if (vertex_id == kInvalidElementId) {
          return false;
        }
        resolved.push_back(vertex_id);
    }
    return true;
}

bool resolve_vertex_indices(
    std::span<const std::size_t> indices,
    const std::vector<ElementId>& vertex_ids,
    std::vector<ElementId>& resolved)
{
    resolved.clear();
    resolved.reserve(indices.size());
    for (const std::size_t index : indices) {
        if (index >= vertex_ids.size()) {
            return false;
        }
        const ElementId vertex_id = vertex_ids[index];
        if (vertex_id == kInvalidElementId) {
          return false;
        }
        resolved.push_back(vertex_id);
    }
    return true;
}

} // namespace

DocumentBuilder::VertexRef DocumentBuilder::vertex(quader::QVec3 position)
{
    const ElementId id = add_vertex(document_, position);
    vertex_ids_.push_back(id);
    return {vertex_ids_.size() - 1U};
}

DocumentBuilder& DocumentBuilder::vertices(std::span<const quader::QVec3> positions)
{
    for (const quader::QVec3 position : positions) {
        static_cast<void>(vertex(position));
    }
    return *this;
}

DocumentBuilder &DocumentBuilder::face(std::span<const VertexRef> vertices,
                                       std::uint32_t material_slot) {
  std::vector<ElementId> resolved;
  if (resolve_vertex_refs(vertices, vertex_ids_, resolved)) {
    static_cast<void>(add_face(document_, resolved, material_slot));
  }
  return *this;
}

DocumentBuilder &
DocumentBuilder::face_by_index(std::span<const std::size_t> vertices,
                               std::uint32_t material_slot) {
  std::vector<ElementId> resolved;
  if (resolve_vertex_indices(vertices, vertex_ids_, resolved)) {
    static_cast<void>(add_face(document_, resolved, material_slot));
  }
  return *this;
}

DocumentBuilder &DocumentBuilder::quad(VertexRef a, VertexRef b, VertexRef c,
                                       VertexRef d,
                                       std::uint32_t material_slot) {
  const std::array vertices{a, b, c, d};
  return face(vertices, material_slot);
}

DocumentBuilder &DocumentBuilder::triangle(VertexRef a, VertexRef b,
                                           VertexRef c,
                                           std::uint32_t material_slot) {
  const std::array vertices{a, b, c};
  return face(vertices, material_slot);
}

DocumentBuilder &DocumentBuilder::cube(quader::QVec3 min_bounds,
                                       quader::QVec3 max_bounds,
                                       std::uint32_t material_slot) {
  if (!finite_vec3(min_bounds) || !finite_vec3(max_bounds)) {
    return *this;
  }
  const float min_x = std::min(min_bounds.x, max_bounds.x);
  const float min_y = std::min(min_bounds.y, max_bounds.y);
  const float min_z = std::min(min_bounds.z, max_bounds.z);
  const float max_x = std::max(min_bounds.x, max_bounds.x);
  const float max_y = std::max(min_bounds.y, max_bounds.y);
  const float max_z = std::max(min_bounds.z, max_bounds.z);
  const float width = max_x - min_x;
  const float height = max_y - min_y;
  const float depth = max_z - min_z;
  if (width <= document_internal::kEpsilon ||
      height <= document_internal::kEpsilon ||
      depth <= document_internal::kEpsilon) {
    return *this;
  }

  CubePrimitiveSpec spec;
  spec.width = width;
  spec.height = height;
  spec.depth = depth;

  Document cube_document = make_cube(spec);
  const quader::QVec3 center{
      (min_x + max_x) * 0.5F,
      (min_y + max_y) * 0.5F,
      (min_z + max_z) * 0.5F,
  };
  static_cast<void>(translate_selection(
      cube_document, all_vertex_selection(cube_document), center));
  append_document(document_, vertex_ids_, cube_document, material_slot);
  return *this;
}

DocumentBuilder &
DocumentBuilder::box_from_corners(std::span<const quader::QVec3> corners,
                                  std::uint32_t material_slot) {
  if (corners.size() != 8U) {
    return *this;
  }
  for (const quader::QVec3 corner : corners) {
    if (!finite_vec3(corner)) {
      return *this;
    }
  }
  for (std::size_t left = 0; left < corners.size(); ++left) {
    for (std::size_t right = left + 1U; right < corners.size(); ++right) {
      if (length_squared(corners[left] - corners[right]) <=
          document_internal::kEpsilon * document_internal::kEpsilon) {
        return *this;
      }
    }
  }

  Document box_document;
  std::array<ElementId, 8> vertex_ids{};
  for (std::size_t index = 0; index < corners.size(); ++index) {
    vertex_ids[index] = add_vertex(box_document, corners[index]);
  }

  const quader::QVec3 center = average_points(corners);
  const std::array<std::array<int, 4>, 6> faces{{
      {0, 1, 2, 3},
      {4, 5, 6, 7},
      {0, 4, 5, 1},
      {1, 5, 6, 2},
      {2, 6, 7, 3},
      {3, 7, 4, 0},
  }};
  for (const std::array<int, 4> &face : faces) {
    if (!add_oriented_box_quad(box_document, vertex_ids, corners, face, center,
                               material_slot)) {
      return *this;
    }
  }
  if (compile_document(box_document).indices.empty()) {
    return *this;
  }

  append_document(document_, vertex_ids_, box_document);
  return *this;
}

DocumentBuilder& DocumentBuilder::translate(quader::QVec3 delta)
{
    static_cast<void>(translate_selection(document_, all_vertex_selection(document_), delta));
    return *this;
}

DocumentBuilder& DocumentBuilder::scale(quader::QVec3 scale, quader::QVec3 pivot)
{
    static_cast<void>(transform_selection(document_, all_vertex_selection(document_), scale_transform(scale, pivot)));
    return *this;
}

DocumentBuilder& DocumentBuilder::transform(const Transform3& transform, quader::QVec3 pivot)
{
    static_cast<void>(transform_selection(document_, all_vertex_selection(document_), transform_around_pivot(transform, pivot)));
    return *this;
}

DocumentBuilder &
DocumentBuilder::assign_material_slot(std::uint32_t material_slot) {
  std::vector<ElementId> face_ids;
  face_ids.reserve(document_.faces.size());
  for (const Face &face : document_.faces) {
    face_ids.push_back(face.id);
  }
  for (const ElementId face_id : face_ids) {
    static_cast<void>(
        assign_face_material_slot(document_, face_id, material_slot));
  }
  return *this;
}

DocumentBuilder& DocumentBuilder::clear()
{
    document_ = {};
    vertex_ids_.clear();
    return *this;
}

const Document& DocumentBuilder::preview() const
{
    return document_;
}

Document DocumentBuilder::build() const
{
    return document_;
}

Document DocumentBuilder::take()
{
    Document document = std::move(document_);
    document_ = {};
    vertex_ids_.clear();
    return document;
}

} // namespace quader_poly
