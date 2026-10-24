#include "decals/DecalCache.h"
#include "resources/CacheIO.h"
#include <chrono>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace crimson;
using namespace crimson::decals;
namespace {
void check(bool condition,const char*message){if(!condition)throw std::runtime_error(message);}
DecalSurfaceSnapshot plane(){SurfaceReceiver receiver;receiver.id={17,2,3};receiver.generation=73;
    receiver.triangles={{8,{{{-2,-2,0},{2,-2,0},{2,2,0}}},{{{0,0,1},{.2f,0,1},{0,.2f,1}}}},
                        {9,{{{-2,-2,0},{2,2,0},{-2,2,0}}},{{{0,0,1},{0,.2f,1},{0,0,1}}}}};
    receiver.localToWorld=math::translation({4,5,6})*math::scaling({-2,3,1});return {{receiver}};}
DecalBuildRequest request(){DecalBuildRequest result;result.seed={{17,2,3},8,{.5f,0,.5f}};result.frame=math::translation({4,5,6});return result;}
std::string bytes(const std::filesystem::path&file){std::ifstream input(file,std::ios::binary);return {std::istreambuf_iterator<char>{input},{}};}
void store(const std::filesystem::path&file,const std::string&data){writeFileAtomically(file,data);}
void reseal(std::string&data){uint64_t hash=streaming::detail::HashSeed;streaming::detail::hash(hash,data.data()+36,data.size()-36);std::memcpy(data.data()+28,&hash,sizeof(hash));}
template<class T>void replace(std::string&data,size_t offset,T value){std::memcpy(data.data()+offset,&value,sizeof(value));reseal(data);}
void proofs(const std::filesystem::path&root){
    constexpr uint64_t recipe=0x897613ac;auto surface=plane();const auto buildRequest=request();const auto file=decalCachePath(root,recipe);
    check(!readDecalCache(file,recipe,surface,buildRequest),"Missing cache did not fall through to rebuild.");
    auto patch=buildDecalPatch(surface,buildRequest);check(patch.usable(),"Cache source fixture failed its real chart solve.");
    check(writeDecalCache(file,recipe,patch,buildRequest.limits),"Valid chart did not publish atomically.");
    const auto original=bytes(file);const auto stamp=std::filesystem::last_write_time(file);
    check(writeDecalCache(file,recipe,patch,buildRequest.limits)&&std::filesystem::last_write_time(file)==stamp,"Equal cache publication rewrote immutable bytes.");
    surface.receivers[0].generation=9001;auto restored=readDecalCache(file,recipe,surface,buildRequest);
    check(restored&&restored->chunks.size()==patch.chunks.size()&&restored->statistics.outputTriangles==patch.statistics.outputTriangles,"Chart cache round trip changed topology.");
    check(restored->chunks[0].generation==9001,"Cache retained obsolete renderer resource revision.");
    for(size_t c=0;c<patch.chunks.size();++c){check(restored->chunks[c].indices==patch.chunks[c].indices,"Cache changed triangle indices.");
        for(size_t v=0;v<patch.chunks[c].vertices.size();++v){const auto&a=patch.chunks[c].vertices[v],&b=restored->chunks[c].vertices[v];
            check(math::length(a.position-b.position)<3e-5&&math::length(a.baseNormal-b.baseNormal)<1e-5&&a.uv==b.uv&&a.sourceTriangle==b.sourceTriangle&&a.barycentrics==b.barycentrics,"Cache lost UV, affine normal, or source provenance.");}}
    check(!readDecalCache(file,recipe+1,surface,buildRequest),"Stale recipe cache accepted.");
    auto wrongVersion=original;const uint32_t version=DecalChartCacheVersion-1;std::memcpy(wrongVersion.data()+8,&version,sizeof(version));store(file,wrongVersion);
    check(!readDecalCache(file,recipe,surface,buildRequest),"Obsolete cache version accepted.");
    store(file,original.substr(0,original.size()/2));check(!readDecalCache(file,recipe,surface,buildRequest),"Truncated cache accepted.");
    auto damaged=original;damaged.back()^=0x13;store(file,damaged);check(!readDecalCache(file,recipe,surface,buildRequest),"Damaged checksum accepted.");
    // Recompute the checksum to exercise semantic checks independently of byte integrity.
    // Header 36, fixed payload 20, first chunk header 24; first vertex begins at 80.
    auto invalid=original;replace(invalid,80,std::numeric_limits<float>::quiet_NaN());store(file,invalid);check(!readDecalCache(file,recipe,surface,buildRequest),"Nonfinite cache position accepted.");
    invalid=original;replace(invalid,92,1.5f);store(file,invalid);check(!readDecalCache(file,recipe,surface,buildRequest),"Out-of-domain cache UV accepted.");
    invalid=original;replace(invalid,100,uint32_t{999999});store(file,invalid);check(!readDecalCache(file,recipe,surface,buildRequest),"Missing source triangle accepted.");
    invalid=original;replace(invalid,104,5.f);store(file,invalid);check(!readDecalCache(file,recipe,surface,buildRequest),"Invalid source barycentrics accepted.");
    invalid=original;replace(invalid,80,500.f);store(file,invalid);check(!readDecalCache(file,recipe,surface,buildRequest),"Off-surface cached position accepted.");
    invalid=original;for(size_t i=0;i<sizeof(Vec2);++i)std::swap(invalid[92+i],invalid[128+i]);reseal(invalid);store(file,invalid);check(!readDecalCache(file,recipe,surface,buildRequest),"Folded cache UV orientation accepted.");
    auto upsideDown=patch;for(auto&chunk:upsideDown.chunks)for(auto&vertex:chunk.vertices)vertex.uv[1]=1-vertex.uv[1];
    check(writeDecalCache(file,recipe,upsideDown,buildRequest.limits)&&!readDecalCache(file,recipe,surface,buildRequest),"Obsolete upward raster V escaped cache orientation validation.");
    invalid=original;replace(invalid,72,uint32_t{0xffffffff});store(file,invalid);check(!readDecalCache(file,recipe,surface,buildRequest),"Unbounded vertex allocation accepted.");
    invalid=original;replace(invalid,76,uint32_t{0xffffffff});store(file,invalid);check(!readDecalCache(file,recipe,surface,buildRequest),"Unbounded index allocation accepted.");
    invalid=original;replace(invalid,52,uint32_t{0xffffffff});store(file,invalid);check(!readDecalCache(file,recipe,surface,buildRequest),"Unbounded receiver allocation accepted.");
    // Corruption never touches authored geometry, and normal rebuilding replaces it.
    patch=buildDecalPatch(surface,buildRequest);check(patch.usable()&&writeDecalCache(file,recipe,patch,buildRequest.limits),"Corrupt cache did not recover through the real builder.");
    check(readDecalCache(file,recipe,surface,buildRequest).has_value(),"Rebuilt cache remained unusable.");
    auto moved=surface;moved.receivers[0].localToWorld=math::translation({30,0,0});check(!readDecalCache(file,recipe,moved,buildRequest),"Stale source transform escaped correspondence validation.");
    std::stop_source stop;stop.request_stop();check(!readDecalCache(file,recipe,surface,buildRequest,stop.get_token())&&!writeDecalCache(file,recipe,patch,buildRequest.limits,stop.get_token()),"Canceled cache work still published.");
    check(bytes(file)==original,"Recovery did not restore the deterministic complete payload.");
    const auto blocked=root/"blocked";store(blocked,"A file cannot contain a derived cache directory.");
    check(!writeDecalCache(blocked/"cache.bin",recipe,patch,buildRequest.limits)&&patch.usable(),"Cache write failure damaged a usable CPU patch.");
    auto mirroredRequest=buildRequest;mirroredRequest.frame.at(0,0)=-1;const auto mirrored=buildDecalPatch(surface,mirroredRequest);const auto mirroredFile=decalCachePath(root,recipe+1);
    check(mirrored.usable()&&writeDecalCache(mirroredFile,recipe+1,mirrored,mirroredRequest.limits)&&readDecalCache(mirroredFile,recipe+1,surface,mirroredRequest).has_value(),"Valid authored mirror was rejected as a cache foldover.");
}
}
int main(){const auto root=std::filesystem::temp_directory_path()/("crimson-decal-cache-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try{proofs(root);std::filesystem::remove_all(root);std::cout<<"decal_cache: versioned atomic round trip, current revisions/normals, strict bounded provenance and corrupt/stale recovery proofs passed\n";return 0;
    }catch(const std::exception&error){std::cerr<<error.what()<<'\n';std::error_code ignored;std::filesystem::remove_all(root,ignored);return 1;}}
