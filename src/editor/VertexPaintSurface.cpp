#include "VertexPaintSurface.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <span>

namespace crimson {
namespace {
bool boundsHit(const Ray& ray,const Aabb& bounds,Vec3 precision,double maximum,double& entry) {
    if(!bounds.valid)return false;
    double first=0,last=maximum;
    for(unsigned axis=0;axis<3;++axis) {
        const double origin=ray.origin[axis],direction=ray.direction[axis];
        if(direction==0) {if(origin<double(bounds.minimum[axis])-precision[axis]||origin>double(bounds.maximum[axis])+precision[axis])return false;continue;}
        double a=(double(bounds.minimum[axis])-precision[axis]-origin)/direction,b=(double(bounds.maximum[axis])+precision[axis]-origin)/direction;
        if(a>b)std::swap(a,b);first=std::max(first,a);last=std::min(last,b);
        if(first>last)return false;
    }
    entry=first;return last>=0;
}
bool triangleHit(const Ray& ray,Vec3 a,Vec3 b,Vec3 c,Vec3 precision,bool doubleSided,double maximum,double& distance,Vec3& normal) {
    // Use doubles through the intersection, including edge tests. World-space
    // FP32 readback positions are deliberately absent from this visibility path.
    double ab[3],ac[3],p[3],t[3],q[3];
    for(unsigned i=0;i<3;++i){ab[i]=double(b[i])-a[i];ac[i]=double(c[i])-a[i];t[i]=double(ray.origin[i])-a[i];}
    for(unsigned i=0;i<3;++i)p[i]=double(ray.direction[(i+1)%3])*ac[(i+2)%3]-double(ray.direction[(i+2)%3])*ac[(i+1)%3];
    double determinant{};for(unsigned i=0;i<3;++i)determinant+=ab[i]*p[i];
    double n[3],nn{};for(unsigned i=0;i<3;++i){n[i]=ab[(i+1)%3]*ac[(i+2)%3]-ab[(i+2)%3]*ac[(i+1)%3];nn+=n[i]*n[i];}
    if(!(nn>0))return false;
    // A ray numerically coplanar with a neighboring face cannot establish an
    // occluder at its own origin. The threshold follows FP32 direction accuracy.
    const double parallelError=2*std::numeric_limits<float>::epsilon()*std::sqrt(nn)*math::length(ray.direction);
    if(doubleSided?std::abs(determinant)<=parallelError:determinant<=parallelError)return false;
    double u{};for(unsigned i=0;i<3;++i)u+=t[i]*p[i];u/=determinant;
    double toleranceU{},toleranceV{},toleranceSum{};
    for(unsigned i=0;i<3;++i) {
        const double gradientU=(ac[(i+1)%3]*n[(i+2)%3]-ac[(i+2)%3]*n[(i+1)%3])/nn;
        const double gradientV=(n[(i+1)%3]*ab[(i+2)%3]-n[(i+2)%3]*ab[(i+1)%3])/nn;
        toleranceU+=std::abs(gradientU)*precision[i];toleranceV+=std::abs(gradientV)*precision[i];toleranceSum+=std::abs(gradientU+gradientV)*precision[i];
    }
    if(u< -toleranceU)return false;
    for(unsigned i=0;i<3;++i)q[i]=t[(i+1)%3]*ab[(i+2)%3]-t[(i+2)%3]*ab[(i+1)%3];
    double v{},hit{};for(unsigned i=0;i<3;++i){v+=double(ray.direction[i])*q[i];hit+=ac[i]*q[i];}v/=determinant;hit/=determinant;
    if(v< -toleranceV||u+v>1+toleranceSum||hit<0||hit>maximum)return false;
    normal=math::normalized(math::cross(b-a,c-a));distance=hit;return true;
}
std::optional<VertexPaintSurfaceHit> trace(std::span<const VertexPaintSurfaceMesh> surfaces,const Ray& worldRay,std::optional<std::array<std::uint64_t,3>> identity={}) {
    if(!math::finite(worldRay.origin)||!math::finite(worldRay.direction)||math::length(worldRay.direction)<1e-12)return {};
    double closest=std::numeric_limits<double>::infinity();std::optional<VertexPaintSurfaceHit> result;
    for(const auto& surface:surfaces) {
        if(identity&&(surface.entity!=(*identity)[0]||surface.draw!=(*identity)[1]||surface.primitive!=(*identity)[2]))continue;
        const Ray ray{math::transformPoint(surface.inverse,worldRay.origin),math::transformDirection(surface.inverse,worldRay.direction)};
        // Authoring matrices, ray endpoints and model vertices are FP32. Bound
        // their cancellation error in each local axis, rather than opening a
        // fixed world-space gap around every mesh. Small/near-origin geometry
        // retains a proportionately small tolerance.
        Vec3 precision;
        double distanceSquared{};
        for(unsigned axis=0;axis<3;++axis){const double extent=std::max(std::abs(double(surface.bounds.minimum[axis])-ray.origin[axis]),std::abs(double(surface.bounds.maximum[axis])-ray.origin[axis]));distanceSquared+=extent*extent;}
        const double travel=std::sqrt(distanceSquared)/std::max(math::length(ray.direction),1e-30);
        for(unsigned axis=0;axis<3;++axis) {
            double magnitude=std::abs(double(surface.inverse.at(axis,3)));
            for(unsigned column=0;column<3;++column)magnitude+=std::abs(double(surface.inverse.at(axis,column))*worldRay.origin[column])+travel*std::abs(double(surface.inverse.at(axis,column))*worldRay.direction[column]);
            precision[axis]=static_cast<float>(2*std::numeric_limits<float>::epsilon()*std::max(magnitude,1e-3));
        }
        double entry{};if(!boundsHit(ray,surface.bounds,precision,closest,entry))continue;
        if(!surface.mesh) {
            if(entry<=closest){closest=entry;result=VertexPaintSurfaceHit{surface.entity,surface.draw,surface.primitive,surface.resourceRevision,worldRay.origin+worldRay.direction*entry,{},false};}
            continue;
        }
        const auto& mesh=*surface.mesh;
        for(std::uint32_t node=0;std::size_t(node)*2+1<mesh.nodes.size();) {
            const auto& minimum=mesh.nodes[std::size_t(node)*2];const auto& maximum=mesh.nodes[std::size_t(node)*2+1];
            const auto escape=std::bit_cast<std::uint32_t>(minimum[3]),header=std::bit_cast<std::uint32_t>(maximum[3]);
            if(!boundsHit(ray,{{minimum[0],minimum[1],minimum[2]},{maximum[0],maximum[1],maximum[2]},true},precision,closest,entry)){node=escape;continue;}
            const auto count=header>>24,first=header&0x00ffffffu;
            if(!count){++node;continue;}
            for(std::uint32_t index=first;index<first+count;++index) {
                const auto position=[&](unsigned corner){const auto& p=mesh.triangles[std::size_t(index)*3+corner];return Vec3{p[0],p[1],p[2]};};
                Vec3 normal;double distance{};
                if(!triangleHit(ray,position(0),position(1),position(2),precision,surface.doubleSided,closest,distance,normal))continue;
                if(result&&std::abs(distance-closest)<=1e-8) {
                    // Distinct draws at the same depth need the actual visible
                    // GPU identity. Shared edges within one draw remain exact.
                    if(result->entity!=surface.entity||result->draw!=surface.draw||result->primitive!=surface.primitive)result->exact=false;
                    if(!surface.exact)result->exact=false;
                    continue;
                }
                closest=distance;
                const auto normalMatrix=math::normalMatrix(surface.world);
                normal=normalMatrix?math::normalized(math::transformDirection(*normalMatrix,normal)):Vec3{0,1,0};
                if(math::dot(normal,worldRay.direction)>0)normal=-normal;
                result=VertexPaintSurfaceHit{surface.entity,surface.draw,surface.primitive,surface.resourceRevision,worldRay.origin+worldRay.direction*distance,normal,surface.exact};
            }
            node=escape;
        }
    }
    return result;
}
}
VertexPaintSurfaceSnapshot makeVertexPaintSurfaceSnapshot(std::vector<VertexPaintSurfaceMesh> surfaces,std::uint64_t signature) {
    auto shared=std::make_shared<const std::vector<VertexPaintSurfaceMesh>>(std::move(surfaces));
    return {signature,true,{},[shared](const Ray& ray){return trace(*shared,ray);},
        [shared](const Ray& ray,EntityId entity,std::uint32_t draw,std::uint32_t primitive){return trace(*shared,ray,std::array<std::uint64_t,3>{entity,draw,primitive});}};
}
}
