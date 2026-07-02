////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <mesh/polygon/poly_operation_descriptor.hpp>
#include <mesh/polygon/poly_operation_registry.hpp>

namespace quader_poly {

PolyOperationRegistry::PolyOperationRegistry(std::span<const PolyOperationMetadata> operations)
    : operations_(operations)
{
}

std::span<const PolyOperationMetadata> PolyOperationRegistry::operations() const
{
    return operations_;
}

const PolyOperationMetadata* PolyOperationRegistry::find_by_id(std::string_view id) const
{
    for (const PolyOperationMetadata& operation : operations_) {
        if (operation.id == id) {
            return &operation;
        }
    }
    return nullptr;
}

bool PolyOperationRegistry::contains(std::string_view id) const
{
    return find_by_id(id) != nullptr;
}

const PolyOperationRegistry& poly_operation_registry()
{
  static const PolyOperationRegistry kRegistry(poly_operation_descriptors());
  return kRegistry;
}

std::span<const PolyOperationMetadata> poly_operation_entries()
{
    return poly_operation_registry().operations();
}

const PolyOperationMetadata* poly_operation_for_id(std::string_view id)
{
    return poly_operation_registry().find_by_id(id);
}

} // namespace quader_poly
