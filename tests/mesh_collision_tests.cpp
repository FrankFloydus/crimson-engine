#include "scene/SceneComponents.h"
#include "physics/PhysicsWorld.h"
#include "navigation_mesh_fixture.h"
#include "runtime/PlaySession.h"
#include "physics/ColliderGeometry.h"
#include "scene/SceneExtraction.h"
#include "editor/CollisionWire.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <map>
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
    EntityRecord record;record.id=1;record.name="Triangle floor";record.local=world;record.collider.emplace();record.collider->shape=ColliderShape::Trimesh;
    record.collider->source=AssetRef<ModelAsset>{"assets/models/collision/manifest.json"};return record;
}
EntityRecord character(Vec3 position={0,2,0}) {
    EntityRecord record;record.id=2;record.name="Character";record.controller.emplace();record.motor.emplace();record.playerInput.emplace();record.navigation.emplace();record.local=math::translation(position);return record;
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
    crimson::test::NavigationMeshFixture navigation(crimson::test::navigationTestVolume());require(navigation.reconcile(physics,{}),"Mesh floor did not build navigation.");
    require(navigation.findPath({-3,.02f,0},{3,0,0}).has_value(),"Navigation omitted accepted triangle floor geometry: "+navigation.status());
    const auto revision=physics.geometryRevision(),navRevision=navigation.revision();
    floor.collider->collisionColor={1,0,1,.5f};floor.collider->showCollisionInGame=true;
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
    floor.collider->source={};scene.apply({floor},{});physics.reconcile(scene,{},changed);navigation.reconcile(physics,{});
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
        crimson::test::NavigationMeshFixture navigation(crimson::test::navigationTestVolume());navigation.reconcile(physics,{});
        const auto start=math::transformPoint(world,{-3,.02f,0}),destination=math::transformPoint(world,{3,0,0});
        require(navigation.findPath(start,destination).has_value(),"Reflected/sheared/ramp triangles lost their walkable winding: "+navigation.status());
    }
    auto steep=collider(math::eulerRotationXYZ({0,0,1.1f}));Scene scene;scene.apply({steep},{});PhysicsWorld physics;physics.reconcile(scene,{},sources);
    crimson::test::NavigationMeshFixture navigation(crimson::test::navigationTestVolume());navigation.reconcile(physics,{});
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
void nativeTrianglesWithoutAssets() {
    const auto geometry=plane();auto floor=collider();floor.collider->source={};
    floor.collider->mesh=makeCollisionMesh(geometry->positions,geometry->indices);
    Scene scene;scene.apply({floor},{});PhysicsWorld physics;physics.reconcile(scene);
    require(physics.colliderCount()==1&&physics.status().empty(),"Native triangles required a model asset: "+physics.status());
    const auto hit=physics.raycast({{0,3,0},{0,-1,0}},10);
    require(hit&&hit->entity==1&&near(hit->position[1],0),"Native triangles were not used by physics raycasts.");
    require(pickCollisionShape(scene,{{0,3,0},{0,-1,0}},10)==1,"Native triangles required an asset query for editor picking.");
    const std::array<EntityId,1> selected{1};const auto bounds=selectionBounds(scene,selected,{});
    require(bounds.valid&&bounds.minimum==geometry->bounds.minimum&&bounds.maximum==geometry->bounds.maximum,"Native collision bounds were omitted.");
    crimson::test::NavigationMeshFixture navigation(crimson::test::navigationTestVolume());navigation.reconcile(physics,{});
    require(navigation.findPath({-3,0,0},{3,0,0}).has_value(),"Native triangles did not generate navigation.");
    const auto revision=physics.geometryRevision();auto raised=geometry->positions;for(auto& point:raised)point[1]=.5f;
    floor.collider->mesh=makeCollisionMesh(std::move(raised),geometry->indices);scene.apply({floor},{});physics.reconcile(scene);
    const auto raisedHit=physics.raycast({{0,3,0},{0,-1,0}},10);
    require(physics.geometryRevision()>revision&&raisedHit&&near(raisedHit->position[1],.5),"Native payload replacement retained stale physics.");
    floor.collider->shape=ColliderShape::Box;scene.apply({floor},{});physics.reconcile(scene);
    floor.collider->shape=ColliderShape::Trimesh;scene.apply({floor},{});physics.reconcile(scene);
    require(physics.colliderCount()==1&&physics.staticMeshes().size()==1&&physics.staticBoxes().empty(),"Native shape switching left duplicate bodies.");
    SceneDocument authored(std::filesystem::temp_directory_path()/"crimson-native-collision-transient");
    require(authored.edit("Native collision Play",[&](auto& records,auto& properties){records={floor,character()};properties={};properties.playerCharacter=2;}),authored.status());
    PlaySession play(authored.projectRoot());require(play.start(authored),"Native Play needed a model lookup: "+play.status());
    Camera camera;for(unsigned frame=0;frame<180;++frame)play.advance(1.0/60,Vec2{},camera,true);
    require(near(play.document().scene().worldTransform(2)->at(1,3),.5),"Native Play collision did not hold the Character.");
    play.stop();
}
void primitiveShapesAndNavigation() {
    for(const auto shape:{ColliderShape::Sphere,ColliderShape::Cylinder,ColliderShape::Capsule}) {
        EntityRecord obstacle;obstacle.id=1;obstacle.name="Primitive";obstacle.collider.emplace();
        obstacle.collider->shape=shape;obstacle.collider->radius=1;obstacle.collider->height=4;
        obstacle.local=math::translation({0,shape==ColliderShape::Sphere?1.f:2.f,0});
        EntityRecord ground;ground.id=3;ground.name="Ground";ground.collider=SceneCollider{{16,.5f,16}};ground.local=math::translation({0,-.25f,0});
        Scene scene;scene.apply({obstacle,ground},{});PhysicsWorld physics;physics.reconcile(scene);
        require(physics.colliderCount()==2&&physics.status().empty(),"Primitive collider did not create a native body: "+physics.status());
        const auto hit=physics.raycast({{0,8,0},{0,-1,0}},12);
        const float top=shape==ColliderShape::Sphere?2.f:4.f;
        require(hit&&hit->entity==1&&near(hit->position[1],top,.005),"Primitive total height or centered origin disagrees with physics.");
        const auto surfaces=physics.staticMeshes();
        require(surfaces.size()==1&&surfaces.front().solid,"Primitive was not published as closed navigation geometry.");
        const auto revision=physics.geometryRevision();const auto geometry=surfaces.front().mesh;
        obstacle.collider->collisionColor={1,0,0,.5f};obstacle.collider->showCollisionInGame=true;
        obstacle.collider->source=AssetRef<ModelAsset>{"assets/models/inactive/manifest.json"};obstacle.collider->size={9,9,9};
        scene.apply({obstacle,ground},{});physics.reconcile(scene);
        require(physics.geometryRevision()==revision&&physics.staticMeshes().front().mesh==geometry,"Inactive/debug fields regenerated primitive geometry.");
        crimson::test::NavigationMeshFixture navigation(crimson::test::navigationTestVolume());require(navigation.reconcile(physics,{}),"Primitive navigation failed to build.");
        const auto route=navigation.findPath({-4,.02f,0},{4,0,0});
        require(route.has_value(),"Primitive obstacle disconnected a navigable surrounding floor: "+navigation.status());
        require(std::any_of(route->corners.begin(),route->corners.end(),[](const auto& corner){return std::abs(corner[2])>.9f;}),"Navigation allowed a route through a solid primitive interior.");
        if(shape==ColliderShape::Cylinder) {
            Scene platform;obstacle.local=math::translation({0,-.5f,0});obstacle.collider->radius=4;obstacle.collider->height=1;
            platform.apply({obstacle},{});PhysicsWorld platformPhysics;platformPhysics.reconcile(platform);crimson::test::NavigationMeshFixture platformNav(crimson::test::navigationTestVolume());platformNav.reconcile(platformPhysics,{});
            require(platformNav.findPath({-2,.02f,0},{2,0,0}).has_value(),"Cylinder top was not usable as a walking surface.");
        }
    }
}
void primitiveTransformsPickingAndDebug() {
    const auto orientation=math::eulerRotationXYZ({.3f,.5f,.2f});
    for(const auto shape:{ColliderShape::Sphere,ColliderShape::Cylinder,ColliderShape::Capsule}) {
        EntityRecord record;record.id=1;record.name="Scaled shape";record.collider.emplace();record.collider->shape=shape;
        record.collider->radius=1;record.collider->height=4;
        record.local=math::translation({3,2,1})*orientation*math::scaling({-2,1,3});
        Scene scene;scene.apply({record},{});PhysicsWorld physics;physics.reconcile(scene);
        require(physics.colliderCount()==1&&physics.status().empty(),"Nonuniform/reflected/rotated primitive was disabled: "+physics.status());
        const auto direction=math::normalized(math::transformDirection(orientation,{1,0,0}));
        const Vec3 center{3,2,1};const Ray ray{center+direction*10,-direction};
        const auto hit=physics.raycast(ray,20);
        require(hit&&math::length(hit->position-(center+direction*2))<.012,"Nonuniform primitive silently averaged or discarded its scale.");
        require(pickCollisionShape(scene,ray,20)==1,"Analytic primitive picking lost a transformed collider.");
        const auto bounds=selectionBounds(scene,std::array<EntityId,1>{1},{}),expected=math::transformBounds(primitiveColliderBounds(*record.collider),record.local);
        require(bounds.valid&&math::length(bounds.minimum-expected.minimum)<1e-4&&math::length(bounds.maximum-expected.maximum)<1e-4,"Selection bounds disagree with primitive dimensions.");
        record.local={};scene.apply({record},{});
        require(!pickCollisionShape(scene,{{.9f,5,.9f},{0,-1,0}},20),"Rounded collider selection used its box instead of its curved interior.");
        require(pickCollisionShape(scene,{{0,0,0},{1,0,0}},20)==1,"Picking inside a primitive did not select it.");
        const auto& debug=colliderCollisionMesh(*record.collider);Aabb drawn;
        for(const auto& point:debug.vertices)math::include(drawn,colliderCollisionPoint(point,*record.collider));
        const auto localBounds=primitiveColliderBounds(*record.collider);
        require(math::length(drawn.minimum-localBounds.minimum)<1e-4&&math::length(drawn.maximum-localBounds.maximum)<1e-4,"Debug fill is not centered or disagrees with primitive bounds.");
        require(&colliderCollisionMesh(*record.collider)==&debug,"Unchanged debug geometry was regenerated.");
        const auto prepared=primitiveColliderMesh(*record.collider,24,5);
        require(prepared->positions.size()<=256,"Nonuniform primitive exceeded Jolt's convex vertex limit.");
        std::map<std::array<std::uint32_t,2>,unsigned> edges;
        for(std::size_t index=0;index<prepared->indices.size();index+=3) {
            const auto a=prepared->positions.at(prepared->indices[index]),b=prepared->positions.at(prepared->indices[index+1]),c=prepared->positions.at(prepared->indices[index+2]);
            require(math::dot(math::cross(b-a,c-a),(a+b+c)/3)>0,"Primitive navigation surface contains an inward/degenerate face.");
            for(unsigned corner=0;corner<3;++corner) {
                const auto x=prepared->indices[index+corner],y=prepared->indices[index+(corner+1)%3];++edges[{std::min(x,y),std::max(x,y)}];
            }
        }
        for(const auto& [edge,count]:edges){(void)edge;require(count==2,"Primitive navigation volume has an open edge.");}
        record.local.at(0,1)=.4f;scene.apply({record},{});physics.reconcile(scene);
        require(physics.colliderCount()==0&&physics.status().find("sheared")!=std::string::npos,"Primitive accepted a shear rejected by the static transform contract.");
    }
}
void shapeSwitchesReplaceOneBody() {
    auto record=collider();Scene scene;PhysicsWorld physics;const auto mesh=plane();const std::array sources{PhysicsMeshCollider{1,mesh,1}};
    for(const auto shape:{ColliderShape::Box,ColliderShape::Sphere,ColliderShape::Cylinder,ColliderShape::Capsule,ColliderShape::Trimesh,ColliderShape::Box}) {
        record.collider->shape=shape;scene.apply({record},{});const auto revision=physics.geometryRevision();physics.reconcile(scene,{},sources);
        require(physics.colliderCount()==1&&physics.geometryRevision()==revision+1,"Changing shape left duplicate/stale bodies or missed geometry invalidation.");
        require(physics.staticBoxes().size()==(shape==ColliderShape::Box?1u:0u),"Shape switch published stale box navigation data.");
        require(scene.find(1)->collider->source==record.collider->source,"Changing primitive erased the retained Trimesh reference.");
    }
}
}
int main(){try{floorNavigationAndAcceptedReplacement();affineMeshTransformsAndRamps();playReadinessAndStaticOwnership();nativeTrianglesWithoutAssets();primitiveShapesAndNavigation();primitiveTransformsPickingAndDebug();shapeSwitchesReplaceOneBody();std::cout<<"Collider shapes, native triangles, affine transforms, picking, debug geometry, accepted navigation and Play ownership checks passed.\n";return 0;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
