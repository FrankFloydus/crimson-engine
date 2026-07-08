////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <mesh/polygon/document_model.hpp>

#include <compare>
#include <utility>

namespace quader_poly {

/**
 * Strong vertex identity adapter for code that is migrating away from raw ElementId values.
 */
struct VertexId {
    ElementId value = kInvalidElementId;

    constexpr VertexId() = default;
    explicit constexpr VertexId(ElementId element_id)
        : value(element_id)
    {
    }

    friend constexpr auto operator<=>(const VertexId& left, const VertexId& right) = default;
};

/**
 * Strong face identity adapter for code that is migrating away from raw ElementId values.
 */
struct FaceId {
    ElementId value = kInvalidElementId;

    constexpr FaceId() = default;
    explicit constexpr FaceId(ElementId element_id)
        : value(element_id)
    {
    }

    friend constexpr auto operator<=>(const FaceId& left, const FaceId& right) = default;
};

/**
 * Canonical, undirected edge identity adapter backed by two vertex handles.
 */
struct EdgeKey {
    VertexId a;
    VertexId b;

    constexpr EdgeKey() = default;
    constexpr EdgeKey(VertexId first_vertex, VertexId second_vertex)
        : a(first_vertex)
        , b(second_vertex)
    {
    }

    friend constexpr auto operator<=>(const EdgeKey& left, const EdgeKey& right) = default;
};

inline constexpr VertexId kInvalidVertexId {};
inline constexpr FaceId kInvalidFaceId {};
inline constexpr EdgeKey kInvalidEdgeKey {};

[[nodiscard]] constexpr bool is_valid(VertexId vertex_id)
{
    return vertex_id.value != kInvalidElementId;
}

[[nodiscard]] constexpr bool is_valid(FaceId face_id)
{
    return face_id.value != kInvalidElementId;
}

[[nodiscard]] constexpr bool is_valid(EdgeKey edge_key)
{
    return is_valid(edge_key.a) && is_valid(edge_key.b) && edge_key.a != edge_key.b;
}

[[nodiscard]] constexpr ElementId to_element_id(VertexId vertex_id)
{
    return vertex_id.value;
}

[[nodiscard]] constexpr ElementId to_element_id(FaceId face_id)
{
    return face_id.value;
}

[[nodiscard]] constexpr VertexId as_vertex_id(ElementId element_id)
{
    return VertexId { element_id };
}

[[nodiscard]] constexpr FaceId as_face_id(ElementId element_id)
{
    return FaceId { element_id };
}

[[nodiscard]] constexpr EdgeKey as_edge_key(VertexId first_vertex, VertexId second_vertex)
{
    return first_vertex <= second_vertex ? EdgeKey { first_vertex, second_vertex }
                                         : EdgeKey { second_vertex, first_vertex };
}

[[nodiscard]] constexpr EdgeKey as_edge_key(ElementId first_vertex_id, ElementId second_vertex_id)
{
    return as_edge_key(as_vertex_id(first_vertex_id), as_vertex_id(second_vertex_id));
}

[[nodiscard]] constexpr EdgeKey as_edge_key(Edge edge)
{
    return as_edge_key(edge.a, edge.b);
}

[[nodiscard]] constexpr Edge to_edge(EdgeKey edge_key)
{
    return Edge { to_element_id(edge_key.a), to_element_id(edge_key.b) };
}

[[nodiscard]] constexpr std::pair<ElementId, ElementId> to_element_ids(EdgeKey edge_key)
{
    return { to_element_id(edge_key.a), to_element_id(edge_key.b) };
}

} // namespace quader_poly
