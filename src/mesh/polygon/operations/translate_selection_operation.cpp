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

using namespace document_internal;

namespace {

/**
 * Implements the Translate Selection Operation modeling operation for the polygon document and mesh editing core.
 */
class TranslateSelectionOperation final : public PolyOperation {
public:
    explicit TranslateSelectionOperation(quader::QVec3 delta);

    [[nodiscard]] std::string_view id() const override { return poly_operation_descriptor(PolyOperationKey::TranslateSelection).id; }
    [[nodiscard]] std::string_view label() const override { return poly_operation_descriptor(PolyOperationKey::TranslateSelection).label; }
    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;

private:
    quader::QVec3 delta_;
};

TranslateSelectionOperation::TranslateSelectionOperation(quader::QVec3 delta)
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

OperationResult translate_selection(Document& document, const Selection& selection, quader::QVec3 delta)
{
    QDR_PROFILE_SCOPE("qdr_document.translate_selection");
    Selection operation_selection = selection;
    return TranslateSelectionOperation(delta).apply(document, operation_selection);
}

} // namespace quader_poly
