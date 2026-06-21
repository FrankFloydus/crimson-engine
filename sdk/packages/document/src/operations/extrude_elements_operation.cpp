#include <quader_poly/poly_operation.h>

namespace quader_poly {

class ExtrudeElementsOperation final : public PolyOperation {
public:
    ExtrudeElementsOperation(Vec3 offset, float closed_edge_ledge_size = 0.0F);

    [[nodiscard]] std::string_view id() const override { return "extrude_selected_elements"; }
    [[nodiscard]] std::string_view label() const override { return "Extrude Selected Elements"; }
    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;

private:
    Vec3 offset_;
    float closed_edge_ledge_size_ = 0.0F;
};

namespace document_internal {
[[nodiscard]] OperationResult extrude_selected_elements_impl(Document& document, Selection& selection, Vec3 offset, float closed_edge_ledge_size);
} // namespace document_internal

ExtrudeElementsOperation::ExtrudeElementsOperation(Vec3 offset, float closed_edge_ledge_size)
    : offset_(offset)
    , closed_edge_ledge_size_(closed_edge_ledge_size)
{
}

OperationResult ExtrudeElementsOperation::apply(Document& document, Selection& selection) const
{
    return document_internal::extrude_selected_elements_impl(document, selection, offset_, closed_edge_ledge_size_);
}

OperationResult extrude_selected_elements(Document& document, Selection& selection, Vec3 offset, float closed_edge_ledge_size)
{
    return ExtrudeElementsOperation(offset, closed_edge_ledge_size).apply(document, selection);
}

} // namespace quader_poly