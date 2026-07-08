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
 * Implements the Extrude Elements Operation modeling operation for the polygon document and mesh editing core.
 */
class ExtrudeElementsOperation final : public PolyOperation {
public:
    ExtrudeElementsOperation(quader::QVec3 offset, float closed_edge_ledge_size = 0.0F);

    [[nodiscard]] std::string_view id() const override { return poly_operation_descriptor(PolyOperationKey::ExtrudeSelectedElements).id; }
    [[nodiscard]] std::string_view label() const override { return poly_operation_descriptor(PolyOperationKey::ExtrudeSelectedElements).label; }
    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;

private:
    quader::QVec3 offset_;
    float closed_edge_ledge_size_ = 0.0F;
};

/**
 * Extrudes selected elements by applying an affine transform around a pivot.
 */
class TransformExtrudeElementsOperation final : public PolyOperation {
public:
    TransformExtrudeElementsOperation(Transform3 transform, quader::QVec3 pivot);

    [[nodiscard]] std::string_view id() const override { return poly_operation_descriptor(PolyOperationKey::ExtrudeSelectedElements).id; }
    [[nodiscard]] std::string_view label() const override { return poly_operation_descriptor(PolyOperationKey::ExtrudeSelectedElements).label; }
    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;

private:
    Transform3 transform_;
    quader::QVec3 pivot_;
};

namespace document_internal {
[[nodiscard]] OperationResult extrude_selected_elements_impl(Document& document, Selection& selection, quader::QVec3 offset, float closed_edge_ledge_size);
[[nodiscard]] OperationResult transform_extrude_selected_elements_impl(Document& document, Selection& selection, const Transform3& transform, quader::QVec3 pivot);
} // namespace document_internal

ExtrudeElementsOperation::ExtrudeElementsOperation(quader::QVec3 offset, float closed_edge_ledge_size)
    : offset_(offset)
    , closed_edge_ledge_size_(closed_edge_ledge_size)
{
}

OperationResult ExtrudeElementsOperation::apply(Document& document, Selection& selection) const
{
    return document_internal::extrude_selected_elements_impl(document, selection, offset_, closed_edge_ledge_size_);
}

TransformExtrudeElementsOperation::TransformExtrudeElementsOperation(Transform3 transform, quader::QVec3 pivot)
    : transform_(transform)
    , pivot_(pivot)
{
}

OperationResult TransformExtrudeElementsOperation::apply(Document& document, Selection& selection) const
{
    return document_internal::transform_extrude_selected_elements_impl(document, selection, transform_, pivot_);
}

OperationResult extrude_selected_elements(Document& document, Selection& selection, quader::QVec3 offset, float closed_edge_ledge_size)
{
    return ExtrudeElementsOperation(offset, closed_edge_ledge_size).apply(document, selection);
}

OperationResult transform_extrude_selected_elements(Document& document, Selection& selection, const Transform3& transform, quader::QVec3 pivot)
{
    return TransformExtrudeElementsOperation(transform, pivot).apply(document, selection);
}

} // namespace quader_poly
