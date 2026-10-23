#include "assets/MaterialAsset.h"
#include "material_fixtures.h"
#include "editor/MaterialTextureDemand.h"
#include "assets/ContentStorage.h"
#include "resources/AssetId.h"
#include <crimson/renderer/forward/MaterialGraphImageBake.h>
#include <crimson/renderer/forward/PostProcess.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace crimson;
namespace fs=std::filesystem;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
template<class F> void rejects(F action,const char* message){try{action();}catch(const std::exception&){return;}throw std::runtime_error(message);}
bool near(float a,float b){return std::abs(a-b)<1e-6f;}
std::string read(const fs::path& file){std::ifstream input(file,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};}
void write(const fs::path& file,const std::string& data){fs::create_directories(file.parent_path());std::ofstream output(file,std::ios::binary);output<<data;}
struct Project {
    fs::path root=fs::temp_directory_path()/("crimson-material-graph-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~Project(){std::error_code error;const auto temp=fs::weakly_canonical(fs::temp_directory_path(),error);if(error)return;const auto target=fs::weakly_canonical(root,error);
        if(!error&&target.parent_path()==temp&&target.filename().string().starts_with("crimson-material-graph-"))fs::remove_all(target,error);}
};
MaterialGraph blank(){MaterialGraph graph;addMaterialNode(graph,MaterialNodeKind::Output);return graph;}
MaterialNodeId node(MaterialGraph& graph,MaterialNodeKind kind){return addMaterialNode(graph,kind).id;}
void materialStylizationAuthoring() {
    auto graph=blank();const auto out=materialOutputNode(graph).id;
    const auto texture=node(graph,MaterialNodeKind::Texture2D),scalar=node(graph,MaterialNodeKind::Scalar);
    const auto vector=node(graph,MaterialNodeKind::Vector2),uv=node(graph,MaterialNodeKind::UV);
    const auto normal=node(graph,MaterialNodeKind::DecodeNormal);
    const auto pixel=node(graph,MaterialNodeKind::Pixelization),poster=node(graph,MaterialNodeKind::Posterization),dither=node(graph,MaterialNodeKind::Dither);
    require(findMaterialNode(graph,pixel)->stylization.pixelSize==4&&findMaterialNode(graph,poster)->stylization.levels==8&&
        findMaterialNode(graph,dither)->stylization.levels==17&&findMaterialNode(graph,dither)->stylization.pattern==DitherPattern::Bayer4x4&&
        findMaterialNode(graph,dither)->stylization.size==1&&findMaterialNode(graph,dither)->stylization.strength==1,
        "Material stylization defaults differ from their independent post-process controls.");
    for(const auto id:{pixel,poster,dither}) {
        for(const auto [pin,type]:std::array{std::pair{MaterialPin::R,MaterialValueType::Scalar},std::pair{MaterialPin::RGB,MaterialValueType::Color},std::pair{MaterialPin::RGBA,MaterialValueType::Color4}}) {
            connectMaterialNodes(graph,texture,pin,id,MaterialPin::Left);
            require(materialOutputType(graph,id,MaterialPin::Value)==type,"Stylization changed the connected image family.");
        }
        const auto before=serializeMaterialGraph(graph);
        for(const auto [source,pin]:std::array{std::pair{vector,MaterialPin::XY},std::pair{uv,MaterialPin::UV},std::pair{normal,MaterialPin::Normal}})
            require(!materialCanConnect(graph,source,pin,id,MaterialPin::Left),"Stylization accepted a Vector2, Mapping or decoded Normal image.");
        require(serializeMaterialGraph(graph)==before,"A rejected stylization connection changed authored data.");
        connectMaterialNodes(graph,scalar,MaterialPin::Value,id,MaterialPin::Left);
    }
    const auto time=node(graph,MaterialNodeKind::Time);
    for(const auto id:{pixel,dither})require(!materialCanConnect(graph,time,MaterialPin::Value,id,MaterialPin::Left),
        "A static stylization image accepted a runtime Time source.");
    require(materialCanConnect(graph,time,MaterialPin::Value,poster,MaterialPin::Left),"Direct Posterization rejected a supported runtime uniform.");
    auto* posterNode=findMaterialNode(graph,poster);posterNode->stylization.levels=5;
    findMaterialNode(graph,scalar)->scalar=.21404114f;
    require(materialExpressionUniform(graph,poster,MaterialPin::Value),"A constant Posterization was not retained as a uniform expression.");
    require(near(evaluateMaterialUniform(graph,poster,MaterialPin::Value).value[0],post_processing::posterize({.21404114f,.21404114f,.21404114f},5)[0]),
        "Uniform Posterization differs from the existing perceptual post-process equation.");
    const auto color=node(graph,MaterialNodeKind::Color);findMaterialNode(graph,color)->color={-.5f,4.f,.21404114f,.37f};
    connectMaterialNodes(graph,color,MaterialPin::RGBA,poster,MaterialPin::Left);
    const auto value=evaluateMaterialUniform(graph,poster,MaterialPin::Value);
    require(value.type==MaterialValueType::Color4&&value.value[0]==0&&value.value[1]==1&&value.value[3]==.37f,
        "Posterization clipped alpha, lost RGBA type or failed its bounded display color contract.");
    connectMaterialNodes(graph,texture,MaterialPin::RGB,pixel,MaterialPin::Left);
    connectMaterialNodes(graph,pixel,MaterialPin::Value,out,MaterialPin::BaseColor);
    connectMaterialNodes(graph,dither,MaterialPin::Value,out,MaterialPin::Roughness);
    auto* pixelNode=findMaterialNode(graph,pixel);pixelNode->stylization.pixelSize=7;
    auto* ditherNode=findMaterialNode(graph,dither);ditherNode->stylization={4,11,DitherPattern::Bayer8x8,3,.6f};
    const auto text=serializeMaterialGraph(graph);
    require(nlohmann::json::parse(text)["version"]==14&&serializeMaterialGraph(parseMaterialGraph(text))==text,
        "Stylization settings, image types or identities failed schema14 persistence.");
    auto prior=nlohmann::json::parse(text);prior["version"]=13;
    rejects([&]{parseMaterialGraph(prior.dump());},"A schema13 file accepted schema14 material kinds.");
    for(const auto id:{pixel,poster,dither}) {
        const auto invalid=[&](auto change,const char* message){auto bad=graph;change(*findMaterialNode(bad,id));rejects([&]{validateMaterialGraph(bad);},message);};
        if(id==pixel)for(const int size:{0,129})invalid([&](auto& n){n.stylization.pixelSize=size;},"Pixelization accepted an out-of-range block size.");
        if(id==poster||id==dither)for(const int levels:{1,257})invalid([&](auto& n){n.stylization.levels=levels;},"Stylization accepted an out-of-range level count.");
        if(id==dither) {
            for(const int size:{0,17})invalid([&](auto& n){n.stylization.size=size;},"Dither accepted an out-of-range cell size.");
            for(const float strength:{-.1f,1.1f,std::numeric_limits<float>::infinity()})invalid([&](auto& n){n.stylization.strength=strength;},"Dither accepted invalid Strength.");
            invalid([](auto& n){n.stylization.pattern=static_cast<DitherPattern>(4);},"Dither accepted an unknown pattern.");
        }
    }
    for(const auto [id,field]:std::array{std::pair{pixel,"pixelSize"},std::pair{poster,"levels"},std::pair{dither,"size"},std::pair{dither,"pattern"}}) {
        auto malformed=nlohmann::json::parse(text);for(auto& n:malformed["nodes"])if(n["id"]==id)n["stylization"][field]=1.5;
        rejects([&]{parseMaterialGraph(malformed.dump());},"A fractional stylization integer was accepted on load.");
    }
}
MaterialValue evaluate(const MaterialGraph& graph,MaterialNodeId id,MaterialPin pin) {
    const auto& n=*findMaterialNode(graph,id);MaterialValue result;result.type=materialOutputType(graph,id,pin);
    if(n.kind==MaterialNodeKind::Scalar){result.value={n.scalar,n.scalar,n.scalar,n.scalar};return result;}
    if(n.kind==MaterialNodeKind::RadialGradient){result.value={.5625f,.5625f,.5625f,.5625f};return result;}
    if(n.kind==MaterialNodeKind::Color||n.kind==MaterialNodeKind::Texture2D||n.kind==MaterialNodeKind::ParticleColor) {
        Vec4 sample=n.kind==MaterialNodeKind::ParticleColor?Vec4{.5f,.75f,.25f,.4f}:n.color;
        if(n.kind==MaterialNodeKind::Texture2D)sample=(n.texture.image.id.find("orm")!=std::string::npos||n.texture.image.id==canonicalAssetId("assets/textures/orm.png"))?Vec4{.2f,.3f,.6f,.8f}:
            (n.texture.image.id.find("emit")!=std::string::npos||n.texture.image.id==canonicalAssetId("assets/textures/emit.png"))?Vec4{.1f,.2f,.3f,1}:Vec4{.4f,.6f,.8f,.5f};
        if(pin==MaterialPin::RGB)result.value=sample;
        else {const auto channel=pin==MaterialPin::R?0:pin==MaterialPin::G?1:pin==MaterialPin::B?2:3;const auto x=sample[channel];result.value={x,x,x,x};}
        return result;
    }
    if(n.kind==MaterialNodeKind::Clamp) {
        const auto* input=materialInputLink(graph,id,MaterialPin::Left);
        const auto value=std::clamp(input?evaluate(graph,input->fromNode,input->fromPin).value[0]:n.left.value[0],0.f,1.f);
        result.value={value,value,value,value};return result;
    }
    if(n.kind==MaterialNodeKind::Multiply) {
        const auto input=[&](MaterialPin inputPin,const MaterialValue& fallback) {const auto* l=materialInputLink(graph,id,inputPin);return l?evaluate(graph,l->fromNode,l->fromPin):fallback;};
        const auto a=input(MaterialPin::Left,n.left),b=input(MaterialPin::Right,n.right);
        for(size_t i=0;i<4;++i)result.value[i]=a.value[a.type==MaterialValueType::Scalar?0:i]*b.value[b.type==MaterialValueType::Scalar?0:i];return result;
    }
    throw std::runtime_error("Fixture evaluator expected a numeric node.");
}
MaterialValue output(const MaterialGraph& graph,MaterialPin pin) {
    const auto& n=materialOutputNode(graph);const auto* l=materialInputLink(graph,n.id,pin);return l?evaluate(graph,l->fromNode,l->fromPin):materialInputDefault(n,pin);
}
void typesChainsAndTransactions() {
    auto graph=blank();const auto out=materialOutputNode(graph).id;
    const auto uv=node(graph,MaterialNodeKind::UV),first=node(graph,MaterialNodeKind::Texture2D),second=node(graph,MaterialNodeKind::Texture2D);
    findMaterialNode(graph,first)->texture.image.id=canonicalAssetId("assets/textures/first.png");findMaterialNode(graph,second)->texture.image.id=canonicalAssetId("assets/textures/second.png");
    connectMaterialNodes(graph,uv,MaterialPin::UV,first,MaterialPin::UV);connectMaterialNodes(graph,uv,MaterialPin::UV,second,MaterialPin::UV);
    const auto multiply=node(graph,MaterialNodeKind::Multiply),scale=node(graph,MaterialNodeKind::Scalar),chain=node(graph,MaterialNodeKind::Multiply);
    findMaterialNode(graph,scale)->scalar=2;
    connectMaterialNodes(graph,first,MaterialPin::RGB,multiply,MaterialPin::Left);connectMaterialNodes(graph,second,MaterialPin::RGB,multiply,MaterialPin::Right);
    connectMaterialNodes(graph,multiply,MaterialPin::Value,chain,MaterialPin::Left);connectMaterialNodes(graph,scale,MaterialPin::Value,chain,MaterialPin::Right);
    connectMaterialNodes(graph,chain,MaterialPin::Value,out,MaterialPin::BaseColor);
    require(materialOutputType(graph,chain,MaterialPin::Value)==MaterialValueType::Color&&near(output(graph,MaterialPin::BaseColor).value[0],.32f),"Texture multiplication or scalar-color chaining failed.");
    const auto before=serializeMaterialGraph(graph);
    rejects([&]{connectMaterialNodes(graph,chain,MaterialPin::Value,multiply,MaterialPin::Left);},"Graph accepted a cycle.");
    require(serializeMaterialGraph(graph)==before,"Rejected connection changed graph data or consumed an identity.");
    rejects([&]{connectMaterialNodes(graph,uv,MaterialPin::UV,out,MaterialPin::Roughness);},"Mapping connected to a numeric input.");
    const auto normal=node(graph,MaterialNodeKind::Texture2D);findMaterialNode(graph,normal)->texture.role=MaterialTextureRole::Normal;
    connectMaterialNodes(graph,normal,MaterialPin::Normal,out,MaterialPin::Normal);
    rejects([&]{connectMaterialNodes(graph,normal,MaterialPin::Normal,multiply,MaterialPin::Left);},"Normal connected to Multiply.");
    const auto link=connectMaterialNodes(graph,scale,MaterialPin::Value,out,MaterialPin::Roughness);
    const auto replacement=connectMaterialNodes(graph,first,MaterialPin::R,out,MaterialPin::Roughness);
    require(link!=replacement&&materialInputLink(graph,out,MaterialPin::Roughness)->id==replacement&&
        std::count_if(graph.links.begin(),graph.links.end(),[&](const auto& l){return l.toNode==out&&l.toPin==MaterialPin::Roughness;})==1,"Input replacement did not retain one wire.");
    require(!removeMaterialNode(graph,out),"Output could be deleted.");
    require(removeMaterialNode(graph,first)&&std::none_of(graph.links.begin(),graph.links.end(),[&](const auto& l){return l.fromNode==first||l.toNode==first;}),"Node deletion left dangling wires.");
    validateMaterialGraph(graph);
    auto invalid=graph;invalid.nodes.push_back(invalid.nodes.front());rejects([&]{validateMaterialGraph(invalid);},"Duplicate node identity accepted.");
    invalid=graph;invalid.nextNodeId=1;rejects([&]{validateMaterialGraph(invalid);},"Identity reuse accepted.");
    invalid=graph;findMaterialNode(invalid,scale)->scalar=std::numeric_limits<float>::infinity();rejects([&]{validateMaterialGraph(invalid);},"Nonfinite scalar accepted.");
    invalid=graph;findMaterialNode(invalid,second)->texture.image.id="assets/../outside.png";rejects([&]{validateMaterialGraph(invalid);},"Escaping graph image path accepted.");
    const auto before_rewrite=serializeMaterialGraph(graph);
    for(const auto& image_id:{std::string("../outside.png"),canonicalAssetId("builtin:environment/studio")}) {
        rejects([&]{rewriteMaterialGraphTextures(graph,[&](const auto&,MaterialTextureRole){return image_id;});},"Texture rewrite accepted an unmanaged or protected image reference.");
        require(serializeMaterialGraph(graph)==before_rewrite,"Rejected texture rewrite changed the graph.");
        auto invalid_conversion=graph;findMaterialNode(invalid_conversion,second)->texture.image.id=image_id;
        rejects([&]{materialGraphLegacySurface(invalid_conversion);},"Legacy surface conversion accepted an unmanaged or protected image reference.");
        rejects([&]{materialGraphFixedSurface(invalid_conversion);},"Fixed surface conversion accepted an unmanaged or protected image reference.");
        ModelMaterial legacy;legacy.baseColorTexture.image.id=image_id;
        rejects([&]{materialGraphFromLegacy(legacy,MaterialShading::StandardPbr,MaterialBlend::Opaque);},"Legacy graph conversion accepted an unmanaged or protected image reference.");
        rejects([&]{materialGraphFromLegacy(legacy,MaterialShading::Unlit,MaterialBlend::Alpha);},"Legacy particle graph conversion accepted an unmanaged or protected image reference.");
    }
    const auto before_invalid_mutation=serializeMaterialGraphData(invalid);
    rejects([&]{materialGraphOrder(invalid,MaterialShading::StandardPbr,MaterialBlend::Opaque);},"Graph ordering accepted an unmanaged image reference.");
    rejects([&]{connectMaterialNodes(invalid,scale,MaterialPin::Value,out,MaterialPin::Roughness);},"Connection accepted a graph containing an unmanaged image reference.");
    require(serializeMaterialGraphData(invalid)==before_invalid_mutation,"Rejected image-policy connection changed graph data or consumed an identity.");
    rejects([&]{migrateLegacyParticleGraph(invalid,true);},"Legacy particle conversion accepted an unmanaged image reference.");
    require(serializeMaterialGraphData(invalid)==before_invalid_mutation,"Rejected image-policy conversion changed graph data or consumed an identity.");
    auto bounded=blank();while(bounded.nodes.size()<MaterialGraph::MaximumNodes)addMaterialNode(bounded,MaterialNodeKind::Scalar);
    rejects([&]{addMaterialNode(bounded,MaterialNodeKind::Scalar);},"Node bound was ignored.");
}
void signaturesAndDormantReferences() {
    auto graph=blank();const auto out=materialOutputNode(graph).id,rough=node(graph,MaterialNodeKind::Scalar);
    connectMaterialNodes(graph,rough,MaterialPin::Value,out,MaterialPin::Roughness);
    const auto texture=node(graph,MaterialNodeKind::Texture2D);findMaterialNode(graph,texture)->texture.image.id=canonicalAssetId("assets/textures/dormant.png");
    connectMaterialNodes(graph,texture,MaterialPin::R,out,MaterialPin::Occlusion);
    const auto topology=materialGraphTopologyHash(graph,MaterialShading::StandardPbr,MaterialBlend::Opaque);
    const auto values=materialGraphValueHash(graph,MaterialShading::StandardPbr,MaterialBlend::Opaque),layout=materialGraphLayoutHash(graph);
    findMaterialNode(graph,rough)->position={40,50};findMaterialNode(graph,rough)->name="Visible roughness";findMaterialNode(graph,rough)->scalarRange={-1,4};
    require(topology==materialGraphTopologyHash(graph,MaterialShading::StandardPbr,MaterialBlend::Opaque)&&values==materialGraphValueHash(graph,MaterialShading::StandardPbr,MaterialBlend::Opaque)&&layout!=materialGraphLayoutHash(graph),"Layout or slider bounds changed shader identity.");
    findMaterialNode(graph,rough)->scalar=.4f;
    require(topology==materialGraphTopologyHash(graph,MaterialShading::StandardPbr,MaterialBlend::Opaque)&&values!=materialGraphValueHash(graph,MaterialShading::StandardPbr,MaterialBlend::Opaque),"Uniform edit changed topology or failed to change values.");
    materialOutputNode(graph).output.shading=MaterialShading::Unlit;
    const auto order=materialGraphOrder(graph,MaterialShading::Unlit,MaterialBlend::Opaque);
    require(std::find(order.begin(),order.end(),texture)==order.end()&&materialGraphTextureReferences(graph)==std::vector<std::string>{canonicalAssetId("assets/textures/dormant.png")},"Dormant resources were prepared or forgotten as authored dependencies.");
    const auto restored=parseMaterialGraph(serializeMaterialGraph(graph));
    require(materialInputLink(restored,out,MaterialPin::Roughness)&&materialInputLink(restored,out,MaterialPin::Occlusion),"Model switching or serialization destroyed hidden links.");
    const auto normal=node(graph,MaterialNodeKind::Texture2D);findMaterialNode(graph,normal)->texture.role=MaterialTextureRole::Normal;findMaterialNode(graph,normal)->texture.image.id=canonicalAssetId("assets/textures/dormant.png");
    rewriteMaterialGraphTextures(graph,[](const auto&,MaterialTextureRole role){return std::string(role==MaterialTextureRole::Normal?NeutralNormalTextureId:GrayTextureId);});
    require(findMaterialNode(graph,texture)->texture.image.id==GrayTextureId&&findMaterialNode(graph,normal)->texture.image.id==NeutralNormalTextureId,"Graph reference replacement lost role-correct dormant defaults.");
}
void refractionAuthoringAndPersistence() {
    auto graph=blank();const auto output=materialOutputNode(graph).id;
    const auto defaults=materialOutputNode(graph).output;
    require(!defaults.refraction&&defaults.transmission==1&&defaults.ior==1.5f&&defaults.thickness==0&&
        defaults.attenuationColor==Vec3{1,1,1}&&defaults.attenuationDistance==1,"New material optical defaults changed or enabled refraction.");
    const MaterialPin optical[]{MaterialPin::Transmission,MaterialPin::Ior,MaterialPin::Thickness,MaterialPin::AttenuationColor,MaterialPin::AttenuationDistance};
    for(const auto shading:{MaterialShading::StandardPbr,MaterialShading::Unlit})for(const auto blend:{MaterialBlend::Opaque,MaterialBlend::Mask,MaterialBlend::Alpha,MaterialBlend::Additive}) {
        auto& settings=materialOutputNode(graph).output;settings.shading=shading;settings.blend=blend;settings.refraction=true;
        const bool active=shading==MaterialShading::StandardPbr&&blend==MaterialBlend::Alpha;
        require(materialRefractionActive(settings)==active,"Refraction activation escaped Lit Translucent.");
        for(const auto pin:optical)require(materialPinActive(shading,blend,pin,false,true)==active,"Optical input activation disagreed with the shared material capability.");
        require(materialPinActive(shading,blend,MaterialPin::Specular,false,true)==(shading==MaterialShading::StandardPbr&&!active),"Refraction did not make Specular dormant.");
    }
    auto& settings=materialOutputNode(graph).output;settings=defaults;settings.blend=MaterialBlend::Alpha;
    const auto specular=node(graph,MaterialNodeKind::Texture2D),ior=node(graph,MaterialNodeKind::Texture2D),color=node(graph,MaterialNodeKind::Color);
    findMaterialNode(graph,specular)->texture.image.id=canonicalAssetId("assets/textures/specular.png");
    findMaterialNode(graph,ior)->texture.image.id=canonicalAssetId("assets/textures/ior.png");
    findMaterialNode(graph,color)->color={.25f,.5f,.75f,.2f};
    const auto specularLink=connectMaterialNodes(graph,specular,MaterialPin::R,output,MaterialPin::Specular);
    const auto disabled=serializeMaterialGraph(graph);
    rejects([&]{connectMaterialNodes(graph,ior,MaterialPin::R,output,MaterialPin::Ior);},"Disabled Refraction accepted a new optical link.");
    require(serializeMaterialGraph(graph)==disabled,"Rejected optical connection changed authored state.");
    const auto inactiveValues=materialGraphValueHash(graph,MaterialShading::StandardPbr,MaterialBlend::Alpha);
    materialOutputNode(graph).output.thickness=2;
    require(materialGraphValueHash(graph,MaterialShading::StandardPbr,MaterialBlend::Alpha)==inactiveValues,"Dormant optical defaults changed active material values.");
    materialOutputNode(graph).output.refraction=true;
    const auto opticalLink=connectMaterialNodes(graph,ior,MaterialPin::RGB,output,MaterialPin::Ior);
    connectMaterialNodes(graph,color,MaterialPin::RGBA,output,MaterialPin::AttenuationColor);
    const auto topology=materialGraphTopologyHash(graph,MaterialShading::StandardPbr,MaterialBlend::Alpha);
    const auto values=materialGraphValueHash(graph,MaterialShading::StandardPbr,MaterialBlend::Alpha);
    auto order=materialGraphOrder(graph,MaterialShading::StandardPbr,MaterialBlend::Alpha);
    require(std::find(order.begin(),order.end(),ior)!=order.end()&&std::find(order.begin(),order.end(),specular)==order.end()&&
        materialInputLink(graph,output,MaterialPin::Specular)->id==specularLink,"Active refraction lost dormant Specular or prepared its branch.");
    auto demand=materialRuntimeTextureMappings(graph);
    require(demand.size()==1&&demand.front().node==ior,"Optical streaming demand included dormant Specular or omitted active IOR.");
    materialOutputNode(graph).output.specular=.9f;
    require(materialGraphValueHash(graph,MaterialShading::StandardPbr,MaterialBlend::Alpha)==values,"Dormant Specular default changed optical shader values.");
    materialOutputNode(graph).output.attenuationDistance=3.5f;
    require(materialGraphTopologyHash(graph,MaterialShading::StandardPbr,MaterialBlend::Alpha)==topology&&
        materialGraphValueHash(graph,MaterialShading::StandardPbr,MaterialBlend::Alpha)!=values,"Optical value edit compiled topology or retained stale values.");
    const auto active=serializeMaterialGraph(graph);auto reopened=parseMaterialGraph(active);
    require(nlohmann::json::parse(active)["version"]==14&&serializeMaterialGraph(reopened)==active&&
        materialInputLink(reopened,output,MaterialPin::Ior)->id==opticalLink,"Optical defaults and pin identities failed schema14 round trip.");
    materialOutputNode(graph).output.refraction=false;
    order=materialGraphOrder(graph,MaterialShading::StandardPbr,MaterialBlend::Alpha);demand=materialRuntimeTextureMappings(graph);
    require(std::find(order.begin(),order.end(),ior)==order.end()&&std::find(order.begin(),order.end(),specular)!=order.end()&&
        demand.size()==1&&demand.front().node==specular&&materialInputLink(graph,output,MaterialPin::Ior)->id==opticalLink,
        "Disabling Refraction did not restore Specular and retain dormant optical connections.");
    require(materialGraphTextureReferences(graph).size()==2,"Dormant optical texture reference disappeared from asset retention.");
    materialOutputNode(graph).output.refraction=true;
    require(serializeMaterialGraph(graph)==active,"Refraction toggle reset authored defaults or links.");
    for(const auto pin:optical) {
        auto candidate=materialOutputNode(graph);const auto before=serializeMaterialGraph(graph);
        const MaterialValue value=pin==MaterialPin::AttenuationColor?MaterialValue{MaterialValueType::Color,{-1,0,0,1}}:MaterialValue{MaterialValueType::Scalar,{-1,-1,-1,-1}};
        require(!setMaterialInputDefault(candidate,pin,value),"Invalid optical fallback was accepted.");
        auto unchanged=graph;materialOutputNode(unchanged)=candidate;require(serializeMaterialGraph(unchanged)==before,"Rejected optical fallback mutated its node.");
    }
    const auto invalidDefault=[&](const auto& edit){auto invalid=graph;edit(materialOutputNode(invalid).output);rejects([&]{validateMaterialGraph(invalid);},"Invalid optical Output value was accepted.");};
    invalidDefault([](auto& o){o.transmission=1.01f;});invalidDefault([](auto& o){o.ior=.99f;});
    invalidDefault([](auto& o){o.thickness=-.1f;});invalidDefault([](auto& o){o.attenuationDistance=0;});
    invalidDefault([](auto& o){o.attenuationColor[1]=1.1f;});invalidDefault([](auto& o){o.ior=std::numeric_limits<float>::infinity();});
    auto large=graph;auto& largeOutput=materialOutputNode(large);require(setMaterialInputDefault(largeOutput,MaterialPin::Ior,{MaterialValueType::Scalar,{5,5,5,5}})&&
        setMaterialInputDefault(largeOutput,MaterialPin::Thickness,{MaterialValueType::Scalar,{10000,10000,10000,10000}}),"Soft optical gesture ranges became hard upper limits.");
    validateMaterialGraph(large);
    Project project;MaterialAsset asset;asset.id="assets/materials/glass.crimson-material";asset.name="Glass";asset.graph=reopened;
    canonicalizeMaterialAsset(asset);saveMaterialAsset(project.root,asset);const auto file=materialAssetPath(project.root,asset.id);const auto bytes=read(file);
    const auto loaded=loadMaterialAsset(project.root,asset.id);
    require(read(file)==bytes&&serializeMaterialGraph(loaded.graph)==active&&nlohmann::json::parse(bytes)["version"]==6&&
        modelMaterialCompatible(loaded)&&particleMaterialCompatible(loaded),"Glass native save/reopen changed data, version or shared consumer compatibility.");
    auto old=nlohmann::json::parse(serializeMaterialGraph(blank()));old["version"]=12;
    for(const char* key:{"refraction","transmission","ior","thickness","attenuationColor","attenuationDistance"})old["nodes"][0]["output"].erase(key);
    const auto legacy=parseMaterialGraph(old.dump());const auto& migrated=materialOutputNode(legacy).output;
    require(!migrated.refraction&&migrated.transmission==1&&migrated.ior==1.5f&&migrated.thickness==0&&migrated.attenuationColor==Vec3{1,1,1}&&migrated.attenuationDistance==1,
        "Older graph data did not acquire disabled optical defaults in memory.");
    auto oldAsset=nlohmann::json::parse(bytes);oldAsset["graph"]=old;const auto oldBytes=oldAsset.dump(2);write(file,oldBytes);
    const auto loadedOld=loadMaterialAsset(project.root,asset.id);
    require(!materialOutputNode(loadedOld.graph).output.refraction&&read(file)==oldBytes,"Loading a pre-refraction native file enabled optics or rewrote its bytes.");
}

void heightTessellationPersistence() {
    const Aabb original{{-1,-2,-3},{1,2,3},true};
    MaterialTessellation boundsSettings;
    require(materialTessellationBounds(original,boundsSettings).minimum==original.minimum,
        "Disabled displacement changed original mesh bounds.");
    boundsSettings.enabled=true;boundsSettings.amount=-2;boundsSettings.midpoint=.25f;
    const auto expanded=materialTessellationBounds(original,boundsSettings);
    auto affine=math::scaling({2,.5f,-3});affine.at(0,1)=1.25f;
    const auto transformed=math::transformBounds(expanded,affine);
    // Endpoint heights and all normal octants must fit after reflection,
    // nonuniform scaling and shear; these are the extremal permitted offsets.
    for(unsigned corner=0;corner<8;++corner)for(const float height:{0.f,1.f})for(unsigned normal=0;normal<8;++normal) {
        Vec3 vertex{corner&1?1.f:-1.f,corner&2?2.f:-2.f,corner&4?3.f:-3.f};
        const auto direction=math::normalized(Vec3{normal&1?1.f:-1.f,normal&2?1.f:-1.f,normal&4?1.f:-1.f});
        const auto point=math::transformPoint(affine,vertex+direction*((height-.25f)*-2));
        for(unsigned axis=0;axis<3;++axis)require(point[axis]>=transformed.minimum[axis]-1e-5f&&point[axis]<=transformed.maximum[axis]+1e-5f,
            "Derived bounds clipped permitted displaced geometry after an affine transform.");
    }
    auto graph=blank();const auto out=materialOutputNode(graph).id;
    const auto initialTopology=materialGraphTopologyHash(graph,MaterialShading::StandardPbr,MaterialBlend::Opaque);
    require(!materialOutputNode(graph).output.tessellation.enabled&&near(materialInputDefault(materialOutputNode(graph),MaterialPin::Height).value[0],.5f),
        "New materials must have disabled tessellation and a neutral Height fallback.");
    const auto image=node(graph,MaterialNodeKind::Texture2D);findMaterialNode(graph,image)->texture.role=MaterialTextureRole::Data;
    findMaterialNode(graph,image)->texture.image.id=canonicalAssetId("assets/textures/height.png");
    rejects([&]{connectMaterialNodes(graph,image,MaterialPin::R,out,MaterialPin::Height);},"Disabled Height accepted a new connection.");
    materialOutputNode(graph).output.tessellation.enabled=true;
    require(!materialGraphFixedSurface(graph)&&materialGraphTopologyHash(graph,MaterialShading::StandardPbr,MaterialBlend::Opaque)!=initialTopology,
        "Enabling tessellation failed to select a graph shader topology.");
    const auto link=connectMaterialNodes(graph,image,MaterialPin::RGB,out,MaterialPin::Height);
    require(materialOutputType(graph,image,MaterialPin::RGB)==MaterialValueType::Color,"Height changed its authored RGB source pin.");
    const auto enabledTopology=materialGraphTopologyHash(graph,MaterialShading::StandardPbr,MaterialBlend::Opaque);
    const auto values=materialGraphValueHash(graph,MaterialShading::StandardPbr,MaterialBlend::Opaque);
    auto& settings=materialOutputNode(graph).output.tessellation;settings.amount=-.35f;settings.midpoint=.2f;settings.maxFactor=16;settings.heightLod=2;
    materialOutputNode(graph).output.height=.75f;
    require(materialGraphTopologyHash(graph,MaterialShading::StandardPbr,MaterialBlend::Opaque)==enabledTopology&&
        materialGraphValueHash(graph,MaterialShading::StandardPbr,MaterialBlend::Opaque)!=values,"Displacement controls compiled a new topology or failed to update values.");
    const auto expected=settings;const auto before=serializeMaterialGraph(graph);const auto loaded=parseMaterialGraph(before);
    require(serializeMaterialGraph(loaded)==before&&materialOutputNode(loaded).output.tessellation==expected&&materialInputLink(loaded,out,MaterialPin::Height)->id==link,
        "Tessellation settings or Height links failed to round trip.");
    materialOutputNode(graph).output.tessellation.enabled=false;
    const auto dormant=materialGraphOrder(graph,MaterialShading::StandardPbr,MaterialBlend::Opaque);
    require(std::find(dormant.begin(),dormant.end(),image)==dormant.end()&&materialInputLink(graph,out,MaterialPin::Height)->id==link&&
        materialGraphTextureReferences(graph)==std::vector<std::string>{canonicalAssetId("assets/textures/height.png")}&&materialGraphFixedSurface(graph),
        "Disabled tessellation prepared Height work, discarded dependencies or prevented the ordinary fixed path.");
    auto legacy=nlohmann::json::parse(serializeMaterialGraph(blank()));legacy["version"]=7;
    legacy["nodes"][0]["output"].erase("height");legacy["nodes"][0]["output"].erase("tessellation");
    const auto old=parseMaterialGraph(legacy.dump());
    require(!materialOutputNode(old).output.tessellation.enabled&&near(materialOutputNode(old).output.height,.5f),"Older graphs implicitly enabled displacement.");
    for(const auto factor:{nlohmann::json(0),nlohmann::json(65),nlohmann::json(-1),nlohmann::json(2.5),nlohmann::json("8")}) {
        auto invalid=nlohmann::json::parse(before);invalid["nodes"][0]["output"]["tessellation"]["maxFactor"]=factor;
        rejects([&]{parseMaterialGraph(invalid.dump());},"Malformed tessellation factor was accepted.");
    }
    for(const auto pair:{std::pair{"midpoint",-1.f},std::pair{"midpoint",2.f},std::pair{"heightLod",-1.f},std::pair{"heightLod",17.f}}) {
        auto invalid=nlohmann::json::parse(before);invalid["nodes"][0]["output"]["tessellation"][pair.first]=pair.second;
        rejects([&]{parseMaterialGraph(invalid.dump());},"Invalid displacement range was accepted.");
    }
    Project project;MaterialAsset asset;asset.id="assets/materials/height.crimson-material";asset.name="Height";asset.graph=loaded;
    saveMaterialAsset(project.root,asset);const auto disk=read(materialAssetPath(project.root,asset.id));
    const auto reopened=loadMaterialAsset(project.root,asset.id);
    require(materialOutputNode(reopened.graph).output.tessellation==expected&&read(materialAssetPath(project.root,asset.id))==disk,
        "Native material reopen changed displacement settings or rewrote authored bytes.");
}
void receiveFogPersistence() {
    Project project;auto material=createMaterialAsset(project.root,"Black exterior");
    require(materialOutputNode(material.graph).output.receiveFog&&material.surface.receiveFog&&ModelMaterial{}.receiveFog,
        "New and imported materials must receive fog by default.");
    for(const auto shading:{MaterialShading::StandardPbr,MaterialShading::Unlit})
        for(const auto blend:{MaterialBlend::Opaque,MaterialBlend::Mask,MaterialBlend::Alpha,MaterialBlend::Additive}) {
            auto& settings=materialOutputNode(material.graph).output;settings.shading=shading;settings.blend=blend;settings.receiveFog=true;
            const auto topology=materialGraphTopologyHash(material.graph,shading,blend),values=materialGraphValueHash(material.graph,shading,blend);
            settings.receiveFog=false;canonicalizeMaterialAsset(material);
            require(topology==materialGraphTopologyHash(material.graph,shading,blend)&&values!=materialGraphValueHash(material.graph,shading,blend),
                "Fog reception must change material values without selecting a new shader topology.");
            require(!material.surface.receiveFog&&!materialOutputNode(*material.surface.graph).output.receiveFog,
                "Fog exclusion was lost from the immutable runtime material transport.");
            saveMaterialAsset(project.root,material);const auto loaded=loadMaterialAsset(project.root,material.id);
            require(!materialOutputNode(loaded.graph).output.receiveFog&&!loaded.surface.receiveFog&&serializeMaterialAsset(loaded)==serializeMaterialAsset(material),
                "Fog exclusion did not survive native material save/reload.");
        }
    const auto copy=copyMaterialAsset(project.root,material,"Exterior copy");
    require(!materialOutputNode(copy.graph).output.receiveFog&&!copy.surface.receiveFog,"Copying a material reset its fog reception.");
    auto legacy=material.surface;legacy.graph.reset();
    require(!materialOutputNode(materialGraphFromLegacy(legacy,material.shading,material.blend)).output.receiveFog,
        "Legacy runtime transport conversion reset an explicit fog exclusion.");
    auto old=nlohmann::json::parse(serializeMaterialAsset(material));
    auto& output=old["graph"]["nodes"][0]["output"];output.erase("receiveFog");output["shading"]=0;output["blend"]=0;
    // Native material revisions 4-6 and graph revisions 1-4 all predate this
    // optional property. Loading them must preserve their previous fog behavior.
    for(const int materialVersion:{4,5,6})for(const int graphVersion:{1,2,3,4}) {
        old["version"]=materialVersion;old["graph"]["version"]=graphVersion;
        if(graphVersion==2)output["usage"]=0;
        const auto bytes=old.dump();write(materialAssetPath(project.root,material.id),bytes);
        const auto loaded=loadMaterialAsset(project.root,material.id);
        require(loaded.surface.receiveFog&&materialOutputNode(loaded.graph).output.receiveFog&&read(materialAssetPath(project.root,material.id))==bytes,
            "An older material lost fog reception or was rewritten while loading.");
    }
    for(const auto& invalid:nlohmann::json::array({0,1,"false",nullptr})) {
        output["receiveFog"]=invalid;
        rejects([&]{parseMaterialGraph(old["graph"].dump());},"A non-boolean fog reception setting was accepted.");
    }
}
void rgbScalarInputsAndFallbacks() {
    auto graph=blank();const auto out=materialOutputNode(graph).id;
    const auto rough=node(graph,MaterialNodeKind::Texture2D),metal=node(graph,MaterialNodeKind::Texture2D);
    findMaterialNode(graph,rough)->texture.role=MaterialTextureRole::Data;findMaterialNode(graph,rough)->texture.image.id=canonicalAssetId("assets/textures/roughness.png");
    findMaterialNode(graph,metal)->texture.role=MaterialTextureRole::Data;findMaterialNode(graph,metal)->texture.image.id=canonicalAssetId("assets/textures/metallic.png");
    for(const auto channel:{MaterialPin::R,MaterialPin::G,MaterialPin::B,MaterialPin::A,MaterialPin::RGB}) {
        connectMaterialNodes(graph,rough,channel,out,MaterialPin::Roughness);
        connectMaterialNodes(graph,metal,channel,out,MaterialPin::Metallic);
        require(materialInputLink(graph,out,MaterialPin::Roughness)->fromPin==channel&&materialInputLink(graph,out,MaterialPin::Metallic)->fromPin==channel,
            "Texture scalar connection rewrote an explicitly selected channel.");
    }
    const auto bytes=serializeMaterialGraph(graph);require(serializeMaterialGraph(parseMaterialGraph(bytes))==bytes,"RGB scalar links lost their identities on round-trip.");
    auto fallback=materialGraphLegacySurface(graph);
    require(fallback.metallicRoughnessTexture.image.empty()&&fallback.graph&&serializeMaterialGraph(*fallback.graph)==bytes,
        "Independent RGB scalar maps were incorrectly interpreted as packed G/B or lost from authoritative graph transport.");
    connectMaterialNodes(graph,rough,MaterialPin::G,out,MaterialPin::Roughness);
    connectMaterialNodes(graph,rough,MaterialPin::B,out,MaterialPin::Metallic);
    require(materialGraphLegacySurface(graph).metallicRoughnessTexture.image.id==canonicalAssetId("assets/textures/roughness.png"),"Explicit packed G/B lost legacy fallback representation.");
    materialOutputNode(graph).output.blend=MaterialBlend::Alpha;
    connectMaterialNodes(graph,rough,MaterialPin::RGB,out,MaterialPin::Opacity);
    require(materialGraphLegacySurface(graph).baseColorTexture.image.empty(),"RGB scalar coverage was misinterpreted as texture alpha by legacy fallback.");
    connectMaterialNodes(graph,rough,MaterialPin::A,out,MaterialPin::Opacity);
    require(materialGraphLegacySurface(graph).baseColorTexture.image.id==canonicalAssetId("assets/textures/roughness.png"),"Explicit texture alpha lost legacy fallback representation.");
    const auto color=node(graph,MaterialNodeKind::Color),scalar=node(graph,MaterialNodeKind::Scalar),product=node(graph,MaterialNodeKind::Multiply),saturate=node(graph,MaterialNodeKind::Clamp);
    findMaterialNode(graph,color)->color={.2f,.9f,.7f,.8f};findMaterialNode(graph,scalar)->scalar=.5f;
    connectMaterialNodes(graph,scalar,MaterialPin::Value,product,MaterialPin::Left);
    connectMaterialNodes(graph,scalar,MaterialPin::Value,product,MaterialPin::Right);
    const auto roughLink=connectMaterialNodes(graph,product,MaterialPin::Value,out,MaterialPin::Roughness);
    connectMaterialNodes(graph,color,MaterialPin::RGB,product,MaterialPin::Left);
    require(materialOutputType(graph,product,MaterialPin::Value)==MaterialValueType::Color&&materialInputLink(graph,out,MaterialPin::Roughness)->id==roughLink,
        "Upstream Scalar-to-Color reconnection invalidated an existing scalar consumer or replaced its link.");
    connectMaterialNodes(graph,product,MaterialPin::Value,saturate,MaterialPin::Left);
    connectMaterialNodes(graph,saturate,MaterialPin::Value,out,MaterialPin::Metallic);
    require(near(evaluateMaterialUniform(graph,saturate,MaterialPin::Value).value[0],.1f),"Uniform Saturate did not use the Color expression's first component.");
    fallback=materialGraphLegacySurface(graph);
    require(near(fallback.roughness,.1f)&&near(fallback.metallic,.1f),"Scalar fallback factors used luminance or another Color channel.");
    const auto uv=node(graph,MaterialNodeKind::UV),vector=node(graph,MaterialNodeKind::Vector2),normal=node(graph,MaterialNodeKind::Texture2D);
    findMaterialNode(graph,normal)->texture.role=MaterialTextureRole::Normal;
    const auto before=serializeMaterialGraph(graph);
    rejects([&]{connectMaterialNodes(graph,uv,MaterialPin::UV,out,MaterialPin::Roughness);},"Mapping was implicitly converted to Scalar.");
    rejects([&]{connectMaterialNodes(graph,normal,MaterialPin::Normal,out,MaterialPin::Metallic);},"Normal was implicitly converted to Scalar.");
    rejects([&]{connectMaterialNodes(graph,vector,MaterialPin::XY,saturate,MaterialPin::Left);},"Vector2 was implicitly converted to Scalar.");
    rejects([&]{connectMaterialNodes(graph,vector,MaterialPin::XY,product,MaterialPin::Right);},"Color Multiply accepted Vector2 after scalar conversion was added.");
    require(serializeMaterialGraph(graph)==before,"Rejected scalar conversions mutated the graph.");
}
void migrationAndAuthority() {
    Project project;MaterialAsset legacy;legacy.id="assets/materials/legacy.crimson-material";legacy.name="Imported";legacy.originModel="assets/models/original/manifest.json";
    auto& s=legacy.surface;s.baseColor={.25f,.5f,.75f,.35f};s.roughness=.7f;s.metallic=.4f;s.emissive={2,3,4};s.normalScale=-.75f;s.occlusionStrength=.6f;s.alphaMask=true;s.doubleSided=true;s.alphaCutoff=2;
    s.baseColorTexture.image.id="assets/textures/color.png";s.baseColorTexture.texCoord=1;
    s.metallicRoughnessTexture.image.id="assets/textures/orm.png";s.occlusionTexture=s.metallicRoughnessTexture;
    s.normalTexture.image.id="assets/textures/normal.png";s.emissiveTexture.image.id="assets/textures/emit.png";
    s.baseColorTexture.wrapU=ModelWrap::Mirror;s.normalTexture.minFilter=ModelFilter::NearestMipmapLinear;
    s.uvScale={.25f,-.5f,2};s.uvOffset={.1f,.2f,.3f};s.uvRotation={10,20,30};s.uvScaleLinked=false;
    for(const int version:{1,2,3}) {
        const auto bytes=material_test::legacyMaterialJson(legacy,version).dump(2);write(project.root/legacy.id,bytes);
        const auto loaded=loadMaterialAsset(project.root,legacy.id);
        require(read(project.root/legacy.id)==bytes&&!loaded.graph.empty()&&loaded.originModel==canonicalAssetId(legacy.originModel),"Migration rewrote the file or lost imported identity.");
        require(loaded.surface.receiveFog&&materialOutputNode(loaded.graph).output.receiveFog,"Legacy material migration disabled fog reception.");
        require(near(output(loaded.graph,MaterialPin::BaseColor).value[0],.1f)&&near(output(loaded.graph,MaterialPin::OpacityMask).value[0],.175f)&&
            near(output(loaded.graph,MaterialPin::Roughness).value[0],.21f)&&near(output(loaded.graph,MaterialPin::Metallic).value[0],.24f)&&
            near(output(loaded.graph,MaterialPin::Emission).value[2],1.2f),"Migration changed map factors or packed/alpha channels.");
        require(loaded.surface.baseColor==s.baseColor&&loaded.surface.roughness==s.roughness&&loaded.surface.metallic==s.metallic&&loaded.surface.normalScale==s.normalScale&&loaded.surface.occlusionStrength==s.occlusionStrength,"Legacy transport lost map factors or normal/AO strengths.");
        require(loaded.surface.baseColorTexture.texCoord==1&&loaded.surface.baseColorTexture.wrapU==ModelWrap::Mirror&&loaded.surface.normalTexture.minFilter==ModelFilter::NearestMipmapLinear,"Migration lost UV1 or mixed sampler settings.");
        require(loaded.surface.uvScale==(version==3?s.uvScale:Vec3{1,1,1}),"Legacy mapping version defaults changed.");
        require(std::count_if(loaded.graph.nodes.begin(),loaded.graph.nodes.end(),[](const auto& n){return n.kind==MaterialNodeKind::Texture2D;})==4,"Equivalent packed AO/roughness/metallic sample was not shared.");
        saveMaterialAsset(project.root,loaded);const auto json=nlohmann::json::parse(read(project.root/legacy.id));
        require(json.at("version")==6&&json.contains("graph")&&!json.contains("baseColorTexture")&&serializeMaterialAsset(loadMaterialAsset(project.root,legacy.id))==serializeMaterialAsset(loaded),"Version 6 retained competing legacy authority or failed to round-trip.");
    }
    s.triplanar=true;s.triplanarWorld=true;s.sharedSampler=ModelSampler{ModelWrap::Repeat,ModelWrap::Mirror,ModelFilter::NearestMipmapNearest,ModelFilter::Nearest};
    write(project.root/legacy.id,material_test::legacyMaterialJson(legacy).dump());auto world=loadMaterialAsset(project.root,legacy.id);
    require(world.surface.triplanarWorld&&world.surface.uvScale==s.uvScale,"World triplanar mapping was not migrated.");
    for(const auto& n:world.graph.nodes)if(n.kind==MaterialNodeKind::Texture2D)require(n.texture.sampler==*s.sharedSampler,"Shared legacy sampler did not reach every Texture node.");
    const auto previous=serializeMaterialAsset(world);world.surface.baseColor={1,0,0,1};canonicalizeMaterialAsset(world);
    require(serializeMaterialAsset(world)==previous&&world.surface.baseColor==s.baseColor,"Derived compatibility transport overrode graph authority.");
    const auto pinned=world.surface.graph;materialOutputNode(world.graph).output.alphaCutoff=.2f;canonicalizeMaterialAsset(world);
    require(materialOutputNode(*pinned).output.alphaCutoff==2&&world.surface.alphaCutoff==.2f,"Canonicalization mutated an immutable prior graph snapshot.");
    auto sprite=defaultParticleMaterialAsset();canonicalizeMaterialAsset(sprite);require(particleMaterialCompatible(sprite)&&sprite.graph.nodes.size()==4&&std::any_of(sprite.graph.nodes.begin(),sprite.graph.nodes.end(),[](const auto& n){return n.kind==MaterialNodeKind::RadialGradient;}),"Procedural particle compatibility was lost.");
    auto mesh=defaultMaterialAsset();canonicalizeMaterialAsset(mesh);materialOutputNode(mesh.graph).output.shading=MaterialShading::Unlit;canonicalizeMaterialAsset(mesh);
    require(modelMaterialCompatible(mesh)&&particleMaterialCompatible(mesh),"Unlit opaque mesh compatibility was not added.");
}
void repeatedSourceDag() {
    MaterialAsset material;material.graph=blank();auto& graph=material.graph;
    materialOutputNode(graph).output.blend=MaterialBlend::Mask;
    const auto outputId=materialOutputNode(graph).id;
    const auto scalar=node(graph,MaterialNodeKind::Scalar),color=node(graph,MaterialNodeKind::Color);
    findMaterialNode(graph,scalar)->scalar=1;findMaterialNode(graph,color)->color={1,1,1,.5f};
    const auto link=[&](MaterialNodeId source,MaterialPin sourcePin,MaterialNodeId target,MaterialPin targetPin) {
        graph.links.push_back({graph.nextLinkId++,source,target,sourcePin,targetPin});
    };
    for(const auto seed:{scalar,color}) {
        auto previous=seed;auto pin=seed==scalar?MaterialPin::Value:MaterialPin::RGB;
        for(int i=0;i<48;++i) {
            const auto product=node(graph,MaterialNodeKind::Multiply);
            link(previous,pin,product,MaterialPin::Left);link(previous,pin,product,MaterialPin::Right);
            previous=product;pin=MaterialPin::Value;
        }
        link(previous,pin,outputId,seed==scalar?MaterialPin::Roughness:MaterialPin::BaseColor);
    }
    link(color,MaterialPin::A,outputId,MaterialPin::OpacityMask);
    const auto started=std::chrono::steady_clock::now();
    validateMaterialGraph(graph);canonicalizeMaterialAsset(material);
    require(material.surface.roughness==1&&material.surface.baseColor==Vec4{1,1,1,.5f},"Shared DAG evaluation lost values or confused outputs from one node.");
    require(std::chrono::steady_clock::now()-started<std::chrono::seconds(5),"Bounded shared DAG evaluation recomputed repeated branches.");
}
void activeCompatibilityTransport() {
    MaterialAsset material;material.graph=blank();auto& graph=material.graph;
    const auto out=materialOutputNode(graph).id,uv=node(graph,MaterialNodeKind::UV),normal=node(graph,MaterialNodeKind::Texture2D);
    findMaterialNode(graph,uv)->uv.mode=MaterialUvMode::UV1;
    findMaterialNode(graph,normal)->texture.role=MaterialTextureRole::Normal;
    findMaterialNode(graph,normal)->texture.image.id=canonicalAssetId("assets/textures/normal-uv1.png");
    connectMaterialNodes(graph,uv,MaterialPin::UV,normal,MaterialPin::UV);
    connectMaterialNodes(graph,normal,MaterialPin::Normal,out,MaterialPin::Normal);
    materialOutputNode(graph).output.shading=MaterialShading::Unlit;canonicalizeMaterialAsset(material);
    require(material.surface.normalTexture.image.empty()&&materialTextureReferences(material)==std::vector<std::string>{canonicalAssetId("assets/textures/normal-uv1.png")},
        "Inactive Unlit normal branch leaked a UV1 requirement into legacy rendering transport or lost its authored dependency.");
    materialOutputNode(graph).output.shading=MaterialShading::StandardPbr;canonicalizeMaterialAsset(material);
    require(material.surface.normalTexture.image.id==canonicalAssetId("assets/textures/normal-uv1.png")&&material.surface.normalTexture.texCoord==1,
        "Returning to Lit did not restore the dormant normal branch and its UV selection.");
}
void usageOpacityAndSpecular() {
    auto graph=blank();const auto out=materialOutputNode(graph).id;
    const auto coverage=node(graph,MaterialNodeKind::Scalar);findMaterialNode(graph,coverage)->scalar=.4f;
    require(near(materialInputDefault(materialOutputNode(graph),MaterialPin::Specular).value[0],.5f),"Default Specular changed dielectric F0.");
    for(const auto shading:{MaterialShading::StandardPbr,MaterialShading::Unlit})for(const auto blend:{MaterialBlend::Opaque,MaterialBlend::Mask,MaterialBlend::Alpha,MaterialBlend::Additive}) {
        auto& settings=materialOutputNode(graph).output;settings.shading=shading;settings.blend=blend;
        validateMaterialGraph(graph);
        require(materialPinActive(shading,blend,MaterialPin::Opacity)==(blend==MaterialBlend::Alpha||blend==MaterialBlend::Additive)&&
            materialPinActive(shading,blend,MaterialPin::OpacityMask)==(blend==MaterialBlend::Mask),"Coverage pin activation does not match blend mode.");
        require(materialPinActive(shading,blend,MaterialPin::Specular)==(shading==MaterialShading::StandardPbr),"Specular activation does not match shader model.");
        MaterialAsset material;material.graph=graph;
        require(modelMaterialCompatible(material)&&particleMaterialCompatible(material),"Unified material shader/blend was rejected by a geometry consumer.");
    }
    auto& settings=materialOutputNode(graph).output;settings.shading=MaterialShading::StandardPbr;settings.blend=MaterialBlend::Mask;
    const auto mask=connectMaterialNodes(graph,coverage,MaterialPin::Value,out,MaterialPin::OpacityMask);
    materialOutputNode(graph).output.blend=MaterialBlend::Alpha;
    const auto alpha=connectMaterialNodes(graph,coverage,MaterialPin::Value,out,MaterialPin::Opacity);
    const auto before=serializeMaterialGraph(graph);
    rejects([&]{connectMaterialNodes(graph,coverage,MaterialPin::Value,out,MaterialPin::OpacityMask);},"Inactive coverage input accepted a new connection.");
    require(serializeMaterialGraph(graph)==before&&materialInputLink(graph,out,MaterialPin::OpacityMask)->id==mask&&materialInputLink(graph,out,MaterialPin::Opacity)->id==alpha,
        "Changing blend mode or rejecting an inactive input destroyed preserved coverage connections.");
    const auto uv=node(graph,MaterialNodeKind::UV),texture=node(graph,MaterialNodeKind::Texture2D);
    findMaterialNode(graph,uv)->uv.mode=MaterialUvMode::UV1;
    connectMaterialNodes(graph,uv,MaterialPin::UV,texture,MaterialPin::UV);connectMaterialNodes(graph,texture,MaterialPin::RGB,out,MaterialPin::BaseColor);
    MaterialAsset material;material.graph=graph;
    require(particleMaterialCompatible(material)&&modelMaterialCompatible(material),"Shared UV1 material was rejected by a geometry consumer.");
    const auto surface=serializeMaterialGraph(graph);
    require(serializeMaterialGraph(material.graph)==surface,"Compatibility altered shader, blend or mappings.");

}
void vectorMappingsAndUniformSafety() {
    auto graph=blank();const auto out=materialOutputNode(graph).id;
    const auto vector=node(graph,MaterialNodeKind::Vector2),scalar=node(graph,MaterialNodeKind::Scalar),multiply=node(graph,MaterialNodeKind::Multiply);
    const auto uv=node(graph,MaterialNodeKind::UV),first=node(graph,MaterialNodeKind::Texture2D),second=node(graph,MaterialNodeKind::Texture2D);
    findMaterialNode(graph,vector)->vector2={-2,3};findMaterialNode(graph,scalar)->scalar=4;
    findMaterialNode(graph,uv)->uv.scale={1,1,7};findMaterialNode(graph,uv)->uv.offset={2,4,9};findMaterialNode(graph,uv)->uv.rotation={10,20,30};
    connectMaterialNodes(graph,vector,MaterialPin::XY,multiply,MaterialPin::Left);connectMaterialNodes(graph,scalar,MaterialPin::Value,multiply,MaterialPin::Right);
    connectMaterialNodes(graph,multiply,MaterialPin::Value,uv,MaterialPin::ScaleXY);connectMaterialNodes(graph,vector,MaterialPin::XY,uv,MaterialPin::OffsetXY);
    connectMaterialNodes(graph,uv,MaterialPin::UV,first,MaterialPin::UV);connectMaterialNodes(graph,uv,MaterialPin::UV,second,MaterialPin::UV);
    connectMaterialNodes(graph,first,MaterialPin::RGB,out,MaterialPin::BaseColor);
    require(evaluateMaterialUniform(graph,multiply,MaterialPin::Value).value==Vec4{-8,12,0,0}&&
        evaluateMaterialUniform(graph,vector,MaterialPin::X).value[0]==-2&&evaluateMaterialUniform(graph,vector,MaterialPin::Y).value[0]==3,"Vector2 channels or scalar/vector multiplication failed.");
    for(const auto mode:{MaterialUvMode::UV0,MaterialUvMode::UV1,MaterialUvMode::LocalTriplanar,MaterialUvMode::WorldTriplanar}) {
        findMaterialNode(graph,uv)->uv.mode=mode;const auto mapping=materialTextureMapping(graph,first);
        require(mapping==materialTextureMapping(graph,second)&&mapping.scale==Vec3{-8,12,7}&&mapping.offset==Vec3{-2,3,9}&&mapping.rotation==Vec3{10,20,30},"Resolved mapping lost fan-out, signed XY or authored XYZ components.");
        const auto legacy=materialGraphLegacySurface(graph);require(legacy.uvScale==mapping.scale&&legacy.uvOffset==mapping.offset,"Legacy extraction did not use the uniform mapping resolver.");
    }
    const auto topology=materialGraphTopologyHash(graph,MaterialShading::StandardPbr,MaterialBlend::Opaque);
    const auto values=materialGraphValueHash(graph,MaterialShading::StandardPbr,MaterialBlend::Opaque);
    findMaterialNode(graph,scalar)->scalar=5;
    require(topology==materialGraphTopologyHash(graph,MaterialShading::StandardPbr,MaterialBlend::Opaque)&&values!=materialGraphValueHash(graph,MaterialShading::StandardPbr,MaterialBlend::Opaque),"Uniform UV parameter changed topology or failed to change its packet identity.");
    const auto before=serializeMaterialGraph(graph);
    rejects([&]{connectMaterialNodes(graph,second,MaterialPin::R,multiply,MaterialPin::Right);},"Upstream reconnection introduced a texture-dependent UV transform.");
    rejects([&]{connectMaterialNodes(graph,second,MaterialPin::RGB,multiply,MaterialPin::Right);},"Multiply accepted Color times Vector2.");
    require(serializeMaterialGraph(graph)==before,"Rejected Vector2 operation consumed identity or changed a link.");
    for(const auto kind:{MaterialNodeKind::ParticleColor,MaterialNodeKind::RadialGradient}) {
        const auto varying=node(graph,kind);const auto pin=kind==MaterialNodeKind::ParticleColor?MaterialPin::R:MaterialPin::Value;
        require(!materialExpressionUniform(graph,varying,pin),"Varying particle/procedural input was reported uniform.");
        rejects([&]{connectMaterialNodes(graph,varying,pin,multiply,MaterialPin::Right);},"Varying particle/procedural input entered a UV transform chain.");
    }
    const auto square=node(graph,MaterialNodeKind::Multiply);
    connectMaterialNodes(graph,vector,MaterialPin::XY,square,MaterialPin::Left);connectMaterialNodes(graph,vector,MaterialPin::XY,square,MaterialPin::Right);
    require(evaluateMaterialUniform(graph,square,MaterialPin::Value).value==Vec4{4,9,0,0},"Vector2 component multiplication failed.");
    const auto serialized=serializeMaterialGraph(graph);require(serializeMaterialGraph(parseMaterialGraph(serialized))==serialized,"Vector2 mapping graph failed schema 3 round-trip.");
    const auto scaleLink=materialInputLink(graph,uv,MaterialPin::ScaleXY)->id;removeMaterialLink(graph,scaleLink);
    require(resolveMaterialUvMapping(graph,uv).scale==Vec3{1,1,7},"Disconnecting a transform lost its authored fallback.");
}
void schemaOneMigration() {
    Project project;MaterialAsset material;material.id="assets/materials/schema-one.crimson-material";material.name="Old graph";material.graph=blank();
    const auto out=materialOutputNode(material.graph).id,scalar=node(material.graph,MaterialNodeKind::Scalar);
    findMaterialNode(material.graph,scalar)->scalar=.35f;materialOutputNode(material.graph).output.blend=MaterialBlend::Mask;
    const auto link=connectMaterialNodes(material.graph,scalar,MaterialPin::Value,out,MaterialPin::OpacityMask);
    for(const auto blend:{MaterialBlend::Opaque,MaterialBlend::Mask,MaterialBlend::Alpha,MaterialBlend::Additive}) {
        auto json=nlohmann::json::parse(serializeMaterialAsset(material));json["version"]=4;auto& graph=json["graph"];graph["version"]=1;
        auto& output=graph["nodes"][0]["output"];output.erase("usage");output.erase("specular");output.erase("opacityMask");
        output["particleSoftDisc"]=true;output["opacity"]=.7f;output["blend"]=static_cast<int>(blend);
        output["shading"]=static_cast<int>(blend==MaterialBlend::Alpha||blend==MaterialBlend::Additive?MaterialShading::Unlit:MaterialShading::StandardPbr);
        graph["links"][0]["toPin"]=static_cast<int>(MaterialPin::Opacity);
        const auto bytes=json.dump(2);write(materialAssetPath(project.root,material.id),bytes);const auto loaded=loadMaterialAsset(project.root,material.id);
        const bool particle=blend==MaterialBlend::Alpha||blend==MaterialBlend::Additive;
        const auto migrated=std::find_if(loaded.graph.links.begin(),loaded.graph.links.end(),[&](const auto& value){return value.id==link;});
        require(read(materialAssetPath(project.root,material.id))==bytes&&migrated!=loaded.graph.links.end()&&migrated->fromNode==scalar,
            "v4 migration rewrote bytes or lost stable coverage identities.");
        require(particle?(migrated->toPin==MaterialPin::Left):(migrated->toNode==out&&migrated->toPin==MaterialPin::OpacityMask),
            "Old coverage was not retargeted through visible particle modulation.");
        const auto& settings=materialOutputNode(loaded.graph).output;
        require(settings.specular==.5f&&(particle?settings.opacity:settings.opacityMask)==.7f,"v4 migration changed default coverage or Specular.");
        if(particle)require(near(::output(loaded.graph,MaterialPin::Opacity).value[0],.35f*.4f*.5625f),"Legacy particle opacity, tint alpha or soft disc was lost.");

    }
}
void explicitParticleMigration() {
    auto graph=blank();const auto root=materialOutputNode(graph).id;
    materialOutputNode(graph).output.shading=MaterialShading::Unlit;materialOutputNode(graph).output.blend=MaterialBlend::Alpha;
    const auto color=node(graph,MaterialNodeKind::Color),texture=node(graph,MaterialNodeKind::Texture2D);
    findMaterialNode(graph,color)->name="Authored HDR";findMaterialNode(graph,color)->color={12,3,2,.7f};
    const auto colorLink=connectMaterialNodes(graph,color,MaterialPin::RGB,root,MaterialPin::BaseColor);
    const auto alphaLink=connectMaterialNodes(graph,texture,MaterialPin::A,root,MaterialPin::Opacity);
    for(const bool image:{false,true})for(const bool soft:{false,true}) {
        findMaterialNode(graph,texture)->texture.image.id=image?canonicalAssetId("assets/textures/alpha.png"):"";
        auto serialized=nlohmann::json::parse(serializeMaterialGraph(graph));serialized["version"]=2;
        serialized["nodes"][0]["output"]["usage"]=1;serialized["nodes"][0]["output"]["particleSoftDisc"]=soft;
        const auto migrated=parseMaterialGraph(serialized.dump());
        require(migrated.nodes.size()==graph.nodes.size()+((soft&&!image)?6:4),"Legacy migration produced incorrect visible adapter nodes.");
        require(findMaterialNode(migrated,color)->name=="Authored HDR"&&findMaterialNode(migrated,color)->color==Vec4{12,3,2,.7f},"Migration changed a custom name or HDR value.");
        for(const auto id:{colorLink,alphaLink})require(std::any_of(migrated.links.begin(),migrated.links.end(),[&](const auto& l){return l.id==id&&l.toPin==MaterialPin::Left;}),"Migration replaced an original link identity.");
        require(near(output(migrated,MaterialPin::BaseColor).value[0],6)&&near(output(migrated,MaterialPin::Opacity).value[0],.5f*.4f*((soft&&!image)?.5625f:1.f)),
            "Visible migration changed legacy particle color or coverage.");
        const auto native=serializeMaterialGraph(migrated);
        require(native==serializeMaterialGraph(parseMaterialGraph(native))&&native.find("particleSoftDisc")==std::string::npos&&native.find("usage")==std::string::npos,
            "Native round-trip repeated migration or retained hidden particle semantics.");
        auto changed=migrated;findMaterialNode(changed,texture)->texture.image.id=image?"":canonicalAssetId("assets/textures/new.png");
        require(std::count_if(changed.nodes.begin(),changed.nodes.end(),[](const auto& n){return n.kind==MaterialNodeKind::RadialGradient;})==((soft&&!image)?1:0),
            "Image reassignment changed the explicit radial graph.");
        serialized["nodes"][0]["output"]["usage"]=0;
        require(serializeMaterialGraph(parseMaterialGraph(serialized.dump()))==serializeMaterialGraph(graph),"Schema 2 Surface graph gained particle adapters.");
    }
    auto bounded=blank();while(bounded.nodes.size()<128)addMaterialNode(bounded,MaterialNodeKind::Scalar);
    materialOutputNode(bounded).output.shading=MaterialShading::Unlit;materialOutputNode(bounded).output.blend=MaterialBlend::Alpha;
    migrateLegacyParticleGraph(bounded,true);require(bounded.nodes.size()==134,"Old graph at its node limit could not migrate losslessly.");
    auto hdr=blank();const auto hdrOut=materialOutputNode(hdr).id,hdrValue=node(hdr,MaterialNodeKind::Scalar);
    materialOutputNode(hdr).output.shading=MaterialShading::Unlit;materialOutputNode(hdr).output.blend=MaterialBlend::Alpha;
    findMaterialNode(hdr,hdrValue)->scalar=4;connectMaterialNodes(hdr,hdrValue,MaterialPin::Value,hdrOut,MaterialPin::Opacity);
    migrateLegacyParticleGraph(hdr,true);
    require(near(output(hdr,MaterialPin::Opacity).value[0],.4f*.5625f)&&findMaterialNode(hdr,hdrValue)->scalar==4,
        "Legacy coverage was clamped after particle alpha/disc or rewrote its authored HDR source.");
    const auto saturation=std::find_if(hdr.nodes.begin(),hdr.nodes.end(),[](const auto& n){return n.kind==MaterialNodeKind::Clamp;});
    require(saturation!=hdr.nodes.end()&&materialExpressionUniform(hdr,saturation->id,MaterialPin::Value)&&
        evaluateMaterialUniform(hdr,saturation->id,MaterialPin::Value).value[0]==1,"Uniform Saturate evaluation failed.");
    findMaterialNode(hdr,hdrValue)->scalar=-2;
    require(output(hdr,MaterialPin::Opacity).value[0]==0&&evaluateMaterialUniform(hdr,saturation->id,MaterialPin::Value).value[0]==0,
        "Saturate did not preserve lower coverage bounds.");
    const auto particle=defaultParticleMaterialAsset();require(near(output(particle.graph,MaterialPin::Opacity).value[0],.4f*.5625f),"Default particle graph lost explicit soft coverage.");
    auto plain=blank();materialOutputNode(plain).output.shading=MaterialShading::Unlit;materialOutputNode(plain).output.blend=MaterialBlend::Alpha;
    require(near(output(plain,MaterialPin::Opacity).value[0],1)&&plain.nodes.size()==1,"A new ordinary material gained particle modulation or a disc.");
}
void snapshotMaterials(const fs::path& root) {
    // Preserve the original version-2/version-6 migration snapshots under tests.
    // Project asset deletion or normal editing must not invalidate this suite.
    Project project;
    for(const char* fixture:{"legacy-v2-alpha.crimson-material","graph-v3-particle.crimson-material","graph-v3-world-triplanar.crimson-material"}) {
        const auto snapshot=root/"tests/fixtures/material-migration"/fixture;
        const auto before=read(snapshot);require(!before.empty(),"Expected immutable material migration fixture is unavailable.");
        const auto source=nlohmann::json::parse(before);const auto id=source.at("id").get<std::string>();
        write(project.root/id,before);const auto material=loadMaterialAsset(project.root,id);
        require(!material.graph.empty()&&material.id==canonicalAssetId(id)&&read(project.root/id)==before&&read(snapshot)==before,"Snapshot migration rewrote authored bytes or lost identity.");
        if(source.at("version").get<int>()<4) {
            require(material.surface.baseColor==source.at("baseColor").get<Vec4>()&&material.surface.roughness==source.at("roughness").get<float>()&&material.surface.metallic==source.at("metallic").get<float>(),"Snapshot migration changed material factors.");
            if(source.at("version")==3)require(material.surface.uvScale==source.at("uvScale").get<Vec3>()&&material.surface.triplanarWorld==source.value("triplanarWorld",false),"Snapshot migration changed world triplanar or scale.");
            const auto& texture=source.at("baseColorTexture");const auto& sampler=source.contains("sharedSampler")&&!source.at("sharedSampler").is_null()?source.at("sharedSampler"):texture;
            require(material.surface.baseColorTexture.image.id==canonicalAssetId(texture.at("image").get<std::string>())&&static_cast<int>(material.surface.baseColorTexture.minFilter)==sampler.at("minFilter").get<int>(),"Snapshot migration lost texture identity or filtering.");
        }
        std::cout<<"Snapshot material read without changes: "<<id<<'\n';
    }
}
void arithmeticAndAnimatedMappings() {
    auto graph=blank();
    const auto add=node(graph,MaterialNodeKind::Add),lerp=node(graph,MaterialNodeKind::Lerp),power=node(graph,MaterialNodeKind::Power);
    const auto time=node(graph,MaterialNodeKind::Time),panner=node(graph,MaterialNodeKind::Panner),uv=node(graph,MaterialNodeKind::UV);
    const auto vector=node(graph,MaterialNodeKind::Vector2),texture=node(graph,MaterialNodeKind::Texture2D),color=node(graph,MaterialNodeKind::Color);
    require(evaluateMaterialUniform(graph,add,MaterialPin::Value).value[0]==1&&evaluateMaterialUniform(graph,lerp,MaterialPin::Value).value[0]==.5f&&
        evaluateMaterialUniform(graph,power,MaterialPin::Value).value[0]==0,"Arithmetic node defaults differ from the authoring contract.");
    findMaterialNode(graph,lerp)->alpha.value={2,2,2,2};
    require(evaluateMaterialUniform(graph,lerp,MaterialPin::Value).value[0]==2,"Lerp clamped its interpolation factor.");
    findMaterialNode(graph,vector)->vector2={2,-3};
    connectMaterialNodes(graph,vector,MaterialPin::XY,add,MaterialPin::Left);
    require(materialOutputType(graph,add,MaterialPin::Value)==MaterialValueType::Vector2&&evaluateMaterialUniform(graph,add,MaterialPin::Value).value[1]==-2,
        "Add did not broadcast scalar addition over Vector2.");
    rejects([&]{connectMaterialNodes(graph,color,MaterialPin::RGB,add,MaterialPin::Right);},"Add mixed Vector2 and Color.");
    const auto leftLink=materialInputLink(graph,add,MaterialPin::Left)->id;
    removeMaterialLink(graph,leftLink);
    require(findMaterialNode(graph,add)->left.type==MaterialValueType::Vector2,"Disconnected Add lost its vector default type.");
    findMaterialNode(graph,power)->left={MaterialValueType::Color,{.5f,-1,0,0}};
    require(evaluateMaterialUniform(graph,power,MaterialPin::Value).value==Vec4{.25f,0,0,0},"Power did not clamp nonpositive bases component-wise.");
    findMaterialNode(graph,color)->color={3,99,99,1};
    connectMaterialNodes(graph,color,MaterialPin::RGB,power,MaterialPin::Right);
    require(evaluateMaterialUniform(graph,power,MaterialPin::Value).value[0]==.125f,"Power Exponent did not use receiving-input first-component conversion.");
    require(materialExpressionUniform(graph,time,MaterialPin::Value)&&materialExpressionTimeDependent(graph,time,MaterialPin::Value),"Time was not classified as runtime uniform.");
    require(evaluateMaterialUniform(graph,time,MaterialPin::Value,{7,11}).value[0]==7,"Time did not use game seconds by default.");
    findMaterialNode(graph,time)->time={true,true,3};
    require(evaluateMaterialUniform(graph,time,MaterialPin::Value,{7,11}).value[0]==2,"Time period/real-time selection is incorrect.");
    findMaterialNode(graph,time)->time.period=0;
    require(evaluateMaterialUniform(graph,time,MaterialPin::Value,{7,11}).value[0]==0,"A zero enabled Time period did not stop time.");
    findMaterialNode(graph,time)->time.periodEnabled=false;
    findMaterialNode(graph,uv)->uv={MaterialUvMode::WorldTriplanar,{-2,3,4},{1,2,3},{10,20,30},false};
    findMaterialNode(graph,panner)->panner.speed={.25f,-.5f};
    connectMaterialNodes(graph,uv,MaterialPin::UV,panner,MaterialPin::UV);
    connectMaterialNodes(graph,panner,MaterialPin::UV,texture,MaterialPin::UV);
    const auto before=serializeMaterialGraph(graph);
    const auto valueHash=materialGraphValueHash(graph,MaterialShading::StandardPbr,MaterialBlend::Opaque);
    auto mapping=materialTextureMapping(graph,texture,{2,11});
    require(mapping.mode==MaterialUvMode::WorldTriplanar&&mapping.scale==Vec3{-2,3,4}&&mapping.rotation==Vec3{10,20,30}&&mapping.offset==Vec3{1.5f,1,3},
        "Panner lost existing mapping mode, affine fields or signed speed.");
    findMaterialNode(graph,panner)->panner.fractionalPart=true;
    mapping=materialTextureMapping(graph,texture,{3,11});
    require(mapping.offset==Vec3{1.75f,2.5f,3},"Panner fractional mode wrapped the source coordinates rather than just displacement.");
    rejects([&]{connectMaterialNodes(graph,texture,MaterialPin::RGB,panner,MaterialPin::Time);},"Panner accepted fragment-varying Time.");
    connectMaterialNodes(graph,vector,MaterialPin::XY,panner,MaterialPin::Speed);
    connectMaterialNodes(graph,time,MaterialPin::Value,panner,MaterialPin::Time);
    mapping=materialTextureMapping(graph,texture,{7,1.25});
    require(mapping.offset==Vec3{1.5f,2.25f,3},"Panner did not consume connected uniform Speed and Time.");
    const auto serialized=serializeMaterialGraph(graph);
    require(nlohmann::json::parse(serialized).at("version")==14&&serializeMaterialGraph(parseMaterialGraph(serialized))==serialized,"Current schema14 nodes failed round-trip.");
    (void)materialTextureMapping(graph,texture,{1,17});
    require(serializeMaterialGraph(graph)==serialized,"Frame evaluation mutated the authored graph.");
    auto old=nlohmann::json::parse(before);old["version"]=3;
    require(parseMaterialGraph(old.dump()).nodes.size()==graph.nodes.size(),"Schema 3 compatibility was lost.");
    require(valueHash==materialGraphValueHash(parseMaterialGraph(before),MaterialShading::StandardPbr,MaterialBlend::Opaque),"Frame evaluation changed an authored hash.");
}
void imageAdjustmentsAndGrunge() {
    require(static_cast<int>(MaterialNodeKind::Clamp)==9&&static_cast<int>(MaterialNodeKind::Power)==14&&
        static_cast<int>(MaterialNodeKind::Desaturation)==15,"New kinds renumbered existing serialized nodes.");
    auto graph=blank();const auto root=materialOutputNode(graph).id;
    const auto desaturation=node(graph,MaterialNodeKind::Desaturation),contrast=node(graph,MaterialNodeKind::Contrast),
        contrastRgb=node(graph,MaterialNodeKind::ContrastRGB),hue=node(graph,MaterialNodeKind::HueShift),grunge=node(graph,MaterialNodeKind::Grunge);
    for(const auto id:{desaturation,contrast,contrastRgb,hue}) {
        const auto& n=*findMaterialNode(graph,id);
        require(n.left.type==MaterialValueType::Color&&n.left.value==Vec4{1,1,1,1}&&n.right.type==MaterialValueType::Scalar&&
            n.right.value[0]==(id==hue?0.f:1.f),"Image adjustment defaults are inconsistent.");
        require(materialExpressionUniform(graph,id,MaterialPin::RGB),"Constant image adjustment was classified as varying.");
    }
    auto rgb=[&](MaterialNodeId id){const auto value=evaluateMaterialUniform(graph,id,MaterialPin::RGB).value;return Vec3{value[0],value[1],value[2]};};
    const auto equalRgb=[](Vec3 a,Vec3 b){return std::abs(a[0]-b[0])<3e-6f&&std::abs(a[1]-b[1])<3e-6f&&std::abs(a[2]-b[2])<3e-6f;};
    findMaterialNode(graph,desaturation)->left.value={1,0,0,1};findMaterialNode(graph,desaturation)->right.value[0]=0;
    require(equalRgb(rgb(desaturation),{.2126f,.2126f,.2126f}),"Desaturation did not use linear luminance.");
    findMaterialNode(graph,desaturation)->right.value[0]=1;
    require(equalRgb(rgb(desaturation),{1,0,0}),"Desaturation one did not preserve its input.");
    findMaterialNode(graph,desaturation)->right.value[0]=2;
    require(equalRgb(rgb(desaturation),{1.7874f,-.2126f,-.2126f}),"Increased saturation clamped signed/HDR output.");
    require(near(evaluateMaterialUniform(graph,desaturation,MaterialPin::G).value[0],-.2126f),"Adjustment channel output lost its selected channel.");
    findMaterialNode(graph,contrast)->left.value={1,0,0,1};findMaterialNode(graph,contrast)->right.value[0]=2;
    require(near(evaluateMaterialUniform(graph,contrast,MaterialPin::Value).value[0],-.0748f)&&equalRgb(rgb(contrast),{-.0748f,-.0748f,-.0748f}),
        "Contrast failed grayscale conversion, unclamped range or RGB expansion.");
    findMaterialNode(graph,contrast)->right.value[0]=0;
    require(equalRgb(rgb(contrast),{.5f,.5f,.5f}),"Zero Contrast did not produce middle gray.");
    findMaterialNode(graph,contrastRgb)->left.value={-1,.5f,3,1};findMaterialNode(graph,contrastRgb)->right.value[0]=2;
    require(equalRgb(rgb(contrastRgb),{-2.5f,.5f,5.5f}),"ContrastRGB altered channels or clipped signed/HDR values.");
    findMaterialNode(graph,hue)->left.value={2,-1,-1,1};findMaterialNode(graph,hue)->right.value[0]=120;
    require(equalRgb(rgb(hue),{-1,2,-1}),"HueShift failed a signed/HDR red-to-green rotation.");
    findMaterialNode(graph,hue)->right.value[0]=-120;
    require(equalRgb(rgb(hue),{-1,-1,2}),"Negative HueShift failed degree wrapping.");
    findMaterialNode(graph,hue)->right.value[0]=720;
    require(rgb(hue)==Vec3{2,-1,-1},"A complete hue rotation altered its input.");
    findMaterialNode(graph,hue)->left.value={-2,-2,-2,1};findMaterialNode(graph,hue)->right.value[0]=137;
    require(rgb(hue)==Vec3{-2,-2,-2},"HueShift altered an achromatic signed input.");
    const auto scalar=node(graph,MaterialNodeKind::Scalar),color=node(graph,MaterialNodeKind::Color),vector=node(graph,MaterialNodeKind::Vector2),
        texture=node(graph,MaterialNodeKind::Texture2D),uv=node(graph,MaterialNodeKind::UV),time=node(graph,MaterialNodeKind::Time);
    findMaterialNode(graph,scalar)->scalar=.25f;
    const auto fallback=findMaterialNode(graph,hue)->left;
    const auto scalarLink=connectMaterialNodes(graph,scalar,MaterialPin::Value,hue,MaterialPin::Left);
    require(rgb(hue)==Vec3{.25f,.25f,.25f},"Image adjustment did not broadcast a Scalar Input.");
    removeMaterialLink(graph,scalarLink);
    require(findMaterialNode(graph,hue)->left==fallback,"Disconnecting an adjustment changed its authored Input fallback.");
    rejects([&]{connectMaterialNodes(graph,vector,MaterialPin::XY,hue,MaterialPin::Left);},"Image adjustment accepted Vector2.");
    rejects([&]{connectMaterialNodes(graph,uv,MaterialPin::UV,hue,MaterialPin::Left);},"Image adjustment accepted Mapping.");
    rejects([&]{connectMaterialNodes(graph,scalar,MaterialPin::Value,root,MaterialPin::BaseColor);},"Image adjustments globally loosened Scalar-to-Color typing.");
    findMaterialNode(graph,color)->color={2,99,99,1};
    const auto factorLink=connectMaterialNodes(graph,color,MaterialPin::RGB,desaturation,MaterialPin::Right);
    require(equalRgb(rgb(desaturation),{1.7874f,-.2126f,-.2126f}),"Adjustment factor did not consume the receiving input's first component.");
    removeMaterialLink(graph,factorLink);
    connectMaterialNodes(graph,time,MaterialPin::Value,hue,MaterialPin::Right);
    require(materialExpressionUniform(graph,hue,MaterialPin::RGB)&&materialExpressionTimeDependent(graph,hue,MaterialPin::RGB),
        "Animated image adjustment lost uniform/time classification.");
    connectMaterialNodes(graph,desaturation,MaterialPin::RGB,root,MaterialPin::BaseColor);
    const auto topology=materialGraphTopologyHash(graph,MaterialShading::StandardPbr,MaterialBlend::Opaque),values=materialGraphValueHash(graph,MaterialShading::StandardPbr,MaterialBlend::Opaque);
    findMaterialNode(graph,desaturation)->right.value[0]=3;
    require(topology==materialGraphTopologyHash(graph,MaterialShading::StandardPbr,MaterialBlend::Opaque)&&values!=materialGraphValueHash(graph,MaterialShading::StandardPbr,MaterialBlend::Opaque),
        "Image adjustment parameter edit changed topology or failed to change value identity.");
    const auto fixed=materialGraphFixedSurface(graph);
    require(fixed&&near(fixed->baseColor[0],rgb(desaturation)[0]),"Uniform adjustment was not reduced into exact fixed-surface factors.");
    connectMaterialNodes(graph,texture,MaterialPin::RGB,desaturation,MaterialPin::Left);
    require(!materialExpressionUniform(graph,desaturation,MaterialPin::RGB)&&!materialGraphFixedSurface(graph),"Textured adjustment was evaluated as a constant/fixed surface.");
    require(materialOutputType(graph,grunge,MaterialPin::Value)==MaterialValueType::Scalar&&materialOutputType(graph,grunge,MaterialPin::RGB)==MaterialValueType::Color&&
        !materialExpressionUniform(graph,grunge,MaterialPin::Value),"Grunge is not a general varying Scalar/RGB source.");
    connectMaterialNodes(graph,uv,MaterialPin::UV,grunge,MaterialPin::UV);
    connectMaterialNodes(graph,grunge,MaterialPin::RGB,root,MaterialPin::BaseColor);
    connectMaterialNodes(graph,grunge,MaterialPin::Value,root,MaterialPin::Roughness);
    require(!materialGraphFixedSurface(graph),"Procedural Grunge was certified as a fixed surface.");
    const auto grungeTopology=materialGraphTopologyHash(graph,MaterialShading::StandardPbr,MaterialBlend::Opaque),grungeValues=materialGraphValueHash(graph,MaterialShading::StandardPbr,MaterialBlend::Opaque);
    findMaterialNode(graph,grunge)->grunge={65535,64,1,0,.75f};
    require(grungeTopology==materialGraphTopologyHash(graph,MaterialShading::StandardPbr,MaterialBlend::Opaque)&&grungeValues!=materialGraphValueHash(graph,MaterialShading::StandardPbr,MaterialBlend::Opaque),
        "Grunge controls changed shader topology or failed to change values.");
    auto& grungeSettings=findMaterialNode(graph,grunge)->grunge;
    grungeSettings.baseNoisiness=.2f;grungeSettings.dirtSpecksOpacity=.3f;grungeSettings.invertDirt=true;
    grungeSettings.scratchesOpacity=.4f;grungeSettings.sharpen=.5f;grungeSettings.largeVariationIntensity=.6f;
    grungeSettings.stainsOpacity=.7f;grungeSettings.leaksOpacity=.8f;
    const auto bytes=serializeMaterialGraph(graph);require(serializeMaterialGraph(parseMaterialGraph(bytes))==bytes,"Schema 7 image adjustments/Grunge failed round-trip.");
    const auto current=nlohmann::json::parse(bytes);
    for(const auto& n:current["nodes"])if(n["id"]==grunge)require(!n["grunge"].contains("style")&&n["grunge"].contains("stainsOpacity")&&n["grunge"].contains("leaksOpacity"),"Current Grunge persistence retained a Style selector.");
    for(const int version:{5,6})for(const int style:{0,1,2,3}) {
        auto prior=current;prior["version"]=version;
        for(auto& n:prior["nodes"])if(n["id"]==grunge) {
            n["grunge"]["style"]=style;n["grunge"].erase("stainsOpacity");n["grunge"].erase("leaksOpacity");
            if(version==5)for(const auto property:{"baseNoisiness","dirtSpecksOpacity","invertDirt","scratchesOpacity","sharpen","largeVariationIntensity"})n["grunge"].erase(property);
        }
        const auto loadedPrior=parseMaterialGraph(prior.dump());const auto& oldSettings=findMaterialNode(loadedPrior,grunge)->grunge;
        require(oldSettings.baseNoisiness==(version==5?.55f:.2f)&&oldSettings.dirtSpecksOpacity==(version==5?.62f:.3f)&&oldSettings.invertDirt==(version==6)&&
            oldSettings.scratchesOpacity==(version==5?.84f:.4f)&&oldSettings.sharpen==(version==5?.63f:.5f)&&oldSettings.largeVariationIntensity==(version==5?.9f:.6f)&&
            oldSettings.seed==65535&&oldSettings.tiling==64&&oldSettings.coverage==1&&oldSettings.disorder==0&&oldSettings.detail==.75f&&
            oldSettings.stainsOpacity==(style==1?1.f:style==0?.35f:0.f)&&oldSettings.leaksOpacity==(style==3?1.f:0.f),
            "Prior Grunge style conversion lost authored values or layer defaults.");
        require(loadedPrior.links==graph.links&&loadedPrior.nextNodeId==graph.nextNodeId&&loadedPrior.nextLinkId==graph.nextLinkId,
            "Grunge style conversion changed links or persistent identities.");
        auto upgraded=nlohmann::json::parse(serializeMaterialGraph(loadedPrior));require(upgraded["version"]==14,"Legacy Grunge did not write the current schema.");
        for(const auto& n:upgraded["nodes"])if(n["id"]==grunge)require(!n["grunge"].contains("style"),"Legacy Grunge style survived its one-time conversion.");
        for(auto& n:prior["nodes"])if(n["id"]==grunge)n["grunge"]["style"]=4;
        rejects([&]{parseMaterialGraph(prior.dump());},"Invalid legacy Grunge style was accepted.");
    }
    require(materialTextureMapping(graph,grunge)==resolveMaterialUvMapping(graph,uv),"Grunge did not resolve its authored texture Mapping.");
    for(const auto property:{"seed","tiling"}) {
        auto json=nlohmann::json::parse(bytes);for(auto& n:json["nodes"])if(n["id"]==grunge)n["grunge"][property]=1.5;
        rejects([&]{parseMaterialGraph(json.dump());},"Fractional Grunge integer setting was accepted.");
        for(auto& n:json["nodes"])if(n["id"]==grunge)n["grunge"][property]=std::uint64_t{4294967297};
        rejects([&]{parseMaterialGraph(json.dump());},"Oversized Grunge integer setting wrapped into an accepted value.");
    }
    const auto invalidGrunge=[&](const auto& mutation){auto bad=graph;mutation(findMaterialNode(bad,grunge)->grunge);rejects([&]{validateMaterialGraph(bad);},"Invalid Grunge controls were accepted.");};
    invalidGrunge([](auto& g){g.seed=-1;});invalidGrunge([](auto& g){g.seed=65536;});
    invalidGrunge([](auto& g){g.tiling=0;});invalidGrunge([](auto& g){g.tiling=65;});invalidGrunge([](auto& g){g.coverage=1.1f;});
    invalidGrunge([](auto& g){g.disorder=-.1f;});invalidGrunge([](auto& g){g.detail=std::numeric_limits<float>::infinity();});
    invalidGrunge([](auto& g){g.baseNoisiness=-.1f;});invalidGrunge([](auto& g){g.dirtSpecksOpacity=1.1f;});
    invalidGrunge([](auto& g){g.scratchesOpacity=std::numeric_limits<float>::quiet_NaN();});invalidGrunge([](auto& g){g.sharpen=-1;});
    invalidGrunge([](auto& g){g.largeVariationIntensity=2;});
    invalidGrunge([](auto& g){g.stainsOpacity=-1;});invalidGrunge([](auto& g){g.leaksOpacity=2;});
    auto bad=graph;findMaterialNode(bad,hue)->right.value[0]=std::numeric_limits<float>::quiet_NaN();
    rejects([&]{validateMaterialGraph(bad);},"Nonfinite adjustment factor was accepted.");
    auto legacy=blank();const auto clamp=node(legacy,MaterialNodeKind::Clamp);findMaterialNode(legacy,clamp)->left.value[0]=2;
    auto json=nlohmann::json::parse(serializeMaterialGraph(legacy));json["version"]=4;
    for(auto& n:json["nodes"])if(n["id"]==clamp)n["name"]="Saturate";
    const auto converted=parseMaterialGraph(json.dump());
    require(findMaterialNode(converted,clamp)->name=="Clamp"&&evaluateMaterialUniform(converted,clamp,MaterialPin::Value).value[0]==1,
        "Legacy ordinal 9 did not retain clamp semantics/default-name migration.");
    for(auto& n:json["nodes"])if(n["id"]==clamp)n["name"]="Authored clamp name";
    require(findMaterialNode(parseMaterialGraph(json.dump()),clamp)->name=="Authored clamp name","Clamp migration overwrote a custom name.");
    json["version"]=5;for(auto& n:json["nodes"])if(n["id"]==clamp)n["name"]="Saturate";
    require(findMaterialNode(parseMaterialGraph(json.dump()),clamp)->name=="Saturate","Current-schema custom names were migrated again.");
    auto samplerGraph=blank();const auto samplerOutput=materialOutputNode(samplerGraph).id;
    MaterialNodeId previous{};
    for(unsigned i=0;i<12;++i) {
        const auto imageNode=node(samplerGraph,i%2?MaterialNodeKind::Grunge:MaterialNodeKind::Texture2D);
        if(previous) {
            const auto multiply=node(samplerGraph,MaterialNodeKind::Multiply);
            connectMaterialNodes(samplerGraph,previous,i==1?MaterialPin::RGB:MaterialPin::Value,multiply,MaterialPin::Left);
            connectMaterialNodes(samplerGraph,imageNode,MaterialPin::RGB,multiply,MaterialPin::Right);previous=multiply;
        } else previous=imageNode;
    }
    connectMaterialNodes(samplerGraph,previous,MaterialPin::Value,samplerOutput,MaterialPin::BaseColor);
    const auto extra=node(samplerGraph,MaterialNodeKind::Grunge),extraMultiply=node(samplerGraph,MaterialNodeKind::Multiply);
    connectMaterialNodes(samplerGraph,previous,MaterialPin::Value,extraMultiply,MaterialPin::Left);
    connectMaterialNodes(samplerGraph,extra,MaterialPin::RGB,extraMultiply,MaterialPin::Right);
    const auto bounded=serializeMaterialGraph(samplerGraph);
    rejects([&]{connectMaterialNodes(samplerGraph,extraMultiply,MaterialPin::Value,samplerOutput,MaterialPin::BaseColor);},"The combined Texture 2D/Grunge sampler budget was ignored.");
    require(serializeMaterialGraph(samplerGraph)==bounded,"Rejected sampler-budget overflow modified the graph.");
}
void imageNodeFoundation() {
    auto graph=blank();const auto out=materialOutputNode(graph).id;
    const auto merge=node(graph,MaterialNodeKind::RGBAMerge),split=node(graph,MaterialNodeKind::RGBASplit),alphaSplit=node(graph,MaterialNodeKind::AlphaSplit),alphaMerge=node(graph,MaterialNodeKind::AlphaMerge);
    require(!setMaterialInputDefault(*findMaterialNode(graph,merge),MaterialPin::Left,MaterialValue{}),"RGBA Merge accepted a nonexistent fallback input.");
    const Vec4 packed{.2f,.4f,.8f,0};for(unsigned i=0;i<4;++i){const auto p=i==0?MaterialPin::R:i==1?MaterialPin::G:i==2?MaterialPin::B:MaterialPin::A;require(setMaterialInputDefault(*findMaterialNode(graph,merge),p,{MaterialValueType::Scalar,{packed[i],packed[i],packed[i],packed[i]}}),"RGBA fallback setter rejected a lane.");}
    connectMaterialNodes(graph,merge,MaterialPin::RGBA,split,MaterialPin::Left);connectMaterialNodes(graph,merge,MaterialPin::RGBA,alphaSplit,MaterialPin::Left);
    for(unsigned i=0;i<4;++i){const auto p=i==0?MaterialPin::R:i==1?MaterialPin::G:i==2?MaterialPin::B:MaterialPin::A;require(near(evaluateMaterialUniform(graph,split,p).value[0],packed[i]),"RGBA split lost or reordered a channel.");}
    require(evaluateMaterialUniform(graph,alphaSplit,MaterialPin::RGB).value==Vec4{.2f,.4f,.8f,1},"Alpha Split premultiplied or erased RGB at zero alpha.");
    require(evaluateMaterialUniform(graph,alphaSplit,MaterialPin::A).value[0]==0,"Alpha Split ignored original alpha.");
    connectMaterialNodes(graph,alphaSplit,MaterialPin::RGB,alphaMerge,MaterialPin::Left);connectMaterialNodes(graph,alphaSplit,MaterialPin::A,alphaMerge,MaterialPin::Alpha);
    require(evaluateMaterialUniform(graph,alphaMerge,MaterialPin::RGBA).value==packed,"Alpha Merge failed lossless straight-alpha reconstruction.");
    connectMaterialNodes(graph,merge,MaterialPin::RGB,split,MaterialPin::Left);require(evaluateMaterialUniform(graph,split,MaterialPin::A).value[0]==1,"RGB-to-RGBA did not supply opaque alpha.");
    connectMaterialNodes(graph,merge,MaterialPin::RGBA,split,MaterialPin::Left);
    const auto invert=node(graph,MaterialNodeKind::Invert),levels=node(graph,MaterialNodeKind::Levels),bc=node(graph,MaterialNodeKind::BrightnessContrast),sharpen=node(graph,MaterialNodeKind::Sharpen);
    connectMaterialNodes(graph,merge,MaterialPin::RGBA,invert,MaterialPin::Left);connectMaterialNodes(graph,invert,MaterialPin::Value,levels,MaterialPin::Left);connectMaterialNodes(graph,levels,MaterialPin::Value,bc,MaterialPin::Left);connectMaterialNodes(graph,bc,MaterialPin::Value,sharpen,MaterialPin::Left);
    require(materialOutputType(graph,sharpen,MaterialPin::Value)==MaterialValueType::Color4,"Image operators discarded RGBA type.");
    {auto disconnected=graph;const auto link=materialInputLink(disconnected,invert,MaterialPin::Left)->id;require(removeMaterialLink(disconnected,link),"Image disconnection failed.");require(materialOutputType(disconnected,invert,MaterialPin::Value)==MaterialValueType::Color4&&materialInputDefault(*findMaterialNode(disconnected,invert),MaterialPin::Left).value[3]==1,"Image disconnection did not retain RGBA type and its authored fallback.");validateMaterialGraph(disconnected);}
    const auto adjusted=evaluateMaterialUniform(graph,sharpen,MaterialPin::Value);require(near(adjusted.value[0],.8f)&&near(adjusted.value[1],.6f)&&near(adjusted.value[2],.2f)&&adjusted.value[3]==0,"Neutral image adjustments or alpha preservation failed.");
    findMaterialNode(graph,levels)->image.outputWhite=0;require(evaluateMaterialUniform(graph,levels,MaterialPin::Value).value==Vec4{0,0,0,0},"Levels output collapse changed alpha.");findMaterialNode(graph,levels)->image.outputWhite=1;
    findMaterialNode(graph,bc)->image.brightness=1;require(evaluateMaterialUniform(graph,bc,MaterialPin::Value).value==Vec4{1,1,1,0},"Brightness white endpoint/alpha failed.");findMaterialNode(graph,bc)->image.brightness=-1;require(evaluateMaterialUniform(graph,bc,MaterialPin::Value).value==Vec4{0,0,0,0},"Brightness black endpoint/alpha failed.");findMaterialNode(graph,bc)->image.brightness=0;
    const auto blend=node(graph,MaterialNodeKind::Blend),gray=node(graph,MaterialNodeKind::Grayscale),toColor=node(graph,MaterialNodeKind::ToColor);
    connectMaterialNodes(graph,merge,MaterialPin::RGBA,blend,MaterialPin::Left);findMaterialNode(graph,blend)->right={MaterialValueType::Color4,{.8f,.2f,.1f,1}};
    findMaterialNode(graph,blend)->image.opacity=0;require(evaluateMaterialUniform(graph,blend,MaterialPin::Value).value==packed,"Blend opacity zero changed its base.");
    findMaterialNode(graph,blend)->image.opacity=1;findMaterialNode(graph,blend)->alpha.value={0,0,0,0};require(evaluateMaterialUniform(graph,blend,MaterialPin::Value).value==packed,"Blend mask zero changed its base.");
    findMaterialNode(graph,blend)->alpha.value={1,1,1,1};require(evaluateMaterialUniform(graph,blend,MaterialPin::Value).value==Vec4{.8f,.2f,.1f,1},"Blend Mix endpoint failed.");
    {auto endpoints=graph;findMaterialNode(endpoints,merge)->color[3]=.8f;findMaterialNode(endpoints,blend)->right.value[3]=.1f;require(evaluateMaterialUniform(endpoints,blend,MaterialPin::Value).value==Vec4{.8f,.2f,.1f,.1f},"Blend full-mask RGB/alpha endpoints suffered float cancellation.");findMaterialNode(endpoints,merge)->color={3,-.5f,.8f,.2f};for(int mode=0;mode<=10;++mode){auto& n=*findMaterialNode(endpoints,blend);n.image.blendMode=static_cast<MaterialImageBlend>(mode);n.image.opacity=0;require(evaluateMaterialUniform(endpoints,blend,MaterialPin::Value).value==Vec4{3,-.5f,.8f,.2f},"Blend disabled opacity clipped HDR base data.");}}
    for(int mode=0;mode<=10;++mode){findMaterialNode(graph,blend)->image.blendMode=static_cast<MaterialImageBlend>(mode);const auto v=evaluateMaterialUniform(graph,blend,MaterialPin::Value);require(std::all_of(v.value.begin(),v.value.end(),[](float x){return std::isfinite(x);}),"Blend mode produced a nonfinite result.");}
    connectMaterialNodes(graph,merge,MaterialPin::RGBA,gray,MaterialPin::Left);findMaterialNode(graph,gray)->image.grayMode=MaterialGrayscaleMode::Alpha;require(evaluateMaterialUniform(graph,gray,MaterialPin::Value).value[0]==0,"Grayscale alpha selection failed.");findMaterialNode(graph,gray)->image.grayMode=MaterialGrayscaleMode::Luminance;require(near(evaluateMaterialUniform(graph,gray,MaterialPin::Value).value[0],.38636f),"Grayscale Rec709 conversion failed.");
    connectMaterialNodes(graph,gray,MaterialPin::Value,toColor,MaterialPin::Left);require(evaluateMaterialUniform(graph,toColor,MaterialPin::RGBA).value[3]==1,"To Color did not supply opaque alpha.");
    const auto noise=node(graph,MaterialNodeKind::Noise),h2n=node(graph,MaterialNodeKind::HeightToNormal);connectMaterialNodes(graph,noise,MaterialPin::Value,h2n,MaterialPin::Left);require(!materialExpressionUniform(graph,noise,MaterialPin::Value)&&!materialExpressionUniform(graph,h2n,MaterialPin::Normal),"Spatial procedural nodes were classified as uniform.");
    auto a=findMaterialNode(graph,noise)->image;const auto n0=sampleMaterialNoise({.17f,-.23f,.61f},a);require(n0==sampleMaterialNoise({.17f,-.23f,.61f},a),"Noise is not deterministic.");a.seed=314;require(std::abs(n0-sampleMaterialNoise({.17f,-.23f,.61f},a))>.0001f,"Noise seed did not change the field.");require(near(sampleMaterialNoise({.17f,-.23f,.61f},a,100),.5f),"Unresolved Clouds did not converge to its mean.");a.noisePattern=MaterialNoisePattern::Spots;const auto spot=sampleMaterialNoise({.17f,-.23f,.61f},a,100);require(spot>0&&spot<.1f,"Unresolved Spots did not converge to its measured mean.");
    connectMaterialNodes(graph,sharpen,MaterialPin::Value,out,MaterialPin::BaseColor);
    const auto json=serializeMaterialGraph(graph);require(json.find("\"version\":14")!=std::string::npos,"New nodes did not write graph schema 14.");require(serializeMaterialGraph(parseMaterialGraph(json))==json,"Image node settings, outputs or IDs failed round trip.");
    auto bad=graph;findMaterialNode(bad,levels)->image.inputWhite=0;rejects([&]{validateMaterialGraph(bad);},"Levels accepted an empty input interval.");
    const auto vector=node(graph,MaterialNodeKind::Vector2);rejects([&]{connectMaterialNodes(graph,vector,MaterialPin::XY,invert,MaterialPin::Left);},"Vector2 connected to an image node.");
    auto& settings=findMaterialNode(graph,blend)->image;const auto topology=materialGraphTopologyHash(graph,MaterialShading::StandardPbr,MaterialBlend::Opaque),values=materialGraphValueHash(graph,MaterialShading::StandardPbr,MaterialBlend::Opaque);settings.contrast=.2f;require(materialGraphTopologyHash(graph,MaterialShading::StandardPbr,MaterialBlend::Opaque)==topology,"A numeric image setting changed topology identity.");require(materialGraphValueHash(graph,MaterialShading::StandardPbr,MaterialBlend::Opaque)==values,"An inactive image branch affected active material values.");
}
// Exercise artist controls on nonuniform, asymmetric data. A hash changing is
// insufficient: these checks require changed pixels and, where meaningful,
// an independently predictable direction or exact numeric result.
void originalNodePropertySemantics() {
    auto graph=blank();const auto source=node(graph,MaterialNodeKind::Texture2D);
    findMaterialNode(graph,source)->texture.image.id=canonicalAssetId("assets/textures/property-fixture.png");
    findMaterialNode(graph,source)->texture.role=MaterialTextureRole::Data;
    auto pixels=std::make_shared<DecodedImage>();pixels->width=16;pixels->height=12;pixels->rgba.resize(16*12*4);
    for(unsigned y=0;y<12;++y)for(unsigned x=0;x<16;++x) {
        const auto i=(y*16+x)*4;
        pixels->rgba[i]=static_cast<std::uint8_t>((x*x*11+y*23+17)%256);
        pixels->rgba[i+1]=static_cast<std::uint8_t>((x*31+y*y*7+63)%256);
        pixels->rgba[i+2]=static_cast<std::uint8_t>((x*13+y*47+119)%256);
        pixels->rgba[i+3]=static_cast<std::uint8_t>((x*19+y*29+37)%256);
    }
    const auto resolve=[&](const std::string&)->std::shared_ptr<const DecodedImage>{return pixels;};
    const auto image=[&](const MaterialGraph& g,MaterialNodeId target,MaterialPin pin=MaterialPin::Value) {
        return bakeMaterialGraphImagePreview(g,{target,pin},resolve,{16,12});
    };
    const auto differs=[](const FloatImage& a,const FloatImage& b) {
        if(a.width!=b.width||a.height!=b.height)return true;
        unsigned changed{};for(std::size_t i=0;i<a.rgba.size();++i)changed+=std::abs(a.rgba[i]-b.rgba[i])>1e-5f;
        return changed>8;
    };
    const auto branch=[&](MaterialNodeKind kind,MaterialPin sourcePin=MaterialPin::RGBA) {
        auto result=graph;const auto id=node(result,kind);connectMaterialNodes(result,source,sourcePin,id,MaterialPin::Left);
        return std::pair{std::move(result),id};
    };
    const auto sensitivity=[&](const MaterialGraph& baseline,MaterialNodeId id,const char* message,const auto& edit,MaterialPin pin=MaterialPin::Value) {
        auto changed=baseline;edit(*findMaterialNode(changed,id));validateMaterialGraph(changed);
        require(differs(image(baseline,id,pin),image(changed,id,pin)),message);
        require(materialGraphValueHashData(baseline,MaterialShading::StandardPbr,MaterialBlend::Opaque,id)!=
                materialGraphValueHashData(changed,MaterialShading::StandardPbr,MaterialBlend::Opaque,id),"An effective original-node property edit retained its preview value key.");
        auto activeBaseline=baseline,activeChanged=changed;
        const auto destination=materialOutputType(baseline,id,pin)==MaterialValueType::Scalar?MaterialPin::Roughness:MaterialPin::BaseColor;
        connectMaterialNodes(activeBaseline,id,pin,materialOutputNode(activeBaseline).id,destination);
        connectMaterialNodes(activeChanged,id,pin,materialOutputNode(activeChanged).id,destination);
        require(materialGraphTopologyHash(activeBaseline,MaterialShading::StandardPbr,MaterialBlend::Opaque)==
                materialGraphTopologyHash(activeChanged,MaterialShading::StandardPbr,MaterialBlend::Opaque),"A numeric original-node property edit changed material topology.");
    };
    {
        auto [g,id]=branch(MaterialNodeKind::Levels);
        sensitivity(g,id,"Levels Input black did not remove low values.",[](auto& n){n.image.inputBlack=.2f;});
        sensitivity(g,id,"Levels Input white did not expand the input range.",[](auto& n){n.image.inputWhite=.75f;});
        sensitivity(g,id,"Levels Midtones did not brighten middle values.",[](auto& n){n.image.gamma=2;});
        sensitivity(g,id,"Levels Output black did not lift shadows.",[](auto& n){n.image.outputBlack=.2f;});
        sensitivity(g,id,"Levels Output white did not lower highlights.",[](auto& n){n.image.outputWhite=.65f;});
        const auto original=image(g,id);findMaterialNode(g,id)->image.gamma=2;const auto lifted=image(g,id);
        for(std::size_t i=0;i<original.rgba.size();i+=4) {
            for(unsigned c=0;c<3;++c)require(lifted.rgba[i+c]+1e-6f>=original.rgba[i+c],"Larger Levels Midtones darkened an in-range channel.");
            require(lifted.rgba[i+3]==original.rgba[i+3],"Levels altered alpha while adjusting midtones.");
        }
    }
    {
        auto [g,id]=branch(MaterialNodeKind::BrightnessContrast);
        sensitivity(g,id,"Brightness control did not affect image pixels.",[](auto& n){n.image.brightness=.25f;});
        sensitivity(g,id,"Contrast control did not affect image pixels.",[](auto& n){n.image.contrast=.4f;});
        const auto original=image(g,id);findMaterialNode(g,id)->image.brightness=.25f;const auto bright=image(g,id);
        for(std::size_t i=0;i<original.rgba.size();i+=4)for(unsigned c=0;c<3;++c)
            require(bright.rgba[i+c]+1e-6f>=original.rgba[i+c],"Positive Brightness darkened a channel.");
    }
    {
        auto [g,id]=branch(MaterialNodeKind::Blend);auto& n=*findMaterialNode(g,id);
        n.right={MaterialValueType::Color4,{.71f,.23f,.58f,.42f}};n.alpha.value={.7f,.7f,.7f,.7f};n.image.opacity=.8f;
        // Modes may coincide on specific channels. This asymmetric source has
        // channels on both sides of the Overlay/Soft Light pivots.
        const auto mix=image(g,id);
        for(unsigned mode=1;mode<=10;++mode) {
            auto other=g;findMaterialNode(other,id)->image.blendMode=static_cast<MaterialImageBlend>(mode);
            require(differs(mix,image(other,id)),"A Blend mode was indistinguishable from Mix on asymmetric RGBA data.");
        }
        sensitivity(g,id,"Blend Opacity did not affect pixels.",[](auto& n){n.image.opacity=.25f;});
        sensitivity(g,id,"Blend Mask did not affect pixels.",[](auto& n){n.alpha.value={.15f,.15f,.15f,.15f};});
        auto neutral=g;findMaterialNode(neutral,id)->image.opacity=0;
        require(!differs(image(neutral,id),image(graph,source,MaterialPin::RGBA)),"Blend zero opacity did not preserve its exact Base.");
    }
    {
        auto uniform=blank();const auto id=node(uniform,MaterialNodeKind::Grayscale);auto& n=*findMaterialNode(uniform,id);
        n.left={MaterialValueType::Color4,{.13f,.48f,.81f,.26f}};
        const std::array<float,9> expected{{.13f*.2126f+.48f*.7152f+.81f*.0722f,.13f,.48f,.81f,.26f,(.13f+.48f+.81f)/3,.47f,.13f,.81f}};
        for(unsigned mode=0;mode<expected.size();++mode) {
            n.image.grayMode=static_cast<MaterialGrayscaleMode>(mode);
            require(near(evaluateMaterialUniform(uniform,id,MaterialPin::Value).value[0],expected[mode]),"Grayscale Method selected the wrong channel or equation.");
        }
        // Equal RGB makes several conversion methods intentionally identical.
        n.left={MaterialValueType::Color,{.3f,.3f,.3f,0}};n.image.grayMode=MaterialGrayscaleMode::Alpha;
        require(evaluateMaterialUniform(uniform,id,MaterialPin::Value).value[0]==1,"RGB-to-RGBA conversion did not make Grayscale Alpha opaque.");
    }
    {
        auto [g,id]=branch(MaterialNodeKind::Sharpen);
        sensitivity(g,id,"Sharpen Amount did not affect a nonuniform image.",[](auto& n){n.image.sharpen=.15f;});
        sensitivity(g,id,"Sharpen Detail size did not change the sampling support.",[](auto& n){n.image.radius=.1f;});
        findMaterialNode(g,id)->image.sharpen=0;
        require(!differs(image(g,id),image(graph,source,MaterialPin::RGBA)),"Sharpen Amount zero did not preserve its exact Input.");
    }
    {
        auto [g,id]=branch(MaterialNodeKind::HeightToNormal,MaterialPin::R);
        sensitivity(g,id,"Height-to-Normal Strength did not change the encoded normal.",[](auto& n){n.image.normalStrength=3;},MaterialPin::RGB);
        sensitivity(g,id,"Height-to-Normal Flip Y did not reverse its Y component.",[](auto& n){n.image.normalFlipY=true;},MaterialPin::RGB);
        sensitivity(g,id,"Height-to-Normal Detail size did not change the gradient support.",[](auto& n){n.image.radius=.1f;},MaterialPin::RGB);
        const auto normal=image(g,id,MaterialPin::RGB);findMaterialNode(g,id)->image.normalFlipY=true;const auto flipped=image(g,id,MaterialPin::RGB);
        for(std::size_t i=0;i<normal.rgba.size();i+=4)
            require(near(normal.rgba[i],flipped.rgba[i])&&near(normal.rgba[i+2],flipped.rgba[i+2])&&near(normal.rgba[i+1]+flipped.rgba[i+1],1),"Height-to-Normal Flip Y changed X/Z or failed to invert encoded Y.");
        findMaterialNode(g,id)->image.normalStrength=0;const auto flat=image(g,id,MaterialPin::RGB);
        for(std::size_t i=0;i<flat.rgba.size();i+=4)require(flat.rgba[i]==.5f&&flat.rgba[i+1]==.5f&&flat.rgba[i+2]==1,"Height-to-Normal zero Strength was not flat.");
    }
    {
        auto g=graph;const auto uv=node(g,MaterialNodeKind::UV);connectMaterialNodes(g,uv,MaterialPin::UV,source,MaterialPin::UV);
        const auto original=image(g,source,MaterialPin::RGBA);
        const auto mappingChanged=[&](const auto& edit,const char* message){auto changed=g;edit(findMaterialNode(changed,uv)->uv);require(differs(original,image(changed,source,MaterialPin::RGBA)),message);};
        mappingChanged([](auto& m){m.scale[0]=1.35f;},"UV Scale X did not change source coordinates.");
        mappingChanged([](auto& m){m.scale[1]=.65f;},"UV Scale Y did not change source coordinates.");
        mappingChanged([](auto& m){m.rotation[2]=37;},"UV Rotation did not change source coordinates.");
        mappingChanged([](auto& m){m.offset[0]=.17f;},"UV Offset X did not change source coordinates.");
        mappingChanged([](auto& m){m.offset[1]=-.23f;},"UV Offset Y did not change source coordinates.");
        // Link scale controls the editor gesture only, never sampled pixels.
        auto linked=g;findMaterialNode(linked,uv)->uv.scaleLinked=false;
        require(!differs(original,image(linked,source,MaterialPin::RGBA)),"UV Link scale changed pixels without changing scale values.");
        findMaterialNode(g,uv)->uv.offset={.17f,-.23f,0};
        const auto repeat=image(g,source,MaterialPin::RGBA);
        for(const auto wrap:{ModelWrap::Clamp,ModelWrap::Mirror}) {
            auto changed=g;findMaterialNode(changed,source)->texture.sampler.wrapU=wrap;
            require(differs(repeat,image(changed,source,MaterialPin::RGBA)),"Texture Wrap U was ignored.");
            changed=g;findMaterialNode(changed,source)->texture.sampler.wrapV=wrap;
            require(differs(repeat,image(changed,source,MaterialPin::RGBA)),"Texture Wrap V was ignored.");
        }
        auto nearest=g;findMaterialNode(nearest,source)->texture.sampler.magFilter=ModelFilter::Nearest;
        require(differs(repeat,image(nearest,source,MaterialPin::RGBA)),"Texture Magnification filter was ignored.");
        findMaterialNode(g,uv)->uv.scale={2.2f,1.7f,1};const auto minified=image(g,source,MaterialPin::RGBA);
        nearest=g;findMaterialNode(nearest,source)->texture.sampler.minFilter=ModelFilter::Nearest;
        require(differs(minified,image(nearest,source,MaterialPin::RGBA)),"Texture Minification filter was ignored.");
        auto color=g;findMaterialNode(color,source)->texture.role=MaterialTextureRole::Color;
        require(differs(minified,image(color,source,MaterialPin::RGBA)),"Texture Color versus Linear Data did not change decoded RGB.");
    }
    {
        MaterialImageNode settings;const auto base=settings;
        double differenceSeed{},differenceDetail{},differenceContrast{},differencePattern{},featureError{};
        for(unsigned y=0;y<24;++y)for(unsigned x=0;x<32;++x) {
            const Vec3 p{(x+.37f)/32,(y+.61f)/24,.17f};const float original=sampleMaterialNoise(p,base);
            settings=base;settings.seed=123;differenceSeed+=std::abs(original-sampleMaterialNoise(p,settings));
            settings=base;settings.roughness=.9f;differenceDetail+=std::abs(original-sampleMaterialNoise(p,settings));
            settings=base;settings.noiseContrast=.9f;const auto contrasted=sampleMaterialNoise(p,settings);differenceContrast+=std::abs(original-contrasted);
            require(std::abs(contrasted-.5f)+1e-6f>=std::abs(original-.5f),"Noise Contrast reduced distance from neutral gray.");
            settings=base;settings.noisePattern=MaterialNoisePattern::Spots;differencePattern+=std::abs(original-sampleMaterialNoise(p,settings));
            settings=base;settings.featureSize*=2;featureError+=std::abs(original-sampleMaterialNoise(p*2,settings));
        }
        require(differenceSeed>1&&differenceDetail>1&&differenceContrast>1&&differencePattern>1,"An analytic Noise Seed, Detail, Contrast or Pattern control did not change its field.");
        require(featureError<.01,"Larger Noise Feature size did not enlarge structures in its coordinate domain.");
        settings=base;settings.noisePattern=MaterialNoisePattern::Spots;const auto spots=sampleMaterialNoise({.271f,.483f,.17f},settings);settings.roughness=1;
        require(sampleMaterialNoise({.271f,.483f,.17f},settings)==spots,"Spots unexpectedly consumed the disabled Clouds Detail amount.");
    }
    {
        auto g=blank();const auto id=node(g,MaterialNodeKind::RGBAMerge);
        for(unsigned c=0;c<4;++c) {
            const auto pin=c==0?MaterialPin::R:c==1?MaterialPin::G:c==2?MaterialPin::B:MaterialPin::A;
            const auto before=evaluateMaterialUniform(g,id,MaterialPin::RGBA).value;
            require(setMaterialInputDefault(*findMaterialNode(g,id),pin,{MaterialValueType::Scalar,{.37f,.37f,.37f,.37f}}),"RGBA Merge rejected an editable channel default.");
            const auto after=evaluateMaterialUniform(g,id,MaterialPin::RGBA).value;
            require(after[c]==.37f&&after[c]!=before[c],"RGBA Merge channel default did not reach its corresponding output.");
            for(unsigned other=0;other<4;++other)if(other!=c)require(after[other]==before[other],"RGBA Merge edited the wrong channel default.");
        }
        const auto add=node(g,MaterialNodeKind::Add);connectMaterialNodes(g,id,MaterialPin::RGBA,add,MaterialPin::Left);
        removeMaterialLink(g,materialInputLink(g,add,MaterialPin::Left)->id);
        auto& fallback=findMaterialNode(g,add)->left;require(fallback.type==MaterialValueType::Color4,"Arithmetic disconnection did not retain RGBA fallback.");
        fallback.value={.11f,.22f,.33f,.44f};findMaterialNode(g,add)->right.value={0,0,0,0};
        require(evaluateMaterialUniform(g,add,MaterialPin::Value).value==fallback.value,"Arithmetic RGBA defaults did not affect all four channels.");
        // Slider range is authoring UI metadata, not a scalar value multiplier.
        const auto scalar=node(g,MaterialNodeKind::Scalar);findMaterialNode(g,scalar)->scalar=.37f;
        findMaterialNode(g,scalar)->scalarRange={-8,12};require(evaluateMaterialUniform(g,scalar,MaterialPin::Value).value[0]==.37f,"Scalar Slider range changed its value.");
    }
    {
        MaterialGrungeNode baseline;const auto original=generateMaterialGrungeImage(baseline,128);
        const auto changed=[&](const char* message,const auto& edit) {
            auto settings=baseline;edit(settings);const auto result=generateMaterialGrungeImage(settings,128);
            unsigned count{};for(std::size_t i=0;i<original.rgba.size();i+=4)count+=original.rgba[i]!=result.rgba[i];
            require(count>8,message);
        };
        changed("Grunge Seed did not change the generated pattern.",[](auto& g){g.seed=71;});
        changed("Grunge Coverage did not change the generated mask.",[](auto& g){g.coverage=.7f;});
        changed("Grunge Disorder did not change the generated pattern.",[](auto& g){g.disorder=.2f;});
        changed("Grunge Detail did not change the generated fine detail.",[](auto& g){g.detail=.9f;});
        changed("Grunge Base Noisiness did not change the generated body.",[](auto& g){g.baseNoisiness=.1f;});
        changed("Grunge Dirt Specks Opacity did not remove its layer.",[](auto& g){g.dirtSpecksOpacity=0;});
        changed("Grunge Stains Opacity did not remove its layer.",[](auto& g){g.stainsOpacity=0;});
        changed("Grunge Scratches Opacity did not remove its layer.",[](auto& g){g.scratchesOpacity=0;});
        changed("Grunge Leaks Opacity did not introduce its layer.",[](auto& g){g.leaksOpacity=.8f;});
        changed("Grunge Sharpen did not affect generated local detail.",[](auto& g){g.sharpen=0;});
        changed("Grunge Large Variation Intensity did not affect the broad field.",[](auto& g){g.largeVariationIntensity=.1f;});
        changed("Grunge Invert Dirt did not reverse speck contribution.",[](auto& g){g.invertDirt=true;});
        auto tiled=baseline;tiled.tiling=7;
        require(generateMaterialGrungeImage(tiled,128).rgba==original.rgba,"Grunge Tiling rebaked source pixels instead of changing runtime sampling.");
        // With coverage zero every layer is intentionally suppressed.
        baseline.coverage=0;const auto empty=generateMaterialGrungeImage(baseline,128);
        for(std::size_t i=0;i<empty.rgba.size();i+=4)require(empty.rgba[i]==0&&empty.rgba[i+3]==255,"Grunge zero Coverage did not produce an opaque black mask.");
    }
}
void cloudScaleSchemaMigration() {
    auto graph=blank();
    const auto cloud1=node(graph,MaterialNodeKind::ProceduralTexture),cloud2=node(graph,MaterialNodeKind::ProceduralTexture),other=node(graph,MaterialNodeKind::ProceduralTexture);
    findMaterialNode(graph,cloud1)->procedural.settings=material_procedural::default_settings(material_procedural::Pattern::Clouds1);
    findMaterialNode(graph,cloud2)->procedural.settings=material_procedural::default_settings(material_procedural::Pattern::Clouds2);
    findMaterialNode(graph,other)->procedural.settings=material_procedural::default_settings(material_procedural::Pattern::FineDirt);
    auto prior=nlohmann::json::parse(serializeMaterialGraph(graph));prior["version"]=11;
    for(auto& n:prior["nodes"]) {
        if(n["id"]==cloud1)n["procedural"]["scale"]=3.25f;
        if(n["id"]==cloud2)n["procedural"]["scale"]=4.f;
        if(n["id"]==other)n["procedural"]["scale"]=2.75f;
    }
    const auto originalBytes=prior.dump();const auto migrated=parseMaterialGraph(originalBytes);
    require(near(findMaterialNode(migrated,cloud1)->procedural.settings.scale,1/3.25f)&&
            near(findMaterialNode(migrated,cloud2)->procedural.settings.scale,.25f),"Schema11 cloud frequencies were not converted to reciprocal feature size.");
    require(findMaterialNode(migrated,other)->procedural.settings.scale==2.75f,"Cloud Scale conversion changed a different procedural family.");
    const auto current=serializeMaterialGraph(migrated);const auto reopened=parseMaterialGraph(current);
    require(nlohmann::json::parse(current)["version"]==14&&serializeMaterialGraph(reopened)==current&&
            findMaterialNode(reopened,cloud1)->procedural.settings.scale==findMaterialNode(migrated,cloud1)->procedural.settings.scale,
            "Current schema cloud Scale was converted twice or did not round trip.");
    require(prior.dump()==originalBytes,"Loading cloud Scale migration mutated the supplied serialized source.");
    for(auto& n:prior["nodes"])if(n["id"]==cloud1)n["procedural"]["scale"]=.5f;
    rejects([&]{parseMaterialGraph(prior.dump());},"Legacy cloud frequency below its original supported minimum was accepted.");
}
void resizeImageBaking() {
    static_assert(static_cast<unsigned>(MaterialNodeKind::Resize)==33);
    auto graph=blank();const auto texture=node(graph,MaterialNodeKind::Texture2D),levels=node(graph,MaterialNodeKind::Levels),resize=node(graph,MaterialNodeKind::Resize);
    auto* source=findMaterialNode(graph,texture);source->texture.image.id=canonicalAssetId("assets/textures/resize.png");source->texture.role=MaterialTextureRole::Data;
    findMaterialNode(graph,levels)->image.gamma=2;
    findMaterialNode(graph,resize)->resize={5,1,MaterialResizeFilter::Linear};
    connectMaterialNodes(graph,texture,MaterialPin::RGBA,levels,MaterialPin::Left);connectMaterialNodes(graph,levels,MaterialPin::Value,resize,MaterialPin::Left);
    require(materialOutputType(graph,resize,MaterialPin::Value)==MaterialValueType::Color4,"Resize lost the upstream RGBA type.");
    auto pixels=std::make_shared<DecodedImage>(DecodedImage{2,1,{0,0,0,0,255,255,255,255}});
    const auto resolve=[&](const std::string&)->std::shared_ptr<const DecodedImage>{return pixels;};
    auto image=bakeMaterialResizeImage(graph,resize,resolve);
    require(image.width==5&&image.height==1&&near(image.rgba[8],.5f)&&near(image.rgba[11],.5f),"Resize applied Levels after upsampling or coupled alpha to RGB.");
    require(materialResizeImageSources(graph,resize)==std::vector<std::string>{canonicalAssetId("assets/textures/resize.png")},"Resize dependency extraction lost its source image.");
    const auto before=serializeMaterialGraph(graph);std::string diagnostic;
    const auto uv=node(graph,MaterialNodeKind::UV);findMaterialNode(graph,uv)->uv.mode=MaterialUvMode::UV1;
    const auto candidateBefore=serializeMaterialGraph(graph);
    require(!materialCanConnect(graph,uv,MaterialPin::UV,texture,MaterialPin::UV,&diagnostic)&&!diagnostic.empty()&&serializeMaterialGraph(graph)==candidateBefore,
        "Context-dependent Resize connection changed the graph or lacked a diagnostic.");
    const auto time=node(graph,MaterialNodeKind::Time);
    require(!materialCanConnect(graph,time,MaterialPin::Value,levels,MaterialPin::Left,&diagnostic),"Resize accepted Time input.");
    auto serialized=nlohmann::json::parse(before);require(serialized["version"]==14,"Resize did not write graph schema 14.");
    require(serializeMaterialGraph(parseMaterialGraph(before))==before,"Resize settings or typed input failed round trip.");
    auto malformed=serialized;for(auto& n:malformed["nodes"])if(n["id"]==resize)n["resize"]["width"]=1.5;
    rejects([&]{parseMaterialGraph(malformed.dump());},"Resize accepted a fractional width.");
    malformed=serialized;for(auto& n:malformed["nodes"])if(n["id"]==resize)n["resize"]["filter"]=.5;
    rejects([&]{parseMaterialGraph(malformed.dump());},"Resize accepted a fractional filter.");
    malformed=serialized;malformed["version"]=9;rejects([&]{parseMaterialGraph(malformed.dump());},"Schema 9 accepted a schema 10 Resize node.");
    std::stop_source cancellation;cancellation.request_stop();
    rejects([&]{bakeMaterialResizeImage(graph,resize,resolve,cancellation.get_token());},"Resize ignored cancellation.");

    auto linearGraph=blank();const auto tex=node(linearGraph,MaterialNodeKind::Texture2D),size=node(linearGraph,MaterialNodeKind::Resize);
    findMaterialNode(linearGraph,tex)->texture.image.id=canonicalAssetId("assets/textures/linear.png");
    findMaterialNode(linearGraph,size)->resize={1,1,MaterialResizeFilter::Linear};
    connectMaterialNodes(linearGraph,tex,MaterialPin::RGBA,size,MaterialPin::Left);
    pixels=std::make_shared<DecodedImage>(DecodedImage{1,1,{128,64,255,0}});
    image=bakeMaterialResizeImage(linearGraph,size,resolve);
    require(std::abs(image.rgba[0]-.2158605f)<1e-6f&&std::abs(image.rgba[1]-.05126946f)<1e-6f&&image.rgba[2]==1&&image.rgba[3]==0,
        "Color Resize did not decode sRGB before filtering or discarded transparent RGB.");
    findMaterialNode(linearGraph,tex)->texture.role=MaterialTextureRole::Data;
    image=bakeMaterialResizeImage(linearGraph,size,resolve);require(near(image.rgba[0],128/255.f)&&image.rgba[3]==0,"Linear Data Resize was gamma decoded.");
    const auto assigned=findMaterialNode(linearGraph,tex)->texture.image.id;findMaterialNode(linearGraph,tex)->texture.image.id.clear();
    image=bakeMaterialResizeImage(linearGraph,size,{});require(image.rgba==std::vector<float>{1,1,1,1},"Unassigned Resize source differed from the shader's white Texture 2D default.");
    findMaterialNode(linearGraph,tex)->texture.image.id=assigned;

    pixels=std::make_shared<DecodedImage>();pixels->width=8;pixels->height=4;pixels->rgba.resize(8*4*4);
    for(unsigned y=0;y<4;++y)for(unsigned x=0;x<8;++x){const auto i=(y*8+x)*4;pixels->rgba[i]=pixels->rgba[i+1]=pixels->rgba[i+2]=x%2?255:0;pixels->rgba[i+3]=255;}
    findMaterialNode(linearGraph,size)->resize={3,2,MaterialResizeFilter::Area};image=bakeMaterialResizeImage(linearGraph,size,resolve);
    require(image.width==3&&image.height==2&&near(image.rgba[0],.375f)&&near(image.rgba[4],.5f)&&near(image.rgba[8],.625f),"Area Resize lost fractional source-pixel coverage on a nonsquare image.");
    pixels=std::make_shared<DecodedImage>(DecodedImage{2,1,{0,0,0,255,255,255,255,255}});
    findMaterialNode(linearGraph,size)->resize={5,1,MaterialResizeFilter::Nearest};image=bakeMaterialResizeImage(linearGraph,size,resolve);
    require(image.rgba[0]==0&&image.rgba[4]==0&&image.rgba[8]==1&&image.rgba[12]==1&&image.rgba[16]==1,"Nearest Resize did not select source texel centers.");

    const auto inner=node(linearGraph,MaterialNodeKind::Resize);findMaterialNode(linearGraph,inner)->resize={3,1,MaterialResizeFilter::Nearest};
    connectMaterialNodes(linearGraph,tex,MaterialPin::RGBA,inner,MaterialPin::Left);connectMaterialNodes(linearGraph,inner,MaterialPin::Value,size,MaterialPin::Left);
    findMaterialNode(linearGraph,size)->resize={6,1,MaterialResizeFilter::Linear};image=bakeMaterialResizeImage(linearGraph,size,resolve);
    require(near(image.rgba[0],0)&&near(image.rgba[4],.25f)&&near(image.rgba[8],.75f)&&near(image.rgba[12],1),"Nested Resize did not use the declared intermediate lattice.");

    auto constant=blank();const auto c=node(constant,MaterialNodeKind::Color),r=node(constant,MaterialNodeKind::Resize);
    findMaterialNode(constant,c)->color={-2,4,.25f,0};findMaterialNode(constant,r)->resize={4,3,MaterialResizeFilter::Area};
    connectMaterialNodes(constant,c,MaterialPin::RGBA,r,MaterialPin::Left);image=bakeMaterialResizeImage(constant,r,{});
    for(std::size_t i=0;i<image.rgba.size();i+=4)require(image.rgba[i]==-2&&image.rgba[i+1]==4&&image.rgba[i+2]==.25f&&image.rgba[i+3]==0,"Resize clipped float/HDR values or premultiplied independent alpha.");
    const auto fallback=node(constant,MaterialNodeKind::Resize);findMaterialNode(constant,fallback)->resize={2,3,MaterialResizeFilter::Linear};findMaterialNode(constant,fallback)->left={MaterialValueType::Scalar,{-3,-3,-3,-3}};
    image=bakeMaterialResizeImage(constant,fallback,{});require(image.width==2&&image.height==3&&image.rgba[0]==-3&&image.rgba[3]==1,"Unconnected Resize failed to bake its typed fallback.");

    auto large=blank();const auto bitmap=node(large,MaterialNodeKind::Texture2D),big=node(large,MaterialNodeKind::Resize);
    findMaterialNode(large,bitmap)->texture.image.id=canonicalAssetId("assets/textures/256.png");findMaterialNode(large,bitmap)->texture.role=MaterialTextureRole::Data;
    findMaterialNode(large,big)->resize={2048,2048,MaterialResizeFilter::Linear};connectMaterialNodes(large,bitmap,MaterialPin::RGBA,big,MaterialPin::Left);
    pixels=std::make_shared<DecodedImage>();pixels->width=pixels->height=256;pixels->rgba.resize(256*256*4);
    for(unsigned y=0;y<256;++y)for(unsigned x=0;x<256;++x){const auto i=(y*256+x)*4;pixels->rgba[i]=static_cast<std::uint8_t>(x);pixels->rgba[i+1]=static_cast<std::uint8_t>(y);pixels->rgba[i+2]=127;pixels->rgba[i+3]=255;}
    image=bakeMaterialResizeImage(large,big,resolve);const auto center=(1024u*2048+1024)*4;
    require(image.width==2048&&image.height==2048&&std::abs(image.rgba[center]-(127.5625f/255))<1e-6f&&image.rgba[center+3]==1,"256-to-2K Resize did not produce a real filtered image.");
}
void resizeMappingsAndSpatialBranches() {
    auto graph=blank();const auto uv=node(graph,MaterialNodeKind::UV),texture=node(graph,MaterialNodeKind::Texture2D),resize=node(graph,MaterialNodeKind::Resize);
    findMaterialNode(graph,texture)->texture.image.id=canonicalAssetId("assets/textures/wrap.png");findMaterialNode(graph,texture)->texture.role=MaterialTextureRole::Data;
    findMaterialNode(graph,uv)->uv.offset={.5f,0,0};findMaterialNode(graph,resize)->resize={2,1,MaterialResizeFilter::Linear};
    connectMaterialNodes(graph,uv,MaterialPin::UV,texture,MaterialPin::UV);connectMaterialNodes(graph,texture,MaterialPin::R,resize,MaterialPin::Left);
    auto source=std::make_shared<DecodedImage>(DecodedImage{2,1,{0,0,0,255,255,255,255,255}});
    const auto resolve=[&](const std::string&)->std::shared_ptr<const DecodedImage>{return source;};
    auto image=bakeMaterialResizeImage(graph,resize,resolve);require(image.rgba[0]==1&&image.rgba[4]==0,"Resize ignored authored Repeat addressing or UV0 offset.");
    findMaterialNode(graph,texture)->texture.sampler.wrapU=ModelWrap::Mirror;image=bakeMaterialResizeImage(graph,resize,resolve);
    require(image.rgba[0]==1&&image.rgba[4]==1,"Resize ignored Mirror addressing.");
    findMaterialNode(graph,texture)->texture.sampler.wrapU=ModelWrap::Clamp;
    findMaterialNode(graph,uv)->uv.offset={0,0,0};
    const auto normal=node(graph,MaterialNodeKind::HeightToNormal);findMaterialNode(graph,normal)->image.radius=.25f;
    connectMaterialNodes(graph,texture,MaterialPin::R,normal,MaterialPin::Left);connectMaterialNodes(graph,normal,MaterialPin::RGB,resize,MaterialPin::Left);
    image=bakeMaterialResizeImage(graph,resize,resolve);
    require(std::abs(image.rgba[0]-(.5f-.005f/std::sqrt(1.0001f)))<1e-6f&&near(image.rgba[1],.5f),"Resize did not evaluate complete upstream Height-to-Normal RGB differences.");
    require(!materialCanConnect(graph,normal,MaterialPin::Normal,resize,MaterialPin::Left),"Resize accepted a decoded surface Normal.");
    const auto levels=node(graph,MaterialNodeKind::Levels),sharpen=node(graph,MaterialNodeKind::Sharpen);
    findMaterialNode(graph,levels)->image.gamma=2;findMaterialNode(graph,sharpen)->image.radius=1.f/3;findMaterialNode(graph,resize)->resize={3,1,MaterialResizeFilter::Linear};
    source=std::make_shared<DecodedImage>(DecodedImage{3,1,{0,0,0,255,64,64,64,255,255,255,255,255}});
    connectMaterialNodes(graph,texture,MaterialPin::R,levels,MaterialPin::Left);connectMaterialNodes(graph,levels,MaterialPin::Value,sharpen,MaterialPin::Left);connectMaterialNodes(graph,sharpen,MaterialPin::Value,resize,MaterialPin::Left);
    image=bakeMaterialResizeImage(graph,resize,resolve);const float adjusted=std::sqrt(64/255.f);
    require(std::abs(image.rgba[4]-std::clamp(3*adjusted-1,0.f,1.f))<1e-6f,"Sharpen sampled only a terminal texture and skipped upstream Levels.");
    std::string diagnostic;
    const auto panner=node(graph,MaterialNodeKind::Panner);findMaterialNode(graph,panner)->panner.speed={1,0};
    require(!materialCanConnect(graph,panner,MaterialPin::UV,texture,MaterialPin::UV,&diagnostic)&&!diagnostic.empty(),"Resize accepted moving Panner coordinates.");
    findMaterialNode(graph,panner)->panner.speed={0,0};
    require(materialCanConnect(graph,panner,MaterialPin::UV,texture,MaterialPin::UV),"Resize rejected a static zero-speed UV0 Panner.");

    auto many=blank();MaterialNodeId previous{};for(unsigned i=0;i<13;++i) {
        const auto t=node(many,MaterialNodeKind::Texture2D);findMaterialNode(many,t)->texture.image.id=canonicalAssetId("assets/textures/bake"+std::to_string(i)+".png");
        if(!previous)previous=t;else{const auto add=node(many,MaterialNodeKind::Add);connectMaterialNodes(many,previous,findMaterialNode(many,previous)->kind==MaterialNodeKind::Texture2D?MaterialPin::RGB:MaterialPin::Value,add,MaterialPin::Left);connectMaterialNodes(many,t,MaterialPin::RGB,add,MaterialPin::Right);previous=add;}
    }
    const auto boundary=node(many,MaterialNodeKind::Resize);connectMaterialNodes(many,previous,MaterialPin::Value,boundary,MaterialPin::Left);
    connectMaterialNodes(many,boundary,MaterialPin::Value,materialOutputNode(many).id,MaterialPin::BaseColor);
    require(materialResizeImageSources(many,boundary).size()==13,"CPU Resize sources incorrectly consumed the renderer's 12 sampler budget.");
}
void exactColdModelSurface() {
    ModelMaterial original;original.baseColor={.3f,.5f,.7f,1};original.metallic=.4f;original.roughness=.8f;
    original.normalScale=.7f;original.occlusionStrength=.6f;original.emissive={1,.5f,.2f};
    original.uvScale={2,3,1};original.uvOffset={.2f,.1f,0};original.uvRotation={0,0,30};
    const std::array slots{&original.baseColorTexture,&original.metallicRoughnessTexture,&original.normalTexture,&original.occlusionTexture,&original.emissiveTexture};
    unsigned index{};for(auto* slot:slots){slot->image.id=canonicalAssetId("assets/textures/map"+std::to_string(index++)+".png");slot->texCoord=1;}
    auto graph=materialGraphFromLegacy(original,MaterialShading::StandardPbr,MaterialBlend::Opaque);
    const auto fixed=materialGraphFixedSurface(graph);
    require(fixed && !fixed->graph && fixed->baseColor==original.baseColor && fixed->roughness==original.roughness &&
        fixed->metallic==original.metallic && fixed->normalScale==original.normalScale && fixed->uvScale==original.uvScale &&
        fixed->emissiveTexture.image==original.emissiveTexture.image && fixed->normalTexture.texCoord==1,
        "Conventional imported PBR maps/factors did not lower exactly for a cold model.");
    auto changed=graph;const auto root=materialOutputNode(changed).id;
    const auto rough=materialInputLink(changed,root,MaterialPin::Roughness)->fromNode;
    const auto power=addMaterialNode(changed,MaterialNodeKind::Power).id;
    findMaterialNode(changed,power)->left={MaterialValueType::Scalar,{.5f,.5f,.5f,.5f}};
    findMaterialNode(changed,power)->right={MaterialValueType::Scalar,{2,2,2,2}};
    connectMaterialNodes(changed,power,MaterialPin::Value,root,MaterialPin::Specular);
    require(materialGraphFixedSurface(changed)->specular==.25f,"Uniform arithmetic was needlessly rejected by the fixed lowering.");
    connectMaterialNodes(changed,rough,MaterialPin::Value,power,MaterialPin::Left);
    require(!materialGraphFixedSurface(changed),"Textured Power was silently approximated by the fixed lowering.");
    changed=graph;materialOutputNode(changed).output.blend=MaterialBlend::Alpha;
    require(!materialGraphFixedSurface(changed),"Independent graph coverage was coupled to the base texture during cold presentation.");
    changed=graph;
    for(auto& node:changed.nodes)if(node.kind==MaterialNodeKind::Texture2D&&node.texture.role==MaterialTextureRole::Color){node.texture.role=MaterialTextureRole::Data;break;}
    require(!materialGraphFixedSurface(changed),"Wrong texture decoding was certified as an exact fixed surface.");
    changed=graph;const auto uv=addMaterialNode(changed,MaterialNodeKind::UV).id;
    findMaterialNode(changed,uv)->uv.offset={.8f,0,0};
    for(const auto& node:changed.nodes)if(node.kind==MaterialNodeKind::Texture2D&&node.texture.role==MaterialTextureRole::Normal){
        connectMaterialNodes(changed,uv,MaterialPin::UV,node.id,MaterialPin::UV);break;
    }
    require(!materialGraphFixedSurface(changed),"Different per-map transforms were silently combined during cold presentation.");
    changed=graph;
    if(const auto* link=materialInputLink(changed,root,MaterialPin::Roughness))removeMaterialLink(changed,link->id);
    materialOutputNode(changed).output.roughness=.7f;
    require(!materialGraphFixedSurface(changed),"Packed texture changed a constant roughness input during cold presentation.");
    changed=graph;const auto time=addMaterialNode(changed,MaterialNodeKind::Time).id;
    connectMaterialNodes(changed,time,MaterialPin::Value,root,MaterialPin::Specular);
    require(!materialGraphFixedSurface(changed),"Animated graph uniforms were frozen by the fixed lowering.");
}
void atomicPointNodesAndSources() {
    MaterialAtomicImageNode a;
    // Independent affine-transfer and scan oracles, including HDR inputs and
    // endpoint behavior. Contrast is applied after the coverage offset.
    for(float coverage:{0.f,.17f,.5f,.83f,1.f})for(float contrast:{0.f,.27f,1.f})for(float v:{-5.f,0.f,.2f,.5f,.8f,1.f,8.f}){
        a.coverage=coverage;a.contrast=contrast;
        const float bounded=std::min(1.f,std::max(0.f,v));
        const float expected=std::min(1.f,std::max(0.f,(bounded+2*coverage-1.5f)*(1+15*contrast)+.5f));
        require(near(materialAtomicMaskAdjust(v,a),expected),"Mask Adjust changed clamp/offset/gain order.");
        const float center=1-coverage,half=std::min(center,1-center)*(1-contrast);
        const float scan=coverage==0?0:coverage==1?1:half<=1e-6f?(v>=center?1.f:0.f):std::min(1.f,std::max(0.f,(v-(center-half))/(2*half)));
        require(std::abs(materialAtomicHistogramScan(v,a)-scan)<2e-6f,"Histogram Scan endpoints or affine interior changed.");
    }
    a.direction=0;a.bothDirections=false;
    require(std::abs(materialAtomicDirectionSelect({1,.75f,.5f,0},a)-2/std::sqrt(5.f))<1e-6f,"Direction Select skipped encoded-vector normalization.");
    a.direction=1;require(materialAtomicDirectionSelect({1,.75f,.5f,0},a)==0,"Direction Select selected opposite hemisphere.");
    a.bothDirections=true;require(std::abs(materialAtomicDirectionSelect({1,.75f,.5f,0},a)-2/std::sqrt(5.f))<1e-6f,"Both Directions did not preserve absolute normalized component.");
    a.direction=4;require(materialAtomicDirectionSelect({.5f,.5f,.5f,0},a)==1,"Degenerate direction did not choose finite +Z fallback.");
    auto graph=blank();const auto source=node(graph,MaterialNodeKind::Color),input=node(graph,MaterialNodeKind::ImageInput),scan=node(graph,MaterialNodeKind::HistogramScan),mask=node(graph,MaterialNodeKind::MaskAdjust),direction=node(graph,MaterialNodeKind::DirectionSelect),decoder=node(graph,MaterialNodeKind::DecodeNormal);
    findMaterialNode(graph,source)->color={.2f,.4f,.8f,0};
    connectMaterialNodes(graph,source,MaterialPin::RGBA,input,MaterialPin::Left);
    connectMaterialNodes(graph,input,MaterialPin::RGBA,scan,MaterialPin::Left);
    connectMaterialNodes(graph,scan,MaterialPin::Value,mask,MaterialPin::Left);
    connectMaterialNodes(graph,scan,MaterialPin::Value,direction,MaterialPin::Left);
    connectMaterialNodes(graph,scan,MaterialPin::Value,decoder,MaterialPin::Left);
    connectMaterialNodes(graph,decoder,MaterialPin::Normal,materialOutputNode(graph).id,MaterialPin::Normal);
    connectMaterialNodes(graph,input,MaterialPin::RGB,materialOutputNode(graph).id,MaterialPin::BaseColor);
    const auto resolved=evaluateMaterialUniform(graph,scan,MaterialPin::Value);
    require(resolved.type==MaterialValueType::Color4&&near(resolved.value[0],.2f)&&near(resolved.value[1],.4f)&&near(resolved.value[2],.8f)&&resolved.value[3]==0,"Adaptable scan lost RGBA, neutral transfer or transparent RGB.");
    require(materialOutputType(graph,mask,MaterialPin::Value)==MaterialValueType::Scalar&&materialOutputType(graph,direction,MaterialPin::Value)==MaterialValueType::Scalar&&materialOutputType(graph,decoder,MaterialPin::Normal)==MaterialValueType::Normal,"Fixed atomic outputs changed their semantic types.");
    require(!materialCanConnect(graph,decoder,MaterialPin::Normal,scan,MaterialPin::Left),"Image filter accepted a decoded surface Normal.");
    const auto originalNodes=graph.nodes.size();require(originalNodes==7&&graph.links.size()==7,"Atomic direct wiring inserted processing nodes.");
    const auto fullOrder=materialGraphOrder(graph,MaterialShading::StandardPbr,MaterialBlend::Opaque);require(std::find(fullOrder.begin(),fullOrder.end(),source)!=fullOrder.end(),"Enabled Image Input ignored connected Base.");
    findMaterialNode(graph,input)->color={.6f,.3f,.1f,0};findMaterialNode(graph,input)->atomic.enabled=false;
    auto order=materialGraphOrder(graph,MaterialShading::StandardPbr,MaterialBlend::Opaque);
    require(std::find(order.begin(),order.end(),source)==order.end()&&evaluateMaterialUniform(graph,input,MaterialPin::RGBA).value==Vec4{.6f,.3f,.1f,0},"Disabled Image Input evaluated upstream Base or lost constant alpha.");
    findMaterialNode(graph,input)->texture.image.id=canonicalAssetId("assets/textures/override.png");
    require(materialGraphTextureReferences(graph).size()==1,"Disabled Image Input lost its authored dependency.");
    const auto dormantHash=materialGraphValueHash(graph,MaterialShading::StandardPbr,MaterialBlend::Opaque);
    findMaterialNode(graph,input)->texture.image.id=canonicalAssetId("assets/textures/other.png");
    require(materialGraphValueHash(graph,MaterialShading::StandardPbr,MaterialBlend::Opaque)==dormantHash,"Disabled override changed active evaluation identity.");
    findMaterialNode(graph,input)->atomic.enabled=true;order=materialGraphOrder(graph,MaterialShading::StandardPbr,MaterialBlend::Opaque);
    require(std::find(order.begin(),order.end(),source)==order.end()&&!materialExpressionUniform(graph,input,MaterialPin::RGBA),"Assigned Image Input did not override/prune Base.");
    for(const auto kind:{MaterialNodeKind::Blur,MaterialNodeKind::GrowShrink,MaterialNodeKind::DirectionalWarp,MaterialNodeKind::ImageTransform,MaterialNodeKind::ProceduralTexture,MaterialNodeKind::HeightOcclusion,MaterialNodeKind::NormalOcclusion})node(graph,kind);
    const auto text=serializeMaterialGraph(graph);require(text.find("\"version\":14")!=std::string::npos&&serializeMaterialGraph(parseMaterialGraph(text))==text,"Atomic kinds/settings changed during schema14 round trip.");
    auto prior=nlohmann::json::parse(text);prior["version"]=10;rejects([&]{parseMaterialGraph(prior.dump());},"Prior schema accepted atomic node payloads.");
}

void runtimeTextureDemandMappings() {
    auto graph=blank();const auto output=materialOutputNode(graph).id;
    const auto mapping=node(graph,MaterialNodeKind::UV);
    findMaterialNode(graph,mapping)->uv.scale={.001f,.002f,.003f};
    findMaterialNode(graph,mapping)->uv.offset={.25f,.5f,.75f};
    const auto texture=node(graph,MaterialNodeKind::Texture2D);
    findMaterialNode(graph,texture)->texture.image.id=canonicalAssetId("assets/textures/demand.png");
    connectMaterialNodes(graph,mapping,MaterialPin::UV,texture,MaterialPin::UV);
    const auto blur=node(graph,MaterialNodeKind::Blur),resize=node(graph,MaterialNodeKind::Resize);
    connectMaterialNodes(graph,texture,MaterialPin::RGBA,blur,MaterialPin::Left);
    connectMaterialNodes(graph,blur,MaterialPin::Value,resize,MaterialPin::Left);
    connectMaterialNodes(graph,resize,MaterialPin::Value,output,MaterialPin::BaseColor);
    const auto lookup=[](const auto& sampled,MaterialNodeId id)->const MaterialUvMapping* {
        for(const auto& value:sampled)if(value.node==id)return &value.mapping;return nullptr;
    };
    auto sampled=materialRuntimeTextureMappings(graph);
    require(sampled.size()==1&&sampled[0].node==resize&&sampled[0].mapping==MaterialUvMapping{},
        "Streaming demand counted private bitmap/filter mappings instead of one outer cached sampler.");
    // A second ordinary consumer must still request the source bitmap's actual
    // mapping even though that source is also private to the cached branch.
    connectMaterialNodes(graph,texture,MaterialPin::RGB,output,MaterialPin::Emission);
    sampled=materialRuntimeTextureMappings(graph);
    require(sampled.size()==2&&lookup(sampled,texture)&&*lookup(sampled,texture)==findMaterialNode(graph,mapping)->uv,
        "A cached cut hid an independently sampled direct source or lost its mapping.");
    const auto transform=node(graph,MaterialNodeKind::ImageTransform);
    MaterialUvMapping authored;authored.mode=MaterialUvMode::WorldTriplanar;
    authored.scale={3,-5,7};authored.offset={1,2,3};authored.rotation={13,19,37};
    findMaterialNode(graph,transform)->uv=authored;
    connectMaterialNodes(graph,resize,MaterialPin::Value,transform,MaterialPin::Left);
    connectMaterialNodes(graph,transform,MaterialPin::Value,output,MaterialPin::BaseColor);
    sampled=materialRuntimeTextureMappings(graph);
    require(sampled.size()==2&&lookup(sampled,transform)&&*lookup(sampled,transform)==authored&&!lookup(sampled,resize)&&!lookup(sampled,blur),
        "Completed-image mapping was replaced by identity or nested cached images inflated runtime demand.");
    const auto runtimeUv=node(graph,MaterialNodeKind::UV),panner=node(graph,MaterialNodeKind::Panner);
    findMaterialNode(graph,runtimeUv)->uv.mode=MaterialUvMode::UV1;
    findMaterialNode(graph,runtimeUv)->uv.scale={2,4,1};findMaterialNode(graph,panner)->panner.speed={.25f,.5f};
    connectMaterialNodes(graph,runtimeUv,MaterialPin::UV,panner,MaterialPin::UV);
    connectMaterialNodes(graph,panner,MaterialPin::UV,transform,MaterialPin::UV);
    sampled=materialRuntimeTextureMappings(graph,{2,0});
    require(lookup(sampled,transform)&&lookup(sampled,transform)->mode==MaterialUvMode::UV1&&
        lookup(sampled,transform)->scale==Vec3{2,4,1}&&lookup(sampled,transform)->offset==Vec3{.5f,1,0},
        "Runtime cached-image demand ignored UV1 or the current animated Mapping context.");
    const auto image=node(graph,MaterialNodeKind::ImageInput);
    connectMaterialNodes(graph,texture,MaterialPin::RGB,image,MaterialPin::Left);
    connectMaterialNodes(graph,image,MaterialPin::RGBA,output,MaterialPin::Emission);
    sampled=materialRuntimeTextureMappings(graph);
    require(sampled.size()==2&&lookup(sampled,texture),"Unassigned Image Input did not follow its active Base image demand.");
    findMaterialNode(graph,image)->texture.image.id=canonicalAssetId("assets/textures/override.png");
    sampled=materialRuntimeTextureMappings(graph);
    require(sampled.size()==2&&lookup(sampled,image)&&*lookup(sampled,image)==MaterialUvMapping{}&&!lookup(sampled,texture),
        "Image Input override failed to prune its Base or contribute an actual runtime sampler.");
    findMaterialNode(graph,image)->atomic.enabled=false;sampled=materialRuntimeTextureMappings(graph);
    require(sampled.size()==1&&!lookup(sampled,image)&&!lookup(sampled,texture),"Disabled Image Input still requested image storage or Base demand.");
    const auto grunge=node(graph,MaterialNodeKind::Grunge),procedural=node(graph,MaterialNodeKind::ProceduralTexture);
    findMaterialNode(graph,grunge)->grunge.tiling=3;
    connectMaterialNodes(graph,mapping,MaterialPin::UV,grunge,MaterialPin::UV);
    connectMaterialNodes(graph,grunge,MaterialPin::Value,output,MaterialPin::Roughness);
    connectMaterialNodes(graph,procedural,MaterialPin::Value,output,MaterialPin::Occlusion);
    sampled=materialRuntimeTextureMappings(graph);
    const auto* grungeMapping=lookup(sampled,grunge);
    require(sampled.size()==3&&grungeMapping&&near(grungeMapping->scale[0],.003f)&&near(grungeMapping->offset[1],1.5f)&&
        lookup(sampled,procedural)&&*lookup(sampled,procedural)==MaterialUvMapping{},
        "Grunge tiling or generated procedural sampler was missing from runtime streaming demand.");
    const auto height=node(graph,MaterialNodeKind::GrowShrink);
    connectMaterialNodes(graph,procedural,MaterialPin::Value,height,MaterialPin::Left);
    materialOutputNode(graph).output.tessellation.enabled=true;
    connectMaterialNodes(graph,height,MaterialPin::Value,output,MaterialPin::Height);
    sampled=materialRuntimeTextureMappings(graph);require(lookup(sampled,height),"Enabled Height cached sampler was missing from demand.");
    materialOutputNode(graph).output.tessellation.enabled=false;
    sampled=materialRuntimeTextureMappings(graph);require(!lookup(sampled,height),"Inactive tessellation retained a demand-only Height sampler.");
}

}
int main(int argc,char** argv){try{materialStylizationAuthoring();refractionAuthoringAndPersistence();originalNodePropertySemantics();cloudScaleSchemaMigration();runtimeTextureDemandMappings();atomicPointNodesAndSources();resizeImageBaking();resizeMappingsAndSpatialBranches();imageNodeFoundation();typesChainsAndTransactions();signaturesAndDormantReferences();heightTessellationPersistence();receiveFogPersistence();rgbScalarInputsAndFallbacks();migrationAndAuthority();repeatedSourceDag();activeCompatibilityTransport();usageOpacityAndSpecular();vectorMappingsAndUniformSafety();schemaOneMigration();explicitParticleMigration();arithmeticAndAnimatedMappings();imageAdjustmentsAndGrunge();exactColdModelSurface();if(argc>1)snapshotMaterials(fs::path(argv[1]));std::cout<<"Material graph types, chains, migration, signatures and references passed.\n";return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
