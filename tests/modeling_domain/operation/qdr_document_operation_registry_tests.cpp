////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <mesh/polygon/poly_operation_descriptor.h>
#include <mesh/polygon/poly_operation_registry.h>

#include <catch2/catch_test_macros.hpp>

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

TEST_CASE("poly operation registry exposes every public poly operation id", "[operation][registry]")
{
    const qp::PolyOperationRegistry& registry = qp::poly_operation_registry();
    const auto operations = registry.operations();
    const auto descriptors = qp::poly_operation_descriptors();
    REQUIRE(operations.size() == descriptors.size());
    CHECK(operations.data() == descriptors.data());

    std::set<std::string> ids;
    for (const qp::PolyOperationMetadata& descriptor : descriptors) {
        CHECK_FALSE(descriptor.id.empty());
        CHECK_FALSE(descriptor.label.empty());
        CHECK(ends_with(descriptor.class_name, "Operation"));
        CHECK(ids.emplace(descriptor.id).second);

        const qp::PolyOperationMetadata* operation = registry.find_by_id(descriptor.id);
        REQUIRE(operation != nullptr);
        CHECK(operation == &descriptor);
        CHECK(std::string(operation->id) == std::string(descriptor.id));
        CHECK(std::string(operation->label) == std::string(descriptor.label));
        CHECK(std::string(operation->class_name) == std::string(descriptor.class_name));
        const qp::PolyOperationMetadata* routed_operation = qp::poly_operation_for_id(descriptor.id);
        REQUIRE(routed_operation != nullptr);
        CHECK(routed_operation == operation);
        CHECK(registry.contains(descriptor.id));
    }

    CHECK(qp::poly_operation_for_id("missing") == nullptr);
    CHECK_FALSE(registry.contains("missing"));
}
