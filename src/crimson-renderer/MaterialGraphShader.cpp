#include "MaterialGraphShader.h"
#include "assets/MaterialAsset.h"
#include <algorithm>
#include <map>
#include <functional>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>

namespace crimson {
namespace {
std::string param(std::size_t row) {return "u_graphParams["+std::to_string(row)+"]";}
std::string valueSuffix(MaterialPin pin) {
    switch(pin) {
    case MaterialPin::RGB:return ".rgb";
    case MaterialPin::XY:return ".xy";
    case MaterialPin::X:return ".x";
    case MaterialPin::Y:return ".y";
    case MaterialPin::R:return ".r";
    case MaterialPin::G:return ".g";
    case MaterialPin::B:return ".b";
    case MaterialPin::A:return ".a";
    default:return {};
    }
}
std::string glslType(MaterialValueType type) {return type==MaterialValueType::Scalar?"float":type==MaterialValueType::Vector2?"vec2":"vec3";}
struct Lowering {
    const MaterialGraph& graph;
    MaterialGraphShader result;
    std::map<MaterialNodeId,std::string> expressions;
    std::map<MaterialNodeId,std::uint16_t> mappings,textureSettings;
    std::ostringstream code;
    std::uint16_t row(const Vec4& value) {
        if(result.uniforms.size()>=MaterialGraphMaxParameterRows)throw std::runtime_error("Material graph exceeds 208 vec4 parameter rows.");
        const auto index=static_cast<std::uint16_t>(result.uniforms.size());result.uniforms.push_back(value);return index;
    }
    std::uint16_t mapping(const MaterialUvMapping& uv) {
        constexpr float radians=3.14159265358979323846f/180.f;
        const auto first=row({uv.scale[0],uv.scale[1],uv.scale[2],uv.mode==MaterialUvMode::LocalTriplanar||uv.mode==MaterialUvMode::WorldTriplanar?1.f:0.f});
        row({uv.offset[0],uv.offset[1],uv.offset[2],uv.mode==MaterialUvMode::WorldTriplanar?1.f:0.f});
        row({uv.rotation[0]*radians,uv.rotation[1]*radians,uv.rotation[2]*radians,uv.mode==MaterialUvMode::UV1?1.f:0.f});
        return first;
    }
    std::string linked(const MaterialLink& link) const {
        const auto found=expressions.find(link.fromNode);
        if(found==expressions.end())throw std::runtime_error("Material graph source order is invalid at node "+std::to_string(link.fromNode)+".");
        return found->second+valueSuffix(link.fromPin);
    }
    std::string linkedAs(const MaterialLink& link,MaterialValueType destination) const {
        const auto source=materialOutputType(graph,link.fromNode,link.fromPin);
        if(!materialValueAssignable(source,destination))throw std::runtime_error("Material graph input conversion is invalid.");
        const auto expression=linked(link);
        // Explicit swizzle keeps CPU first-component evaluation and every
        // generated pass identical, including chained Color expressions.
        return source==destination?expression:"("+expression+").x";
    }
    std::string input(const MaterialNode& node,MaterialPin pin) {
        const auto fallback=materialInputDefault(node,pin);
        if(const auto* link=materialInputLink(graph,node.id,pin))
            return node.kind==MaterialNodeKind::Multiply?linked(*link):linkedAs(*link,fallback.type);
        return param(row(fallback.value))+(fallback.type==MaterialValueType::Scalar?".x":fallback.type==MaterialValueType::Vector2?".xy":".rgb");
    }
    std::string output(const MaterialNode& node,MaterialPin pin,const std::string& fallback,bool active=true) const {
        if(active)if(const auto* link=materialInputLink(graph,node.id,pin))return linkedAs(*link,materialInputDefault(node,pin).type);
        return fallback;
    }

};
void addBinding(std::vector<ShaderUniformContract>& manifest,const std::string& name,ShaderUniformKind kind=ShaderUniformKind::Vec4,std::uint16_t count=1,std::uint16_t slot=0,
    ShaderTextureDimension dimension=ShaderTextureDimension::Texture2D,bool comparison=false) {
    manifest.push_back({name,kind,count,slot,false,dimension,comparison});
}
std::vector<ShaderUniformContract> manifest(const MaterialGraphShader& graph,MaterialGraphPass pass) {
    std::vector<ShaderUniformContract> result;
    addBinding(result,MaterialGraphParameterUniform,ShaderUniformKind::Vec4,MaterialGraphMaxParameterRows);
    for(const auto& texture:graph.textures)addBinding(result,materialGraphSamplerName(texture.slot),ShaderUniformKind::Sampler,1,texture.slot);
    if(pass==MaterialGraphPass::Model || pass==MaterialGraphPass::Particle) {
        for(auto name:{"u_cameraPos","u_lightCounts","u_environment","u_modelOptions","u_modelEmissiveMap","u_modelUv","u_shadowAtlas","u_shadowLayout"})addBinding(result,name);
        addBinding(result,"u_modelNormalMatrix",ShaderUniformKind::Mat4);
        addBinding(result,"u_direction",ShaderUniformKind::Vec4,4);addBinding(result,"u_directionColor",ShaderUniformKind::Vec4,4);
        addBinding(result,"u_pointPosition",ShaderUniformKind::Vec4,16);addBinding(result,"u_pointColor",ShaderUniformKind::Vec4,16);
        addBinding(result,"u_pointShadow",ShaderUniformKind::Vec4,16);
        addBinding(result,"s_irradiance",ShaderUniformKind::Sampler,1,12,ShaderTextureDimension::Cube);
        addBinding(result,"s_prefiltered",ShaderUniformKind::Sampler,1,13,ShaderTextureDimension::Cube);
        addBinding(result,"s_brdfLut",ShaderUniformKind::Sampler,1,14);
        addBinding(result,"s_pointShadow",ShaderUniformKind::Sampler,1,15,ShaderTextureDimension::Texture2D,true);
    }
    if(pass==MaterialGraphPass::Model||pass==MaterialGraphPass::Particle)
        for(auto name:{"u_fogColorDensity","u_fogDepth","u_fogHeight"})addBinding(result,name);
    if(pass==MaterialGraphPass::Picking)addBinding(result,"u_pickId");
    return result;
}
}
static MaterialGraphShader generateGraphShader(const MaterialAsset& material,std::optional<MaterialGraphPreviewTarget> preview) {
    const auto legacy=material.graph.empty()?materialGraphFromLegacy(material.surface,material.shading,material.blend):MaterialGraph{};
    const auto& graph=material.graph.empty()?legacy:material.graph;
    validateMaterialGraph(graph);
    const auto& output=materialOutputNode(graph);const auto& settings=output.output;
    const bool lit=!preview && settings.shading==MaterialShading::StandardPbr,opacityActive=!preview && settings.blend!=MaterialBlend::Opaque;
    const auto opacityPin=settings.blend==MaterialBlend::Mask?MaterialPin::OpacityMask:MaterialPin::Opacity;
    Lowering lower{graph};
    lower.result.nodePreview=preview.has_value();
    lower.row({settings.baseColor[0],settings.baseColor[1],settings.baseColor[2],settings.blend==MaterialBlend::Mask?settings.opacityMask:settings.opacity});
    lower.row({settings.roughness,settings.metallic,settings.occlusion,settings.occlusionStrength});
    lower.row({settings.emission[0],settings.emission[1],settings.emission[2],settings.alphaCutoff});
    lower.row({settings.doubleSided?1.f:0.f,0.f,lit?0.f:1.f,preview?0.f:static_cast<float>(settings.blend)});
    const auto specularRow=lower.row({settings.specular,0,0,0});
    const auto identity=lower.mapping({});
    auto order=materialGraphOrder(graph,settings.shading,settings.blend);
    if(preview) {
        const auto* target=findMaterialNode(graph,preview->node);
        if(!target || target->kind==MaterialNodeKind::Output)throw std::runtime_error("Node preview target is unavailable.");
        materialOutputType(graph,preview->node,preview->pin);
        order.clear();std::set<MaterialNodeId> visited;
        std::function<void(MaterialNodeId)> visit=[&](auto id){
            if(!visited.insert(id).second)return;
            const auto& node=*findMaterialNode(graph,id);
            for(const auto& pin:materialNodeInputs(graph,node,settings.shading,settings.blend))
                if(const auto* link=materialInputLink(graph,id,pin.pin))visit(link->fromNode);
            order.push_back(id);
        };
        visit(preview->node);
    }
    std::size_t number{};
    for(const auto id:order) {
        const auto& node=*findMaterialNode(graph,id);const auto expression="g"+std::to_string(number++);
        lower.expressions.emplace(id,expression);
        switch(node.kind) {
        case MaterialNodeKind::Output:break;
        case MaterialNodeKind::UV:lower.mappings.emplace(id,lower.mapping(resolveMaterialUvMapping(graph,id)));break;
        case MaterialNodeKind::Scalar:lower.code<<"    float "<<expression<<" = "<<param(lower.row({node.scalar,0,0,0}))<<".x;\n";break;
        case MaterialNodeKind::Vector2:lower.code<<"    vec2 "<<expression<<" = "<<param(lower.row({node.vector2[0],node.vector2[1],0,0}))<<".xy;\n";break;
        case MaterialNodeKind::ParticleColor:lower.code<<"    vec4 "<<expression<<" = context.particleColor;\n";break;
        case MaterialNodeKind::RadialGradient: {
            auto mappingRow=identity;
            if(const auto* link=materialInputLink(graph,id,MaterialPin::UV))mappingRow=lower.mappings.at(link->fromNode);
            lower.code<<"    float "<<expression<<" = ccGraphRadialGradient(context, "<<param(mappingRow)<<", "<<param(mappingRow+1)<<", "<<param(mappingRow+2)<<");\n";break;
        }
        case MaterialNodeKind::Color:lower.code<<"    vec4 "<<expression<<" = "<<param(lower.row(node.color))<<";\n";break;
        case MaterialNodeKind::Saturate: {
            const auto value=lower.input(node,MaterialPin::Left);
            lower.code<<"    float "<<expression<<" = clamp("<<value<<", 0.0, 1.0);\n";break;
        }
        case MaterialNodeKind::Multiply: {
            const auto a=lower.input(node,MaterialPin::Left),b=lower.input(node,MaterialPin::Right);
            const auto type=materialOutputType(graph,id,MaterialPin::Value);
            lower.code<<"    "<<glslType(type)<<" "<<expression<<" = "<<a<<" * "<<b<<";\n";break;
        }
        case MaterialNodeKind::Texture2D: {
            if(lower.result.textures.size()>=MaterialGraphMaxTextureBindings)throw std::runtime_error("Material graph node "+std::to_string(id)+" exceeds 12 active texture bindings.");
            auto mappingRow=identity;
            if(const auto* link=materialInputLink(graph,id,MaterialPin::UV))mappingRow=lower.mappings.at(link->fromNode);
            const auto settingsRow=lower.row({node.texture.normalStrength,node.texture.image.empty()?0.f:1.f,0,0});
            lower.textureSettings.emplace(id,settingsRow);
            const auto slot=static_cast<std::uint16_t>(lower.result.textures.size());
            const auto role=node.texture.role==MaterialTextureRole::Color?MaterialGraphTextureRole::Color:node.texture.role==MaterialTextureRole::Normal?MaterialGraphTextureRole::Normal:MaterialGraphTextureRole::Data;
            lower.result.textures.push_back({id,slot,mappingRow,settingsRow,node.texture.image,node.texture.sampler,role});
            const bool normal=role==MaterialGraphTextureRole::Normal;
            lower.result.usesNormal|=normal;
            // The role belongs to shader identity even though Color/Data decode
            // is carried by the GPU resource's format, not by a second gamma op.
            lower.code<<"    // sample role "<<static_cast<unsigned>(role)<<"\n";
            if(normal)lower.code<<"#if CC_GRAPH_SURFACE\n";
            lower.code<<"    "<<(normal?"vec3 ":"vec4 ")<<expression<<" = "<<(normal?"ccGraphNormal(":"ccGraphSample(")<<materialGraphSamplerName(slot)
                <<", context, "<<param(mappingRow)<<", "<<param(mappingRow+1)<<", "<<param(mappingRow+2)<<", "<<param(settingsRow)<<");\n";
            if(normal)lower.code<<"#endif\n";
            break;
        }
        }
    }
    std::ostringstream evaluate;
    evaluate<<"GraphMaterial ccEvaluateGraph(GraphContext context)\n{\n"<<lower.code.str()<<"    GraphMaterial result;\n";
    evaluate<<"    result.baseColor = "<<lower.output(output,MaterialPin::BaseColor,"u_graphParams[0].rgb",!preview)<<";\n";
    evaluate<<"    result.opacity = clamp("<<lower.output(output,opacityPin,"u_graphParams[0].w",opacityActive)<<", 0.0, 1.0);\n";
    evaluate<<"    result.roughness = "<<lower.output(output,MaterialPin::Roughness,"u_graphParams[1].x",lit)<<";\n";
    evaluate<<"    result.metallic = "<<lower.output(output,MaterialPin::Metallic,"u_graphParams[1].y",lit)<<";\n";
    evaluate<<"    result.specular = "<<lower.output(output,MaterialPin::Specular,param(specularRow)+".x",lit)<<";\n";
    evaluate<<"    result.occlusion = "<<lower.output(output,MaterialPin::Occlusion,"u_graphParams[1].z",lit)<<";\n";
    evaluate<<"    result.emission = "<<lower.output(output,MaterialPin::Emission,"u_graphParams[2].rgb",lit)<<";\n";
    evaluate<<"    result.normal = ccMappingNormalize(context.normal, vec3(0.0, 1.0, 0.0));\n#if CC_GRAPH_SURFACE\n";
    evaluate<<"    result.normal = "<<lower.output(output,MaterialPin::Normal,"result.normal",lit)<<";\n#endif\n";
    if(preview) {
        const auto type=materialOutputType(graph,preview->node,preview->pin);
        if(type==MaterialValueType::Mapping) {
            const auto row=lower.mappings.at(preview->node);
            evaluate<<"    result.baseColor = ccGraphChecker(context, "<<param(row)<<", "<<param(row+1)<<", "<<param(row+2)<<");\n";
        } else {
            const auto expression=lower.expressions.at(preview->node)+valueSuffix(preview->pin);
            evaluate<<"    result.baseColor = ";
            if(type==MaterialValueType::Scalar)evaluate<<"vec3_splat("<<expression<<")";
            else if(type==MaterialValueType::Vector2)evaluate<<"vec3("<<expression<<", 0.0)";
            else if(type==MaterialValueType::Normal)evaluate<<"("<<expression<<" * 0.5 + vec3_splat(0.5))";
            else evaluate<<expression;
            evaluate<<";\n";
        }
        evaluate<<"    result.opacity = 1.0;\n";
    }
    evaluate<<"    return result;\n}\n";
    lower.result.usesTextures=!lower.result.textures.empty();
    for(const auto pass:{MaterialGraphPass::Model,MaterialGraphPass::Picking,MaterialGraphPass::PointShadow,MaterialGraphPass::Particle}) {
        if(preview && pass!=MaterialGraphPass::Model)continue;
        MaterialGraphShaderVariant variant;variant.pass=pass;
        const bool model=pass==MaterialGraphPass::Model,particle=pass==MaterialGraphPass::Particle,pick=pass==MaterialGraphPass::Picking;
        variant.vertexName=particle?"vs_particle":model?"vs_model":pick?"vs_pick":"vs_point_shadow";
        variant.varyingName=particle?"particle.varying.def.sc":"model.varying.def.sc";
        std::ostringstream source;
        source<<((model || particle)?
            "$input v_worldPosition, v_normal, v_texcoord0, v_texcoord1, v_tangent, v_color0, v_localPosition, v_localNormal\n":
            "$input v_texcoord0, v_texcoord1, v_color0, v_localPosition, v_localNormal, v_worldPosition, v_normal\n");
        source<<"// crimson-material-graph-generator-v5\n#define CC_GRAPH_SURFACE "<<(model || particle)<<"\n#define CC_GRAPH_MODEL "<<model<<"\n#define CC_GRAPH_PARTICLE "<<particle
            <<"\n#define CC_GRAPH_PICK "<<pick<<"\n#define CC_GRAPH_MASK "<<(!preview && settings.blend==MaterialBlend::Mask)<<"\n#define CC_GRAPH_LIT "<<lit<<"\n";
        source<<"#include <bgfx_shader.sh>\nuniform vec4 u_graphParams[208];\n";
        if(model || particle)source<<"uniform mat4 u_modelNormalMatrix;\nuniform vec4 u_modelUv;\n";
        for(const auto& texture:lower.result.textures)source<<"SAMPLER2D("<<materialGraphSamplerName(texture.slot)<<", "<<texture.slot<<");\n";
        source<<"#include \"material_graph_common.sh\"\n"<<evaluate.str();
        source<<"#include \""<<(particle?"material_graph_particle.sh":model?"material_graph_model.sh":"material_graph_depth.sh")<<"\"\n";
        variant.source=source.str();variant.manifest=manifest(lower.result,pass);lower.result.variants.push_back(std::move(variant));
    }
    std::uint64_t hash=14695981039346656037ull;
    for(const auto& variant:lower.result.variants)for(unsigned char ch:variant.source){hash^=ch;hash*=1099511628211ull;}
    lower.result.topologyKey=std::string(preview?"graph-preview-v5-":"graph-v5-")+std::to_string(hash);
    return std::move(lower.result);
}
MaterialGraphShader generateMaterialGraphShader(const MaterialAsset& material) {return generateGraphShader(material,{});}
MaterialGraphShader generateMaterialGraphPreviewShader(const MaterialAsset& material,MaterialGraphPreviewTarget target) {return generateGraphShader(material,target);}
ShaderCompileRecipe materialGraphShaderRecipe(const ShaderCompileConfig& config,const MaterialGraphShader& graph,const MaterialGraphShaderVariant& variant) {
    ShaderCompileRecipe result;
    result.name=graph.topologyKey+"-"+std::to_string(static_cast<unsigned>(variant.pass));result.source=variant.source;
    result.varyingDefinition=config.sourceDirectory/variant.varyingName;
    result.includeDirectories={config.sourceDirectory,config.includeDirectory};
    result.contract="crimson-material-graph-v5;12-textures;208-vec4;ibl12-14;shadow15;particle-instance80-orientation;rgb-to-scalar-r";
    return result;
}
}
