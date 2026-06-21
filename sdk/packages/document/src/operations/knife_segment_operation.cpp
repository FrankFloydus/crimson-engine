#include <quader_poly/document.h>
#include <quader_poly/poly_operation.h>

#include <quader_diagnostics/profile.h>

#include "../internal/quader_poly_document_bridge_surface_helpers.h"
#include "../internal/quader_poly_document_hull_plane_helpers.h"
#include "../internal/quader_poly_document_knife_helpers.h"
#include "../internal/quader_poly_document_topology_backend.h"
#include "../internal/quader_poly_document_topology_helpers.h"

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

class KnifeSegmentOperation final : public PolyOperation {
public:
    KnifeSegmentOperation(KnifePointTarget previous, KnifePointTarget current);

    [[nodiscard]] std::string_view id() const override { return "knife_segment"; }
    [[nodiscard]] std::string_view label() const override { return "Knife Segment"; }
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
    selection.mode = SelectionMode::vertex;
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
