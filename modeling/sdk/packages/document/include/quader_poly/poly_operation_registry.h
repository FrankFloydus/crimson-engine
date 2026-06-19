#pragma once

#include <quader_poly/poly_operation.h>

#include <span>
#include <string_view>

namespace quader_poly {

class PolyOperationRegistry {
public:
    explicit PolyOperationRegistry(std::span<const PolyOperationMetadata> operations);

    [[nodiscard]] std::span<const PolyOperationMetadata> operations() const;
    [[nodiscard]] const PolyOperationMetadata* find_by_id(std::string_view id) const;
    [[nodiscard]] bool contains(std::string_view id) const;

private:
    std::span<const PolyOperationMetadata> operations_;
};

[[nodiscard]] const PolyOperationRegistry& poly_operation_registry();
[[nodiscard]] std::span<const PolyOperationMetadata> poly_operation_entries();
[[nodiscard]] const PolyOperationMetadata* poly_operation_for_id(std::string_view id);

} // namespace quader_poly
