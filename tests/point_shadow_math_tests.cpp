#include "crimson-renderer/PointShadowMath.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <set>
#include <stdexcept>

namespace {
using namespace crimson;
void check(bool value,const char* message) {if (!value) throw std::runtime_error(message);}
bool close(double a,double b,double tolerance=2e-5) {return std::abs(a-b)<=tolerance;}
void layoutChecks() {
    using namespace point_shadow;
    for (unsigned resolution:{16u,256u,512u,1024u,2048u}) for (unsigned capacity=1;capacity<=16;++capacity) {
        const auto atlas=atlasLayout(resolution,capacity);
        check(atlas.capacity>=capacity && atlas.capacity<=16,"Atlas rounded capacity is invalid.");
        check(atlas.pitch==resolution+4 && atlas.columns*atlas.rows==atlas.capacity*6,"Atlas packing wasted or lost face tiles.");
        check(atlas.bytes()==std::size_t(atlas.width)*atlas.height*4,"D32F accounting is incorrect.");
        std::set<std::pair<unsigned,unsigned>> occupied;
        for (unsigned slot=0;slot<atlas.capacity;++slot) for (unsigned face=0;face<6;++face) {
            const auto rect=tileRect(atlas,slot,face);
            check(rect.width==atlas.pitch && rect.height==atlas.pitch,"Tile clear must include both guards.");
            check(unsigned(rect.x)+rect.width<=atlas.width && unsigned(rect.y)+rect.height<=atlas.height,"Face escaped atlas.");
            check(occupied.emplace(rect.x,rect.y).second,"Shadow face clears overlap another tile.");
        }
        check(tileRect(atlas,atlas.capacity,0).width==0 && tileRect(atlas,0,6).width==0,"Invalid tiles must be rejected.");
    }
    const auto full=atlasLayout(1024,16);
    check(full.columns==12 && full.rows==8 && full.width==12336 && full.height==8224,"Full atlas must use 12x8 guarded faces.");
    check(full.bytes()==std::size_t(96)*1028*1028*4,"Full atlas memory must include exactly 96 guarded D32F faces.");
    for (auto layout:{atlasLayout(0,1),atlasLayout(1024,0),atlasLayout(1024,17),atlasLayout(65535,16)})
        check(layout.bytes()==0 && layout.capacity==0,"Invalid atlas dimensions were accepted.");
}
void projectionChecks() {
    using namespace point_shadow;
    constexpr Vec3 position{2,3,-4};
    const std::array<Vec3,6> axes{{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}}};
    for (unsigned resolution:{256u,512u,1024u}) for (unsigned face=0;face<6;++face) {
        const auto camera=faceCamera(position,20,face,static_cast<std::uint16_t>(resolution));
        const auto tangent=faceTanHalfFov(resolution);
        check(faceIndex(axes[face])==face && close(camera.nearPlane,.05),"Face axis or near plane changed.");
        const auto right=camera.right(),up=math::cross(right,camera.forward());
        for (float x:{-.999f,-.5f,0.f,.5f,.999f}) for (float y:{-.999f,-.5f,0.f,.5f,.999f}) {
            const Vec3 direction=axes[face]*5+right*(x*5)+up*(y*5);
            check(faceIndex(direction)==face,"Face selection disagrees with the camera basis.");
            const auto canonical=faceCoordinates(direction);
            const Vec2 expected{(canonical[0]-.5f)/tangent+.5f,(canonical[1]-.5f)/tangent+.5f};
            for (auto depth:{ClipDepth::ZeroToOne,ClipDepth::NegativeOneToOne}) {
                const auto projected=math::project(position+direction,camera.projectionMatrix(1,depth)*camera.viewMatrix(),{0,0,1,1},depth);
                check(projected && close((*projected)[0],expected[0]) && close((*projected)[1],expected[1]),"Receiver face UV does not match rendered projection.");
                check(close((*projected)[2],projectedDepth(5,camera.nearPlane,camera.farPlane)),"Receiver depth does not match rendered projection.");
            }
        }
        const auto boundary=math::project(position+axes[face]*5+right*5,camera.projectionMatrix(1,ClipDepth::ZeroToOne)*camera.viewMatrix(),
            {0,0,float(resolution+4),float(resolution+4)},ClipDepth::ZeroToOne);
        check(boundary && close((*boundary)[0],double(resolution)+2,.001),"90-degree seam must have exactly two guard texels.");
        const Aabb front{position+axes[face]*4-Vec3{.1f,.1f,.1f},position+axes[face]*4+Vec3{.1f,.1f,.1f},true};
        const Aabb behind{position-axes[face]*4-Vec3{.1f,.1f,.1f},position-axes[face]*4+Vec3{.1f,.1f,.1f},true};
        check(intersectsFace(front,camera) && !intersectsFace(behind,camera),"Light-face culling rejected a caster or retained the opposite face.");
        check(intersectsFace({},camera),"Missing caster bounds must be conservative.");
    }
    for (float radius:{.01f,.1f,1.f,20.f,100000.f}) {
        const auto near=nearPlane(radius);
        check(near>0 && near<radius && close(near,std::min(.05f,radius*.01f)),"Radius-dependent near plane is invalid.");
        check(close(projectedDepth(near,near,radius),0) && close(projectedDepth(radius,near,radius),1),"Near/far depth endpoints changed.");
    }
    PointLight point;point.radius=100000;
    check(point.inverseRadiusSquared()==1.f/(100000.f*100000.f),"Point shadow range lost the authored maximum.");
    const auto camera=faceCamera(point.position,point.radius,0,512);
    check(camera.farPlane==100000&&close(projectedDepth(100000,camera.nearPlane,camera.farPlane),1),
        "Finite point shadow projection does not end at the authored light radius.");
    point.radius=std::numeric_limits<float>::max();
    check(point.inverseRadiusSquared()==1.f/(100000.f*100000.f),"Point radius sanitization overflowed its maximum.");
    check(faceIndex({1,1,1})==0 && faceIndex({-1,-1,-1})==1 && faceIndex({0,1,1})==2,"Seam tie-breaking must be deterministic.");
    check(faceCoordinates({})==Vec2{.5f,.5f} && math::finite(faceCoordinates({std::numeric_limits<float>::infinity(),0,0})),"Degenerate directions produced invalid UVs.");
    check(std::isnan(projectedDepth(0,.1f,20)) && std::isnan(projectedDepth(1,20,20)),"Invalid depth projections must be explicit.");
    const Aabb unit{{-1,-1,-1},{1,1,1},true};
    check(intersectsLight(unit,{2,0,0},1) && !intersectsLight(unit,{3,0,0},1),"Sphere/AABB range culling is incorrect.");
}
void receiverPlaneChecks() {
    using namespace point_shadow;
    constexpr float near=.05f,far=30;
    bool exposesConstantDepthError=false;
    for (unsigned resolution:{256u,512u,1024u}) for (unsigned face=0;face<6;++face) {
        const auto camera=faceCamera({},far,face,static_cast<std::uint16_t>(resolution));
        const auto forward=camera.forward(),right=camera.right(),up=math::cross(right,forward);
        for (float slopeX:{-10.f,-.7f,0.f,.7f,10.f}) for (float slopeY:{-.7f,0.f,.7f}) {
            const auto normal=math::normalized(-forward+right*slopeX+up*slopeY);
            // Noninteger texel phases exercise the true bilinear footprint.
            const auto point=forward*4+right*.013f+up*.027f;
            const auto gradient=receiverPlaneGradient(point,normal,face,resolution,near,far);
            const auto inverse=receiverPlaneGradient(point,-normal,face,resolution,near,far);
            check(close(gradient[0],inverse[0],1e-9) && close(gradient[1],inverse[1],1e-9),"Receiver-plane correction depends on winding or normal sign.");
            const double axial=math::dot(point,forward),planeDistance=math::dot(normal,point);
            const double coordinateX=math::dot(point,right)/axial,coordinateY=-math::dot(point,up)/axial;
            const double pixelX=2+(coordinateX*.5+.5)*resolution,pixelY=2+(coordinateY*.5+.5)*resolution;
            const double center=projectedDepth(static_cast<float>(axial),near,far);
            const auto intersectedDepth=[&](double x,double y) {
                const auto ray=forward+right*(coordinateX+2*x/resolution)-up*(coordinateY+2*y/resolution);
                const auto distance=planeDistance/math::dot(normal,ray);
                return projectedDepth(static_cast<float>(distance),near,far);
            };
            for (int y=-1;y<=1;++y) for (int x=-1;x<=1;++x) {
                const double tap=center+gradient[0]*x+gradient[1]*y;
                check(close(tap,intersectedDepth(x,y),2e-6),"Analytic receiver gradient disagrees with independent ray-plane intersection.");
                exposesConstantDepthError|=std::abs(center-intersectedDepth(x,y))>1e-4;
                const double lowerX=std::floor(pixelX+x-.5)+.5-pixelX,lowerY=std::floor(pixelY+y-.5)+.5-pixelY;
                const double shallow=center+gradient[0]*(lowerX+(gradient[0]<0?1:0))+gradient[1]*(lowerY+(gradient[1]<0?1:0));
                for (unsigned corner=0;corner<4;++corner)
                    check(shallow<=intersectedDepth(lowerX+(corner&1),lowerY+((corner>>1)&1))+2e-6,
                        "Comparison reference can self-shadow a contributing bilinear texel.");
            }
        }
    }
    check(exposesConstantDepthError,"Plane regression does not expose the old constant-depth PCF error.");
    check(receiverPlaneGradient({1,0,0},{0,1,0},0,256,near,far)==Vec2{},"A grazing receiver produced an unbounded gradient.");
    check(receiverPlaneGradient({0,1,0},{},2,256,near,far)==Vec2{},"A degenerate plane produced an unbounded gradient.");
}
void softShadowGeometryChecks() {
    using namespace point_shadow;
    constexpr float near=.05f,far=20;
    // Readback depth is face-axis distance, whereas the PCSS blocker estimate
    // needs radial distance. Test the inverse against actual camera matrices,
    // including rays that select a different face at each edge/corner.
    for (unsigned face=0;face<6;++face) {
        const auto camera=faceCamera({},far,face,256);
        const auto right=camera.right(),up=math::cross(right,camera.forward());
        for (float x:{-1.f,-.95f,0.f,.95f,1.f}) for (float y:{-1.f,0.f,1.f}) {
            const auto direction=math::normalized(camera.forward()+right*x+up*y);
            for (float distance:{.1f,2.f,3.8f,8.f}) {
                const auto selected=faceCamera({},far,faceIndex(direction),256);
                const auto point=direction*distance;
                const auto projected=math::project(point,selected.projectionMatrix(1,ClipDepth::ZeroToOne)*selected.viewMatrix(),
                    {0,0,1,1},ClipDepth::ZeroToOne);
                check(bool(projected),"Soft-shadow blocker did not project into its selected cube face.");
                const auto decoded=radialDistanceFromDepth((*projected)[2],direction,near,far);
                check(close(decoded,distance,1e-4),"PCSS blocker reconstruction confused radial and face-axis depth.");
                check(close(decoded,radialDistanceFromDepth((*projected)[2],direction*7,near,far),1e-5),
                    "PCSS blocker reconstruction depends on input ray length.");
            }
        }
    }
    check(close(radialDistanceFromDepth(0,{0,0,-1},near,far),near) &&
        close(radialDistanceFromDepth(1,{1,1,1},near,far),far*std::sqrt(3.0),1e-5),
        "PCSS perspective-depth endpoints do not match the face planes.");
    check(std::isnan(radialDistanceFromDepth(.5f,{},near,far)) &&
        std::isnan(radialDistanceFromDepth(-.1f,{1,0,0},near,far)) &&
        std::isnan(radialDistanceFromDepth(.5f,{1,0,0},far,near)),
        "Invalid PCSS blocker depth silently produced a finite occluder.");
    const auto farPenumbra=penumbraRadius(.7f,4,2),contactPenumbra=penumbraRadius(.7f,4,3.8f);
    check(close(farPenumbra,.7) && close(contactPenumbra,.7*.2/3.8),
        "Source radius does not produce the similar-triangle contact-hardening scale.");
    // Independent rays from the two opposite emitter endpoints meet at the
    // blocker and then separate by twice the penumbra at the receiver plane.
    const auto endpoint=[&](double source,double blockerDistance) {
        return source+(0-source)*4/blockerDistance;
    };
    check(close(std::abs(endpoint(.7,2)-endpoint(-.7,2)),2*farPenumbra) &&
        close(std::abs(endpoint(.7,3.8)-endpoint(-.7,3.8)),2*contactPenumbra),
        "Penumbra radius disagrees with independent emitter-to-blocker ray intersections.");
    check(penumbraRadius(0,4,2)==0 && penumbraRadius(.7f,4,4)==0 &&
        penumbraRadius(.7f,4,5)==0 && penumbraRadius(.7f,4,0)==0 &&
        penumbraRadius(std::numeric_limits<float>::infinity(),4,2)==0,
        "Disabled, contact or invalid source sizes created a soft-shadow kernel.");
}
void casterFootprintChecks() {
    using namespace point_shadow;
    const Aabb bounds{{-1,-1,-1},{1,1,1},true};
    const auto radius=std::sqrt(3.0);
    for(unsigned resolution:{256u,512u,1024u}) {
        const auto footprint=casterTexelWorld(bounds,{10,0,0},resolution);
        check(close(footprint,2*(10-radius)/resolution,1e-8),"Caster demand disagrees with the original perspective LOD distance.");
        const auto camera=faceCamera({10,0,0},20,1,static_cast<std::uint16_t>(resolution));
        const auto verticalFov=camera.verticalFov(1)*std::numbers::pi/180;
        check(close(footprint,(10-radius)*2*std::tan(verticalFov*.5)/(resolution+4),1e-8),"Guard pixels changed the inner-face world-space footprint.");
        check(casterTexelWorld(bounds,{0,0,0},resolution)<1e-10f,"A light inside the caster did not request original geometry.");
        check(casterTexelWorld({}, {10,0,0},resolution)<1e-10f,"Missing caster bounds reduced shadow detail.");
    }
    // A child AABB's circumsphere can protrude beyond the parent AABB's
    // circumsphere. Using only the parent sphere would under-request its LOD.
    const Aabb child{{.5f,-1,-1},{1,1,1},true};
    check(casterTexelWorld(child,{10,0,0},1024)<casterTexelWorld(bounds,{10,0,0},1024),
        "The aggregate-demand regression fixture no longer exposes protruding primitive spheres.");
    check(casterTexelWorld(bounds,{10,0,0},0)<1e-10f,"Invalid resolution reduced caster detail.");
}
}
int main() {try {layoutChecks();projectionChecks();receiverPlaneChecks();softShadowGeometryChecks();casterFootprintChecks();std::cout<<"Point-shadow atlas, projection, blocker reconstruction, contact hardening, receiver plane, guard, culling and caster-demand checks passed.\n";return 0;}
    catch (const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}}
