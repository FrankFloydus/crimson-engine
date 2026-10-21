#include "scene/SceneDocument.h"
#include "scene/SceneOutlinerRows.h"
#include "editor/QuaderSceneIntegration.h"
#include "assets/AtomicFile.h"
#include "assets/ContentStorage.h"
#include "assets/QuaderSceneBundle.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
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
    auto wall=*document.scene().find(visual);wall.outlinerStyle.emplace();wall.outlinerStyle->color=Vec4{.2f,.5f,.7f,1};
    wall.meshShading=MeshShading::Smooth;
    require(document.updateEntity(visual,wall),document.status());
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
    require(document.scene().find(visual)->outlinerStyle==wall.outlinerStyle,"Quader refresh discarded entity/component Outliner colors.");
    require(document.scene().find(visual)->meshShading==MeshShading::Smooth,"Quader refresh discarded authored mesh shading when replacing its geometry generation.");
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
    mesh.modelId="assets/models/"+generation+"/manifest.json";
    mesh.collision=makeCollisionMesh({{0,0,0},{1,0,0},{0,0,1}},{0,2,1});
    result.meshes.push_back(mesh);
    QuaderImportedLight sun;sun.sourceKey="light/sun";sun.name="Imported sun";sun.intensity=2;sun.castShadows=true;
    sun.color={.53709873f,.73535698f,1.f};result.lights.push_back(sun);
    QuaderImportedLight point;point.sourceKey="light/point";point.name="Imported point";point.point=true;point.radius=7;
    point.intensity=3;point.color={.5f,.7f,1};point.local=math::translation({4,5,6});result.lights.push_back(point);
    return result;
}
void quaderMeshOwnsColliderHierarchy() {
    Project project;SceneDocument document(project.root);const auto owner=document.createQuader();
    QuaderLink link{"C:/authoring/project.quader","project-id","scene-id",1,true,false};
    auto initial=lightSyncFixture("hierarchy-a");
    auto wall=initial.meshes.front();wall.sourceKey="mesh/wall";wall.name="Wall";
    wall.modelId="assets/models/hierarchy-a-wall/manifest.json";wall.local=math::translation({-4,1,2});
    initial.meshes.push_back(wall);
    require(applyQuaderSceneImport(document,owner,link,initial),document.status());
    const auto imported=generatedId(document.scene(),owner,"Imported");
    const std::array keys{"mesh/floor","mesh/wall"};
    const std::array visuals{generatedId(document.scene(),owner,keys[0]),generatedId(document.scene(),owner,keys[1])};
    const std::array collisions{generatedId(document.scene(),owner,std::string(keys[0])+"/collision"),
        generatedId(document.scene(),owner,std::string(keys[1])+"/collision")};
    require(visuals[0]!=visuals[1]&&collisions[0]!=collisions[1],"Distinct Quader source meshes shared a scene entity or collider.");
    const auto hierarchy=[&](const Scene& scene) {
        const auto importedRecord=scene.find(imported);
        require(importedRecord&&importedRecord->parent==owner&&generatedId(scene,owner,"Imported")==imported,
            "Quader/Imported hierarchy or its persistent identity changed.");
        for(size_t index=0;index<keys.size();++index) {
            const auto visualRecord=scene.find(visuals[index]),collisionRecord=scene.find(collisions[index]);
            require(visualRecord&&collisionRecord&&visualRecord->parent==imported&&collisionRecord->parent==visuals[index],
                "Each Quader mesh must remain the real parent of its own collider under Quader/Imported.");
            require(generatedId(scene,owner,keys[index])==visuals[index]&&
                generatedId(scene,owner,std::string(keys[index])+"/collision")==collisions[index],
                "Quader hierarchy replacement changed stable mesh/collider source entity identities.");
            require(collisionRecord->local==Mat4{},"A generated collider left its own mesh-local coordinate system.");
        }
        const auto records=scene.records();const auto rows=sceneOutlinerRows(scene);
        require(rows.size()==records.size(),"Quader hierarchy added synthetic component rows or hid an imported entity.");
        std::unordered_map<EntityId,EntityId> displayedParents;
        for(const auto row:rows)require(displayedParents.emplace(row.entity,row.parent).second,"A Quader entity appeared in more than one Outliner row.");
        for(const auto& record:records)require(displayedParents.contains(record.id)&&displayedParents.at(record.id)==record.parent,
            "The Outliner flattened an actual imported parent/child relationship.");
        for(size_t index=0;index<keys.size();++index)
            require(displayedParents.at(visuals[index])==imported&&displayedParents.at(collisions[index])==visuals[index],
                "Each imported mesh and its own collider must display as nested real entities.");
    };
    hierarchy(document.scene());
    require(document.save("Quader hierarchy"),document.status());SceneDocument loaded(project.root);
    require(loaded.load(document.assetId()),loaded.status());hierarchy(loaded.scene());
    auto replacement=lightSyncFixture("hierarchy-b");replacement.meshes.front().name="Updated floor";
    replacement.meshes.front().local=math::translation({5,6,7});replacement.meshes.front().local.at(0,1)=.25f;
    wall.modelId="assets/models/hierarchy-b-wall/manifest.json";wall.name="Updated wall";
    wall.local=math::translation({-6,2,-3});wall.local.at(2,0)=-.15f;replacement.meshes.push_back(wall);
    require(applyQuaderSceneImport(loaded,owner,link,replacement),loaded.status());hierarchy(loaded.scene());
    for(size_t index=0;index<keys.size();++index)
        require(loaded.scene().find(visuals[index])->local==replacement.meshes[index].local,
            "Reimport did not retain the updated affine pose on the corresponding mesh.");
    require(loaded.save(),loaded.status());SceneDocument reopened(project.root);
    require(reopened.load(loaded.assetId()),reopened.status());hierarchy(reopened.scene());
}
void sourceGroupsNamesAndOrderSurviveRefresh() {
    Project project;SceneDocument document(project.root);const auto owner=document.createQuader();
    QuaderLink link{"C:/authoring/project.quader","project-id","scene-id",1,true,false};
    auto initial=lightSyncFixture("groups-a");
    QuaderImportedGroup room;room.sourceKey="group:100:0";room.name="Room A";room.order=1;
    room.local=math::translation({10,2,-4});room.local.at(0,1)=.25f;
    QuaderImportedGroup walls;walls.sourceKey="group:101:0";walls.parentKey=room.sourceKey;walls.name="Walls";walls.order=2;
    walls.local=math::translation({-2,3,1});walls.local.at(2,0)=-.15f;
    QuaderImportedGroup empty;empty.sourceKey="group:102:0";empty.name="Empty authored group";empty.order=0;
    // Source categories need not arrive in parent-first order.
    initial.groups={walls,room,empty};
    initial.meshes[0].parentKey=room.sourceKey;initial.meshes[0].name="Wall A";initial.meshes[0].order=0;
    auto wall=initial.meshes[0];wall.sourceKey="mesh/wall-c";wall.name="Wall C";wall.parentKey=walls.sourceKey;wall.order=1;
    initial.meshes.push_back(wall);
    initial.lights[0].parentKey=room.sourceKey;initial.lights[0].order=1;initial.lights[1].order=2;
    require(applyQuaderSceneImport(document,owner,link,initial),document.status());
    const auto imported=generatedId(document.scene(),owner,"Imported"),roomId=generatedId(document.scene(),owner,room.sourceKey);
    const auto wallsId=generatedId(document.scene(),owner,walls.sourceKey),emptyId=generatedId(document.scene(),owner,empty.sourceKey);
    const auto floorId=generatedId(document.scene(),owner,"mesh/floor"),wallId=generatedId(document.scene(),owner,wall.sourceKey);
    const auto sunId=generatedId(document.scene(),owner,"light/sun"),pointId=generatedId(document.scene(),owner,"light/point");
    const auto colliderId=generatedId(document.scene(),owner,wall.sourceKey+"/collision");
    require(document.scene().children(imported)==std::vector<EntityId>{emptyId,roomId,pointId}&&
        document.scene().children(roomId)==std::vector<EntityId>{floorId,sunId,wallsId},
        "Quader grouping lost authored sibling order between groups, meshes and lights.");
    require(document.scene().find(roomId)->name==room.name&&document.scene().find(wallsId)->name==walls.name&&
        document.scene().find(wallId)->name==wall.name&&document.scene().find(wallId)->parent==wallsId&&
        document.scene().find(colliderId)->parent==wallId,
        "Quader authored names or nested mesh/collider grouping were flattened.");
    require(document.scene().find(roomId)->local==room.local&&document.scene().find(wallsId)->local==walls.local&&
        document.scene().find(wallId)->local==wall.local,"Nested Quader groups changed their exact authored local matrices.");
    const auto actualWorld=document.scene().worldTransform(wallId);const auto expectedWorld=room.local*walls.local*wall.local;
    require(actualWorld.has_value(),"Nested Quader groups lost their world placement.");
    // Scene walks upward as room * (walls * wall); this independent expected
    // product associates left. Float matrix products need not be bit-identical.
    for(std::size_t index=0;index<16;++index)
        require(std::isfinite((*actualWorld)[index])&&std::abs((*actualWorld)[index]-expectedWorld[index])<=
            1e-5f*std::max(1.f,std::abs(expectedWorld[index])),"Nested Quader groups lost affine placement.");
    const auto rows=sceneOutlinerRows(document.scene());
    require(rows.size()==document.scene().records().size()&&std::any_of(rows.begin(),rows.end(),[&](const auto& row){return row.entity==wallsId&&row.parent==roomId;}),
        "The Outliner did not expose the real imported Quader group.");
    auto authored=*document.scene().find(roomId);authored.visible=false;require(document.updateEntity(roomId,authored),document.status());
    const auto before=snapshot(document);const auto history=document.history().size();
    auto replacement=initial;replacement.sourceKey="groups-b";replacement.groups.erase(replacement.groups.begin());
    replacement.groups[0].name="Renamed / Room";replacement.groups[0].local=math::translation({20,4,-8});
    replacement.meshes.pop_back();replacement.meshes[0].parentKey=empty.sourceKey;replacement.meshes[0].name="Renamed Wall A";
    replacement.meshes[0].local=math::translation({8,6,4});
    require(applyQuaderSceneImport(document,owner,link,replacement),document.status());
    require(document.history().size()==history+1&&generatedId(document.scene(),owner,room.sourceKey)==roomId&&
        generatedId(document.scene(),owner,empty.sourceKey)==emptyId&&generatedId(document.scene(),owner,"mesh/floor")==floorId,
        "Source group rename/reparenting changed matching persistent identities or split the import transaction.");
    require(document.scene().find(roomId)->name=="Renamed / Room"&&!document.scene().find(roomId)->visible&&
        document.scene().find(floorId)->name=="Renamed Wall A"&&document.scene().find(floorId)->parent==emptyId&&
        document.scene().find(floorId)->local==replacement.meshes[0].local,
        "Quader refresh did not apply exact source group names, parenting and placement while retaining authored visibility.");
    require(!document.scene().contains(wallsId)&&!document.scene().contains(wallId)&&!document.scene().contains(colliderId),
        "Removed Quader source groups/meshes or their collider children survived refresh.");
    const auto after=snapshot(document);
    require(document.undo()&&snapshot(document)==before&&document.scene().find(wallId)->parent==wallsId,
        "Grouped Quader refresh undo lost removed hierarchy, names or entity identities.");
    require(document.redo()&&snapshot(document)==after,"Grouped Quader refresh redo lost the replacement hierarchy.");
    auto invalid=replacement;invalid.meshes[0].parentKey="group/missing";
    require(!applyQuaderSceneImport(document,owner,link,invalid)&&snapshot(document)==after,
        "A missing imported group partially applied or silently flattened Quader hierarchy.");
    invalid=replacement;invalid.groups[0].parentKey=invalid.groups[1].sourceKey;invalid.groups[1].parentKey=invalid.groups[0].sourceKey;
    require(!applyQuaderSceneImport(document,owner,link,invalid)&&snapshot(document)==after,
        "A cyclic imported hierarchy partially applied a Quader refresh.");
    require(document.save("Quader source groups"),document.status());SceneDocument loaded(project.root);
    require(loaded.load(document.assetId())&&snapshot(loaded)==snapshot(document),"Quader source hierarchy/names did not survive scene save and reopen.");
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
    const auto authoredColor=document.scene().find(sun)->directional->color;
    require(authoredColor==initial.lights[0].color,"Scene publication encoded an already prepared Quader light color again.");
    const auto decode=[](float value){return value<=.04045f?value/12.92f:std::pow((value+.055f)/1.055f,2.4f);};
    require(std::abs(decode(authoredColor[0])-.25f)<1e-6f&&std::abs(decode(authoredColor[1])-.5f)<1e-6f,
        "Prepared Quader light colors did not restore quarter/half linear radiance through the renderer's decode.");
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
        document.scene().find(collision)->collider->source.empty()&&document.scene().find(collision)->collider->mesh==replacement.meshes[0].collision,
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
void nativeCollisionHistoryAndRefresh() {
    Project project;SceneDocument document(project.root);const auto owner=document.createQuader();
    QuaderLink link{"C:/authoring/project.quader","project-id","scene-id",1,false,false};
    auto initial=lightSyncFixture("native-a");
    require(applyQuaderSceneImport(document,owner,link,initial),document.status());
    const auto collision=generatedId(document.scene(),owner,"mesh/floor/collision");
    auto record=document.scene().find(collision);
    require(record&&record->collider&&record->collider->shape==ColliderShape::Trimesh&&record->collider->source.empty()&&
        !record->model&&sameCollisionMesh(record->collider->mesh,initial.meshes[0].collision),
        "Imported collision did not become a native Trimesh entity without a model asset reference.");
    auto authored=*record;authored.visible=false;authored.local=math::translation({.2f,.3f,.4f});
    authored.collider->shape=ColliderShape::Capsule;authored.collider->radius=.7f;authored.collider->height=2.4f;
    authored.collider->showCollisionInGame=true;authored.collider->collisionColor={.2f,.4f,.6f,.8f};
    require(document.updateEntity(collision,authored),document.status());
    const auto before=snapshot(document);
    auto replacement=lightSyncFixture("native-b");
    replacement.meshes[0].collision=makeCollisionMesh({{0,0,0},{2,0,0},{0,0,3}},{0,2,1});
    require(applyQuaderSceneImport(document,owner,link,replacement),document.status());
    auto expected=*authored.collider;expected.mesh=replacement.meshes[0].collision;
    record=document.scene().find(collision);
    require(record&&record->collider==expected&&!record->visible&&record->local==authored.local,
        "Native collision refresh lost primitive overrides, visibility or the new exact source payload.");
    const auto after=snapshot(document);
    require(document.undo()&&snapshot(document)==before&&sameCollisionMesh(document.scene().find(collision)->collider->mesh,initial.meshes[0].collision),
        "Undo did not retain the previous immutable native collision mesh.");
    require(document.redo()&&snapshot(document)==after,"Redo did not restore refreshed native collision data.");
    auto trimesh=*document.scene().find(collision);trimesh.collider->shape=ColliderShape::Trimesh;
    require(document.updateEntity(collision,trimesh),document.status());
    require(sameCollisionMesh(document.scene().find(collision)->collider->mesh,replacement.meshes[0].collision),
        "Switching a primitive override back to Trimesh lost its refreshed native triangles.");
    for(const auto& id:document.historicalModelReferences())require(id.find("collision")==std::string::npos,
        "Native collision introduced a browsable or historical model asset reference.");
    require(document.save("Native Quader collision"),document.status());SceneDocument loaded(project.root);
    require(loaded.load(document.assetId())&&snapshot(loaded)==snapshot(document)&&
        sameCollisionMesh(loaded.scene().find(collision)->collider->mesh,replacement.meshes[0].collision)&&
        loaded.scene().find(collision)->collider->source.empty(),"Native collision triangles did not survive scene save/reopen exactly.");
}
void authoredVisibilitySurvivesSourceSync() {
    Project project;SceneDocument document(project.root);const auto owner=document.createQuader();
    QuaderLink link{"C:/authoring/project.quader","project-id","scene-id",1,true,false};
    require(applyQuaderSceneImport(document,owner,link,lightSyncFixture("visibility-a")),document.status());
    const auto imported=generatedId(document.scene(),owner,"Imported"),visual=generatedId(document.scene(),owner,"mesh/floor");
    const auto collision=generatedId(document.scene(),owner,"mesh/floor/collision"),sun=generatedId(document.scene(),owner,"light/sun");
    const auto point=generatedId(document.scene(),owner,"light/point");
    require(imported&&visual&&collision&&sun&&point,"Visibility fixture lost a generated entity.");
    for(const auto id:{imported,visual,collision,sun,point})
        require(document.scene().find(id)->visible,"New generated entities should be visible by default.");
    for(const auto id:{imported,visual,sun}) {
        auto record=*document.scene().find(id);record.visible=false;
        require(document.updateEntity(id,record,"Hide generated entity"),document.status());
    }
    require(!document.scene().isVisible(collision)&&document.scene().find(collision)->visible,
        "Hiding an imported group changed its child's authored visibility or failed to hide it.");
    auto hiddenCollision=*document.scene().find(collision);hiddenCollision.visible=false;
    require(document.updateEntity(collision,hiddenCollision,"Hide generated collider"),document.status());
    const auto manual=document.createEmpty("Manual sibling");document.select({manual},manual);
    require(document.reparentSelection(owner),document.status());
    auto manualRecord=*document.scene().find(manual);manualRecord.visible=false;
    require(document.updateEntity(manual,manualRecord),document.status());
    const auto before=snapshot(document);const auto history=document.history().size();
    auto replacement=lightSyncFixture("visibility-b");
    auto added=replacement.meshes.front();added.sourceKey="mesh/new";added.name="New visible mesh";
    replacement.meshes.push_back(added);
    require(applyQuaderSceneImport(document,owner,link,replacement),document.status());
    require(document.history().size()==history+1,"Visibility-preserving refresh should be one history command.");
    for(const auto id:{imported,visual,collision,sun})
        require(!document.scene().find(id)->visible,"Quader refresh reset an authored hidden group, mesh, collider or light.");
    require(document.scene().find(point)->visible,"Quader refresh changed a matching child's authored visible state.");
    require(!document.scene().isVisible(collision)&&!document.scene().isVisible(point)&&!document.scene().find(manual)->visible,
        "Quader refresh lost inherited hiding or changed a manual sibling.");
    const auto addedId=generatedId(document.scene(),owner,"mesh/new");
    require(addedId&&document.scene().find(addedId)->visible&&!document.scene().isVisible(addedId),
        "A new mesh under a hidden imported group should retain its visible authored default.");
    const auto after=snapshot(document);
    require(document.undo()&&snapshot(document)==before,"Quader visibility refresh undo lost authored states.");
    require(document.redo()&&snapshot(document)==after,"Quader visibility refresh redo lost authored states.");
    auto group=*document.scene().find(imported);group.visible=true;require(document.updateEntity(imported,group),document.status());
    require(document.scene().isVisible(point)&&document.scene().isVisible(addedId)&&!document.scene().isVisible(visual)&&
        !document.scene().isVisible(collision),"Showing a refreshed group did not restore its children's independent authored states.");
    require(document.save("Quader visibility"),document.status());SceneDocument loaded(project.root);
    require(loaded.load(document.assetId())&&snapshot(loaded)==snapshot(document)&&!loaded.scene().isVisible(collision)&&
        loaded.scene().isVisible(point),"Quader authored and inherited visibility did not survive save/reopen.");
}
void sourceHiddenMeshesRemainEditable() {
    Project project;SceneDocument document(project.root);const auto owner=document.createQuader();
    QuaderLink link{"C:/authoring/project.quader","project-id","scene-id",1,true,false};
    auto initial=lightSyncFixture("hidden-a");initial.meshes[0].visible=false;
    auto empty=initial.meshes[0];empty.sourceKey="mesh/empty";empty.modelId.clear();empty.collision.reset();
    initial.meshes.push_back(empty);
    require(applyQuaderSceneImport(document,owner,link,initial),document.status());
    const auto visual=generatedId(document.scene(),owner,"mesh/floor"),emptyId=generatedId(document.scene(),owner,"mesh/empty");
    const auto hidden=document.scene().find(visual);
    require(hidden&&hidden->model&&hidden->model->id==initial.meshes[0].modelId&&!hidden->visible&&!document.scene().isVisible(visual),
        "A source-hidden Quader mesh lost its prepared model reference instead of using entity visibility.");
    require(emptyId&&!document.scene().find(emptyId)->model&&!document.scene().find(emptyId)->visible,
        "A source-hidden mesh without prepared geometry synthesized a model reference.");
    auto shown=*hidden;shown.visible=true;require(document.updateEntity(visual,shown,"Show imported mesh"),document.status());
    require(document.scene().isVisible(visual),"An imported source-hidden mesh could not be shown using its entity property.");
    auto replacement=lightSyncFixture("hidden-b");replacement.meshes[0].visible=false;
    require(applyQuaderSceneImport(document,owner,link,replacement),document.status());
    const auto refreshed=document.scene().find(visual);
    require(refreshed&&refreshed->visible&&document.scene().isVisible(visual)&&refreshed->model&&
        refreshed->model->id==replacement.meshes[0].modelId,"Source-hidden refresh reset Crimson visibility or lost current geometry.");
}
void directionalCompatibilityAndMeshDefaults() {
    Project project;SceneDocument document(project.root);const auto sun=document.createDirectionalLight();
    require(document.scene().find(sun)->directional->castShadows,"New directional lights must enable shadows.");
    auto serialized=nlohmann::json::parse(snapshot(document));require(serialized.at("version")==23,"New scenes did not use schema 23.");
    serialized["version"]=8;
    for(auto& entity:serialized["entities"])if(entity.contains("directional")) {
        entity["directional"].erase("castShadows");entity["directional"].erase("shadowDistance");
        entity["directional"].erase("shadowDepthBias");entity["directional"].erase("shadowNormalBias");
        entity["directional"].erase("shadowAngularDiameter");
    }
    const auto id="assets/scenes/legacy.crimson-scene";fs::create_directories(project.root/"assets/scenes");
    {std::ofstream output(project.root/id,std::ios::binary);output<<serialized.dump();}
    const auto legacy=readSceneSnapshot(project.root,id);
    require(legacy.records.size()==1&&legacy.records[0].directional&&!legacy.records[0].directional->castShadows&&
        legacy.records[0].directional->shadowDistance==100&&legacy.records[0].directional->shadowAngularDiameter==.53f,
        "Old directional lights gained shadows or invalid shadow defaults.");
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
    auto model=*document.scene().find(visual);model.lightingScopes={lighting_scope::World,0};model.castShadows=false;
    require(document.updateEntity(visual,model),document.status());
    auto lamp=*document.scene().find(point);lamp.point->roles={lighting_scope::Character,lighting_scope::World};
    lamp.point->intensity=99;lamp.point->radius=99;lamp.point->castShadows=true;lamp.point->indirectEnergy=2.5f;lamp.local=math::translation({99,99,99});
    require(document.updateEntity(point,lamp),document.status());
    auto directional=*document.scene().find(sun);directional.directional->roles={lighting_scope::World,lighting_scope::Character};
    directional.directional->castShadows=false;directional.directional->indirectEnergy=0;directional.directional->shadowAngularDiameter=1.5f;
    require(document.updateEntity(sun,directional),document.status());
    const auto before=snapshot(document);
    auto replacement=lightSyncFixture("lighting-b");replacement.lights[1].intensity=11;replacement.lights[1].radius=17;
    replacement.lights[0].intensity=5;replacement.lights[0].color={.25f,.5f,.75f};replacement.lights[1].color={1,.25f,.1f};
    replacement.lights[1].local=math::translation({5,7,9});
    auto added=replacement.lights[1];added.sourceKey="light/new-point";replacement.lights.push_back(added);
    require(applyQuaderSceneImport(document,owner,link,replacement),document.status());
    require(generatedId(document.scene(),owner,"mesh/floor")==visual && generatedId(document.scene(),owner,"light/point")==point &&
        generatedId(document.scene(),owner,"light/sun")==sun,"Role-preserving import changed persistent source identities.");
    require(document.scene().find(visual)->lightingScopes==model.lightingScopes && document.scene().find(visual)->castShadows &&
        document.scene().find(visual)->model->id==replacement.meshes[0].modelId,"Source sync lost Crimson scopes or retained unrelated source-owned model fields.");
    require(document.scene().find(point)->point->roles==lamp.point->roles && document.scene().find(point)->point->indirectEnergy==2.5f &&
        document.scene().find(point)->point->intensity==11 && document.scene().find(point)->point->color==replacement.lights[1].color &&
        document.scene().find(point)->point->radius==17 && !document.scene().find(point)->point->castShadows &&
        document.scene().find(point)->local==replacement.lights[1].local,"Point role/GI preservation blocked source light/transform synchronization.");
    require(document.scene().find(sun)->directional->roles==directional.directional->roles && document.scene().find(sun)->directional->indirectEnergy==0 &&
        document.scene().find(sun)->directional->shadowAngularDiameter==1.5f &&
        document.scene().find(sun)->directional->intensity==5 && document.scene().find(sun)->directional->color==replacement.lights[0].color &&
        document.scene().find(sun)->directional->castShadows,"Directional role/GI preservation lost authored values or blocked source lighting/shadow synchronization.");
    const auto fresh=generatedId(document.scene(),owner,"light/new-point");
    require(fresh && document.scene().find(fresh)->point->roles==ScenePointLight{}.roles && document.scene().find(fresh)->point->indirectEnergy==1,
        "A new source inherited another light's custom roles or GI energy.");
    const auto after=snapshot(document);require(document.undo() && snapshot(document)==before,"Role-preserving import undo was not exact.");
    require(document.redo() && snapshot(document)==after,"Role-preserving import redo lost scopes or source fields.");
    require(document.save("Scoped bridge lighting"),document.status());SceneDocument loaded(project.root);
    require(loaded.load(document.assetId()) && snapshot(loaded)==snapshot(document),"Source-synchronized Crimson roles did not survive save/reopen.");
    const auto beforeKinds=snapshot(document);replacement.lights[1].point=false;replacement.lights[0].point=true;replacement.lights[0].radius=9;
    require(applyQuaderSceneImport(document,owner,link,replacement),document.status());
    require(document.scene().find(point)->directional && !document.scene().find(point)->point &&
        document.scene().find(point)->directional->roles==SceneDirectionalLight{}.roles && document.scene().find(point)->directional->indirectEnergy==1 &&
        document.scene().find(point)->directional->shadowAngularDiameter==.53f &&
        document.scene().find(sun)->point && !document.scene().find(sun)->directional && document.scene().find(sun)->point->roles==ScenePointLight{}.roles &&
        document.scene().find(sun)->point->indirectEnergy==1,"Changing a source light kind retained incompatible previous-kind roles or GI energy.");
    const auto afterKinds=snapshot(document);require(document.undo()&&snapshot(document)==beforeKinds,"Light-kind sync undo lost previous GI energy.");
    require(document.redo()&&snapshot(document)==afterKinds,"Light-kind sync redo lost reset GI energy.");
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
    auto ambiguous=lightSyncFixture("slots-ambiguous");sources[ambiguous.meshes[0].modelId]={b,b,c};
    require(applyQuaderSceneImport(document,owner,link,ambiguous,query)&&document.scene().find(visual)->modelMaterials.empty()&&
        document.scene().find(visual)->model->id==ambiguous.meshes[0].modelId,"An ambiguous source slot blocked sync or retained its stale override.");
    entity=*document.scene().find(visual);setModelMaterialOverride(entity.modelMaterials,2,replacement);require(document.updateEntity(visual,entity),document.status());
    auto removed=lightSyncFixture("slots-removed");sources[removed.meshes[0].modelId]={a,b};
    require(applyQuaderSceneImport(document,owner,link,removed,query)&&document.scene().find(visual)->modelMaterials.empty()&&
        document.scene().find(visual)->model->id==removed.meshes[0].modelId,"A removed source slot blocked sync or retained its stale override.");
    auto repeated=lightSyncFixture("slots-repeated-a");sources[repeated.meshes[0].modelId]={b,b,c};
    require(applyQuaderSceneImport(document,owner,link,repeated,query),document.status());
    entity=*document.scene().find(visual);setModelMaterialOverride(entity.modelMaterials,1,replacement);require(document.updateEntity(visual,entity),document.status());
    auto repeatedNext=lightSyncFixture("slots-repeated-b");sources[repeatedNext.meshes[0].modelId]={b,b,c};
    require(applyQuaderSceneImport(document,owner,link,repeatedNext,query) && document.scene().find(visual)->modelMaterials==entity.modelMaterials,
        "Unchanged repeated source slots lost their numbered instance assignments.");
    auto repeatedReordered=lightSyncFixture("slots-repeated-reordered");sources[repeatedReordered.meshes[0].modelId]={c,b,b};
    require(applyQuaderSceneImport(document,owner,link,repeatedReordered,query)&&document.scene().find(visual)->modelMaterials.empty(),
        "Changed repeated source IDs blocked sync or guessed an ambiguous numbered assignment.");

    const std::string d="assets/materials/source-d.crimson-material";
    const AssetRef<MaterialAsset> survivorA{"assets/materials/instance-a.crimson-material"},survivorD{"assets/materials/instance-d.crimson-material"};
    auto mixedBefore=lightSyncFixture("slots-mixed-before");sources[mixedBefore.meshes[0].modelId]={a,b,c,d};
    require(applyQuaderSceneImport(document,owner,link,mixedBefore,query),document.status());
    entity=*document.scene().find(visual);
    setModelMaterialOverride(entity.modelMaterials,0,survivorA);setModelMaterialOverride(entity.modelMaterials,1,replacement);
    setModelMaterialOverride(entity.modelMaterials,2,replacement);setModelMaterialOverride(entity.modelMaterials,3,survivorD);
    setModelMaterialOverride(entity.modelMaterials,6,replacement); // Authored before source metadata was available.
    require(document.updateEntity(visual,entity),document.status());const auto stale=snapshot(document);const auto history=document.history().size();
    auto mixedAfter=lightSyncFixture("slots-mixed-after");sources[mixedAfter.meshes[0].modelId]={c,d,c,a};
    require(applyQuaderSceneImport(document,owner,link,mixedAfter,query),document.status());
    ModelMaterialOverrides expected;setModelMaterialOverride(expected,1,survivorD);setModelMaterialOverride(expected,3,survivorA);
    require(document.scene().find(visual)->modelMaterials==expected&&document.history().size()==history+1&&!document.transactionActive(),
        "Mixed sync did not preserve unique remaps while dropping removed, ambiguous and out-of-range overrides in one command.");
    const auto cleaned=snapshot(document);
    require(document.undo()&&snapshot(document)==stale,"Undo did not restore the exact stale slot assignments and former source package.");
    require(document.redo()&&snapshot(document)==cleaned,"Redo did not drop stale assignments and restore valid remaps atomically.");
    const auto rejectsUnknown=[&](const QuaderImportResult& result) {
        const auto unchanged=snapshot(document);const auto count=document.history().size();
        require(!applyQuaderSceneImport(document,owner,link,result,query)&&snapshot(document)==unchanged&&
            document.history().size()==count&&!document.transactionActive(),"Unknown source metadata was treated as confirmed stale assignments or partially replaced the scene.");
        require(document.status().find("source material slots")!=std::string::npos,"Unavailable metadata failure lost its diagnostic.");
    };
    rejectsUnknown(lightSyncFixture("slots-unavailable"));
    auto missingBefore=lightSyncFixture("slots-known-after");sources[missingBefore.meshes[0].modelId]={a,b,c,d};
    sources.erase(mixedAfter.meshes[0].modelId);rejectsUnknown(missingBefore);sources[mixedAfter.meshes[0].modelId]={c,d,c,a};
    require(document.save("Cleaned source slots"),document.status());SceneDocument loaded(project.root);
    require(loaded.load(document.assetId())&&snapshot(loaded)==snapshot(document)&&loaded.scene().find(visual)->modelMaterials==expected,
        "Automatically cleaned and remapped source slots did not survive save/reopen.");
}
void singleMaterialLegacyFallbackSync() {
    Project project;SceneDocument document(project.root);const auto owner=document.createQuader();
    QuaderLink link{"C:/authoring/project.quader","project-id","scene-id",1,true,false};
    const std::string sourceMaterial="assets/materials/one-quader-material.crimson-material";
    const AssetRef<MaterialAsset> authoredMaterial{"assets/materials/authored-instance.crimson-material"};
    const AssetRef<MaterialAsset> unusedOverride{"assets/materials/old-unused-slot.crimson-material"};
    auto initial=lightSyncFixture("one-material-legacy");
    auto legacy=std::make_shared<ModelAsset>();legacy->id=initial.meshes.front().modelId;
    legacy->materials.resize(1);legacy->materialIds={sourceMaterial};legacy->legacyUnusedMaterialSlot=1;
    initial.preparedModels.push_back(legacy);
    const ModelMaterialSourceQuery query=[&](const std::string& id)->std::optional<std::vector<std::string>> {
        return id==legacy->id?std::optional{legacy->materialIds}:std::nullopt;
    };
    require(applyQuaderSceneImport(document,owner,link,initial,query),document.status());
    const auto visual=generatedId(document.scene(),owner,"mesh/floor");
    auto entity=*document.scene().find(visual);entity.modelMaterials={authoredMaterial,unusedOverride};
    require(document.updateEntity(visual,entity),document.status());
    auto unchanged=initial;unchanged.sourceKey="same-geometry-refresh";unchanged.meshes.front().local=math::translation({4,5,6});
    require(applyQuaderSceneImport(document,owner,link,unchanged,query),document.status());
    require(document.scene().find(visual)->model->id==legacy->id&&document.scene().find(visual)->modelMaterials==entity.modelMaterials,
        "Reusing an immutable legacy package discarded the original real or unused slot assignment.");
    const auto before=snapshot(document);
    auto changed=lightSyncFixture("one-material-current");
    auto current=std::make_shared<ModelAsset>();current->id=changed.meshes.front().modelId;
    current->materials.resize(1);current->materialIds={sourceMaterial};changed.preparedModels.push_back(current);
    require(applyQuaderSceneImport(document,owner,link,changed,query),document.status());
    const ModelMaterialOverrides expected{authoredMaterial};
    require(generatedId(document.scene(),owner,"mesh/floor")==visual&&document.scene().find(visual)->model->id==current->id&&
        document.scene().find(visual)->modelMaterials==expected,
        "A changed one-material Quader mesh lost its real override or carried a nonexistent legacy fallback into the new package.");
    const auto after=snapshot(document);
    require(document.undo()&&snapshot(document)==before&&document.scene().find(visual)->modelMaterials==entity.modelMaterials,
        "Undo did not restore the old immutable package and its exact authored override vector.");
    require(document.redo()&&snapshot(document)==after,"Redo did not preserve the one real material assignment after replacing legacy geometry.");
    require(document.save("Single-material Quader compatibility"),document.status());SceneDocument loaded(project.root);
    require(loaded.load(document.assetId())&&!loaded.dirty()&&loaded.scene().find(visual)->modelMaterials==expected&&snapshot(loaded)==snapshot(document),
        "One-material Quader package replacement changed real assignments during save/reopen.");
    SceneDocument runtime(project.root);require(runtime.resetTransientFrom(loaded)&&runtime.scene().find(visual)->modelMaterials==expected,
        "Play isolation lost the real material assignment after legacy fallback removal.");
}
void bundleMigrationPreservesSceneHierarchyAndAssignments() {
    using Json=nlohmann::json;
    Project project;initializeContentStorage(project.root);SceneDocument document(project.root);const auto owner=document.createQuader();
    QuaderSceneBundle bundle;bundle.projectId="project-id";bundle.sceneId="scene-id";bundle.name="Room";
    bundle.id=quaderSceneBundleId(bundle.projectId,bundle.sceneId);
    QuaderBundleModel model;model.legacyManifest="assets/models/quader-scene-storage/manifest.json";
    const auto legacyDirectory=fs::path(model.legacyManifest).parent_path();
    const std::array<const char*,4> members{"manifest.json","model.gltf","source.gltf","buffers/0.bin"};
    for(std::size_t index=0;index<members.size();++index)model.memberIds[index]=canonicalAssetId((legacyDirectory/members[index]).generic_string());
    model.id=model.memberIds[0];model.buffer="mesh";
    model.gltf=Json{{"asset",{{"version","2.0"}}},{"buffers",Json::array({{{"uri","buffers/0.bin"},{"byteLength",model.buffer.size()}}})}}.dump();
    model.manifest=Json{{"version",1},{"name","Floor"},{"format","Quader"},{"document","model.gltf"},{"original","source.gltf"},
        {"materialAssets",Json::array({DefaultMaterialId})},{"dependencies",Json::array({{{"kind","buffer"},{"index",0},{"path","buffers/0.bin"}}})},
        {"quader",{{"project",bundle.projectId},{"scene",bundle.sceneId},{"collision",false}}}}.dump();
    const std::array<std::string,4> contents{model.manifest,model.gltf,model.gltf,model.buffer};
    std::vector<StoredAssetPath> locations;
    for(std::size_t index=0;index<members.size();++index) {
        const auto legacy=(legacyDirectory/members[index]).generic_string();const auto file=resolveAssetPath(project.root,legacy);
        writeFileAtomically(file,contents[index]);locations.push_back({legacy,file});
    }
    registerAssetPaths(project.root,locations);
    auto imported=lightSyncFixture("bundle-storage");imported.meshes.front().modelId=model.id;
    QuaderLink link{"C:/unavailable/source/project.qproj",bundle.projectId,bundle.sceneId,1,false,false};
    require(applyQuaderSceneImport(document,owner,link,imported),document.status());
    const auto visual=generatedId(document.scene(),owner,"mesh/floor");
    const auto collider=generatedId(document.scene(),owner,"mesh/floor/collision");
    const ModelMaterialOverrides assignments{AssetRef<MaterialAsset>{canonicalAssetId("assets/materials/custom-floor.crimson-material")},
        AssetRef<MaterialAsset>{canonicalAssetId("assets/materials/old-unused-slot.crimson-material")}};
    auto edited=*document.scene().find(visual);edited.modelMaterials=assignments;
    require(document.updateEntity(visual,edited)&&document.save("Bundled room"),document.status());
    const auto before=snapshot(document);
    const auto read=[](const fs::path& file){std::ifstream input(file,std::ios::binary);return std::string{std::istreambuf_iterator<char>(input),{}};};
    const auto sceneFile=resolveAssetPath(project.root,document.assetId());const auto saved=read(sceneFile);
    initializeContentStorage(project.root);
    const auto migrated=findQuaderSceneBundle(project.root,bundle.projectId,bundle.sceneId);
    require(migrated&&migrated->second.models.size()==1&&migrated->second.models.front().id==model.id,
        "Legacy scene import did not migrate to one bundle with its original model identity.");
    require(snapshot(document)==before&&read(sceneFile)==saved&&document.scene().find(collider)->parent==visual&&
        document.scene().find(visual)->modelMaterials==assignments,
        "Storage migration changed scene bytes, mesh/collider hierarchy, IDs or authored material assignments.");
    require(!fs::exists(locations.front().file.parent_path()),"Migration retained the loose authored mesh package.");
    const auto cache=fs::weakly_canonical(project.root/".crimson/cache");
    require(cache.parent_path()==fs::weakly_canonical(project.root/".crimson"),"Test cache cleanup escaped the disposable project.");
    fs::remove_all(cache);initializeContentStorage(project.root);
    require(read(sceneFile)==saved&&read(resolveAssetPath(project.root,model.id))==model.manifest,
        "Cache regeneration lost a referenced model or rewrote the saved scene.");
    SceneDocument reopened(project.root);require(reopened.load(document.assetId())&&!reopened.dirty()&&snapshot(reopened)==before,
        "Reopening regenerated geometry changed the imported scene.");
    require(reopened.scene().find(collider)->parent==visual&&reopened.scene().find(visual)->modelMaterials==assignments&&
        sameCollisionMesh(reopened.scene().find(collider)->collider->mesh,imported.meshes.front().collision),
        "Bundle regeneration lost individual mesh ownership, native collision or material overrides.");
    SceneDocument runtime(project.root);require(runtime.resetTransientFrom(reopened)&&runtime.scene().find(visual)->modelMaterials==assignments&&
        runtime.scene().find(collider)->parent==visual,"Play copying changed regenerated bundle references or hierarchy.");
    require(document.undo()&&document.scene().find(visual)->modelMaterials.empty()&&document.redo()&&snapshot(document)==before,
        "Bundle storage migration invalidated existing material-assignment undo/redo.");
}
}
int main(){try{reimportIdentityHistoryAndAtomicFailure();quaderMeshOwnsColliderHierarchy();sourceGroupsNamesAndOrderSurviveRefresh();independentLightSyncPolicy();nativeCollisionHistoryAndRefresh();authoredVisibilitySurvivesSourceSync();sourceHiddenMeshesRemainEditable();directionalCompatibilityAndMeshDefaults();clusteredPointLightSceneImport();crimsonLightingRolesSurviveSourceSync();materialSlotsSurviveSourceSync();singleMaterialLegacyFallbackSync();bundleMigrationPreservesSceneHierarchyAndAssignments();std::cout<<"Quader scene identity, authored groups/names/order, mesh/collider hierarchy, native collision, atomic reimport, visibility, independent light sync, history and compatibility checks passed.\n";return 0;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
