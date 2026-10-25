#include "DecalPaintSurface.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <tuple>

namespace crimson::decals {
namespace {
void cancelled(std::stop_token stop) { if(stop.stop_requested())throw std::runtime_error("Decal paint surface preparation cancelled."); }
using D3=std::array<double,3>;
D3 difference(Vec3 a,Vec3 b) { return {double(a[0])-b[0],double(a[1])-b[1],double(a[2])-b[2]}; }
D3 cross(D3 a,D3 b) { return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]}; }
double dot(D3 a,D3 b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
const ModelVertex& vertex(const MeshPaintGeometry& geometry,const MeshPaintCell& cell,std::uint32_t index) {
    return cell.shadingVertexCount?geometry.shadingVertices.at(std::size_t(cell.shadingOffset)+index):geometry.vertices.at(cell.vertices.at(index));
}
struct Face {
    bool candidate{true},normalSet{},seen{};
    D3 normal{};
    Vec3 origin{},shadingNormal{};
};
}
std::shared_ptr<const SurfaceBoundaryLineage> meshPaintBoundaryLineage(const ModelPrimitive& source,
    const MeshPaintGeometry& refined,std::stop_token stop) {
    cancelled(stop);if(!refined.level)return {};
    const auto coarse=buildMeshPaintGeometry(source,0,stop);
    if(coarse->sourceSignature!=refined.sourceSignature)throw std::runtime_error("Decal boundary has incompatible paint source provenance.");
    auto result=std::make_shared<SurfaceBoundaryLineage>();uint32_t triangle{};
    for(const auto& cell:refined.cells){
        cancelled(stop);if(cell.sourceFace>=coarse->cells.size())throw std::runtime_error("Decal boundary has an invalid source face.");
        const auto& face=coarse->cells[cell.sourceFace];
        for(unsigned t=0;t<cell.cornerCount-2u;++t,++triangle){
            SurfaceBoundaryTriangle record;record.triangle=triangle;
            for(unsigned c=0;c<3;++c){
                const auto logical=cell.triangleCorners[t*3+c];const auto uv=cell.sourceCoordinates[logical];
                for(unsigned edge=0;edge<face.cornerCount;++edge){
                    const auto a=face.sourceCoordinates[edge],b=face.sourceCoordinates[(edge+1)%face.cornerCount];
                    const double dx=double(b[0])-a[0],dy=double(b[1])-a[1],x=double(uv[0])-a[0],y=double(uv[1])-a[1];
                    if(dx*y-dy*x!=0)continue;double parameter=std::abs(dx)>=std::abs(dy)?x/dx:y/dy;if(parameter<0||parameter>1)continue;
                    auto first=coarse->vertices.at(face.vertices[edge]).position,last=coarse->vertices.at(face.vertices[(edge+1)%face.cornerCount]).position;
                    // The same original edge in opposite face winding uses
                    // the identical arithmetic recipe and parameter direction.
                    if(std::tie(last[0],last[1],last[2])<std::tie(first[0],first[1],first[2])){std::swap(first,last);parameter=1-parameter;}
                    record.points[c]={first,last,parameter};record.corners|=1u<<c;break;
                }
            }
            if(record.corners)result->triangles.push_back(record);
        }
    }
    return result->triangles.empty()?nullptr:result;
}
std::optional<ModelPrimitive> simplifyDecalPaintPrimitive(const ModelPrimitive& source,
    const MeshPaintGeometry& refined,std::stop_token stop,const SurfaceBoundaryLineage* fineBoundary,
    std::shared_ptr<const SurfaceBoundaryLineage>* retainedBoundary) {
    if(retainedBoundary)retainedBoundary->reset();
    cancelled(stop);if(!refined.level)return {};
    // Canonical level-zero cells supply the exact source-face numbering and
    // original triangle winding, including imported shading aliases.
    const auto coarse=buildMeshPaintGeometry(source,0,stop);
    if(coarse->sourceSignature!=refined.sourceSignature)throw std::runtime_error("Decal paint surface has incompatible source provenance.");
    std::vector<Face> faces(coarse->cells.size());
    for(std::size_t i=0;i<faces.size();++i){
        cancelled(stop);const auto& cell=coarse->cells[i];auto& face=faces[i];
        const auto point=[&](unsigned c){return coarse->vertices.at(cell.vertices.at(c)).position;};
        face.origin=point(0);face.normal=cross(difference(point(1),face.origin),difference(point(2),face.origin));
        face.candidate=dot(face.normal,face.normal)>0;
        // Exact coordinate tests are intentionally conservative. A quad warped
        // by even a few micrometres is real geometry and keeps its refinement.
        for(unsigned c=0;c<cell.cornerCount&&face.candidate;++c){
            face.candidate=dot(face.normal,difference(point(c),face.origin))==0;
            const auto turn=cross(difference(point((c+1)%cell.cornerCount),point(c)),
                difference(point((c+2)%cell.cornerCount),point((c+1)%cell.cornerCount)));
            face.candidate&=dot(face.normal,turn)>0;
        }
        for(unsigned t=0;t<cell.cornerCount-2u&&face.candidate;++t){
            const auto a=point(cell.triangleCorners[t*3]),b=point(cell.triangleCorners[t*3+1]),c=point(cell.triangleCorners[t*3+2]);
            face.candidate=dot(face.normal,cross(difference(b,a),difference(c,a)))>0;
        }
    }
    for(const auto& cell:refined.cells){
        cancelled(stop);if(cell.sourceFace>=faces.size())throw std::runtime_error("Decal paint surface has an invalid source face.");
        auto& face=faces[cell.sourceFace];face.seen=true;if(!face.candidate)continue;
        for(unsigned t=0;t<cell.cornerCount-2u&&face.candidate;++t){
            const auto point=[&](unsigned c){return refined.vertices.at(cell.vertices.at(cell.triangleCorners[t*3+c])).position;};
            face.candidate=dot(face.normal,cross(difference(point(1),point(0)),difference(point(2),point(0))))>0;
        }
        if(!face.candidate)continue;
        for(std::uint32_t c=0;c<meshPaintCellVertexCount(cell);++c){
            const auto& value=vertex(refined,cell,c);
            if(dot(face.normal,difference(value.position,face.origin))!=0){face.candidate=false;break;}
            // The outer polygon is part of the surface as well. Float-rounded
            // edge midpoints must still lie exactly on their source segments;
            // coplanarity alone would permit a slightly different boundary.
            const auto& original=coarse->cells.at(cell.sourceFace);
            const auto uv=cell.sourceCoordinates.at(meshPaintCellLogicalCorner(cell,c));
            for(unsigned edge=0;edge<original.cornerCount;++edge){
                const auto a=original.sourceCoordinates[edge],b=original.sourceCoordinates[(edge+1)%original.cornerCount];
                const double x=double(b[0])-a[0],y=double(b[1])-a[1],u=double(uv[0])-a[0],v=double(uv[1])-a[1];
                if(x*v-y*u!=0)continue;
                const auto first=coarse->vertices.at(original.vertices[edge]).position,last=coarse->vertices.at(original.vertices[(edge+1)%original.cornerCount]).position;
                const auto deviation=cross(difference(value.position,first),difference(last,first));
                if(dot(deviation,deviation)!=0){face.candidate=false;break;}
            }
            if(!face.candidate)break;
            if(!face.normalSet){face.shadingNormal=value.normal;face.normalSet=true;}
            else if(value.normal!=face.shadingNormal){face.candidate=false;break;}
        }
    }
    const auto simplify=[](const Face& face){return face.candidate&&face.seen&&face.normalSet;};
    if(std::none_of(faces.begin(),faces.end(),simplify))return {};
    ModelPrimitive result;result.material=source.material;result.uvMask=source.uvMask;result.tangentUv=source.tangentUv;result.bounds=refined.bounds;
    auto remapped=retainedBoundary&&fineBoundary?std::make_shared<SurfaceBoundaryLineage>():nullptr;uint32_t originalTriangle{};
    std::vector<bool> written(faces.size());
    for(const auto& cell:refined.cells){
        cancelled(stop);const auto& face=faces[cell.sourceFace];
        const uint32_t sourceTriangle=originalTriangle;originalTriangle+=cell.cornerCount-2u;
        if(simplify(face)){
            if(written[cell.sourceFace])continue;written[cell.sourceFace]=true;
            const auto& original=coarse->cells.at(cell.sourceFace);const auto first=static_cast<std::uint32_t>(result.vertices.size());
            for(unsigned c=0;c<original.cornerCount;++c){auto value=coarse->vertices.at(original.vertices[c]);value.normal=face.shadingNormal;result.vertices.push_back(value);}
            for(unsigned c=0;c<3u*(original.cornerCount-2u);++c)result.indices.push_back(first+original.triangleCorners[c]);
        }else{
            const auto firstTriangle=static_cast<uint32_t>(result.indices.size()/3);
            const auto first=static_cast<std::uint32_t>(result.vertices.size());const auto count=meshPaintCellVertexCount(cell);
            for(std::uint32_t c=0;c<count;++c)result.vertices.push_back(vertex(refined,cell,c));
            for(unsigned c=0;c<3u*(cell.cornerCount-2u);++c)result.indices.push_back(first+(cell.shadingVertexCount==6?c:cell.triangleCorners[c]));
            if(remapped)for(unsigned t=0;t<cell.cornerCount-2u;++t){const auto& records=fineBoundary->triangles;const auto at=std::lower_bound(records.begin(),records.end(),sourceTriangle+t,[](const auto& record,uint32_t id){return record.triangle<id;});if(at!=records.end()&&at->triangle==sourceTriangle+t){auto record=*at;record.triangle=firstTriangle+t;remapped->triangles.push_back(record);}}
        }
    }
    result.sourceVertexCount=result.vertices.size();result.sourceIndexCount=result.indices.size();if(retainedBoundary&&remapped&&!remapped->triangles.empty())*retainedBoundary=std::move(remapped);return result;
}
}
