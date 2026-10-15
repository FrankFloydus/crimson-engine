#include "crimson-renderer/DirectionalShadowMath.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <set>
#include <stdexcept>

namespace {
using namespace crimson;
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
bool close(float a,float b,float tolerance=1e-5f){return std::abs(a-b)<=tolerance;}
void atlasChecks() {
    using namespace directional_shadow;
    for(unsigned resolution:{256u,512u,1024u})for(unsigned points:{0u,1u,2u,4u,8u,16u})for(unsigned lights=0;lights<=4;++lights)for(unsigned cameras:{1u,2u}) {
        if(!points&&!lights)continue;
        const auto atlas=atlasLayout(resolution,points,lights,cameras);
        check(atlas.width&&atlas.height&&atlas.width<=16384&&atlas.height<=16384,"Supported shadow budget exceeds D3D11 atlas dimensions.");
        check(atlas.bytes()==std::size_t(atlas.width)*atlas.height*4,"Shadow accounting lost atlas padding.");
        std::set<std::pair<unsigned,unsigned>> occupied;
        const auto include=[&](const auto& rect) {
            check(rect.width==resolution+4&&rect.height==resolution+4,"Cascade guards changed point shadow quality.");
            check(rect.x+rect.width<=atlas.width&&rect.y+rect.height<=atlas.height,"Shadow tile escaped its allocation.");
            check(occupied.emplace(rect.x,rect.y).second,"Point/cascade/camera regions overlap.");
        };
        for(unsigned light=0;light<atlas.points.capacity;++light)for(unsigned face=0;face<6;++face)include(point_shadow::tileRect(atlas.points,light,face));
        for(unsigned camera=0;camera<cameras;++camera)for(unsigned light=0;light<atlas.directionalCapacity;++light)for(unsigned cascade=0;cascade<4;++cascade)include(tileRect(atlas,light,camera,cascade));
    }
    check(!atlasLayout(1024,17,1,2).width&&!atlasLayout(1024,16,5,2).width&&!atlasLayout(1024,0,0,2).width,"Invalid atlas budgets were accepted.");
    check(atlasLayout(1024,16,4,2).width==12336&&atlasLayout(1024,16,4,2).height==12336,"Maximum atlas packing unexpectedly grew.");
    for(unsigned index=0;index<4;++index) {
        const auto transform=casterTile(index);check(transform[0]==.5f&&transform[1]==.5f,"Grouped caster scale is not one quadrant.");
        check(transform[2]==(index%2?.5f:-.5f)&&transform[3]==(index/2?-.5f:.5f),"Top-left cascade quadrant orientation changed.");
    }
}
void gameAtlasChecks() {
    using namespace directional_shadow;
    for(unsigned resolution:{256u,512u,1024u})for(unsigned points=0;points<=16;++points)for(unsigned lights=0;lights<=4;++lights) {
        if(!points&&!lights)continue;
        const auto atlas=atlasLayout(resolution,points,lights,2,true),legacy=atlasLayout(resolution,points,lights,2);
        check(atlas.width && atlas.height && atlas.width<=16384 && atlas.height<=16384 && atlas.gameplayContext,
            "A supported compact Scene/Game shadow atlas failed allocation math.");
        check(atlas.bytes()==std::size_t(atlas.width)*atlas.height*sizeof(float) && atlas.bytes()<=legacy.bytes(),
            "Compact directional packing increased allocation or lost guard accounting.");
        std::set<std::pair<unsigned,unsigned>> occupied;
        const auto include=[&](const auto& rect) {
            check(rect.width==resolution+4 && rect.height==resolution+4,"Compact shadow packing changed projection resolution or guards.");
            check(rect.x+rect.width<=atlas.width && rect.y+rect.height<=atlas.height,"A compact directional tile escaped its atlas.");
            check(occupied.emplace(rect.x,rect.y).second,"Point/Scene/Game projection tiles overlap in the compact atlas.");
        };
        for(unsigned light=0;light<atlas.points.capacity;++light)for(unsigned face=0;face<6;++face)include(point_shadow::tileRect(atlas.points,light,face));
        for(unsigned camera=0;camera<2;++camera)for(unsigned light=0;light<atlas.directionalCapacity;++light) {
            const unsigned projections=camera?2:4;const auto block=blockRect(atlas,light,camera);
            check(block.width==projections*atlas.pitch && block.height==atlas.pitch,
                "The active-world clear rectangle is not one contiguous row of its actual projections.");
            for(unsigned projection=0;projection<projections;++projection) {
                const auto rect=tileRect(atlas,light,camera,projection);include(rect);
                check(rect.x==block.x+projection*atlas.pitch && rect.y==block.y,
                    "A projection tile left its light/context clear rectangle.");
            }
            check(!tileRect(atlas,light,camera,projections).width,"An unused compact projection retained an atlas tile.");
        }
    }
    const auto compact=atlasLayout(1024,16,1,2,true),legacy=atlasLayout(1024,16,1,2);
    check(compact.width==legacy.width && compact.height==9*1028 && legacy.height==10*1028 && compact.bytes()*10==legacy.bytes()*9,
        "One directional Scene/Game light did not reduce the full point-shadow atlas allocation by ten percent.");
    const auto maximum=atlasLayout(1024,16,4,2,true);
    check(maximum.width==12336 && maximum.height==10280,"Maximum compact directional packing unexpectedly grew.");
    check(!atlasLayout(1024,0,1,1,true).width && !atlasLayout(1024,16,5,2,true).width,
        "Unsupported compact atlas contexts or capacities passed validation.");
    for(const unsigned columns:{2u,4u})for(unsigned projection=0;projection<columns;++projection) {
        const auto tile=casterTile(projection,columns,1);
        check(close(tile[0],1.f/columns) && tile[1]==1 && close(tile[2],-1.f+(2.f*projection+1)/columns) && tile[3]==0,
            "Horizontal caster projection addressing changed its clip-space orientation.");
    }
    check(casterTile(4,4,1)==Vec4{} && casterTile(0,0,1)==Vec4{},"Invalid caster tile coordinates produced a valid remap.");
}
void dedicatedAtlasChecks() {
    using namespace directional_shadow;
    for(unsigned resolution:{256u,512u,1024u})for(unsigned points:{0u,1u,2u,4u,8u,16u})
        for(unsigned lights=0;lights<=4;++lights)for(unsigned cameras:{1u,2u}) {
        const auto layout=atlasLayout(resolution,points,lights,cameras,cameras==2,true);
        check(layout.width && layout.height && layout.width<=16384 && layout.height<=16384 && layout.characterShadow,
            "Dedicated character map exceeded the supported atlas budget.");
        const auto tile=layout.characterTile;
        check(tile.width==resolution+4 && tile.height==resolution+4 && tile.x+tile.width<=layout.width && tile.y+tile.height<=layout.height,
            "Dedicated character tile escaped the guarded allocation.");
        const auto overlaps=[&](const point_shadow::TileRect& other) {
            return tile.x<other.x+other.width && other.x<tile.x+tile.width && tile.y<other.y+other.height && other.y<tile.y+tile.height;
        };
        for(unsigned point=0;point<layout.points.capacity;++point)for(unsigned face=0;face<6;++face)
            check(!overlaps(point_shadow::tileRect(layout.points,point,face)),"Character tile overlaps an ordinary point map.");
        for(unsigned camera=0;camera<cameras;++camera)for(unsigned light=0;light<layout.directionalCapacity;++light)
            check(!overlaps(blockRect(layout,light,camera)),"Character tile overlaps a directional block.");
        check(layout.bytes()==std::size_t(layout.width)*layout.height*4,"Dedicated atlas accounting omitted rectangular padding.");
    }
    const auto only=atlasLayout(1024,0,0,2,true,true);
    check(only.width==1028 && only.height==1028 && only.points.capacity==0 && only.directionalCapacity==0,
        "Character shadow consumed ordinary light slots or redundant camera tiles.");
}
void dedicatedProjectionChecks() {
    using namespace directional_shadow;
    CharacterShadowSource source;source.enabled=true;source.strength=.8f;source.distance=20;source.direction={-.5f,-1,.25f};
    Aabb caster{{-.4f,0,-.3f},{.4f,2,.3f},true};
    const auto fit=buildCharacter(source,caster,1024);check(bool(fit),"Finite player shadow input failed fitting.");
    for(unsigned corner=0;corner<8;++corner) {
        const Vec3 point{corner&1?caster.maximum[0]:caster.minimum[0],corner&2?caster.maximum[1]:caster.minimum[1],corner&4?caster.maximum[2]:caster.minimum[2]};
        const auto assertCovered=[&](const Vec3& p) {
            const auto clip=math::transformHomogeneous(fit->receiverViewProjection,{p[0],p[1],p[2],1});
            check(math::finite(clip) && std::abs(clip[0])<=1.0001f && std::abs(clip[1])<=1.0001f && clip[2]>=0 && clip[2]<=1,
                "Dedicated fit clipped the player or its downstream receiver reach.");
        };
        assertCovered(point);assertCovered(point+fit->forward*source.distance);
    }
    auto translated=caster;translated.minimum=translated.minimum+Vec3{1,0,0};translated.maximum=translated.maximum+Vec3{1,0,0};
    const auto moved=buildCharacter(source,translated,1024);
    check(moved && moved->receiverViewProjection!=fit->receiverViewProjection && close(moved->radius,fit->radius),
        "Character map failed to follow player geometry at stable scale.");
    const auto longRange=source.distance;source.distance*=2;
    const auto farther=buildCharacter(source,caster,1024);
    check(farther && farther->radius==fit->radius && farther->farDepth>fit->farDepth+longRange*.99f,
        "Changing downstream shadow reach degraded orthographic texel density.");
    source.enabled=false;check(!buildCharacter(source,caster,1024),"Disabled character shadow retained a projection.");source.enabled=true;
    check(!buildCharacter(source,{},1024)&&!buildCharacter(source,caster,2),"Invalid dedicated shadow bounds/resolution were accepted.");
    source.direction={};check(!buildCharacter(source,caster,1024),"Zero character shadow direction was accepted.");
}
void projectionChecks() {
    using namespace directional_shadow;
    DirectionalLight light;light.direction={0,-1,0};light.castShadows=true;
    ShadowCamera camera;camera.camera.position={0,2,5};camera.camera.target={0,2,0};camera.camera.nearPlane=.1f;camera.camera.farPlane=2048;camera.aspect=1.6f;
    const Aabb upstream{{-.5f,100,-5},{.5f,101,-4},true};
    const auto fits=build(light,camera,1024,std::span(&upstream,1));check(fits.valid,"A valid camera/sun failed cascade fitting.");
    check(fits.projectionCount==4 && fits.policy==ProjectionPolicy::Cascaded && fits.characterProjection==NoCharacterProjection,
        "The legacy Scene/thumbnail fit no longer has four independent cascades.");
    float previous=camera.camera.nearPlane;
    for(unsigned index=0;index<4;++index) {
        const auto& cascade=fits.values[index];check(cascade.splitFar>previous&&cascade.splitFar<=100,"Cascade splits are not increasing or distance-clamped.");previous=cascade.splitFar;
        check(cascade.texelWorld>0&&math::finite(cascade.receiverViewProjection),"Cascade fit contains invalid projection values.");
        const auto near=cascade.center+cascade.forward*(cascade.nearDepth-static_cast<float>(math::dot(cascade.center,cascade.forward)));
        const auto far=cascade.center+cascade.forward*(cascade.farDepth-static_cast<float>(math::dot(cascade.center,cascade.forward)));
        check(close(math::transformHomogeneous(cascade.receiverViewProjection,{near[0],near[1],near[2],1})[2],0,2e-5f),"Orthographic near depth is not zero.");
        check(close(math::transformHomogeneous(cascade.receiverViewProjection,{far[0],far[1],far[2],1})[2],1,2e-5f),"Orthographic far depth is not one.");
    }
    check(intersects(upstream,fits.values[0]),"An offscreen upstream caster was culled.");
    const auto p=math::transformHomogeneous(fits.values[0].receiverViewProjection,{0,100.5f,-4.5f,1});check(p[2]>=0&&p[2]<=1,"Upstream caster was clipped at directional near depth.");
    check(!intersects(Aabb{{-1,-10000,-5},{1,-9999,-4},true},fits.values[0]),"Downstream geometry cannot shadow receivers but survived culling.");
    check(intersects({},fits.values[0]),"Missing bounds must remain conservative.");
    auto moved=camera;moved.camera.position=moved.camera.position+fits.values[0].right*(fits.values[0].texelWorld*.1f);moved.camera.target=moved.camera.target+fits.values[0].right*(fits.values[0].texelWorld*.1f);
    const auto next=build(light,moved,1024,std::span(&upstream,1));
    for(unsigned column=0;column<4;++column)for(unsigned row=0;row<2;++row)check(close(next.values[0].receiverViewProjection.at(row,column),fits.values[0].receiverViewProjection.at(row,column)),"Subtexel camera motion changed stable cascade UVs.");
    for(const auto direction:{Vec3{0,1,0},Vec3{0,0,1},Vec3{1,0,0},Vec3{-.3f,-1,.7f}}){light.direction=direction;check(build(light,camera,256).valid,"A light parallel to an up axis failed fitting.");}
    light.shadowDistance=std::numeric_limits<float>::infinity();check(!build(light,camera,1024).valid,"Nonfinite shadow distance was accepted.");
    light.shadowDistance=.01f;check(!build(light,camera,1024).valid,"Shadow distance before camera near was accepted.");
}
void covered(const Vec3& point,const directional_shadow::Cascade& fit,const char* message,float tolerance=3e-4f) {
    const auto clip=math::transformHomogeneous(fit.receiverViewProjection,{point[0],point[1],point[2],1});
    check(math::finite(clip) && std::abs(clip[3]-1)<1e-5f && std::abs(clip[0])<=1+tolerance && std::abs(clip[1])<=1+tolerance &&
        clip[2]>=-tolerance && clip[2]<=1+tolerance,message);
}
void sameUvs(const directional_shadow::Cascade& a,const directional_shadow::Cascade& b,const char* message) {
    for(unsigned column=0;column<4;++column)for(unsigned row=0;row<2;++row)
        check(close(a.receiverViewProjection.at(row,column),b.receiverViewProjection.at(row,column),1e-6f),message);
}
void gameProjectionChecks() {
    using namespace directional_shadow;
    DirectionalLight light;light.direction={-.4f,-1,.25f};light.shadowDistance=65;
    ShadowCamera input;input.camera.position={18,24,19};input.camera.target={0,1,-3};input.camera.nearPlane=.1f;input.camera.farPlane=200;
    input.camera.fovAxis=CameraFovAxis::Vertical;
    for(const auto aspect:{.55f,1.f,16.f/9,3.5f})for(const auto fov:{15.f,45.f,90.f,120.f})
        for(const auto direction:{Vec3{0,-1,0},Vec3{0,1,0},Vec3{1,0,0},Vec3{0,0,1},Vec3{-.3f,-1,.7f}}) {
        input.aspect=aspect;input.camera.fov=fov;light.direction=direction;
        const auto fit=buildGame(light,input,1024);
        check(fit.valid && fit.projectionCount==1 && fit.characterProjection==NoCharacterProjection && fit.policy==ProjectionPolicy::Gameplay,
            "A valid Game camera/light did not produce exactly one world projection.");
        check(fit.distance==65 && fit.splits==Vec4{65,65,65,65} && fit.values[0].splitNear==input.camera.nearPlane && fit.values[0].splitFar==65,
            "The gameplay fit introduced cascade splits or lost its bounded distance.");
        const auto forward=input.camera.forward(),right=input.camera.right(),up=math::normalized(math::cross(right,forward));
        const double tangent=std::tan(input.camera.verticalFov(aspect)*std::numbers::pi/360);
        for(unsigned corner=0;corner<8;++corner) {
            const double depth=corner&4?65:input.camera.nearPlane;
            const auto point=input.camera.eye()+forward*depth+right*((corner&1?1:-1)*tangent*aspect*depth)+up*((corner&2?1:-1)*tangent*depth);
            covered(point,fit.values[0],"The single gameplay projection clips a bounded camera-frustum corner.");
        }
    }
    // The shorter-axis editor convention remains a valid camera input too.
    input.camera.fovAxis=CameraFovAxis::ShorterDimension;input.camera.fov=45;input.aspect=.6f;
    check(buildGame(light,input,256).valid,"Shorter-axis FOV failed the bounded Game fit.");
    input.camera.farPlane=30;
    check(buildGame(light,input,256).distance==30,"The Game shadow fit extended beyond the camera far plane.");

    light.direction={0,-1,0};light.shadowDistance=100;
    input.camera={};input.camera.position={0,2,5};input.camera.target={0,2,0};input.camera.nearPlane=.1f;
    input.camera.farPlane=2048;input.camera.fov=45;input.camera.fovAxis=CameraFovAxis::Vertical;input.aspect=1.6f;
    const Aabb upstream{{-.5f,100,-5},{.5f,101,-4},true};
    const auto fit=buildGame(light,input,1024,std::span(&upstream,1));
    check(fit.valid && intersects(upstream,fit.values[0]),"Game fitting culled an offscreen upstream shadow caster.");
    for(unsigned corner=0;corner<8;++corner)covered({corner&1?.5f:-.5f,corner&2?101.f:100.f,corner&4?-4.f:-5.f},fit.values[0],
        "The Game near plane clips an upstream caster.");
    check(!intersects(Aabb{{-1,-10000,-5},{1,-9999,-4},true},fit.values[0]),"A downstream object outside receiver depth survived Game caster culling.");
    check(intersects({},fit.values[0]),"Missing caster bounds stopped being conservative for gameplay shadows.");
    auto moved=input;
    const auto offset=fit.values[0].right*(fit.values[0].texelWorld*.1f);
    moved.camera.position=moved.camera.position+offset;moved.camera.target=moved.camera.target+offset;
    const auto stable=buildGame(light,moved,1024,std::span(&upstream,1));
    check(stable.values[0].radius==fit.values[0].radius && stable.values[0].texelWorld==fit.values[0].texelWorld,
        "Camera translation changed the quantized Game projection size.");
    sameUvs(fit.values[0],stable.values[0],"Subtexel camera motion changed stabilized Game shadow UVs.");

    // Large translations use explicit light-space view rows rather than a
    // float look-at eye+direction subtraction that can alter the basis.
    const auto largeOffset=Vec3{20000,8000,-17000};
    moved=input;moved.camera.position=moved.camera.position+largeOffset;moved.camera.target=moved.camera.target+largeOffset;
    const auto distant=buildGame(light,moved,1024);
    check(distant.valid && distant.values[0].radius==fit.values[0].radius && distant.values[0].right==fit.values[0].right &&
        distant.values[0].up==fit.values[0].up,"World translation rotated or resized the stable gameplay projection.");
}
void characterProjectionChecks() {
    using namespace directional_shadow;
    DirectionalLight light;light.direction={-.4f,-1,.25f};light.shadowDistance=80;
    ShadowCamera input;input.camera.position={10,12,10};input.camera.target={0,1,0};input.camera.nearPlane=.1f;
    input.camera.farPlane=200;input.camera.fov=35;input.camera.fovAxis=CameraFovAxis::Vertical;input.aspect=16.f/9;
    const Aabb character{{-.4f,0,-.3f},{.4f,1.8f,.3f},true};
    const auto fit=buildGame(light,input,1024,{},character);
    check(fit.valid && fit.projectionCount==2 && fit.characterProjection==1 && fit.policy==ProjectionPolicy::Gameplay,
        "A visible valid character did not add its independent shadow projection.");
    check(fit.values[1].radius>=2 && fit.values[1].texelWorld<fit.values[0].texelWorld,
        "Character focus did not retain a sane minimum footprint and finer texels.");
    check(fit.values[1].farDepth>=fit.values[0].farDepth,
        "Character focus cannot reach the world projection's downstream receivers.");
    for(unsigned corner=0;corner<8;++corner) {
        const auto point=Vec3{corner&1?character.maximum[0]:character.minimum[0],corner&2?character.maximum[1]:character.minimum[1],
            corner&4?character.maximum[2]:character.minimum[2]};
        covered(point,fit.values[1],"Character projection clips its character subtree bounds.");
        const auto downstream=point+fit.values[1].forward*std::max(0.0,.95*(double(fit.values[0].farDepth)-math::dot(point,fit.values[1].forward)));
        covered(downstream,fit.values[1],"Character projection clips the character shadow on a downstream world receiver.");
        const auto from=math::transformHomogeneous(fit.values[1].receiverViewProjection,{point[0],point[1],point[2],1});
        const auto to=math::transformHomogeneous(fit.values[1].receiverViewProjection,{downstream[0],downstream[1],downstream[2],1});
        check(close(from[0],to[0],2e-5f) && close(from[1],to[1],2e-5f),"A character shadow changed its light-space footprint along the casting direction.");
    }
    const auto absent=buildGame(light,input,1024);
    check(absent.valid && absent.projectionCount==1 && absent.characterProjection==NoCharacterProjection,
        "A missing character left a phantom focus projection.");
    for(const auto invalid:{Aabb{},Aabb{{1,1,1},{0,0,0},true},Aabb{{0,0,0},{std::numeric_limits<float>::infinity(),1,1},true},
        Aabb{{0,0,0},{1,std::numeric_limits<float>::quiet_NaN(),1},true}}) {
        const auto ignored=buildGame(light,input,1024,{},invalid);
        check(ignored.valid && ignored.projectionCount==1 && ignored.characterProjection==NoCharacterProjection,
            "Invalid character bounds invalidated the world projection or allocated a focus map.");
    }
    const auto behind=input.camera.eye()-input.camera.forward()*20;
    const auto beyond=input.camera.eye()+input.camera.forward()*200;
    for(const auto origin:{behind,beyond}) {
        const auto ignored=buildGame(light,input,1024,{},Aabb{origin-Vec3{1,1,1},origin+Vec3{1,1,1},true});
        check(ignored.valid && ignored.projectionCount==1,"An out-of-range character allocated a gameplay focus projection.");
    }

    light.direction={0,-1,0};
    const Aabb upstream{{-.5f,100,-.5f},{.5f,101,.5f},true};
    const auto separate=buildGame(light,input,1024,std::span(&upstream,1),character);
    check(separate.valid && separate.projectionCount==2 && separate.values[0].nearDepth<-100 && separate.values[1].nearDepth>-3,
        "World upstream casters expanded the independent character-only near depth.");
    auto movedCharacter=character;const auto offset=separate.values[1].right*(separate.values[1].texelWorld*.1f);
    movedCharacter.minimum=movedCharacter.minimum+offset;movedCharacter.maximum=movedCharacter.maximum+offset;
    const auto stable=buildGame(light,input,1024,std::span(&upstream,1),movedCharacter);
    sameUvs(separate.values[1],stable.values[1],"Subtexel character motion changed its stabilized shadow UVs.");
}
void receiverAwareGameChecks() {
    using namespace directional_shadow;
    DirectionalLight light;light.direction={-.4f,-1,.25f};light.shadowDistance=100;
    ShadowCamera input;input.camera.position={0,8,10};input.camera.target={};input.camera.nearPlane=.1f;
    input.camera.farPlane=200;input.camera.fov=45;input.camera.fovAxis=CameraFovAxis::Vertical;input.aspect=16.f/9;
    const Aabb floor{{-4,0,-4},{4,0,4},true};
    for(const unsigned resolution:{256u,1024u}) {
        const auto legacy=buildGame(light,input,resolution);
        const auto cropped=buildGame(light,input,resolution,{}, {},std::span(&floor,1));
        check(cropped.valid && cropped.projectionCount==1 && cropped.values[0].texelWorld<legacy.values[0].texelWorld*.2f,
            "A small visible floor retained the texel density of the unused hundred-metre frustum.");
        for(unsigned corner=0;corner<4;++corner)covered({corner&1?4.f:-4.f,0,corner&2?4.f:-4.f},cropped.values[0],
            "Receiver cropping clipped a visible flat-floor corner.");
        const auto upstreamCenter=-math::normalized(light.direction)*100;
        const Aabb upstream{upstreamCenter-Vec3{.5f,.5f,.5f},upstreamCenter+Vec3{.5f,.5f,.5f},true};
        const auto withCaster=buildGame(light,input,resolution,std::span(&upstream,1),{},std::span(&floor,1));
        sameUvs(cropped.values[0],withCaster.values[0],"An upstream caster expanded the cropped receiver XY footprint.");
        check(withCaster.values[0].nearDepth<cropped.values[0].nearDepth-50,"Receiver cropping removed upstream caster depth coverage.");
        for(unsigned corner=0;corner<8;++corner)covered({corner&1?upstream.maximum[0]:upstream.minimum[0],
            corner&2?upstream.maximum[1]:upstream.minimum[1],corner&4?upstream.maximum[2]:upstream.minimum[2]},withCaster.values[0],
            "Cropped world projection clips an offscreen upstream caster.");
        const std::array<Aabb,2> withDistant{{floor,{{1000,0,1000},{1004,5,1004},true}}};
        const auto distant=buildGame(light,input,resolution,{}, {},withDistant);
        check(distant.values[0].receiverViewProjection==cropped.values[0].receiverViewProjection,
            "A receiver outside the bounded camera frustum reduced shadow density.");
        const auto noIntersection=buildGame(light,input,resolution,{}, {},std::span(&withDistant[1],1));
        check(noIntersection.values[0].receiverViewProjection==legacy.values[0].receiverViewProjection,
            "An empty receiver intersection did not retain conservative full-frustum coverage.");
        for(const auto unknown:{Aabb{},Aabb{{1,1,1},{0,0,0},true},
            Aabb{{0,0,0},{std::numeric_limits<float>::infinity(),1,1},true},
            Aabb{{0,0,0},{1,std::numeric_limits<float>::quiet_NaN(),1},true}}) {
            const std::array<Aabb,2> incomplete{{floor,unknown}};
            const auto fallback=buildGame(light,input,resolution,{}, {},incomplete);
            check(fallback.valid && fallback.values[0].receiverViewProjection==legacy.values[0].receiverViewProjection,
                "Unknown receiver metadata silently cropped potentially visible shadow receivers.");
        }
        auto shifted=input;const auto offset=cropped.values[0].right*(cropped.values[0].texelWorld*.1f);
        shifted.camera.position=shifted.camera.position+offset;shifted.camera.target=shifted.camera.target+offset;
        const auto stable=buildGame(light,shifted,resolution,{}, {},std::span(&floor,1));
        check(stable.values[0].radius==cropped.values[0].radius,"Subtexel camera motion resized a fully visible receiver envelope.");
        sameUvs(cropped.values[0],stable.values[0],"Subtexel camera motion moved a stabilized cropped shadow projection.");
    }

    // A huge zero-height floor has no original corner inside the camera and
    // contains no original frustum corner. Its intersection comes from edges.
    const Aabb broadFloor{{-1000,0,-1000},{1000,0,1000},true};
    unsigned checked{};
    auto boundedCamera=input.camera;boundedCamera.farPlane=light.shadowDistance;
    const auto viewProjection=boundedCamera.projectionMatrix(input.aspect,ClipDepth::ZeroToOne)*boundedCamera.viewMatrix();
    for(const auto direction:{Vec3{0,-1,0},Vec3{-.4f,-1,.25f},Vec3{-.97f,-.2f,.1f}})for(const auto resolution:{256u,1024u}) {
        light.direction=direction;
        const auto cropped=buildGame(light,input,resolution,{}, {},std::span(&broadFloor,1));
        check(cropped.valid,"Frustum/flat-floor edge clipping failed.");
        for(int x=-40;x<=40;x+=2)for(int z=-40;z<=40;z+=2) {
            const Vec3 point{float(x),0,float(z)};
            const auto clip=math::transformHomogeneous(viewProjection,{point[0],point[1],point[2],1});
            if(clip[3]<=0 || std::abs(clip[0])>clip[3] || std::abs(clip[1])>clip[3] || clip[2]<0 || clip[2]>clip[3])continue;
            covered(point,cropped.values[0],"A sampled visible receiver lost shadow coverage after cropping.");++checked;
        }
        // Conversely, AABB-edge/frustum clipping must handle a thin line whose
        // endpoints both lie outside the camera, not only frustum-edge hits.
        const Aabb line{{-100,0,0},{100,0,0},true};
        const auto thin=buildGame(light,input,resolution,{}, {},std::span(&line,1));
        for(int x=-8;x<=8;++x)covered({float(x),0,0},thin.values[0],"A thin receiver crossing the frustum was lost during edge clipping.");
    }
    check(checked>200,"Receiver intersection oracle covered too few visible floor samples.");

    light.direction={-.4f,-1,.25f};
    const Aabb room{{-4,0,-4},{4,4,4},true},character{{-.4f,0,-.3f},{.4f,1.8f,.3f},true};
    const auto focused=buildGame(light,input,1024,std::span(&room,1),character,std::span(&room,1));
    check(focused.valid && focused.projectionCount==2 && focused.characterProjection==1,
        "Receiver-aware world fitting removed a visible focused character map.");
    for(unsigned corner=0;corner<8;++corner)covered({corner&1?character.maximum[0]:character.minimum[0],
        corner&2?character.maximum[1]:character.minimum[1],corner&4?character.maximum[2]:character.minimum[2]},focused.values[1],
        "Receiver cropping clipped the separate character projection.");
    auto movedCharacter=character;movedCharacter.minimum=movedCharacter.minimum+Vec3{.8f,0,.2f};movedCharacter.maximum=movedCharacter.maximum+Vec3{.8f,0,.2f};
    const auto movedFocus=buildGame(light,input,1024,std::span(&room,1),movedCharacter,std::span(&room,1));
    check(movedFocus.valid && movedFocus.projectionCount==2 &&
        movedFocus.values[0].receiverViewProjection==focused.values[0].receiverViewProjection,
        "Moving focus inside a fixed receiver envelope invalidated the stationary world projection.");
    check(movedFocus.values[1].receiverViewProjection!=focused.values[1].receiverViewProjection,
        "The focused projection did not follow the character independently of world coverage.");
}
void gameInvalidChecks() {
    using namespace directional_shadow;
    DirectionalLight light;ShadowCamera input;input.camera.nearPlane=.1f;input.camera.farPlane=100;
    const auto rejected=[&](DirectionalLight sun,ShadowCamera camera,unsigned resolution=1024) {
        const auto fit=buildGame(sun,camera,resolution);
        check(!fit.valid && fit.projectionCount==0 && fit.characterProjection==NoCharacterProjection,
            "Invalid gameplay input produced a partial shadow fit.");
    };
    for(const auto resolution:{0u,1u,2u,std::numeric_limits<unsigned>::max()})rejected(light,input,resolution);
    auto changed=light;changed.direction={0,0,0};rejected(changed,input);
    changed=light;changed.direction[0]=std::numeric_limits<float>::quiet_NaN();rejected(changed,input);
    for(const auto distance:{0.f,.01f,std::numeric_limits<float>::infinity()}) {changed=light;changed.shadowDistance=distance;rejected(changed,input);}
    for(const auto value:{0.f,-1.f,std::numeric_limits<float>::quiet_NaN()}) {auto camera=input;camera.camera.nearPlane=value;rejected(light,camera);}
    for(const auto value:{0.f,.01f,std::numeric_limits<float>::infinity()}) {auto camera=input;camera.camera.farPlane=value;rejected(light,camera);}
    for(const auto value:{0.f,-1.f,std::numeric_limits<float>::infinity()}) {auto camera=input;camera.aspect=value;rejected(light,camera);}
    for(const auto value:{0.f,180.f,std::numeric_limits<float>::quiet_NaN()}) {auto camera=input;camera.camera.fov=value;rejected(light,camera);}
    auto camera=input;camera.camera.position[0]=std::numeric_limits<float>::quiet_NaN();rejected(light,camera);
    camera=input;camera.camera.target=camera.camera.position;rejected(light,camera);
    const Aabb invalid{{0,0,0},{std::numeric_limits<float>::infinity(),1,1},true};
    check(buildGame(light,input,1024,std::span(&invalid,1)).valid,"Invalid caster metadata poisoned the bounded world projection.");
}
void lodChecks() {
    std::array<streaming::ModelLod,3> levels;levels[0].error=0;levels[1].error=.01f;levels[2].error=.1f;
    check(directional_shadow::chooseLod(levels,{},.02f)==1,"Orthographic LOD used a perspective distance.");
    check(directional_shadow::chooseLod(levels,math::scaling({10,1,1}),.02f)==0,"Affine scale failed to refine directional geometry.");
    check(directional_shadow::chooseLod(levels,math::scaling({-1,1,1}),.02f)==1,"Mirroring changed orthographic error magnitude.");
    levels[1].error=levels[2].error=0;
    check(directional_shadow::chooseLod(levels,{},1e-20f)==0,"A light within a caster must retain original geometry even when simplified levels report zero error.");
}
}
int main(){try{atlasChecks();gameAtlasChecks();dedicatedAtlasChecks();dedicatedProjectionChecks();projectionChecks();gameProjectionChecks();characterProjectionChecks();receiverAwareGameChecks();gameInvalidChecks();lodChecks();std::cout<<"Directional shadow atlas, camera isolation, cascaded/gameplay/focused/receiver fitting, depth, caster and LOD checks passed.\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
