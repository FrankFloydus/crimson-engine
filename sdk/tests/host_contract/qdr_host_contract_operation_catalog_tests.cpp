#include <catch2/catch_test_macros.hpp>

#include <quader_editor_core/operation_registry.h>
#include <quader_host_contract/host_contract.h>

#include <string_view>

namespace qhc = quader_host_contract;
namespace qec = quader_editor_core;

namespace {

bool same_text(std::string_view left, std::string_view right)
{
	return left == right;
}

} // namespace

TEST_CASE("host contract operation catalog mirrors editor-core registry identity", "[host-contract]")
{
	const std::span<const qhc::HostOperationDescriptor> descriptors = qhc::operation_catalog();
	const std::span<const qec::EditorOperationRegistryEntry> registry_entries =
		qec::operation_registry().entries();

	REQUIRE(descriptors.size() == registry_entries.size());

	for (const qec::EditorOperationRegistryEntry& entry : registry_entries) {
		const qhc::HostOperationDescriptor *descriptor = qhc::find_operation_descriptor(entry.id);
		REQUIRE(descriptor != nullptr);
		CHECK(same_text(descriptor->id, entry.id));
		CHECK(same_text(descriptor->label, entry.operation->label()));
		CHECK(descriptor->supports_preview == entry.operation->supports_preview());
	}
}

TEST_CASE("host contract operation catalog is descriptor-only metadata", "[host-contract]")
{
	for (const qhc::HostOperationDescriptor& descriptor : qhc::operation_catalog()) {
		CHECK_FALSE(descriptor.id.empty());
		CHECK_FALSE(descriptor.label.empty());
		for (const qhc::HostOptionDescriptor& option : descriptor.options) {
			CHECK_FALSE(option.id.empty());
			CHECK_FALSE(option.label.empty());
		}
	}
}
