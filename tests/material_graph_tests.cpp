#include "assets/MaterialAsset.h"
#include "material_fixtures.h"
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
        if(n.kind==MaterialNodeKind::Texture2D)sample=n.texture.image.id.find("orm")!=std::string::npos?Vec4{.2f,.3f,.6f,.8f}:
            n.texture.image.id.find("emit")!=std::string::npos?Vec4{.1f,.2f,.3f,1}:Vec4{.4f,.6f,.8f,.5f};
        if(pin==MaterialPin::RGB)result.value=sample;
        else {const auto channel=pin==MaterialPin::R?0:pin==MaterialPin::G?1:pin==MaterialPin::B?2:3;const auto x=sample[channel];result.value={x,x,x,x};}
        return result;
    }
    if(n.kind==MaterialNodeKind::Saturate) {
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
    findMaterialNode(graph,first)->texture.image.id="assets/textures/first.png";findMaterialNode(graph,second)->texture.image.id="assets/textures/second.png";
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
    auto bounded=blank();while(bounded.nodes.size()<MaterialGraph::MaximumNodes)addMaterialNode(bounded,MaterialNodeKind::Scalar);
    rejects([&]{addMaterialNode(bounded,MaterialNodeKind::Scalar);},"Node bound was ignored.");
}
void signaturesAndDormantReferences() {
    auto graph=blank();const auto out=materialOutputNode(graph).id,rough=node(graph,MaterialNodeKind::Scalar);
    connectMaterialNodes(graph,rough,MaterialPin::Value,out,MaterialPin::Roughness);
    const auto texture=node(graph,MaterialNodeKind::Texture2D);findMaterialNode(graph,texture)->texture.image.id="assets/textures/dormant.png";
    connectMaterialNodes(graph,texture,MaterialPin::R,out,MaterialPin::Occlusion);
    const auto topology=materialGraphTopologyHash(graph,MaterialShading::StandardPbr,MaterialBlend::Opaque);
    const auto values=materialGraphValueHash(graph,MaterialShading::StandardPbr,MaterialBlend::Opaque),layout=materialGraphLayoutHash(graph);
    findMaterialNode(graph,rough)->position={40,50};findMaterialNode(graph,rough)->name="Visible roughness";findMaterialNode(graph,rough)->scalarRange={-1,4};
    require(topology==materialGraphTopologyHash(graph,MaterialShading::StandardPbr,MaterialBlend::Opaque)&&values==materialGraphValueHash(graph,MaterialShading::StandardPbr,MaterialBlend::Opaque)&&layout!=materialGraphLayoutHash(graph),"Layout or slider bounds changed shader identity.");
    findMaterialNode(graph,rough)->scalar=.4f;
    require(topology==materialGraphTopologyHash(graph,MaterialShading::StandardPbr,MaterialBlend::Opaque)&&values!=materialGraphValueHash(graph,MaterialShading::StandardPbr,MaterialBlend::Opaque),"Uniform edit changed topology or failed to change values.");
    materialOutputNode(graph).output.shading=MaterialShading::Unlit;
    const auto order=materialGraphOrder(graph,MaterialShading::Unlit,MaterialBlend::Opaque);
    require(std::find(order.begin(),order.end(),texture)==order.end()&&materialGraphTextureReferences(graph)==std::vector<std::string>{"assets/textures/dormant.png"},"Dormant resources were prepared or forgotten as authored dependencies.");
    const auto restored=parseMaterialGraph(serializeMaterialGraph(graph));
    require(materialInputLink(restored,out,MaterialPin::Roughness)&&materialInputLink(restored,out,MaterialPin::Occlusion),"Model switching or serialization destroyed hidden links.");
    const auto normal=node(graph,MaterialNodeKind::Texture2D);findMaterialNode(graph,normal)->texture.role=MaterialTextureRole::Normal;findMaterialNode(graph,normal)->texture.image.id="assets/textures/dormant.png";
    rewriteMaterialGraphTextures(graph,[](const auto&,MaterialTextureRole role){return std::string(role==MaterialTextureRole::Normal?NeutralNormalTextureId:GrayTextureId);});
    require(findMaterialNode(graph,texture)->texture.image.id==GrayTextureId&&findMaterialNode(graph,normal)->texture.image.id==NeutralNormalTextureId,"Graph reference replacement lost role-correct dormant defaults.");
}
void rgbScalarInputsAndFallbacks() {
    auto graph=blank();const auto out=materialOutputNode(graph).id;
    const auto rough=node(graph,MaterialNodeKind::Texture2D),metal=node(graph,MaterialNodeKind::Texture2D);
    findMaterialNode(graph,rough)->texture.role=MaterialTextureRole::Data;findMaterialNode(graph,rough)->texture.image.id="assets/textures/roughness.png";
    findMaterialNode(graph,metal)->texture.role=MaterialTextureRole::Data;findMaterialNode(graph,metal)->texture.image.id="assets/textures/metallic.png";
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
    require(materialGraphLegacySurface(graph).metallicRoughnessTexture.image.id=="assets/textures/roughness.png","Explicit packed G/B lost legacy fallback representation.");
    materialOutputNode(graph).output.blend=MaterialBlend::Alpha;
    connectMaterialNodes(graph,rough,MaterialPin::RGB,out,MaterialPin::Opacity);
    require(materialGraphLegacySurface(graph).baseColorTexture.image.empty(),"RGB scalar coverage was misinterpreted as texture alpha by legacy fallback.");
    connectMaterialNodes(graph,rough,MaterialPin::A,out,MaterialPin::Opacity);
    require(materialGraphLegacySurface(graph).baseColorTexture.image.id=="assets/textures/roughness.png","Explicit texture alpha lost legacy fallback representation.");
    const auto color=node(graph,MaterialNodeKind::Color),scalar=node(graph,MaterialNodeKind::Scalar),product=node(graph,MaterialNodeKind::Multiply),saturate=node(graph,MaterialNodeKind::Saturate);
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
        require(read(project.root/legacy.id)==bytes&&!loaded.graph.empty()&&loaded.originModel==legacy.originModel,"Migration rewrote the file or lost imported identity.");
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
    findMaterialNode(graph,normal)->texture.image.id="assets/textures/normal-uv1.png";
    connectMaterialNodes(graph,uv,MaterialPin::UV,normal,MaterialPin::UV);
    connectMaterialNodes(graph,normal,MaterialPin::Normal,out,MaterialPin::Normal);
    materialOutputNode(graph).output.shading=MaterialShading::Unlit;canonicalizeMaterialAsset(material);
    require(material.surface.normalTexture.image.empty()&&materialTextureReferences(material)==std::vector<std::string>{"assets/textures/normal-uv1.png"},
        "Inactive Unlit normal branch leaked a UV1 requirement into legacy rendering transport or lost its authored dependency.");
    materialOutputNode(graph).output.shading=MaterialShading::StandardPbr;canonicalizeMaterialAsset(material);
    require(material.surface.normalTexture.image.id=="assets/textures/normal-uv1.png"&&material.surface.normalTexture.texCoord==1,
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
        const auto bytes=json.dump(2);write(project.root/material.id,bytes);const auto loaded=loadMaterialAsset(project.root,material.id);
        const bool particle=blend==MaterialBlend::Alpha||blend==MaterialBlend::Additive;
        const auto migrated=std::find_if(loaded.graph.links.begin(),loaded.graph.links.end(),[&](const auto& value){return value.id==link;});
        require(read(project.root/material.id)==bytes&&migrated!=loaded.graph.links.end()&&migrated->fromNode==scalar,
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
        findMaterialNode(graph,texture)->texture.image.id=image?"assets/textures/alpha.png":"";
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
        auto changed=migrated;findMaterialNode(changed,texture)->texture.image.id=image?"":"assets/textures/new.png";
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
    const auto saturation=std::find_if(hdr.nodes.begin(),hdr.nodes.end(),[](const auto& n){return n.kind==MaterialNodeKind::Saturate;});
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
    for(const char* id:{"assets/materials/4c37e1e87014-0.crimson-material","assets/materials/4c7d9544b858-1.crimson-material","assets/materials/55f2270f21c4-0.crimson-material"}) {
        const auto before=read(root/id);require(!before.empty(),"Expected snapshot material is unavailable.");
        const auto source=nlohmann::json::parse(before);const auto material=loadMaterialAsset(root,id);
        require(!material.graph.empty()&&material.id==id&&read(root/id)==before,"Snapshot migration rewrote authored bytes or lost identity.");
        if(source.at("version").get<int>()<4) {
            require(material.surface.baseColor==source.at("baseColor").get<Vec4>()&&material.surface.roughness==source.at("roughness").get<float>()&&material.surface.metallic==source.at("metallic").get<float>(),"Snapshot migration changed material factors.");
            if(source.at("version")==3)require(material.surface.uvScale==source.at("uvScale").get<Vec3>()&&material.surface.triplanarWorld==source.value("triplanarWorld",false),"Snapshot migration changed world triplanar or scale.");
            const auto& texture=source.at("baseColorTexture");const auto& sampler=source.contains("sharedSampler")&&!source.at("sharedSampler").is_null()?source.at("sharedSampler"):texture;
            require(material.surface.baseColorTexture.image.id==texture.at("image").get<std::string>()&&static_cast<int>(material.surface.baseColorTexture.minFilter)==sampler.at("minFilter").get<int>(),"Snapshot migration lost texture identity or filtering.");
        }
        std::cout<<"Snapshot material read without changes: "<<id<<'\n';
    }
}
}
int main(int argc,char** argv){try{typesChainsAndTransactions();signaturesAndDormantReferences();rgbScalarInputsAndFallbacks();migrationAndAuthority();repeatedSourceDag();activeCompatibilityTransport();usageOpacityAndSpecular();vectorMappingsAndUniformSafety();schemaOneMigration();explicitParticleMigration();if(argc>1)snapshotMaterials(fs::path(argv[1]));std::cout<<"Material graph types, chains, migration, signatures and references passed.\n";return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
