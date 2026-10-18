#include "DirectionalShadowMath.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace crimson::directional_shadow {
namespace {
bool valid(const Aabb& b){return b.valid && math::finite(b.minimum) && math::finite(b.maximum) && b.minimum[0]<=b.maximum[0] && b.minimum[1]<=b.maximum[1] && b.minimum[2]<=b.maximum[2];}
std::pair<double,double> interval(const Aabb& box,const Vec3& axis) {
    const auto center=(box.minimum+box.maximum)*.5,extent=(box.maximum-box.minimum)*.5;
    const double c=math::dot(center,axis),r=std::abs(axis[0])*extent[0]+std::abs(axis[1])*extent[1]+std::abs(axis[2])*extent[2];
    return {c-r,c+r};
}
}
AtlasLayout atlasLayout(unsigned resolution,unsigned pointCapacity,unsigned directionalCapacity,unsigned cameras) {
    if(!resolution || resolution>65531 || pointCapacity>16 || directionalCapacity>4 || !cameras || cameras>2 ||
        (!pointCapacity&&!directionalCapacity))return {};
    AtlasLayout result;result.points=pointCapacity?point_shadow::atlasLayout(resolution,pointCapacity):point_shadow::AtlasLayout{};
    if(pointCapacity&&!result.points.width)return {};
    unsigned directional=directionalCapacity?1:0;while(directional<directionalCapacity)directional*=2;
    const unsigned pitch=resolution+4,pointColumns=result.points.columns,pointRows=result.points.rows;
    const unsigned blocks=directional*cameras;
    const unsigned targetColumns=blocks?2*static_cast<unsigned>(std::ceil(std::sqrt(double(blocks)))):0;
    const unsigned columns=std::max(pointColumns,targetColumns),blockColumns=std::max(1u,columns/2);
    const unsigned directionalRows=blocks?2*((blocks+blockColumns-1)/blockColumns):0;
    unsigned rows=pointRows+directionalRows;
    if(!columns || pitch>65535/columns || !rows || pitch>65535/rows)return {};
    result.resolution=static_cast<std::uint16_t>(resolution);result.pitch=static_cast<std::uint16_t>(pitch);
    result.width=static_cast<std::uint16_t>(columns*pitch);result.height=static_cast<std::uint16_t>(rows*pitch);
    result.directionalCapacity=static_cast<std::uint16_t>(directional);result.cameraCount=static_cast<std::uint16_t>(cameras);
    result.directionalY=static_cast<std::uint16_t>(pointRows*pitch);result.blockColumns=static_cast<std::uint16_t>(blockColumns);
    return result;
}
point_shadow::TileRect blockRect(const AtlasLayout& a,unsigned light,unsigned camera) {
    if(!a.pitch || light>=a.directionalCapacity || camera>=a.cameraCount)return {};
    const unsigned block=camera*a.directionalCapacity+light;
    return {static_cast<std::uint16_t>((block%a.blockColumns)*2*a.pitch),static_cast<std::uint16_t>(a.directionalY+(block/a.blockColumns)*2*a.pitch),
        static_cast<std::uint16_t>(2*a.pitch),static_cast<std::uint16_t>(2*a.pitch)};
}
point_shadow::TileRect tileRect(const AtlasLayout& a,unsigned light,unsigned camera,unsigned cascade) {
    auto r=blockRect(a,light,camera);if(!r.width)return {};
    const auto columns=static_cast<unsigned>(r.width/a.pitch),rows=static_cast<unsigned>(r.height/a.pitch);
    if(!columns || cascade>=columns*rows)return {};
    r.x=static_cast<std::uint16_t>(r.x+(cascade%columns)*a.pitch);r.y=static_cast<std::uint16_t>(r.y+(cascade/columns)*a.pitch);
    r.width=r.height=a.pitch;return r;
}
Vec4 casterTile(unsigned cascade) {
    if(cascade>=CascadeCount)return {};
    return {.5f,.5f,cascade%2?.5f:-.5f,cascade/2?-.5f:.5f};
}
bool intersects(const Aabb& bounds,const Cascade& cascade) {
    if(!valid(bounds))return true;
    for(const auto& axis:{cascade.right,cascade.up}) {
        const auto [lo,hi]=interval(bounds,axis);const auto c=math::dot(cascade.center,axis);
        const double r=cascade.radius+2*cascade.texelWorld;
        if(hi<c-r || lo>c+r)return false;
    }
    const auto [lo,hi]=interval(bounds,cascade.forward);(void)hi;
    return lo<=cascade.farDepth;
}
Cascades build(const DirectionalLight& light,const ShadowCamera& input,unsigned resolution,std::span<const Aabb> bounds) {
    Cascades result;
    const auto& camera=input.camera;
    if(resolution<=6 || resolution>65531 || !math::finite(light.direction) || math::length(light.direction)<=1e-8 ||
        !std::isfinite(light.shadowDistance) || light.shadowDistance<=0 || !std::isfinite(light.shadowAngularDiameter) ||
        light.shadowAngularDiameter<0 || light.shadowAngularDiameter>2 || !std::isfinite(light.shadowDepthBias) || light.shadowDepthBias<0 ||
        !std::isfinite(light.shadowNormalBias) || light.shadowNormalBias<0 ||
        !std::isfinite(input.aspect) || input.aspect<=0 || !math::finite(camera.position) || !math::finite(camera.target) ||
        !math::finite(camera.up) || math::length(camera.target-camera.position)<=1e-8 || !std::isfinite(camera.fov) ||
        camera.fov<=0 || camera.fov>=180 || !std::isfinite(camera.nearPlane) || camera.nearPlane<=0 || !std::isfinite(camera.farPlane))return result;
    const double nearValue=camera.nearPlane,farValue=std::min(camera.farPlane,light.shadowDistance);
    const double innerMargin=2+light.shadowNormalBias;
    if(farValue<=nearValue || resolution<=2*innerMargin)return result;
    const auto forward=camera.forward(),right=camera.right(),up=math::normalized(math::cross(right,forward));
    const double tangent=std::tan(camera.verticalFov(input.aspect)*std::numbers::pi/360);
    const auto direction=math::normalized(light.direction);
    const auto lightUp=std::abs(direction[1])<.99?Vec3{0,1,0}:Vec3{0,0,1};
    const auto lightRight=math::normalized(math::cross(direction,lightUp)),lightVertical=math::normalized(math::cross(lightRight,direction));
    // One world-space halo for every cascade. Softness deliberately saturates
    // after shadowDistance metres of blocker/receiver separation; distant
    // upstream casters still cast. Unlike a per-map texel cap this preserves
    // the same physical penumbra across splits and a stable projection scale.
    result.tanHalfAngularDiameter=static_cast<float>(std::tan(light.shadowAngularDiameter*std::numbers::pi/360));
    result.maxPenumbra=result.tanHalfAngularDiameter*light.shadowDistance;
    std::array<double,CascadeCount+1> splits;splits[0]=nearValue;
    constexpr double lambda=.8;
    for(unsigned i=1;i<=CascadeCount;++i) {
        const double fraction=double(i)/CascadeCount;
        splits[i]=i==CascadeCount?farValue:lambda*nearValue*std::pow(farValue/nearValue,fraction)+(1-lambda)*(nearValue+(farValue-nearValue)*fraction);
        result.splits[i-1]=static_cast<float>(splits[i]);
    }
    std::array<double,CascadeCount> radii{};
    for(unsigned index=CascadeCount;index-->0;) {
        const double sliceNear=index?splits[index]-(splits[index]-splits[index-1])*.1:splits[0];
        const double halfDepth=(splits[index+1]-sliceNear)*.5;
        const double sphere=std::sqrt(halfDepth*halfDepth+splits[index+1]*splits[index+1]*tangent*tangent*(1+double(input.aspect)*input.aspect));
        const double base=std::max(.001,std::ceil((sphere+result.maxPenumbra)*16)/16);
        radii[index]=base*resolution/(resolution-2*innerMargin);
        // Both samples in an overlap use the same smoothly interpolated world
        // normal offset. Cover the coarser neighbour's maximum offset too.
        if(index+1<CascadeCount)
            radii[index]=std::max(radii[index],(base+light.shadowNormalBias*2*radii[index+1]/resolution)*resolution/(resolution-4));
    }
    for(unsigned index=0;index<CascadeCount;++index) {
        auto& cascade=result.values[index];cascade.right=lightRight;cascade.up=lightVertical;cascade.forward=direction;
        cascade.splitNear=static_cast<float>(splits[index]);cascade.splitFar=static_cast<float>(splits[index+1]);
        const double sliceNear=index?splits[index]-(splits[index]-splits[index-1])*.1:splits[0];
        const double middle=(sliceNear+splits[index+1])*.5;
        // View-local doubles avoid translation-dependent size and basis noise.
        const double radius=radii[index];
        cascade.radius=static_cast<float>(radius);cascade.texelWorld=static_cast<float>(2*radius/resolution);
        const auto center=camera.eye()+forward*middle;
        const double cx=std::round(math::dot(center,lightRight)/cascade.texelWorld)*cascade.texelWorld;
        const double cy=std::round(math::dot(center,lightVertical)/cascade.texelWorld)*cascade.texelWorld;
        const double cz=math::dot(center,direction);
        cascade.center=lightRight*cx+lightVertical*cy+direction*cz;
        double nearDepth=cz-radius,farDepth=cz+radius;
        cascade.farDepth=static_cast<float>(farDepth);
        for(const auto& box:bounds)if(valid(box) && intersects(box,cascade))nearDepth=std::min(nearDepth,interval(box,direction).first);
        const double margin=std::max(.01,double(cascade.texelWorld)*2+light.shadowDepthBias+light.shadowNormalBias*cascade.texelWorld);
        nearDepth-=margin;farDepth+=margin;
        cascade.nearDepth=static_cast<float>(nearDepth);cascade.farDepth=static_cast<float>(farDepth);
        // Explicit light-space rows preserve the basis at large coordinates.
        cascade.view={lightRight[0],lightVertical[0],-direction[0],0,lightRight[1],lightVertical[1],-direction[1],0,
            lightRight[2],lightVertical[2],-direction[2],0,-cx,-cy,nearDepth,1};
        const auto inner=math::orthographic(-radius,radius,-radius,radius,0,farDepth-nearDepth,ClipDepth::ZeroToOne);
        const double guarded=radius+2*cascade.texelWorld;
        cascade.projection=math::orthographic(-guarded,guarded,-guarded,guarded,0,farDepth-nearDepth,ClipDepth::ZeroToOne);
        cascade.receiverViewProjection=inner*cascade.view;
        if(!math::finite(cascade.center)||!math::finite(cascade.view)||!math::finite(cascade.receiverViewProjection)||!math::finite(cascade.projection))return {};
    }
    result.distance=static_cast<float>(farValue);result.projectionCount=CascadeCount;result.valid=true;return result;
}
std::uint32_t chooseLod(std::span<const streaming::ModelLod> levels,const Mat4& world,float texelWorld,float allowedPixels) {
    if(levels.empty() || texelWorld<=std::numeric_limits<float>::epsilon() || !std::isfinite(texelWorld))return 0;
    // Frobenius bound remains conservative for affine shear and reflection.
    double scaleSquared=0;for(unsigned column=0;column<3;++column)for(unsigned row=0;row<3;++row)scaleSquared+=double(world.at(row,column))*world.at(row,column);
    const double scale=std::sqrt(scaleSquared),error=texelWorld*allowedPixels;
    for(std::uint32_t level=static_cast<std::uint32_t>(levels.size());level-->0;)if(levels[level].error*scale<=error)return level;
    return 0;
}
} // namespace crimson::directional_shadow
