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
    for(unsigned points=0;points<16;++points)for(unsigned spots=1;spots+points<=16;++spots) {
        const auto atlas=atlasLayout(1024,points,4,2,spots);
        check(atlas.width&&atlas.height&&atlas.width<=16384&&atlas.height<=16384,"Mixed Spot atlas exceeded supported extents.");
        std::set<std::pair<unsigned,unsigned>> occupied;
        const auto include=[&](const auto& rect) {
            check(rect.width==1028&&rect.height==1028&&rect.x+rect.width<=atlas.width&&rect.y+rect.height<=atlas.height,
                "Mixed shadow tile escaped guarded allocation.");
            check(occupied.emplace(rect.x,rect.y).second,"Spot projection overlapped a Point or directional tile.");
        };
        for(unsigned p=0;p<atlas.points.capacity;++p)for(unsigned face=0;face<6;++face)include(point_shadow::tileRect(atlas.points,p,face));
        for(unsigned s=0;s<spots;++s)include(spotTileRect(atlas,s));
        for(unsigned c=0;c<2;++c)for(unsigned d=0;d<4;++d)for(unsigned cascade=0;cascade<4;++cascade)include(tileRect(atlas,d,c,cascade));
    }
    const auto singleSpot=atlasLayout(1024,0,0,1,1);
    check(singleSpot.bytes()==std::size_t(1028)*1028*4&&singleSpot.points.capacity==0,
        "One Spot allocated six cube faces instead of one projective tile.");
    check(!atlasLayout(1024,16,0,1,1).width&&!spotTileRect(singleSpot,1).width,"Mixed local shadow admission exceeded sixteen.");
    for(unsigned index=0;index<4;++index) {
        const auto transform=casterTile(index);check(transform[0]==.5f&&transform[1]==.5f,"Grouped caster scale is not one quadrant.");
        check(transform[2]==(index%2?.5f:-.5f)&&transform[3]==(index/2?-.5f:.5f),"Top-left cascade quadrant orientation changed.");
    }
}
void projectionChecks() {
    using namespace directional_shadow;
    DirectionalLight light;light.direction={0,-1,0};light.castShadows=true;
    ShadowCamera camera;camera.camera.position={0,2,5};camera.camera.target={0,2,0};camera.camera.nearPlane=.1f;camera.camera.farPlane=2048;camera.aspect=1.6f;
    const Aabb upstream{{-.5f,100,-.5f},{.5f,101,0},true};
    const auto fits=build(light,camera,1024,std::span(&upstream,1));check(fits.valid,"A valid camera/sun failed cascade fitting.");
    check(fits.projectionCount==4,
        "Scene/Game do not share four independent cascades.");
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
    const auto p=math::transformHomogeneous(fits.values[0].receiverViewProjection,{0,100.5f,-.25f,1});check(p[2]>=0&&p[2]<=1,"Upstream caster was clipped at directional near depth.");
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
void sharedProjectionChecks() {
    using namespace directional_shadow;
    DirectionalLight light;light.direction={-.4f,-1,.25f};light.shadowDistance=65;
    ShadowCamera input;input.camera.position={18,24,19};input.camera.target={0,1,-3};input.camera.nearPlane=.05f;input.camera.farPlane=200;
    input.camera.fovAxis=CameraFovAxis::Vertical;
    for(const auto aspect:{.55f,1.f,16.f/9,3.5f})for(const auto fov:{15.f,45.f,90.f,120.f})
        for(const auto direction:{Vec3{0,-1,0},Vec3{0,1,0},Vec3{1,0,0},Vec3{0,0,1},Vec3{-.3f,-1,.7f}}) {
        input.aspect=aspect;input.camera.fov=fov;light.direction=direction;
        const auto fit=build(light,input,1024);
        check(fit.valid && fit.projectionCount==4 && fit.distance==65,"A valid freely moving camera failed shared CSM fitting.");
        const auto forward=input.camera.forward(),right=input.camera.right(),up=math::normalized(math::cross(right,forward));
        const double tangent=std::tan(input.camera.verticalFov(aspect)*std::numbers::pi/360);
        for(unsigned i=0;i<4;++i) {
            const auto& cascade=fit.values[i];
            const double near=i?cascade.splitNear-(cascade.splitNear-fit.values[i-1].splitNear)*.1:input.camera.nearPlane;
            const double nextTexel=i<3?fit.values[i+1].texelWorld:cascade.texelWorld;
            for(unsigned corner=0;corner<8;++corner) {
                const double depth=corner&4?cascade.splitFar:near;
                const auto point=input.camera.eye()+forward*depth+right*((corner&1?1:-1)*tangent*aspect*depth)+up*((corner&2?1:-1)*tangent*depth);
                covered(point,cascade,"A shared cascade clips a receiver or overlap corner.");
                // Worst-axis normal offset, physical disk halo and complete
                // PCF support must stay inside this map's rendered guard.
                for(const auto axis:{cascade.right,cascade.up})for(const double sign:{-1.,1.}) {
                    const auto query=point+axis*(sign*(fit.maxPenumbra+light.shadowNormalBias*std::max(nextTexel,double(cascade.texelWorld))+3*cascade.texelWorld));
                    const auto clip=math::transformHomogeneous(cascade.projection*cascade.view,{query[0],query[1],query[2],1});
                    check(std::abs(clip[0])<=1.0001 && std::abs(clip[1])<=1.0001,"PCSS halo, normal offset or PCF footprint escaped rendered directional guards.");
                }
            }
        }
        auto rotated=input;rotated.camera.target=rotated.camera.position+Vec3{.3f,-.2f,-.95f}*10;
        const auto turn=build(light,rotated,1024);
        for(unsigned i=0;i<4;++i)check(turn.values[i].radius==fit.values[i].radius,"Camera rotation changed stable cascade scale.");
    }
    light.direction={0,-1,0};light.shadowDistance=100;input={};input.camera.nearPlane=.05f;input.camera.farPlane=500;input.aspect=1.6f;
    const auto fit=build(light,input,1024);
    check(fit.splits[0]<6 && fit.splits[1]<13 && fit.splits[2]<28 && fit.splits[3]==100,
        "FPS PSSM splits lost near-camera sampling density.");
    check(close(fit.maxPenumbra,light.shadowDistance*std::tan(light.shadowAngularDiameter*std::numbers::pi_v<float>/360),1e-5f),
        "Sun angular diameter uses a radius/diameter or metre conversion error.");
    const Aabb high{{-1,1000,-1},{1,1001,1},true};
    const auto upstream=build(light,input,1024,std::span(&high,1));
    for(unsigned i=0;i<4;++i) {
        check(fit.values[i].radius==upstream.values[i].radius && fit.values[i].texelWorld==upstream.values[i].texelWorld,
            "Changing upstream depth changed PCSS halo/cascade scale.");
        sameUvs(fit.values[i],upstream.values[i],"An upstream caster moved stable XY shadow samples.");
        // Decode a physical blocker gap independently under both depth spans.
        for(const double gap:{.01,2.,30.,200.}) {
            const double span=upstream.values[i].farDepth-upstream.values[i].nearDepth;
            const double receiver=.7,blocker=receiver-gap/span;
            const double radius=std::min(double(upstream.maxPenumbra),(receiver-blocker)*span*upstream.tanHalfAngularDiameter);
            check(std::abs(radius-std::min(double(upstream.maxPenumbra),gap*upstream.tanHalfAngularDiameter))<1e-10,
                "Per-cascade normalized depth changed physical penumbra width.");
        }
    }
    const auto offset=Vec3{20000,8000,-17000};auto moved=input;moved.camera.position=moved.camera.position+offset;moved.camera.target=moved.camera.target+offset;
    const auto distant=build(light,moved,1024);
    for(unsigned i=0;i<4;++i)check(distant.values[i].radius==fit.values[i].radius && distant.values[i].right==fit.values[i].right && distant.values[i].up==fit.values[i].up,
        "Large world translation rotated or resized a cascade.");
    light.shadowAngularDiameter=0;const auto hard=build(light,input,1024);
    check(hard.valid && hard.maxPenumbra==0 && hard.tanHalfAngularDiameter==0 && hard.values[0].radius<fit.values[0].radius,
        "Zero sun diameter retained a soft-shadow halo.");
}
void invalidChecks() {
    using namespace directional_shadow;
    DirectionalLight light;ShadowCamera input;input.camera.nearPlane=.1f;input.camera.farPlane=100;
    const auto rejected=[&](DirectionalLight sun,ShadowCamera camera,unsigned resolution=1024) {
        const auto fit=build(sun,camera,resolution);
        check(!fit.valid && fit.projectionCount==0,"Invalid input produced a partial shadow fit.");
    };
    for(const auto resolution:{0u,1u,2u,6u,std::numeric_limits<unsigned>::max()})rejected(light,input,resolution);
    auto changed=light;changed.direction={0,0,0};rejected(changed,input);
    changed=light;changed.direction[0]=std::numeric_limits<float>::quiet_NaN();rejected(changed,input);
    for(const auto distance:{0.f,.01f,std::numeric_limits<float>::infinity()}) {changed=light;changed.shadowDistance=distance;rejected(changed,input);}
    for(const auto diameter:{-1.f,2.01f,std::numeric_limits<float>::quiet_NaN()}) {changed=light;changed.shadowAngularDiameter=diameter;rejected(changed,input);}
    for(const auto value:{0.f,-1.f,std::numeric_limits<float>::quiet_NaN()}) {auto camera=input;camera.camera.nearPlane=value;rejected(light,camera);}
    for(const auto value:{0.f,.01f,std::numeric_limits<float>::infinity()}) {auto camera=input;camera.camera.farPlane=value;rejected(light,camera);}
    for(const auto value:{0.f,-1.f,std::numeric_limits<float>::infinity()}) {auto camera=input;camera.aspect=value;rejected(light,camera);}
    for(const auto value:{0.f,180.f,std::numeric_limits<float>::quiet_NaN()}) {auto camera=input;camera.camera.fov=value;rejected(light,camera);}
    auto camera=input;camera.camera.position[0]=std::numeric_limits<float>::quiet_NaN();rejected(light,camera);
    camera=input;camera.camera.target=camera.camera.position;rejected(light,camera);
    const Aabb invalid{{0,0,0},{std::numeric_limits<float>::infinity(),1,1},true};
    check(build(light,input,1024,std::span(&invalid,1)).valid,"Invalid caster metadata poisoned the stable world projection.");
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
int main(){try{atlasChecks();projectionChecks();sharedProjectionChecks();invalidChecks();lodChecks();std::cout<<"Directional shadow atlas, camera isolation, shared four-cascade PSSM/soft-halo fitting, depth, caster and LOD checks passed.\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
