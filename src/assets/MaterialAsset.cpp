#include "MaterialAsset.h"
#include "AtomicFile.h"
#include "ContentStorage.h"
#include "resources/AssetId.h"
#include "math/Json.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <fstream>
#include <stdexcept>
#include <unordered_set>
#include <optional>
#include <map>

namespace crimson {
namespace {
using Json=nlohmann::json;
std::filesystem::path path(const std::string& id) { return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(id.data()),id.size())); }
std::filesystem::path materialPath(const std::filesystem::path& root,const std::string& id) {
    if(isAssetUuid(id)) {
        std::optional<std::filesystem::path> existing;
        try{existing=resolveAssetPath(root,id);}catch(const std::exception&){/* New UUID has no location until its first save. */}
        if(existing){if(existing->extension()!=".crimson-material")throw std::runtime_error("Asset identity does not reference a material file.");return *existing;}
        return resolveAssetPath(root,"assets/materials")/(id+".crimson-material");
    }
    const auto relative=path(id);
    if (relative.has_root_path() || relative.parent_path()!=std::filesystem::path("assets/materials") || relative.extension()!=".crimson-material" || id.find(':')!=std::string::npos)
        throw std::runtime_error("Material asset must be stored directly under assets/materials.");
    if(contentStorageReady(root))return resolveAssetPath(root,id);
    const auto result=std::filesystem::weakly_canonical(root/relative),directory=std::filesystem::weakly_canonical(root/"assets/materials");
    const auto containment=result.lexically_relative(std::filesystem::weakly_canonical(root));
    if (result.parent_path()!=directory || containment.empty() || containment.is_absolute() || *containment.begin()=="..")
        throw std::runtime_error("Material path escapes its managed directory.");
    return result;
}
bool safeImage(const std::string& id) {
    if(isAssetUuid(id))return true;
    if (id.empty() || id==GrayTextureId || id==NeutralNormalTextureId || id=="builtin:texture/gray" || id=="builtin:texture/normal") return true;
    const auto relative=path(id);
    if (relative.has_root_path() || relative.empty() || id.find(':')!=std::string::npos) return false;
    for(const auto& part:relative) if(part=="..") return false;
    return relative.begin()!=relative.end() && *relative.begin()=="assets";
}
ModelTexture readTexture(const Json& j) {
    ModelTexture t; t.image=AssetRef<DecodedImage>{j.at("image").get<std::string>()}; t.texCoord=j.at("uv").get<uint32_t>();
    if(!safeImage(t.image.id))throw std::runtime_error("Material image identity is invalid.");
    t.image.id=canonicalAssetId(t.image.id);
    const int u=j.at("wrapU").get<int>(),v=j.at("wrapV").get<int>(),min=j.at("minFilter").get<int>(),mag=j.at("magFilter").get<int>();
    if(u<0||u>2||v<0||v>2||min<0||min>5||mag<0||mag>1) throw std::runtime_error("Material sampler is invalid.");
    t.wrapU=static_cast<ModelWrap>(u);t.wrapV=static_cast<ModelWrap>(v);t.minFilter=static_cast<ModelFilter>(min);t.magFilter=static_cast<ModelFilter>(mag);return t;
}
Json samplerJson(const ModelSampler& sampler) {
    return {{"wrapU",static_cast<int>(sampler.wrapU)},{"wrapV",static_cast<int>(sampler.wrapV)},
        {"minFilter",static_cast<int>(sampler.minFilter)},{"magFilter",static_cast<int>(sampler.magFilter)}};
}
ModelSampler readSampler(const Json& j) {
    const int u=j.at("wrapU").get<int>(),v=j.at("wrapV").get<int>(),min=j.at("minFilter").get<int>(),mag=j.at("magFilter").get<int>();
    if(u<0||u>2||v<0||v>2||min<0||min>5||mag<0||mag>1)throw std::runtime_error("Material sampler is invalid.");
    return {static_cast<ModelWrap>(u),static_cast<ModelWrap>(v),static_cast<ModelFilter>(min),static_cast<ModelFilter>(mag)};
}
const char* shadingName(MaterialShading shading) {
    switch(shading){case MaterialShading::StandardPbr:return "standard-pbr";case MaterialShading::Unlit:return "unlit";}
    throw std::runtime_error("Unsupported material shading mode.");
}
const char* blendName(MaterialBlend blend) {
    switch(blend){case MaterialBlend::Opaque:return "opaque";case MaterialBlend::Mask:return "mask";case MaterialBlend::Alpha:return "alpha";case MaterialBlend::Additive:return "additive";}
    throw std::runtime_error("Unsupported material blend mode.");
}
MaterialShading readShading(const std::string& value) {
    if(value=="standard-pbr")return MaterialShading::StandardPbr;if(value=="unlit")return MaterialShading::Unlit;
    throw std::runtime_error("Unsupported material shading mode.");
}
MaterialBlend readBlend(const std::string& value) {
    if(value=="opaque")return MaterialBlend::Opaque;if(value=="mask")return MaterialBlend::Mask;
    if(value=="alpha")return MaterialBlend::Alpha;if(value=="additive")return MaterialBlend::Additive;
    throw std::runtime_error("Unsupported material blend mode.");
}
std::string uniqueId(const std::filesystem::path& root) {
    (void)root;return newAssetId();
}
std::string uniqueName(const std::filesystem::path& root,const std::string& requested) {
    const std::string base=requested.empty()?"New Material":requested;if(base.size()>1024)throw std::runtime_error("Material name exceeds 1024 UTF-8 bytes.");
    std::unordered_set<std::string> names;
    const auto readName=[&](const auto& file){try{std::ifstream input(file);const auto json=Json::parse(input);names.insert(json.at("name").get<std::string>());}catch(const std::exception&){/* Damaged files remain untouched. */}};
    if(contentStorageReady(root)) {for(const auto& entry:storedAssetPaths(root))if(entry.file.extension()==".crimson-material")readName(entry.file);}
    else {const auto directory=root/"assets/materials";if(std::filesystem::exists(directory))for(const auto& entry:std::filesystem::directory_iterator(directory))if(entry.is_regular_file()&&entry.path().extension()==".crimson-material")readName(entry.path());}
    if(!names.contains(base))return base;
    for(uint64_t suffix=2;;++suffix){const auto name=base+" "+std::to_string(suffix);if(name.size()>1024)throw std::runtime_error("Material name is too long for a unique suffix.");if(!names.contains(name))return name;}
}
}
std::filesystem::path materialAssetPath(const std::filesystem::path& root,const std::string& id){return materialPath(root,id);}
std::string importedMaterialId(const std::string& modelId,std::size_t index) {
    if(isAssetUuid(modelId))return derivedAssetId(modelId,"material:"+std::to_string(index));
    const auto token=path(modelId).parent_path().filename().generic_u8string();
    if(token.empty()) throw std::runtime_error("Model package identity is empty.");
    return canonicalAssetId("assets/materials/"+std::string(reinterpret_cast<const char*>(token.data()),token.size())+"-m"+std::to_string(index)+".crimson-material");
}
MaterialGraph materialGraphFromLegacy(const ModelMaterial& surface,MaterialShading shading,MaterialBlend blend) {
    MaterialGraph graph;graph.nodes.reserve(32);graph.links.reserve(40);
    const auto outputId=addMaterialNode(graph,MaterialNodeKind::Output,{700,80}).id;
    auto& settings=materialOutputNode(graph).output;settings.shading=shading;
    settings.blend=blend==MaterialBlend::Opaque?(surface.alphaBlend?MaterialBlend::Alpha:surface.alphaMask?MaterialBlend::Mask:blend):blend;
    const bool sprite=shading==MaterialShading::Unlit&&(settings.blend==MaterialBlend::Alpha||settings.blend==MaterialBlend::Additive);
    settings.baseColor={surface.baseColor[0],surface.baseColor[1],surface.baseColor[2]};settings.opacity=surface.baseColor[3];
    settings.opacityMask=surface.baseColor[3];settings.specular=surface.specular;
    settings.roughness=surface.roughness;settings.metallic=surface.metallic;settings.emission=surface.emissive;
    settings.occlusionStrength=surface.occlusionStrength;settings.alphaCutoff=surface.alphaCutoff;settings.doubleSided=surface.doubleSided;
    settings.receiveFog=surface.receiveFog;
    const auto link=[&](MaterialNodeId from,MaterialPin fromPin,MaterialNodeId to,MaterialPin toPin) {
        graph.links.push_back({graph.nextLinkId++,from,to,fromPin,toPin});
    };
    const auto mapping=[&](uint32_t uv,float y) {
        // Legacy sprites always sampled UV0 with the ordinary XY/Z transform,
        // even when the shared material carried a triplanar projection flag.
        MaterialUvMapping value;value.mode=sprite?MaterialUvMode::UV0:surface.triplanar?
            (surface.triplanarWorld?MaterialUvMode::WorldTriplanar:MaterialUvMode::LocalTriplanar):(uv?MaterialUvMode::UV1:MaterialUvMode::UV0);
        value.scale=surface.uvScale;value.offset=surface.uvOffset;value.rotation=surface.uvRotation;value.scaleLinked=surface.uvScaleLinked;
        for(const auto& n:graph.nodes)if(n.kind==MaterialNodeKind::UV&&n.uv==value)return n.id;
        auto& n=addMaterialNode(graph,MaterialNodeKind::UV,{-750,y});n.uv=value;return n.id;
    };
    const auto texture=[&](const ModelTexture& slot,MaterialTextureRole role,float y)->MaterialNodeId {
        if(slot.image.empty())return 0;
        const auto uvId=mapping(slot.texCoord,y);const auto sampler=materialSampler(surface,slot);
        for(const auto& n:graph.nodes)if(n.kind==MaterialNodeKind::Texture2D&&n.texture.image==slot.image&&n.texture.role==role&&
            n.texture.sampler==sampler&&(role!=MaterialTextureRole::Normal||n.texture.normalStrength==surface.normalScale)) {
            const auto* input=materialInputLink(graph,n.id,MaterialPin::UV);if(input&&input->fromNode==uvId)return n.id;
        }
        auto& n=addMaterialNode(graph,MaterialNodeKind::Texture2D,{-450,y});n.texture.image=slot.image;n.texture.role=role;n.texture.sampler=sampler;
        if(role==MaterialTextureRole::Normal)n.texture.normalStrength=surface.normalScale;
        const auto id=n.id;link(uvId,MaterialPin::UV,id,MaterialPin::UV);return id;
    };
    const auto parameter=[&](MaterialPin target,const Vec4& value,bool color,MaterialNodeId image,MaterialPin channel,float y) {
        const auto title=std::string(materialPinName(target));
        const bool neutral=color?(value[0]==1&&value[1]==1&&value[2]==1):value[0]==1;
        if(image&&neutral){link(image,channel,outputId,target);return;}
        auto& constant=addMaterialNode(graph,color?MaterialNodeKind::Color:MaterialNodeKind::Scalar,{-120,y+90});
        constant.name=title+(image?" factor":"");constant.color=value;constant.scalar=value[0];
        if(value[0]<0)constant.scalarRange[0]=std::min(-1.f,value[0]);
        if(value[0]>1)constant.scalarRange[1]=value[0];
        const auto id=constant.id;const auto pin=color?MaterialPin::RGB:MaterialPin::Value;
        if(!image){link(id,pin,outputId,target);return;}
        auto& multiply=addMaterialNode(graph,MaterialNodeKind::Multiply,{300,y});multiply.name=title+" × factor";
        const auto product=multiply.id;link(image,channel,product,MaterialPin::Left);link(id,pin,product,MaterialPin::Right);
        link(product,MaterialPin::Value,outputId,target);
    };
    const auto base=texture(surface.baseColorTexture,MaterialTextureRole::Color,0);
    parameter(MaterialPin::BaseColor,surface.baseColor,true,base,MaterialPin::RGB,0);
    const auto packed=texture(surface.metallicRoughnessTexture,MaterialTextureRole::Data,300);
    parameter(MaterialPin::Roughness,{surface.roughness,surface.roughness,surface.roughness,surface.roughness},false,packed,MaterialPin::G,300);
    parameter(MaterialPin::Metallic,{surface.metallic,surface.metallic,surface.metallic,surface.metallic},false,packed,MaterialPin::B,530);
    if(const auto normal=texture(surface.normalTexture,MaterialTextureRole::Normal,760))link(normal,MaterialPin::Normal,outputId,MaterialPin::Normal);
    else if(surface.normalScale!=1) {
        const auto uvId=mapping(surface.normalTexture.texCoord,760);
        auto& normalNode=addMaterialNode(graph,MaterialNodeKind::Texture2D,{-450,760});normalNode.name="Normal (unassigned)";
        normalNode.texture.role=MaterialTextureRole::Normal;normalNode.texture.normalStrength=surface.normalScale;normalNode.texture.sampler=materialSampler(surface,surface.normalTexture);
        link(uvId,MaterialPin::UV,normalNode.id,MaterialPin::UV);
    }
    if(const auto occlusion=texture(surface.occlusionTexture,MaterialTextureRole::Data,990))link(occlusion,MaterialPin::R,outputId,MaterialPin::Occlusion);
    const auto emission=texture(surface.emissiveTexture,MaterialTextureRole::Color,1220);
    parameter(MaterialPin::Emission,{surface.emissive[0],surface.emissive[1],surface.emissive[2],1},true,emission,MaterialPin::RGB,1220);
    parameter(settings.blend==MaterialBlend::Opaque||settings.blend==MaterialBlend::Mask?MaterialPin::OpacityMask:MaterialPin::Opacity,
        {surface.baseColor[3],surface.baseColor[3],surface.baseColor[3],surface.baseColor[3]},false,base,MaterialPin::A,1450);
    // An untextured material can still have authored mapping ready to reuse.
    if(std::none_of(graph.nodes.begin(),graph.nodes.end(),[](const auto& n){return n.kind==MaterialNodeKind::UV;}))(void)mapping(0,0);
    if(sprite)migrateLegacyParticleGraph(graph,true);
    validateMaterialGraph(graph);return graph;
}
namespace {
struct LegacyExpression {Vec4 factor{1,1,1,1};const MaterialNode* texture{};MaterialPin channel{MaterialPin::RGB};bool representable{true};};
using LegacyExpressionCache=std::map<std::pair<MaterialNodeId,MaterialPin>,LegacyExpression>;
LegacyExpression legacyExpression(const MaterialGraph& graph,MaterialNodeId id,MaterialPin pin,LegacyExpressionCache& cache) {
    const auto key=std::pair{id,pin};
    if(const auto found=cache.find(key);found!=cache.end())return found->second;
    const auto expression=[&] {
    const auto& n=*findMaterialNode(graph,id);LegacyExpression result;
    // Particle Color is neutral on meshes and in legacy fallback transport.
    if(n.kind==MaterialNodeKind::ParticleColor)return result;
    if(n.kind==MaterialNodeKind::Texture2D){result.texture=&n;result.channel=pin;return result;}
    if(n.kind==MaterialNodeKind::Scalar){result.factor={n.scalar,n.scalar,n.scalar,n.scalar};return result;}
    if(n.kind==MaterialNodeKind::Vector2) {
        if(pin==MaterialPin::XY)result.factor={n.vector2[0],n.vector2[1],0,0};
        else {const auto value=n.vector2[pin==MaterialPin::X?0:1];result.factor={value,value,value,value};}
        return result;
    }
    if(n.kind==MaterialNodeKind::Color) {
        if(pin==MaterialPin::RGB)result.factor=n.color;
        else {const auto index=pin==MaterialPin::R?0:pin==MaterialPin::G?1:pin==MaterialPin::B?2:3;const auto v=n.color[index];result.factor={v,v,v,v};}
        return result;
    }
    if(n.kind==MaterialNodeKind::Clamp) {
        // Only a uniform clamp can be represented by the legacy factor slots.
        // Keep textured saturation in the authoritative graph, not an incorrect
        // texture-times-factor approximation that changes coverage.
        if(materialExpressionUniform(graph,id,MaterialPin::Value)&&!materialExpressionTimeDependent(graph,id,MaterialPin::Value))result.factor=evaluateMaterialUniform(graph,id,MaterialPin::Value).value;
        else result.representable=false;
        return result;
    }
    if(n.kind==MaterialNodeKind::Multiply) {
        const auto input=[&](MaterialPin inputPin,const MaterialValue& fallback) {
            if(const auto* l=materialInputLink(graph,id,inputPin))return legacyExpression(graph,l->fromNode,l->fromPin,cache);
            LegacyExpression e;e.factor=fallback.type==MaterialValueType::Scalar?Vec4{fallback.value[0],fallback.value[0],fallback.value[0],fallback.value[0]}:fallback.value;return e;
        };
        const auto a=input(MaterialPin::Left,n.left),b=input(MaterialPin::Right,n.right);
        for(size_t i=0;i<4;++i)result.factor[i]=a.factor[i]*b.factor[i];
        result.texture=a.texture?a.texture:b.texture;result.channel=a.texture?a.channel:b.channel;
        result.representable=a.representable&&b.representable&&!(a.texture&&b.texture);return result;
    }
    if(n.kind==MaterialNodeKind::Add||n.kind==MaterialNodeKind::Lerp||n.kind==MaterialNodeKind::Power||materialNodeImageAdjustment(n.kind)) {
        if(materialExpressionUniform(graph,id,pin)&&!materialExpressionTimeDependent(graph,id,pin))result.factor=evaluateMaterialUniform(graph,id,pin).value;
        else result.representable=false;
        return result;
    }
    result.representable=false;return result;
    }();
    cache.emplace(key,expression);return expression;
}
}
ModelMaterial materialGraphLegacySurface(const MaterialGraph& graph,const std::string& name) {
    validateMaterialGraph(graph);const auto& root=materialOutputNode(graph);const auto& output=root.output;
    const auto coverage=output.blend==MaterialBlend::Mask?MaterialPin::OpacityMask:MaterialPin::Opacity;
    ModelMaterial surface;surface.name=name;surface.baseColor={output.baseColor[0],output.baseColor[1],output.baseColor[2],output.blend==MaterialBlend::Mask?output.opacityMask:output.opacity};
    surface.specular=output.specular;
    surface.roughness=output.roughness;surface.metallic=output.metallic;surface.emissive=output.emission;
    surface.occlusionStrength=output.occlusionStrength;surface.alphaCutoff=output.alphaCutoff;surface.doubleSided=output.doubleSided;surface.alphaMask=output.blend==MaterialBlend::Mask;
    surface.receiveFog=output.receiveFog;
    surface.alphaBlend=output.blend==MaterialBlend::Alpha;
    bool mappingSet=false;
    const auto applyTexture=[&](const MaterialNode* texture,ModelTexture& target) {
        if(!texture)return;target.image=texture->texture.image;const auto& sampler=texture->texture.sampler;
        target.wrapU=sampler.wrapU;target.wrapV=sampler.wrapV;target.minFilter=sampler.minFilter;target.magFilter=sampler.magFilter;
        const auto mapping=materialTextureMapping(graph,texture->id);
        target.texCoord=mapping.mode==MaterialUvMode::UV1?1u:0u;
        if(!mappingSet){surface.uvScale=mapping.scale;surface.uvOffset=mapping.offset;surface.uvRotation=mapping.rotation;surface.uvScaleLinked=mapping.scaleLinked;
            surface.triplanar=mapping.mode==MaterialUvMode::LocalTriplanar||mapping.mode==MaterialUvMode::WorldTriplanar;
            surface.triplanarWorld=mapping.mode==MaterialUvMode::WorldTriplanar;mappingSet=true;}
    };
    LegacyExpressionCache expressionCache;
    const auto activeInputs=materialOutputInputs(output.shading,output.blend);
    const auto expression=[&](MaterialPin pin)->std::optional<LegacyExpression> {
        if(std::none_of(activeInputs.begin(),activeInputs.end(),[&](const auto& input){return input.pin==pin;}))return {};
        const auto* link=materialInputLink(graph,root.id,pin);if(!link)return {};
        auto value=legacyExpression(graph,link->fromNode,link->fromPin,expressionCache);
        if(materialInputDefault(root,pin).type==MaterialValueType::Scalar&&materialOutputType(graph,link->fromNode,link->fromPin)==MaterialValueType::Color) {
            const auto red=value.factor[0];value.factor={red,red,red,red};
            if(value.channel==MaterialPin::RGB)value.channel=MaterialPin::R;
        }
        return value.representable?std::optional{value}:std::nullopt;
    };
    if(const auto e=expression(MaterialPin::BaseColor)){for(size_t i=0;i<3;++i)surface.baseColor[i]=e->factor[i];applyTexture(e->texture,surface.baseColorTexture);}
    if(const auto e=expression(coverage)){surface.baseColor[3]=e->factor[0];if(e->channel==MaterialPin::A&&surface.baseColorTexture.image.empty())applyTexture(e->texture,surface.baseColorTexture);}
    if(const auto e=expression(MaterialPin::Specular))surface.specular=e->factor[0];
    if(const auto e=expression(MaterialPin::Roughness)){surface.roughness=e->factor[0];if(e->channel==MaterialPin::G)applyTexture(e->texture,surface.metallicRoughnessTexture);}
    if(const auto e=expression(MaterialPin::Metallic)){surface.metallic=e->factor[0];if(e->channel==MaterialPin::B&&surface.metallicRoughnessTexture.image.empty())applyTexture(e->texture,surface.metallicRoughnessTexture);}
    if(const auto e=expression(MaterialPin::Normal)){applyTexture(e->texture,surface.normalTexture);if(e->texture)surface.normalScale=e->texture->texture.normalStrength;}
    else for(const auto& n:graph.nodes)if(n.kind==MaterialNodeKind::Texture2D&&n.texture.role==MaterialTextureRole::Normal&&n.texture.image.empty()){surface.normalScale=n.texture.normalStrength;break;}
    if(const auto e=expression(MaterialPin::Occlusion))applyTexture(e->texture,surface.occlusionTexture);
    if(const auto e=expression(MaterialPin::Emission)){for(size_t i=0;i<3;++i)surface.emissive[i]=e->factor[i];applyTexture(e->texture,surface.emissiveTexture);}
    if(!mappingSet)for(const auto& n:graph.nodes)if(n.kind==MaterialNodeKind::UV){const auto mapping=resolveMaterialUvMapping(graph,n.id);
        surface.uvScale=mapping.scale;surface.uvOffset=mapping.offset;surface.uvRotation=mapping.rotation;surface.uvScaleLinked=mapping.scaleLinked;
        surface.triplanar=mapping.mode==MaterialUvMode::LocalTriplanar||mapping.mode==MaterialUvMode::WorldTriplanar;surface.triplanarWorld=mapping.mode==MaterialUvMode::WorldTriplanar;break;}
    surface.graph=std::make_shared<const MaterialGraph>(graph);return surface;
}
void canonicalizeMaterialAsset(MaterialAsset& material) {
    if(material.graph.empty())material.graph=materialGraphFromLegacy(material.surface,material.shading,material.blend);
    validateMaterialGraph(material.graph);
    material.id=canonicalAssetId(material.id);material.originModel=canonicalAssetId(material.originModel);
    for(auto& node:material.graph.nodes)if(node.kind==MaterialNodeKind::Texture2D)node.texture.image.id=canonicalAssetId(node.texture.image.id);
    const auto& output=materialOutputNode(material.graph).output;material.shading=output.shading;material.blend=output.blend;
    material.surface=materialGraphLegacySurface(material.graph,material.name);
}
std::optional<ModelMaterial> materialGraphFixedSurface(const MaterialGraph& graph) {
    validateMaterialGraph(graph);
    const auto& root=materialOutputNode(graph);const auto& output=root.output;
    // Other blend modes need independent graph coverage, including its clamp
    // before vertex alpha. Keep those on their complete graph generation.
    if(output.shading!=MaterialShading::StandardPbr || output.blend!=MaterialBlend::Opaque)return {};
    for(const auto id:materialGraphOrder(graph,output.shading,output.blend)) {
        const auto kind=findMaterialNode(graph,id)->kind;
        if(kind==MaterialNodeKind::Time || kind==MaterialNodeKind::Panner ||
           kind==MaterialNodeKind::ParticleColor || kind==MaterialNodeKind::RadialGradient || kind==MaterialNodeKind::Grunge)return {};
    }
    LegacyExpressionCache cache;
    const auto expression=[&](MaterialPin pin) {
        const auto fallback=materialInputDefault(root,pin);
        LegacyExpression value;value.factor=fallback.type==MaterialValueType::Scalar?
            Vec4{fallback.value[0],fallback.value[0],fallback.value[0],fallback.value[0]}:fallback.value;
        if(const auto* link=materialInputLink(graph,root.id,pin)) {
            value=legacyExpression(graph,link->fromNode,link->fromPin,cache);
            if(fallback.type==MaterialValueType::Scalar && materialOutputType(graph,link->fromNode,link->fromPin)==MaterialValueType::Color) {
                const auto red=value.factor[0];value.factor={red,red,red,red};
                if(value.channel==MaterialPin::RGB)value.channel=MaterialPin::R;
            }
        }
        return value;
    };
    const auto base=expression(MaterialPin::BaseColor),rough=expression(MaterialPin::Roughness),metal=expression(MaterialPin::Metallic),
        specular=expression(MaterialPin::Specular),normal=expression(MaterialPin::Normal),
        occlusion=expression(MaterialPin::Occlusion),emission=expression(MaterialPin::Emission);
    for(const auto* value:{&base,&rough,&metal,&specular,&normal,&occlusion,&emission})if(!value->representable)return {};
    const auto textureIs=[](const LegacyExpression& value,MaterialPin pin,MaterialTextureRole role) {
        return !value.texture || (value.channel==pin && value.texture->texture.role==role);
    };
    if(!textureIs(base,MaterialPin::RGB,MaterialTextureRole::Color) || !textureIs(rough,MaterialPin::G,MaterialTextureRole::Data) ||
       !textureIs(metal,MaterialPin::B,MaterialTextureRole::Data) || specular.texture ||
       !textureIs(normal,MaterialPin::Normal,MaterialTextureRole::Normal) ||
       !textureIs(occlusion,MaterialPin::R,MaterialTextureRole::Data) ||
       !textureIs(emission,MaterialPin::RGB,MaterialTextureRole::Color))return {};
    const auto sameTexture=[&](const MaterialNode* a,const MaterialNode* b) {
        return a->texture.image==b->texture.image && a->texture.sampler==b->texture.sampler &&
            a->texture.role==b->texture.role && materialTextureMapping(graph,a->id)==materialTextureMapping(graph,b->id);
    };
    if(rough.texture && metal.texture && !sameTexture(rough.texture,metal.texture))return {};
    // The fixed ORM slot samples both channels. A missing channel expression
    // is equivalent only when its factor is zero (or there is no packed map).
    if((rough.texture && !metal.texture && metal.factor[0]!=0) ||
       (metal.texture && !rough.texture && rough.factor[0]!=0))return {};
    if(occlusion.factor[0]!=1 || (normal.texture && normal.factor!=Vec4{1,1,1,1}))return {};
    std::optional<MaterialUvMapping> shared;
    for(const auto* value:{&base,&rough,&metal,&normal,&occlusion,&emission})if(value->texture) {
        auto mapping=materialTextureMapping(graph,value->texture->id);
        // Fixed slots can select UV0/UV1 independently, but their affine
        // transform and ordinary/triplanar projection are shared.
        if(mapping.mode==MaterialUvMode::UV1)mapping.mode=MaterialUvMode::UV0;
        mapping.scaleLinked=true; // Authoring preference has no shader effect.
        if(shared && *shared!=mapping)return {};
        shared=mapping;
    }
    auto result=materialGraphLegacySurface(graph);result.graph.reset();return result;
}
std::vector<std::string> materialTextureReferences(const MaterialAsset& material) {
    if(!material.graph.empty())return materialGraphTextureReferences(material.graph);
    std::vector<std::string> result;
    for(const auto* slot:{&material.surface.baseColorTexture,&material.surface.metallicRoughnessTexture,&material.surface.normalTexture,&material.surface.occlusionTexture,&material.surface.emissiveTexture})
        if(!slot->image.empty()&&std::find(result.begin(),result.end(),slot->image.id)==result.end())result.push_back(slot->image.id);
    return result;
}
MaterialAsset defaultMaterialAsset() {
    MaterialAsset m; m.id=DefaultMaterialId;m.name="Default material";m.surface.name=m.name;
    m.surface.baseColor={.5f,.5f,.5f,1};m.surface.roughness=1;m.surface.metallic=0;return m;
}
MaterialAsset defaultParticleMaterialAsset() {
    MaterialAsset material;material.id=DefaultParticleMaterialId;material.name="Default particle material";material.surface.name=material.name;
    material.surface.baseColor={1,1,1,1};material.surface.metallic=0;material.surface.roughness=1;
    material.surface.baseColorTexture.wrapU=material.surface.baseColorTexture.wrapV=ModelWrap::Clamp;
    material.shading=MaterialShading::Unlit;material.blend=MaterialBlend::Additive;
    auto& graph=material.graph;
    const auto output=addMaterialNode(graph,MaterialNodeKind::Output,{500,0}).id;
    materialOutputNode(graph).output.shading=MaterialShading::Unlit;
    materialOutputNode(graph).output.blend=MaterialBlend::Additive;
    materialOutputNode(graph).output.baseColor={1,1,1};
    const auto color=addMaterialNode(graph,MaterialNodeKind::ParticleColor,{-350,0}).id;
    const auto radial=addMaterialNode(graph,MaterialNodeKind::RadialGradient,{-350,230}).id;
    const auto opacity=addMaterialNode(graph,MaterialNodeKind::Multiply,{70,200}).id;
    connectMaterialNodes(graph,color,MaterialPin::RGB,output,MaterialPin::BaseColor);
    connectMaterialNodes(graph,color,MaterialPin::A,opacity,MaterialPin::Left);
    connectMaterialNodes(graph,radial,MaterialPin::Value,opacity,MaterialPin::Right);
    connectMaterialNodes(graph,opacity,MaterialPin::Value,output,MaterialPin::Opacity);
    canonicalizeMaterialAsset(material);return material;
}
bool particleMaterialCompatible(const MaterialAsset& material) {
    return modelMaterialCompatible(material);
}
bool modelMaterialCompatible(const MaterialAsset& material) {
    try {
        // Consumer compatibility is a graph property, independent of shading,
        // blending, or the geometry that eventually supplies its inputs.
        if(!material.graph.empty())validateMaterialGraph(material.graph);
        else {validateMaterialAsset(material);(void)materialGraphFromLegacy(material.surface,material.shading,material.blend);}
        return true;
    }catch(const std::exception&){return false;}
}
ModelSampler materialSampler(const ModelMaterial& material,const ModelTexture& texture) {
    return material.sharedSampler.value_or(ModelSampler{texture.wrapU,texture.wrapV,texture.minFilter,texture.magFilter});
}
void validateMaterialAsset(const MaterialAsset& a) {
    if(a.id.empty()||a.name.empty()||a.name.size()>1024)throw std::runtime_error("Material contains invalid identity or name.");
    if(!a.graph.empty()){validateMaterialGraph(a.graph);return;}
    (void)shadingName(a.shading);(void)blendName(a.blend);
    const auto& m=a.surface;
    if(a.id.empty()||a.name.empty()||a.name.size()>1024||!math::finite(m.baseColor)||!math::finite(m.emissive)||
        !math::finite(m.metallic)||!math::finite(m.roughness)||!math::finite(m.specular)||!math::finite(m.normalScale)||!math::finite(m.occlusionStrength)||!math::finite(m.alphaCutoff)||
        !math::finite(m.uvScale)||!math::finite(m.uvOffset)||!math::finite(m.uvRotation))
        throw std::runtime_error("Material contains invalid identity or nonfinite factors.");
    if(m.metallic<0||m.metallic>1||m.roughness<0||m.roughness>1||m.occlusionStrength<0||m.occlusionStrength>1||m.alphaCutoff<0||
        std::any_of(m.baseColor.begin(),m.baseColor.end(),[](float x){return x<0||x>1;})||std::any_of(m.emissive.begin(),m.emissive.end(),[](float x){return x<0;}))
        throw std::runtime_error("Material factors exceed supported ranges.");
    for(const auto* t:{&m.baseColorTexture,&m.metallicRoughnessTexture,&m.normalTexture,&m.occlusionTexture,&m.emissiveTexture}) {
        if(t->texCoord>1||!safeImage(t->image.id)) throw std::runtime_error("Material texture reference or UV set is invalid.");
        const int u=static_cast<int>(t->wrapU),v=static_cast<int>(t->wrapV),min=static_cast<int>(t->minFilter),mag=static_cast<int>(t->magFilter);
        if(u<0||u>2||v<0||v>2||min<0||min>5||mag<0||mag>1)throw std::runtime_error("Material sampler is invalid.");
    }
    if(m.sharedSampler)(void)readSampler(samplerJson(*m.sharedSampler));
}
std::string serializeMaterialAsset(const MaterialAsset& a) {
    validateMaterialAsset(a);auto canonical=a;canonicalizeMaterialAsset(canonical);
    return Json{{"version",6},{"type","crimson-material"},{"id",canonical.id},{"name",a.name},{"originModel",canonical.originModel},
        {"graph",Json::parse(serializeMaterialGraph(canonical.graph))}}.dump(2)+"\n";
}
MaterialAsset loadMaterialAsset(const std::filesystem::path& root,const std::string& id) {
    const auto canonicalId=canonicalAssetId(id);
    if(canonicalId==DefaultMaterialId||canonicalId==DefaultParticleMaterialId) {
        if(!contentStorageReady(root)||!std::filesystem::is_regular_file(resolveAssetPath(root,canonicalId))) {
            auto a=canonicalId==DefaultMaterialId?defaultMaterialAsset():defaultParticleMaterialAsset();canonicalizeMaterialAsset(a);return a;
        }
    }
    const auto file=materialPath(root,isEngineAssetId(canonicalId)?canonicalId:id);if(std::filesystem::file_size(file)>1024u*1024u)throw std::runtime_error("Material file exceeds 1 MiB.");
    std::ifstream input(file,std::ios::binary);if(!input)throw std::runtime_error("Cannot read material asset.");const auto j=Json::parse(input);
    (void)materialPath(root,j.at("id").get<std::string>());
    if(!j.at("version").is_number_integer()||(j.at("version")!=1&&j.at("version")!=2&&j.at("version")!=3&&j.at("version")!=4&&j.at("version")!=5&&j.at("version")!=6)||j.at("type")!="crimson-material"||canonicalAssetId(j.at("id").get<std::string>())!=canonicalId)throw std::runtime_error("Material type, version or identity is invalid.");
    MaterialAsset a;a.id=canonicalId;a.name=j.at("name").get<std::string>();a.originModel=canonicalAssetId(j.value("originModel",std::string{}));auto& m=a.surface;m.name=a.name;
    if(j.at("version")==4||j.at("version")==5||j.at("version")==6){a.graph=parseMaterialGraph(j.at("graph").dump());canonicalizeMaterialAsset(a);validateMaterialAsset(a);return a;}
    m.baseColor=j.at("baseColor").get<Vec4>();m.metallic=j.at("metallic").get<float>();m.roughness=j.at("roughness").get<float>();m.normalScale=j.at("normalScale").get<float>();
    m.occlusionStrength=j.at("occlusionStrength").get<float>();m.emissive=j.at("emissive").get<Vec3>();m.alphaMask=j.at("alphaMask").get<bool>();m.doubleSided=j.at("doubleSided").get<bool>();m.alphaCutoff=j.at("alphaCutoff").get<float>();
    if(j.at("version")==1)a.blend=m.alphaMask?MaterialBlend::Mask:MaterialBlend::Opaque;
    else {a.shading=readShading(j.at("shading").get<std::string>());a.blend=readBlend(j.at("blend").get<std::string>());m.alphaMask=a.blend==MaterialBlend::Mask;}
    if(j.at("version")==3) {
        m.uvScale=j.at("uvScale").get<Vec3>();m.uvOffset=j.at("uvOffset").get<Vec3>();m.uvRotation=j.at("uvRotation").get<Vec3>();
        m.triplanar=j.at("triplanar").get<bool>();m.triplanarWorld=j.value("triplanarWorld",false);m.uvScaleLinked=j.at("uvScaleLinked").get<bool>();
        if(!j.at("sharedSampler").is_null())m.sharedSampler=readSampler(j.at("sharedSampler"));
    }
    m.baseColorTexture=readTexture(j.at("baseColorTexture"));m.metallicRoughnessTexture=readTexture(j.at("metallicRoughnessTexture"));m.normalTexture=readTexture(j.at("normalTexture"));
    m.occlusionTexture=readTexture(j.at("occlusionTexture"));m.emissiveTexture=readTexture(j.at("emissiveTexture"));validateMaterialAsset(a);canonicalizeMaterialAsset(a);return a;
}
void saveMaterialAsset(const std::filesystem::path& root,const MaterialAsset& a) {
    if(isEngineAssetId(a.id))throw std::runtime_error("Built-in materials cannot be edited.");
    const auto file=materialPath(root,a.id);if(contentStorageReady(root))(void)checkedContentPath(root,file);
    writeFileAtomically(file,serializeMaterialAsset(a));
    if(contentStorageReady(root))registerAssetPath(root,a.id,file);
}
void ensureEngineMaterialAssets(const std::filesystem::path& root) {
    if(!contentStorageReady(root))return;
    for(const auto material:{defaultMaterialAsset(),defaultParticleMaterialAsset()}) {
        const auto file=resolveAssetPath(root,material.id);
        if(!std::filesystem::exists(file))writeFileAtomically(file,serializeMaterialAsset(material));
    }
}
MaterialAsset createMaterialAsset(const std::filesystem::path& root,const std::string& name) {
    if(!contentStorageReady(root))initializeContentStorage(root);
    auto material=defaultMaterialAsset();material.id=uniqueId(root);material.name=uniqueName(root,name);material.surface.name=material.name;
    constexpr float midgrayLinear=.21404114f;material.surface.baseColor={midgrayLinear,midgrayLinear,midgrayLinear,1};
    material.surface.sharedSampler=ModelSampler{ModelWrap::Repeat,ModelWrap::Repeat,ModelFilter::LinearMipmapLinear,ModelFilter::Linear};
    canonicalizeMaterialAsset(material);
    saveMaterialAsset(root,material);return material;
}
MaterialAsset copyMaterialAsset(const std::filesystem::path& root,const MaterialAsset& source,const std::string& name) {
    if(!contentStorageReady(root))initializeContentStorage(root);
    validateMaterialAsset(source);auto material=source;material.id=uniqueId(root);material.name=uniqueName(root,name.empty()?source.name:name);
    material.surface.name=material.name;material.originModel.clear();canonicalizeMaterialAsset(material);saveMaterialAsset(root,material);return material;
}
void promoteModelMaterials(const std::filesystem::path& root,const ModelAsset& model,const std::function<bool(const std::string&)>& deleted,
    const std::function<void(const std::string&)>& created) {
    for(std::size_t i=0;i<model.materials.size();++i){const auto id=importedMaterialId(model.id,i);
        if(i<model.materialIds.size() && model.materialIds[i]!=id)continue;
        if((deleted&&deleted(id))||std::filesystem::exists(materialPath(root,id)))continue;
        MaterialAsset a{id,model.materials[i].name,model.id,model.materials[i]};
        a.blend=a.surface.alphaBlend?MaterialBlend::Alpha:a.surface.alphaMask?MaterialBlend::Mask:MaterialBlend::Opaque;saveMaterialAsset(root,a);
        if(created)created(id);
    }
}
}
