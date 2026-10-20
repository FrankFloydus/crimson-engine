#include "MaterialGraphShader.h"
#include "assets/MaterialGraph.h"
namespace crimson {
ShaderCompileRecipe materialGraphShaderRecipe(const ShaderCompileConfig& config,const MaterialGraphShader& graph,const MaterialGraphShaderVariant& variant) {
    ShaderCompileRecipe result;
    // Resource jobs and disk entries describe one compiler input, not the entire
    // material bundle. Identical passes share pending work and cached binaries.
    std::uint64_t hash=14695981039346656037ull;
    for(unsigned char ch:variant.source){hash^=ch;hash*=1099511628211ull;}
    result.name=std::string(graph.nodePreview?"graph-preview-stage-":"graph-stage-")+std::to_string(hash);result.source=variant.source;
    result.varyingDefinition=config.sourceDirectory/variant.varyingName;
    result.includeDirectories={config.sourceDirectory,config.includeDirectory};
    result.contract="crimson-material-graph-v8;height-domain-v1;12-textures;208-vec4;ibl12-13;forward-plus-brdf14;shadow15;point-directional-depth;runtime-uniforms;particle-instance80-orientation;rgb-to-scalar-r;point-record6;near-falloff-specular;finite-point-range-v4;source-radius-ggx-pcss-v2;distance-attenuation-v1;environment-ambient-color-v1";
    if(variant.pass==MaterialGraphPass::Model || variant.pass==MaterialGraphPass::Picking)result.contract+=";model-instance-opacity-v1";
    return result;
}
ShaderCompileRecipe materialGraphDomainShaderRecipe(const ShaderCompileConfig& config,const MaterialGraphShader&,const MaterialGraphShaderVariant& variant) {
    ShaderCompileRecipe result;
    std::uint64_t hash=14695981039346656037ull;
    for(unsigned char ch:variant.domainSource){hash^=ch;hash*=1099511628211ull;}
    result.name="graph-domain-stage-"+std::to_string(hash);result.source=variant.domainSource;
    result.stage=ShaderStage::Domain;result.raw=true;
    result.varyingDefinition=config.sourceDirectory/variant.varyingName;
    result.contract="crimson-material-domain-v1;native-sm5;model108;12-textures;208-vec4;height01";
    return result;
}
MaterialGraphShader generateMaterialGraphShader(const MaterialAsset& material) {return generateMaterialGraphShaderData(material,material_graph_image_policy());}
MaterialGraphShader generateMaterialGraphPreviewShader(const MaterialAsset& material,MaterialGraphPreviewTarget target) {return generateMaterialGraphPreviewShaderData(material,target,material_graph_image_policy());}
}
