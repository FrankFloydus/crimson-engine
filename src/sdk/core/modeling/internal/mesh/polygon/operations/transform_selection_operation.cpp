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
#include <mesh/polygon/internal/quader_poly_document_uv_helpers.hpp>

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
 * Implements the Transform Selection Operation modeling operation for the polygon document and mesh editing core.
 */
class TransformSelectionOperation final : public PolyOperation {
public:
    explicit TransformSelectionOperation(Transform3 transform);

    [[nodiscard]] std::string_view id() const override { return poly_operation_descriptor(PolyOperationKey::TransformSelection).id; }
    [[nodiscard]] std::string_view label() const override { return poly_operation_descriptor(PolyOperationKey::TransformSelection).label; }
    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;

private:
    Transform3 transform_;
};

TransformSelectionOperation::TransformSelectionOperation(Transform3 transform)
    : transform_(transform)
{
}

OperationResult TransformSelectionOperation::apply(Document& document, Selection& selection) const
{
    QDR_PROFILE_SCOPE("qdr_document.TransformSelectionOperation.apply");
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

        vertex->position = transform_point(transform_, vertex->position);
        changed = true;
    }
    return { changed, {} };
}

} // namespace

OperationResult transform_selection(Document& document, const Selection& selection, const Transform3& transform)
{
    QDR_PROFILE_SCOPE("qdr_document.transform_selection");
    Selection operation_selection = selection;
    return TransformSelectionOperation(transform).apply(document, operation_selection);
}

} // namespace quader_poly
