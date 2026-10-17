#include "agent_profile_fixture.h"
#include "editor/CollisionOverlays.h"
#include "resources/AssetId.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using namespace crimson;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(float actual,float expected,const char* message){require(std::abs(actual-expected)<1e-5f,message);}
void near(const Vec3& actual,const Vec3& expected,const char* message){require(math::length(actual-expected)<1e-4,message);}
template<class Predicate> void await(ResourceManager& resources,Predicate predicate){
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{5};
    do{resources.pump();if(predicate())return;std::this_thread::sleep_for(std::chrono::milliseconds{1});}while(std::chrono::steady_clock::now()<deadline);
    throw std::runtime_error("Collision overlay worker did not publish its immutable geometry.");
}
EntityRecord collider(EntityId id,ColliderShape shape){EntityRecord record;record.id=id;record.name="Collider";record.collider.emplace();record.collider->shape=shape;return record;}
std::shared_ptr<StaticTriangleMesh> plane(float height=0,std::uint64_t fingerprint=1){
    auto result=std::make_shared<StaticTriangleMesh>();result->positions={{-2,height,-2},{2,height,-2},{2,height,2},{-2,height,2}};
    result->indices={0,2,1,0,3,2};result->fingerprint=fingerprint;for(const auto& point:result->positions)math::include(result->bounds,point);return result;
}
ResourceConfig config(){ResourceConfig result;result.workers=1;result.idleGrace=std::chrono::milliseconds{0};result.cpuRetentionBytes=0;return result;}
void primitivePlanes(const CollisionOverlayGeometry& geometry,const SceneCollider& collider) {
    for(const auto& edge:geometry.edges) {
        require(math::finite(edge.normalA)&&math::finite(edge.normalB),"A prepared primitive edge has nonfinite surface planes.");
        near(math::length(edge.normalA),1,"A prepared primitive edge lost its first unit surface normal.");
        near(math::length(edge.normalB),1,"A prepared primitive edge lost its second unit surface normal.");
        require(!edge.concave&&!edge.planeFallback,"An analytic convex primitive acquired concave or nonmanifold edges.");
        const auto& a=geometry.wireVertices.at(edge.a);const auto& b=geometry.wireVertices.at(edge.b);
        const auto direction=b-a,midpoint=(a+b)*.5f;
        require(std::abs(math::dot(edge.normalA,direction))<math::length(direction)*1e-4+1e-6&&
            std::abs(math::dot(edge.normalB,direction))<math::length(direction)*1e-4+1e-6,
            "Primitive normals are not tangent to their final authored-size wire segment.");
        if(collider.shape==ColliderShape::Box) {
            near(math::dot(edge.normalA,edge.normalB),0,"Box edge planes must be perpendicular.");
            require(math::dot(edge.normalA,midpoint)>0&&math::dot(edge.normalB,midpoint)>0,"Box planes point inward.");
        } else if(collider.shape==ColliderShape::Cylinder) {
            near(edge.normalA[1],0,"Cylinder side plane gained a cap component.");
            require(math::dot(edge.normalA,midpoint)>0,"Cylinder side plane points inward.");
            if(a[1]==b[1]) {
                near(std::abs(edge.normalB[1]),1,"Cylinder rim omitted its cap plane.");
                require(edge.normalB[1]*midpoint[1]>0,"Cylinder cap plane points inward.");
            } else near(edge.normalA,edge.normalB,"Cylinder side contours must use their common tangent plane.");
        } else {
            near(edge.normalA,edge.normalB,"Smooth primitive contours must retain their common tangent plane.");
            const auto stem=collider.shape==ColliderShape::Sphere?0.f:collider.height*.5f-collider.radius;
            const auto radial=midpoint-Vec3{0,std::clamp(midpoint[1],-stem,stem),0};
            require(math::dot(edge.normalA,radial)>math::length(radial)*.9999,"Sphere/Capsule tangent ignored the authored centered surface.");
        }
    }
}
void primitiveIdentityAndVisibility(){
    ResourceManager resources(config());CollisionOverlays overlays(resources);unsigned queries{};
    const CollisionMeshQuery query=[&](const std::string&)->CollisionMeshResult{++queries;throw std::runtime_error("Primitive requested managed triangle geometry.");};
    for(const auto shape:{ColliderShape::Box,ColliderShape::Sphere,ColliderShape::Cylinder,ColliderShape::Capsule}) {
        auto record=collider(1,shape);record.collider->size={2,3,4};record.collider->radius=.75f;record.collider->height=3;
        record.collider->collisionColor={.2f,.3f,.4f,.5f};Scene scene;scene.apply({record},{});
        const auto initial=overlays.prepare(scene,{},true,query);require(initial.scene.size()==1&&initial.game.empty(),"Primitive overlay did not respect Show in Game default.");
        const auto geometry=initial.scene.front().geometry;require(geometry&&!geometry->triangles.empty()&&!geometry->edges.empty(),"Primitive packet omitted immutable fill or wire geometry.");
        primitivePlanes(*geometry,*record.collider);
        near(initial.scene.front().color[3],.1f,"Unselected Scene alpha must be authored alpha times .2.");
        near(initial.scene.front().fillAlphaScale,1.f/16,"Collision fill opacity contract changed.");
        if(shape==ColliderShape::Box)require(geometry->edges.size()==12,"Prepared Box wire exposed triangulation diagonals.");
        record.local=math::translation({4,5,6})*math::eulerRotationXYZ({.2f,.3f,.1f})*math::scaling({2,1,-1});record.local.at(0,1)+=.15f;
        record.collider->showCollisionInGame=true;record.collider->collisionColor={.7f,.6f,.5f,.75f};
        // Inactive authored fallbacks must not create another geometry version.
        if(shape==ColliderShape::Box){record.collider->radius=4;record.collider->height=9;}
        else{record.collider->size={8,9,10};if(shape==ColliderShape::Sphere)record.collider->height=9;}
        record.collider->source=AssetRef<ModelAsset>{canonicalAssetId("assets/models/inactive/manifest.json")};scene.apply({record},{});
        const std::array selected{record.id};const auto changed=overlays.prepare(scene,selected,true,query);
        require(changed.scene.size()==1&&changed.game.size()==1,"Visible primitive did not produce both view packets.");
        require(changed.scene.front().geometry==geometry&&changed.game.front().geometry==geometry,"Color, pose, selection or inactive fields regenerated primitive topology.");
        require(changed.scene.front().world==record.local&&changed.game.front().world==record.local,"Primitive packet lost its exact affine pose.");
        require(changed.scene.front().color[0]==.7f&&changed.game.front().color==record.collider->collisionColor,"Overlay RGB or authored Game alpha changed.");
        near(changed.scene.front().color[3],.6f,"Selected Scene alpha must be authored alpha times .8.");
        near(initial.scene.front().color[3],.1f,"Changing current display state mutated a previously prepared packet.");
        const auto unselected=overlays.prepare(scene,{},true,query);require(unselected.scene.front().geometry==geometry,"Changing selection replaced primitive geometry.");
        near(unselected.scene.front().color[3],.15f,"Unselected Scene alpha did not update independently of Game alpha.");
        near(unselected.game.front().color[3],.75f,"Scene selection altered Game overlay alpha.");
    }
    require(queries==0&&resources.stats().loads==0&&resources.stats().queued==0&&resources.stats().running==0,"Primitive display changes queued resource preparation.");
}
void hierarchicalVisibility(){
    ResourceManager resources(config());CollisionOverlays overlays(resources);Scene scene;
    EntityRecord group;group.id=10;group.name="Hidden group";group.visible=false;
    auto mesh=collider(1,ColliderShape::Trimesh);mesh.parent=group.id;mesh.collider->source=AssetRef<ModelAsset>{canonicalAssetId("assets/models/hidden/manifest.json")};
    mesh.collider->showCollisionInGame=true;
    auto primitive=collider(2,ColliderShape::Box);primitive.parent=group.id;primitive.collider->showCollisionInGame=true;
    EntityRecord actor;actor.id=3;actor.name="Character";actor.parent=group.id;actor.controller.emplace();actor.controller->showCollisionInGame=true;
    auto child=collider(4,ColliderShape::Sphere);child.parent=group.id;child.visible=false;child.collider->showCollisionInGame=true;
    const auto publish=[&]{scene.apply({group,mesh,primitive,actor,child},{});};publish();unsigned queries{};
    const CollisionMeshQuery query=[&](const std::string&)->CollisionMeshResult{++queries;throw std::runtime_error("Hidden collider requested triangle geometry.");};
    const std::array selected{mesh.id,primitive.id,actor.id,child.id};
    const auto hidden=overlays.prepare(scene,selected,true,query);
    require(hidden.scene.empty()&&hidden.game.empty()&&queries==0&&resources.stats().queued==0,
        "A hidden hierarchy emitted overlays or scheduled Trimesh preparation.");
    group.visible=true;mesh.visible=false;publish();
    const auto shown=overlays.prepare(scene,selected,true,query);
    require(shown.scene.size()==2&&shown.game.size()==2&&queries==0&&!scene.find(child.id)->visible,
        "Showing a parent failed to restore eligible overlays or overwrote a hidden child's own setting.");
    const auto geometry=shown.scene.front().geometry;group.visible=false;publish();
    const auto hiddenAgain=overlays.prepare(scene,selected,true,query);
    require(hiddenAgain.scene.empty()&&hiddenAgain.game.empty()&&queries==0,
        "Hiding a parent retained selected collision overlays.");
    group.visible=true;publish();const auto restored=overlays.prepare(scene,selected,true,query);
    require(restored.scene.size()==2&&restored.game.size()==2&&restored.scene.front().geometry==geometry&&resources.stats().loads==0,
        "Restoring primitive overlays rebuilt their immutable geometry or lost the visibility state.");
}
void disabledOverlaysDoNoWork(){
    ResourceManager resources(config());CollisionOverlays overlays(resources);Scene scene;auto mesh=collider(1,ColliderShape::Trimesh);
    mesh.collider->source=AssetRef<ModelAsset>{canonicalAssetId("assets/models/floor/manifest.json")};mesh.collider->showCollisionInGame=true;
    auto primitive=collider(2,ColliderShape::Box);EntityRecord character;character.id=3;character.name="Character";character.controller.emplace();character.controller->showCollisionInGame=true;
    scene.apply({mesh,primitive,character},{});unsigned queries{};
    const CollisionMeshQuery query=[&](const std::string&)->CollisionMeshResult{++queries;throw std::runtime_error("Hidden overlays requested collision sources.");};
    for(unsigned frame=0;frame<10;++frame){const auto packets=overlays.prepare(scene,{},false,query);require(packets.scene.empty()&&packets.game.empty(),"Global Collisions off retained a view overlay.");}
    resources.pump();require(queries==0&&resources.stats().loads==0&&resources.stats().queued==0&&resources.stats().running==0,"Global Collisions off scheduled CPU preparation.");
    mesh.local=math::scaling({0,1,1});scene.apply({mesh},{});const auto singular=overlays.prepare(scene,{},true,query);
    require(singular.scene.empty()&&singular.game.empty()&&queries==0,"A singular collision transform requested or published geometry.");
}
void nativeMeshWithoutAssetQuery(){
    ResourceManager resources(config());CollisionOverlays overlays(resources);Scene scene;
    auto record=collider(1,ColliderShape::Trimesh);const auto original=plane();
    record.collider->mesh=makeCollisionMesh(original->positions,original->indices);record.collider->showCollisionInGame=true;
    auto second=record;second.id=2;second.collider->mesh=makeCollisionMesh(original->positions,original->indices);
    require(second.collider->mesh!=record.collider->mesh,"Native overlay fixture did not create distinct payload identities.");
    scene.apply({record,second},{});
    const CollisionMeshQuery forbidden=[](const std::string&)->CollisionMeshResult {throw std::runtime_error("Native collision queried a model asset.");};
    PreparedCollisionOverlays packets;
    await(resources,[&]{packets=overlays.prepare(scene,{},true,forbidden);return packets.scene.size()==2;});
    require(packets.game.size()==2&&!packets.scene.front().geometry->vertices.empty(),"Native collision omitted Scene/Game geometry.");
    const auto accepted=packets.scene.front().geometry;const auto loads=resources.stats().loads;
    for(unsigned frame=0;frame<8;++frame){resources.pump();packets=overlays.prepare(scene,{},true,forbidden);}
    require(packets.scene.size()==2&&packets.game.size()==2&&packets.scene.front().geometry==accepted&&resources.stats().loads==loads,
        "Native collision lost packets or rebuilt unchanged overlay geometry.");
    auto raised=original->positions;for(auto& point:raised)point[1]=2;
    record.collider->mesh=makeCollisionMesh(std::move(raised),original->indices);scene.apply({record},{});
    await(resources,[&]{packets=overlays.prepare(scene,{},true,forbidden);return packets.scene.size()==1&&packets.scene.front().geometry!=accepted;});
    for(const auto& point:packets.scene.front().geometry->vertices)near(point[1],2,"Native collision overlay retained its old payload.");
}
void characterFeetAndSharedCapsule(){
    ResourceManager resources(config());CollisionOverlays overlays(resources);Scene scene;EntityRecord actor;actor.id=1;actor.name="Character";actor.controller.emplace();
    actor.controller->profile=testing::agentProfile({2.4f,.4f,45,.3f});actor.controller->showCollisionInGame=true;actor.controller->collisionColor={.2f,.5f,.8f,.6f};
    actor.local=math::translation({4,3,2})*math::eulerRotationXYZ({.1f,.4f,.2f})*math::scaling({2,1.5f,1});
    auto centered=collider(2,ColliderShape::Capsule);centered.collider->height=2.4f;centered.collider->radius=.4f;centered.local=math::translation({-4,0,0});
    scene.apply({actor,centered},{});const std::array selected{actor.id};const auto packets=overlays.prepare(scene,selected,true,{},testing::agentProfileQuery());
    require(packets.scene.size()==2&&packets.game.size()==1,"Character or centered Capsule packet was omitted.");
    const auto expectedWorld=actor.local*math::translation({0,2.4f*.5f,0});
    const auto found=std::find_if(packets.scene.begin(),packets.scene.end(),[&](const auto& packet){return packet.world==expectedWorld;});
    require(found!=packets.scene.end()&&packets.game.front().world==expectedWorld,"Character's centered debug capsule was not translated from its feet origin.");
    const auto geometry=found->geometry;require(packets.scene[0].geometry==packets.scene[1].geometry&&packets.game.front().geometry==geometry,"Matching Character and Collider capsules did not share immutable geometry.");
    Aabb bounds;for(const auto& point:geometry->vertices)math::include(bounds,point);
    near(bounds.minimum[1],-1.2f,"Cached Capsule geometry is not centered.");near(bounds.maximum[1],1.2f,"Cached Capsule total height changed.");
    near(math::transformPoint(found->world,{0,bounds.minimum[1],0}),math::transformPoint(actor.local,{}),"Character overlay bottom moved away from the authored feet.");
    near(math::transformPoint(found->world,{0,bounds.maximum[1],0}),math::transformPoint(actor.local,{0,2.4f,0}),"Character overlay top disagrees with authored total height.");
    near(found->color[3],.48f,"Selected Character did not use Scene selection opacity.");near(packets.game.front().color[3],.6f,"Character Game opacity lost its authored alpha.");
}
void asynchronousMeshReuseAndReplacement(){
    ResourceManager resources(config());CollisionOverlays overlays(resources);Scene scene;auto record=collider(1,ColliderShape::Trimesh);
    const auto id=canonicalAssetId("assets/models/floor/manifest.json");record.collider->source=AssetRef<ModelAsset>{id};record.collider->showCollisionInGame=true;
    record.collider->collisionColor={.3f,.5f,.7f,.5f};auto second=record;second.id=2;second.name="Shared collider";second.local=math::translation({5,0,0});scene.apply({record,second},{});
    auto source=plane();const auto positions=source->positions;const auto triangles=source->indices;CollisionMeshResult current{source,1,true,{}};unsigned queries{};
    const CollisionMeshQuery query=[&](const std::string& requested){require(requested==id,"Overlay requested a different source identity.");++queries;return current;};
    const auto cold=overlays.prepare(scene,{},true,query);
    // Publication occurs only in pump(): this remains deterministic even when
    // the tiny worker job finishes before the first prepare call returns.
    require(cold.scene.empty()&&cold.game.empty()&&resources.stats().loads==0,"Trimesh adjacency was synchronously prepared or published from a worker.");
    PreparedCollisionOverlays ready;await(resources,[&]{ready=overlays.prepare(scene,{},true,query);return ready.scene.size()==2&&ready.game.size()==2;});
    const auto geometry=ready.scene.front().geometry;require(geometry&&geometry->vertices==positions&&geometry->triangles==triangles,"Trimesh overlay changed original collision triangles or vertices.");
    require(geometry->edges.size()==4,"Coplanar Trimesh diagonal survived prepared feature-edge extraction.");
    for(const auto& edge:geometry->edges)require(edge.normalA==Vec3{0,1,0}&&edge.normalB==edge.normalA&&!edge.concave&&!edge.planeFallback,
        "Prepared Trimesh boundaries lost their original surface plane or acquired concavity.");
    require(ready.scene[1].geometry==geometry&&ready.game[0].geometry==geometry&&ready.game[1].geometry==geometry,"Repeated sources or Scene/Game prepared duplicate Trimesh geometry.");
    require(resources.stats().loads==1,"Shared Trimesh consumers queued duplicate adjacency jobs.");
    const auto loads=resources.stats().loads;
    record.local=math::translation({0,3,0});record.collider->collisionColor={1,.2f,.1f,.25f};second=record;second.id=2;second.name="Shared collider";second.local=math::translation({5,3,0});scene.apply({record,second},{});
    const std::array selected{EntityId{1}};
    for(unsigned frame=0;frame<12;++frame){const auto warm=overlays.prepare(scene,selected,true,query);require(warm.scene.size()==2&&warm.scene.front().geometry==geometry,"Warm Trimesh display changes replaced accepted topology.");resources.pump();}
    require(resources.stats().loads==loads,"Warm Trimesh color, pose or selection scheduled new worker preparation.");
    auto replacement=plane(2,2);current={replacement,2,true,{}};
    const auto pending=overlays.prepare(scene,selected,true,query);
    require(pending.scene.size()==2&&pending.scene.front().geometry==geometry&&pending.game.front().geometry==geometry,"A pending source revision removed the last-good overlay.");
    require(resources.stats().loads==loads,"A replacement published before the main-thread pump boundary.");
    PreparedCollisionOverlays replaced;await(resources,[&]{replaced=overlays.prepare(scene,selected,true,query);return replaced.scene.size()==2&&replaced.scene.front().geometry!=geometry;});
    require(replaced.scene.front().geometry->vertices==replacement->positions&&replaced.scene.front().geometry->triangles==replacement->indices,"Accepted replacement did not carry the new exact source triangles.");
    require(replaced.scene[1].geometry==replaced.scene[0].geometry&&replaced.game[0].geometry==replaced.scene[0].geometry,"Replacement publication tore the shared Scene/Game source version.");
    require(resources.stats().loads==loads+1,"One source revision produced more than one adjacency job.");
    require(source->positions==positions&&source->indices==triangles&&geometry->vertices==positions&&geometry->triangles==triangles,"A new source mutated original collision inputs or a leased prepared version.");
    current={};
    for(unsigned frame=0;frame<4;++frame) {
        resources.pump();const auto unavailable=overlays.prepare(scene,{},true,query);
        require(unavailable.scene.size()==2&&unavailable.game.size()==2&&
            unavailable.scene.front().geometry==replaced.scene.front().geometry&&unavailable.game.front().geometry==replaced.scene.front().geometry,
            "A temporarily unavailable source discarded its last-good Scene/Game overlays.");
    }
    require(resources.stats().loads==loads+1,"A temporarily unavailable source scheduled replacement preparation.");
    record.collider->source={};second.collider->source={};scene.apply({record,second},{});const auto calls=queries;const auto cleared=overlays.prepare(scene,{},true,query);
    require(cleared.scene.empty()&&cleared.game.empty()&&queries==calls,"Clearing Trimesh source retained a packet or queried a missing reference.");
}
void preparedCreasePlanes(){
    ResourceManager resources(config());CollisionOverlays overlays(resources);Scene scene;
    const auto concaveId=canonicalAssetId("assets/models/concave/manifest.json"),convexId=canonicalAssetId("assets/models/convex/manifest.json"),nonmanifoldId=canonicalAssetId("assets/models/nonmanifold/manifest.json");
    auto concave=collider(1,ColliderShape::Trimesh),convex=collider(2,ColliderShape::Trimesh),nonmanifold=collider(3,ColliderShape::Trimesh);
    concave.collider->source=AssetRef<ModelAsset>{concaveId};convex.collider->source=AssetRef<ModelAsset>{convexId};nonmanifold.collider->source=AssetRef<ModelAsset>{nonmanifoldId};scene.apply({concave,convex,nonmanifold},{});
    auto reentrant=std::make_shared<StaticTriangleMesh>();reentrant->positions={{0,0,0},{1,0,0},{0,1,0},{0,0,1}};reentrant->indices={0,1,2,1,0,3};reentrant->fingerprint=10;
    auto outward=std::make_shared<StaticTriangleMesh>(*reentrant);outward->positions[3][2]=-1;outward->fingerprint=11;
    auto ambiguous=std::make_shared<StaticTriangleMesh>(*reentrant);ambiguous->positions.push_back({0,-1,0});ambiguous->indices.insert(ambiguous->indices.end(),{0,1,4});ambiguous->fingerprint=12;
    const CollisionMeshQuery query=[&](const std::string& id){return CollisionMeshResult{id==concaveId?reentrant:id==convexId?outward:ambiguous,1,true,{}};};
    PreparedCollisionOverlays ready;await(resources,[&]{ready=overlays.prepare(scene,{},true,query);return ready.scene.size()==3;});
    for(const auto& packet:ready.scene) {
        const auto& geometry=*packet.geometry;
        const bool expectedFallback=geometry.vertices.size()==5,expectedConcave=geometry.vertices[3][2]>0&&!expectedFallback;
        const auto found=std::find_if(geometry.edges.begin(),geometry.edges.end(),[](const auto& edge){return edge.a==0&&edge.b==1;});
        require(found!=geometry.edges.end(),"Prepared crease lost its shared edge.");
        require(found->normalA==Vec3{0,0,1}&&found->normalB==Vec3{0,geometry.vertices[3][2]>0?1.f:-1.f,0}&&found->concave==expectedConcave&&found->planeFallback==expectedFallback,
            "Worker conversion discarded adjacent crease planes, classification or nonmanifold fallback.");
    }
}
void resourceLeasesOutlivePreparation(){
    ResourceManager resources(config());PreparedCollisionOverlays pinned;std::weak_ptr<const CollisionOverlayGeometry> weakGeometry;std::weak_ptr<const StaticTriangleMesh> weakSource;
    {
        CollisionOverlays overlays(resources);Scene scene;auto record=collider(1,ColliderShape::Trimesh);record.collider->source=AssetRef<ModelAsset>{canonicalAssetId("assets/models/lifetime/manifest.json")};scene.apply({record},{});
        auto source=plane();weakSource=source;CollisionMeshResult current{source,1,true,{}};
        const CollisionMeshQuery query=[&](const std::string&){return current;};
        (void)overlays.prepare(scene,{},true,query);await(resources,[&]{pinned=overlays.prepare(scene,{},true,query);return !pinned.scene.empty();});
        weakGeometry=pinned.scene.front().geometry;current={};source.reset();
        await(resources,[&]{return weakSource.expired();});
        require(resources.stats().cpuBytes>0,"Prepared Trimesh allocation was not accounted.");
    }
    resources.trim();require(!weakGeometry.expired()&&!pinned.scene.front().geometry->triangles.empty()&&resources.stats().cpuBytes>0,
        "Destroying the preparation owner or trimming under pressure invalidated a submitted packet lease.");
    pinned={};resources.trim();require(weakGeometry.expired()&&resources.stats().cpuBytes==0&&resources.stats().liveInterests==0,
        "Released collision overlay geometry or resource interests survived pressure trimming.");
}
}
int main(){try{primitiveIdentityAndVisibility();hierarchicalVisibility();disabledOverlaysDoNoWork();nativeMeshWithoutAssetQuery();characterFeetAndSharedCapsule();asynchronousMeshReuseAndReplacement();preparedCreasePlanes();resourceLeasesOutlivePreparation();
    std::cout<<"Collision overlay cache, opacity, async replacement and lifetime preparation passed.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
