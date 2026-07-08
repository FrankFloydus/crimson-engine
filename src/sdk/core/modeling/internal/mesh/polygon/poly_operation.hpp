////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <mesh/polygon/document_operations.hpp>

#include <string_view>

namespace quader_poly {

/**
 * Represents a Poly Operation Metadata value used by the polygon document and mesh editing core.
 */
struct PolyOperationMetadata {
    std::string_view id;
    std::string_view label;
    std::string_view class_name;
};

/**
 * Implements the Poly Operation modeling operation for the polygon document and mesh editing core.
 */
class PolyOperation {
public:
    virtual ~PolyOperation() = default;

    [[nodiscard]] virtual std::string_view id() const { return {}; }
    [[nodiscard]] virtual std::string_view label() const { return {}; }
    [[nodiscard]] virtual OperationResult apply(Document& document, Selection& selection) const = 0;
};

} // namespace quader_poly
