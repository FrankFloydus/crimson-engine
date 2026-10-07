////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "support/guardrail_file_scan.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cctype>
#include <string>
#include <string_view>

namespace quader {
namespace {

using test::guardrail::read_source_file;
using test::guardrail::source_slice;

std::size_t count_occurrences(std::string_view haystack,
                              std::string_view needle) {
  std::size_t count = 0;
  std::size_t offset = 0;
  while (!needle.empty()) {
    offset = haystack.find(needle, offset);
    if (offset == std::string_view::npos) {
      break;
    }
    ++count;
    offset += needle.size();
  }
  return count;
}

std::string compact_source_text(std::string_view text) {
  std::string compact;
  compact.reserve(text.size());
  for (const char character : text) {
    if (!std::isspace(static_cast<unsigned char>(character))) {
      compact.push_back(character);
    }
  }
  return compact;
}

bool source_contains_compact(std::string_view haystack,
                             std::string_view needle) {
  return compact_source_text(haystack).find(compact_source_text(needle)) !=
         std::string::npos;
}

TEST(GroundGridMaterialTests, UsesUserWorldAnchoring) {
  const std::string source =
      read_source_file("resources/materials/ground_grid.mat");
  const std::string grid_renderer_source =
      read_source_file("src/render/viewport_grid_renderer.cpp");

  EXPECT_NE(source.find("shadingModel : unlit"), std::string::npos);
  EXPECT_NE(source.find("blending : transparent"), std::string::npos);
  EXPECT_NE(source.find("depthWrite : false"), std::string::npos);
  EXPECT_NE(source.find("variable_gridWorldPosition.xyz"), std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      source, "worldPosition = (getUserWorldFromWorldMatrix() * "
              "vec4(shaderWorldPosition, 1.0)).xyz"));
  EXPECT_EQ(source.find("getWorldPosition()"), std::string::npos);
  EXPECT_NE(source.find("localPosition"), std::string::npos);
  EXPECT_NE(source.find("variable_localPosition"), std::string::npos);
  EXPECT_NE(source.find("cameraSize"), std::string::npos);
  EXPECT_NE(source.find("visibleGridSize"), std::string::npos);
  EXPECT_NE(source.find("minGridWorldSpacing"), std::string::npos);
  EXPECT_NE(source.find("majorCoverage = parentGridValue * "
                        "materialParams.majorGridColor.a"),
            std::string::npos);
  EXPECT_NE(source.find("minorCoverage = gridValue * "
                        "materialParams.gridColor.a * (1.0 - clamp("),
            std::string::npos);
  EXPECT_EQ(source.find("alpha = max(alpha, parentGridValue * "
                        "materialParams.majorGridColor.a)"),
            std::string::npos);
  EXPECT_NE(source.find("vec4(color * alpha, alpha)"), std::string::npos);
  EXPECT_EQ(source.find("getUV0()"), std::string::npos);
  EXPECT_EQ(source.find("planeCenter"), std::string::npos);
  EXPECT_NE(grid_renderer_source.find("ViewportProjection::Perspective ? 1.0f"),
            std::string::npos);
  EXPECT_NE(grid_renderer_source.find(".castShadows(false)"),
            std::string::npos);
  EXPECT_NE(grid_renderer_source.find(".receiveShadows(false)"),
            std::string::npos);
}

TEST(GroundGridMaterialTests,
     LineThicknessNarrowsCoverageWithoutScalingCenterOpacity) {
  const std::string source =
      read_source_file("resources/materials/ground_grid.mat");
  const std::size_t grid_line_start = source.find("float gridLine(");
  const std::size_t axis_line_start = source.find("float axisLine(");
  const std::size_t normalize_start = source.find("vec3 safeNormalize(");

  ASSERT_NE(grid_line_start, std::string::npos);
  ASSERT_NE(axis_line_start, std::string::npos);
  ASSERT_NE(normalize_start, std::string::npos);
  ASSERT_LT(grid_line_start, axis_line_start);
  ASSERT_LT(axis_line_start, normalize_start);

  const std::string grid_line =
      source.substr(grid_line_start, axis_line_start - grid_line_start);
  const std::string axis_line =
      source.substr(axis_line_start, normalize_start - axis_line_start);
  EXPECT_NE(grid_line.find("lineWidth = antialias * max(widthScale, 0.05) * "
                           "thickness"),
            std::string::npos);
  EXPECT_NE(axis_line.find("lineWidth = antialias * 0.9 * thickness"),
            std::string::npos);
  EXPECT_NE(grid_line.find("return coverage;"), std::string::npos);
  EXPECT_NE(axis_line.find("return coverage;"), std::string::npos);
  EXPECT_EQ(grid_line.find("coverage * min(thickness"), std::string::npos);
  EXPECT_EQ(axis_line.find("coverage * min(thickness"), std::string::npos);
}

TEST(GroundGridMaterialTests,
     AnalyticPerspectiveBindingsFollowThePresentationRevisionGate) {
  const std::string source =
      read_source_file("src/render/viewport_grid_renderer.cpp");
  const std::string signature = source_slice(
      source, "std::uint64_t ground_grid_presentation_signature(",
      "filament::math::mat4f ground_grid_transform(");
  EXPECT_NE(signature.find("static_cast<std::uint32_t>(state.projection)"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      source, "\"analyticGridEnabled\", controller.state().projection == "
              "ViewportProjection::Perspective ? 1.0f : 0.0f"));
  const std::size_t unchanged_gate =
      source.find("if (presentation_signature_ == presentation_signature)");
  ASSERT_NE(unchanged_gate, std::string::npos);
  for (const std::string_view binding : {
           "\"analyticGridEnabled\"", "\"depthBiasAmount\"",
           "\"analyticLineThickness\"", "\"gridIntensity\""}) {
    const std::size_t binding_start = source.find(binding);
    ASSERT_NE(binding_start, std::string::npos) << binding;
    EXPECT_LT(unchanged_gate, binding_start) << binding;
  }
}

TEST(GroundGridMaterialTests,
     AnalyticDepthBiasPreservesWorldCoordinatesAndPerspectiveInterpolation) {
  const std::string source =
      read_source_file("resources/materials/ground_grid.mat");
  const std::string vertex = source_slice(source, "vertex {", "fragment {");
  EXPECT_NE(source.find("vertexDomain : device"), std::string::npos);
  const std::size_t world_assignment =
      vertex.find("material.gridWorldPosition = worldPosition");
  const std::size_t depth_bias =
      vertex.find("clipPosition.z += clipPosition.w * bias");
  ASSERT_NE(world_assignment, std::string::npos);
  ASSERT_NE(depth_bias, std::string::npos);
  EXPECT_LT(world_assignment, depth_bias);
  EXPECT_TRUE(source_contains_compact(
      vertex, "vec4 devicePosition = vec4(clipPosition.xy, "
              "1.0 - 2.0 * clipPosition.z, clipPosition.w)"));
  EXPECT_NE(vertex.find("material.clipSpaceTransform[3] = devicePosition"),
            std::string::npos);
}

TEST(GroundGridMaterialTests,
     PerspectiveUsesConfiguredColorsAndCoverageWithoutCameraRelativeFade) {
  const std::string source =
      read_source_file("resources/materials/ground_grid.mat");
  const std::size_t fragment_start =
      source.find("void material(inout MaterialInputs material)");
  ASSERT_NE(fragment_start, std::string::npos);
  const std::string fragment = source.substr(fragment_start);
  const std::string perspective = source_slice(
      fragment, "if (materialParams.analyticGridEnabled > 0.5)", "} else {");
  EXPECT_NE(perspective.find("analyticGroundGrid("), std::string::npos);
  EXPECT_EQ(perspective.find("length("), std::string::npos);
  EXPECT_EQ(perspective.find("smoothstep("), std::string::npos);
  EXPECT_EQ(perspective.find("discard"), std::string::npos);
  EXPECT_NE(perspective.find("color = grid.rgb"), std::string::npos);
  EXPECT_NE(perspective.find("alpha = grid.a"), std::string::npos);

  const std::string analytic = source_slice(
      source, "vec4 analyticGroundGrid(",
      "void material(inout MaterialInputs material)");
  EXPECT_NE(analytic.find("vec4 gridColor = materialParams.gridColor"),
            std::string::npos);
  EXPECT_NE(analytic.find("vec4 majorGridColor = materialParams.majorGridColor"),
            std::string::npos);
  EXPECT_NE(analytic.find("color.a *= coverageSum / 9.0"), std::string::npos);
  EXPECT_EQ(source.find("sampler2d"), std::string::npos);

  const std::string orthographic = source_slice(
      fragment, "} else {", "alpha = clamp(alpha * materialParams.gridIntensity,");
  EXPECT_NE(orthographic.find("length(coords - cameraCoords)"),
            std::string::npos);
  EXPECT_NE(orthographic.find("distanceFade * edgeFade"), std::string::npos);
  EXPECT_NE(orthographic.find("alpha *= fade"), std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      fragment, "alpha = clamp(alpha * materialParams.gridIntensity, 0.0, 1.0)"));
}

TEST(GroundGridMaterialTests,
     InstanceEditGridUsesSettingsFadeWithoutReplacingAuthoredColors) {
  const std::string source =
      read_source_file("src/render/viewport_grid_renderer.cpp");
  EXPECT_EQ(source.find("instance_edit_grid_color("), std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      source, "instance_edit_style.resolve_grid_color(settings.color(id), "
              "instance_edit_context)"));
  for (const std::string_view color_binding : {
           "\"gridColor\", grid_color(ViewportColorSettingId::GridMinor)",
           "\"majorGridColor\", grid_color(ViewportColorSettingId::GridMajor)",
           "\"originUColor\", grid_color(grid.axes.u_color)",
           "\"originVColor\", grid_color(grid.axes.v_color)"}) {
    EXPECT_TRUE(source_contains_compact(source, color_binding)) << color_binding;
  }

  const std::size_t signature_begin =
      source.find("std::uint64_t ground_grid_presentation_signature(");
  const std::size_t signature_end =
      source.find("filament::math::mat4f ground_grid_transform(", signature_begin);
  ASSERT_NE(signature_begin, std::string::npos);
  ASSERT_NE(signature_end, std::string::npos);
  const std::string signature =
      source.substr(signature_begin, signature_end - signature_begin);
  EXPECT_NE(signature.find(".add_bool(instance_edit_context)"),
            std::string::npos);
  EXPECT_NE(signature.find("ViewportFloatSettingId::InstanceEditGridFade"),
            std::string::npos);
  const std::size_t unchanged_gate =
      source.find("if (presentation_signature_ == presentation_signature)");
  const std::size_t resolve_style =
      source.find("InstanceEditStyle::from_settings(settings)");
  ASSERT_NE(unchanged_gate, std::string::npos);
  ASSERT_NE(resolve_style, std::string::npos);
  EXPECT_LT(unchanged_gate, resolve_style);
}

TEST(QuaderMeshMaterialTests, UsesOverlaySelectionInsteadOfStandaloneTint) {
  const std::string source =
      read_source_file("resources/materials/quader_mesh.mat");

  EXPECT_EQ(source.find("selectionTint"), std::string::npos);
  EXPECT_EQ(source.find("getColor()"), std::string::npos);
  EXPECT_EQ(source.find("vertexColor"), std::string::npos);
  EXPECT_EQ(source.find("color,"), std::string::npos);
}

TEST(QuaderMeshMaterialTests,
     InstanceEditContextSharesShaderAndUniformContractAcrossMeshesAndWires) {
  const std::string cmake = read_source_file("CMakeLists.txt");
  const std::string shared_shader =
      read_source_file("resources/materials/instance_edit.glsl");
  const std::string binding =
      read_source_file("src/render/material_binding_service.cpp");
  EXPECT_EQ(count_occurrences(shared_shader, "vec4 applyInstanceEditContext("),
            1u);
  for (const std::string_view material_name : {
           "quader_mesh", "quader_mesh_lit", "quader_selected_object_line"}) {
    SCOPED_TRACE(material_name);
    const std::string source = read_source_file(
        "resources/materials/" + std::string(material_name) + ".mat");
    EXPECT_NE(source.find("#include \"instance_edit.glsl\""),
              std::string::npos);
    EXPECT_EQ(source.find("vec4 applyInstanceEditContext("), std::string::npos);
    EXPECT_EQ(source.find("applyInstanceEditDim("), std::string::npos);
    EXPECT_NE(source.find("applyInstanceEditContext("), std::string::npos);
    EXPECT_TRUE(source_contains_compact(
        source, "materialParams.instanceEditDim * "
                "materialParams.instanceEditContextStrength"));
    for (const std::string_view uniform : {
             "instanceEditContextColor", "instanceEditContextStrength",
             "instanceEditContextTintAmount", "instanceEditContextDesaturation"}) {
      EXPECT_TRUE(source_contains_compact(
          source, "name : " + std::string(uniform))) << uniform;
      EXPECT_NE(source.find("materialParams." + std::string(uniform)),
                std::string::npos) << uniform;
      EXPECT_NE(binding.find("\"" + std::string(uniform) + "\""),
                std::string::npos) << uniform;
    }

    const std::size_t material_begin = cmake.find(
        "quader_register_filament_material(" + std::string(material_name) +
        "_material ");
    ASSERT_NE(material_begin, std::string::npos);
    const std::size_t material_end = cmake.find(')', material_begin);
    ASSERT_NE(material_end, std::string::npos);
    EXPECT_NE(cmake.substr(material_begin, material_end - material_begin)
                  .find("resources/materials/instance_edit.glsl"),
              std::string::npos);
  }

  EXPECT_TRUE(source_contains_compact(
      binding, "bind_instance_edit_context_style(material_instance, "
               "InstanceEditStyle::from_settings(settings))"));
  EXPECT_TRUE(source_contains_compact(
      binding, "setParameter(\"instanceEditContextColor\", "
               "setting_color(style.context))"));
  for (const std::string_view member : {
           "context_strength", "context_tint_amount", "context_desaturation"}) {
    EXPECT_NE(binding.find("style." + std::string(member)), std::string::npos);
    EXPECT_TRUE(source_contains_compact(
        binding, ".add_float(instance_edit_style." + std::string(member) + ")"));
  }
}

TEST(QuaderMeshMaterialTests,
     TextureSamplingUsesPortableChartsAndPerInstanceTransforms) {
  const std::string vertex_helpers =
      read_source_file("resources/materials/mesh_vertex_normal_mapping.glsl");
  EXPECT_TRUE(source_contains_compact(vertex_helpers,
      "return materialParams.coarseGeometryEnabled > 0.5 ? int(getCustom6().x) : getInstanceIndex()"));
  EXPECT_TRUE(source_contains_compact(vertex_helpers,
      "return materialParams.coarseGeometryEnabled > 0.5 ? getCustom5().xyz : getPosition().xyz"));
  const std::array<std::string, 2> sources{
      read_source_file("resources/materials/quader_mesh.mat"),
      read_source_file("resources/materials/quader_mesh_lit.mat"),
  };
  std::array<std::string, 2> vertex_blocks;
  for (std::size_t index = 0; index < sources.size(); ++index) {
    const std::string &source = sources[index];
    EXPECT_TRUE(source_contains_compact(source, "flipUV : true"));
    const std::size_t requires_begin = source.find("requires : [");
    ASSERT_NE(requires_begin, std::string::npos);
    const std::size_t requires_end = source.find(']', requires_begin);
    ASSERT_NE(requires_end, std::string::npos);
    const std::string_view required_attributes(
        source.data() + requires_begin, requires_end - requires_begin);
    for (const std::string_view attribute :
         {"uv0", "tangents", "custom0", "custom1", "custom2", "custom3",
          "custom4"}) {
      EXPECT_NE(required_attributes.find(attribute), std::string_view::npos)
          << attribute;
    }
    EXPECT_TRUE(source_contains_compact(
        source, "type : mat4[64], name : textureSamplingFromLocal"));
    EXPECT_TRUE(source_contains_compact(
        source, "type : mat3[64], name : textureSamplingCofactor"));
    const std::size_t vertex_begin = source.find("vertex {");
    const std::size_t vertex_end = source.find("\nfragment {", vertex_begin);
    ASSERT_NE(vertex_begin, std::string::npos);
    ASSERT_NE(vertex_end, std::string::npos);
    const std::string vertex =
        source.substr(vertex_begin, vertex_end - vertex_begin);
    vertex_blocks[index] = vertex;
    EXPECT_NE(vertex.find("#include \"mesh_vertex_normal_mapping.glsl\""),
              std::string::npos);
    EXPECT_NE(vertex.find("int instanceIndex = meshOccurrenceIndex()"),
              std::string::npos);
    EXPECT_NE(vertex.find("textureSamplingFromLocal[instanceIndex]"),
              std::string::npos);
    EXPECT_NE(vertex.find("textureSamplingCofactor[instanceIndex]"),
              std::string::npos);
    EXPECT_NE(vertex.find("geometricNormal = getCustom2().xyz"),
              std::string::npos);
    EXPECT_NE(vertex.find("carrier = getCustom1()"), std::string::npos);
    EXPECT_NE(vertex.find("if (all(equal(carrier, vec4(0.0))))"),
              std::string::npos);
    EXPECT_NE(vertex.find("samplingNormal / normalMagnitude"),
              std::string::npos);
    EXPECT_LT(vertex.find("samplingNormal / normalMagnitude"),
              vertex.find("generatedTextureUv(samplingFromLocal[0].xyz, samplingNormal)"));
    EXPECT_TRUE(source_contains_compact(
        vertex, "chartDelta = generatedTextureUv("
                "samplingFromLocal[3].xyz, geometricNormal)"));
    EXPECT_NE(vertex.find("if (linearSampling)"), std::string::npos);
    EXPECT_TRUE(source_contains_compact(
        vertex, "generatedTextureUv(samplingFromLocal[0].xyz, samplingNormal)"
                " - generatedTextureUv(vec3(1.0, 0.0, 0.0), geometricNormal)"));
    EXPECT_TRUE(source_contains_compact(
        vertex, "chartDelta = deltaX * position.x + deltaY * position.y"
                " + deltaZ * position.z"
                " + generatedTextureUv(samplingFromLocal[3].xyz, samplingNormal)"));
    EXPECT_TRUE(source_contains_compact(
        vertex, "dot(carrier.xy, chartDelta), dot(carrier.zw, chartDelta)"));
    EXPECT_TRUE(source_contains_compact(
        vertex, "vec3(position.x, -position.z, position.y)"));
    EXPECT_TRUE(source_contains_compact(
        vertex, "dominant.x >= dominant.y && dominant.x >= dominant.z"));
    EXPECT_NE(vertex.find("if (dominant.y >= dominant.z)"),
              std::string::npos);
    EXPECT_NE(vertex.find("sourceNormal.x >= 0.0 ? 1.0 : -1.0"),
              std::string::npos);
    EXPECT_NE(vertex.find("sourceNormal.y >= 0.0 ? 1.0 : -1.0"),
              std::string::npos);
    EXPECT_NE(vertex.find("sourceNormal.z >= 0.0 ? 1.0 : -1.0"),
              std::string::npos);
    const std::size_t identity_bypass =
        vertex.find("if (samplingFromLocal == mat4(1.0))");
    const std::size_t position_read = vertex.find("position = meshLocalPosition()");
    ASSERT_NE(identity_bypass, std::string::npos);
    ASSERT_NE(position_read, std::string::npos);
    EXPECT_LT(identity_bypass, position_read);
    // Filament's automatic UV flip is already applied at materialVertex entry;
    // preserve the existing fragment unflip when adding the canonical UV delta.
    EXPECT_TRUE(source_contains_compact(
        vertex, "material.uv0 += vec2(uvCorrection.x, -uvCorrection.y)"));
    EXPECT_NE(source.find("vec2(getUV0().x, 1.0 - getUV0().y)"),
              std::string::npos);
    EXPECT_EQ(vertex.find("material.worldPosition ="), std::string::npos);
    EXPECT_EQ(vertex.find("inverse(samplingFromLocal)"), std::string::npos);
  }
  EXPECT_EQ(vertex_blocks[0], vertex_blocks[1]);
}

TEST(QuaderMeshMaterialTests,
     SharedQuadCorrectionFeedsTheFinalMaterialUvAndNormalDerivativePath) {
  const std::string quad_source =
      read_source_file("resources/materials/mesh_quad_uv.glsl");
  const std::size_t fallback_gate =
      quad_source.find("if (variable_quadUvCoordinates.w <= 0.5)");
  const std::size_t coordinate_read =
      quad_source.find("coordinate = variable_quadUvCoordinates.xyz");
  ASSERT_NE(fallback_gate, std::string::npos);
  ASSERT_NE(coordinate_read, std::string::npos);
  EXPECT_LT(fallback_gate, coordinate_read);
  EXPECT_NE(quad_source.find("highp vec2 applyQuadUvInterpolation"),
            std::string::npos);
  EXPECT_NE(quad_source.find("variable_quadUvCorrection.xy"),
            std::string::npos);
  EXPECT_NE(quad_source.find("variable_quadUvCorrection.zw"),
            std::string::npos);
  EXPECT_NE(quad_source.find("isnan(discriminant)"), std::string::npos);
  EXPECT_NE(quad_source.find("isinf(discriminant)"), std::string::npos);
  EXPECT_NE(quad_source.find("any(isnan(corrected))"), std::string::npos);
  EXPECT_NE(quad_source.find("any(isinf(corrected))"), std::string::npos);

  for (const std::string_view material : {"quader_mesh.mat",
                                         "quader_mesh_lit.mat"}) {
    const std::string source =
        read_source_file("resources/materials/" + std::string(material));
    EXPECT_NE(source.find("#include \"mesh_quad_uv.glsl\""),
              std::string::npos);
    EXPECT_TRUE(source_contains_compact(
        source, "material.quadUvCoordinates = getCustom3()"));
    EXPECT_TRUE(source_contains_compact(
        source, "material.quadUvCorrection = getCustom4()"));
    EXPECT_TRUE(source_contains_compact(
        source, "material.textureSamplingCorrection.x = max("
                "material.textureSamplingCorrection.x,"
                "material.quadUvCoordinates.w)"));
    EXPECT_TRUE(source_contains_compact(
        source, "uv = applyTextureTransform(applyQuadUvInterpolation("
                "vec2(getUV0().x, 1.0 - getUV0().y)))"));
  }
}

TEST(QuaderMeshMaterialTests, UsesLitTextureTransformTimesColorAndPbrFactors) {
  const std::string shaded_source =
      read_source_file("resources/materials/quader_mesh.mat");
  const std::string source =
      read_source_file("resources/materials/quader_mesh_lit.mat");
  const std::string normal_mapping =
      read_source_file("resources/materials/mesh_normal_mapping.glsl");

  EXPECT_NE(shaded_source.find("shadingModel : unlit"), std::string::npos);
  EXPECT_NE(shaded_source.find("uv = applyTextureTransform("),
            std::string::npos);
  EXPECT_NE(shaded_source.find(
                "(uv - materialParams.textureTransformCenter)"),
            std::string::npos);
  EXPECT_NE(shaded_source.find(
                "* materialParams.textureTransformScale"),
            std::string::npos);
  EXPECT_NE(shaded_source.find(
                "+ materialParams.textureTransformOffset"),
            std::string::npos);
  EXPECT_NE(shaded_source.find("texture(materialParams_baseColorTexture, uv)"),
            std::string::npos);
  EXPECT_NE(shaded_source.find(
                "baseColor = baseSample.rgb * "
                "materialParams.baseColorFactor.rgb"),
            std::string::npos);
  EXPECT_NE(shaded_source.find(
                "sourceAlpha = baseSample.a * "
                "materialParams.baseColorFactor.a"),
            std::string::npos);
  EXPECT_NE(shaded_source.find("vec4(baseColor, sourceAlpha)"),
            std::string::npos);
  EXPECT_NE(shaded_source.find(
                "material.baseColor = vec4(edited.rgb * edited.a, edited.a)"),
            std::string::npos);
  EXPECT_NE(shaded_source.find("normalize(vec3(-0.09, 0.47, 0.88))"),
            std::string::npos);
  EXPECT_NE(shaded_source.find(
                "(dot(normalView, keyDirectionView) + diffuseWrap)"),
            std::string::npos);
  EXPECT_NE(shaded_source.find(
                "dot(normalWorld, worldUp) * 0.5 + 0.5"),
            std::string::npos);
  EXPECT_NE(shaded_source.find(
                "min(ambient + keyIntensity * wrappedDiffuse, 1.0)"),
            std::string::npos);
  EXPECT_NE(shaded_source.find("name : shadedKeyDirectionX"),
            std::string::npos);
  EXPECT_NE(shaded_source.find("name : shadedKeyDirectionY"),
            std::string::npos);
  EXPECT_NE(shaded_source.find("name : shadedKeyDirectionZ"),
            std::string::npos);
  EXPECT_NE(shaded_source.find("baseColor *= lighting"), std::string::npos);
  EXPECT_EQ(shaded_source.find("roughnessTexture"), std::string::npos);
  EXPECT_EQ(shaded_source.find("metallicTexture"), std::string::npos);
  EXPECT_NE(shaded_source.find("name : normalTexture"), std::string::npos);
  EXPECT_EQ(shaded_source.find("shadedModelingSpecular"), std::string::npos);
  EXPECT_NE(shaded_source.find("getWorldViewVector"), std::string::npos);
  EXPECT_NE(shaded_source.find("shadedSurfaceColor(vec3 albedo"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      shaded_source,
      "if (solidPresentation) { float lighting = mix(1.0, "
      "shadedModelingLighting(), lightingEnabled); baseColor *= lighting; } "
      "else { baseColor = mix(baseColor, shadedSurfaceColor(baseColor, normalWorld), "
      "lightingEnabled); }"));
  EXPECT_EQ(shaded_source.find("editor_unlit"), std::string::npos);
  EXPECT_NE(source.find("shadingModel : lit"), std::string::npos);
  EXPECT_EQ(source.find("doubleSided : true"), std::string::npos);
  EXPECT_NE(source.find("vec2 uv = applyTextureTransform("),
            std::string::npos);
  EXPECT_NE(source.find("cos(materialParams.textureTransformRotationRadians)"),
            std::string::npos);
  EXPECT_NE(source.find("sin(materialParams.textureTransformRotationRadians)"),
            std::string::npos);
  EXPECT_NE(source.find("texture(materialParams_baseColorTexture, uv)"),
            std::string::npos);
  EXPECT_NE(source.find("texture(materialParams_roughnessTexture, uv).r"),
            std::string::npos);
  EXPECT_NE(source.find("baseSample.rgb * materialParams.baseColorFactor.rgb"),
            std::string::npos);
  EXPECT_NE(source.find(
                "float sourceAlpha = baseSample.a * "
                "materialParams.baseColorFactor.a"),
            std::string::npos);
  EXPECT_NE(source.find(
                "material.baseColor = vec4(edited.rgb * edited.a, edited.a)"),
            std::string::npos);
  EXPECT_NE(source.find("roughnessSample * materialParams.roughnessFactor"),
            std::string::npos);
  EXPECT_NE(source.find("name : metallicTexture"), std::string::npos);
  EXPECT_NE(source.find("name : normalTexture"), std::string::npos);
  EXPECT_NE(source.find(
                "texture(materialParams_metallicTexture, uv).r"),
            std::string::npos);
  EXPECT_NE(normal_mapping.find(
                "texture(materialParams_normalTexture, uv).xyz"),
            std::string::npos);
  EXPECT_NE(normal_mapping.find("normalSample.y *= materialParams.normalYSign"),
            std::string::npos);
  EXPECT_NE(normal_mapping.find("normalSample.xy *= "
                        "clamp(materialParams.normalStrength, -16.0, 16.0)"),
            std::string::npos);
  EXPECT_NE(source.find("metallicSample * materialParams.metallicFactor"),
            std::string::npos);
  EXPECT_NE(source.find("material.normal = normalSample"), std::string::npos);
  EXPECT_EQ(source.find("editor_unlit"), std::string::npos);
}

TEST(QuaderMeshMaterialTests,
     SharesNormalMapSamplingBeforeFilamentPreparesEachMaterial) {
  const std::string source =
      read_source_file("resources/materials/mesh_normal_mapping.glsl");
  for (const char *path : {"resources/materials/quader_mesh.mat",
                           "resources/materials/quader_mesh_lit.mat"}) {
    SCOPED_TRACE(path);
    const std::string material = read_source_file(path);
    EXPECT_NE(material.find("#include \"mesh_normal_mapping.glsl\""),
              std::string::npos);
    const std::size_t fragment_start =
        material.find("void material(inout MaterialInputs material)");
    ASSERT_NE(fragment_start, std::string::npos);
    const std::string fragment = material.substr(fragment_start);
    const std::size_t sample = fragment.find("sampleTextureNormal(uv)");
    ASSERT_NE(sample, std::string::npos);
    std::size_t assignment =
        fragment.find("material.normal = sampleTextureNormal(uv)");
    if (assignment == std::string::npos) {
      assignment = fragment.find("material.normal = normalSample");
    }
    const std::size_t prepare =
        fragment.find("prepareMaterial(material)", sample);
    ASSERT_NE(assignment, std::string::npos);
    ASSERT_NE(prepare, std::string::npos);
    EXPECT_LT(sample, prepare);
    EXPECT_LT(assignment, prepare);
    const std::string before_sample = fragment.substr(0u, sample);
    if (before_sample.find("prepareMaterial(material)") != std::string::npos) {
      // The UV diagnostic terminates with a neutral normal before textured PBR.
      const std::string diagnostic = source_slice(
          fragment, "if (debugMode == 7.0) {",
          "vec3 normalSample = sampleTextureNormal(uv);");
      EXPECT_EQ(count_occurrences(before_sample, "prepareMaterial(material)"),
                1u);
      EXPECT_EQ(count_occurrences(diagnostic, "prepareMaterial(material)"), 1u);
      EXPECT_TRUE(source_contains_compact(
          diagnostic, "material.normal = vec3(0.0, 0.0, 1.0); "
                      "prepareMaterial(material);"));
      EXPECT_TRUE(source_contains_compact(diagnostic, "return; }"));
    }
  }
  EXPECT_NE(source.find("return normalForTextureUv(normalSample, uv)"),
            std::string::npos);
  EXPECT_NE(source.find("normalSample = safeTextureNormal("),
            std::string::npos);
  EXPECT_NE(source.find("uvDx = dFdx(uv)"), std::string::npos);
  EXPECT_NE(source.find("uvDy = dFdy(uv)"), std::string::npos);
  EXPECT_NE(source.find("dFdx(getWorldPosition())"), std::string::npos);
  EXPECT_NE(source.find("dFdy(getWorldPosition())"), std::string::npos);
  EXPECT_NE(source.find("inverse(worldTangentFrame) * mappedWorldNormal"),
            std::string::npos);
  EXPECT_NE(source.find("isnan(frameDeterminant) || isinf(frameDeterminant)"),
            std::string::npos);
  const std::size_t unchanged_chart_gate =
      source.find("if (variable_textureSamplingCorrection.x <= 0.5");
  ASSERT_NE(unchanged_chart_gate, std::string::npos);
  EXPECT_LT(unchanged_chart_gate, source.find("uvDx = dFdx(uv)"));
  EXPECT_TRUE(source_contains_compact(
      source, "materialParams.textureTransformRotationRadians == 0.0"
              " && all(equal(materialParams.textureTransformScale, vec2(1.0)))"));
}

TEST(QuaderMeshMaterialTests,
     RenderedMaterialSupportsPerViewportPbrDebugViews) {
  const std::string source =
      read_source_file("resources/materials/quader_mesh_lit.mat");
  const std::string renderer =
      read_source_file("src/render/viewport_renderer.cpp");

  EXPECT_NE(source.find("postLightingBlending : opaque"), std::string::npos);
  EXPECT_NE(source.find("getMaterialGlobal0().x"), std::string::npos);
  EXPECT_NE(source.find("material.postLightingColor"), std::string::npos);
  EXPECT_NE(source.find("material.postLightingMixFactor"),
            std::string::npos);
  EXPECT_NE(source.find("material.reflectance = 0.0"), std::string::npos);
  EXPECT_NE(source.find("mix(vec3(0.04), albedo, metallic)"),
            std::string::npos);
  EXPECT_NE(source.find("getWorldTangentFrame() * normalSample"),
            std::string::npos);
  EXPECT_NE(source.find("debugColor * sourceAlpha, sourceAlpha"),
            std::string::npos);
  EXPECT_NE(renderer.find("view_->setMaterialGlobal("), std::string::npos);
  EXPECT_NE(renderer.find("ViewportMaterialDebugMode mode"),
            std::string::npos);
}

TEST(QuaderMeshMaterialTests, ShadedPreviewBindsBaseColorAndNormalSurfaceInputs) {
  const std::string source =
      read_source_file("src/render/material_preview_renderer.cpp");

  EXPECT_EQ(source.find("const auto bind_surface_parameters"),
            std::string::npos);
  EXPECT_NE(source.find(
                "shaded_instance->setParameter(\"baseColorTexture\""),
            std::string::npos);
  EXPECT_NE(source.find(
                "rendered_instance->setParameter(\"roughnessTexture\""),
            std::string::npos);
  EXPECT_NE(source.find("shaded_instance->setParameter(\"normalTexture\""),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      source, "wire_instance->setParameter(\"normalTexture\", fallback_textures[3], sampler)"));
  EXPECT_NE(source.find("wire_instance->setParameter(\"normalStrength\", 0.0f)"),
            std::string::npos);
}

TEST(QuaderMeshMaterialTests,
     SolidPresentationUsesOnePerViewNeutralSamplerContract) {
  const std::string material =
      read_source_file("resources/materials/quader_mesh.mat");
  const std::string binding =
      read_source_file("src/render/material_binding_service.cpp");
  const std::string preview =
      read_source_file("src/render/material_preview_renderer.cpp");
  const std::string textures =
      read_source_file("src/render/texture_binding_service.cpp");
  const std::string render_system_header =
      read_source_file("src/render/render_system.hpp");
  const std::string render_system_source =
      read_source_file("src/render/render_system.cpp");
  const std::string scene_bridge_source =
      read_source_file("src/render/scene_render_bridge.cpp");
  const std::string renderer_header =
      read_source_file("src/render/viewport_renderer.hpp");
  const std::string renderer_source =
      read_source_file("src/render/viewport_renderer.cpp");

  EXPECT_NE(material.find("name : solidColorTexture"), std::string::npos);
  EXPECT_NE(material.find("getMaterialGlobal0().y >= 0.5"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      material,
      "if (solidPresentation) { baseColor = texture("
      "materialParams_solidColorTexture, vec2(0.5)).rgb; "
      "sourceAlpha = 1.0; } else { vec4 baseSample = texture("));
  EXPECT_TRUE(source_contains_compact(
      material, "material.normal = vec3(0.0, 0.0, 1.0); if (!solidPresentation) {"));
  EXPECT_TRUE(source_contains_compact(
      material, "if (materialParams.shadedLightingEnabled > 0.0) { "
                "material.normal = sampleTextureNormal(uv); }"));
  EXPECT_NE(material.find("materialParams_baseColorTexture"),
            std::string::npos);
  EXPECT_NE(material.find("materialParams.baseColorFactor"),
            std::string::npos);
  EXPECT_NE(material.find("baseColor = applySurfaceGrid(baseColor)"),
            std::string::npos);
  EXPECT_NE(material.find("applyInstanceEditContext("), std::string::npos);

  EXPECT_EQ(count_occurrences(binding, "\"solidColorTexture\""), 2u)
      << "both embedded and projected shaded materials need the sampler";
  EXPECT_NE(binding.find("texture_binding_->solid_color_texture()"),
            std::string::npos);
  EXPECT_NE(preview.find(
                "shaded_instance->setParameter(\"solidColorTexture\""),
            std::string::npos);
  EXPECT_NE(preview.find(
                "wire_instance->setParameter(\"solidColorTexture\""),
            std::string::npos);
  EXPECT_NE(preview.find("filament::Texture &solid_color_texture"),
            std::string::npos);
  EXPECT_EQ(preview.find("setParameter(\"solidColorTexture\", "
                         "fallback_textures[0]"),
            std::string::npos)
      << "previews must borrow the same RenderSystem-owned Solid texture";

  EXPECT_TRUE(source_contains_compact(
      render_system_source,
      "resource_root_ / \"textures\" / \"default_texture.png\""));
  EXPECT_EQ(count_occurrences(render_system_source, "default_texture.png"),
            1u);
  EXPECT_NE(render_system_source.find(
                ".color_space = PngTextureColorSpace::Srgb"),
            std::string::npos);
  EXPECT_NE(render_system_source.find(".generate_mipmaps = true"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      render_system_source,
      "std::byte{181u}, std::byte{181u}, std::byte{181u}, "
      "std::byte{255u}"));
  EXPECT_NE(render_system_header.find(
                "filament::Texture *solid_color_texture_ = nullptr"),
            std::string::npos);
  EXPECT_NE(render_system_source.find(
                "engine_->destroy(solid_color_texture_)"),
            std::string::npos);
  EXPECT_GE(count_occurrences(render_system_source,
                              "*solid_color_texture_"),
            2u)
      << "the scene bridge and material previews share one texture";
  EXPECT_EQ(textures.find("default_texture.png"), std::string::npos);
  EXPECT_NE(textures.find(
                "solid_color_texture_ = &solid_color_texture"),
            std::string::npos);
  EXPECT_EQ(textures.find("destroy_texture(solid_color_texture_)"),
            std::string::npos)
      << "TextureBindingService only borrows the RenderSystem texture";
  EXPECT_NE(scene_bridge_source.find(
                "texture_binding_.initialize(engine, resource_root, "
                "solid_color_texture)"),
            std::string::npos);

  EXPECT_NE(renderer_header.find("set_material_presentation("),
            std::string::npos);
  EXPECT_NE(renderer_header.find("solid_material_presentation_"),
            std::string::npos);
  EXPECT_NE(renderer_header.find("has_material_presentation_"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      renderer_source,
      "if (has_material_presentation_ && material_debug_mode_ == mode && "
      "solid_material_presentation_ == solid && "
      "mesh_grid_visible_ == mesh_grid_visible) { return; }"));
  EXPECT_TRUE(source_contains_compact(
      renderer_source,
      "float4{static_cast<float>(mode), solid ? 1.0f : 0.0f, "
      "mesh_grid_visible ? 0.0f : 1.0f, 1.0f}"));
  EXPECT_EQ(count_occurrences(renderer_source, "view_->setMaterialGlobal("),
            1u)
      << "debug and Solid state must be composed into one cached view global";
}

TEST(SceneRenderBridgeTests,
     SolidAndShadedShareTheExistingEditorProxyLayer) {
  const std::string layers =
      read_source_file("src/render/render_layers.hpp");
  const std::string mesh_header =
      read_source_file("src/render/mesh_renderable_cache.hpp");
  const std::string mesh_source =
      read_source_file("src/render/mesh_renderable_cache.cpp");
  const std::string instance_header =
      read_source_file("src/render/instance_renderable_cache.hpp");
  const std::string instance_source =
      read_source_file("src/render/instance_renderable_cache.cpp");
  const std::string preview_header =
      read_source_file("src/render/tool_preview_renderable_cache.hpp");
  const std::string preview_source =
      read_source_file("src/render/tool_preview_renderable_cache.cpp");

  EXPECT_EQ(layers.find("kViewportSolidSceneLayerMask"), std::string::npos);
  EXPECT_EQ(mesh_header.find("solid_mesh_proxies_"), std::string::npos);
  EXPECT_EQ(mesh_source.find("solid_mesh_proxies_"), std::string::npos);
  EXPECT_EQ(instance_header.find("solid_instance_proxies_"),
            std::string::npos);
  EXPECT_EQ(instance_source.find("solid_instance_proxies_"),
            std::string::npos);
  EXPECT_EQ(preview_header.find("solid_preview_proxies_"),
            std::string::npos);
  EXPECT_EQ(preview_source.find("solid_preview_proxies_"),
            std::string::npos);
  EXPECT_NE(mesh_header.find("shaded_mesh_proxies_"), std::string::npos);
  EXPECT_NE(mesh_source.find("kViewportShadedSceneLayerMask"),
            std::string::npos);
  EXPECT_NE(instance_source.find("kViewportShadedSceneLayerMask"),
            std::string::npos);
  EXPECT_NE(preview_source.find("kViewportShadedSceneLayerMask"),
            std::string::npos);
}

TEST(QuaderMeshMaterialTests,
     SharedSurfaceGridIncludeIsTrackedForBothMaterials) {
  const std::string cmake = read_source_file("CMakeLists.txt");
  for (const std::string name : {"quader_mesh", "quader_mesh_lit"}) {
    SCOPED_TRACE(name);
    const std::string material =
        read_source_file("resources/materials/" + name + ".mat");
    EXPECT_EQ(count_occurrences(material, "#include \"mesh_surface_grid.glsl\""),
              1u);
    EXPECT_EQ(material.find("vec4 surfaceGridOverlay() {"), std::string::npos);
    const std::string registration = source_slice(
        cmake, "quader_register_filament_material(" + name + "_material", ")");
    EXPECT_NE(registration.find("resources/materials/mesh_surface_grid.glsl"),
              std::string::npos);
  }
}

TEST(QuaderMeshMaterialTests, SupportsOptionalWorldSpaceSurfaceGrid) {
  const std::string vertex_helpers =
      read_source_file("resources/materials/mesh_vertex_normal_mapping.glsl");
  const std::string world_normal = source_slice(vertex_helpers,
      "vec3 meshSurfaceWorldNormal(", "bool requiresWorldTextureNormalCorrection(");
  EXPECT_NE(world_normal.find("if (materialParams.coarseGeometryEnabled > 0.5)"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(world_normal,
      "return materialParams.coarseWorldNormalFromLocal[meshOccurrenceIndex()] * localNormal"));
  EXPECT_NE(world_normal.find("return getWorldFromModelNormalMatrix() * localNormal"),
            std::string::npos);
  const std::array<std::string, 2> sources = {
      read_source_file("resources/materials/quader_mesh.mat"),
      read_source_file("resources/materials/quader_mesh_lit.mat"),
  };
  const std::string source =
      read_source_file("resources/materials/mesh_surface_grid.glsl");

  for (const std::string &material : sources) {
    EXPECT_NE(material.find("surfaceGridMinorColor"), std::string::npos);
    EXPECT_NE(material.find("surfaceGridMajorColor"), std::string::npos);
    EXPECT_NE(material.find("surfaceGridEnabled"), std::string::npos);
    EXPECT_NE(material.find("surfaceGridMinorLineThickness"), std::string::npos);
    EXPECT_NE(material.find("surfaceGridMajorLineThickness"), std::string::npos);
    EXPECT_NE(material.find("tangents"), std::string::npos);
    EXPECT_NE(material.find("custom0"), std::string::npos);
    EXPECT_NE(material.find("surfaceGridProjection"), std::string::npos);
    EXPECT_NE(material.find("getCustom0()"), std::string::npos);
    EXPECT_NE(material.find("#include \"mesh_vertex_normal_mapping.glsl\""),
              std::string::npos);
    EXPECT_NE(material.find("meshSurfaceWorldNormal(projection.xyz)"),
              std::string::npos);
  }

  EXPECT_NE(source.find("variable_surfaceGridProjection"),
            std::string::npos);
  EXPECT_NE(source.find("getUserWorldPosition()"), std::string::npos);
  EXPECT_NE(source.find("getWorldPosition()"), std::string::npos);
  EXPECT_NE(source.find(
                "vec3 surfaceGridProjectionWeights(vec3 normal)"),
            std::string::npos);
  EXPECT_EQ(count_occurrences(source, "weights *= weights;"), 2u);
  EXPECT_NE(source.find("return weights / weightSum"), std::string::npos);
  const std::size_t face_chart_begin = source.find(
      "vec3 surfaceGridFaceProjectionWeights(vec3 normal)");
  const std::size_t face_chart_end = source.find(
      "vec3 surfaceGridPlaneLineCoverages(", face_chart_begin);
  ASSERT_NE(face_chart_begin, std::string::npos);
  ASSERT_NE(face_chart_end, std::string::npos);
  const std::string_view face_chart(source.data() + face_chart_begin,
                                    face_chart_end - face_chart_begin);
  EXPECT_NE(face_chart.find(
                "dominant.x > dominant.y && dominant.x > dominant.z"),
            std::string::npos);
  EXPECT_NE(face_chart.find("dominant.z > dominant.y"), std::string::npos);
  EXPECT_NE(face_chart.find("return vec3(0.0, 1.0, 0.0)"),
            std::string::npos);
  EXPECT_NE(source.find(
                "vec3 surfaceGridPlaneLineCoverages("),
            std::string::npos);
  EXPECT_NE(source.find(
                "vec3 surfaceGridPlaneSlotCoverages("),
            std::string::npos);
  EXPECT_EQ(count_occurrences(
                source, "max(axisCoverages.y, axisCoverages.z)"),
            2u);
  EXPECT_EQ(count_occurrences(
                source, "max(axisCoverages.x, axisCoverages.z)"),
            2u);
  EXPECT_EQ(count_occurrences(
                source, "max(axisCoverages.x, axisCoverages.y)"),
            2u);
  EXPECT_TRUE(source_contains_compact(
      source, "vec3 projectionWeights = faceProjection.w > 0.5 ? "
              "surfaceGridFaceProjectionWeights(faceNormal) : "
              "surfaceGridProjectionWeights("
              "userWorldFromWorld * getWorldGeometricNormalVector())"));
  EXPECT_NE(source.find(
                "float minorLine = dot("),
            std::string::npos);
  EXPECT_NE(source.find(
                "float majorLine = dot(majorPlaneCoverages, "
                "projectionWeights)"),
            std::string::npos);
  EXPECT_EQ(source.find("surfaceGridCoordinates("), std::string::npos);
  EXPECT_EQ(source.find("surfaceGridFaceNormal("), std::string::npos);
  EXPECT_EQ(source.find("cross(dFdx("), std::string::npos);
  EXPECT_EQ(source.find("axisTieEpsilon"), std::string::npos);
  EXPECT_NE(face_chart.find("vec3 dominant = abs(normal)"), std::string::npos);
  EXPECT_EQ(face_chart.find("dominant.x >= dominant.y"), std::string::npos);
  EXPECT_EQ(face_chart.find("dominant.y >= dominant.z"), std::string::npos);
  EXPECT_NE(source.find("surfaceGridFloor("), std::string::npos);
  EXPECT_NE(source.find("surfaceGridLineCoverage("), std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      source, "float worldPixelWidth = max("
              "length(dFdx(rasterWorldPosition)),"
              "length(dFdy(rasterWorldPosition)))"));
  EXPECT_NE(source.find("surfaceGridMinorLineThickness"), std::string::npos);
  EXPECT_NE(source.find("surfaceGridMajorLineThickness"), std::string::npos);
  EXPECT_NE(source.find("float derivativeWidth = coordinatePixelWidth"),
            std::string::npos);
  EXPECT_NE(source.find("vec3 coordinatePixelWidths = abs(userWorldDx) + abs(userWorldDy)"),
            std::string::npos);
  EXPECT_NE(source.find("float lodSpacing = max(surfaceGridFloor"),
            std::string::npos);
  EXPECT_NE(source.find("return mix(currentCoverage, nextCoverage, blend);"),
            std::string::npos);
  EXPECT_EQ(source.find("surfaceGridVisibility("), std::string::npos);
  EXPECT_EQ(source.find("coverage * surfaceGridVisibility"),
            std::string::npos);
  EXPECT_EQ(count_occurrences(
                source,
                "float minRenderableSpacing = max(worldPixelWidth, 0.0) * "
                "4.0;"),
            2u);
  EXPECT_EQ(source.find("Strip test"), std::string::npos);
  EXPECT_EQ(source.find("vec2 coords = worldPosition.xz;"),
            std::string::npos);
  EXPECT_EQ(source.find("return coverage;"), std::string::npos);
  EXPECT_EQ(source.find("return coverage * min(thickness, 1.0)"),
            std::string::npos);

  const std::string &shaded_source = sources.front();
  const std::size_t lighting_start =
      shaded_source.find("baseColor *= lighting;");
  const std::size_t grid_start =
      shaded_source.find("baseColor = applySurfaceGrid(baseColor);");
  const std::size_t context_dim_start =
      shaded_source.find("vec4 edited = applyInstanceEditContext(");
  ASSERT_NE(lighting_start, std::string::npos);
  ASSERT_NE(grid_start, std::string::npos);
  ASSERT_NE(context_dim_start, std::string::npos);
  EXPECT_LT(lighting_start, grid_start);
  EXPECT_LT(grid_start, context_dim_start);
}

TEST(QuaderMeshMaterialTests,
     RenderedSurfaceGridComposesAfterPbrOutsideMaterialDiagnostics) {
  const std::string source =
      read_source_file("resources/materials/quader_mesh_lit.mat");
  EXPECT_NE(source.find("postLightingBlending : opaque"), std::string::npos);
  EXPECT_EQ(source.find("applySurfaceGrid(baseColor)"), std::string::npos);
  EXPECT_EQ(count_occurrences(source, "surfaceGridOverlay()"), 1u);
  EXPECT_TRUE(source_contains_compact(
      source, "if (debugMode == 0.0) { vec4 grid = surfaceGridOverlay();"));
  EXPECT_TRUE(source_contains_compact(
      source, "vec4 gridEdited = applyInstanceEditContext("
              "vec4(grid.rgb, sourceAlpha),"));
  EXPECT_TRUE(source_contains_compact(
      source, "material.postLightingColor = vec4("
              "gridEdited.rgb * gridEdited.a, gridEdited.a)"));
  EXPECT_NE(source.find("material.postLightingMixFactor = grid.a"),
            std::string::npos);

  const std::size_t diagnostic_mix = source.find(
      "debugMode >= 3.0 && debugMode <= 6.0 ? 1.0 : 0.0");
  const std::size_t grid_start = source.find("surfaceGridOverlay()");
  ASSERT_NE(diagnostic_mix, std::string::npos);
  ASSERT_NE(grid_start, std::string::npos);
  EXPECT_LT(diagnostic_mix, grid_start);
}

TEST(QuaderMeshMaterialTests,
     SurfaceGridMajorSlotsExcludeMinorCoverageIndependentOfMajorAlpha) {
  const std::string source =
      read_source_file("resources/materials/mesh_surface_grid.glsl");

  const std::size_t slot_function_start =
      source.find("float surfaceGridLineSlotCoverage(");
  const std::size_t projection_function_start =
      source.find("vec3 surfaceGridProjectionWeights(");
  ASSERT_NE(slot_function_start, std::string::npos);
  ASSERT_NE(projection_function_start, std::string::npos);
  ASSERT_LT(slot_function_start, projection_function_start);
  const std::string slot_function = source.substr(
      slot_function_start, projection_function_start - slot_function_start);

  EXPECT_NE(slot_function.find("return max(currentCoverage, nextCoverage)"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      source, "float majorSlotThickness = max("
              "materialParams.surfaceGridMinorLineThickness, "
              "materialParams.surfaceGridMajorLineThickness)"));
  EXPECT_NE(source.find("vec3 majorSlotMask = step(vec3(0.000001), "
                        "majorSlotCoverages)"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      source, "minorPlaneCoverages * (vec3(1.0) - majorSlotMask)"));
  EXPECT_NE(source.find("float majorCoverage = majorLine * "
                        "materialParams.surfaceGridMajorColor.a"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      source, "float minorCoverage = minorLine * "
              "materialParams.surfaceGridMinorColor.a"));
  EXPECT_EQ(source.find("1.0 - clamp(majorCoverage"), std::string::npos);
  EXPECT_EQ(source.find("float majorMask = clamp(majorLine"),
            std::string::npos);
}

TEST(QuaderMeshMaterialTests,
     SurfaceGridProjectionAttributeKeepsFaceNormalsStableAndPreviewCompatible) {
  const std::string factory_source =
      read_source_file("src/render/mesh_renderable_factory.cpp");
  const std::string projection_source =
      read_source_file("src/render/mesh_surface_grid_projection.cpp");
  const std::string preview_source =
      read_source_file("src/render/material_preview_renderer.cpp");

  EXPECT_NE(factory_source.find(
                "build_mesh_surface_grid_vertex_projections("),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      factory_source, "mesh.vertices, mesh.indices, mesh.primitives"));
  EXPECT_NE(factory_source.find("surface_grid_projection"),
            std::string::npos);
  EXPECT_NE(factory_source.find("filament::VertexAttribute::CUSTOM0"),
            std::string::npos);
  EXPECT_NE(factory_source.find("AttributeType::FLOAT4"),
            std::string::npos);

  const std::string projection_plan_source = source_slice(
      projection_source,
      "MeshSurfaceGridProjectionPlan prepare_mesh_surface_grid_projection(",
      "std::vector<MeshSurfaceGridVertexProjection>");
  const std::string projection_build_source = source_slice(
      projection_source,
      "std::span<const MeshVertex> vertices, const MeshSurfaceGridProjectionPlan &plan,",
      "std::span<const MeshVertex> vertices, std::span<const std::uint32_t> indices,");
  EXPECT_TRUE(source_contains_compact(
      projection_plan_source,
      "if (vertex < vertex_count && stamps[vertex] != face) { "
      "stamps[vertex] = face; ++references[vertex]; }"));
  EXPECT_TRUE(source_contains_compact(
      projection_plan_source,
      "binding.exclusive = std::ranges::all_of(binding.vertices, "
      "[&](auto vertex) { return references[vertex] == 1u; });"));
  EXPECT_TRUE(source_contains_compact(
      projection_build_source,
      "if (face.vertices.size() < 3u || !face.exclusive) continue;"));
  EXPECT_TRUE(source_contains_compact(
      projection_build_source,
      "area_normal += cross(position(triangle[1]) - position(triangle[0]), "
      "position(triangle[2]) - position(triangle[0]));"));
  EXPECT_TRUE(source_contains_compact(
      projection_build_source,
      "const auto normal = normalize(area_normal); "
      "if (face.vertices.size() != 3u && !std::ranges::all_of(face.vertices, "
      "[&](auto index) { const auto vertex_normal = normalize(vertices[index].normal); "
      "return length_squared(vertex_normal) > kNormalEpsilonSquared && "
      "dot(vertex_normal, normal) >= kFlatNormalAlignment; })) continue;"));
  EXPECT_TRUE(source_contains_compact(
      projection_build_source,
      "for (const auto vertex : face.vertices) projections[vertex] = {normal, true};"));
  EXPECT_TRUE(source_contains_compact(
      projection_build_source,
      "return positions.empty() ? vertices[index].position : positions[index];"));
  EXPECT_EQ(projection_source.find(
                "coalesced_render_mesh_primitive_spans"),
            std::string::npos);

  EXPECT_NE(preview_source.find("surface_grid_projection"),
            std::string::npos);
  EXPECT_NE(preview_source.find("filament::VertexAttribute::CUSTOM0"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      preview_source, "vertex.normal.z, 0.0f"));

  // Static, streamed and retained coarse geometry share the sampling contract.
  // Check each allocation path so an extra declaration cannot hide a missing
  // attribute in another path.
  const std::array<std::string, 3u> upload_paths{
      source_slice(factory_source, "bool MeshRenderableFactory::acquire_geometry(",
                   "bool MeshRenderableFactory::acquire_collision_geometry("),
      source_slice(factory_source, "bool MeshRenderableFactory::acquire_dynamic_geometry(",
                   "void MeshRenderableFactory::release_geometry("),
      source_slice(factory_source, "MeshRenderableProxy MeshRenderableFactory::create_editable_surface_geometry(",
                   "MeshRenderableProxy MeshRenderableFactory::create_editable_surface_proxy("),
  };
  for (const std::string_view vertex_type :
       {"RenderVertex", "DynamicRenderVertexStatic"}) {
    for (const std::string_view attribute :
         {"uv_carrier", "uv_geometric_normal", "quad_uv_coordinates",
          "quad_uv_correction"}) {
      EXPECT_NE(factory_source.find("offsetof(" + std::string(vertex_type) +
                                    ", " + std::string(attribute) + ")"),
                std::string::npos);
    }
  }
  for (const std::string_view attribute : {"CUSTOM1", "CUSTOM2", "CUSTOM3",
                                          "CUSTOM4"}) {
    const std::string qualified =
        "filament::VertexAttribute::" + std::string(attribute);
    for (const auto &upload : upload_paths) {
      EXPECT_EQ(count_occurrences(upload, qualified), 1u) << qualified;
    }
    EXPECT_NE(preview_source.find(qualified), std::string::npos);
  }
  for (const std::string_view attribute :
       {"uv_carrier", "uv_geometric_normal", "quad_uv_coordinates",
        "quad_uv_correction"}) {
    EXPECT_NE(preview_source.find("offsetof(PreviewGpuVertex, " +
                                  std::string(attribute) + ")"),
              std::string::npos);
  }
  EXPECT_TRUE(source_contains_compact(
      factory_source, "build_mesh_quad_uv_interpolations("
                      "mesh.vertices, mesh.indices, mesh.primitives, positions)"));
  EXPECT_EQ(count_occurrences(
                factory_source,
                "build_render_vertices<DynamicRenderVertexStatic>(mesh, dynamic_positions)"),
            2u);
  for (const std::string_view instance :
       {"shaded_instance", "rendered_instance", "wire_instance"}) {
    EXPECT_NE(preview_source.find("bind_mesh_texture_mappings(*" +
                                  std::string(instance) + ")"),
              std::string::npos);
  }
  const std::string binding_source =
      read_source_file("src/render/material_binding_service.cpp");
  const std::string binding_header =
      read_source_file("src/render/material_binding_service.hpp");
  EXPECT_NE(binding_header.find("kMeshTextureMappingCapacity = 64u"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      binding_source, "setParameter(\"textureSamplingFromLocal\","
                      " matrices.data(), count)"));
  EXPECT_TRUE(source_contains_compact(
      binding_source, "setParameter(\"textureSamplingCofactor\","
                      " cofactors.data(), count)"));
}

TEST(OverlayMaterialTests, PremultipliesVertexAlphaForTransparentFaceFill) {
  const std::string source =
      read_source_file("resources/materials/vertex_color_overlay.mat");

  EXPECT_NE(source.find("blending : transparent"), std::string::npos);
  EXPECT_NE(source.find("depthWrite : false"), std::string::npos);
  EXPECT_NE(source.find("vec4(vertexColor.rgb * vertexColor.a, vertexColor.a)"),
            std::string::npos);
}

TEST(OverlayMaterialTests,
     LineAndPointMaterialsUseScreenSpaceOverlayExpansion) {
  const std::string line_source =
      read_source_file("resources/materials/quader_overlay_line.mat");

  EXPECT_NE(line_source.find("vertexDomain : device"), std::string::npos);
  EXPECT_NE(line_source.find("requires : [\n        color,\n        custom0,\n        custom1,\n        custom2"),
            std::string::npos);
  EXPECT_NE(line_source.find("getWorldFromModelMatrix()"),
            std::string::npos);
  EXPECT_EQ(line_source.find("material.world_position"), std::string::npos);
  EXPECT_NE(line_source.find("getClipFromViewMatrix()"), std::string::npos);
  EXPECT_NE(line_source.find("name : nearPlane"), std::string::npos);
  EXPECT_NE(line_source.find("name : depthBias"), std::string::npos);
  EXPECT_NE(line_source.find("getResolution()"), std::string::npos);
  EXPECT_NE(line_source.find(
                "vec4(1000000.0, 1000000.0, 0.0, 1.0)"),
            std::string::npos);
  EXPECT_NE(line_source.find("variable_lineDistancePixels.x"),
            std::string::npos);

  const std::string point_source =
      read_source_file("resources/materials/quader_overlay_point.mat");
  const std::string icon_source =
      read_source_file("resources/materials/quader_overlay_icon.mat");

  EXPECT_NE(point_source.find("vertexDomain : device"), std::string::npos);
  EXPECT_NE(point_source.find("requires : [\n        uv0,\n        color"),
            std::string::npos);
  EXPECT_EQ(point_source.find("material.world_position"), std::string::npos);
  EXPECT_NE(point_source.find("getWorldFromModelMatrix()"),
            std::string::npos);
  EXPECT_NE(point_source.find("getClipFromViewMatrix()"), std::string::npos);
  EXPECT_NE(point_source.find("name : nearPlane"), std::string::npos);
  EXPECT_NE(point_source.find("getResolution()"), std::string::npos);
  EXPECT_NE(point_source.find(
                "biasedView.z = min(centerView.z + 0.04 * depthBiasUnits"),
            std::string::npos);
  EXPECT_NE(point_source.find(
                "biasedClip = getClipFromViewMatrix() * biasedView"),
            std::string::npos);
  EXPECT_NE(point_source.find("legacyMaximumPull"), std::string::npos);
  EXPECT_EQ(point_source.find("deviceDepth = max(-1.0, deviceDepth - "),
            std::string::npos)
      << "a fixed post-projection pull makes occluded points leak at distance";
  EXPECT_NE(point_source.find("variable_pointUv.xy"), std::string::npos);
  EXPECT_NE(point_source.find("materialParams.innerRadius"), std::string::npos);
  const std::size_t inner_radius_guard =
      point_source.find("if (materialParams.innerRadius > 0.0)");
  const std::size_t ring_coverage =
      point_source.find("color.a *= coverage");
  ASSERT_NE(ring_coverage, std::string::npos);
  ASSERT_NE(inner_radius_guard, std::string::npos);
  EXPECT_LT(inner_radius_guard, ring_coverage);

  EXPECT_NE(icon_source.find("vertexDomain : device"), std::string::npos);
  EXPECT_NE(icon_source.find("requires : [\n        uv0,\n        color"),
            std::string::npos);
  EXPECT_NE(icon_source.find("type : sampler2d"), std::string::npos);
  EXPECT_NE(icon_source.find("name : iconTexture"), std::string::npos);
  EXPECT_NE(icon_source.find("materialParams_iconTexture"), std::string::npos);
  EXPECT_EQ(icon_source.find("material.world_position"), std::string::npos);
  EXPECT_EQ(icon_source.find("getClipFromViewMatrix()"), std::string::npos);
  EXPECT_EQ(icon_source.find("name : nearPlane"), std::string::npos);
  EXPECT_NE(icon_source.find("sampled.rgb * tint.rgb * alpha"),
            std::string::npos);
}

TEST(ViewportOverlayPolicyTests,
     NativeViewportKeepsTransientObjectSelectionWireBeforeProjection) {
  const std::string viewport_panel_source =
      read_source_file("src/ui/qt_viewport_panel.cpp");
  const std::string overlay_service_source = read_source_file(
      "src/editor/modeling/services/modeling_session_services.cpp");

  EXPECT_TRUE(source_contains_compact(
      viewport_panel_source,
      "suppress_persistent_object_selection = cached_overlay_mode == "
      "ModelingSelectionMode::Object"));
  EXPECT_EQ(viewport_panel_source.find("modeling.selection_summary()"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find("modeling.content_revision()"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find("modeling.selection_revision()"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find(
                "modeling.selection_overlay_signature("
                "selection_overlay_options)"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find(
                "modeling_.selection_overlay_render_frame(options_)"),
            std::string::npos);
  EXPECT_EQ(viewport_panel_source.find("selection_overlay_cache_"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find(".selection_source = selection_source"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find(
                ".selection_frame_provider = &selection_frame_provider"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find(".scene_revision = document.revision()"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find(".asset_revision = assets.revision()"),
            std::string::npos);
  EXPECT_EQ(viewport_panel_source.find("strip_persistent_object_selection("),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      overlay_service_source,
      "options.suppress_persistent_object_selection && selection_mode == "
      "quader_editor_core::SelectionMode::Object && object.selected"));
  EXPECT_NE(overlay_service_source.find(
                "remove_persistent_object_face_fill_triangles(entry.frame)"),
            std::string::npos);
}

TEST(ViewportOverlayPolicyTests,
     QtViewportSeparatesRetainedAndDynamicOverlayStreams) {
  const std::string source = read_source_file("src/ui/qt_viewport_panel.cpp");
  const std::string header = read_source_file("src/ui/qt_viewport_panel.hpp");
  const std::string coordinator_source = read_source_file(
      "src/render/overlay/viewport_overlay_submission_coordinator.cpp");
  const std::string coordinator_header = read_source_file(
      "src/render/overlay/viewport_overlay_submission_coordinator.hpp");
  const std::string tool_projection_source = read_source_file(
      "src/editor/viewport/viewport_tool_overlay_projection.cpp");

  EXPECT_TRUE(source_contains_compact(
      source, "suppress_persistent_object_selection = cached_overlay_mode == "
              "ModelingSelectionMode::Object"));
  EXPECT_NE(
      source.find("modeling.selection_overlay_signature("
                  "selection_overlay_options)"),
      std::string::npos);
  EXPECT_NE(source.find("modeling_.selection_overlay_render_frame(options_)"),
            std::string::npos);
  EXPECT_NE(source.find(".scene_revision = document.revision()"),
            std::string::npos);
  EXPECT_NE(source.find(".asset_revision = assets.revision()"),
            std::string::npos);
  EXPECT_EQ(source.find("sync_selection_overlay("), std::string::npos);
  EXPECT_EQ(source.find("PaneOverlaySubmissionKey"), std::string::npos);
  EXPECT_EQ(source.find("PaneDynamicOverlaySubmissionKey"),
            std::string::npos);
  EXPECT_EQ(header.find("PaneOverlaySubmissionKey"), std::string::npos);
  EXPECT_EQ(header.find("PaneDynamicOverlaySubmissionKey"),
            std::string::npos);
  for (const std::string_view forbidden : {
           "build_scene_overlay_frame(", "render_bridge.set_overlay_frame(",
           "render_bridge.set_dynamic_overlay_frame(",
           "render_bridge.update_overlay_camera("}) {
    EXPECT_EQ(source.find(forbidden), std::string::npos) << forbidden;
  }
  EXPECT_EQ(count_occurrences(source, "submit_viewport_overlay("), 1u);
  EXPECT_TRUE(source_contains_compact(
      source, ".selection_source = selection_source"));
  EXPECT_TRUE(source_contains_compact(
      source, ".selection_frame_provider = &selection_frame_provider"));
  EXPECT_TRUE(source_contains_compact(
      source, ".curve_preview = native_presentation.curve_preview"));
  EXPECT_TRUE(source_contains_compact(
      source, "modeling.volatile_overlay_snapshot()"));
  EXPECT_TRUE(source_contains_compact(
      source, "ViewportToolOverlaySource dynamic_tool_frame_provider"));
  EXPECT_TRUE(source_contains_compact(
      source, ".dynamic_tool_source = dynamic_tool_frame_provider.source_stamp()"));
  EXPECT_TRUE(source_contains_compact(
      source, ".dynamic_tool_frame_provider = &dynamic_tool_frame_provider"));
  for (const std::string_view forbidden : {
           "QtDynamicToolOverlayFrameProvider",
           "ViewportOverlayFrame dynamic_overlay_frame",
           "ViewportToolOverlayProjection::append_box(",
           "ViewportToolOverlayProjection::append_transform_gizmo(",
           "SelectionOverlayProjection::append_world_space_overlay_points(",
           "ViewportToolOverlayProjection::append_selection_marquee(",
           "ViewportToolOverlayProjection::append_pivot_handle(",
           "ViewportToolOverlayProjection::append_linear_array_handle(",
           "ViewportToolOverlayProjection::append_radial_array_center("}) {
    EXPECT_EQ(source.find(forbidden), std::string::npos) << forbidden;
  }
  EXPECT_NE(tool_projection_source.find(
                "ViewportToolOverlaySource::build_dynamic_tool_overlay_frame()"),
            std::string::npos);
  EXPECT_NE(tool_projection_source.find(
                "ViewportToolOverlayProjection::append_box("),
            std::string::npos);
  for (const std::string_view camera_field : {
           "camera->eye", "camera->forward", "camera->right", "camera->up",
           "camera->viewport_size", "camera->projection",
           "camera->orthographic_size", "camera->fov_degrees",
           "camera->near_plane", "camera->far_plane"}) {
    EXPECT_NE(tool_projection_source.find(camera_field), std::string::npos)
        << camera_field;
  }
  EXPECT_NE(tool_projection_source.find("assert(points->size() <= 2u"),
            std::string::npos);
  EXPECT_EQ(tool_projection_source.find(
                "for (const quader_editor_core::SelectionOverlayPoint"),
            std::string::npos);
  EXPECT_EQ(source.find("CurveToolOverlayCache"), std::string::npos);
  EXPECT_EQ(source.find("curve_tool_overlay_cache_"), std::string::npos);
  EXPECT_NE(coordinator_header.find(
                "CurveToolOverlayCache curve_overlay_source_"),
            std::string::npos);

  for (const std::string_view stamp_type : {
           "ViewportOverlayPersistentSubmissionStamp",
           "ViewportOverlayDynamicSubmissionStamp"}) {
    SCOPED_TRACE(stamp_type);
    const std::size_t stamp_begin =
        coordinator_header.find("struct " + std::string(stamp_type));
    ASSERT_NE(stamp_begin, std::string::npos);
    const std::size_t stamp_end = coordinator_header.find("};", stamp_begin);
    ASSERT_NE(stamp_end, std::string::npos);
    const std::string stamp =
        coordinator_header.substr(stamp_begin, stamp_end - stamp_begin);
    EXPECT_NE(stamp.find("ViewportOverlayProjection projection"),
              std::string::npos);
    EXPECT_NE(stamp.find("float raster_scale"), std::string::npos);
    EXPECT_NE(stamp.find("default;"), std::string::npos);
  }

  EXPECT_EQ(
      coordinator_source.find("OverlayRenderableBuilder::frame_signature("),
      std::string::npos);
  EXPECT_EQ(coordinator_source.find("source_frame_signature("),
            std::string::npos);
  const std::size_t dynamic_source_change = coordinator_source.find(
      "const bool dynamic_source_changed");
  const std::size_t dynamic_source_gate = coordinator_source.find(
      "if (dynamic_source_changed)", dynamic_source_change);
  const std::size_t dynamic_provider_build = coordinator_source.find(
      "build_dynamic_tool_overlay_frame()", dynamic_source_gate);
  const std::size_t volatile_append = coordinator_source.find(
      "*request.volatile_modeling_overlay_frame", dynamic_source_gate);
  const std::size_t dynamic_gate = coordinator_source.find(
      "if (dynamic_changed)", dynamic_source_gate);
  const std::size_t curve_append = coordinator_source.find(
      "append_overlay_frame(dynamic_frame, curve_overlay_frame)",
      dynamic_gate);
  const std::size_t curve_build = coordinator_source.find(
      "curve_overlay_source_.frame(curve_preview)", dynamic_gate);
  const std::size_t dynamic_style = coordinator_source.find(
      "ViewportOverlayStyleResolver::apply(", curve_append);
  const std::size_t dynamic_submit = coordinator_source.find(
      "target.submit_dynamic_overlay(", dynamic_style);
  ASSERT_NE(dynamic_source_change, std::string::npos);
  ASSERT_NE(dynamic_source_gate, std::string::npos);
  ASSERT_NE(dynamic_provider_build, std::string::npos);
  ASSERT_NE(volatile_append, std::string::npos);
  ASSERT_NE(dynamic_gate, std::string::npos);
  ASSERT_NE(curve_build, std::string::npos);
  ASSERT_NE(curve_append, std::string::npos);
  ASSERT_NE(dynamic_style, std::string::npos);
  ASSERT_NE(dynamic_submit, std::string::npos);
  EXPECT_LT(dynamic_source_change, dynamic_source_gate);
  EXPECT_NE(coordinator_source.find(
                "pane.dynamic.raster_scale != dynamic_stamp.raster_scale",
                dynamic_source_change),
            std::string::npos);
  EXPECT_LT(dynamic_source_gate, dynamic_provider_build);
  EXPECT_LT(dynamic_provider_build, volatile_append);
  EXPECT_LT(dynamic_source_gate, volatile_append);
  EXPECT_LT(volatile_append, dynamic_gate);
  EXPECT_LT(dynamic_gate, curve_build);
  EXPECT_LT(curve_build, curve_append);
  EXPECT_LT(dynamic_gate, curve_append);
  EXPECT_LT(curve_append, dynamic_style);
  EXPECT_LT(dynamic_style, dynamic_submit);
  EXPECT_EQ(coordinator_source.find(
                "frame_signature(*request.volatile_modeling_overlay_frame"),
            std::string::npos);
  EXPECT_EQ(coordinator_source.find("frame_signature(curve_overlay_frame"),
            std::string::npos);

  const std::size_t cached_selection = coordinator_source.find(
      "frame = *selection_overlay_frame");
  const std::size_t scene_overlay = coordinator_source.find(
      "ViewportSceneOverlayBuilder::build_selection_frame(",
      cached_selection);
  const std::size_t style_resolve = coordinator_source.find(
      "ViewportOverlayStyleResolver::apply_from(", scene_overlay);
  const std::size_t persistent_submit = coordinator_source.find(
      "target.submit_persistent_overlay(");
  ASSERT_NE(cached_selection, std::string::npos);
  ASSERT_NE(scene_overlay, std::string::npos);
  ASSERT_NE(style_resolve, std::string::npos);
  ASSERT_NE(persistent_submit, std::string::npos);
  EXPECT_LT(cached_selection, scene_overlay);
  EXPECT_LT(scene_overlay, style_resolve);
  EXPECT_LT(style_resolve, persistent_submit);

  EXPECT_TRUE(source_contains_compact(
      source, "ViewportCameraAdapter::overlay_camera(pane_workspace.camera, "
              "pane_workspace.render_size)"));
  EXPECT_TRUE(source_contains_compact(
      coordinator_source, ".projection = request.camera.projection"));
  EXPECT_TRUE(source_contains_compact(
      coordinator_source,
      "if (!persistent_changed && !camera_update.persistent_available)"));
  EXPECT_TRUE(source_contains_compact(
      coordinator_source,
      "if (!dynamic_changed && !camera_update.dynamic_available)"));

  // The shared render-owned selection publication remains camera-independent.
  // Pane-specific dimension dashes and other dynamic records use the camera
  // only after the shared selection and dimension bounds publications.
  const std::size_t cache_begin = coordinator_source.find(
      "if (selection_overlay_visible) {");
  ASSERT_NE(cache_begin, std::string::npos);
  const std::size_t cache_end = coordinator_source.find(
      "const bool dimension_box_visible", cache_begin);
  ASSERT_NE(cache_end, std::string::npos);
  const std::string cache = coordinator_source.substr(
      cache_begin, cache_end - cache_begin);
  EXPECT_EQ(cache.find(".projection"), std::string::npos);
  EXPECT_EQ(cache.find("request.camera"), std::string::npos);
  const std::size_t cache_gate =
      cache.find("if (!selection_source_.valid || "
                 "selection_source_.stamp != source_stamp)");
  const std::size_t cache_extract =
      cache.find("build_selection_overlay_frame()");
  ASSERT_NE(cache_gate, std::string::npos);
  ASSERT_NE(cache_extract, std::string::npos);
  EXPECT_LT(cache_gate, cache_extract);
}

TEST(ViewportOverlayPolicyTests,
     QtViewportExtractsOneSceneOverlaySnapshotBeforePaneComposition) {
  const std::string source = read_source_file("src/ui/qt_viewport_panel.cpp");
  const std::string coordinator = read_source_file(
      "src/render/overlay/viewport_overlay_submission_coordinator.cpp");

  const std::size_t snapshot =
      source.find("build_scene_overlay_snapshot(");
  const std::size_t pane_composition =
      source.find("workspace_->clear_pane_visibility();", snapshot);
  const std::size_t scene_overlay =
      source.find("submit_viewport_overlay(", pane_composition);
  ASSERT_NE(snapshot, std::string::npos);
  ASSERT_NE(pane_composition, std::string::npos);
  ASSERT_NE(scene_overlay, std::string::npos);
  EXPECT_EQ(count_occurrences(source, "build_scene_overlay_snapshot("), 1u);
  EXPECT_LT(snapshot, pane_composition);
  EXPECT_LT(pane_composition, scene_overlay);
  EXPECT_TRUE(source_contains_compact(
      source, ".scene_overlay_snapshot = scene_overlay_snapshot"));
  EXPECT_NE(coordinator.find(
                "ViewportSceneOverlayBuilder::build_selection_frame("),
            std::string::npos);
}

TEST(ViewportOverlayPolicyTests,
     ObjectTransformDragBoundsPreferSceneWorldAabb) {
  const std::string source = read_source_file(
      "src/editor/modeling/services/modeling_session_service.cpp");

  const std::size_t drag_bounds = source.find("transform_drag_bounds() const");
  const std::size_t drag_bounds_end =
      source.find("cached_scene_transformed_objects_for_picking() const",
                  drag_bounds);
  ASSERT_NE(drag_bounds, std::string::npos);
  ASSERT_NE(drag_bounds_end, std::string::npos);
  const std::string drag_bounds_source =
      source.substr(drag_bounds, drag_bounds_end - drag_bounds);
  const std::size_t projected_world_bounds = drag_bounds_source.find(
      "selection_bounds()");
  const std::size_t selected_scene_bounds = drag_bounds_source.find(
      "selected_transform_world_bounds(scene_, assets_)");
  ASSERT_NE(projected_world_bounds, std::string::npos);
  ASSERT_NE(selected_scene_bounds, std::string::npos);
  EXPECT_LT(projected_world_bounds, selected_scene_bounds);
  EXPECT_NE(source.find(
                "selection_projection_service_.world_selection_bounds("),
            std::string::npos);
}

TEST(ViewportOverlayPolicyTests,
     WireframeCacheUsesSelectionStyleForSelectedMeshes) {
  const std::string cache_source =
      read_source_file("src/render/overlay/scene_wireframe_overlay_cache.cpp");
  const std::string cache_header =
      read_source_file("src/render/overlay/scene_wireframe_overlay_cache.hpp");
  EXPECT_NE(cache_source.find("mesh.selected"), std::string::npos);
  EXPECT_TRUE(source_contains_compact(cache_source,
                                      "ViewportOverlayLayer::SelectedEdge : "
                                      "ViewportOverlayLayer::WireframeXray"));
  EXPECT_TRUE(source_contains_compact(
      cache_source, "if (mesh.selected) { return mesh.active_selected ? "
                    "style.active_selection_wire_color : style.selection_wire_color; }"));
  EXPECT_NE(cache_source.find("mesh.wireframe_tint"), std::string::npos);
  EXPECT_NE(cache_source.find("style.selection_edge_line_width_pixels"),
            std::string::npos);
  EXPECT_NE(cache_header.find("synced_selected_mesh_wire_visible_"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      cache_source,
      "synced_selected_mesh_wire_visible_ == "
      "snapshot.selected_mesh_wire_visible"));
  EXPECT_TRUE(source_contains_compact(
      cache_source,
      "synced_selected_mesh_wire_visible_ = "
      "snapshot.selected_mesh_wire_visible"));
}

TEST(ViewportOverlayPolicyTests,
     PersistentInstanceCachesShareOneInstancedWireResourceBackend) {
  const std::string wireframe_cache =
      read_source_file("src/render/overlay/scene_wireframe_overlay_cache.cpp");
  const std::string wireframe_header =
      read_source_file("src/render/overlay/scene_wireframe_overlay_cache.hpp");
  const std::string selection_cache = read_source_file(
      "src/render/overlay/instance_selection_overlay_cache.cpp");
  const std::string selection_header = read_source_file(
      "src/render/overlay/instance_selection_overlay_cache.hpp");
  const std::string backend = read_source_file(
      "src/render/overlay/instanced_wire_render_backend.cpp");
  const std::string backend_header = read_source_file(
      "src/render/overlay/instanced_wire_render_backend.hpp");
  const std::string overlay_snapshot_header =
      read_source_file("src/render/overlay/render_overlay_snapshot.hpp");
  const std::string persistent_style_header = read_source_file(
      "src/render/overlay/viewport_persistent_overlay_style.hpp");
  const std::string cmake = read_source_file("CMakeLists.txt");
  EXPECT_NE(wireframe_cache.find(
                "std::unordered_set<RenderInstancePrimitiveKey"),
            std::string::npos);
  EXPECT_EQ(wireframe_cache.find(
                "std::vector<RenderInstancePrimitiveKey> unique_geometry"),
            std::string::npos);

  const auto acquire_body = [](const std::string &source,
                               std::string_view acquire,
                               std::string_view next_function) {
    const std::size_t begin = source.find(acquire);
    const std::size_t end = source.find(next_function, begin);
    EXPECT_NE(begin, std::string::npos);
    EXPECT_NE(end, std::string::npos);
    return begin == std::string::npos || end == std::string::npos
               ? std::string{}
               : source.substr(begin, end - begin);
  };

  const std::string backend_acquire = acquire_body(
      backend, "InstancedWireRenderBackend::acquire_geometry(",
      "InstancedWireRenderBackend::create_batch(");
  EXPECT_NE(backend_acquire.find(
                "++stats_.unique_geometry_resource_count"),
            std::string::npos);
  EXPECT_NE(backend_acquire.find("++stats_.geometry_upload_count"),
            std::string::npos);
  EXPECT_NE(backend_acquire.find("++stats_.geometry_reuse_count"),
            std::string::npos);
  EXPECT_EQ(backend_acquire.find("resource_count ="), std::string::npos);
  EXPECT_NE(backend.find(
                "--stats_.unique_geometry_resource_count"),
            std::string::npos);
  EXPECT_NE(wireframe_cache.find("destroy_raster_groups(created)"),
            std::string::npos);
  EXPECT_NE(wireframe_cache.find("prune_unused_wire_geometry()"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      backend_acquire,
      "resource->wire_geometry->content_id == "
      "input.wire_geometry->content_id && "
      "resource->color == input.color"));
  EXPECT_TRUE(source_contains_compact(
      selection_cache,
      ".material_payload = static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>("
      "record.logical_wire_width_pixels)) | "
      "(static_cast<std::uint64_t>(record.active_selected) << 32u)"));
  EXPECT_NE(selection_cache.find(".index_offset = record.wire_color"),
            std::string::npos);
  EXPECT_NE(overlay_snapshot_header.find("float logical_wire_width_pixels"),
            std::string::npos);
  EXPECT_NE(persistent_style_header.find(
                "float selection_edge_logical_width_pixels"),
            std::string::npos);
  EXPECT_NE(backend.find(
                "setParameter(\"lineWidth\", line_width_pixels)"),
            std::string::npos);
  EXPECT_NE(selection_cache.find("reconcile_wire_raster_groups("),
            std::string::npos);
  EXPECT_NE(wireframe_cache.find("reconcile_raster_groups("),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      selection_cache,
      "stats_.selected_member_count = "
      "snapshot.instance_selection_fills.size(); "
      "prune_unused_wire_geometry(); "
      "refresh_wire_resource_stats(); return true;"));

  for (const std::string *cache : {&wireframe_cache, &selection_cache}) {
    EXPECT_EQ(cache->find("VertexBuffer::Builder"), std::string::npos);
    EXPECT_EQ(cache->find("IndexBuffer::Builder"), std::string::npos);
    EXPECT_EQ(cache->find("InstanceBuffer::Builder"), std::string::npos);
    EXPECT_EQ(cache->find("RenderableManager::Builder"), std::string::npos);
    EXPECT_EQ(cache->find("struct WireGeometryResource"), std::string::npos);
  }
  EXPECT_NE(backend.find("filament::VertexBuffer::Builder"),
            std::string::npos);
  EXPECT_NE(backend.find("filament::InstanceBuffer::Builder"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      backend,
      ".layerMask(kViewportAllLayerMask, style.layer_mask)"));
  EXPECT_TRUE(source_contains_compact(
      wireframe_cache,
      "mesh.selected ? kViewportOverlayLayerMask : wire_layer_mask(mesh, snapshot)"));
  EXPECT_TRUE(source_contains_compact(
      selection_cache, ".layer_mask = raster_group.layer_mask"));
  EXPECT_TRUE(source_contains_compact(
      wireframe_cache, "wire_backend_.update_clip_plane(proxy_group.proxies"));
  EXPECT_TRUE(source_contains_compact(
      selection_cache, "wire_backend_.update_clip_plane(proxy_group.proxies"));
  EXPECT_NE(wireframe_header.find("std::vector<RasterProxyGroup>"),
            std::string::npos);
  EXPECT_NE(selection_header.find("std::vector<WireRasterProxyGroup>"),
            std::string::npos);
  EXPECT_NE(persistent_style_header.find(
                "kMaxViewportPersistentOverlayRasterGroups = 4u"),
            std::string::npos);
  EXPECT_NE(backend_header.find(
                "std::span<const std::span<const BatchProxy>>"),
            std::string::npos);
  EXPECT_NE(backend_header.find("struct GeometryResource;"),
            std::string::npos);
  EXPECT_NE(wireframe_header.find(
                "InstancedWireRenderBackend wire_backend_"),
            std::string::npos);
  EXPECT_NE(selection_header.find(
                "InstancedWireRenderBackend wire_backend_"),
            std::string::npos);
  EXPECT_NE(cmake.find(
                "src/render/overlay/instanced_wire_render_backend.cpp"),
            std::string::npos);
}

TEST(ViewportPersistentOverlayStyleSnapshotTests,
     PersistentCachesConsumeSnapshotStylesWithoutChangingRenderPolicy) {
  const std::string selected_cache =
      read_source_file("src/render/overlay/selected_object_overlay_cache.cpp");
  const std::string wireframe_cache = read_source_file(
      "src/render/overlay/scene_wireframe_overlay_cache.cpp");
  const std::string wire_backend = read_source_file(
      "src/render/overlay/instanced_wire_render_backend.cpp");

  EXPECT_EQ(selected_cache.find("kOverlaySelectionFaceColor"),
            std::string::npos);
  EXPECT_NE(selected_cache.find("snapshot.style.face_signature"),
            std::string::npos);
  EXPECT_NE(selected_cache.find("style.selection_face_color"),
            std::string::npos);
  EXPECT_EQ(wireframe_cache.find(
                "mesh.selected ? kOverlaySelectionWireColor"),
            std::string::npos);
  EXPECT_NE(wireframe_cache.find("snapshot.style.wire_signature"),
            std::string::npos);
  EXPECT_NE(wireframe_cache.find("style.selection_wire_color"),
            std::string::npos);
  EXPECT_NE(wireframe_cache.find(
                "style.selection_edge_line_width_pixels"),
            std::string::npos);

  EXPECT_NE(selected_cache.find("OverlayRenderableBuilder::make_state("),
            std::string::npos);
  EXPECT_NE(wireframe_cache.find("OverlayRenderableBuilder::make_state("),
            std::string::npos);
  EXPECT_NE(selected_cache.find(".culling(false)"), std::string::npos);
  EXPECT_NE(wire_backend.find(".culling(false)"), std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      selected_cache, ".blendOrder(0, state.blend_order)"
                      ".globalBlendOrderEnabled(0, true)"));
  EXPECT_TRUE(source_contains_compact(
      wire_backend, ".blendOrder(0u, style.render_state.blend_order)"
                    ".globalBlendOrderEnabled(0u, true)"));
}

TEST(ViewportOverlayPolicyTests,
     UnchangedMeshInputsBypassWirePlanningDuringCurveEdits) {
  const std::string source = read_source_file(
      "src/render/overlay/scene_wireframe_overlay_cache.cpp");
  const std::size_t sync_begin =
      source.find("void SceneWireframeOverlayCache::sync(");
  ASSERT_NE(sync_begin, std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      source, "scene_wireframe_overlay_mesh_inputs_match("
              "synced_meshes_, snapshot.meshes)"));
  EXPECT_TRUE(source_contains_compact(
      source, "if (mesh_inputs_match)"));
  EXPECT_TRUE(source_contains_compact(
      source, "++stats_.mesh_input_reuse_count"));
  EXPECT_TRUE(source_contains_compact(
      source, "if (line_width_only_style_changed || "
              "raster_groups_changed)"));
  EXPECT_TRUE(source_contains_compact(
      source, "reconcile_raster_groups(prepared, snapshot, raster_groups)"));
  EXPECT_TRUE(source_contains_compact(
      source,
      "if (has_synced_ && !render_source_dependencies_match("
      "synced_source_stamp_, snapshot, "
      "RenderSourceDependency::SceneBinding)) { clear();"));
  EXPECT_TRUE(source_contains_compact(
      source, "prepare_wire_batches(snapshot, max_instances_per_chunk_, &plan_)"));
  EXPECT_TRUE(source_contains_compact(
      source, "synced_meshes_ = snapshot.meshes;"));
  EXPECT_NE(source.find("synced_meshes_.clear();"), std::string::npos);
  EXPECT_NE(source.find("has_synced_ = false;"), std::string::npos);
}

TEST(ViewportOverlayPolicyTests,
     InstancedWireCachesGatePlanningOnTypedSourceStamps) {
  const std::string stamp_header =
      read_source_file("src/render/render_source_stamp.hpp");
  const std::string wireframe_header =
      read_source_file("src/render/overlay/scene_wireframe_overlay_cache.hpp");
  const std::string wireframe_source =
      read_source_file("src/render/overlay/scene_wireframe_overlay_cache.cpp");
  const std::string selection_header = read_source_file(
      "src/render/overlay/instance_selection_overlay_cache.hpp");
  const std::string selection_source = read_source_file(
      "src/render/overlay/instance_selection_overlay_cache.cpp");

  EXPECT_NE(wireframe_header.find("RenderSourceStamp synced_source_stamp_"),
            std::string::npos);
  EXPECT_NE(selection_header.find("RenderSourceStamp synced_source_stamp_"),
            std::string::npos);
  EXPECT_EQ(wireframe_header.find("synced_scene_revision_"),
            std::string::npos);
  EXPECT_EQ(selection_header.find("synced_local_visibility_revision_"),
            std::string::npos);
  EXPECT_NE(stamp_header.find(
                "kRenderOverlaySceneWireframeCacheNoWorkDependencies"),
            std::string::npos);
  EXPECT_NE(stamp_header.find(
                "kRenderOverlayInstanceSelectionCacheNoWorkDependencies"),
            std::string::npos);

  const std::size_t selection_sync = selection_source.find(
      "bool InstanceSelectionOverlayCache::sync(");
  const std::size_t selection_gate = selection_source.find(
      "kRenderOverlayInstanceSelectionCacheNoWorkDependencies",
      selection_sync);
  const std::size_t selection_prepare = selection_source.find(
      "const PreparedPlans prepared = prepare(snapshot);", selection_sync);
  ASSERT_NE(selection_sync, std::string::npos);
  ASSERT_NE(selection_gate, std::string::npos);
  ASSERT_NE(selection_prepare, std::string::npos);
  EXPECT_LT(selection_gate, selection_prepare);
  EXPECT_TRUE(source_contains_compact(
      selection_source.substr(selection_sync,
                              selection_prepare - selection_sync),
      "!wire_style_changed && !fill_style_changed && "
      "!raster_groups_changed && !clip_changed) { return false; }"));

  EXPECT_NE(wireframe_source.find(
                "kRenderOverlaySceneWireframeCacheNoWorkDependencies"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      wireframe_source, "synced_source_stamp_ = snapshot;"));
  EXPECT_TRUE(source_contains_compact(
      selection_source, "synced_source_stamp_ = snapshot;"));
}

TEST(RenderSourceStampPolicyTests,
     NamedMaskConstructionDoesNotPolluteDependencyOperatorLookup) {
  const std::string stamp_header =
      read_source_file("src/render/render_source_stamp.hpp");

  EXPECT_NE(stamp_header.find(
                "from(std::initializer_list<RenderSourceDependency>"),
            std::string::npos);
  EXPECT_NE(stamp_header.find("RenderSourceDependencyMask::from({"),
            std::string::npos);
  EXPECT_EQ(stamp_header.find("operator|"), std::string::npos)
      << "A namespace-level dependency operator hides Filament bitmask "
         "operators in render translation units.";
}

TEST(ViewportPersistentOverlayStyleSnapshotTests,
     InstanceEditContextChangesOnlyWireMaterialUniforms) {
  const std::string source = read_source_file(
      "src/render/overlay/scene_wireframe_overlay_cache.cpp");
  const std::size_t sync_begin =
      source.find("void SceneWireframeOverlayCache::sync(");
  const std::size_t prepare =
      source.find("const PreparedPlan prepared = prepare(snapshot);", sync_begin);
  ASSERT_NE(sync_begin, std::string::npos);
  ASSERT_NE(prepare, std::string::npos);
  const std::string unchanged = source.substr(sync_begin, prepare - sync_begin);
  EXPECT_TRUE(source_contains_compact(
      unchanged,
      "render_source_dependencies_match(synced_source_stamp_, snapshot, "
      "RenderSourceDependency::InstanceEditScope)"));
  EXPECT_TRUE(source_contains_compact(
      unchanged, "synced_instance_edit_context_signature_ != "
                 "snapshot.style.instance_edit_context_signature"));
  EXPECT_TRUE(source_contains_compact(
      unchanged, "!clip_changed && !raster_groups_changed && "
                 "!layer_mask_changed && !style_changed"));
  EXPECT_TRUE(source_contains_compact(
      unchanged,
      "if (context_style_changed) { "
      "update_instance_edit_style(snapshot.style.instance_edit_style); "
      "synced_instance_edit_context_signature_ = "
      "snapshot.style.instance_edit_context_signature; } return; }"));

  const std::size_t update_begin =
      source.find("void SceneWireframeOverlayCache::update_instance_edit_style(");
  const std::size_t update_end =
      source.find("void SceneWireframeOverlayCache::set_all_layer_masks(",
                  update_begin);
  ASSERT_NE(update_begin, std::string::npos);
  ASSERT_NE(update_end, std::string::npos);
  const std::string update = source.substr(update_begin, update_end - update_begin);
  EXPECT_TRUE(source_contains_compact(
      update,
      "for (RasterProxyGroup &proxy_group : raster_proxy_groups_) { "
      "wire_backend_.update_instance_edit_style(proxy_group.proxies, style, "
      "true); }"));
  for (const std::string_view geometry_work : {
           "prepare(", "rebuild(", "setBuffer", "instance_buffer",
           "getRenderableManager(", "create_batch("}) {
    EXPECT_EQ(update.find(geometry_work), std::string::npos) << geometry_work;
  }

  const std::size_t geometry_begin =
      source.find("std::uint64_t wire_geometry_signature(");
  const std::size_t key_begin =
      source.find("RenderInstancePrimitiveKey wire_key(", geometry_begin);
  const std::size_t key_end = source.find("struct PrimitiveKeyHash", key_begin);
  ASSERT_NE(geometry_begin, std::string::npos);
  ASSERT_NE(key_begin, std::string::npos);
  ASSERT_NE(key_end, std::string::npos);
  EXPECT_EQ(source.substr(geometry_begin, key_begin - geometry_begin)
                .find("instance_edit"),
            std::string::npos);
  const std::string key = source.substr(key_begin, key_end - key_begin);
  EXPECT_TRUE(source_contains_compact(
      key, "is_instance_edit_context(instance_edit_scope, mesh.occurrence, "
           "mesh.selected)"));
  EXPECT_NE(key.find("RenderInstancePresentationFlag::Dimmed"),
            std::string::npos);
  EXPECT_EQ(key.find("instance_edit_style"), std::string::npos);
  const std::string backend = read_source_file(
      "src/render/overlay/instanced_wire_render_backend.cpp");
  EXPECT_TRUE(source_contains_compact(
      source, ".instance_edit_dim = dimmed ? 1.0f : 0.0f"));
  EXPECT_TRUE(source_contains_compact(
      backend, "setParameter(\"instanceEditDim\", style.instance_edit_dim)"));
  EXPECT_TRUE(source_contains_compact(
      backend, "bind_instance_edit_context_style(*proxy.material_instance_, "
               "style.instance_edit_style)"));

  const std::string selected_source = read_source_file(
      "src/render/overlay/instance_selection_overlay_cache.cpp");
  const std::size_t selected_begin =
      selected_source.find("InstancedWireBatchStyle wire_batch_style(");
  const std::size_t selected_end =
      selected_source.find("} // namespace", selected_begin);
  ASSERT_NE(selected_begin, std::string::npos);
  ASSERT_NE(selected_end, std::string::npos);
  const std::string selected =
      selected_source.substr(selected_begin, selected_end - selected_begin);
  EXPECT_TRUE(source_contains_compact(
      selected, ".instance_edit_dim = 0.0f"));
  EXPECT_TRUE(source_contains_compact(
      selected, ".instance_edit_style = {}"));
}

TEST(SceneRenderBridgeTests, AppliesSharedOverlayStateThroughProxyFactory) {
  const std::string bridge_source =
      read_source_file("src/render/scene_render_bridge.cpp");
  const std::string builder_source =
      read_source_file("src/render/overlay/overlay_renderable_builder.cpp");
  const std::string proxy_factory_source =
      read_source_file("src/render/overlay/overlay_proxy_factory.cpp");
  const std::string tool_preview_cache_source =
      read_source_file("src/render/tool_preview_renderable_cache.cpp");

  EXPECT_NE(proxy_factory_source.find("setColorWrite(state.color_write)"),
            std::string::npos);
  EXPECT_NE(proxy_factory_source.find("setDepthWrite(state.depth_write)"),
            std::string::npos);
  EXPECT_NE(proxy_factory_source.find(
                "setDepthFunc(to_filament_depth_func(state.depth_func))"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      proxy_factory_source, "setTransparencyMode(to_filament_transparency_mode("
                            "state.transparency_mode))"));
  EXPECT_TRUE(source_contains_compact(
      proxy_factory_source, "setPolygonOffset(state.polygon_offset_scale, "
                            "state.polygon_offset_constant)"));
  EXPECT_NE(proxy_factory_source.find("DepthFunc::GE"), std::string::npos);
  EXPECT_NE(proxy_factory_source.find("DepthFunc::E"), std::string::npos);
  EXPECT_NE(builder_source.find("OverlayCullingMode::Back"), std::string::npos);
  EXPECT_NE(builder_source.find("OverlayTransparencyMode::TwoPassesOneSide"),
            std::string::npos);
  EXPECT_NE(
      builder_source.find("viewport_overlay_triangle_is_two_sided(layer)"),
      std::string::npos);
  EXPECT_NE(builder_source.find("OverlayCullingMode::None"), std::string::npos);
  EXPECT_NE(builder_source.find("OverlayTransparencyMode::TwoPassesTwoSides"),
            std::string::npos);
  EXPECT_NE(bridge_source.find("RenderToolPreviewSceneSnapshot preview"),
            std::string::npos);
  EXPECT_NE(
      bridge_source.find("tool_preview_cache_.set_meshes(editable_surface_cache_.prepared_fallback_meshes())"),
            std::string::npos);
  EXPECT_NE(tool_preview_cache_source.find("current_generation_"),
            std::string::npos);
  EXPECT_NE(tool_preview_cache_source.find("create_shared_instance"),
            std::string::npos);
  EXPECT_NE(tool_preview_cache_source.find(
                "void ToolPreviewRenderableCache::clear()"),
            std::string::npos);
}

TEST(SceneRenderBridgeTests,
     RetainedMaterialUniformsInvalidateAtEachProxyCacheBoundary) {
  const std::string bridge_source =
      read_source_file("src/render/scene_render_bridge.cpp");
  const std::string mesh_cache_source =
      read_source_file("src/render/mesh_renderable_cache.cpp");
  const std::string instance_cache_source =
      read_source_file("src/render/instance_renderable_cache.cpp");
  const std::string instance_cache_header =
      read_source_file("src/render/instance_renderable_cache.hpp");
  const std::string preview_cache_source =
      read_source_file("src/render/tool_preview_renderable_cache.cpp");
  const std::string preview_cache_header =
      read_source_file("src/render/tool_preview_renderable_cache.hpp");
  const std::string viewport_source =
      read_source_file("src/ui/qt_viewport_panel.cpp");

  for (const std::string *header :
       {&instance_cache_header, &preview_cache_header}) {
    EXPECT_NE(header->find("material_uniform_signature_"),
              std::string::npos);
    EXPECT_NE(header->find("material_uniforms_dirty_ = true"),
              std::string::npos);
  }
  for (const std::string *source :
       {&instance_cache_source, &preview_cache_source}) {
    EXPECT_TRUE(source_contains_compact(
        *source,
        "if (!material_uniforms_dirty_ && material_uniform_signature_ == "
        "signature) { return; }"));
    EXPECT_NE(source->find("material_uniforms_dirty_ = true;"),
              std::string::npos);
    EXPECT_NE(source->find("material_uniforms_dirty_ = false;"),
              std::string::npos);
    const std::size_t unchanged_gate =
        source->find("if (!material_uniforms_dirty_");
    const std::size_t material_loop = source->find("for (", unchanged_gate);
    const std::size_t uniform_apply =
        source->find("factory_->update_surface_grid(", unchanged_gate);
    ASSERT_NE(unchanged_gate, std::string::npos);
    ASSERT_NE(material_loop, std::string::npos);
    ASSERT_NE(uniform_apply, std::string::npos);
    EXPECT_LT(unchanged_gate, material_loop);
    EXPECT_LT(material_loop, uniform_apply);
  }
  EXPECT_TRUE(source_contains_compact(
      mesh_cache_source,
      "if (!surface_grid_dirty_ && surface_grid_signature_ == signature) { "
      "return; } update_surface_grid_proxy_map("));
  EXPECT_NE(instance_cache_source.find("Instance material uniforms apply"),
            std::string::npos);
  EXPECT_NE(preview_cache_source.find(
                "Tool preview material uniforms apply"),
            std::string::npos);

  EXPECT_TRUE(source_contains_compact(
      bridge_source,
      "instance_cache_.update_surface_grid(grid_world_spacing, settings); "
      "tool_preview_cache_.update_surface_grid(grid_world_spacing, settings);"));
  EXPECT_EQ(bridge_source.find("material_uniform_signature_"),
            std::string::npos)
      << "a bridge-wide gate would miss proxies created inside either cache";

  const std::size_t preview_sync = viewport_source.find(
      "QuaderRenderApiRuntimeAccess::sync_tool_previews");
  const std::size_t material_sync = viewport_source.find(
      "render_bridge.update_mesh_surface_grid(grid_world_size, mesh_grid_settings)");
  ASSERT_NE(preview_sync, std::string::npos);
  ASSERT_NE(material_sync, std::string::npos);
  EXPECT_LT(preview_sync, material_sync)
      << "new preview proxies must be marked dirty before current settings "
         "are applied in the same frame";
}

TEST(SceneRenderBridgeTests,
     InstanceEditSyncReadsSanitizedScopeWithoutPresentationCopies) {
  const std::string source = read_source_file("src/api/quader_render_api.cpp");
  const std::string service =
      read_source_file("src/editor/instances/scene_instance_service.hpp");
  const std::size_t scope_begin =
      source.find("RenderInstanceEditScope instance_edit_render_scope(");
  const std::size_t scope_end = source.find("\n}", scope_begin);
  ASSERT_NE(scope_begin, std::string::npos);
  ASSERT_NE(scope_end, std::string::npos);
  const std::string scope = source.substr(scope_begin, scope_end - scope_begin);
  EXPECT_NE(scope.find("session.instances().instance_edit_scope()"),
            std::string::npos);
  EXPECT_NE(scope.find("edit_scope.active()"), std::string::npos);
  EXPECT_NE(scope.find("edit_scope.occurrence()"), std::string::npos);
  EXPECT_NE(scope.find("edit_scope.definition()"), std::string::npos);
  EXPECT_EQ(scope.find("instance_edit_state()"), std::string::npos);

  for (const std::string_view method : {"sync_scene", "sync_scene_asset_overlays"}) {
    const std::size_t begin = source.find(
        "QuaderRenderApiRuntimeAccess::" + std::string(method) + "(");
    const std::size_t end = source.find("\n}", begin);
    ASSERT_NE(begin, std::string::npos) << method;
    ASSERT_NE(end, std::string::npos) << method;
    const std::string body = source.substr(begin, end - begin);
    EXPECT_NE(body.find("instance_edit_render_scope(scene_session)"),
              std::string::npos) << method;
    EXPECT_EQ(body.find("instance_edit_state()"), std::string::npos) << method;
  }

  const std::size_t getter_begin =
      service.find("InstanceEditContext instance_edit_scope() const");
  const std::size_t getter_end = service.find("\n  }", getter_begin);
  ASSERT_NE(getter_begin, std::string::npos);
  ASSERT_NE(getter_end, std::string::npos);
  const std::string getter = service.substr(getter_begin, getter_end - getter_begin);
  EXPECT_TRUE(source_contains_compact(
      getter, "sanitize_edit_context(); return edit_context_;"));
  for (const std::string_view presentation_work : {
           "instance_edit_state(", "name", "std::string"}) {
    EXPECT_EQ(getter.find(presentation_work), std::string::npos)
        << presentation_work;
    EXPECT_EQ(scope.find(presentation_work), std::string::npos)
        << presentation_work;
  }
}

TEST(SceneRenderBridgeTests,
     ToolPreviewPublishesCandidateBeforeSuppressingSourcesAndCachesFailure) {
  const std::string cache_header =
      read_source_file("src/render/tool_preview_renderable_cache.hpp");
  const std::string cache_source =
      read_source_file("src/render/tool_preview_renderable_cache.cpp");
  const std::string bridge_source =
      read_source_file("src/render/scene_render_bridge.cpp");
  const std::string render_api_source =
      read_source_file("src/api/quader_render_api.cpp");

  EXPECT_NE(cache_header.find("current_generation_"), std::string::npos);
  EXPECT_NE(cache_header.find("warm_generation_"), std::string::npos);
  EXPECT_NE(cache_header.find("retired_generations_"), std::string::npos);
  EXPECT_NE(cache_source.find("group_topology_equals"), std::string::npos);
  EXPECT_NE(cache_source.find("preview_mesh.mesh.geometry_payload()"),
            std::string::npos);
  EXPECT_NE(cache_source.find("Tool preview ordered active fast path"),
            std::string::npos);
  EXPECT_NE(cache_source.find("detach_generation(*generation)"),
            std::string::npos);
  EXPECT_NE(cache_source.find("factory_->destroy_detached(proxy)"),
            std::string::npos);
  EXPECT_NE(cache_source.find(
                "Tool preview build hidden candidate generation"),
            std::string::npos);

  const std::size_t preview_begin = bridge_source.find("ToolPreviewSubmissionResult SceneRenderBridge::set_tool_preview_scene(");
  ASSERT_NE(preview_begin, std::string::npos);
  const std::size_t surface_prepare = bridge_source.find("editable_surface_cache_.prepare(", preview_begin);
  const std::size_t surface_failed = bridge_source.find("surface_result == ToolPreviewSubmissionResult::Failed", surface_prepare);
  const std::size_t light_prepare =
      bridge_source.find("tool_preview_light_cache_.prepare(preview.lights)", surface_failed);
  const std::size_t publish = bridge_source.find(
      "tool_preview_cache_.set_meshes(editable_surface_cache_.prepared_fallback_meshes())", light_prepare);
  const std::size_t failed = bridge_source.find(
      "mesh_result == ToolPreviewSubmissionResult::Failed", publish);
  const std::size_t surface_discard = bridge_source.find("editable_surface_cache_.discard_prepared()", failed);
  const std::size_t light_discard = bridge_source.find("tool_preview_light_cache_.discard_prepared()", failed);
  const std::size_t surface_publish = bridge_source.find("editable_surface_cache_.publish_prepared()", failed);
  const std::size_t light_publish = bridge_source.find(
      "tool_preview_light_cache_.publish_prepared()", surface_publish);
  const std::size_t exclusion = bridge_source.find("sync_service_.set_retained_mesh_exclusion(", light_publish);
  const std::size_t overlay = bridge_source.find(
      "overlay_cache_.set_transform_position_streams(",
      light_publish);
  const std::size_t suppression = bridge_source.find(
      "Tool preview update source suppression", overlay);
  ASSERT_NE(surface_prepare, std::string::npos);
  ASSERT_NE(surface_failed, std::string::npos);
  ASSERT_NE(light_prepare, std::string::npos);
  ASSERT_NE(publish, std::string::npos);
  ASSERT_NE(failed, std::string::npos);
  ASSERT_NE(surface_discard, std::string::npos);
  ASSERT_NE(light_discard, std::string::npos);
  ASSERT_NE(surface_publish, std::string::npos);
  ASSERT_NE(light_publish, std::string::npos);
  ASSERT_NE(exclusion, std::string::npos);
  ASSERT_NE(overlay, std::string::npos);
  ASSERT_NE(suppression, std::string::npos);
  EXPECT_LT(surface_prepare, surface_failed);
  EXPECT_LT(surface_failed, light_prepare);
  EXPECT_LT(light_prepare, publish);
  EXPECT_LT(publish, failed);
  EXPECT_LT(failed, light_discard);
  EXPECT_LT(failed, surface_discard);
  EXPECT_LT(light_discard, surface_publish);
  EXPECT_LT(surface_discard, surface_publish);
  EXPECT_LT(surface_publish, light_publish);
  EXPECT_LT(light_publish, exclusion);
  EXPECT_LT(exclusion, overlay);
  EXPECT_LT(overlay, suppression);

  const std::size_t api_submission = render_api_source.find(
      "bridge.set_tool_preview_scene(");
  const std::size_t api_failure = render_api_source.find(
      "preview_result == ToolPreviewSubmissionResult::Failed",
      api_submission);
  const std::size_t cache_sync_gate = render_api_source.find(
      "api.generated_tool_preview_sync_bridge_ = &bridge", api_failure);
  const std::size_t cached_failure = render_api_source.find(
      "api.generated_tool_preview_attempt_failed_ = publication_failed",
      cache_sync_gate);
  const std::size_t failure_return =
      render_api_source.find("if (publication_failed)", cached_failure);
  ASSERT_NE(api_submission, std::string::npos);
  ASSERT_NE(api_failure, std::string::npos);
  ASSERT_NE(cache_sync_gate, std::string::npos);
  ASSERT_NE(cached_failure, std::string::npos);
  ASSERT_NE(failure_return, std::string::npos);
  EXPECT_LT(api_submission, api_failure);
  EXPECT_LT(api_failure, cache_sync_gate);
  EXPECT_LT(cache_sync_gate, cached_failure);
  EXPECT_LT(cached_failure, failure_return)
      << "failed preview attempts must be cached before returning";
}

TEST(SceneRenderBridgeTests,
     GeneratedToolPreviewGateTracksGeometryBeforeCompilingMeshes) {
  const std::string api_source =
      read_source_file("src/api/quader_render_api.cpp");
  const std::size_t key_begin =
      api_source.find("QuaderRenderApi::GeneratedToolPreviewSourceStamp");
  const std::size_t source_revision =
      api_source.find("project.modeling().tool_preview_source_revision()",
                      key_begin);
  const std::size_t source_gate = api_source.find(
      "api.generated_tool_preview_source_ == generated_tool_preview_source",
      source_revision);
  const std::size_t signature =
      api_source.find("project.modeling().tool_preview_signature()",
                      source_gate);
  const std::size_t semantic_gate = api_source.find(
      "api.generated_tool_preview_signature_ == generated_tool_preview_signature",
      signature);
  const std::size_t compile =
      api_source.find("project.modeling().tool_preview_meshes()", semantic_gate);
  ASSERT_NE(key_begin, std::string::npos);
  ASSERT_NE(source_revision, std::string::npos);
  ASSERT_NE(source_gate, std::string::npos);
  ASSERT_NE(signature, std::string::npos);
  ASSERT_NE(semantic_gate, std::string::npos);
  ASSERT_NE(compile, std::string::npos);
  EXPECT_LT(source_revision, source_gate);
  EXPECT_LT(source_gate, signature);
  EXPECT_LT(signature, semantic_gate);
  EXPECT_LT(semantic_gate, compile);
  const std::string gate_inputs =
      api_source.substr(key_begin, source_gate - key_begin);
  for (const char *dependency : {
           "project.modeling().tool_preview_source_revision()",
           "project.modeling().content_revision()",
           "project.modeling().modeling_object_map_revision()",
           "project.scene().transform_revision()",
           "project.scene().hierarchy_presentation_revision()",
           "project.instances().instance_edit_revision()"}) {
    EXPECT_NE(gate_inputs.find(dependency), std::string::npos) << dependency;
  }
  EXPECT_EQ(gate_inputs.find("tool_preview_signature()"), std::string::npos);
  EXPECT_EQ(gate_inputs.find("tool_preview_meshes()"), std::string::npos);
  EXPECT_EQ(gate_inputs.find("instance_edit_state()"), std::string::npos)
      << "the presentation snapshot copies names before the unchanged gate";

  const std::string service_source =
      read_source_file("src/editor/modeling/services/modeling_session_services.cpp");
  const std::size_t begin =
      service_source.find("ModelingOverlayService::tool_preview_signature(");
  const std::size_t end =
      service_source.find("ModelingOverlayService::volatile_overlay_signature(",
                          begin);
  ASSERT_NE(begin, std::string::npos);
  ASSERT_NE(end, std::string::npos);
  const std::string signature_source = service_source.substr(begin, end - begin);
  for (const char *expensive_projection : {
           "box_preview_descriptor(", "mirror_plane_points(",
           "preview_mirror(", "tool_preview_meshes("}) {
    EXPECT_EQ(signature_source.find(expensive_projection), std::string::npos)
        << "preview state hashing must stay ahead of geometry generation: "
        << expensive_projection;
  }
  EXPECT_NE(signature_source.find("BoxDragState::Height"), std::string::npos);
  EXPECT_NE(signature_source.find("box.height_distance"), std::string::npos);
  EXPECT_NE(signature_source.find("mirror_has_mesh_preview(mirror)"),
            std::string::npos);
  EXPECT_NE(signature_source.find("mirror.target_object_ids"), std::string::npos);
  EXPECT_NE(signature_source.find("mirror.plane.points"), std::string::npos);
}

TEST(SceneRenderBridgeTests,
     ToolPreviewRenderablesUseTheSubmittedCullingPolicy) {
  const std::string cache_source =
      read_source_file("src/render/tool_preview_renderable_cache.cpp");

  EXPECT_TRUE(source_contains_compact(
      cache_source,
      "transforms.front(), preview_mesh.cast_shadows, "
      "preview_mesh.double_sided, 0u, false, false"));
  EXPECT_TRUE(source_contains_compact(
      cache_source,
      "rendered_prototype, transforms.front(), preview_mesh.double_sided, "
      "0u, true, false"));
  EXPECT_TRUE(source_contains_compact(
      cache_source,
      "rendered_prototype, transforms.front(), preview_mesh.double_sided, "
      "0u, true, true"));
  EXPECT_EQ(cache_source.find("transforms.front(), false, true, 0u"),
            std::string::npos);
}

TEST(SceneRenderBridgeTests,
     StaticArrayPreviewsUseCapacityRetainedExplicitInstanceChunks) {
  const std::string cache_header =
      read_source_file("src/render/tool_preview_renderable_cache.hpp");
  const std::string cache_source =
      read_source_file("src/render/tool_preview_renderable_cache.cpp");
  const std::string factory_source =
      read_source_file("src/render/mesh_renderable_factory.cpp");

  EXPECT_NE(cache_header.find("struct ExplicitInstanceChunk final"),
            std::string::npos);
  EXPECT_NE(cache_header.find("explicit_instance_capacities"),
            std::string::npos);
  EXPECT_NE(cache_source.find(
                "plan_tool_preview_instances("),
            std::string::npos);
  EXPECT_NE(cache_source.find(
                "factory_->create_explicit_instances("),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      cache_source,
      "prepare_generation(*current_generation_, meshes, candidate_indices, "
      "false, preparation_failed)"));
  EXPECT_TRUE(source_contains_compact(
      cache_source,
      "prepare_generation(*warm_generation_, meshes, candidate_indices, "
      "true, preparation_failed)"));
  EXPECT_TRUE(source_contains_compact(
      factory_source,
      "instance_capacity = instance_capacity == 0u ? "
      "world_transforms.size() : instance_capacity"));
  EXPECT_NE(factory_source.find(
                ".instances(world_transforms.size(), proxy.instance_buffer)"),
            std::string::npos);
}

TEST(SceneRenderBridgeTests,
     ToolPreviewLightsRetainHighWaterPoolsAcrossCountChanges) {
  const std::string cache_header =
      read_source_file("src/render/tool_preview_renderable_cache.hpp");
  const std::string cache_source =
      read_source_file("src/render/tool_preview_renderable_cache.cpp");
  const std::string light_policy =
      read_source_file("src/render/light_render_policy.hpp");

  EXPECT_NE(cache_header.find("struct LightPool final"),
            std::string::npos);
  EXPECT_NE(cache_header.find("Grow,"), std::string::npos);
  EXPECT_NE(cache_source.find("build_inert_growth("), std::string::npos);
  EXPECT_NE(cache_source.find("plan_tool_preview_light_pools("),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      cache_source,
      "const bool enabled = proxy_index < record.instance_transforms.size()"));
  EXPECT_TRUE(source_contains_compact(
      cache_source,
      "update_filament_light_properties(manager, instance, proxy.light, "
      "record.light, proxy.enabled, enabled)"));
  EXPECT_TRUE(source_contains_compact(
      light_policy,
      "manager.setIntensity(instance, "
      "enabled ? filament_light_intensity(after) : 0.0f)"))
      << "inactive high-water proxies must remain inert";
}

TEST(SceneRenderBridgeTests,
     ToolPreviewRetirementAndClaimIndexesKeepIdleWorkLinear) {
  const std::string cache_header =
      read_source_file("src/render/tool_preview_renderable_cache.hpp");
  const std::string cache_source =
      read_source_file("src/render/tool_preview_renderable_cache.cpp");

  EXPECT_NE(cache_header.find("std::size_t live_proxy_count = 0u"),
            std::string::npos);
  EXPECT_NE(cache_header.find("exact_match_candidate_visits"),
            std::string::npos);
  EXPECT_NE(cache_header.find("structural_match_candidate_visits"),
            std::string::npos);
  EXPECT_NE(cache_header.find("retired_proxy_destroy_attempts"),
            std::string::npos);
  EXPECT_NE(cache_source.find("struct StableCandidateBucket"),
            std::string::npos);
  EXPECT_NE(cache_source.find("std::size_t cursor = 0u"),
            std::string::npos);
  EXPECT_NE(cache_source.find("group_exact_reuse_base_signature"),
            std::string::npos);
  EXPECT_EQ(cache_source.find("std::unordered_multimap"), std::string::npos)
      << "a claimed multimap range restarts at its claimed prefix for every "
         "preview group";

  const std::size_t count_start = cache_source.find(
      "std::size_t ToolPreviewRenderableCache::group_proxy_count(");
  const std::size_t count_end =
      cache_source.find("preview_material_signature(", count_start);
  ASSERT_NE(count_start, std::string::npos);
  ASSERT_NE(count_end, std::string::npos);
  const std::string count_source =
      cache_source.substr(count_start, count_end - count_start);
  EXPECT_NE(count_source.find("return group.live_proxy_count;"),
            std::string::npos);
  EXPECT_EQ(count_source.find("count_if"), std::string::npos);
  EXPECT_EQ(count_source.find("rendered_instances"), std::string::npos);
  EXPECT_EQ(count_source.find("shaded_instances"), std::string::npos);
  EXPECT_EQ(count_source.find("overlay_depth_instances"), std::string::npos);

  const std::size_t drain_start = cache_source.find(
      "void ToolPreviewRenderableCache::drain_retired_destroy_queue(");
  const std::size_t drain_end =
      cache_source.find("void ToolPreviewRenderableCache::destroy_group(",
                        drain_start);
  ASSERT_NE(drain_start, std::string::npos);
  ASSERT_NE(drain_end, std::string::npos);
  const std::string drain_source =
      cache_source.substr(drain_start, drain_end - drain_start);
  EXPECT_NE(drain_source.find(
                "++work_counters_.retired_proxy_destroy_attempts"),
            std::string::npos);
  EXPECT_EQ(drain_source.find("count_if"), std::string::npos);
}

TEST(SceneRenderBridgeTests, UsesQuadMaterialsForSelectionLineAndPointOverlay) {
  const std::string proxy_factory_source =
      read_source_file("src/render/overlay/overlay_proxy_factory.cpp");
  const std::string line_builder_source =
      read_source_file("src/render/overlay/overlay_line_mesh_builder.cpp");
  const std::string point_builder_source =
      read_source_file("src/render/overlay/overlay_point_mesh_builder.cpp");
  const std::string renderable_builder_source =
      read_source_file("src/render/overlay/overlay_renderable_builder.hpp");
  const std::string renderable_builder_impl =
      read_source_file("src/render/overlay/overlay_renderable_builder.cpp");
  const std::string projector_source =
      read_source_file("src/render/overlay/overlay_camera_projector.cpp");

  EXPECT_NE(line_builder_source.find("append_line_quad"), std::string::npos);
  EXPECT_NE(point_builder_source.find("append_point_quad"), std::string::npos);
  EXPECT_NE(proxy_factory_source.find("material.createInstance()"),
            std::string::npos);
  EXPECT_NE(proxy_factory_source.find("*overlay_line_material_"),
            std::string::npos);
  EXPECT_NE(proxy_factory_source.find("*overlay_point_material_"),
            std::string::npos);
  EXPECT_NE(proxy_factory_source.find("*overlay_icon_material_"),
            std::string::npos);
  EXPECT_NE(proxy_factory_source.find("setParameter(\"iconTexture\""),
            std::string::npos);
  const std::size_t point_light_icon_path = proxy_factory_source.find(
      "resource_root / \"icons\" / \"point_light.png\"");
  ASSERT_NE(point_light_icon_path, std::string::npos);
  const std::size_t point_light_icon_load_start =
      proxy_factory_source.rfind("load_png_texture", point_light_icon_path);
  ASSERT_NE(point_light_icon_load_start, std::string::npos);
  const std::size_t point_light_icon_load_end =
      proxy_factory_source.find("});", point_light_icon_path);
  ASSERT_NE(point_light_icon_load_end, std::string::npos);
  const std::string point_light_icon_load = proxy_factory_source.substr(
      point_light_icon_load_start,
      point_light_icon_load_end - point_light_icon_load_start);
  EXPECT_NE(point_light_icon_load.find(".flip_y = false"), std::string::npos);
  EXPECT_EQ(point_light_icon_load.find(".flip_y = true"), std::string::npos);
  EXPECT_NE(point_light_icon_load.find(".generate_mipmaps = true"),
            std::string::npos);
  EXPECT_NE(proxy_factory_source.find(
                "kPointLightIconTextureExtent = 1024u"),
            std::string::npos);
  EXPECT_NE(proxy_factory_source.find("make_point_light_icon_rgba()"),
            std::string::npos);
  EXPECT_NE(proxy_factory_source.find("load_rgba_texture("),
            std::string::npos);
  EXPECT_EQ(count_occurrences(
                proxy_factory_source,
                "filament::TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR"),
            2u);
  EXPECT_NE(proxy_factory_source.find("OverlayRenderableProxyKind::Icon"),
            std::string::npos);
  EXPECT_NE(proxy_factory_source.find("filament::VertexAttribute::CUSTOM0"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      proxy_factory_source,
      "AttributeType::FLOAT4, offsetof(OverlayLineVertex, position)"));
  EXPECT_TRUE(source_contains_compact(
      proxy_factory_source,
      ".attribute(filament::VertexAttribute::CUSTOM0, 1, "
      "AttributeType::FLOAT4, 0, sizeof(OverlayFloat4))"));
  EXPECT_TRUE(source_contains_compact(
      proxy_factory_source,
      ".attribute(filament::VertexAttribute::CUSTOM1, 2, "
      "AttributeType::FLOAT4, 0, sizeof(OverlayFloat4))"));
  EXPECT_TRUE(source_contains_compact(
      proxy_factory_source,
      "AttributeType::FLOAT4, offsetof(OverlayLineVertex, control)"));
  EXPECT_TRUE(source_contains_compact(
      proxy_factory_source,
      "AttributeType::FLOAT4, offsetof(OverlayPointVertex, position)"));
  EXPECT_NE(projector_source.find("OverlayProjectedPoint project"),
            std::string::npos);
  EXPECT_NE(projector_source.find("near_plane_distance(a)"), std::string::npos);
  EXPECT_NE(proxy_factory_source.find(
                "setParameter(\"lineWidth\", batch.width_pixels)"),
            std::string::npos);
  EXPECT_EQ(proxy_factory_source.find(
                "setParameter(\"point_size\", point_size_pixels)"),
            std::string::npos);
  EXPECT_NE(proxy_factory_source.find("setParameter(\"nearPlane\""),
            std::string::npos);
  EXPECT_NE(proxy_factory_source.find("setParameter(\"depthBias\""),
            std::string::npos);
  EXPECT_NE(renderable_builder_source.find("LineBatchKey"), std::string::npos);
  EXPECT_NE(renderable_builder_source.find("IconBatchKey"), std::string::npos);
  EXPECT_NE(
      renderable_builder_impl.find("OverlayPointMeshBuilder::build_icons"),
      std::string::npos);
  EXPECT_EQ(proxy_factory_source.find("PrimitiveType::LINES"),
            std::string::npos);
  EXPECT_EQ(proxy_factory_source.find("PrimitiveType::POINTS"),
            std::string::npos);
}

TEST(SceneRenderBridgeTests, BindsNormalsAndBuildsPrimitiveSpans) {
  const std::string source =
      read_source_file("src/render/mesh_renderable_factory.cpp");

  EXPECT_NE(source.find("mesh_tangent_frames(mesh, positions)"),
            std::string::npos);
  EXPECT_NE(source.find("positions = effective_mesh_positions(mesh, positions)"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(source,
      "positions.empty() ? v1.position - v0.position : positions[i1] - positions[i0]"));
  EXPECT_TRUE(source_contains_compact(source,
      "positions.empty() ? v2.position - v0.position : positions[i2] - positions[i0]"));
  EXPECT_NE(source.find("tangent_frame_from_basis("),
            std::string::npos);
  EXPECT_NE(source.find("delta1.x * delta2.y - delta1.y * delta2.x"),
            std::string::npos);
  EXPECT_NE(source.find("!std::isfinite(determinant)"),
            std::string::npos);
  EXPECT_NE(source.find("std::fabs(determinant) <= 0.000001f"),
            std::string::npos);
  EXPECT_NE(source.find("filament::VertexAttribute::TANGENTS"),
            std::string::npos);
  EXPECT_NE(source.find("coalesced_render_mesh_primitive_spans(mesh)"),
            std::string::npos);
  EXPECT_NE(source.find("primitive.index_offset"), std::string::npos);
  EXPECT_NE(source.find("primitive.index_count"), std::string::npos);
  EXPECT_NE(source.find("primitive.material_slot"), std::string::npos);
  EXPECT_NE(source.find("material->material_for_slot(primitive.material_slot)"),
            std::string::npos);
}

TEST(SceneRenderBridgeTests, UsesRenderSceneSnapshotsForIncrementalMeshSync) {
  const std::string snapshot_header =
      read_source_file("src/render/render_scene_snapshot.hpp");
  const std::string stamp_header =
      read_source_file("src/render/render_source_stamp.hpp");
  const std::string sync_header =
      read_source_file("src/render/scene_render_sync_service.hpp");
  const std::string sync_source =
      read_source_file("src/render/scene_render_sync_service.cpp");
  const std::string cache_source =
      read_source_file("src/render/mesh_renderable_cache.cpp");
  const std::string mesh_record = quader::test::guardrail::source_slice(
      snapshot_header, "struct RenderSceneMeshRecord",
      "struct RenderSceneLightRecord");
  const std::string light_record = quader::test::guardrail::source_slice(
      snapshot_header, "struct RenderSceneLightRecord",
      "struct RenderSceneSnapshotDirtyState");

  EXPECT_NE(snapshot_header.find("struct RenderSceneSnapshot"),
            std::string::npos);
  EXPECT_NE(snapshot_header.find("RenderSceneMeshRecord"), std::string::npos);
  EXPECT_NE(snapshot_header.find("RenderSceneLightRecord"), std::string::npos);
  EXPECT_NE(snapshot_header.find(
                "struct RenderSceneSnapshot : RenderSourceStamp"),
            std::string::npos);
  EXPECT_NE(stamp_header.find("std::uint64_t scene_revision"),
            std::string::npos);
  EXPECT_NE(stamp_header.find("std::uint64_t asset_revision"),
            std::string::npos);
  EXPECT_NE(sync_header.find("sync(const RenderSceneSnapshot"),
            std::string::npos);
  EXPECT_EQ(sync_header.find("sync(const SceneDocument"), std::string::npos);
  EXPECT_EQ(sync_header.find("sync(const AssetRegistry"), std::string::npos);

  EXPECT_NE(sync_header.find("RenderSourceStamp synced_source_stamp_"),
            std::string::npos);
  for (const std::string_view retired_scalar : {
           "synced_revision_", "synced_local_visibility_revision_",
           "synced_asset_revision_", "synced_project_material_revision_",
           "synced_instance_edit_scope_", "synced_scene_binding_",
       }) {
    EXPECT_EQ(sync_header.find(retired_scalar), std::string::npos)
        << retired_scalar;
    EXPECT_EQ(sync_source.find(retired_scalar), std::string::npos)
        << retired_scalar;
  }
  EXPECT_NE(sync_source.find("render_asset_revisions_changed"),
            std::string::npos);
  EXPECT_NE(sync_source.find("kRenderSceneSyncDocumentDependencies"),
            std::string::npos);
  EXPECT_NE(sync_source.find("kRenderSceneSyncAssetDependencies"),
            std::string::npos);
  EXPECT_NE(sync_source.find("kRenderSceneSyncContextDependencies"),
            std::string::npos);
  EXPECT_NE(sync_source.find("render_source_dependencies_match("),
            std::string::npos);
  EXPECT_NE(sync_source.find("synced_source_stamp_ = snapshot"),
            std::string::npos);
  EXPECT_EQ(
      sync_source.find("build_scene_render_extraction_plan(document, assets,"),
      std::string::npos);
  EXPECT_EQ(sync_source.find("const SceneDocument"), std::string::npos);
  EXPECT_EQ(sync_source.find("const AssetRegistry"), std::string::npos);
  EXPECT_NE(sync_source.find("mesh_cache_->needs_rebuild"), std::string::npos);
  EXPECT_EQ(mesh_record.find("const MeshAsset *"), std::string::npos);
  EXPECT_EQ(mesh_record.find("const MeshComponent *"), std::string::npos);
  EXPECT_EQ(mesh_record.find("const MaterialComponent *"), std::string::npos);
  EXPECT_EQ(light_record.find("const LightComponent *"), std::string::npos);
  EXPECT_NE(cache_source.find(
                "existing->second.mesh_revision != mesh.mesh.mesh_revision"),
            std::string::npos);
  EXPECT_NE(cache_source.find(
                "existing->second.material_revision != mesh.material_revision"),
            std::string::npos);
  EXPECT_NE(
      cache_source.find(
          "factory_->update_transform(existing->second, mesh.world_transform)"),
      std::string::npos);
  const std::size_t change_start = sync_source.find(
      "bool SceneRenderSyncService::render_asset_revisions_changed");
  ASSERT_NE(change_start, std::string::npos);
  const std::size_t rebuild_start =
      sync_source.find("void SceneRenderSyncService::rebuild(");
  ASSERT_NE(rebuild_start, std::string::npos);
  const std::string change_source =
      sync_source.substr(change_start, rebuild_start - change_start);
  EXPECT_EQ(change_source.find("document.entities()"), std::string::npos);
  EXPECT_EQ(change_source.find("document.is_effectively_visible"),
            std::string::npos);
  EXPECT_EQ(change_source.find("assets.mesh("), std::string::npos);
  EXPECT_EQ(change_source.find("material_binding_->material_revision"),
            std::string::npos);
  const std::string rebuild_source = sync_source.substr(rebuild_start);
  EXPECT_NE(rebuild_source.find("can_sync_transforms_only(snapshot)"),
            std::string::npos);
  EXPECT_NE(sync_source.find("mesh.transform_dirty"), std::string::npos);
  EXPECT_NE(sync_source.find("light.transform_dirty"), std::string::npos);
  EXPECT_NE(sync_source.find("openLocalTransformTransaction()"),
            std::string::npos);
  EXPECT_NE(sync_source.find("commitLocalTransformTransaction()"),
            std::string::npos);
  EXPECT_NE(rebuild_source.find(
                "mesh_cache_->remove_except(snapshot.live_mesh_entities)"),
            std::string::npos);
  EXPECT_EQ(rebuild_source.find("mesh_proxies_.clear()"), std::string::npos);
}

TEST(SceneRenderBridgeTests, UsesAnisotropicSamplingForMeshTextures) {
  const std::string source =
      read_source_file("src/render/material_binding_service.cpp");

  EXPECT_NE(source.find("kMeshTextureAnisotropy"), std::string::npos);
  EXPECT_NE(source.find("sampler.setAnisotropy(kMeshTextureAnisotropy)"),
            std::string::npos);
  EXPECT_NE(
      source.find("filament::TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR"),
      std::string::npos);
}

TEST(RenderSystemTests, ShadedViewportUsesClearColorWithoutSkybox) {
  const std::string source = read_source_file("src/render/render_system.cpp");

  EXPECT_NE(source.find("renderer.setClearOptions(clear)"),
            std::string::npos);
  EXPECT_NE(source.find("configure_renderer(*renderer_)"), std::string::npos);
  EXPECT_NE(source.find("clear.discard = false"), std::string::npos);
  EXPECT_NE(source.find("renderer_clear_color_ = {environment.clear_color.x"),
            std::string::npos);
  EXPECT_EQ(source.find("scene_->setSkybox(skybox_)"), std::string::npos);
  EXPECT_EQ(source.find("Skybox::Builder()"), std::string::npos);
}

TEST(RenderSystemTests,
     ExposesCompletedFramePerformanceStatsFromRendererHistory) {
  const std::string header = read_source_file("src/render/render_system.hpp");
  const std::string source = read_source_file("src/render/render_system.cpp");

  EXPECT_NE(header.find(
                "RenderFramePerformanceStats frame_performance_stats() const"),
            std::string::npos);
  EXPECT_NE(header.find("RenderFramePerformanceStats frame_performance_stats_"),
            std::string::npos);
  EXPECT_NE(header.find("RenderPresentationFrameTracker"), std::string::npos);
  EXPECT_NE(header.find("begin_presentation_frame()"), std::string::npos);
  EXPECT_NE(header.find("complete_presentation_frame("), std::string::npos);
  EXPECT_NE(source.find("presentation_frame_tracker_.begin_presentation_frame("),
            std::string::npos);
  EXPECT_NE(source.find("presentation_frame_tracker_.complete_presentation_frame("),
            std::string::npos);
  EXPECT_EQ(source.find("update_frame_performance_stats_after_end_frame"),
            std::string::npos);
  EXPECT_NE(source.find("renderer->getFrameInfoHistory(history_size)"),
            std::string::npos);
  EXPECT_NE(source.find("renderer->getMaxFrameHistorySize()"),
            std::string::npos);
  EXPECT_NE(source.find("performance_renderer_"), std::string::npos);
  EXPECT_NE(source.find("info.denoisedGpuFrameDuration"), std::string::npos);
  EXPECT_NE(source.find("info.gpuFrameDuration"), std::string::npos);
  EXPECT_NE(source.find("filament::Renderer::FrameInfo::INVALID"),
            std::string::npos);
  EXPECT_NE(source.find("filament::Renderer::FrameInfo::PENDING"),
            std::string::npos);
}

TEST(RenderSystemTests, PropagatesDisplayRefreshRateToEveryRenderer) {
  const std::string header = read_source_file("src/render/render_system.hpp");
  const std::string source = read_source_file("src/render/render_system.cpp");

  EXPECT_NE(header.find("set_display_refresh_rate_hz("), std::string::npos);
  EXPECT_NE(header.find("display_refresh_rate_hz_ = 60.0f"),
            std::string::npos);
  EXPECT_NE(header.find("std::vector<std::shared_ptr<RenderSurfaceState>> "
                        "additional_surfaces_"),
            std::string::npos);
  EXPECT_NE(source.find("normalized_display_refresh_rate_hz("),
            std::string::npos);
  EXPECT_NE(source.find("renderer.setDisplayInfo("), std::string::npos);
  EXPECT_NE(source.find("renderer_->setDisplayInfo(display_info)"),
            std::string::npos);
  EXPECT_NE(source.find("surface->renderer->setDisplayInfo(display_info)"),
            std::string::npos);
  EXPECT_NE(source.find("additional_surfaces_.push_back(surface)"),
            std::string::npos);
  EXPECT_NE(source.find("std::erase_if(additional_surfaces_"),
            std::string::npos);
}

TEST(RenderSystemTests, VsyncUsesBackendModeAndNonblockingGpuReadiness) {
  const std::string source = read_source_file("src/render/render_system.cpp");
  const std::string platform_header =
      read_source_file("src/render/render_vulkan_composition_platform.hpp");
  const std::string synchronize = source_slice(
      source, "void RenderSystem::synchronize_surface_presentation(",
      "void RenderSystem::destroy_additional_surface(");
  const std::string readiness = source_slice(
      source, "bool RenderSystem::ready_for_next_frame()",
      "bool RenderSystem::begin_frame_with_swap_chain(");
  const std::string toggle = source_slice(
      source, "void RenderSystem::set_vsync_enabled(",
      "bool RenderSystem::presentation_is_synchronized()");
  EXPECT_NE(source.find("#include \"render/render_vulkan_composition_platform.hpp\""),
            std::string::npos);
  EXPECT_NE(source.find(": public RenderVulkanCompositionPlatform"),
            std::string::npos);
  EXPECT_NE(platform_header.find(": public filament::backend::VulkanPlatformWindows"),
            std::string::npos);
  EXPECT_NE(source.find("onSwapChainPresentModeSelected("), std::string::npos);
  EXPECT_NE(source.find("CONFIG_UNSYNCHRONIZED"), std::string::npos);
  EXPECT_NE(readiness.find("shouldRenderFrame()"), std::string::npos);
  EXPECT_NE(readiness.find("submitted_last_frame"), std::string::npos);
  EXPECT_EQ(readiness.find("flushAndWait"), std::string::npos);
  EXPECT_LT(synchronize.find("surface.renderer_synchronized == synchronized"),
            synchronize.find("setDisplayInfo("));
  EXPECT_LT(toggle.find("vsync_enabled_ == enabled"),
            toggle.find("flushAndWait()"));
  EXPECT_EQ(toggle.find("createRenderer()"), std::string::npos);
  const std::string preview = read_source_file("src/ui/qt_material_preview_surface.cpp");
  const std::string preview_render = source_slice(
      preview, "bool QtMaterialPreviewSurface::render_frame()",
      "void QtMaterialPreviewSurface::synchronize_render_size()");
  EXPECT_NE(preview_render.find("needs_initial_frame()"), std::string::npos);
  EXPECT_NE(preview_render.find("render_pending_ || swap_chain_.needs_initial_frame()"),
            std::string::npos);
}

TEST(ViewportRendererTests,
     ViewportPreferencesAndRenderedEnvironmentOwnTheirBackdrops) {
  const std::string renderer =
      read_source_file("src/render/viewport_renderer.cpp");
  const std::string renderer_header =
      read_source_file("src/render/viewport_renderer.hpp");
  const std::string environment_library =
      read_source_file("src/render/filament_environment_library.cpp");
  const std::string viewport_panel =
      read_source_file("src/ui/qt_viewport_panel.cpp");

  EXPECT_TRUE(source_contains_compact(
      renderer, "viewport_clear_color(settings.environment, rendered_background, "
                "viewport_settings)"));
  EXPECT_TRUE(source_contains_compact(
      renderer_header,
      "void apply_scene_settings(const SceneSettings &settings, "
      "bool rendered_mode, const ViewportSettings &viewport_settings);"));
  EXPECT_TRUE(source_contains_compact(
      viewport_panel, "renderer.apply_scene_settings(scene_settings, "
                      "viewport_shading_uses_scene_lighting(shading), pane_settings)"));
  EXPECT_NE(renderer.find("if (rendered_mode)"), std::string::npos);
  EXPECT_NE(renderer.find("environment.clear_color.x"), std::string::npos);
  EXPECT_NE(renderer.find("ViewportColorSettingId::Background"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      renderer, "add_setting_color(signature, resolved_background)"));
  EXPECT_TRUE(source_contains_compact(
      renderer, "render_pass_coordinator_.set_background_color(resolved_background)"));
  EXPECT_EQ(renderer_header.find("camera_near_plane_"), std::string::npos);
  EXPECT_EQ(renderer_header.find("camera_far_plane_"), std::string::npos);
  EXPECT_NE(renderer.find("controller.state().near_plane"), std::string::npos);
  EXPECT_NE(renderer.find("controller.state().far_plane"), std::string::npos);
  EXPECT_NE(
      environment_library.find(
          "settings.show_background && settings.background_intensity > 0.0f"),
      std::string::npos);
}

TEST(RenderSystemTests, InitializeUnwindsPartialFilamentStateOnFailure) {
  const std::string header = read_source_file("src/render/render_system.hpp");
  const std::string source = read_source_file("src/render/render_system.cpp");

  EXPECT_NE(header.find("[[nodiscard]] bool initialized() const"),
            std::string::npos);
  const std::size_t initialize = source.find("bool RenderSystem::initialize(");
  ASSERT_NE(initialize, std::string::npos);
  const std::size_t first_shutdown = source.find("shutdown();", initialize);
  const std::size_t engine_creation = source.find("engine_ = filament::Engine::create", initialize);
  ASSERT_NE(first_shutdown, std::string::npos);
  ASSERT_NE(engine_creation, std::string::npos);
  EXPECT_LT(first_shutdown, engine_creation);
  EXPECT_NE(source.find("const auto fail = [&](std::string_view message)"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(source, "shutdown(); return false;"));
  EXPECT_NE(source.find("if (!primary_surface_->swap_chain)"), std::string::npos);
  EXPECT_NE(source.find("if (!renderer_)"), std::string::npos);
  EXPECT_NE(source.find("if (!scene_)"), std::string::npos);
  EXPECT_NE(source.find(
                "if (!material_library_.initialize(*engine_, executable_dir))"),
            std::string::npos);
  EXPECT_NE(source.find("primary_surface_ != nullptr ? primary_surface_->swap_chain : nullptr"),
            std::string::npos);
  EXPECT_NE(source.find("if (!renderer->beginFrame(swap_chain))"),
            std::string::npos);
}

TEST(RenderSystemTests,
     NativeViewportSwapChainsOwnDedicatedFilamentRenderers) {
  const std::string header = read_source_file("src/render/render_system.hpp");
  const std::string source = read_source_file("src/render/render_system.cpp");
  const std::string surface =
      read_source_file("src/ui/qt_viewport_surface.cpp");

  EXPECT_NE(source.find("filament::Renderer *renderer = nullptr"),
            std::string::npos);
  EXPECT_NE(header.find("bool begin_frame(RenderAdditionalSwapChain &surface)"),
            std::string::npos);
  EXPECT_NE(header.find("void end_frame(RenderAdditionalSwapChain &surface)"),
            std::string::npos);
  EXPECT_NE(source.find("filament::Renderer *renderer = "
                        "engine_->createRenderer()"),
            std::string::npos);
  EXPECT_NE(source.find("configure_renderer(*renderer)"), std::string::npos);
  EXPECT_NE(source.find(
                "RenderAdditionalSwapChain(std::move(surface))"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(source,
      "begin_frame_with_renderer(surface.state_->renderer, "
      "surface.state_->swap_chain, surface.state_.get())"));
  EXPECT_NE(source.find("viewport_renderer.render(*surface.state_->renderer)"),
            std::string::npos);
  EXPECT_NE(source.find("end_frame_with_renderer(surface.state_->renderer)"),
            std::string::npos);
  EXPECT_NE(surface.find("render_system_->begin_frame(swap_chain_)"),
            std::string::npos);
  EXPECT_NE(surface.find(
                "render_system_->render_viewport(renderer_, swap_chain_)"),
            std::string::npos);
  EXPECT_NE(surface.find("render_system_->end_frame(swap_chain_)"),
            std::string::npos);
  EXPECT_EQ(surface.find("begin_frame(swap_chain_.get())"), std::string::npos);
}

TEST(RenderProxyLifecycleTests, FilamentResourceCreationUsesRenderOwnedGuards) {
  const std::string light_cache_source =
      read_source_file("src/render/light_renderable_cache.cpp");
  const std::string grid_renderer_source =
      read_source_file("src/render/viewport_grid_renderer.cpp");
  const std::string render_target_source =
      read_source_file("src/render/viewport_render_target.cpp");

  EXPECT_NE(light_cache_source.find("LightProxyCreationGuard creation_guard"),
            std::string::npos);
  EXPECT_NE(light_cache_source.find("creation_guard.release()"),
            std::string::npos);
  EXPECT_NE(grid_renderer_source.find("struct GroundGridCreationGuard"),
            std::string::npos);
  EXPECT_NE(grid_renderer_source.find("creation_guard.vertex_buffer"),
            std::string::npos);
  EXPECT_NE(grid_renderer_source.find("creation_guard.release()"),
            std::string::npos);
  EXPECT_NE(render_target_source.find("struct RenderTargetCreationGuard"),
            std::string::npos);
  EXPECT_NE(render_target_source.find("creation_guard.color_texture"),
            std::string::npos);
  EXPECT_NE(render_target_source.find("creation_guard.release()"),
            std::string::npos);
  const std::size_t register_texture =
      render_target_source.find("texture_registry_->register_texture(");
  ASSERT_NE(register_texture, std::string::npos);
  EXPECT_NE(render_target_source.find("creation_guard.color_texture",
                                      register_texture),
            std::string::npos);
  EXPECT_LT(register_texture,
            render_target_source.find("creation_guard.release()"));
}

TEST(SceneRenderBridgeTests, UsesExplicitTextureColorSpaceForMaterialUploads) {
  const std::string texture_source =
      read_source_file("src/render/texture_binding_service.cpp");
  const std::string png_loader_source =
      read_source_file("src/render/png_texture_loader.cpp");
  const std::string material_source =
      read_source_file("src/render/material_binding_service.cpp");
  const std::string factory_source =
      read_source_file("src/render/mesh_renderable_factory.cpp");
  const std::string app_source = read_source_file("src/app/app.cpp");
  const std::string bootstrap_source = read_source_file(
      "src/editor/session/project_asset_bootstrap_service.cpp");
  const std::string material_library_source =
      read_source_file("src/render/filament_material_library.cpp");
  const std::string scene_bridge_source =
      read_source_file("src/render/scene_render_bridge.cpp");

  EXPECT_NE(
      texture_source.find(
          "create_solid_texture(255, 255, 255, 255, TextureColorSpace::Srgb)"),
      std::string::npos);
  EXPECT_NE(texture_source.find("create_solid_texture(255, 255, 255, 255, "
                                "TextureColorSpace::Linear)"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(texture_source,
                                      "load_png_texture(*engine_, path, "
                                      "PngTextureLoadOptions{"));
  EXPECT_TRUE(source_contains_compact(
      png_loader_source, "color_space == PngTextureColorSpace::Srgb ? "
                         "filament::Texture::InternalFormat::SRGB8_A8 : "
                         "filament::Texture::InternalFormat::RGBA8"));
  EXPECT_NE(material_source.find("MinFilter::LINEAR_MIPMAP_LINEAR"),
            std::string::npos);
  EXPECT_NE(png_loader_source.find("mip_level_count(width, height)"),
            std::string::npos);
  EXPECT_NE(png_loader_source.find("Texture::Usage::GEN_MIPMAPPABLE"),
            std::string::npos);
  EXPECT_NE(png_loader_source.find("texture->generateMipmaps(engine)"),
            std::string::npos);
  EXPECT_NE(material_source.find("material_instance->setCullingMode("),
            std::string::npos);
  EXPECT_NE(material_source.find("double_sided ?"), std::string::npos);
  EXPECT_EQ(material_source.find("colorCulling"), std::string::npos);
  EXPECT_EQ(material_source.find("setCullingMode(\n            colorCulling"),
            std::string::npos);
  EXPECT_NE(
      material_source.find("filament::MaterialInstance::CullingMode::NONE"),
      std::string::npos);
  EXPECT_NE(
      material_source.find(": filament::MaterialInstance::CullingMode::BACK"),
      std::string::npos);
  EXPECT_EQ(
      material_source.find(
          "setCullingMode(filament::MaterialInstance::CullingMode::BACK);"),
      std::string::npos);
  EXPECT_NE(bootstrap_source.find(
                "session.assets().add_texture(std::move(roughness));"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      bootstrap_source, "set_material_texture_slot(expected_default, "
                        "material_texture_slot::kBaseColor, albedo_texture)"));
  EXPECT_EQ(
      bootstrap_source.find(
          "make_default_quader_material(\"Default Quader\", albedo_texture"),
      std::string::npos);
  EXPECT_EQ(bootstrap_source.find("const AssetId roughnessTexture = "
                                  "session.assets().add_texture"),
            std::string::npos);
  EXPECT_EQ(app_source.find("set_material_texture_slot("), std::string::npos);
  EXPECT_EQ(app_source.find("make_default_quader_material("),
            std::string::npos);
  EXPECT_NE(app_source.find(
                "render_system_.initialize_scene_render_bridge(render_bridge_"),
            std::string::npos);
  EXPECT_EQ(app_source.find("render_system_.quader_mesh_material()"),
            std::string::npos);
  EXPECT_EQ(app_source.find("render_system_.quader_mesh_lit_material()"),
            std::string::npos);
  EXPECT_NE(material_library_source.find("filament_material_catalog.hpp"),
            std::string::npos);
  EXPECT_NE(
      material_library_source.find("std::string(kFilamentMaterialDirectory)"),
      std::string::npos);
  EXPECT_NE(
      material_library_source.find("std::string(kQuaderMeshMaterialFile)"),
      std::string::npos);
  EXPECT_NE(
      material_library_source.find("std::string(kQuaderMeshLitMaterialFile)"),
      std::string::npos);
  EXPECT_EQ(
      material_library_source.find("materials\" / \"quader_mesh.filamat\""),
      std::string::npos);
  EXPECT_EQ(
      material_library_source.find("materials\" / \"quader_mesh_lit.filamat\""),
      std::string::npos);
  EXPECT_NE(scene_bridge_source.find("material_binding_.initialize(shaded_mesh_"
                                     "material, rendered_mesh_material,"),
            std::string::npos);
  EXPECT_NE(scene_bridge_source.find("texture_binding_)"), std::string::npos);
  EXPECT_NE(
      factory_source.find("material_binding_->create_mesh_material_instance("),
      std::string::npos);
  EXPECT_NE(
      factory_source.find("primitive_material, double_sided, editor_unlit)"),
      std::string::npos);
  EXPECT_NE(
      material_source.find(
          "editor_unlit ? *shaded_mesh_material_ : *rendered_mesh_material_"),
      std::string::npos);
  EXPECT_NE(material_source.find("if (!editor_unlit)"), std::string::npos);
  EXPECT_NE(material_source.find("material_texture_slot::kRoughness"),
            std::string::npos);
  EXPECT_NE(material_source.find("material_texture_slot::kMetallic"),
            std::string::npos);
  EXPECT_NE(material_source.find("material_texture_slot::kNormal"),
            std::string::npos);
  EXPECT_NE(material_source.find("apply_mesh_surface_grid"), std::string::npos);
  EXPECT_NE(material_source.find("surfaceGridMinorColor"), std::string::npos);
  EXPECT_NE(material_source.find("surfaceGridMajorSize"), std::string::npos);
  EXPECT_NE(material_source.find("ViewportBoolSettingId::ShowMeshGrid"),
            std::string::npos);
  EXPECT_NE(material_source.find("ViewportColorSettingId::MeshGridMinor"),
            std::string::npos);
  EXPECT_NE(material_source.find("ViewportColorSettingId::MeshGridMajor"),
            std::string::npos);
  EXPECT_NE(material_source.find("ViewportFloatSettingId::GridMinorLineSize"),
            std::string::npos);
  EXPECT_EQ(
      material_source.find("material_instance->setParameter(\"editor_unlit\""),
      std::string::npos);
  EXPECT_EQ(factory_source.find(".culling(!doubleSided)"), std::string::npos);
  EXPECT_NE(texture_source.find("TextureCacheKey cache_key"),
            std::string::npos);
  EXPECT_NE(texture_source.find(".color_space = texture->color_space"),
            std::string::npos);
  EXPECT_EQ(texture_source.find("bool srgb"), std::string::npos);
  EXPECT_EQ(material_source.find("bool srgb"), std::string::npos);
}

TEST(SceneRenderBridgeTests,
     TextureBindingServiceReclaimsOnlyAfterFinalMaterialConsumer) {
  const std::string texture_header =
      read_source_file("src/render/texture_binding_service.hpp");
  const std::string texture_source =
      read_source_file("src/render/texture_binding_service.cpp");

  const std::string material_source =
      read_source_file("src/render/material_binding_service.cpp");
  const std::string factory_source =
      read_source_file("src/render/mesh_renderable_factory.cpp");
  EXPECT_NE(texture_header.find("consumer_count"), std::string::npos);
  EXPECT_NE(texture_source.find("--cached->second.consumer_count == 0u"),
            std::string::npos);
  EXPECT_NE(material_source.find("retain_material_textures(*source, *duplicate)"),
            std::string::npos);
  EXPECT_NE(factory_source.find("duplicate_mesh_material_instance(instance)"),
            std::string::npos);
  EXPECT_NE(factory_source.find("destroy_mesh_material_instance(engine, material_instance)"),
            std::string::npos);
  const std::size_t instance_destroy =
      material_source.find("engine.destroy(material_instance)");
  const std::size_t texture_release =
      material_source.find("release_material_textures(consumer)");
  ASSERT_NE(instance_destroy, std::string::npos);
  ASSERT_NE(texture_release, std::string::npos);
  EXPECT_LT(instance_destroy, texture_release);

  const std::size_t bind_scene =
      texture_source.find("void TextureBindingService::bind_scene(");
  const std::size_t resolve_texture =
      texture_source.find("TextureBindingService::resolve_texture(");
  ASSERT_NE(bind_scene, std::string::npos);
  ASSERT_NE(resolve_texture, std::string::npos);
  ASSERT_LT(bind_scene, resolve_texture);
  const std::string binding_release =
      texture_source.substr(bind_scene, resolve_texture - bind_scene);
  EXPECT_EQ(binding_release.find("destroy_texture("),
            std::string::npos);
  EXPECT_EQ(binding_release.find("texture_cache_.clear()"),
            std::string::npos)
      << "Scene switching must not invalidate a still-live retired consumer.";
}

TEST(SceneRenderBridgeTests,
     SkipsOverlayProxyRebuildWhenOverlayPayloadIsUnchanged) {
  const std::string cache_source =
      read_source_file("src/render/overlay/overlay_renderable_cache.cpp");
  const std::string cache_header =
      read_source_file("src/render/overlay/overlay_renderable_cache.hpp");
  const std::string builder_source =
      read_source_file("src/render/overlay/overlay_renderable_builder.cpp");
  const std::string signature_header =
      read_source_file("src/render/render_signature_builder.hpp");
  const std::string proxy_factory_source =
      read_source_file("src/render/overlay/overlay_proxy_factory.cpp");

  EXPECT_NE(
      cache_source.find("OverlayRenderableBuilder::frame_signatures(frame)"),
      std::string::npos);
  EXPECT_NE(
      cache_source.find(
          "OverlayRenderableBuilder::layer_build_signature(slot.signatures"),
      std::string::npos);
  EXPECT_NE(cache_header.find(
                "std::array<OverlaySlot, kMaxViewportOverlaySlots> slots_"),
            std::string::npos);
  EXPECT_NE(cache_header.find("OverlayFrameSignatures signatures"),
            std::string::npos);
  EXPECT_NE(cache_source.find("slot.proxy_groups"), std::string::npos);
  EXPECT_NE(signature_header.find("struct OverlayFrameSignatures"),
            std::string::npos);
  EXPECT_NE(builder_source.find(
                "RenderSignatureBuilder::overlay_frame_signatures(frame)"),
            std::string::npos);
  EXPECT_NE(cache_source.find("slot.frame_signature"), std::string::npos);
  EXPECT_NE(cache_source.find("OverlayRenderableBuilder::camera_signature"),
            std::string::npos);
  EXPECT_NE(cache_source.find("const bool camera_changed = camera_signature != "
                              "slot.camera_signature"),
            std::string::npos);
  EXPECT_NE(cache_source.find(
                "const bool layer_changed = layer_mask != slot.layer_mask"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      cache_source,
      "if (!frame_changed) {"
      "if (!camera_changed && !layer_changed) { return false; }"));
  EXPECT_NE(cache_source.find("group.signature = 0;"), std::string::npos);
  EXPECT_NE(cache_source.find("rebuild_slot(slot, layer_changed);"),
            std::string::npos);
  EXPECT_NE(cache_source.find("if (group.signature == layer_signature)"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      cache_source, "layer_has_renderable_primitives(slot.signatures, layer)"));
  EXPECT_TRUE(source_contains_compact(
      cache_source,
      "if (!rebuild_camera_independent_layers &&"
      "!OverlayRenderableBuilder::layer_build_depends_on_camera(layer)) {"
      "continue; }"));
  EXPECT_TRUE(source_contains_compact(
      cache_source,
      "factory_->update_layer_proxies(group.proxies, plan, layer_mask)"));
  EXPECT_TRUE(source_contains_compact(
      cache_source,
      "factory_->create_layer_proxies(plan, layer_mask)"));
  EXPECT_NE(proxy_factory_source.find(
                "bool OverlayProxyFactory::update_layer_proxies"),
            std::string::npos);
  EXPECT_NE(builder_source.find("viewport_overlay_draw_order(layer) * 64"),
            std::string::npos);
}

TEST(SceneRenderBridgeTests,
     ReusesCanonicalLineIndicesWhenOnlyProjectedVerticesChange) {
  const std::string proxy_factory_source =
      read_source_file("src/render/overlay/overlay_proxy_factory.cpp");
  const std::string line_builder_source =
      read_source_file("src/render/overlay/overlay_line_mesh_builder.cpp");

  const std::size_t line_update = proxy_factory_source.find(
      "void OverlayProxyFactory::update_line_proxy");
  const std::size_t edit_wire_update = proxy_factory_source.find(
      "void OverlayProxyFactory::update_edit_wire_proxy");
  const std::size_t point_update = proxy_factory_source.find(
      "void OverlayProxyFactory::update_point_proxy");
  ASSERT_NE(line_update, std::string::npos);
  ASSERT_NE(edit_wire_update, std::string::npos);
  ASSERT_NE(point_update, std::string::npos);
  ASSERT_LT(line_update, edit_wire_update);
  ASSERT_LT(edit_wire_update, point_update);

  const std::string line_update_source = proxy_factory_source.substr(
      line_update, edit_wire_update - line_update);
  const std::string edit_wire_update_source = proxy_factory_source.substr(
      edit_wire_update, point_update - edit_wire_update);
  EXPECT_NE(line_update_source.find("update_canonical_quad_proxy_vertices"),
            std::string::npos);
  EXPECT_NE(
      edit_wire_update_source.find("update_canonical_quad_proxy_vertices"),
      std::string::npos);
  EXPECT_EQ(line_update_source.find("update_proxy_buffers"), std::string::npos);
  EXPECT_EQ(edit_wire_update_source.find("update_proxy_buffers"),
            std::string::npos);

  const std::size_t canonical_update = proxy_factory_source.find(
      "void update_canonical_quad_proxy_vertices");
  const std::size_t namespace_end =
      proxy_factory_source.find("} // namespace", canonical_update);
  ASSERT_NE(canonical_update, std::string::npos);
  ASSERT_NE(namespace_end, std::string::npos);
  const std::string canonical_update_source = proxy_factory_source.substr(
      canonical_update, namespace_end - canonical_update);
  EXPECT_NE(canonical_update_source.find("update_proxy_vertex_buffer"),
            std::string::npos);
  EXPECT_EQ(canonical_update_source.find("index_buffer->setBuffer"),
            std::string::npos);

  EXPECT_TRUE(source_contains_compact(
      proxy_factory_source,
      "proxy.vertex_count == mesh.vertices.size() && proxy.index_count == "
      "mesh.indices.size()"));
  EXPECT_TRUE(source_contains_compact(
      line_builder_source,
      "mesh.indices.push_back(offset); mesh.indices.push_back(offset + 1u); "
      "mesh.indices.push_back(offset + 2u); mesh.indices.push_back(offset); "
      "mesh.indices.push_back(offset + 2u); mesh.indices.push_back(offset + "
      "3u);"));
}

TEST(SceneRenderBridgeTests,
     ConvertsSceneOverlayColorsToLinearBeforeViewportEncode) {
  const std::string source =
      read_source_file("src/render/overlay/overlay_face_fill_builder.cpp");

  EXPECT_NE(source.find("rgba8_srgb_to_linear(triangle.color)"),
            std::string::npos);
  EXPECT_NE(source.find("linear_color"), std::string::npos);
}

TEST(SceneRenderBridgeTests,
     MeshRenderablesReceiveShadowsAndViewportEnablesShadowing) {
  const std::string mesh_factory =
      read_source_file({"src", "render", "mesh_renderable_factory.cpp"});
  const std::string mesh_cache =
      read_source_file({"src", "render", "mesh_renderable_cache.cpp"});
  const std::string viewport_renderer =
      read_source_file({"src", "render", "viewport_renderer.cpp"});
  const std::string viewport_panel =
      read_source_file({"src", "ui", "qt_viewport_panel.cpp"});

  EXPECT_NE(mesh_factory.find(".castShadows(cast_shadows)"), std::string::npos);
  EXPECT_NE(mesh_factory.find(".receiveShadows(!editor_unlit && !depth_only)"),
            std::string::npos);
  EXPECT_NE(mesh_factory.find("material_instance->setColorWrite(false)"),
            std::string::npos);
  EXPECT_NE(mesh_factory.find("material_instance->setDepthWrite(true)"),
            std::string::npos);
  EXPECT_NE(mesh_factory.find("material_instance->setDepthCulling(true)"),
            std::string::npos);
  EXPECT_NE(mesh_factory.find(".channel(channel)"), std::string::npos);
  EXPECT_NE(mesh_cache.find("kViewportRenderedSceneLayerMask"),
            std::string::npos);
  EXPECT_NE(mesh_cache.find("kViewportShadedSceneLayerMask"),
            std::string::npos);
  EXPECT_NE(mesh_cache.find("kViewportOverlayLayerMask"), std::string::npos);
  EXPECT_NE(mesh_cache.find("kViewportOverlayDepthChannel"), std::string::npos);
  EXPECT_NE(mesh_cache.find("rendered_mesh_proxies_"), std::string::npos);
  EXPECT_NE(mesh_cache.find("shaded_mesh_proxies_"), std::string::npos);
  EXPECT_NE(mesh_cache.find("overlay_depth_mesh_proxies_"), std::string::npos);
  EXPECT_NE(viewport_renderer.find("view_->setShadowingEnabled(true)"),
            std::string::npos);
  EXPECT_NE(
      viewport_renderer.find("view_->setShadowType(filament::ShadowType::PCF)"),
      std::string::npos);
  EXPECT_NE(
      viewport_renderer.find("overlay_view_->setPostProcessingEnabled(false)"),
      std::string::npos);
  EXPECT_NE(viewport_renderer.find(
                "overlay_view_->setAntiAliasing(filament::AntiAliasing::NONE)"),
            std::string::npos);
  EXPECT_NE(viewport_renderer.find(
                "overlay_view_->setDithering(filament::Dithering::NONE)"),
            std::string::npos);
  EXPECT_NE(viewport_renderer.find("overlay_view_->setColorGrading(nullptr)"),
            std::string::npos);
  EXPECT_NE(viewport_renderer.find("overlay_view_->setShadowingEnabled(false)"),
            std::string::npos);
  EXPECT_NE(viewport_renderer.find("overlay_view_->setBlendMode(filament::View:"
                                   ":BlendMode::TRANSLUCENT)"),
            std::string::npos);
  EXPECT_NE(viewport_panel.find("renderer.set_scene_layer_mask"),
            std::string::npos);
  EXPECT_NE(viewport_panel.find("kViewportRenderedSceneLayerMask"),
            std::string::npos);
  EXPECT_NE(viewport_panel.find("kViewportShadedSceneLayerMask"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      viewport_panel,
      "renderer.set_shadowing_enabled("
      "viewport_shading_uses_scene_lighting(shading))"));
  EXPECT_TRUE(source_contains_compact(
      viewport_panel,
      "const ViewportShadingMode shading = "
      "workspace_->shading_mode(pane_state.id)"));
}

TEST(ViewportRendererTests, OverlayPassUsesNeutralExposureCamera) {
  const std::string header =
      read_source_file({"src", "render", "viewport_renderer.hpp"});
  const std::string source =
      read_source_file({"src", "render", "viewport_renderer.cpp"});

  EXPECT_NE(header.find("filament::Camera *overlay_camera_ = nullptr"),
            std::string::npos);
  EXPECT_NE(header.find("utils::Entity overlay_camera_entity_"),
            std::string::npos);
  EXPECT_NE(
      source.find(
          "overlay_camera_ = engine_->createCamera(overlay_camera_entity_)"),
      std::string::npos);
  EXPECT_NE(source.find("overlay_view_->setCamera(overlay_camera_)"),
            std::string::npos);
  EXPECT_EQ(source.find("overlay_view_->setCamera(camera_)"),
            std::string::npos);
  EXPECT_NE(source.find("apply_look_at(overlay_camera_)"), std::string::npos);
  EXPECT_NE(source.find("apply_projection(overlay_camera_)"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      source, "camera_->setExposure(kLightPreviewAperture, "
              "kLightPreviewShutterSeconds, kLightPreviewSensitivity)"));
  EXPECT_EQ(source.find("settings.camera.use_exposure"), std::string::npos);
  EXPECT_EQ(source.find("settings.camera.aperture"), std::string::npos);
  EXPECT_NE(
      source.find("apply_neutral_overlay_exposure(filament::Camera *camera)"),
      std::string::npos);
  EXPECT_NE(source.find("kViewportOverlayCameraAperture"), std::string::npos);
  EXPECT_NE(source.find("kViewportOverlayCameraShutterSpeed"),
            std::string::npos);
  EXPECT_NE(source.find("kViewportOverlayCameraSensitivity"),
            std::string::npos);
  EXPECT_EQ(count_occurrences(source, "overlay_camera_->setExposure("), 0u);
  EXPECT_GE(count_occurrences(
                source, "apply_neutral_overlay_exposure(overlay_camera_)"),
            3u);
}

TEST(ViewportRendererTests,
     RetainedPresentationStateSkipsUnchangedFilamentSetters) {
  const std::string renderer_header =
      read_source_file("src/render/viewport_renderer.hpp");
  const std::string renderer_source =
      read_source_file("src/render/viewport_renderer.cpp");
  const std::string grid_header =
      read_source_file("src/render/viewport_grid_renderer.hpp");
  const std::string grid_source =
      read_source_file("src/render/viewport_grid_renderer.cpp");

  EXPECT_NE(renderer_header.find("camera_presentation_signature_"),
            std::string::npos);
  EXPECT_NE(renderer_header.find("scene_settings_presentation_signature_"),
            std::string::npos);
  EXPECT_NE(renderer_header.find("shadowing_enabled_"), std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      renderer_source,
      "if (camera_presentation_signature_ == presentation_signature) { "
      "return; } const auto pose = controller.pose();"));
  EXPECT_TRUE(source_contains_compact(
      renderer_source,
      "if (scene_settings_presentation_signature_ == presentation_signature) "
      "{ return; } const bool post_processing_enabled"));
  EXPECT_TRUE(source_contains_compact(
      renderer_source,
      "if (render_pass_coordinator_.set_grid_visible(visible)) { "
      "update_visible_layers(); }"));
  EXPECT_TRUE(source_contains_compact(
      renderer_source,
      "if (shadowing_enabled_ == enabled) { return; }"));
  EXPECT_GE(count_occurrences(renderer_source,
                              "reset_presentation_cache();"),
            2u);

  EXPECT_NE(grid_header.find("presentation_signature_"), std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      grid_source,
      "if (presentation_signature_ == presentation_signature) { return; } "
      "const ViewportCameraPose pose = controller.pose();"));
  EXPECT_GE(count_occurrences(grid_source, "presentation_signature_.reset();"),
            4u);
  const std::size_t unchanged_gate =
      grid_source.find("if (presentation_signature_ ==");
  ASSERT_NE(unchanged_gate, std::string::npos);
  const std::size_t transform_setter =
      grid_source.find("setTransform(", unchanged_gate);
  const std::size_t material_setter =
      grid_source.find("setParameter(", unchanged_gate);
  ASSERT_NE(transform_setter, std::string::npos);
  ASSERT_NE(material_setter, std::string::npos);
  EXPECT_LT(unchanged_gate, transform_setter);
  EXPECT_LT(unchanged_gate, material_setter);
}

TEST(RenderLifecycleTests, ResourceOwnersShutdownBeforeInitialize) {
  for (const char *relative_path : {
           "src/render/scene_render_bridge.cpp",
           "src/render/texture_binding_service.cpp",
           "src/render/viewport_grid_renderer.cpp",
       }) {
    const std::string source = read_source_file(relative_path);
    const std::size_t initialize = source.find("::initialize");
    ASSERT_NE(initialize, std::string::npos) << relative_path;
    const std::size_t shutdown = source.find("shutdown();", initialize);
    ASSERT_NE(shutdown, std::string::npos) << relative_path;
    EXPECT_LT(shutdown, source.find("engine_ =", initialize)) << relative_path;
  }
}

TEST(TextureRegistryTests, TracksPerTexturePresentationColorSpace) {
  const std::string header_source =
      read_source_file("src/render/texture_registry.hpp");

  EXPECT_NE(header_source.find("enum class TextureSampleColorSpace"),
            std::string::npos);
  EXPECT_NE(header_source.find("DisplaySrgb"), std::string::npos);
  EXPECT_NE(header_source.find("DisplaySrgbPremultipliedAlpha"),
            std::string::npos);
  EXPECT_NE(header_source.find("LinearSrgb"), std::string::npos);
  EXPECT_NE(header_source.find("using RenderTextureHandle = std::uintptr_t"),
            std::string::npos);
  EXPECT_NE(
      header_source.find("TextureSampleColorSpace "
                         "sample_color_space(RenderTextureHandle id) const"),
      std::string::npos);
  EXPECT_NE(header_source.find("void update_texture(RenderTextureHandle id"),
            std::string::npos);

  const std::string source =
      read_source_file("src/render/texture_registry.cpp");

  EXPECT_NE(source.find(".sample_color_space = sample_color_space"),
            std::string::npos);
  EXPECT_NE(source.find("TextureSampleColorSpace::DisplaySrgb"),
            std::string::npos);
  EXPECT_NE(source.find("it->second.sample_color_space"), std::string::npos);
}

TEST(ViewportRendererTests,
     PresentsSceneAndOverlayThroughNativeQtChildSwapchain) {
  const std::string source =
      read_source_file("src/render/viewport_render_target.cpp");
  const std::string renderer_header =
      read_source_file("src/render/viewport_renderer.hpp");
  const std::string renderer_source =
      read_source_file("src/render/viewport_renderer.cpp");
  const std::string coordinator_source =
      read_source_file("src/render/viewport_render_pass_coordinator.cpp");
  const std::string viewport_panel_source =
      read_source_file("src/ui/qt_viewport_panel.cpp");
  const std::string viewport_surface_source =
      read_source_file("src/ui/qt_viewport_surface.cpp");
  const std::string grid_source =
      read_source_file("src/render/viewport_grid_renderer.cpp");

  EXPECT_NE(source.find(".format(Texture::InternalFormat::RGBA8)"),
            std::string::npos);
  EXPECT_NE(source.find("kViewportRenderTargetCapacityStep = 128u"),
            std::string::npos);
  EXPECT_NE(source.find("viewport_render_target_capacity_for_size"),
            std::string::npos);
  EXPECT_NE(source.find(
                "view->setViewport({0, 0, viewport_width_, viewport_height_})"),
            std::string::npos);
  EXPECT_NE(source.find("void ViewportRenderTarget::set_sample_color_space"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      source,
      "texture_registry_->update_texture(texture_handle_, color_texture_, "
      "sample_color_space_)"));
  EXPECT_TRUE(source_contains_compact(
      source,
      "texture_registry_->register_texture(creation_guard.color_texture, "
      "sample_color_space_)"));
  EXPECT_NE(renderer_header.find(
                "RenderTextureHandle overlay_texture_handle() const"),
            std::string::npos);
  EXPECT_NE(renderer_header.find("ViewportRenderTarget overlay_render_target_"),
            std::string::npos);
  EXPECT_NE(renderer_header.find("float texture_max_u() const"),
            std::string::npos);
  EXPECT_NE(renderer_header.find("float texture_max_v() const"),
            std::string::npos);
  EXPECT_NE(renderer_header.find("scene_texture_sample_color_space_"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      renderer_source, "render_target_.resize(width_, height_, view_, "
                       "scene_texture_sample_color_space_)"));
  EXPECT_TRUE(source_contains_compact(
      renderer_source,
      "return post_processing_enabled ? TextureSampleColorSpace::DisplaySrgb : "
      "TextureSampleColorSpace::LinearSrgb"));
  EXPECT_TRUE(source_contains_compact(renderer_source,
                                      "render_target_.set_sample_color_space("
                                      "scene_texture_sample_color_space_)"));
  EXPECT_TRUE(source_contains_compact(
      renderer_source,
      "static_cast<float>(width_) / static_cast<float>(std::max(1u, "
      "render_target_.width()))"));
  EXPECT_NE(renderer_source.find(
                "TextureSampleColorSpace::DisplaySrgbPremultipliedAlpha"),
            std::string::npos);
  EXPECT_NE(coordinator_source.find(
                "overlay_clear.clear = overlay_uses_separate_target"),
            std::string::npos);
  EXPECT_NE(coordinator_source.find(
                "overlay_clear.clearColor = {0.0f, 0.0f, 0.0f, 0.0f}"),
            std::string::npos);
  EXPECT_NE(coordinator_source.find("overlay_visible_layer_mask() == 0u && "
                                    "!overlay_uses_separate_target"),
            std::string::npos);
  EXPECT_NE(viewport_surface_source.find(
                "render_system_->begin_frame(swap_chain_)"),
            std::string::npos);
  EXPECT_NE(viewport_surface_source.find(
                "render_system_->render_viewport(renderer_, swap_chain_)"),
            std::string::npos);
  EXPECT_NE(viewport_surface_source.find(
                "render_system_->end_frame(swap_chain_)"),
            std::string::npos);
  const std::size_t aggregate_begin = viewport_panel_source.find(
      "render_system_->begin_presentation_frame()");
  const std::size_t pane_submission = viewport_panel_source.find(
      "pane_state.surface->render_frame()", aggregate_begin);
  const std::size_t aggregate_complete = viewport_panel_source.find(
      "render_system_->complete_presentation_frame(submitted)",
      pane_submission);
  ASSERT_NE(aggregate_begin, std::string::npos);
  ASSERT_NE(pane_submission, std::string::npos);
  ASSERT_NE(aggregate_complete, std::string::npos);
  EXPECT_LT(aggregate_begin, pane_submission);
  EXPECT_LT(pane_submission, aggregate_complete);
  EXPECT_EQ(viewport_panel_source.find("overlay_texture_handle()"),
            std::string::npos);
  EXPECT_EQ(viewport_panel_source.find("texture_max_u()"), std::string::npos);
  EXPECT_EQ(viewport_panel_source.find("texture_max_v()"), std::string::npos);
  EXPECT_EQ(viewport_panel_source.find("ImGui::"), std::string::npos);
  EXPECT_NE(
      grid_source.find("settings.color(ViewportColorSettingId::GridMinor)"),
      std::string::npos);
  EXPECT_NE(
      grid_source.find(
          "settings.float_value(ViewportFloatSettingId::GridMinorLineSize)"),
      std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      grid_source, "setDepthCulling(controller.state().projection != "
                   "ViewportProjection::Orthographic)"));
  EXPECT_EQ(source.find(".format(Texture::InternalFormat::SRGB8_A8)"),
            std::string::npos);
}

TEST(ViewportRendererTests,
     ViewportInfoOverlayUsesRendererTimingAndVisibleMeshSubmissionStats) {
  const std::string app_source = read_source_file("src/app/app.cpp");
  const std::string ui_system_header = read_source_file("src/ui/ui_system.hpp");
  const std::string ui_system_source = read_source_file("src/ui/ui_system.cpp");
  const std::string viewport_panel_header =
      read_source_file("src/ui/qt_viewport_panel.hpp");
  const std::string viewport_panel_source =
      read_source_file("src/ui/qt_viewport_panel.cpp");
  const std::string viewport_info_stats_header =
      read_source_file("src/render/viewport_info_stats.hpp");
  const std::string viewport_info_stats_source =
      read_source_file("src/render/viewport_info_stats.cpp");

  const std::size_t viewport_info_begin = viewport_panel_source.find(
      "void QtViewportPanel::draw_viewport_info_overlay");
  const std::size_t viewport_info_end = viewport_panel_source.find(
      "void QtViewportPanel::update_split_handles", viewport_info_begin);
  ASSERT_NE(viewport_info_begin, std::string::npos);
  ASSERT_NE(viewport_info_end, std::string::npos);
  const std::string viewport_info_source = viewport_panel_source.substr(
      viewport_info_begin, viewport_info_end - viewport_info_begin);

  EXPECT_NE(app_source.find("ui_.refresh(frame_timing, render_system_."
                            "frame_performance_stats())"),
            std::string::npos);
  EXPECT_NE(ui_system_header.find(
                "RenderFramePerformanceStats render_performance"),
            std::string::npos);
  EXPECT_NE(ui_system_source.find(
                "viewport_panel_->set_render_performance_stats("),
            std::string::npos);
  EXPECT_NE(viewport_panel_header.find("set_render_performance_stats"),
            std::string::npos);
  EXPECT_NE(viewport_panel_header.find("draw_viewport_info_overlay"),
            std::string::npos);
  EXPECT_NE(viewport_panel_header.find(
                "viewport_info_cached_visibility_revision_"),
            std::string::npos);
  EXPECT_NE(viewport_panel_header.find(
                "viewport_info_cached_transform_revision_"),
            std::string::npos);
  EXPECT_NE(viewport_panel_header.find(
                "viewport_info_cached_mesh_binding_revision_"),
            std::string::npos);
  EXPECT_NE(viewport_panel_header.find(
                "viewport_info_cached_render_component_revision_"),
            std::string::npos);
  EXPECT_NE(viewport_panel_header.find("viewport_info_cached_asset_revision_"),
            std::string::npos);
  EXPECT_NE(viewport_panel_header.find(
                "viewport_info_cached_local_visibility_revision_"),
            std::string::npos);
  EXPECT_NE(viewport_panel_header.find(
                "viewport_info_cached_scene_snapshot_"),
            std::string::npos);
  EXPECT_NE(viewport_panel_header.find("viewport_info_cached_pane_id_"),
            std::string::npos);
  EXPECT_NE(viewport_panel_header.find("viewport_info_cached_camera_"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find("same_viewport_info_camera"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      viewport_panel_source,
      "build_viewport_info_scene_snapshot(document,assets,"
      "&local_visibility)"));
  EXPECT_TRUE(source_contains_compact(
      viewport_panel_source,
      "collect_viewport_info_scene_stats("
      "viewport_info_cached_scene_snapshot_,camera)"));
  EXPECT_NE(viewport_info_source.find("kViewportInfoSceneStatsInterval"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      viewport_info_source,
      "const bool pane_cache_hit="
      "viewport_info_cached_pane_id_==pane_state.id"));
  EXPECT_TRUE(source_contains_compact(
      viewport_info_source,
      "const bool camera_cache_hit=pane_cache_hit&&"
      "same_viewport_info_camera(viewport_info_cached_camera_,camera)"));
  EXPECT_TRUE(source_contains_compact(
      viewport_info_source,
      "const bool recent_stats_sample=pane_cache_hit&&"));
  EXPECT_NE(viewport_info_source.find("kViewportInfoTextInterval"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      viewport_info_source,
      "if (text_changed || overlay_was_hidden) { "
      "update_pane_overlay_geometry(pane_state); }"));
  EXPECT_EQ(count_occurrences(viewport_info_source,
                              "update_pane_overlay_geometry("), 1u)
      << "native overlay geometry may refresh only after text or visibility changes";
  EXPECT_TRUE(source_contains_compact(
      viewport_info_stats_header,
      "ViewportInfoSceneSnapshot build_viewport_info_scene_snapshot("
      "const SceneDocument&document,const AssetRegistry&assets,"
      "const ViewportLocalVisibilitySnapshot*local_visibility=nullptr)"));
  EXPECT_NE(viewport_info_stats_source.find(
                "document.mesh_occurrence_projection()"),
            std::string::npos);
  EXPECT_EQ(viewport_info_stats_source.find(
                "document.is_effectively_visible(entity_id)"),
            std::string::npos);
  EXPECT_NE(viewport_info_stats_source.find(
                "coalesced_mesh_primitive_spans(mesh)"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      viewport_info_source,
      "const bool structural_snapshot_cache_hit="
      "viewport_info_scene_snapshot_valid_&&"
      "viewport_info_cached_document_==&document&&"
      "viewport_info_cached_assets_==&assets&&"
      "viewport_info_cached_visibility_revision_==visibility_revision&&"
      "viewport_info_cached_mesh_binding_revision_==mesh_binding_revision&&"
      "viewport_info_cached_render_component_revision_=="
      "render_component_revision&&"
      "viewport_info_cached_layer_revision_==layer_revision&&"
      "viewport_info_cached_asset_revision_==asset_revision&&"
      "viewport_info_cached_local_visibility_revision_=="
      "local_visibility_revision"));
  EXPECT_TRUE(source_contains_compact(
      viewport_info_source,
      "bool snapshot_cache_hit=structural_snapshot_cache_hit&&"
      "viewport_info_cached_transform_revision_==transform_revision"));
  EXPECT_TRUE(source_contains_compact(
      viewport_info_source,
      "if(structural_snapshot_cache_hit&&"
      "viewport_info_cached_transform_revision_!=transform_revision){"
      "QDR_PROFILE_SCOPE(\"Viewport info refresh transform bounds\");"
      "if(try_refresh_viewport_info_scene_snapshot_transforms("));
  EXPECT_TRUE(source_contains_compact(
      viewport_info_source,
      "if(!snapshot_cache_hit){"
      "QDR_PROFILE_SCOPE(\"Viewport info rebuild scene snapshot\")"));
  EXPECT_TRUE(source_contains_compact(
      viewport_info_source,
      "const bool stats_cache_hit=viewport_info_scene_stats_valid_&&"
      "(camera_cache_hit||recent_stats_sample)"));
  EXPECT_NE(viewport_info_source.find(
                "QDR_PROFILE_SCOPE(\"Viewport info cull scene snapshot\")"),
            std::string::npos);
  EXPECT_EQ(viewport_info_source.find("document.revision()"),
            std::string::npos);
  EXPECT_NE(viewport_info_source.find("document.visibility_revision()"),
            std::string::npos);
  EXPECT_NE(viewport_info_source.find("asset_revision = assets.revision()"),
            std::string::npos);
  EXPECT_NE(viewport_info_stats_source.find("viewport_info_bounds_visible"),
            std::string::npos);
  EXPECT_NE(viewport_info_stats_source.find(
                "stats.draw_calls += renderable.draw_calls"),
            std::string::npos);
  EXPECT_NE(viewport_info_stats_source.find(
                "stats.primitives += span.index_count / 3u"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find("ViewportBoolSettingId::ShowInfo"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find("pane_state.id == focused_pane"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find("ui.viewport.info.draw_calls"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find("ui.viewport.info.vertices"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find("ui.viewport.info.edges"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find("ui.viewport.info.faces"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find("ui.viewport.info.triangles"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find(
                "render_performance_.frames_per_second"),
            std::string::npos);
  EXPECT_EQ(viewport_panel_source.find(
                "1.0 / ui_context.viewport->frame_timing.raw_delta_seconds"),
            std::string::npos);
  EXPECT_EQ(viewport_panel_source.find(
                "1000.0 / render_performance_.gpu_frame_ms"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find("new QLabel(pane_state.frame)"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find("viewportInfo_%1"), std::string::npos);
  EXPECT_NE(viewport_panel_source.find("Qt::WA_NativeWindow"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find("Qt::WA_TransparentForMouseEvents"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find("info_overlay->setText(text)"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find("info_overlay->show()"),
            std::string::npos);
  EXPECT_EQ(viewport_panel_source.find("setStyleSheet("), std::string::npos);
  EXPECT_EQ(viewport_panel_source.find("setPalette("), std::string::npos);
  const std::size_t control_font_begin = viewport_panel_source.find(
      "void QtViewportPanel::scale_pane_controls(");
  const std::size_t control_font_end = viewport_panel_source.find(
      "void QtViewportPanel::sync_pane_control_selection(", control_font_begin);
  ASSERT_NE(control_font_begin, std::string::npos);
  ASSERT_NE(control_font_end, std::string::npos);
  std::string outside_control_font =
      viewport_panel_source.substr(0, control_font_begin) +
      viewport_panel_source.substr(control_font_end);
  // The retained camera popup restores the normal application font on scaling.
  constexpr std::string_view kPopupFontReset = "setFont(QApplication::font());";
  const std::size_t popup_font_reset_position =
      outside_control_font.find(kPopupFontReset);
  ASSERT_NE(popup_font_reset_position, std::string::npos);
  outside_control_font.erase(popup_font_reset_position, kPopupFontReset.size());
  EXPECT_EQ(outside_control_font.find("setFont("), std::string::npos)
      << "only the compact strip controls may customize the application font";
  EXPECT_EQ(viewport_panel_source.find("ImGui::"), std::string::npos);
}

TEST(ViewportRendererTests,
     MultiViewportRenderDoesNotMutateSceneInsideFilamentFrame) {
  const std::string renderer_source =
      read_source_file("src/render/viewport_renderer.cpp");
  const std::string grid_renderer_source =
      read_source_file("src/render/viewport_grid_renderer.cpp");
  const std::string coordinator_source =
      read_source_file("src/render/viewport_render_pass_coordinator.cpp");
  const std::string app_source = read_source_file("src/app/app.cpp");
  const std::string ui_system_source = read_source_file("src/ui/ui_system.cpp");
  const std::string viewport_panel_source =
      read_source_file("src/ui/qt_viewport_panel.cpp");
  const std::string overlay_cache_source =
      read_source_file("src/render/overlay/overlay_renderable_cache.cpp");
  const std::string overlay_proxy_factory_source =
      read_source_file("src/render/overlay/overlay_proxy_factory.cpp");
  const std::string scene_bridge_source =
      read_source_file("src/render/scene_render_bridge.cpp");
  const std::string mesh_cache_source =
      read_source_file("src/render/mesh_renderable_cache.cpp");
  const std::string mesh_factory_source =
      read_source_file("src/render/mesh_renderable_factory.cpp");

  const std::size_t render_start =
      renderer_source.find("void ViewportRenderer::render");
  const std::size_t render_end =
      renderer_source.find("void ViewportRenderer::update_visible_layers");
  ASSERT_NE(render_start, std::string::npos);
  ASSERT_NE(render_end, std::string::npos);
  const std::string render_function =
      renderer_source.substr(render_start, render_end - render_start);
  EXPECT_EQ(render_function.find("scene_->addEntity"), std::string::npos);
  EXPECT_EQ(render_function.find("scene_->remove"), std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      renderer_source, "grid_renderer_.update(controller, width(), height(), "
                       "grid_world_spacing, settings, visible, "
                       "instance_edit_context_active_,"));
  EXPECT_TRUE(source_contains_compact(
      grid_renderer_source,
      ".layerMask(kViewportAllLayerMask, grid_layer_mask_)"));
  EXPECT_NE(renderer_source.find("render_pass_coordinator_.apply_visible_"
                                 "layers(view_, overlay_view_)"),
            std::string::npos);
  EXPECT_EQ(renderer_source.find("setChannelDepthClearEnabled("),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      renderer_source,
      "render_pass_coordinator_.render(renderer, view_, overlay_view_, "
      "surface_ == ViewportRenderSurface::Texture)"));
  EXPECT_TRUE(source_contains_compact(
      coordinator_source, "scene_view->setVisibleLayers(kViewportAllLayerMask, "
                          "scene_visible_layer_mask())"));
  EXPECT_TRUE(source_contains_compact(
      coordinator_source,
      "overlay_view->setVisibleLayers(kViewportAllLayerMask, "
      "overlay_visible_layer_mask())"));
  EXPECT_NE(coordinator_source.find("layer_mask | kViewportOverlayLayerMask"),
            std::string::npos);
  EXPECT_NE(coordinator_source.find("clear.discard = false"),
            std::string::npos);
  EXPECT_NE(coordinator_source.find(
                "overlay_clear.clear = overlay_uses_separate_target"),
            std::string::npos);
  EXPECT_NE(coordinator_source.find("renderer.setClearOptions(clear);"),
            std::string::npos);

  const auto app_frame_start = app_source.find("bool App::render_frame(");
  const auto app_frame_end = app_source.find("void App::request_viewport_presentation(", app_frame_start);
  ASSERT_NE(app_frame_start, std::string::npos);
  ASSERT_NE(app_frame_end, std::string::npos);
  const auto app_frame = app_source.substr(app_frame_start, app_frame_end - app_frame_start);
  const std::size_t process_input =
      app_frame.find("ui_.process_viewport_input()");
  const std::size_t sync_scene =
      app_frame.find("QuaderRenderApiRuntimeAccess::sync_scene(");
  const std::size_t prepare_viewports =
      app_frame.find("ui_.prepare_viewports(render_bridge_, "
                      "render_overlay_snapshot_cache_)");
  const std::size_t render_viewports =
      app_frame.find("ui_.render_surfaces(render_scene_viewports)");
  ASSERT_NE(process_input, std::string::npos);
  ASSERT_NE(sync_scene, std::string::npos);
  ASSERT_NE(prepare_viewports, std::string::npos);
  ASSERT_NE(render_viewports, std::string::npos);
  EXPECT_LT(process_input, sync_scene);
  EXPECT_LT(sync_scene, prepare_viewports);
  EXPECT_LT(prepare_viewports, render_viewports);

  EXPECT_NE(ui_system_source.find("void UISystem::process_viewport_input("),
            std::string::npos);
  EXPECT_NE(ui_system_source.find("void UISystem::prepare_viewports("),
            std::string::npos);
  EXPECT_NE(ui_system_source.find("viewport_panel_->prepare_viewports("),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find("workspace_->active_pane_placements()"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find("const SceneDocument &document"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find("const AssetRegistry &assets"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find(
                "renderer.update_camera(pane_workspace.camera)"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      viewport_panel_source, "pane_workspace.camera.set_clip_planes( "
                             "scene_settings.camera.near_plane, "
                             "scene_settings.camera.far_plane); "
                             "renderer.apply_scene_settings("));
  EXPECT_NE(
      viewport_panel_source.find("renderer.set_scene_layer_mask"),
      std::string::npos);
  EXPECT_NE(
      viewport_panel_source.find("renderer.set_shadowing_enabled"),
      std::string::npos);
  EXPECT_NE(viewport_panel_source.find("renderer.set_scene_visible"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find("ViewportBoolSettingId::ShowGrid"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find("ViewportBoolSettingId::ShowGizmos"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find("ViewportBoolSettingId::ShowOverlays"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find("ViewportBoolSettingId::ShowInfo"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      viewport_panel_source,
      "renderer.update_ground_grid(pane_workspace.camera, "
      "grid_world_size, pane_settings, grid_visible)"));
  EXPECT_TRUE(source_contains_compact(
      viewport_panel_source,
      "render_bridge.update_mesh_surface_grid(grid_world_size, mesh_grid_settings)"));
  EXPECT_TRUE(source_contains_compact(
      viewport_panel_source,
      "mesh_grid_settings.set_bool_value(ViewportBoolSettingId::ShowMeshGrid, "
      "std::any_of(active_placements.begin(), active_placements.end(), "
      "[&](const auto &placement) { return "
      "workspace_->effective_settings(placement.pane_id).bool_value("
      "ViewportBoolSettingId::ShowMeshGrid); }))"))
      << "shared material preparation must retain the grid needed by any pane";
  EXPECT_TRUE(source_contains_compact(
      viewport_panel_source,
      "renderer.set_material_presentation(viewport_material_debug_mode(shading), "
      "shading == ViewportShadingMode::Solid, "
      "pane_settings.bool_value(ViewportBoolSettingId::ShowMeshGrid))"))
      << "each view independently gates the shared mesh-grid material";
  EXPECT_TRUE(source_contains_compact(
      scene_bridge_source,
      "mesh_cache_.update_surface_grid(grid_world_spacing, settings)"));
  EXPECT_TRUE(source_contains_compact(
      mesh_cache_source,
      "update_surface_grid_proxy_map(*factory_, rendered_mesh_proxies_, "
      "grid_world_spacing, settings)"));
  EXPECT_TRUE(source_contains_compact(
      mesh_cache_source,
      "update_surface_grid_proxy_map(*factory_, shaded_mesh_proxies_, "
      "grid_world_spacing, settings)"));
  EXPECT_EQ(mesh_cache_source.find("update_surface_grid_proxy_map(*factory_, "
                                   "overlay_depth_mesh_proxies_"),
            std::string::npos);
  EXPECT_NE(
      mesh_factory_source.find("material_binding_->apply_mesh_surface_grid("),
      std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      viewport_panel_source,
      "renderer.set_overlay_visible(should_enable_viewport_overlay_pass({"));
  EXPECT_TRUE(source_contains_compact(
      viewport_panel_source,
      "const bool tool_overlay_visible = active_box_preview_.ok && "
      "active_box_preview_.footprint_count >= 2U && "
      "active_box_preview_.drag_state == ModelingBoxDragState::Footprint"))
      << "the shared primitive footprint must remain visible in every pane";
  EXPECT_NE(viewport_panel_source.find(
                ".tool_overlay_visible = tool_overlay_visible"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find(
                "viewport_shading_shows_light_gizmos(shading)"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      viewport_panel_source,
      "const bool point_light_gizmos_visible = gizmos_visible && "
      "viewport_shading_shows_light_gizmos(shading)"));
  EXPECT_TRUE(source_contains_compact(
      viewport_panel_source,
      "scene_overlay_requests[viewport_pane_index(placement.pane_id)] = "
      "overlays_visible || point_light_gizmos_visible || "
      "assets.curve_count() != 0u"));
  EXPECT_TRUE(source_contains_compact(
      viewport_panel_source,
      "const bool scene_overlay_requested ="
      "scene_overlay_requests[viewport_index]"));
  EXPECT_NE(viewport_panel_source.find(".point_light_gizmos_visible = "
                                       "point_light_gizmos_visible"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find(
                "render_bridge.submit_viewport_overlay("),
            std::string::npos);
  EXPECT_EQ(viewport_panel_source.find("render_bridge.set_overlay_frame("),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find(
                "viewport_grid_layer_mask(viewport_pane_index(pane_state.id))"),
            std::string::npos);
  EXPECT_NE(overlay_proxy_factory_source.find(
                ".layerMask(kViewportAllLayerMask, layer_mask)"),
            std::string::npos);
  EXPECT_TRUE(source_contains_compact(
      overlay_cache_source,
      "void OverlayRenderableCache::rebuild_slot(OverlaySlot &slot, "
      "bool rebuild_camera_independent_layers)"));
}

TEST(ViewportOverlayPolicyTests,
     PersistentObjectSelectionCacheSourceOwnsFaceFillsOnly) {
  const std::string cache_header =
      read_source_file("src/render/overlay/selected_object_overlay_cache.hpp");
  const std::string cache_source =
      read_source_file("src/render/overlay/selected_object_overlay_cache.cpp");
  EXPECT_NE(cache_header.find("RenderSourceStamp synced_source_stamp_"),
            std::string::npos);
  for (const std::string_view retired_scalar : {
           "synced_scene_revision_", "synced_asset_revision_",
           "synced_selection_revision_", "synced_visibility_revision_",
           "synced_local_visibility_revision_", "synced_transform_revision_",
           "synced_mesh_binding_revision_", "synced_scene_binding_",
       }) {
    EXPECT_EQ(cache_header.find(retired_scalar), std::string::npos)
        << retired_scalar;
    EXPECT_EQ(cache_source.find(retired_scalar), std::string::npos)
        << retired_scalar;
  }
  EXPECT_NE(cache_source.find(
                "kRenderOverlaySelectedObjectSourceDependencies"),
            std::string::npos);
  EXPECT_NE(cache_source.find("render_source_dependencies_match("),
            std::string::npos);
  EXPECT_NE(cache_source.find("synced_source_stamp_ = snapshot"),
            std::string::npos);
  EXPECT_EQ(cache_source.find("SelectedObjectLineVertex"), std::string::npos);
  EXPECT_EQ(cache_source.find("update_line_proxy"), std::string::npos);
  EXPECT_EQ(cache_source.find("line_proxy_"), std::string::npos);
  EXPECT_EQ(cache_source.find("setParameter(\"nearPlane\""), std::string::npos);
  EXPECT_NE(cache_source.find("destroy_face_batches()"), std::string::npos);
}

TEST(SceneMeshOccurrenceProjectionPolicyTests,
     SceneOwnsCanonicalVisibleMeshPlacementExpansion) {
  const std::string projection_header =
      read_source_file("src/scene/scene_mesh_occurrence_projection.hpp");
  const std::string projection_source =
      read_source_file("src/scene/scene_mesh_occurrence_projection.cpp");
  const std::string document_header =
      read_source_file("src/scene/scene_document.hpp");
  const std::string document_source =
      read_source_file("src/scene/scene_document.cpp");
  const std::string scene_bounds_source =
      read_source_file("src/scene/scene_bounds.cpp");
  const std::array<std::string, 5> occurrence_consumers{
      read_source_file("src/render/scene_render_extraction.cpp"),
      read_source_file("src/render/viewport_info_stats.cpp"),
      read_source_file("src/render/overlay/render_overlay_snapshot.cpp"),
      read_source_file("src/editor/placement/scene_surface_query.cpp"),
      read_source_file("src/editor/projection/picking_cache_projection.cpp"),
  };

  EXPECT_NE(projection_header.find("SceneMeshOccurrenceProjectionRecord"),
            std::string::npos);
  EXPECT_NE(projection_header.find("EntityId display_entity"),
            std::string::npos);
  EXPECT_NE(projection_header.find("EntityId geometry_entity"),
            std::string::npos);
  EXPECT_NE(projection_header.find("EntityId definition_entity"),
            std::string::npos);
  EXPECT_NE(projection_header.find("QMat4 world_from_geometry"),
            std::string::npos);
  EXPECT_NE(projection_header.find("bool selectable"), std::string::npos);
  EXPECT_NE(projection_header.find("bool effectively_locked"),
            std::string::npos);
  EXPECT_EQ(projection_header.find("assets/"), std::string::npos);
  EXPECT_EQ(projection_header.find("render/"), std::string::npos);
  EXPECT_EQ(projection_header.find("viewport/"), std::string::npos);
  EXPECT_EQ(projection_source.find("assets/"), std::string::npos);
  EXPECT_EQ(projection_source.find("render/"), std::string::npos);
  EXPECT_EQ(projection_source.find("viewport/"), std::string::npos);
  EXPECT_NE(document_header.find("hierarchy_projection() const"),
            std::string::npos);
  EXPECT_NE(document_header.find("mesh_occurrence_projection() const"),
            std::string::npos);
  EXPECT_NE(document_header.find("SceneSpatialProjectionCacheStats"),
            std::string::npos);
  EXPECT_NE(document_header.find("spatial_projection_restore_epoch_"),
            std::string::npos);
  EXPECT_NE(document_source.find(
                "invalidate_spatial_projection_caches_after_revision_restore()"),
            std::string::npos);
  EXPECT_NE(projection_source.find("document.hierarchy_projection()"),
            std::string::npos);

  const std::array<std::string, 3> hierarchy_consumers{
      read_source_file("src/editor/modeling/picking/curve_picking_cache.cpp"),
      read_source_file("src/editor/viewport/viewport_scene_projection.cpp"),
      read_source_file(
          "src/editor/commands/document/scene_texture_mapping_plan.cpp"),
  };
  for (const std::string &consumer : hierarchy_consumers) {
    EXPECT_NE(consumer.find("hierarchy_projection()"), std::string::npos);
    EXPECT_EQ(consumer.find("hierarchy_projection_snapshot()"),
              std::string::npos);
  }

  for (const std::string &consumer : occurrence_consumers) {
    EXPECT_NE(consumer.find("mesh_occurrence_projection()"),
              std::string::npos);
    EXPECT_EQ(consumer.find("build_scene_mesh_occurrence_projection("),
              std::string::npos);
  }
  for (std::size_t index = 0u; index < 3u; ++index) {
    const std::string &consumer = occurrence_consumers[index];
    EXPECT_EQ(consumer.find("instance_occurrences_for_definition("),
              std::string::npos);
    EXPECT_EQ(consumer.find("scene_hierarchy_segment_is_visible("),
              std::string::npos);
    EXPECT_EQ(consumer.find("try_scene_matrix_inverse("), std::string::npos);
    EXPECT_EQ(consumer.find("containing_instance_definition("),
              std::string::npos);
  }

  EXPECT_NE(scene_bounds_source.find("records_for_display(occurrence)"),
            std::string::npos);
  EXPECT_NE(scene_bounds_source.find("document.mesh_occurrence_projection()"),
            std::string::npos);
  EXPECT_NE(scene_bounds_source.find("entity_world_bounds_with_projection("),
            std::string::npos);
  EXPECT_NE(
      scene_bounds_source.find("entity_hierarchy_world_bounds_with_projection("),
      std::string::npos);
  EXPECT_EQ(scene_bounds_source.find("instance_occurrences_for_definition("),
            std::string::npos);
  EXPECT_EQ(scene_bounds_source.find("scene_hierarchy_segment_is_visible("),
            std::string::npos);
  EXPECT_EQ(scene_bounds_source.find("try_scene_matrix_inverse("),
            std::string::npos);
  EXPECT_EQ(scene_bounds_source.find("containing_instance_definition("),
            std::string::npos);
  EXPECT_EQ(scene_bounds_source.find("build_scene_mesh_occurrence_projection("),
            std::string::npos);
}

TEST(ViewportOverlayPolicyTests,
     RuntimeSelectionOverlaySourceUsesSemanticRoleTokens) {
  const std::string runtime_header = read_source_file(
      "src/editor/modeling/runtime/selection_types.hpp");
  const std::string projection = read_source_file(
      "src/editor/projection/selection_overlay_projection.cpp");
  EXPECT_NE(runtime_header.find("SelectionOverlayStyleRole"),
            std::string::npos);
  EXPECT_NE(runtime_header.find("selection_overlay_style_token("),
            std::string::npos);
  EXPECT_EQ(runtime_header.find("0xebffe05cU"), std::string::npos);
  EXPECT_EQ(runtime_header.find("0xff1fadffU"), std::string::npos);
  EXPECT_EQ(runtime_header.find("0xffffe66bU"), std::string::npos);
  EXPECT_NE(projection.find("selection_overlay_style_role_from_token("),
            std::string::npos);
  EXPECT_EQ(
      projection.find("viewport_overlay_is_native_remove_color(color_abgr)"),
      std::string::npos);
  EXPECT_EQ(projection.find(
                "viewport_overlay_is_native_selection_wire_color(color_abgr)"),
            std::string::npos);
}

TEST(RenderProxyLifecycleTests,
     RenderableProxySourcesEnforceMoveOnlyResourceTransfer) {
  const std::string mesh_proxy_header =
      read_source_file("src/render/mesh_renderable_proxy.hpp");
  const std::string mesh_factory_source =
      read_source_file("src/render/mesh_renderable_factory.cpp");
  const std::string overlay_proxy_header =
      read_source_file("src/render/overlay/overlay_renderable_proxy.hpp");
  const std::string overlay_factory_source =
      read_source_file("src/render/overlay/overlay_proxy_factory.cpp");
  const std::string light_cache_header =
      read_source_file("src/render/light_renderable_cache.hpp");
  const std::string light_cache_source =
      read_source_file("src/render/light_renderable_cache.cpp");

  EXPECT_NE(mesh_proxy_header.find(
                "MeshRenderableProxy(const MeshRenderableProxy&) = delete"),
            std::string::npos);
  EXPECT_NE(mesh_proxy_header.find(
                "MeshRenderableProxy(MeshRenderableProxy&& other) noexcept"),
            std::string::npos);
  EXPECT_NE(
      mesh_proxy_header.find("std::exchange(other.vertex_buffer, nullptr)"),
      std::string::npos);
  EXPECT_NE(mesh_factory_source.find("class MeshRenderableCreationGuard"),
            std::string::npos);
  EXPECT_NE(mesh_factory_source.find("destroy_renderable_component_resources"),
            std::string::npos);
  EXPECT_NE(mesh_factory_source.find("release_geometry(proxy)"),
            std::string::npos);
  EXPECT_NE(mesh_factory_source.find("creation_guard.release()"),
            std::string::npos);
  EXPECT_NE(
      overlay_proxy_header.find(
          "OverlayRenderableProxy(const OverlayRenderableProxy&) = delete"),
      std::string::npos);
  EXPECT_NE(
      overlay_proxy_header.find(
          "OverlayRenderableProxy(OverlayRenderableProxy &&other) noexcept"),
      std::string::npos);
  EXPECT_NE(overlay_factory_source.find("class OverlayProxyCreationGuard"),
            std::string::npos);
  EXPECT_NE(overlay_factory_source.find("destroy_overlay_proxy_resources"),
            std::string::npos);
  EXPECT_NE(overlay_factory_source.find("creation_guard.release()"),
            std::string::npos);
  EXPECT_NE(overlay_factory_source.find("proxies.push_back(std::move(proxy))"),
            std::string::npos);
  EXPECT_NE(overlay_factory_source.find("void append_overlay_proxy("),
            std::string::npos);
  // Generic and retained edit-wire builders both attach shared material passes.
  EXPECT_EQ(count_occurrences(overlay_factory_source,
                              "filament::RenderableManager::Builder renderable_builder("),
            2u);
  EXPECT_EQ(
      count_occurrences(overlay_factory_source, "scene.addEntity(entity)"), 1u);
  // Retained face arenas return a guarded proxy directly to the cache owner.
  // All other factory publication still goes through the move-only helper.
  const std::string retained_face_source = source_slice(
      overlay_factory_source,
      "OverlayRenderableProxy OverlayProxyFactory::create_retained_face_proxy(",
      "bool OverlayProxyFactory::update_retained_face_proxy(");
  EXPECT_EQ(count_occurrences(overlay_factory_source,
                              "scene_->addEntity(entity)"),
            1u);
  EXPECT_EQ(count_occurrences(retained_face_source,
                              "scene_->addEntity(entity)"),
            1u);
  const std::size_t retained_guard = retained_face_source.find(
      "OverlayProxyCreationGuard guard(*engine_, *scene_, proxy)");
  const std::size_t retained_allocation = retained_face_source.find(
      "proxy.vertex_buffer = filament::VertexBuffer::Builder()");
  ASSERT_NE(retained_guard, std::string::npos);
  ASSERT_NE(retained_allocation, std::string::npos);
  EXPECT_LT(retained_guard, retained_allocation);
  EXPECT_TRUE(source_contains_compact(
      retained_face_source,
      "scene_->addEntity(entity); guard.mark_scene_added(); guard.release();"));
  const std::string creation_guard_source = source_slice(
      overlay_factory_source, "class OverlayProxyCreationGuard",
      "void publish_overlay_proxy(");
  EXPECT_TRUE(source_contains_compact(
      creation_guard_source,
      "if (active_) { destroy_overlay_proxy_resources(engine_, &scene_, proxy_, "
      "scene_added_); }"));
  EXPECT_NE(overlay_proxy_header.find(
                "face_fill_materials(std::move(other.face_fill_materials))"),
            std::string::npos);
  EXPECT_EQ(count_occurrences(overlay_factory_source,
                              "proxies.push_back(std::move(proxy))"),
            1u);
  EXPECT_NE(light_cache_header.find("LightProxy(const LightProxy&) = delete"),
            std::string::npos);
  EXPECT_NE(light_cache_header.find("LightProxy(LightProxy &&other) noexcept"),
            std::string::npos);
  EXPECT_NE(light_cache_source.find(
                "light_proxies_.emplace(light.entity_id, std::move(proxy))"),
            std::string::npos);
  EXPECT_NE(light_cache_source.find("LightProxyCreationGuard creation_guard"),
            std::string::npos);
  EXPECT_NE(light_cache_source.find("creation_guard.release()"),
            std::string::npos);
}

TEST(RenderProxyLifecycleTests,
     RenderOwnerSourcesExplicitlyDeleteTransfers) {
  const std::string render_system_header =
      read_source_file("src/render/render_system.hpp");
  const std::string scene_render_bridge_header =
      read_source_file("src/render/scene_render_bridge.hpp");
  const std::string viewport_renderer_header =
      read_source_file("src/render/viewport_renderer.hpp");

  EXPECT_NE(
      render_system_header.find("RenderSystem(const RenderSystem&) = delete"),
      std::string::npos);
  EXPECT_NE(scene_render_bridge_header.find(
                "SceneRenderBridge(SceneRenderBridge&&) = delete"),
            std::string::npos);
  EXPECT_NE(viewport_renderer_header.find(
                "ViewportRenderer(const ViewportRenderer&) = delete"),
            std::string::npos);
}

} // namespace
} // namespace quader
