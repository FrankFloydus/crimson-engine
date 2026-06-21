#include <quader_poly/document.h>

#include <quader_geometry/geometry.h>

#include "internal/quader_poly_document_constants.h"

#include <algorithm>
#include <utility>

namespace quader_poly {
namespace {

using namespace document_internal;

quader_geometry::Vec3f geometry_vec3(const Vec3& value)
{
    return { value.x, value.y, value.z };
}

Vec3 poly_vec3(quader_geometry::Vec3f value)
{
    return { value.x, value.y, value.z };
}

} // namespace

bool Selection::empty() const
{
    return vertices.empty() && edges.empty() && faces.empty();
}

void Selection::clear()
{
    vertices.clear();
    edges.clear();
    faces.clear();
    has_active = false;
    active_kind = ElementKind::vertex;
    active_vertex = invalid_element_id;
    active_edge = {};
    active_face = invalid_element_id;
}

bool FacePerimeterInfo::empty() const
{
    return edges.empty();
}

bool FacePerimeterInfo::has_open_edges() const
{
    return !open_edges.empty();
}

bool FacePerimeterInfo::has_closed_edges() const
{
    return !closed_edges.empty();
}

bool FacePerimeterInfo::has_nonmanifold_edges() const
{
    return !nonmanifold_edges.empty();
}

bool FacePerimeterInfo::has_only_open_edges() const
{
    return !edges.empty() && open_edges.size() == edges.size();
}

bool FacePerimeterInfo::has_only_closed_edges() const
{
    return !edges.empty() && closed_edges.size() == edges.size();
}

Vec3 operator+(const Vec3& left, const Vec3& right)
{
    return { left.x + right.x, left.y + right.y, left.z + right.z };
}

Vec3 operator-(const Vec3& left, const Vec3& right)
{
    return { left.x - right.x, left.y - right.y, left.z - right.z };
}

Vec3 operator*(const Vec3& value, float scalar)
{
    return { value.x * scalar, value.y * scalar, value.z * scalar };
}

Vec3 operator/(const Vec3& value, float scalar)
{
    return { value.x / scalar, value.y / scalar, value.z / scalar };
}

Vec3& operator+=(Vec3& left, const Vec3& right)
{
    left = left + right;
    return left;
}

float dot(const Vec3& left, const Vec3& right)
{
    return quader_geometry::dot(geometry_vec3(left), geometry_vec3(right));
}

Vec3 cross(const Vec3& left, const Vec3& right)
{
    return poly_vec3(quader_geometry::cross(geometry_vec3(left), geometry_vec3(right)));
}

float length_squared(const Vec3& value)
{
    return quader_geometry::length_squared(geometry_vec3(value));
}

float length(const Vec3& value)
{
    return quader_geometry::length(geometry_vec3(value));
}

Vec3 normalize_or_zero(const Vec3& value)
{
    return poly_vec3(quader_geometry::normalize_or_zero(geometry_vec3(value), epsilon));
}

Edge make_edge(ElementId a, ElementId b)
{
    if (a > b) {
        std::swap(a, b);
    }
    return { a, b };
}

std::optional<float> edge_factor_from_position(const Document& document, Edge edge, Vec3 position)
{
    edge = make_edge(edge.a, edge.b);
    const Vertex* a = find_vertex(document, edge.a);
    const Vertex* b = find_vertex(document, edge.b);
    if (a == nullptr || b == nullptr) {
        return std::nullopt;
    }

    const quader_geometry::Segment3<float> segment { geometry_vec3(a->position), geometry_vec3(b->position) };
    if (quader_geometry::length_squared(segment.b - segment.a) <= epsilon) {
        return std::nullopt;
    }

    const quader_geometry::ClosestSegmentPoint3<float> closest =
        quader_geometry::closest_point_on_segment<float>(
            geometry_vec3(position),
            segment,
            epsilon);

    return closest.segment_factor;
}

std::optional<float> edge_factor_from_ray(const Document& document, Edge edge, const Ray& local_ray)
{
    edge = make_edge(edge.a, edge.b);
    const Vertex* a = find_vertex(document, edge.a);
    const Vertex* b = find_vertex(document, edge.b);
    if (a == nullptr || b == nullptr) {
        return std::nullopt;
    }

    const Ray ray {
        local_ray.origin,
        normalize_or_zero(local_ray.direction),
    };
    if (length_squared(ray.direction) <= epsilon) {
        return std::nullopt;
    }

    const quader_geometry::SegmentRayClosestPoints<float> closest =
        quader_geometry::closest_points_segment_ray<float>(
            { geometry_vec3(a->position), geometry_vec3(b->position) },
            { geometry_vec3(ray.origin), geometry_vec3(ray.direction) },
            epsilon);
    if (!closest.valid) {
        return std::nullopt;
    }

    return edge_factor_from_position(document, edge, poly_vec3(closest.segment_point));
}

} // namespace quader_poly
