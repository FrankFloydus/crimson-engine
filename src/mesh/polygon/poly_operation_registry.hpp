////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <mesh/polygon/poly_operation.hpp>

#include <span>
#include <string_view>

namespace quader_poly {

/**
 * Owns Poly Operation Registry lookup and registration for the polygon document and mesh editing core.
 */
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
