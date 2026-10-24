#include "VertexPaintSubdivisionOverlay.h"
#include <crimson/renderer/core/Profiler.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace crimson {
std::shared_ptr<const CollisionOverlayGeometry> buildVertexPaintSubdivisionOverlay(const MeshPaintGeometry& source,std::stop_token stop) {
    CRIMSON_PROFILE_ZONE("Vertex paint subdivision overlay worker");
    const auto canceled=[&]{if(stop.stop_requested())throw std::runtime_error("Subdivision overlay preparation canceled.");};
    canceled();
    if(source.vertices.size()>MaximumMeshPaintVertices||source.cells.size()>MaximumMeshPaintTriangles)
        throw std::runtime_error("Subdivision overlay exceeds the mesh paint geometry limits.");
    auto result=std::make_shared<CollisionOverlayGeometry>();result->vertices.reserve(source.vertices.size());
    for(std::size_t index=0;index<source.vertices.size();++index) {
        if((index&1023)==0)canceled();
        const auto point=source.vertices[index].position;
        if(!math::finite(point))throw std::runtime_error("Subdivision overlay contains a nonfinite position.");
        result->vertices.push_back(point);
    }
    struct EdgeUse {std::uint64_t key{};Vec3 normal{};std::uint32_t opposite{};};
    std::vector<EdgeUse> uses;uses.reserve(source.cells.size()*4);
    for(std::size_t index=0;index<source.cells.size();++index) {
        if((index&1023)==0)canceled();
        const auto& cell=source.cells[index];
        if(cell.cornerCount!=3&&cell.cornerCount!=4)throw std::runtime_error("Subdivision overlay has an invalid logical cell.");
        for(unsigned corner=0;corner<cell.cornerCount;++corner)
            if(cell.vertices[corner]>=source.vertices.size())throw std::runtime_error("Subdivision overlay has an invalid corner index.");
        const auto triangleCount=unsigned(cell.cornerCount)-2;
        for(unsigned corner=0;corner<triangleCount*3;++corner)
            if(cell.triangleCorners[corner]>=cell.cornerCount)throw std::runtime_error("Subdivision overlay has an invalid triangle corner.");
        for(unsigned corner=0;corner<cell.cornerCount;++corner) {
            const auto next=(corner+1)%cell.cornerCount;
            const auto first=cell.vertices[corner],second=cell.vertices[next];
            if(first==second||result->vertices[first]==result->vertices[second])continue;
            // A warped quad has two actual surface planes. Use the triangle
            // incident to this perimeter edge for stable coplanar wire depth.
            bool found=false;
            for(unsigned triangle=0;triangle<triangleCount&&!found;++triangle)for(unsigned edge=0;edge<3&&!found;++edge) {
                const auto a=cell.triangleCorners[triangle*3+edge],b=cell.triangleCorners[triangle*3+(edge+1)%3];
                if(!((a==corner&&b==next)||(a==next&&b==corner)))continue;
                const auto opposite=cell.vertices[cell.triangleCorners[triangle*3+(edge+2)%3]];
                const auto normal=math::normalized(math::cross(result->vertices[cell.vertices[b]]-result->vertices[cell.vertices[a]],
                    result->vertices[opposite]-result->vertices[cell.vertices[a]]),Vec3{},1e-15);
                const auto low=std::min(first,second),high=std::max(first,second);
                uses.push_back({(std::uint64_t(low)<<32)|high,normal,opposite});found=true;
            }
            if(!found)throw std::runtime_error("Subdivision overlay perimeter is absent from its surface triangles.");
        }
    }
    canceled();std::sort(uses.begin(),uses.end(),[](const auto& a,const auto& b){return a.key<b.key;});canceled();
    std::size_t unique{};for(std::size_t index=0;index<uses.size();++index)if(index==0||uses[index-1].key!=uses[index].key)++unique;
    result->edges.reserve(unique);
    for(std::size_t first=0;first<uses.size();) {
        if((first&1023)==0)canceled();
        auto last=first+1;while(last<uses.size()&&uses[last].key==uses[first].key)++last;
        const auto a=std::uint32_t(uses[first].key>>32),b=std::uint32_t(uses[first].key);
        const auto normalA=uses[first].normal,normalB=last-first>1?uses[first+1].normal:normalA;
        const bool fallback=last-first>2||math::length(normalA)==0||math::length(normalB)==0;
        const bool concave=!fallback&&last-first==2&&math::dot(normalA,result->vertices[uses[first+1].opposite]-result->vertices[a])>0;
        result->edges.push_back({a,b,normalA,normalB,false,concave,fallback});first=last;
    }
    canceled();return result;
}
}
