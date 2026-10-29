#include "integration/LevelSession.h"
#include "integration/native_scene_projection.hpp"
#include "scene/scene_document.hpp"
#include "scene/light_properties.hpp"
#include "assets/AssetLibrary.h"
#include "assets/ContentStorage.h"
#include "assets/MaterialAsset.h"
#include "runtime/PlaySession.h"
#include "resources/AssetId.h"
#include "math/Json.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace crimson;
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
struct Project {
    std::filesystem::path root=std::filesystem::temp_directory_path()/("crimson-native-projection-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Project(){std::filesystem::create_directories(root);}
    ~Project(){std::error_code error;const auto target=std::filesystem::weakly_canonical(root,error),temporary=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),error);if(!error&&target.parent_path()==temporary&&target.filename().string().starts_with("crimson-native-projection-"))std::filesystem::remove_all(target,error);}
};
void oneNativeWorldAndStableReferences(){
    Project project;integration::LevelSession level(project.root);quader::SceneDocument native;integration::NativeSceneProjection codec;
    const auto actor=native.create_entity("Player"),camera=native.create_entity("Eye");require(native.set_parent(camera,actor,false),"Cannot author native hierarchy.");
    EntityRecord player;player.id=1;player.name="Player";player.actor.emplace();player.controller.emplace();player.movement.emplace();player.playerInput.emplace();
    EntityRecord eye;eye.id=2;eye.name="Eye";eye.camera.emplace();eye.cameraFollow=SceneCameraFollow{1,{0,1.65f,0},Mat4{}};
    const auto actor_uuid=native.uuid(actor),camera_uuid=native.uuid(camera);const auto resolver=[&](EntityId id){return id==1?actor_uuid:id==2?camera_uuid:std::string{};};
    require(native.set_engine_components(actor,codec.engine_components(player,resolver))&&native.set_engine_components(camera,codec.engine_components(eye,resolver)),"Cannot author native typed components.");
    SceneProperties properties;properties.playerCharacter=1;properties.activeCamera=2;require(native.set_engine_settings(codec.engine_settings(properties,resolver)),"Cannot author native selectors.");
    quader::TransformComponent placement;placement.parenting_offset.values=math::translation({3,2,-4}).values;placement.parenting_offset.values[4]=.2f;require(native.set_transform(actor,placement),"Cannot author affine native placement.");
    require(level.publishNativeScene(native,"scene-a",1),level.status());const auto projected_actor=level.nativeProjection().entity_id(actor_uuid),projected_camera=level.nativeProjection().entity_id(camera_uuid);
    require(level.document().transient()&&!level.document().dirty()&&level.document().history().size()==0,"Native projection became an independent authored document/history.");
    require(level.document().scene().records().size()==native.entities().size()&&level.document().scene().find(projected_camera)->parent==projected_actor,"Projection lost native entity count or hierarchy.");
    require(level.document().scene().find(projected_actor)->local.values==native.local_transform_matrix(actor).values&&level.document().scene().find(projected_camera)->cameraFollow->target==projected_actor,"Native affine basis or UUID link was lost.");
    require(level.document().scene().properties().activeCamera==projected_camera&&level.document().scene().properties().playerCharacter==projected_actor,"Native scene selectors did not resolve to the same projected entities.");
    require(!std::filesystem::exists(project.root/".crimson/level-bindings.json")&&!std::filesystem::exists(project.root/".crimson/editor-session.json"),"Projection wrote independent level/session state.");
    require(native.destroy_entities(std::array{actor,camera}),"Cannot delete native fixture entities.");require(level.publishNativeScene(native,"scene-a",2),level.status());require(level.document().scene().records().empty()&&!level.document().scene().properties().activeCamera&&!level.document().scene().properties().playerCharacter,"Deleted native selector UUIDs resolved to stale retained IDs.");
    const auto restored_actor=native.create_entity("Player"),restored_camera=native.create_entity("Eye");require(native.set_entity_uuid(restored_actor,actor_uuid)&&native.set_entity_uuid(restored_camera,camera_uuid),"Cannot restore exact UUIDs.");require(native.set_engine_components(restored_actor,codec.engine_components(player,resolver))&&native.set_engine_components(restored_camera,codec.engine_components(eye,resolver))&&native.set_parent(restored_camera,restored_actor,false),"Cannot restore native component links.");
    require(level.publishNativeScene(native,"scene-a",3),level.status());require(level.nativeProjection().entity_id(actor_uuid)==projected_actor&&level.nativeProjection().entity_id(camera_uuid)==projected_camera&&level.document().scene().find(projected_camera)->cameraFollow->target==projected_actor,"Native undo-style UUID restoration changed identity or lost links.");
}
void unresolvedLinksAndNativeGeometryStayOpaque(){
    Project project;integration::LevelSession level(project.root);quader::SceneDocument native;integration::NativeSceneProjection codec;
    const auto target=native.create_entity("Target"),camera=native.create_entity("Camera"),mesh=native.create_entity("Native mesh");const auto target_uuid=native.uuid(target),camera_uuid=native.uuid(camera);
    EntityRecord eye;eye.id=1;eye.camera.emplace();eye.cameraFollow=SceneCameraFollow{2,{1,2,3},Mat4{}};const auto resolver=[&](EntityId id){return id==2?target_uuid:std::string{};};require(native.set_engine_components(camera,codec.engine_components(eye,resolver)),"Cannot seed a native follow reference.");
    require(native.set_mesh(mesh,quader::MeshComponent{42}),"Cannot mark native geometry.");const auto material=level.assets().createMaterial("Assigned native surface");require(bool(material),"Cannot create typed material fixture.");EntityRecord appearance;appearance.nativeGeometry=true;appearance.modelMaterials.emplace_back(material->id);const auto envelope=codec.engine_components(appearance);
    require(envelope.asset_references_complete&&std::find(envelope.asset_references.begin(),envelope.asset_references.end(),material->id)!=envelope.asset_references.end(),"Typed native material reference was not inventoried.");const auto json=nlohmann::json::parse(envelope.text());require(!json.contains("__nativeGeometry")&&!json.contains("local")&&!json.contains("id")&&!json.contains("model"),"Envelope duplicated native geometry/transform/identity truth.");require(native.set_engine_components(mesh,envelope),"Cannot author native mesh appearance.");
    require(level.publishNativeScene(native,"scene-b",1),level.status());const auto mesh_id=level.nativeProjection().entity_id(native.uuid(mesh));require(level.document().scene().find(mesh_id)->nativeGeometry&&!level.document().scene().find(mesh_id)->model&&level.document().scene().find(mesh_id)->modelMaterials.front().id==material->id,"Transient native geometry appearance required a fake model asset.");
    require(native.destroy_entity(target)&&level.publishNativeScene(native,"scene-b",2),"Missing native reference blocked the projection.");auto projected=*level.document().scene().find(level.nativeProjection().entity_id(camera_uuid));require(!projected.cameraFollow,"Missing target produced a dangling transient follow.");projected.camera->verticalFov=70;const auto changed=level.nativeProjection().engine_components(projected);require(std::any_of(changed.entity_references.begin(),changed.entity_references.end(),[&](const auto& ref){return ref.slot=="cameraFollow.target"&&ref.entity_uuid==target_uuid;}),"An unrelated edit discarded an unresolved authored UUID reference.");
}
}
int main(){try{oneNativeWorldAndStableReferences();unresolvedLinksAndNativeGeometryStayOpaque();std::cout<<"Native-only scene projection, stable UUID links, missing-reference retention and native material ownership passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
