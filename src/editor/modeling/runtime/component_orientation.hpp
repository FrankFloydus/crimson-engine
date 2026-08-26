////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <quader/modeling/neutral/polygon/document_model.hpp>

#include <optional>

namespace quader_editor_core {

/**
 * Builds the active vertex's deterministic, object-local transform frame.
 *
 * The Z axis is the sum of incident face normals. The X axis points toward the
 * lowest-ID usable neighboring vertex after projection onto the tangent plane.
 */
[[nodiscard]] std::optional<quader_poly::Transform3>
component_vertex_local_orientation(
    const quader_poly::Document &document,
    quader_poly::ElementId vertex_id,
    const quader_poly::Transform3 &object_fallback = {});

/**
 * Builds the active edge's deterministic, object-local transform frame.
 *
 * X follows the edge's canonical endpoint ordering and Z is the sum of its
 * incident face normals.
 */
[[nodiscard]] std::optional<quader_poly::Transform3>
component_edge_local_orientation(
    const quader_poly::Document &document, quader_poly::Edge edge,
    const quader_poly::Transform3 &object_fallback = {});

/**
 * Builds the active face's deterministic, object-local transform frame.
 *
 * Z follows the authored face normal and X follows the first nondegenerate
 * perimeter edge in authored winding order.
 */
[[nodiscard]] std::optional<quader_poly::Transform3>
component_face_local_orientation(
    const quader_poly::Document &document,
    quader_poly::ElementId face_id,
    const quader_poly::Transform3 &object_fallback = {});

} // namespace quader_editor_core
