#include "DecalModelSurface.h"
#include "DecalSampling.h"
#include <crimson/renderer/forward/ModelData.h>
#include <crimson/renderer/forward/MeshNormals.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace crimson::decals {
namespace {
void cancelled(std::stop_token stop) { if(stop.stop_requested())throw std::runtime_error("Decal surface preparation cancelled."); }
bool overlaps(const Aabb& a,const Aabb& b) {
    if(!a.valid||!b.valid)return false;
    for(unsigned axis=0;axis<3;++axis)if(a.maximum[axis]<b.minimum[axis]||a.minimum[axis]>b.maximum[axis])return false;
    return true;
}
Aabb triangleBounds(const SurfaceTriangle& triangle) {
    Aabb result;for(const auto p:triangle.positions)math::include(result,p);return result;
}
uint32_t buildNode(ModelDrawSurface& draw,uint32_t first,uint32_t count,std::stop_token stop) {
    cancelled(stop);
    const auto index=static_cast<uint32_t>(draw.nodes.size());
    draw.nodes.emplace_back();Aabb bounds,centres;
    for(uint32_t i=first;i<first+count;++i) {
        const auto box=triangleBounds(draw.surface.triangles[draw.order[i]]);
        math::include(bounds,box.minimum);math::include(bounds,box.maximum);
        math::include(centres,(box.minimum+box.maximum)*.5f);
    }
    draw.nodes[index].bounds=bounds;
    if(count<=8) { draw.nodes[index].first=first;draw.nodes[index].count=count;return index; }
    const auto extent=centres.maximum-centres.minimum;
    unsigned axis=extent[1]>extent[0]?1:0;if(extent[2]>extent[axis])axis=2;
    std::stable_sort(draw.order.begin()+first,draw.order.begin()+first+count,[&](auto a,auto b) {
        const auto aa=triangleBounds(draw.surface.triangles[a]),bb=triangleBounds(draw.surface.triangles[b]);
        const auto ac=aa.minimum[axis]+aa.maximum[axis],bc=bb.minimum[axis]+bb.maximum[axis];
        return ac==bc?draw.surface.triangles[a].triangle<draw.surface.triangles[b].triangle:ac<bc;
    });
    const auto half=count/2;
    const auto left=buildNode(draw,first,half,stop),right=buildNode(draw,first+half,count-half,stop);
    draw.nodes[index].left=left;draw.nodes[index].right=right;return index;
}
Vec3 safeNormal(Vec3 value,Vec3 fallback) {
    const auto length=math::length(value);
    return math::finite(value)&&length>1e-12?value*static_cast<float>(1/length):fallback;
}
bool commonBoundary(const SurfaceTriangle& a,Vec3 ab,const SurfaceTriangle& b,Vec3 bb) {
    // A seed on a shared render-triangle edge has two valid barycentric descriptions.
    // Interior overlap remains ambiguous even when the positions are coincident.
    constexpr float epsilon=1e-5f;
    if(std::min({ab[0],ab[1],ab[2]})>epsilon||std::min({bb[0],bb[1],bb[2]})>epsilon)return false;
    unsigned shared{};
    for(const auto x:a.positions)if(std::any_of(b.positions.begin(),b.positions.end(),[&](const auto y) {
        return math::length(x-y)<1e-6;
    }))++shared;
    return shared>=2;
}
Vec3 closestBarycentrics(Vec3 p,Vec3 a,Vec3 b,Vec3 c) {
    const auto ab=b-a,ac=c-a,ap=p-a;const double d1=math::dot(ab,ap),d2=math::dot(ac,ap);
    if(d1<=0&&d2<=0)return {1,0,0};
    const auto bp=p-b;const double d3=math::dot(ab,bp),d4=math::dot(ac,bp);if(d3>=0&&d4<=d3)return {0,1,0};
    const double vc=d1*d4-d3*d2;if(vc<=0&&d1>=0&&d3<=0){const double v=d1/(d1-d3);return {1-v,v,0};}
    const auto cp=p-c;const double d5=math::dot(ab,cp),d6=math::dot(ac,cp);if(d6>=0&&d5<=d6)return {0,0,1};
    const double vb=d5*d2-d1*d6;if(vb<=0&&d2>=0&&d6<=0){const double w=d2/(d2-d6);return {1-w,0,w};}
    const double va=d3*d6-d5*d4;if(va<=0&&(d4-d3)>=0&&(d5-d6)>=0){const double w=(d4-d3)/((d4-d3)+(d5-d6));return {0,1-w,w};}
    const double inverse=1/(va+vb+vc),v=vb*inverse,w=vc*inverse;return {1-v-w,v,w};
}
ModelSurfaceHit sourceHit(const ModelDrawSurface&draw,const SurfaceTriangle&triangle,uint64_t entity,Vec3 bary) {
    ModelSurfaceHit hit;hit.seed={{entity,draw.surface.id.draw,draw.surface.id.primitive,draw.surface.id.sourceInstanceId},triangle.triangle,bary};
    hit.localPoint=math::transformPoint(draw.surface.localToWorld,triangle.positions[0]*bary[0]+triangle.positions[1]*bary[1]+triangle.positions[2]*bary[2]);
    auto normal=triangle.normals[0]*bary[0]+triangle.normals[1]*bary[1]+triangle.normals[2]*bary[2];
    if(const auto matrix=math::normalMatrix(draw.surface.localToWorld))normal=math::transformDirection(*matrix,normal);
    hit.localNormal=safeNormal(normal,{0,1,0});return hit;
}
}
size_t ModelSurface::bytes() const noexcept {
    size_t result=sizeof(*this)+draws.capacity()*sizeof(ModelDrawSurface)+diagnostic.capacity();
    for(const auto& draw:draws){result+=draw.surface.triangles.capacity()*sizeof(SurfaceTriangle)+
        draw.order.capacity()*sizeof(uint32_t)+draw.nodes.capacity()*sizeof(SourceBvhNode);if(draw.surface.boundary)result+=sizeof(SurfaceBoundaryLineage)+draw.surface.boundary->triangles.capacity()*sizeof(SurfaceBoundaryTriangle);}
    return result;
}
ModelSurface buildModelSurface(const ModelAsset& model,uint64_t fingerprint,SourceNormals mode,
    const OriginalPrimitiveLoader& original,std::stop_token stop) {
    ModelSurface result;result.fingerprint=fingerprint;
    std::vector<std::shared_ptr<const ModelPrimitive>> originals(model.primitives.size());
    std::vector<std::vector<Vec3>> smooth(model.primitives.size());
    size_t total{};
    for(uint32_t index=0;index<model.draws.size();++index) {
        cancelled(stop);const auto& sourceDraw=model.draws[index];
        if(sourceDraw.primitive>=model.primitives.size())throw std::runtime_error("Decal receiver has an invalid draw primitive.");
        const auto* primitive=&model.primitives[sourceDraw.primitive];
        if(primitive->vertices.empty()) {
            auto& loaded=originals[sourceDraw.primitive];
            if(!loaded&&original)loaded=original(sourceDraw.primitive,stop);
            if(!loaded)throw std::runtime_error("Decal receiver original geometry is unavailable.");
            primitive=loaded.get();
        }
        if(primitive->hasSkinning||!primitive->skinWeights.empty()||!primitive->morphTargets.empty()) {
            result.diagnostic="Skinned and morphed decal receivers are unsupported.";continue;
        }
        if(primitive->indices.size()%3)throw std::runtime_error("Decal receiver indices are not triangles.");
        if(total+primitive->indices.size()/3>4'000'000)throw std::runtime_error("Decal receiver source exceeds its geometry budget.");
        total+=primitive->indices.size()/3;
        if(!math::finite(sourceDraw.transform)||!math::inverse(sourceDraw.transform))throw std::runtime_error("Decal receiver draw transform is singular or nonfinite.");
        auto& normals=smooth[sourceDraw.primitive];
        if(mode==SourceNormals::Smooth&&normals.empty())normals=generate_smooth_mesh_normals(*primitive);
        ModelDrawSurface draw;draw.surface.id={0,index,sourceDraw.primitive};draw.surface.generation=fingerprint;
        draw.surface.localToWorld=sourceDraw.transform;draw.material=primitive->material;
        draw.surface.triangles.reserve(primitive->indices.size()/3);
        for(size_t triangle=0;triangle<primitive->indices.size()/3;++triangle) {
            if((triangle&1023)==0)cancelled(stop);
            SurfaceTriangle value;value.triangle=static_cast<uint32_t>(triangle);
            for(unsigned corner=0;corner<3;++corner) {
                const auto vertex=primitive->indices[triangle*3+corner];
                if(vertex>=primitive->vertices.size())throw std::runtime_error("Decal receiver triangle index is out of bounds.");
                const auto& source=primitive->vertices[vertex];
                if(!math::finite(source.position))throw std::runtime_error("Decal receiver contains nonfinite positions.");
                value.positions[corner]=source.position;
                value.normals[corner]=mode==SourceNormals::Smooth?normals.at(vertex):source.normal;
            }
            const auto cross=math::cross(value.positions[1]-value.positions[0],value.positions[2]-value.positions[0]);
            if(math::length(cross)<=1e-12)continue;
            const auto normal=safeNormal(cross,{0,1,0});
            for(auto& n:value.normals)n=mode==SourceNormals::Flat?normal:safeNormal(n,normal);
            for(const auto point:value.positions)math::include(draw.bounds,point);
            draw.surface.triangles.push_back(value);
        }
        if(draw.surface.triangles.empty())continue;
        draw.order.resize(draw.surface.triangles.size());std::iota(draw.order.begin(),draw.order.end(),0u);
        draw.nodes.reserve(draw.order.size()/3+1);buildNode(draw,0,static_cast<uint32_t>(draw.order.size()),stop);
        result.draws.push_back(std::move(draw));
    }
    if(result.draws.empty()&&result.diagnostic.empty())result.diagnostic="No usable decal receiver triangles.";
    return result;
}
std::vector<uint32_t> querySurfaceTriangles(const ModelDrawSurface& draw,const Mat4& entityWorld,const Aabb& worldBounds,std::stop_token stop) {
    std::vector<uint32_t> result;if(draw.nodes.empty()||!worldBounds.valid)return result;
    const auto inverse=math::inverse(entityWorld*draw.surface.localToWorld);if(!inverse)return result;
    const auto local=math::transformBounds(worldBounds,*inverse);
    std::vector<uint32_t> pending{0};
    while(!pending.empty()) {
        cancelled(stop);
        const auto node=pending.back();pending.pop_back();const auto& value=draw.nodes.at(node);
        if(!overlaps(local,value.bounds))continue;
        if(value.count) {
            for(uint32_t i=value.first;i<value.first+value.count;++i)if(overlaps(local,triangleBounds(draw.surface.triangles[draw.order[i]])))result.push_back(draw.order[i]);
        } else {pending.push_back(value.right);pending.push_back(value.left);}
    }
    std::sort(result.begin(),result.end(),[&](auto a,auto b){return draw.surface.triangles[a].triangle<draw.surface.triangles[b].triangle;});
    return result;
}
DecalSourceQuery decalSourceQuery(const ModelSurface& model,const Mat4& entityWorld,const DecalBuildRequest& request) {
    const auto draw=std::find_if(model.draws.begin(),model.draws.end(),[&](const auto& value){return value.surface.id.draw==request.seed.receiver.draw&&value.surface.id.primitive==request.seed.receiver.primitive;});
    if(draw==model.draws.end())throw std::runtime_error("Seed receiver draw is unavailable.");
    const auto triangle=std::find_if(draw->surface.triangles.begin(),draw->surface.triangles.end(),[&](const auto& value){return value.triangle==request.seed.triangle;});
    if(triangle==draw->surface.triangles.end())throw std::runtime_error("Seed receiver triangle is unavailable.");
    const auto bary=detail::d3(request.seed.barycentrics);
    if(!detail::finite(bary)||std::abs(bary.x+bary.y+bary.z-1)>1e-4||std::min({bary.x,bary.y,bary.z})< -1e-5)throw std::runtime_error("Invalid seed barycentrics.");
    const auto transform=entityWorld*draw->surface.localToWorld;
    const auto point=detail::point(transform,detail::d3(triangle->positions[0])*bary.x+detail::d3(triangle->positions[1])*bary.y+detail::d3(triangle->positions[2])*bary.z);
    const auto normal=detail::normal(transform,detail::cross(detail::d3(triangle->positions[1])-detail::d3(triangle->positions[0]),detail::d3(triangle->positions[2])-detail::d3(triangle->positions[0])));
    const auto domain=detail::samplingDomain(request,point,normal);
    return {{point.x,point.y,point.z},domain.radius,domain.intrinsicRadius+domain.spacing+domain.tolerance,domain.tolerance};
}
std::vector<uint32_t> querySurfaceTriangles(const ModelDrawSurface& draw,const Mat4& entityWorld,
    const DecalSourceQuery& query,std::stop_token stop) {
    const detail::D3 center{query.center[0],query.center[1],query.center[2]};
    if(!detail::finite(center)||!std::isfinite(query.radius)||!std::isfinite(query.maximumDistance)||!std::isfinite(query.tolerance)||query.radius<=0||query.maximumDistance<=0||query.tolerance<0)throw std::runtime_error("Invalid decal source query.");
    Aabb box;box.valid=true;
    // Outward rounding keeps the float BVH broad phase conservative relative
    // to the shared double-precision world-space inclusion test below.
    for(size_t axis=0;axis<3;++axis){box.minimum[axis]=std::nextafter(static_cast<float>(query.center[axis]-query.radius-query.tolerance),-INFINITY);box.maximum[axis]=std::nextafter(static_cast<float>(query.center[axis]+query.radius+query.tolerance),INFINITY);}
    auto indices=querySurfaceTriangles(draw,entityWorld,box,stop);const auto transform=entityWorld*draw.surface.localToWorld;size_t tested{};
    std::erase_if(indices,[&](uint32_t index){if((tested++&1023)==0)cancelled(stop);const auto& triangle=draw.surface.triangles[index];std::array<detail::D3,3> positions;for(size_t corner=0;corner<3;++corner)positions[corner]=detail::point(transform,detail::d3(triangle.positions[corner]));
        // A receiver-local AABB can grow substantially through an affine
        // inverse. Check the original world box before the distance frontier.
        for(size_t axis=0;axis<3;++axis){double minimum=INFINITY,maximum=-INFINITY;for(const auto position:positions){minimum=std::min(minimum,position[axis]);maximum=std::max(maximum,position[axis]);}if(minimum>query.center[axis]+query.radius+query.tolerance||maximum<query.center[axis]-query.radius-query.tolerance)return true;}
        return detail::triangleDistance(center,positions)>query.maximumDistance;
    });
    return indices;
}
SurfaceHitResult resolveModelSurfaceHit(const ModelSurface& model,uint64_t entity,const Mat4& entityWorld,
    uint32_t drawIndex,uint32_t primitive,Vec3 visiblePoint,float tolerance) {
    SurfaceHitResult result;if(!math::finite(visiblePoint))return result;
    const auto found=std::find_if(model.draws.begin(),model.draws.end(),[&](const auto& draw){return draw.surface.id.draw==drawIndex&&draw.surface.id.primitive==primitive;});
    if(found==model.draws.end())return result;
    const auto& draw=*found;const auto transform=entityWorld*draw.surface.localToWorld;
    if(tolerance<=0)tolerance=std::max(1e-5f,8*std::numeric_limits<float>::epsilon()*std::max({1.f,std::abs(visiblePoint[0]),std::abs(visiblePoint[1]),std::abs(visiblePoint[2])}));
    if(!std::isfinite(tolerance)||tolerance<=0)return result;
    Aabb box;math::include(box,visiblePoint-Vec3{tolerance,tolerance,tolerance});math::include(box,visiblePoint+Vec3{tolerance,tolerance,tolerance});
    const SurfaceTriangle* selected{};Vec3 selectedBary{};
    for(const auto index:querySurfaceTriangles(draw,entityWorld,box)) {
        const auto& triangle=draw.surface.triangles[index];
        const auto a=math::transformPoint(transform,triangle.positions[0]);
        const auto b=math::transformPoint(transform,triangle.positions[1]);
        const auto c=math::transformPoint(transform,triangle.positions[2]);
        const auto e1=b-a,e2=c-a,p=visiblePoint-a;
        const auto d00=math::dot(e1,e1),d01=math::dot(e1,e2),d11=math::dot(e2,e2),d20=math::dot(p,e1),d21=math::dot(p,e2);
        const auto denominator=d00*d11-d01*d01;if(std::abs(denominator)<1e-24)continue;
        const auto v=(d11*d20-d01*d21)/denominator,w=(d00*d21-d01*d20)/denominator;
        Vec3 bary{static_cast<float>(1-v-w),static_cast<float>(v),static_cast<float>(w)};
        const auto epsilon=static_cast<float>(tolerance/std::max(1e-8,std::sqrt(std::max(d00,d11))));
        if(std::min({bary[0],bary[1],bary[2]})<-epsilon||std::max({bary[0],bary[1],bary[2]})>1+epsilon)continue;
        for(auto& x:bary)x=std::clamp(x,0.f,1.f);
        bary=bary/(bary[0]+bary[1]+bary[2]);
        if(math::length(a*bary[0]+b*bary[1]+c*bary[2]-visiblePoint)>tolerance)continue;
        if(selected&&!commonBoundary(*selected,selectedBary,triangle,bary)) {result.status=SurfaceHitStatus::Ambiguous;return result;}
        if(!selected||triangle.triangle<selected->triangle){selected=&triangle;selectedBary=bary;}
    }
    if(!selected)return result;
    ModelSurfaceHit hit;hit.seed={{entity,drawIndex,primitive},selected->triangle,selectedBary};
    const auto local=selected->positions[0]*selectedBary[0]+selected->positions[1]*selectedBary[1]+selected->positions[2]*selectedBary[2];
    auto normal=selected->normals[0]*selectedBary[0]+selected->normals[1]*selectedBary[1]+selected->normals[2]*selectedBary[2];
    hit.localPoint=math::transformPoint(draw.surface.localToWorld,local);
    if(const auto matrix=math::normalMatrix(draw.surface.localToWorld))normal=math::transformDirection(*matrix,normal);
    hit.localNormal=safeNormal(normal,{0,1,0});result.status=SurfaceHitStatus::Found;result.hit=hit;return result;
}
SurfaceHitResult projectModelSurfaceHit(const ModelSurface&model,uint64_t entity,const Mat4&world,
    Vec3 point,float maximumDistance,const std::function<bool(uint32_t)>&materialAllowed) {
    SurfaceHitResult result;if(!math::finite(point)||!std::isfinite(maximumDistance)||maximumDistance<=0||!math::inverse(world))return result;
    const Vec3 extent{maximumDistance,maximumDistance,maximumDistance};const Aabb region{point-extent,point+extent,true};
    double best=double(maximumDistance)*maximumDistance;const double tie=std::max(1e-12,best*1e-10);
    const ModelDrawSurface* selectedDraw{};const SurfaceTriangle*selected{};Vec3 selectedBary{};bool ambiguous=false;
    for(const auto&draw:model.draws){if(materialAllowed&&!materialAllowed(draw.material))continue;const auto transform=world*draw.surface.localToWorld;
        for(const auto index:querySurfaceTriangles(draw,world,region)){const auto&triangle=draw.surface.triangles[index];const auto a=math::transformPoint(transform,triangle.positions[0]),b=math::transformPoint(transform,triangle.positions[1]),c=math::transformPoint(transform,triangle.positions[2]);const auto bary=closestBarycentrics(point,a,b,c),delta=a*bary[0]+b*bary[1]+c*bary[2]-point;const double distance=math::dot(delta,delta);
            if(distance>best+tie)continue;
            if(!selected||distance<best-tie){selected=&triangle;selectedDraw=&draw;selectedBary=bary;best=distance;ambiguous=false;}
            else {
                const bool vertexA=std::max({selectedBary[0],selectedBary[1],selectedBary[2]})>1-1e-5f,vertexB=std::max({bary[0],bary[1],bary[2]})>1-1e-5f;
                const auto selectedTransform=world*selectedDraw->surface.localToWorld;
                const auto previous=math::transformPoint(selectedTransform,selected->positions[0]*selectedBary[0]+selected->positions[1]*selectedBary[1]+selected->positions[2]*selectedBary[2]);
                auto selectedWorld=*selected,triangleWorld=triangle;
                for(auto& position:selectedWorld.positions)position=math::transformPoint(selectedTransform,position);
                for(auto& position:triangleWorld.positions)position=math::transformPoint(transform,position);
                const bool samePoint=math::length(previous-(point+delta))<1e-6;
                if(!samePoint||(!commonBoundary(selectedWorld,selectedBary,triangleWorld,bary)&&(!vertexA||!vertexB)))ambiguous=true;
            }
        }
    }
    if(!selected)return result;if(ambiguous){result.status=SurfaceHitStatus::Ambiguous;return result;}
    result.status=SurfaceHitStatus::Found;result.hit=sourceHit(*selectedDraw,*selected,entity,selectedBary);return result;
}
}
