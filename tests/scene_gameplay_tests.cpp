#include "scene/SceneDocument.h"
#include "scene/SceneExtraction.h"
#include "scene/BuiltinModels.h"
#include "assets/MaterialAsset.h"
#include "math/Json.h"
#include "crimson-renderer/shaders/point_light.sh"
#include "crimson-renderer/shaders/ambient_visibility_response.sh"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <utility>

namespace {
using namespace crimson;
using Json=nlohmann::json;
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
void near(const Vec3& a,const Vec3& b){require(math::length(a-b)<.0002,"Unexpected position.");}
Vec3 position(const Scene& scene,EntityId id){return math::transformPoint(*scene.worldTransform(id),{0,0,0});}
struct TempProject {
    std::filesystem::path root;
    TempProject(){root=std::filesystem::temp_directory_path()/("crimson-gameplay-scene-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(root);}
    ~TempProject(){std::error_code error;const auto target=std::filesystem::weakly_canonical(root,error);const auto parent=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),error);
        if(!error && target.parent_path()==parent && target.filename().string().starts_with("crimson-gameplay-scene-"))std::filesystem::remove_all(target,error);}
};
void pose(SceneDocument& document,EntityId id,const Vec3& location){
    auto matrix=document.scene().find(id)->local;matrix=*math::withAffinePosition(matrix,location);
    const std::array updates{ScenePoseUpdate{id,matrix}};require(document.applyRuntimePoses(updates),document.status());
}
void move(SceneDocument& document,EntityId id,const Vec3& location){auto record=*document.scene().find(id);record.local=*math::withAffinePosition(record.local,location);require(document.updateEntity(id,record),document.status());}
void same(const std::vector<EntityRecord>& a,const std::vector<EntityRecord>& b){require(a.size()==b.size(),"Record count changed.");for(size_t i=0;i<a.size();++i)require(sameEntity(a[i],b[i]),"Authored records changed.");}

EntityId addControlledActor(SceneDocument& document,const std::string& name="Actor") {
    if(!document.begin("Compose controlled actor"))return NoEntity;
    EntityRecord record;record.name=name;record.actor.emplace();record.controller.emplace();record.motor.emplace();
    record.navigation.emplace();record.playerInput.emplace();record.lightRig.emplace();record.darknessReveal.emplace();
    if(!document.update([&](auto& records,auto& properties){
        record.id=1;for(const auto& current:records)record.id=std::max(record.id,current.id+1);
        for(const auto& current:records)if(!current.parent)record.order=std::max(record.order,current.order+1);
        records.push_back(record);if(!properties.playerCharacter)properties.playerCharacter=record.id;
    })){document.cancel();return NoEntity;}
    document.select({record.id},record.id);if(!document.commit())return NoEntity;return record.id;
}
Json legacyComposition(Json data,const NavigationAgentProfileQuery& profiles={}) {
    data["version"]=15;
    for(auto& entity:data["entities"]) {
        if(entity.contains("controller")) {
            auto character=entity.at("controller");const auto profile=resolveNavigationAgentProfile(character.at("profile").get<std::string>(),profiles);
            require(profile!=nullptr,"Legacy fixture profile is unavailable.");character["height"]=profile->traversal.height;character["radius"]=profile->traversal.radius;character.erase("profile");character["moveSpeed"]=entity.value("motor",Json::object()).value("moveSpeed",5.f);
            character["showRoute"]=entity.value("navigation",Json::object()).value("showRoute",true);
            if(entity.contains("lightRig"))character["lightRig"]=entity.at("lightRig");
            if(entity.contains("darknessReveal"))character["darknessReveal"]=entity.at("darknessReveal");
            entity["character"]=std::move(character);
        }
        for(const auto* field:{"actor","motor","controller","navigation","playerInput","lightRig","darknessReveal"})entity.erase(field);
        if(entity.contains("camera")) {
            const auto follow=entity.value("cameraFollow",Json::object());
            entity["camera"]["followTarget"]=follow.value("target",Json("0000000000000000"));
            entity["camera"]["followOffset"]=follow.value("offset",Json(Vec3{}));entity.erase("cameraFollow");
        }
    }
    return data;
}

void composedPlayerRecipeAndMigration() {
    TempProject project;SceneDocument document(project.root);
    const auto player=document.createPlayer("Hero");require(player&&document.history().size()==1,document.status());
    const auto root=*document.scene().find(player);const auto children=document.scene().children(player);
    require(!root.parent&&!root.model&&root.actor&&root.motor&&root.controller&&root.navigation&&root.playerInput&&root.lightRig&&root.darknessReveal,
        "Create Player did not compose independent capabilities on a generic root.");
    require(children.size()==1&&document.selection().ids==std::vector<EntityId>{player}&&document.selection().primary==player,
        "Create Player did not create one real child camera and select its root.");
    const auto camera=children.front();const auto child=*document.scene().find(camera);
    require(child.camera&&child.camera->verticalFov==35&&child.cameraFollow&&child.cameraFollow->target==player&&
        child.cameraFollow->offset==Vec3{6.5f,13.1f,6.5f}&&document.scene().properties().activeCamera==camera&&document.scene().properties().playerCharacter==player,
        "Create Player camera lens, follow settings or explicit roles are incorrect.");
    constexpr float radians=3.14159265358979323846f/180.f;
    const auto expectedBasis=math::eulerRotationXYZ(Vec3{-55,45,0}*radians);
    require(child.cameraFollow->worldBasis==expectedBasis,"Create Player lost the authored ARPG camera rotation.");
    const auto created=document.scene().records();
    require(document.undo()&&document.scene().records().empty()&&!document.scene().properties().activeCamera&&!document.scene().properties().playerCharacter,
        "Create Player did not undo its entire recipe in one command.");
    require(document.redo(),document.status());same(created,document.scene().records());
    const auto second=document.createPlayer("Second");require(second&&document.scene().properties().playerCharacter==player&&document.scene().properties().activeCamera==camera,
        "A second Player stole an existing controlled entity or active camera.");
    const auto thirdCamera=document.createCamera("Chosen camera");require(document.setActiveCamera(thirdCamera),document.status());
    const auto third=document.createPlayer("Third");require(third&&document.scene().properties().activeCamera==thirdCamera,
        "Create Player replaced an explicitly active camera.");
    const auto viewBefore=*document.scene().worldTransform(camera);
    Scene runtime;runtime.apply(document.scene().records(),document.scene().properties());
    const auto moved=math::translation({5,2,-3})*math::eulerRotationXYZ({0,1.2f,0})*math::scaling({2,1.5f,.7f});
    const std::array poses{ScenePoseUpdate{player,moved}};runtime.applyRuntimePoses(poses);
    near(position(runtime,camera),Vec3{5,2,-3}+child.cameraFollow->offset);
    const auto runtimeWorld=*runtime.worldTransform(camera);
    for(unsigned axis=0;axis<3;++axis)near(math::transformDirection(runtimeWorld,axis==0?Vec3{1,0,0}:axis==1?Vec3{0,1,0}:Vec3{0,0,1}),
        math::transformDirection(viewBefore,axis==0?Vec3{1,0,0}:axis==1?Vec3{0,1,0}:Vec3{0,0,1}));
    const auto json=Json::parse(serializeSceneSnapshot({{},"Composed",document.scene().records(),document.scene().properties()}));
    require(json.at("version")==18,"Component scene writer did not use schema18.");
    for(const auto& entity:json.at("entities")) {
        require(!entity.contains("character"),"Current scene writer retained the removed Character representation.");
        if(entity.contains("camera"))require(!entity.at("camera").contains("followTarget")&&!entity.at("camera").contains("followOffset"),
            "Camera lens retained the removed embedded follow fields.");
    }
    // An Actor and a spatial reveal are independent of locomotion/control.
    const auto visual=document.createEmpty("Visual actor"),reveal=document.createEmpty("Spatial reveal");
    auto visualRecord=*document.scene().find(visual);visualRecord.actor.emplace();visualRecord.lightRig.emplace();
    require(document.updateEntity(visual,visualRecord),document.status());
    auto revealRecord=*document.scene().find(reveal);revealRecord.darknessReveal=DarknessRevealSettings{false,3.5f};
    require(document.updateEntity(reveal,revealRecord),document.status());
    auto invalid=revealRecord;invalid.motor.emplace();require(!document.updateEntity(reveal,invalid),"Motor without Controller was accepted.");
    invalid=revealRecord;invalid.navigation.emplace();require(!document.updateEntity(reveal,invalid),"Navigation without Motor/Controller was accepted.");
    invalid=revealRecord;invalid.playerInput.emplace();require(!document.updateEntity(reveal,invalid),"Player Input without Motor/Controller was accepted.");
    invalid=revealRecord;invalid.lightRig.emplace();require(!document.updateEntity(reveal,invalid),"Light Rig without an Actor boundary was accepted.");
    require(!document.setPlayerCharacter(visual),"A visual-only Actor became the controlled player.");
    require(document.save("Independent composition"),document.status());
    SceneDocument reopened(project.root);require(reopened.load(document.assetId()),reopened.status());
    same(document.scene().records(),reopened.scene().records());
    require(reopened.scene().find(reveal)->darknessReveal&&!reopened.scene().find(reveal)->actor&&!reopened.scene().find(reveal)->controller,
        "Reopening an independent spatial reveal invented gameplay or Actor components.");

    SceneDocument legacySource(project.root);const auto legacyActor=addControlledActor(legacySource,"Old actor"),legacyCamera=legacySource.createCamera("Old camera");
    auto old=*legacySource.scene().find(legacyActor);const auto oldProfile=std::make_shared<const NavigationAgentProfile>(NavigationAgentProfile{newAssetId(),"Old custom agent",{2.4f,.4f,45,.3f}});
    old.controller->profile=AssetRef<NavigationAgentProfile>{oldProfile->id};old.motor->moveSpeed=8.25f;
    const NavigationAgentProfileQuery oldProfiles=[&](const auto& id){return id==oldProfile->id?oldProfile:nullptr;};
    old.navigation->showRoute=false;old.controller->showCollisionInGame=true;old.controller->collisionColor={.2f,.6f,.8f,.7f};
    old.lightRig->showGizmos=false;old.darknessReveal=DarknessRevealSettings{false,12.f};
    require(legacySource.updateEntity(legacyActor,old)&&legacySource.setCameraFollow(legacyCamera,legacyActor),legacySource.status());
    const auto legacyWorld=*legacySource.scene().worldTransform(legacyCamera);
    auto legacy=legacyComposition(Json::parse(serializeSceneSnapshot({{},"Legacy composition",legacySource.scene().records(),legacySource.scene().properties()})),oldProfiles);
    const std::string legacyId="assets/scenes/legacy-composition.crimson-scene";const auto bytes=legacy.dump();
    {std::ofstream file(project.root/legacyId,std::ios::binary);file<<bytes;}
    const auto loaded=readSceneSnapshot(project.root,legacyId);Scene migrated;migrated.apply(loaded.records,loaded.properties);
    require(loaded.records.size()==2&&migrated.properties().activeCamera==legacyCamera&&migrated.properties().playerCharacter==legacyActor,
        "Legacy component migration invented entities/cameras or changed explicit roles.");
    const auto converted=*migrated.find(legacyActor);
    const auto convertedProfile=std::find_if(loaded.stagedNavigationProfiles.begin(),loaded.stagedNavigationProfiles.end(),[&](const auto& profile){return profile->id==converted.controller->profile.id;});
    require(convertedProfile!=loaded.stagedNavigationProfiles.end()&&(*convertedProfile)->traversal==oldProfile->traversal,"Legacy Character migration lost custom capsule dimensions.");
    require(converted.actor&&converted.motor==old.motor&&converted.controller->showCollisionInGame==old.controller->showCollisionInGame&&
        converted.controller->collisionColor==old.controller->collisionColor&&converted.navigation==old.navigation&&converted.playerInput&&
        converted.lightRig==old.lightRig&&converted.darknessReveal==old.darknessReveal,"Legacy Character migration lost independently authored values.");
    const auto convertedWorld=*migrated.worldTransform(legacyCamera);
    for(unsigned index=0;index<16;++index)require(std::abs(convertedWorld[index]-legacyWorld[index])<.0002f,"Legacy follow migration changed the visible camera pose.");
    {std::ifstream file(project.root/legacyId,std::ios::binary);const std::string unchanged{std::istreambuf_iterator<char>(file),{}};require(unchanged==bytes,"Read migration rewrote the saved legacy scene.");}
    const auto current=Json::parse(serializeSceneSnapshot(loaded));require(current.at("version")==18,"Migrated scene did not write only the current representation.");
    for(const auto& entity:current.at("entities"))require(!entity.contains("character"),"Saving migrated components retained Character data.");
}

void nativeColliderStorageAndValidation() {
    TempProject project;
    const std::vector<Vec3> positions{{-2,0,-2},{2,0,-2},{2,0,2},{-2,0,2}};
    const std::vector<std::uint32_t> indices{0,2,1,0,3,2};
    const auto geometry=makeCollisionMesh(positions,indices);
    require(geometry->validated()&&geometry->bounds.valid&&sameCollisionMesh(geometry,makeCollisionMesh(positions,indices)),"Native collider factory did not produce stable validated geometry.");
    SceneDocument document(project.root);
    require(document.edit("Native collision",[&](auto& records,auto& properties){
        records.clear();properties={};EntityRecord record;record.id=1;record.name="Native collision";record.collider.emplace();
        record.collider->shape=ColliderShape::Trimesh;record.collider->mesh=geometry;records.push_back(std::move(record));
    }),document.status());
    document.select({1});require(document.duplicateSelection(),document.status());
    require(document.scene().find(document.selection().primary)->collider->mesh==geometry,"Native collision duplication copied triangle arrays or lost the payload.");
    require(document.undo()&&document.scene().records().size()==1&&document.redo(),document.status());
    require(document.scene().find(document.selection().primary)->collider->mesh==geometry,"Native collision redo did not retain its immutable payload.");
    require(document.save("Native collision"),document.status());
    const auto snapshot=readSceneSnapshot(project.root,document.assetId());
    require(snapshot.records.size()==2&&sameCollisionMesh(snapshot.records[0].collider->mesh,geometry)&&snapshot.records[0].collider->source.empty(),"Native collision persistence required a model asset or changed its triangles.");
    auto json=Json::parse(serializeSceneSnapshot(snapshot));
    require(json.at("version")==18&&json["entities"][0]["collider"].contains("mesh"),"Native collision did not write schema18 triangle data.");
    for(const auto index:Json::array({-1,4294967296ull,.5})) {
        auto malformed=json;malformed["entities"][0]["collider"]["mesh"]["indices"][0]=index;
        {std::ofstream output(sceneAssetPath(project.root,document.assetId()),std::ios::binary);output<<malformed.dump();}
        bool rejected=false;try{(void)readSceneSnapshot(project.root,document.assetId());}catch(const std::exception&){rejected=true;}
        require(rejected,"Native collision accepted a negative, overflowing or fractional index.");
    }
    for(const auto& bad:std::vector<std::vector<std::uint32_t>>{{0,1},{0,0,0},{0,1,9}}) {
        bool rejected=false;try{(void)makeCollisionMesh(positions,bad);}catch(const std::exception&){rejected=true;}
        require(rejected,"Native collision accepted incomplete, degenerate or invalid triangles.");
    }
    auto invalid=positions;invalid[0][0]=std::numeric_limits<float>::infinity();
    bool rejected=false;try{(void)makeCollisionMesh(invalid,indices);}catch(const std::exception&){rejected=true;}
    require(rejected,"Native collision accepted a nonfinite position.");
}
void componentsAndStorage(){
    TempProject project;SceneDocument document(project.root);
    const auto character=addControlledActor(document),second=addControlledActor(document,"Other");
    const auto box=document.createBox();const auto collider=document.createCollider("Collider",box);
    const auto camera=document.createCamera(),otherCamera=document.createCamera("Other camera");
    require(character && second && box && collider && camera && otherCamera,document.status());
    require(document.scene().properties().playerCharacter==character && document.scene().properties().activeCamera==camera,"Additional entities stole activation.");
    require(document.scene().find(box)->model->id==BuiltinBoxModelId,"Box did not use builtin geometry.");
    require(document.scene().find(collider)->parent==box && document.scene().find(collider)->local==Mat4{},"Collider child was not identity local.");
    require(document.setCameraFollow(camera,character),document.status());
    auto characterRecord=*document.scene().find(character);characterRecord.motor->moveSpeed=7;require(document.updateEntity(character,characterRecord),document.status());
    require(document.save("Gameplay"),document.status());
    const auto snapshot=readSceneSnapshot(project.root,document.assetId());
    require(Json::parse(serializeSceneSnapshot(snapshot)).at("version")==18,"Scene writer did not use version 18.");
    SceneDocument reopened(project.root);require(reopened.load(document.assetId()),reopened.status());same(document.scene().records(),reopened.scene().records());
    require(sameProperties(document.scene().properties(),reopened.scene().properties()),"Gameplay scene properties did not round-trip.");
    auto legacy=legacyComposition(Json::parse(serializeSceneSnapshot(snapshot)));legacy["version"]=3;legacy["settings"].erase("activeCamera");legacy["settings"].erase("playerCharacter");
    for(auto& record:legacy["entities"]){record.erase("character");record.erase("collider");record.erase("camera");}
    const auto legacyId="assets/scenes/legacy-gameplay.crimson-scene";
    {std::ofstream stream(project.root/legacyId);stream<<legacy.dump();}
    require(reopened.load(legacyId),reopened.status());require(!reopened.scene().properties().activeCamera && !reopened.scene().properties().playerCharacter,"Legacy scene gained gameplay activations.");
    auto invalid=*document.scene().find(character);invalid.controller->profile=AssetRef<NavigationAgentProfile>{"invalid-profile"};
    require(!document.updateEntity(character,invalid),"Invalid capsule accepted.");
    invalid=*document.scene().find(collider);invalid.collider->size[0]=0;require(!document.updateEntity(collider,invalid),"Zero collider size accepted.");
    invalid=*document.scene().find(camera);invalid.camera->nearPlane=invalid.camera->farPlane;require(!document.updateEntity(camera,invalid),"Invalid camera clipping accepted.");
    invalid=*document.scene().find(camera);invalid.camera->verticalFov=.5f;require(!document.updateEntity(camera,invalid),"Out-of-contract camera FOV accepted.");
    invalid=*document.scene().find(box);invalid.model=AssetRef<ModelAsset>{"builtin:unknown"};require(!document.updateEntity(box,invalid),"Unknown builtin model identity accepted.");
    require(!document.setActiveCamera(character) && !document.setPlayerCharacter(camera),"Wrong component activation accepted.");
}

void collisionDisplayStorageAndHistory(){
    TempProject project;SceneDocument document(project.root);
    const auto character=addControlledActor(document),collider=document.createCollider();
    for(const auto color:{document.scene().find(character)->controller->collisionColor,document.scene().find(collider)->collider->collisionColor})
        require(std::lround(color[0]*255)==0x39 && std::lround(color[1]*255)==0xaf &&
            std::lround(color[2]*255)==0xdd && color[3]==1,"New collision colors must default to #39AFDDFF.");
    require(document.scene().find(character)->navigation->showRoute &&
        !document.scene().find(character)->controller->showCollisionInGame &&
        !document.scene().find(collider)->collider->showCollisionInGame,"New entities have unexpected debug visibility defaults.");
    auto actor=*document.scene().find(character);
    actor.navigation->showRoute=false;actor.controller->showCollisionInGame=true;actor.controller->collisionColor={.9f,.1f,.3f,.25f};
    require(document.updateEntity(character,actor),document.status());
    require(document.undo() && document.scene().find(character)->navigation->showRoute &&
        !document.scene().find(character)->controller->showCollisionInGame,"Undo did not restore character debug display.");
    require(document.redo() && document.scene().find(character)->controller==actor.controller && document.scene().find(character)->navigation==actor.navigation,"Redo lost character debug display.");
    auto box=*document.scene().find(collider);box.collider->showCollisionInGame=true;box.collider->collisionColor={.2f,.4f,.8f,.75f};
    require(document.updateEntity(collider,box),document.status());
    require(document.save("Collision display"),document.status());
    SceneDocument reopened(project.root);require(reopened.load(document.assetId()),reopened.status());
    same(document.scene().records(),reopened.scene().records());
    auto legacy=legacyComposition(Json::parse(serializeSceneSnapshot(readSceneSnapshot(project.root,document.assetId()))));
    const std::string legacyId="assets/scenes/legacy-collision-display.crimson-scene";
    for(auto& record:legacy["entities"])for(const auto* component:{"character","collider"})if(record.contains(component))
        record[component]["collisionColor"].erase(3);
    {std::ofstream output(project.root/legacyId);output<<legacy.dump();}
    require(reopened.load(legacyId),reopened.status());
    require(reopened.scene().find(character)->controller->collisionColor==Vec4{.9f,.1f,.3f,1} &&
        reopened.scene().find(collider)->collider->collisionColor==Vec4{.2f,.4f,.8f,1},
        "RGB collision colors did not preserve their channels and opaque wire alpha.");
    for(auto& record:legacy["entities"])for(const auto* component:{"character","collider"})if(record.contains(component)) {
        record[component].erase("showRoute");record[component].erase("showCollisionInGame");record[component].erase("collisionColor");
    }
    {std::ofstream output(project.root/legacyId);output<<legacy.dump();}
    require(reopened.load(legacyId),reopened.status());
    require(reopened.scene().find(character)->navigation->showRoute &&
        !reopened.scene().find(character)->controller->showCollisionInGame &&
        reopened.scene().find(character)->controller->collisionColor==SceneCharacterController{}.collisionColor &&
        !reopened.scene().find(collider)->collider->showCollisionInGame &&
        reopened.scene().find(collider)->collider->collisionColor==SceneCollider{}.collisionColor,
        "Legacy scenes did not retain default route/collision display.");
    auto invalid=actor;invalid.controller->collisionColor[0]=std::numeric_limits<float>::infinity();
    require(!document.updateEntity(character,invalid),"Nonfinite character collision color was accepted.");
    invalid=box;invalid.collider->collisionColor[1]=-1;
    require(!document.updateEntity(collider,invalid),"Negative collider color was accepted.");
    invalid=box;invalid.collider->collisionColor[2]=2;
    require(!document.updateEntity(collider,invalid),"Out-of-range collider color was accepted.");
    invalid=actor;invalid.controller->collisionColor[3]=std::numeric_limits<float>::quiet_NaN();
    require(!document.updateEntity(character,invalid),"Nonfinite collision alpha was accepted.");
    invalid=box;invalid.collider->collisionColor[3]=1.01f;
    require(!document.updateEntity(collider,invalid),"Out-of-range collision alpha was accepted.");
    invalid=box;invalid.collider->collisionColor[3]=0;
    require(document.updateEntity(collider,invalid),"Fully transparent collision color was rejected.");
}

void unifiedColliderStorageHistoryAndCompatibility() {
    TempProject project;SceneDocument document(project.root);
    const auto id=document.createCollider();auto authored=*document.scene().find(id);
    authored.collider->size={2,3,4};authored.collider->radius=.7f;authored.collider->height=2.8f;
    authored.collider->source=AssetRef<ModelAsset>{BuiltinBoxModelId};
    authored.collider->showCollisionInGame=true;authored.collider->collisionColor={.1f,.3f,.7f,.6f};
    const auto original=*document.scene().find(id);
    for(const auto shape:{ColliderShape::Box,ColliderShape::Sphere,ColliderShape::Cylinder,ColliderShape::Capsule,ColliderShape::Trimesh}) {
        authored.collider->shape=shape;require(document.updateEntity(id,authored),document.status());
        require(document.undo()&&document.redo()&&document.scene().find(id)->collider==authored.collider,"Collider shape edit did not undo/redo all retained settings.");
        require(document.save("Unified colliders"),document.status());SceneDocument loaded(project.root);
        require(loaded.load(document.assetId())&&loaded.scene().find(id)->collider==authored.collider,"A collider shape lost retained dimensions/source/display on save/reopen.");
        const auto json=Json::parse(serializeSceneSnapshot(readSceneSnapshot(project.root,document.assetId())));
        require(json.at("version")==18&&json.at("entities").at(0).contains("collider")&&!json.at("entities").at(0).contains("meshCollider"),"Unified collider writer retained the removed meshCollider representation.");
        SceneDocument runtime(project.root);require(runtime.resetTransientFrom(document)&&runtime.scene().find(id)->collider==authored.collider,"Play copy lost unified collider settings.");
    }
    auto invalid=authored;invalid.collider->shape=static_cast<ColliderShape>(255);
    require(!document.updateEntity(id,invalid),"An unknown ColliderShape was accepted.");
    for(const auto value:{0.f,-1.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        invalid=authored;invalid.collider->radius=value;require(!document.updateEntity(id,invalid),"Invalid retained collider radius was accepted.");
        invalid=authored;invalid.collider->height=value;require(!document.updateEntity(id,invalid),"Invalid retained collider height was accepted.");
    }
    invalid=authored;invalid.collider->shape=ColliderShape::Capsule;invalid.collider->height=.1f;
    require(!document.updateEntity(id,invalid),"Capsule total height smaller than its diameter was accepted.");
    invalid=authored;invalid.collider->source=AssetRef<ModelAsset>{DefaultMaterialId};
    require(!document.updateEntity(id,invalid),"A protected material was accepted as a collider model source.");
    invalid=authored;invalid.controller.emplace();require(!document.updateEntity(id,invalid),"A Character and Trimesh Collider shared an entity.");
    invalid.collider->shape=ColliderShape::Box;require(document.updateEntity(id,invalid),"A previously valid Character/Box combination was rejected.");
    require(document.undo()&&document.scene().find(id)->collider==authored.collider,"Legacy Character/Box compatibility edit failed to undo.");
    auto legacy=legacyComposition(Json::parse(serializeSceneSnapshot({{},"Legacy colliders",{original},{},{}})));legacy["version"]=11;
    auto& entity=legacy["entities"][0];for(const auto* field:{"shape","radius","height","source"})entity["collider"].erase(field);
    const std::string legacyId="assets/scenes/legacy-unified-collider.crimson-scene";
    const auto writeLegacy=[&]{const auto path=sceneAssetPath(project.root,legacyId);std::ofstream file(path);file<<legacy.dump();};
    writeLegacy();auto loaded=readSceneSnapshot(project.root,legacyId);
    require(loaded.records[0].collider==original.collider,"A v11 Box collider did not migrate with defaults.");
    entity.erase("collider");entity["meshCollider"]={{"source","assets/models/legacy-collision/manifest.json"},{"showCollisionInGame",true},{"collisionColor",Vec4{.2f,.3f,.4f,.5f}}};
    writeLegacy();loaded=readSceneSnapshot(project.root,legacyId);
    require(loaded.records[0].collider&&loaded.records[0].collider->shape==ColliderShape::Trimesh&&loaded.records[0].collider->showCollisionInGame&&
        loaded.records[0].collider->collisionColor==Vec4{.2f,.3f,.4f,.5f}&&!loaded.records[0].collider->source.empty(),"A v11 meshCollider did not migrate its source/display to Trimesh.");
    const auto current=Json::parse(serializeSceneSnapshot(loaded));require(current.at("version")==18&&current["entities"][0]["collider"].at("shape")=="trimesh"&&!current["entities"][0].contains("meshCollider"),"Legacy collider conversion was not one-way on write.");
}

void runtimePatches(){
    TempProject project;SceneDocument authored(project.root);
    const auto actor=addControlledActor(authored),other=addControlledActor(authored,"Other");
    const auto camera=authored.createCamera();require(authored.setCameraFollow(camera,actor),authored.status());
    const auto original=authored.scene().records();const auto authoredHistory=authored.history().size();const auto selection=authored.selection();
    SceneDocument runtime(project.root);require(runtime.resetTransientFrom(authored),runtime.status());
    const auto generation=runtime.scene().generation(),revision=runtime.scene().revision(),poseRevision=runtime.scene().poseRevision();
    pose(runtime,actor,{2,0,0});require(!runtime.dirty() && runtime.history().size()==0,"Simulation entered history/dirty state.");
    require(runtime.scene().generation()==generation && runtime.scene().revision()==revision && runtime.scene().poseRevision()>poseRevision,"Runtime pose invalidated edit identity.");
    require(!runtime.save("Accidental save"),"Transient document was saved.");
    require(runtime.begin("Rename moving actor"),runtime.status());
    require(runtime.update([&](auto& records,auto&){for(auto& record:records)if(record.id==actor)record.name="First";}),runtime.status());
    pose(runtime,actor,{5,0,0});pose(runtime,other,{9,0,0});
    require(runtime.update([&](auto& records,auto&){for(auto& record:records)if(record.id==actor)record.name="Final";}),runtime.status());
    require(runtime.commit() && runtime.undo(),runtime.status());near(position(runtime.scene(),actor),{5,0,0});near(position(runtime.scene(),other),{9,0,0});
    require(runtime.scene().find(actor)->name=="Actor","Rename Undo failed.");
    require(runtime.redo(),runtime.status());near(position(runtime.scene(),actor),{5,0,0});
    require(runtime.begin("Cancel rename"),runtime.status());
    require(runtime.update([&](auto& records,auto&){for(auto& record:records)if(record.id==actor)record.name="Canceled";}),runtime.status());
    pose(runtime,actor,{7,0,0});runtime.cancel();near(position(runtime.scene(),actor),{7,0,0});require(runtime.scene().find(actor)->name=="Final","Cancel did not restore only edited fields.");
    require(runtime.beginTransform("Transform actor",{actor}),runtime.status());require(runtime.transformEditRoots()==std::vector<EntityId>{actor},"Transform gesture roots missing.");
    require(runtime.update([&](auto& records,auto&){for(auto& record:records)if(record.id==actor)record.local=math::translation({11,0,0});}),runtime.status());
    pose(runtime,other,{15,0,0});require(runtime.commit() && runtime.transformEditRoots().empty() && runtime.undo(),runtime.status());
    near(position(runtime.scene(),actor),{7,0,0});near(position(runtime.scene(),other),{15,0,0});
    same(original,authored.scene().records());require(authored.history().size()==authoredHistory && authored.selection().ids==selection.ids && authored.dirty(),"Runtime altered authored history or selection.");
    require(runtime.resetTransientFrom(authored),runtime.status());same(original,runtime.scene().records());require(!runtime.history().canUndo(),"Second Play retained runtime undo.");
    const std::array bad{ScenePoseUpdate{actor,math::translation({20,0,0})},ScenePoseUpdate{999,Mat4{}}};
    require(!runtime.applyRuntimePoses(bad),"Invalid runtime pose batch accepted.");same(original,runtime.scene().records());
}

void followAndHierarchy(){
    TempProject project;SceneDocument document(project.root);
    const auto actor=addControlledActor(document),camera=document.createCamera();const auto cameraChild=document.createEmpty("Camera child");
    document.select({cameraChild});require(document.reparentSelection(camera),document.status());
    const auto beforeCamera=position(document.scene(),camera),beforeChild=position(document.scene(),cameraChild);
    require(document.setCameraFollow(camera,actor),document.status());near(position(document.scene(),camera),beforeCamera);
    move(document,actor,{3,0,4});near(position(document.scene(),camera),beforeCamera+Vec3{3,0,4});near(position(document.scene(),cameraChild),beforeChild+Vec3{3,0,4});
    require(document.undo(),document.status());near(position(document.scene(),camera),beforeCamera);
    const auto preview=resolveGameCamera(document.scene());require(preview && preview->fovAxis==CameraFovAxis::Vertical,"Game camera projection contract missing.");
    const auto forward=math::normalized(preview->target-preview->position);
    move(document,camera,beforeCamera+Vec3{2,0,0});move(document,actor,{1,0,0});
    near(position(document.scene(),camera),beforeCamera+Vec3{3,0,0});const auto movedCamera=resolveGameCamera(document.scene());near(math::normalized(movedCamera->target-movedCamera->position),forward);
    const auto fixed=position(document.scene(),camera);require(document.setCameraFollow(camera,NoEntity),document.status());near(position(document.scene(),camera),fixed);
    move(document,actor,{2,0,0});near(position(document.scene(),camera),fixed);
    require(!document.setCameraFollow(camera,cameraChild) && !document.setCameraFollow(camera,camera),"Camera follow dependency cycle accepted.");
    require(document.setCameraFollow(camera,actor),document.status());
    const auto group=document.createEmpty("Group");document.select({actor,camera});require(document.reparentSelection(group),document.status());
    document.select({group});require(document.duplicateSelection(),document.status());const auto copy=document.selection().primary;
    EntityId copiedActor{},copiedCamera{};for(const auto id:document.scene().subtree(copy)){const auto r=document.scene().find(id);if(r->actor)copiedActor=id;if(r->camera)copiedCamera=id;}
    require(copiedActor && copiedCamera && document.scene().find(copiedCamera)->cameraFollow->target==copiedActor,"Duplicated follow reference did not remap internally.");
    require(document.scene().properties().activeCamera==camera && document.scene().properties().playerCharacter==actor,"Duplicate stole active roles.");
    const auto atDeletion=position(document.scene(),camera);document.select({actor});require(document.eraseSelection(),document.status());
    require(!document.scene().properties().playerCharacter && !document.scene().find(camera)->cameraFollow,"Deletion retained player/follow references.");near(position(document.scene(),camera),atDeletion);
    require(document.undo(),document.status());require(document.scene().properties().playerCharacter==actor && document.scene().find(camera)->cameraFollow->target==actor,"Deletion Undo lost stable references.");
    document.select({camera});require(!document.ungroupSelection(),"Camera was treated as an empty group.");
    SceneDocument runtime(project.root);require(runtime.resetTransientFrom(document),runtime.status());
    const auto offset=runtime.scene().find(camera)->cameraFollow->offset;pose(runtime,actor,{9,0,2});
    near(position(runtime.scene(),camera),position(runtime.scene(),actor)+offset);
    require(runtime.begin("Rename camera"),runtime.status());require(runtime.update([&](auto& records,auto&){for(auto& r:records)if(r.id==camera)r.name="Renamed";}),runtime.status());
    pose(runtime,actor,{12,0,3});require(runtime.commit() && runtime.undo(),runtime.status());near(position(runtime.scene(),camera),position(runtime.scene(),actor)+offset);
}
void followDependenciesAndAtomicFailure(){
    TempProject project;SceneDocument document(project.root);
    const auto target=document.createEmpty("Target"),parent=document.createEmpty("Camera parent");
    const auto first=document.createCamera("First"),second=document.createCamera("Second");
    document.select({first});require(document.reparentSelection(parent),document.status());
    require(document.setCameraFollow(first,target) && document.setCameraFollow(second,first),document.status());
    const auto firstPosition=position(document.scene(),first),secondPosition=position(document.scene(),second);
    move(document,target,{2,3,4});near(position(document.scene(),first),firstPosition+Vec3{2,3,4});near(position(document.scene(),second),secondPosition+Vec3{2,3,4});
    require(!document.setCameraFollow(first,second),"Indirect follow cycle was accepted.");
    const auto before=document.scene().records();auto singular=*document.scene().find(parent);singular.local=math::scaling({0,1,1});
    require(!document.updateEntity(parent,singular),"Singular following camera parent was accepted.");same(before,document.scene().records());
    SceneDocument runtime(project.root);require(runtime.resetTransientFrom(document),runtime.status());const auto revision=runtime.scene().poseRevision();
    const std::array invalid{ScenePoseUpdate{target,math::translation({20,3,4})},ScenePoseUpdate{parent,math::scaling({0,1,1})}};
    require(!runtime.applyRuntimePoses(invalid),"Runtime singular camera parent was accepted.");same(before,runtime.scene().records());require(runtime.scene().poseRevision()==revision,"Failed batch changed pose revision.");
    document.select({parent});require(document.ungroupSelection(),document.status());
    near(position(document.scene(),first),firstPosition+Vec3{2,3,4});
    require(document.undo(),document.status());same(before,document.scene().records());
}
void parentFollowKeepsWorldOffset(){
    TempProject project;SceneDocument document(project.root);
    const auto parent=document.createEmpty("Follow parent"),camera=document.createCamera();
    document.select({camera});require(document.reparentSelection(parent),document.status());
    require(document.setCameraFollow(camera,parent),document.status());
    const auto offset=document.scene().find(camera)->cameraFollow->offset;
    const auto worldBefore=*document.scene().worldTransform(camera);
    auto transformed=*document.scene().find(parent);
    transformed.local=math::translation({1,2,3})*math::rotation(math::axisAngle({0,1,0},.7))*math::scaling({2,3,4});
    require(document.updateEntity(parent,transformed),document.status());
    near(position(document.scene(),camera),position(document.scene(),parent)+offset);
    require(document.scene().find(camera)->cameraFollow->offset==offset,"Parent rotation/scale changed the world-space follow offset.");
    const auto expectedDirection=math::normalized(math::transformDirection(worldBefore,{0,0,-1}));
    const auto game=resolveGameCamera(document.scene());near(math::normalized(game->target-game->position),expectedDirection);
    require(document.undo(),document.status());near(position(document.scene(),camera),offset);
}
void boxMaterialAndPointRadius(){
    TempProject project;SceneDocument document(project.root);
    const auto first=document.createBox("First"),second=document.createBox("Second"),light=document.createPointLight();
    const std::string red="assets/materials/red.crimson-material",blue="assets/materials/blue.crimson-material";
    auto edited=*document.scene().find(first);edited.modelMaterials={AssetRef<MaterialAsset>{red}};require(document.updateEntity(first,edited),document.status());
    require(document.scene().find(second)->modelMaterials.empty(),"Assigning one Box changed another instance.");
    require(document.undo() && document.scene().find(first)->modelMaterials.empty(),"Material assignment did not undo.");
    require(document.redo() && document.scene().find(first)->modelMaterials[0].id==red,"Material assignment did not redo.");
    document.select({first});require(document.duplicateSelection(),document.status());const auto duplicate=document.selection().primary;
    require(document.scene().find(duplicate)->modelMaterials[0].id==red,"Duplicate lost its material assignment.");
    edited=*document.scene().find(light);edited.point->radius=6.25f;edited.local=math::translation({2,3,4})*math::scaling({4,2,3});
    require(document.updateEntity(light,edited),document.status());
    auto extracted=extractScene(document.scene());
    require(extracted.models.size()==3 && extracted.models[0].assetId==BuiltinBoxModelId,"Box geometry extraction changed.");
    for(const auto& model:extracted.models)require(model.materialOverrides==(model.id==second?ModelMaterialOverrides{}:ModelMaterialOverrides{AssetRef<MaterialAsset>{red}}),"Extraction lost an instance material.");
    require(extracted.points.size()==1 && extracted.points[0].radius==6.25f,"Point radius was lost or scaled in extraction.");
    require(document.undo() && document.scene().find(light)->point->radius==20.f,"Point radius was absent from history equality.");
    require(document.redo(),document.status());
    require(document.save("Materials and range"),document.status());SceneDocument reopened(project.root);require(reopened.load(document.assetId()),reopened.status());
    same(document.scene().records(),reopened.scene().records());
    auto legacy=legacyComposition(Json::parse(serializeSceneSnapshot(readSceneSnapshot(project.root,document.assetId()))));
    for(auto& entity:legacy["entities"]){entity.erase("modelMaterials");if(entity.contains("point"))entity["point"].erase("radius");}
    const auto legacyId="assets/scenes/legacy-material-range.crimson-scene";{std::ofstream stream(project.root/legacyId);stream<<legacy.dump();}
    require(reopened.load(legacyId),reopened.status());require(reopened.scene().find(first)->modelMaterials.empty() && reopened.scene().find(light)->point->radius==20.f,"Legacy defaults changed.");
    legacy["version"]=10;
    for(auto& entity:legacy["entities"])if(entity["name"]=="First")entity["modelMaterial"]=red;
    {std::ofstream stream(project.root/legacyId);stream<<legacy.dump();}
    require(reopened.load(legacyId) && reopened.scene().find(first)->modelMaterials==ModelMaterialOverrides{AssetRef<MaterialAsset>{red}},
        "Legacy Box material did not migrate to slot zero.");
    const auto migrated=Json::parse(serializeSceneSnapshot({legacyId,"Migrated",reopened.scene().records(),reopened.scene().properties()}));
    for(const auto& entity:migrated["entities"])require(!entity.contains("modelMaterial"),"Scene writer retained the retired scalar material field.");
    edited=*document.scene().find(light);edited.modelMaterials={AssetRef<MaterialAsset>{red}};require(!document.updateEntity(light,edited),"Non-model material override accepted.");
    edited=*document.scene().find(first);edited.modelMaterials={AssetRef<MaterialAsset>{DefaultParticleMaterialId}};require(!document.updateEntity(first,edited),"Protected sprite material accepted for a Box.");
    edited.modelMaterials={AssetRef<MaterialAsset>{"assets/materials/../outside.crimson-material"}};require(!document.updateEntity(first,edited),"Unsafe material reference accepted.");
    edited=*document.scene().find(light);for(const float radius:{0.f,-1.f,100001.f,std::numeric_limits<float>::quiet_NaN()}){edited.point->radius=radius;require(!document.updateEntity(light,edited),"Invalid point radius accepted.");}
    SceneDocument runtime(project.root);require(runtime.resetTransientFrom(document),runtime.status());
    require(runtime.begin("Live material assignment"),runtime.status());
    require(runtime.update([&](auto& records,auto&){for(auto& record:records)if(record.id==first)record.modelMaterials={AssetRef<MaterialAsset>{blue}};}),runtime.status());
    pose(runtime,first,{10,0,0});require(runtime.commit(),runtime.status());
    require(runtime.undo() && runtime.scene().find(first)->modelMaterials[0].id==red,"Runtime assignment did not undo.");near(position(runtime.scene(),first),{10,0,0});
    require(runtime.redo() && runtime.scene().find(first)->modelMaterials[0].id==blue,"Runtime assignment did not redo.");near(position(runtime.scene(),first),{10,0,0});
    const auto references=runtime.historicalMaterialReferences();require(std::find(references.begin(),references.end(),red)!=references.end() && std::find(references.begin(),references.end(),blue)!=references.end(),"Material assignment history lost a dependency.");
    require(document.scene().find(first)->modelMaterials[0].id==red && !document.dirty(),"Runtime material changed the authored document.");
}
void importedMaterialSlots() {
    TempProject project;SceneDocument document(project.root);
    const std::string modelId="assets/models/imported/manifest.json",red="assets/materials/red.crimson-material",blue="assets/materials/blue.crimson-material";
    const auto first=document.createModel(modelId,"First"),second=document.createModel(modelId,"Second");
    auto edited=*document.scene().find(first);
    setModelMaterialOverride(edited.modelMaterials,0,AssetRef<MaterialAsset>{red});
    setModelMaterialOverride(edited.modelMaterials,2,AssetRef<MaterialAsset>{blue});
    require(document.updateEntity(first,edited),document.status());
    const auto assigned=edited.modelMaterials;
    require(assigned.size()==3 && assigned[1].empty() && document.scene().find(second)->modelMaterials.empty(),
        "Imported slot assignment changed an inherited slot or another instance.");
    require(document.undo() && document.scene().find(first)->modelMaterials.empty(),"Imported slot assignment did not undo.");
    require(document.redo() && document.scene().find(first)->modelMaterials==assigned,"Imported slot assignment did not redo.");
    edited=*document.scene().find(first);setModelMaterialOverride(edited.modelMaterials,2,{});
    require(edited.modelMaterials.size()==1 && document.updateEntity(first,edited),"Clearing the last slot did not trim inherited entries.");
    require(document.undo() && document.scene().find(first)->modelMaterials==assigned,"Clearing a slot lost the other assignments.");
    document.select({first});require(document.duplicateSelection(),document.status());const auto duplicate=document.selection().primary;
    require(document.scene().find(duplicate)->modelMaterials==assigned,"Duplicate lost imported material slots.");
    const auto extracted=extractScene(document.scene());
    for(const auto& model:extracted.models)require(model.materialOverrides==(model.id==second?ModelMaterialOverrides{}:assigned),
        "Scene extraction lost an imported material slot.");
    require(document.save("Imported slots"),document.status());SceneDocument reopened(project.root);
    require(reopened.load(document.assetId()),reopened.status());same(document.scene().records(),reopened.scene().records());
    SceneDocument runtime(project.root);require(runtime.resetTransientFrom(document),runtime.status());
    require(runtime.scene().find(first)->modelMaterials==assigned,"Play did not copy imported material slots.");
    edited=*runtime.scene().find(first);setModelMaterialOverride(edited.modelMaterials,0,{});require(runtime.updateEntity(first,edited),runtime.status());
    pose(runtime,first,{10,0,0});require(runtime.undo(),runtime.status());
    require(runtime.scene().find(first)->modelMaterials==assigned,"Live slot undo did not restore assignments.");near(position(runtime.scene(),first),{10,0,0});
    require(document.scene().find(first)->modelMaterials==assigned && !document.dirty(),"Live slot edits changed the authored scene.");
    const auto history=document.historicalMaterialReferences();
    require(std::find(history.begin(),history.end(),red)!=history.end() && std::find(history.begin(),history.end(),blue)!=history.end(),
        "Imported material history lost slot dependencies.");
    const auto box=document.createBox();edited=*document.scene().find(box);setModelMaterialOverride(edited.modelMaterials,1,AssetRef<MaterialAsset>{red});
    require(!document.updateEntity(box,edited),"Built-in geometry accepted a nonexistent material slot.");
    bool rejected=false;try{setModelMaterialOverride(edited.modelMaterials,MaximumModelMaterialSlots,AssetRef<MaterialAsset>{red});}catch(const std::exception&){rejected=true;}
    require(rejected,"Out-of-range slot assignment was accepted.");
}
void modelFadeParticipationStorageAndRuntime() {
    TempProject project;SceneDocument document(project.root);
    const auto wall=document.createBox("Wall"),pillar=document.createBox("Pillar"),empty=document.createEmpty("Empty");
    require(wall&&pillar&&empty&&!document.scene().find(wall)->fadeWhenOccluding&&!document.scene().find(pillar)->fadeWhenOccluding,
        "New model instances must default to no player-occlusion participation.");
    require(document.save("Fade participation"),document.status());
    const auto originalFingerprint=sceneFileFingerprint(project.root,document.assetId());
    const auto before=*document.scene().find(wall);auto edited=before;edited.fadeWhenOccluding=true;
    require(!sameEntity(before,edited)&&document.updateEntity(wall,edited)&&document.dirty(),"Fade participation was omitted from model equality or document edits.");
    require(document.undo()&&!document.scene().find(wall)->fadeWhenOccluding&&!document.dirty(),"Undo did not restore disabled participation and clean state.");
    require(document.redo()&&document.scene().find(wall)->fadeWhenOccluding,"Redo lost authored fade participation.");
    const auto extracted=extractScene(document.scene());
    for(const auto& model:extracted.models)require(model.fadeWhenOccluding==(model.id==wall),"Participation was shared by another instance of the same asset.");
    document.select({wall});require(document.duplicateSelection(),document.status());const auto duplicate=document.selection().primary;
    require(document.scene().find(duplicate)->fadeWhenOccluding&&!document.scene().find(pillar)->fadeWhenOccluding,
        "Duplication lost participation or changed an unrelated model.");
    require(document.save(),document.status());
    require(sceneFileFingerprint(project.root,document.assetId())!=originalFingerprint,"Participation save did not invalidate the scene preview fingerprint.");
    const auto snapshot=readSceneSnapshot(project.root,document.assetId());const auto serialized=Json::parse(serializeSceneSnapshot(snapshot));
    require(serialized.at("version")==18,"Fade participation did not use the current scene schema.");
    for(const auto& entity:serialized.at("entities")) {
        if(entity.contains("model"))require(entity.at("fadeWhenOccluding").is_boolean(),"A model did not write its explicit boolean participation.");
        else require(!entity.contains("fadeWhenOccluding"),"A nonmodel wrote model-only fade settings.");
    }
    SceneDocument reopened(project.root);require(reopened.load(document.assetId()),reopened.status());same(document.scene().records(),reopened.scene().records());
    SceneDocument runtime(project.root);require(runtime.resetTransientFrom(document)&&runtime.scene().find(wall)->fadeWhenOccluding,
        "Play copy lost authored participation.");
    auto live=*runtime.scene().find(wall);live.fadeWhenOccluding=false;require(runtime.updateEntity(wall,live),runtime.status());
    pose(runtime,wall,{3,4,5});require(runtime.undo()&&runtime.scene().find(wall)->fadeWhenOccluding,"Live participation undo did not restore its authored value.");
    near(position(runtime.scene(),wall),{3,4,5});
    require(runtime.redo()&&!runtime.scene().find(wall)->fadeWhenOccluding,"Live participation redo failed.");near(position(runtime.scene(),wall),{3,4,5});
    require(document.scene().find(wall)->fadeWhenOccluding&&!document.dirty(),"Play changes modified the authored document.");
    for(const auto version:{9,12}) {
        auto legacy=legacyComposition(serialized);legacy["version"]=version;for(auto& entity:legacy["entities"])entity.erase("fadeWhenOccluding");
        const auto id="assets/scenes/legacy-fade-"+std::to_string(version)+".crimson-scene";const auto bytes=legacy.dump();
        std::filesystem::create_directories((project.root/id).parent_path());{std::ofstream file(project.root/id);file<<bytes;}
        require(reopened.load(id),reopened.status());
        for(const auto& record:reopened.scene().records())require(!record.fadeWhenOccluding,"An older scene acquired opt-in fading.");
        std::ifstream file(project.root/id);const std::string retained{std::istreambuf_iterator<char>(file),{}};
        require(retained==bytes&&!reopened.dirty(),"Reading an older scene rewrote authored bytes or marked migration dirty.");
    }
    const auto badRecord=*document.scene().find(empty);auto invalid=badRecord;invalid.fadeWhenOccluding=true;
    require(!document.updateEntity(empty,invalid)&&sameEntity(*document.scene().find(empty),badRecord),"A nonmodel accepted fade participation.");
}
void entityVisibilityStorageAndRuntime() {
    TempProject project;SceneDocument document(project.root);
    const auto group=document.createEmpty("Visibility group"),model=document.createBox("Hidden model");
    const auto collider=document.createCollider("Collider",model),actor=addControlledActor(document),camera=document.createCamera();
    const auto light=document.createPointLight(),particle=document.createParticle("assets/particles/visibility.crimson-particle","Particles");
    const auto environment=document.createEnvironment();
    document.select({model,actor,camera,light,particle,environment});require(document.reparentSelection(group),document.status());
    require(group&&model&&collider&&actor&&camera&&light&&particle&&environment,document.status());
    for(const auto& record:document.scene().records())require(record.visible&&document.scene().isVisible(record.id),"New entity did not start visible.");
    const auto properties=document.scene().properties();const auto settings=resolveEnvironment(document.scene());
    require(document.save("Entity visibility"),document.status());
    const auto initialFingerprint=sceneFileFingerprint(project.root,document.assetId());
    const auto before=*document.scene().find(group);auto hidden=before;hidden.visible=false;
    require(!sameEntity(before,hidden)&&document.updateEntity(group,hidden,"Hide group")&&document.dirty(),"Visibility edit was omitted from equality or history.");
    for(const auto id:document.scene().subtree(group))require(!document.scene().isVisible(id),"Hidden group left a descendant effectively visible.");
    require(document.scene().find(model)->visible&&sameProperties(properties,document.scene().properties())&&resolveGameCamera(document.scene())&&
        sameEnvironment(settings,resolveEnvironment(document.scene()))&&document.scene().find(actor)->actor&&document.scene().find(collider)->collider,
        "Visibility changed descendant choices, active camera/environment, Character or Collider data.");
    require(document.undo()&&!document.dirty()&&document.scene().isVisible(model),"Visibility Undo did not restore the clean saved hierarchy.");
    require(document.redo()&&!document.scene().isVisible(model),"Visibility Redo failed to hide descendants.");
    auto authoredModel=*document.scene().find(model);authoredModel.visible=false;require(document.updateEntity(model,authoredModel,"Hide model"),document.status());
    document.select({group});require(document.duplicateSelection(),document.status());const auto duplicate=document.selection().primary;
    require(duplicate!=group&&!document.scene().find(duplicate)->visible&&!document.scene().isVisible(duplicate),"Duplicating a hidden group lost authored visibility.");
    bool copiedModel{};for(const auto id:document.scene().subtree(duplicate)) {
        require(!document.scene().isVisible(id),"Duplicating a hidden hierarchy left a descendant visible.");
        if(document.scene().find(id)->model){copiedModel=true;require(!document.scene().find(id)->visible,"Duplicating a hierarchy lost a child's own visibility flag.");}
    }
    require(copiedModel,"Visibility duplication fixture lost its model.");
    auto shown=*document.scene().find(group);shown.visible=true;require(document.updateEntity(group,shown,"Show group"),document.status());
    require(document.scene().isVisible(actor)&&!document.scene().isVisible(model)&&!document.scene().isVisible(collider)&&!document.scene().isVisible(duplicate),
        "Showing the original group ignored child overrides or changed its independent duplicate.");
    require(document.save(),document.status());require(sceneFileFingerprint(project.root,document.assetId())!=initialFingerprint,"Visibility save left the scene fingerprint unchanged.");
    const auto snapshot=readSceneSnapshot(project.root,document.assetId());const auto serialized=Json::parse(serializeSceneSnapshot(snapshot));
    require(serialized.at("version")==18,"Entity visibility did not write scene schema 18.");
    for(const auto& entity:serialized.at("entities"))require(entity.at("visible").is_boolean(),"An entity did not write its authored visibility flag.");
    SceneDocument reopened(project.root);require(reopened.load(document.assetId()),reopened.status());same(document.scene().records(),reopened.scene().records());
    require(reopened.scene().isVisible(actor)&&!reopened.scene().isVisible(model)&&!reopened.scene().isVisible(duplicate),"Save/load lost effective hierarchy visibility.");
    SceneDocument runtime(project.root);require(runtime.resetTransientFrom(document),runtime.status());
    require(runtime.scene().find(group)->visible&&!runtime.scene().find(model)->visible&&!runtime.scene().isVisible(model),"Play copy lost authored visibility.");
    auto live=*runtime.scene().find(group);live.visible=false;require(runtime.updateEntity(group,live,"Hide during Play"),runtime.status());
    pose(runtime,actor,{3,4,5});require(runtime.undo()&&runtime.scene().isVisible(actor)&&!runtime.scene().isVisible(model),"Live visibility Undo failed to restore only its authored field.");
    near(position(runtime.scene(),actor),{3,4,5});require(runtime.redo()&&!runtime.scene().isVisible(actor),"Live visibility Redo failed.");near(position(runtime.scene(),actor),{3,4,5});
    require(document.scene().isVisible(actor)&&!document.dirty()&&sameProperties(properties,document.scene().properties()),"Play visibility edits changed the authored scene or gameplay properties.");
    for(const auto version:{12,13,14}) {
        auto legacy=legacyComposition(serialized);legacy["version"]=version;for(auto& entity:legacy["entities"])entity.erase("visible");
        const auto id="assets/scenes/legacy-visible-"+std::to_string(version)+".crimson-scene";const auto bytes=legacy.dump();
        const auto file=project.root/id;std::filesystem::create_directories(file.parent_path());{std::ofstream output(file);output<<bytes;}
        require(reopened.load(id),reopened.status());for(const auto& record:reopened.scene().records())require(record.visible&&reopened.scene().isVisible(record.id),"Missing visibility did not retain legacy all-visible behavior.");
        {std::ifstream input(file);const std::string retained{std::istreambuf_iterator<char>(input),{}};require(retained==bytes&&!reopened.dirty(),"Reading a legacy scene rewrote authored visibility or marked migration dirty.");}
    }
    const auto retained=reopened.scene().records();const auto history=reopened.history().size();
    for(const auto& value:std::array<Json,4>{Json(0),Json("false"),Json::array(),Json(nullptr)}) {
        auto invalid=serialized;invalid["entities"][0]["visible"]=value;const std::string id="assets/scenes/invalid-visibility.crimson-scene";
        {std::ofstream output(project.root/id);output<<invalid.dump();}
        require(!reopened.load(id),"A nonboolean saved visibility flag was accepted.");same(retained,reopened.scene().records());
        require(reopened.history().size()==history,"Rejected visibility load changed history.");
    }
}
void pointFalloffAndSpecularStorageAndRuntime() {
    TempProject project;SceneDocument document(project.root);const auto light=document.createPointLight();
    require(light,document.status());
    const auto initial=*document.scene().find(light);
    require(initial.point->nearFalloffRadius==0&&initial.point->specularStrength==1&&initial.point->attenuation==2,
        "Ordinary point-light defaults changed inverse-square or specular response.");
    auto edited=initial;edited.point->nearFalloffRadius=2.75f;edited.point->specularStrength=.35f;edited.point->attenuation=1.25f;
    edited.point->radius=15;edited.point->intensity=71;edited.point->castShadows=true;
    require(document.updateEntity(light,edited),document.status());
    require(document.undo()&&*document.scene().find(light)->point==*initial.point,
        "Point falloff/specular undo did not restore the original values.");
    require(document.redo()&&*document.scene().find(light)->point==*edited.point,
        "Point falloff/specular redo lost the authored values.");
    require(document.save("Point falloff"),document.status());const auto authored=document.scene().records();
    const auto serialized=Json::parse(serializeSceneSnapshot(readSceneSnapshot(project.root,document.assetId())));
    require(serialized.at("version")==18&&serialized["entities"][0]["point"].at("nearFalloffRadius")==2.75f&&
        serialized["entities"][0]["point"].at("specularStrength")==.35f&&serialized["entities"][0]["point"].at("attenuation")==1.25f,
        "Point falloff/specular fields did not persist in the current scene schema.");
    SceneDocument reopened(project.root);require(reopened.load(document.assetId()),reopened.status());same(authored,reopened.scene().records());
    const auto extracted=extractScene(reopened.scene());
    require(extracted.points.size()==1&&extracted.points.front().nearFalloffRadius==2.75f&&
        extracted.points.front().specularStrength==.35f&&extracted.points.front().intensity==71&&
        extracted.points.front().radius==15&&extracted.points.front().castShadows&&extracted.points.front().attenuation==1.25f,
        "Ordinary point extraction lost authored response or unrelated light properties.");
    auto legacy=legacyComposition(serialized);legacy["entities"][0]["point"].erase("nearFalloffRadius");
    legacy["entities"][0]["point"].erase("specularStrength");
    legacy["entities"][0]["point"].erase("attenuation");
    const std::string legacyId="assets/scenes/point-response-defaults.crimson-scene";
    {std::ofstream stream(project.root/legacyId);stream<<legacy.dump();}
    require(reopened.load(legacyId),reopened.status());
    auto expected=*edited.point;expected.nearFalloffRadius=0;expected.specularStrength=1;expected.attenuation=2;
    require(*reopened.scene().find(light)->point==expected,
        "Missing optional point response fields did not preserve legacy defaults and other properties.");
    const auto beforeInvalid=reopened.scene().records();
    for(const auto& [field,bad]:std::array<std::pair<const char*,float>,6>{{
        {"nearFalloffRadius",-.01f},{"nearFalloffRadius",100001.f},{"specularStrength",-.01f},{"specularStrength",1.01f},
        {"attenuation",-10.01f},{"attenuation",10.01f}}}) {
        auto invalid=serialized;invalid["entities"][0]["point"][field]=bad;
        const std::string invalidId="assets/scenes/invalid-point-response.crimson-scene";
        {std::ofstream stream(project.root/invalidId);stream<<invalid.dump();}
        require(!reopened.load(invalidId),"Invalid persisted point response was accepted.");same(beforeInvalid,reopened.scene().records());
    }
    for(const auto bad:{-.01f,100001.f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
        auto invalid=edited;invalid.point->nearFalloffRadius=bad;
        require(!document.updateEntity(light,invalid),"Invalid point near falloff was accepted.");
    }
    for(const auto bad:{-.01f,1.01f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
        auto invalid=edited;invalid.point->specularStrength=bad;
        require(!document.updateEntity(light,invalid),"Invalid point specular strength was accepted.");
    }
    for(const auto radius:{0.f,100000.f})for(const auto specular:{0.f,1.f}) {
        auto valid=edited;valid.point->nearFalloffRadius=radius;valid.point->specularStrength=specular;
        Scene::validate({valid},{});
    }
    for(const auto bad:{-10.01f,10.01f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
        auto invalid=edited;invalid.point->attenuation=bad;
        require(!document.updateEntity(light,invalid),"Invalid point distance attenuation was accepted.");
    }
    for(const auto attenuation:{-10.f,0.f,1.f,2.f,10.f}) {auto valid=edited;valid.point->attenuation=attenuation;Scene::validate({valid},{});}
    same(authored,document.scene().records());
    SceneDocument runtime(project.root);require(runtime.resetTransientFrom(document),runtime.status());
    require(runtime.begin("Live point response"),runtime.status());
    require(runtime.update([=](auto& records,auto&){for(auto& record:records)if(record.id==light) {
        record.point->nearFalloffRadius=5;record.point->specularStrength=.8f;record.point->attenuation=0;
    }}),runtime.status());pose(runtime,light,{9,3,2});require(runtime.commit(),runtime.status());
    require(runtime.undo()&&*runtime.scene().find(light)->point==*edited.point,
        "Live point response undo did not restore authored fields.");near(position(runtime.scene(),light),{9,3,2});
    require(runtime.redo(),runtime.status());const auto play=extractScene(runtime.scene());
    require(play.points.front().nearFalloffRadius==5&&play.points.front().specularStrength==.8f&&play.points.front().attenuation==0,
        "Play extraction did not use edited ordinary point response.");
    same(authored,document.scene().records());require(!document.dirty(),"Live point response changed the authored document.");
}
void shadowSettingsStorageHistoryAndExtraction(){
    TempProject project;SceneDocument document(project.root);
    const auto parent=document.createBox("Parent"),child=document.createBox("Child"),light=document.createPointLight();
    require(parent && child && light,document.status());
    document.select({child});require(document.reparentSelection(parent),document.status());
    auto parentRecord=*document.scene().find(parent),lightRecord=*document.scene().find(light);
    require(parentRecord.castShadows && parentRecord.receiveShadows && !lightRecord.point->castShadows &&
        lightRecord.point->shadowDepthBias==.005f && lightRecord.point->shadowNormalBias==1.f,"New shadow settings have incompatible defaults.");
    parentRecord.castShadows=false;parentRecord.receiveShadows=false;
    require(document.updateEntity(parent,parentRecord),document.status());
    lightRecord.point->castShadows=true;lightRecord.point->shadowDepthBias=.013f;lightRecord.point->shadowNormalBias=2.5f;
    lightRecord.local=math::translation({2,3,4})*math::scaling({4,2,3});
    require(document.updateEntity(light,lightRecord),document.status());
    auto extracted=extractScene(document.scene());
    require(extracted.points.size()==1 && extracted.points.front().entityId==light && extracted.points.front().castShadows &&
        extracted.points.front().shadowDepthBias==.013f && extracted.points.front().shadowNormalBias==2.5f,
        "Light extraction lost persistent identity or transformed shadow bias units.");
    for(const auto& model:extracted.models) {
        require(model.castShadows==(model.id!=parent) && model.receiveShadows==(model.id!=parent),
            "Model extraction inherited parent shadow flags or lost a local setting.");
    }
    require(document.undo() && !document.scene().find(light)->point->castShadows && document.scene().find(light)->point->shadowDepthBias==.005f,
        "Point shadow settings did not participate in history equality.");
    require(document.redo(),document.status());
    document.select({parent,light});require(document.duplicateSelection(),document.status());
    bool copiedModel=false,copiedLight=false;
    for(const auto& record:document.scene().records()) {
        if(record.id!=parent && record.name==parentRecord.name+"_001") {
            copiedModel=true;require(!record.castShadows && !record.receiveShadows,"Duplication lost model shadow flags.");
        }
        if(record.id!=light && record.point) {
            copiedLight=true;require(*record.point==*lightRecord.point,"Duplication lost point shadow settings.");
        }
    }
    require(copiedModel && copiedLight,"Shadow duplicate fixture did not copy both components.");
    require(document.save("Point shadows"),document.status());
    const auto snapshot=readSceneSnapshot(project.root,document.assetId());
    auto serialized=Json::parse(serializeSceneSnapshot(snapshot));
    require(serialized.at("version")==18,"Scene writer did not retain the current version.");
    SceneDocument reopened(project.root);require(reopened.load(document.assetId()),reopened.status());same(document.scene().records(),reopened.scene().records());
    for(const int version:{1,2,3,4}) {
        auto legacy=legacyComposition(serialized);legacy["version"]=version;
        for(auto& entity:legacy["entities"]) {
            entity.erase("castShadows");entity.erase("receiveShadows");
            if(entity.contains("point")) {
                entity["point"].erase("castShadows");entity["point"].erase("shadowDepthBias");entity["point"].erase("shadowNormalBias");
            }
        }
        if(version==1)legacy["settings"]={{"exposure",1.f},{"environment",{{"map",""},{"enabled",true},{"intensity",1.f},{"rotation",0.f}}}};
        const auto id="assets/scenes/legacy-shadows-"+std::to_string(version)+".crimson-scene";
        {std::ofstream stream(project.root/id);stream<<legacy.dump();}
        require(reopened.load(id),reopened.status());
        for(const auto& record:reopened.scene().records()) {
            require(record.castShadows && record.receiveShadows,"An older scene did not default model shadows to enabled.");
            if(record.point)require(!record.point->castShadows && record.point->shadowDepthBias==.005f && record.point->shadowNormalBias==1.f,
                "An older scene enabled point shadows or changed default bias units.");
        }
    }
    for(const float invalid:{-1.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        auto record=*document.scene().find(light);record.point->shadowDepthBias=invalid;
        require(!document.updateEntity(light,record),"Invalid depth bias entered the scene.");
        record=*document.scene().find(light);record.point->shadowNormalBias=invalid;
        require(!document.updateEntity(light,record),"Invalid normal bias entered the scene.");
    }
    const auto authored=document.scene().records();const auto authoredHistory=document.history().size();
    SceneDocument runtime(project.root);require(runtime.resetTransientFrom(document),runtime.status());
    require(runtime.begin("Runtime shadows"),runtime.status());
    require(runtime.update([&](auto& records,auto&){for(auto& record:records) {
        if(record.id==child)record.castShadows=false;
        if(record.id==light)record.point->shadowNormalBias=4.f;
    }}),runtime.status());
    pose(runtime,child,{9,0,0});pose(runtime,light,{7,5,2});require(runtime.commit(),runtime.status());
    require(runtime.undo() && runtime.scene().find(child)->castShadows && runtime.scene().find(light)->point->shadowNormalBias==2.5f,
        "Runtime shadow undo did not restore only authored fields.");
    near(position(runtime.scene(),child),{9,0,0});near(position(runtime.scene(),light),{7,5,2});
    require(runtime.redo() && !runtime.scene().find(child)->castShadows && runtime.scene().find(light)->point->shadowNormalBias==4.f,
        "Runtime shadow redo lost field patches.");
    near(position(runtime.scene(),child),{9,0,0});near(position(runtime.scene(),light),{7,5,2});
    require(runtime.begin("Cancel receive setting"),runtime.status());
    require(runtime.update([&](auto& records,auto&){for(auto& record:records)if(record.id==child)record.receiveShadows=false;}),runtime.status());
    pose(runtime,child,{12,0,0});runtime.cancel();
    require(!runtime.scene().find(child)->castShadows && runtime.scene().find(child)->receiveShadows,"Cancel restored an unrelated shadow field.");
    near(position(runtime.scene(),child),{12,0,0});
    same(authored,document.scene().records());require(document.history().size()==authoredHistory && !document.dirty(),"Runtime shadow editing changed authored state.");
    require(runtime.resetTransientFrom(document),runtime.status());same(authored,runtime.scene().records());
}
}
namespace {
void lightIndirectEnergyExtractionAndRuntime() {
    TempProject project;SceneDocument document(project.root);
    const auto point=document.createPointLight(),directional=document.createDirectionalLight(),actor=addControlledActor(document);
    require(point&&directional&&actor,document.status());
    require(document.edit("Author light GI energy",[&](auto& records,auto&){for(auto& record:records) {
        if(record.id==point)record.point->indirectEnergy=2.5f;
        if(record.id==directional)record.directional->indirectEnergy=0;
        if(record.id==actor){record.lightRig->enabled=true;record.lightRig->localWorld.indirectEnergy=4;}
    }})&&document.save("Light GI runtime"),document.status());
    const auto authored=document.scene().records();const auto authoredHistory=document.history().size();const auto authoredSelection=document.selection();
    const auto authoredCharacterLights=extractScene(document.scene()).actorLighting.front().lights;
    const auto checkEnergy=[&](const Scene& scene,float pointEnergy,float directionalEnergy,float localEnergy) {
        require(scene.find(point)->point->indirectEnergy==pointEnergy&&scene.find(directional)->directional->indirectEnergy==directionalEnergy&&
            scene.find(actor)->lightRig->localWorld.indirectEnergy==localEnergy,"Scene lost independent per-light GI tuning.");
        const auto extracted=extractScene(scene);
        require(extracted.points.size()==2&&extracted.directional.size()==1,"GI energy changed eligible source count.");
        for(const auto& source:extracted.points)require(source.indirectEnergy==(source.entityId==point?pointEnergy:localEnergy)&&
            source.enabled&&source.intensity>0,"Point or Local World GI energy was lost during extraction or changed direct illumination.");
        require(extracted.directional.front().indirectEnergy==directionalEnergy&&extracted.directional.front().enabled&&
            extracted.directional.front().intensity>0,"Directional GI energy was lost during extraction or changed direct illumination.");
        require(extracted.actorLighting.size()==1&&extracted.actorLighting.front().lights==authoredCharacterLights,
            "Per-light GI tuning changed private Character Key/Fill.");
    };
    checkEnergy(document.scene(),2.5f,0,4);
    SceneDocument runtime(project.root);require(runtime.resetTransientFrom(document)&&!runtime.dirty()&&!runtime.history().canUndo(),runtime.status());
    checkEnergy(runtime.scene(),2.5f,0,4);
    require(runtime.begin("Tune GI during Play")&&runtime.update([&](auto& records,auto&){for(auto& record:records) {
        if(record.id==point)record.point->indirectEnergy=0;
        if(record.id==directional)record.directional->indirectEnergy=3;
        if(record.id==actor)record.lightRig->localWorld.indirectEnergy=16;
    }}),runtime.status());
    pose(runtime,actor,{7,0,2});require(runtime.commit(),runtime.status());checkEnergy(runtime.scene(),0,3,16);
    require(runtime.undo(),runtime.status());checkEnergy(runtime.scene(),2.5f,0,4);near(position(runtime.scene(),actor),{7,0,2});
    require(runtime.redo(),runtime.status());checkEnergy(runtime.scene(),0,3,16);near(position(runtime.scene(),actor),{7,0,2});
    const auto committedHistory=runtime.history().size();
    require(runtime.begin("Cancel Play light GI")&&runtime.update([&](auto& records,auto&){for(auto& record:records) {
        if(record.point)record.point->indirectEnergy=8;if(record.directional)record.directional->indirectEnergy=8;
        if(record.lightRig)record.lightRig->localWorld.indirectEnergy=8;
    }}),runtime.status());
    pose(runtime,actor,{8,0,3});runtime.cancel();checkEnergy(runtime.scene(),0,3,16);near(position(runtime.scene(),actor),{8,0,3});
    require(runtime.history().size()==committedHistory,"Canceling Play GI tuning added a history command.");
    same(authored,document.scene().records());require(document.history().size()==authoredHistory&&!document.dirty()&&
        document.selection().ids==authoredSelection.ids&&document.selection().primary==authoredSelection.primary,
        "Play GI tuning changed authored data, history, dirty state or selection.");
    require(runtime.resetTransientFrom(document)&&!runtime.dirty()&&!runtime.history().canUndo(),runtime.status());checkEnergy(runtime.scene(),2.5f,0,4);
}
void environmentToneMappingStorageAndRuntime() {
    TempProject project;SceneDocument document(project.root);
    const auto environment=document.createEnvironment(),actor=addControlledActor(document);
    require(environment && actor,document.status());
    require(resolveEnvironment(document.scene()).environment.toneMapping==ToneMapping::Reinhard,"New environment changed legacy appearance.");
    const GlowSettings authoredGlow{true,2.25f,3.5f,.65f};
    const ToneMappingParameters authoredTone{4,2.5f,24,1.7f};
    auto record=*document.scene().find(environment);record.environment->environment.toneMapping=ToneMapping::AgX;
    record.environment->environment.toneMappingParameters=authoredTone;
    record.environment->environment.glow=authoredGlow;
    require(document.updateEntity(environment,record),document.status());
    require(document.undo() && resolveEnvironment(document.scene()).environment.toneMapping==ToneMapping::Reinhard,"Tone mapping equality omitted history.");
    require(resolveEnvironment(document.scene()).environment.toneMappingParameters==ToneMappingParameters{},"Tone mapping undo lost the previous curve parameters.");
    require(resolveEnvironment(document.scene()).environment.glow==GlowSettings{},"Environment undo lost the original disabled Glow defaults.");
    require(document.redo() && document.save("Tone mapping"),document.status());
    SceneDocument reopened(project.root);require(reopened.load(document.assetId()),reopened.status());
    require(resolveEnvironment(reopened.scene()).environment.toneMapping==ToneMapping::AgX,"Tone mapping did not round-trip.");
    require(resolveEnvironment(reopened.scene()).environment.toneMappingParameters==authoredTone,"Tone mapping parameters did not round-trip.");
    require(resolveEnvironment(reopened.scene()).environment.glow==authoredGlow,"Nondefault Glow did not survive scene persistence.");
    document.select({environment});require(document.duplicateSelection(),document.status());
    for(const auto& entity:document.scene().records())if(entity.environment) {
        require(entity.environment->environment.toneMapping==ToneMapping::AgX,"Environment duplication lost tone mapping.");
        require(entity.environment->environment.toneMappingParameters==authoredTone,"Environment duplication lost tone mapping parameters.");
        require(entity.environment->environment.glow==authoredGlow,"Environment duplication lost authored Glow values.");
    }
    auto legacy=legacyComposition(Json::parse(serializeSceneSnapshot(readSceneSnapshot(project.root,document.assetId()))));
    for(auto& entity:legacy["entities"])if(entity.contains("environment")) {
        entity["environment"]["environment"].erase("toneMapping");entity["environment"]["environment"].erase("toneMappingParameters");
    }
    const std::string legacyId="assets/scenes/legacy-tone.crimson-scene";
    {std::ofstream output(project.root/legacyId);output<<legacy.dump();}
    require(reopened.load(legacyId),reopened.status());
    require(resolveEnvironment(reopened.scene()).environment.toneMapping==ToneMapping::Reinhard,"Missing tone mapping did not preserve old scenes.");
    require(resolveEnvironment(reopened.scene()).environment.toneMappingParameters==ToneMappingParameters{},"Missing tone mapping parameters changed old curve defaults.");
    require(resolveEnvironment(reopened.scene()).environment.glow==authoredGlow,"Missing tone mapping reset unrelated Glow settings.");
    record.environment->environment.toneMapping=static_cast<ToneMapping>(99);
    require(!document.updateEntity(environment,record),"Invalid tone mapping entered the scene.");
    const auto authored=document.scene().records();const auto history=document.history().size();
    const auto authoredDirty=document.dirty();const auto authoredSelection=document.selection();
    SceneDocument runtime(project.root);require(runtime.resetTransientFrom(document),runtime.status());
    require(resolveEnvironment(runtime.scene()).environment.glow==authoredGlow && !runtime.dirty() && !runtime.history().canUndo(),
        "Play clone discarded Glow or inherited authored dirty/history state.");
    require(resolveEnvironment(runtime.scene()).environment.toneMappingParameters==authoredTone,"Play clone lost authored tone mapping tuning.");
    const ToneMappingParameters runtimeTone{8,4,32,2.2f};
    require(runtime.begin("Tone mapping during Play"),runtime.status());
    require(runtime.update([&](auto& records,auto&){for(auto& entity:records)if(entity.id==environment) {
        entity.environment->environment.toneMapping=ToneMapping::ACES;entity.environment->environment.toneMappingParameters=runtimeTone;}}),runtime.status());
    pose(runtime,actor,{7,0,2});require(runtime.commit(),runtime.status());
    require(extractScene(runtime.scene()).environment.environment.toneMappingParameters==runtimeTone,"Render extraction lost live tone mapping parameters.");
    require(runtime.undo() && resolveEnvironment(runtime.scene()).environment.toneMapping==ToneMapping::AgX,"Runtime tone mapping undo failed.");
    require(resolveEnvironment(runtime.scene()).environment.toneMappingParameters==authoredTone,"Runtime tone mapping undo lost authored tuning.");
    near(position(runtime.scene(),actor),{7,0,2});
    require(runtime.redo() && resolveEnvironment(runtime.scene()).environment.toneMapping==ToneMapping::ACES,"Runtime tone mapping redo failed.");
    require(resolveEnvironment(runtime.scene()).environment.toneMappingParameters==runtimeTone,"Runtime tone mapping redo lost live tuning.");
    near(position(runtime.scene(),actor),{7,0,2});
    require(resolveEnvironment(runtime.scene()).environment.glow==authoredGlow,"Runtime tone mapping changed unrelated Glow.");
    require(runtime.begin("Cancel tone mapping tuning")&&runtime.update([&](auto& records,auto&){for(auto& entity:records)if(entity.id==environment)
        entity.environment->environment.toneMappingParameters.agxContrast=3;}),runtime.status());
    pose(runtime,actor,{8,0,3});runtime.cancel();
    require(resolveEnvironment(runtime.scene()).environment.toneMappingParameters==runtimeTone,"Canceled Play tone mapping tuning changed committed parameters.");
    near(position(runtime.scene(),actor),{8,0,3});

    const GlowSettings runtimeGlow{true,4.25f,1.5f,2.5f};
    const auto beforeGlowHistory=runtime.history().size();
    require(runtime.begin("Glow during Play"),runtime.status());
    require(runtime.update([&](auto& records,auto&){for(auto& entity:records)if(entity.id==environment)
        entity.environment->environment.glow.strength=3.5f;}),runtime.status());
    pose(runtime,actor,{9,0,4});
    require(runtime.update([&](auto& records,auto&){for(auto& entity:records)if(entity.id==environment)
        entity.environment->environment.glow=runtimeGlow;}) && runtime.commit(),runtime.status());
    require(runtime.history().size()==beforeGlowHistory+1,"Glow drag did not form one runtime history command.");
    require(runtime.undo() && resolveEnvironment(runtime.scene()).environment.glow==authoredGlow,
        "Runtime Glow undo did not restore the authored values.");
    require(resolveEnvironment(runtime.scene()).environment.toneMapping==ToneMapping::ACES,"Glow undo changed an earlier tone-mapping edit.");
    near(position(runtime.scene(),actor),{9,0,4});
    require(runtime.redo() && resolveEnvironment(runtime.scene()).environment.glow==runtimeGlow,"Runtime Glow redo lost its values.");
    near(position(runtime.scene(),actor),{9,0,4});
    const auto committedHistory=runtime.history().size();
    require(runtime.begin("Cancel Glow during Play"),runtime.status());
    require(runtime.update([&](auto& records,auto&){for(auto& entity:records)if(entity.id==environment) {
        entity.environment->environment.glow.enabled=false;entity.environment->environment.glow.threshold=.1f;
    }}),runtime.status());
    pose(runtime,actor,{11,0,6});runtime.cancel();
    require(!runtime.transactionActive() && resolveEnvironment(runtime.scene()).environment.glow==runtimeGlow &&
        resolveEnvironment(runtime.scene()).environment.toneMapping==ToneMapping::ACES && runtime.history().size()==committedHistory,
        "Cancel changed committed Glow/tone mapping or added a runtime command.");
    near(position(runtime.scene(),actor),{11,0,6});
    same(authored,document.scene().records());require(document.history().size()==history,"Runtime tone mapping changed authored history.");
    require(document.dirty()==authoredDirty && document.selection().ids==authoredSelection.ids &&
        document.selection().primary==authoredSelection.primary,"Runtime Glow changed authored dirty state or selection.");
    require(runtime.resetTransientFrom(document),runtime.status());same(authored,runtime.scene().records());
    require(resolveEnvironment(runtime.scene()).environment.glow==authoredGlow &&
        resolveEnvironment(runtime.scene()).environment.toneMapping==ToneMapping::AgX && resolveEnvironment(runtime.scene()).environment.toneMappingParameters==authoredTone &&
        !runtime.history().canUndo() && !runtime.dirty(),
        "Starting Play again retained runtime Glow, tone mapping or history.");
}
void environmentAmbientLightRuntime() {
    TempProject project;SceneDocument document(project.root);
    const auto environment=document.createEnvironment(),actor=addControlledActor(document);
    const AmbientLightSettings authoredAmbient{AmbientLightSource::Sky,{.3f,.4f,.5f},.65f,1.8f};
    auto record=*document.scene().find(environment);record.environment->environment.ambientLight=authoredAmbient;
    require(document.updateEntity(environment,record),document.status());
    const auto authored=document.scene().records();const auto authoredHistory=document.history().size();
    SceneDocument runtime(project.root);require(runtime.resetTransientFrom(document),runtime.status());
    require(resolveEnvironment(runtime.scene()).environment.ambientLight==authoredAmbient,
        "Play clone lost authored ambient light settings.");
    const AmbientLightSettings runtimeAmbient{AmbientLightSource::Color,{.7f,.2f,.1f},.25f,3.2f};
    require(runtime.begin("Ambient light during Play"),runtime.status());
    require(runtime.update([&](auto& records,auto&){for(auto& entity:records)if(entity.id==environment)
        entity.environment->environment.ambientLight=runtimeAmbient;}),runtime.status());
    pose(runtime,actor,{8,0,3});require(runtime.commit(),runtime.status());
    require(extractScene(runtime.scene()).environment.environment.ambientLight==runtimeAmbient,
        "Render extraction lost live ambient source, color, contribution or energy.");
    require(runtime.undo() && resolveEnvironment(runtime.scene()).environment.ambientLight==authoredAmbient,
        "Runtime ambient light undo lost authored values.");near(position(runtime.scene(),actor),{8,0,3});
    require(runtime.redo() && resolveEnvironment(runtime.scene()).environment.ambientLight==runtimeAmbient,
        "Runtime ambient light redo lost live values.");near(position(runtime.scene(),actor),{8,0,3});
    require(runtime.begin("Cancel ambient light") && runtime.update([&](auto& records,auto&){for(auto& entity:records)if(entity.id==environment)
        entity.environment->environment.ambientLight.source=AmbientLightSource::Disabled;}),runtime.status());
    pose(runtime,actor,{9,0,4});runtime.cancel();
    require(resolveEnvironment(runtime.scene()).environment.ambientLight==runtimeAmbient,"Canceled ambient edit changed committed values.");
    near(position(runtime.scene(),actor),{9,0,4});same(authored,document.scene().records());
    require(document.history().size()==authoredHistory,"Runtime ambient light edit mutated the authored document.");
    require(runtime.resetTransientFrom(document) && resolveEnvironment(runtime.scene()).environment.ambientLight==authoredAmbient &&
        !runtime.history().canUndo() && !runtime.dirty(),"Restarting Play retained runtime ambient light edits.");
}
void environmentSsgiRuntime() {
    TempProject project;SceneDocument document(project.root);
    const auto environment=document.createEnvironment(),actor=addControlledActor(document);
    SsgiSettings authoredSsgi{true,1.4f,4.f,.15f,.8f,SsgiQuality::Custom,4};
    authoredSsgi.rays=6;authoredSsgi.steps=30;authoredSsgi.rayBias=.03f;authoredSsgi.denoiseRadius=2;authoredSsgi.denoiseStrength=.7f;authoredSsgi.radianceClamp=24;
    const SsaoSettings authoredSsao{true,1.3f,.7f,SsaoQuality::High};
    auto record=*document.scene().find(environment);record.environment->environment.ssgi=authoredSsgi;record.environment->environment.ssao=authoredSsao;
    require(document.updateEntity(environment,record),document.status());
    const auto authored=document.scene().records();const auto history=document.history().size();
    SceneDocument runtime(project.root);require(runtime.resetTransientFrom(document),runtime.status());
    require(resolveEnvironment(runtime.scene()).environment.ssgi==authoredSsgi&&resolveEnvironment(runtime.scene()).environment.ssao==authoredSsao,"Play clone lost authored GI tuning.");
    SsgiSettings liveSsgi{false,.7f,2.f,.075f,.55f,SsgiQuality::Custom,2};
    liveSsgi.rays=3;liveSsgi.steps=11;liveSsgi.rayBias=.015f;liveSsgi.denoiseRadius=1;liveSsgi.denoiseStrength=.3f;liveSsgi.radianceClamp=0;
    const SsaoSettings liveSsao{false,.5f,.2f,SsaoQuality::Low};
    require(runtime.begin("Tune GI during Play") && runtime.update([&](auto& records,auto&){for(auto& entity:records)if(entity.id==environment) {
        entity.environment->environment.ssgi=liveSsgi;entity.environment->environment.ssao=liveSsao;}}),runtime.status());
    pose(runtime,actor,{8,0,3});require(runtime.commit(),runtime.status());
    require(extractScene(runtime.scene()).environment.environment.ssgi==liveSsgi&&extractScene(runtime.scene()).environment.environment.ssao==liveSsao,"Render extraction lost live GI values.");
    require(runtime.undo() && resolveEnvironment(runtime.scene()).environment.ssgi==authoredSsgi&&resolveEnvironment(runtime.scene()).environment.ssao==authoredSsao,"Play GI undo lost authored values.");
    near(position(runtime.scene(),actor),{8,0,3});
    require(runtime.redo() && resolveEnvironment(runtime.scene()).environment.ssgi==liveSsgi&&resolveEnvironment(runtime.scene()).environment.ssao==liveSsao,"Play GI redo lost live values.");
    require(runtime.begin("Cancel live GI") && runtime.update([&](auto& records,auto&){for(auto& entity:records)if(entity.id==environment) {
        entity.environment->environment.ssgi.enabled=true;entity.environment->environment.ssao.radius=3;}}),runtime.status());
    pose(runtime,actor,{9,0,4});runtime.cancel();
    require(resolveEnvironment(runtime.scene()).environment.ssgi==liveSsgi&&resolveEnvironment(runtime.scene()).environment.ssao==liveSsao,"Canceled Play GI edit changed committed values.");
    near(position(runtime.scene(),actor),{9,0,4});same(authored,document.scene().records());
    require(document.history().size()==history,"Play GI edits mutated authored history.");
    require(runtime.resetTransientFrom(document) && resolveEnvironment(runtime.scene()).environment.ssgi==authoredSsgi&&resolveEnvironment(runtime.scene()).environment.ssao==authoredSsao &&
        !runtime.history().canUndo() && !runtime.dirty(),"Restarting Play retained runtime GI tuning or history.");
}
void lightingRolesRigStorageAndRuntime() {
    TempProject project;SceneDocument document(project.root);
    const auto player=addControlledActor(document),box=document.createBox(),point=document.createPointLight(),sun=document.createDirectionalLight(),environment=document.createEnvironment();
    require(player && box && point && sun && environment,document.status());
    require(!document.scene().find(player)->lightRig.value().enabled &&
        !document.scene().find(environment)->environment->environment.ambientVisibility.enabled,
        "Existing scenes acquired enabled player lighting or ambient attenuation.");
    require(document.scene().find(player)->lightRig.value().localWorld.intensity==64.f/9.f,
        "New Character did not receive the softened point-light default.");
    require(document.scene().find(point)->point->roles.ambientReveal && !document.scene().find(sun)->directional->roles.ambientReveal,
        "Point and directional ambient-reveal defaults lost their separate roles.");
    auto actor=*document.scene().find(player);auto& rig=actor.lightRig.value();
    rig.enabled=true;rig.fill.enabled=true;rig.showGizmos=true;
    rig.key.direction={.4f,-.7f,-.2f};rig.key.intensity=2.1f;rig.key.diffuse=.8f;rig.key.specular=.7f;
    rig.fill.color={.3f,.5f,1};rig.fill.intensity=.4f;rig.fill.diffuse=.6f;rig.fill.specular=.8f;
    rig.localWorld.height=3;rig.localWorld.intensity=17;rig.localWorld.shadowNormalBias=.8f;
    rig.localWorld.radius=12.5f;rig.localWorld.nearFalloffRadius=2.2f;rig.localWorld.specularStrength=.25f;
    rig.localWorld.rangeFalloffExponent=.75f;rig.localWorld.edgeSoftness=.8f;rig.localWorld.sourceRadius=.6f;rig.localWorld.attenuation=1.5f;
    rig.characterShadow.strength=.9f;rig.characterShadow.distance=23;rig.characterShadow.shadowDepthBias=.007f;
    actor.darknessReveal.value()={true,11};
    require(document.updateEntity(player,actor),document.status());
    auto mesh=*document.scene().find(box);mesh.lightingScopes={lighting_scope::World,0,lighting_scope::Character};
    require(document.updateEntity(box,mesh),document.status());
    auto lamp=*document.scene().find(point);lamp.point->roles={lighting_scope::Character,lighting_scope::World,lighting_scope::Character,false,.3f};
    lamp.point->rangeFalloffExponent=2.3f;lamp.point->edgeSoftness=.6f;lamp.point->sourceRadius=.4f;lamp.point->attenuation=-.5f;
    require(document.updateEntity(point,lamp),document.status());
    auto directional=*document.scene().find(sun);directional.directional->roles={lighting_scope::World,lighting_scope::Character,lighting_scope::World,true,.5f};
    require(document.updateEntity(sun,directional),document.status());
    auto sky=*document.scene().find(environment);auto& ambient=sky.environment->environment.ambientVisibility;
    ambient={true,.75f,.08f,.35f,.6f,false,true};require(document.updateEntity(environment,sky),document.status());
    require(document.save("Scoped lighting"),document.status());
    const auto authored=document.scene().records();const auto snapshot=readSceneSnapshot(project.root,document.assetId());
    SceneDocument reopened(project.root);require(reopened.load(document.assetId()),reopened.status());same(authored,reopened.scene().records());
    const auto currentJson=legacyComposition(Json::parse(serializeSceneSnapshot(snapshot)));
    for(const auto& record:currentJson["entities"]) {
        require(!record.contains("lightingCategory"),"Transient lighting membership was serialized into scene data.");
        if(record.contains("character")) {
            const auto& stored=record["character"]["lightRig"];
            require(stored.at("version")==4&&stored.contains("localWorld")&&!stored.contains("worldFill")&&
                stored["localWorld"].at("radius")==12.5f&&stored["localWorld"].at("rangeFalloffExponent")==.75f&&
                stored["localWorld"].at("edgeSoftness")==.8f&&stored["localWorld"].at("sourceRadius")==.6f&&stored["localWorld"].at("attenuation")==1.5f&&
                !stored["localWorld"].contains("softSourceRadius"),
                "Local World Light writer lost attenuation/source controls or retained the removed fill mode.");
            const auto& reveal=record["character"]["darknessReveal"];
            require(reveal.at("version")==2&&reveal.at("spread")==11&&!reveal.contains("radius"),
                "Character lighting writer retained old radius data or lost the shared spread.");
        }
        if(record.contains("point"))require(record["point"].at("sourceRadius")==.4f&&record["point"].at("attenuation")==-.5f&&
            !record["point"].contains("softSourceRadius"),
            "Point writer lost Source Radius or retained the superseded field.");
    }
    auto formerSource=currentJson;
    for(auto& record:formerSource["entities"]) {
        if(record.contains("character")) {
            auto& light=record["character"]["lightRig"]["localWorld"];
            light["softSourceRadius"]=light.at("sourceRadius");light.erase("sourceRadius");
        }
        if(record.contains("point")) {
            auto& light=record["point"];light["softSourceRadius"]=light.at("sourceRadius");light.erase("sourceRadius");
        }
    }
    const std::string formerSourceId="assets/scenes/former-source-radius.crimson-scene";
    {std::ofstream file(project.root/formerSourceId);file<<formerSource.dump();}
    require(reopened.load(formerSourceId),reopened.status());same(authored,reopened.scene().records());
    require(reopened.save()&&reopened.load(formerSourceId),reopened.status());same(authored,reopened.scene().records());
    const auto upgradedSource=legacyComposition(Json::parse(serializeSceneSnapshot(readSceneSnapshot(project.root,formerSourceId))));
    for(const auto& record:upgradedSource["entities"]) {
        if(record.contains("character"))require(record["character"]["lightRig"]["localWorld"].contains("sourceRadius")&&
            !record["character"]["lightRig"]["localWorld"].contains("softSourceRadius"),"Rig source-size conversion was not written once.");
        if(record.contains("point"))require(record["point"].contains("sourceRadius")&&!record["point"].contains("softSourceRadius"),
            "Ordinary point source-size conversion was not written once.");
    }
    auto absentSource=currentJson;
    for(auto& record:absentSource["entities"]) {
        if(record.contains("character")) {
            record["character"]["lightRig"]["localWorld"].erase("sourceRadius");
            record["character"]["lightRig"]["localWorld"].erase("attenuation");
        }
        if(record.contains("point")){record["point"].erase("sourceRadius");record["point"].erase("attenuation");}
    }
    const std::string absentSourceId="assets/scenes/absent-source-radius.crimson-scene";
    {std::ofstream file(project.root/absentSourceId);file<<absentSource.dump();}
    require(reopened.load(absentSourceId),reopened.status());
    require(reopened.scene().find(player)->lightRig.value().localWorld.sourceRadius==0&&
        reopened.scene().find(point)->point->sourceRadius==0&&reopened.scene().find(point)->point->attenuation==2&&
        reopened.scene().find(player)->lightRig.value().localWorld.attenuation==2,
        "Missing source sizes or attenuation changed the punctual inverse-square defaults.");
    const auto writeAndLoadReveal=[&](const Json& reveal,const char* fileName) {
        auto data=currentJson;
        for(auto& record:data["entities"])if(record.contains("character"))record["character"]["darknessReveal"]=reveal;
        const std::string id=std::string("assets/scenes/")+fileName+".crimson-scene";
        {std::ofstream file(project.root/id);file<<data.dump();}
        return reopened.load(id);
    };
    for(const auto& old:std::array<Json,2>{Json{{"enabled",false},{"radius",6.5f}},Json{{"version",1},{"enabled",false},{"radius",6.5f}}}) {
        require(writeAndLoadReveal(old,"old-character-radius"),reopened.status());
        require(reopened.scene().find(player)->darknessReveal.value()==DarknessRevealSettings{false,6.5f},
            "Legacy reveal radius did not preserve enable state and numeric spread during conversion.");
        require(reopened.save(),reopened.status());
        const auto migratedReveal=legacyComposition(Json::parse(serializeSceneSnapshot(readSceneSnapshot(project.root,reopened.assetId()))));
        for(const auto& entity:migratedReveal["entities"])if(entity.contains("character")) {
            const auto& value=entity["character"]["darknessReveal"];
            require(value.at("version")==2&&value.at("spread")==6.5f&&!value.contains("radius"),
                "Saving a migrated character reveal retained radius or lost spread.");
        }
    }
    require(writeAndLoadReveal(Json{{"version",2},{"enabled",true}},"missing-character-spread"),reopened.status());
    require(reopened.scene().find(player)->darknessReveal.value()==DarknessRevealSettings{},
        "Missing current spread lost the default Character lighting scale.");
    for(const auto& bad:std::array<Json,4>{Json{{"version",3},{"spread",11}},Json{{"version",2},{"spread",0}},
        Json{{"version",2},{"spread",100001}},Json{{"version",1},{"radius",-1}}}) {
        const auto prior=reopened.scene().records();const auto history=reopened.history().size();
        require(!writeAndLoadReveal(bad,"invalid-character-spread"),"Invalid saved Character spread/version was accepted.");
        same(prior,reopened.scene().records());require(reopened.history().size()==history,"Invalid spread load changed history.");
    }
    auto missingLocalControls=currentJson;
    for(auto& record:missingLocalControls["entities"])if(record.contains("character")) {
        auto& stored=record["character"]["lightRig"];stored["version"]=2;
        stored["localWorld"]["radius"]=13;
        stored["localWorld"].erase("nearFalloffRadius");stored["localWorld"].erase("specularStrength");
    }
    const std::string missingLocalControlsId="assets/scenes/local-world-v2-defaults.crimson-scene";
    {std::ofstream stream(project.root/missingLocalControlsId);stream<<missingLocalControls.dump();}
    require(reopened.load(missingLocalControlsId),reopened.status());
    auto expectedOldV2=rig;expectedOldV2.localWorld.radius=13;expectedOldV2.localWorld.nearFalloffRadius=1.5f;
    expectedOldV2.localWorld.specularStrength=0;expectedOldV2.localWorld.rangeFalloffExponent=4;
    expectedOldV2.localWorld.edgeSoftness=1;expectedOldV2.localWorld.sourceRadius=0;
    require(reopened.scene().find(player)->lightRig.value()==expectedOldV2,
        "An existing v2 rig did not preserve its actual point-light settings.");
    require(reopened.save(),reopened.status());
    require(reopened.load(missingLocalControlsId)&&reopened.scene().find(player)->lightRig.value()==expectedOldV2,
        "Migrated Local World Light did not survive Save/reopen.");
    const auto savedV3=legacyComposition(Json::parse(serializeSceneSnapshot(readSceneSnapshot(project.root,missingLocalControlsId))));
    for(const auto& entity:savedV3["entities"])
        if(entity.contains("character"))require(entity["character"]["lightRig"].at("version")==4&&
            entity["character"]["lightRig"].contains("localWorld")&&!entity["character"]["lightRig"].contains("worldFill"),
            "Saving v2 did not write the current point-light representation.");
    auto customV2=missingLocalControls;
    for(auto& record:customV2["entities"])if(record.contains("character")) {
        record["character"]["lightRig"]["localWorld"]["nearFalloffRadius"]=2.25f;
        record["character"]["lightRig"]["localWorld"]["specularStrength"]=.4f;
    }
    const std::string customV2Id="assets/scenes/local-world-v2-custom.crimson-scene";
    {std::ofstream stream(project.root/customV2Id);stream<<customV2.dump();}
    require(reopened.load(customV2Id),reopened.status());
    auto expectedCustomV2=expectedOldV2;expectedCustomV2.localWorld.nearFalloffRadius=2.25f;expectedCustomV2.localWorld.specularStrength=.4f;
    require(reopened.scene().find(player)->lightRig.value()==expectedCustomV2,
        "Explicit v2 near falloff changed foot energy or unrelated rig settings during conversion.");
    auto oldFill=currentJson;
    for(auto& record:oldFill["entities"])if(record.contains("character")) {
        auto& stored=record["character"]["lightRig"];stored["version"]=3;
        stored["worldFill"]=stored["localWorld"];stored.erase("localWorld");
    }
    const std::string oldFillId="assets/scenes/world-fill-v3-migration.crimson-scene";
    {std::ofstream stream(project.root/oldFillId);stream<<oldFill.dump();}
    require(reopened.load(oldFillId),reopened.status());
    auto expectedOldFill=rig;auto& convertedFill=expectedOldFill.localWorld;
    convertedFill.radius=22;convertedFill.nearFalloffRadius=1.5f;convertedFill.specularStrength=0;
    convertedFill.rangeFalloffExponent=1;convertedFill.edgeSoftness=1;convertedFill.sourceRadius=0;convertedFill.attenuation=2;
    const float centerHeight=characterLightingCenterHeight(resolveAgentTraversal(*actor.controller)->height);
    const float oldFootEnergy=17*ambient_visibility::radialRevealWeightSquared(centerHeight*centerHeight,11);
    convertedFill.intensity=oldFootEnergy/point_lighting::pointLightAttenuation(9,1.f/(22*22),2.25f,1,1);
    const auto migratedFill=reopened.scene().find(player)->lightRig.value();
    auto migratedWithoutIntensity=migratedFill;auto expectedWithoutIntensity=expectedOldFill;
    migratedWithoutIntensity.localWorld.intensity=expectedWithoutIntensity.localWorld.intensity=0;
    require(migratedWithoutIntensity==expectedWithoutIntensity&&
        std::abs(migratedFill.localWorld.intensity-convertedFill.intensity)<1e-4f,
        "V3 fill migration lost foot brightness, independent reveal spread or unrelated rig fields.");
    require(reopened.save()&&reopened.load(oldFillId),reopened.status());
    require(reopened.scene().find(player)->lightRig.value()==migratedFill,
        "V3-to-v4 point-light migration was applied more than once.");
    auto narrowFill=oldFill;
    for(auto& record:narrowFill["entities"])if(record.contains("character")) {
        record["character"]["lightRig"]["worldFill"]["height"]=2;
        record["character"]["darknessReveal"]["spread"]=.5f;
    }
    const std::string narrowFillId="assets/scenes/world-fill-v3-narrow-spread.crimson-scene";
    {std::ofstream stream(project.root/narrowFillId);stream<<narrowFill.dump();}
    require(reopened.load(narrowFillId),reopened.status());
    const auto narrowPoint=reopened.scene().find(player)->lightRig.value().localWorld;
    const float footAttenuation=point_lighting::pointLightAttenuation(narrowPoint.height*narrowPoint.height,
        1.f/(narrowPoint.radius*narrowPoint.radius),narrowPoint.nearFalloffRadius*narrowPoint.nearFalloffRadius,
        narrowPoint.rangeFalloffExponent,narrowPoint.edgeSoftness);
    const float narrowFootEnergy=17*ambient_visibility::radialRevealWeightSquared(centerHeight*centerHeight,.5f);
    require(narrowPoint.radius==2.5f&&narrowPoint.height==2&&std::isfinite(narrowPoint.intensity)&&
        narrowPoint.intensity>0&&narrowPoint.intensity<1000000&&footAttenuation>0&&
        std::abs(narrowPoint.intensity*footAttenuation-narrowFootEnergy)<1e-5f,
        "A narrow v3 spread migrated to a point that cannot reach the feet or needlessly saturates intensity.");
    auto extremeV2=customV2;
    for(auto& record:extremeV2["entities"])if(record.contains("character")) {
        auto& old=record["character"]["lightRig"]["localWorld"];
        old["height"]=.01f;old["intensity"]=10000;old["radius"]=100000;old["nearFalloffRadius"]=0;
    }
    const std::string extremeV2Id="assets/scenes/local-world-v2-extreme.crimson-scene";
    {std::ofstream stream(project.root/extremeV2Id);stream<<extremeV2.dump();}
    require(reopened.load(extremeV2Id)&&reopened.scene().find(player)->lightRig.value().localWorld.intensity==10000&&
        reopened.scene().find(player)->lightRig.value().localWorld.nearFalloffRadius==0,
        "Valid extreme v2 authored point settings were changed during migration.");
    const auto beforeInvalidLoad=reopened.scene().records();const auto invalidLoadId="assets/scenes/invalid-local-world.crimson-scene";
    for(const auto& [field,bad]:std::array<std::pair<const char*,float>,4>{{
        {"nearFalloffRadius",-.01f},{"nearFalloffRadius",100001.f},{"specularStrength",-.01f},{"specularStrength",1.01f}}}) {
        auto invalidData=missingLocalControls;
        for(auto& record:invalidData["entities"])if(record.contains("character"))record["character"]["lightRig"]["localWorld"][field]=bad;
        {std::ofstream stream(project.root/invalidLoadId);stream<<invalidData.dump();}
        require(!reopened.load(invalidLoadId),"Invalid superseded local world control was ignored by migration.");
        same(beforeInvalidLoad,reopened.scene().records());
    }
    auto pointRig=legacyComposition(Json::parse(serializeSceneSnapshot(snapshot)));
    for(auto& record:pointRig["entities"])if(record.contains("character")) {
        record["character"].erase("darknessReveal");
        record["character"]["lightRig"]={{"enabled",true},{"showGizmos",true},
            {"key",{{"enabled",true},{"worldOffset",Vec3{-3,5,3}},{"color",Vec3{1,.8f,.6f}},{"intensity",40},
                {"radius",12},{"castShadows",true}}},
            {"fill",{{"enabled",true},{"worldOffset",Vec3{3,3,-2}},{"color",Vec3{.3f,.5f,1}},{"intensity",10}}},
            {"reveal",{{"enabled",true},{"radius",9},{"strength",1}}}};
    }
    const std::string pointRigId="assets/scenes/old-point-rig.crimson-scene";
    {std::ofstream stream(project.root/pointRigId);stream<<pointRig.dump();}
    require(reopened.load(pointRigId),reopened.status());
    const auto converted=reopened.scene().find(player)->lightRig.value();
    const auto formerKeyEnergy=40*point_lighting::pointLightAttenuation(43,1.f/144);
    const auto formerFillEnergy=10*point_lighting::pointLightAttenuation(22,1.f/64);
    require(converted.enabled&&converted.showGizmos&&std::abs(converted.key.intensity-formerKeyEnergy)<1e-5f&&
        converted.fill.enabled&&std::abs(converted.fill.intensity-formerFillEnergy)<1e-5f&&
        converted.localWorld.castShadows&&converted.characterShadow.enabled,
        "Former point rig conversion changed character irradiance or shadow settings.");
    require(reopened.scene().find(player)->darknessReveal.value()==DarknessRevealSettings{true,9},
        "Old player reveal radius was not converted to the shared Character lighting spread.");
    near(converted.key.direction,math::normalized(Vec3{3,-5,-3}));
    require(std::abs(converted.localWorld.intensity*point_lighting::pointLightAttenuation(
        converted.localWorld.height*converted.localWorld.height,1.f/(converted.localWorld.radius*converted.localWorld.radius),
        converted.localWorld.nearFalloffRadius*converted.localWorld.nearFalloffRadius,
        converted.localWorld.rangeFalloffExponent,converted.localWorld.edgeSoftness)-formerKeyEnergy)<1e-5f,
        "Former point rig conversion changed the world source's irradiance at the player's feet.");
    require(extractScene(reopened.scene()).points.size()==2,
        "Separated rig created more than one world point source.");
    const auto rewritten=legacyComposition(Json::parse(serializeSceneSnapshot(readSceneSnapshot(project.root,pointRigId))));
    for(const auto& record:rewritten["entities"])if(record.contains("character")) {
        const auto& written=record["character"]["lightRig"];
        require(written.at("version")==4&&written.contains("key")&&written.contains("fill")&&written.contains("localWorld")&&
            !written.contains("worldFill")&&written["localWorld"].contains("radius")&&
            written["localWorld"].contains("nearFalloffRadius")&&written["localWorld"].contains("specularStrength")&&
            !written.contains("actorLights")&&!written.contains("reveal")&&!written.contains("mode")&&
            !written["key"].contains("worldOffset")&&!written["key"].contains("radius")&&
            !written["characterShadow"].contains("direction"),
            "Scene writer retained replaced point/directional or independent shadow direction settings.");
    }
    auto directionalRig=legacyComposition(Json::parse(serializeSceneSnapshot(snapshot)));
    const auto formerKeyDirection=math::normalized(Vec3{4,-7,-2});
    for(auto& record:directionalRig["entities"])if(record.contains("character")) {
        record["character"]["lightRig"]={{"enabled",true},{"showGizmos",true},{"actorLights",Json::array({
            {{"enabled",true},{"direction",formerKeyDirection},{"color",Vec3{1,.8f,.6f}},{"intensity",2.f},{"diffuse",.7f},{"specular",.4f}},
            {{"enabled",true},{"direction",Vec3{0,-1,0}},{"color",Vec3{.3f,.5f,1}},{"intensity",.5f}},
            {{"enabled",true},{"direction",Vec3{0,0,1}},{"color",Vec3{1,1,1}},{"intensity",1.f}}})}};
    }
    const std::string directionalRigId="assets/scenes/replaced-directional-rig.crimson-scene";
    {std::ofstream stream(project.root/directionalRigId);stream<<directionalRig.dump();}
    require(reopened.load(directionalRigId),reopened.status());
    const auto migrated=reopened.scene().find(player)->lightRig.value();
    require(migrated.enabled&&migrated.showGizmos&&migrated.key.intensity==2&&
        migrated.fill.enabled&&migrated.fill.intensity==.5f&&migrated.key.color==Vec3{1,.8f,.6f}&&
        migrated.key.diffuse==.7f&&migrated.key.specular==.4f&&!migrated.localWorld.enabled,
        "Former character-light rig failed one-time conversion or acquired world illumination.");
    near(migrated.key.direction,formerKeyDirection);
    require(reopened.scene().find(player)->darknessReveal.value()==actor.darknessReveal.value(),
        "Directional rig conversion changed the independently authored darkness range.");
    require(extractScene(reopened.scene()).points.size()==1,"Removed directional rim remained as an extra local source.");
    const auto convertedJson=legacyComposition(Json::parse(serializeSceneSnapshot(readSceneSnapshot(project.root,directionalRigId))));
    for(const auto& record:convertedJson["entities"])if(record.contains("character"))
        require(!record["character"]["lightRig"].contains("actorLights"),"Writer retained replaced actor-light data.");
    auto legacy=legacyComposition(Json::parse(serializeSceneSnapshot(snapshot)));
    for(auto& record:legacy["entities"]) {
        record.erase("lightingScopes");
        if(record.contains("point"))record["point"].erase("roles");
        if(record.contains("directional"))record["directional"].erase("roles");
        if(record.contains("character")){record["character"].erase("lightRig");record["character"].erase("darknessReveal");}
        if(record.contains("environment"))record["environment"]["environment"].erase("ambientVisibility");
    }
    const std::string legacyId="assets/scenes/legacy-lighting.crimson-scene";
    {std::ofstream stream(project.root/legacyId);stream<<legacy.dump();}
    require(reopened.load(legacyId),reopened.status());
    require(reopened.scene().find(box)->lightingScopes==SurfaceLightingScopes{} &&
        !reopened.scene().find(player)->lightRig.value().enabled &&
        reopened.scene().find(player)->darknessReveal.value()==DarknessRevealSettings{} &&
        !reopened.scene().find(environment)->environment->environment.ambientVisibility.enabled &&
        reopened.scene().find(point)->point->roles==ScenePointLight{}.roles &&
        reopened.scene().find(sun)->directional->roles==LightRoles{},"Missing optional lighting fields changed legacy defaults.");
    auto invalid=mesh;invalid.lightingScopes.directReceiverMask=4;require(!document.updateEntity(box,invalid),"Unknown surface-lighting bits were accepted.");
    invalid=lamp;invalid.point->roles.shadowCasterMask=4;require(!document.updateEntity(point,invalid),"Unknown caster bits were accepted.");
    invalid=lamp;invalid.point->roles.ambientRevealStrength=1.01f;require(!document.updateEntity(point,invalid),"Out-of-range ambient reveal strength was accepted.");
    invalid=actor;invalid.lightRig.value().fill.enabled=false;invalid.lightRig.value().fill.diffuse=-1;
    require(!document.updateEntity(player,invalid),"Disabled rig light retained invalid stored fields.");
    invalid=actor;invalid.lightRig.value().key.direction[0]=std::numeric_limits<float>::quiet_NaN();
    require(!document.updateEntity(player,invalid),"Nonfinite character light direction was accepted.");
    for(const auto bad:{-.01f,1000001.f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
        invalid=actor;invalid.lightRig.value().localWorld.intensity=bad;
        require(!document.updateEntity(player,invalid),"Invalid Local World Light intensity was accepted.");
    }
    for(const auto& [field,bad]:std::array<std::pair<const char*,float>,9>{{
        {"radius",0.f},{"nearFalloffRadius",-1.f},{"rangeFalloffExponent",.24f},{"rangeFalloffExponent",8.01f},
        {"edgeSoftness",.049f},{"sourceRadius",-1.f},{"sourceRadius",100001.f},{"attenuation",-10.01f},{"attenuation",10.01f}}}) {
        auto invalidData=currentJson;
        for(auto& record:invalidData["entities"])if(record.contains("character"))
            record["character"]["lightRig"]["localWorld"][field]=bad;
        {std::ofstream stream(project.root/invalidLoadId);stream<<invalidData.dump();}
        const auto before=reopened.scene().records();
        require(!reopened.load(invalidLoadId),"Invalid current point attenuation/source settings were accepted.");
        same(before,reopened.scene().records());
    }
    invalid=actor;invalid.darknessReveal.value().spread=0;
    require(!document.updateEntity(player,invalid),"Invalid character darkness range was accepted.");
    invalid=sky;invalid.environment->environment.ambientVisibility.transition=.0001f;
    require(!document.updateEntity(environment,invalid),"Degenerate ambient transition was accepted.");
    same(authored,document.scene().records());
    SceneDocument runtime(project.root);require(runtime.resetTransientFrom(document),runtime.status());
    const auto changeRig=[&](float intensity) {
        return [=](auto& records,auto&) {for(auto& record:records)if(record.id==player) {
            record.lightRig.value().key.intensity=intensity;
            record.lightRig.value().localWorld.intensity=intensity*.01f;
        }};
    };
    const auto beforeHistory=runtime.history().size();require(runtime.begin("Live player lighting"),runtime.status());
    require(runtime.update(changeRig(99)),runtime.status());pose(runtime,player,{9,0,3});
    require(runtime.update(changeRig(120)) && runtime.commit(),runtime.status());
    require(runtime.history().size()==beforeHistory+1 && runtime.scene().find(player)->lightRig.value().key.intensity==120,
        "Live rig drag did not create one semantic command.");
    require(runtime.undo() && runtime.scene().find(player)->lightRig.value()==rig,"Rig undo did not restore exact nested settings.");
    near(position(runtime.scene(),player),{9,0,3});
    require(runtime.redo() && runtime.scene().find(player)->lightRig.value().key.intensity==120,"Rig redo lost the committed value.");
    require(runtime.scene().find(player)->lightRig.value().localWorld.intensity==120.f*.01f,
        "Rig redo lost Local World Light intensity.");
    const auto playLighting=extractScene(runtime.scene());
    require(playLighting.points.size()==2&&playLighting.points.back().intensity==120.f*.01f&&
        playLighting.points.back().radius==rig.localWorld.radius&&playLighting.points.back().rangeFalloffExponent==.75f&&
        playLighting.points.back().edgeSoftness==.8f&&playLighting.points.back().sourceRadius==.6f&&playLighting.points.back().attenuation==1.5f,
        "Play extraction dropped Local World Light attenuation/source controls.");
    require(runtime.begin("Cancel live rig"),runtime.status());require(runtime.update(changeRig(240)),runtime.status());
    pose(runtime,player,{10,0,4});runtime.cancel();
    require(runtime.scene().find(player)->lightRig.value().key.intensity==120,"Cancel did not restore the prior rig settings.");
    near(position(runtime.scene(),player),{10,0,4});
    auto changedReveal=*runtime.scene().find(player);changedReveal.darknessReveal.value()={false,4};
    require(runtime.updateEntity(player,changedReveal),runtime.status());pose(runtime,player,{11,0,5});
    const auto revealDisabled=extractScene(runtime.scene());
    require(revealDisabled.reveals.empty()&&revealDisabled.points.back().radius==rig.localWorld.radius,
        "Disabling Darkness Reveal resized the independent Local World Light.");
    require(runtime.undo()&&runtime.scene().find(player)->darknessReveal.value()==actor.darknessReveal.value(),
        "Darkness range undo did not restore the exact character settings.");near(position(runtime.scene(),player),{11,0,5});
    auto runtimeMesh=*runtime.scene().find(box);runtimeMesh.lightingScopes.shadowCasterMask=lighting_scope::All;
    require(runtime.updateEntity(box,runtimeMesh),runtime.status());
    pose(runtime,box,{2,3,4});require(runtime.undo() && runtime.scene().find(box)->lightingScopes==mesh.lightingScopes,
        "Scope undo lost independently authored masks.");near(position(runtime.scene(),box),{2,3,4});
    same(authored,document.scene().records());require(!document.dirty(),"Live rig controls changed the authored scene.");
}
void localRigSharesPointSourceBudget() {
    std::vector<EntityRecord> records;records.reserve(MaximumPointLights+MaximumDirectionalLights+1);
    EntityRecord player;player.id=1;player.name="Player";player.actor.emplace();player.controller.emplace();player.motor.emplace();player.navigation.emplace();player.playerInput.emplace();player.lightRig.emplace();player.darknessReveal.emplace();player.lightRig.value().enabled=true;
    player.lightRig.value().fill.enabled=true;
    records.push_back(player);
    for(std::size_t index=0;index<MaximumPointLights-1;++index) {
        EntityRecord light;light.id=index+2;light.name="Light";light.point.emplace();records.push_back(std::move(light));
    }
    for(std::size_t index=0;index<MaximumDirectionalLights;++index) {
        EntityRecord light;light.id=index+MaximumPointLights+2;light.name="Sun";light.directional.emplace();records.push_back(std::move(light));
    }
    const SceneProperties properties{0,0,player.id};Scene::validate(records,properties);
    Scene scene;scene.apply(records,properties);const auto extracted=extractScene(scene);
    require(extracted.points.size()==MaximumPointLights && extracted.directional.size()==MaximumDirectionalLights,
        "Local rig failed at the exact combined point-source capacity.");
    EntityRecord overflow;overflow.id=MaximumPointLights+MaximumDirectionalLights+2;overflow.name="Overflow light";overflow.point.emplace();
    auto tooMany=records;tooMany.push_back(overflow);
    bool rejected=false;try {scene.apply(tooMany,properties);}catch(const std::runtime_error&){rejected=true;}
    require(rejected,"Scene validation admitted combined local/authored point-source overflow.");
    same(scene.records(),records);
    tooMany.front().lightRig.value().localWorld.enabled=false;
    Scene::validate(tooMany,properties); // Character lights/shadow use no point slots.
    tooMany.front().lightRig.value().localWorld.enabled=true;
    tooMany.front().lightRig.value().enabled=false;
    Scene::validate(tooMany,properties); // The rig's master toggle gates its cost.
    tooMany.front().lightRig.value().enabled=true;
    auto noPlayer=properties;noPlayer.playerCharacter=NoEntity;
    bool noPlayerRejected=false;try{Scene::validate(tooMany,noPlayer);}catch(const std::runtime_error&){noPlayerRejected=true;}
    require(noPlayerRejected,"An undesignated actor rig escaped the shared point-source budget.");

    TempProject project;SceneDocument document(project.root);
    const SceneSnapshot snapshot{"assets/scenes/local-rig-capacity.crimson-scene","Local rig capacity",records,properties,{}};
    require(document.openPreparedSnapshot(snapshot),document.status());
    document.select({player.id});const auto history=document.history().size(),generation=document.documentGeneration();
    const auto assetId=document.assetId();const auto dirty=document.dirty();
    require(!document.createPointLight()&&!document.transactionActive(),"Point creation published combined rig overflow.");
    same(document.scene().records(),records);
    require(document.history().size()==history&&document.documentGeneration()==generation&&document.assetId()==assetId&&
        document.dirty()==dirty&&document.selection().primary==player.id,"Rejected point creation changed document history, identity or selection.");

    // A malformed saved scene must be rejected before replacing the open scene.
    auto overflowJson=Json::parse(serializeSceneSnapshot(snapshot));
    auto extraJson=*std::find_if(overflowJson["entities"].begin(),overflowJson["entities"].end(),[](const auto& value){return value.contains("point");});
    extraJson["id"]="000000000000ffff";overflowJson["entities"].push_back(std::move(extraJson));
    std::filesystem::create_directories(project.root/"assets/scenes");
    const std::string overflowId="assets/scenes/overflow-rig-capacity.crimson-scene";
    {std::ofstream stream(project.root/overflowId);stream<<overflowJson.dump();}
    require(!document.load(overflowId),"Loading combined point/rig overflow succeeded.");
    same(document.scene().records(),records);
    require(document.history().size()==history&&document.documentGeneration()==generation&&document.assetId()==assetId&&
        document.dirty()==dirty&&document.selection().primary==player.id,"Rejected scene load changed document history, identity or selection.");

    // Enabling the world source or designating a player must reject overflow atomically too.
    tooMany.front().lightRig.value().localWorld.enabled=false;
    const SceneSnapshot oneLight{"assets/scenes/character-only-rig.crimson-scene","Character-only rig",tooMany,properties,{}};
    require(document.openPreparedSnapshot(oneLight),document.status());
    auto fullRig=*document.scene().find(player.id);fullRig.lightRig.value().localWorld.enabled=true;
    const auto oneLightHistory=document.history().size();
    require(!document.updateEntity(player.id,fullRig),"Enabling the Local World Light published an overflow scene.");
    same(document.scene().records(),tooMany);
    require(document.history().size()==oneLightHistory,"Rejected rig activation added an undo entry.");
    tooMany.front().lightRig.value().localWorld.enabled=true;
    const SceneSnapshot inactiveRig{"assets/scenes/undesignated-rig.crimson-scene","Undesignated rig",tooMany,noPlayer,{}};
    require(!document.openPreparedSnapshot(inactiveRig),"An undesignated actor rig published combined point-source overflow.");
    auto lastGood=tooMany;lastGood.front().lightRig->localWorld.enabled=false;
    same(document.scene().records(),lastGood);
}
void modelAnimationStorageAndRuntime() {
    TempProject project;SceneDocument document(project.root);
    const auto model=document.createModel("assets/models/animated/manifest.json","Animated model"),empty=document.createEmpty("Empty");
    require(model&&empty,document.status());
    require(document.scene().find(model)->modelAnimation==SceneModelAnimation{},"New models did not retain the imported bind pose by default.");
    auto authored=*document.scene().find(model);authored.modelAnimation={2,false,false,1.5f,2.25f};
    require(document.updateEntity(model,authored,"Configure animation"),document.status());
    require(extractScene(document.scene()).models.front().animation==authored.modelAnimation,"Render extraction lost authored playback settings.");
    require(document.undo()&&document.scene().find(model)->modelAnimation==SceneModelAnimation{}&&document.redo(),
        "Animation controls did not participate in scene history.");
    require(document.save("Animated scene"),document.status());
    const auto snapshot=readSceneSnapshot(project.root,document.assetId());const auto json=Json::parse(serializeSceneSnapshot(snapshot));
    require(json.at("version")==18,"Animation writer did not use scene schema 18.");
    SceneDocument reopened(project.root);require(reopened.load(document.assetId()),reopened.status());
    same(document.scene().records(),reopened.scene().records());
    const auto history=document.history().size();const auto records=document.scene().records();
    const auto rejectsRecord=[&](const EntityRecord& bad) {
        require(!document.updateEntity(bad.id,bad),"Invalid animation settings were published.");
        same(document.scene().records(),records);require(document.history().size()==history,"Rejected animation edit added history.");
    };
    auto invalid=authored;invalid.modelAnimation.clip=-2;rejectsRecord(invalid);
    for(const auto speed:{-.01f,100.01f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
        invalid=authored;invalid.modelAnimation.speed=speed;rejectsRecord(invalid);
    }
    for(const auto time:{-.01f,1.1e9f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
        invalid=authored;invalid.modelAnimation.startTime=time;rejectsRecord(invalid);
    }
    invalid=*document.scene().find(empty);invalid.modelAnimation.clip=0;rejectsRecord(invalid);
    auto legacy=json;legacy["version"]=9;for(auto& entity:legacy["entities"])entity.erase("modelAnimation");
    const std::string legacyId="assets/scenes/legacy-animation.crimson-scene";const auto legacyBytes=legacy.dump();
    {std::ofstream file(project.root/legacyId);file<<legacyBytes;}
    require(reopened.load(legacyId)&&reopened.scene().find(model)->modelAnimation==SceneModelAnimation{},
        "Existing scenes acquired an active animation or lost bind-pose defaults.");
    for(const auto& clip:std::array<Json,3>{Json(-2),Json(1.5),Json(std::numeric_limits<std::uint64_t>::max())}) {
        auto malformed=json;for(auto& entity:malformed["entities"])if(entity.contains("modelAnimation"))entity["modelAnimation"]["clip"]=clip;
        const std::string id="assets/scenes/invalid-animation.crimson-scene";{std::ofstream file(project.root/id);file<<malformed.dump();}
        const auto before=reopened.scene().records();require(!reopened.load(id),"Invalid saved animation clip was accepted.");same(before,reopened.scene().records());
    }
    SceneDocument runtime(project.root);require(runtime.resetTransientFrom(document),runtime.status());
    auto live=*runtime.scene().find(model);live.modelAnimation.playing=true;live.modelAnimation.speed=2;
    require(runtime.updateEntity(model,live),runtime.status());pose(runtime,model,{3,4,5});
    require(runtime.undo()&&runtime.scene().find(model)->modelAnimation==authored.modelAnimation,
        "Live animation undo did not restore exact authored controls.");near(position(runtime.scene(),model),{3,4,5});
    same(document.scene().records(),records);require(!document.dirty(),"Live animation controls changed the authored scene.");
    document.select({model});require(document.duplicateSelection(),document.status());
    require(document.scene().find(document.selection().primary)->modelAnimation==authored.modelAnimation,
        "Duplicating a model lost its playback settings.");
}
}
int main(){try{composedPlayerRecipeAndMigration();nativeColliderStorageAndValidation();componentsAndStorage();collisionDisplayStorageAndHistory();unifiedColliderStorageHistoryAndCompatibility();runtimePatches();followAndHierarchy();followDependenciesAndAtomicFailure();parentFollowKeepsWorldOffset();boxMaterialAndPointRadius();importedMaterialSlots();modelFadeParticipationStorageAndRuntime();entityVisibilityStorageAndRuntime();pointFalloffAndSpecularStorageAndRuntime();shadowSettingsStorageHistoryAndExtraction();lightIndirectEnergyExtractionAndRuntime();environmentToneMappingStorageAndRuntime();environmentAmbientLightRuntime();environmentSsgiRuntime();lightingRolesRigStorageAndRuntime();localRigSharesPointSourceBudget();modelAnimationStorageAndRuntime();std::cout<<"Gameplay scene tests passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
