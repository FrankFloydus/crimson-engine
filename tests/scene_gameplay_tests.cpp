#include "assets/AnimationAsset.h"
#include "scene/SceneDocument.h"
#include "scene/SceneComponents.h"
#include "scene/SceneExtraction.h"
#include "scene/BuiltinModels.h"
#include "assets/MaterialAsset.h"
#include "assets/GameplayEffectAsset.h"
#include "assets/AtomicFile.h"
#include "math/Json.h"
#include "point_light.sh"
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
#include <map>
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
void writeSceneFixture(const std::filesystem::path& root,const std::string& id,const Json& data) {
    auto file=root/id;bool registerFile=false;
    if(contentStorageReady(root)) {
        const auto entries=storedAssetPaths(root);const auto canonical=canonicalAssetId(id);
        const auto found=std::find_if(entries.begin(),entries.end(),[&](const auto& entry){return entry.id==canonical;});
        if(found!=entries.end())file=found->file;
        else {file=root/"Content"/(canonical+".crimson-scene");registerFile=true;}
    }
    writeFileAtomically(file,data.dump());if(registerFile)registerAssetPath(root,canonicalAssetId(id),file);
}
void sameAfterLegacyInitialization(const std::vector<EntityRecord>& expected,const SceneDocument& converted) {
    auto actual=converted.scene().records();require(expected.size()==actual.size(),"Legacy conversion changed record count.");
    const auto initialValues=[&](const EntityRecord& record) {
        std::map<GameplayTag,double> result;
        if(record.effects)for(const auto& reference:record.effects->starting) {
            auto effect=engineGameplayEffect(reference.id);if(!effect)effect=converted.gameplayEffect(reference.id);
            if(!effect)effect=std::make_shared<const GameplayEffectAsset>(loadGameplayEffectAsset(converted.projectRoot(),reference.id));
            require(effect->durationPolicy==EffectDurationPolicy::Instant&&effect->period==0,"Legacy initialization changed effect lifetime.");
            for(const auto& modifier:effect->modifiers) {
                require(modifier.operation==EffectModifierOperation::Override&&modifier.magnitude.calculation.empty(),"Legacy initialization changed literal Override semantics.");
                result[modifier.attribute]=modifier.magnitude.literal;
            }
        }
        return result;
    };
    for(std::size_t index=0;index<actual.size();++index) {
        require(initialValues(expected[index])==initialValues(actual[index]),"Legacy initialization lost an authored starting value.");
        actual[index].effects=expected[index].effects;
    }
    same(expected,actual);
}

EntityId addControlledActor(SceneDocument& document,const std::string& name="Actor") {
    if(!document.begin("Compose controlled actor"))return NoEntity;
    EntityRecord record;record.name=name;record.actor.emplace();record.controller.emplace();record.movement.emplace();
    initializePlayerEffects(record);
    record.controller->collision.layers=PlayerCollisionLayer;
    record.navigation.emplace();record.playerInput.emplace();
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
            require(profile!=nullptr,"Legacy fixture profile is unavailable.");character["height"]=profile->traversal.height;character["radius"]=profile->traversal.radius;character.erase("profile");character["moveSpeed"]=5.0;
            for(const auto& seed:entity.value("attributes",Json::array()))if(seed.is_object()&&GameplayTag{seed.at("tag").get<std::string>()}==stats::MovementSpeed)character["moveSpeed"]=seed.at("startingBase");
            character["showRoute"]=entity.value("navigation",Json::object()).value("showRoute",true);
            if(entity.contains("lightRig"))character["lightRig"]=entity.at("lightRig");
            if(entity.contains("darknessReveal"))character["darknessReveal"]=entity.at("darknessReveal");
            entity["character"]=std::move(character);
        }
        for(const auto* field:{"actor","movement","controller","navigation","playerInput","lightRig","darknessReveal","attributes","tags","effects"})entity.erase(field);
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
    require(!root.parent&&!root.model&&root.actor&&root.movement&&root.controller&&!root.navigation&&root.playerInput,
        "Create Player did not compose independent capabilities on a generic root.");
    require(children.size()==1&&document.selection().ids==std::vector<EntityId>{player}&&document.selection().primary==player,
        "Create Player did not create one real child camera and select its root.");
    const auto camera=children.front();const auto child=*document.scene().find(camera);
    require(child.camera==std::optional<SceneCamera>{SceneCamera{75,.05f,2048}}&&child.cameraFollow&&child.cameraFollow->target==player&&
        child.cameraFollow->offset==Vec3{0,1.65f,0}&&document.scene().properties().activeCamera==camera&&document.scene().properties().playerCharacter==player,
        "Create Player camera lens, follow settings or explicit roles are incorrect.");
    require(child.cameraFollow->worldBasis==Mat4{},"Create Player camera does not face forward.");
    const auto created=document.scene().records();
    require(document.undo()&&document.scene().records().empty()&&!document.scene().properties().activeCamera&&!document.scene().properties().playerCharacter,
        "Create Player did not undo its entire recipe in one command.");
    require(document.redo(),document.status());same(created,document.scene().records());
    const auto second=document.createPlayer("Second");require(second&&document.scene().properties().playerCharacter==player&&document.scene().properties().activeCamera==camera,
        "A second Player stole an existing controlled entity or active camera.");
    const auto thirdCamera=document.createCamera("Chosen camera");require(document.setActiveCamera(thirdCamera),document.status());
    const auto third=document.createPlayer("Third");require(third&&document.scene().properties().activeCamera==thirdCamera,
        "Create Player replaced an explicitly active camera.");
    Scene runtime;runtime.apply(document.scene().records(),document.scene().properties());
    const auto moved=math::translation({5,2,-3})*math::eulerRotationXYZ({0,1.2f,0})*math::scaling({2,1.5f,.7f});
    const std::array poses{ScenePoseUpdate{player,moved}};runtime.applyRuntimePoses(poses);
    near(position(runtime,camera),Vec3{5,2,-3}+child.cameraFollow->offset);
    const auto runtimeWorld=*runtime.worldTransform(camera);
    const auto rootOrientation=math::orthonormalFrame(moved);
    for(unsigned axis=0;axis<3;++axis)near(math::transformDirection(runtimeWorld,axis==0?Vec3{1,0,0}:axis==1?Vec3{0,1,0}:Vec3{0,0,1}),
        math::transformDirection(rootOrientation,axis==0?Vec3{1,0,0}:axis==1?Vec3{0,1,0}:Vec3{0,0,1}));
    const auto json=Json::parse(serializeSceneSnapshot({{},"Composed",document.scene().records(),document.scene().properties()}));
    require(json.at("version")==24,"Component scene writer did not use schema24.");
    for(const auto& entity:json.at("entities")) {
        require(!entity.contains("character"),"Current scene writer retained the removed Character representation.");
        if(entity.contains("camera"))require(!entity.at("camera").contains("followTarget")&&!entity.at("camera").contains("followOffset"),
            "Camera lens retained the removed embedded follow fields.");
    }
    SceneDocument legacySource(project.root);const auto legacyActor=addControlledActor(legacySource,"Old actor"),legacyCamera=legacySource.createCamera("Old camera");
    auto old=*legacySource.scene().find(legacyActor);const auto oldProfile=std::make_shared<const NavigationAgentProfile>(NavigationAgentProfile{newAssetId(),"Old custom agent",{2.4f,.4f,45,.3f}});
    old.controller->profile=AssetRef<NavigationAgentProfile>{oldProfile->id};
    const NavigationAgentProfileQuery oldProfiles=[&](const auto& id){return id==oldProfile->id?oldProfile:nullptr;};
    old.navigation->showRoute=false;old.controller->showCollisionInGame=true;old.controller->collisionColor={.2f,.6f,.8f,.7f};
    require(legacySource.updateEntity(legacyActor,old)&&legacySource.setCameraFollow(legacyCamera,legacyActor),legacySource.status());
    const auto legacyWorld=*legacySource.scene().worldTransform(legacyCamera);
    auto legacy=legacyComposition(Json::parse(serializeSceneSnapshot({{},"Legacy composition",legacySource.scene().records(),legacySource.scene().properties()})),oldProfiles);
    for(auto& entity:legacy["entities"])if(entity.contains("character"))entity["character"]["moveSpeed"]=8.25;
    const std::string legacyId="assets/scenes/legacy-composition.crimson-scene";const auto bytes=legacy.dump();
    writeSceneFixture(project.root,legacyId,legacy);
    const auto loaded=readSceneSnapshot(project.root,legacyId);Scene migrated;migrated.apply(loaded.records,loaded.properties);
    require(loaded.records.size()==2&&migrated.properties().activeCamera==legacyCamera&&migrated.properties().playerCharacter==legacyActor,
        "Legacy component migration invented entities/cameras or changed explicit roles.");
    const auto converted=*migrated.find(legacyActor);
    const auto convertedProfile=std::find_if(loaded.stagedNavigationProfiles.begin(),loaded.stagedNavigationProfiles.end(),[&](const auto& profile){return profile->id==converted.controller->profile.id;});
    require(convertedProfile!=loaded.stagedNavigationProfiles.end()&&(*convertedProfile)->traversal==oldProfile->traversal,"Legacy Character migration lost custom capsule dimensions.");
    require(converted.actor&&converted.movement==old.movement&&converted.controller->showCollisionInGame==old.controller->showCollisionInGame&&
        converted.controller->collisionColor==old.controller->collisionColor&&converted.navigation==old.navigation&&converted.playerInput.has_value(),"Legacy Character migration lost independently authored values.");
    require(loaded.stagedGameplayEffects.size()==1&&converted.effects&&converted.effects->starting.front().id==loaded.stagedGameplayEffects.front()->id,
        "Legacy Character migration omitted its independent initialization effect.");
    const auto& initial=*loaded.stagedGameplayEffects.front();
    for(const auto& [tag,value]:std::array{std::pair{stats::Vitality,50.0},std::pair{stats::Spirit,50.0},std::pair{stats::MovementSpeed,8.25}}) {
        const auto modifier=std::find_if(initial.modifiers.begin(),initial.modifiers.end(),[&](const auto& mod){return mod.attribute==tag;});
        require(modifier!=initial.modifiers.end()&&modifier->operation==EffectModifierOperation::Override&&modifier->magnitude.literal==value,
            "Legacy Character migration lost primary stats or movement speed from its initialization effect.");
    }
    const auto convertedWorld=*migrated.worldTransform(legacyCamera);
    for(unsigned index=0;index<16;++index)require(std::abs(convertedWorld[index]-legacyWorld[index])<.0002f,"Legacy follow migration changed the visible camera pose.");
    {std::ifstream file(sceneAssetPath(project.root,legacyId),std::ios::binary);const std::string unchanged{std::istreambuf_iterator<char>(file),{}};require(unchanged==bytes,"Read migration rewrote the saved legacy scene.");}
    const auto current=Json::parse(serializeSceneSnapshot(loaded));require(current.at("version")==24,"Migrated scene did not write only the current representation.");
    for(const auto& entity:current.at("entities"))require(!entity.contains("character"),"Saving migrated components retained Character data.");
}

void firstPersonInputStorageAndCameraConversion() {
    TempProject project;SceneDocument document(project.root);
    const auto player=document.createPlayer("First-person player"),camera=document.scene().properties().activeCamera;
    const ScenePlayerInput defaults;const MovementComponent movementDefaults;
    require(defaults.enabled&&!defaults.invertY&&defaults.mouseSensitivity==.10f&&movementDefaults.jumpHeight==1.1f&&
        movementDefaults.groundAcceleration==45&&movementDefaults.groundFriction==8&&movementDefaults.airAcceleration==12&&movementDefaults.airControl==.35f&&
        movementDefaults.strafeTilt==3&&movementDefaults.tiltResponse==12&&movementDefaults.stairSmoothingTime==.10f,"First-person controller defaults changed.");
    auto tuned=*document.scene().find(player);
    tuned.playerInput=ScenePlayerInput{false,.23f,true};
    tuned.movement=MovementComponent{true,1.7f,62.f,9.5f,15.f,.6f,5.f,17.f,.24f};
    const auto history=document.history().size();
    require(document.updateEntity(player,tuned,"Tune first-person controls")&&document.history().size()==history+1,document.status());
    require(document.undo()&&document.scene().find(player)->playerInput==std::optional<ScenePlayerInput>{defaults},"First-person controls did not undo together.");
    require(document.redo()&&document.scene().find(player)->playerInput==tuned.playerInput,"First-person controls did not redo exactly.");
    require(document.save("First-person controls"),document.status());
    const auto snapshot=readSceneSnapshot(project.root,document.assetId());
    same(document.scene().records(),snapshot.records);
    SceneDocument reopened(project.root);require(reopened.load(document.assetId())&&!reopened.dirty(),reopened.status());
    same(document.scene().records(),reopened.scene().records());
    const auto saved=Json::parse(serializeSceneSnapshot(snapshot));
    for(const auto& entity:saved.at("entities"))if(entity.contains("playerInput")) {
        require(entity.at("playerInput").size()==4&&entity.at("playerInput").at("mouseSensitivity")==.23f&&
            entity.at("playerInput").at("invertY")==true&&entity.at("movement").at("jumpHeight")==1.7f&&
            entity.at("movement").at("stairSmoothingTime")==.24f&&!entity.contains("motor"),"Movement ownership was not persisted.");
    }
    constexpr std::array<float MovementComponent::*,8> fields{&MovementComponent::jumpHeight,&MovementComponent::groundAcceleration,
        &MovementComponent::groundFriction,&MovementComponent::airAcceleration,&MovementComponent::airControl,
        &MovementComponent::strafeTilt,&MovementComponent::tiltResponse,&MovementComponent::stairSmoothingTime};
    const auto records=document.scene().records();const auto beforeHistory=document.history().size();
    for(const auto field:fields)for(const auto bad:{-1.f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
        auto invalid=tuned;(*invalid.movement).*field=bad;
        require(!document.updateEntity(player,invalid),"Invalid movement control was published.");
        same(records,document.scene().records());require(document.history().size()==beforeHistory,"Rejected movement property added history.");
    }
    for(const float bad:{0.f,11.f,std::numeric_limits<float>::quiet_NaN()}) {
        auto invalid=tuned;invalid.playerInput->mouseSensitivity=bad;
        require(!document.updateEntity(player,invalid),"Invalid mouse sensitivity was accepted.");
    }
    for(const float endpoint:{0.f,1.f}) {
        auto valid=tuned;valid.movement->stairSmoothingTime=endpoint;
        require(document.updateEntity(player,valid),document.status());
        require(document.undo()&&document.scene().find(player)->movement==tuned.movement,"Stair smoothing did not undo.");
    }
    // Existing v23 scenes migrate old motor/input values without rewriting their file.
    auto previous=saved;
    for(auto& entity:previous["entities"]) {
        if(entity.contains("movement")) {
            entity["motor"]={{"enabled",entity["movement"]["enabled"]}};
            for(auto it=entity["movement"].begin();it!=entity["movement"].end();++it)
                if(it.key()!="enabled")entity["playerInput"][it.key()]=it.value();
            entity.erase("movement");entity["lightRig"]={{"enabled",true}};entity["darknessReveal"]={{"enabled",true},{"spread",8}};
        }
    }
    const auto environment=document.createEnvironment();
    previous["entities"].push_back(Json::parse(serializeSceneSnapshot({{},"Environment",{*document.scene().find(environment)}, {}}))["entities"][0]);
    previous["entities"].back()["environment"]["environment"]["ambientVisibility"]={{"enabled",true}};
    const std::string previousId="assets/scenes/previous-movement.crimson-scene";writeSceneFixture(project.root,previousId,previous);
    require(reopened.load(previousId)&&!reopened.dirty(),reopened.status());
    require(reopened.scene().find(player)->movement==tuned.movement&&reopened.scene().find(player)->playerInput==tuned.playerInput,"Legacy movement tuning was lost.");
    {std::ifstream input(sceneAssetPath(project.root,previousId),std::ios::binary);require(std::string{std::istreambuf_iterator<char>(input),{}}==previous.dump(),"Conversion rewrote saved data.");}
    const auto current=Json::parse(serializeSceneSnapshot({{},"Converted",reopened.scene().records(),reopened.scene().properties()}));
    for(const auto& entity:current["entities"]) {
        require(!entity.contains("lightRig")&&!entity.contains("darknessReveal")&&!entity.contains("motor"),"Writer retained removed capabilities.");
        if(entity.contains("environment"))require(!entity["environment"]["environment"].contains("ambientVisibility"),"Writer retained Ambient Darkness.");
    }

    constexpr float radians=3.14159265358979323846f/180.f;
    const auto oldBasis=math::eulerRotationXYZ(Vec3{-55,45,0}*radians);
    auto legacy=saved;
    for(auto& entity:legacy["entities"]) {
        if(entity.contains("playerInput")) {
            entity["motor"]={{"enabled",true}};entity.erase("movement");entity["playerInput"]={{"enabled",true}};entity["navigation"]={{"enabled",false},{"showRoute",false}};
        }
        if(entity.contains("camera")) {
            entity["camera"]={{"verticalFov",35.f},{"near",.1f},{"far",2048.f}};
            entity["cameraFollow"]["offset"]=Vec3{6.5f,13.1f,6.5f};entity["cameraFollow"]["worldBasis"]=oldBasis;
            entity["local"]=*math::withAffinePosition(oldBasis,{6.5f,13.1f,6.5f});
        }
    }
    const std::string legacyId="assets/scenes/legacy-first-person.crimson-scene";
    writeSceneFixture(project.root,legacyId,legacy);const auto legacyPath=sceneAssetPath(project.root,legacyId);
    const auto bytes=legacy.dump();const auto converted=readSceneSnapshot(project.root,legacyId);Scene scene;scene.apply(converted.records,converted.properties);
    const auto convertedCamera=*scene.find(camera);
    require(scene.find(player)->playerInput==std::optional<ScenePlayerInput>{defaults}&&scene.find(player)->navigation==std::optional<SceneNavigationAgent>{SceneNavigationAgent{false,false}},
        "Old player conversion lost defaults or the independent Navigation capability.");
    require(convertedCamera.camera==std::optional<SceneCamera>{SceneCamera{75,.05f,2048}}&&
        convertedCamera.cameraFollow->offset==Vec3{0,1.65f,0}&&convertedCamera.cameraFollow->worldBasis==Mat4{},
        "Exact old built-in camera was not converted to first person.");
    near(position(scene,camera),{0,1.65f,0});
    {std::ifstream input(legacyPath,std::ios::binary);require(std::string{std::istreambuf_iterator<char>(input),{}}==bytes,"FPS conversion rewrote the saved scene.");}
    require(reopened.load(legacyId)&&!reopened.dirty(),"In-memory FPS conversion dirtied the scene.");
    const auto convertedCurrent=Json::parse(serializeSceneSnapshot(converted));
    for(const auto& entity:convertedCurrent.at("entities"))if(entity.contains("playerInput"))
        require(entity.at("playerInput").contains("mouseSensitivity"),"Converted player did not write the current input representation.");

    // Any authored lens, offset, basis or current control field proves the
    // camera belongs to the user and must retain its exact saved values.
    for(unsigned variant=0;variant<6;++variant) {
        auto custom=legacy;
        for(auto& entity:custom["entities"]) {
            if(variant==3&&entity.contains("playerInput"))entity["playerInput"]["mouseSensitivity"]=.4f;
            if(variant==5&&entity.contains("playerInput"))entity["playerInput"]["stairSmoothingTime"]=.2f;
            if(!entity.contains("camera"))continue;
            if(variant==0)entity["camera"]["verticalFov"]=42.f;
            if(variant==1)entity["cameraFollow"]["offset"]=Vec3{6.5f,13.2f,6.5f};
            if(variant==2)entity["cameraFollow"]["worldBasis"]=math::eulerRotationXYZ(Vec3{-55,45,1}*radians);
            if(variant==4)entity["parent"]="0000000000000000";
        }
        writeSceneFixture(project.root,legacyId,custom);const auto retained=readSceneSnapshot(project.root,legacyId);
        const auto found=std::find_if(retained.records.begin(),retained.records.end(),[&](const auto& record){return record.id==camera;});
        for(const auto& entity:custom.at("entities"))if(entity.contains("camera"))
            require(found!=retained.records.end()&&found->camera->verticalFov==entity.at("camera").at("verticalFov").get<float>()&&
                found->cameraFollow->offset==entity.at("cameraFollow").at("offset").get<Vec3>()&&
                found->cameraFollow->worldBasis==entity.at("cameraFollow").at("worldBasis").get<Mat4>(),
                "Legacy FPS conversion changed an authored camera or a standalone override.");
    }
}

void playerTraceSettingsStorageHistoryAndRuntime() {
    TempProject project;SceneDocument document(project.root);const auto player=document.createPlayer("Trace player");
    const SceneTraceTestSettings defaults;
    require(defaults.duration==.5f&&defaults.length==10.f&&defaults.collisionQuadSize==.12f&&defaults.collisionMask==AllCollisionLayers&&defaults.hitImpulse==50.f&&
        document.scene().find(player)->playerInput->traceTest==defaults,"Player trace defaults changed.");
    auto tuned=*document.scene().find(player);tuned.playerInput->traceTest={1.25f,23.f,.3f,WorldCollisionLayer|EnemyCollisionLayer|(1u<<31),250.f};
    const auto history=document.history().size();require(document.updateEntity(player,tuned,"Tune trace test")&&
        document.history().size()==history+1,document.status());
    require(document.undo()&&document.scene().find(player)->playerInput->traceTest==defaults,"Trace properties did not undo together.");
    require(document.redo()&&document.scene().find(player)->playerInput==tuned.playerInput,"Trace properties did not redo exactly.");
    require(document.save("Trace settings"),document.status());
    SceneDocument reopened(project.root);require(reopened.load(document.assetId())&&!reopened.dirty(),reopened.status());
    same(document.scene().records(),reopened.scene().records());
    const auto saved=Json::parse(serializeSceneSnapshot({{},"Trace",document.scene().records(),document.scene().properties()}));
    require(saved.at("version")==24,"Optional trace settings unnecessarily changed the scene schema.");
    for(const auto& entity:saved.at("entities"))if(entity.contains("playerInput"))
        require(entity.at("playerInput").at("traceTest").at("hitImpulse")==250.f,"Trace hit impulse was omitted from the current writer.");
    auto legacy=saved;for(auto& entity:legacy["entities"])if(entity.contains("playerInput"))entity["playerInput"].erase("traceTest");
    const std::string legacyId="assets/scenes/no-trace-settings.crimson-scene";writeSceneFixture(project.root,legacyId,legacy);
    require(reopened.load(legacyId)&&!reopened.dirty()&&reopened.scene().find(player)->playerInput->traceTest==defaults,
        "An existing Player did not receive trace defaults without dirtying the scene.");
    {std::ifstream input(sceneAssetPath(project.root,legacyId),std::ios::binary);
        require(std::string{std::istreambuf_iterator<char>(input),{}}==legacy.dump(),"Loading trace defaults rewrote saved scene data.");}
    auto previousTrace=saved;for(auto& entity:previousTrace["entities"])if(entity.contains("playerInput"))entity["playerInput"]["traceTest"].erase("hitImpulse");
    writeSceneFixture(project.root,legacyId,previousTrace);
    auto previousDefaults=tuned.playerInput->traceTest;previousDefaults.hitImpulse=defaults.hitImpulse;
    require(reopened.load(legacyId)&&!reopened.dirty()&&reopened.scene().find(player)->playerInput->traceTest==previousDefaults,
        "Missing hit impulse changed existing trace settings or dirtied the scene.");
    {std::ifstream input(sceneAssetPath(project.root,legacyId),std::ios::binary);
        require(std::string{std::istreambuf_iterator<char>(input),{}}==previousTrace.dump(),"Loading the optional hit impulse rewrote saved scene data.");}
    const auto records=document.scene().records();const auto beforeHistory=document.history().size();
    constexpr std::array<float SceneTraceTestSettings::*,4> fields{&SceneTraceTestSettings::duration,&SceneTraceTestSettings::length,&SceneTraceTestSettings::collisionQuadSize,&SceneTraceTestSettings::hitImpulse};
    for(const auto field:fields)for(const float bad:{-1.f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
        auto invalid=tuned;invalid.playerInput->traceTest.*field=bad;
        require(!document.updateEntity(player,invalid),"Invalid trace dimensions were accepted.");
        same(records,document.scene().records());require(document.history().size()==beforeHistory,"Rejected trace settings added history.");
    }
    for(const auto field:{&SceneTraceTestSettings::length,&SceneTraceTestSettings::collisionQuadSize}) {
        auto invalid=tuned;invalid.playerInput->traceTest.*field=0;
        require(!document.updateEntity(player,invalid),"Zero trace length or marker size was accepted.");
    }
    for(const auto mask:{0u,AllCollisionLayers}) {
        auto valid=tuned;valid.playerInput->traceTest.collisionMask=mask;valid.playerInput->traceTest.duration=0;
        require(document.updateEntity(player,valid)&&document.undo(),"Empty/full trace mask or zero duration was rejected.");
    }
    for(const float impulse:{0.f,100000.f}) {
        auto valid=tuned;valid.playerInput->traceTest.hitImpulse=impulse;
        require(document.updateEntity(player,valid)&&document.undo(),"A bounded trace impulse endpoint was rejected.");
    }
    {auto invalid=tuned;invalid.playerInput->traceTest.hitImpulse=100001.f;
        require(!document.updateEntity(player,invalid),"A trace impulse above the maximum was accepted.");
        same(records,document.scene().records());}
    for(const auto mask:{Json(-1),Json(4294967296ull),Json(1.5),Json("World")}) {
        auto invalid=saved;for(auto& entity:invalid["entities"])if(entity.contains("playerInput"))entity["playerInput"]["traceTest"]["collisionMask"]=mask;
        writeSceneFixture(project.root,legacyId,invalid);require(!reopened.load(legacyId),"Invalid persisted trace mask was accepted.");
    }
    for(const auto impulse:{Json(-1),Json(100001),Json(nullptr),Json("50"),Json(true)}) {
        auto invalid=saved;for(auto& entity:invalid["entities"])if(entity.contains("playerInput"))entity["playerInput"]["traceTest"]["hitImpulse"]=impulse;
        writeSceneFixture(project.root,legacyId,invalid);require(!reopened.load(legacyId),"Invalid persisted hit impulse was accepted.");
    }
    SceneDocument runtime(project.root);require(runtime.resetTransientFrom(document),runtime.status());
    auto live=*runtime.scene().find(player);live.playerInput->traceTest.length=42;live.playerInput->traceTest.hitImpulse=0;
    require(runtime.updateEntity(player,live,"Live trace length"),runtime.status());pose(runtime,player,{3,0,4});
    require(runtime.undo()&&runtime.scene().find(player)->playerInput->traceTest==tuned.playerInput->traceTest,
        "Runtime trace tuning did not undo.");near(position(runtime.scene(),player),{3,0,4});
    require(runtime.redo()&&runtime.scene().find(player)->playerInput->traceTest.length==42&&runtime.scene().find(player)->playerInput->traceTest.hitImpulse==0,"Runtime trace tuning did not redo.");
    near(position(runtime.scene(),player),{3,0,4});same(records,document.scene().records());
}

void presentationCameraPoseOverrides() {
    TempProject project;SceneDocument document(project.root);const auto player=document.createPlayer();
    const auto camera=document.scene().properties().activeCamera;
    auto actor=*document.scene().find(player);actor.local=math::translation({3,1,-2})*math::eulerRotationXYZ({0,.4f,0})*math::scaling({2,1.5f,.7f});
    require(document.updateEntity(player,actor),document.status());
    Scene presentation;presentation.apply(document.scene().records(),document.scene().properties());
    const auto authored=*presentation.find(camera),actorBefore=*presentation.find(player);
    const auto revision=presentation.revision(),generation=presentation.generation(),poseRevision=presentation.poseRevision();
    const auto world=math::translation({3,2.65f,-2})*math::eulerRotationXYZ({.3f,.8f,-.04f});
    presentation.applyRuntimeCameraPose(camera,world);
    const auto resolved=*presentation.worldTransform(camera);
    for(unsigned index=0;index<16;++index)require(std::abs(resolved[index]-world[index])<.0001f,"Presentation camera override lost the world affine pose.");
    require(presentation.find(camera)->cameraFollow==authored.cameraFollow&&presentation.find(camera)->camera==authored.camera&&
        sameEntity(*presentation.find(player),actorBefore)&&presentation.revision()==revision&&presentation.generation()==generation&&
        presentation.poseRevision()==poseRevision+1,"Presentation camera override changed authored data, controller pose or edit identity.");
    presentation.applyRuntimeCameraPose(camera,world);require(presentation.poseRevision()==poseRevision+1,"Equal runtime camera pose caused an invalidation.");
    for(const auto id:{NoEntity,player,EntityId{999}}) {
        bool rejected=false;try{presentation.applyRuntimeCameraPose(id,world);}catch(const std::exception&){rejected=true;}
        require(rejected,"Runtime camera pose accepted a missing entity or a non-camera.");
    }
    auto invalid=world;invalid[0]=std::numeric_limits<float>::quiet_NaN();
    bool rejected=false;try{presentation.applyRuntimeCameraPose(camera,invalid);}catch(const std::exception&){rejected=true;}
    require(rejected&&presentation.poseRevision()==poseRevision+1,"Invalid camera pose mutated presentation.");
    presentation.applyRuntimePoses({});
    require(presentation.find(camera)->cameraFollow==authored.cameraFollow,"Follow reset lost authored camera settings.");
    const auto restored=*presentation.worldTransform(camera),expected=*document.scene().worldTransform(camera);
    for(unsigned index=0;index<16;++index)require(std::abs(restored[index]-expected[index])<.0001f,"Follow did not restore authored camera placement after a transient view override.");
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
    require(json.at("version")==24&&json["entities"][0]["collider"].contains("mesh"),"Native collision did not write schema21 triangle data.");
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
    auto characterRecord=*document.scene().find(character);
    characterRecord.effects->starting.emplace_back(canonicalAssetId("tests/gameplay/custom-initialization"));
    require(document.updateEntity(character,characterRecord),document.status());
    require(document.save("Gameplay"),document.status());
    const auto snapshot=readSceneSnapshot(project.root,document.assetId());
    require(Json::parse(serializeSceneSnapshot(snapshot)).at("version")==24,"Scene writer did not use version 24.");
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
        require(json.at("version")==24&&json.at("entities").at(0).contains("collider")&&!json.at("entities").at(0).contains("meshCollider"),"Unified collider writer retained the removed meshCollider representation.");
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
    const auto current=Json::parse(serializeSceneSnapshot(loaded));require(current.at("version")==24&&current["entities"][0]["collider"].at("shape")=="trimesh"&&!current["entities"][0].contains("meshCollider"),"Legacy collider conversion was not one-way on write.");
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
void sphereCreationMaterialsAndStorage(){
    TempProject project;SceneDocument document(project.root);
    const auto marker=document.createEmpty("Marker");const auto initialHistory=document.history().size();
    const auto sphere=document.createSphere();
    require(sphere && document.scene().find(sphere)->name=="Sphere" && document.scene().find(sphere)->model->id==BuiltinSphereModelId,
        "Sphere creation did not use the named built-in geometry.");
    require(document.selection().ids==std::vector<EntityId>{sphere} && document.selection().primary==sphere &&
        document.history().size()==initialHistory+1,"Sphere creation did not select the new mesh as one history command.");
    require(document.scene().find(sphere)->local==Mat4{} && !document.scene().find(sphere)->collider && !document.scene().find(sphere)->actor,
        "Sphere creation did not follow ordinary mesh defaults.");
    require(document.undo() && !document.scene().contains(sphere) && document.selection().primary==marker,
        "Undo did not remove the Sphere and restore the prior selection.");
    require(document.redo() && document.scene().find(sphere)->model->id==BuiltinSphereModelId && document.selection().primary==sphere,
        "Redo did not restore the Sphere with its persistent identity.");
    const auto second=document.createSphere("Other sphere");
    const std::string material=canonicalAssetId("tests/sphere/surface.crimson-material");
    auto edited=*document.scene().find(sphere);edited.modelMaterials={AssetRef<MaterialAsset>{material}};
    edited.primitive.emplace();edited.primitive->radius=1.25f;edited.primitive->sides=12;edited.primitive->rings=6;
    edited.local=math::translation({2,3,4})*math::scaling({2,1,3});
    require(document.updateEntity(sphere,edited),document.status());
    require(document.scene().find(second)->name=="Other sphere" && document.scene().find(second)->modelMaterials.empty(),
        "A Sphere material assignment changed another instance.");
    require(document.undo() && document.scene().find(sphere)->modelMaterials.empty() && document.redo() &&
        document.scene().find(sphere)->modelMaterials==edited.modelMaterials,"Sphere material/transform edits did not round-trip through history.");
    auto invalid=edited;setModelMaterialOverride(invalid.modelMaterials,1,AssetRef<MaterialAsset>{material});
    require(!document.updateEntity(sphere,invalid),"Sphere accepted a nonexistent second material slot.");
    document.select({sphere});require(document.duplicateSelection(),document.status());const auto duplicate=document.selection().primary;
    require(document.scene().find(duplicate)->model->id==BuiltinSphereModelId &&
        document.scene().find(duplicate)->modelMaterials==edited.modelMaterials && document.scene().find(duplicate)->local==edited.local &&
        document.scene().find(duplicate)->primitive==edited.primitive,
        "Duplicating a Sphere lost its model, material assignment or transform.");
    const auto extracted=extractScene(document.scene());
    require(extracted.models.size()==3,"Sphere instances did not use ordinary model extraction.");
    for(const auto& model:extracted.models) {
        require(model.assetId==BuiltinSphereModelId,"Sphere extraction lost the built-in identity.");
        require(model.materialOverrides==(model.id==second?ModelMaterialOverrides{}:edited.modelMaterials),
            "Sphere extraction lost a per-instance material assignment.");
        require(model.primitive==(model.id==second?document.scene().find(second)->primitive:edited.primitive),"Sphere extraction lost authored primitive parameters.");
    }
    require(document.save("Sphere primitives"),document.status());
    const auto snapshot=readSceneSnapshot(project.root,document.assetId());
    require(Json::parse(serializeSceneSnapshot(snapshot)).at("version")==24,"Sphere primitive unnecessarily changed the scene schema.");
    SceneDocument reopened(project.root);require(reopened.load(document.assetId()),reopened.status());
    same(document.scene().records(),reopened.scene().records());require(!reopened.dirty(),"Loading a Sphere scene dirtied its authored document.");
    SceneDocument runtime(project.root);require(runtime.resetTransientFrom(document),runtime.status());
    auto live=*runtime.scene().find(sphere);live.modelMaterials.clear();
    require(runtime.updateEntity(sphere,live) && runtime.scene().find(sphere)->model->id==BuiltinSphereModelId,
        "Sphere was lost from the transient Play document.");
    require(document.scene().find(sphere)->modelMaterials==edited.modelMaterials && !document.dirty(),
        "Runtime Sphere edits changed the authored scene.");
}
void primitiveParametersStorageHistoryAndRuntime(){
    TempProject project;SceneDocument document(project.root);
    const auto box=document.createBox(),sphere=document.createSphere(),cylinder=document.createCylinder(),empty=document.createEmpty("Marker");
    require(cylinder && document.scene().find(cylinder)->name=="Cylinder" && document.scene().find(cylinder)->model->id==BuiltinCylinderModelId,
        "Cylinder creation did not use its ordinary named mesh identity.");
    for(const auto id:{box,sphere,cylinder})require(document.scene().find(id)->primitive.value_or(PrimitiveMeshSettings{})==PrimitiveMeshSettings{},
        "New primitives did not retain shared default parameters.");
    auto boxRecord=*document.scene().find(box);boxRecord.primitive.emplace();boxRecord.primitive->size={2,3,4};
    auto sphereRecord=*document.scene().find(sphere);sphereRecord.primitive.emplace();sphereRecord.primitive->radius=2;sphereRecord.primitive->sides=9;sphereRecord.primitive->rings=5;
    auto cylinderRecord=*document.scene().find(cylinder);cylinderRecord.primitive.emplace();cylinderRecord.primitive->radius=1.5f;cylinderRecord.primitive->height=4;cylinderRecord.primitive->sides=7;
    for(const auto& record:{boxRecord,sphereRecord,cylinderRecord})require(document.updateEntity(record.id,record),document.status());
    require(document.undo() && document.scene().find(cylinder)->primitive.value_or(PrimitiveMeshSettings{})==PrimitiveMeshSettings{} &&
        document.redo() && document.scene().find(cylinder)->primitive==cylinderRecord.primitive,"Primitive parameters were omitted from history fields.");
    document.select({cylinder});require(document.duplicateSelection(),document.status());
    require(document.scene().find(document.selection().primary)->primitive==cylinderRecord.primitive,"Cylinder duplication lost its authored geometry parameters.");
    require(document.save("Authored primitive parameters"),document.status());
    const auto json=Json::parse(serializeSceneSnapshot(readSceneSnapshot(project.root,document.assetId())));
    require(json.at("version")==24,"Primitive parameters unnecessarily changed the scene schema.");
    for(const auto& entity:json.at("entities"))if(entity.contains("model")) {
        const auto& value=entity.at("primitive");
        if(entity.at("name")=="Box")require(value.at("size")==Json::array({2,3,4}) && !value.contains("radius") && !value.contains("sides"),
            "Box writer retained inactive primitive fields.");
        else if(entity.at("name")=="Sphere")require(value.at("radius")==2 && value.at("sides")==9 && value.at("rings")==5 && !value.contains("height") && !value.contains("size"),
            "Sphere writer lost active parameters or retained inactive dimensions.");
        else require(value.at("radius")==1.5f && value.at("height")==4 && value.at("sides")==7 && !value.contains("rings") && !value.contains("size"),
            "Cylinder writer lost active parameters or retained Sphere/Box fields.");
    }
    SceneDocument reopened(project.root);require(reopened.load(document.assetId()),reopened.status());same(document.scene().records(),reopened.scene().records());
    const auto records=document.scene().records();const auto history=document.history().size();
    auto invalid=boxRecord;invalid.primitive->size[1]=0;require(!document.updateEntity(box,invalid),"Zero Box size was published.");
    invalid=sphereRecord;invalid.primitive->rings=1;require(!document.updateEntity(sphere,invalid),"Sphere accepted too few rings.");
    invalid=cylinderRecord;invalid.primitive->sides=257;require(!document.updateEntity(cylinder,invalid),"Cylinder accepted too many sides.");
    invalid=*document.scene().find(empty);invalid.primitive.emplace();require(!document.updateEntity(empty,invalid),"A non-mesh entity accepted primitive parameters.");
    same(records,document.scene().records());require(document.history().size()==history && !document.dirty(),"Rejected primitive edits changed scene state/history.");
    for(const auto& code:std::array<Json,8>{Json(-1),Json(2),Json(257),Json(1.5),Json(true),Json("32"),Json(nullptr),Json(std::numeric_limits<std::uint64_t>::max())}) {
        auto malformed=json;for(auto& entity:malformed["entities"])if(entity["name"]=="Sphere")entity["primitive"]["sides"]=code;
        const std::string id="assets/scenes/invalid-primitive-parameters.crimson-scene";writeSceneFixture(project.root,id,malformed);
        const auto before=reopened.scene().records();require(!reopened.load(id),"Malformed saved primitive side count was accepted.");same(before,reopened.scene().records());
    }
    auto legacy=json;for(auto& entity:legacy["entities"])entity.erase("primitive");
    const std::string legacyId="assets/scenes/default-primitive-parameters.crimson-scene";writeSceneFixture(project.root,legacyId,legacy);
    const auto legacyFile=sceneAssetPath(project.root,legacyId);std::ifstream beforeFile(legacyFile,std::ios::binary);
    const std::string beforeBytes((std::istreambuf_iterator<char>(beforeFile)),{});
    require(reopened.load(legacyId) && !reopened.dirty(),"Missing primitive parameters dirtied the loaded scene.");
    for(const auto& record:reopened.scene().records())require(record.primitive.value_or(PrimitiveMeshSettings{})==PrimitiveMeshSettings{},"Missing primitive parameters did not default to original geometry.");
    std::ifstream afterFile(legacyFile,std::ios::binary);const std::string afterBytes((std::istreambuf_iterator<char>(afterFile)),{});
    require(beforeBytes==afterBytes,"Defaulting primitive parameters rewrote authored scene bytes.");
    SceneDocument runtime(project.root);require(runtime.resetTransientFrom(document),runtime.status());
    auto live=*runtime.scene().find(cylinder);live.primitive->height=8;
    require(runtime.updateEntity(cylinder,live),runtime.status());pose(runtime,cylinder,{6,7,8});
    require(runtime.undo() && runtime.scene().find(cylinder)->primitive==cylinderRecord.primitive && runtime.redo() &&
        runtime.scene().find(cylinder)->primitive==live.primitive,"Live primitive parameter history lost its patch.");
    near(position(runtime.scene(),cylinder),{6,7,8});same(records,document.scene().records());
    require(!document.dirty() && document.history().size()==history,"Live primitive parameters leaked into the authored scene.");
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
void obsoleteModelFadeSettingsAreIgnored() {
    TempProject project;SceneDocument document(project.root);
    require(document.createBox("Wall")&&document.createEmpty("Marker"),document.status());
    require(document.save("Obsolete fading"),document.status());
    const auto authored=document.scene().records();
    const auto serialized=Json::parse(serializeSceneSnapshot(readSceneSnapshot(project.root,document.assetId())));
    for(const auto version:{12,13,23}) {
        auto legacy=legacyComposition(serialized);legacy["version"]=version;
        for(auto& entity:legacy["entities"])entity["fadeWhenOccluding"]=entity.contains("model")?Json(true):Json::object();
        const auto id="assets/scenes/obsolete-fade-"+std::to_string(version)+".crimson-scene";
        std::filesystem::create_directories((project.root/id).parent_path());const auto bytes=legacy.dump();
        {std::ofstream file(project.root/id);file<<bytes;}
        SceneDocument reopened(project.root);require(reopened.load(id),reopened.status());same(authored,reopened.scene().records());
        require(!reopened.dirty(),"Ignoring removed model fading marked a scene dirty.");
        std::ifstream file(project.root/id);const std::string retained{std::istreambuf_iterator<char>(file),{}};
        require(retained==bytes,"Loading obsolete fading rewrote authored scene bytes.");
        const auto current=Json::parse(serializeSceneSnapshot(readSceneSnapshot(project.root,id)));
        require(current.at("version")==24,"Removed fading changed the current scene schema.");
        for(const auto& entity:current.at("entities"))require(!entity.contains("fadeWhenOccluding"),"Scene writer retained removed model fading.");
    }
}
void directionalAngularDiameterStorageHistoryAndRuntime() {
    TempProject project;SceneDocument document(project.root);const auto sun=document.createDirectionalLight();
    require(sun&&document.scene().find(sun)->directional->shadowAngularDiameter==.53f,
        "New directional lights lost the solar angular diameter default.");
    const auto defaults=*document.scene().find(sun);auto authored=defaults;authored.directional->shadowAngularDiameter=1.25f;
    require(document.updateEntity(sun,authored,"Change angular diameter")&&
        extractScene(document.scene()).directional.front().shadowAngularDiameter==1.25f,
        "Directional angular diameter did not reach scene history or render extraction.");
    require(document.undo()&&document.scene().find(sun)->directional->shadowAngularDiameter==.53f&&
        document.redo()&&document.scene().find(sun)->directional->shadowAngularDiameter==1.25f,
        "Directional angular diameter undo/redo lost its exact authored value.");
    require(document.save("Angular directional light"),document.status());
    const auto saved=Json::parse(serializeSceneSnapshot(readSceneSnapshot(project.root,document.assetId())));
    require(saved.at("version")==24&&saved.at("entities").front().at("directional").at("shadowAngularDiameter")==1.25f,
        "Directional angular diameter did not use the optional v23 light representation.");
    SceneDocument reopened(project.root);require(reopened.load(document.assetId()),reopened.status());same(document.scene().records(),reopened.scene().records());
    for(const auto angle:{0.f,2.f}) {
        auto endpoint=authored;endpoint.directional->shadowAngularDiameter=angle;
        require(document.updateEntity(sun,endpoint),"Supported directional angular diameter endpoint was rejected.");
    }
    require(document.updateEntity(sun,authored),document.status());
    const auto original=document.scene().records();const auto history=document.history().size();
    for(const auto angle:{-.01f,2.01f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
        auto invalid=authored;invalid.directional->shadowAngularDiameter=angle;
        require(!document.updateEntity(sun,invalid)&&document.history().size()==history,"Invalid angular diameter added an authored edit.");same(original,document.scene().records());
    }
    auto legacy=saved;legacy["entities"].front()["directional"].erase("shadowAngularDiameter");
    const auto id="assets/scenes/legacy-angular.crimson-scene";std::filesystem::create_directories((project.root/id).parent_path());
    const auto bytes=legacy.dump();{std::ofstream file(project.root/id);file<<bytes;}
    require(reopened.load(id)&&reopened.scene().find(sun)->directional->shadowAngularDiameter==.53f&&!reopened.dirty(),
        "Missing optional angular diameter did not default without dirtying the scene.");
    {std::ifstream file(project.root/id);const std::string retained{std::istreambuf_iterator<char>(file),{}};require(retained==bytes,"Loading an older light rewrote scene bytes.");}
    for(const auto& angle:std::array<Json,5>{Json(-.01f),Json(2.01f),Json(nullptr),Json("large"),Json(true)}) {
        auto invalid=saved;invalid["entities"].front()["directional"]["shadowAngularDiameter"]=angle;
        const auto invalidId="assets/scenes/invalid-angular.crimson-scene";{std::ofstream file(project.root/invalidId);file<<invalid.dump();}
        const auto before=reopened.scene().records();require(!reopened.load(invalidId),"Invalid saved angular diameter was accepted.");same(before,reopened.scene().records());
    }
    SceneDocument runtime(project.root);require(runtime.resetTransientFrom(document),runtime.status());
    auto live=*runtime.scene().find(sun);live.directional->shadowAngularDiameter=.25f;
    require(runtime.updateEntity(sun,live),runtime.status());pose(runtime,sun,{3,4,5});
    require(runtime.undo()&&runtime.scene().find(sun)->directional->shadowAngularDiameter==1.25f,"Live angular diameter undo lost authored light settings.");near(position(runtime.scene(),sun),{3,4,5});
    require(runtime.redo()&&runtime.scene().find(sun)->directional->shadowAngularDiameter==.25f,"Live angular diameter redo failed.");
    same(original,document.scene().records());
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
    require(serialized.at("version")==24,"Entity visibility did not write scene schema 24.");
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
    require(serialized.at("version")==24&&serialized["entities"][0]["point"].at("nearFalloffRadius")==2.75f&&
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
    require(serialized.at("version")==24,"Scene writer did not retain the current version.");
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
    }})&&document.save("Light GI runtime"),document.status());
    const auto authored=document.scene().records();const auto authoredHistory=document.history().size();const auto authoredSelection=document.selection();
    const auto checkEnergy=[&](const Scene& scene,float pointEnergy,float directionalEnergy,float localEnergy) {
        require(scene.find(point)->point->indirectEnergy==pointEnergy&&scene.find(directional)->directional->indirectEnergy==directionalEnergy,"Scene lost independent per-light GI tuning.");
        const auto extracted=extractScene(scene);
        require(extracted.points.size()==1&&extracted.directional.size()==1,"GI energy changed eligible source count.");
        for(const auto& source:extracted.points)require(source.indirectEnergy==(source.entityId==point?pointEnergy:localEnergy)&&
            source.enabled&&source.intensity>0,"Point or Local World GI energy was lost during extraction or changed direct illumination.");
        require(extracted.directional.front().indirectEnergy==directionalEnergy&&extracted.directional.front().enabled&&
            extracted.directional.front().intensity>0,"Directional GI energy was lost during extraction or changed direct illumination.");
    };
    checkEnergy(document.scene(),2.5f,0,4);
    SceneDocument runtime(project.root);require(runtime.resetTransientFrom(document)&&!runtime.dirty()&&!runtime.history().canUndo(),runtime.status());
    checkEnergy(runtime.scene(),2.5f,0,4);
    require(runtime.begin("Tune GI during Play")&&runtime.update([&](auto& records,auto&){for(auto& record:records) {
        if(record.id==point)record.point->indirectEnergy=0;
        if(record.id==directional)record.directional->indirectEnergy=3;
    }}),runtime.status());
    pose(runtime,actor,{7,0,2});require(runtime.commit(),runtime.status());checkEnergy(runtime.scene(),0,3,16);
    require(runtime.undo(),runtime.status());checkEnergy(runtime.scene(),2.5f,0,4);near(position(runtime.scene(),actor),{7,0,2});
    require(runtime.redo(),runtime.status());checkEnergy(runtime.scene(),0,3,16);near(position(runtime.scene(),actor),{7,0,2});
    const auto committedHistory=runtime.history().size();
    require(runtime.begin("Cancel Play light GI")&&runtime.update([&](auto& records,auto&){for(auto& record:records) {
        if(record.point)record.point->indirectEnergy=8;if(record.directional)record.directional->indirectEnergy=8;
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
    const auto authoredGlow=[] {
        GlowSettings glow{true,2.25f,3.5f,.65f};glow.tint={.4f,.6f,.8f};glow.saturation=1.4f;glow.hdrScale=.25f;
        glow.blendMode=GlowBlendMode::Screen;glow.mix=.25f;glow.anamorphic=-.4f;glow.quality=GlowQuality::Low;
        glow.levels={.1f,0,.3f,0,.5f,0,.7f};return glow;
    }();
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

    const auto runtimeGlow=[] {
        GlowSettings glow{true,4.25f,1.5f,2.5f};glow.tint={.8f,.4f,.2f};glow.saturation=.6f;glow.hdrScale=.85f;
        glow.blendMode=GlowBlendMode::Mix;glow.mix=.8f;glow.anamorphic=.5f;
        glow.levels={0,.8f,0,.6f,0,.4f,0};return glow;
    }();
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
void environmentGlowLevelsRuntime() {
    TempProject project;SceneDocument document(project.root);
    const auto environment=document.createEnvironment(),actor=addControlledActor(document);
    GlowSettings authored{true,.3f,2.5f,.75f};authored.tint={.6f,.8f,1};authored.hdrScale=.8f;
    authored.anamorphic=-.25f;authored.quality=GlowQuality::Low;authored.levels={.1f,.2f,0,.4f,0,.6f,.7f};
    auto record=*document.scene().find(environment);record.environment->environment.glow=authored;
    require(document.updateEntity(environment,record) && document.save("Glow runtime"),document.status());
    const auto records=document.scene().records();const auto history=document.history().size();
    const auto selection=document.selection();const bool dirty=document.dirty();
    SceneDocument runtime(project.root);require(runtime.resetTransientFrom(document),runtime.status());
    require(resolveEnvironment(runtime.scene()).environment.glow==authored && !runtime.dirty() && !runtime.history().canUndo(),
        "Play clone lost Glow tuning or inherited authored history.");
    GlowSettings live=authored;live.strength=.7f;live.radius=4;live.threshold=2;live.tint={1,.4f,.2f};
    live.hdrScale=.2f;live.anamorphic=.5f;live.quality=GlowQuality::High;live.levels={0,0,0,0,0,0,1};
    require(runtime.begin("Glow during Play"),runtime.status());
    require(runtime.update([&](auto& values,auto&){for(auto& entity:values)if(entity.id==environment)
        entity.environment->environment.glow.levels[0]=.9f;}),runtime.status());
    pose(runtime,actor,{8,0,3});
    require(runtime.update([&](auto& values,auto&){for(auto& entity:values)if(entity.id==environment)
        entity.environment->environment.glow=live;}) && runtime.commit(),runtime.status());
    require(runtime.history().size()==1 && extractScene(runtime.scene()).environment.environment.glow==live,
        "Glow gesture did not form one runtime command or extraction lost artistic controls/levels.");
    require(runtime.undo() && resolveEnvironment(runtime.scene()).environment.glow==authored,
        "Runtime Glow undo did not restore authored controls and seven levels.");near(position(runtime.scene(),actor),{8,0,3});
    require(runtime.redo() && resolveEnvironment(runtime.scene()).environment.glow==live,
        "Runtime Glow redo lost its controls.");near(position(runtime.scene(),actor),{8,0,3});
    const auto committedHistory=runtime.history().size();
    require(runtime.begin("Cancel Glow") && runtime.update([&](auto& values,auto&){for(auto& entity:values)if(entity.id==environment) {
        entity.environment->environment.glow.enabled=false;entity.environment->environment.glow.levels.fill(0);
    }}),runtime.status());
    pose(runtime,actor,{9,0,4});runtime.cancel();
    require(resolveEnvironment(runtime.scene()).environment.glow==live && runtime.history().size()==committedHistory,
        "Canceled Glow controls changed the committed values or history.");near(position(runtime.scene(),actor),{9,0,4});
    same(records,document.scene().records());
    require(document.history().size()==history && document.dirty()==dirty && document.selection().ids==selection.ids &&
        document.selection().primary==selection.primary,"Runtime Glow changed the authored scene state.");
    require(runtime.resetTransientFrom(document) && resolveEnvironment(runtime.scene()).environment.glow==authored &&
        !runtime.history().canUndo() && !runtime.dirty(),"Restarting Play retained runtime Glow edits.");
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
void meshShadingStorageHistoryAndRuntime() {
    TempProject project;SceneDocument document(project.root);
    const auto box=document.createBox(),model=document.createModel("assets/models/normal-fixture/manifest.json","Imported"),empty=document.createEmpty();
    require(box&&model&&empty,document.status());
    require(document.scene().find(box)->meshShading==MeshShading::UseMeshNormals &&
        document.scene().find(model)->meshShading==MeshShading::UseMeshNormals,"New meshes did not preserve their source normals by default.");
    auto smooth=*document.scene().find(box);smooth.meshShading=MeshShading::Smooth;
    require(document.updateEntity(box,smooth,"Smooth mesh"),document.status());
    auto flat=*document.scene().find(model);flat.meshShading=MeshShading::Flat;
    require(document.updateEntity(model,flat,"Flat mesh"),document.status());
    const auto extracted=extractScene(document.scene());
    for(const auto& mesh:extracted.models)require(mesh.meshShading==(mesh.id==box?MeshShading::Smooth:MeshShading::Flat),
        "Render extraction changed per-occurrence mesh shading.");
    require(document.undo()&&document.scene().find(model)->meshShading==MeshShading::UseMeshNormals&&document.redo()&&
        document.scene().find(model)->meshShading==MeshShading::Flat,"Mesh shading was omitted from scene undo/redo.");
    auto replacement=*document.scene().find(model);replacement.model=AssetRef<ModelAsset>{"assets/models/replacement/manifest.json"};
    require(document.updateEntity(model,replacement,"Replace mesh")&&document.scene().find(model)->meshShading==MeshShading::Flat,
        "Changing a mesh asset discarded its occurrence shading policy.");
    require(document.undo()&&document.scene().find(model)->model==flat.model&&document.scene().find(model)->meshShading==MeshShading::Flat&&
        document.redo(),"Mesh replacement undo changed the retained shading policy.");
    require(document.save("Mesh shading"),document.status());
    const auto json=Json::parse(serializeSceneSnapshot(readSceneSnapshot(project.root,document.assetId())));
    require(json.at("version")==24,"Mesh shading unnecessarily changed the scene schema.");
    SceneDocument reopened(project.root);require(reopened.load(document.assetId()),reopened.status());
    same(document.scene().records(),reopened.scene().records());require(!reopened.dirty(),"Reading mesh shading dirtied the scene.");
    const auto records=document.scene().records();const auto history=document.history().size();
    auto invalid=*document.scene().find(box);invalid.meshShading=static_cast<MeshShading>(255);
    require(!document.updateEntity(box,invalid),"Invalid mesh shading entered the scene.");
    invalid=*document.scene().find(empty);invalid.meshShading=MeshShading::Smooth;
    require(!document.updateEntity(empty,invalid),"A non-mesh entity accepted mesh shading.");
    same(records,document.scene().records());require(document.history().size()==history&&!document.dirty(),"Rejected mesh shading changed scene history.");
    for(const auto version:{22,23}) {
        auto legacy=json;legacy["version"]=version;for(auto& entity:legacy["entities"])entity.erase("meshShading");
        const std::string id="assets/scenes/default-mesh-shading.crimson-scene";writeSceneFixture(project.root,id,legacy);
        const auto path=sceneAssetPath(project.root,id);std::ifstream beforeFile(path,std::ios::binary);
        const std::string beforeBytes((std::istreambuf_iterator<char>(beforeFile)),{});
        require(reopened.load(id),reopened.status());
        for(const auto& record:reopened.scene().records())require(record.meshShading==MeshShading::UseMeshNormals,
            "A missing optional mesh shading field changed source normals.");
        std::ifstream afterFile(path,std::ios::binary);const std::string afterBytes((std::istreambuf_iterator<char>(afterFile)),{});
        require(beforeBytes==afterBytes&&!reopened.dirty(),"Defaulting mesh shading rewrote or dirtied authored data.");
    }
    for(const auto& code:std::array<Json,7>{Json(-1),Json(3),Json(256),Json(1.5),Json(true),Json("Smooth"),Json(std::numeric_limits<std::uint64_t>::max())}) {
        auto malformed=json;for(auto& entity:malformed["entities"])if(entity.contains("model"))entity["meshShading"]=code;
        const std::string id="assets/scenes/invalid-mesh-shading.crimson-scene";writeSceneFixture(project.root,id,malformed);
        const auto before=reopened.scene().records();require(!reopened.load(id),"Malformed saved mesh shading was accepted.");
        same(before,reopened.scene().records());
    }
    SceneDocument runtime(project.root);require(runtime.resetTransientFrom(document),runtime.status());
    auto live=*runtime.scene().find(box);live.meshShading=MeshShading::Flat;
    require(runtime.updateEntity(box,live),runtime.status());pose(runtime,box,{3,4,5});
    require(runtime.undo()&&runtime.scene().find(box)->meshShading==MeshShading::Smooth,"Runtime undo lost authored mesh shading.");
    near(position(runtime.scene(),box),{3,4,5});
    require(runtime.redo()&&runtime.scene().find(box)->meshShading==MeshShading::Flat,"Runtime redo lost a mesh shading patch.");
    require(runtime.begin("Cancel shading"),runtime.status());
    require(runtime.update([&](auto& entities,auto&){for(auto& entity:entities)if(entity.id==box)entity.meshShading=MeshShading::UseMeshNormals;}),runtime.status());
    pose(runtime,box,{6,7,8});runtime.cancel();
    require(runtime.scene().find(box)->meshShading==MeshShading::Flat,"Cancel did not restore mesh shading.");
    near(position(runtime.scene(),box),{6,7,8});same(records,document.scene().records());
    require(!document.dirty()&&document.history().size()==history,"Play mesh shading leaked into Edit state.");
    document.select({box});require(document.duplicateSelection(),document.status());
    require(document.scene().find(document.selection().primary)->meshShading==MeshShading::Smooth,"Duplication dropped mesh shading.");
}
void modelAnimationStorageAndRuntime() {
    TempProject project;SceneDocument document(project.root);
    const auto model=document.createModel("assets/models/animated/manifest.json","Animated model"),empty=document.createEmpty("Empty");
    require(model&&empty,document.status());
    require(document.scene().find(model)->modelAnimation==SceneModelAnimation{},"New models did not retain the imported bind pose by default.");
    auto authored=*document.scene().find(model);authored.modelAnimation.asset.id="f5d1dd3d-dcfe-4f05-891e-06ac972fd22e";authored.modelAnimation.playing=false;authored.modelAnimation.loopOverride=false;authored.modelAnimation.speed=1.5f;authored.modelAnimation.startTime=2.25f;
    require(document.updateEntity(model,authored,"Configure animation"),document.status());
    require(extractScene(document.scene()).models.front().animation==authored.modelAnimation,"Render extraction lost authored playback settings.");
    require(document.undo()&&document.scene().find(model)->modelAnimation==SceneModelAnimation{}&&document.redo(),
        "Animation controls did not participate in scene history.");
    require(document.save("Animated scene"),document.status());
    const auto snapshot=readSceneSnapshot(project.root,document.assetId());const auto json=Json::parse(serializeSceneSnapshot(snapshot));
    require(json.at("version")==24,"Animation writer did not use scene schema 24.");
    SceneDocument reopened(project.root);require(reopened.load(document.assetId()),reopened.status());
    same(document.scene().records(),reopened.scene().records());
    const auto history=document.history().size();const auto records=document.scene().records();
    const auto rejectsRecord=[&](const EntityRecord& bad) {
        require(!document.updateEntity(bad.id,bad),"Invalid animation settings were published.");
        same(document.scene().records(),records);require(document.history().size()==history,"Rejected animation edit added history.");
    };
    auto invalid=authored;
    for(const auto speed:{-.01f,100.01f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
        invalid=authored;invalid.modelAnimation.speed=speed;rejectsRecord(invalid);
    }
    for(const auto time:{-.01f,1.1e9f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
        invalid=authored;invalid.modelAnimation.startTime=time;rejectsRecord(invalid);
    }
    invalid=*document.scene().find(empty);invalid.modelAnimation.asset=authored.modelAnimation.asset;rejectsRecord(invalid);
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
    auto embedded=json;
    for(auto& entity:embedded["entities"])if(entity.contains("modelAnimation")) {
        entity["modelAnimation"].erase("asset");entity["modelAnimation"].erase("loopOverride");
        entity["modelAnimation"]["clip"]=2;entity["modelAnimation"]["loop"]=false;
    }
    const std::string embeddedId="assets/scenes/embedded-animation.crimson-scene";const auto embeddedBytes=embedded.dump();
    {std::ofstream file(project.root/embeddedId);file<<embeddedBytes;}
    require(reopened.load(embeddedId),reopened.status());
    const auto converted=reopened.scene().find(model)->modelAnimation;
    require(converted.asset.id==importedAnimationId(authored.model->id,2)&&converted.loopOverride==false&&!reopened.dirty(),
        "Legacy embedded clip did not convert to its stable Animation identity without dirtying the scene.");
    std::ifstream untouched(project.root/embeddedId);require(std::string(std::istreambuf_iterator<char>(untouched),{})==embeddedBytes,
        "Loading legacy animation rewrote its scene.");
    const auto convertedJson=Json::parse(serializeSceneSnapshot(readSceneSnapshot(project.root,embeddedId)));
    for(const auto& entity:convertedJson["entities"])if(entity.contains("modelAnimation"))
        require(entity["modelAnimation"].contains("asset")&&!entity["modelAnimation"].contains("clip"),"Current scene writer retained embedded clip indices.");
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
int main(){try{composedPlayerRecipeAndMigration();firstPersonInputStorageAndCameraConversion();playerTraceSettingsStorageHistoryAndRuntime();presentationCameraPoseOverrides();nativeColliderStorageAndValidation();componentsAndStorage();collisionDisplayStorageAndHistory();unifiedColliderStorageHistoryAndCompatibility();runtimePatches();followAndHierarchy();followDependenciesAndAtomicFailure();parentFollowKeepsWorldOffset();sphereCreationMaterialsAndStorage();primitiveParametersStorageHistoryAndRuntime();boxMaterialAndPointRadius();importedMaterialSlots();obsoleteModelFadeSettingsAreIgnored();directionalAngularDiameterStorageHistoryAndRuntime();entityVisibilityStorageAndRuntime();pointFalloffAndSpecularStorageAndRuntime();shadowSettingsStorageHistoryAndExtraction();lightIndirectEnergyExtractionAndRuntime();environmentToneMappingStorageAndRuntime();environmentGlowLevelsRuntime();environmentAmbientLightRuntime();environmentSsgiRuntime();meshShadingStorageHistoryAndRuntime();modelAnimationStorageAndRuntime();std::cout<<"Gameplay scene tests passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
