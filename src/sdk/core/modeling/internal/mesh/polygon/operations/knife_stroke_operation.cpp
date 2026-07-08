////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <mesh/polygon/document.hpp>
#include <mesh/polygon/poly_operation.hpp>
#include <mesh/polygon/poly_operation_descriptor.hpp>

#include <diagnostics/profile.hpp>

#include <mesh/polygon/internal/quader_poly_document_bridge_surface_helpers.hpp>
#include <mesh/polygon/internal/quader_poly_document_hull_plane_helpers.hpp>
#include <mesh/polygon/internal/quader_poly_document_knife_helpers.hpp>
#include <mesh/polygon/internal/quader_poly_document_topology_backend.hpp>
#include <mesh/polygon/internal/quader_poly_document_topology_helpers.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace quader_poly {

/**
 * Implements the Knife Stroke Operation modeling operation for the polygon document and mesh editing core.
 */
class KnifeStrokeOperation final : public PolyOperation {
public:
    KnifeStrokeOperation(std::span<const KnifePointTarget> points, std::span<const KnifeStrokeSegment> segments);

    [[nodiscard]] std::string_view id() const override { return poly_operation_descriptor(PolyOperationKey::KnifeStroke).id; }
    [[nodiscard]] std::string_view label() const override { return poly_operation_descriptor(PolyOperationKey::KnifeStroke).label; }
    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;

private:
    std::vector<KnifePointTarget> points_;
    std::vector<KnifeStrokeSegment> segments_;
};

using namespace document_internal;

KnifeStrokeOperation::KnifeStrokeOperation(std::span<const KnifePointTarget> points, std::span<const KnifeStrokeSegment> segments)
    : points_(points.begin(), points.end())
    , segments_(segments.begin(), segments.end())
{
}

OperationResult KnifeStrokeOperation::apply(Document& document, Selection& selection) const
{
    QDR_PROFILE_SCOPE("qdr_document.KnifeStrokeOperation.apply");
    KnifeStrokeCandidate candidate = build_knife_stroke_candidate(document, points_, segments_);
    if (!candidate.changed) {
        return { false, candidate.message.empty() ? std::string("Knife stroke could not be applied.") : candidate.message };
    }

    document = std::move(candidate.document);
    selection.clear();
    selection.mode = SelectionMode::Vertex;
    for (const ElementId vertex_id : candidate.selected_vertices) {
        if (find_vertex(document, vertex_id) != nullptr) {
            add_unique_id(selection.vertices, vertex_id);
        }
    }
    activate_last_selection(selection);
    return { true, {} };
}

OperationResult knife_stroke(Document& document, Selection& selection, std::span<const KnifePointTarget> points, std::span<const KnifeStrokeSegment> segments)
{
    QDR_PROFILE_SCOPE("qdr_document.knife_stroke");
    return KnifeStrokeOperation(points, segments).apply(document, selection);
}

} // namespace quader_poly
