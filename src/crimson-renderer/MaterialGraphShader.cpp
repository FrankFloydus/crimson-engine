#include "MaterialGraphShader.h"
#include "assets/MaterialGraph.h"
#include "assets/MaterialAsset.h"
#include <stdexcept>
namespace crimson {
namespace {
// These literals identify individual compiler inputs, not the renderer family.
// Keep historical tags exact for independent coverage, reflection, transmission,
// Decal and native-domain recipes. Actual changed includes still invalidate
// their consumers; the current renderer-family ABI is separately 40.
constexpr const char* surfaceShaderContract="crimson-material-graph-v13;abi39;decal-domain-v2;pbr-quality-v1;ssr-reflection-v1;surface5-v1;refraction-pair-matte-v3;height-domain-v2;mesh-shading-v1;atomic-images-v1;12-textures;208-vec4;ibl12-13;forward-plus-brdf14;shadow15;point-directional-depth;runtime-uniforms;particle-instance80-orientation;rgb-to-scalar-r;point-record6;near-falloff-specular;finite-point-range-v4;source-radius-ggx-pcss-v2;distance-attenuation-v1;environment-ambient-color-v1";
constexpr const char* lightingShaderContract="crimson-material-graph-v13;abi40;decal-domain-v2;pbr-quality-v1;ssr-reflection-v1;surface5-v1;refraction-pair-matte-v3;height-domain-v2;mesh-shading-v1;atomic-images-v1;12-textures;208-vec4;ibl12-13;forward-plus-brdf14;shadow15;point-directional-depth;runtime-uniforms;particle-instance80-orientation;rgb-to-scalar-r;local-record8;spot-cone-v1;spot-perspective-shadow-v1;near-falloff-specular;finite-point-range-v4;source-radius-ggx-pcss-v2;distance-attenuation-v1;environment-ambient-color-v1";
constexpr const char* decalShaderContract="crimson-material-graph-v13;abi39;decal-domain-v2;pbr-quality-v1;ssr-reflection-v1;surface5-v1;refraction-pair-matte-v3;height-domain-v2;mesh-shading-v1;atomic-images-v1;12-textures;208-vec4;ibl12-13;forward-plus-brdf14;shadow15;point-directional-depth;runtime-uniforms;particle-instance80-orientation;rgb-to-scalar-r;point-record6;near-falloff-specular;finite-point-range-v4;source-radius-ggx-pcss-v2;distance-attenuation-v1;environment-ambient-color-v1";
}
ShaderCompileRecipe materialGraphShaderRecipe(const ShaderCompileConfig& config,const MaterialGraphShader& graph,const MaterialGraphShaderVariant& variant) {
    ShaderCompileRecipe result;
    // Resource jobs and disk entries describe one compiler input, not the entire
    // material bundle. Identical passes share pending work and cached binaries.
    std::uint64_t hash=14695981039346656037ull;
    for(unsigned char ch:variant.source){hash^=ch;hash*=1099511628211ull;}
    result.name=std::string(graph.nodePreview?"graph-preview-stage-":"graph-stage-")+std::to_string(hash);result.source=variant.source;
    result.varyingDefinition=config.sourceDirectory/variant.varyingName;
    result.includeDirectories={config.sourceDirectory,config.includeDirectory};
    result.contract=variant.pass==MaterialGraphPass::Decal?decalShaderContract:
        variant.pass==MaterialGraphPass::Model||variant.pass==MaterialGraphPass::Particle?lightingShaderContract:surfaceShaderContract;
    if(variant.pass==MaterialGraphPass::Model || variant.pass==MaterialGraphPass::Picking || variant.pass==MaterialGraphPass::ModelTransmission || variant.pass==MaterialGraphPass::ModelReflection)result.contract+=";model-instance-opacity-v1";
    return result;
}
ShaderCompileRecipe materialGraphDomainShaderRecipe(const ShaderCompileConfig& config,const MaterialGraphShader&,const MaterialGraphShaderVariant& variant) {
    ShaderCompileRecipe result;
    std::uint64_t hash=14695981039346656037ull;
    for(unsigned char ch:variant.domainSource){hash^=ch;hash*=1099511628211ull;}
    result.name="graph-domain-stage-"+std::to_string(hash);result.source=variant.domainSource;
    result.stage=ShaderStage::Domain;result.raw=true;
    result.varyingDefinition=config.sourceDirectory/variant.varyingName;
    result.contract="crimson-material-domain-v4;abi39;native-sm5;model108;shading-stream1-texcoord5;quad-paint-stream2-v1;12-textures;208-vec4;height01";
    return result;
}
MaterialGraphSourceSet resolveMaterialSources(const MaterialAsset& material,const std::function<std::shared_ptr<const MaterialAsset>(const std::string&)>& load) {
    MaterialGraphSourceSet result;
    for(const auto& id:materialGraphMaterialReferences(material.graph,true)) {
        if(id==material.id)throw std::runtime_error("A Blend material cannot reference itself.");
        auto source=load(id);if(!source)throw std::runtime_error("Blend source material is unavailable: "+id);
        if(!materialBlendSourceCompatible(*source))throw std::runtime_error("Blend layers require ordinary Lit, Opaque materials: "+source->name);
        result.push_back(std::move(source));
    }
    return result;
}
MaterialGraphShader generateMaterialGraphShader(const MaterialAsset& material,const MaterialGraphSourceSet& sources) {return generateMaterialGraphShaderData(material,material_graph_image_policy(),sources);}
MaterialGraphShader generateMaterialGraphPreviewShader(const MaterialAsset& material,MaterialGraphPreviewTarget target,const MaterialGraphSourceSet& sources) {return generateMaterialGraphPreviewShaderData(material,target,material_graph_image_policy(),sources);}
}
