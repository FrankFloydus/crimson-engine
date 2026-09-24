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
 * The first usable incident face supplies Z. Its outgoing perimeter edge
 * supplies -Y, independently of neighboring vertex IDs.
 * The optional output retains the raw edge for affine world-space projection.
 */
[[nodiscard]] std::optional<quader_poly::Transform3>
component_vertex_local_orientation(
    const quader_poly::Document &document,
    quader_poly::ElementId vertex_id,
    const quader_poly::Transform3 &object_fallback = {},
    quader::QVec3 *directed_edge = nullptr);

/**
 * Builds the active edge's deterministic, object-local transform frame.
 *
 * The first usable incident face supplies Z and the edge's authored direction
 * supplies -Y. Reversing the requested endpoint order does not change the frame.
 * The optional output retains the raw edge for affine world-space projection.
 */
[[nodiscard]] std::optional<quader_poly::Transform3>
component_edge_local_orientation(
    const quader_poly::Document &document, quader_poly::Edge edge,
    const quader_poly::Transform3 &object_fallback = {},
    quader::QVec3 *directed_edge = nullptr);

/**
 * Builds the active face's deterministic, object-local transform frame.
 *
 * Z follows the authored face normal. X is Z crossed with the face texture
 * V gradient, so in-plane roll follows the face mapping, not perimeter order.
 */
[[nodiscard]] std::optional<quader_poly::Transform3>
component_face_local_orientation(
    const quader_poly::Document &document,
    quader_poly::ElementId face_id,
    const quader_poly::Transform3 &object_fallback = {});

} // namespace quader_editor_core
