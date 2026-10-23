#include "assets/DecalAsset.h"
#include "assets/AssetReferenceManager.h"
#include "assets/AtomicFile.h"
#include "assets/BrowserCatalog.h"
#include "assets/ContentStorage.h"
#include "assets/MaterialAsset.h"
#include "editor/DecalDocument.h"
#include "scene/SceneDocument.h"
#include "scene/SceneOutlinerRows.h"
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
DecalAsset mapped(DecalAsset asset) {
    asset.colorEnabled=asset.normalEnabled=asset.roughnessEnabled=true;asset.color={.2f,.3f,.4f};asset.colorStrength=.7f;
    asset.colorTexture.id=newAssetId();asset.coverageTexture.id=newAssetId();asset.normalTexture.id=newAssetId();asset.roughnessTexture.id=newAssetId();
    asset.coverageChannel=DecalTextureChannel::A;asset.roughnessChannel=DecalTextureChannel::G;asset.normalStrength=.4f;asset.roughness=.15f;asset.roughnessStrength=.8f;return asset;
}
void assetsRoundTripAndPolicies() {
    Project p;const auto original=createDecalAsset(p.root,"Corner dirt");
    require(original.colorEnabled&&!original.normalEnabled&&!original.roughnessEnabled,"A new decal must have a visible color surface without enabling other channels.");
    const auto asset=mapped(original);saveDecalAsset(p.root,asset);const auto bytes=read(decalAssetPath(p.root,asset.id));
    require(loadDecalAsset(p.root,asset.id)==asset&&serializeDecalAsset(deserializeDecalAsset(bytes,asset.id))==bytes,"Decal v1 did not preserve its typed maps and channel strengths.");
    require(decalTextureReferences(asset).size()==4,"Decal dependency scan omitted an authored map.");
    auto disabled=asset;disabled.colorEnabled=disabled.normalEnabled=disabled.roughnessEnabled=false;
    require(decalTextureReferences(disabled).size()==4,"Disabling a channel lost its authored reference.");
    auto bad=asset;bad.normalStrength=std::numeric_limits<float>::quiet_NaN();rejects([&]{saveDecalAsset(p.root,bad);},"Invalid decal values reached disk.");
    require(read(decalAssetPath(p.root,asset.id))==bytes,"Invalid save changed the valid source file.");
    bad=asset;bad.coverageChannel=static_cast<DecalTextureChannel>(9);rejects([&]{validateDecalAsset(bad);},"Invalid image channel was accepted.");
    bad=asset;bad.colorTexture.id="assets/textures/../foreign.png";rejects([&]{validateDecalAsset(bad);},"Escaping image reference was accepted.");
    auto json=Json::parse(bytes);json["version"]=2;rejects([&]{deserializeDecalAsset(json.dump());},"Future decal version was accepted.");
    json=Json::parse(bytes);json["normal"]["enabled"]=1;rejects([&]{deserializeDecalAsset(json.dump());},"Nonboolean channel enable was accepted.");
    const auto copied=saveDecalAssetAs(p.root,asset);require(copied.id!=asset.id&&copied.name!=asset.name&&copied.colorTexture==asset.colorTexture&&read(decalAssetPath(p.root,asset.id))==bytes,"Save As mutated the source or its shared map identities.");
    BrowserCatalog browser(p.root);const std::array<AssetEntry,2> entries{{{asset.id,asset.name,AssetKind::Decal},{copied.id,copied.name,AssetKind::Decal}}};browser.synchronize(entries);
    const auto folder=browser.createFolder(BrowserCatalog::ContentFolder,"Decal proof");const std::array<std::string,1> selection{asset.id};browser.move(selection,folder);browser.rename(asset.id,"Moved surface");
    require(loadDecalAsset(p.root,asset.id)==asset&&decalAssetPath(p.root,asset.id).parent_path()==browser.folderPath(folder),"Decal move/rename lost its managed identity or data.");
    const auto filtered=browser.items(folder,"",BrowserAssetFilter::Decals);require(filtered.size()==1&&filtered.front().id==asset.id,"Decal browser filtering omitted the moved asset.");
    AssetReferenceManager references(p.root);const auto material=createMaterialAsset(p.root,"Wrong kind");
    const std::array<AssetEntry,1> removed{{{asset.id,asset.name,AssetKind::Decal}}};
    rejects([&]{references.replace(removed,{{asset.id,material.id}});},"Decal deletion accepted a material as replacement.");
    references.replace(removed,{{asset.id,copied.id}});require(references.resolveDecal(asset.id)==copied.id,"Decal replacement did not retain the selected surface.");
    references.save();AssetReferenceManager reopened(p.root);require(reopened.resolveDecal(asset.id)==copied.id,"Decal replacement policy did not survive reload.");
}
void replacementCoverageIsSafe() {
    auto asset=mapped(defaultDecalAsset());asset.id=newAssetId();const auto original=asset;
    require(rewriteDecalTextureReferences(asset,[&](const auto& id,bool){return id==original.normalTexture.id?std::string{}:id;}),"Explicit normal clear made no change.");
    require(!asset.normalEnabled&&asset.colorEnabled&&asset.roughnessEnabled,"Missing normal either erased lower decals or disabled unrelated channels.");
    asset=original;rewriteDecalTextureReferences(asset,[&](const auto& id,bool){return id==original.coverageTexture.id?std::string{}:id;});
    require(!asset.colorEnabled&&!asset.normalEnabled&&!asset.roughnessEnabled,"Deleted explicit coverage became constant-one influence.");
    asset=original;asset.coverageTexture.id.clear();const auto colorMask=asset.colorTexture.id;
    rewriteDecalTextureReferences(asset,[&](const auto& id,bool){return id==colorMask?std::string{}:id;});
    require(!asset.colorEnabled&&!asset.normalEnabled&&!asset.roughnessEnabled,"Deleted color-alpha coverage became constant-one influence.");
    asset=original;rejects([&]{rewriteDecalTextureReferences(asset,[&](const auto&,bool)->std::string{throw std::runtime_error("Rejected replacement");});},"Rejected replacement succeeded.");
    require(asset==original,"A failed replacement partially changed the decal asset.");
}
void documentHistoryAndSaveAs() {
    Project p;DecalDocument document(p.root);require(document.create("Wet patch"),document.status());
    const auto before=document.definition();const auto revision=document.revision(),preview=document.previewRevision();const auto committed=document.committed();
    require(document.begin("Drag roughness")&&document.update([](auto& asset){asset.roughnessEnabled=true;asset.roughness=.2f;}),document.status());
    require(document.committed()==committed&&document.revision()==revision&&document.previewRevision()>preview,"A preview gesture published an uncommitted generation.");
    require(!document.update([](auto& asset){asset.roughness=-1;}),"Invalid preview was accepted.");document.cancel();
    require(document.definition()==before&&!document.dirty()&&!document.canUndo()&&document.revision()==revision,"Cancel left preview data or history behind.");
    const auto image=newAssetId();require(document.edit("Add color",[&](auto& asset){asset.colorEnabled=true;asset.colorTexture.id=image;}),document.status());
    require(document.canUndo()&&document.historicalTextureReferences()==std::vector<std::string>{image},"Decal history did not retain shared textures.");
    const auto oldId=document.assetId();const auto source=read(decalAssetPath(p.root,oldId));require(document.saveAs("Independent decal"),document.status());const auto newId=document.assetId();
    require(newId!=oldId&&!document.dirty()&&read(decalAssetPath(p.root,oldId))==source,"Save As overwrote the source or retained the wrong dirty state.");
    require(document.undo()&&document.assetId()==newId,"Undo restored the pre-Save-As asset identity.");require(document.redo()&&document.assetId()==newId,document.status());
    require(document.save(),document.status());const auto saved=document.savedDefinition();require(document.edit("Tint",[](auto& asset){asset.color={.1f,.2f,.3f};}),document.status());
    require(document.discard()&&document.definition()==saved&&!document.dirty(),"Discard did not restore the saved decal generation.");
}
void sceneCapabilityHistoryAndMigration() {
    Project p;const auto asset=createDecalAsset(p.root,"Scene decal");SceneDocument document(p.root);const auto receiver=document.createBox("Receiver");
    SceneDecal value;value.asset.id=asset.id;value.seed.receiver=receiver;value.seed.modelId=document.scene().find(receiver)->model->id;value.seed.sourceKey="mesh:101:201:0";value.seed.fingerprint=0x0123456789abcdefull;
    value.seed.barycentric={.2f,.3f,.5f};value.seed.localNormal={0,1,0};value.tint={.3f,.4f,.5f};value.opacity=.6f;value.priority=-2;value.receivers.included={receiver};
    const auto decal=document.createDecal(value,"Wrapped stain",math::translation({1,0,2}));require(decal!=0&&document.scene().find(decal)->decal==value,document.status());
    require(hasSceneComponent(*document.scene().find(decal),SceneComponentKind::Decal)&&!isSceneGroup(*document.scene().find(decal)),"Decal capability was presented as an empty group.");
    require(document.save("Decal scene"),document.status());const auto sceneId=document.assetId();const auto file=sceneAssetPath(p.root,sceneId);const auto bytes=read(file);
    require(Json::parse(bytes).at("version")==24,"New decal capability did not use schema 24.");SceneDocument reopened(p.root);require(reopened.load(sceneId)&&!reopened.dirty()&&reopened.scene().find(decal)->decal==value,reopened.status());
    document.select({receiver,decal},decal);require(document.duplicateSelection(),document.status());EntityId duplicateDecal{},duplicateReceiver{};
    for(const auto id:document.selection().ids){const auto record=document.scene().find(id);if(record->decal)duplicateDecal=id;if(record->model)duplicateReceiver=id;}
    require(duplicateDecal&&duplicateReceiver&&document.scene().find(duplicateDecal)->decal->seed.receiver==duplicateReceiver&&document.scene().find(duplicateDecal)->decal->receivers.included==std::vector<EntityId>{duplicateReceiver},"Duplicating receiver+decal lost internal occurrence references.");
    require(document.undo()&&!document.scene().contains(duplicateDecal)&&document.scene().find(decal)->decal==value,document.status());
    document.select({receiver},receiver);require(document.eraseSelection()&&document.scene().find(decal)->decal->seed.receiver==receiver,"Deleting a receiver destroyed its repairable decal provenance.");
    require(document.undo()&&document.scene().contains(receiver)&&document.scene().find(decal)->decal==value,document.status());
    const auto historical=document.historicalDecalReferences();require(std::find(historical.begin(),historical.end(),asset.id)!=historical.end(),"Scene history omitted decal asset references.");
    const auto models=document.historicalModelReferences();require(std::find(models.begin(),models.end(),value.seed.modelId)!=models.end(),"Scene history omitted seed geometry generations.");
    auto bad=*document.scene().find(decal);bad.decal->seed.barycentric={-1,1,1};const auto previous=*document.scene().find(decal);require(!document.updateEntity(decal,bad)&&sameEntity(*document.scene().find(decal),previous),"Malformed seed partially changed the scene.");
    auto legacy=Json::parse(bytes);legacy["version"]=23;writeFileAtomically(file,legacy.dump(2)+"\n");const auto legacyBytes=read(file);
    require(reopened.load(sceneId)&&!reopened.dirty()&&!reopened.scene().find(decal)->decal&&read(file)==legacyBytes,"Old scene loading created a decal, dirtied data or rewrote its bytes.");
    require(reopened.save()&&Json::parse(read(file)).at("version")==24,reopened.status());
}
}
int main() {
    try{assetsRoundTripAndPolicies();replacementCoverageIsSafe();documentHistoryAndSaveAs();sceneCapabilityHistoryAndMigration();std::cout<<"Decal asset, document and scene tests passed\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
