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
using Point=std::array<double,3>;
struct Plane {Point normal;double offset;};
double dot(const Point& point,const Vec3& axis) {
    return point[0]*axis[0]+point[1]*axis[1]+point[2]*axis[2];
}
double distance(const Plane& plane,const Point& point) {
    return plane.normal[0]*point[0]+plane.normal[1]*point[1]+plane.normal[2]*point[2]+plane.offset;
}
bool inside(const Point& point,const std::array<Plane,6>& planes,double epsilon) {
    return std::all_of(planes.begin(),planes.end(),[&](const auto& plane){return distance(plane,point)>=-epsilon;});
}
bool clipEdge(const Point& begin,const Point& end,const std::array<Plane,6>& planes,double epsilon,Point& clippedBegin,Point& clippedEnd) {
    double first=0,last=1;
    for(const auto& plane:planes) {
        const auto a=distance(plane,begin)+epsilon,b=distance(plane,end)+epsilon;
        if(a<0 && b<0)return false;
        if(a>=0 && b>=0)continue;
        const auto crossing=a/(a-b);
        if(a<0)first=std::max(first,crossing);else last=std::min(last,crossing);
        if(first>last)return false;
    }
    for(unsigned axis=0;axis<3;++axis) {
        const auto delta=end[axis]-begin[axis];
        clippedBegin[axis]=begin[axis]+delta*first;clippedEnd[axis]=begin[axis]+delta*last;
    }
    return true;
}
struct ReceiverFit {Point minimum{},maximum{};};
std::optional<ReceiverFit> receiverFit(const Camera& camera,double aspect,double nearValue,double farValue,
    const Vec3& lightRight,const Vec3& lightUp,const Vec3& lightDirection,std::span<const Aabb> receivers) {
    if(receivers.empty() || std::any_of(receivers.begin(),receivers.end(),[](const auto& box){return !valid(box);}))return {};
    const auto eye=camera.eye(),forward=camera.forward(),right=camera.right(),up=math::normalized(math::cross(right,forward));
    const double vertical=std::tan(camera.verticalFov(static_cast<float>(aspect))*std::numbers::pi/360),horizontal=vertical*aspect;
    const auto normal=[&](const Vec3& axis,double sign,double depth) {
        return Point{sign*axis[0]+depth*forward[0],sign*axis[1]+depth*forward[1],sign*axis[2]+depth*forward[2]};
    };
    // Camera-relative doubles keep clipping accurate at large world origins.
    const std::array<Plane,6> frustumPlanes{{{normal(forward,1,0),-nearValue},{normal(forward,-1,0),farValue},
        {normal(right,1,horizontal),0},{normal(right,-1,horizontal),0},
        {normal(up,1,vertical),0},{normal(up,-1,vertical),0}}};
    std::array<Point,8> frustumCorners;
    for(unsigned corner=0;corner<8;++corner) {
        const auto depth=corner&4?farValue:nearValue;
        for(unsigned axis=0;axis<3;++axis)frustumCorners[corner][axis]=depth*(forward[axis]+(corner&1?1:-1)*horizontal*right[axis]+(corner&2?1:-1)*vertical*up[axis]);
    }
    ReceiverFit result;result.minimum.fill(std::numeric_limits<double>::infinity());result.maximum.fill(-std::numeric_limits<double>::infinity());
    const Point lightEye{math::dot(eye,lightRight),math::dot(eye,lightUp),math::dot(eye,lightDirection)};
    bool any=false;
    const auto include=[&](const Point& relative) {
        const Point coordinates{dot(relative,lightRight)+lightEye[0],dot(relative,lightUp)+lightEye[1],dot(relative,lightDirection)+lightEye[2]};
        for(unsigned axis=0;axis<3;++axis) {
            result.minimum[axis]=std::min(result.minimum[axis],coordinates[axis]);result.maximum[axis]=std::max(result.maximum[axis],coordinates[axis]);
        }
        any=true;
    };
    const double epsilon=std::max(1.0,farValue)*1e-8;
    for(const auto& box:receivers) {
        Point low,high;
        for(unsigned axis=0;axis<3;++axis){low[axis]=double(box.minimum[axis])-eye[axis];high[axis]=double(box.maximum[axis])-eye[axis];}
        const std::array<Plane,6> boxPlanes{{{{1,0,0},-low[0]},{{-1,0,0},high[0]},{{0,1,0},-low[1]},
            {{0,-1,0},high[1]},{{0,0,1},-low[2]},{{0,0,-1},high[2]}}};
        std::array<Point,8> boxCorners;
        for(unsigned corner=0;corner<8;++corner) {
            boxCorners[corner]={corner&1?high[0]:low[0],corner&2?high[1]:low[1],corner&4?high[2]:low[2]};
            if(inside(boxCorners[corner],frustumPlanes,epsilon))include(boxCorners[corner]);
            if(inside(frustumCorners[corner],boxPlanes,epsilon))include(frustumCorners[corner]);
        }
        // Every vertex of two intersected convex polyhedra is an original
        // contained corner or an edge/face crossing from one of the polyhedra.
        // Clip edges from BOTH shapes; this also handles zero-height floors.
        for(unsigned corner=0;corner<8;++corner)for(unsigned axis=0;axis<3;++axis)if(!(corner&(1u<<axis))) {
            Point a,b;const auto other=corner|(1u<<axis);
            if(clipEdge(frustumCorners[corner],frustumCorners[other],boxPlanes,epsilon,a,b)){include(a);include(b);}
            if(clipEdge(boxCorners[corner],boxCorners[other],frustumPlanes,epsilon,a,b)){include(a);include(b);}
        }
    }
    return any?std::optional<ReceiverFit>{result}:std::nullopt;
}
}
AtlasLayout atlasLayout(unsigned resolution,unsigned pointCapacity,unsigned directionalCapacity,unsigned cameras,bool gameplayContext) {
    if(!resolution || resolution>65531 || pointCapacity>16 || directionalCapacity>4 || !cameras || cameras>2 ||
        (gameplayContext && cameras!=2) || (!pointCapacity&&!directionalCapacity))return {};
    AtlasLayout result;result.points=pointCapacity?point_shadow::atlasLayout(resolution,pointCapacity):point_shadow::AtlasLayout{};
    if(pointCapacity&&!result.points.width)return {};
    unsigned directional=directionalCapacity?1:0;while(directional<directionalCapacity)directional*=2;
    const unsigned pitch=resolution+4,pointColumns=result.points.columns,pointRows=result.points.rows;
    const unsigned blocks=directional*cameras;
    const unsigned targetColumns=blocks?2*static_cast<unsigned>(std::ceil(std::sqrt(double(blocks)))):0;
    const unsigned columns=std::max(pointColumns,targetColumns),blockColumns=std::max(1u,columns/2);
    struct GridBlock {unsigned x{},y{},width{},height{};};
    std::array<std::array<GridBlock,MaxLights>,WorldCameraCount> packed{};
    unsigned directionalRows=blocks?2*((blocks+blockColumns-1)/blockColumns):0;
    if(gameplayContext && directional) {
        // One contiguous row per light/context permits partial clears while
        // allocating six actual projection tiles instead of two four-tile blocks.
        std::array<unsigned,MaxLights*WorldCameraCount> occupied{};
        directionalRows=0;
        for(unsigned context=0;context<cameras;++context)for(unsigned light=0;light<directional;++light) {
            const unsigned width=context?2:4;bool placed=false;
            for(unsigned row=0;row<occupied.size() && !placed;++row)for(unsigned x=0;x+width<=columns;++x) {
                const unsigned mask=((1u<<width)-1)<<x;
                if(occupied[row]&mask)continue;
                occupied[row]|=mask;packed[context][light]={x,row,width,1};
                directionalRows=std::max(directionalRows,row+1);placed=true;break;
            }
            if(!placed)return {};
        }
    }
    unsigned rows=pointRows+directionalRows;
    if(!columns || pitch>65535/columns || !rows || pitch>65535/rows)return {};
    result.resolution=static_cast<std::uint16_t>(resolution);result.pitch=static_cast<std::uint16_t>(pitch);
    result.width=static_cast<std::uint16_t>(columns*pitch);result.height=static_cast<std::uint16_t>(rows*pitch);
    result.directionalCapacity=static_cast<std::uint16_t>(directional);result.cameraCount=static_cast<std::uint16_t>(cameras);
    result.directionalY=static_cast<std::uint16_t>(pointRows*pitch);result.blockColumns=static_cast<std::uint16_t>(blockColumns);
    result.gameplayContext=gameplayContext;
    if(gameplayContext)for(unsigned context=0;context<cameras;++context)for(unsigned light=0;light<directional;++light) {
        const auto& block=packed[context][light];
        result.directionalBlocks[context][light]={static_cast<std::uint16_t>(block.x*pitch),static_cast<std::uint16_t>((pointRows+block.y)*pitch),
            static_cast<std::uint16_t>(block.width*pitch),static_cast<std::uint16_t>(block.height*pitch)};
    }
    return result;
}
point_shadow::TileRect blockRect(const AtlasLayout& a,unsigned light,unsigned camera) {
    if(!a.pitch || light>=a.directionalCapacity || camera>=a.cameraCount)return {};
    if(a.gameplayContext)return a.directionalBlocks[camera][light];
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
Vec4 casterTile(unsigned cascade,unsigned columns,unsigned rows) {
    if(!columns || !rows || columns>4 || rows>4 || cascade>=columns*rows)return {};
    const float x=1.f/columns,y=1.f/rows;
    return {x,y,-1+x*(2*(cascade%columns)+1),1-y*(2*(cascade/columns)+1)};
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
    if(!resolution || !math::finite(light.direction) || !std::isfinite(light.shadowDistance) || light.shadowDistance<=0 || !std::isfinite(input.aspect)||input.aspect<=0)return result;
    const auto& camera=input.camera;const float nearValue=std::max(1e-5f,camera.nearPlane),farValue=std::min(camera.farPlane,light.shadowDistance);
    if(!std::isfinite(nearValue)||!std::isfinite(farValue)||farValue<=nearValue)return result;
    const auto forward=camera.forward(),right=camera.right(),up=math::normalized(math::cross(right,forward));
    const double tangent=std::tan(camera.verticalFov(input.aspect)*std::numbers::pi/360);
    const auto direction=math::normalized(light.direction,{0,-1,0});
    const auto lightUp=std::abs(direction[1])<.99?Vec3{0,1,0}:Vec3{0,0,1};
    const auto lightRight=math::normalized(math::cross(direction,lightUp)),lightVertical=math::normalized(math::cross(lightRight,direction));
    constexpr std::array<float,5> fractions{0,.1f,.3f,.6f,1};
    for(unsigned index=0;index<4;++index) {
        auto& cascade=result.values[index];cascade.right=lightRight;cascade.up=lightVertical;cascade.forward=direction;
        cascade.splitNear=nearValue+(farValue-nearValue)*fractions[index];cascade.splitFar=nearValue+(farValue-nearValue)*fractions[index+1];
        result.splits[index]=cascade.splitFar;
        // Ten percent overlap gives the receiver a continuous split transition.
        const float sliceNear=index?cascade.splitNear-(cascade.splitNear-(nearValue+(farValue-nearValue)*fractions[index-1]))*.1f:cascade.splitNear;
        std::array<Vec3,8> corners;
        Vec3 center{};
        for(unsigned corner=0;corner<8;++corner) {
            const double depth=corner&4?cascade.splitFar:sliceNear;
            corners[corner]=camera.eye()+forward*depth+right*((corner&1?1:-1)*tangent*input.aspect*depth)+up*((corner&2?1:-1)*tangent*depth);
            center=center+corners[corner]*.125;
        }
        double radius=0;for(const auto& p:corners)radius=std::max(radius,math::length(p-center));
        // Quantization also removes translation-dependent float noise in the sphere fit.
        radius=std::max(.001,std::ceil(radius*16)/16);
        if(resolution>2)radius*=double(resolution)/(resolution-2); // Cover the center's half-texel snap conservatively.
        cascade.radius=static_cast<float>(radius);cascade.texelWorld=static_cast<float>(2*radius/resolution);
        const double cx=std::round(math::dot(center,lightRight)/cascade.texelWorld)*cascade.texelWorld;
        const double cy=std::round(math::dot(center,lightVertical)/cascade.texelWorld)*cascade.texelWorld;
        const double cz=math::dot(center,direction);
        cascade.center=lightRight*cx+lightVertical*cy+direction*cz;
        double nearDepth=cz-radius,farDepth=cz+radius;
        cascade.farDepth=static_cast<float>(farDepth);
        for(const auto& b:bounds)if(valid(b)&&intersects(b,cascade))nearDepth=std::min(nearDepth,interval(b,direction).first);
        // Include a small conservative margin; the near plane never slices upstream casters.
        nearDepth-=std::max(.01,double(cascade.texelWorld)*2);farDepth+=std::max(.01,double(cascade.texelWorld)*2);
        cascade.nearDepth=static_cast<float>(nearDepth);cascade.farDepth=static_cast<float>(farDepth);
        const auto eye=lightRight*cx+lightVertical*cy+direction*nearDepth;
        cascade.view=math::lookAt(eye,eye+direction,lightVertical);
        const auto inner=math::orthographic(-radius,radius,-radius,radius,0,farDepth-nearDepth,ClipDepth::ZeroToOne);
        const double guarded=radius+2*cascade.texelWorld;
        cascade.projection=math::orthographic(-guarded,guarded,-guarded,guarded,0,farDepth-nearDepth,ClipDepth::ZeroToOne);
        cascade.receiverViewProjection=inner*cascade.view;
        if(!math::finite(cascade.receiverViewProjection)||!math::finite(cascade.projection))return {};
    }
    result.distance=farValue;result.projectionCount=CascadeCount;result.valid=true;return result;
}
Cascades buildGame(const DirectionalLight& light,const ShadowCamera& input,unsigned resolution,
    std::span<const Aabb> bounds,std::optional<Aabb> focusBounds,std::span<const Aabb> receiverBounds) {
    Cascades result;result.policy=ProjectionPolicy::Gameplay;
    const auto& camera=input.camera;
    if(resolution<=2 || resolution>65531 || !math::finite(light.direction) || math::length(light.direction)<=1e-8 ||
        !std::isfinite(light.shadowDistance) || light.shadowDistance<=0 || !std::isfinite(input.aspect) || input.aspect<=0 ||
        !math::finite(camera.position) || !math::finite(camera.target) || !math::finite(camera.up) ||
        math::length(camera.target-camera.position)<=1e-8 || !std::isfinite(camera.fov) || camera.fov<=0 || camera.fov>=180 ||
        !std::isfinite(camera.nearPlane) || camera.nearPlane<=0 || !std::isfinite(camera.farPlane))return result;
    const double nearValue=camera.nearPlane,farValue=std::min(camera.farPlane,light.shadowDistance);
    if(farValue<=nearValue)return result;
    const auto direction=math::normalized(light.direction,{0,-1,0});
    const auto lightUp=std::abs(direction[1])<.99?Vec3{0,1,0}:Vec3{0,0,1};
    const auto right=math::normalized(math::cross(direction,lightUp)),up=math::normalized(math::cross(right,direction));

    // The radius comes from view-local extents rather than subtracting world
    // corners, so translation cannot change its quantized size through noise.
    const double middle=(nearValue+farValue)*.5,halfDepth=(farValue-nearValue)*.5;
    const double tangent=std::tan(camera.verticalFov(input.aspect)*std::numbers::pi/360);
    const auto center=camera.eye()+camera.forward()*middle;
    const double radius=std::sqrt(halfDepth*halfDepth+farValue*farValue*tangent*tangent*(1+double(input.aspect)*input.aspect));
    const auto coordinates=[&](const Vec3& point){return Point{math::dot(point,right),math::dot(point,up),math::dot(point,direction)};};
    const auto fit=[&](Cascade& output,const Point& origin,double wantedRadius,double minimumDepth,double maximumDepth,
                       std::span<const Aabb> casters) {
        if(!std::all_of(origin.begin(),origin.end(),[](double value){return std::isfinite(value);}) ||
            !std::isfinite(wantedRadius) || !std::isfinite(minimumDepth) || !std::isfinite(maximumDepth))return false;
        const double extent=std::max(.001,std::ceil(wantedRadius*16)/16)*double(resolution)/(resolution-2);
        output.right=right;output.up=up;output.forward=direction;
        output.radius=static_cast<float>(extent);output.texelWorld=static_cast<float>(2*extent/resolution);
        if(!std::isfinite(output.radius) || !std::isfinite(output.texelWorld) || output.texelWorld<=0)return false;
        const double cx=std::round(origin[0]/output.texelWorld)*output.texelWorld;
        const double cy=std::round(origin[1]/output.texelWorld)*output.texelWorld;
        const double cz=origin[2];
        output.center=right*cx+up*cy+direction*cz;
        output.nearDepth=static_cast<float>(minimumDepth);output.farDepth=static_cast<float>(maximumDepth);
        for(const auto& box:casters)if(valid(box) && intersects(box,output))minimumDepth=std::min(minimumDepth,interval(box,direction).first);
        const double margin=std::max(.01,double(output.texelWorld)*2);
        minimumDepth-=margin;maximumDepth+=margin;
        if(maximumDepth<=minimumDepth)return false;
        output.nearDepth=static_cast<float>(minimumDepth);output.farDepth=static_cast<float>(maximumDepth);
        // Direct light-space rows avoid constructing eye+direction at large
        // coordinates, where float rounding could rotate a look-at basis.
        output.view={right[0],up[0],-direction[0],0, right[1],up[1],-direction[1],0,
            right[2],up[2],-direction[2],0, -cx,-cy,minimumDepth,1};
        const auto inner=math::orthographic(-extent,extent,-extent,extent,0,maximumDepth-minimumDepth,ClipDepth::ZeroToOne);
        const double guarded=extent+2*output.texelWorld;
        output.projection=math::orthographic(-guarded,guarded,-guarded,guarded,0,maximumDepth-minimumDepth,ClipDepth::ZeroToOne);
        output.receiverViewProjection=inner*output.view;
        output.splitNear=static_cast<float>(nearValue);output.splitFar=static_cast<float>(farValue);
        return math::finite(output.center) && math::finite(output.view) && math::finite(output.projection) &&
            math::finite(output.receiverViewProjection) && std::isfinite(output.nearDepth) && std::isfinite(output.farDepth);
    };
    const auto depth=math::dot(center,direction);
    if(const auto receivers=receiverFit(camera,input.aspect,nearValue,farValue,right,up,direction,receiverBounds)) {
        Point receiverCenter;
        for(unsigned axis=0;axis<3;++axis)receiverCenter[axis]=(receivers->minimum[axis]+receivers->maximum[axis])*.5;
        const auto receiverRadius=std::max(receivers->maximum[0]-receivers->minimum[0],receivers->maximum[1]-receivers->minimum[1])*.5;
        if(!fit(result.values[0],receiverCenter,receiverRadius,receivers->minimum[2],receivers->maximum[2],bounds))return result;
    } else if(!fit(result.values[0],coordinates(center),radius,depth-radius,depth+radius,bounds))return result;
    result.distance=static_cast<float>(farValue);
    result.splits={result.distance,result.distance,result.distance,result.distance};
    result.projectionCount=1;result.valid=true;
    if(focusBounds && valid(*focusBounds) && intersects(*focusBounds,result.values[0])) {
        const auto cameraRange=interval(*focusBounds,camera.forward());
        const double eyeDepth=math::dot(camera.eye(),camera.forward());
        if(cameraRange.second<eyeDepth+nearValue || cameraRange.first>eyeDepth+farValue)return result;
        const auto focusCenter=(focusBounds->minimum+focusBounds->maximum)*.5;
        const auto x=interval(*focusBounds,right),y=interval(*focusBounds,up),z=interval(*focusBounds,direction);
        const double focusRadius=std::max({2.0,(x.second-x.first)*.5,(y.second-y.first)*.5});
        // World geometry is handled by the independent world projection. This
        // map's near plane needs only its character subtree, while its far plane
        // reaches every downstream receiver covered by the world projection.
        if(fit(result.values[1],coordinates(focusCenter),focusRadius,z.first,std::max(z.second,double(result.values[0].farDepth)),{})) {
            result.projectionCount=2;result.characterProjection=1;
        }
    }
    return result;
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
