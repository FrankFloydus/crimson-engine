////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <editor/metadata/host_contract.hpp>

#include <fstream>
#include <string>
#include <type_traits>

namespace qhc = quader_host_contract;

namespace {

std::string read_host_contract_header()
{
	std::ifstream stream(QUADER_HOST_CONTRACT_HEADER_PATH);
	EXPECT_TRUE(stream.good());
	return std::string(
		std::istreambuf_iterator<char>(stream),
		std::istreambuf_iterator<char>());
}

std::string read_host_contract_source()
{
	std::ifstream stream(QUADER_HOST_CONTRACT_SOURCE_PATH);
	EXPECT_TRUE(stream.good());
	return std::string(
		std::istreambuf_iterator<char>(stream),
		std::istreambuf_iterator<char>());
}

bool has_token(const std::string &text, const std::string &token)
{
	return text.find(token) != std::string::npos;
}

} // namespace

TEST(QdrEditorMetadataSurfaceTests, HostContractPublicAPIStaysDescriptorOnly) {
	static_assert(std::is_trivially_copyable_v<qhc::HostOptionDescriptor>);
	static_assert(std::is_trivially_copyable_v<qhc::HostOperationDescriptor>);
	static_assert(std::is_trivially_copyable_v<qhc::HostActionAvailabilityDescriptor>);
	static_assert(std::is_trivially_copyable_v<qhc::HostActionDescriptor>);
	static_assert(std::is_trivially_copyable_v<qhc::HostOperationPanelDescriptor>);
	static_assert(std::is_trivially_copyable_v<qhc::HostUiSurfaceDescriptor>);
	static_assert(std::is_trivially_copyable_v<qhc::HostUiGroupDescriptor>);
	static_assert(std::is_trivially_copyable_v<qhc::HostUiPlacementDescriptor>);
	static_assert(std::is_trivially_copyable_v<qhc::HostInteractionDescriptor>);
	static_assert(std::is_trivially_copyable_v<qhc::HostToolGroupDescriptor>);
	static_assert(std::is_trivially_copyable_v<qhc::HostPanelDescriptor>);
	static_assert(std::is_trivially_copyable_v<qhc::HostToolDescriptor>);
	static_assert(std::is_trivially_copyable_v<qhc::HostSelectionModeDescriptor>);
	static_assert(std::is_trivially_copyable_v<qhc::HostShortcutDescriptor>);
	static_assert(std::is_trivially_copyable_v<qhc::HostGestureDescriptor>);
	static_assert(std::is_trivially_copyable_v<qhc::HostTransformAxisDescriptor>);
	static_assert(std::is_trivially_copyable_v<qhc::HostVector3Descriptor>);
	static_assert(std::is_trivially_copyable_v<qhc::HostViewportViewDescriptor>);
	static_assert(std::is_trivially_copyable_v<qhc::HostGridPresetDescriptor>);
	static_assert(std::is_trivially_copyable_v<qhc::HostViewportDefaultsDescriptor>);
	static_assert(std::is_trivially_copyable_v<qhc::HostOverlayLayerDescriptor>);
	static_assert(std::is_trivially_copyable_v<qhc::HostColorDescriptor>);
	static_assert(std::is_trivially_copyable_v<qhc::HostOverlayRenderPolicyDescriptor>);
	static_assert(std::is_trivially_copyable_v<qhc::HostOverlayPresentationDescriptor>);
	static_assert(std::is_trivially_copyable_v<qhc::HostPreferenceChoiceDescriptor>);
	static_assert(std::is_trivially_copyable_v<qhc::HostPreferenceDescriptor>);
	static_assert(std::is_trivially_copyable_v<qhc::HostPreferenceGroupDescriptor>);
	static_assert(std::is_trivially_copyable_v<qhc::HostVector2Descriptor>);
	static_assert(std::is_trivially_copyable_v<qhc::HostMaterialTextureAssetDescriptor>);
	static_assert(std::is_trivially_copyable_v<qhc::HostMaterialGeneratedTextureDescriptor>);
	static_assert(std::is_trivially_copyable_v<qhc::HostMaterialScalarDescriptor>);
	static_assert(std::is_trivially_copyable_v<qhc::HostMaterialRecipeDescriptor>);

	const std::string header = read_host_contract_header();
	const std::string legacy_export_macro = std::string("QDR_") + "UN" + "ITY_API";
	const std::string legacy_bridge_prefix = std::string("qdr_") + "un" + "ity";
	const std::string legacy_engine_namespace = std::string("Un") + "ityEngine";

	EXPECT_TRUE(header.find("operation_command") == std::string::npos);
	EXPECT_TRUE(header.find("runtime_command") == std::string::npos);
	EXPECT_TRUE(header.find("EditorOperationCommand") == std::string::npos);
	EXPECT_TRUE(header.find("EditorRuntimeCommand") == std::string::npos);
	EXPECT_TRUE(header.find(legacy_export_macro) == std::string::npos);
	EXPECT_TRUE(header.find(legacy_bridge_prefix) == std::string::npos);
	EXPECT_TRUE(header.find("execute") == std::string::npos);
	EXPECT_TRUE(header.find("dispatch") == std::string::npos);
	EXPECT_TRUE(header.find("payload") == std::string::npos);
	EXPECT_FALSE(has_token(header, std::string("Open") + "Mesh"));
	EXPECT_FALSE(has_token(header, std::string("gte") + "::"));

	const std::string source = read_host_contract_source();
	EXPECT_TRUE(source.find("EditorPrefs") == std::string::npos);
	EXPECT_TRUE(source.find("QVertexWorks.Quader") == std::string::npos);
	EXPECT_TRUE(source.find(legacy_engine_namespace) == std::string::npos);
	EXPECT_TRUE(source.find("QuaderViewportSettingsState") == std::string::npos);
	EXPECT_TRUE(source.find("DllImport") == std::string::npos);
}
