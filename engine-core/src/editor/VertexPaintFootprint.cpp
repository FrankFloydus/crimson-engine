#include "VertexPaintFootprint.h"
#include <crimson/renderer/core/Profiler.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace crimson {
namespace {
double lengthSquared(Vec3 point){return math::dot(point,point);}
bool intersects(const Aabb& bounds,Vec3 center,double radiusSquared) {
    double distance{};
    for(unsigned axis=0;axis<3;++axis) {
        const double delta=std::max({double(bounds.minimum[axis])-center[axis],0.,double(center[axis])-bounds.maximum[axis]});
        distance+=delta*delta;
    }
    return bounds.valid&&distance<=radiusSquared;
}
// Closest point on a nondegenerate triangle, retaining its interpolation
// coordinates so local-space quad reconstruction avoids world cancellation.
std::optional<Vec3> closestTriangle(Vec3 point,Vec3 a,Vec3 b,Vec3 c) {
    const auto ab=b-a,ac=c-a,ap=point-a;
    const double aa=lengthSquared(ab),cc=lengthSquared(ac),nn=lengthSquared(math::cross(ab,ac));
    if(!(aa>0&&cc>0&&nn>aa*cc*1e-24))return {};
    const auto d1=math::dot(ab,ap),d2=math::dot(ac,ap);
    if(d1<=0&&d2<=0)return Vec3{1,0,0};
    const auto bp=point-b;const auto d3=math::dot(ab,bp),d4=math::dot(ac,bp);
    if(d3>=0&&d4<=d3)return Vec3{0,1,0};
    const auto vc=d1*d4-d3*d2;
    if(vc<=0&&d1>=0&&d3<=0){const auto v=d1/(d1-d3);return Vec3{1-v,v,0};}
    const auto cp=point-c;const auto d5=math::dot(ab,cp),d6=math::dot(ac,cp);
    if(d6>=0&&d5<=d6)return Vec3{0,0,1};
    const auto vb=d5*d2-d1*d6;
    if(vb<=0&&d2>=0&&d6<=0){const auto w=d2/(d2-d6);return Vec3{1-w,0,w};}
    const auto va=d3*d6-d5*d4;
    if(va<=0&&(d4-d3)>=0&&(d5-d6)>=0){const auto w=(d4-d3)/((d4-d3)+(d5-d6));return Vec3{0,1-w,w};}
    const auto denominator=va+vb+vc;if(!(denominator>0))return {};
    const auto v=vb/denominator,w=vc/denominator;return Vec3{1-v-w,v,w};
}
std::optional<std::array<float,4>> quadBasis(const std::array<Vec3,4>& corners,Vec3 point) {
    // Match MeshPaintGpu's local-space projected twist correction, including
    // warped quads and nonuniform occurrence transforms.
    const auto a=corners[1]-corners[0],b=corners[3]-corners[0];
    const auto twist=corners[0]-corners[1]+corners[2]-corners[3],delta=point-corners[0];
    const double aa=math::dot(a,a),ab=math::dot(a,b),bb=math::dot(b,b),determinant=aa*bb-ab*ab;
    if(!(determinant>aa*bb*1e-12))return {};
    const auto coordinates=[&](Vec3 value){const auto av=math::dot(a,value),bv=math::dot(b,value);return std::array<double,2>{(av*bb-bv*ab)/determinant,(bv*aa-av*ab)/determinant};};
    const auto correction=coordinates(twist),projected=coordinates(delta);
    double u=projected[0],v=projected[1];
    for(unsigned iteration=0;iteration<10;++iteration) {
        const auto x=u+correction[0]*u*v-projected[0],y=v+correction[1]*u*v-projected[1];
        const auto ax=1+correction[0]*v,ay=correction[1]*v,bx=correction[0]*u,by=1+correction[1]*u;
        const auto d=ax*by-ay*bx;if(std::abs(d)<1e-12)return {};
        const auto du=(x*by-y*bx)/d,dv=(ax*y-ay*x)/d;u-=du;v-=dv;
        if(std::abs(du)+std::abs(dv)<1e-8)break;
    }
    if(!std::isfinite(u)||!std::isfinite(v)||u< -1e-4||u>1+1e-4||v< -1e-4||v>1+1e-4)return {};
    if(std::abs(u+correction[0]*u*v-projected[0])+std::abs(v+correction[1]*u*v-projected[1])>1e-5)return {};
    u=std::clamp(u,0.,1.);v=std::clamp(v,0.,1.);
    return std::array<float,4>{float((1-u)*(1-v)),float(u*(1-v)),float(u*v),float((1-u)*v)};
}
float opacity(double distance,float radius,float strength,float hardness) {
    distance/=radius;if(distance>=1)return 0;
    if(distance<=hardness||hardness>=1)return strength;
    const auto t=std::clamp(float((1-distance)/(1-hardness)),0.f,1.f);return strength*t*t*(3-2*t);
}
}
void VertexPaintFootprintCache::clear(){meshes_.clear();cells_.clear();nodes_.clear();width_=height_=0;projection_={};}
void VertexPaintFootprintCache::build(std::uint32_t first,std::uint32_t count) {
    const auto node=std::uint32_t(nodes_.size());nodes_.emplace_back();
    Aabb bounds,centers;
    for(std::uint32_t i=first;i<first+count;++i) {
        math::include(bounds,cells_[i].bounds);
        math::include(centers,cells_[i].bounds.minimum*.5+cells_[i].bounds.maximum*.5);
    }
    nodes_[node].bounds=bounds;
    if(count<=8){nodes_[node].first=first;nodes_[node].count=count;}
    else {
        unsigned axis=0;for(unsigned i=1;i<3;++i)if(double(centers.maximum[i])-centers.minimum[i]>double(centers.maximum[axis])-centers.minimum[axis])axis=i;
        const auto middle=first+count/2;
        std::nth_element(cells_.begin()+first,cells_.begin()+middle,cells_.begin()+first+count,[axis](const auto& a,const auto& b){
            const auto x=double(a.bounds.minimum[axis])+a.bounds.maximum[axis],y=double(b.bounds.minimum[axis])+b.bounds.maximum[axis];
            if(x!=y)return x<y;return a.target!=b.target?a.target<b.target:a.cell<b.cell;
        });
        build(first,count/2);build(middle,count-count/2);
    }
    nodes_[node].escape=std::uint32_t(nodes_.size());
}
bool VertexPaintFootprintCache::prepare(std::span<const VertexPaintFootprintMesh> meshes,const Mat4& vp,std::uint32_t width,std::uint32_t height) {
    CRIMSON_PROFILE_ZONE("Vertex paint surface footprint preparation");
    if(!width||!height||width>16384||height>16384||!math::finite(vp)||meshes.size()>std::numeric_limits<std::uint32_t>::max()){clear();return false;}
    // The spatial hierarchy depends only on immutable geometry and occurrence
    // transforms. Camera changes update projection without rebuilding cells.
    bool same=width_&&meshes.size()==meshes_.size();
    for(std::size_t index=0;same&&index<meshes.size();++index)same=meshes[index].geometry==meshes_[index].geometry&&meshes[index].world==meshes_[index].world;
    if(same){projection_=vp;width_=width;height_=height;return true;}
    clear();
    std::uint64_t vertices{},cells{};
    for(const auto& mesh:meshes) {
        if(!mesh.geometry||!math::finite(mesh.world)||!math::isAffine(mesh.world)||!math::inverse(mesh.world))return false;
        vertices+=mesh.geometry->vertices.size();cells+=mesh.geometry->cells.size();
        if(mesh.geometry->vertices.size()>MaximumMeshPaintVertices||mesh.geometry->cells.size()>MaximumMeshPaintTriangles||vertices>MaximumScenePaintVertices||cells>MaximumScenePaintVertices*2)return false;
    }
    meshes_.assign(meshes.begin(),meshes.end());cells_.reserve(std::size_t(cells));
    for(std::uint32_t target=0;target<meshes_.size();++target) {
        const auto& mesh=meshes_[target];
        for(std::uint32_t index=0;index<mesh.geometry->cells.size();++index) {
            const auto& cell=mesh.geometry->cells[index];Aabb bounds;
            if(cell.cornerCount!=3&&cell.cornerCount!=4){clear();return false;}
            for(unsigned corner=0;corner<cell.cornerCount;++corner) {
                if(cell.vertices[corner]>=mesh.geometry->vertices.size()){clear();return false;}
                const auto world=math::transformPoint(mesh.world,mesh.geometry->vertices[cell.vertices[corner]].position);
                if(!math::finite(world)){clear();return false;}math::include(bounds,world);
            }
            for(unsigned corner=0;corner<3u*(cell.cornerCount-2u);++corner)if(cell.triangleCorners[corner]>=cell.cornerCount){clear();return false;}
            cells_.push_back({target,index,bounds});
        }
    }
    if(!cells_.empty()){nodes_.reserve(cells_.size()/2+1);build(0,std::uint32_t(cells_.size()));}
    projection_=vp;width_=width;height_=height;return true;
}
VertexPaintFootprintResult VertexPaintFootprintCache::collect(Vec3 center,float radius,float strength,float hardness,const Visibility& visible) const {
    CRIMSON_PROFILE_ZONE("Vertex paint surface footprint query");
    VertexPaintFootprintResult result;
    if(!width_||!height_||!math::finite(center)||!std::isfinite(radius)||radius<=0||!std::isfinite(strength)||strength<=0||strength>1||!std::isfinite(hardness)||hardness<0||hardness>1||!visible)return result;
    const auto radiusSquared=double(radius)*radius;
    for(std::uint32_t node=0;node<nodes_.size();) {
        const auto& branch=nodes_[node];if(!intersects(branch.bounds,center,radiusSquared)){node=branch.escape;continue;}
        if(!branch.count){++node;continue;}
        for(auto index=branch.first;index<branch.first+branch.count;++index) {
            const auto& cached=cells_[index];if(!intersects(cached.bounds,center,radiusSquared))continue;
            const auto& mesh=meshes_[cached.target];const auto& cell=mesh.geometry->cells[cached.cell];
            std::array<Vec3,4> local{},world{};
            for(unsigned corner=0;corner<cell.cornerCount;++corner){local[corner]=mesh.geometry->vertices[cell.vertices[corner]].position;world[corner]=math::transformPoint(mesh.world,local[corner]);}
            struct Point {Vec3 barycentric;unsigned triangle{};};
            std::array<Point,4> points{};unsigned count{};
            for(unsigned t=0;t<cell.cornerCount-2u;++t) {
                const auto a=cell.triangleCorners[t*3],b=cell.triangleCorners[t*3+1],c=cell.triangleCorners[t*3+2];
                const auto bary=closestTriangle(center,world[a],world[b],world[c]);if(!bary)continue;
                const auto q=world[a]+(world[b]-world[a])*(*bary)[1]+(world[c]-world[a])*(*bary)[2];
                const auto distance=lengthSquared(q-center);if(!(distance<radiusSquared))continue;
                points[count++]={*bary,t};
                // At a Base-only control corner the nearest point carries no
                // overlay, although the visible mask inside the brush may do.
                // Sample a bounded inset toward this actual triangle's center
                // so those neighboring contributions can also be restored.
                const auto centroid=world[a]+(world[b]-world[a])/3+(world[c]-world[a])/3;
                const auto direction=centroid-q;const auto dd=lengthSquared(direction);
                if(dd>0) {
                    const auto qd=math::dot(q-center,direction);
                    const auto discriminant=qd*qd+dd*(radiusSquared-distance);
                    const auto boundary=(-qd+std::sqrt(std::max(discriminant,0.)))/dd;
                    const auto fraction=std::min({1.,double(radius)*.5/std::sqrt(dd),boundary*.5});
                    if(fraction>1e-8)points[count++]={*bary*(1-fraction)+Vec3{1.f/3,1.f/3,1.f/3}*fraction,t};
                }
            }
            for(unsigned p=0;p<count;++p) {
                const auto& samplePoint=points[p];const auto& bary=samplePoint.barycentric;const auto triangle=samplePoint.triangle;
                const auto a=cell.triangleCorners[triangle*3],b=cell.triangleCorners[triangle*3+1],c=cell.triangleCorners[triangle*3+2];
                const auto point=world[a]+(world[b]-world[a])*bary[1]+(world[c]-world[a])*bary[2];
                const auto localPoint=local[a]+(local[b]-local[a])*bary[1]+(local[c]-local[a])*bary[2];
                const auto amount=opacity(math::length(point-center),radius,strength,hardness);if(amount<=0)continue;
                const auto pixel=math::project(point,projection_,{0,0,float(width_),float(height_)},ClipDepth::NegativeOneToOne);
                if(!pixel||(*pixel)[2]<0||(*pixel)[2]>1||(*pixel)[0]<0||(*pixel)[0]>=width_||(*pixel)[1]<0||(*pixel)[1]>=height_)continue;
                std::array<float,4> basis{};
                if(cell.cornerCount==4){const auto quad=quadBasis(local,localPoint);if(!quad)continue;basis=*quad;}
                else for(unsigned corner=0;corner<3;++corner)basis[cell.triangleCorners[corner]]=bary[corner];
                const auto maximum=*std::max_element(basis.begin(),basis.end());if(!(maximum>0))continue;
                const VertexPaintFootprintSample sample{cached.target,cached.cell,cell.vertices[cell.triangleCorners[triangle*3]],point,{(*pixel)[0],(*pixel)[1]}};
                const auto answer=visible(sample);
                if(!answer){result.influences.clear();result.missing=sample;return result;}if(!*answer)continue;
                result.influences.push_back({cached.target,cached.cell,cell.vertices,basis,cell.cornerCount,amount});
            }
        }
        node=branch.escape;
    }
    return result;
}
}
