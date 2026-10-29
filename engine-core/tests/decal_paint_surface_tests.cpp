#include "decals/DecalPaintSurface.h"
#include "decal_boundary_lineage_fixture.h"
#include "decals/DecalModelSurface.h"
#include "decals/DecalCache.h"
#include "decals/DecalInternal.h"
#include "decal_saved_contact_fixture.h"
#include "decal_saved_contact_models.h"
#include "decal_intrinsic_source_fixture.h"
#include "decal_intrinsic_coverage_fixture.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <map>

using namespace crimson;
using namespace crimson::decals;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Action> void fails(Action action,const char* message){bool failed{};try{action();}catch(const std::exception&){failed=true;}require(failed,message);}
ModelPrimitive quad(float x=0,float warp=0){
    ModelPrimitive result;result.material=7;result.uvMask=3;result.tangentUv=1;
    for(const auto position:std::array<Vec3,4>{{{x,0,0},{x+2,0,0},{x+2,2,warp},{x,2,0}}}){
        ModelVertex value;value.position=position;value.normal={0,0,1};value.uv0={position[0],position[1]};result.vertices.push_back(value);
    }
    result.indices={0,1,2,0,2,3};result.sourceFaces.push_back({{0,1,2,3},{0,1},4});return result;
}
void append(ModelPrimitive& target,const ModelPrimitive& source){
    const auto first=static_cast<std::uint32_t>(target.vertices.size()),triangle=static_cast<std::uint32_t>(target.indices.size()/3);
    target.vertices.insert(target.vertices.end(),source.vertices.begin(),source.vertices.end());
    for(const auto index:source.indices)target.indices.push_back(first+index);
    for(auto face:source.sourceFaces){for(unsigned c=0;c<face.cornerCount;++c)face.vertices[c]+=first;for(unsigned t=0;t<face.cornerCount-2u;++t)face.triangles[t]+=triangle;target.sourceFaces.push_back(face);}
}
bool sameVertex(const ModelVertex& a,const ModelVertex& b){return std::memcmp(&a,&b,sizeof(ModelVertex))==0;}
void exactPlane(){
    const auto source=quad();const auto geometry=buildMeshPaintGeometry(source,5);const auto before=meshPaintRenderPrimitive(source,*geometry);
    const auto proxy=simplifyDecalPaintPrimitive(source,*geometry);require(proxy.has_value(),"A redundant planar paint grid was retained.");
    require(before.indices.size()==6144&&proxy->indices.size()==6&&proxy->vertices.size()==4,"Planar paint-only refinement did not collapse to its source quad.");
    require(proxy->material==source.material&&proxy->uvMask==source.uvMask&&proxy->tangentUv==source.tangentUv,"Receiver material/UV metadata changed.");
    for(std::size_t i=0;i<source.vertices.size();++i)require(sameVertex(proxy->vertices[i],source.vertices[i]),"Planar receiver proxy changed an original corner.");
    require(geometry->indices.size()==6144&&geometry->cells.size()==1024&&source.indices.size()==6,"Decal preparation changed the rendered paint geometry.");
    require(!simplifyDecalPaintPrimitive(source,*buildMeshPaintGeometry(source,0)),"Unrefined paint geometry gained a redundant proxy.");
    ModelAsset dense;dense.primitives.push_back(before);dense.draws.push_back({0});ModelAsset reduced;reduced.primitives.push_back(*proxy);reduced.draws=dense.draws;
    const auto denseSurface=buildModelSurface(dense,1,SourceNormals::UseMeshNormals),proxySurface=buildModelSurface(reduced,2,SourceNormals::UseMeshNormals);
    const auto world=math::translation({3,2,25})*math::scaling({1.5f,.75f,-2});const auto point=math::transformPoint(world,{.371f,.517f,0});
    const auto a=resolveModelSurfaceHit(denseSurface,91,world,0,0,point),b=resolveModelSurfaceHit(proxySurface,91,world,0,0,point);
    require(a.hit&&b.hit&&math::length(a.hit->localPoint-b.hit->localPoint)<1e-6f&&a.hit->localNormal==b.hit->localNormal,"Receiver proxy changed an affine visible surface hit or its normal.");
}
void mixedGeometryAndNormals(){
    auto source=quad();append(source,quad(3,3.29e-6f));auto varying=quad(6);varying.vertices[2].normal={0,.5f,.8660254f};append(source,varying);
    const auto geometry=buildMeshPaintGeometry(source,3);const auto dense=meshPaintRenderPrimitive(source,*geometry);const auto proxy=simplifyDecalPaintPrimitive(source,*geometry);
    require(proxy&&proxy->indices.size()==6+2*384,"Mixed receiver did not keep warped/varying-normal faces at their real density.");
    std::size_t at=0;for(std::size_t i=0;i<dense.indices.size();++i){const auto& value=dense.vertices[dense.indices[i]];if(value.position[0]<3)continue;
        while(at<proxy->indices.size()&&proxy->vertices[proxy->indices[at]].position[0]<3)++at;
        require(at<proxy->indices.size()&&sameVertex(value,proxy->vertices[proxy->indices[at++]]),"A retained dense triangle or shading normal changed.");}
    auto warped=quad(0,3.29e-6f);require(!simplifyDecalPaintPrimitive(warped,*buildMeshPaintGeometry(warped,3)),"Micrometre-scale warped geometry was flattened.");
    auto subtle=quad();subtle.vertices[1].normal={1e-7f,0,1};require(!simplifyDecalPaintPrimitive(subtle,*buildMeshPaintGeometry(subtle,3)),"Small authored normal variation was discarded.");
    auto reversed=quad();reversed.indices={0,1,2,0,3,2};require(!simplifyDecalPaintPrimitive(reversed,*buildMeshPaintGeometry(reversed,3)),"An opposite original triangle winding replaced refined polygon winding.");
}
void shadingAliasesAndTriangle(){
    auto source=quad();for(unsigned i=3;i<6;++i){const auto index=source.indices[i];auto value=source.vertices[index];value.uv0[0]=-value.uv0[0];source.indices[i]=static_cast<std::uint32_t>(source.vertices.size());source.vertices.push_back(value);}
    const auto geometry=buildMeshPaintGeometry(source,3);const auto proxy=simplifyDecalPaintPrimitive(source,*geometry);
    require(proxy&&proxy->indices.size()==6,"A UV-only shading seam prevented exact decal surface simplification.");
    source.vertices[source.indices[3]].normal={0,1,0};
    require(!simplifyDecalPaintPrimitive(source,*buildMeshPaintGeometry(source,3)),"A source-triangle normal seam was averaged by the decal proxy.");
    auto triangle=quad();triangle.vertices.resize(3);triangle.indices={0,1,2};triangle.sourceFaces.clear();
    const auto triangleProxy=simplifyDecalPaintPrimitive(triangle,*buildMeshPaintGeometry(triangle,3));
    require(triangleProxy&&triangleProxy->indices.size()==3,"A constant-normal triangle's paint corner patches were retained.");
}
void invalidAndCancellation(){
    const auto source=quad();const auto geometry=buildMeshPaintGeometry(source,3);auto incompatible=quad();incompatible.vertices[0].position[0]=-.25f;
    fails([&]{simplifyDecalPaintPrimitive(incompatible,*geometry);},"Incompatible source provenance was silently accepted.");
    std::stop_source stop;stop.request_stop();fails([&]{simplifyDecalPaintPrimitive(source,*geometry,stop.get_token());},"Cancelled decal surface preparation continued.");
}
void savedMainContactWorkflows(){
    namespace saved=crimson::decals::test::saved_contact;
    struct Source {uint64_t entity;Mat4 world;ModelSurface surface;};
    const auto prepare=[](ModelAsset original) {
        for(auto& primitive:original.primitives) {
            const auto geometry=buildMeshPaintGeometry(primitive,5);
            const auto proxy=simplifyDecalPaintPrimitive(primitive,*geometry);
            primitive=proxy?*proxy:meshPaintRenderPrimitive(primitive,*geometry);
        }
        return buildModelSurface(original,1,SourceNormals::UseMeshNormals);
    };
    const auto minimal=saved::ceilingSurface();
    const std::array<Source,2> sources{{
        {0x6f1f1f91d24b1ed1ull,minimal.receivers[0].localToWorld,prepare(saved::roomModel())},
        {0x70793cb67cef2449ull,minimal.receivers[1].localToWorld,prepare(saved::pillarModel())}
    }};
    struct TemporaryCache {
        std::filesystem::path file=std::filesystem::temp_directory_path()/("crimson-saved-contact-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".bin");
        ~TemporaryCache(){std::error_code ignored;std::filesystem::remove(file,ignored);}
    } cache;
    bool cacheFailure=false;
    for(const unsigned scenario:{0u,1u,2u}) {
        const bool ceiling=scenario==0,includeRoom=scenario!=1;
        const char* label=ceiling?"Saved ceiling":includeRoom?"Saved pillar with room barrier":"Saved pillar without room barrier";
        auto request=ceiling?saved::ceilingRequest():saved::pillarRequest();
        const auto& seed=ceiling?sources[0]:sources[1];
        const auto query=decalSourceQuery(seed.surface,seed.world,request);
        DecalSurfaceSnapshot snapshot;size_t selected{};
        for(const auto& source:sources)if(includeRoom||source.entity==seed.entity)for(const auto& draw:source.surface.draws) {
            const auto indices=querySurfaceTriangles(draw,source.world,query);if(indices.empty())continue;
            auto receiver=draw.surface;receiver.id.entity=source.entity;receiver.localToWorld=source.world*receiver.localToWorld;receiver.triangles.clear();
            for(const auto index:indices)receiver.triangles.push_back(draw.surface.triangles[index]);selected+=indices.size();snapshot.receivers.push_back(std::move(receiver));
        }
        require(selected==(ceiling?11532u:includeRoom?8025u:6050u),"Saved fixture stopped reproducing the actual level5 source selection.");
        request.limits.maximumTriangles=static_cast<uint32_t>(std::clamp<size_t>(selected*2,16384,65536));
        const auto patch=buildDecalPatch(snapshot,request);
        if(!patch.usable())throw std::runtime_error(std::string(label)+": "+patch.message);
        require(patch.statistics.inputTriangles==selected&&patch.statistics.analysisTriangles<100000&&patch.statistics.outputTriangles<=request.limits.maximumTriangles,
            "Saved source preparation exceeded its bounded work/publication contract.");
        double area{},frontArea{},sideArea{},maximumError{};const auto front=math::normalized(math::transformDirection(request.frame,{0,0,1}));
        for(const auto& chunk:patch.chunks) {
            const auto receiver=std::find_if(snapshot.receivers.begin(),snapshot.receivers.end(),[&](const auto& value){return value.id==chunk.receiver;});
            require(receiver!=snapshot.receivers.end()&&receiver->generation==chunk.generation,"Saved source receiver identity/generation changed.");
            std::unordered_map<uint32_t,const SurfaceTriangle*> triangles;for(const auto& triangle:receiver->triangles)triangles.emplace(triangle.triangle,&triangle);
            for(const auto& vertex:chunk.vertices) {
                const auto source=triangles.find(vertex.sourceTriangle);require(source!=triangles.end(),"Saved source triangle identity changed.");
                Vec3 local;for(size_t i=0;i<3;++i)local=local+source->second->positions[i]*vertex.barycentrics[i];
                maximumError=std::max(maximumError,math::length(math::transformPoint(receiver->localToWorld,local)-vertex.position));
                require(vertex.uv[0]>=0&&vertex.uv[0]<=1&&vertex.uv[1]>=0&&vertex.uv[1]<=1,"Saved source emitted UVs beyond the image.");
            }
            for(size_t i=0;i<chunk.indices.size();i+=3) {
                const auto& a=chunk.vertices.at(chunk.indices[i]);const auto& b=chunk.vertices.at(chunk.indices[i+1]);const auto& c=chunk.vertices.at(chunk.indices[i+2]);
                area+=.5*std::abs((double(b.uv[0])-a.uv[0])*(double(c.uv[1])-a.uv[1])-(double(b.uv[1])-a.uv[1])*(double(c.uv[0])-a.uv[0]));
                if(chunk.receiver==request.seed.receiver){const auto cross=math::cross(b.position-a.position,c.position-a.position);const auto normal=math::normalized(cross,{0,0,0},0);const double worldArea=.5*math::length(cross),dot=math::dot(normal,front);if(dot>.9)frontArea+=worldArea;if(std::abs(dot)<.1){sideArea+=worldArea;if(!ceiling&&includeRoom&&worldArea>1e-14)require(normal[2]<-.99f,"Saved pillar crossed its opposite-normal coplanar room-wall barrier.");}}
            }
        }
        std::cout<<label<<" input="<<selected<<" analysis="<<patch.statistics.analysisTriangles<<" output="<<patch.statistics.outputTriangles
            <<" UVarea="<<area<<" frontArea="<<frontArea<<" sideArea="<<sideArea<<" provenanceError="<<maximumError<<'\n';
        // At Z=25.75 the full room has an opposite-normal wall over the pillar
        // side. That multiplicity must stop traversal. The visible side starts
        // at the other front corner; derive its retained width from the saved
        // placement, rather than demanding a bridge across the blocked side.
        double expectedArea=1,expectedSide=.5;
        if(!ceiling&&includeRoom) {
            const auto visibleCorner=math::transformPoint(seed.world,{.5f,0,-.5f});
            const auto origin=math::transformPoint(request.frame,{}),axis=math::normalized(math::transformDirection(request.frame,{1,0,0}));
            expectedSide=.5*math::length(math::transformDirection(request.frame,{1,0,0}))-math::dot(origin-visibleCorner,axis);
            expectedArea=.5+expectedSide;
            require(patch.status==DecalPatchStatus::Incomplete,"Opposite-normal coplanar overlap stopped reporting its genuine incomplete footprint.");
            for(const auto& chunk:patch.chunks)require(chunk.receiver==request.seed.receiver,"Saved pillar unexpectedly crossed its room-wall ambiguity barrier.");
        }
        require(std::abs(area-expectedArea)<.005,"Saved decal lost part of its available image coverage or bridged an ambiguity barrier.");
        require(maximumError<2e-5,"Saved decal moved away from its original painted surface.");
        if(!ceiling)require(std::abs(frontArea-.5)<.02&&std::abs(sideArea-expectedSide)<.02,"Saved pillar decal failed to cover its available front and side surfaces.");
        const uint64_t recipe=0xc3111a9u+scenario;
        require(writeDecalCache(cache.file,recipe,patch,request.limits),"Repaired saved decal could not publish its disposable cache.");
        const auto restored=readDecalCache(cache.file,recipe,snapshot,request);
        if(!restored) {
            const auto diagnostic=std::filesystem::path(__FILE__).parent_path().parent_path()/"build"/"decal-cache-diagnostics"/("saved-contact-"+std::to_string(scenario)+".bin");
            std::filesystem::create_directories(diagnostic.parent_path());std::filesystem::copy_file(cache.file,diagnostic,std::filesystem::copy_options::overwrite_existing);
            std::cerr<<label<<" cache read rejected diagnostic="<<diagnostic.string()<<'\n';cacheFailure=true;continue;
        }
        require(restored->chunks.size()==patch.chunks.size()&&restored->statistics.outputTriangles==patch.statistics.outputTriangles,"Repaired saved decal cache changed publication size.");
        for(size_t c=0;c<patch.chunks.size();++c) {
            const auto& a=patch.chunks[c];const auto& b=restored->chunks[c];
            require(a.receiver==b.receiver&&a.generation==b.generation&&a.indices==b.indices&&a.vertices.size()==b.vertices.size(),"Saved decal cache changed receiver topology.");
            for(size_t v=0;v<a.vertices.size();++v)require(a.vertices[v].uv==b.vertices[v].uv&&a.vertices[v].sourceTriangle==b.vertices[v].sourceTriangle&&a.vertices[v].barycentrics==b.vertices[v].barycentrics,
                "Saved decal cache changed image coordinates or original-source correspondence.");
        }
    }
    require(!cacheFailure,"Repaired saved decal failed its real cache round trip.");
}
void savedIntrinsicDoorway(bool useProxy=false){
    namespace fixture=crimson::decals::test::intrinsic_source;
    auto room=crimson::decals::test::saved_contact::roomModel();
    std::vector<std::shared_ptr<const SurfaceBoundaryLineage>> boundaries;
    for(auto& primitive:room.primitives){const auto geometry=buildMeshPaintGeometry(primitive,5);auto boundary=meshPaintBoundaryLineage(primitive,*geometry);
        std::shared_ptr<const SurfaceBoundaryLineage> retained;auto proxy=useProxy?simplifyDecalPaintPrimitive(primitive,*geometry,{},boundary.get(),&retained):std::optional<ModelPrimitive>{};
        if(proxy){primitive=std::move(*proxy);boundary=std::move(retained);}else primitive=meshPaintRenderPrimitive(primitive,*geometry);boundaries.push_back(std::move(boundary));}
    const auto world=fixture::world();const auto request=fixture::savedRequest();
    std::array<ModelSurface,2> sources{{buildModelSurface(room,1,SourceNormals::UseMeshNormals),buildModelSurface(fixture::archModel(),1,SourceNormals::UseMeshNormals)}};
    for(auto& draw:sources[0].draws)draw.surface.boundary=boundaries.at(draw.surface.id.primitive);
    const auto query=decalSourceQuery(sources[1],world,request);DecalSurfaceSnapshot original;size_t selected{};
    for(size_t i=0;i<sources.size();++i)for(const auto& draw:sources[i].draws){const auto indices=querySurfaceTriangles(draw,world,query);if(indices.empty())continue;
        auto receiver=draw.surface;receiver.id.entity=i?fixture::ArchEntity:0x6f1f1f91d24b1ed1ull;receiver.localToWorld=world*receiver.localToWorld;receiver.triangles.clear();
        for(const auto index:indices)receiver.triangles.push_back(draw.surface.triangles[index]);selected+=indices.size();original.receivers.push_back(std::move(receiver));}
    require(selected==(useProxy?13691u:13850u),"Saved intrinsic doorway stopped reproducing its exact painted source input.");
    for(const bool reversed:{false,true}) {
    auto snapshot=original;if(reversed){std::reverse(snapshot.receivers.begin(),snapshot.receivers.end());for(auto& receiver:snapshot.receivers)std::reverse(receiver.triangles.begin(),receiver.triangles.end());}
    const auto patch=buildDecalPatch(snapshot,request);
    if(!patch.usable())throw std::runtime_error("Saved intrinsic doorway: "+patch.message);
    require(!patch.statistics.usedDevelopableUnfolding&&patch.statistics.inputTriangles==selected&&patch.statistics.analysisTriangles<100000&&patch.statistics.outputTriangles<=request.limits.maximumTriangles,
        "Saved doorway did not exercise its bounded intrinsic chart solve.");
    double uvArea{},frontArea{},depthArea{},maximumError{};bool seedCovered=false;
    for(const auto& chunk:patch.chunks){const auto receiver=std::find_if(snapshot.receivers.begin(),snapshot.receivers.end(),[&](const auto& r){return r.id==chunk.receiver;});
        require(receiver!=snapshot.receivers.end()&&receiver->generation==chunk.generation,"Intrinsic doorway changed receiver identity/generation.");
        std::unordered_map<uint32_t,const SurfaceTriangle*> triangles;for(const auto& t:receiver->triangles)triangles.emplace(t.triangle,&t);
        for(const auto& v:chunk.vertices){require(math::finite(v.position)&&math::finite(v.baseNormal)&&math::finite(v.uv)&&math::finite(v.barycentrics),"Intrinsic doorway published non-finite geometry.");
            const auto t=triangles.find(v.sourceTriangle);require(t!=triangles.end(),"Intrinsic doorway lost original triangle provenance.");Vec3 local;for(size_t k=0;k<3;++k)local=local+t->second->positions[k]*v.barycentrics[k];
            maximumError=std::max(maximumError,math::length(math::transformPoint(receiver->localToWorld,local)-v.position));require(v.uv[0]>=0&&v.uv[0]<=1&&v.uv[1]>=0&&v.uv[1]<=1,"Intrinsic doorway exceeded its authored image bounds.");
            seedCovered|=chunk.receiver==request.seed.receiver&&v.sourceTriangle==request.seed.triangle;}
        for(size_t i=0;i<chunk.indices.size();i+=3){const auto& a=chunk.vertices.at(chunk.indices[i]);const auto& b=chunk.vertices.at(chunk.indices[i+1]);const auto& c=chunk.vertices.at(chunk.indices[i+2]);
            uvArea+=.5*std::abs((double(b.uv[0])-a.uv[0])*(double(c.uv[1])-a.uv[1])-(double(b.uv[1])-a.uv[1])*(double(c.uv[0])-a.uv[0]));
            if(chunk.receiver.entity==fixture::ArchEntity){const auto cross=math::cross(b.position-a.position,c.position-a.position);const double area=.5*math::length(cross);const auto normal=math::normalized(cross,{0,0,0},0);if(normal[2]<-.9)frontArea+=area;if(std::abs(normal[2])<.5)depthArea+=area;}}
    }
    std::cout<<"Saved intrinsic doorway proxy="<<useProxy<<" reversed="<<reversed<<" input="<<selected<<" analysis="<<patch.statistics.analysisTriangles<<" output="<<patch.statistics.outputTriangles<<" UVarea="<<uvArea
        <<" archFrontArea="<<frontArea<<" archDepthArea="<<depthArea<<" provenanceError="<<maximumError<<'\n';
    require(seedCovered&&uvArea>.8&&frontArea>.05&&depthArea>.1,"Intrinsic doorway lost its seed/front/return surface coverage.");
    const auto coverage=crimson::decals::test::assertSavedIntrinsicCoverage(patch,request,0x6f1f1f91d24b1ed1ull);
    std::cout<<"Saved doorway guaranteed front samples="<<coverage.samples<<" arch="<<coverage.archSamples<<" room="<<coverage.roomSamples<<" maximumUVerror="<<coverage.maximumUvError<<'\n';
    require(maximumError<2e-5,"Intrinsic conditioning moved the doorway away from its original source geometry.");

    // The original arch is open at its outer crown: its front47 and the room
    // front are the two incident sheets. Preserve that valid continuation;
    // generic three-sheet ambiguity barriers are covered by the surface tests.
    DecalBuildStatistics statistics;const auto analysis=crimson::decals::detail::buildTopology(snapshot,request,statistics,{});
    using Edge=std::pair<size_t,size_t>;std::map<Edge,std::vector<size_t>> edges;
    for(size_t f=0;f<analysis.faces.size();++f)for(size_t e=0;e<3;++e)edges[std::minmax(analysis.faces[f].v[e],analysis.faces[f].v[(e+1)%3])].push_back(f);
    size_t crownSegments{},roomContinuations{};
    for(const auto& face:analysis.faces){const auto& source=analysis.sources[face.source];const auto& receiver=snapshot.receivers[source.receiver];const auto& triangle=receiver.triangles[source.triangle];
        if(receiver.id.entity!=fixture::ArchEntity||triangle.triangle!=47)continue;
        for(size_t e=0;e<3;++e){const size_t a=face.v[e],b=face.v[(e+1)%3];const auto ba=crimson::decals::detail::barycentric(source,analysis.positions[a]),bb=crimson::decals::detail::barycentric(source,analysis.positions[b]);
            if(std::abs(ba.y)>1e-6||std::abs(bb.y)>1e-6||crimson::decals::detail::length(analysis.positions[a]-analysis.positions[b])<1e-5)continue;++crownSegments;
            for(const auto f:edges.at(std::minmax(a,b))){const auto& other=analysis.sources[analysis.faces[f].source];const auto& r=snapshot.receivers[other.receiver];
                if(r.id.entity==fixture::ArchEntity)require(r.triangles[other.triangle].triangle==47,"Saved doorway crown joined an unrelated arch sheet.");
                else {require(r.id.entity==0x6f1f1f91d24b1ed1ull&&crimson::decals::detail::dot(source.geometricNormal,other.geometricNormal)>.999,
                    "Saved doorway crown joined a hidden or opposing receiver sheet.");++roomContinuations;}}}
    }
    require(crownSegments>0&&roomContinuations>0,"Saved doorway lost the valid arch-to-room front continuation at its actual crown edge.");

    const auto cache=std::filesystem::temp_directory_path()/("crimson-intrinsic-doorway-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".bin");
    struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code ignored;std::filesystem::remove(path,ignored);}} cleanup{cache};
    constexpr uint64_t recipe=0x8ce1cbaae7c7e135ull;require(writeDecalCache(cache,recipe,patch,request.limits),"Intrinsic doorway cache publication failed.");
    const auto restored=readDecalCache(cache,recipe,snapshot,request);require(restored&&restored->chunks.size()==patch.chunks.size()&&restored->statistics.outputTriangles==patch.statistics.outputTriangles,"Intrinsic doorway cache round trip failed.");
    crimson::decals::test::assertSavedIntrinsicCoverage(*restored,request,0x6f1f1f91d24b1ed1ull);
    for(size_t c=0;c<patch.chunks.size();++c){const auto& a=patch.chunks[c];const auto& b=restored->chunks[c];require(a.receiver==b.receiver&&a.indices==b.indices&&a.vertices.size()==b.vertices.size(),"Intrinsic doorway cache changed receiver topology.");
        for(size_t v=0;v<a.vertices.size();++v)require(a.vertices[v].uv==b.vertices[v].uv&&a.vertices[v].sourceTriangle==b.vertices[v].sourceTriangle&&a.vertices[v].barycentrics==b.vertices[v].barycentrics,"Intrinsic doorway cache changed UV/source correspondence.");}
    }
}
}
int main(){try{crimson::decals::test::runBoundaryLineageProofs();crimson::decals::test::runIntrinsicCoverageOracleProofs();exactPlane();mixedGeometryAndNormals();shadingAliasesAndTriangle();invalidAndCancellation();savedMainContactWorkflows();savedIntrinsicDoorway();savedIntrinsicDoorway(true);
    std::cout<<"decal_paint_surface: exact planar refinement removal, mixed warped/normal fields, aliases, affine seed recovery and immutability passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
