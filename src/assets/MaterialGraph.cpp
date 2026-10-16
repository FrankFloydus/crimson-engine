#include "MaterialGraph.h"
#include "math/Json.h"
#include "resources/AssetId.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace crimson {
namespace {
using Json=nlohmann::json;
[[noreturn]] void invalid(const std::string& message) { throw std::runtime_error("Material graph: "+message); }
bool numeric(MaterialValueType type) { return type==MaterialValueType::Scalar || type==MaterialValueType::Color || type==MaterialValueType::Vector2; }
bool arithmetic(MaterialNodeKind kind) {return kind==MaterialNodeKind::Multiply||kind==MaterialNodeKind::Add||kind==MaterialNodeKind::Lerp||kind==MaterialNodeKind::Power;}
bool colorInput(MaterialValueType type) {return type==MaterialValueType::Scalar||type==MaterialValueType::Color;}
Vec3 shiftedHue(Vec3 rgb,float degrees) {
    const float low=std::min({rgb[0],rgb[1],rgb[2]}),high=std::max({rgb[0],rgb[1],rgb[2]}),chroma=high-low;
    const float shift=std::fmod(degrees,360.f)/360.f;
    if(chroma<=0||shift==0)return rgb;
    float hue=high==rgb[0]?(rgb[1]-rgb[2])/chroma:high==rgb[1]?(rgb[2]-rgb[0])/chroma+2.f:(rgb[0]-rgb[1])/chroma+4.f;
    hue=hue/6.f+shift;hue-=std::floor(hue);
    constexpr float offsets[]{0.f,2.f/3.f,1.f/3.f};
    for(unsigned i=0;i<3;++i) {
        const float phase=hue+offsets[i],wrapped=phase-std::floor(phase);
        rgb[i]=low+chroma*std::clamp(std::abs(wrapped*6.f-3.f)-1.f,0.f,1.f);
    }
    return rgb;
}
MaterialValueType combined(MaterialValueType a,MaterialValueType b,const char* operation) {
    if(!numeric(a)||!numeric(b))invalid(std::string(operation)+" accepts only Scalar, Vector2 and Color inputs.");
    if(a!=MaterialValueType::Scalar&&b!=MaterialValueType::Scalar&&a!=b)invalid(std::string(operation)+" cannot mix Vector2 and Color inputs.");
    return a==MaterialValueType::Scalar?b:a;
}
bool validImage(const std::string& id) {
    if(id.empty() || id=="builtin:texture/gray" || id=="builtin:texture/normal")return true;
    if(isAssetUuid(id))return !isEngineAssetId(id)||id==canonicalAssetId("builtin:texture/gray")||id==canonicalAssetId("builtin:texture/normal");
    if(!id.starts_with("assets/") || id.find(':')!=std::string::npos || id.find('\\')!=std::string::npos)return false;
    const std::filesystem::path path{std::u8string(reinterpret_cast<const char8_t*>(id.data()),id.size())};
    if(path.is_absolute() || path.has_root_path())return false;
    for(const auto& part:path)if(part=="..")return false;
    return true;
}
void validateSampler(const ModelSampler& s) {
    if(static_cast<unsigned>(s.wrapU)>2 || static_cast<unsigned>(s.wrapV)>2 ||
        static_cast<unsigned>(s.minFilter)>5 || static_cast<unsigned>(s.magFilter)>1)invalid("invalid texture sampler.");
}
Json samplerJson(const ModelSampler& s) {
    return {{"wrapU",static_cast<int>(s.wrapU)},{"wrapV",static_cast<int>(s.wrapV)},
        {"minFilter",static_cast<int>(s.minFilter)},{"magFilter",static_cast<int>(s.magFilter)}};
}
ModelSampler readSampler(const Json& j) {
    ModelSampler s{static_cast<ModelWrap>(j.at("wrapU").get<int>()),static_cast<ModelWrap>(j.at("wrapV").get<int>()),
        static_cast<ModelFilter>(j.at("minFilter").get<int>()),static_cast<ModelFilter>(j.at("magFilter").get<int>())};
    validateSampler(s);return s;
}
Json valueJson(const MaterialValue& v) { return {{"type",static_cast<int>(v.type)},{"value",v.value}}; }
MaterialValue readValue(const Json& j) { return {static_cast<MaterialValueType>(j.at("type").get<int>()),j.at("value").get<Vec4>()}; }
Json nodeJson(const MaterialNode& n,bool layout) {
    Json j{{"id",n.id},{"kind",static_cast<int>(n.kind)}};
    if(layout){j["name"]=n.name;j["position"]=n.position;}
    switch(n.kind) {
    case MaterialNodeKind::Output: {
        const auto& o=n.output;
        j["output"]={{"shading",static_cast<int>(o.shading)},{"blend",static_cast<int>(o.blend)},
            {"baseColor",o.baseColor},{"emission",o.emission},{"roughness",o.roughness},{"metallic",o.metallic},
            {"occlusion",o.occlusion},{"opacity",o.opacity},{"opacityMask",o.opacityMask},{"specular",o.specular},{"occlusionStrength",o.occlusionStrength},
            {"alphaCutoff",o.alphaCutoff},{"doubleSided",o.doubleSided},{"receiveFog",o.receiveFog}};break;
    }
    case MaterialNodeKind::UV:
        j["uv"]={{"mode",static_cast<int>(n.uv.mode)},{"scale",n.uv.scale},{"offset",n.uv.offset},
            {"rotation",n.uv.rotation}};
        if(layout)j["uv"]["scaleLinked"]=n.uv.scaleLinked;
        break;
    case MaterialNodeKind::Texture2D:
        j["texture"]={{"image",canonicalAssetId(n.texture.image.id)},{"role",static_cast<int>(n.texture.role)},
            {"sampler",samplerJson(n.texture.sampler)},{"normalStrength",n.texture.normalStrength}};break;
    case MaterialNodeKind::Scalar:
        j["scalar"]=n.scalar;if(layout)j["range"]=n.scalarRange;break;
    case MaterialNodeKind::Color:j["color"]=n.color;break;
    case MaterialNodeKind::Vector2:j["vector2"]=n.vector2;break;
    case MaterialNodeKind::Clamp:j["left"]=valueJson(n.left);break;
    case MaterialNodeKind::Desaturation:case MaterialNodeKind::Contrast:case MaterialNodeKind::ContrastRGB:case MaterialNodeKind::HueShift:
    case MaterialNodeKind::Multiply:case MaterialNodeKind::Add:case MaterialNodeKind::Power:
        j["left"]=valueJson(n.left);j["right"]=valueJson(n.right);break;
    case MaterialNodeKind::Lerp:j["left"]=valueJson(n.left);j["right"]=valueJson(n.right);j["alpha"]=valueJson(n.alpha);break;
    case MaterialNodeKind::Time:j["time"]={{"ignorePause",n.time.ignorePause},{"periodEnabled",n.time.periodEnabled},{"period",n.time.period}};break;
    case MaterialNodeKind::Panner:j["panner"]={{"speed",n.panner.speed},{"fractionalPart",n.panner.fractionalPart},{"coordinate",static_cast<int>(n.panner.coordinate)}};break;
    case MaterialNodeKind::Grunge:j["grunge"]={{"seed",n.grunge.seed},{"tiling",n.grunge.tiling},
        {"coverage",n.grunge.coverage},{"disorder",n.grunge.disorder},{"detail",n.grunge.detail},
        {"baseNoisiness",n.grunge.baseNoisiness},{"dirtSpecksOpacity",n.grunge.dirtSpecksOpacity},{"invertDirt",n.grunge.invertDirt},
        {"scratchesOpacity",n.grunge.scratchesOpacity},{"sharpen",n.grunge.sharpen},{"largeVariationIntensity",n.grunge.largeVariationIntensity},
        {"stainsOpacity",n.grunge.stainsOpacity},{"leaksOpacity",n.grunge.leaksOpacity}};break;
    case MaterialNodeKind::ParticleColor:case MaterialNodeKind::RadialGradient:break;
    }
    return j;
}
MaterialNode readNode(const Json& j,int graphVersion) {
    MaterialNode n;n.id=j.at("id").get<MaterialNodeId>();n.kind=static_cast<MaterialNodeKind>(j.at("kind").get<int>());
    n.name=j.at("name").get<std::string>();n.position=j.at("position").get<Vec2>();
    switch(n.kind) {
    case MaterialNodeKind::Output: {
        const auto& o=j.at("output");auto& v=n.output;
        v.shading=static_cast<MaterialShading>(o.at("shading").get<int>());v.blend=static_cast<MaterialBlend>(o.at("blend").get<int>());
        v.baseColor=o.at("baseColor").get<Vec3>();v.emission=o.at("emission").get<Vec3>();
        v.roughness=o.at("roughness").get<float>();v.metallic=o.at("metallic").get<float>();
        v.occlusion=o.at("occlusion").get<float>();v.opacity=o.at("opacity").get<float>();
        v.opacityMask=o.value("opacityMask",1.f);v.specular=o.value("specular",.5f);
        v.occlusionStrength=o.at("occlusionStrength").get<float>();v.alphaCutoff=o.at("alphaCutoff").get<float>();
        v.doubleSided=o.at("doubleSided").get<bool>();v.receiveFog=o.value("receiveFog",true);break;
    }
    case MaterialNodeKind::UV: {
        const auto& u=j.at("uv");n.uv.mode=static_cast<MaterialUvMode>(u.at("mode").get<int>());
        n.uv.scale=u.at("scale").get<Vec3>();n.uv.offset=u.at("offset").get<Vec3>();n.uv.rotation=u.at("rotation").get<Vec3>();
        n.uv.scaleLinked=u.at("scaleLinked").get<bool>();break;
    }
    case MaterialNodeKind::Texture2D: {
        const auto& t=j.at("texture");const auto image=t.at("image").get<std::string>();
        if(!validImage(image))invalid("invalid Texture 2D image reference.");n.texture.image.id=canonicalAssetId(image);
        n.texture.role=static_cast<MaterialTextureRole>(t.at("role").get<int>());
        n.texture.sampler=readSampler(t.at("sampler"));n.texture.normalStrength=t.at("normalStrength").get<float>();break;
    }
    case MaterialNodeKind::Scalar:n.scalar=j.at("scalar").get<float>();n.scalarRange=j.at("range").get<Vec2>();break;
    case MaterialNodeKind::Color:n.color=j.at("color").get<Vec4>();break;
    case MaterialNodeKind::Vector2:n.vector2=j.at("vector2").get<Vec2>();break;
    case MaterialNodeKind::Clamp:n.left=readValue(j.at("left"));break;
    case MaterialNodeKind::Desaturation:case MaterialNodeKind::Contrast:case MaterialNodeKind::ContrastRGB:case MaterialNodeKind::HueShift:
    case MaterialNodeKind::Multiply:case MaterialNodeKind::Add:case MaterialNodeKind::Power:
        n.left=readValue(j.at("left"));n.right=readValue(j.at("right"));break;
    case MaterialNodeKind::Lerp:n.left=readValue(j.at("left"));n.right=readValue(j.at("right"));n.alpha=readValue(j.at("alpha"));break;
    case MaterialNodeKind::Time: {
        const auto& t=j.at("time");n.time={t.at("ignorePause").get<bool>(),t.at("periodEnabled").get<bool>(),t.at("period").get<float>()};break;
    }
    case MaterialNodeKind::Panner: {
        const auto& p=j.at("panner");n.panner={p.at("speed").get<Vec2>(),p.at("fractionalPart").get<bool>(),static_cast<MaterialUvMode>(p.at("coordinate").get<int>())};break;
    }
    case MaterialNodeKind::Grunge: {
        const auto& g=j.at("grunge");
        const auto integer=[&](const char* key,int minimum,int maximum) {
            const auto& value=g.at(key);if(!value.is_number_integer())invalid("Grunge integer controls must be integers.");
            const auto number=value.get<std::int64_t>();
            if(number<minimum||number>maximum)invalid("Grunge integer setting is outside its supported range.");
            return static_cast<int>(number);
        };
        n.grunge={integer("seed",0,65535),integer("tiling",1,64),
            g.at("coverage").get<float>(),g.at("disorder").get<float>(),g.at("detail").get<float>()};
        n.grunge.baseNoisiness=g.value("baseNoisiness",n.grunge.baseNoisiness);
        n.grunge.dirtSpecksOpacity=g.value("dirtSpecksOpacity",n.grunge.dirtSpecksOpacity);
        n.grunge.invertDirt=g.value("invertDirt",n.grunge.invertDirt);
        n.grunge.scratchesOpacity=g.value("scratchesOpacity",n.grunge.scratchesOpacity);
        n.grunge.sharpen=g.value("sharpen",n.grunge.sharpen);
        n.grunge.largeVariationIntensity=g.value("largeVariationIntensity",n.grunge.largeVariationIntensity);
        if(graphVersion<7) {
            const int formerStyle=integer("style",0,3);
            n.grunge.stainsOpacity=formerStyle==1?1.f:formerStyle==0?.35f:0.f;
            n.grunge.leaksOpacity=formerStyle==3?1.f:0.f;
        } else {
            n.grunge.stainsOpacity=g.value("stainsOpacity",n.grunge.stainsOpacity);
            n.grunge.leaksOpacity=g.value("leaksOpacity",n.grunge.leaksOpacity);
        }
        break;
    }
    case MaterialNodeKind::ParticleColor:case MaterialNodeKind::RadialGradient:break;
    default:invalid("unknown node kind.");
    }
    return n;
}
using TypeCache=std::map<std::pair<MaterialNodeId,MaterialPin>,MaterialValueType>;
MaterialValueType outputType(const MaterialGraph& graph,MaterialNodeId id,MaterialPin pin,std::unordered_set<MaterialNodeId>& visiting,TypeCache& cache) {
    const auto key=std::pair{id,pin};
    if(const auto found=cache.find(key);found!=cache.end())return found->second;
    const auto* node=findMaterialNode(graph,id);if(!node)invalid("link refers to a missing source node.");
    if(!visiting.insert(id).second)invalid("cycles are not supported.");
    struct Pop {std::unordered_set<MaterialNodeId>& set;MaterialNodeId id;~Pop(){set.erase(id);}} pop{visiting,id};
    const auto type=[&]()->MaterialValueType {
    switch(node->kind) {
    case MaterialNodeKind::UV:case MaterialNodeKind::Panner:if(pin==MaterialPin::UV)return MaterialValueType::Mapping;break;
    case MaterialNodeKind::Time:
    case MaterialNodeKind::Scalar:if(pin==MaterialPin::Value)return MaterialValueType::Scalar;break;
    case MaterialNodeKind::Clamp:
    case MaterialNodeKind::RadialGradient:if(pin==MaterialPin::Value)return MaterialValueType::Scalar;break;
    case MaterialNodeKind::Contrast:case MaterialNodeKind::Grunge:
        if(pin==MaterialPin::Value)return MaterialValueType::Scalar;
        if(pin==MaterialPin::RGB)return MaterialValueType::Color;
        break;
    case MaterialNodeKind::Desaturation:case MaterialNodeKind::ContrastRGB:case MaterialNodeKind::HueShift:
        if(pin==MaterialPin::RGB)return MaterialValueType::Color;
        if(pin==MaterialPin::R||pin==MaterialPin::G||pin==MaterialPin::B)return MaterialValueType::Scalar;
        break;
    case MaterialNodeKind::Vector2:
        if(pin==MaterialPin::XY)return MaterialValueType::Vector2;
        if(pin==MaterialPin::X||pin==MaterialPin::Y)return MaterialValueType::Scalar;
        break;
    case MaterialNodeKind::Color:
    case MaterialNodeKind::ParticleColor:
    case MaterialNodeKind::Texture2D:
        if(node->kind==MaterialNodeKind::Texture2D && node->texture.role==MaterialTextureRole::Normal) {
            if(pin==MaterialPin::Normal)return MaterialValueType::Normal;
        } else {
            if(pin==MaterialPin::RGB)return MaterialValueType::Color;
            if(pin==MaterialPin::R || pin==MaterialPin::G || pin==MaterialPin::B || pin==MaterialPin::A)return MaterialValueType::Scalar;
        }
        break;
    case MaterialNodeKind::Multiply:case MaterialNodeKind::Add:case MaterialNodeKind::Lerp:case MaterialNodeKind::Power:
        if(pin==MaterialPin::Value) {
            const auto inputType=[&](MaterialPin input,const MaterialValue& fallback) {
                if(const auto* link=materialInputLink(graph,id,input))return outputType(graph,link->fromNode,link->fromPin,visiting,cache);
                return fallback.type;
            };
            const auto a=inputType(MaterialPin::Left,node->left),b=inputType(MaterialPin::Right,node->right);
            if(node->kind==MaterialNodeKind::Power) {
                if(!numeric(a)||!materialValueAssignable(b,MaterialValueType::Scalar))invalid("Power requires a numeric Base and a Scalar Exponent.");
                return a;
            }
            const auto result=combined(a,b,materialNodeKindName(node->kind));
            if(node->kind==MaterialNodeKind::Lerp) {
                const auto alpha=inputType(MaterialPin::Alpha,node->alpha);
                if(alpha!=MaterialValueType::Scalar&&alpha!=result)invalid("Lerp Alpha must be Scalar or match A/B.");
            }
            return result;
        }
        break;
    case MaterialNodeKind::Output:break;
    }
    invalid("node "+std::to_string(id)+" has no output "+materialPinName(pin)+".");
    }();
    cache.emplace(key,type);return type;
}
std::vector<MaterialNodeId> branchOrder(const MaterialGraph& graph,MaterialShading shading,MaterialBlend blend,MaterialNodeId root) {
    validateMaterialGraph(graph);std::vector<MaterialNodeId> result;std::unordered_set<MaterialNodeId> visited;
    const auto visit=[&](auto&& self,MaterialNodeId id)->void {
        if(!visited.insert(id).second)return;const auto* node=findMaterialNode(graph,id);
        if(!node)invalid("hash root refers to a missing node.");
        for(const auto& input:materialNodeInputs(graph,*node,shading,blend))if(const auto* link=materialInputLink(graph,id,input.pin))self(self,link->fromNode);
        result.push_back(id);
    };
    visit(visit,root);return result;
}
Json activeJson(const MaterialGraph& graph,MaterialShading shading,MaterialBlend blend,bool values,MaterialNodeId root=0) {
    const auto order=root?branchOrder(graph,shading,blend,root):materialGraphOrder(graph,shading,blend);
    std::unordered_map<MaterialNodeId,std::size_t> indices;
    for(std::size_t i=0;i<order.size();++i)indices[order[i]]=i;
    Json result{{"shading",static_cast<int>(shading)},{"blend",static_cast<int>(blend)},
        {"nodes",Json::array()},{"links",Json::array()}};
    for(const auto id:order) {
        const auto& node=*findMaterialNode(graph,id);
        Json j=values?nodeJson(node,false):Json{{"kind",static_cast<int>(node.kind)}};
        j["id"]=indices.at(id);
        if(!values) {
            if(node.kind==MaterialNodeKind::Texture2D)j["role"]=static_cast<int>(node.texture.role);
            if(arithmetic(node.kind)||materialNodeImageAdjustment(node.kind)){j["leftType"]=static_cast<int>(node.left.type);j["rightType"]=static_cast<int>(node.right.type);}
            if(node.kind==MaterialNodeKind::Lerp)j["alphaType"]=static_cast<int>(node.alpha.type);
        }
        result["nodes"].push_back(std::move(j));
        for(const auto& input:materialNodeInputs(graph,node,shading,blend))if(const auto* link=materialInputLink(graph,id,input.pin))
            result["links"].push_back({indices.at(link->fromNode),static_cast<int>(link->fromPin),indices.at(id),static_cast<int>(link->toPin)});
    }
    return result;
}
std::uint64_t hash(const std::string& text) {
    std::uint64_t result=14695981039346656037ull;for(const unsigned char c:text){result^=c;result*=1099511628211ull;}return result;
}
void retainDisconnectedType(MaterialGraph& graph,const MaterialLink& link) {
    auto* target=findMaterialNode(graph,link.toNode);
    if(!target||!arithmetic(target->kind))return;
    if(target->kind==MaterialNodeKind::Power&&link.toPin==MaterialPin::Right)return;
    auto* fallback=link.toPin==MaterialPin::Left?&target->left:link.toPin==MaterialPin::Right?&target->right:
        target->kind==MaterialNodeKind::Lerp&&link.toPin==MaterialPin::Alpha?&target->alpha:nullptr;
    if(!fallback)return;
    const auto type=materialOutputType(graph,link.fromNode,link.fromPin);
    if((type==MaterialValueType::Color||type==MaterialValueType::Vector2)&&fallback->type==MaterialValueType::Scalar) {
        const auto scalar=fallback->value[0];fallback->value={scalar,scalar,scalar,scalar};
    }
    fallback->type=type;
}
}

const char* materialNodeKindName(MaterialNodeKind kind) {
    switch(kind){case MaterialNodeKind::Output:return "Output";case MaterialNodeKind::UV:return "UV";
    case MaterialNodeKind::Texture2D:return "Texture 2D";case MaterialNodeKind::Scalar:return "Scalar (1D)";
    case MaterialNodeKind::Color:return "Constant";case MaterialNodeKind::Multiply:return "Multiply";case MaterialNodeKind::Vector2:return "Vector2";
    case MaterialNodeKind::Clamp:return "Clamp";case MaterialNodeKind::ParticleColor:return "Particle Color";case MaterialNodeKind::RadialGradient:return "Radial Gradient";
    case MaterialNodeKind::Add:return "Add";case MaterialNodeKind::Lerp:return "Lerp";case MaterialNodeKind::Panner:return "Panner";
    case MaterialNodeKind::Time:return "Time";case MaterialNodeKind::Power:return "Power";
    case MaterialNodeKind::Desaturation:return "Desaturation";case MaterialNodeKind::Contrast:return "Contrast";
    case MaterialNodeKind::ContrastRGB:return "ContrastRGB";case MaterialNodeKind::HueShift:return "HueShift";case MaterialNodeKind::Grunge:return "Grunge";}return "Unknown";
}
bool materialNodeImageAdjustment(MaterialNodeKind kind) {
    return kind==MaterialNodeKind::Desaturation||kind==MaterialNodeKind::Contrast||kind==MaterialNodeKind::ContrastRGB||kind==MaterialNodeKind::HueShift;
}
const char* materialPinName(MaterialPin pin) {
    switch(pin){case MaterialPin::Value:return "Value";case MaterialPin::RGB:return "RGB";case MaterialPin::R:return "R";
    case MaterialPin::G:return "G";case MaterialPin::B:return "B";case MaterialPin::A:return "A";case MaterialPin::Normal:return "Normal";
    case MaterialPin::UV:return "UV";case MaterialPin::Left:return "A";case MaterialPin::Right:return "B";
    case MaterialPin::BaseColor:return "Base Color";case MaterialPin::Roughness:return "Roughness";case MaterialPin::Metallic:return "Metallic";
    case MaterialPin::Occlusion:return "Ambient Occlusion";case MaterialPin::Emission:return "Emissive Color";case MaterialPin::Opacity:return "Opacity";
    case MaterialPin::Specular:return "Specular";case MaterialPin::OpacityMask:return "Opacity Mask";
    case MaterialPin::XY:return "XY";case MaterialPin::X:return "X";case MaterialPin::Y:return "Y";
    case MaterialPin::ScaleXY:return "Scale XY";case MaterialPin::OffsetXY:return "Offset XY";
    case MaterialPin::Alpha:return "Alpha";case MaterialPin::Time:return "Time";case MaterialPin::Speed:return "Speed";}return "Unknown";
}
const char* materialValueTypeName(MaterialValueType type) {
    switch(type){case MaterialValueType::Scalar:return "Scalar";case MaterialValueType::Color:return "Color";
    case MaterialValueType::Mapping:return "UV Mapping";case MaterialValueType::Normal:return "Normal";case MaterialValueType::Vector2:return "Vector2";}return "Unknown";
}
bool materialValueAssignable(MaterialValueType source,MaterialValueType destination) {
    return source==destination || (source==MaterialValueType::Color&&destination==MaterialValueType::Scalar);
}
MaterialNode* findMaterialNode(MaterialGraph& graph,MaterialNodeId id) {
    const auto it=std::find_if(graph.nodes.begin(),graph.nodes.end(),[&](const auto& n){return n.id==id;});return it==graph.nodes.end()?nullptr:&*it;
}
const MaterialNode* findMaterialNode(const MaterialGraph& graph,MaterialNodeId id) {
    const auto it=std::find_if(graph.nodes.begin(),graph.nodes.end(),[&](const auto& n){return n.id==id;});return it==graph.nodes.end()?nullptr:&*it;
}
MaterialNode& materialOutputNode(MaterialGraph& graph) {
    const auto it=std::find_if(graph.nodes.begin(),graph.nodes.end(),[](const auto& n){return n.kind==MaterialNodeKind::Output;});
    if(it==graph.nodes.end())invalid("an Output node is required.");return *it;
}
const MaterialNode& materialOutputNode(const MaterialGraph& graph) {
    const auto it=std::find_if(graph.nodes.begin(),graph.nodes.end(),[](const auto& n){return n.kind==MaterialNodeKind::Output;});
    if(it==graph.nodes.end())invalid("an Output node is required.");return *it;
}
const MaterialLink* materialInputLink(const MaterialGraph& graph,MaterialNodeId id,MaterialPin pin) {
    const auto it=std::find_if(graph.links.begin(),graph.links.end(),[&](const auto& l){return l.toNode==id&&l.toPin==pin;});return it==graph.links.end()?nullptr:&*it;
}
std::vector<MaterialPinDefinition> materialOutputInputs(MaterialShading shading,MaterialBlend blend,bool all) {
    std::vector<MaterialPinDefinition> result{{MaterialPin::BaseColor,shading==MaterialShading::Unlit?"Emissive Color":"Base Color",MaterialValueType::Color}};
    if(all||shading==MaterialShading::StandardPbr) {
        result.push_back({MaterialPin::Roughness,"Roughness",MaterialValueType::Scalar});
        result.push_back({MaterialPin::Metallic,"Metallic",MaterialValueType::Scalar});
        result.push_back({MaterialPin::Specular,"Specular",MaterialValueType::Scalar});
        result.push_back({MaterialPin::Normal,"Normal",MaterialValueType::Normal});
        result.push_back({MaterialPin::Occlusion,"Ambient Occlusion",MaterialValueType::Scalar});
        result.push_back({MaterialPin::Emission,"Emissive Color",MaterialValueType::Color});
    }
    if(all||blend==MaterialBlend::Alpha||blend==MaterialBlend::Additive)result.push_back({MaterialPin::Opacity,"Opacity",MaterialValueType::Scalar});
    if(all||blend==MaterialBlend::Mask)result.push_back({MaterialPin::OpacityMask,"Opacity Mask",MaterialValueType::Scalar});
    return result;
}
bool materialPinActive(MaterialShading shading,MaterialBlend blend,MaterialPin pin) {
    const auto inputs=materialOutputInputs(shading,blend);
    return std::any_of(inputs.begin(),inputs.end(),[&](const auto& input){return input.pin==pin;});
}
std::vector<MaterialPinDefinition> materialNodeInputs(const MaterialGraph& graph,const MaterialNode& n,MaterialShading shading,MaterialBlend blend,bool all) {
    if(n.kind==MaterialNodeKind::Output)return materialOutputInputs(shading,blend,all);
    if(n.kind==MaterialNodeKind::Texture2D||n.kind==MaterialNodeKind::RadialGradient||n.kind==MaterialNodeKind::Grunge)return {{MaterialPin::UV,"UV",MaterialValueType::Mapping}};
    if(n.kind==MaterialNodeKind::Clamp)return {{MaterialPin::Left,"Value",MaterialValueType::Scalar}};
    if(materialNodeImageAdjustment(n.kind)) {
        const auto* link=materialInputLink(graph,n.id,MaterialPin::Left);
        return {{MaterialPin::Left,"Input",link?materialOutputType(graph,link->fromNode,link->fromPin):n.left.type},
            {MaterialPin::Right,n.kind==MaterialNodeKind::Desaturation?"Saturation":n.kind==MaterialNodeKind::HueShift?"Degrees":"Contrast",MaterialValueType::Scalar}};
    }
    if(n.kind==MaterialNodeKind::UV)return {{MaterialPin::ScaleXY,"Scale XY",MaterialValueType::Vector2},{MaterialPin::OffsetXY,"Offset XY",MaterialValueType::Vector2}};
    if(n.kind==MaterialNodeKind::Panner)return {{MaterialPin::UV,"Coordinate",MaterialValueType::Mapping},
        {MaterialPin::Time,"Time",MaterialValueType::Scalar},{MaterialPin::Speed,"Speed",MaterialValueType::Vector2}};
    if(arithmetic(n.kind)) {
        const auto type=[&](MaterialPin pin,const MaterialValue& fallback){const auto* l=materialInputLink(graph,n.id,pin);return l?materialOutputType(graph,l->fromNode,l->fromPin):fallback.type;};
        if(n.kind==MaterialNodeKind::Power)return {{MaterialPin::Left,"Base",type(MaterialPin::Left,n.left)},{MaterialPin::Right,"Exponent",MaterialValueType::Scalar}};
        auto result=std::vector<MaterialPinDefinition>{{MaterialPin::Left,"A",type(MaterialPin::Left,n.left)},{MaterialPin::Right,"B",type(MaterialPin::Right,n.right)}};
        if(n.kind==MaterialNodeKind::Lerp)result.push_back({MaterialPin::Alpha,"Alpha",type(MaterialPin::Alpha,n.alpha)});
        return result;
    }
    return {};
}
std::vector<MaterialPinDefinition> materialNodeOutputs(const MaterialGraph& graph,const MaterialNode& n) {
    if(n.kind==MaterialNodeKind::Output)return {};
    if(n.kind==MaterialNodeKind::UV||n.kind==MaterialNodeKind::Panner)return {{MaterialPin::UV,"UV",MaterialValueType::Mapping}};
    if(n.kind==MaterialNodeKind::Contrast||n.kind==MaterialNodeKind::Grunge)return {{MaterialPin::Value,"Value",MaterialValueType::Scalar},{MaterialPin::RGB,"RGB",MaterialValueType::Color}};
    if(materialNodeImageAdjustment(n.kind))return {{MaterialPin::RGB,"RGB",MaterialValueType::Color},{MaterialPin::R,"R",MaterialValueType::Scalar},
        {MaterialPin::G,"G",MaterialValueType::Scalar},{MaterialPin::B,"B",MaterialValueType::Scalar}};
    if(n.kind==MaterialNodeKind::Vector2)return {{MaterialPin::XY,"XY",MaterialValueType::Vector2},{MaterialPin::X,"X",MaterialValueType::Scalar},{MaterialPin::Y,"Y",MaterialValueType::Scalar}};
    if(n.kind==MaterialNodeKind::Scalar||arithmetic(n.kind)||n.kind==MaterialNodeKind::Time||n.kind==MaterialNodeKind::RadialGradient||n.kind==MaterialNodeKind::Clamp)return {{MaterialPin::Value,"Value",materialOutputType(graph,n.id,MaterialPin::Value)}};
    if(n.kind==MaterialNodeKind::Texture2D&&n.texture.role==MaterialTextureRole::Normal)return {{MaterialPin::Normal,"Normal",MaterialValueType::Normal}};
    return {{MaterialPin::RGB,"RGB",MaterialValueType::Color},{MaterialPin::R,"R",MaterialValueType::Scalar},
        {MaterialPin::G,"G",MaterialValueType::Scalar},{MaterialPin::B,"B",MaterialValueType::Scalar},{MaterialPin::A,"A",MaterialValueType::Scalar}};
}
MaterialValueType materialOutputType(const MaterialGraph& graph,MaterialNodeId id,MaterialPin pin) {
    std::unordered_set<MaterialNodeId> visiting;TypeCache cache;return outputType(graph,id,pin,visiting,cache);
}
MaterialValue materialInputDefault(const MaterialNode& n,MaterialPin pin) {
    if(n.kind==MaterialNodeKind::Clamp&&pin==MaterialPin::Left)return n.left;
    if(materialNodeImageAdjustment(n.kind)){if(pin==MaterialPin::Left)return n.left;if(pin==MaterialPin::Right)return n.right;}
    if(arithmetic(n.kind)){if(pin==MaterialPin::Left)return n.left;if(pin==MaterialPin::Right)return n.right;if(n.kind==MaterialNodeKind::Lerp&&pin==MaterialPin::Alpha)return n.alpha;}
    if(n.kind==MaterialNodeKind::Panner) {
        if(pin==MaterialPin::UV)return {MaterialValueType::Mapping,{}};
        if(pin==MaterialPin::Time)return {MaterialValueType::Scalar,{0,0,0,0}};
        if(pin==MaterialPin::Speed)return {MaterialValueType::Vector2,{n.panner.speed[0],n.panner.speed[1],0,0}};
    }
    if((n.kind==MaterialNodeKind::Texture2D||n.kind==MaterialNodeKind::RadialGradient||n.kind==MaterialNodeKind::Grunge)&&pin==MaterialPin::UV)return {MaterialValueType::Mapping,{}};
    if(n.kind==MaterialNodeKind::UV) {
        if(pin==MaterialPin::ScaleXY)return {MaterialValueType::Vector2,{n.uv.scale[0],n.uv.scale[1],0,0}};
        if(pin==MaterialPin::OffsetXY)return {MaterialValueType::Vector2,{n.uv.offset[0],n.uv.offset[1],0,0}};
    }
    if(n.kind==MaterialNodeKind::Output) {
        const auto& o=n.output;
        if(pin==MaterialPin::BaseColor)return {MaterialValueType::Color,{o.baseColor[0],o.baseColor[1],o.baseColor[2],1}};
        if(pin==MaterialPin::Emission)return {MaterialValueType::Color,{o.emission[0],o.emission[1],o.emission[2],1}};
        if(pin==MaterialPin::Normal)return {MaterialValueType::Normal,{0,0,1,0}};
        float value{};switch(pin){case MaterialPin::Roughness:value=o.roughness;break;case MaterialPin::Metallic:value=o.metallic;break;
        case MaterialPin::Occlusion:value=o.occlusion;break;case MaterialPin::Opacity:value=o.opacity;break;
        case MaterialPin::Specular:value=o.specular;break;case MaterialPin::OpacityMask:value=o.opacityMask;break;default:invalid("unknown input default.");}
        return {MaterialValueType::Scalar,{value,value,value,value}};
    }
    invalid("node has no such input default.");
}
bool materialExpressionUniform(const MaterialGraph& graph,MaterialNodeId id,MaterialPin pin) {
    (void)materialOutputType(graph,id,pin);
    std::unordered_set<MaterialNodeId> visited;
    const auto visit=[&](auto&& self,MaterialNodeId current)->bool {
        if(!visited.insert(current).second)return true;
        const auto& n=*findMaterialNode(graph,current);
        if(n.kind==MaterialNodeKind::Scalar||n.kind==MaterialNodeKind::Color||n.kind==MaterialNodeKind::Vector2||n.kind==MaterialNodeKind::Time)return true;
        if(!arithmetic(n.kind)&&!materialNodeImageAdjustment(n.kind)&&n.kind!=MaterialNodeKind::Clamp)return false;
        const auto& settings=materialOutputNode(graph).output;
        for(const auto& input:materialNodeInputs(graph,n,settings.shading,settings.blend))
            if(const auto* link=materialInputLink(graph,current,input.pin);link&&!self(self,link->fromNode))return false;
        return true;
    };
    return visit(visit,id);
}
bool materialExpressionTimeDependent(const MaterialGraph& graph,MaterialNodeId id,MaterialPin pin) {
    (void)materialOutputType(graph,id,pin);
    std::unordered_set<MaterialNodeId> visited;
    const auto& settings=materialOutputNode(graph).output;
    const auto visit=[&](auto&& self,MaterialNodeId current)->bool {
        if(!visited.insert(current).second)return false;
        const auto& node=*findMaterialNode(graph,current);
        if(node.kind==MaterialNodeKind::Time)return true;
        if(node.kind==MaterialNodeKind::Panner&&!materialInputLink(graph,current,MaterialPin::Time))return true;
        for(const auto& input:materialNodeInputs(graph,node,settings.shading,settings.blend))
            if(const auto* link=materialInputLink(graph,current,input.pin);link&&self(self,link->fromNode))return true;
        return false;
    };
    return visit(visit,id);
}
MaterialValue evaluateMaterialUniform(const MaterialGraph& graph,MaterialNodeId id,MaterialPin pin,MaterialEvaluationContext context) {
    if(!materialExpressionUniform(graph,id,pin))invalid("node "+std::to_string(id)+" is not a uniform numeric expression.");
    std::map<std::pair<MaterialNodeId,MaterialPin>,MaterialValue> cache;
    const auto evaluate=[&](auto&& self,MaterialNodeId current,MaterialPin output)->MaterialValue {
        const auto key=std::pair{current,output};if(const auto found=cache.find(key);found!=cache.end())return found->second;
        const auto& n=*findMaterialNode(graph,current);MaterialValue result;result.type=materialOutputType(graph,current,output);
        if(n.kind==MaterialNodeKind::Time) {
            double seconds=n.time.ignorePause?context.realSeconds:context.gameSeconds;
            if(!std::isfinite(seconds))invalid("Time requires finite frame seconds.");
            if(n.time.periodEnabled)seconds=n.time.period>0?seconds-std::floor(seconds/n.time.period)*n.time.period:0;
            const auto value=static_cast<float>(seconds);result.value={value,value,value,value};
        } else if(n.kind==MaterialNodeKind::Scalar)result.value={n.scalar,n.scalar,n.scalar,n.scalar};
        else if(n.kind==MaterialNodeKind::Vector2) {
            if(output==MaterialPin::XY)result.value={n.vector2[0],n.vector2[1],0,0};
            else {const float value=n.vector2[output==MaterialPin::X?0:1];result.value={value,value,value,value};}
        } else if(n.kind==MaterialNodeKind::Color) {
            if(output==MaterialPin::RGB)result.value=n.color;
            else {const float value=n.color[output==MaterialPin::R?0:output==MaterialPin::G?1:output==MaterialPin::B?2:3];result.value={value,value,value,value};}
        } else {
            const auto input=[&](MaterialPin inputPin,const MaterialValue& fallback) {
                const auto* link=materialInputLink(graph,current,inputPin);return link?self(self,link->fromNode,link->fromPin):fallback;
            };
            const auto left=input(MaterialPin::Left,n.left);
            if(materialNodeImageAdjustment(n.kind)) {
                const auto right=input(MaterialPin::Right,n.right);
                const float factor=right.value[0];
                Vec3 rgb=left.type==MaterialValueType::Scalar?Vec3{left.value[0],left.value[0],left.value[0]}:Vec3{left.value[0],left.value[1],left.value[2]};
                const float luminance=rgb[0]*.2126f+rgb[1]*.7152f+rgb[2]*.0722f;
                if(n.kind==MaterialNodeKind::Desaturation)for(auto& channel:rgb)channel=luminance+(channel-luminance)*factor;
                else if(n.kind==MaterialNodeKind::Contrast) {const float value=(luminance-.5f)*factor+.5f;rgb={value,value,value};}
                else if(n.kind==MaterialNodeKind::ContrastRGB)for(auto& channel:rgb)channel=(channel-.5f)*factor+.5f;
                else rgb=shiftedHue(rgb,factor);
                if(output==MaterialPin::RGB)result.value={rgb[0],rgb[1],rgb[2],1};
                else {const float value=rgb[output==MaterialPin::G?1:output==MaterialPin::B?2:0];result.value={value,value,value,value};}
            } else if(n.kind==MaterialNodeKind::Clamp) {
                const auto value=std::clamp(left.value[0],0.f,1.f);result.value={value,value,value,value};
            }else {
                const auto right=input(MaterialPin::Right,n.right);
                const auto alpha=n.kind==MaterialNodeKind::Lerp?input(MaterialPin::Alpha,n.alpha):MaterialValue{};
                for(std::size_t i=0;i<4;++i) {
                    const auto a=left.value[left.type==MaterialValueType::Scalar?0:i],b=right.value[right.type==MaterialValueType::Scalar||n.kind==MaterialNodeKind::Power?0:i];
                    if(n.kind==MaterialNodeKind::Add)result.value[i]=a+b;
                    else if(n.kind==MaterialNodeKind::Lerp){const auto t=alpha.value[alpha.type==MaterialValueType::Scalar?0:i];result.value[i]=a+(b-a)*t;}
                    else if(n.kind==MaterialNodeKind::Power)result.value[i]=a<=2.980233e-8f?0.f:std::pow(a,b);
                    else result.value[i]=a*b;
                }
            }
        }
        if(!math::finite(result.value))invalid("node "+std::to_string(current)+" uniform expression overflows its supported range.");
        cache.emplace(key,result);return result;
    };
    return evaluate(evaluate,id,pin);
}
MaterialUvMapping resolveMaterialUvMapping(const MaterialGraph& graph,MaterialNodeId id,MaterialEvaluationContext context) {
    const auto* node=findMaterialNode(graph,id);
    if(!node||(node->kind!=MaterialNodeKind::UV&&node->kind!=MaterialNodeKind::Panner))invalid("mapping resolver requires a UV or Panner node.");
    if(node->kind==MaterialNodeKind::Panner) {
        MaterialUvMapping result;result.mode=node->panner.coordinate;
        if(const auto* link=materialInputLink(graph,id,MaterialPin::UV))result=resolveMaterialUvMapping(graph,link->fromNode,context);
        double seconds=context.gameSeconds;
        if(const auto* link=materialInputLink(graph,id,MaterialPin::Time))seconds=evaluateMaterialUniform(graph,link->fromNode,link->fromPin,context).value[0];
        auto speed=node->panner.speed;
        if(const auto* link=materialInputLink(graph,id,MaterialPin::Speed)) {
            const auto value=evaluateMaterialUniform(graph,link->fromNode,link->fromPin,context);
            if(value.type!=MaterialValueType::Vector2)invalid("Panner Speed requires a uniform Vector2 expression.");
            speed={value.value[0],value.value[1]};
        }
        if(!std::isfinite(seconds))invalid("Panner requires finite frame seconds.");
        for(unsigned axis=0;axis<2;++axis) {
            double displacement=seconds*speed[axis];
            if(node->panner.fractionalPart)displacement-=std::floor(displacement);
            result.offset[axis]=static_cast<float>(result.offset[axis]+displacement);
        }
        if(!math::finite(result.offset))invalid("Panner mapping overflows its supported range.");
        return result;
    }
    auto result=node->uv;
    for(const auto input:{MaterialPin::ScaleXY,MaterialPin::OffsetXY})if(const auto* link=materialInputLink(graph,id,input)) {
        const auto value=evaluateMaterialUniform(graph,link->fromNode,link->fromPin,context);
        if(value.type!=MaterialValueType::Vector2)invalid("UV transforms require a Vector2 expression.");
        auto& target=input==MaterialPin::ScaleXY?result.scale:result.offset;
        target[0]=value.value[0];target[1]=value.value[1];
    }
    return result;
}
MaterialUvMapping materialTextureMapping(const MaterialGraph& graph,MaterialNodeId id,MaterialEvaluationContext context) {
    const auto* node=findMaterialNode(graph,id);
    if(!node||(node->kind!=MaterialNodeKind::Texture2D&&node->kind!=MaterialNodeKind::Grunge))invalid("texture mapping resolver requires a Texture 2D or Grunge node.");
    const auto* input=materialInputLink(graph,id,MaterialPin::UV);
    return input?resolveMaterialUvMapping(graph,input->fromNode,context):MaterialUvMapping{};
}
void migrateLegacyParticleGraph(MaterialGraph& graph,bool softDisc) {
    validateMaterialGraph(graph);
    auto candidate=graph;const auto root=materialOutputNode(candidate);
    const auto coveragePin=root.output.blend==MaterialBlend::Mask?MaterialPin::OpacityMask:MaterialPin::Opacity;
    std::unordered_set<MaterialNodeId> seen;
    const auto textured=[&](auto&& self,MaterialNodeId id)->bool {
        if(!seen.insert(id).second)return false;
        const auto& n=*findMaterialNode(candidate,id);
        if(n.kind==MaterialNodeKind::Texture2D)return !n.texture.image.empty();
        if(n.kind==MaterialNodeKind::Multiply)for(const auto pin:{MaterialPin::Left,MaterialPin::Right})
            if(const auto* link=materialInputLink(candidate,id,pin);link&&self(self,link->fromNode))return true;
        return false;
    };
    const auto* opacity=materialInputLink(candidate,root.id,coveragePin);
    const bool radial=softDisc&&!(opacity&&textured(textured,opacity->fromNode));
    const auto link=[&](MaterialNodeId from,MaterialPin fromPin,MaterialNodeId to,MaterialPin toPin) {
        if(candidate.links.size()>=MaterialGraph::MaximumLinks)invalid("legacy particle conversion exceeds 288 links.");
        if(!candidate.nextLinkId||candidate.nextLinkId==std::numeric_limits<MaterialLinkId>::max())invalid("link identity space is exhausted.");
        candidate.links.push_back({candidate.nextLinkId++,from,to,fromPin,toPin});
    };
    // The former shader clamped authored coverage before multiplying particle
    // alpha and the disc. Preserve this order even for HDR/negative expressions.
    const auto bounded=addMaterialNode(candidate,MaterialNodeKind::Clamp,{root.position[0]-620,root.position[1]+100}).id;
    findMaterialNode(candidate,bounded)->left=materialInputDefault(root,coveragePin);
    for(auto& previous:candidate.links)if(previous.toNode==root.id&&previous.toPin==coveragePin) {
        previous.toNode=bounded;previous.toPin=MaterialPin::Left;break;
    }
    link(bounded,MaterialPin::Value,root.id,coveragePin);
    const auto particle=addMaterialNode(candidate,MaterialNodeKind::ParticleColor,{root.position[0]-620,root.position[1]+240}).id;
    const auto multiply=[&](MaterialPin input,MaterialNodeId source,MaterialPin sourcePin,Vec2 position,const char* name) {
        auto& product=addMaterialNode(candidate,MaterialNodeKind::Multiply,position);product.name=name;
        product.left=materialInputDefault(root,input);const auto productId=product.id;
        for(auto& previous:candidate.links)if(previous.toNode==root.id&&previous.toPin==input) {
            previous.toNode=productId;previous.toPin=MaterialPin::Left;break;
        }
        link(source,sourcePin,productId,MaterialPin::Right);link(productId,MaterialPin::Value,root.id,input);
    };
    multiply(MaterialPin::BaseColor,particle,MaterialPin::RGB,{root.position[0]-280,root.position[1]-100},"Particle color modulation");
    multiply(coveragePin,particle,MaterialPin::A,{root.position[0]-280,root.position[1]+170},"Particle opacity modulation");
    if(radial) {
        const auto gradient=addMaterialNode(candidate,MaterialNodeKind::RadialGradient,{root.position[0]-620,root.position[1]+430}).id;
        multiply(coveragePin,gradient,MaterialPin::Value,{root.position[0]-80,root.position[1]+350},"Soft particle coverage");
    }
    validateMaterialGraph(candidate);graph=std::move(candidate);
}
MaterialNode& addMaterialNode(MaterialGraph& graph,MaterialNodeKind kind,Vec2 position) {
    if(graph.nodes.size()>=MaterialGraph::MaximumNodes)invalid("at most 144 nodes are supported.");
    if(kind==MaterialNodeKind::Output&&std::any_of(graph.nodes.begin(),graph.nodes.end(),[](const auto& n){return n.kind==MaterialNodeKind::Output;}))invalid("only one Output node is allowed.");
    if(!graph.nextNodeId||graph.nextNodeId==std::numeric_limits<MaterialNodeId>::max())invalid("node identity space is exhausted.");
    MaterialNode node;node.id=graph.nextNodeId++;node.kind=kind;node.name=materialNodeKindName(kind);node.position=position;
    if(kind==MaterialNodeKind::Add||kind==MaterialNodeKind::Lerp||kind==MaterialNodeKind::Power)node.left.value={0,0,0,0};
    if(kind==MaterialNodeKind::Power)node.right.value={2,2,2,2};
    if(materialNodeImageAdjustment(kind))node.left={MaterialValueType::Color,{1,1,1,1}};
    if(kind==MaterialNodeKind::HueShift)node.right.value={0,0,0,0};
    graph.nodes.push_back(std::move(node));return graph.nodes.back();
}
bool removeMaterialNode(MaterialGraph& graph,MaterialNodeId id) {
    const auto* n=findMaterialNode(graph,id);if(!n||n->kind==MaterialNodeKind::Output)return false;
    for(const auto& link:graph.links)if(link.fromNode==id)retainDisconnectedType(graph,link);
    std::erase_if(graph.links,[&](const auto& l){return l.fromNode==id||l.toNode==id;});
    std::erase_if(graph.nodes,[&](const auto& value){return value.id==id;});return true;
}
bool removeMaterialLink(MaterialGraph& graph,MaterialLinkId id) {
    const auto it=std::find_if(graph.links.begin(),graph.links.end(),[&](const auto& l){return l.id==id;});if(it==graph.links.end())return false;
    retainDisconnectedType(graph,*it);graph.links.erase(it);return true;
}
MaterialLinkId connectMaterialNodes(MaterialGraph& graph,MaterialNodeId from,MaterialPin output,MaterialNodeId to,MaterialPin input) {
    if(const auto* target=findMaterialNode(graph,to);target&&target->kind==MaterialNodeKind::Output&&
        !materialPinActive(target->output.shading,target->output.blend,input))invalid("node "+std::to_string(to)+" input "+materialPinName(input)+" is inactive for this material.");
    auto candidate=graph;
    std::erase_if(candidate.links,[&](const auto& l){return l.toNode==to&&l.toPin==input;});
    if(candidate.links.size()>=MaterialGraph::MaximumLinks)invalid("at most 288 links are supported.");
    if(!candidate.nextLinkId||candidate.nextLinkId==std::numeric_limits<MaterialLinkId>::max())invalid("link identity space is exhausted.");
    const auto id=candidate.nextLinkId++;candidate.links.push_back({id,from,to,output,input});
    validateMaterialGraph(candidate);graph=std::move(candidate);return id;
}
void validateMaterialGraph(const MaterialGraph& graph) {
    if(graph.nodes.empty()||graph.nodes.size()>MaterialGraph::MaximumNodes||graph.links.size()>MaterialGraph::MaximumLinks)invalid("node or link count exceeds its supported bounds.");
    std::unordered_set<MaterialNodeId> ids;MaterialNodeId maximum{};std::size_t outputs{};
    for(const auto& n:graph.nodes) {
        if(!n.id||!ids.insert(n.id).second)invalid("node identities must be nonzero and unique.");maximum=std::max(maximum,n.id);
        if(!math::finite(n.position)||n.name.size()>1024)invalid("invalid node position or name.");
        switch(n.kind) {
        case MaterialNodeKind::Output: {
            ++outputs;const auto& o=n.output;
            if(static_cast<unsigned>(o.shading)>1||static_cast<unsigned>(o.blend)>3)invalid("unsupported shader model or blend mode.");
            if(!math::finite(o.baseColor)||!math::finite(o.emission)||!math::finite(o.roughness)||!math::finite(o.metallic)||
                !math::finite(o.occlusion)||!math::finite(o.opacity)||!math::finite(o.opacityMask)||!math::finite(o.specular)||!math::finite(o.occlusionStrength)||!math::finite(o.alphaCutoff))invalid("nonfinite Output default.");
            if(o.occlusionStrength<0||o.occlusionStrength>1||o.alphaCutoff<0)invalid("invalid Output strength or cutoff.");break;
        }
        case MaterialNodeKind::UV:
            if(static_cast<unsigned>(n.uv.mode)>3||!math::finite(n.uv.scale)||!math::finite(n.uv.offset)||!math::finite(n.uv.rotation))invalid("invalid UV mapping.");break;
        case MaterialNodeKind::Texture2D:
            if(static_cast<unsigned>(n.texture.role)>2||!validImage(n.texture.image.id)||!math::finite(n.texture.normalStrength))invalid("invalid Texture 2D definition.");
            validateSampler(n.texture.sampler);break;
        case MaterialNodeKind::Scalar:
            if(!math::finite(n.scalar)||!math::finite(n.scalarRange)||n.scalarRange[0]>=n.scalarRange[1])invalid("Scalar requires a finite value and an increasing slider range.");break;
        case MaterialNodeKind::Color:if(!math::finite(n.color))invalid("Color requires finite components.");break;
        case MaterialNodeKind::Vector2:if(!math::finite(n.vector2))invalid("Vector2 requires finite components.");break;
        case MaterialNodeKind::ParticleColor:case MaterialNodeKind::RadialGradient:break;
        case MaterialNodeKind::Clamp:
            if(n.left.type!=MaterialValueType::Scalar||!math::finite(n.left.value))invalid("Clamp requires a finite Scalar input default.");break;
        case MaterialNodeKind::Desaturation:case MaterialNodeKind::Contrast:case MaterialNodeKind::ContrastRGB:case MaterialNodeKind::HueShift:
            if(!colorInput(n.left.type)||n.right.type!=MaterialValueType::Scalar||!math::finite(n.left.value)||!math::finite(n.right.value))
                invalid("Image adjustments require a finite Scalar/Color Input and Scalar adjustment default.");break;
        case MaterialNodeKind::Grunge: {
            const auto& g=n.grunge;
            const auto unit=[](float v){return math::finite(v)&&v>=0&&v<=1;};
            if(g.seed<0||g.seed>65535||g.tiling<1||g.tiling>64||
                !unit(g.coverage)||!unit(g.disorder)||!unit(g.detail)||!unit(g.baseNoisiness)||!unit(g.dirtSpecksOpacity)||
                !unit(g.scratchesOpacity)||!unit(g.sharpen)||!unit(g.largeVariationIntensity)||!unit(g.stainsOpacity)||!unit(g.leaksOpacity))
                invalid("Grunge requires Seed 0-65535, Tiling 1-64 and finite controls in 0-1.");break;
        }
        case MaterialNodeKind::Multiply:case MaterialNodeKind::Add:case MaterialNodeKind::Lerp:case MaterialNodeKind::Power:
            if(!numeric(n.left.type)||!numeric(n.right.type)||!math::finite(n.left.value)||!math::finite(n.right.value))invalid("Arithmetic defaults must be finite Scalar, Vector2 or Color values.");
            if(n.kind==MaterialNodeKind::Power&&n.right.type!=MaterialValueType::Scalar)invalid("Power Exponent default must be Scalar.");
            if(n.kind==MaterialNodeKind::Lerp&&(!numeric(n.alpha.type)||!math::finite(n.alpha.value)))invalid("Lerp Alpha default must be finite and numeric.");break;
        case MaterialNodeKind::Time:
            if(!math::finite(n.time.period)||n.time.period<0)invalid("Time Period must be finite and nonnegative.");break;
        case MaterialNodeKind::Panner:
            if(!math::finite(n.panner.speed)||static_cast<unsigned>(n.panner.coordinate)>1)invalid("Panner requires finite Speed and UV0/UV1 coordinates.");break;
        default:invalid("unknown node kind.");
        }
    }
    if(outputs!=1)invalid("exactly one Output node is required.");
    if(!graph.nextNodeId||graph.nextNodeId<=maximum)invalid("next node identity would reuse an existing identity.");
    std::unordered_set<MaterialLinkId> linkIds;MaterialLinkId maxLink{};
    std::unordered_set<std::string> destinations;
    for(const auto& link:graph.links) {
        if(!link.id||!linkIds.insert(link.id).second)invalid("link identities must be nonzero and unique.");maxLink=std::max(maxLink,link.id);
        if(!ids.contains(link.fromNode)||!ids.contains(link.toNode))invalid("a link endpoint does not exist.");
        const auto key=std::to_string(link.toNode)+":"+std::to_string(static_cast<int>(link.toPin));
        if(!destinations.insert(key).second)invalid("an input accepts only one connection.");
    }
    if(!graph.nextLinkId||graph.nextLinkId<=maxLink)invalid("next link identity would reuse an existing identity.");
    std::unordered_set<MaterialNodeId> active,done;
    const auto visit=[&](auto&& self,MaterialNodeId id)->void {
        if(done.contains(id))return;if(!active.insert(id).second)invalid("cycles are not supported.");
        for(const auto& link:graph.links)if(link.toNode==id)self(self,link.fromNode);
        active.erase(id);done.insert(id);
    };
    for(const auto& node:graph.nodes)visit(visit,node.id);
    for(const auto& link:graph.links) {
        const auto& target=*findMaterialNode(graph,link.toNode);const auto type=materialOutputType(graph,link.fromNode,link.fromPin);
        if(materialNodeImageAdjustment(target.kind)&&link.toPin==MaterialPin::Left) {
            if(!colorInput(type))invalid("Image adjustment Input accepts only Scalar or Color.");continue;
        }
        if(arithmetic(target.kind)&&(link.toPin==MaterialPin::Left||link.toPin==MaterialPin::Right||
            (target.kind==MaterialNodeKind::Lerp&&link.toPin==MaterialPin::Alpha))&&
            !(target.kind==MaterialNodeKind::Power&&link.toPin==MaterialPin::Right)) {
            if(!numeric(type))invalid("Arithmetic accepts only Scalar, Vector2 and Color inputs.");continue;
        }
        const auto inputs=materialNodeInputs(graph,target,MaterialShading::StandardPbr,MaterialBlend::Mask,true);
        const auto pin=std::find_if(inputs.begin(),inputs.end(),[&](const auto& p){return p.pin==link.toPin;});
        if(pin==inputs.end())invalid("node "+std::to_string(target.id)+" has no input "+materialPinName(link.toPin)+".");
        if(!materialValueAssignable(type,pin->type))invalid(std::string(materialValueTypeName(type))+" cannot connect to "+materialValueTypeName(pin->type)+".");
    }
    // Validate every authored arithmetic/mapping branch, including dormant or
    // disconnected branches; upstream reconnection must not introduce warps.
    for(const auto& node:graph.nodes) {
        if(arithmetic(node.kind))(void)materialOutputType(graph,node.id,MaterialPin::Value);
        if(node.kind==MaterialNodeKind::UV||node.kind==MaterialNodeKind::Panner)(void)resolveMaterialUvMapping(graph,node.id);
    }
    std::unordered_set<MaterialNodeId> reachable;std::size_t textureBindings{};
    const auto& output=materialOutputNode(graph);
    const auto sampleCount=[&](auto&& self,MaterialNodeId id)->void {
        if(!reachable.insert(id).second)return;const auto& node=*findMaterialNode(graph,id);
        if((node.kind==MaterialNodeKind::Texture2D||node.kind==MaterialNodeKind::Grunge)&&++textureBindings>MaterialGraph::MaximumTextureBindings)
            invalid("at most 12 active Texture 2D and Grunge samplers are supported.");
        for(const auto& input:materialNodeInputs(graph,node,output.output.shading,output.output.blend))
            if(const auto* link=materialInputLink(graph,id,input.pin))self(self,link->fromNode);
    };
    sampleCount(sampleCount,output.id);
}
std::vector<MaterialNodeId> materialGraphOrder(const MaterialGraph& graph,MaterialShading shading,MaterialBlend blend) {
    return branchOrder(graph,shading,blend,materialOutputNode(graph).id);
}
std::vector<std::string> materialGraphTextureReferences(const MaterialGraph& graph) {
    std::vector<std::string> result;
    for(const auto& n:graph.nodes)if(n.kind==MaterialNodeKind::Texture2D&&!n.texture.image.empty()&&
        std::find(result.begin(),result.end(),n.texture.image.id)==result.end())result.push_back(n.texture.image.id);
    return result;
}
void rewriteMaterialGraphTextures(MaterialGraph& graph,const std::function<std::string(const std::string&,MaterialTextureRole)>& replace) {
    auto candidate=graph;for(auto& n:candidate.nodes)if(n.kind==MaterialNodeKind::Texture2D&&!n.texture.image.empty())n.texture.image.id=replace(n.texture.image.id,n.texture.role);
    validateMaterialGraph(candidate);graph=std::move(candidate);
}
std::uint64_t materialGraphTopologyHash(const MaterialGraph& graph,MaterialShading shading,MaterialBlend blend) {return hash(activeJson(graph,shading,blend,false).dump());}
std::uint64_t materialGraphValueHash(const MaterialGraph& graph,MaterialShading shading,MaterialBlend blend) {return hash(activeJson(graph,shading,blend,true).dump());}
std::uint64_t materialGraphValueHash(const MaterialGraph& graph,MaterialShading shading,MaterialBlend blend,MaterialNodeId root) {return hash(activeJson(graph,shading,blend,true,root).dump());}
std::uint64_t materialGraphLayoutHash(const MaterialGraph& graph) {
    Json j=Json::array();for(const auto& n:graph.nodes)j.push_back({n.id,n.name,n.position,n.scalarRange,n.uv.scaleLinked});return hash(j.dump());
}
std::string serializeMaterialGraph(const MaterialGraph& graph) {
    validateMaterialGraph(graph);Json j{{"version",7},{"nextNodeId",graph.nextNodeId},{"nextLinkId",graph.nextLinkId},{"nodes",Json::array()},{"links",Json::array()}};
    for(const auto& n:graph.nodes)j["nodes"].push_back(nodeJson(n,true));
    for(const auto& l:graph.links)j["links"].push_back({{"id",l.id},{"fromNode",l.fromNode},{"fromPin",static_cast<int>(l.fromPin)},
        {"toNode",l.toNode},{"toPin",static_cast<int>(l.toPin)}});
    return j.dump();
}
MaterialGraph parseMaterialGraph(const std::string& text) {
    if(text.size()>1024u*1024u)invalid("serialized graph exceeds 1 MiB.");const auto j=Json::parse(text);
    if((j.at("version")!=1&&j.at("version")!=2&&j.at("version")!=3&&j.at("version")!=4&&j.at("version")!=5&&j.at("version")!=6&&j.at("version")!=7)||!j.at("nodes").is_array()||!j.at("links").is_array()||
        j.at("nodes").size()>MaterialGraph::MaximumNodes||j.at("links").size()>MaterialGraph::MaximumLinks)invalid("unsupported graph version or counts.");
    MaterialGraph graph;graph.nextNodeId=j.at("nextNodeId").get<MaterialNodeId>();graph.nextLinkId=j.at("nextLinkId").get<MaterialLinkId>();
    for(const auto& n:j.at("nodes"))graph.nodes.push_back(readNode(n,j.at("version").get<int>()));
    if(j.at("version").get<int>()<5)for(auto& node:graph.nodes)
        if(node.kind==MaterialNodeKind::Clamp&&node.name=="Saturate")node.name="Clamp";
    for(const auto& l:j.at("links"))graph.links.push_back({l.at("id").get<MaterialLinkId>(),l.at("fromNode").get<MaterialNodeId>(),
        l.at("toNode").get<MaterialNodeId>(),static_cast<MaterialPin>(l.at("fromPin").get<int>()),static_cast<MaterialPin>(l.at("toPin").get<int>())});
    if(j.at("version")==1) {
        auto& root=materialOutputNode(graph);auto& output=root.output;
        output.specular=.5f;
        if(output.blend==MaterialBlend::Opaque||output.blend==MaterialBlend::Mask) {
            output.opacityMask=output.opacity;
            for(auto& link:graph.links)if(link.toNode==root.id&&link.toPin==MaterialPin::Opacity)link.toPin=MaterialPin::OpacityMask;
        }
    }
    if(j.at("version").get<int>()<3) {
        const auto& root=materialOutputNode(graph);
        const auto& serialized=*std::find_if(j.at("nodes").begin(),j.at("nodes").end(),[&](const auto& value){return value.at("id")==root.id;});
        const auto& output=serialized.at("output");
        const int usage=j.at("version")==2?output.at("usage").get<int>():
            root.output.shading==MaterialShading::Unlit&&(root.output.blend==MaterialBlend::Alpha||root.output.blend==MaterialBlend::Additive)?1:0;
        if(usage<0||usage>1)invalid("unsupported legacy material usage.");
        if(usage==1)migrateLegacyParticleGraph(graph,output.at("particleSoftDisc").get<bool>());
    }
    validateMaterialGraph(graph);return graph;
}
} // namespace crimson
