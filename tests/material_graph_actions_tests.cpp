#include "editor/MaterialGraphActions.h"
#include "editor/MaterialDocument.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <chrono>
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
    auto& sample=findMaterialNode(source,texture)->texture;sample.image.id="assets/textures/paint.png";
    sample.sampler={ModelWrap::Clamp,ModelWrap::Mirror,ModelFilter::NearestMipmapNearest,ModelFilter::Nearest};
    const auto normal=addMaterialNode(source,MaterialNodeKind::Texture2D,{40,340}).id;
    auto& normalSample=findMaterialNode(source,normal)->texture;normalSample.image.id="assets/textures/normal.png";
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
    require(textureChoices.size()==3&&textureChoices.front().kind==MaterialNodeKind::Texture2D&&textureChoices.back().kind==MaterialNodeKind::Panner,"UV output offered incompatible node kinds.");
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
    require(normalChoices.size()==1&&normalChoices.front().kind==MaterialNodeKind::Texture2D&&normalChoices.front().textureRole==MaterialTextureRole::Normal&&normalChoices.front().pin==MaterialPin::Normal,"Normal input offered non-normal expressions.");
    const auto normalTexture=addConnectedMaterialGraphNode(graph,normal,normalChoices.front(),{});
    require(materialGraphNodeChoices(graph,{normalTexture,MaterialPin::Normal,false}).empty(),"Normal output incorrectly offered arithmetic nodes.");
    materialOutputNode(graph).output.blend=MaterialBlend::Mask;
    const auto opacity=choiceFor(graph,{output,MaterialPin::OpacityMask,true},MaterialNodeKind::Texture2D);
    require(opacity.pin==MaterialPin::A&&opacity.textureRole==MaterialTextureRole::Color,"Opacity suggestion did not use a texture alpha channel.");
    const auto outgoing=materialGraphNodeChoices(graph,{texture,MaterialPin::R,false});
    require(outgoing.size()==5&&outgoing.front().kind==MaterialNodeKind::Multiply&&outgoing.front().pin==MaterialPin::Left&&outgoing.back().kind==MaterialNodeKind::Power,"Numeric output did not offer compatible arithmetic inputs.");
    rejectedWithoutChange(graph,[&]{addConnectedMaterialGraphNode(graph,{9999,MaterialPin::Value,false},outgoing.front(),{});},"Stale endpoint changed the graph.");
    rejectedWithoutChange(graph,[&]{addConnectedMaterialGraphNode(graph,roughness,{MaterialNodeKind::UV,MaterialTextureRole::Color,MaterialPin::UV},{});},"Incompatible drag-created node changed the graph.");
    materialOutputNode(graph).output.shading=MaterialShading::Unlit;
    rejectedWithoutChange(graph,[&]{addConnectedMaterialGraphNode(graph,normal,normalChoices.front(),{});},"Dormant shader input accepted node creation.");
}
void documentCommandsAndDeletedClipboard() {
    Project project;MaterialDocument document(project.root);require(document.create("Clipboard history"),"Could not create the clipboard document.");
    std::vector<MaterialNodeId> selected;const std::string removed="assets/textures/deleted-clipboard.png";
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
}
int main() {
    try {copyPastePreservesSubgraph();externalColorInputsRetainTypes();vectorClipboardAndConnectedCreation();malformedAndBoundedPasteIsAtomic();compatibleCreationAndReplacement();documentCommandsAndDeletedClipboard();rgbScalarConnectionHistory();animatedNodeHistory();std::cout<<"Material graph actions tests passed.\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
