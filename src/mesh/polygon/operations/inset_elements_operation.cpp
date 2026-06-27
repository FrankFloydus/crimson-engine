////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <mesh/polygon/poly_operation.hpp>
#include <mesh/polygon/poly_operation_descriptor.hpp>

namespace quader_poly {

/**
 * Implements the Inset Elements Operation modeling operation for the polygon document and mesh editing core.
 */
class InsetElementsOperation final : public PolyOperation {
public:
    InsetElementsOperation(Transform3 transform, quader::QVec3 pivot);

    [[nodiscard]] std::string_view id() const override { return poly_operation_descriptor(PolyOperationKey::InsetSelectedElements).id; }
    [[nodiscard]] std::string_view label() const override { return poly_operation_descriptor(PolyOperationKey::InsetSelectedElements).label; }
    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;

private:
    Transform3 transform_;
    quader::QVec3 pivot_;
};

namespace document_internal {
[[nodiscard]] OperationResult inset_selected_elements_impl(Document& document, Selection& selection, const Transform3& transform, quader::QVec3 pivot);
} // namespace document_internal

InsetElementsOperation::InsetElementsOperation(Transform3 transform, quader::QVec3 pivot)
    : transform_(transform)
    , pivot_(pivot)
{
}

OperationResult InsetElementsOperation::apply(Document& document, Selection& selection) const
{
    return document_internal::inset_selected_elements_impl(document, selection, transform_, pivot_);
}

OperationResult inset_selected_elements(Document& document, Selection& selection, const Transform3& transform, quader::QVec3 pivot)
{
    return InsetElementsOperation(transform, pivot).apply(document, selection);
}

} // namespace quader_poly