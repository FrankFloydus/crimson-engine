#include "assets/DecalAsset.h"
#include "assets/AssetReferenceManager.h"
#include "assets/AtomicFile.h"
#include "assets/BrowserCatalog.h"
#include "assets/ContentStorage.h"
#include "assets/MaterialAsset.h"
#include "editor/DecalDocument.h"
#include "scene/SceneDocument.h"
#include "scene/SceneOutlinerRows.h"
#include "scene/SceneExtraction.h"
#include "resources/AssetId.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace crimson;
namespace fs=std::filesystem;
using Json=nlohmann::json;
void require(bool condition,const std::string& message){if(!condition)throw std::runtime_error(message);}
template<class F>void rejects(F&& action,const char* message){try{action();}catch(const std::exception&){return;}throw std::runtime_error(message);}
struct Project {
    fs::path root=fs::temp_directory_path()/("crimson-decal-data-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~Project(){std::error_code error;const auto owner=fs::weakly_canonical(fs::temp_directory_path(),error);if(error)return;const auto target=fs::weakly_canonical(root,error);
        if(!error&&target.parent_path()==owner&&target.filename().string().starts_with("crimson-decal-data-"))fs::remove_all(target,error);}
};
std::string read(const fs::path& file){std::ifstream stream(file,std::ios::binary);require(bool(stream),"Fixture source file is unavailable.");return {std::istreambuf_iterator<char>(stream),{}};}
Json legacyDecal(const std::string& id,const std::array<std::string,4>& images) {
    return {{"version",1},{"type","crimson-decal"},{"id",id},{"name","Migrated stain"},
        {"color",{{"enabled",true},{"factor",{.2f,.3f,.4f}},{"strength",.7f},{"texture",images[0]}}},
        {"coverage",{{"texture",images[1]},{"channel","a"}}},
        {"normal",{{"enabled",true},{"strength",.4f},{"texture",images[2]}}},
        {"roughness",{{"enabled",true},{"value",.15f},{"strength",.8f},{"texture",images[3]},{"channel","g"}}}};
}
void assetsRoundTripAndPolicies() {
    Project p;const auto asset=createDecalAsset(p.root,"Corner dirt");const auto bytes=read(decalAssetPath(p.root,asset.id));
    const auto material=loadMaterialAsset(p.root,asset.material.id);const auto& output=materialOutputNode(material.graph).output;
    require(material.shading==MaterialShading::Decal&&output.decalColorEnabled&&!output.decalNormalEnabled&&!output.decalRoughnessEnabled,"A new decal must reference a visible Decal graph material.");
    require(decalMaterialCompatible(material)&&!modelMaterialCompatible(material)&&!particleMaterialCompatible(material),"Decal material entered an unsupported mesh or particle consumer.");
    require(Json::parse(bytes).at("version")==2&&loadDecalAsset(p.root,asset.id)==asset,"Decal v2 material reference did not round trip.");
    require(decalMaterialReferences(asset)==std::vector<std::string>{material.id},"Decal dependency scan omitted its material.");
    auto bad=asset;bad.material.id="assets/textures/../foreign.png";rejects([&]{saveDecalAsset(p.root,bad);},"Escaping material reference reached disk.");
    require(read(decalAssetPath(p.root,asset.id))==bytes,"Invalid save changed the valid source file.");
    auto json=Json::parse(bytes);json["version"]=3;rejects([&]{deserializeDecalAsset(json.dump());},"Future decal version was accepted.");
    const auto copied=saveDecalAssetAs(p.root,asset);require(copied.id!=asset.id&&copied.name!=asset.name&&copied.material==asset.material&&read(decalAssetPath(p.root,asset.id))==bytes,"Save As mutated its source or duplicated a shared material.");
    BrowserCatalog browser(p.root);const std::array<AssetEntry,2> entries{{{asset.id,asset.name,AssetKind::Decal},{copied.id,copied.name,AssetKind::Decal}}};browser.synchronize(entries);
    const auto folder=browser.createFolder(BrowserCatalog::ContentFolder,"Decal proof");const std::array<std::string,1> selection{asset.id};browser.move(selection,folder);browser.rename(asset.id,"Moved surface");
    require(loadDecalAsset(p.root,asset.id)==asset&&decalAssetPath(p.root,asset.id).parent_path()==browser.folderPath(folder),"Decal move/rename lost its managed identity or data.");
    const auto filtered=browser.items(folder,"",BrowserAssetFilter::Decals);require(filtered.size()==1&&filtered.front().id==asset.id,"Decal browser filtering omitted the moved asset.");
    AssetReferenceManager references(p.root);const auto wrong=createMaterialAsset(p.root,"Wrong kind");const std::array<AssetEntry,1> removed{{{asset.id,asset.name,AssetKind::Decal}}};
    rejects([&]{references.replace(removed,{{asset.id,wrong.id}});},"Decal deletion accepted a material as replacement.");
    references.replace(removed,{{asset.id,copied.id}});require(references.resolveDecal(asset.id)==copied.id,"Decal replacement lost its selected asset.");
    references.save();AssetReferenceManager reopened(p.root);require(reopened.resolveDecal(asset.id)==copied.id,"Decal replacement policy did not survive reload.");
}
void replacementCoverageIsSafe() {
    Project p;initializeContentStorage(p.root);const auto id=newAssetId();const std::array<std::string,4> images{newAssetId(),newAssetId(),newAssetId(),newAssetId()};
    const auto source=legacyDecal(id,images).dump(2)+"\n";const auto file=p.root/"Content/legacy.crimson-decal";writeFileAtomically(file,source);registerAssetPath(p.root,id,file);
    const auto before=storedAssetPaths(p.root).size();const auto loaded=loadDecalAsset(p.root,id);const auto again=loadDecalAsset(p.root,id);
    require(loaded.material==again.material&&loaded.stagedMaterial&&read(file)==source&&storedAssetPaths(p.root).size()==before,"v1 loading wrote files or changed deterministic material identity.");
    const auto& m=*loaded.stagedMaterial;const auto& o=materialOutputNode(m.graph).output;
    require(m.shading==MaterialShading::Decal&&o.decalColorEnabled&&o.decalNormalEnabled&&o.decalRoughnessEnabled&&o.decalColorStrength==.7f&&o.decalNormalStrength==.4f&&o.decalRoughnessStrength==.8f&&o.roughness==.15f,"v1 conversion lost channel coverage or factors.");
    require(materialTextureReferences(m)==std::vector<std::string>(images.begin(),images.end()),"v1 conversion lost authored color, alpha, normal or roughness maps.");
    const auto& graph=m.graph;const auto* opacity=materialInputLink(graph,materialOutputNode(graph).id,MaterialPin::Opacity);const auto* roughness=materialInputLink(graph,materialOutputNode(graph).id,MaterialPin::Roughness);
    require(opacity&&opacity->fromPin==MaterialPin::A&&roughness&&roughness->fromPin==MaterialPin::G,"v1 selected coverage/roughness channels changed.");
    saveDecalAsset(p.root,loaded);require(Json::parse(read(file)).at("version")==2&&loadMaterialAsset(p.root,loaded.material.id).graph.nodes.size()==m.graph.nodes.size(),"Explicit Save did not materialize v2 and the equivalent graph together.");
    auto authored=loadMaterialAsset(p.root,loaded.material.id);materialOutputNode(authored.graph).output.baseColor={.8f,.7f,.6f};canonicalizeMaterialAsset(authored);saveMaterialAsset(p.root,authored);
    writeFileAtomically(file,source);const auto durable=read(materialAssetPath(p.root,authored.id));saveDecalAsset(p.root,loadDecalAsset(p.root,id));require(read(materialAssetPath(p.root,authored.id))==durable,"Repeated migration overwrote an independently authored companion material.");
    auto alpha=legacyDecal(newAssetId(),images);alpha["coverage"]["texture"]="";alpha["normal"]["texture"]="";const auto colorAlpha=deserializeDecalAsset(alpha.dump());const auto& converted=colorAlpha.stagedMaterial->graph;
    const auto* link=materialInputLink(converted,materialOutputNode(converted).id,MaterialPin::Opacity);require(link&&link->fromPin==MaterialPin::A&&!materialOutputNode(converted).output.decalNormalEnabled,"Implicit color alpha or missing normal conversion changed influence.");
}
void documentHistoryAndSaveAs() {
    Project p;DecalDocument document(p.root);require(document.create("Wet patch"),document.status());
    const auto before=document.definition();const auto revision=document.revision(),preview=document.previewRevision();const auto committed=document.committed();const auto material=newAssetId();
    require(document.begin("Assign material")&&document.update([&](auto& asset){asset.material.id=material;}),document.status());
    require(document.committed()==committed&&document.revision()==revision&&document.previewRevision()>preview,"A preview gesture published an uncommitted generation.");
    require(!document.update([](auto& asset){asset.material.id="../bad";}),"Invalid reference preview was accepted.");document.cancel();
    require(document.definition()==before&&!document.dirty()&&!document.canUndo()&&document.revision()==revision,"Cancel left preview data or history behind.");
    require(document.edit("Assign material",[&](auto& asset){asset.material.id=material;}),document.status());const auto refs=document.historicalMaterialReferences();
    require(document.canUndo()&&std::find(refs.begin(),refs.end(),before.material.id)!=refs.end(),"Decal history did not retain the previous material reference.");
    const auto oldId=document.assetId();const auto source=read(decalAssetPath(p.root,oldId));require(document.saveAs("Independent decal"),document.status());const auto newId=document.assetId();
    require(newId!=oldId&&!document.dirty()&&read(decalAssetPath(p.root,oldId))==source,"Save As overwrote its source or retained wrong dirty state.");
    require(document.undo()&&document.assetId()==newId,"Undo restored the pre-Save-As identity.");require(document.redo()&&document.assetId()==newId,document.status());
    require(document.save(),document.status());const auto saved=document.savedDefinition();require(document.edit("Clear material",[](auto& asset){asset.material.id.clear();}),document.status());
    require(document.discard()&&document.definition()==saved&&!document.dirty(),"Discard did not restore saved decal material.");
}
void genericVisualReceiveDecals() {
    Project project;SceneDocument document(project.root);const auto box=document.createBox("Receiver"),particle=document.createParticle("","Emitter");
    require(document.scene().find(box)->receiveDecals&&document.scene().find(particle)->receiveDecals,"Visual receivers are not enabled by default.");
    auto record=*document.scene().find(box);record.receiveDecals=false;require(document.updateEntity(box,record,"Disable decals"),document.status());
    require(document.undo()&&document.scene().find(box)->receiveDecals&&document.redo()&&!document.scene().find(box)->receiveDecals,"Receive Decals did not undo/redo.");
    record=*document.scene().find(particle);record.receiveDecals=false;require(document.updateEntity(particle,record),document.status());
    require(document.save("Receiver flags"),document.status());const auto file=sceneAssetPath(project.root,document.assetId());const auto before=read(file);
    SceneDocument reopened(project.root);require(reopened.load(document.assetId())&&!reopened.dirty()&&!reopened.scene().find(box)->receiveDecals&&!reopened.scene().find(particle)->receiveDecals&&read(file)==before,"Receiver flags changed during clean load.");
    const auto extracted=extractScene(reopened.scene());require(extracted.models.size()==1&&!extracted.models.front().receiveDecals&&extracted.particles.size()==1&&!extracted.particles.front().receiveDecals,"Model/particle extraction lost the generic receiver flag.");
    auto json=Json::parse(before);for(auto& entity:json["entities"])entity.erase("receiveDecals");writeFileAtomically(file,json.dump());require(reopened.load(document.assetId())&&reopened.scene().find(box)->receiveDecals&&reopened.scene().find(particle)->receiveDecals,"Missing receiver flags did not preserve legacy default-on behavior.");
    json["entities"][0]["receiveDecals"]="false";writeFileAtomically(file,json.dump());require(!reopened.load(document.assetId()),"A nonboolean receiver flag was silently accepted.");
    EntityRecord parent;parent.id=1;parent.name="Actor";parent.actor.emplace();parent.receiveDecals=false;EntityRecord visual;visual.id=2;visual.parent=1;visual.name="Independent visual";visual.model=AssetRef<ModelAsset>{BuiltinBoxModelId};Scene scene;scene.apply({parent,visual},{});require(extractScene(scene).models.front().receiveDecals,"Receiver flags leaked from an actor parent to its visual child.");
}
void boxDecalHistoryAndPersistence() {
    Project p;SceneDocument document(p.root);const auto parent=document.createEmpty("Parent");
    auto pRecord=*document.scene().find(parent);pRecord.local=math::translation({2,3,4})*math::scaling({2,3,-4});
    require(document.updateEntity(parent,pRecord),document.status());
    const auto box=document.createBoxDecal("Box Decal",parent);
    require(box&&document.scene().find(box)->decal->projection==SceneDecalProjection::Box,"Box decal creation lost projection mode.");
    require(document.undo()&&!document.scene().contains(box)&&document.redo()&&document.scene().contains(box),"Box creation did not undo/redo.");
    const auto world=*document.scene().worldTransform(box);const std::array selected{box};
    const auto bounds=selectionBounds(document.scene(),selected,{});
    const auto expected=math::transformBounds(Aabb{{-.5f,-.5f,-.5f},{.5f,.5f,.5f},true},world);
    require(bounds.valid&&bounds.minimum==expected.minimum&&bounds.maximum==expected.maximum,"Box selection omitted its depth or parent affine.");
    const auto boxes=selectionBoxes(document.scene(),selected,{});
    require(boxes.size()==1&&boxes.front().local.minimum[2]==-.5f&&boxes.front().local.maximum[2]==.5f,"Box wire bounds retained a flat conforming footprint.");
    require(extractScene(document.scene()).decals.front().decal.projection==SceneDecalProjection::Box,"Extraction lost box mode.");
    require(document.save("Box decal scene"),document.status());const auto file=sceneAssetPath(p.root,document.assetId());const auto bytes=read(file);
    auto json=Json::parse(bytes);auto found=std::find_if(json["entities"].begin(),json["entities"].end(),[](const auto& e){return e.contains("decal");});
    require(found!=json["entities"].end()&&(*found)["decal"]["projection"]=="Box"&&!(*found)["decal"].contains("seed"),"Box writer retained irrelevant surface seed data.");
    SceneDocument reopened(p.root);require(reopened.load(document.assetId())&&!reopened.dirty()&&reopened.scene().find(box)->local==document.scene().find(box)->local&&reopened.scene().find(box)->decal==document.scene().find(box)->decal&&read(file)==bytes,"Box load changed data or dirtied scene.");
    (*found)["decal"]["projection"]="Unknown";writeFileAtomically(file,json.dump());
    require(!reopened.load(document.assetId()),"Unknown box projection was silently accepted.");
}
void sceneCapabilityHistoryAndMigration() {
    Project p;const auto asset=createDecalAsset(p.root,"Scene decal");SceneDocument document(p.root);const auto receiver=document.createBox("Receiver");
    SceneDecal value;value.asset.id=asset.id;value.seed.receiver=receiver;value.seed.modelId=document.scene().find(receiver)->model->id;value.seed.fingerprint=0x0123456789abcdefull;
    value.seed.barycentric={.2f,.3f,.5f};value.seed.localNormal={0,1,0};value.tint={.3f,.4f,.5f};value.opacity=.6f;value.priority=-2;
    const auto decal=document.createDecal(value,"Wrapped stain",math::translation({1,0,2}));require(decal!=0&&document.scene().find(decal)->decal==value,document.status());
    require(hasSceneComponent(*document.scene().find(decal),SceneComponentKind::Decal)&&!isSceneGroup(*document.scene().find(decal)),"Decal capability was presented as an empty group.");
    require(document.save("Decal scene"),document.status());const auto sceneId=document.assetId();const auto file=sceneAssetPath(p.root,sceneId);const auto bytes=read(file);
    const auto savedJson=Json::parse(bytes);const auto savedDecal=std::find_if(savedJson["entities"].begin(),savedJson["entities"].end(),[](const auto& entity){return entity.contains("decal");});
    require(savedDecal!=savedJson["entities"].end()&&!(*savedDecal)["decal"].contains("receivers")&&!(*savedDecal)["decal"]["seed"].contains("sourceKey"),"Current writer retained removed decal receiver policy.");
    require(Json::parse(bytes).at("version")==25,"New decal capability did not use schema 25.");SceneDocument reopened(p.root);require(reopened.load(sceneId)&&!reopened.dirty()&&reopened.scene().find(decal)->decal==value,reopened.status());
    document.select({receiver,decal},decal);require(document.duplicateSelection(),document.status());EntityId duplicateDecal{},duplicateReceiver{};
    for(const auto id:document.selection().ids){const auto record=document.scene().find(id);if(record->decal)duplicateDecal=id;if(record->model)duplicateReceiver=id;}
    require(duplicateDecal&&duplicateReceiver&&document.scene().find(duplicateDecal)->decal->seed.receiver==duplicateReceiver,"Duplicating receiver+decal lost internal occurrence references.");
    require(document.undo()&&!document.scene().contains(duplicateDecal)&&document.scene().find(decal)->decal==value,document.status());
    document.select({receiver},receiver);require(document.eraseSelection()&&document.scene().find(decal)->decal->seed.receiver==receiver,"Deleting a receiver destroyed its repairable decal provenance.");
    require(document.undo()&&document.scene().contains(receiver)&&document.scene().find(decal)->decal==value,document.status());
    const auto historical=document.historicalDecalReferences();require(std::find(historical.begin(),historical.end(),asset.id)!=historical.end(),"Scene history omitted decal asset references.");
    const auto models=document.historicalModelReferences();require(std::find(models.begin(),models.end(),value.seed.modelId)!=models.end(),"Scene history omitted seed geometry generations.");
    auto bad=*document.scene().find(decal);bad.decal->seed.barycentric={-1,1,1};const auto previous=*document.scene().find(decal);require(!document.updateEntity(decal,bad)&&sameEntity(*document.scene().find(decal),previous),"Malformed seed partially changed the scene.");
    auto oldPolicy=Json::parse(bytes);const auto oldEntity=std::find_if(oldPolicy["entities"].begin(),oldPolicy["entities"].end(),[](const auto& entity){return entity.contains("decal");});auto& oldDecal=(*oldEntity)["decal"];oldDecal["seed"]["sourceKey"]="previous-import-source";oldDecal["receivers"]={{"scope","SeedQuaderScene"},{"include",Json::array()},{"exclude",Json::array({receiver})}};
    const auto oldPolicyBytes=oldPolicy.dump(2)+"\n";writeFileAtomically(file,oldPolicyBytes);require(reopened.load(sceneId)&&!reopened.dirty()&&reopened.scene().find(decal)->decal==value&&read(file)==oldPolicyBytes,"Removed import scope affected generic receiver loading or rewrote scene bytes.");
    auto legacy=Json::parse(bytes);legacy["version"]=23;writeFileAtomically(file,legacy.dump(2)+"\n");const auto legacyBytes=read(file);
    require(reopened.load(sceneId)&&!reopened.dirty()&&!reopened.scene().find(decal)->decal&&read(file)==legacyBytes,"Old scene loading created a decal, dirtied data or rewrote its bytes.");
    require(reopened.save()&&Json::parse(read(file)).at("version")==25,reopened.status());
}
}
int main() {
    try{assetsRoundTripAndPolicies();replacementCoverageIsSafe();documentHistoryAndSaveAs();sceneCapabilityHistoryAndMigration();genericVisualReceiveDecals();boxDecalHistoryAndPersistence();std::cout<<"Decal asset, document and scene tests passed\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
