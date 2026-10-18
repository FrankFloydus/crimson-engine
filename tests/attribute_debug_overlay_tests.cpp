#include "tools/GameView.h"
#include "scene/BuiltinModels.h"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool near(const Vec3& first,const Vec3& second){return math::length(first-second)<1e-4f;}
const auto Profile=canonicalAssetId("test:attribute-debug/profile");
NavigationAgentProfileQuery profiles() {
    const auto profile=std::make_shared<const NavigationAgentProfile>(NavigationAgentProfile{Profile,"Debug fixture",{2.5f,.4f,45,.3f}});
    return [profile](const std::string& id){return id==Profile?profile:nullptr;};
}
EntityRecord actor() {
    EntityRecord result;result.id=2;result.name="Observed actor";result.actor.emplace();result.controller.emplace();
    result.controller->profile=AssetRef<NavigationAgentProfile>{Profile};
    result.local=math::translation({5,1,-3})*math::eulerRotationXYZ({0,.7f,0});return result;
}
EntityRecord entity(EntityId id,EntityId parent,Vec3 position) {
    EntityRecord result;result.id=id;result.name="Child";result.parent=parent;result.local=math::translation(position);return result;
}
void uprightCapsuleUsesAcceptedDimensions() {
    Scene scene;scene.apply({actor()},{});const auto before=scene.records();
    const auto result=attributeDebugActorBounds(scene,2,profiles());
    require(result.valid && near(result.minimum,{4.6f,1,-3.4f}) && near(result.maximum,{5.4f,3.5f,-2.6f}),
        "Debug capsule bounds used authored yaw or ignored accepted profile dimensions.");
    const auto after=scene.records();require(before.size()==after.size() && sameEntity(before.front(),after.front()),
        "Computing debug bounds mutated the scene.");
    const std::array updates{ScenePoseUpdate{2,math::translation({8,1,-1})*math::eulerRotationXYZ({0,1.2f,0})}};
    scene.applyRuntimePoses(updates);const auto presented=attributeDebugActorBounds(scene,2,profiles());
    require(near(presented.minimum,result.minimum+Vec3{3,0,2}) && near(presented.maximum,result.maximum+Vec3{3,0,2}),
        "Debug bounds retained an old pose instead of using the presentation scene.");
}
void ownedVisualsExcludeHelpersAndNestedActors() {
    auto root=actor();auto group=entity(3,2,{2,1,0});group.local=group.local*math::scaling({2,1,.5f});
    auto model=entity(4,3,{0,.5f,0});model.model=AssetRef<ModelAsset>{BuiltinBoxModelId};
    auto camera=entity(5,2,{100,100,100});camera.camera.emplace();
    auto light=entity(6,2,{-100,100,100});light.point.emplace();light.point->radius=500;
    auto nested=entity(7,2,{1000,0,0});nested.actor.emplace();
    auto nestedModel=entity(8,7,{});nestedModel.model=AssetRef<ModelAsset>{BuiltinBoxModelId};
    auto hidden=entity(9,2,{-1000,0,0});hidden.model=AssetRef<ModelAsset>{BuiltinBoxModelId};hidden.visible=false;
    auto collider=entity(10,2,{});collider.collider=SceneCollider{{100,100,100}};
    Scene scene;scene.apply({root,group,model,camera,light,nested,nestedModel,hidden,collider},{});
    const Aabb local{{-.5f,0,-.5f},{.5f,2,.5f},true};unsigned queries{};
    const auto result=attributeDebugActorBounds(scene,2,profiles(),[&](const std::string& id)->std::optional<Aabb>{
        require(id==BuiltinBoxModelId,"The debug overlay queried an unrelated resource.");++queries;return local;
    });
    auto expected=attributeDebugActorBounds(scene,2,profiles());
    math::include(expected,math::transformBounds(local,*scene.worldTransform(4)));
    require(queries==1 && result.valid && near(result.minimum,expected.minimum) && near(result.maximum,expected.maximum),
        "Owned visual bounds omitted composed transforms or included hidden/nested actors and helper extents.");
}
void unavailableAndHiddenTargetsDoNotInventBounds() {
    auto root=actor();Scene scene;scene.apply({root},{});
    require(!attributeDebugActorBounds(scene,NoEntity,profiles()).valid && !attributeDebugActorBounds(scene,55,profiles()).valid,
        "A missing debug target invented a selection marker.");
    require(!attributeDebugActorBounds(scene,2).valid,"A missing custom profile silently used default capsule dimensions.");
    root.visible=false;scene.apply({root},{});require(!attributeDebugActorBounds(scene,2,profiles()).valid,
        "A hidden target retained a diagnostic model/capsule box.");
}
void attributeOnlyActorGetsCompactMarker() {
    auto root=actor();root.controller.reset();
    Scene scene;scene.apply({root},{});const auto bounds=attributeDebugActorBounds(scene,2);
    require(bounds.valid && near(bounds.minimum,{4.75f,.75f,-3.25f}) && near(bounds.maximum,{5.25f,1.25f,-2.75f}),
        "An attribute-only actor did not get a compact world-axis target marker.");
    root.visible=false;scene.apply({root},{});require(!attributeDebugActorBounds(scene,2).valid,
        "A hidden attribute-only actor retained its fallback marker.");
}
}
int main(){
    try{uprightCapsuleUsesAcceptedDimensions();ownedVisualsExcludeHelpersAndNestedActors();unavailableAndHiddenTargetsDoNotInventBounds();attributeOnlyActorGetsCompactMarker();
        std::cout<<"Attribute debug actor bounds checks passed.\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
