#include "DecalModelSurface.h"
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
}
size_t ModelSurface::bytes() const noexcept {
    size_t result=sizeof(*this)+draws.capacity()*sizeof(ModelDrawSurface)+diagnostic.capacity();
    for(const auto& draw:draws)result+=draw.surface.triangles.capacity()*sizeof(SurfaceTriangle)+
        draw.order.capacity()*sizeof(uint32_t)+draw.nodes.capacity()*sizeof(SourceBvhNode);
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
std::vector<uint32_t> querySurfaceTriangles(const ModelDrawSurface& draw,const Mat4& entityWorld,const Aabb& worldBounds) {
    std::vector<uint32_t> result;if(draw.nodes.empty()||!worldBounds.valid)return result;
    const auto inverse=math::inverse(entityWorld*draw.surface.localToWorld);if(!inverse)return result;
    const auto local=math::transformBounds(worldBounds,*inverse);
    std::vector<uint32_t> pending{0};
    while(!pending.empty()) {
        const auto node=pending.back();pending.pop_back();const auto& value=draw.nodes.at(node);
        if(!overlaps(local,value.bounds))continue;
        if(value.count) {
            for(uint32_t i=value.first;i<value.first+value.count;++i)if(overlaps(local,triangleBounds(draw.surface.triangles[draw.order[i]])))result.push_back(draw.order[i]);
        } else {pending.push_back(value.right);pending.push_back(value.left);}
    }
    std::sort(result.begin(),result.end(),[&](auto a,auto b){return draw.surface.triangles[a].triangle<draw.surface.triangles[b].triangle;});
    return result;
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
}
