#include "assets/MaterialAsset.h"
#include "material_fixtures.h"
#include "assets/ContentStorage.h"
#include "resources/AssetId.h"
#include <algorithm>
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
    require(nlohmann::json::parse(serialized).at("version")==9&&serializeMaterialGraph(parseMaterialGraph(serialized))==serialized,"Current schema9 nodes failed round-trip.");
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
        auto upgraded=nlohmann::json::parse(serializeMaterialGraph(loadedPrior));require(upgraded["version"]==9,"Legacy Grunge did not write the current schema.");
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
    const auto json=serializeMaterialGraph(graph);require(json.find("\"version\":9")!=std::string::npos,"New nodes did not write graph schema 9.");require(serializeMaterialGraph(parseMaterialGraph(json))==json,"Image node settings, outputs or IDs failed round trip.");
    auto bad=graph;findMaterialNode(bad,levels)->image.inputWhite=0;rejects([&]{validateMaterialGraph(bad);},"Levels accepted an empty input interval.");
    const auto vector=node(graph,MaterialNodeKind::Vector2);rejects([&]{connectMaterialNodes(graph,vector,MaterialPin::XY,invert,MaterialPin::Left);},"Vector2 connected to an image node.");
    auto& settings=findMaterialNode(graph,blend)->image;const auto topology=materialGraphTopologyHash(graph,MaterialShading::StandardPbr,MaterialBlend::Opaque),values=materialGraphValueHash(graph,MaterialShading::StandardPbr,MaterialBlend::Opaque);settings.contrast=.2f;require(materialGraphTopologyHash(graph,MaterialShading::StandardPbr,MaterialBlend::Opaque)==topology,"A numeric image setting changed topology identity.");require(materialGraphValueHash(graph,MaterialShading::StandardPbr,MaterialBlend::Opaque)==values,"An inactive image branch affected active material values.");
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
}
int main(int argc,char** argv){try{imageNodeFoundation();typesChainsAndTransactions();signaturesAndDormantReferences();heightTessellationPersistence();receiveFogPersistence();rgbScalarInputsAndFallbacks();migrationAndAuthority();repeatedSourceDag();activeCompatibilityTransport();usageOpacityAndSpecular();vectorMappingsAndUniformSafety();schemaOneMigration();explicitParticleMigration();arithmeticAndAnimatedMappings();imageAdjustmentsAndGrunge();exactColdModelSurface();if(argc>1)snapshotMaterials(fs::path(argv[1]));std::cout<<"Material graph types, chains, migration, signatures and references passed.\n";return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
