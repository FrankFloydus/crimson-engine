////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "io/qdr/qdr_polygon_document_codec.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace quader_io {
namespace {

using json = QdrJson;

json vec2_to_json(quader::QVec2 value) {
    return json{
            {"x", value.x},
            {"y", value.y},
    };
}

json vec3_to_json(quader::QVec3 value) {
    return json{
            {"x", value.x},
            {"y", value.y},
            {"z", value.z},
    };
}

json edge_to_json(quader_poly::Edge edge) {
    return json{
            {"a", edge.a},
            {"b", edge.b},
    };
}

std::string surface_shading_name(quader_poly::SurfaceShading shading) {
    switch (shading) {
    case quader_poly::SurfaceShading::flat:
        return "flat";
    case quader_poly::SurfaceShading::smooth:
        return "smooth";
    case quader_poly::SurfaceShading::authored:
        return "authored";
    }
    return "flat";
}

quader::QVec2 vec2_from_json(const json& value) {
    return {
            value.at("x").get<float>(),
            value.at("y").get<float>(),
    };
}

quader::QVec3 vec3_from_json(const json& value) {
    return {
            value.at("x").get<float>(),
            value.at("y").get<float>(),
            value.at("z").get<float>(),
    };
}

quader_poly::Edge edge_from_json(const json& value) {
    return quader_poly::make_edge(
            value.at("a").get<quader_poly::ElementId>(),
            value.at("b").get<quader_poly::ElementId>());
}

quader_poly::SurfaceShading parse_surface_shading(const std::string& value) {
    if (value == "flat") {
        return quader_poly::SurfaceShading::flat;
    }
    if (value == "smooth") {
        return quader_poly::SurfaceShading::smooth;
    }
    if (value == "authored") {
        return quader_poly::SurfaceShading::authored;
    }
    throw std::runtime_error("Unknown face normal shading: " + value);
}

bool contains_duplicate_ids(std::vector<quader_poly::ElementId> ids) {
    std::sort(ids.begin(), ids.end());
    return std::adjacent_find(ids.begin(), ids.end()) != ids.end();
}

bool face_references_existing_vertices(const quader_poly::Document& document, const quader_poly::Face& face) {
    return std::all_of(face.vertices.begin(), face.vertices.end(), [&document](quader_poly::ElementId id) {
        return quader_poly::find_vertex(document, id) != nullptr;
    });
}

} // namespace

QdrJson qdr_polygon_document_to_json(const quader_poly::Document& document) {
    json vertices = json::array();
    for (const quader_poly::Vertex& vertex : document.vertices) {
        vertices.push_back({
                {"id", vertex.id},
                {"position", vec3_to_json(vertex.position)},
        });
    }

    json faces = json::array();
    for (const quader_poly::Face& face : document.faces) {
        json uvs = json::array();
        for (quader::QVec2 uv : face.uvs) {
            uvs.push_back(vec2_to_json(uv));
        }

        faces.push_back({
                {"id", face.id},
                {"vertices", face.vertices},
                {"uvs", uvs},
                {"materialSlot", face.material_slot},
                {"normalShading", surface_shading_name(face.normal_shading)},
        });
    }

    const std::vector<quader_poly::Edge> document_edges = quader_poly::document_edges(document);
    json hard_normal_edges = json::array();
    std::set<std::pair<quader_poly::ElementId, quader_poly::ElementId>> serialized_hard_edges;
    for (quader_poly::Edge edge : document.hard_normal_edges) {
        edge = quader_poly::make_edge(edge.a, edge.b);
        if (std::find(document_edges.begin(), document_edges.end(), edge) == document_edges.end()) {
            continue;
        }
        if (!serialized_hard_edges.insert({edge.a, edge.b}).second) {
            continue;
        }
        hard_normal_edges.push_back(edge_to_json(edge));
    }

    return {
            {"nextVertexId", document.next_vertex_id},
            {"nextFaceId", document.next_face_id},
            {"vertices", vertices},
            {"faces", faces},
            {"hardNormalEdges", hard_normal_edges},
    };
}

quader_poly::Document qdr_polygon_document_from_json(const QdrJson& value) {
    quader_poly::Document document;
    document.next_vertex_id = value.at("nextVertexId").get<quader_poly::ElementId>();
    document.next_face_id = value.at("nextFaceId").get<quader_poly::ElementId>();

    std::set<quader_poly::ElementId> vertex_ids;
    quader_poly::ElementId max_vertex_id = 0;
    for (const json& vertex_json : value.at("vertices")) {
        quader_poly::Vertex vertex;
        vertex.id = vertex_json.at("id").get<quader_poly::ElementId>();
        vertex.position = vec3_from_json(vertex_json.at("position"));
        if (vertex.id == quader_poly::invalid_element_id || !vertex_ids.insert(vertex.id).second) {
            throw std::runtime_error("Document contains an invalid or duplicate vertex id.");
        }
        max_vertex_id = std::max(max_vertex_id, vertex.id);
        document.vertices.push_back(vertex);
    }

    std::set<quader_poly::ElementId> face_ids;
    quader_poly::ElementId max_face_id = 0;
    for (const json& face_json : value.at("faces")) {
        quader_poly::Face face;
        face.id = face_json.at("id").get<quader_poly::ElementId>();
        face.vertices = face_json.at("vertices").get<std::vector<quader_poly::ElementId>>();
        face.material_slot = face_json.value("materialSlot", 0U);
        face.normal_shading = parse_surface_shading(face_json.value("normalShading", std::string("flat")));
        for (const json& uv_json : face_json.at("uvs")) {
            face.uvs.push_back(vec2_from_json(uv_json));
        }
        if (face.id == quader_poly::invalid_element_id || !face_ids.insert(face.id).second) {
            throw std::runtime_error("Document contains an invalid or duplicate face id.");
        }
        if (face.vertices.size() < 3U) {
            throw std::runtime_error("Document contains a face with fewer than three vertices.");
        }
        if (contains_duplicate_ids(face.vertices)) {
            throw std::runtime_error("Document contains a face with duplicate vertex ids.");
        }
        if (!face.uvs.empty() && face.uvs.size() != face.vertices.size()) {
            throw std::runtime_error("Document contains a face whose UV count does not match its vertex count.");
        }
        document.faces.push_back(std::move(face));
        max_face_id = std::max(max_face_id, document.faces.back().id);
    }

    for (const quader_poly::Face& face : document.faces) {
        if (!face_references_existing_vertices(document, face)) {
            throw std::runtime_error("Document contains a face that references a missing vertex.");
        }
    }

    std::set<std::pair<quader_poly::ElementId, quader_poly::ElementId>> hard_normal_edges;
    if (const auto hard_edges_json = value.find("hardNormalEdges"); hard_edges_json != value.end()) {
        const std::vector<quader_poly::Edge> valid_edges = quader_poly::document_edges(document);
        for (const json& edge_json : *hard_edges_json) {
            quader_poly::Edge edge = edge_from_json(edge_json);
            if (std::find(valid_edges.begin(), valid_edges.end(), edge) == valid_edges.end()) {
                throw std::runtime_error("Document contains a hard normal edge that is not part of the mesh.");
            }
            if (!hard_normal_edges.insert({edge.a, edge.b}).second) {
                throw std::runtime_error("Document contains duplicate hard normal edges.");
            }
            document.hard_normal_edges.push_back(edge);
        }
    }

    document.next_vertex_id = std::max(document.next_vertex_id, max_vertex_id + 1U);
    document.next_face_id = std::max(document.next_face_id, max_face_id + 1U);
    return document;
}

} // namespace quader_io
