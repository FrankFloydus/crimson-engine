////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <editor/metadata/host_contract.hpp>

#include <cmath>
#include <fstream>
#include <span>
#include <string>
#include <string_view>

namespace qhc = quader_host_contract;

namespace {

bool same_text(std::string_view left, std::string_view right)
{
	return left == right;
}

bool close_to(double left, double right)
{
	return std::abs(left - right) <= 0.000001;
}

std::string read_host_contract_source()
{
	std::ifstream stream(QUADER_HOST_CONTRACT_SOURCE_PATH);
	if (!stream.good()) {
		ADD_FAILURE() << "Host contract source could not be opened";
		return {};
	}
	return std::string(
		std::istreambuf_iterator<char>(stream),
		std::istreambuf_iterator<char>());
}

bool span_contains(std::span<const std::string_view> values, std::string_view value)
{
	for (std::string_view candidate : values) {
		if (candidate == value) {
			return true;
		}
	}
	return false;
}

const qhc::HostOverlayRenderPolicyDescriptor& require_render_policy(std::string_view id)
{
	for (const qhc::HostOverlayRenderPolicyDescriptor& descriptor : qhc::overlay_presentation().render_policies) {
		if (descriptor.id == id) {
			return descriptor;
		}
	}
	ADD_FAILURE() << "Missing host overlay render policy descriptor";
	static const qhc::HostOverlayRenderPolicyDescriptor missing_descriptor {};
	return missing_descriptor;
}

const qhc::HostMaterialScalarDescriptor& require_scalar(
		const qhc::HostMaterialRecipeDescriptor& recipe,
		qhc::HostMaterialScalarKind kind)
{
	for (const qhc::HostMaterialScalarDescriptor& scalar : recipe.scalars) {
		if (scalar.kind == kind) {
			return scalar;
		}
	}
	ADD_FAILURE() << "Missing host material scalar descriptor";
	static const qhc::HostMaterialScalarDescriptor missing_descriptor {};
	return missing_descriptor;
}

void check_color(const qhc::HostColorDescriptor& color, double r, double g, double b, double a)
{
	EXPECT_TRUE(close_to(color.r, r));
	EXPECT_TRUE(close_to(color.g, g));
	EXPECT_TRUE(close_to(color.b, b));
	EXPECT_TRUE(close_to(color.a, a));
}

} // namespace

TEST(QdrEditorMetadataPresentationMaterialTests, HostContractDescribesViewportOverlayPresentationPolicy) {
	const qhc::HostOverlayPresentationDescriptor presentation = qhc::overlay_presentation();

	EXPECT_TRUE(presentation.suppress_gizmo_for_blocking_preview);
	EXPECT_TRUE(presentation.suppress_gizmo_for_modal_tool);
	check_color(presentation.blocking_preview_wire_color, 0x5c / 255.0, 0xe0 / 255.0, 1.0, 0xeb / 255.0);

	EXPECT_TRUE(presentation.blocking_preview_override_layer_ids.size() == 4U);
	EXPECT_TRUE(span_contains(presentation.blocking_preview_override_layer_ids, "selected_face_edge"));
	EXPECT_TRUE(span_contains(presentation.blocking_preview_override_layer_ids, "selected_edge"));
	EXPECT_TRUE(span_contains(presentation.blocking_preview_override_layer_ids, "source_wire"));
	EXPECT_TRUE(span_contains(presentation.blocking_preview_override_layer_ids, "open_edge"));

	EXPECT_TRUE(presentation.draw_order_layer_ids.size() == 15U);
	EXPECT_TRUE(same_text(presentation.draw_order_layer_ids.front(), "selected_visible_face"));
	EXPECT_TRUE(same_text(presentation.draw_order_layer_ids[4], "source_wire"));
	EXPECT_TRUE(same_text(presentation.draw_order_layer_ids[13], "hover_vertex"));
	EXPECT_TRUE(same_text(presentation.draw_order_layer_ids.back(), "snap_target_vertex"));

	EXPECT_TRUE(presentation.transient_hover_mask_writer_layer_ids.size() == 4U);
	EXPECT_TRUE(span_contains(presentation.transient_hover_mask_writer_layer_ids, "hover_face"));
	EXPECT_TRUE(span_contains(presentation.transient_hover_mask_writer_layer_ids, "hover_vertex"));

	EXPECT_TRUE(presentation.transient_hover_suppressed_layer_ids.size() == 6U);
	EXPECT_TRUE(span_contains(presentation.transient_hover_suppressed_layer_ids, "selected_visible_face"));
	EXPECT_TRUE(span_contains(presentation.transient_hover_suppressed_layer_ids, "selected_vertex"));

	const qhc::HostOverlayRenderPolicyDescriptor& fill = require_render_policy("depth_tested_fill");
	EXPECT_TRUE(fill.geometry_kind == qhc::HostOverlayGeometryKind::triangles);
	EXPECT_TRUE(close_to(fill.depth_bias, 0.035));
	EXPECT_FALSE(fill.draws_on_top);

	const qhc::HostOverlayRenderPolicyDescriptor& line = require_render_policy("depth_tested_line");
	EXPECT_TRUE(line.geometry_kind == qhc::HostOverlayGeometryKind::lines);
	EXPECT_TRUE(close_to(line.depth_bias, 0.02));
	EXPECT_TRUE(close_to(line.feather_pixels, 1.0));
	EXPECT_FALSE(line.draws_on_top);

	const qhc::HostOverlayRenderPolicyDescriptor& on_top_point = require_render_policy("draw_on_top_point");
	EXPECT_TRUE(on_top_point.geometry_kind == qhc::HostOverlayGeometryKind::points);
	EXPECT_TRUE(close_to(on_top_point.depth_bias, 0.0));
	EXPECT_TRUE(on_top_point.draws_on_top);
}

TEST(QdrEditorMetadataPresentationMaterialTests, HostContractDefaultMaterialRecipeIsSemanticAndEngineNeutral) {
	const std::span<const qhc::HostMaterialTextureAssetDescriptor> assets = qhc::material_texture_asset_catalog();
	EXPECT_TRUE(assets.size() == 2U);

	const qhc::HostMaterialTextureAssetDescriptor *albedo =
		qhc::find_material_texture_asset_descriptor("default_albedo");
	EXPECT_TRUE(albedo != nullptr);
	EXPECT_TRUE(albedo->role == qhc::HostMaterialTextureRole::base_color);
	EXPECT_TRUE(same_text(albedo->package_relative_path, "assets/materials/default/default_albedo.png"));

	const qhc::HostMaterialTextureAssetDescriptor *roughness =
		qhc::find_material_texture_asset_descriptor("default_roughness");
	EXPECT_TRUE(roughness != nullptr);
	EXPECT_TRUE(roughness->role == qhc::HostMaterialTextureRole::roughness);
	EXPECT_TRUE(same_text(roughness->package_relative_path, "assets/materials/default/default_roughness.png"));

	const qhc::HostMaterialGeneratedTextureDescriptor *metallic_smoothness =
		qhc::find_material_generated_texture_descriptor("default_metallic_smoothness");
	EXPECT_TRUE(metallic_smoothness != nullptr);
	EXPECT_TRUE(metallic_smoothness->role == qhc::HostMaterialTextureRole::metallic_smoothness);
	EXPECT_TRUE(same_text(metallic_smoothness->source_texture_asset_id, "default_roughness"));
	EXPECT_TRUE(metallic_smoothness->invert_source_grayscale);
	EXPECT_TRUE(metallic_smoothness->target_channel == qhc::HostMaterialTextureChannel::alpha);
	EXPECT_TRUE(close_to(metallic_smoothness->constant_red, 0.0));
	EXPECT_TRUE(close_to(metallic_smoothness->constant_green, 0.0));
	EXPECT_TRUE(close_to(metallic_smoothness->constant_blue, 0.0));

	const qhc::HostMaterialGeneratedTextureDescriptor *packed_mask =
		qhc::find_material_generated_texture_descriptor("default_packed_mask");
	EXPECT_TRUE(packed_mask != nullptr);
	EXPECT_TRUE(packed_mask->role == qhc::HostMaterialTextureRole::mask);
	EXPECT_TRUE(packed_mask->invert_source_grayscale);
	EXPECT_TRUE(close_to(packed_mask->constant_green, 1.0));

	const qhc::HostMaterialRecipeDescriptor *recipe = qhc::find_material_recipe_descriptor("default_material");
	EXPECT_TRUE(recipe != nullptr);
	EXPECT_TRUE(same_text(recipe->label, "Quader Default Material"));
	check_color(recipe->base_color, 1.0, 1.0, 1.0, 1.0);
	EXPECT_TRUE(recipe->texture_asset_ids.size() == 2U);
	EXPECT_TRUE(span_contains(recipe->texture_asset_ids, "default_albedo"));
	EXPECT_TRUE(span_contains(recipe->texture_asset_ids, "default_roughness"));
	EXPECT_TRUE(recipe->generated_texture_ids.size() == 2U);
	EXPECT_TRUE(span_contains(recipe->generated_texture_ids, "default_metallic_smoothness"));
	EXPECT_TRUE(span_contains(recipe->generated_texture_ids, "default_packed_mask"));
	EXPECT_TRUE(close_to(recipe->texture_scale.x, 1.0));
	EXPECT_TRUE(close_to(recipe->texture_scale.y, -1.0));
	EXPECT_TRUE(close_to(recipe->texture_offset.x, 0.0));
	EXPECT_TRUE(close_to(recipe->texture_offset.y, 1.0));

	EXPECT_TRUE(close_to(require_scalar(*recipe, qhc::HostMaterialScalarKind::metallic).value, 0.0));
	EXPECT_TRUE(close_to(require_scalar(*recipe, qhc::HostMaterialScalarKind::smoothness).value, 1.0));
	EXPECT_TRUE(close_to(require_scalar(*recipe, qhc::HostMaterialScalarKind::smoothness_texture_channel).value, 0.0));
	EXPECT_TRUE(close_to(require_scalar(*recipe, qhc::HostMaterialScalarKind::smoothness_remap_min).value, 0.0));
	EXPECT_TRUE(close_to(require_scalar(*recipe, qhc::HostMaterialScalarKind::smoothness_remap_max).value, 1.0));
	EXPECT_TRUE(close_to(require_scalar(*recipe, qhc::HostMaterialScalarKind::ambient_occlusion_remap_min).value, 1.0));
	EXPECT_TRUE(close_to(require_scalar(*recipe, qhc::HostMaterialScalarKind::ambient_occlusion_remap_max).value, 1.0));
}

TEST(QdrEditorMetadataPresentationMaterialTests, HostContractMaterialMetadataAvoidsEngineSpecificShaderAndPropertyNames) {
	const std::string source = read_host_contract_source();
	EXPECT_TRUE(source.find("Universal Render Pipeline") == std::string::npos);
	EXPECT_TRUE(source.find("HDRP/Lit") == std::string::npos);
	EXPECT_TRUE(source.find("Standard") == std::string::npos);
	EXPECT_TRUE(source.find("Unlit/Texture") == std::string::npos);
	EXPECT_TRUE(source.find("_BaseMap") == std::string::npos);
	EXPECT_TRUE(source.find("_BaseColorMap") == std::string::npos);
	EXPECT_TRUE(source.find("_MetallicGlossMap") == std::string::npos);
	EXPECT_TRUE(source.find("_MaskMap") == std::string::npos);
	EXPECT_TRUE(source.find("Packages/com.qvertexworks.quader") == std::string::npos);
}
