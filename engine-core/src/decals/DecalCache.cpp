#include "DecalCache.h"
#include "DecalInternal.h"
#include "DecalOrientation.h"
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
bool validLimits(const DecalBuildLimits&limits){return limits.maximumTriangles>=4&&limits.maximumTriangles<=1'000'000&&limits.maximumInputTriangles>0&&limits.maximumInputTriangles<=1'000'000&&std::isfinite(limits.maximumStretch)&&limits.maximumStretch>=1;}
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
double roundingRadius(float value){
    const float above=std::nextafter(value,std::numeric_limits<float>::infinity()),below=std::nextafter(value,-std::numeric_limits<float>::infinity());
    const double upper=std::isfinite(above)?double(above)-value:0,lower=std::isfinite(below)?double(value)-below:0;
    return .5*std::max(upper,lower);
}
struct QuantizedArea {double value,error;};
QuantizedArea quantizedArea(float ax,float ay,float bx,float by,float cx,float cy){
    const double ex=double(bx)-ax,ey=double(by)-ay,fx=double(cx)-ax,fy=double(cy)-ay;
    const double exError=roundingRadius(bx)+roundingRadius(ax),eyError=roundingRadius(by)+roundingRadius(ay);
    const double fxError=roundingRadius(cx)+roundingRadius(ax),fyError=roundingRadius(cy)+roundingRadius(ay);
    const double error=std::abs(ex)*fyError+std::abs(fy)*exError+exError*fyError+
        std::abs(ey)*fxError+std::abs(fx)*eyError+eyError*fxError+
        32*std::numeric_limits<double>::epsilon()*(std::abs(ex*fy)+std::abs(ey*fx));
    return {detail::robustOrient2D(ax,ay,bx,by,cx,cy),error};
}
bool validTriangle(const DecalPatchVertex&a,const DecalPatchVertex&b,const DecalPatchVertex&c,
    const SourceReference&source,double frameParity,double frameArea,double maximumStretch){
    const auto uv=quantizedArea(a.uv[0],a.uv[1],b.uv[0],b.uv[1],c.uv[0],c.uv[1]);
    const auto bary=quantizedArea(a.barycentrics[1],a.barycentrics[2],b.barycentrics[1],b.barycentrics[2],c.barycentrics[1],c.barycentrics[2]);
    const auto&m=source.receiver->localToWorld;const auto&t=*source.triangle;
    const double parity=detail::dot(detail::column(m,0),detail::cross(detail::column(m,1),detail::column(m,2)));
    // Validate resolved signs in source coordinates. Reader-side float world
    // reprojection can collapse a valid subpixel triangle independently of UV.
    // Half-ULP intervals distinguish a genuine fold from quantization to zero.
    if(std::abs(uv.value)>uv.error&&std::abs(bary.value)>bary.error){
        bool positive=(uv.value>0)==(bary.value>0);if(parity<0)positive=!positive;if(frameParity<0)positive=!positive;
        if(positive)return false;
    }
    const auto p=detail::point(m,detail::d3(t.positions[0])),q=detail::point(m,detail::d3(t.positions[1])),r=detail::point(m,detail::d3(t.positions[2]));
    const double sourceArea=detail::length(detail::cross(q-p,r-p));if(!std::isfinite(sourceArea)||sourceArea<=0)return false;
    const double worldLow=std::max(0.,std::abs(bary.value)-bary.error)*sourceArea,worldHigh=(std::abs(bary.value)+bary.error)*sourceArea;
    const double chartLow=std::max(0.,std::abs(uv.value)-uv.error)*frameArea,chartHigh=(std::abs(uv.value)+uv.error)*frameArea;
    const double stretchSquared=maximumStretch*maximumStretch,roundoff=32*std::numeric_limits<double>::epsilon()*std::max(worldHigh,chartHigh);
    // A collapsed UV must still correspond to a source triangle small enough
    // to collapse within that precision. Large erased/folded cache triangles
    // remain invalid even when their quantized orientation is indeterminate.
    return worldLow<=chartHigh*stretchSquared+roundoff&&chartLow<=worldHigh*stretchSquared+roundoff;
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
        const auto&seedTriangle=*seed->second.triangle;const auto seedNormal=detail::normal(seed->second.receiver->localToWorld,detail::cross(detail::d3(seedTriangle.positions[1])-detail::d3(seedTriangle.positions[0]),detail::d3(seedTriangle.positions[2])-detail::d3(seedTriangle.positions[0])));
        const double frameParity=detail::dot(detail::cross(detail::column(request.frame,0),detail::column(request.frame,1)),seedNormal),frameArea=std::abs(frameParity);if(!std::isfinite(frameParity)||frameArea<1e-12)return {};
        std::set<ReceiverId>seen;uint64_t totalVertices=0,totalIndices=0;
        for(uint32_t chunkIndex=0;chunkIndex<chunks;++chunkIndex){cancelled(stop);DecalPatchChunk chunk;chunk.receiver={reader.take<uint64_t>(),reader.take<uint32_t>(),reader.take<uint32_t>(),reader.take<uint64_t>()};if(!seen.insert(chunk.receiver).second)return {};
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
            for(size_t i=0;i<indices;i+=3){const auto&a=chunk.vertices[chunk.indices[i]],&b=chunk.vertices[chunk.indices[i+1]],&c=chunk.vertices[chunk.indices[i+2]];if(a.sourceTriangle!=b.sourceTriangle||a.sourceTriangle!=c.sourceTriangle)return {};
                if(!validTriangle(a,b,c,sources.at({chunk.receiver,a.sourceTriangle}),frameParity,frameArea,request.limits.maximumStretch))return {};
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
        for(const auto&chunk:patch.chunks){cancelled(stop);totalVertices+=chunk.vertices.size();totalIndices+=chunk.indices.size();if(totalVertices>uint64_t(limits.maximumTriangles)*3||totalIndices>uint64_t(limits.maximumTriangles)*3||totalVertices*36+totalIndices*4+patch.chunks.size()*32+56>MaximumFileBytes)return false;
            io::append(payload,chunk.receiver.entity);io::append(payload,chunk.receiver.draw);io::append(payload,chunk.receiver.primitive);io::append(payload,chunk.receiver.sourceInstanceId);io::append(payload,static_cast<uint32_t>(chunk.vertices.size()));io::append(payload,static_cast<uint32_t>(chunk.indices.size()));
            for(const auto&vertex:chunk.vertices){io::append(payload,vertex.position);io::append(payload,vertex.uv);io::append(payload,vertex.sourceTriangle);io::append(payload,vertex.barycentrics);}io::appendBytes(payload,chunk.indices.data(),chunk.indices.size()*sizeof(uint32_t));
        }
        std::string bytes;io::append(bytes,Magic);io::append(bytes,DecalChartCacheVersion);io::append(bytes,recipe);io::append(bytes,static_cast<uint64_t>(payload.size()));io::append(bytes,checksum(payload));bytes+=payload;
        if(bytes.size()>MaximumFileBytes)return false;cancelled(stop);io::writeCacheFileAtomically(path,bytes);return true;
    }catch(const std::exception&){return false;}
}
}
