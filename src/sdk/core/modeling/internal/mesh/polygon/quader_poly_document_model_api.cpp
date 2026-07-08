////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <mesh/polygon/document.hpp>

#include <mesh/geometry/geometry.hpp>

#include <mesh/polygon/internal/quader_poly_document_constants.hpp>

#include <algorithm>
#include <utility>

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
    active_kind = ElementKind::Vertex;
    active_vertex = kInvalidElementId;
    active_edge = {};
    active_face = kInvalidElementId;
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

Edge make_edge(ElementId a, ElementId b)
{
    if (a > b) {
        std::swap(a, b);
    }
    return { a, b };
}

std::optional<float> edge_factor_from_position(const Document& document, Edge edge, quader::QVec3 position)
{
    edge = make_edge(edge.a, edge.b);
    const Vertex* a = find_vertex(document, edge.a);
    const Vertex* b = find_vertex(document, edge.b);
    if (a == nullptr || b == nullptr) {
        return std::nullopt;
    }

    const quader_geometry::QSegment3<float> segment { geometry_vec3(a->position), geometry_vec3(b->position) };
    if (quader_geometry::length_squared(segment.b - segment.a) <= kEpsilon) {
      return std::nullopt;
    }

    const quader_geometry::QClosestSegmentPoint3<float> closest =
        quader_geometry::closest_point_on_segment<float>(
            geometry_vec3(position), segment, kEpsilon);

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
    if (length_squared(ray.direction) <= kEpsilon) {
      return std::nullopt;
    }

    const quader_geometry::QSegmentRayClosestPoints<float> closest =
        quader_geometry::closest_points_segment_ray<float>(
            {geometry_vec3(a->position), geometry_vec3(b->position)},
            {geometry_vec3(ray.origin), geometry_vec3(ray.direction)},
            kEpsilon);
    if (!closest.valid) {
        return std::nullopt;
    }

    return edge_factor_from_position(document, edge, poly_vec3(closest.segment_point));
}

} // namespace quader_poly
