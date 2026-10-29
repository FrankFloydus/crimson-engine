#include "navigation/NavigationWorld.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using namespace crimson;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
NavigationAgentProfile profile(std::string id="test-agent"){return {std::move(id),"Test",{}};}
PhysicsBox box(EntityId id,Vec3 position,Vec3 size){return {id,math::translation(position),size};}
NavigationVolumeDescriptor volume(Vec3 size={48,12,24}){NavigationVolumeDescriptor result;result.entity=99;result.size=size;return result;}
void settle(ResourceManager& manager,NavigationWorld& world,const std::string& id) {
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    do{manager.pump();world.pumpPublication();if(world.readiness(id)!=NavigationStatus::Preparing) {
        const auto state=world.debugSnapshot(id);if(state.status!=NavigationStatus::Ready)throw std::runtime_error(state.diagnostic);return;
    }std::this_thread::sleep_for(std::chrono::milliseconds(1));}while(std::chrono::steady_clock::now()<end);
    throw std::runtime_error("Navigation worker did not settle.");
}
void seamAndActorFreeProfiles() {
    ResourceManager manager;NavigationWorld world(manager);const std::vector profiles{profile(),profile("enemy")};
    const std::vector boxes{box(1,{0,-.5f,0},{46,1,22})};const auto bounds=volume();
    world.reconcile(1,bounds,profiles,boxes,{});require(world.readiness("test-agent")==NavigationStatus::Preparing,"Actor-free profile did not schedule asynchronous tiles.");
    settle(manager,world,"test-agent");settle(manager,world,"enemy");
    auto route=world.query("test-agent",{-20,.02f,0},{20,0,0});
    require(route.status==NavigationStatus::Ready&&route.path&&route.path->tiles.size()>=4,"Path did not cross world-grid tile seams.");
    require(world.valid("test-agent",*route.path),"Fresh route corridor was already stale.");
    const auto builds=world.buildCount();world.reconcile(1,bounds,profiles,boxes,{});require(world.buildCount()==builds,"Identical accepted inputs rebuilt tiles.");
    require(world.readiness("unlisted")==NavigationStatus::Unavailable&&!world.query("unlisted",{},{1,0,0}).path,"Unlisted type silently created navigation data.");
    const auto debug=world.debugSnapshot("enemy");require(!debug.triangles.empty(),"Actor-free enemy profile did not produce actual mesh geometry.");
    const auto metadata=world.summary("enemy");require(metadata.vertices.empty()&&metadata.pendingVertices.empty()&&metadata.readyTiles==debug.readyTiles&&metadata.revision==debug.revision,"Metadata query copied mesh payloads or lost generation/counts.");
}
void localChangesAndPendingRoutes() {
    ResourceManager manager;NavigationWorld world(manager);const std::vector profiles{profile()};const auto bounds=volume({72,12,24});
    auto boxes=std::vector{box(1,{0,-.5f,0},{70,1,22}),box(2,{25,1,5},{1,2,1})};
    world.reconcile(1,bounds,profiles,boxes,{});settle(manager,world,"test-agent");
    const auto route=world.query("test-agent",{-30,.02f,-5},{-20,0,-5});require(route.path.has_value(),"Local route fixture failed.");
    const auto before=world.buildCount();boxes[1].world=math::translation({26,1,5});world.reconcile(1,bounds,profiles,boxes,{});
    require(world.valid("test-agent",*route.path),"A distant source edit invalidated an unrelated route corridor.");
    const auto pending=world.debugSnapshot("test-agent");require(pending.pendingTiles>0&&pending.pendingTiles<pending.pendingTiles+pending.readyTiles,"Source edit rebuilt the complete volume.");
    require(!pending.pendingTriangles.empty(),"Dirty tiles did not retain debug-only old geometry.");settle(manager,world,"test-agent");
    require(world.buildCount()-before<8,"A local source edit scheduled the whole world.");
    auto crossing=world.query("test-agent",{-30,.02f,0},{30,0,0});require(crossing.path.has_value(),"Crossing route fixture failed.");
    boxes.push_back(box(3,{0,1,0},{1,2,30}));world.reconcile(1,bounds,profiles,boxes,{});
    require(!world.valid("test-agent",*crossing.path),"Dirty corridor retained obsolete path validity.");
    const auto request=world.query("test-agent",{-30,.02f,0},{30,0,0});require(request.status==NavigationStatus::Preparing&&!request.path,"Pending tile hole became an artificial accepted destination.");
    settle(manager,world,"test-agent");
}
void orientedAndMultilevelVolume() {
    ResourceManager manager;NavigationWorld world(manager);const std::vector profiles{profile()};
    auto bounds=volume({14,10,10});bounds.world=math::eulerRotationXYZ({.15f,.4f,.1f});
    const std::vector boxes{box(1,{0,-.5f,0},{40,1,40}),box(2,{0,2.5f,0},{40,1,40}),box(3,{0,8.5f,0},{40,1,40})};
    world.reconcile(1,bounds,profiles,boxes,{});settle(manager,world,"test-agent");const auto debug=world.debugSnapshot("test-agent");
    require(!debug.vertices.empty(),"Oriented bounds removed all valid interior ground.");bool lower=false,upper=false;
    for(const auto& point:debug.vertices){require(navigationVolumeContains(bounds,point),"Debug polygons leaked through an oriented XYZ boundary.");lower|=point[1]<1;upper|=point[1]>2&&point[1]<4;require(point[1]<8,"Outside upper floor leaked into the volume.");}
    require(lower&&upper,"Tall volume flattened multiple walking surfaces into one.");
    bounds.size[1]=2;world.reconcile(1,bounds,profiles,boxes,{});settle(manager,world,"test-agent");
    for(const auto& point:world.debugSnapshot("test-agent").vertices)require(point[1]<2,"Shrinking Y retained the upper walking layer.");
}
void generationAndProfileReplacement() {
    ResourceManager manager;NavigationWorld world(manager);auto selected=profile();std::vector profiles{selected};const auto bounds=volume();
    std::vector boxes{box(1,{0,-.5f,0},{46,1,22})};world.reconcile(1,bounds,profiles,boxes,{});
    world.reconcile(2,std::nullopt,profiles,boxes,{});
    for(unsigned index=0;index<20;++index){manager.pump();world.pumpPublication();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    require(world.readiness(selected.id)==NavigationStatus::Unavailable&&world.debugSnapshot(selected.id).triangles.empty(),"Late old-domain completion resurrected navigation.");
    world.reconcile(2,bounds,profiles,boxes,{});settle(manager,world,selected.id);const auto builds=world.buildCount();
    profiles[0].traversal.maxStep=.1f;world.reconcile(2,bounds,profiles,boxes,{});require(world.readiness(selected.id)==NavigationStatus::Preparing&&world.buildCount()>builds,"Step-only profile edit reused incompatible data.");settle(manager,world,selected.id);
    world.setSourceReadiness(false,"Waiting for accepted triangles.");require(world.query(selected.id,{0,.02f,0},{1,0,0}).status==NavigationStatus::Preparing,"Pending collision accepted a new cache-hit route.");world.setSourceReadiness(true);
}
void cacheRecoveryAndPrimitiveIdentity() {
    const auto root=std::filesystem::temp_directory_path()/("crimson-nav-cache-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);NavigationTileInput input;input.volume=volume({24,12,24});input.tile={0,0};input.minimumY=-6.05f;input.maximumY=8.0f;input.boxes={box(1,{0,-.5f,0},{20,1,20})};
    auto first=buildNavigationTile(input,{},root);require(!first->triangles.empty(),"Cache fixture tile has no actual geometry.");
    const auto cache=std::filesystem::directory_iterator(root)->path();const auto time=std::filesystem::last_write_time(cache);auto warm=buildNavigationTile(input,{},root);
    require(warm->data==first->data&&std::filesystem::last_write_time(cache)==time,"Warm cache rewrote equal bytes.");
    {std::ofstream corrupt(cache,std::ios::binary|std::ios::trunc);corrupt<<"damaged";}
    auto repaired=buildNavigationTile(input,{},root);require(repaired->data==first->data&&std::filesystem::file_size(cache)>7,"Corrupt tile cache failed to rebuild.");
    auto mesh=std::make_shared<StaticTriangleMesh>();mesh->positions={{0,0,0},{1,0,0},{0,0,1}};mesh->indices={0,2,1};input.meshes={{2,{},mesh,true}};
    const auto original=navigationTileFingerprint(input);auto changed=std::make_shared<StaticTriangleMesh>(*mesh);changed->positions[1][0]=2;input.meshes[0].mesh=changed;
    require(navigationTileFingerprint(input)!=original,"Unfingerprinted primitive geometry collided in the disk cache.");
    // Only the exact scratch directory created above is removed.
    const auto absolute=std::filesystem::absolute(root).lexically_normal();require(std::filesystem::equivalent(absolute.parent_path(),std::filesystem::temp_directory_path()),"Scratch cache escaped temporary root.");std::filesystem::remove_all(absolute);
}
void zeroTraversalAndBudgetFailures() {
    NavigationTileInput input;input.volume=volume({24,12,24});input.minimumY=-6.05f;input.maximumY=8;input.agent.maxSlope=0;input.agent.maxStep=0;
    input.boxes={box(1,{5,-.5f,5},{12,1,12})};require(!buildNavigationTile(input)->triangles.empty(),"Zero slope/step excluded perfectly flat primitive ground.");
    input.boxes[0].world=math::translation({5,0,5})*math::eulerRotationXYZ({0,0,.0008726646f});
    require(buildNavigationTile(input)->triangles.empty(),"Zero slope silently accepted a 0.05 degree primitive ramp.");
    auto mesh=std::make_shared<StaticTriangleMesh>();mesh->positions={{1,0,1},{10,0,1},{10,0,10},{1,0,10}};mesh->indices={0,2,1,0,3,2};
    input.boxes.clear();input.meshes={{2,{},mesh,false}};require(!buildNavigationTile(input)->triangles.empty(),"Zero slope excluded flat triangle ground.");
    input.meshes[0].world=math::eulerRotationXYZ({0,0,.0008726646f});require(buildNavigationTile(input)->triangles.empty(),"Zero slope silently accepted a 0.05 degree triangle ramp.");
    ResourceManager manager;NavigationWorld world(manager);const std::vector profiles{profile()};const std::vector boxes{box(1,{0,-.5f,0},{20,1,20})};
    world.reconcile(1,volume({100000,12,100000}),profiles,boxes,{});
    require(world.readiness("test-agent")==NavigationStatus::Failed&&world.debugSnapshot("test-agent").triangles.empty(),"Oversized tile coverage escaped a bounded failure.");
    world.reconcile(1,volume(),profiles,boxes,{});settle(manager,world,"test-agent");const auto revision=world.revision("test-agent");
    world.reconcile(1,std::nullopt,profiles,boxes,{});world.reconcile(1,volume(),profiles,boxes,{});settle(manager,world,"test-agent");
    require(world.revision("test-agent")>revision,"Recreated supported data reused an old overlay generation.");
}
void collisionReadinessGatesPublication() {
    ResourceManager manager;NavigationWorld world(manager);const std::vector profiles{profile()};const std::vector boxes{box(1,{0,-.5f,0},{20,1,20})};
    world.setSourceReadiness(false,"Waiting for accepted collision.");world.reconcile(11,volume({24,12,24}),profiles,boxes,{});
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    do{manager.pump();world.pumpPublication();if(manager.idle())break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}while(std::chrono::steady_clock::now()<deadline);
    const auto pending=world.debugSnapshot("test-agent");require(pending.status==NavigationStatus::Preparing&&pending.readyTiles==0&&pending.vertices.empty(),"Initial domain reset or cache completion published before collision readiness.");
    require(world.project("test-agent",{0,0,0}).status==NavigationStatus::Preparing,"Spawn projection bypassed collision readiness.");
    world.setSourceReadiness(true);settle(manager,world,"test-agent");require(world.debugSnapshot("test-agent").readyTiles>0,"Opening collision gate did not publish completed tiles.");
}
}
int main(){try{seamAndActorFreeProfiles();localChangesAndPendingRoutes();orientedAndMultilevelVolume();generationAndProfileReplacement();cacheRecoveryAndPrimitiveIdentity();zeroTraversalAndBudgetFailures();collisionReadinessGatesPublication();std::cout<<"Tiled async navigation, profiles, volume masks, route validity and cache checks passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
