#include "assets/AssetLibrary.h"
#include "assets/ContentStorage.h"
#include "decals/DecalResolve.h"
#include "editor/DecalResources.h"
#include "scene/SceneDocument.h"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using namespace crimson;
namespace fs=std::filesystem;
void check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
struct Project {
    fs::path root=fs::temp_directory_path()/("crimson-decal-spatial-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Project(){fs::create_directories(root);initializeContentStorage(root);}
    ~Project(){std::error_code error;const auto owner=fs::weakly_canonical(fs::temp_directory_path(),error),target=fs::weakly_canonical(root,error);
        if(!error&&target.parent_path()==owner&&target.filename().string().starts_with("crimson-decal-spatial-"))fs::remove_all(target,error);}
};
void boxBypassesSurfacePreparation(){
    Project project;const auto asset=createDecalAsset(project.root,"Box material pending");
    ResourceManager resources;AssetLibrary assets(project.root,&resources);DecalResources decals(assets,resources);
    SceneDocument document(project.root);const auto receiver=document.createBox("Receiver");
    const auto box=document.createBoxDecal();auto record=*document.scene().find(box);record.decal->asset.id=asset.id;
    record.local=math::translation({0,.5f,0})*math::scaling({4,3,.2f});check(document.updateEntity(box,record),document.status());
    for(unsigned retry=0;retry<20;++retry){
        resources.pump();assets.updateResources(0);
        const auto prepared=decals.prepare(document.scene(),extractScene(document.scene()));
        check(prepared.receivers.empty()&&!decals.patch(box)&&!decals.surfaceFrame(box),"Box preparation acquired a frozen surface or original-LOD dependency.");
    }
    const auto initial=decals.stats();check(initial.spatialPreparations==0&&initial.seedQueries==0,"Box material readiness retries ran surface discovery or seed queries.");
    const auto previous=record.local;record.local=math::scaling({2,1,1})*record.local;
    check(document.updateEntity(box,record)&&document.undo(),document.status());
    check(document.scene().find(box)->local==previous&&!document.transactionActive(),"Box edit held commands or lost exact undo.");
    auto receiverRecord=*document.scene().find(receiver);receiverRecord.receiveDecals=false;check(document.updateEntity(receiver,receiverRecord),document.status());
    decals.prepare(document.scene(),extractScene(document.scene()));
    check(decals.stats().spatialPreparations==0&&decals.stats().seedQueries==0,"Receiver changes sent a box through the conforming solver.");
    resources.stop();
}
void exercise(){
    Project project;
    const auto material=createMaterialAsset(project.root,"Receiver");
    const auto decalAsset=createDecalAsset(project.root,"Pending decal material");
    ResourceManager resources;AssetLibrary assets(project.root,&resources);DecalResources decals(assets,resources);
    SceneDocument document(project.root);const auto receiver=document.createBox("Receiver");
    auto receiverRecord=*document.scene().find(receiver);receiverRecord.modelMaterials={AssetRef<MaterialAsset>{material.id}};
    check(document.updateEntity(receiver,receiverRecord),document.status());
    SceneDecal authored;authored.asset.id=decalAsset.id;authored.seed.receiver=receiver;
    authored.seed.modelId=receiverRecord.model->id;authored.seed.localPoint={.1f,.5f,.1f};
    const auto frame=decals::surfacePlacementFrame(authored.seed.localPoint,{0,1,0})*math::scaling({.4f,.4f,1});
    const auto decal=document.createDecal(authored,"Patch",frame);
    DecalPreparedScene prepared;
    const auto tick=[&]{resources.pump();assets.updateResources(0);decals.collect();prepared=decals.prepare(document.scene(),extractScene(document.scene()),ResourcePriority::Preview);};
    const auto until=[&](const auto& condition,const std::string& reason){
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        do{tick();if(condition())return;std::this_thread::sleep_for(std::chrono::milliseconds(1));}while(std::chrono::steady_clock::now()<deadline);
        throw std::runtime_error(reason+": "+prepared.status);
    };
    // Real worker geometry/chart preparation completes; only the material GPU
    // owner is absent. This reproduces thumbnail retries without a window.
    until([&]{return bool(decals.patch(decal));},"The initial CPU decal patch did not publish");
    check(!prepared.ready&&!prepared.refined&&prepared.decals.empty(),"A pending material was advertised as a drawable decal.");
    const auto initial=decals.stats();const auto initialPatch=decals.patch(decal);
    check(initial.seedQueries>0,"The initial receiver seed was never resolved.");
    const auto retryStart=std::chrono::steady_clock::now();
    for(unsigned retry=0;retry<100;++retry)tick();
    const auto retryMicroseconds=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-retryStart).count()/100;
    auto observed=decals.stats();
    check(observed.spatialPreparations==initial.spatialPreparations&&observed.seedQueries==initial.seedQueries,
        "Unchanged chart/material retries repeated receiver discovery or seed projection.");
    check(observed.spatialCacheHits>=initial.spatialCacheHits+100&&decals.patch(decal)==initialPatch,
        "Unchanged retries lost their retained spatial/render publication.");
    check(!prepared.ready&&!prepared.status.empty(),"Caching spatial work stopped asynchronous material readiness checks.");

    auto decalRecord=*document.scene().find(decal);decalRecord.decal->tint={.2f,.3f,.4f};decalRecord.decal->opacity=.6f;
    check(document.updateEntity(decal,decalRecord),document.status());tick();
    check(decals.stats().spatialPreparations==observed.spatialPreparations,"Appearance-only decal edits rebuilt receiver geometry.");

    auto previous=decals.stats();decalRecord.local=math::translation({.1f,0,0})*decalRecord.local;
    check(document.updateEntity(decal,decalRecord),document.status());tick();
    check(decals.stats().spatialPreparations>previous.spatialPreparations&&decals.stats().seedQueries>previous.seedQueries,
        "A decal placement edit reused the old seed projection.");
    until([&]{return decals.patch(decal)&&decals.patch(decal)->fingerprint!=initialPatch->fingerprint;},"Moved decal chart did not publish");

    previous=decals.stats();receiverRecord.local=math::translation({0,.1f,0});
    check(document.updateEntity(receiver,receiverRecord),document.status());tick();
    check(decals.stats().spatialPreparations>previous.spatialPreparations&&decals.stats().seedQueries>previous.seedQueries,
        "A receiver transform edit reused the old surface projection.");
    const auto movedFrame=decals.surfaceFrame(decal);
    check(movedFrame&&std::abs(math::transformPoint(*movedFrame,{})[1]-.6f)<.0001f,
        "Receiver movement retained the old conformed surface frame.");
    until([&]{return bool(decals.patch(decal));},"Transformed receiver chart did not publish");

    previous=decals.stats();receiverRecord.primitive=PrimitiveMeshSettings{};receiverRecord.primitive->size={1.2f,1,1};
    check(document.updateEntity(receiver,receiverRecord),document.status());
    until([&]{return decals.patch(decal)&&decals.stats().seedQueries>previous.seedQueries;},"Changed primitive geometry did not rebuild its surface/chart");
    check(decals.stats().spatialPreparations>previous.spatialPreparations,"A primitive geometry edit reused receiver discovery.");

    previous=decals.stats();auto unlit=material;materialOutputNode(unlit.graph).output.shading=MaterialShading::Unlit;
    canonicalizeMaterialAsset(unlit);assets.publishMaterial(std::make_shared<const MaterialAsset>(unlit));tick();
    check(decals.stats().spatialPreparations>previous.spatialPreparations&&prepared.receivers.empty()&&!decals.patch(decal),
        "A receiver material eligibility change retained its old decal patch.");
    previous=decals.stats();for(unsigned retry=0;retry<50;++retry)tick();
    check(decals.stats().spatialPreparations==previous.spatialPreparations&&decals.stats().seedQueries==previous.seedQueries,
        "An unchanged unsupported receiver retried spatial preparation.");
    check(!prepared.ready&&prepared.status.find("Unlit")!=std::string::npos,"Cached unsupported coverage lost its diagnostic/readiness.");
    assets.publishMaterial(std::make_shared<const MaterialAsset>(material));
    until([&]{return bool(decals.patch(decal));},"A restored eligible material did not restore the retained chart");

    previous=decals.stats();receiverRecord.receiveDecals=false;
    check(document.updateEntity(receiver,receiverRecord),document.status());tick();
    check(decals.stats().spatialPreparations>previous.spatialPreparations&&prepared.receivers.empty()&&!decals.patch(decal),
        "Receive Decals changes retained stale receiver eligibility.");
    receiverRecord.receiveDecals=true;check(document.updateEntity(receiver,receiverRecord),document.status());
    until([&]{return bool(decals.patch(decal));},"Restoring Receive Decals did not restore the chart");
    previous=decals.stats();for(unsigned retry=0;retry<50;++retry)tick();
    check(decals.stats().spatialPreparations==previous.spatialPreparations&&decals.stats().seedQueries==previous.seedQueries,
        "Restored receiver retries repeated spatial preparation.");
    std::cout<<"Decal spatial cache: 200 unchanged retries retained receiver discovery/seed work; geometry, placement, transform, material and eligibility invalidation passed. Initial seed queries="
        <<initial.seedQueries<<", after 100 pending retries="<<observed.seedQueries<<", mean CPU fixture retry="<<retryMicroseconds<<" us (single box; no GPU or editor).\n";
    resources.stop();
}
}
int main(){try{boxBypassesSurfacePreparation();exercise();return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
