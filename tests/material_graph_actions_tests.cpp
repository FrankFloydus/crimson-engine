#include "resources/AssetId.h"
#include "editor/MaterialGraphActions.h"
#include "editor/MaterialDocument.h"
#include "assets/MaterialGraphReferences.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace {
using namespace crimson;
using Json=nlohmann::json;
namespace fs=std::filesystem;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
template<class F> void rejectedWithoutChange(MaterialGraph& graph,F action,const char* message) {
    const auto before=serializeMaterialGraph(graph);bool rejected{};
    try {action();}catch(const std::exception&){rejected=true;}
    require(rejected&&serializeMaterialGraph(graph)==before,message);
}
MaterialGraph graphWithOutput(){MaterialGraph graph;addMaterialNode(graph,MaterialNodeKind::Output,{650,90});return graph;}
struct Project {
    fs::path root=fs::temp_directory_path()/("crimson-material-actions-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~Project(){
        std::error_code error;const auto temp=fs::weakly_canonical(fs::temp_directory_path(),error);if(error)return;
        const auto target=fs::weakly_canonical(root,error);
        if(!error&&target.parent_path()==temp&&target.filename().string().starts_with("crimson-material-actions-"))fs::remove_all(target,error);
    }
};
Json nodeValues(const MaterialGraph& graph,MaterialNodeId id) {
    const auto serialized=Json::parse(serializeMaterialGraph(graph));
    for(auto node:serialized.at("nodes"))if(node.at("id")==id){node.erase("id");node.erase("position");return node;}
    throw std::runtime_error("Missing node in serialized graph.");
}
void copyPastePreservesSubgraph() {
    auto source=graphWithOutput();const auto output=materialOutputNode(source).id;
    const auto uv=addMaterialNode(source,MaterialNodeKind::UV,{-120,180}).id;
    auto& mapping=findMaterialNode(source,uv)->uv;mapping.mode=MaterialUvMode::WorldTriplanar;
    mapping.scale={2,3,4};mapping.offset={.1f,.2f,.3f};mapping.rotation={15,30,45};mapping.scaleLinked=false;
    const auto texture=addMaterialNode(source,MaterialNodeKind::Texture2D,{40,80}).id;
    auto& sample=findMaterialNode(source,texture)->texture;sample.image.id=canonicalAssetId("assets/textures/paint.png");
    sample.sampler={ModelWrap::Clamp,ModelWrap::Mirror,ModelFilter::NearestMipmapNearest,ModelFilter::Nearest};
    const auto normal=addMaterialNode(source,MaterialNodeKind::Texture2D,{40,340}).id;
    auto& normalSample=findMaterialNode(source,normal)->texture;normalSample.image.id=canonicalAssetId("assets/textures/normal.png");
    normalSample.role=MaterialTextureRole::Normal;normalSample.normalStrength=-.35f;
    const auto scalar=addMaterialNode(source,MaterialNodeKind::Scalar,{200,260}).id;
    findMaterialNode(source,scalar)->scalar=-3.25f;findMaterialNode(source,scalar)->scalarRange={-8,12};findMaterialNode(source,scalar)->name="HDR factor";
    const auto color=addMaterialNode(source,MaterialNodeKind::Color,{40,520}).id;
    findMaterialNode(source,color)->color={.125f,1.75f,.25f,.6f};
    const auto multiply=addMaterialNode(source,MaterialNodeKind::Multiply,{390,80}).id;
    findMaterialNode(source,multiply)->left.value={.3f,.4f,.5f,.6f};findMaterialNode(source,multiply)->right.value={7,8,9,10};
    connectMaterialNodes(source,uv,MaterialPin::UV,texture,MaterialPin::UV);
    connectMaterialNodes(source,uv,MaterialPin::UV,normal,MaterialPin::UV);
    connectMaterialNodes(source,texture,MaterialPin::RGB,multiply,MaterialPin::Left);
    connectMaterialNodes(source,scalar,MaterialPin::Value,multiply,MaterialPin::Right);
    connectMaterialNodes(source,multiply,MaterialPin::Value,output,MaterialPin::BaseColor);
    connectMaterialNodes(source,normal,MaterialPin::Normal,output,MaterialPin::Normal);
    const std::array ordered{uv,texture,normal,scalar,color,multiply};
    const std::array selected{multiply,output,normal,color,uv,texture,scalar,uv,MaterialNodeId{9999}};
    const auto before=serializeMaterialGraph(source),clipboard=copyMaterialGraphSelection(source,selected);
    require(!clipboard.empty()&&serializeMaterialGraph(source)==before,"Copy changed the graph or produced no clipboard.");
    require(clipboard==copyMaterialGraphSelection(source,ordered),"Clipboard depends on selection ordering, duplicates, unknown IDs or Output.");
    const auto firstId=source.nextNodeId,firstLink=source.nextLinkId;
    const auto created=pasteMaterialGraphSelection(source,clipboard,{1000,900});
    require(created.size()==ordered.size()&&source.nodes.size()==13&&source.links.size()==10,"Paste copied protected Output or external links, or lost internal links.");
    for(std::size_t i=0;i<created.size();++i) {
        const auto& old=*findMaterialNode(source,ordered[i]);const auto& node=*findMaterialNode(source,created[i]);
        require(created[i]==firstId+i&&nodeValues(source,ordered[i])==nodeValues(source,created[i]),"Paste changed a node parameter or reused identity.");
        require(node.position==Vec2{old.position[0]+1120,old.position[1]+820},"Paste did not preserve relative placement at the requested anchor.");
    }
    const std::unordered_set<MaterialNodeId> newIds(created.begin(),created.end());
    for(const auto& link:source.links)if(link.id>=firstLink)require(newIds.contains(link.fromNode)&&newIds.contains(link.toNode),"Paste retained an external connection.");
    const auto repeated=pasteMaterialGraphSelection(source,clipboard,{2000,900});
    require(repeated.front()>created.back(),"Repeated paste reused earlier pasted node identities.");
    const auto duplicated=duplicateMaterialGraphSelection(source,ordered);
    for(std::size_t i=0;i<duplicated.size();++i){const auto a=findMaterialNode(source,ordered[i])->position,b=findMaterialNode(source,duplicated[i])->position;require(b==Vec2{a[0]+32,a[1]+32},"Duplicate lost its relative offset.");}
    const std::array outputOnly{output};const auto stable=serializeMaterialGraph(source);
    require(copyMaterialGraphSelection(source,outputOnly).empty()&&duplicateMaterialGraphSelection(source,outputOnly).empty()&&deleteMaterialGraphSelection(source,outputOnly)==0&&serializeMaterialGraph(source)==stable,"Output protection failed.");
}
void externalColorInputsRetainTypes() {
    auto graph=graphWithOutput();const auto output=materialOutputNode(graph).id;
    const auto external=addMaterialNode(graph,MaterialNodeKind::Color).id;
    const auto first=addMaterialNode(graph,MaterialNodeKind::Multiply).id;
    const auto second=addMaterialNode(graph,MaterialNodeKind::Multiply).id;
    findMaterialNode(graph,first)->left.value={.25f,8,9,10};
    connectMaterialNodes(graph,external,MaterialPin::RGB,first,MaterialPin::Left);
    connectMaterialNodes(graph,first,MaterialPin::Value,second,MaterialPin::Left);
    connectMaterialNodes(graph,second,MaterialPin::Value,output,MaterialPin::BaseColor);
    const std::array selected{first,second};const auto pasted=pasteMaterialGraphSelection(graph,copyMaterialGraphSelection(graph,selected),{500,500});
    require(!materialInputLink(graph,pasted[0],MaterialPin::Left)&&findMaterialNode(graph,pasted[0])->left.type==MaterialValueType::Color&&
        findMaterialNode(graph,pasted[0])->left.value==Vec4{.25f,.25f,.25f,.25f}&&materialOutputType(graph,pasted[1],MaterialPin::Value)==MaterialValueType::Color,
        "Copying a partial Color chain lost its inferred disconnected default type.");
    require(materialInputLink(graph,pasted[1],MaterialPin::Left)->fromNode==pasted[0],"Copying a partial chain lost the internal link.");
    const std::array remove{external};require(deleteMaterialGraphSelection(graph,remove)==1&&materialOutputType(graph,second,MaterialPin::Value)==MaterialValueType::Color,"Deleting an external Color dependency invalidated downstream links.");
}
void vectorClipboardAndConnectedCreation() {
    auto graph=graphWithOutput();const auto uv=addMaterialNode(graph,MaterialNodeKind::UV).id;
    const MaterialGraphEndpoint scale{uv,MaterialPin::ScaleXY,true};const auto choices=materialGraphNodeChoices(graph,scale);
    const auto vector=std::find_if(choices.begin(),choices.end(),[](const auto& c){return c.kind==MaterialNodeKind::Vector2;});
    const auto multiply=std::find_if(choices.begin(),choices.end(),[](const auto& c){return c.kind==MaterialNodeKind::Multiply;});
    require(vector!=choices.end()&&vector->pin==MaterialPin::XY&&multiply!=choices.end()&&choices.size()==5,"UV transform did not offer uniform Vector2 constants and arithmetic.");
    const auto source=addConnectedMaterialGraphNode(graph,scale,*vector,{10,20});findMaterialNode(graph,source)->vector2={-2,4};
    const auto product=addConnectedMaterialGraphNode(graph,scale,*multiply,{50,20});
    connectMaterialNodes(graph,source,MaterialPin::XY,product,MaterialPin::Left);
    require(materialOutputType(graph,product,MaterialPin::Value)==MaterialValueType::Vector2,"New Multiply did not infer Vector2.");
    const std::array selected{product,uv};const auto pasted=pasteMaterialGraphSelection(graph,copyMaterialGraphSelection(graph,selected),{400,300});
    MaterialNodeId pastedProduct{},pastedUv{};
    for(const auto id:pasted)if(findMaterialNode(graph,id)->kind==MaterialNodeKind::Multiply)pastedProduct=id;else pastedUv=id;
    require(findMaterialNode(graph,pastedProduct)->left.type==MaterialValueType::Vector2&&
        materialInputLink(graph,pastedUv,MaterialPin::ScaleXY)->fromNode==pastedProduct,
        "Copying a Vector2 partial chain lost fallback type or internal transform connection.");
    const auto outgoing=materialGraphNodeChoices(graph,{source,MaterialPin::XY,false});
    require(outgoing.size()==6&&std::any_of(outgoing.begin(),outgoing.end(),[](const auto& c){return c.kind==MaterialNodeKind::UV&&c.pin==MaterialPin::ScaleXY;})&&
        std::any_of(outgoing.begin(),outgoing.end(),[](const auto& c){return c.kind==MaterialNodeKind::Panner&&c.pin==MaterialPin::Speed;}),
        "Vector2 output did not offer uniform mapping and arithmetic destinations.");
}
void malformedAndBoundedPasteIsAtomic() {
    auto source=graphWithOutput();const auto scalar=addMaterialNode(source,MaterialNodeKind::Scalar).id;const std::array selected{scalar};
    const auto clipboard=copyMaterialGraphSelection(source,selected);auto target=graphWithOutput();
    for(const auto& text:std::vector<std::string>{"","not json","{}",std::string(MaterialGraphClipboardMaximumBytes+1,'x')})
        rejectedWithoutChange(target,[&]{pasteMaterialGraphSelection(target,text,{});},"Malformed/oversized clipboard changed the destination.");
    auto payload=Json::parse(clipboard);payload["version"]=2;
    rejectedWithoutChange(target,[&]{pasteMaterialGraphSelection(target,payload.dump(),{});},"Unsupported clipboard version was accepted.");
    payload=Json::parse(clipboard);payload["graph"]["nodes"][1]["id"]=1;
    rejectedWithoutChange(target,[&]{pasteMaterialGraphSelection(target,payload.dump(),{});},"Duplicate identities were accepted.");
    payload=Json::parse(clipboard);payload["graph"]["links"].push_back({{"id",1},{"fromNode",2},{"fromPin",static_cast<int>(MaterialPin::Value)},{"toNode",1},{"toPin",static_cast<int>(MaterialPin::Roughness)}});payload["graph"]["nextLinkId"]=2;
    rejectedWithoutChange(target,[&]{pasteMaterialGraphSelection(target,payload.dump(),{});},"A clipboard connection to protected Output was accepted.");
    rejectedWithoutChange(target,[&]{pasteMaterialGraphSelection(target,clipboard,{std::numeric_limits<float>::infinity(),0});},"A nonfinite paste position changed the destination.");
    target.nextNodeId=std::numeric_limits<MaterialNodeId>::max();
    rejectedWithoutChange(target,[&]{pasteMaterialGraphSelection(target,clipboard,{});},"Paste wrapped the node identity counter.");
    target=graphWithOutput();while(target.nodes.size()<MaterialGraph::MaximumNodes)addMaterialNode(target,MaterialNodeKind::Scalar);
    rejectedWithoutChange(target,[&]{pasteMaterialGraphSelection(target,clipboard,{});},"Over-budget paste partially inserted its nodes.");
    require(materialGraphNodeChoices(target,{materialOutputNode(target).id,MaterialPin::Roughness,true}).empty(),"Full graph still offered impossible node creation.");
    auto chain=graphWithOutput();const auto a=addMaterialNode(chain,MaterialNodeKind::Scalar).id,b=addMaterialNode(chain,MaterialNodeKind::Multiply).id;
    connectMaterialNodes(chain,a,MaterialPin::Value,b,MaterialPin::Left);const std::array pair{a,b};const auto linked=copyMaterialGraphSelection(chain,pair);
    target=graphWithOutput();target.nextLinkId=std::numeric_limits<MaterialLinkId>::max();
    rejectedWithoutChange(target,[&]{pasteMaterialGraphSelection(target,linked,{});},"Paste wrapped the link identity counter.");
    payload=Json::parse(linked);payload["graph"]["links"][0]["fromPin"]=static_cast<int>(MaterialPin::RGB);
    target=graphWithOutput();rejectedWithoutChange(target,[&]{pasteMaterialGraphSelection(target,payload.dump(),{});},"Invalid pasted pin types changed the destination.");
    payload=Json::parse(linked);payload["graph"]["links"][0]["fromNode"]=3;payload["graph"]["links"][0]["fromPin"]=static_cast<int>(MaterialPin::Value);
    rejectedWithoutChange(target,[&]{pasteMaterialGraphSelection(target,payload.dump(),{});},"A cyclic clipboard was accepted.");
}
MaterialGraphNodeChoice choiceFor(const MaterialGraph& graph,MaterialGraphEndpoint endpoint,MaterialNodeKind kind) {
    const auto choices=materialGraphNodeChoices(graph,endpoint);
    const auto choice=std::find_if(choices.begin(),choices.end(),[&](const auto& c){return c.kind==kind;});
    if(choice==choices.end())throw std::runtime_error("Expected node creation choice is missing.");return *choice;
}
void compatibleCreationAndReplacement() {
    auto graph=graphWithOutput();const auto output=materialOutputNode(graph).id;
    const MaterialGraphEndpoint roughness{output,MaterialPin::Roughness,true};
    const auto scalar=addConnectedMaterialGraphNode(graph,roughness,choiceFor(graph,roughness,MaterialNodeKind::Scalar),{100,200});
    require(materialInputLink(graph,output,MaterialPin::Roughness)->fromNode==scalar,"Dragging from an input did not create its source.");
    const auto before=serializeMaterialGraph(graph);const auto roughTexture=choiceFor(graph,roughness,MaterialNodeKind::Texture2D);
    require(roughTexture.pin==MaterialPin::R&&roughTexture.textureRole==MaterialTextureRole::Data&&serializeMaterialGraph(graph)==before,"Scalar texture suggestion assumed packed channels or changed the occupied input.");
    const auto texture=addConnectedMaterialGraphNode(graph,roughness,roughTexture,{240,200});
    require(materialInputLink(graph,output,MaterialPin::Roughness)->fromNode==texture&&graph.links.size()==1&&findMaterialNode(graph,scalar),"Connected creation failed to replace exactly one occupied input.");
    const MaterialGraphEndpoint mapping{texture,MaterialPin::UV,true};const auto uvChoices=materialGraphNodeChoices(graph,mapping);
    require(uvChoices.size()==2&&uvChoices.front().kind==MaterialNodeKind::UV&&uvChoices.back().kind==MaterialNodeKind::Panner,"UV input offered incompatible node kinds.");
    const auto uv=addConnectedMaterialGraphNode(graph,mapping,uvChoices.front(),{});
    const MaterialGraphEndpoint uvOutput{uv,MaterialPin::UV,false};const auto textureChoices=materialGraphNodeChoices(graph,uvOutput);
    require(textureChoices.size()==6&&textureChoices.front().kind==MaterialNodeKind::Texture2D&&
        std::any_of(textureChoices.begin(),textureChoices.end(),[](const auto& c){return c.kind==MaterialNodeKind::ImageTransform&&c.pin==MaterialPin::UV;}),"UV output omitted completed-image mapping or offered incompatible node kinds.");
    const auto secondTexture=addConnectedMaterialGraphNode(graph,uvOutput,textureChoices.front(),{});
    const auto gradient=addConnectedMaterialGraphNode(graph,uvOutput,choiceFor(graph,uvOutput,MaterialNodeKind::RadialGradient),{});
    require(materialInputLink(graph,gradient,MaterialPin::UV)->fromNode==uv&&materialOutputType(graph,gradient,MaterialPin::Value)==MaterialValueType::Scalar,
        "UV output did not create a mapped radial gradient.");
    const auto particle=choiceFor(graph,roughness,MaterialNodeKind::ParticleColor);
    require(particle.pin==MaterialPin::R,"Particle Color scalar suggestion did not select a channel.");
    require(materialInputLink(graph,texture,MaterialPin::UV)->fromNode==uv&&materialInputLink(graph,secondTexture,MaterialPin::UV)->fromNode==uv,"Creating from shared UV output stole its first connection.");
    const MaterialGraphEndpoint baseColor{output,MaterialPin::BaseColor,true};
    const auto multiply=addConnectedMaterialGraphNode(graph,baseColor,choiceFor(graph,baseColor,MaterialNodeKind::Multiply),{});
    require(materialOutputType(graph,multiply,MaterialPin::Value)==MaterialValueType::Color,"New Multiply was not initialized for the destination Color type.");
    const MaterialGraphEndpoint normal{output,MaterialPin::Normal,true};const auto normalChoices=materialGraphNodeChoices(graph,normal);
    require(normalChoices.size()==3&&normalChoices.front().kind==MaterialNodeKind::Texture2D&&normalChoices.front().textureRole==MaterialTextureRole::Normal&&normalChoices.front().pin==MaterialPin::Normal&&
        std::all_of(normalChoices.begin(),normalChoices.end(),[](const auto& c){return c.pin==MaterialPin::Normal&&(c.kind==MaterialNodeKind::Texture2D||c.kind==MaterialNodeKind::HeightToNormal||c.kind==MaterialNodeKind::DecodeNormal);}),"Normal input offered non-normal expressions.");
    const auto normalTexture=addConnectedMaterialGraphNode(graph,normal,normalChoices.front(),{});
    require(materialGraphNodeChoices(graph,{normalTexture,MaterialPin::Normal,false}).empty(),"Normal output incorrectly offered arithmetic nodes.");
    materialOutputNode(graph).output.blend=MaterialBlend::Mask;
    const auto opacity=choiceFor(graph,{output,MaterialPin::OpacityMask,true},MaterialNodeKind::Texture2D);
    require(opacity.pin==MaterialPin::A&&opacity.textureRole==MaterialTextureRole::Color,"Opacity suggestion did not use a texture alpha channel.");
    const auto outgoing=materialGraphNodeChoices(graph,{texture,MaterialPin::R,false});
    require(outgoing.size()>9&&outgoing.front().kind==MaterialNodeKind::Multiply&&outgoing.front().pin==MaterialPin::Left&&std::any_of(outgoing.begin(),outgoing.end(),[](const auto& choice){return choice.kind==MaterialNodeKind::HeightToNormal;}),"Numeric output did not offer compatible arithmetic and adjustment inputs.");
    rejectedWithoutChange(graph,[&]{addConnectedMaterialGraphNode(graph,{9999,MaterialPin::Value,false},outgoing.front(),{});},"Stale endpoint changed the graph.");
    rejectedWithoutChange(graph,[&]{addConnectedMaterialGraphNode(graph,roughness,{MaterialNodeKind::UV,MaterialTextureRole::Color,MaterialPin::UV},{});},"Incompatible drag-created node changed the graph.");
    materialOutputNode(graph).output.shading=MaterialShading::Unlit;
    rejectedWithoutChange(graph,[&]{addConnectedMaterialGraphNode(graph,normal,normalChoices.front(),{});},"Dormant shader input accepted node creation.");
}
void documentCommandsAndDeletedClipboard() {
    Project project;MaterialDocument document(project.root);require(document.create("Clipboard history"),"Could not create the clipboard document.");
    std::vector<MaterialNodeId> selected;const std::string removed=canonicalAssetId("assets/textures/deleted-clipboard.png");
    require(document.edit("Add texture roles",[&](auto& asset){
        for(const auto role:{MaterialTextureRole::Color,MaterialTextureRole::Data,MaterialTextureRole::Normal}) {
            const auto id=addMaterialNode(asset.graph,MaterialNodeKind::Texture2D).id;auto& texture=findMaterialNode(asset.graph,id)->texture;
            texture.role=role;texture.image.id=removed;selected.push_back(id);
        }
    })&&document.save(),"Could not prepare clipboard role references.");
    const auto clipboard=copyMaterialGraphSelection(document.definition().graph,selected),beforeCut=serializeMaterialAsset(document.definition());
    require(document.edit("Cut nodes",[&](auto& asset){deleteMaterialGraphSelection(asset.graph,selected);})&&document.undo()&&serializeMaterialAsset(document.definition())==beforeCut&&!document.dirty(),"Cut did not undo as one complete command.");
    require(document.redo()&&document.undo(),"Cut could not redo and undo.");
    const auto replace=[&](const std::string& id,MaterialTextureRole role){return id==removed?std::string(role==MaterialTextureRole::Normal?NeutralNormalTextureId:GrayTextureId):id;};
    require(document.reconcileTextureReferences(replace),"Initial texture deletion reconciliation failed.");
    const auto beforePaste=serializeMaterialAsset(document.definition());std::vector<MaterialNodeId> pasted;
    require(document.edit("Paste nodes",[&](auto& asset){pasted=pasteMaterialGraphSelection(asset.graph,clipboard,{300,400});}),"Old clipboard could not be pasted.");
    const auto pastedReferences=materialTextureReferences(document.definition());
    require(std::find(pastedReferences.begin(),pastedReferences.end(),removed)!=pastedReferences.end(),"Fixture did not reintroduce the stale clipboard reference.");
    require(document.reconcileTextureReferences(replace),"Repeated deletion policy could not reconcile freshly pasted nodes.");
    const auto assertRewritten=[&] {
        for(const auto* asset:{&document.definition(),document.committed().get()})for(const auto& node:asset->graph.nodes)if(node.kind==MaterialNodeKind::Texture2D)
            require(node.texture.image.id==(node.texture.role==MaterialTextureRole::Normal?NeutralNormalTextureId:GrayTextureId),"Pasted historical texture did not receive its role-correct fallback.");
        const auto history=document.historicalTextureReferences();require(std::find(history.begin(),history.end(),removed)==history.end(),"Pasted deleted reference survived in undo/redo history.");
    };
    assertRewritten();const auto afterPaste=serializeMaterialAsset(document.definition());
    require(document.undo()&&serializeMaterialAsset(document.definition())==beforePaste,"Paste/reconciliation did not undo as one command.");assertRewritten();
    require(document.redo()&&serializeMaterialAsset(document.definition())==afterPaste,"Paste redo did not preserve new identities and reconciled references.");assertRewritten();
    const auto beforeDuplicate=serializeMaterialAsset(document.definition());
    require(document.edit("Duplicate nodes",[&](auto& asset){duplicateMaterialGraphSelection(asset.graph,pasted);})&&document.undo()&&serializeMaterialAsset(document.definition())==beforeDuplicate,"Duplicate was not one undoable command.");
    const auto immutable=document.committed();const auto revision=document.revision();
    require(!document.edit("Invalid paste",[&](auto& asset){pasteMaterialGraphSelection(asset.graph,"garbage",{});})&&document.committed()==immutable&&document.revision()==revision,"Invalid paste published a partial document revision.");
    MaterialDocument builtin(project.root);require(builtin.load(DefaultMaterialId)&&!builtin.editable(),"Could not prepare a protected material document.");
    require(!builtin.edit("Paste to protected material",[&](auto& asset){pasteMaterialGraphSelection(asset.graph,clipboard,{});}),"Document allowed clipboard edits to a protected material.");
}
void rgbScalarConnectionHistory() {
    Project project;MaterialDocument document(project.root);require(document.create("Texture scalar inputs"),"Could not create a scalar-link document.");
    MaterialNodeId rough{},metal{},out{};
    require(document.edit("Add data maps",[&](auto& asset){
        out=materialOutputNode(asset.graph).id;
        rough=addMaterialNode(asset.graph,MaterialNodeKind::Texture2D).id;
        metal=addMaterialNode(asset.graph,MaterialNodeKind::Texture2D).id;
        findMaterialNode(asset.graph,rough)->texture.role=MaterialTextureRole::Data;
        findMaterialNode(asset.graph,metal)->texture.role=MaterialTextureRole::Data;
    }),"Could not prepare texture nodes for connection history.");
    const auto before=serializeMaterialAsset(document.definition());
    MaterialLinkId roughLink{},metalLink{};
    require(document.edit("Connect data maps",[&](auto& asset){
        roughLink=connectMaterialNodes(asset.graph,rough,MaterialPin::RGB,out,MaterialPin::Roughness);
        metalLink=connectMaterialNodes(asset.graph,metal,MaterialPin::RGB,out,MaterialPin::Metallic);
    }),"Document rejected RGB roughness/metallic map connections.");
    const auto after=serializeMaterialAsset(document.definition());
    require(document.undo()&&serializeMaterialAsset(document.definition())==before,"Replacing scalar inputs with RGB maps did not undo atomically.");
    require(document.redo()&&serializeMaterialAsset(document.definition())==after,"RGB scalar-link redo changed pin or link identities.");
    require(document.save(),"Could not save RGB scalar links.");
    MaterialDocument reopened(project.root);require(reopened.load(document.assetId()),"Could not reopen RGB scalar links.");
    const auto& graph=reopened.definition().graph;
    require(materialInputLink(graph,out,MaterialPin::Roughness)->id==roughLink&&materialInputLink(graph,out,MaterialPin::Roughness)->fromPin==MaterialPin::RGB&&
        materialInputLink(graph,out,MaterialPin::Metallic)->id==metalLink&&materialInputLink(graph,out,MaterialPin::Metallic)->fromPin==MaterialPin::RGB,
        "Save/reopen rewrote RGB-to-scalar connections.");
}
void directChannelAuthoringAndResize() {
    Project project;MaterialDocument document(project.root);require(document.create("Direct channel connections"),"Could not create connection fixture.");
    MaterialNodeId output{},color{},levels{},resize{},uv{},texture{};
    require(document.edit("Configure connection sources",[&](auto& asset) {
        asset.graph={};output=addMaterialNode(asset.graph,MaterialNodeKind::Output).id;
        color=addMaterialNode(asset.graph,MaterialNodeKind::Color,{-600,0}).id;
        findMaterialNode(asset.graph,color)->color={.21f,.47f,.83f,.35f};
        levels=addMaterialNode(asset.graph,MaterialNodeKind::Levels,{-300,200}).id;
        resize=addMaterialNode(asset.graph,MaterialNodeKind::Resize,{-300,400}).id;
        uv=addMaterialNode(asset.graph,MaterialNodeKind::UV).id;texture=addMaterialNode(asset.graph,MaterialNodeKind::Texture2D).id;
    }),"Could not configure typed connections.");
    const auto& graph=document.definition().graph;
    require(inspectMaterialGraphConnection(graph,color,MaterialPin::RGB,output,MaterialPin::Roughness).kind==MaterialGraphConnectionKind::RedChannel&&
        inspectMaterialGraphConnection(graph,color,MaterialPin::RGBA,output,MaterialPin::Metallic).kind==MaterialGraphConnectionKind::RedChannel,
        "Cross-family scalar authoring did not expose its Red conversion.");
    require(inspectMaterialGraphConnection(graph,color,MaterialPin::RGB,levels,MaterialPin::Left).kind==MaterialGraphConnectionKind::Direct&&
        inspectMaterialGraphConnection(graph,color,MaterialPin::RGBA,resize,MaterialPin::Left).kind==MaterialGraphConnectionKind::Direct,
        "A polymorphic image input was treated as a fixed Scalar socket.");
    require(inspectMaterialGraphConnection(graph,uv,MaterialPin::UV,output,MaterialPin::Roughness).kind==MaterialGraphConnectionKind::Invalid&&
        inspectMaterialGraphConnection(graph,color,MaterialPin::RGB,texture,MaterialPin::UV).kind==MaterialGraphConnectionKind::Invalid&&
        inspectMaterialGraphConnection(graph,color,MaterialPin::RGB,output,MaterialPin::Normal).kind==MaterialGraphConnectionKind::Invalid,
        "Mapping or Normal links accepted numeric coercion.");
    const auto before=serializeMaterialAsset(document.definition());const auto nodeCount=graph.nodes.size();
    require(document.edit("Connect RGBA to Scalar",[&](auto& asset) {
        connectMaterialGraphAuthoring(asset.graph,color,MaterialPin::RGBA,output,MaterialPin::Roughness);
    }),"Direct RGBA-to-Scalar connection failed.");
    const auto& connected=document.definition().graph;const auto* link=materialInputLink(connected,output,MaterialPin::Roughness);
    require(link&&link->fromNode==color&&link->fromPin==MaterialPin::RGBA&&connected.links.size()==1&&
        connected.nodes.size()==nodeCount&&evaluateMaterialUniform(connected,color,MaterialPin::RGBA).value[0]==.21f,
        "RGBA-to-Scalar authoring changed endpoints, inserted a node, or changed first-component behavior.");
    const auto after=serializeMaterialAsset(document.definition());
    require(document.undo()&&serializeMaterialAsset(document.definition())==before&&document.redo()&&serializeMaterialAsset(document.definition())==after,
        "Direct RGBA-to-Scalar wire did not form one exact undo/redo command.");
    require(document.edit("Resize an RGB image",[&](auto& asset) {
        connectMaterialGraphAuthoring(asset.graph,color,MaterialPin::RGB,resize,MaterialPin::Left);
        auto& settings=findMaterialNode(asset.graph,resize)->resize;settings.width=320;settings.height=192;settings.filter=MaterialResizeFilter::Area;
    }),"Could not author Resize settings.");
    require(materialOutputType(document.definition().graph,resize,MaterialPin::Value)==MaterialValueType::Color,
        "Resize failed to preserve the connected RGB family.");
    const std::array selection{resize};auto copy=document.definition().graph;
    const auto pasted=pasteMaterialGraphSelection(copy,copyMaterialGraphSelection(copy,selection),{500,500});
    const auto& settings=findMaterialNode(copy,pasted.front())->resize;
    require(settings.width==320&&settings.height==192&&settings.filter==MaterialResizeFilter::Area,
        "Resize clipboard lost dimensions or filtering.");
    auto full=graphWithOutput();const auto source=addMaterialNode(full,MaterialNodeKind::Color).id;
    while(full.nodes.size()<MaterialGraph::MaximumNodes)addMaterialNode(full,MaterialNodeKind::Scalar);
    const auto stable=serializeMaterialGraph(full);
    require(inspectMaterialGraphConnection(full,source,MaterialPin::RGB,materialOutputNode(full).id,MaterialPin::Roughness).kind==MaterialGraphConnectionKind::RedChannel&&
        serializeMaterialGraph(full)==stable,"Direct RGB-to-Scalar inspection incorrectly required another node slot.");
    connectMaterialGraphAuthoring(full,source,MaterialPin::RGB,materialOutputNode(full).id,MaterialPin::Roughness);
    require(full.nodes.size()==MaterialGraph::MaximumNodes&&full.links.size()==1&&
        materialInputLink(full,materialOutputNode(full).id,MaterialPin::Roughness)->fromNode==source,
        "A full graph could not connect existing RGB and Scalar endpoints directly.");
    auto linksFull=graphWithOutput();const auto channel=addMaterialNode(linksFull,MaterialNodeKind::Color).id;
    while(linksFull.links.size()<MaterialGraph::MaximumLinks) {
        const auto merge=addMaterialNode(linksFull,MaterialNodeKind::RGBAMerge).id;
        for(const auto pin:{MaterialPin::R,MaterialPin::G,MaterialPin::B,MaterialPin::A})
            linksFull.links.push_back({linksFull.nextLinkId++,channel,merge,MaterialPin::R,pin});
    }
    validateMaterialGraph(linksFull);
    require(inspectMaterialGraphConnection(linksFull,channel,MaterialPin::RGBA,materialOutputNode(linksFull).id,MaterialPin::Roughness).kind==MaterialGraphConnectionKind::Invalid,
        "Direct conversion ignored the real graph link budget.");
    rejectedWithoutChange(linksFull,[&]{connectMaterialGraphAuthoring(linksFull,channel,MaterialPin::RGBA,materialOutputNode(linksFull).id,MaterialPin::Roughness);},
        "A link-budget rejection partially changed the authored graph.");
}
void directDesaturationHeightRoute() {
    auto graph=graphWithOutput();const auto output=materialOutputNode(graph).id;
    materialOutputNode(graph).output.tessellation.enabled=true;
    const auto albedo=addMaterialNode(graph,MaterialNodeKind::Texture2D).id;
    findMaterialNode(graph,albedo)->texture.image.id=canonicalAssetId("assets/textures/height-albedo.png");
    findMaterialNode(graph,albedo)->texture.role=MaterialTextureRole::Color;
    const auto desaturation=addMaterialNode(graph,MaterialNodeKind::Desaturation).id;
    findMaterialNode(graph,desaturation)->right.value={0,0,0,0};
    const auto levels=addMaterialNode(graph,MaterialNodeKind::Levels).id;
    findMaterialNode(graph,levels)->image.gamma=2;
    const auto nodeCount=graph.nodes.size();
    connectMaterialGraphAuthoring(graph,albedo,MaterialPin::RGB,desaturation,MaterialPin::Left);
    connectMaterialGraphAuthoring(graph,desaturation,MaterialPin::RGB,levels,MaterialPin::Left);
    connectMaterialGraphAuthoring(graph,levels,MaterialPin::Value,output,MaterialPin::Height);
    require(graph.nodes.size()==nodeCount&&graph.links.size()==3&&
        materialInputLink(graph,desaturation,MaterialPin::Left)->fromNode==albedo&&
        materialInputLink(graph,levels,MaterialPin::Left)->fromNode==desaturation&&
        materialInputLink(graph,output,MaterialPin::Height)->fromNode==levels&&
        materialInputLink(graph,output,MaterialPin::Height)->fromPin==MaterialPin::Value,
        "Albedo -> Desaturation -> Levels -> Height inserted an unrequested conversion node.");
    // Texture sampling is not a uniform expression. Substitute a known linear
    // texel at the same source ID to check the downstream numerical contract.
    auto numerical=graph;auto& texel=*findMaterialNode(numerical,albedo);texel.kind=MaterialNodeKind::Color;texel.color={.21f,.47f,.83f,.35f};
    const auto value=evaluateMaterialUniform(numerical,levels,MaterialPin::Value);
    const float expected=std::sqrt(.21f*.2126f+.47f*.7152f+.83f*.0722f);
    require(value.type==MaterialValueType::Color&&std::abs(value.value[0]-expected)<1e-6f&&
        value.value[0]==value.value[1]&&value.value[1]==value.value[2],
        "Saturation zero and Levels did not deliver the same B&W height value in every RGB component.");
    require(serializeMaterialGraph(parseMaterialGraph(serializeMaterialGraph(graph)))==serializeMaterialGraph(graph),
        "Saving the direct Height route inserted conversion nodes or changed its endpoints.");
}
void mixedImageInputCapabilities() {
    auto graph=graphWithOutput();const auto scalar=addMaterialNode(graph,MaterialNodeKind::Scalar).id;
    const auto color=addMaterialNode(graph,MaterialNodeKind::Color).id;
    const auto vector=addMaterialNode(graph,MaterialNodeKind::Vector2).id;
    const auto mapping=addMaterialNode(graph,MaterialNodeKind::UV).id;
    const auto normal=addMaterialNode(graph,MaterialNodeKind::Texture2D).id;
    findMaterialNode(graph,normal)->texture.role=MaterialTextureRole::Normal;
    for(const auto kind:{MaterialNodeKind::Levels,MaterialNodeKind::Invert,MaterialNodeKind::Blend,
        MaterialNodeKind::BrightnessContrast,MaterialNodeKind::Sharpen,MaterialNodeKind::Resize,
        MaterialNodeKind::Desaturation,MaterialNodeKind::Contrast,MaterialNodeKind::ContrastRGB,
        MaterialNodeKind::HueShift,MaterialNodeKind::Grayscale}) {
        const auto node=addMaterialNode(graph,kind).id;
        for(const auto pin:{MaterialPin::Left,MaterialPin::Right}) {
            const bool expected=pin==MaterialPin::Left||kind==MaterialNodeKind::Blend;
            require(materialGraphInputMixedImage(*findMaterialNode(graph,node),pin)==expected,
                "Image capability metadata confused an image input with an adjustment strength.");
            if(!expected)continue;
            for(const auto source:std::array{MaterialGraphEndpoint{scalar,MaterialPin::Value,false},
                MaterialGraphEndpoint{color,MaterialPin::RGB,false},MaterialGraphEndpoint{color,MaterialPin::RGBA,false}}) {
                require(materialCanConnect(graph,source.node,source.pin,node,pin),
                    "A mixed image socket does not accept all advertised Scalar/RGB/RGBA families.");
                connectMaterialNodes(graph,source.node,source.pin,node,pin);
                const auto type=materialOutputType(graph,source.node,source.pin);
                require(materialGraphInputMixedImage(*findMaterialNode(graph,node),pin),
                    "Connecting a source changed an image input's accepted-family metadata.");
                if(kind==MaterialNodeKind::Grayscale)require(materialOutputType(graph,node,MaterialPin::Value)==MaterialValueType::Scalar,
                    "A mixed Grayscale input changed its fixed scalar output family.");
                else if(kind==MaterialNodeKind::Contrast)require(materialOutputType(graph,node,MaterialPin::Value)==MaterialValueType::Scalar&&
                    materialOutputType(graph,node,MaterialPin::RGB)==MaterialValueType::Color,
                    "Mixed Contrast input metadata changed its fixed scalar/RGB output families.");
                else if(materialNodeImageAdjustment(kind))require(materialOutputType(graph,node,MaterialPin::RGB)==MaterialValueType::Color,
                    "An image adjustment's mixed input changed its RGB output family.");
                else if(kind==MaterialNodeKind::Blend)require(materialOutputType(graph,node,MaterialPin::Value)==
                    (pin==MaterialPin::Left?type:MaterialValueType::Color4),
                    "Mixed Blend input metadata changed its combined resolved output family.");
                else require(materialOutputType(graph,node,MaterialPin::Value)==type,
                    "Mixed input metadata replaced an image node's resolved output family.");
            }
            for(const auto source:std::array{MaterialGraphEndpoint{vector,MaterialPin::XY,false},
                MaterialGraphEndpoint{mapping,MaterialPin::UV,false},MaterialGraphEndpoint{normal,MaterialPin::Normal,false}})
                require(!materialCanConnect(graph,source.node,source.pin,node,pin),
                    "Mixed image capability advertised non-image numeric or surface families.");
        }
        require(!materialGraphInputMixedImage(*findMaterialNode(graph,node),MaterialPin::Alpha),
            "An image mask was mislabeled as accepting mixed image families.");
    }
    for(const auto kind:{MaterialNodeKind::Output,MaterialNodeKind::ToColor,MaterialNodeKind::HeightToNormal,
        MaterialNodeKind::RGBAMerge,MaterialNodeKind::RGBASplit,MaterialNodeKind::AlphaMerge,MaterialNodeKind::AlphaSplit,
        MaterialNodeKind::Texture2D,MaterialNodeKind::UV,MaterialNodeKind::Panner,MaterialNodeKind::Multiply,
        MaterialNodeKind::Add,MaterialNodeKind::Lerp,MaterialNodeKind::Power,MaterialNodeKind::Clamp}) {
        MaterialNode node;node.kind=kind;
        for(const auto pin:{MaterialPin::Left,MaterialPin::Right,MaterialPin::Alpha,MaterialPin::Height,
            MaterialPin::BaseColor,MaterialPin::Roughness,MaterialPin::Normal,MaterialPin::UV,MaterialPin::R,
            MaterialPin::G,MaterialPin::B,MaterialPin::A,MaterialPin::ScaleXY,MaterialPin::OffsetXY,
            MaterialPin::Time,MaterialPin::Speed})
            require(!materialGraphInputMixedImage(node,pin),
                "Fixed semantic or Vector2-capable numeric sockets were mislabeled as mixed images.");
    }
    const auto stable=serializeMaterialGraph(graph);
    for(const auto& node:graph.nodes)for(const auto pin:{MaterialPin::Left,MaterialPin::Right,MaterialPin::Alpha})
        (void)materialGraphInputMixedImage(node,pin);
    require(serializeMaterialGraph(graph)==stable,"Accepted-family presentation changed authored graph data.");
}
void animatedNodeHistory() {
    Project project;MaterialDocument document(project.root);require(document.create("Animated node history"),"Could not create animation document.");
    const auto before=serializeMaterialAsset(document.definition());std::vector<MaterialNodeId> nodes;
    require(document.edit("Add animation chain",[&](auto& asset) {
        for(const auto kind:{MaterialNodeKind::Add,MaterialNodeKind::Lerp,MaterialNodeKind::Panner,MaterialNodeKind::Time,MaterialNodeKind::Power})
            nodes.push_back(addMaterialNode(asset.graph,kind).id);
        findMaterialNode(asset.graph,nodes[2])->panner.speed={.2f,-.4f};
        findMaterialNode(asset.graph,nodes[3])->time={true,true,2};
        connectMaterialNodes(asset.graph,nodes[3],MaterialPin::Value,nodes[2],MaterialPin::Time);
    }),"Could not add animation chain.");
    const auto after=serializeMaterialAsset(document.definition());
    require(document.undo()&&serializeMaterialAsset(document.definition())==before&&document.redo()&&serializeMaterialAsset(document.definition())==after,
        "Animation chain was not one reversible graph command.");
    const auto clipboard=copyMaterialGraphSelection(document.definition().graph,nodes);
    require(document.edit("Duplicate animation chain",[&](auto& asset){pasteMaterialGraphSelection(asset.graph,clipboard,{500,400});}),"Could not paste new nodes.");
    require(document.undo()&&serializeMaterialAsset(document.definition())==after,"Pasting animated nodes did not restore the source graph on undo.");
    require(document.begin("Change animation speed")&&document.update([&](auto& asset){findMaterialNode(asset.graph,nodes[2])->panner.speed={7,8};}),"Could not begin speed gesture.");
    document.cancel();require(serializeMaterialAsset(document.definition())==after,"Cancel did not restore exact Panner speed.");
    require(document.save(),"Could not save animated material.");MaterialDocument reopened(project.root);
    require(reopened.load(document.assetId())&&serializeMaterialAsset(reopened.definition())==serializeMaterialAsset(document.definition()),"Animated nodes lost settings during save/reopen.");
}
void adjustmentAndGrungeActions() {
    auto graph=graphWithOutput();const auto output=materialOutputNode(graph).id;
    const auto source=addMaterialNode(graph,MaterialNodeKind::Color).id;
    const MaterialGraphEndpoint rgbSource{source,MaterialPin::RGB,false},scalarSource{source,MaterialPin::R,false};
    const MaterialGraphEndpoint colorTarget{output,MaterialPin::BaseColor,true},scalarTarget{output,MaterialPin::Roughness,true};
    for(const auto kind:{MaterialNodeKind::Desaturation,MaterialNodeKind::Contrast,MaterialNodeKind::ContrastRGB,MaterialNodeKind::HueShift}) {
        const auto colorChoice=choiceFor(graph,colorTarget,kind),scalarChoice=choiceFor(graph,scalarTarget,kind);
        require(colorChoice.pin==MaterialPin::RGB&&scalarChoice.pin==(kind==MaterialNodeKind::Contrast?MaterialPin::Value:MaterialPin::R),
            "Image adjustment suggestions chose incompatible destination outputs.");
        for(const auto endpoint:{rgbSource,scalarSource}) {
            const auto id=addConnectedMaterialGraphNode(graph,endpoint,choiceFor(graph,endpoint,kind),{});
            const auto* link=materialInputLink(graph,id,MaterialPin::Left);
            require(link&&link->fromNode==source&&link->fromPin==endpoint.pin&&findMaterialNode(graph,id)->left.type==MaterialValueType::Color,
                "Image adjustment creation failed to preserve scalar/RGB links and its authored Color fallback.");
        }
        addConnectedMaterialGraphNode(graph,colorTarget,colorChoice,{});
        require(materialInputLink(graph,output,MaterialPin::BaseColor)->fromPin==MaterialPin::RGB,"Image adjustment did not wire its RGB output to Base Color.");
    }
    const auto uv=addMaterialNode(graph,MaterialNodeKind::UV).id;
    const MaterialGraphEndpoint mapping{uv,MaterialPin::UV,false};
    const auto grunge=addConnectedMaterialGraphNode(graph,mapping,choiceFor(graph,mapping,MaterialNodeKind::Grunge),{});
    require(materialInputLink(graph,grunge,MaterialPin::UV)->fromNode==uv&&
        choiceFor(graph,colorTarget,MaterialNodeKind::Grunge).pin==MaterialPin::RGB&&choiceFor(graph,scalarTarget,MaterialNodeKind::Grunge).pin==MaterialPin::Value,
        "Grunge suggestions failed to connect Mapping, scalar and RGB destinations.");

    Project project;MaterialDocument document(project.root);require(document.create("Adjustment history"),"Could not create adjustment history document.");
    std::vector<MaterialNodeId> selected;const auto before=serializeMaterialAsset(document.definition());
    require(document.edit("Create image tools",[&](auto& asset) {
        for(const auto kind:{MaterialNodeKind::Desaturation,MaterialNodeKind::Contrast,MaterialNodeKind::ContrastRGB,MaterialNodeKind::HueShift,MaterialNodeKind::Grunge})
            selected.push_back(addMaterialNode(asset.graph,kind).id);
        findMaterialNode(asset.graph,selected[0])->right.value={2.7f,2.7f,2.7f,2.7f};
        findMaterialNode(asset.graph,selected[3])->right.value={-450,-450,-450,-450};
        auto& layers=findMaterialNode(asset.graph,selected[4])->grunge;
        layers.seed=65535;layers.tiling=64;layers.coverage=.2f;layers.disorder=.7f;layers.detail=.9f;
        layers.baseNoisiness=.17f;layers.dirtSpecksOpacity=.28f;layers.scratchesOpacity=.39f;
        layers.stainsOpacity=.63f;layers.leaksOpacity=.74f;
        layers.sharpen=.41f;layers.largeVariationIntensity=.52f;layers.invertDirt=true;
        connectMaterialNodes(asset.graph,selected[4],MaterialPin::RGB,selected[0],MaterialPin::Left);
    }),"Could not create image-tool history fixture.");
    const auto authored=serializeMaterialAsset(document.definition());
    require(document.undo()&&serializeMaterialAsset(document.definition())==before&&document.redo()&&serializeMaterialAsset(document.definition())==authored,
        "Image tool creation did not form one reversible graph command.");
    const auto clipboard=copyMaterialGraphSelection(document.definition().graph,selected);std::vector<MaterialNodeId> pasted;
    require(document.edit("Paste image tools",[&](auto& asset){pasted=pasteMaterialGraphSelection(asset.graph,clipboard,{600,300});}),"Could not paste image adjustment and Grunge nodes.");
    const auto* copiedGrunge=findMaterialNode(document.definition().graph,pasted[4]);
    require(copiedGrunge->grunge.seed==65535&&copiedGrunge->grunge.tiling==64&&
        copiedGrunge->grunge.coverage==.2f&&copiedGrunge->grunge.disorder==.7f&&copiedGrunge->grunge.detail==.9f&&
        copiedGrunge->grunge.baseNoisiness==.17f&&copiedGrunge->grunge.dirtSpecksOpacity==.28f&&copiedGrunge->grunge.scratchesOpacity==.39f&&
        copiedGrunge->grunge.stainsOpacity==.63f&&copiedGrunge->grunge.leaksOpacity==.74f&&
        copiedGrunge->grunge.sharpen==.41f&&copiedGrunge->grunge.largeVariationIntensity==.52f&&copiedGrunge->grunge.invertDirt&&
        findMaterialNode(document.definition().graph,pasted[3])->right.value[0]==-450&&
        materialInputLink(document.definition().graph,pasted[0],MaterialPin::Left)->fromNode==pasted[4],
        "Image tool clipboard lost settings or its internal links.");
    require(document.undo()&&serializeMaterialAsset(document.definition())==authored,"Image tool paste was split across undo commands.");
    require(document.save(),"Could not save image tool document.");MaterialDocument reopened(project.root);
    require(reopened.load(document.assetId())&&serializeMaterialAsset(reopened.definition())==authored,"Image adjustments or Grunge changed during save/reopen.");
}
void requestedImageAndChannelActions() {
    Project project;MaterialDocument document(project.root);require(document.create("Image nodes history"),"Could not create image-node document.");
    std::vector<MaterialNodeId> added;
    require(document.edit("Add requested image nodes",[&](auto& asset) {
        for(const auto kind:{MaterialNodeKind::Levels,MaterialNodeKind::Invert,MaterialNodeKind::Blend,MaterialNodeKind::Grayscale,MaterialNodeKind::ToColor,MaterialNodeKind::Noise,
            MaterialNodeKind::BrightnessContrast,MaterialNodeKind::Sharpen,MaterialNodeKind::HeightToNormal,MaterialNodeKind::RGBAMerge,MaterialNodeKind::RGBASplit,MaterialNodeKind::AlphaSplit,MaterialNodeKind::AlphaMerge})
            added.push_back(addMaterialNode(asset.graph,kind).id);
        findMaterialNode(asset.graph,added[0])->image.gamma=1.6f;
        findMaterialNode(asset.graph,added[2])->image.blendMode=MaterialImageBlend::SoftLight;
        findMaterialNode(asset.graph,added[5])->image.seed=421;
        findMaterialNode(asset.graph,added[5])->image.noisePattern=MaterialNoisePattern::Spots;
        findMaterialNode(asset.graph,added[8])->image.normalFlipY=true;
        findMaterialNode(asset.graph,added[9])->color={.2f,2.f,-.4f,0.f};
        connectMaterialNodes(asset.graph,added[9],MaterialPin::RGBA,added[10],MaterialPin::Left);
        connectMaterialNodes(asset.graph,added[9],MaterialPin::RGBA,added[11],MaterialPin::Left);
        connectMaterialNodes(asset.graph,added[11],MaterialPin::RGB,added[12],MaterialPin::Left);
        connectMaterialNodes(asset.graph,added[11],MaterialPin::A,added[12],MaterialPin::Alpha);
    }),"Could not prepare requested image nodes.");
    const auto authored=serializeMaterialAsset(document.definition());
    require(document.save(),"Could not save image-node document.");
    const auto text=copyMaterialGraphSelection(document.definition().graph,added);std::vector<MaterialNodeId> pasted;
    require(document.edit("Paste image nodes",[&](auto& asset){pasted=pasteMaterialGraphSelection(asset.graph,text,{400,300});}),"Image-node paste failed.");
    require(pasted.size()==added.size(),"Image-node clipboard omitted a requested kind.");
    const auto& graph=document.definition().graph;
    require(findMaterialNode(graph,pasted[0])->image.gamma==1.6f&&findMaterialNode(graph,pasted[2])->image.blendMode==MaterialImageBlend::SoftLight&&
        findMaterialNode(graph,pasted[5])->image.seed==421&&findMaterialNode(graph,pasted[5])->image.noisePattern==MaterialNoisePattern::Spots&&
        findMaterialNode(graph,pasted[8])->image.normalFlipY&&findMaterialNode(graph,pasted[9])->color==Vec4{.2f,2.f,-.4f,0.f},"Image-node clipboard changed settings or hidden transparent RGB.");
    require(materialInputLink(graph,pasted[10],MaterialPin::Left)->fromNode==pasted[9]&&
        evaluateMaterialUniform(graph,pasted[12],MaterialPin::RGBA).value==Vec4{.2f,2.f,-.4f,0.f},"Image-node clipboard lost RGBA channel identity or straight data.");
    require(document.undo()&&serializeMaterialAsset(document.definition())==authored,"Image-node paste was not one undo command.");
    require(document.redo()&&document.undo(),"Image-node paste redo/undo failed.");
    MaterialDocument reopened(project.root);require(reopened.load(document.assetId())&&serializeMaterialAsset(reopened.definition())==authored,"Requested nodes changed during save/reopen.");
    auto candidate=document.definition().graph;
    const auto normal=materialGraphNodeChoices(candidate,{materialOutputNode(candidate).id,MaterialPin::Normal,true});
    require(std::any_of(normal.begin(),normal.end(),[](const auto& choice){return choice.kind==MaterialNodeKind::HeightToNormal&&choice.pin==MaterialPin::Normal;}),"Normal input cannot create Height to Normal.");
    const auto split=added[10];const auto sources=materialGraphNodeChoices(candidate,{split,MaterialPin::Left,true});
    require(std::any_of(sources.begin(),sources.end(),[](const auto& choice){return choice.kind==MaterialNodeKind::RGBAMerge&&choice.pin==MaterialPin::RGBA;}),"RGBA input cannot create RGBA Merge.");
    const auto outgoing=materialGraphNodeChoices(candidate,{added[9],MaterialPin::RGBA,false});
    require(std::any_of(outgoing.begin(),outgoing.end(),[](const auto& choice){return choice.kind==MaterialNodeKind::AlphaSplit&&choice.pin==MaterialPin::Left;})&&
        std::none_of(outgoing.begin(),outgoing.end(),[](const auto& choice){return choice.kind==MaterialNodeKind::UV||choice.kind==MaterialNodeKind::Vector2;}),"RGBA pin-drag suggestions lost channel nodes or offered mapping expressions.");
}

void materialStylizationAuthoring() {
    constexpr std::array kinds{MaterialNodeKind::Pixelization,MaterialNodeKind::Dither,MaterialNodeKind::Posterization};
    Project project;MaterialDocument document(project.root);require(document.create("Material stylization"),"Could not create stylization document.");
    std::vector<MaterialNodeId> selected;
    require(document.edit("Author material stylization",[&](auto& asset) {
        auto& graph=asset.graph;
        for(const auto kind:kinds)require(std::count(materialGraphNodeKinds().begin(),materialGraphNodeKinds().end(),kind)==1,"Stylization node missing from the add catalog.");
        selected.push_back(addMaterialNode(graph,MaterialNodeKind::Color).id);
        findMaterialNode(graph,selected.front())->color={.2f,.4f,.6f,.25f};
        for(const auto kind:kinds) {
            auto& node=addMaterialNode(graph,kind);selected.push_back(node.id);
            node.left={MaterialValueType::Color4,{.1f,.3f,.5f,.7f}};
            require(materialGraphInputMixedImage(node,MaterialPin::Left),"Stylization input lost its grayscale/color capability.");
        }
        findMaterialNode(graph,selected[1])->stylization.pixelSize=9;
        auto& dither=findMaterialNode(graph,selected[2])->stylization;
        dither.pattern=DitherPattern::InterleavedGradientNoise;dither.size=3;dither.strength=.65f;dither.levels=12;
        findMaterialNode(graph,selected[3])->stylization.levels=12;
        connectMaterialGraphAuthoring(graph,selected[0],MaterialPin::RGBA,selected[1],MaterialPin::Left);
        connectMaterialGraphAuthoring(graph,selected[1],MaterialPin::Value,selected[2],MaterialPin::Left);
        connectMaterialGraphAuthoring(graph,selected[2],MaterialPin::Value,selected[3],MaterialPin::Left);
        for(std::size_t index=1;index<selected.size();++index)
            require(materialOutputType(graph,selected[index],MaterialPin::Value)==MaterialValueType::Color4,"Stylization chain discarded RGBA type.");
    }),"Could not author material stylization.");
    const auto authored=serializeMaterialAsset(document.definition());
    require(document.undo()&&document.redo()&&serializeMaterialAsset(document.definition())==authored,"Stylization authoring did not undo and redo exactly.");
    require(document.save(),"Could not save material stylization.");
    const auto copied=copyMaterialGraphSelection(document.definition().graph,selected);std::vector<MaterialNodeId> pasted;
    require(document.edit("Paste stylization",[&](auto& asset){pasted=pasteMaterialGraphSelection(asset.graph,copied,{500,200});}),"Could not paste material stylization.");
    require(pasted.size()==selected.size(),"Stylization clipboard omitted nodes.");
    const auto& pastedGraph=document.definition().graph;
    const auto& pixel=findMaterialNode(pastedGraph,pasted[1])->stylization;
    const auto& dither=findMaterialNode(pastedGraph,pasted[2])->stylization;
    const auto& poster=findMaterialNode(pastedGraph,pasted[3])->stylization;
    require(pixel.pixelSize==9&&dither.pattern==DitherPattern::InterleavedGradientNoise&&dither.size==3&&dither.strength==.65f&&dither.levels==12&&poster.levels==12,
        "Stylization clipboard lost effect controls.");
    require(materialInputLink(pastedGraph,pasted[2],MaterialPin::Left)->fromNode==pasted[1]&&
        materialInputLink(pastedGraph,pasted[3],MaterialPin::Left)->fromNode==pasted[2],"Stylization clipboard lost chain wiring.");
    require(document.undo()&&serializeMaterialAsset(document.definition())==authored,"Stylization paste did not undo as one command.");
    MaterialDocument reopened(project.root);
    require(reopened.load(document.assetId())&&serializeMaterialAsset(reopened.definition())==authored,"Stylization settings changed during save/reopen.");
    auto graph=document.definition().graph;
    const auto destination=materialOutputNode(graph).id;
    const auto outgoing=materialGraphNodeChoices(graph,{selected.front(),MaterialPin::RGBA,false});
    const auto incoming=materialGraphNodeChoices(graph,{destination,MaterialPin::BaseColor,true});
    for(const auto kind:kinds) {
        require(std::any_of(outgoing.begin(),outgoing.end(),[&](const auto& choice){return choice.kind==kind&&choice.pin==MaterialPin::Left;}),"RGBA wire search omitted a stylization target.");
        const auto choice=std::find_if(incoming.begin(),incoming.end(),[&](const auto& value){return value.kind==kind&&value.pin==MaterialPin::Value;});
        require(choice!=incoming.end(),"Color input search omitted a stylization source.");
        auto candidate=graph;const auto created=addConnectedMaterialGraphNode(candidate,{destination,MaterialPin::BaseColor,true},*choice,{200,200});
        require(materialOutputType(candidate,created,MaterialPin::Value)==MaterialValueType::Color&&
            materialInputLink(candidate,destination,MaterialPin::BaseColor)->fromNode==created,"Connected stylization creation did not retain the Color input family.");
    }
}

void atomicAuthoringAndReferences() {
    constexpr std::array kinds{MaterialNodeKind::Blur,MaterialNodeKind::GrowShrink,MaterialNodeKind::MaskAdjust,MaterialNodeKind::HistogramScan,
        MaterialNodeKind::DirectionalWarp,MaterialNodeKind::ImageTransform,MaterialNodeKind::ProceduralTexture,MaterialNodeKind::HeightOcclusion,
        MaterialNodeKind::NormalOcclusion,MaterialNodeKind::DirectionSelect,MaterialNodeKind::ImageInput,MaterialNodeKind::DecodeNormal};
    Project project;MaterialDocument document(project.root);require(document.create("Atomic image nodes"),"Could not create atomic authoring fixture.");
    std::vector<MaterialNodeId> added;
    require(document.edit("Add image atoms",[&](auto& asset){
        for(const auto kind:kinds) {
            require(std::count(materialGraphNodeKinds().begin(),materialGraphNodeKinds().end(),kind)==1,"Missing or duplicate atomic catalog entry.");
            added.push_back(addMaterialNode(asset.graph,kind).id);
        }
    }),"Could not author all atomic defaults.");
    const auto authored=serializeMaterialAsset(document.definition());
    require(document.undo()&&document.redo()&&serializeMaterialAsset(document.definition())==authored,"Atomic settings did not survive exact undo/redo.");
    const auto clipboard=copyMaterialGraphSelection(document.definition().graph,added);
    require(document.edit("Paste image atoms",[&](auto& asset){require(pasteMaterialGraphSelection(asset.graph,clipboard,{100,100}).size()==kinds.size(),"Atomic clipboard omitted a kind.");}),"Atomic clipboard paste failed.");
    require(document.undo()&&serializeMaterialAsset(document.definition())==authored,"Atomic clipboard undo changed original nodes.");
    auto graph=graphWithOutput();const auto output=materialOutputNode(graph).id;
    const auto color=addMaterialNode(graph,MaterialNodeKind::Color).id;
    for(const auto kind:{MaterialNodeKind::Blur,MaterialNodeKind::HistogramScan,MaterialNodeKind::DirectionalWarp,MaterialNodeKind::ImageTransform}) {
        const auto id=addMaterialNode(graph,kind).id;
        require(materialGraphInputMixedImage(*findMaterialNode(graph,id),MaterialPin::Left),"New adaptable atom lost its mixed input capability.");
        const auto before=graph.nodes.size();connectMaterialGraphAuthoring(graph,color,MaterialPin::RGBA,id,MaterialPin::Left);
        require(graph.nodes.size()==before&&materialOutputType(graph,id,MaterialPin::Value)==MaterialValueType::Color4,"Atomic image connection inserted a node or discarded RGBA type.");
    }
    const auto normalChoices=materialGraphNodeChoices(graph,{output,MaterialPin::Normal,true});
    require(std::any_of(normalChoices.begin(),normalChoices.end(),[](const auto& c){return c.kind==MaterialNodeKind::DecodeNormal&&c.pin==MaterialPin::Normal;}),"Normal socket search omitted the processed normal decoder.");
    const auto input=addMaterialNode(graph,MaterialNodeKind::ImageInput).id;
    const auto overrideId=canonicalAssetId("Content/textures/override.png");
    auto& source=*findMaterialNode(graph,input);source.texture.image.id=overrideId;source.atomic.enabled=false;
    connectMaterialGraphAuthoring(graph,input,MaterialPin::RGB,output,MaterialPin::BaseColor);
    ModelMaterial material;material.graph=std::make_shared<MaterialGraph>(graph);
    std::vector<std::string> authoredReferences,activeReferences;
    visitMaterialTextures(material,false,[&](const auto& id,auto){authoredReferences.push_back(id);});
    visitMaterialTextures(material,true,[&](const auto& id,auto){activeReferences.push_back(id);});
    require(authoredReferences==std::vector<std::string>{overrideId}&&activeReferences.empty(),"Disabled Image Input lost authored dependency or requested unused residency.");
}
void imageRemappingAuthoring() {
    Project project;MaterialDocument document(project.root);require(document.create("Painterly color mapping"),"Could not create image-remapping document.");
    std::vector<MaterialNodeId> selected;
    require(document.edit("Author image remapping",[&](auto& asset) {
        auto& graph=asset.graph;selected.push_back(addMaterialNode(graph,MaterialNodeKind::Color).id);
        for(const auto kind:{MaterialNodeKind::Kuwahara,MaterialNodeKind::GradientMap,MaterialNodeKind::ColorRangeMask,MaterialNodeKind::DetailBalance}) {
            require(std::count(materialGraphNodeKinds().begin(),materialGraphNodeKinds().end(),kind)==1,"Image remapping node missing from add catalog.");
            auto& node=addMaterialNode(graph,kind);selected.push_back(node.id);
            require(materialGraphInputMixedImage(node,MaterialPin::Left),"Image remapping input omitted its grayscale/color capability.");
        }
        findMaterialNode(graph,selected[1])->kuwahara={7,.35f};
        auto& gradient=findMaterialNode(graph,selected[2])->gradientMap;
        gradient.stops={{11,.1f,{4,.2f,.1f}},{12,.5f,{.3f,2,.2f}},{13,.9f,{.1f,.2f,8}}};gradient.nextStopId=14;gradient.interpolation=MaterialGradientInterpolation::Smooth;
        findMaterialNode(graph,selected[3])->colorRangeMask={{.2f,.4f,.6f},.22f,.13f,true};
        findMaterialNode(graph,selected[4])->detailBalance={6,.4f};
        connectMaterialGraphAuthoring(graph,selected[0],MaterialPin::RGBA,selected[1],MaterialPin::Left);
        connectMaterialGraphAuthoring(graph,selected[1],MaterialPin::Value,selected[2],MaterialPin::Left);
        connectMaterialGraphAuthoring(graph,selected[2],MaterialPin::RGBA,selected[3],MaterialPin::Left);
        connectMaterialGraphAuthoring(graph,selected[3],MaterialPin::Value,selected[4],MaterialPin::Left);
    }),"Could not author image-remapping nodes.");
    const auto authored=serializeMaterialAsset(document.definition());
    require(document.undo()&&document.redo()&&serializeMaterialAsset(document.definition())==authored,"Image remapping did not undo and redo as one edit.");
    require(document.save(),"Could not save image-remapping nodes.");
    const auto clipboard=copyMaterialGraphSelection(document.definition().graph,selected);std::vector<MaterialNodeId> pasted;
    require(document.edit("Paste image remapping",[&](auto& asset){pasted=pasteMaterialGraphSelection(asset.graph,clipboard,{600,100});}),"Could not paste image-remapping nodes.");
    const auto& graph=document.definition().graph;
    require(pasted.size()==selected.size()&&findMaterialNode(graph,pasted[1])->kuwahara==findMaterialNode(graph,selected[1])->kuwahara&&
        findMaterialNode(graph,pasted[2])->gradientMap==findMaterialNode(graph,selected[2])->gradientMap&&
        findMaterialNode(graph,pasted[3])->colorRangeMask==findMaterialNode(graph,selected[3])->colorRangeMask&&
        findMaterialNode(graph,pasted[4])->detailBalance==findMaterialNode(graph,selected[4])->detailBalance,
        "Image-remapping clipboard lost filtering, HDR ramp colors/identities or mask controls.");
    require(materialInputLink(graph,pasted[2],MaterialPin::Left)->fromNode==pasted[1]&&materialInputLink(graph,pasted[3],MaterialPin::Left)->fromNode==pasted[2],"Image-remapping clipboard lost its typed chain.");
    require(document.undo()&&serializeMaterialAsset(document.definition())==authored,"Image-remapping paste did not undo as one command.");
    MaterialDocument reopened(project.root);require(reopened.load(document.assetId())&&serializeMaterialAsset(reopened.definition())==authored,"Image-remapping save/reopen changed authored settings.");
    const auto& restored=document.definition().graph;const auto output=materialOutputNode(restored).id;
    const auto targets=materialGraphNodeChoices(restored,{selected[0],MaterialPin::RGBA,false});
    for(const auto kind:{MaterialNodeKind::Kuwahara,MaterialNodeKind::GradientMap,MaterialNodeKind::ColorRangeMask,MaterialNodeKind::DetailBalance})
        require(std::any_of(targets.begin(),targets.end(),[&](const auto& choice){return choice.kind==kind&&choice.pin==MaterialPin::Left;}),"RGBA pin search omitted an image-remapping target.");
    const auto sources=materialGraphNodeChoices(restored,{output,MaterialPin::BaseColor,true});
    const auto gradient=std::find_if(sources.begin(),sources.end(),[](const auto& choice){return choice.kind==MaterialNodeKind::GradientMap&&choice.pin==MaterialPin::RGB;});
    require(gradient!=sources.end(),"Color input search omitted Gradient Map RGB.");
    auto candidate=restored;const auto connected=addConnectedMaterialGraphNode(candidate,{output,MaterialPin::BaseColor,true},*gradient,{200,100});
    require(materialInputLink(candidate,output,MaterialPin::BaseColor)->fromNode==connected,"Creating Gradient Map from a Color input lost its link.");
    const auto detail=std::find_if(sources.begin(),sources.end(),[](const auto& choice){return choice.kind==MaterialNodeKind::DetailBalance&&choice.pin==MaterialPin::Value;});
    require(detail!=sources.end(),"Color input search omitted Detail Balance.");candidate=restored;
    const auto detailId=addConnectedMaterialGraphNode(candidate,{output,MaterialPin::BaseColor,true},*detail,{200,100});
    require(materialOutputType(candidate,detailId,MaterialPin::Value)==MaterialValueType::Color&&materialInputLink(candidate,output,MaterialPin::BaseColor)->fromNode==detailId,
        "Creating Detail Balance from a Color input lost its family or link.");
}
void cachedRuntimeUvRequirements() {
    auto graph=graphWithOutput();const auto out=materialOutputNode(graph).id;
    const auto source=addMaterialNode(graph,MaterialNodeKind::Texture2D).id;
    findMaterialNode(graph,source)->texture.image.id=canonicalAssetId("Content/textures/private.png");
    const auto resize=addMaterialNode(graph,MaterialNodeKind::Resize).id,transform=addMaterialNode(graph,MaterialNodeKind::ImageTransform).id;
    connectMaterialNodes(graph,source,MaterialPin::RGB,resize,MaterialPin::Left);
    connectMaterialNodes(graph,resize,MaterialPin::Value,transform,MaterialPin::Left);
    connectMaterialNodes(graph,transform,MaterialPin::Value,out,MaterialPin::BaseColor);
    findMaterialNode(graph,transform)->uv.mode=MaterialUvMode::WorldTriplanar;
    require(materialGraphRequiredUvs(graph)==0,"World triplanar cached image inherited a private source's mesh UV0 requirement.");
    findMaterialNode(graph,transform)->uv.mode=MaterialUvMode::LocalTriplanar;
    require(materialGraphRequiredUvs(graph)==0,"Local triplanar cached image required mesh UVs.");
    findMaterialNode(graph,transform)->uv.mode=MaterialUvMode::UV1;
    require(materialGraphRequiredUvs(graph)==2,"Cached Image Transform did not require its runtime UV1 chart.");
    connectMaterialNodes(graph,source,MaterialPin::R,out,MaterialPin::Roughness);
    require(materialGraphRequiredUvs(graph)==3,"Cutting a cached branch hid a source still sampled directly through UV0.");
    removeMaterialLink(graph,materialInputLink(graph,out,MaterialPin::Roughness)->id);
    findMaterialNode(graph,transform)->uv.mode=MaterialUvMode::UV0;
    require(materialGraphRequiredUvs(graph)==1,"Cached runtime UV0 was omitted from mesh compatibility.");
    const auto procedural=addMaterialNode(graph,MaterialNodeKind::ProceduralTexture).id,gray=addMaterialNode(graph,MaterialNodeKind::ToColor).id;
    connectMaterialNodes(graph,procedural,MaterialPin::Value,gray,MaterialPin::Left);connectMaterialNodes(graph,gray,MaterialPin::RGB,out,MaterialPin::BaseColor);
    require(materialGraphRequiredUvs(graph)==1,"A procedural-only cached sampler did not require runtime UV0.");
    const auto uv=addMaterialNode(graph,MaterialNodeKind::UV).id;findMaterialNode(graph,uv)->uv.mode=MaterialUvMode::UV1;
    const auto noise=addMaterialNode(graph,MaterialNodeKind::Noise).id;
    connectMaterialNodes(graph,uv,MaterialPin::UV,noise,MaterialPin::UV);connectMaterialNodes(graph,noise,MaterialPin::RGB,out,MaterialPin::BaseColor);
    require(materialGraphRequiredUvs(graph)==2,"Direct analytic Noise lost its mesh UV1 requirement.");
    const auto radial=addMaterialNode(graph,MaterialNodeKind::RadialGradient).id;connectMaterialNodes(graph,uv,MaterialPin::UV,radial,MaterialPin::UV);
    connectMaterialNodes(graph,radial,MaterialPin::Value,gray,MaterialPin::Left);connectMaterialNodes(graph,gray,MaterialPin::RGB,out,MaterialPin::BaseColor);
    require(materialGraphRequiredUvs(graph)==2,"Direct analytic Radial Gradient lost its mesh UV1 requirement.");
    // Analytic sources in a static image consume an internal UV0 lattice. Only
    // the outer image mapping determines whether the eventual mesh needs UVs.
    removeMaterialLink(graph,materialInputLink(graph,noise,MaterialPin::UV)->id);
    connectMaterialNodes(graph,noise,MaterialPin::RGB,resize,MaterialPin::Left);
    findMaterialNode(graph,transform)->uv.mode=MaterialUvMode::WorldTriplanar;connectMaterialNodes(graph,transform,MaterialPin::Value,out,MaterialPin::BaseColor);
    require(materialGraphRequiredUvs(graph)==0,"A cached analytic source leaked its private UV0 lattice into triplanar compatibility.");
    const auto image=addMaterialNode(graph,MaterialNodeKind::ImageInput).id;
    connectMaterialNodes(graph,uv,MaterialPin::UV,source,MaterialPin::UV);
    // The bitmap now feeds this separate active Image Input; the cached
    // branch above consumes Noise instead.
    connectMaterialNodes(graph,source,MaterialPin::RGBA,image,MaterialPin::Left);
    connectMaterialNodes(graph,image,MaterialPin::RGB,out,MaterialPin::BaseColor);
    require(materialGraphRequiredUvs(graph)==2,"Image Input with no override ignored its active Base mapping.");
    findMaterialNode(graph,image)->texture.image.id=canonicalAssetId("Content/textures/override.png");
    require(materialGraphRequiredUvs(graph)==1,"Assigned Image Input failed to prune Base UV1 or omitted override UV0.");
    findMaterialNode(graph,image)->atomic.enabled=false;
    require(materialGraphRequiredUvs(graph)==0,"Disabled Image Input retained an unused override/Base UV requirement.");
    // An unassigned bitmap uses the white fallback and does not sample its
    // selected chart. Assigning an image restores that runtime requirement.
    const auto empty=addMaterialNode(graph,MaterialNodeKind::Texture2D).id;
    connectMaterialNodes(graph,uv,MaterialPin::UV,empty,MaterialPin::UV);connectMaterialNodes(graph,empty,MaterialPin::RGB,out,MaterialPin::BaseColor);
    require(materialGraphRequiredUvs(graph)==0,"Unassigned Texture2D retained an unused mesh UV requirement.");
    findMaterialNode(graph,empty)->texture.image.id=canonicalAssetId("Content/textures/assigned.png");
    require(materialGraphRequiredUvs(graph)==2,"Assigned Texture2D omitted its selected UV1 requirement.");
    findMaterialNode(graph,empty)->texture.image={};
    require(materialGraphRequiredUvs(graph)==0,"Clearing Texture2D retained its previous image's mesh UV requirement.");
}

}
int main() {
    try {copyPastePreservesSubgraph();externalColorInputsRetainTypes();vectorClipboardAndConnectedCreation();malformedAndBoundedPasteIsAtomic();compatibleCreationAndReplacement();documentCommandsAndDeletedClipboard();rgbScalarConnectionHistory();directChannelAuthoringAndResize();directDesaturationHeightRoute();mixedImageInputCapabilities();animatedNodeHistory();adjustmentAndGrungeActions();requestedImageAndChannelActions();materialStylizationAuthoring();atomicAuthoringAndReferences();imageRemappingAuthoring();cachedRuntimeUvRequirements();std::cout<<"Material graph actions tests passed.\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
