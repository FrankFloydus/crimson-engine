#include "resources/StreamingModel.h"
#include "resources/TextureMipCache.h"
#include "assets/AtomicFile.h"
#include "assets/MaterialPreviewGeometry.h"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numbers>
#include <stdexcept>

namespace {
void check(bool condition,const char* text) { if (!condition) throw std::runtime_error(text); }
void cleanup(const std::filesystem::path& root) {
    const auto resolved=std::filesystem::weakly_canonical(root),temporary=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path());
    check(resolved.parent_path()==temporary && resolved.filename().string().starts_with("crimson-streaming-"),"Streaming test cleanup escaped its disposable directory.");
    std::filesystem::remove_all(resolved);
}
template<class F> void rejects(F&& function) { bool rejected=false; try { function(); } catch (...) { rejected=true; } check(rejected,"Malformed cache should fail explicitly."); }
crimson::ModelAsset model() {
    using namespace crimson;
    ModelAsset asset; asset.id="assets/models/fixture/manifest.json"; asset.name="Streaming grid"; asset.format="GLB";
    ModelPrimitive primitive; primitive.uvMask=1; primitive.tangentUv=0;
    constexpr unsigned side=32;
    for (unsigned y=0;y<=side;++y) for (unsigned x=0;x<=side;++x) {
        ModelVertex vertex; vertex.position={static_cast<float>(x)/side,0,static_cast<float>(y)/side}; vertex.uv0={static_cast<float>(x)/side,static_cast<float>(y)/side};
        math::include(asset.bounds,vertex.position); primitive.vertices.push_back(vertex);
    }
    for (unsigned y=0;y<side;++y) for (unsigned x=0;x<side;++x) {
        const auto a=y*(side+1)+x,b=a+1,c=a+side+1,d=c+1;
        primitive.indices.insert(primitive.indices.end(),{a,c,b,b,c,d});
    }
    primitive.sourceVertexCount=primitive.vertices.size(); primitive.sourceIndexCount=primitive.indices.size();
    asset.primitives.push_back(std::move(primitive)); asset.materials.push_back({}); asset.materialIds.emplace_back("fixture-material");
    asset.meshes.push_back({"Grid",{0}}); asset.nodes.push_back({"Grid",{}, {},0}); asset.roots.push_back(0); asset.draws.push_back({0,{}}); return asset;
}
void textureMips(const std::filesystem::path& root) {
    using namespace crimson::streaming;
    const std::vector<std::uint8_t> pixels{0,0,0,255,255,255,255,255};
    const auto color=generateTextureMips(2,1,pixels,TextureRole::Color),data=generateTextureMips(2,1,pixels,TextureRole::Data);
    check(color.size()==2 && color.back().rgba[0]==188 && data.back().rgba[0]==128,"Color/data filtering roles must remain distinct.");
    const std::vector<std::uint8_t> normal{128,128,255,255,128,128,255,255};
    check(generateTextureMips(2,1,normal,TextureRole::Normal).back().rgba==std::vector<std::uint8_t>({128,128,255,255}),"Neutral normal must survive mip generation.");
    const std::vector<std::uint8_t> odd{0,3,6,9,255,6,9,12,0,9,12,15};
    const auto npot=generateTextureMips(3,1,odd,TextureRole::Data);
    check(npot.back().rgba==std::vector<std::uint8_t>({85,6,9,12}),"NPOT mip filter must include all source samples and packed channels.");
    const auto key=textureContentKey(pixels,TextureRole::Color);
    const auto written=writeTextureMipCache(root/"texture.mips",key,TextureRole::Color,color);
    const auto cached=readTextureMipCache(written.file,key,TextureRole::Color);
    check(readTextureMip(cached,1).rgba==color.back().rgba,"Cached mip must be independently readable.");
    rejects([&]{readTextureMipCache(written.file,key+1,TextureRole::Color);});
    rejects([&]{readTextureMipCache(written.file,key,TextureRole::Normal);});
    { std::fstream file(written.file,std::ios::in|std::ios::out|std::ios::binary); file.seekp(static_cast<std::streamoff>(cached.levels[1].offset)); file.put('x'); }
    check(readTextureMip(cached,0).rgba==pixels,"A damaged tail does not force unrelated payload reads.");
    rejects([&]{readTextureMip(cached,1);});
    rejects([&]{generateTextureMips(2,2,pixels,TextureRole::Data);});
    std::vector<MipInfo> levels{{1024,512},{512,256},{256,128},{128,64},{64,32}};
    check(coarseTextureMip(levels)==4,"Coarse first texture request should cap its longest edge at64.");
}
void materialSphere() {
    using namespace crimson;
    const auto sphere=materialPreviewGeometry(); const auto level=materialPreviewGeometryLevel();
    check(sphere==materialPreviewGeometry() && level==materialPreviewGeometryLevel(),"Material preview CPU geometry must be shared across materials.");
    check(sphere->id.starts_with("builtin:") && sphere->primitives.size()==1 && sphere->materials.size()==1 && sphere->draws.size()==1,"Material preview sphere must have one stable built-in primitive and material.");
    const auto& primitive=sphere->primitives.front();
    check(primitive.vertices.size()==33*33 && primitive.indices.size()==6*32*31 && primitive.uvMask==3,"Material preview sphere must retain both UV sets and nondegenerate pole triangles.");
    for(const auto& vertex:primitive.vertices) {
        const Vec3 tangent{vertex.tangent[0],vertex.tangent[1],vertex.tangent[2]};
        check(std::abs(math::dot(vertex.position,vertex.position)-1)<1e-5f && vertex.position==vertex.normal,"Material preview sphere must have radius one and unit outward normals.");
        check(std::abs(math::dot(tangent,tangent)-1)<1e-5f && std::abs(math::dot(tangent,vertex.normal))<1e-5f && vertex.tangent[3]==1,"Preview tangents must form a valid handed orthonormal frame.");
        check(vertex.uv0==vertex.uv1 && vertex.color==Vec4{1,1,1,1},"Material preview UV1 and vertex colors must preserve authored material factors.");
    }
    for(std::uint32_t row=0;row<=32;++row) {
        const auto& a=primitive.vertices[row*33]; const auto& b=primitive.vertices[row*33+32];
        check(a.position==b.position && a.tangent==b.tangent && a.uv0[0]==0 && b.uv0[0]==1,"Sphere UV seam must have exact matching geometry and tangents.");
    }
    for(std::size_t i=0;i<primitive.indices.size();i+=3) {
        const auto& a=primitive.vertices[primitive.indices[i]].position;
        const auto& b=primitive.vertices[primitive.indices[i+1]].position;
        const auto& c=primitive.vertices[primitive.indices[i+2]].position;
        check(math::dot(math::cross(b-a,c-a),a+b+c)>1e-7f,"Material preview sphere triangles must be nondegenerate and outward wound.");
    }
    check(sphere->bounds.valid && sphere->bounds.minimum==Vec3{-1,-1,-1} && sphere->bounds.maximum==Vec3{1,1,1},"Material sphere bounds must remain centered at the origin.");
    check(sphere->nodes.front().local==Mat4{} && sphere->draws.front().transform==Mat4{},"Material preview geometry must not bake a presentation transform.");
    check(sphere->stream && sphere->stream->primitives.front().levels.size()==1 && level->validated && !level->wideIndices && level->indices16.size()==primitive.indices.size(),"Material preview sphere must use one validated tracked GPU LOD.");
}
void geometry(const std::filesystem::path& root) {
    using namespace crimson;
    auto asset=model(); const auto package=root/"assets"/"models"/"fixture"; std::filesystem::create_directories(package);
    writeFileAtomically(package/"manifest.json",R"({"document":"model.gltf","dependencies":[{"kind":"buffer","path":"buffer.bin"}]})");
    writeFileAtomically(package/"model.gltf","{}"); writeFileAtomically(package/"buffer.bin","fixture");
    const auto stream=streaming::buildModelCache(asset,root);
    check(stream.primitives.size()==1 && stream.primitives[0].levels.size()>1,"Continuous geometry should generate compact streaming LODs.");
    const auto& levels=stream.primitives[0].levels;
    const auto original=streaming::loadModelLod(levels.front());
    check(original->geometry.vertices.size()==asset.primitives[0].vertices.size() && original->geometry.indices==asset.primitives[0].indices,"Original authored LOD must remain exact.");
    const auto coarse=streaming::loadModelLod(levels.back(),0,static_cast<std::uint32_t>(levels.size()-1));
    check(coarse->geometry.vertices.size()<original->geometry.vertices.size() && coarse->geometry.indices.size()<original->geometry.indices.size(),"A coarse LOD must compact both vertices and indices.");
    for (const auto& vertex:coarse->geometry.vertices) check(vertex.normal==Vec3{0,1,0} && vertex.tangent==Vec4{1,0,0,1} && vertex.color==Vec4{1,1,1,1},"LOD compaction must preserve attributes.");
    const auto warm=streaming::loadModelMetadataCache(package/"manifest.json",root);
    check(warm && warm->stream && warm->primitives[0].vertices.empty() && warm->primitives[0].sourceVertexCount==asset.primitives[0].vertices.size(),"Warm model load must retain metadata without full geometry.");
    check(warm->draws[0].transform==asset.draws[0].transform && warm->materialIds==asset.materialIds,"Warm cache must preserve transforms and material identities.");
    check(warm->stream->primitives[0].uvSpan[0]==Vec2{1,1},"Warm metadata must retain UV density extents.");
    check(streaming::chooseModelLod(levels,{},asset.bounds,{0,0,.5f},std::numbers::pi_v<float>/4,1080)==0,"Near geometry should request authored detail.");
    check(streaming::chooseModelLod(levels,{},asset.bounds,{0,0,1000},std::numbers::pi_v<float>/4,1080)>0,"Distant geometry should choose bounded-error coarse detail.");
    auto curved=asset;curved.id="curved-fixture";
    for(auto& vertex:curved.primitives[0].vertices)vertex.position[1]=.05f*std::sin(vertex.position[0]*std::numbers::pi_v<float>)*std::sin(vertex.position[2]*std::numbers::pi_v<float>);
    const auto curvedStream=streaming::buildModelCache(curved,root);
    for(const auto& level:curvedStream.primitives[0].levels)check(level.error<=.01f,"Accumulated LOD error must remain within1% of the original geometry scale.");
    ModelAsset wide;wide.id="wide-fixture";ModelPrimitive large;large.vertices.resize(65537);large.indices={0,1,65536};wide.primitives.push_back(std::move(large));
    const auto wideStream=streaming::buildModelCache(wide,root);const auto wideLevel=streaming::loadModelLod(wideStream.primitives[0].levels[0]);
    check(wideLevel->wideIndices && wideLevel->indices16.empty() && wideLevel->validated,"Wide GPU index requirements must be prepared on the worker.");
    writeFileAtomically(package/"buffer.bin","changed");
    check(!streaming::loadModelMetadataCache(package/"manifest.json",root),"Geometry dependency changes must invalidate metadata cache.");
    { std::fstream file(levels.back().file,std::ios::in|std::ios::out|std::ios::binary); file.seekp(-1,std::ios::end); file.put('x'); }
    rejects([&]{streaming::loadModelLod(levels.back());});
    check(streaming::loadModelLod(levels.front())->geometry.indices==asset.primitives[0].indices,"Corrupt coarse payload must not damage original LOD.");
}
}
int main() {
    const auto root=std::filesystem::temp_directory_path()/("crimson-streaming-"+std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    try { std::filesystem::create_directories(root); textureMips(root); geometry(root); materialSphere(); cleanup(root); std::cout<<"Streaming mip/LOD/cache/material-sphere checks passed.\n"; return 0; }
    catch (const std::exception& error) { try { cleanup(root); } catch (...) {} std::cerr<<error.what()<<'\n'; return 1; }
}
