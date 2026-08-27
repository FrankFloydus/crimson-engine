////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/neutral/polygon/document.hpp>

#include <diagnostics/profile.hpp>
#include <quader/modeling/neutral/geometry/geometry.hpp>

#include <quader/modeling/neutral/polygon/internal/quader_poly_document_constants.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_mesh_internal.hpp>
#include <quader/modeling/neutral/polygon/document_texture.hpp>

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <vector>

namespace quader_poly {
namespace {

using namespace document_internal;

quader_geometry::QVec3f geometry_vec3(const quader::QVec3& value)
{
    return { value.x, value.y, value.z };
}

quader::QVec3 poly_vec3(quader_geometry::QVec3f value)
{
    return { value.x, value.y, value.z };
}

quader_geometry::QTriangle3<float> geometry_triangle(const quader::QVec3& a, const quader::QVec3& b, const quader::QVec3& c)
{
    return { geometry_vec3(a), geometry_vec3(b), geometry_vec3(c) };
}

/**
 * Retains the standalone face_normal lookup behavior without building an
 * operation-wide index for one isolated face query.
 */
class DirectVertexLookup final {
public:
    explicit DirectVertexLookup(const Document& document)
        : document_(document)
    {
    }

    [[nodiscard]] const Vertex* find(ElementId id) const
    {
        return find_vertex(document_, id);
    }

private:
    const Document& document_;
};

template <typename VertexLookup>
quader::QVec3 face_normal_from_lookup(
    const Face& face,
    const VertexLookup& vertex_lookup)
{
    quader::QVec3 normal;
    if (face.vertices.size() < 3) {
        return { 0.0F, 1.0F, 0.0F };
    }

    for (std::size_t index = 0; index < face.vertices.size(); ++index) {
        const Vertex* current = vertex_lookup.find(face.vertices[index]);
        const Vertex* next =
            vertex_lookup.find(face.vertices[(index + 1) % face.vertices.size()]);
        if (current == nullptr || next == nullptr) {
            continue;
        }

        normal.x += (current->position.y - next->position.y) *
                    (current->position.z + next->position.z);
        normal.y += (current->position.z - next->position.z) *
                    (current->position.x + next->position.x);
        normal.z += (current->position.x - next->position.x) *
                    (current->position.y + next->position.y);
    }

    normal = normalize_or_zero(normal);
    if (length_squared(normal) > kEpsilon) {
        return normal;
    }

    const Vertex* a = vertex_lookup.find(face.vertices[0]);
    const Vertex* b = vertex_lookup.find(face.vertices[1]);
    const Vertex* c = vertex_lookup.find(face.vertices[2]);
    if (a == nullptr || b == nullptr || c == nullptr) {
        return { 0.0F, 1.0F, 0.0F };
    }

    normal = poly_vec3(quader_geometry::triangle_unit_normal(
        geometry_triangle(a->position, b->position, c->position)));
    return length_squared(normal) > kEpsilon
               ? normal
               : quader::QVec3 { 0.0F, 1.0F, 0.0F };
}

std::map<ElementId, quader::QVec3> build_smooth_normals(
    const Document& document,
    const VertexLookupIndex& vertex_lookup)
{
    QDR_PROFILE_SCOPE("qdr_document.build_smooth_normals");
    std::map<ElementId, quader::QVec3> normals;
    for (const Face& face : document.faces) {
        if (face.vertices.size() < 3) {
            continue;
        }

        const quader::QVec3 normal =
            face_normal_from_lookup(face, vertex_lookup);
        for (const ElementId vertex_id : face.vertices) {
            normals[vertex_id] += normal;
        }
    }

    for (auto& [vertex_id, normal] : normals) {
        static_cast<void>(vertex_id);
        normal = normalize_or_zero(normal);
    }
    return normals;
}

/**
 * Stores the Corner Key data contract used by the polygon document and mesh editing core.
 */
struct CornerKey {
  ElementId face_id = kInvalidElementId;
  ElementId vertex_id = kInvalidElementId;

  friend bool operator<(const CornerKey &left, const CornerKey &right) {
    if (left.face_id != right.face_id) {
      return left.face_id < right.face_id;
    }
    return left.vertex_id < right.vertex_id;
  }
};

/**
 * Represents an Edge Less value used by the polygon document and mesh editing core.
 */
struct EdgeLess {
    bool operator()(Edge left, Edge right) const
    {
        left = make_edge(left.a, left.b);
        right = make_edge(right.a, right.b);
        if (left.a != right.a) {
            return left.a < right.a;
        }
        return left.b < right.b;
    }
};

/**
 * Owns Corner Disjoint Set behavior within the polygon document and mesh editing core.
 */
class CornerDisjointSet {
public:
    void add(CornerKey key)
    {
        parents_.try_emplace(key, key);
    }

    CornerKey find(CornerKey key)
    {
        auto iterator = parents_.find(key);
        if (iterator == parents_.end()) {
            parents_.emplace(key, key);
            return key;
        }
        if (iterator->second.face_id == key.face_id && iterator->second.vertex_id == key.vertex_id) {
            return key;
        }

        iterator->second = find(iterator->second);
        return iterator->second;
    }

    void unite(CornerKey first, CornerKey second)
    {
        const CornerKey first_root = find(first);
        const CornerKey second_root = find(second);
        if (first_root.face_id == second_root.face_id && first_root.vertex_id == second_root.vertex_id) {
            return;
        }
        parents_[second_root] = first_root;
    }

private:
    std::map<CornerKey, CornerKey> parents_;
};

bool face_uses_smooth_normals(const Face& face, SurfaceShading shading)
{
    switch (shading) {
    case SurfaceShading::Flat:
      return false;
    case SurfaceShading::Smooth:
      return true;
    case SurfaceShading::Authored:
      return face.normal_shading == SurfaceShading::Smooth;
    }
    return false;
}

std::map<CornerKey, quader::QVec3> build_authored_smooth_corner_normals(
    const Document& document,
    SurfaceShading shading,
    const VertexLookupIndex& vertex_lookup)
{
    QDR_PROFILE_SCOPE("qdr_document.build_authored_smooth_corner_normals");
    CornerDisjointSet corners;
    std::map<ElementId, const Face*> smooth_faces;
    std::map<ElementId, quader::QVec3> face_normals;
    std::map<Edge, std::vector<ElementId>, EdgeLess> faces_by_edge;
    std::set<Edge, EdgeLess> hard_edges;

    for (Edge edge : document.hard_normal_edges) {
        hard_edges.insert(make_edge(edge.a, edge.b));
    }

    for (const Face& face : document.faces) {
        if (!face_uses_smooth_normals(face, shading) || face.vertices.size() < 3U) {
            continue;
        }

        smooth_faces[face.id] = &face;
        face_normals[face.id] =
            face_normal_from_lookup(face, vertex_lookup);
        for (ElementId vertex_id : face.vertices) {
            corners.add({ face.id, vertex_id });
        }
        for (std::size_t index = 0; index < face.vertices.size(); ++index) {
            const Edge edge = make_edge(face.vertices[index], face.vertices[(index + 1U) % face.vertices.size()]);
            faces_by_edge[edge].push_back(face.id);
        }
    }

    for (const auto& [edge, face_ids] : faces_by_edge) {
        if (hard_edges.find(edge) != hard_edges.end() || face_ids.size() < 2U) {
            continue;
        }

        for (std::size_t index = 1; index < face_ids.size(); ++index) {
            corners.unite({ face_ids.front(), edge.a }, { face_ids[index], edge.a });
            corners.unite({ face_ids.front(), edge.b }, { face_ids[index], edge.b });
        }
    }

    std::map<CornerKey, quader::QVec3> normal_sums_by_root;
    for (const auto& [face_id, face] : smooth_faces) {
        const quader::QVec3 normal = face_normals[face_id];
        for (ElementId vertex_id : face->vertices) {
            normal_sums_by_root[corners.find({ face_id, vertex_id })] += normal;
        }
    }

    std::map<CornerKey, quader::QVec3> normals;
    for (const auto& [face_id, face] : smooth_faces) {
        for (ElementId vertex_id : face->vertices) {
            const CornerKey key { face_id, vertex_id };
            const quader::QVec3 normal = normalize_or_zero(normal_sums_by_root[corners.find(key)]);
            normals[key] = length_squared(normal) > kEpsilon
                               ? normal
                               : face_normals[face_id];
        }
    }
    return normals;
}

} // namespace

quader::QVec3 face_normal(const Document& document, const Face& face)
{
    const DirectVertexLookup vertex_lookup(document);
    return face_normal_from_lookup(face, vertex_lookup);
}

void compile_document_into(const Document &document, CompiledMesh &mesh,
                           SurfaceShading shading)
{
    QDR_PROFILE_SCOPE("qdr_document.compile_document");
    mesh.vertices.clear();
    mesh.indices.clear();
    mesh.primitives.clear();
    const VertexLookupIndex vertex_lookup(document);

    // A compiled mesh owns one vertex per authored face corner and at most
    // (corner_count - 2) triangles per face. Reserve once per document and
    // reuse one corner-index buffer across faces. Transform reconciliation
    // compiles hundreds of small documents in one batch, so eliminating the
    // former two heap allocations per face is material to release latency.
    std::size_t compiled_vertex_capacity = 0U;
    std::size_t compiled_index_capacity = 0U;
    std::size_t maximum_face_size = 0U;
    for (const Face &face : document.faces) {
      compiled_vertex_capacity += face.vertices.size();
      maximum_face_size = std::max(maximum_face_size, face.vertices.size());
      if (face.vertices.size() >= 3U) {
        compiled_index_capacity += (face.vertices.size() - 2U) * 3U;
      }
    }
    mesh.vertices.reserve(compiled_vertex_capacity);
    mesh.indices.reserve(compiled_index_capacity);
    mesh.primitives.reserve(document.faces.size());

    const std::map<ElementId, quader::QVec3> smooth_normals =
        shading == SurfaceShading::Smooth
            ? build_smooth_normals(document, vertex_lookup)
            : std::map<ElementId, quader::QVec3>{};
    const std::map<CornerKey, quader::QVec3> authored_smooth_corner_normals =
        shading == SurfaceShading::Authored
            ? build_authored_smooth_corner_normals(
                  document, shading, vertex_lookup)
            : std::map<CornerKey, quader::QVec3>{};

    std::vector<std::uint32_t> compiled_face_vertices;
    compiled_face_vertices.reserve(maximum_face_size);
    std::vector<quader::QVec3> texture_face_positions;
    texture_face_positions.reserve(maximum_face_size);

    for (const Face& face : document.faces) {
        QDR_PROFILE_SCOPE("qdr_document.compile_document.face");
        const quader::QVec3 normal =
            face_normal_from_lookup(face, vertex_lookup);
        const std::vector<Triangle> triangles =
            triangulate_face_local_indices(
                document,
                face,
                vertex_lookup,
                normal);
        if (triangles.empty()) {
            continue;
        }

        compiled_face_vertices.clear();
        const int dropped_axis = dropped_axis_for_normal(normal);
        const bool use_loop_uvs = face_has_loop_uvs(face);
        const FaceUvBasis uv_basis =
            use_loop_uvs
                ? FaceUvBasis {}
                : generated_face_uv_basis(
                      document,
                      face,
                      normal,
                      vertex_lookup);
        texture_face_positions.clear();
        if (use_loop_uvs) {
          for (const ElementId id : face.vertices) {
            const Vertex *vertex = vertex_lookup.find(id);
            if (vertex != nullptr) {
              texture_face_positions.push_back(vertex->position);
            }
          }
        }
        const auto uv_carrier = face_texture_carrier(
            texture_face_positions,
            use_loop_uvs ? std::span<const quader::QVec2>(face.uvs)
                         : std::span<const quader::QVec2>{},
            normal, face.texture_transform);
        for (std::size_t local_index = 0; local_index < face.vertices.size(); ++local_index) {
            const ElementId vertex_id = face.vertices[local_index];
            const Vertex* vertex = vertex_lookup.find(vertex_id);
            if (vertex == nullptr) {
                compiled_face_vertices.clear();
                break;
            }

            compiled_face_vertices.push_back(static_cast<std::uint32_t>(mesh.vertices.size()));
            quader::QVec3 vertex_normal = normal;
            if (shading == SurfaceShading::Smooth) {
              const auto smooth_normal = smooth_normals.find(vertex_id);
              if (smooth_normal != smooth_normals.end() &&
                  length_squared(smooth_normal->second) > kEpsilon) {
                vertex_normal = smooth_normal->second;
              }
            } else if (face_uses_smooth_normals(face, shading)) {
              const auto smooth_normal =
                  authored_smooth_corner_normals.find({face.id, vertex_id});
              if (smooth_normal != authored_smooth_corner_normals.end() &&
                  length_squared(smooth_normal->second) > kEpsilon) {
                vertex_normal = smooth_normal->second;
              }
            }

            mesh.vertices.push_back({
                vertex->position,
                vertex_normal,
                apply_face_texture_transform(
                    use_loop_uvs ? face.uvs[local_index]
                                 : generated_uv_for_position(
                                       vertex->position, uv_basis, dropped_axis),
                    face.texture_transform),
                0xff9f9f9f,
                vertex_id,
                uv_carrier,
                normal,
            });
        }

        if (compiled_face_vertices.size() != face.vertices.size()) {
            continue;
        }

        const std::uint32_t index_offset = static_cast<std::uint32_t>(mesh.indices.size());
        for (const Triangle& triangle : triangles) {
            const Vertex* a =
                vertex_lookup.find(face.vertices[triangle.a]);
            const Vertex* b =
                vertex_lookup.find(face.vertices[triangle.b]);
            const Vertex* c =
                vertex_lookup.find(face.vertices[triangle.c]);
            if (a == nullptr || b == nullptr || c == nullptr) {
                continue;
            }

            const quader_geometry::QVec3f triangle_area_vector =
                quader_geometry::triangle_area_vector(geometry_triangle(a->position, b->position, c->position));
            mesh.indices.push_back(compiled_face_vertices[triangle.a]);
            if (quader_geometry::dot(triangle_area_vector, geometry_vec3(normal)) >= 0.0F) {
                mesh.indices.push_back(compiled_face_vertices[triangle.b]);
                mesh.indices.push_back(compiled_face_vertices[triangle.c]);
            } else {
                mesh.indices.push_back(compiled_face_vertices[triangle.c]);
                mesh.indices.push_back(compiled_face_vertices[triangle.b]);
            }
        }

        mesh.primitives.push_back({
            index_offset,
            static_cast<std::uint32_t>(mesh.indices.size() - index_offset),
            face.material_slot,
        });
    }
}

CompiledMesh compile_document(const Document& document, SurfaceShading shading)
{
    CompiledMesh mesh;
    compile_document_into(document, mesh, shading);
    return mesh;
}

} // namespace quader_poly
