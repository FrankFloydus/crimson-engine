#include "scene/CameraAuthoring.h"
#include "scene/SceneExtraction.h"
#include "assets/AtomicFile.h"
#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {
using namespace crimson;
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void near(Vec3 a,Vec3 b,const char* why){require(math::length(a-b)<.0003f,why);}
void near(const Mat4& a,const Mat4& b,const char* why){for(unsigned i=0;i<16;++i)require(std::abs(a[i]-b[i])<.0003f,why);}
Vec3 forward(const Scene& scene,EntityId camera) {
    const auto view=resolveSceneCamera(scene,camera);require(view.has_value(),"Camera view is missing.");
    return math::normalized(view->target-view->position);
}
struct Project {
    std::filesystem::path root=std::filesystem::temp_directory_path()/
        ("crimson-camera-authoring-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~Project(){std::error_code error;const auto temp=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),error);if(error)return;
        const auto target=std::filesystem::weakly_canonical(root,error);
        if(!error&&target.parent_path()==temp&&target.filename().string().starts_with("crimson-camera-authoring-"))std::filesystem::remove_all(target,error);}
};
void ownedCameraAndDefaults(const std::filesystem::path& root) {
    SceneDocument document(root);const auto external=document.createCamera("Explicit override");
    const auto player=document.createPlayer();const auto camera=resolveOwnedActorCamera(document.scene(),player);
    require(camera&&camera!=external,"Character camera resolution used a scene camera outside the character.");
    const auto record=*document.scene().find(camera);
    require(record.parent==player&&record.cameraFollow&&record.cameraFollow->target==player,"Camera is not an actual following child inside the character.");
    const auto snapshot=captureCameraTemplate(document.scene(),camera,player);
    require(snapshot.has_value(),"Could not capture the owned camera.");
    near(snapshot->offset,{0,1.65f,0},"Player camera offset does not match first-person eye height.");
    near(cameraTemplateRotationDegrees(*snapshot),{},"Player camera does not face forward along -Z.");
    require(snapshot->lens==SceneCamera{75,.05f,2048},"Player camera lens does not match the first-person defaults.");
    require(!document.scene().find(player)->navigation,"First-person Player recipe acquired an unsolicited Navigation Agent.");
    require(document.scene().properties().activeCamera==external,"Creating a character replaced an explicitly chosen scene camera.");
    const auto enemy=document.createEnemy();require(!resolveOwnedActorCamera(document.scene(),enemy),"Enemy acquired an unsolicited camera.");
    document.select({player});require(document.duplicateSelection(),"Could not duplicate character fixture.");
    const auto duplicate=document.selection().primary;const auto duplicateCamera=resolveOwnedActorCamera(document.scene(),duplicate);
    require(duplicateCamera&&duplicateCamera!=camera&&document.scene().find(duplicateCamera)->cameraFollow->target==duplicate,
        "Duplicated character did not retain its own independent child camera.");
}
void authoringHistoryAndPlacement(const std::filesystem::path& root) {
    SceneDocument document(root);const auto player=document.createPlayer();const auto camera=resolveOwnedActorCamera(document.scene(),player);
    auto actor=*document.scene().find(player);actor.local=math::translation({12,0,-4})*math::eulerRotationXYZ({0,.8f,0});
    require(document.updateEntity(player,actor),"Could not place the character fixture.");
    const auto actorLocal=actor.local;const auto baseline=*document.scene().find(camera);
    near(forward(document.scene(),camera),math::transformDirection(actor.local,{0,0,-1}),"Player rotation did not turn its eye camera.");
    auto settings=*captureCameraTemplate(document.scene(),camera,player);settings.offset={4,9,4};settings.lens.verticalFov=42;
    require(setCameraTemplateRotationDegrees(settings,{-58,45,7}),"Could not author camera rotation.");
    const auto history=document.history().size();require(document.begin("Edit character camera"),"Could not start a camera gesture.");
    require(updateCameraAuthoring(document,camera,settings),"Could not update child camera inside the character gesture.");
    settings.offset={5,10,5};require(updateCameraAuthoring(document,camera,settings)&&document.commit(),"Could not finish character camera gesture.");
    require(document.history().size()==history+1,"Camera gesture created more than one undo command.");
    require(document.scene().find(player)->local==actorLocal,"Editing the character camera moved the character itself.");
    near(resolveSceneCamera(document.scene(),camera)->position,Vec3{12,0,-4}+settings.offset,"Camera offset followed actor rotation instead of world axes.");
    near(forward(document.scene(),camera),math::transformDirection(actor.local,{0,0,-1}),"Camera template rotation overrode the Player's forward direction.");
    require(document.scene().find(camera)->cameraFollow->worldBasis==Mat4{}&&
        captureCameraTemplate(document.scene(),camera,player)->worldBasis==Mat4{},"FPS authoring retained an independent camera rotation.");
    require(document.undo(),"Camera gesture did not undo.");near(document.scene().find(camera)->local,baseline.local,"Camera undo lost placement.");
    require(document.scene().find(camera)->camera==baseline.camera,"Camera undo lost lens settings.");
    require(document.redo(),"Camera gesture did not redo.");
    const auto committed=*document.scene().find(camera);const auto committedHistory=document.history().size();
    require(document.begin("Cancel camera gesture"),"Could not start cancellation fixture.");settings.offset={8,17,8};
    require(updateCameraAuthoring(document,camera,settings),"Could not preview camera gesture.");document.cancel();
    require(sameEntity(*document.scene().find(camera),committed)&&document.history().size()==committedHistory,"Escape-style cancellation lost camera values or added history.");
    actor.local=math::translation({12,0,-4})*math::eulerRotationXYZ({0,-.65f,0});
    require(document.updateEntity(player,actor),"Could not rotate the Player after authoring its camera.");
    near(forward(document.scene(),camera),math::transformDirection(actor.local,{0,0,-1}),"Rotating the Player left its authored camera facing the old direction.");
    near(resolveSceneCamera(document.scene(),camera)->position,Vec3{12,0,-4}+committed.cameraFollow->offset,"Player rotation changed the authored eye offset.");
    require(document.scene().find(camera)->camera==committed.camera,"Player rotation changed the camera lens.");
    require(document.undo(),"Player rotation did not undo.");
    near(forward(document.scene(),camera),math::transformDirection(actorLocal,{0,0,-1}),"Player rotation undo did not restore camera forward.");
    require(document.redo(),"Player rotation did not redo.");
    near(forward(document.scene(),camera),math::transformDirection(actor.local,{0,0,-1}),"Player rotation redo did not restore camera forward.");
    require(document.save("Owned camera"),"Could not save character camera.");const auto savedId=document.assetId();
    SceneDocument reopened(root);require(reopened.load(savedId),"Could not reopen character camera.");
    require(resolveOwnedActorCamera(reopened.scene(),player)==camera,"Saved character camera lost its ownership.");
    near(*reopened.scene().worldTransform(camera),*document.scene().worldTransform(camera),"Opening a scene reset authored camera placement.");
    require(reopened.scene().find(camera)->camera==committed.camera,"Opening a scene reset authored FOV.");
    near(forward(reopened.scene(),camera),math::transformDirection(actor.local,{0,0,-1}),"Opening a scene lost the Player's camera direction.");
}
void templateApplicationAndFailures(const std::filesystem::path& root) {
    SceneDocument document(root);const auto player=document.createPlayer();const auto camera=resolveOwnedActorCamera(document.scene(),player);
    auto settings=defaultPlayerCameraTemplate();settings.offset={3,8,3};settings.lens.verticalFov=50;
    const auto standalone=document.createCamera("Override");require(document.setActiveCamera(standalone),"Could not select override.");
    const auto history=document.history().size();require(updateCameraAuthoring(document,camera,settings,"Apply camera template"),"Could not apply camera template.");
    require(document.history().size()==history+1&&document.scene().properties().activeCamera==standalone,"Template application changed camera roles or command count.");
    require(document.scene().find(camera)->parent==player&&document.scene().find(camera)->cameraFollow->target==player,"Template application rewrote parent or follow identity.");
    require(updateCameraAuthoring(document,standalone,settings),"Could not apply settings to a standalone camera.");
    near(resolveSceneCamera(document.scene(),standalone)->position,settings.offset,"Standalone camera settings did not use world position.");
    const auto before=*document.scene().find(camera);const auto failedHistory=document.history().size();
    settings.lens.nearPlane=settings.lens.farPlane;
    require(!updateCameraAuthoring(document,camera,settings)&&sameEntity(before,*document.scene().find(camera))&&document.history().size()==failedHistory,
        "Invalid lens settings changed the camera or history.");
    settings=defaultPlayerCameraTemplate();settings.offset[0]=std::numeric_limits<float>::infinity();
    require(!updateCameraAuthoring(document,camera,settings)&&sameEntity(before,*document.scene().find(camera)),"Nonfinite camera placement was accepted.");
}
void rootForwardThroughAffineParent(const std::filesystem::path& root) {
    SceneDocument document(root);const auto group=document.createEmpty("Reflected parent"),player=document.createPlayer();
    const auto camera=resolveOwnedActorCamera(document.scene(),player);
    auto parent=*document.scene().find(group);
    parent.local=math::eulerRotationXYZ({.2f,.6f,-.15f})*math::scaling({-2,1.5f,.7f});parent.local.at(0,1)+=.3f;
    require(document.updateEntity(group,parent),"Could not author the affine Player parent.");
    auto actor=*document.scene().find(player);actor.parent=group;actor.local=math::eulerRotationXYZ({0,.4f,0});actor.playerInput->enabled=false;
    require(document.updateEntity(player,actor),"Could not parent the Player or disable input.");
    const auto world=*document.scene().worldTransform(player);
    near(forward(document.scene(),camera),math::normalized(math::transformDirection(world,{0,0,-1})),
        "An affine parent or disabled input changed the FPS camera's root-forward direction.");
    auto settings=defaultPlayerCameraTemplate();settings.offset[1]=1.8f;settings.lens.verticalFov=91;
    require(updateCameraAuthoring(document,camera,settings),"Could not author an affine Player's camera lens and height.");
    near(forward(document.scene(),camera),math::normalized(math::transformDirection(world,{0,0,-1})),
        "Camera authoring changed root forward beneath an affine parent.");
    near(resolveSceneCamera(document.scene(),camera)->position,math::transformPoint(world,{})+settings.offset,
        "An affine parent changed the authored world-space eye height.");
}
void existingPlayerCameraMigration(const std::filesystem::path& root) {
    SceneDocument original(root);const auto player=original.createPlayer();const auto camera=resolveOwnedActorCamera(original.scene(),player);
    auto actor=*original.scene().find(player);
    actor.local=math::translation({15,0,-8})*math::eulerRotationXYZ({0,.35f,0});
    require(original.updateEntity(player,actor),"Could not position legacy character.");
    original.select({camera});require(original.reparentSelection(NoEntity),"Could not construct root camera fixture.");
    auto authored=defaultPlayerCameraTemplate();authored.offset={7,13,5};authored.lens={38,.15f,1700};
    require(setCameraTemplateRotationDegrees(authored,{-53,42,3})&&updateCameraAuthoring(original,camera,authored),"Could not author legacy camera.");
    require(original.save("Legacy root player camera"),"Could not save migration fixture.");
    const auto world=*original.scene().worldTransform(camera);const auto originalRecord=*original.scene().find(camera);
    const auto path=sceneAssetPath(root,original.assetId());std::ifstream input(path,std::ios::binary);
    auto json=nlohmann::json::parse(input);input.close();json["version"]=16;writeFileAtomically(path,json.dump(2));
    const auto fingerprint=sceneFileFingerprint(root,original.assetId());
    SceneDocument migrated(root);require(migrated.load(original.assetId()),"Could not migrate legacy player camera.");
    require(!resolveOwnedActorCamera(migrated.scene(),player)&&migrated.scene().find(camera)->parent==NoEntity&&
        migrated.scene().properties().activeCamera==camera,"Custom legacy camera was adopted or replaced by an FPS eye camera.");
    near(*migrated.scene().worldTransform(camera),world,"Legacy camera migration changed the player's custom view.");
    require(migrated.scene().find(camera)->camera==originalRecord.camera&&migrated.scene().find(camera)->cameraFollow==originalRecord.cameraFollow,
        "Legacy camera migration changed authored lens/follow settings.");
    require(migrated.scene().records().size()==original.scene().records().size()&&sceneFileFingerprint(root,original.assetId())==fingerprint,
        "Legacy camera migration duplicated entities or wrote the saved scene during load.");
    require(migrated.save(),"Could not save the preserved custom camera.");
    SceneDocument reopened(root);require(reopened.load(migrated.assetId())&&reopened.scene().find(camera)->parent==NoEntity&&
        reopened.scene().properties().activeCamera==camera,"Custom player camera did not reopen as the same standalone entity.");
    near(*reopened.scene().worldTransform(camera),world,"Saving and reopening changed the custom legacy camera's view.");
    // New documents can still author an intentional standalone camera that follows
    // a player. Current-format saves must not silently reparent that override.
    original.select({camera});require(original.save("Explicit root camera",true),"Could not save current-format override.");
    SceneDocument explicitCamera(root);require(explicitCamera.load(original.assetId()),"Could not load explicit root camera.");
    require(explicitCamera.scene().find(camera)->parent==NoEntity,"Current-format standalone camera was silently reparented.");
}
void compatiblePlayerCameraMigration(const std::filesystem::path& root) {
    SceneDocument original(root);const auto player=original.createPlayer();const auto camera=resolveOwnedActorCamera(original.scene(),player);
    original.select({camera});require(original.reparentSelection(NoEntity),"Could not construct a root-forward legacy camera.");
    require(original.save("Root-forward legacy player camera"),"Could not save a root-forward legacy camera.");
    const auto world=*original.scene().worldTransform(camera);const auto originalRecord=*original.scene().find(camera);
    const auto path=sceneAssetPath(root,original.assetId());std::ifstream input(path,std::ios::binary);
    auto json=nlohmann::json::parse(input);input.close();json["version"]=16;writeFileAtomically(path,json.dump(2));
    const auto fingerprint=sceneFileFingerprint(root,original.assetId());
    SceneDocument migrated(root);require(migrated.load(original.assetId()),"Could not migrate the root-forward legacy camera.");
    require(resolveOwnedActorCamera(migrated.scene(),player)==camera&&migrated.scene().find(camera)->parent==player&&
        migrated.scene().properties().activeCamera==camera,"Compatible legacy camera did not become the same Player's child.");
    near(*migrated.scene().worldTransform(camera),world,"Adopting the compatible camera changed its view.");
    require(migrated.scene().find(camera)->camera==originalRecord.camera&&migrated.scene().find(camera)->cameraFollow==originalRecord.cameraFollow&&
        migrated.scene().records().size()==original.scene().records().size()&&sceneFileFingerprint(root,original.assetId())==fingerprint,
        "Compatible camera adoption changed lens/follow values, duplicated entities or rewrote the saved scene.");
    require(migrated.save(),"Could not save the adopted root-forward camera.");
    SceneDocument reopened(root);require(reopened.load(migrated.assetId())&&resolveOwnedActorCamera(reopened.scene(),player)==camera,
        "Adopted compatible camera did not reopen as the same child entity.");
    near(*reopened.scene().worldTransform(camera),world,"Reopening the compatible camera changed its view.");
}
void savedSceneCameraOwnership(const std::filesystem::path& root) {
    unsigned scenes{},ownedCameras{},standaloneCameras{};
    for(const auto& asset:scanSceneAssets(root)) {
        const auto before=sceneFileFingerprint(root,asset.id);const auto snapshot=readSceneSnapshot(root,asset.id);
        Scene scene;scene.apply(snapshot.records,snapshot.properties);
        if(const auto actor=scene.properties().playerCharacter) {
            const auto camera=resolveOwnedActorCamera(scene,actor);
            if(const auto active=scene.find(scene.properties().activeCamera);active && active->cameraFollow && active->cameraFollow->target==actor) {
                require(captureCameraTemplate(scene,active->id,actor).has_value(),"Existing character camera settings are not editable.");
                if(active->parent==actor) {
                    require(camera==active->id,"Existing owned camera does not resolve inside its Character.");++ownedCameras;
                } else if(!active->parent)++standaloneCameras;
            }
        }
        require(sceneFileFingerprint(root,asset.id)==before,"Camera ownership verification rewrote an authored scene.");++scenes;
    }
    std::cout<<"Read "<<scenes<<" saved scenes; "<<ownedCameras<<" owned player camera(s), "<<standaloneCameras<<" standalone player camera(s), without writing files.\n";
}
}
int main(int argc,char** argv){try{
    if(argc==3&&std::string_view(argv[1])=="--project-root"){savedSceneCameraOwnership(argv[2]);return 0;}
    Project project;ownedCameraAndDefaults(project.root);authoringHistoryAndPlacement(project.root);templateApplicationAndFailures(project.root);rootForwardThroughAffineParent(project.root);existingPlayerCameraMigration(project.root);compatiblePlayerCameraMigration(project.root);
    std::cout<<"Character-owned camera defaults, independent authoring, template application, history and preservation passed.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
