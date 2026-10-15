#pragma once
#include "assets/ModelAsset.h"
#include "assets/MaterialGraph.h"
#include "resources/ShaderCompiler.h"
#include "ShaderPreflight.h"
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace crimson {
struct MaterialAsset;
enum class MaterialGraphPass { Model, Picking, PointShadow, Particle, DirectionalShadow };
enum class MaterialGraphTextureRole { Color, Data, Normal };
struct MaterialGraphTextureBinding {
    std::uint64_t nodeId{};
    std::uint16_t slot{}, mappingRow{}, settingsRow{};
    AssetRef<DecodedImage> image;
    ModelSampler sampler;
    MaterialGraphTextureRole role{MaterialGraphTextureRole::Color};
};
struct MaterialGraphShaderVariant {
    MaterialGraphPass pass{};
    std::string vertexName, varyingName, source;
    std::vector<ShaderUniformContract> manifest;
};
struct MaterialGraphRuntimeParameter {
    MaterialNodeId node{};
    std::uint16_t row{};
    bool mapping{}, coverage{};
};
struct MaterialGraphShader {
    std::string topologyKey;
    std::vector<Vec4> uniforms;
    std::vector<MaterialGraphTextureBinding> textures;
    std::vector<MaterialGraphShaderVariant> variants;
    std::shared_ptr<const MaterialGraph> runtimeGraph;
    std::vector<MaterialGraphRuntimeParameter> runtimeParameters;
    bool usesNormal{}, usesTextures{}, nodePreview{};
    bool dynamicCoverage{};
    // Authored runtime expressions can differ while their t=0 rows and shader
    // topology are identical. Computed once at lowering, never at submission.
    std::uint64_t runtimeDefinitionSignature{};
};
// Patches a submission copy, never the authored packet or compile identity.
void updateMaterialGraphRuntimeUniforms(const MaterialGraph&,std::span<const MaterialGraphRuntimeParameter>,
    MaterialEvaluationContext,std::span<Vec4>);
// Deterministic CPU lowering. Throws node-specific diagnostics for invalid
// graphs/limits; no shaderc/GPU/filesystem work. Packet edits preserve source.
MaterialGraphShader generateMaterialGraphShader(const MaterialAsset&);
MaterialGraphShader generateMaterialGraphPreviewShader(const MaterialAsset&,MaterialGraphPreviewTarget);
ShaderCompileRecipe materialGraphShaderRecipe(const ShaderCompileConfig&,const MaterialGraphShader&,
    const MaterialGraphShaderVariant&);
inline constexpr std::uint16_t MaterialGraphMaxTextureBindings=12;
inline constexpr std::uint16_t MaterialGraphMaxParameterRows=208;
inline constexpr const char* MaterialGraphParameterUniform="u_graphParams";
// Shaderc strips the first Texture/Sampler substring from HLSL names.
inline std::string materialGraphSamplerName(std::uint16_t slot) {return "s_graphMap"+std::to_string(slot);}
}
