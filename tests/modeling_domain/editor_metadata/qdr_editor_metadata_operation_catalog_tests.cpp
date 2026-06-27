////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <editor/modeling/operations/operation_registry.hpp>
#include <editor/metadata/host_contract.hpp>

#include <string_view>

namespace qhc = quader_host_contract;
namespace qec = quader_editor_core;

namespace {

bool same_text(std::string_view left, std::string_view right)
{
	return left == right;
}

} // namespace

TEST(QdrEditorMetadataOperationCatalogTests, HostContractOperationCatalogMirrorsEditorCoreRegistryIdentity) {
	const std::span<const qhc::HostOperationDescriptor> descriptors = qhc::operation_catalog();
	const std::span<const qec::EditorOperationRegistryEntry> registry_entries =
		qec::operation_registry().entries();

	EXPECT_TRUE(descriptors.size() == registry_entries.size());

	for (const qec::EditorOperationRegistryEntry& entry : registry_entries) {
		const qhc::HostOperationDescriptor *descriptor = qhc::find_operation_descriptor(entry.id);
		EXPECT_TRUE(descriptor != nullptr);
		EXPECT_TRUE(same_text(descriptor->id, entry.id));
		EXPECT_TRUE(same_text(descriptor->label, entry.operation->label()));
		EXPECT_TRUE(descriptor->supports_preview == entry.operation->supports_preview());
	}
}

TEST(QdrEditorMetadataOperationCatalogTests, HostContractOperationCatalogIsDescriptorOnlyMetadata) {
	for (const qhc::HostOperationDescriptor& descriptor : qhc::operation_catalog()) {
		EXPECT_FALSE(descriptor.id.empty());
		EXPECT_FALSE(descriptor.label.empty());
		for (const qhc::HostOptionDescriptor& option : descriptor.options) {
			EXPECT_FALSE(option.id.empty());
			EXPECT_FALSE(option.label.empty());
		}
	}
}
