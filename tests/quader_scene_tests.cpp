#include "scene/SceneDocument.h"
#include "editor/QuaderSceneIntegration.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unordered_map>

namespace {
using namespace crimson;
namespace fs=std::filesystem;
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
struct Project {
    fs::path root=fs::temp_directory_path()/("crimson-quader-scene-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~Project(){std::error_code error;const auto parent=fs::weakly_canonical(fs::temp_directory_path(),error);if(error)return;
        const auto target=fs::weakly_canonical(root,error);if(!error&&target.parent_path()==parent&&target.filename().string().starts_with("crimson-quader-scene-"))fs::remove_all(target,error);}
};
std::string snapshot(const SceneDocument& document) {
    return serializeSceneSnapshot({document.assetId(),document.displayName(),document.scene().records(),document.scene().properties(),document.previewCamera()});
}
EntityId generatedId(const Scene& scene,EntityId owner,const std::string& key) {
    for(const auto& record:scene.records())if(record.quaderGenerated&&record.quaderGenerated->owner==owner&&record.quaderGenerated->sourceKey==key)return record.id;
    return NoEntity;
}
std::vector<EntityRecord> importRecords(EntityId owner,const std::string& version,bool oldLight=true) {
    EntityRecord visual;visual.id=1;visual.name="Floor";visual.model=AssetRef<ModelAsset>{"assets/models/"+version+"/manifest.json"};
    visual.local=math::translation({1,2,3});visual.quaderGenerated=QuaderGenerated{owner,"mesh/floor"};
    EntityRecord collision;collision.id=2;collision.name="Floor collision";collision.parent=1;collision.collider.emplace();collision.collider->shape=ColliderShape::Trimesh;
    collision.collider->source=AssetRef<ModelAsset>{"assets/models/"+version+"-collision/manifest.json"};collision.quaderGenerated=QuaderGenerated{owner,"collision/floor"};
    EntityRecord light;light.id=3;light.name="Light";light.directional.emplace();
    light.quaderGenerated=QuaderGenerated{owner,oldLight?"light/old":"light/new"};
    return {visual,collision,light};
}
void reimportIdentityHistoryAndAtomicFailure() {
    Project project;SceneDocument document(project.root);const auto owner=document.createQuader();
    const auto manual=document.createEmpty("Manual child");document.select({manual},manual);
    require(document.reparentSelection(owner),document.status());
    auto manualRecord=*document.scene().find(manual);manualRecord.local=math::translation({9,8,7});
    require(document.updateEntity(manual,manualRecord),document.status());
    QuaderLink link{"C:/authoring/project.quader","project-id","scene-id",.5f,true,false};
    require(document.replaceQuaderImport(owner,link,importRecords(owner,"generation-a")),document.status());
    const auto imported=generatedId(document.scene(),owner,"Imported"),visual=generatedId(document.scene(),owner,"mesh/floor");
    const auto collision=generatedId(document.scene(),owner,"collision/floor"),oldLight=generatedId(document.scene(),owner,"light/old");
    require(imported&&visual&&collision&&oldLight&&document.scene().find(imported)->parent==owner&&
        document.scene().find(visual)->parent==imported&&document.scene().find(collision)->parent==visual,"Import lost generated hierarchy or source identities.");
    require(document.scene().find(manual)->parent==owner&&document.scene().find(manual)->local==manualRecord.local,"Import changed a manual sibling.");
    auto custom=*document.scene().find(collision);custom.collider->shape=ColliderShape::Capsule;
    custom.collider->radius=.7f;custom.collider->height=3.2f;custom.collider->size={2,3,4};
    custom.collider->showCollisionInGame=true;custom.collider->collisionColor={.2f,.4f,.6f,.8f};
    custom.local=math::translation({.35f,.4f,-.2f}); // Center offset from a one-sided resize.
    require(document.updateEntity(collision,custom),document.status());
    const auto environment=document.createEnvironment("Manual environment");auto env=*document.scene().find(environment);env.environment->sun=oldLight;
    require(document.updateEntity(environment,env)&&document.setActiveEnvironment(environment),document.status());
    document.select({manual},manual);const auto before=snapshot(document);const auto history=document.history().size();
    link.importScale=2;link.sceneId="replacement-scene";
    auto next=importRecords(owner,"generation-b",false);next[0].local=math::translation({5,6,7});
    require(document.replaceQuaderImport(owner,link,next),document.status());
    require(document.history().size()==history+1&&document.dirty(),"Reimport did not create exactly one history command.");
    require(generatedId(document.scene(),owner,"Imported")==imported&&generatedId(document.scene(),owner,"mesh/floor")==visual&&
        generatedId(document.scene(),owner,"collision/floor")==collision,"Matching source keys lost persistent entity IDs.");
    auto expected=*custom.collider;expected.source=AssetRef<ModelAsset>{"assets/models/generation-b-collision/manifest.json"};
    require(document.scene().find(collision)->collider==expected,"Quader sync reset an authored collider shape, dimensions, or display instead of updating its source.");
    require(document.scene().find(collision)->local==custom.local&&document.scene().find(visual)->local==next[0].local,
        "Quader sync lost a resized primitive's center offset or stopped syncing its visual parent's source transform.");
    require(!document.scene().contains(oldLight)&&document.scene().find(environment)->environment->sun==NoEntity&&
        document.scene().properties().activeEnvironment==environment,"Removed generated light left an invalid external reference.");
    require(document.scene().find(manual)->name=="Manual child"&&document.scene().find(manual)->local==manualRecord.local&&
        document.selection().primary==manual,"Reimport changed an unrelated authored edit or selection.");
    const auto references=document.historicalModelReferences();
    for(const auto* id:{"assets/models/generation-a/manifest.json","assets/models/generation-a-collision/manifest.json",
        "assets/models/generation-b/manifest.json","assets/models/generation-b-collision/manifest.json"})
        require(std::find(references.begin(),references.end(),id)!=references.end(),"Undo history lost a managed visual/collision package reference.");
    const auto after=snapshot(document);require(document.undo()&&snapshot(document)==before,"Reimport undo did not restore exact previous scene/link state.");
    require(document.redo()&&snapshot(document)==after,"Reimport redo lost generated IDs or fields.");
    auto invalid=importRecords(owner,"invalid");invalid[1].quaderGenerated->sourceKey=invalid[0].quaderGenerated->sourceKey;
    require(!document.replaceQuaderImport(owner,link,invalid)&&snapshot(document)==after&&!document.transactionActive(),"Duplicate source identities partially applied an import.");
    invalid=importRecords(owner,"invalid");invalid[1].parent=9999;
    require(!document.replaceQuaderImport(owner,link,invalid)&&snapshot(document)==after,"Missing generated parent partially applied an import.");
    require(document.save("Quader fixture"),document.status());SceneDocument loaded(project.root);
    require(loaded.load(document.assetId())&&snapshot(loaded)==snapshot(document),"Scene v9 did not preserve Quader/collision authored data.");
    SceneDocument runtime(project.root);require(runtime.resetTransientFrom(document),runtime.status());const auto runtimeBefore=snapshot(runtime);
    require(!runtime.replaceQuaderImport(owner,link,importRecords(owner,"runtime"))&&snapshot(runtime)==runtimeBefore,"Play accepted an authored bridge replacement.");
}
QuaderImportResult lightSyncFixture(const std::string& generation) {
    QuaderImportResult result;result.projectId="project-id";result.sceneId="scene-id";result.sourceKey=generation;
    QuaderImportedMesh mesh;mesh.sourceKey="mesh/floor";mesh.name="Floor";mesh.local=math::translation({1,2,3});
    mesh.modelId="assets/models/"+generation+"/manifest.json";mesh.collisionModelId="assets/models/"+generation+"-collision/manifest.json";
    result.meshes.push_back(mesh);
    QuaderImportedLight sun;sun.sourceKey="light/sun";sun.name="Imported sun";sun.intensity=2;sun.castShadows=true;
    result.lights.push_back(sun);
    QuaderImportedLight point;point.sourceKey="light/point";point.name="Imported point";point.point=true;point.radius=7;
    point.intensity=3;point.color={.5f,.7f,1};point.local=math::translation({4,5,6});result.lights.push_back(point);
    return result;
}
void independentLightSyncPolicy() {
    Project project;SceneDocument document(project.root);const auto owner=document.createQuader();
    require(document.scene().find(owner)->quader->syncLights,"New Quader links must import lights by default.");
    const auto manualSun=document.createDirectionalLight("Manual sun"),manualPoint=document.createPointLight("Manual point");
    document.select({manualPoint},manualPoint);require(document.reparentSelection(owner),document.status());
    const auto manualPointRecord=*document.scene().find(manualPoint);
    QuaderLink link{"C:/authoring/project.quader","project-id","scene-id",1,true,false};
    auto initial=lightSyncFixture("generation-a");
    require(applyQuaderSceneImport(document,owner,link,initial),document.status());
    const auto sun=generatedId(document.scene(),owner,"light/sun"),point=generatedId(document.scene(),owner,"light/point");
    const auto visual=generatedId(document.scene(),owner,"mesh/floor"),collision=generatedId(document.scene(),owner,"mesh/floor/collision");
    require(sun&&point&&visual&&collision&&document.scene().find(sun)->directional&&document.scene().find(point)->point,
        "Lights Sync on did not emit both supported source light types alongside geometry/collision.");
    const auto environment=document.createEnvironment("Manual environment");auto env=*document.scene().find(environment);env.environment->sun=sun;
    require(document.updateEntity(environment,env),document.status());const auto before=snapshot(document);const auto history=document.history().size();
    link.syncLights=false;link.refreshMaterials=true;auto replacement=lightSyncFixture("generation-b");
    require(applyQuaderSceneImport(document,owner,link,replacement),document.status());
    require(document.history().size()==history+1&&!document.scene().find(owner)->quader->syncLights&&document.scene().find(owner)->quader->refreshMaterials,
        "Light-sync policy was not independently committed with the import.");
    require(!document.scene().contains(sun)&&!document.scene().contains(point)&&!generatedId(document.scene(),owner,"light/sun")&&
        !generatedId(document.scene(),owner,"light/point")&&document.scene().find(environment)->environment->sun==NoEntity,
        "Lights Sync off retained generated lights or a dangling Sun link.");
    require(document.scene().find(manualSun)->name=="Manual sun"&&document.scene().find(manualPoint)->point==manualPointRecord.point&&
        document.scene().find(manualPoint)->parent==owner&&document.scene().find(manualPoint)->local==manualPointRecord.local,
        "Lights Sync off removed or changed unrelated authored lights.");
    require(generatedId(document.scene(),owner,"mesh/floor")==visual&&generatedId(document.scene(),owner,"mesh/floor/collision")==collision&&
        document.scene().find(visual)->model->id==replacement.meshes[0].modelId&&
        document.scene().find(collision)->collider->source.id==replacement.meshes[0].collisionModelId,
        "Lights Sync off interrupted geometry/collision replacement or persistent identities.");
    const auto after=snapshot(document);require(document.undo()&&snapshot(document)==before,"Light-sync import undo lost lights or policy.");
    require(document.redo()&&snapshot(document)==after,"Light-sync import redo lost removal or policy.");
    require(document.save("Lights Sync fixture"),document.status());SceneDocument loaded(project.root);
    require(loaded.load(document.assetId())&&snapshot(loaded)==snapshot(document)&&!loaded.scene().find(owner)->quader->syncLights,
        "Scene save/reopen did not preserve Lights Sync off.");
    auto legacy=nlohmann::json::parse(after);for(auto& entity:legacy["entities"])if(entity.contains("quader"))entity["quader"].erase("syncLights");
    const auto legacyId="assets/scenes/quader-without-light-policy.crimson-scene";
    {std::ofstream output(project.root/legacyId,std::ios::binary);output<<legacy.dump();}
    const auto old=readSceneSnapshot(project.root,legacyId);
    const auto oldOwner=std::find_if(old.records.begin(),old.records.end(),[&](const auto& record){return record.id==owner;});
    require(oldOwner!=old.records.end()&&oldOwner->quader&&oldOwner->quader->syncLights,
        "Existing Quader links without a light policy stopped importing lights.");
    link.syncLights=true;require(applyQuaderSceneImport(document,owner,link,replacement),document.status());
    const auto restoredSun=generatedId(document.scene(),owner,"light/sun"),restoredPoint=generatedId(document.scene(),owner,"light/point");
    require(restoredSun&&restoredPoint&&document.scene().find(restoredSun)->directional->intensity==2&&
        document.scene().find(restoredPoint)->point->radius==7&&generatedId(document.scene(),owner,"mesh/floor")==visual&&
        document.scene().contains(manualSun)&&document.scene().contains(manualPoint),"Re-enabling Lights Sync did not restore source lights safely.");
    SceneDocument firstOff(project.root);const auto offOwner=firstOff.createQuader();link.syncLights=false;
    require(applyQuaderSceneImport(firstOff,offOwner,link,initial),firstOff.status());
    require(!generatedId(firstOff.scene(),offOwner,"light/sun")&&!generatedId(firstOff.scene(),offOwner,"light/point")&&
        generatedId(firstOff.scene(),offOwner,"mesh/floor")&&generatedId(firstOff.scene(),offOwner,"mesh/floor/collision"),
        "Initial Lights Sync off still imported source lights or omitted geometry/collision.");
}
void directionalCompatibilityAndMeshDefaults() {
    Project project;SceneDocument document(project.root);const auto sun=document.createDirectionalLight();
    require(document.scene().find(sun)->directional->castShadows,"New directional lights must enable shadows.");
    auto serialized=nlohmann::json::parse(snapshot(document));require(serialized.at("version")==12,"New scenes did not use schema 12.");
    serialized["version"]=8;
    for(auto& entity:serialized["entities"])if(entity.contains("directional")) {
        entity["directional"].erase("castShadows");entity["directional"].erase("shadowDistance");
        entity["directional"].erase("shadowDepthBias");entity["directional"].erase("shadowNormalBias");
    }
    const auto id="assets/scenes/legacy.crimson-scene";fs::create_directories(project.root/"assets/scenes");
    {std::ofstream output(project.root/id,std::ios::binary);output<<serialized.dump();}
    const auto legacy=readSceneSnapshot(project.root,id);
    require(legacy.records.size()==1&&legacy.records[0].directional&&!legacy.records[0].directional->castShadows&&
        legacy.records[0].directional->shadowDistance==100,"Old directional lights gained shadows or invalid shadow defaults.");
    EntityRecord marker;marker.id=1;marker.name="Cleared mesh collision";marker.collider.emplace();marker.collider->shape=ColliderShape::Trimesh;
    Scene scene;scene.apply({marker},{});require(scene.find(1)->collider->source.empty(),"Cleared mesh collision marker was rejected.");
}
void clusteredPointLightSceneImport() {
    Project project;SceneDocument document(project.root);const auto owner=document.createQuader();
    QuaderLink link{"C:/authoring/project.quader","project-id","scene-id",1,true,false};
    auto result=lightSyncFixture("clustered-lights");result.lights.clear();
    for(std::uint32_t i=0;i<64;++i) {
        QuaderImportedLight light;light.sourceKey="light/torch-"+std::to_string(i);light.name="Torch "+std::to_string(i);
        light.point=true;light.radius=7;light.intensity=3;light.local=math::translation({static_cast<float>(i),2,-4});
        result.lights.push_back(std::move(light));
    }
    require(applyQuaderSceneImport(document,owner,link,result),document.status());
    for(std::uint32_t i=0;i<64;++i) {
        const auto id=generatedId(document.scene(),owner,"light/torch-"+std::to_string(i));
        require(id&&document.scene().find(id)->point&&document.scene().find(id)->local[12]==i,"Quader scene publication truncated or mismapped lights above sixteen.");
    }
    require(document.save("Clustered Quader lights"),document.status());
    SceneDocument loaded(project.root);require(loaded.load(document.assetId())&&snapshot(loaded)==snapshot(document),"Many Quader lights did not survive scene save/reopen.");
    require(document.undo(),"Many-light Quader import was not undoable.");
    require(!generatedId(document.scene(),owner,"light/torch-63"),"Many-light Quader undo retained an imported light.");
    require(document.redo()&&snapshot(document)==snapshot(loaded),"Many-light Quader redo lost identities or state.");
}
void crimsonLightingRolesSurviveSourceSync() {
    Project project;SceneDocument document(project.root);const auto owner=document.createQuader();
    QuaderLink link{"C:/authoring/project.qproj","project-id","scene-id",1,true,false};
    auto source=lightSyncFixture("lighting-a");require(applyQuaderSceneImport(document,owner,link,source),document.status());
    const auto visual=generatedId(document.scene(),owner,"mesh/floor"),point=generatedId(document.scene(),owner,"light/point"),sun=generatedId(document.scene(),owner,"light/sun");
    auto model=*document.scene().find(visual);model.lightingScopes={lighting_scope::World,0,lighting_scope::Character};model.castShadows=false;
    require(document.updateEntity(visual,model),document.status());
    auto lamp=*document.scene().find(point);lamp.point->roles={lighting_scope::Character,lighting_scope::World,lighting_scope::Character,false,.3f};
    lamp.point->intensity=99;lamp.point->radius=99;lamp.point->castShadows=true;lamp.local=math::translation({99,99,99});
    require(document.updateEntity(point,lamp),document.status());
    auto directional=*document.scene().find(sun);directional.directional->roles={lighting_scope::World,lighting_scope::Character,lighting_scope::World,true,.7f};
    directional.directional->castShadows=false;require(document.updateEntity(sun,directional),document.status());
    const auto before=snapshot(document);
    auto replacement=lightSyncFixture("lighting-b");replacement.lights[1].intensity=11;replacement.lights[1].radius=17;
    replacement.lights[1].local=math::translation({5,7,9});
    auto added=replacement.lights[1];added.sourceKey="light/new-point";replacement.lights.push_back(added);
    require(applyQuaderSceneImport(document,owner,link,replacement),document.status());
    require(generatedId(document.scene(),owner,"mesh/floor")==visual && generatedId(document.scene(),owner,"light/point")==point &&
        generatedId(document.scene(),owner,"light/sun")==sun,"Role-preserving import changed persistent source identities.");
    require(document.scene().find(visual)->lightingScopes==model.lightingScopes && document.scene().find(visual)->castShadows &&
        document.scene().find(visual)->model->id==replacement.meshes[0].modelId,"Source sync lost Crimson scopes or retained unrelated source-owned model fields.");
    require(document.scene().find(point)->point->roles==lamp.point->roles && document.scene().find(point)->point->intensity==11 &&
        document.scene().find(point)->point->radius==17 && !document.scene().find(point)->point->castShadows &&
        document.scene().find(point)->local==replacement.lights[1].local,"Point role preservation blocked source light/transform synchronization.");
    require(document.scene().find(sun)->directional->roles==directional.directional->roles && document.scene().find(sun)->directional->castShadows,
        "Directional role preservation lost roles or blocked source shadow synchronization.");
    const auto fresh=generatedId(document.scene(),owner,"light/new-point");
    require(fresh && document.scene().find(fresh)->point->roles==ScenePointLight{}.roles,"A new source inherited another light's custom roles.");
    const auto after=snapshot(document);require(document.undo() && snapshot(document)==before,"Role-preserving import undo was not exact.");
    require(document.redo() && snapshot(document)==after,"Role-preserving import redo lost scopes or source fields.");
    require(document.save("Scoped bridge lighting"),document.status());SceneDocument loaded(project.root);
    require(loaded.load(document.assetId()) && snapshot(loaded)==snapshot(document),"Source-synchronized Crimson roles did not survive save/reopen.");
    replacement.lights[1].point=false;require(applyQuaderSceneImport(document,owner,link,replacement),document.status());
    require(document.scene().find(point)->directional && !document.scene().find(point)->point &&
        document.scene().find(point)->directional->roles==SceneDirectionalLight{}.roles,
        "Changing a source light kind retained incompatible previous-kind roles.");
}
void materialSlotsSurviveSourceSync() {
    Project project;SceneDocument document(project.root);const auto owner=document.createQuader();
    QuaderLink link{"C:/authoring/project.quader","project-id","scene-id",1,true,false};
    const std::string a="assets/materials/source-a.crimson-material",b="assets/materials/source-b.crimson-material",c="assets/materials/source-c.crimson-material";
    const AssetRef<MaterialAsset> replacement{"assets/materials/instance.crimson-material"};
    std::unordered_map<std::string,std::vector<std::string>> sources;
    const ModelMaterialSourceQuery query=[&](const std::string& id)->std::optional<std::vector<std::string>> {
        const auto found=sources.find(id);return found==sources.end()?std::nullopt:std::optional{found->second};
    };
    auto initial=lightSyncFixture("slots-a");sources[initial.meshes[0].modelId]={a,b,c};
    require(applyQuaderSceneImport(document,owner,link,initial,query),document.status());
    const auto visual=generatedId(document.scene(),owner,"mesh/floor");
    auto entity=*document.scene().find(visual);setModelMaterialOverride(entity.modelMaterials,1,replacement);
    require(document.updateEntity(visual,entity),document.status());
    auto changed=lightSyncFixture("slots-b");sources[changed.meshes[0].modelId]={a,b,c};
    require(applyQuaderSceneImport(document,owner,link,changed,query),document.status());
    require(document.scene().find(visual)->modelMaterials==entity.modelMaterials,"Ordinary Quader geometry sync lost numbered slot assignments.");
    const auto before=snapshot(document);
    auto reordered=lightSyncFixture("slots-reordered");
    auto prepared=std::make_shared<ModelAsset>();prepared->id=reordered.meshes[0].modelId;prepared->materialIds={b,c,a};
    reordered.preparedModels.push_back(prepared);sources[prepared->id]=prepared->materialIds;
    require(applyQuaderSceneImport(document,owner,link,reordered,query),document.status());
    require(document.scene().find(visual)->modelMaterials==ModelMaterialOverrides{replacement},"Unique source material identity did not follow its reordered slot.");
    const auto remapped=snapshot(document);require(document.undo() && snapshot(document)==before,"Slot remapping did not undo with its Quader import.");
    require(document.redo() && snapshot(document)==remapped,"Slot remapping did not redo with its Quader import.");
    const auto reject=[&](QuaderImportResult result,const std::vector<std::string>& ids) {
        sources[result.meshes[0].modelId]=ids;const auto history=document.history().size();
        require(!applyQuaderSceneImport(document,owner,link,result,query) && snapshot(document)==remapped &&
            document.history().size()==history && !document.transactionActive(),"Ambiguous/missing material source slots partially replaced the scene.");
        require(document.status().find("material slot")!=std::string::npos,"Rejected material remapping omitted its diagnostic.");
    };
    reject(lightSyncFixture("slots-ambiguous"),{b,b,c});
    reject(lightSyncFixture("slots-removed"),{a,c});
    require(!applyQuaderSceneImport(document,owner,link,lightSyncFixture("slots-unavailable"),query) && snapshot(document)==remapped,
        "Unavailable source metadata discarded existing instance assignments.");
    entity=*document.scene().find(visual);entity.modelMaterials.clear();require(document.updateEntity(visual,entity),document.status());
    auto repeated=lightSyncFixture("slots-repeated-a");sources[repeated.meshes[0].modelId]={b,b,c};
    require(applyQuaderSceneImport(document,owner,link,repeated,query),document.status());
    entity=*document.scene().find(visual);setModelMaterialOverride(entity.modelMaterials,1,replacement);require(document.updateEntity(visual,entity),document.status());
    auto repeatedNext=lightSyncFixture("slots-repeated-b");sources[repeatedNext.meshes[0].modelId]={b,b,c};
    require(applyQuaderSceneImport(document,owner,link,repeatedNext,query) && document.scene().find(visual)->modelMaterials==entity.modelMaterials,
        "Unchanged repeated source slots lost their numbered instance assignments.");
    const auto repeatedBefore=snapshot(document);auto repeatedReordered=lightSyncFixture("slots-repeated-reordered");sources[repeatedReordered.meshes[0].modelId]={c,b,b};
    require(!applyQuaderSceneImport(document,owner,link,repeatedReordered,query) && snapshot(document)==repeatedBefore,
        "Repeated source IDs were guessed when their slot layout changed.");
}
}
int main(){try{reimportIdentityHistoryAndAtomicFailure();independentLightSyncPolicy();directionalCompatibilityAndMeshDefaults();clusteredPointLightSceneImport();crimsonLightingRolesSurviveSourceSync();materialSlotsSurviveSourceSync();std::cout<<"Quader scene identity, atomic reimport, independent light sync, history and compatibility checks passed.\n";return 0;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
