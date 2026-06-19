#include <quader_poly/poly_operation.h>

namespace quader_poly {

class InsetElementsOperation final : public PolyOperation {
public:
    InsetElementsOperation(Transform3 transform, Vec3 pivot);

    [[nodiscard]] std::string_view id() const override { return "inset_selected_elements"; }
    [[nodiscard]] std::string_view label() const override { return "Inset Selected Elements"; }
    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;

private:
    Transform3 transform_;
    Vec3 pivot_;
};

namespace document_internal {
[[nodiscard]] OperationResult inset_selected_elements_impl(Document& document, Selection& selection, const Transform3& transform, Vec3 pivot);
} // namespace document_internal

InsetElementsOperation::InsetElementsOperation(Transform3 transform, Vec3 pivot)
    : transform_(transform)
    , pivot_(pivot)
{
}

OperationResult InsetElementsOperation::apply(Document& document, Selection& selection) const
{
    return document_internal::inset_selected_elements_impl(document, selection, transform_, pivot_);
}

OperationResult inset_selected_elements(Document& document, Selection& selection, const Transform3& transform, Vec3 pivot)
{
    return InsetElementsOperation(transform, pivot).apply(document, selection);
}

} // namespace quader_poly