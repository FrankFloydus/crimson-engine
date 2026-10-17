#include "scene/SceneComponents.h"
#include "scene/SceneOutlinerRows.h"
#include "scene/BuiltinModels.h"
#include "scene/SceneExtraction.h"
#include "math/Json.h"
#include <algorithm>
#include <nlohmann/json.hpp>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <unordered_set>
#include <iostream>
#include <limits>
#include <string_view>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void componentTransactions(const std::filesystem::path& root) {
    SceneDocument document(root);const auto id=document.createEmpty("Enemy");
    const auto before=document.history().size();
    require(addSceneComponent(document,id,SceneComponentKind::Navigation),"Could not compose navigation.");
    auto record=*document.scene().find(id);
    require(record.controller&&record.motor&&record.navigation&&!record.playerInput,"Navigation dependency closure was wrong.");
    require(document.history().size()==before+1,"Adding navigation created multiple history commands.");
    require(!addSceneComponent(document,id,SceneComponentKind::Navigation),"Duplicate components were accepted.");
    require(document.undo()&&!document.scene().find(id)->controller,"Dependency closure did not undo together.");
    require(document.redo(),"Dependency closure did not redo.");
    document.edit("Tune enemy",[&](auto& records,auto&){for(auto& entity:records)if(entity.id==id){entity.controller->height=2.4f;entity.motor->moveSpeed=3.25f;}});
    require(addSceneComponent(document,id,SceneComponentKind::LightRig),"Could not attach actor rig.");
    require(addSceneComponent(document,id,SceneComponentKind::DarknessReveal),"Could not attach independent reveal.");
    record=*document.scene().find(id);
    require(record.actor&&record.lightRig&&record.darknessReveal,"Rig did not establish visual ownership.");
    require(!record.lightRig->localWorld.enabled,"Generic actor rig implicitly emitted player-style World illumination.");
    require(record.motor->moveSpeed==3.25f&&record.controller->height==2.4f,"Adding lighting reset locomotion values.");
    require(removeSceneComponent(document,id,SceneComponentKind::LightRig),"Could not remove rig.");
    require(document.scene().find(id)->actor&&document.scene().find(id)->motor&&document.scene().find(id)->darknessReveal,"Removing rig removed unrelated capabilities.");
    require(document.undo()&&document.scene().find(id)->lightRig,"Rig removal did not restore through undo.");
    require(document.edit("Tune rig",[&](auto& records,auto&){for(auto& entity:records)if(entity.id==id)entity.lightRig->key.intensity=2;}),"Could not tune restored rig.");
    const auto removalHistory=document.history().size();
    require(removeSceneComponent(document,id,SceneComponentKind::Controller),"Could not remove controller.");
    record=*document.scene().find(id);
    require(!record.controller&&!record.motor&&!record.navigation&&record.lightRig&&record.darknessReveal,"Controller removal retained invalid dependents or erased lighting.");
    require(document.history().size()==removalHistory+1,"Dependent removal was not one undo command.");
    require(document.undo(),"Controller removal did not undo.");record=*document.scene().find(id);
    require(record.controller->height==2.4f&&record.motor->moveSpeed==3.25f&&record.navigation,"Controller undo lost tuned dependent settings.");
    require(document.begin("Edit Motor"),"Could not start motor gesture.");
    require(document.update([&](auto& records,auto&){for(auto& entity:records)if(entity.id==id)entity.motor->moveSpeed=8;}),"Could not update motor gesture.");
    document.cancel();require(document.scene().find(id)->motor->moveSpeed==3.25f,"Gesture cancellation lost the disconnected motor settings.");
    const auto standalone=document.createEmpty("Reveal volume");
    require(addSceneComponent(document,standalone,SceneComponentKind::DarknessReveal),"Standalone reveal rejected.");
    require(!document.scene().find(standalone)->actor&&!document.scene().find(standalone)->controller,"Reveal acquired unrelated actor/physics capabilities.");
}
void playerRemovalAndCamera(const std::filesystem::path& root) {
    SceneDocument document(root);const auto player=document.createPlayer();
    require(player&&document.scene().properties().playerCharacter==player,"Player recipe failed to select the input owner.");
    const auto camera=document.scene().properties().activeCamera;
    require(camera&&document.scene().find(camera)->parent==player,"Player recipe did not create its child camera.");
    require(document.scene().find(camera)->cameraFollow->target==player,"Child camera did not follow its owner.");
    const auto cameraWorld=*document.scene().worldTransform(camera);
    require(removeSceneComponent(document,player,SceneComponentKind::PlayerInput),"Could not remove player input.");
    const auto actor=*document.scene().find(player);
    require(!document.scene().properties().playerCharacter&&actor.motor&&actor.controller&&actor.navigation&&actor.lightRig,"Input removal left a stale player role or removed shared capabilities.");
    require(document.scene().worldTransform(camera)==cameraWorld,"Removing input moved the camera.");
    require(document.undo()&&document.scene().properties().playerCharacter==player,"Input undo lost the explicit player role.");
    require(removeSceneComponent(document,camera,SceneComponentKind::Camera),"Could not remove camera component.");
    require(!document.scene().properties().activeCamera&&!document.scene().find(camera)->cameraFollow,"Camera removal retained stale active/follow references.");
    require(document.scene().contains(camera)&&document.scene().contains(player),"Component removal deleted an entity.");
    require(document.undo()&&document.scene().properties().activeCamera==camera&&document.scene().find(camera)->cameraFollow,"Camera undo lost its follow configuration.");
    const auto components=sceneComponents(*document.scene().find(player));
    require(components.size()==8&&components.front()==SceneComponentKind::Transform,"Player component rows were incomplete or acquired entity identities.");
}
void followDependency(const std::filesystem::path& root) {
    SceneDocument document(root);const auto owner=document.createEmpty("Camera owner");
    const auto child=document.createEmpty("Invalid descendant target");document.select({child});
    require(document.reparentSelection(owner),"Could not parent follow fixture.");
    const auto target=document.createEmpty("Valid follow target");
    const auto history=document.history().size();
    require(addSceneComponent(document,owner,SceneComponentKind::CameraFollow),"Follow did not skip an invalid descendant target.");
    const auto record=*document.scene().find(owner);
    require(record.camera&&record.cameraFollow&&record.cameraFollow->target==target,"Follow dependency picked a cyclic target.");
    require(document.scene().properties().activeCamera==owner,"Implicit Camera dependency did not set missing activation.");
    require(document.history().size()==history+1,"Camera/Follow dependencies created multiple commands.");
    require(document.undo()&&!document.scene().find(owner)->camera&&!document.scene().properties().activeCamera,"Undo did not restore missing Camera/Follow dependencies and activation.");
}
void recipeAndVisualChildren(const std::filesystem::path& root) {
    SceneDocument document(root);
    const auto emptyEnemy=document.createEnemy();
    const auto enemy=*document.scene().find(emptyEnemy);
    require(enemy.actor&&enemy.motor&&enemy.controller&&enemy.navigation&&enemy.lightRig&&enemy.darknessReveal,
        "Enemy recipe omitted a shared actor capability.");
    require(!enemy.playerInput&&!enemy.camera&&!enemy.cameraFollow&&document.scene().children(emptyEnemy).empty(),
        "Enemy recipe acquired player input or a camera.");
    require(!document.scene().properties().playerCharacter&&!document.scene().properties().activeCamera,
        "Enemy recipe assigned player or camera roles.");
    const auto player=document.createPlayer();const auto properties=document.scene().properties();
    const auto camera=*document.scene().find(properties.activeCamera);
    require(camera.parent==player&&camera.camera&&camera.cameraFollow&&camera.cameraFollow->target==player,
        "Player recipe failed to supply its real child camera.");
    const auto before=document.history().size();const auto secondEnemy=document.createEnemy("Second enemy");
    require(secondEnemy&&document.history().size()==before+1,"Enemy recipe was not one scene command.");
    require(sameProperties(properties,document.scene().properties()),"Enemy recipe replaced explicit scene roles.");
    require(document.undo()&&!document.scene().contains(secondEnemy)&&document.scene().contains(player),
        "Enemy undo changed an existing player or failed to remove the recipe.");
    require(document.redo()&&document.scene().find(secondEnemy)->navigation,"Enemy redo lost its components.");
    const auto placementsBefore=document.history().size();
    const auto models=document.createModels({{BuiltinBoxModelId,"Enemy visual",math::translation({0,1,0})}},secondEnemy);
    require(models.size()==1&&document.scene().find(models.front())->parent==secondEnemy,
        "Dropping a model on an actor visual root failed to create an ordinary child.");
    require(document.scene().find(models.front())->local==math::translation({0,1,0}),"Model drop did not preserve its relative transform.");
    require(document.history().size()==placementsBefore+1&&document.undo()&&!document.scene().contains(models.front()),
        "Model drop was not one undoable placement.");
    require(document.redo()&&document.scene().find(models.front())->parent==secondEnemy,"Child-model placement did not redo.");
    const auto badBefore=document.history().size();
    require(document.createModels({{BuiltinBoxModelId,"Invalid parent",{}}},999).empty()&&document.history().size()==badBefore,
        "Model placement accepted a missing parent or added a failed history command.");
}
void entityHierarchyProjection(const std::filesystem::path& root) {
    std::vector<EntityRecord> records;
    const auto add=[&](EntityId id,const char* name,EntityId parent,uint32_t order) -> EntityRecord& {
        EntityRecord record;record.id=id;record.name=name;record.parent=parent;record.order=order;
        records.push_back(record);return records.back();
    };
    add(1,"Model",0,0).model=AssetRef<ModelAsset>{BuiltinBoxModelId};
    add(2,"Attached collider",1,0).collider.emplace();
    add(3,"Attached point",1,1).point.emplace();
    add(4,"Explicit group",1,2);
    add(5,"Grouped model",4,0).model=AssetRef<ModelAsset>{BuiltinBoxModelId};
    add(6,"Attached model",5,0).model=AssetRef<ModelAsset>{BuiltinBoxModelId};
    auto& actor=add(7,"Player",0,1);actor.actor.emplace();actor.motor.emplace();actor.controller.emplace();
    actor.navigation.emplace();actor.playerInput.emplace();actor.lightRig.emplace();actor.darknessReveal.emplace();
    auto& camera=add(8,"Owned camera",7,0);camera.camera.emplace();camera.cameraFollow=SceneCameraFollow{7,{5,7,5},{}};
    auto& visual=add(9,"Owned visual",7,1);visual.model=AssetRef<ModelAsset>{BuiltinBoxModelId};
    visual.local=math::translation({.25f,1,-.5f});visual.local.at(0,1)=.3f;visual.local.at(0,0)=-1.4f;
    add(10,"Owned particle",9,0).particle=AssetRef<ParticleEffectAsset>{"assets/particles/folded.crimson-particle"};
    add(11,"Attached light",9,1).point.emplace();
    add(12,"Actor child group",7,2);
    add(13,"Independent grouped visual",12,0).model=AssetRef<ModelAsset>{BuiltinBoxModelId};
    add(14,"Nested actor",9,2).actor.emplace();
    add(15,"Nested actor visual",14,0).model=AssetRef<ModelAsset>{BuiltinBoxModelId};
    SceneDocument document(root);
    const bool prepared=document.edit("Prepare entity hierarchy fixture",[&](auto& entities,auto& properties){entities=records;properties={0,8,7};});
    require(prepared,document.status().c_str());
    const auto before=document.scene().records();const auto roles=document.scene().properties();
    const auto generation=document.scene().generation(),revision=document.scene().revision(),pose=document.scene().poseRevision();
    const std::vector<SceneOutlinerRow> expected{{1,0},{2,1},{3,1},{4,1},{5,4},{6,5},{7,0},{9,7},{10,9},{11,9},{14,9},{15,14},{12,7},{13,12}};
    require(sceneOutlinerRows(document.scene())==expected,"Entity projection changed real model/collider/light hierarchy, hid an attached visual or lost ordering.");
    require(sceneEntityParts(document.scene(),7)==std::vector<EntityId>{7,8,9,10},"Player did not consolidate its camera and visual parts in authored order.");
    require(sceneEntityParts(document.scene(),9)==std::vector<EntityId>{9},"Selecting an attached model redirected its Inspector to the Actor.");
    require(sceneEntityParts(document.scene(),14)==std::vector<EntityId>{14,15},"Nested actor parts leaked into the outer Player.");
    require(sceneEntityParts(document.scene(),1)==std::vector<EntityId>{1},"Ordinary model absorbed an attached entity's properties.");
    require(sceneEntityOwner(document.scene(),8)==7&&sceneEntityOwner(document.scene(),9)==9&&sceneEntityOwner(document.scene(),10)==10&&sceneEntityOwner(document.scene(),15)==15&&
        sceneEntityOwner(document.scene(),11)==11&&sceneEntityOwner(document.scene(),13)==13&&sceneEntityOwner(document.scene(),2)==2,
        "Actor ownership crossed an explicit group, nested actor or swallowed an attached nonvisual entity.");
    require(!sceneEntityOwner(document.scene(),999)&&sceneEntityParts(document.scene(),999).empty(),"Missing entity acquired an authoring owner.");
    std::unordered_set<EntityId> unique;
    for(const auto row:sceneOutlinerRows(document.scene())) {
        require(unique.insert(row.entity).second,"Projected entity appeared more than once.");
        const auto record=*document.scene().find(row.entity);
        auto parent=record.parent;
        while(parent&&sceneEntityOwner(document.scene(),parent)!=parent)parent=document.scene().find(parent)->parent;
        require(row.parent==parent,"A real parent was hidden or replaced beyond the actor-owned parts exception.");
    }
    const auto after=document.scene().records();require(after.size()==before.size(),"Projection removed actual scene records.");
    for(size_t index=0;index<before.size();++index)require(sameEntity(before[index],after[index]),"Projection changed an authored transform or capability.");
    require(sameProperties(roles,document.scene().properties())&&document.scene().generation()==generation&&
        document.scene().revision()==revision&&document.scene().poseRevision()==pose,"Projection invalidated runtime state or scene roles.");
    require(document.save("Entity hierarchy"),"Could not save entity hierarchy fixture.");
    SceneDocument reopened(root);require(reopened.load(document.assetId())&&sceneOutlinerRows(reopened.scene())==expected&&!reopened.dirty(),
        "Save/reopen changed real entity hierarchy or dirtied the loaded document.");
    require(removeSceneComponent(reopened,7,SceneComponentKind::Actor)&&sceneEntityOwner(reopened.scene(),9)==9,
        "Removing visual ownership left hidden parts bound to a missing Actor.");
    require(reopened.undo()&&sceneOutlinerRows(reopened.scene())==expected,"Ownership undo did not restore the single Player entry.");
}
void reparentBoxToPlayer(const std::filesystem::path& root) {
    SceneDocument document(root);const auto player=document.createPlayer();
    require(document.edit("Place Player",[&](auto& records,auto&) {
        for(auto& record:records)if(record.id==player)
            record.local=math::translation({5,0,-3})*math::eulerRotationXYZ({0,.7f,0});
    }),"Could not place the Player for the reparent regression.");
    auto boxWorld=math::translation({2,1,-4})*math::eulerRotationXYZ({.1f,.4f,-.2f})*math::scaling({1.2f,.8f,1.5f});
    boxWorld.at(0,1)+=.17f;
    const auto box=document.createBox();require(box!=NoEntity,"Could not create the Box for the reparent regression.");
    require(document.edit("Place Box",[&](auto& records,auto&) {
        for(auto& record:records)if(record.id==box)record.local=boxWorld;
    }),"Could not place the Box for the reparent regression.");
    const auto collider=document.createCollider("Mesh Collider",box);
    require(collider!=NoEntity,"Could not create the Box collider for the reparent regression.");
    const auto original=*document.scene().find(box),originalCollider=*document.scene().find(collider);
    const auto history=document.history().size();document.select({box},box);
    // This is the same world-preserving operation performed on delivery of an
    // Outliner entity drag. No model replacement or new visibility is involved.
    require(document.reparentSelection(player),"Dragging a Box onto Player was rejected.");
    const auto assertAttached=[&](const Scene& scene) {
        const auto rows=sceneOutlinerRows(scene);
        require(std::find(rows.begin(),rows.end(),SceneOutlinerRow{box,player})!=rows.end()&&
            std::find(rows.begin(),rows.end(),SceneOutlinerRow{collider,box})!=rows.end(),
            "Parenting to Player hid the real Box row or detached its collider row.");
        require(sceneEntityOwner(scene,box)==box&&sceneEntityParts(scene,box)==std::vector<EntityId>{box},
            "Parenting to Player redirected the Box selection or Inspector.");
        require(scene.find(box)->parent==player&&scene.find(collider)->parent==box&&scene.isVisible(player)&&scene.isVisible(box),
            "Parenting changed the Box's geometry identity or effective visibility.");
        auto unchanged=*scene.find(box);unchanged.parent=original.parent;unchanged.order=original.order;unchanged.local=original.local;
        require(sameEntity(unchanged,original),"Parenting changed the Box's material, scope, coverage or visibility settings.");
        const auto extracted=extractScene(scene);
        const auto found=std::find_if(extracted.models.begin(),extracted.models.end(),[&](const auto& model){return model.id==box;});
        require(found!=extracted.models.end()&&found->visible&&found->assetId==BuiltinBoxModelId,
            "Parenting the Box removed it from visible render extraction.");
        require(math::finite(found->world)&&math::handedness(found->world)!=0,
            "Parenting produced a nonfinite or singular viewport transform.");
        require(found->actorOwner==player&&found->lightingCategory==lighting_scope::Character&&
            found->lightingScopes.directReceiverMask==lighting_scope::Character,
            "Attached Player geometry lost its existing Character lighting contract.");
        for(unsigned field=0;field<16;++field)require(std::abs(found->world[field]-boxWorld[field])<1e-5f,
            "Parenting moved or distorted the Box in the viewport.");
        require(sameEntity(*scene.find(collider),originalCollider),"Parenting changed the Box's collider identity or local alignment.");
    };
    assertAttached(document.scene());
    require(document.selection().primary==box&&document.selection().ids==std::vector<EntityId>{box}&&
        document.history().size()==history+1,"Parenting did not retain Box selection as one history command.");
    require(document.undo()&&sameEntity(*document.scene().find(box),original),"Undo did not restore the Box's original placement and parent.");
    require(document.redo(),"Parenting the Box could not redo.");assertAttached(document.scene());
    require(document.edit("Hide Player",[&](auto& records,auto&) {
        for(auto& record:records)if(record.id==player)record.visible=false;
    })&&!document.scene().isVisible(box),"Box visibility did not inherit an explicitly hidden Player.");
    require(document.undo(),"Player visibility could not undo.");assertAttached(document.scene());
    require(document.save("Player box parenting"),"Could not save the attached Box.");
    SceneDocument reopened(root);require(reopened.load(document.assetId())&&!reopened.dirty(),"Could not reopen the attached Box.");
    assertAttached(reopened.scene());
    const auto point=reopened.createPointLight("Attached-light regression");reopened.select({point},point);
    require(reopened.reparentSelection(player)&&sceneEntityOwner(reopened.scene(),point)==point,
        "An attached point light lost its own authoring identity.");
}
void legacyOutlinerMetadata(const std::filesystem::path& root) {
    SceneDocument document(root);const auto player=document.createPlayer();const auto camera=document.scene().properties().activeCamera;
    const auto point=document.createPointLight("Legacy point");require(document.save("Legacy layout source"),"Could not save legacy source.");
    auto json=nlohmann::json::parse(serializeSceneSnapshot(readSceneSnapshot(root,document.assetId())));
    const Vec4 red{.8f,.15f,.25f,1},blue{.1f,.3f,.9f,.75f};
    for(auto& entity:json.at("entities")) {
        if(entity.at("name")=="Player") {
            entity["componentOrder"]={"lightRig","motor","transform"};
            entity["outlinerStyle"]={{"components",{{"lightRig",red},{"motor",blue}}}};
        } else if(entity.at("name")=="Camera")entity["outlinerBefore"]="motor";
        else if(entity.at("name")=="Legacy point")entity["outlinerStyle"]={{"color",red},{"components",{{"point",blue}}}};
    }
    const std::string legacyId="assets/scenes/legacy-flat-metadata.crimson-scene";
    const auto write=[&](const auto& value) {std::ofstream output(root/legacyId,std::ios::binary);output<<value.dump(2);};
    write(json);const auto fingerprint=sceneFileFingerprint(root,legacyId);
    SceneDocument migrated(root);require(migrated.load(legacyId)&&!migrated.dirty(),"Legacy layout conversion dirtied its loaded document.");
    require(sceneFileFingerprint(root,legacyId)==fingerprint,"Loading legacy Outliner metadata rewrote the scene.");
    require(sceneOutlinerColor(*migrated.scene().find(player))==blue,"Legacy component color did not become the entity color in canonical order.");
    require(sceneOutlinerColor(*migrated.scene().find(point))==red,"Legacy component color replaced an explicit entity color.");
    require(migrated.scene().find(camera)->parent==player&&migrated.scene().find(camera)->cameraFollow==document.scene().find(camera)->cameraFollow,
        "Legacy layout conversion changed real Camera ownership or follow settings.");
    require(sceneOutlinerRows(migrated.scene())==std::vector<SceneOutlinerRow>{{player,0},{point,0}},"Legacy metadata recreated component or camera rows.");
    require(migrated.save(),"Could not save converted Outliner metadata.");
    const auto current=nlohmann::json::parse(serializeSceneSnapshot(readSceneSnapshot(root,legacyId)));
    for(const auto& entity:current.at("entities")) {
        require(!entity.contains("componentOrder")&&!entity.contains("outlinerBefore"),"Current scenes wrote retired component-row layout.");
        require(!entity.contains("outlinerStyle")||!entity.at("outlinerStyle").contains("components"),"Current scenes wrote retired component colors.");
    }
    SceneDocument reopened(root);require(reopened.load(legacyId)&&sceneOutlinerColor(*reopened.scene().find(player))==blue&&!reopened.dirty(),
        "Converted entity color did not persist without a legacy layout.");
    const auto rejects=[&](auto malformed,const char* message) {write(malformed);SceneDocument invalid(root);require(!invalid.load(legacyId),message);};
    auto malformed=json;malformed["entities"][0]["componentOrder"]={"motor","motor"};rejects(malformed,"Duplicate legacy component identities were accepted.");
    malformed=json;malformed["entities"][0]["outlinerBefore"]="transform";rejects(malformed,"Invalid legacy child anchor was accepted.");
    malformed=json;malformed["entities"][0]["outlinerStyle"]={{"components",{{"motor",Vec4{0,-.1f,0,1}}}}};rejects(malformed,"Discarded legacy color bypassed finite range validation.");
    malformed=json;malformed["entities"][0]["outlinerStyle"]={{"components",{{"unknown",red}}}};rejects(malformed,"Unknown legacy color identity was accepted.");
    write(current);
}
void outlinerColorHistory(const std::filesystem::path& root) {
    SceneDocument document(root);const auto player=document.createPlayer(),enemy=document.createEnemy();
    document.select({player,enemy},enemy);const auto selection=document.selection();
    const auto generation=document.scene().generation(),revision=document.scene().revision(),pose=document.scene().poseRevision();
    const auto history=document.history().size();const Vec4 red{.8f,.15f,.25f,1},blue{.1f,.3f,.9f,.75f};
    const std::array entities{SceneOutlinerColorTarget{player},SceneOutlinerColorTarget{enemy},SceneOutlinerColorTarget{player}};
    require(setSceneOutlinerColors(document,entities,red)&&document.history().size()==history+1,"Batch entity color was not one scene command.");
    require(sceneOutlinerColor(*document.scene().find(player))==red&&sceneOutlinerColor(*document.scene().find(enemy))==red,"Batch entity color did not affect both rows.");
    require(document.selection().ids==selection.ids&&document.selection().primary==selection.primary,"Setting colors changed multi-selection.");
    require(document.scene().generation()==generation&&document.scene().revision()==revision&&document.scene().poseRevision()==pose,
        "Outliner colors invalidated navigation, simulation or rendering.");
    require(document.undo()&&!document.scene().find(player)->outlinerStyle&&!document.scene().find(enemy)->outlinerStyle&&document.redo(),
        "Batch color history failed to restore both entity rows.");
    const auto before=document.scene().records();const auto count=document.history().size();
    const std::array invalidTargets{SceneOutlinerColorTarget{player},SceneOutlinerColorTarget{999}};
    require(!setSceneOutlinerColors(document,invalidTargets,blue)&&document.history().size()==count&&sceneOutlinerColor(*document.scene().find(player))==red,
        "Invalid batch color partially modified valid entries.");
    require(!setSceneOutlinerColors(document,entities,Vec4{std::numeric_limits<float>::quiet_NaN(),0,0,1})&&
        !setSceneOutlinerColors(document,entities,Vec4{0,0,0,1.1f})&&!setSceneOutlinerColors(document,entities,red)&&document.history().size()==count,
        "Invalid or no-op color created history.");
    require(document.save("Outliner colors"),"Could not persist Outliner colors.");
    SceneDocument reopened(root);require(reopened.load(document.assetId()),"Could not reopen Outliner colors.");
    require(reopened.scene().find(player)->outlinerStyle==document.scene().find(player)->outlinerStyle&&
        reopened.scene().find(enemy)->outlinerStyle==document.scene().find(enemy)->outlinerStyle,"Reopening lost entity row colors.");
    reopened.select({player});require(reopened.duplicateSelection(),"Could not duplicate a colored actor.");
    require(reopened.scene().find(reopened.selection().primary)->outlinerStyle==reopened.scene().find(player)->outlinerStyle,"Duplication lost entity color.");
    require(reopened.undo()&&setSceneOutlinerColors(reopened,entities,std::nullopt)&&!reopened.scene().find(player)->outlinerStyle&&!reopened.scene().find(enemy)->outlinerStyle,
        "Clearing entity colors retained empty style metadata.");
    require(reopened.undo()&&sceneOutlinerColor(*reopened.scene().find(player))==red,"Cleared entity colors did not undo.");
    auto invalid=before;invalid.front().outlinerStyle.emplace();invalid.front().outlinerStyle->color=Vec4{0,-.1f,0,1};
    bool rejected=false;try{Scene::validate(invalid,document.scene().properties());}catch(const std::exception&){rejected=true;}
    require(rejected,"Invalid persisted Outliner color was accepted.");
}
void batchComponentRemoval(const std::filesystem::path& root) {
    SceneDocument document(root);const auto player=document.createPlayer(),enemy=document.createEnemy();
    const auto camera=document.scene().properties().activeCamera;const auto count=document.history().size();
    const auto before=document.scene().records();const auto properties=document.scene().properties();
    const std::array targets{SceneComponentTarget{player,SceneComponentKind::Controller},SceneComponentTarget{player,SceneComponentKind::Motor},
        SceneComponentTarget{enemy,SceneComponentKind::LightRig},SceneComponentTarget{enemy,SceneComponentKind::DarknessReveal}};
    require(removeSceneComponents(document,targets)&&document.history().size()==count+1,"Batch component removal created separate commands.");
    const auto p=*document.scene().find(player),e=*document.scene().find(enemy);
    require(!p.controller&&!p.motor&&!p.navigation&&!p.playerInput&&p.lightRig&&p.actor&&!document.scene().properties().playerCharacter&&
        !e.lightRig&&!e.darknessReveal&&e.controller&&e.motor&&e.navigation&&document.scene().contains(camera),
        "Batch removal retained invalid dependencies or deleted unrelated owner/camera data.");
    require(document.undo()&&sameProperties(properties,document.scene().properties()),"Batch removal did not restore scene roles.");
    const auto restored=document.scene().records();require(restored.size()==before.size(),"Batch removal undo changed entity counts.");
    for(size_t index=0;index<before.size();++index)require(sameEntity(before[index],restored[index]),"Batch removal undo lost authored component values.");
    const std::array invalid{SceneComponentTarget{player,SceneComponentKind::Motor},SceneComponentTarget{enemy,SceneComponentKind::Transform}};
    require(!removeSceneComponents(document,invalid)&&document.scene().find(player)->motor,"Batch removal accepted an entity-only entry or partially removed components.");
}
void savedSceneCompatibility(const std::filesystem::path& root) {
    std::vector<std::string> diagnostics;const auto scenes=scanSceneAssets(root,&diagnostics);
    if(!diagnostics.empty())throw std::runtime_error(diagnostics.front());
    for(const auto& asset:scenes) {
        const auto before=sceneFileFingerprint(root,asset.id);
        const auto snapshot=readSceneSnapshot(root,asset.id);
        Scene scene;scene.apply(snapshot.records,snapshot.properties);
        const auto current=serializeSceneSnapshot(snapshot);
        require(!current.empty(),"Migrated saved scene did not serialize.");
        require(scene.records().size()==snapshot.records.size(),"Migration changed saved scene entity counts.");
        require(before==sceneFileFingerprint(root,asset.id),"Reading and dry serialization rewrote an authored scene.");
    }
    std::cout<<"Read and dry-serialized "<<scenes.size()<<" existing project scenes without modifying their files.\n";
}
}
int main(int argc,char** argv) {
    try {
        if(argc==3&&std::string_view(argv[1])=="--project-root") {savedSceneCompatibility(std::filesystem::path(argv[2]));return 0;}
        const auto root=std::filesystem::temp_directory_path()/("crimson-component-tests-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        componentTransactions(root);playerRemovalAndCamera(root);followDependency(root);recipeAndVisualChildren(root);entityHierarchyProjection(root);reparentBoxToPlayer(root);legacyOutlinerMetadata(root);outlinerColorHistory(root);batchComponentRemoval(root);
        std::cout<<"Composed actor recipes, real entity hierarchy, legacy metadata conversion and isolated history passed.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
