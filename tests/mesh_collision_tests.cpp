#include "physics/PhysicsWorld.h"
#include "navigation/NavigationMesh.h"
#include "runtime/PlaySession.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
bool near(double a,double b,double tolerance=.06){return std::abs(a-b)<tolerance;}
std::shared_ptr<StaticTriangleMesh> plane(float radius=8) {
    auto mesh=std::make_shared<StaticTriangleMesh>();mesh->positions={{-radius,0,-radius},{radius,0,-radius},{radius,0,radius},{-radius,0,radius}};
    mesh->indices={0,2,1,0,3,2};for(const auto& point:mesh->positions)math::include(mesh->bounds,point);mesh->fingerprint=1;return mesh;
}
EntityRecord collider(Mat4 world={}) {
    EntityRecord record;record.id=1;record.name="Triangle floor";record.local=world;record.meshCollider.emplace();
    record.meshCollider->source=AssetRef<ModelAsset>{"assets/models/collision/manifest.json"};return record;
}
EntityRecord character(Vec3 position={0,2,0}) {
    EntityRecord record;record.id=2;record.name="Character";record.character.emplace();record.local=math::translation(position);return record;
}
void tick(Scene& scene,PhysicsWorld& physics,std::span<const PhysicsMeshCollider> meshes,int count,Vec3 velocity={}) {
    for(int index=0;index<count;++index)scene.applyRuntimePoses(physics.step(scene,2,velocity,1.f/60,{},meshes));
}
void floorNavigationAndAcceptedReplacement() {
    auto mesh=plane();const std::array sources{PhysicsMeshCollider{1,mesh,1}};
    auto floor=collider();Scene scene;scene.apply({floor,character()},{});PhysicsWorld physics;physics.reconcile(scene,{},sources);
    require(physics.colliderCount()==1&&physics.staticBoxes().empty()&&physics.staticMeshes().size()==1,"Mesh collision was omitted or represented as a Box.");
    const auto hit=physics.raycast({{0,4,0},{0,-1,0}},10);
    require(hit&&hit->entity==1&&near(hit->position[1],0)&&hit->normal[1]>.9f,"Triangle raycast lost persistent identity or winding.");
    const auto below=physics.raycast({{0,-2,0},{0,1,0}},10);
    require(below&&below->entity==1&&near(below->position[1],0),"Thin mesh collision did not accept a backface click ray.");
    tick(scene,physics,sources,180);
    require(physics.grounded(2)&&near((*physics.characterPosition(2))[1],0),"Character did not ground on original mesh triangles.");
    NavigationMesh navigation;require(navigation.reconcile(physics,{}),"Mesh floor did not build navigation.");
    require(navigation.findPath({-3,.02f,0},{3,0,0}).has_value(),"Navigation omitted accepted triangle floor geometry: "+navigation.status());
    const auto revision=physics.geometryRevision(),navRevision=navigation.revision();
    floor.meshCollider->collisionColor={1,0,1,.5f};floor.meshCollider->showCollisionInGame=true;
    auto records=scene.records();for(auto& record:records)if(record.id==1)record=floor;scene.apply(records,{});physics.reconcile(scene,{},sources);
    require(physics.geometryRevision()==revision&&!navigation.reconcile(physics,{})&&navigation.revision()==navRevision,"Debug display settings rebuilt static collision/navigation.");
    auto invalid=std::make_shared<StaticTriangleMesh>(*mesh);invalid->indices[1]=999;
    const std::array rejected{PhysicsMeshCollider{1,invalid,2}};physics.reconcile(scene,{},rejected);
    require(physics.geometryRevision()==revision&&physics.staticMeshes().front().mesh==mesh&&!physics.status().empty(),"Invalid replacement discarded the last accepted mesh.");
    auto raised=plane();for(auto& point:raised->positions)point[1]=1;raised->fingerprint=2;
    const std::array changed{PhysicsMeshCollider{1,raised,2}};physics.reconcile(scene,{},changed);
    require(physics.geometryRevision()==revision+1&&navigation.reconcile(physics,{}),"Valid mesh revision did not invalidate derived navigation.");
    const auto raisedHit=physics.raycast({{0,4,0},{0,-1,0}},10);
    require(raisedHit&&near(raisedHit->position[1],1),"Replacement retained the previous triangle surface.");
    floor.meshCollider->source={};scene.apply({floor},{});physics.reconcile(scene,{},changed);navigation.reconcile(physics,{});
    require(physics.colliderCount()==0&&!physics.raycast({{0,4,0},{0,-1,0}},10)&&!navigation.findPath({},{1,0,0}),"Explicit collision clear retained a body or invented navigation ground.");
}
void affineMeshTransformsAndRamps() {
    const auto mesh=plane();const std::array sources{PhysicsMeshCollider{1,mesh,1}};
    std::array<Mat4,3> transforms{math::translation({3,2,-1})*math::scaling({-2,1,1.5f}),math::eulerRotationXYZ({0,0,.35f}),Mat4{}};
    transforms[2].at(0,2)=.4f;transforms[2].at(1,0)=.2f;transforms[2][13]=1;
    for(const auto& world:transforms) {
        auto floor=collider(world);Scene scene;scene.apply({floor},{});PhysicsWorld physics;physics.reconcile(scene,{},sources);
        require(physics.colliderCount()==1&&physics.status().empty(),"Finite reflection/shear/ramp disabled a triangle collider: "+physics.status());
        const auto center=math::transformPoint(world,{});const auto hit=physics.raycast({center+Vec3{0,5,0},{0,-1,0}},10);
        require(hit&&near(hit->position[1],center[1])&&hit->normal[1]>.7f,"Affine collision surface did not match transformed authored triangles.");
        NavigationMesh navigation;navigation.reconcile(physics,{});
        const auto start=math::transformPoint(world,{-3,.02f,0}),destination=math::transformPoint(world,{3,0,0});
        require(navigation.findPath(start,destination).has_value(),"Reflected/sheared/ramp triangles lost their walkable winding: "+navigation.status());
    }
    auto steep=collider(math::eulerRotationXYZ({0,0,1.1f}));Scene scene;scene.apply({steep},{});PhysicsWorld physics;physics.reconcile(scene,{},sources);
    NavigationMesh navigation;navigation.reconcile(physics,{});
    require(!navigation.findPath(math::transformPoint(steep.local,{-3,0,0}),math::transformPoint(steep.local,{3,0,0})),"Steep triangle surface bypassed navigation slope limits.");
    auto floor=collider(math::eulerRotationXYZ({0,0,.25f}));scene.apply({floor,character({-3,2,0})},{});PhysicsWorld motor;tick(scene,motor,sources,120);
    double highest=-100;
    for(int index=0;index<240;++index){tick(scene,motor,sources,1,{1,0,0});if(motor.grounded(2))highest=std::max(highest,double((*motor.characterPosition(2))[1]));}
    require(highest>-.2,"Character failed to climb a walkable triangle ramp.");
}
void playReadinessAndStaticOwnership() {
    const auto root=std::filesystem::temp_directory_path()/"crimson-mesh-collision-transient";
    SceneDocument authored(root);require(authored.edit("Mesh gameplay fixture",[](auto& records,auto& properties){records={collider(),character()};properties.playerCharacter=2;}),authored.status());
    PlaySession play(root);play.setCollisionMeshDefinitions([](const std::string&){return CollisionMeshResult{{},0,false,"Collision fixture pending"};});
    require(!play.start(authored)&&!play.playing()&&play.status()=="Collision fixture pending","Play started without its first collision generation.");
    const auto mesh=plane();play.setCollisionMeshDefinitions([mesh](const std::string&){return CollisionMeshResult{mesh,1,true,{}};});
    require(play.start(authored),play.status());const auto before=authored.scene().records();
    Camera camera;for(int index=0;index<180;++index)play.advance(1.0/60,Vec2{},camera,true);
    require(near(play.document().scene().worldTransform(2)->at(1,3),0),"Play fixed ticks omitted pinned mesh collision inputs.");
    require(authored.scene().find(2)->local==before[1].local,"Play collision changed the authored scene.");play.stop();
    auto parent=character();parent.id=3;auto child=collider();child.parent=3;Scene scene;scene.apply({parent,child},{});PhysicsWorld physics;
    const std::array sources{PhysicsMeshCollider{1,mesh,1}};physics.reconcile(scene,{},sources);
    require(physics.colliderCount()==0&&!physics.status().empty(),"A Character accepted a nested static mesh body.");
}
}
int main(){try{floorNavigationAndAcceptedReplacement();affineMeshTransformsAndRamps();playReadinessAndStaticOwnership();std::cout<<"Mesh collision, affine transforms, accepted navigation and Play ownership checks passed.\n";return 0;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
