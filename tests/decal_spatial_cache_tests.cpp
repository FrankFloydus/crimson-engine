#include "assets/AssetLibrary.h"
#include "assets/ContentStorage.h"
#include "decals/DecalResolve.h"
#include "decals/DecalPaintSurface.h"
#include "editor/DecalResources.h"
#include "editor/AssetPreviews.h"
#include "editor/MeshPaintResources.h"
#include "scene/SceneDocument.h"
#include <crimson/renderer/forward/ShadowRayGeometry.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
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
        check(!prepared.failed,"Pending box material readiness became a terminal geometry failure.");
    }
    const auto initial=decals.stats();check(initial.spatialPreparations==0&&initial.seedQueries==0,"Box material readiness retries ran surface discovery or seed queries.");
    const auto previous=record.local;record.local=math::scaling({2,1,1})*record.local;
    check(document.updateEntity(box,record)&&document.undo(),document.status());
    check(document.scene().find(box)->local==previous&&!document.transactionActive(),"Box edit held commands or lost exact undo.");
    auto receiverRecord=*document.scene().find(receiver);receiverRecord.receiveDecals=false;check(document.updateEntity(receiver,receiverRecord),document.status());
    decals.prepare(document.scene(),extractScene(document.scene()));
    check(decals.stats().spatialPreparations==0&&decals.stats().seedQueries==0,"Receiver changes sent a box through the conforming solver.");
    auto extraction=extractScene(document.scene());for(auto& decal:extraction.decals)if(decal.id==box)decal.world=math::scaling({0,1,1});
    const auto invalid=decals.prepare(document.scene(),extraction);
    check(invalid.failed&&!invalid.ready&&!invalid.refined,"A static singular decal box transform remained an asynchronous preparation.");
    auto mixed=extractScene(document.scene());auto broken=mixed.decals.front();
    broken.id^=UINT64_C(0x4000000000000000);broken.world=math::scaling({0,1,1});mixed.decals.push_back(broken);
    for(unsigned order=0;order<2;++order){
        const auto result=decals.prepare(document.scene(),mixed);
        check(result.failed&&result.status=="Invalid decal box transform.",
            "A pending decal concealed the terminal result responsible for thumbnail retry backoff.");
        std::reverse(mixed.decals.begin(),mixed.decals.end());
    }
    resources.stop();
}
void missingReceiverIsTerminal(){
    Project project;const auto asset=createDecalAsset(project.root,"No receiver");
    ResourceManager resources;AssetLibrary assets(project.root,&resources);DecalResources decals(assets,resources);
    SceneDocument document(project.root);SceneDecal authored;authored.asset.id=asset.id;document.createDecal(authored);
    for(unsigned attempt=0;attempt<3;++attempt){const auto result=decals.prepare(document.scene(),extractScene(document.scene()));
        check(result.failed&&!result.ready&&!result.refined&&result.decals.empty(),"A static missing receiver was misclassified as asynchronous work.");}
    check(decals.stats().spatialPreparations==1,"A cached missing receiver lost its terminal classification.");resources.stop();
}
void receiverReloadIsNotTerminal(){
    Project project;const std::string id="ca611d1f-5bad-4cf5-9c91-15b2c6f01551";
    const auto file=project.root/"Content"/"ReloadReceiver.crimson-model";
    fs::create_directories(file.parent_path());{std::ofstream output(file);output<<"{}";}
    registerAssetPath(project.root,id,file);
    ResourceManager resources;auto model=std::make_shared<ModelAsset>();model->id=id;
    resources.seed<ModelAsset>(AssetRef<ModelAsset>{id},{model,sizeof(ModelAsset),1,{}});
    const auto until=[&](const auto& condition){const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        do{resources.pump();if(condition())return;std::this_thread::sleep_for(std::chrono::milliseconds(1));}while(std::chrono::steady_clock::now()<deadline);
        throw std::runtime_error("The retained receiver reload fixture did not publish its resource state.");};
    until([&]{return resources.idle();});
    AssetLibrary assets(project.root,&resources);DecalResources decals(assets,resources);
    check(assets.findModel(id)==model,"The receiver reload fixture lost its initial immutable metadata.");
    ResourceRequestOptions options;options.forceReload=true;
    auto failed=resources.request<ModelAsset>(AssetRef<ModelAsset>{id},[](std::stop_token)->ResourceLoadResult<ModelAsset>{throw std::runtime_error("Expected receiver reload failure.");},options);
    until([&]{return failed.state()==ResourceState::Failed;});
    ExtractedModel occurrence;occurrence.assetId=id;
    check(decals.modelSurface(occurrence).failed,"A completed failed receiver reload was not terminal.");
    std::atomic_bool release{},entered{};
    // This owner always cancels before its captured atomics leave scope.
    auto reloading=resources.request<ModelAsset>(AssetRef<ModelAsset>{id},[&](std::stop_token stop){entered=true;while(!release&&!stop.stop_requested())std::this_thread::yield();return ResourceLoadResult<ModelAsset>{model,sizeof(ModelAsset),2,{}};},options);
    try{
        until([&]{return entered.load();});
        check(assets.modelLoading(id)&&!assets.modelDiagnostic(id).empty()&&assets.findModel(id)==model,
            "The receiver reload fixture did not retain the previous snapshot/diagnostic during asynchronous work.");
        const auto pending=decals.modelSurface(occurrence);
        check(!pending.current&&!pending.failed,"A live receiver reload acquired terminal thumbnail failure backoff from its previous diagnostic.");
        release=true;until([&]{return reloading.state()==ResourceState::Ready;});
        resources.stop();
    }catch(...){release=true;resources.stop();throw;}
}
void mixedPaintBoundaryPublication(){
    Project project;const std::string id="d5d4d482-17a2-4cdc-a7d8-909873996642";
    const auto file=project.root/"Content"/"MixedPaintReceiver.crimson-model";
    fs::create_directories(file.parent_path());{std::ofstream output(file);output<<"{}";}registerAssetPath(project.root,id,file);
    auto model=std::make_shared<ModelAsset>();model->id=id;model->materials.resize(1);
    const auto quad=[](float offset,float warp,bool varied){ModelPrimitive primitive;
        const std::array points{Vec3{offset+.1234567f,.2345678f,0},Vec3{offset+1.9876543f,.4567891f,0},Vec3{offset+2.112344f,2.343245f,warp},Vec3{offset+.2481464f,2.1210237f,0}};
        for(size_t i=0;i<points.size();++i){ModelVertex vertex;vertex.position=points[i];vertex.normal=varied?math::normalized(Vec3{float(i)*.02f,0,1}):Vec3{0,0,1};primitive.vertices.push_back(vertex);math::include(primitive.bounds,vertex.position);}
        primitive.indices={0,1,2,0,2,3};primitive.sourceFaces.push_back({{0,1,2,3},{0,1},4});return primitive;};
    // Axis-aligned draw zero has a proven exact proxy. The other draw has a
    // real shallow warp and normal field, and must retain its fine triangle IDs.
    auto planar=quad(0,0,false);for(auto& vertex:planar.vertices){vertex.position[0]=vertex.position[0]<1?0.f:2.f;vertex.position[1]=vertex.position[1]<1?0.f:2.f;}
    planar.bounds={};for(const auto& vertex:planar.vertices)math::include(planar.bounds,vertex.position);
    model->primitives={planar,quad(3,3.29e-6f,true)};model->draws.push_back({0});model->draws.push_back({1});model->draws.push_back({0,math::translation({8,0,0})});
    for(const auto& primitive:model->primitives)for(const auto& vertex:primitive.vertices)math::include(model->bounds,vertex.position);
    const auto coarse=buildMeshPaintGeometry(model->primitives[0],3),fine=buildMeshPaintGeometry(model->primitives[1],3);
    check(decals::simplifyDecalPaintPrimitive(model->primitives[0],*coarse).has_value()&&!decals::simplifyDecalPaintPrimitive(model->primitives[1],*fine),
        "The mixed host fixture stopped distinguishing an exact proxy from retained fine geometry.");
    ResourceManager resources;resources.seed<ModelAsset>(AssetRef<ModelAsset>{id},{model,sizeof(ModelAsset),41,{}});
    AssetLibrary assets(project.root,&resources);MeshPaintResources paint(assets,resources);DecalResources decals(assets,resources);
    EntityRecord record;record.id=71;record.model=AssetRef<ModelAsset>{id};record.meshPaint.emplace();record.meshPaint->subdivisionLevel=3;
    PreparedMeshPaintModel prepared;const auto tick=[&]{resources.pump();assets.updateResources(0);prepared=paint.prepare(record,ResourcePriority::Preview);};
    const auto until=[&](const auto& condition,const char* reason){const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{10};do{tick();if(condition())return;std::this_thread::sleep_for(std::chrono::milliseconds{1});}while(std::chrono::steady_clock::now()<deadline);throw std::runtime_error(std::string(reason)+": "+prepared.diagnostic);};
    until([&]{return prepared.ready;},"Mixed paint receiver did not publish its immutable worker geometry");
    check(prepared.asset&&prepared.decalAsset&&prepared.draws.size()==3&&prepared.decalAsset->draws.size()==3,"Mixed receiver publication lost a draw or model owner.");
    check(prepared.decalAsset!=prepared.asset,"The mixed host fixture stopped exercising the exact proxy publication path.");
    const auto contact=prepared.asset->primitives[0].preparedShadowRays;
    check(contact&&contact->triangleCount==2&&prepared.asset->primitives[0].indices.size()==coarse->indices.size(),
        "An exact planar contact proxy was absent or replaced refined render geometry.");
    check(prepared.asset->primitives[2].preparedShadowRays==contact&&prepared.draws[0].geometry==prepared.draws[2].geometry,
        "Repeated source draws rebuilt rather than shared their immutable exact contact geometry.");
    check(!prepared.asset->primitives[1].preparedShadowRays,
        "A warped/varying-normal draw acquired an unproven coarse contact proxy.");
    const auto& renderBounds=prepared.asset->primitives[0].bounds;
    check(contact->bounds.valid&&contact->bounds.minimum==renderBounds.minimum&&contact->bounds.maximum==renderBounds.maximum,
        "The planar contact proxy changed finite surface bounds.");
    for(const auto& draw:prepared.draws){check(draw.draw<prepared.decalAsset->draws.size(),"Boundary publication used the source primitive as a draw index.");
        const auto primitiveId=prepared.decalAsset->draws[draw.draw].primitive;check(primitiveId==draw.primitive&&primitiveId<prepared.decalAsset->primitives.size(),"Boundary publication no longer addresses the chosen occurrence primitive.");
        const auto& actual=prepared.decalAsset->primitives[primitiveId];
        if(draw.sourcePrimitive==0){check(actual.indices.size()==6&&!draw.decalBoundary,"An entirely collapsed exact proxy acquired fine triangle lineage.");continue;}
        check(actual.indices.size()==draw.geometry->indices.size()&&draw.decalBoundary&&!draw.decalBoundary->triangles.empty(),"A retained fine draw lost its matching perimeter lineage when another draw used a proxy.");
        uint32_t previous=0;bool first=true;size_t rounded=0;
        for(const auto& boundary:draw.decalBoundary->triangles){check(boundary.triangle<actual.indices.size()/3&&(first||boundary.triangle>previous)&&boundary.corners&&!(boundary.corners&~7u),"Published boundary records used stale/proxy triangle IDs.");first=false;previous=boundary.triangle;
            for(size_t corner=0;corner<3;++corner)if(boundary.corners&(1u<<corner)){const auto point=actual.vertices.at(actual.indices.at(boundary.triangle*3+corner)).position;const auto& construction=boundary.points[corner];double error=0;
                for(size_t axis=0;axis<3;++axis){const double expected=double(construction.first[axis])+(double(construction.last[axis])-construction.first[axis])*construction.parameter;const double difference=expected-point[axis];error+=difference*difference;}
                check(error<1e-10,"Published boundary records no longer match the actual fine render triangle corners.");rounded+=error>1e-24;
            }
        }
        if(draw.draw==1)check(rounded>0,"Retained slanted fine draw stopped exercising float-rounded boundary points.");
    }
    ExtractedModel occurrence;occurrence.id=record.id;occurrence.assetId=id;occurrence.meshPaint=record.meshPaint;
    DecalSourceResult source;until([&]{source=decals.modelSurface(occurrence,ResourcePriority::Preview);return source.current;},"Mixed actual decal source did not publish its boundary metadata");
    check(source.model==prepared.decalAsset&&source.surface&&source.surface->draws.size()==3,"Decal resource publication selected a different mixed model/proxy owner.");
    for(const auto& draw:source.surface->draws){check(draw.surface.id.draw<prepared.draws.size()&&draw.surface.boundary==prepared.draws[draw.surface.id.draw].decalBoundary,"Decal source worker lost or cross-assigned a fine draw's boundary metadata.");
        const auto primitiveId=prepared.decalAsset->draws[draw.surface.id.draw].primitive;check(draw.surface.id.primitive==primitiveId,"Decal source receiver identity no longer matches the chosen occurrence primitive.");}
    const auto asset=prepared.asset;const auto signature=prepared.decalSignature;const auto boundary=prepared.draws[1].decalBoundary;
    const auto& selected=prepared.draws[1];SceneMeshPaintDraw stored;stored.draw=selected.draw;stored.primitive=selected.sourcePrimitive;stored.level=3;stored.sourceSignature=selected.geometry->sourceSignature;
    const std::array changes{MeshPaintChange{0,MeshPaintRgb{100,80,20}}};stored.weights=updateMeshPaintPayload(selected.weights,changes);record.meshPaint->draws.push_back(stored);tick();
    check(prepared.ready&&prepared.asset==asset&&prepared.decalSignature==signature&&prepared.draws[1].decalBoundary==boundary,"Paint appearance edits invalidated original-boundary geometry or its source signature.");
    check(prepared.asset->primitives[0].preparedShadowRays==contact&&prepared.asset->primitives[2].preparedShadowRays==contact,
        "COW paint appearance edits invalidated the shared shape-equivalent contact geometry.");
    check(model->primitives[0].indices.size()==6&&model->primitives[1].indices.size()==6,"Boundary publication mutated the original immutable model.");
    std::cout<<"Mixed paint boundary publication: exact contact BLAS shared across repeated draws; warped/varying-normal fine draw retained matching occurrence IDs, lineage and paint-independent geometry identity.\n";
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
    check(!prepared.ready&&!prepared.refined&&!prepared.failed&&prepared.decals.empty(),"A pending material was advertised as drawable or terminally failed.");
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
    check(!prepared.ready&&!prepared.failed&&!prepared.status.empty(),"Caching spatial work stopped asynchronous material readiness checks.");

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
    check(!prepared.ready&&prepared.failed&&prepared.status.find("Unlit")!=std::string::npos,"Cached unsupported coverage lost its terminal diagnostic/readiness.");
    PreviewRetryState retry;const auto clock=PreviewRetryState::Clock::time_point{};unsigned failedAttempts=0;
    for(unsigned frame=0;frame<500;++frame){const auto now=clock+std::chrono::milliseconds{frame};if(!retry.ready(now))continue;++failedAttempts;tick();if(prepared.failed)retry.failed(now);}
    check(failedAttempts==1,"A persistent terminal decal thumbnail failure repeated full preparation each frame.");
    assets.publishMaterial(std::make_shared<const MaterialAsset>(material));
    retry.reset();check(retry.ready(clock),"Source invalidation did not clear the failed thumbnail delay.");
    until([&]{return bool(decals.patch(decal));},"A restored eligible material did not restore the retained chart");
    check(!prepared.failed,"Restored geometry retained a terminal thumbnail failure during material readiness.");
    unsigned pendingAttempts=0;for(unsigned frame=0;frame<50;++frame){const auto now=clock+std::chrono::milliseconds{frame};if(!retry.ready(now))continue;++pendingAttempts;tick();if(prepared.failed)retry.failed(now);}
    check(pendingAttempts==50&&!prepared.failed,"Asynchronous decal material preparation acquired failure backoff.");

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
int main(){try{boxBypassesSurfacePreparation();missingReceiverIsTerminal();receiverReloadIsNotTerminal();mixedPaintBoundaryPublication();exercise();return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
