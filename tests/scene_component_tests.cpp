#include "scene/SceneComponents.h"
#include "scene/SceneOutlinerRows.h"
#include "scene/BuiltinModels.h"
#include <algorithm>
#include <nlohmann/json.hpp>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string_view>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool reorderComponent(SceneDocument& document,EntityId owner,SceneComponentKind moving,
                      std::optional<SceneComponentKind> before=std::nullopt) {
    return reorderSceneOutlinerRow(document,owner,SceneOutlinerRow::componentRow(moving),
        before?std::optional{SceneOutlinerRow::componentRow(*before)}:std::nullopt);
}
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
void componentOrdering(const std::filesystem::path& root) {
    SceneDocument document(root);const auto player=document.createPlayer();
    require(document.save("Component order"),"Could not save component-order fixture.");
    const auto original=sceneComponents(*document.scene().find(player));
    const auto generation=document.scene().generation(),revision=document.scene().revision(),pose=document.scene().poseRevision();
    const auto history=document.history().size();
    require(reorderComponent(document,player,SceneComponentKind::LightRig,SceneComponentKind::Motor),"Could not reorder a component.");
    const auto ordered=sceneComponents(*document.scene().find(player));
    require(ordered!=original&&ordered.front()==SceneComponentKind::Transform&&ordered[2]==SceneComponentKind::LightRig,
        "Component order did not move only the selected component before its target.");
    require(document.history().size()==history+1&&document.dirty(),"Component reorder did not create one dirty history command.");
    require(document.scene().generation()==generation&&document.scene().revision()==revision&&document.scene().poseRevision()==pose,
        "Presentation-only reordering invalidated runtime navigation, particles or render state.");
    require(document.undo()&&sceneComponents(*document.scene().find(player))==original&&!document.dirty(),"Component order did not undo to the saved state.");
    require(document.redo()&&sceneComponents(*document.scene().find(player))==ordered,"Component order did not redo.");
    require(document.scene().generation()==generation&&document.scene().revision()==revision,"Order undo/redo rebuilt the runtime scene.");
    require(!reorderComponent(document,player,SceneComponentKind::Transform,SceneComponentKind::Motor)&&
            !reorderComponent(document,player,SceneComponentKind::Motor,SceneComponentKind::Transform)&&
            !reorderComponent(document,player,SceneComponentKind::Camera)&&
            !reorderComponent(document,player,SceneComponentKind::Motor,SceneComponentKind::Camera),
        "Reordering accepted Transform movement or absent components.");
    require(reorderComponent(document,player,SceneComponentKind::Actor)&&
            sceneComponents(*document.scene().find(player)).back()==SceneComponentKind::Actor,
        "Component could not move to the end.");
    require(document.undo()&&sceneComponents(*document.scene().find(player))==ordered,"Move-to-end order did not undo.");
    require(document.save(),"Could not persist component order.");
    const auto snapshot=readSceneSnapshot(root,document.assetId());
    const auto authored=std::find_if(snapshot.records.begin(),snapshot.records.end(),[&](const auto& record){return record.id==player;});
    require(authored!=snapshot.records.end()&&sceneComponents(*authored)==ordered,"Native scene persistence lost the authored component order.");
    const auto json=nlohmann::json::parse(serializeSceneSnapshot(snapshot));
    require(json.at("version")==17,"Component layout did not persist with the current camera-ownership scene schema.");
    const auto entity=std::find_if(json.at("entities").begin(),json.at("entities").end(),[&](const auto& record){return record.at("name")=="Player";});
    require(entity!=json.at("entities").end()&&entity->at("componentOrder").at(2)=="lightRig",
        "Component layout persisted unstable labels or numeric identities.");
    SceneDocument reopened(root);require(reopened.load(document.assetId()),"Could not reopen ordered components.");
    require(sceneComponents(*reopened.scene().find(player))==ordered,"Reopening lost the component order.");
    require(removeSceneComponent(reopened,player,SceneComponentKind::LightRig),"Could not remove a reordered component.");
    const auto remaining=sceneComponents(*reopened.scene().find(player));
    require(std::find(remaining.begin(),remaining.end(),SceneComponentKind::LightRig)==remaining.end(),"Removed component retained a stale Outliner row.");
    require(reopened.undo()&&sceneComponents(*reopened.scene().find(player))==ordered,"Removal undo lost reordered component placement.");
    auto invalid=reopened.scene().records();invalid.front().componentOrder={static_cast<SceneComponentKind>(255)};
    bool rejected=false;try{Scene::validate(invalid,reopened.scene().properties());}catch(const std::exception&){rejected=true;}
    require(rejected,"Invalid component layout kind was accepted.");
    invalid.front().componentOrder={SceneComponentKind::Motor,SceneComponentKind::Motor};rejected=false;
    try{Scene::validate(invalid,reopened.scene().properties());}catch(const std::exception&){rejected=true;}
    require(rejected,"Duplicate component layout kind was accepted.");
}
void mixedOutlinerOrdering(const std::filesystem::path& root) {
    SceneDocument document(root);const auto player=document.createPlayer();
    const auto camera=document.scene().properties().activeCamera;
    const auto visual=document.createModels({{BuiltinBoxModelId,"Visual",{}}},player).front();
    const auto secondCamera=document.createCamera("Second Camera");document.select({secondCamera});
    require(document.reparentSelection(player),"Could not prepare a second real camera child.");
    require(document.save("Mixed Outliner rows"),"Could not save mixed-row fixture.");
    const auto before=document.scene().records();const auto properties=document.scene().properties();
    const auto generation=document.scene().generation(),revision=document.scene().revision(),pose=document.scene().poseRevision();
    const auto cameraRow=SceneOutlinerRow::childRow(camera),secondRow=SceneOutlinerRow::childRow(secondCamera);
    const auto motorRow=SceneOutlinerRow::componentRow(SceneComponentKind::Motor);
    const auto revealRow=SceneOutlinerRow::componentRow(SceneComponentKind::DarknessReveal);
    const auto original=sceneOutlinerRows(document.scene(),player);const auto history=document.history().size();
    const auto adjacent=[](const auto& rows,SceneOutlinerRow first,SceneOutlinerRow second) {
        const auto at=std::find(rows.begin(),rows.end(),first);
        return at!=rows.end()&&at+1!=rows.end()&&*(at+1)==second;
    };
    const auto unchangedAuthored=[&] {
        require(sameProperties(properties,document.scene().properties()),"Outliner ordering changed active camera or player roles.");
        const auto current=document.scene().records();require(current.size()==before.size(),"Outliner ordering created or removed entities.");
        for(size_t index=0;index<before.size();++index) {
            auto adjusted=current[index];adjusted.order=before[index].order;adjusted.componentOrder=before[index].componentOrder;
            adjusted.outlinerBefore=before[index].outlinerBefore;
            require(sameEntity(before[index],adjusted),"Outliner ordering changed hierarchy, transform, lens, follow or another authored value.");
        }
        require(document.scene().generation()==generation&&document.scene().revision()==revision&&document.scene().poseRevision()==pose,
            "Mixed-row ordering rebuilt runtime navigation, render state or particle playback.");
    };
    require(reorderSceneOutlinerRow(document,player,cameraRow,motorRow),"Actual child camera could not move above Motor.");
    auto rows=sceneOutlinerRows(document.scene(),player);
    require(adjacent(rows,cameraRow,motorRow)&&
        document.scene().find(camera)->outlinerBefore==SceneComponentKind::Motor,"Camera drag did not persist a real child anchor before Motor.");
    require(document.history().size()==history+1,"Mixed camera drag created more than one history command.");unchangedAuthored();
    const auto anchored=rows;
    require(document.undo()&&sceneOutlinerRows(document.scene(),player)==original&&!document.dirty(),"Mixed camera drag did not undo to its saved layout.");
    unchangedAuthored();require(document.redo()&&sceneOutlinerRows(document.scene(),player)==anchored,"Mixed camera drag did not redo.");unchangedAuthored();
    require(reorderSceneOutlinerRow(document,player,revealRow,cameraRow),"Component could not move before a real child camera.");
    rows=sceneOutlinerRows(document.scene(),player);
    require(adjacent(rows,revealRow,cameraRow),"Component-before-child order was not reproduced.");
    unchangedAuthored();
    require(reorderSceneOutlinerRow(document,player,secondRow,cameraRow),"Camera child could not move before another actual child.");
    rows=sceneOutlinerRows(document.scene(),player);
    require(adjacent(rows,secondRow,cameraRow)&&
        document.scene().find(secondCamera)->outlinerBefore==SceneComponentKind::Motor,"Sibling camera order was not preserved within its component anchor.");
    unchangedAuthored();
    require(reorderSceneOutlinerRow(document,player,cameraRow)&&sceneOutlinerRows(document.scene(),player).back()==cameraRow&&
        !document.scene().find(camera)->outlinerBefore,"Actual camera could not return to the end of the component rows.");unchangedAuthored();
    require(!reorderSceneOutlinerRow(document,player,cameraRow,SceneOutlinerRow::componentRow(SceneComponentKind::Transform))&&
        !reorderSceneOutlinerRow(document,visual,cameraRow)&&!reorderSceneOutlinerRow(document,player,cameraRow,cameraRow),
        "Mixed ordering accepted Transform targets, foreign-owner children or self drops.");
    const auto ordered=sceneOutlinerRows(document.scene(),player);
    std::vector<EntityRecord> childCopies;for(const auto id:document.scene().children(player))childCopies.push_back(*document.scene().find(id));
    std::vector<const EntityRecord*> pointers;for(const auto& child:childCopies)pointers.push_back(&child);
    require(sceneOutlinerRows(*document.scene().find(player),pointers)==ordered,"Indexed snapshot rows differed from scene-based mixed ordering.");
    require(document.save(),"Could not save mixed camera/component row order.");
    const auto saved=readSceneSnapshot(root,document.assetId());const auto json=nlohmann::json::parse(serializeSceneSnapshot(saved));
    require(json.at("version")==17,"Optional mixed-row anchors unexpectedly changed the scene schema.");
    const auto serializedCamera=std::find_if(json.at("entities").begin(),json.at("entities").end(),[](const auto& entity){return entity.at("name")=="Second Camera";});
    require(serializedCamera!=json.at("entities").end()&&serializedCamera->at("outlinerBefore")=="motor", "Child anchor did not serialize as a stable component token.");
    SceneDocument reopened(root);require(reopened.load(document.assetId())&&sceneOutlinerRows(reopened.scene(),player)==ordered,
        "Reopening lost the mixed camera/component order.");
    require(reopened.scene().find(camera)->camera==document.scene().find(camera)->camera&&
        reopened.scene().find(camera)->cameraFollow==document.scene().find(camera)->cameraFollow&&
        reopened.scene().find(camera)->parent==player&&sameProperties(properties,reopened.scene().properties()),
        "Mixed-row persistence changed camera lens, ownership, follow or roles.");
    reopened.select({secondCamera});require(reopened.duplicateSelection(),"Could not duplicate an anchored child.");
    require(reopened.scene().find(reopened.selection().primary)->outlinerBefore==SceneComponentKind::Motor,
        "Child duplication discarded its component anchor.");
    require(reopened.undo()&&sceneOutlinerRows(reopened.scene(),player)==ordered,"Duplicate undo changed the original mixed order.");
    require(removeSceneComponent(reopened,player,SceneComponentKind::Motor),"Could not remove a child anchor's component.");
    rows=sceneOutlinerRows(reopened.scene(),player);
    const auto firstChild=std::find_if(rows.begin(),rows.end(),[](const auto& row){return row.kind==SceneOutlinerRowKind::Child;});
    require(firstChild!=rows.end()&&std::all_of(firstChild,rows.end(),[](const auto& row){return row.kind==SceneOutlinerRowKind::Child;}),
        "Child whose component anchor was removed did not fall back to the trailing children.");
    require(reopened.undo()&&sceneOutlinerRows(reopened.scene(),player)==ordered,"Restoring a removed component did not restore its anchored child rows.");
    const auto enemy=reopened.createEnemy();reopened.select({secondCamera});
    const auto world=*reopened.scene().worldTransform(secondCamera);
    require(reopened.reparentSelection(enemy)&&!reopened.scene().find(secondCamera)->outlinerBefore&&
        reopened.scene().worldTransform(secondCamera)==world,"Reparenting retained the old owner's row anchor or moved the camera.");
    require(reopened.undo()&&reopened.scene().find(secondCamera)->outlinerBefore==SceneComponentKind::Motor,
        "Reparent undo failed to restore the previous row anchor.");
    auto invalid=reopened.scene().records();invalid.front().outlinerBefore=SceneComponentKind::Transform;
    bool rejected=false;try{Scene::validate(invalid,reopened.scene().properties());}catch(const std::exception&){rejected=true;}
    require(rejected,"Transform was accepted as a child display anchor.");
    invalid.front().outlinerBefore=static_cast<SceneComponentKind>(255);rejected=false;
    try{Scene::validate(invalid,reopened.scene().properties());}catch(const std::exception&){rejected=true;}
    require(rejected,"Unknown child display anchor was accepted.");
}
void outlinerColorHistory(const std::filesystem::path& root) {
    SceneDocument document(root);const auto player=document.createPlayer(),enemy=document.createEnemy();
    document.select({player,enemy},enemy);const auto selection=document.selection();
    const auto generation=document.scene().generation(),revision=document.scene().revision(),pose=document.scene().poseRevision();
    const auto history=document.history().size();const Vec4 red{.8f,.15f,.25f,1},blue{.1f,.3f,.9f,.75f};
    const std::array entities{SceneOutlinerColorTarget{player,std::nullopt},SceneOutlinerColorTarget{enemy,std::nullopt}};
    require(setSceneOutlinerColors(document,entities,red)&&document.history().size()==history+1,"Batch entity color was not one scene command.");
    require(sceneOutlinerColor(*document.scene().find(player))==red&&sceneOutlinerColor(*document.scene().find(enemy))==red&&
        !sceneOutlinerColor(*document.scene().find(player),SceneComponentKind::Motor),"Entity color leaked into independent component row colors.");
    require(document.selection().ids==selection.ids&&document.selection().primary==selection.primary,"Setting colors changed multi-selection.");
    require(document.scene().generation()==generation&&document.scene().revision()==revision&&document.scene().poseRevision()==pose,
        "Outliner colors invalidated navigation, simulation or rendering.");
    require(document.undo()&&!document.scene().find(player)->outlinerStyle&&!document.scene().find(enemy)->outlinerStyle&&document.redo(),
        "Batch color history failed to restore both entity rows.");
    const std::array components{SceneOutlinerColorTarget{player,SceneComponentKind::Motor},SceneOutlinerColorTarget{enemy,SceneComponentKind::Motor},
        SceneOutlinerColorTarget{player,SceneComponentKind::Motor}};
    require(setSceneOutlinerColors(document,components,blue)&&sceneOutlinerColor(*document.scene().find(player),SceneComponentKind::Motor)==blue&&
        sceneOutlinerColor(*document.scene().find(enemy),SceneComponentKind::Motor)==blue&&sceneOutlinerColor(*document.scene().find(player))==red,
        "Component multi-color failed to preserve independent owner colors.");
    const auto before=document.scene().records();const auto count=document.history().size();
    const std::array invalidTargets{SceneOutlinerColorTarget{player,SceneComponentKind::Motor},SceneOutlinerColorTarget{enemy,SceneComponentKind::Camera}};
    require(!setSceneOutlinerColors(document,invalidTargets,red)&&document.history().size()==count&&
        sceneOutlinerColor(*document.scene().find(player),SceneComponentKind::Motor)==blue,"Invalid batch color partially modified valid entries.");
    require(!setSceneOutlinerColors(document,entities,Vec4{std::numeric_limits<float>::quiet_NaN(),0,0,1})&&
        !setSceneOutlinerColors(document,entities,Vec4{0,0,0,1.1f})&&!setSceneOutlinerColors(document,entities,red)&&document.history().size()==count,
        "Invalid or no-op color created history.");
    require(document.save("Outliner colors"),"Could not persist Outliner colors.");
    const auto snapshot=readSceneSnapshot(root,document.assetId());const auto json=nlohmann::json::parse(serializeSceneSnapshot(snapshot));
    require(json.at("version")==17,"Optional Outliner colors changed the scene schema.");
    bool token=false;for(const auto& entity:json.at("entities"))if(entity.at("name")=="Player")
        token=entity.at("outlinerStyle").at("components").contains("motor");
    require(token,"Component colors did not persist stable component tokens.");
    SceneDocument reopened(root);require(reopened.load(document.assetId()),"Could not reopen Outliner colors.");
    require(reopened.scene().find(player)->outlinerStyle==document.scene().find(player)->outlinerStyle&&
        reopened.scene().find(enemy)->outlinerStyle==document.scene().find(enemy)->outlinerStyle,"Reopening lost row colors.");
    reopened.select({player});require(reopened.duplicateSelection(),"Could not duplicate a colored actor.");
    require(reopened.scene().find(reopened.selection().primary)->outlinerStyle==reopened.scene().find(player)->outlinerStyle,
        "Duplication lost entity/component colors.");
    require(reopened.undo()&&setSceneOutlinerColors(reopened,entities,std::nullopt)&&
        reopened.scene().find(player)->outlinerStyle&&sceneOutlinerColor(*reopened.scene().find(player),SceneComponentKind::Motor)==blue,
        "Clearing an entity color also cleared its component overrides.");
    require(setSceneOutlinerColors(reopened,components,std::nullopt)&&!reopened.scene().find(player)->outlinerStyle&&!reopened.scene().find(enemy)->outlinerStyle,
        "Clearing the final row overrides retained empty style metadata.");
    require(reopened.undo()&&sceneOutlinerColor(*reopened.scene().find(player),SceneComponentKind::Motor)==blue,"Cleared component colors did not undo.");
    auto invalid=before;invalid.front().outlinerStyle.emplace();invalid.front().outlinerStyle->color=Vec4{0,-.1f,0,1};
    bool rejected=false;try{Scene::validate(invalid,document.scene().properties());}catch(const std::exception&){rejected=true;}
    require(rejected,"Invalid persisted Outliner color was accepted.");
}
void batchComponentRemoval(const std::filesystem::path& root) {
    SceneDocument document(root);const auto player=document.createPlayer(),enemy=document.createEnemy();
    const auto camera=document.scene().properties().activeCamera;const auto count=document.history().size();
    const auto before=document.scene().records();const auto properties=document.scene().properties();
    const std::array targets{SceneOutlinerColorTarget{player,SceneComponentKind::Controller},SceneOutlinerColorTarget{player,SceneComponentKind::Motor},
        SceneOutlinerColorTarget{enemy,SceneComponentKind::LightRig},SceneOutlinerColorTarget{enemy,SceneComponentKind::DarknessReveal}};
    require(removeSceneComponents(document,targets)&&document.history().size()==count+1,"Batch component removal created separate commands.");
    const auto p=*document.scene().find(player),e=*document.scene().find(enemy);
    require(!p.controller&&!p.motor&&!p.navigation&&!p.playerInput&&p.lightRig&&p.actor&&!document.scene().properties().playerCharacter&&
        !e.lightRig&&!e.darknessReveal&&e.controller&&e.motor&&e.navigation&&document.scene().contains(camera),
        "Batch removal retained invalid dependencies or deleted unrelated owner/camera data.");
    require(document.undo()&&sameProperties(properties,document.scene().properties()),"Batch removal did not restore scene roles.");
    const auto restored=document.scene().records();require(restored.size()==before.size(),"Batch removal undo changed entity counts.");
    for(size_t index=0;index<before.size();++index)require(sameEntity(before[index],restored[index]),"Batch removal undo lost authored component values.");
    const std::array invalid{SceneOutlinerColorTarget{player,SceneComponentKind::Motor},SceneOutlinerColorTarget{enemy,std::nullopt}};
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
        componentTransactions(root);playerRemovalAndCamera(root);followDependency(root);recipeAndVisualChildren(root);componentOrdering(root);mixedOutlinerOrdering(root);outlinerColorHistory(root);batchComponentRemoval(root);
        std::cout<<"Composed actor recipes, child visuals, component ordering/persistence and isolated history passed.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
