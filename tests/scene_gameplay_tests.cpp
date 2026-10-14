#include "scene/SceneDocument.h"
#include "scene/SceneExtraction.h"
#include "scene/BuiltinModels.h"
#include "assets/MaterialAsset.h"
#include "math/Json.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

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

void componentsAndStorage(){
    TempProject project;SceneDocument document(project.root);
    const auto character=document.createCharacter(),second=document.createCharacter("Other");
    const auto box=document.createBox();const auto collider=document.createCollider("Collider",box);
    const auto camera=document.createCamera(),otherCamera=document.createCamera("Other camera");
    require(character && second && box && collider && camera && otherCamera,document.status());
    require(document.scene().properties().playerCharacter==character && document.scene().properties().activeCamera==camera,"Additional entities stole activation.");
    require(document.scene().find(box)->model->id==BuiltinBoxModelId,"Box did not use builtin geometry.");
    require(document.scene().find(collider)->parent==box && document.scene().find(collider)->local==Mat4{},"Collider child was not identity local.");
    require(document.setCameraFollow(camera,character),document.status());
    auto characterRecord=*document.scene().find(character);characterRecord.character->moveSpeed=7;require(document.updateEntity(character,characterRecord),document.status());
    require(document.save("Gameplay"),document.status());
    const auto snapshot=readSceneSnapshot(project.root,document.assetId());
    require(Json::parse(serializeSceneSnapshot(snapshot)).at("version")==5,"Scene writer did not use version 5.");
    SceneDocument reopened(project.root);require(reopened.load(document.assetId()),reopened.status());same(document.scene().records(),reopened.scene().records());
    require(sameProperties(document.scene().properties(),reopened.scene().properties()),"Gameplay scene properties did not round-trip.");
    auto legacy=Json::parse(serializeSceneSnapshot(snapshot));legacy["version"]=3;legacy["settings"].erase("activeCamera");legacy["settings"].erase("playerCharacter");
    for(auto& record:legacy["entities"]){record.erase("character");record.erase("collider");record.erase("camera");}
    const auto legacyId="assets/scenes/legacy-gameplay.crimson-scene";
    {std::ofstream stream(project.root/legacyId);stream<<legacy.dump();}
    require(reopened.load(legacyId),reopened.status());require(!reopened.scene().properties().activeCamera && !reopened.scene().properties().playerCharacter,"Legacy scene gained gameplay activations.");
    auto invalid=*document.scene().find(character);invalid.character->radius=2;
    require(!document.updateEntity(character,invalid),"Invalid capsule accepted.");
    invalid=*document.scene().find(collider);invalid.collider->size[0]=0;require(!document.updateEntity(collider,invalid),"Zero collider size accepted.");
    invalid=*document.scene().find(camera);invalid.camera->nearPlane=invalid.camera->farPlane;require(!document.updateEntity(camera,invalid),"Invalid camera clipping accepted.");
    invalid=*document.scene().find(camera);invalid.camera->verticalFov=.5f;require(!document.updateEntity(camera,invalid),"Out-of-contract camera FOV accepted.");
    invalid=*document.scene().find(box);invalid.model=AssetRef<ModelAsset>{"builtin:unknown"};require(!document.updateEntity(box,invalid),"Unknown builtin model identity accepted.");
    require(!document.setActiveCamera(character) && !document.setPlayerCharacter(camera),"Wrong component activation accepted.");
}

void runtimePatches(){
    TempProject project;SceneDocument authored(project.root);
    const auto actor=authored.createCharacter(),other=authored.createCharacter("Other");
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
    require(runtime.scene().find(actor)->name=="Character","Rename Undo failed.");
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
    const auto actor=document.createCharacter(),camera=document.createCamera();const auto cameraChild=document.createEmpty("Camera child");
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
    EntityId copiedActor{},copiedCamera{};for(const auto id:document.scene().subtree(copy)){const auto r=document.scene().find(id);if(r->character)copiedActor=id;if(r->camera)copiedCamera=id;}
    require(copiedActor && copiedCamera && document.scene().find(copiedCamera)->camera->followTarget==copiedActor,"Duplicated follow reference did not remap internally.");
    require(document.scene().properties().activeCamera==camera && document.scene().properties().playerCharacter==actor,"Duplicate stole active roles.");
    const auto atDeletion=position(document.scene(),camera);document.select({actor});require(document.eraseSelection(),document.status());
    require(!document.scene().properties().playerCharacter && !document.scene().find(camera)->camera->followTarget,"Deletion retained player/follow references.");near(position(document.scene(),camera),atDeletion);
    require(document.undo(),document.status());require(document.scene().properties().playerCharacter==actor && document.scene().find(camera)->camera->followTarget==actor,"Deletion Undo lost stable references.");
    document.select({camera});require(!document.ungroupSelection(),"Camera was treated as an empty group.");
    SceneDocument runtime(project.root);require(runtime.resetTransientFrom(document),runtime.status());
    const auto offset=runtime.scene().find(camera)->camera->followOffset;pose(runtime,actor,{9,0,2});
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
    const auto offset=document.scene().find(camera)->camera->followOffset;
    const auto localBefore=document.scene().find(camera)->local;
    auto transformed=*document.scene().find(parent);
    transformed.local=math::translation({1,2,3})*math::rotation(math::axisAngle({0,1,0},.7))*math::scaling({2,3,4});
    require(document.updateEntity(parent,transformed),document.status());
    near(position(document.scene(),camera),position(document.scene(),parent)+offset);
    require(document.scene().find(camera)->camera->followOffset==offset,"Parent rotation/scale changed the world-space follow offset.");
    const auto expectedDirection=math::normalized(math::transformDirection(transformed.local*localBefore,{0,0,-1}));
    const auto game=resolveGameCamera(document.scene());near(math::normalized(game->target-game->position),expectedDirection);
    require(document.undo(),document.status());near(position(document.scene(),camera),offset);
}
void boxMaterialAndPointRadius(){
    TempProject project;SceneDocument document(project.root);
    const auto first=document.createBox("First"),second=document.createBox("Second"),light=document.createPointLight();
    const std::string red="assets/materials/red.crimson-material",blue="assets/materials/blue.crimson-material";
    auto edited=*document.scene().find(first);edited.modelMaterial=AssetRef<MaterialAsset>{red};require(document.updateEntity(first,edited),document.status());
    require(document.scene().find(second)->modelMaterial.empty(),"Assigning one Box changed another instance.");
    require(document.undo() && document.scene().find(first)->modelMaterial.empty(),"Material assignment did not undo.");
    require(document.redo() && document.scene().find(first)->modelMaterial.id==red,"Material assignment did not redo.");
    document.select({first});require(document.duplicateSelection(),document.status());const auto duplicate=document.selection().primary;
    require(document.scene().find(duplicate)->modelMaterial.id==red,"Duplicate lost its material assignment.");
    edited=*document.scene().find(light);edited.point->radius=6.25f;edited.local=math::translation({2,3,4})*math::scaling({4,2,3});
    require(document.updateEntity(light,edited),document.status());
    auto extracted=extractScene(document.scene());
    require(extracted.models.size()==3 && extracted.models[0].assetId==BuiltinBoxModelId,"Box geometry extraction changed.");
    for(const auto& model:extracted.models)require(model.materialId==(model.id==second?"":red),"Extraction lost an instance material.");
    require(extracted.points.size()==1 && extracted.points[0].radius==6.25f,"Point radius was lost or scaled in extraction.");
    require(document.undo() && document.scene().find(light)->point->radius==20.f,"Point radius was absent from history equality.");
    require(document.redo(),document.status());
    require(document.save("Materials and range"),document.status());SceneDocument reopened(project.root);require(reopened.load(document.assetId()),reopened.status());
    same(document.scene().records(),reopened.scene().records());
    auto legacy=Json::parse(serializeSceneSnapshot(readSceneSnapshot(project.root,document.assetId())));
    for(auto& entity:legacy["entities"]){entity.erase("modelMaterial");if(entity.contains("point"))entity["point"].erase("radius");}
    const auto legacyId="assets/scenes/legacy-material-range.crimson-scene";{std::ofstream stream(project.root/legacyId);stream<<legacy.dump();}
    require(reopened.load(legacyId),reopened.status());require(reopened.scene().find(first)->modelMaterial.empty() && reopened.scene().find(light)->point->radius==20.f,"Legacy defaults changed.");
    edited=*document.scene().find(light);edited.modelMaterial=AssetRef<MaterialAsset>{red};require(!document.updateEntity(light,edited),"Non-model material override accepted.");
    edited=*document.scene().find(first);edited.model=AssetRef<ModelAsset>{"assets/models/example/model.json"};require(!document.updateEntity(first,edited),"Imported model accepted an unsupported instance override.");
    edited=*document.scene().find(first);edited.modelMaterial=AssetRef<MaterialAsset>{DefaultParticleMaterialId};require(!document.updateEntity(first,edited),"Protected sprite material accepted for a Box.");
    edited.modelMaterial=AssetRef<MaterialAsset>{"assets/materials/../outside.crimson-material"};require(!document.updateEntity(first,edited),"Unsafe material reference accepted.");
    edited=*document.scene().find(light);for(const float radius:{0.f,-1.f,100001.f,std::numeric_limits<float>::quiet_NaN()}){edited.point->radius=radius;require(!document.updateEntity(light,edited),"Invalid point radius accepted.");}
    SceneDocument runtime(project.root);require(runtime.resetTransientFrom(document),runtime.status());
    require(runtime.begin("Live material assignment"),runtime.status());
    require(runtime.update([&](auto& records,auto&){for(auto& record:records)if(record.id==first)record.modelMaterial=AssetRef<MaterialAsset>{blue};}),runtime.status());
    pose(runtime,first,{10,0,0});require(runtime.commit(),runtime.status());
    require(runtime.undo() && runtime.scene().find(first)->modelMaterial.id==red,"Runtime assignment did not undo.");near(position(runtime.scene(),first),{10,0,0});
    require(runtime.redo() && runtime.scene().find(first)->modelMaterial.id==blue,"Runtime assignment did not redo.");near(position(runtime.scene(),first),{10,0,0});
    const auto references=runtime.historicalMaterialReferences();require(std::find(references.begin(),references.end(),red)!=references.end() && std::find(references.begin(),references.end(),blue)!=references.end(),"Material assignment history lost a dependency.");
    require(document.scene().find(first)->modelMaterial.id==red && !document.dirty(),"Runtime material changed the authored document.");
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
        if(record.id!=parent && record.name==parentRecord.name+" Copy") {
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
    require(serialized.at("version")==5,"Scene writer did not retain the current version.");
    SceneDocument reopened(project.root);require(reopened.load(document.assetId()),reopened.status());same(document.scene().records(),reopened.scene().records());
    for(const int version:{1,2,3,4}) {
        auto legacy=serialized;legacy["version"]=version;
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
void environmentToneMappingStorageAndRuntime() {
    TempProject project;SceneDocument document(project.root);
    const auto environment=document.createEnvironment(),actor=document.createCharacter();
    require(environment && actor,document.status());
    require(resolveEnvironment(document.scene()).environment.toneMapping==ToneMapping::Reinhard,"New environment changed legacy appearance.");
    const GlowSettings authoredGlow{true,2.25f,3.5f,.65f};
    auto record=*document.scene().find(environment);record.environment->environment.toneMapping=ToneMapping::AgX;
    record.environment->environment.glow=authoredGlow;
    require(document.updateEntity(environment,record),document.status());
    require(document.undo() && resolveEnvironment(document.scene()).environment.toneMapping==ToneMapping::Reinhard,"Tone mapping equality omitted history.");
    require(resolveEnvironment(document.scene()).environment.glow==GlowSettings{},"Environment undo lost the original disabled Glow defaults.");
    require(document.redo() && document.save("Tone mapping"),document.status());
    SceneDocument reopened(project.root);require(reopened.load(document.assetId()),reopened.status());
    require(resolveEnvironment(reopened.scene()).environment.toneMapping==ToneMapping::AgX,"Tone mapping did not round-trip.");
    require(resolveEnvironment(reopened.scene()).environment.glow==authoredGlow,"Nondefault Glow did not survive scene persistence.");
    document.select({environment});require(document.duplicateSelection(),document.status());
    for(const auto& entity:document.scene().records())if(entity.environment) {
        require(entity.environment->environment.toneMapping==ToneMapping::AgX,"Environment duplication lost tone mapping.");
        require(entity.environment->environment.glow==authoredGlow,"Environment duplication lost authored Glow values.");
    }
    auto legacy=Json::parse(serializeSceneSnapshot(readSceneSnapshot(project.root,document.assetId())));
    for(auto& entity:legacy["entities"])if(entity.contains("environment"))entity["environment"]["environment"].erase("toneMapping");
    const std::string legacyId="assets/scenes/legacy-tone.crimson-scene";
    {std::ofstream output(project.root/legacyId);output<<legacy.dump();}
    require(reopened.load(legacyId),reopened.status());
    require(resolveEnvironment(reopened.scene()).environment.toneMapping==ToneMapping::Reinhard,"Missing tone mapping did not preserve old scenes.");
    require(resolveEnvironment(reopened.scene()).environment.glow==authoredGlow,"Missing tone mapping reset unrelated Glow settings.");
    record.environment->environment.toneMapping=static_cast<ToneMapping>(99);
    require(!document.updateEntity(environment,record),"Invalid tone mapping entered the scene.");
    const auto authored=document.scene().records();const auto history=document.history().size();
    const auto authoredDirty=document.dirty();const auto authoredSelection=document.selection();
    SceneDocument runtime(project.root);require(runtime.resetTransientFrom(document),runtime.status());
    require(resolveEnvironment(runtime.scene()).environment.glow==authoredGlow && !runtime.dirty() && !runtime.history().canUndo(),
        "Play clone discarded Glow or inherited authored dirty/history state.");
    require(runtime.begin("Tone mapping during Play"),runtime.status());
    require(runtime.update([&](auto& records,auto&){for(auto& entity:records)if(entity.id==environment)entity.environment->environment.toneMapping=ToneMapping::ACES;}),runtime.status());
    pose(runtime,actor,{7,0,2});require(runtime.commit(),runtime.status());
    require(runtime.undo() && resolveEnvironment(runtime.scene()).environment.toneMapping==ToneMapping::AgX,"Runtime tone mapping undo failed.");
    near(position(runtime.scene(),actor),{7,0,2});
    require(runtime.redo() && resolveEnvironment(runtime.scene()).environment.toneMapping==ToneMapping::ACES,"Runtime tone mapping redo failed.");
    near(position(runtime.scene(),actor),{7,0,2});
    require(resolveEnvironment(runtime.scene()).environment.glow==authoredGlow,"Runtime tone mapping changed unrelated Glow.");

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
        resolveEnvironment(runtime.scene()).environment.toneMapping==ToneMapping::AgX && !runtime.history().canUndo() && !runtime.dirty(),
        "Starting Play again retained runtime Glow, tone mapping or history.");
}
}
int main(){try{componentsAndStorage();runtimePatches();followAndHierarchy();followDependenciesAndAtomicFailure();parentFollowKeepsWorldOffset();boxMaterialAndPointRadius();shadowSettingsStorageHistoryAndExtraction();environmentToneMappingStorageAndRuntime();std::cout<<"Gameplay scene tests passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
