#include "DecalCache.h"
#include "resources/CacheIO.h"
#include <cmath>
#include <cstring>
#include <map>
#include <set>

namespace crimson::decals {
namespace {
namespace io=streaming::detail;
constexpr std::array<char,8> Magic{'C','R','D','E','C','A','L','1'};
constexpr uint64_t MaximumFileBytes=64ull*1024*1024;
struct Reader {
    std::string_view bytes;
    template<class T>T take(){if(bytes.size()<sizeof(T))throw std::runtime_error("Incomplete decal cache.");T value;std::memcpy(&value,bytes.data(),sizeof(T));bytes.remove_prefix(sizeof(T));return value;}
};
uint64_t checksum(std::string_view bytes){uint64_t result=io::HashSeed;io::hash(result,bytes.data(),bytes.size());return result;}
bool validLimits(const DecalBuildLimits&limits){return limits.maximumTriangles>=4&&limits.maximumTriangles<=1'000'000&&limits.maximumInputTriangles>0&&std::isfinite(limits.maximumStretch)&&limits.maximumStretch>=1;}
void cancelled(std::stop_token stop){if(stop.stop_requested())throw std::runtime_error("Decal cache preparation cancelled.");}
using SourceKey=std::pair<ReceiverId,uint32_t>;
struct SourceReference {const SurfaceReceiver*receiver;const SurfaceTriangle*triangle;Mat4 normal;};
std::map<SourceKey,SourceReference> indexSources(const DecalSurfaceSnapshot&snapshot,const DecalBuildLimits&limits,std::stop_token stop){
    std::map<SourceKey,SourceReference> result;uint64_t count=0;
    for(const auto&receiver:snapshot.receivers){cancelled(stop);const auto normal=math::normalMatrix(receiver.localToWorld);if(!normal)throw std::runtime_error("Invalid cached receiver transform.");
        count+=receiver.triangles.size();if(count>limits.maximumInputTriangles)throw std::runtime_error("Cached source exceeds its geometry budget.");
        for(const auto&triangle:receiver.triangles)if(!result.emplace(SourceKey{receiver.id,triangle.triangle},SourceReference{&receiver,&triangle,*normal}).second)throw std::runtime_error("Ambiguous cached source provenance.");
    }return result;
}
Vec3 sourceNormal(const SourceReference&source,Vec3 bary){
    const auto&t=*source.triangle;const auto geometric=math::normalized(math::cross(t.positions[1]-t.positions[0],t.positions[2]-t.positions[0]));
    Vec3 result{};for(size_t i=0;i<3;++i)result=result+math::normalized(math::transformDirection(source.normal,math::normalized(t.normals[i],geometric)))*bary[i];
    return math::normalized(result,math::normalized(math::transformDirection(source.normal,geometric)));
}
}
std::filesystem::path decalCachePath(const std::filesystem::path&root,uint64_t recipe){return root/".crimson"/"cache"/"decals"/(io::hex(recipe)+".bin");}
std::optional<DecalPatch> readDecalCache(const std::filesystem::path&path,uint64_t recipe,const DecalSurfaceSnapshot&snapshot,const DecalBuildRequest&request,std::stop_token stop){
    try{
        cancelled(stop);if(!validLimits(request.limits))return {};
        std::ifstream input(path,std::ios::binary|std::ios::ate);if(!input)return {};const auto length=input.tellg();
        if(length<0||static_cast<uint64_t>(length)>MaximumFileBytes||length<36)return {};
        std::string bytes(static_cast<size_t>(length),'\0');input.seekg(0);io::readBytes(input,bytes.data(),bytes.size());
        Reader reader{bytes};if(reader.take<std::array<char,8>>()!=Magic||reader.take<uint32_t>()!=DecalChartCacheVersion||reader.take<uint64_t>()!=recipe)return {};
        const auto size=reader.take<uint64_t>(),expectedChecksum=reader.take<uint64_t>();if(size!=reader.bytes.size()||checksum(reader.bytes)!=expectedChecksum)return {};
        DecalPatch result;const auto status=reader.take<uint32_t>();if(status>1)return {};result.status=status?DecalPatchStatus::Incomplete:DecalPatchStatus::Ready;result.message=decalPatchStatusName(result.status);
        const auto strategy=reader.take<uint32_t>();if(strategy>1)return {};result.statistics.usedDevelopableUnfolding=strategy!=0;
        result.statistics.maximumStretch=reader.take<double>();if(!std::isfinite(result.statistics.maximumStretch)||result.statistics.maximumStretch<1-1e-6||result.statistics.maximumStretch>request.limits.maximumStretch+1e-6)return {};
        const auto chunks=reader.take<uint32_t>();if(!chunks||chunks>request.limits.maximumTriangles||chunks>snapshot.receivers.size())return {};
        const auto sources=indexSources(snapshot,request.limits,stop);const auto seed=sources.find({request.seed.receiver,request.seed.triangle});if(seed==sources.end()||!math::isAffine(request.frame))return {};
        const auto&seedTriangle=*seed->second.triangle;const auto seedNormal=math::normalized(math::transformDirection(seed->second.normal,math::cross(seedTriangle.positions[1]-seedTriangle.positions[0],seedTriangle.positions[2]-seedTriangle.positions[0])));
        const double frameParity=math::dot(math::cross(math::transformDirection(request.frame,{1,0,0}),math::transformDirection(request.frame,{0,1,0})),seedNormal);if(!std::isfinite(frameParity)||std::abs(frameParity)<1e-12)return {};
        std::set<ReceiverId>seen;uint64_t totalVertices=0,totalIndices=0;
        for(uint32_t chunkIndex=0;chunkIndex<chunks;++chunkIndex){cancelled(stop);DecalPatchChunk chunk;chunk.receiver={reader.take<uint64_t>(),reader.take<uint32_t>(),reader.take<uint32_t>()};if(!seen.insert(chunk.receiver).second)return {};
            const auto vertices=reader.take<uint32_t>(),indices=reader.take<uint32_t>();totalVertices+=vertices;totalIndices+=indices;
            if(vertices<3||indices<3||indices%3||totalVertices>uint64_t(request.limits.maximumTriangles)*3||totalIndices>uint64_t(request.limits.maximumTriangles)*3||uint64_t(vertices)*36+uint64_t(indices)*4>reader.bytes.size())return {};
            chunk.vertices.reserve(vertices);chunk.indices.reserve(indices);
            for(uint32_t v=0;v<vertices;++v){if((v&1023)==0)cancelled(stop);DecalPatchVertex vertex;vertex.position=reader.take<Vec3>();vertex.uv=reader.take<Vec2>();vertex.sourceTriangle=reader.take<uint32_t>();vertex.barycentrics=reader.take<Vec3>();
                if(!math::finite(vertex.position)||!math::finite(vertex.uv)||!math::finite(vertex.barycentrics)||vertex.uv[0]<0||vertex.uv[0]>1||vertex.uv[1]<0||vertex.uv[1]>1)return {};
                const auto bary=vertex.barycentrics;if(std::min({bary[0],bary[1],bary[2]})< -1e-4f||std::max({bary[0],bary[1],bary[2]})>1.0001f||std::abs(bary[0]+bary[1]+bary[2]-1)>1e-4)return {};
                const auto source=sources.find({chunk.receiver,vertex.sourceTriangle});if(source==sources.end())return {};const auto&t=*source->second.triangle;
                const Vec3 exact=math::transformPoint(source->second.receiver->localToWorld,t.positions[0]*bary[0]+t.positions[1]*bary[1]+t.positions[2]*bary[2]);
                const double tolerance=std::max(2e-5,8*std::numeric_limits<float>::epsilon()*std::max({1.,std::abs(double(exact[0])),std::abs(double(exact[1])),std::abs(double(exact[2]))}));
                if(!math::finite(exact)||math::length(exact-vertex.position)>tolerance)return {};
                vertex.position=exact;vertex.baseNormal=sourceNormal(source->second,bary);if(!math::finite(vertex.baseNormal))return {};
                chunk.generation=source->second.receiver->generation;chunk.vertices.push_back(vertex);
            }
            for(uint32_t i=0;i<indices;++i){const auto index=reader.take<uint32_t>();if(index>=vertices)return {};chunk.indices.push_back(index);}
            for(size_t i=0;i<indices;i+=3){const auto&a=chunk.vertices[chunk.indices[i]],&b=chunk.vertices[chunk.indices[i+1]],&c=chunk.vertices[chunk.indices[i+2]];if(a.sourceTriangle!=b.sourceTriangle||a.sourceTriangle!=c.sourceTriangle)return {};const auto x=b.uv[0]-a.uv[0],y=b.uv[1]-a.uv[1],xx=c.uv[0]-a.uv[0],yy=c.uv[1]-a.uv[1];const double uvArea=double(x)*yy-double(y)*xx;if(std::abs(uvArea)<1e-14)return {};
                const auto&source=sources.at({chunk.receiver,a.sourceTriangle});const auto&t=*source.triangle;const auto normal=math::transformDirection(source.normal,math::cross(t.positions[1]-t.positions[0],t.positions[2]-t.positions[0]));
                // Raster V opposes authored local +Y, including mirrored frames.
                if(math::dot(math::cross(b.position-a.position,c.position-a.position),normal)*uvArea*frameParity>=0)return {};
            }
            result.chunks.push_back(std::move(chunk));
        }
        if(!reader.bytes.empty())return {};result.statistics.outputTriangles=static_cast<uint32_t>(totalIndices/3);return result;
    }catch(const std::exception&){return {};}
}
bool writeDecalCache(const std::filesystem::path&path,uint64_t recipe,const DecalPatch&patch,const DecalBuildLimits&limits,std::stop_token stop){
    try{
        cancelled(stop);if(!patch.usable()||!validLimits(limits)||patch.chunks.empty()||patch.chunks.size()>limits.maximumTriangles)return false;
        std::string payload;io::append(payload,uint32_t(patch.status==DecalPatchStatus::Incomplete));io::append(payload,uint32_t(patch.statistics.usedDevelopableUnfolding));io::append(payload,patch.statistics.maximumStretch);io::append(payload,static_cast<uint32_t>(patch.chunks.size()));
        uint64_t totalVertices=0,totalIndices=0;
        for(const auto&chunk:patch.chunks){cancelled(stop);totalVertices+=chunk.vertices.size();totalIndices+=chunk.indices.size();if(totalVertices>uint64_t(limits.maximumTriangles)*3||totalIndices>uint64_t(limits.maximumTriangles)*3||totalVertices*36+totalIndices*4+patch.chunks.size()*24+56>MaximumFileBytes)return false;
            io::append(payload,chunk.receiver.entity);io::append(payload,chunk.receiver.draw);io::append(payload,chunk.receiver.primitive);io::append(payload,static_cast<uint32_t>(chunk.vertices.size()));io::append(payload,static_cast<uint32_t>(chunk.indices.size()));
            for(const auto&vertex:chunk.vertices){io::append(payload,vertex.position);io::append(payload,vertex.uv);io::append(payload,vertex.sourceTriangle);io::append(payload,vertex.barycentrics);}io::appendBytes(payload,chunk.indices.data(),chunk.indices.size()*sizeof(uint32_t));
        }
        std::string bytes;io::append(bytes,Magic);io::append(bytes,DecalChartCacheVersion);io::append(bytes,recipe);io::append(bytes,static_cast<uint64_t>(payload.size()));io::append(bytes,checksum(payload));bytes+=payload;
        if(bytes.size()>MaximumFileBytes)return false;cancelled(stop);io::writeCacheFileAtomically(path,bytes);return true;
    }catch(const std::exception&){return false;}
}
}
