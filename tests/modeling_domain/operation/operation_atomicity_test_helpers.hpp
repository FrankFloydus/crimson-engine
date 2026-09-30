////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "operation_validation_test_helpers.hpp"

#include <bit>
#include <cstdint>
#include <limits>
#include <sstream>

namespace qdr_test {

/// Checks the edited face through public compilation without unrelated faces.
inline bool face_compiles_to_triangles(
    const quader_poly::Document& document, const quader_poly::Face& face)
{
    quader_poly::Document affected;
    affected.vertices = document.vertices;
    affected.faces = {face};
    return every_face_compiles_to_triangles(affected);
}

/// Captures every authored field in storage order, including nonfinite float bits.
inline std::string document_fingerprint(const quader_poly::Document& document)
{
    std::ostringstream stream;
    const auto append_float = [&stream](float value) {
        stream << std::bit_cast<std::uint32_t>(value) << ',';
    };
    stream << document.next_vertex_id << ',' << document.next_face_id << ';';
    stream << document.vertices.size() << ';';
    for (const auto& vertex : document.vertices) {
        stream << vertex.id << ',';
        append_float(vertex.position.x);
        append_float(vertex.position.y);
        append_float(vertex.position.z);
    }
    stream << ';' << document.faces.size() << ';';
    for (const auto& face : document.faces) {
        stream << face.id << ',' << face.material_slot << ','
               << static_cast<int>(face.normal_shading) << ','
               << face.vertices.size() << ',';
        for (const auto id : face.vertices) {
            stream << id << ',';
        }
        stream << face.uvs.size() << ',';
        for (const auto uv : face.uvs) {
            append_float(uv.x);
            append_float(uv.y);
        }
        append_float(face.texture_transform.scale.x);
        append_float(face.texture_transform.scale.y);
        append_float(face.texture_transform.shift_uv.x);
        append_float(face.texture_transform.shift_uv.y);
        append_float(face.texture_transform.rotation_degrees);
    }
    for (const auto* edges : {&document.hard_normal_edges, &document.soft_normal_edges}) {
        stream << ';' << edges->size() << ';';
        for (const auto edge : *edges) {
            stream << edge.a << ',' << edge.b << ',';
        }
    }
    return stream.str();
}

/// Captures selected and active identities without normalizing their order.
inline std::string selection_fingerprint(const quader_poly::Selection& selection)
{
    std::ostringstream stream;
    stream << static_cast<int>(selection.mode) << ',' << selection.has_active << ','
           << static_cast<int>(selection.active_kind) << ',' << selection.active_vertex << ','
           << selection.active_edge.a << ',' << selection.active_edge.b << ','
           << selection.active_face << ';' << selection.vertices.size() << ';';
    for (const auto id : selection.vertices) {
        stream << id << ',';
    }
    stream << ';' << selection.edges.size() << ';';
    for (const auto edge : selection.edges) {
        stream << edge.a << ',' << edge.b << ',';
    }
    stream << ';' << selection.faces.size() << ';';
    for (const auto id : selection.faces) {
        stream << id << ',';
    }
    return stream.str();
}

/// Adds finite collapsed authored topology with attributes and edge normal modes.
inline quader_poly::ElementId append_collapsed_unrelated_face(quader_poly::Document& document)
{
    using namespace quader_poly;
    const ElementId first = add_vertex(document, {10.0F, 0.0F, 0.0F});
    const ElementId second = add_vertex(document, {11.0F, 0.0F, 0.0F});
    const ElementId third = add_vertex(document, {12.0F, 0.0F, 0.0F});
    const ElementId face_id = add_face(document, std::array{first, second, third}, 17);
    Face& face = *find_face(document, face_id);
    face.uvs = {{0.25F, -0.5F}, {1.25F, 2.5F}, {-3.0F, 4.0F}};
    face.normal_shading = SurfaceShading::Smooth;
    face.texture_transform = {{-2.0F, 0.5F}, {0.125F, -0.25F}, 35.0F};
    document.hard_normal_edges.push_back(make_edge(first, second));
    document.soft_normal_edges.push_back(make_edge(second, third));
    return face_id;
}

/// Returns the added island and loose vertices for exact post-operation comparison.
inline quader_poly::Document append_unrelated_authored_geometry(quader_poly::Document& document)
{
    const std::size_t vertex_start = document.vertices.size();
    const std::size_t face_start = document.faces.size();
    const std::size_t hard_start = document.hard_normal_edges.size();
    const std::size_t soft_start = document.soft_normal_edges.size();
    static_cast<void>(append_collapsed_unrelated_face(document));
    static_cast<void>(quader_poly::add_vertex(document, {14.0F, -2.0F, 3.0F}));
    static_cast<void>(quader_poly::add_vertex(document, {14.0F, -2.0F, 3.0F}));
    quader_poly::Document island;
    island.vertices.assign(document.vertices.begin() + vertex_start, document.vertices.end());
    island.faces.assign(document.faces.begin() + face_start, document.faces.end());
    island.hard_normal_edges.assign(document.hard_normal_edges.begin() + hard_start, document.hard_normal_edges.end());
    island.soft_normal_edges.assign(document.soft_normal_edges.begin() + soft_start, document.soft_normal_edges.end());
    return island;
}

/// Distinguishes invalid authored data from preservable finite collapsed geometry.
enum class AuthoredInputFault {
    NonfiniteVertex,
    NonfiniteUv,
    MissingVertexReference,
    UvCountMismatch,
};

inline constexpr std::array kAuthoredInputFaults{
    AuthoredInputFault::NonfiniteVertex, AuthoredInputFault::NonfiniteUv,
    AuthoredInputFault::MissingVertexReference, AuthoredInputFault::UvCountMismatch};

/// Corrupts the unrelated island so successful local edits cannot erase bad input.
inline void corrupt_unrelated_authored_geometry(
    quader_poly::Document& document, AuthoredInputFault fault)
{
    switch (fault) {
    case AuthoredInputFault::NonfiniteVertex:
        document.vertices.back().position.x = std::numeric_limits<float>::infinity();
        break;
    case AuthoredInputFault::NonfiniteUv:
        document.faces.back().uvs.front().x = std::numeric_limits<float>::quiet_NaN();
        break;
    case AuthoredInputFault::MissingVertexReference:
        document.faces.back().vertices.back() = document.next_vertex_id + 5U;
        break;
    case AuthoredInputFault::UvCountMismatch:
        document.faces.back().uvs.pop_back();
        break;
    }
}

/// Compares the retained island in document storage order, including exact attributes.
inline void expect_unrelated_authored_geometry_preserved(
    const quader_poly::Document& document, const quader_poly::Document& island)
{
    quader_poly::Document retained;
    for (const auto& vertex : document.vertices) {
        if (quader_poly::find_vertex(island, vertex.id) != nullptr) {
            retained.vertices.push_back(vertex);
        }
    }
    for (const auto& face : document.faces) {
        if (quader_poly::find_face(island, face.id) != nullptr) {
            retained.faces.push_back(face);
        }
    }
    for (const auto edge : document.hard_normal_edges) {
        if (quader_poly::find_vertex(island, edge.a) != nullptr &&
            quader_poly::find_vertex(island, edge.b) != nullptr) {
            retained.hard_normal_edges.push_back(edge);
        }
    }
    for (const auto edge : document.soft_normal_edges) {
        if (quader_poly::find_vertex(island, edge.a) != nullptr &&
            quader_poly::find_vertex(island, edge.b) != nullptr) {
            retained.soft_normal_edges.push_back(edge);
        }
    }
    EXPECT_EQ(document_fingerprint(retained), document_fingerprint(island));
}

} // namespace qdr_test
