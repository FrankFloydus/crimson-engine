////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <modeling/polygon/internal/quader_poly_document_mesh_internal.h>

#include <diagnostics/profile.h>
#include <geometry/geometry.h>

#include <modeling/polygon/internal/quader_poly_document_constants.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

namespace quader_poly::document_internal {
namespace {

/**
 * Represents a Triangulation Axis Candidate value used by the polygon document and mesh editing core.
 */
struct TriangulationAxisCandidate {
    int dropped_axis = 1;
    double projected_area = 0.0;
    bool has_duplicate_points = false;
};

quader_geometry::Vec3f geometry_vec3(const Vec3& value)
{
    return { value.x, value.y, value.z };
}

quader_geometry::Vec3d geometry_vec3d(const Vec3& value)
{
    return {
        static_cast<double>(value.x),
        static_cast<double>(value.y),
        static_cast<double>(value.z),
    };
}

Vec3 poly_vec3(quader_geometry::Vec3f value)
{
    return { value.x, value.y, value.z };
}

quader_geometry::Triangle3<float> geometry_triangle(const Vec3& a, const Vec3& b, const Vec3& c)
{
    return { geometry_vec3(a), geometry_vec3(b), geometry_vec3(c) };
}

quader_geometry::Vec3f geometry_triangle_area_vector(const Vec3& a, const Vec3& b, const Vec3& c)
{
    return quader_geometry::triangle_area_vector(geometry_triangle(a, b, c));
}

float triangle_cross_length_squared(const Vec3& a, const Vec3& b, const Vec3& c)
{
    const quader_geometry::Vec3f area_vector = geometry_triangle_area_vector(a, b, c);
    return quader_geometry::length_squared(area_vector) * 4.0F;
}

int dropped_axis_from_geometry_axis(quader_geometry::Axis axis)
{
    switch (axis) {
    case quader_geometry::Axis::x:
        return 0;
    case quader_geometry::Axis::y:
        return 1;
    default:
        return 2;
    }
}

quader_geometry::Axis geometry_axis_from_dropped_axis(int dropped_axis)
{
    switch (dropped_axis) {
    case 0:
        return quader_geometry::Axis::x;
    case 1:
        return quader_geometry::Axis::y;
    default:
        return quader_geometry::Axis::z;
    }
}

quader_geometry::Vec2d projected_point_for_axis(const Vec3& position, int dropped_axis)
{
    return quader_geometry::project_dominant_axis<double>(
        geometry_vec3d(position),
        geometry_axis_from_dropped_axis(dropped_axis));
}

bool projected_points_nearly_equal(quader_geometry::Vec2d left, quader_geometry::Vec2d right)
{
    constexpr double projected_epsilon = 0.000001;
    return quader_geometry::length_squared(left - right) <= projected_epsilon * projected_epsilon;
}

Vec3 project_vector_to_plane(const Vec3& vector, const Vec3& normal, const Vec3& origin)
{
    const quader_geometry::Vec3f geometry_origin = geometry_vec3(origin);
    const quader_geometry::Plane3<float> plane =
        quader_geometry::plane_from_point_normal<float>(geometry_origin, geometry_vec3(normal), epsilon);
    if (quader_geometry::length_squared(plane.normal) <= epsilon * epsilon) {
        return {};
    }
    return poly_vec3(
        quader_geometry::project_point_to_plane<float>(geometry_origin + geometry_vec3(vector), plane) - geometry_origin);
}

TriangulationAxisCandidate triangulation_axis_candidate(const Document& document, const Face& face, int dropped_axis)
{
    std::vector<quader_geometry::Vec2d> points;
    points.reserve(face.vertices.size());
    for (const ElementId vertex_id : face.vertices) {
        const Vertex* vertex = find_vertex(document, vertex_id);
        if (vertex == nullptr) {
            return { dropped_axis, 0.0, true };
        }
        points.push_back(projected_point_for_axis(vertex->position, dropped_axis));
    }

    bool has_duplicate_points = false;
    for (std::size_t index = 0; index < points.size() && !has_duplicate_points; ++index) {
        for (std::size_t other = index + 1U; other < points.size(); ++other) {
            if (projected_points_nearly_equal(points[index], points[other])) {
                has_duplicate_points = true;
                break;
            }
        }
    }

    return {
        dropped_axis,
        std::abs(quader_geometry::polygon_signed_area<double>(std::span<const quader_geometry::Vec2d>(points))),
        has_duplicate_points,
    };
}

std::vector<Triangle> triangulate_face_local_indices_for_axis(const Document& document, const Face& face, int dropped_axis)
{
    QDR_PROFILE_SCOPE("qdr_document.triangulate_face_local_indices_for_axis");
    std::vector<quader_geometry::Vec2d> projected_points;
    projected_points.reserve(face.vertices.size());

    for (const ElementId vertex_id : face.vertices) {
        const Vertex* vertex = find_vertex(document, vertex_id);
        if (vertex == nullptr) {
            return {};
        }
        projected_points.push_back(projected_point_for_axis(vertex->position, dropped_axis));
    }

    const double polygon_area = std::abs(
        quader_geometry::polygon_signed_area<double>(std::span<const quader_geometry::Vec2d>(projected_points)));
    if (polygon_area <= 0.000001) {
        return {};
    }

    const std::vector<quader_geometry::TriangleIndices> indices =
        quader_geometry::triangulate_projected_polygon(std::span<const quader_geometry::Vec2d>(projected_points));
    if (indices.empty()) {
        return {};
    }

    std::vector<Triangle> triangles;
    triangles.reserve(indices.size());
    double covered_area = 0.0;
    for (const quader_geometry::TriangleIndices& triangle_indices : indices) {
        if (triangle_indices[0] >= face.vertices.size() ||
            triangle_indices[1] >= face.vertices.size() ||
            triangle_indices[2] >= face.vertices.size()) {
            return {};
        }
        if (triangle_indices[0] == triangle_indices[1] ||
            triangle_indices[0] == triangle_indices[2] ||
            triangle_indices[1] == triangle_indices[2]) {
            continue;
        }

        const double triangle_area = quader_geometry::triangle_area_abs<double>(
            projected_points[triangle_indices[0]],
            projected_points[triangle_indices[1]],
            projected_points[triangle_indices[2]]);
        if (triangle_area <= 0.000001) {
            continue;
        }

        const Vertex* a = find_vertex(document, face.vertices[triangle_indices[0]]);
        const Vertex* b = find_vertex(document, face.vertices[triangle_indices[1]]);
        const Vertex* c = find_vertex(document, face.vertices[triangle_indices[2]]);
        if (a == nullptr || b == nullptr || c == nullptr) {
            return {};
        }

        if (triangle_cross_length_squared(a->position, b->position, c->position) <= face_area_score_epsilon) {
            continue;
        }

        triangles.push_back({ triangle_indices[0], triangle_indices[1], triangle_indices[2] });
        covered_area += triangle_area;
    }

    const double coverage_tolerance = std::max(0.000001, polygon_area * 0.001);
    if (triangles.empty() || covered_area + coverage_tolerance < polygon_area) {
        return {};
    }

    return triangles;
}

std::vector<Triangle> triangulate_quad_face_local_indices(const Document& document, const Face& face)
{
    const Vertex* a = find_vertex(document, face.vertices[0]);
    const Vertex* b = find_vertex(document, face.vertices[1]);
    const Vertex* c = find_vertex(document, face.vertices[2]);
    const Vertex* d = find_vertex(document, face.vertices[3]);
    if (a == nullptr || b == nullptr || c == nullptr || d == nullptr) {
        return {};
    }

    const auto triangle_is_valid = [&](const Triangle& triangle) {
        const std::array vertices {
            find_vertex(document, face.vertices[triangle.a]),
            find_vertex(document, face.vertices[triangle.b]),
            find_vertex(document, face.vertices[triangle.c]),
        };
        if (vertices[0] == nullptr || vertices[1] == nullptr || vertices[2] == nullptr) {
            return false;
        }
        return triangle_cross_length_squared(vertices[0]->position, vertices[1]->position, vertices[2]->position) >
            face_area_score_epsilon;
    };

    const std::vector<Triangle> first_third_diagonal { { 0U, 1U, 2U }, { 0U, 2U, 3U } };
    const std::vector<Triangle> second_fourth_diagonal { { 0U, 1U, 3U }, { 1U, 2U, 3U } };
    const quader_geometry::Vec3f first_area_vector = geometry_triangle_area_vector(a->position, b->position, c->position);
    const quader_geometry::Vec3f second_area_vector = geometry_triangle_area_vector(a->position, c->position, d->position);
    if (quader_geometry::length_squared(first_area_vector) * 4.0F > face_area_score_epsilon &&
        quader_geometry::length_squared(second_area_vector) * 4.0F > face_area_score_epsilon &&
        quader_geometry::dot(first_area_vector, second_area_vector) < 0.0F) {
        if (std::ranges::all_of(second_fourth_diagonal, triangle_is_valid)) {
            return second_fourth_diagonal;
        }
    }

    if (std::ranges::all_of(first_third_diagonal, triangle_is_valid)) {
        return first_third_diagonal;
    }
    if (std::ranges::all_of(second_fourth_diagonal, triangle_is_valid)) {
        return second_fourth_diagonal;
    }
    return {};
}

Vec3 quader_to_source_vector(const Vec3& vector)
{
    return { vector.x, -vector.z, vector.y };
}

Vec3 source_to_quader_vector(const Vec3& vector)
{
    return { vector.x, vector.z, -vector.y };
}

Vec2 source_projected_coord(const Vec3& source_position, int source_dropped_axis)
{
    float u = 0.0F;
    float v = 0.0F;
    switch (source_dropped_axis) {
    case 0:
        u = source_position.z;
        v = source_position.y;
        break;
    case 1:
        u = source_position.x;
        v = source_position.z;
        break;
    default:
        u = source_position.x;
        v = source_position.y;
        break;
    }

    return { u, v };
}

int source_dropped_axis_from_quader_dropped_axis(int dropped_axis)
{
    switch (dropped_axis) {
    case 0:
        return 0;
    case 1:
        return 2;
    default:
        return 1;
    }
}

int source_dropped_axis_for_normal(const Vec3& source_normal)
{
    return dropped_axis_from_geometry_axis(quader_geometry::dominant_axis(geometry_vec3(source_normal)));
}

Vec2 generated_planar_uv(const Vec3& position, int dropped_axis)
{
    const Vec2 coord = source_projected_coord(quader_to_source_vector(position), source_dropped_axis_from_quader_dropped_axis(dropped_axis));
    return {
        coord.x * generated_uv_tiles_per_world_unit,
        coord.y * generated_uv_tiles_per_world_unit,
    };
}

FaceUvBasis source_projected_uv_basis_for_normal(const Vec3& source_normal)
{
    FaceUvBasis basis;
    basis.origin = {};
    const int source_dropped_axis = source_dropped_axis_for_normal(source_normal);
    switch (source_dropped_axis) {
    case 0:
    {
        const float side_sign = source_normal.x >= 0.0F ? 1.0F : -1.0F;
        basis.u_axis = source_to_quader_vector({ 0.0F, side_sign, 0.0F });
        basis.v_axis = source_to_quader_vector({ 0.0F, 0.0F, -1.0F });
        break;
    }
    case 1:
    {
        const float side_sign = source_normal.y >= 0.0F ? 1.0F : -1.0F;
        basis.u_axis = source_to_quader_vector({ -side_sign, 0.0F, 0.0F });
        basis.v_axis = source_to_quader_vector({ 0.0F, 0.0F, -1.0F });
        break;
    }
    default:
    {
        const float vertical_sign = source_normal.z >= 0.0F ? 1.0F : -1.0F;
        basis.u_axis = source_to_quader_vector({ 1.0F, 0.0F, 0.0F });
        basis.v_axis = source_to_quader_vector({ 0.0F, -vertical_sign, 0.0F });
        break;
    }
    }
    basis.valid = true;
    return basis;
}

} // namespace

int dropped_axis_for_normal(const Vec3& normal)
{
    return dropped_axis_from_geometry_axis(quader_geometry::dominant_axis(geometry_vec3(normal)));
}

std::vector<Triangle> triangulate_face_local_indices(const Document& document, const Face& face)
{
    QDR_PROFILE_SCOPE("qdr_document.triangulate_face_local_indices");
    std::vector<Triangle> triangles;
    if (face.vertices.size() < 3) {
        return triangles;
    }

    if (face.vertices.size() == 3) {
        triangles.push_back({ 0U, 1U, 2U });
        return triangles;
    }
    if (face.vertices.size() == 4) {
        triangles = triangulate_quad_face_local_indices(document, face);
        if (!triangles.empty()) {
            return triangles;
        }
    }

    const Vec3 normal = face_normal(document, face);
    const int preferred_axis = dropped_axis_for_normal(normal);
    std::array<TriangulationAxisCandidate, 3> axes {
        triangulation_axis_candidate(document, face, 0),
        triangulation_axis_candidate(document, face, 1),
        triangulation_axis_candidate(document, face, 2),
    };
    std::ranges::sort(axes, [preferred_axis](const TriangulationAxisCandidate& left, const TriangulationAxisCandidate& right) {
        if (left.has_duplicate_points != right.has_duplicate_points) {
            return !left.has_duplicate_points;
        }
        if (std::abs(left.projected_area - right.projected_area) > 0.000001) {
            return left.projected_area > right.projected_area;
        }
        return left.dropped_axis == preferred_axis && right.dropped_axis != preferred_axis;
    });

    for (const TriangulationAxisCandidate& axis : axes) {
        if (axis.projected_area <= 0.000001) {
            continue;
        }
        triangles = triangulate_face_local_indices_for_axis(document, face, axis.dropped_axis);
        if (!triangles.empty()) {
            return triangles;
        }
    }

    triangles.clear();
    triangles.reserve(face.vertices.size() - 2U);
    for (std::uint32_t index = 1; index + 1U < face.vertices.size(); ++index) {
        const Vertex* a = find_vertex(document, face.vertices[0]);
        const Vertex* b = find_vertex(document, face.vertices[index]);
        const Vertex* c = find_vertex(document, face.vertices[index + 1U]);
        if (a == nullptr || b == nullptr || c == nullptr) {
            triangles.clear();
            return triangles;
        }
        if (triangle_cross_length_squared(a->position, b->position, c->position) <= face_area_score_epsilon) {
            triangles.clear();
            return triangles;
        }
        triangles.push_back({ 0U, index, index + 1U });
    }
    return triangles;
}

FaceUvBasis generated_face_uv_basis(const Document& document, const Face& face, const Vec3& normal)
{
    FaceUvBasis basis;
    if (face.vertices.empty()) {
        return basis;
    }

    if (find_vertex(document, face.vertices.front()) == nullptr) {
        return basis;
    }
    const Vec3 safe_normal = length_squared(normal) > epsilon ? normalize_or_zero(normal) : Vec3 { 0.0F, 1.0F, 0.0F };
    const Vec3 source_normal = normalize_or_zero(quader_to_source_vector(safe_normal));
    if (length_squared(source_normal) <= epsilon) {
        return basis;
    }
    return source_projected_uv_basis_for_normal(source_normal);
}

FaceUvBasis project_uv_basis_to_face_plane(const FaceUvBasis& generated_basis, const Vec3& normal, const Vec3& origin)
{
    FaceUvBasis basis;
    if (!generated_basis.valid) {
        return basis;
    }

    const Vec3 safe_normal = length_squared(normal) > epsilon ? normalize_or_zero(normal) : Vec3 { 0.0F, 1.0F, 0.0F };
    Vec3 u_axis = project_vector_to_plane(generated_basis.u_axis, safe_normal, origin);
    Vec3 v_reference = project_vector_to_plane(generated_basis.v_axis, safe_normal, origin);
    if (length_squared(u_axis) <= epsilon) {
        u_axis = v_reference;
        v_reference = project_vector_to_plane(generated_basis.u_axis, safe_normal, origin);
    }
    if (length_squared(u_axis) <= epsilon) {
        return basis;
    }
    u_axis = normalize_or_zero(u_axis);

    Vec3 v_axis = v_reference - u_axis * dot(v_reference, u_axis);
    if (length_squared(v_axis) <= epsilon) {
        v_axis = cross(safe_normal, u_axis);
        if (dot(v_axis, generated_basis.v_axis) < 0.0F) {
            v_axis = v_axis * -1.0F;
        }
    }
    if (length_squared(v_axis) <= epsilon) {
        return basis;
    }
    v_axis = normalize_or_zero(v_axis);

    basis.origin = origin;
    basis.u_axis = u_axis;
    basis.v_axis = v_axis;
    basis.valid = true;
    return basis;
}

FaceUvBasis face_local_uv_basis(const Document& document, const Face& face, const Vec3& normal)
{
    FaceUvBasis basis;
    if (face.vertices.size() < 2) {
        return basis;
    }

    basis = generated_face_uv_basis(document, face, normal);
    if (basis.valid) {
        const Vertex* origin = find_vertex(document, face.vertices.front());
        if (origin == nullptr) {
            basis.valid = false;
            return basis;
        }
        const FaceUvBasis face_basis = project_uv_basis_to_face_plane(basis, normal, origin->position);
        if (face_basis.valid) {
            return face_basis;
        }
    }

    const Vec3 safe_normal = length_squared(normal) > epsilon ? normalize_or_zero(normal) : Vec3 { 0.0F, 1.0F, 0.0F };
    float best_length_sq = 0.0F;
    bool found_edge = false;
    for (std::size_t index = 0; index < face.vertices.size(); ++index) {
        const Vertex* a = find_vertex(document, face.vertices[index]);
        const Vertex* b = find_vertex(document, face.vertices[(index + 1U) % face.vertices.size()]);
        if (a == nullptr || b == nullptr) {
            continue;
        }
        const Vec3 edge = b->position - a->position;
        const float edge_length_sq = length_squared(edge);
        if (edge_length_sq <= epsilon || edge_length_sq <= best_length_sq) {
            continue;
        }
        best_length_sq = edge_length_sq;
        basis.origin = a->position;
        basis.u_axis = normalize_or_zero(edge);
        found_edge = true;
    }
    if (!found_edge || length_squared(basis.u_axis) <= epsilon) {
        return basis;
    }

    basis.v_axis = normalize_or_zero(cross(safe_normal, basis.u_axis));
    if (length_squared(basis.v_axis) <= epsilon) {
        return basis;
    }
    basis.valid = true;
    return basis;
}

Vec2 generated_face_uv(const Vec3& position, const FaceUvBasis& basis)
{
    if (!basis.valid) {
        return {};
    }
    const Vec3 local = position - basis.origin;
    return {
        dot(local, basis.u_axis) * generated_uv_tiles_per_world_unit,
        dot(local, basis.v_axis) * generated_uv_tiles_per_world_unit,
    };
}

Vec2 generated_uv_for_position(const Vec3& position, const FaceUvBasis& basis, int dropped_axis)
{
    return basis.valid ? generated_face_uv(position, basis) : generated_planar_uv(position, dropped_axis);
}

bool face_has_loop_uvs(const Face& face)
{
    return face.uvs.size() == face.vertices.size();
}

bool assign_generated_face_uvs(Document& document, Face& face)
{
    face.uvs.clear();
    face.uvs.reserve(face.vertices.size());
    const Vec3 normal = face_normal(document, face);
    const int dropped_axis = dropped_axis_for_normal(normal);
    const FaceUvBasis basis = generated_face_uv_basis(document, face, normal);
    for (const ElementId vertex_id : face.vertices) {
        const Vertex* vertex = find_vertex(document, vertex_id);
        if (vertex == nullptr) {
            face.uvs.clear();
            return false;
        }
        face.uvs.push_back(generated_uv_for_position(vertex->position, basis, dropped_axis));
    }
    return face_has_loop_uvs(face);
}

bool assign_face_uvs_from_basis(Document& document, Face& face, const FaceUvBasis& basis)
{
    if (!basis.valid) {
        return false;
    }
    face.uvs.clear();
    face.uvs.reserve(face.vertices.size());
    for (const ElementId vertex_id : face.vertices) {
        const Vertex* vertex = find_vertex(document, vertex_id);
        if (vertex == nullptr) {
            face.uvs.clear();
            return false;
        }
        face.uvs.push_back(generated_face_uv(vertex->position, basis));
    }
    return face_has_loop_uvs(face);
}

bool assign_face_local_uvs(Document& document, Face& face)
{
    const Vec3 normal = face_normal(document, face);
    return assign_face_uvs_from_basis(document, face, face_local_uv_basis(document, face, normal));
}

Vec2 translation_uv_delta(const Document& document, const Face& face, const Vec3& delta)
{
    const Vec3 normal = face_normal(document, face);
    const FaceUvBasis basis = generated_face_uv_basis(document, face, normal);
    if (basis.valid) {
        return {
            dot(delta, basis.u_axis) * generated_uv_tiles_per_world_unit,
            dot(delta, basis.v_axis) * generated_uv_tiles_per_world_unit,
        };
    }
    return generated_planar_uv(delta, dropped_axis_for_normal(normal));
}

bool ensure_face_loop_uvs(Document& document, Face& face)
{
    return face_has_loop_uvs(face) || assign_generated_face_uvs(document, face);
}

} // namespace quader_poly::document_internal
