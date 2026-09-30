////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

// Source-only neutral modeling core contract shared by SDK and native owners.

#include <quader/modeling/neutral/polygon/document_selection.hpp>

#include <optional>
#include <vector>

namespace quader_poly {

/**
 * Stores the Face Perimeter Info data contract used by the polygon document and mesh editing core.
 */
struct FacePerimeterInfo {
    std::vector<Edge> edges;
    std::vector<Edge> open_edges;
    std::vector<Edge> closed_edges;
    std::vector<Edge> nonmanifold_edges;

    [[nodiscard]] bool empty() const;
    [[nodiscard]] bool has_open_edges() const;
    [[nodiscard]] bool has_closed_edges() const;
    [[nodiscard]] bool has_nonmanifold_edges() const;
    [[nodiscard]] bool has_only_open_edges() const;
    [[nodiscard]] bool has_only_closed_edges() const;
};

[[nodiscard]] std::vector<Edge> document_edges(const Document& document);
[[nodiscard]] std::vector<Edge> document_edges(const DocumentReadView& document);
[[nodiscard]] FacePerimeterInfo face_perimeter_info(const Document& document, ElementId face_id);
[[nodiscard]] FacePerimeterInfo selected_faces_perimeter_info(const Document& document, const Selection& selection);
[[nodiscard]] quader::QVec3 face_normal(const Document& document, const Face& face);
[[nodiscard]] quader::QVec3 face_normal(const DocumentReadView &document,
                                       const Face &face);
[[nodiscard]] std::optional<float> edge_factor_from_position(const Document& document, Edge edge, quader::QVec3 position);
[[nodiscard]] std::optional<float> edge_factor_from_ray(const Document& document, Edge edge, const Ray& ray);
[[nodiscard]] std::vector<Edge> edge_loop_edges(const Document& document, Edge seed_edge);
[[nodiscard]] std::vector<Edge> edge_loop_edges(const Document& document, Edge seed_edge, ElementId face_id);
[[nodiscard]] std::vector<Edge> edge_boundary_component_edges(const Document& document, Edge seed_edge);
[[nodiscard]] std::vector<Edge> edge_ring_edges(const Document& document, Edge seed_edge);
[[nodiscard]] bool has_same_geometry(const Document& left, const Document& right);

} // namespace quader_poly
