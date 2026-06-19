#pragma once

#include <quader_poly/document.h>

#include <string_view>

namespace quader_poly {

struct PolyOperationMetadata {
    std::string_view id;
    std::string_view label;
    std::string_view class_name;
};

class PolyOperation {
public:
    virtual ~PolyOperation() = default;

    [[nodiscard]] virtual std::string_view id() const { return {}; }
    [[nodiscard]] virtual std::string_view label() const { return {}; }
    [[nodiscard]] virtual OperationResult apply(Document& document, Selection& selection) const = 0;
};

} // namespace quader_poly
