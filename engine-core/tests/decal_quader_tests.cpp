#include "quader_bundle_fixture.h"
#include "assets/ModelAsset.h"
#include "decals/DecalModelSurface.h"
#include "editor/QuaderSceneIntegration.h"
#include "resources/StreamingModel.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>

namespace {
using namespace crimson;
using namespace crimson::decals;
namespace fs=std::filesystem;
void require(bool value,const std::string& message) { if(!value)throw std::runtime_error(message); }
struct Project {
    fs::path root=fs::temp_directory_path()/("crimson-decal-quader-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~Project() {
        std::error_code error;const auto owner=fs::weakly_canonical(fs::temp_directory_path(),error);if(error)return;
        const auto target=fs::weakly_canonical(root,error);
        if(!error&&target.parent_path()==owner&&target.filename().string().starts_with("crimson-decal-quader-"))fs::remove_all(target,error);
    }
};
std::string read(const fs::path& file) {
    std::ifstream input(file,std::ios::binary);require(bool(input),"Fixture file is unavailable.");
    return {std::istreambuf_iterator<char>(input),{}};
}
std::string snapshot(const SceneDocument& document) {
    return serializeSceneSnapshot({document.assetId(),document.displayName(),document.scene().records(),document.scene().properties(),document.previewCamera()});
}
std::optional<Vec2> uvAt(const DecalPatchChunk& chunk,Vec3 point) {
    for(std::size_t i=0;i<chunk.indices.size();i+=3) {
        const auto& a=chunk.vertices.at(chunk.indices[i]);const auto& b=chunk.vertices.at(chunk.indices[i+1]);const auto& c=chunk.vertices.at(chunk.indices[i+2]);
        const auto e1=b.position-a.position,e2=c.position-a.position,p=point-a.position;
        const double aa=math::dot(e1,e1),ab=math::dot(e1,e2),bb=math::dot(e2,e2),pa=math::dot(p,e1),pb=math::dot(p,e2),det=aa*bb-ab*ab;
        if(det<1e-20)continue;
        const double v=(bb*pa-ab*pb)/det,w=(aa*pb-ab*pa)/det,u=1-v-w;
        if(std::min({u,v,w})<-1e-4||std::max({u,v,w})>1.0001)continue;
        if(math::length(a.position*float(u)+b.position*float(v)+c.position*float(w)-point)>1e-4f)continue;
        return Vec2{float(a.uv[0]*u+b.uv[0]*v+c.uv[0]*w),float(a.uv[1]*u+b.uv[1]*v+c.uv[1]*w)};
    }
    return {};
}
void quaderBundleCornerWrap() {
    Project project;const auto fixture=bundle_test::writeCornerFixture(project.root);
    const auto bundleBefore=read(fixture.file);
    require(fixture.modelIds[0]!=fixture.modelIds[1],"Two ordinary Quader objects share a model UUID.");
    std::array<std::shared_ptr<ModelAsset>,2> models;
    std::array<std::uint64_t,2> fingerprints{};
    QuaderImportResult imported;imported.projectId=fixture.bundle.projectId;imported.sceneId=fixture.bundle.sceneId;imported.sourceKey="decal-corner-proof";
    for(std::size_t member=0;member<models.size();++member) {
        models[member]=loadModelPackage(resolveAssetPath(project.root,fixture.modelIds[member]),project.root);
        require(models[member]->id==fixture.modelIds[member],"Durable bundle expansion lost a model UUID.");
        require(models[member]->primitives.size()==1&&models[member]->primitives.front().indices.size()==6,"Bundle loader did not retain the original quad geometry.");
        fingerprints[member]=streaming::modelContentKey(*models[member]);
        QuaderImportedMesh mesh;mesh.sourceKey=member?"mesh:102:202:0":"mesh:101:201:0";mesh.name=member?"Wall":"Floor";
        mesh.modelId=models[member]->id;mesh.order=static_cast<std::uint32_t>(member);imported.meshes.push_back(mesh);
        imported.preparedModels.push_back(models[member]);
    }
    SceneDocument document(project.root);const auto owner=document.createQuader("Corner source");
    QuaderLink link;link.projectPath="C:/fixtures/decal_corner.quader";
    require(applyQuaderSceneImport(document,owner,link,imported),document.status());
    const auto sceneBefore=snapshot(document);
    std::map<EntityId,std::string> occurrenceAssets;
    DecalSurfaceSnapshot sources;std::array<ModelSurface,2> surfaces;std::array<EntityId,2> entities{};
    for(std::size_t member=0;member<models.size();++member) {
        for(const auto& record:document.scene().records())if(record.model&&record.model->id==models[member]->id) {
            require(!entities[member],"Quader fixture unexpectedly created duplicate object occurrences.");entities[member]=record.id;
        }
        require(entities[member]!=0,"Quader import did not create a visual receiver entity.");
        occurrenceAssets.emplace(entities[member],models[member]->id);
        surfaces[member]=buildModelSurface(*models[member],fingerprints[member],SourceNormals::UseMeshNormals);
        require(surfaces[member].diagnostic.empty()&&surfaces[member].draws.size()==1,"Model adapter rejected a valid imported original quad.");
        const auto world=document.scene().worldTransform(entities[member]);require(world.has_value(),"Imported receiver lost its affine world transform.");
        for(const auto& draw:surfaces[member].draws) {
            auto receiver=draw.surface;receiver.id.entity=entities[member];receiver.localToWorld=*world*receiver.localToWorld;sources.receivers.push_back(std::move(receiver));
        }
    }
    require(entities[0]!=entities[1],"Independent Quader objects collapsed to one receiver entity.");
    const Vec3 seedPoint{.07f,0,.15f};const auto& floorDraw=surfaces[0].draws.front();
    const auto hit=resolveModelSurfaceHit(surfaces[0],entities[0],*document.scene().worldTransform(entities[0]),
        floorDraw.surface.id.draw,floorDraw.surface.id.primitive,seedPoint);
    require(hit.status==SurfaceHitStatus::Found&&hit.hit.has_value(),"Imported render-surface seed could not recover source barycentrics.");
    DecalBuildRequest request;request.seed=hit.hit->seed;
    // One-metre footprint. X points right, Y points back along the floor;
    // intrinsic continuation must turn that axis up the separate wall.
    request.frame=Mat4{};request.frame.at(1,1)=0;request.frame.at(2,1)=-1;
    request.frame.at(1,2)=1;request.frame.at(2,2)=0;
    for(unsigned axis=0;axis<3;++axis)request.frame.at(axis,3)=seedPoint[axis];
    const auto patch=buildDecalPatch(sources,request);
    require(patch.usable(),"Quader corner wrap failed: "+patch.message);
    std::set<EntityId> touched;std::set<std::string> sourceUuids;
    const DecalPatchChunk* floor{};const DecalPatchChunk* wall{};
    bool reachesFloor{},reachesWall{};
    for(const auto& chunk:patch.chunks) {
        touched.insert(chunk.receiver.entity);sourceUuids.insert(occurrenceAssets.at(chunk.receiver.entity));
        if(chunk.receiver.entity==entities[0])floor=&chunk;
        if(chunk.receiver.entity==entities[1])wall=&chunk;
        const auto receiver=std::find_if(sources.receivers.begin(),sources.receivers.end(),[&](const auto& candidate){return candidate.id==chunk.receiver;});
        require(receiver!=sources.receivers.end()&&receiver->generation==chunk.generation,"Decal chunk lost its exact Quader occurrence/generation.");
        for(const auto& vertex:chunk.vertices) {
            const auto triangle=std::find_if(receiver->triangles.begin(),receiver->triangles.end(),[&](const auto& candidate){return candidate.triangle==vertex.sourceTriangle;});
            require(triangle!=receiver->triangles.end(),"Generated vertex has no original source triangle.");
            require(math::finite(vertex.position)&&math::finite(vertex.uv)&&math::finite(vertex.barycentrics),"Generated corner vertex contains nonfinite values.");
            require(std::abs(vertex.barycentrics[0]+vertex.barycentrics[1]+vertex.barycentrics[2]-1)<1e-4f&&
                *std::min_element(vertex.barycentrics.begin(),vertex.barycentrics.end())>=-1e-4f,"Generated vertex escaped its source triangle.");
            const auto local=triangle->positions[0]*vertex.barycentrics[0]+triangle->positions[1]*vertex.barycentrics[1]+triangle->positions[2]*vertex.barycentrics[2];
            require(math::length(math::transformPoint(receiver->localToWorld,local)-vertex.position)<2e-4f,"Decal geometry bridged open space instead of remaining on source geometry.");
            reachesFloor|=chunk.receiver.entity==entities[0]&&vertex.position[2]>.2f;
            reachesWall|=chunk.receiver.entity==entities[1]&&vertex.position[1]>.15f;
        }
    }
    require(touched==std::set<EntityId>{entities[0],entities[1]}&&sourceUuids==std::set<std::string>{fixture.modelIds[0],fixture.modelIds[1]},"One decal did not retain both independent Quader receiver identities.");
    require(floor&&wall&&reachesFloor&&reachesWall,"The chart did not continue from floor onto wall.");
    for(const float x:{-.2f,0.f,.2f}) {
        const auto floorUv=uvAt(*floor,{x,0,0}),wallUv=uvAt(*wall,{x,0,0});
        require(floorUv&&wallUv,"Corner patch has a gap along the source contact edge.");
        const auto dx=(*floorUv)[0]-(*wallUv)[0],dy=(*floorUv)[1]-(*wallUv)[1];
        require(std::sqrt(dx*dx+dy*dy)<=.0025f,"Decal chart is discontinuous across separate Quader mesh UUIDs.");
    }
    require(read(fixture.file)==bundleBefore&&snapshot(document)==sceneBefore,"Decal construction mutated durable source geometry or scene entities.");
    for(std::size_t member=0;member<models.size();++member)require(streaming::modelContentKey(*models[member])==fingerprints[member],"Decal construction edited a loaded source mesh.");
    std::cout<<"Quader corner: "<<patch.statistics.outputTriangles<<" output triangles, "<<patch.statistics.joinedEdges<<" joins, "
        <<patch.statistics.topologyMilliseconds<<" ms topology, "<<patch.statistics.solveMilliseconds<<" ms solve\n";
}
}
int main() {
    try {quaderBundleCornerWrap();std::cout<<"Quader decal source proof passed\n";return 0;}
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
