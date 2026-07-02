////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <editor/modeling/operations/operation_schema.hpp>
#include <editor/metadata/host_contract.hpp>

#include <string_view>

namespace qhc = quader_host_contract;
namespace qec = quader_editor_core;

namespace {

bool same_text(std::string_view left, std::string_view right)
{
	return left == right;
}

const qhc::HostOperationDescriptor& require_operation(std::string_view id)
{
	const qhc::HostOperationDescriptor *descriptor = qhc::find_operation_descriptor(id);
	if (descriptor == nullptr) {
		ADD_FAILURE() << "Missing host operation descriptor";
                static const qhc::HostOperationDescriptor kMissingDescriptor{};
                return kMissingDescriptor;
        }
	return *descriptor;
}

const qhc::HostOptionDescriptor& require_option(const qhc::HostOperationDescriptor& operation, std::string_view id)
{
	for (const qhc::HostOptionDescriptor& option : operation.options) {
		if (option.id == id) {
			return option;
		}
	}
	ADD_FAILURE() << "Missing host option descriptor";
        static const qhc::HostOptionDescriptor kMissingDescriptor{};
        return kMissingDescriptor;
}

void check_int_option(const qhc::HostOptionDescriptor& option, const qec::IntOperationOptionSchema& schema)
{
	EXPECT_TRUE(same_text(option.id, schema.id));
	EXPECT_TRUE(same_text(option.label, schema.label));
        EXPECT_TRUE(option.value_kind == qhc::HostOptionValueKind::Integer);
        EXPECT_TRUE(option.default_integer == schema.default_value);
	EXPECT_TRUE(option.minimum_integer == schema.minimum);
	EXPECT_TRUE(option.maximum_integer == schema.maximum);
	EXPECT_TRUE(option.integer_step == schema.step);
	EXPECT_TRUE(option.hard_limits == schema.hard_limits);
}

void check_float_option(const qhc::HostOptionDescriptor& option, const qec::FloatOperationOptionSchema& schema)
{
	EXPECT_TRUE(same_text(option.id, schema.id));
	EXPECT_TRUE(same_text(option.label, schema.label));
        EXPECT_TRUE(option.value_kind == qhc::HostOptionValueKind::Floating);
        EXPECT_TRUE(option.default_number == schema.default_value);
	EXPECT_TRUE(option.minimum_number == schema.minimum);
	EXPECT_TRUE(option.maximum_number == schema.maximum);
	EXPECT_TRUE(option.number_step == schema.step);
	EXPECT_TRUE(option.hard_limits == schema.hard_limits);
}

void check_bool_option(const qhc::HostOptionDescriptor& option, const qec::BoolOperationOptionSchema& schema)
{
	EXPECT_TRUE(same_text(option.id, schema.id));
	EXPECT_TRUE(same_text(option.label, schema.label));
        EXPECT_TRUE(option.value_kind == qhc::HostOptionValueKind::Boolean);
        EXPECT_TRUE(option.default_bool == schema.default_value);
	EXPECT_TRUE(option.visible == schema.visible);
}

void check_option_descriptor(
		const qhc::HostOptionDescriptor& option,
		const qec::EditorOperationOptionDescriptor& schema)
{
	EXPECT_TRUE(same_text(option.id, schema.option_id));
	EXPECT_TRUE(same_text(option.label, schema.label));
	EXPECT_TRUE(option.default_integer == schema.default_integer);
	EXPECT_TRUE(option.minimum_integer == schema.minimum_integer);
	EXPECT_TRUE(option.maximum_integer == schema.maximum_integer);
	EXPECT_TRUE(option.integer_step == schema.integer_step);
	EXPECT_TRUE(option.default_number == schema.default_number);
	EXPECT_TRUE(option.minimum_number == schema.minimum_number);
	EXPECT_TRUE(option.maximum_number == schema.maximum_number);
	EXPECT_TRUE(option.number_step == schema.number_step);
	EXPECT_TRUE(option.default_bool == schema.default_bool);
	EXPECT_TRUE(option.visible == schema.visible);
	EXPECT_TRUE(option.hard_limits == schema.hard_limits);
	switch (schema.value_kind) {
        case qec::EditorOperationOptionValueKind::Integer:
          EXPECT_TRUE(option.value_kind == qhc::HostOptionValueKind::Integer);
          break;
        case qec::EditorOperationOptionValueKind::Floating:
          EXPECT_TRUE(option.value_kind == qhc::HostOptionValueKind::Floating);
          break;
        case qec::EditorOperationOptionValueKind::Boolean:
          EXPECT_TRUE(option.value_kind == qhc::HostOptionValueKind::Boolean);
          break;
	}
}

} // namespace

TEST(QdrEditorMetadataSchemaMirrorTests, HostContractExposesEditorCoreOperationOptionSchemasAsDescriptors) {
	const qec::BridgeOperationSchema bridge = qec::bridge_operation_schema();
	const qhc::HostOperationDescriptor& bridge_descriptor = require_operation(bridge.operation_id);
	EXPECT_TRUE(bridge_descriptor.options.size() == 1U);
	check_int_option(require_option(bridge_descriptor, bridge.steps.id), bridge.steps);

	const qec::EdgeBevelOperationSchema edge_bevel = qec::edge_bevel_operation_schema();
	const qhc::HostOperationDescriptor& edge_bevel_descriptor = require_operation(edge_bevel.operation_id);
	EXPECT_TRUE(edge_bevel_descriptor.options.size() == 3U);
	check_float_option(require_option(edge_bevel_descriptor, edge_bevel.width.id), edge_bevel.width);
	check_float_option(require_option(edge_bevel_descriptor, edge_bevel.profile.id), edge_bevel.profile);
	check_int_option(require_option(edge_bevel_descriptor, edge_bevel.segments.id), edge_bevel.segments);

	const qec::ThickenOperationSchema thicken = qec::thicken_operation_schema();
	const qhc::HostOperationDescriptor& thicken_descriptor = require_operation(thicken.operation_id);
	EXPECT_TRUE(thicken_descriptor.options.size() == 2U);
	check_float_option(require_option(thicken_descriptor, thicken.thickness.id), thicken.thickness);
	check_bool_option(require_option(thicken_descriptor, thicken.from_center.id), thicken.from_center);

	const qec::SliceQuadOperationSchema slice_quad = qec::slice_quad_operation_schema();
	const qhc::HostOperationDescriptor& slice_quad_descriptor = require_operation(slice_quad.operation_id);
	EXPECT_TRUE(slice_quad_descriptor.options.size() == 2U);
	check_int_option(require_option(slice_quad_descriptor, slice_quad.x_slices.id), slice_quad.x_slices);
	check_int_option(require_option(slice_quad_descriptor, slice_quad.y_slices.id), slice_quad.y_slices);

	const qec::MergeByDistanceOperationSchema merge_by_distance = qec::merge_by_distance_operation_schema();
	const qhc::HostOperationDescriptor& merge_by_distance_descriptor = require_operation(merge_by_distance.operation_id);
	EXPECT_TRUE(merge_by_distance_descriptor.options.size() == 1U);
	check_float_option(require_option(merge_by_distance_descriptor, merge_by_distance.tolerance.id), merge_by_distance.tolerance);
}

TEST(QdrEditorMetadataSchemaMirrorTests, HostOperationOptionsCoverSchemaOwnedOptionDescriptorCatalog) {
	for (const qec::EditorOperationOptionDescriptor& schema : qec::operation_option_descriptor_catalog()) {
		const qhc::HostOperationDescriptor& operation = require_operation(schema.operation_id);
		check_option_descriptor(require_option(operation, schema.option_id), schema);
	}
}
