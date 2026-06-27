////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <mesh/polygon/poly_operation_descriptor.hpp>
#include <mesh/polygon/poly_operation_registry.hpp>

#include "test_support.hpp"

#include <set>
#include <string>
#include <string_view>

namespace qp = quader_poly;

namespace {

bool ends_with(std::string_view value, std::string_view suffix)
{
    return value.size() >= suffix.size()
        && value.substr(value.size() - suffix.size()) == suffix;
}

} // namespace

TEST(QdrDocumentOperationRegistryTests, PolyOperationRegistryExposesEveryPublicPolyOperationId) {
    const qp::PolyOperationRegistry& registry = qp::poly_operation_registry();
    const auto operations = registry.operations();
    const auto descriptors = qp::poly_operation_descriptors();
    EXPECT_TRUE(operations.size() == descriptors.size());
    EXPECT_TRUE(operations.data() == descriptors.data());

    std::set<std::string> ids;
    for (const qp::PolyOperationMetadata& descriptor : descriptors) {
        EXPECT_FALSE(descriptor.id.empty());
        EXPECT_FALSE(descriptor.label.empty());
        EXPECT_TRUE(ends_with(descriptor.class_name, "Operation"));
        EXPECT_TRUE(ids.emplace(descriptor.id).second);

        const qp::PolyOperationMetadata* operation = registry.find_by_id(descriptor.id);
        EXPECT_TRUE(operation != nullptr);
        EXPECT_TRUE(operation == &descriptor);
        EXPECT_TRUE(std::string(operation->id) == std::string(descriptor.id));
        EXPECT_TRUE(std::string(operation->label) == std::string(descriptor.label));
        EXPECT_TRUE(std::string(operation->class_name) == std::string(descriptor.class_name));
        const qp::PolyOperationMetadata* routed_operation = qp::poly_operation_for_id(descriptor.id);
        EXPECT_TRUE(routed_operation != nullptr);
        EXPECT_TRUE(routed_operation == operation);
        EXPECT_TRUE(registry.contains(descriptor.id));
    }

    EXPECT_TRUE(qp::poly_operation_for_id("missing") == nullptr);
    EXPECT_FALSE(registry.contains("missing"));
}
