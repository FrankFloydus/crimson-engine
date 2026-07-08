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
 * Implements the Knife Segment Operation modeling operation for the polygon document and mesh editing core.
 */
class KnifeSegmentOperation final : public PolyOperation {
public:
    KnifeSegmentOperation(KnifePointTarget previous, KnifePointTarget current);

    [[nodiscard]] std::string_view id() const override { return poly_operation_descriptor(PolyOperationKey::KnifeSegment).id; }
    [[nodiscard]] std::string_view label() const override { return poly_operation_descriptor(PolyOperationKey::KnifeSegment).label; }
    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;

private:
    KnifePointTarget previous_;
    KnifePointTarget current_;
};

using namespace document_internal;

KnifeSegmentOperation::KnifeSegmentOperation(KnifePointTarget previous, KnifePointTarget current)
    : previous_(previous)
    , current_(current)
{
}

OperationResult KnifeSegmentOperation::apply(Document& document, Selection& selection) const
{
    QDR_PROFILE_SCOPE("qdr_document.KnifeSegmentOperation.apply");
    KnifeSegmentCandidate candidate = build_knife_segment_candidate(document, previous_, current_);
    if (!candidate.changed) {
        return { false, candidate.message.empty() ? std::string("Knife segment could not be applied.") : candidate.message };
    }

    document = std::move(candidate.document);
    selection.clear();
    selection.mode = SelectionMode::Vertex;
    add_unique_id(selection.vertices, candidate.previous_vertex);
    add_unique_id(selection.vertices, candidate.current_vertex);
    activate_last_selection(selection);
    return { true, {} };
}

OperationResult knife_segment(Document& document, Selection& selection, const KnifePointTarget& previous, const KnifePointTarget& current)
{
    QDR_PROFILE_SCOPE("qdr_document.knife_segment");
    return KnifeSegmentOperation(previous, current).apply(document, selection);
}

} // namespace quader_poly
