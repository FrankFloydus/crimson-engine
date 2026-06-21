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

using namespace document_internal;

namespace {

class TranslateSelectionOperation final : public PolyOperation {
public:
    explicit TranslateSelectionOperation(Vec3 delta);

    [[nodiscard]] std::string_view id() const override { return "translate_selection"; }
    [[nodiscard]] std::string_view label() const override { return "Translate Selection"; }
    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;

private:
    Vec3 delta_;
};

TranslateSelectionOperation::TranslateSelectionOperation(Vec3 delta)
    : delta_(delta)
{
}

OperationResult TranslateSelectionOperation::apply(Document& document, Selection& selection) const
{
    QDR_PROFILE_SCOPE("qdr_document.TranslateSelectionOperation.apply");
    const std::vector<ElementId> ids = selected_vertex_ids(document, selection);
    if (ids.empty()) {
        return { false, "No polygon elements are selected." };
    }

    bool changed = false;
    for (const ElementId id : ids) {
        Vertex* vertex = find_vertex(document, id);
        if (vertex == nullptr) {
            continue;
        }

        vertex->position += delta_;
        changed = true;
    }
    return { changed, {} };
}

} // namespace

OperationResult translate_selection(Document& document, const Selection& selection, Vec3 delta)
{
    QDR_PROFILE_SCOPE("qdr_document.translate_selection");
    Selection operation_selection = selection;
    return TranslateSelectionOperation(delta).apply(document, operation_selection);
}

} // namespace quader_poly
