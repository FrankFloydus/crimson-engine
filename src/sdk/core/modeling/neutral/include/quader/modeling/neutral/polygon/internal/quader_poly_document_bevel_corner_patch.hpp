////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <quader/modeling/neutral/polygon/document_model.hpp>
#include <quader/modeling/neutral/polygon/document_operations.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace quader_poly::document_internal {

/** A sampled side of a corner patch, including its shared endpoint identities. */
struct BevelCornerSide {
    std::vector<ElementId> vertices;
    std::optional<float> profile;
};

/** Samples an endpoint-exact fillet around a prepared profile control point. */
[[nodiscard]] quader::QVec3 edge_bevel_profile_point(
    quader::QVec3 source_position,
    quader::QVec3 first_boundary,
    quader::QVec3 second_boundary,
    int segment,
    int segments,
    float profile,
    BevelProfileType profile_type);

/**
 * Fills a cyclic set of shared boundary chains with concentric quadrilateral
 * rings. Candidate geometry is disposable: degenerate profile samples may move,
 * retaining their identities. Generated cells are validated here; the owner
 * finalizes and publishes the candidate.
 */
[[nodiscard]] bool append_bevel_corner_patch(
    Document& document,
    std::span<const BevelCornerSide> sides,
    quader::QVec3 source_position,
    quader::QVec3 expected_normal,
    const EdgeBevelSettings& settings,
    std::uint32_t material_slot,
    std::vector<Face>& staged_faces,
    std::vector<ElementId>& generated_faces);

} // namespace quader_poly::document_internal
