////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "quader_poly_document_mesh_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace quader_poly::document_internal {

std::vector<ElementId> unique_vertex_ids(std::vector<ElementId> ids);
std::optional<quader::QVec3> uv_basis_origin_for_anchor(const quader::QVec3& position, const quader::QVec2& uv, const quader::QVec3& u_axis, const quader::QVec3& v_axis);
bool uv_basis_varies_on_face(const Document& document, const Face& face, const FaceUvBasis& basis);
std::optional<FaceUvBasis> face_uv_basis_from_loop_uvs(const Document& document, const Face& face);
std::optional<FaceUvProjectionAssignment> face_uv_projection_assignment_from_source( const Document& document, const Face& face, const Document& source_document, const std::set<ElementId>& merge_vertex_ids, ElementId survivor_vertex_id);
bool assign_face_uvs_from_projection_assignment( Document& document, Face& face, const FaceUvProjectionAssignment& assignment);
std::optional<std::size_t> face_vertex_index(const Face& face, ElementId vertex_id);
std::optional<quader::QVec2> face_uv_for_vertex_id(const Face& face, ElementId vertex_id);
bool face_has_y_span(const Document& document, const Face& face);
bool face_is_merge_split_slope(const Document& document, const Face& face);
std::optional<Edge> shared_edge_between_faces(const Face& left, const Face& right);
quader::QVec2 face_uv_delta_for_edge(const Face& face, Edge edge);
float uv_delta_length_squared(quader::QVec2 delta);
const MergeFaceUvAssignment* uv_assignment_for_face( std::span<const MergeFaceUvAssignment> assignments, ElementId face_id);
void stitch_uv_axis_to_reference_seam(Face& target_face, const Face& reference_face, Edge shared_edge, bool stitch_u_axis);
void stitch_merge_split_uv_pair(const Face& reference_face, Face& target_face, Edge shared_edge);
void stitch_merge_split_uv_density(Document& document, std::span<const MergeFaceUvAssignment> assignments);
bool assign_face_uvs_from_source_projection( Document& document, Face& face, const Document& source_document, const std::set<ElementId>& merge_vertex_ids, ElementId survivor_vertex_id);
quader::QVec3 transform_point(const Transform3& transform, quader::QVec3 point);
} // namespace quader_poly::document_internal
