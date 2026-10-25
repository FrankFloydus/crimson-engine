#include "decals/DecalModelSurface.h"
#include "decals/DecalResolve.h"
#include "decals/DecalPaintSurface.h"
#include "decal_probe_snapshot.h"
#include <crimson/renderer/forward/MeshPaint.h>
#include <crimson/renderer/forward/ModelData.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

// A read-only CPU probe for the actual room source reported with the large
// floor-decal failure. It does not initialize the editor, write a model cache,
// or touch the source Quader project. The glTF is supplied explicitly because
// disposable user project assets are not dependencies of the test suite.
using namespace crimson;
using namespace crimson::decals;
namespace {
using Json=nlohmann::json;
namespace fs=std::filesystem;
void require(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
std::vector<unsigned char> readBytes(const fs::path& file) {
    std::ifstream stream(file,std::ios::binary);
    if(!stream)throw std::runtime_error("Cannot read "+file.string());
    return {std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};
}
struct Gltf {
    Json value;
    std::vector<std::vector<unsigned char>> buffers;
    explicit Gltf(const fs::path& file) {
        std::ifstream stream(file);if(!stream)throw std::runtime_error("Cannot read "+file.string());stream>>value;
        for(const auto& buffer:value.at("buffers"))buffers.push_back(readBytes(file.parent_path()/buffer.at("uri").get<std::string>()));
    }
    template<class T> T scalar(std::size_t accessorIndex,std::size_t item,std::size_t component=0) const {
        const auto& accessor=value.at("accessors").at(accessorIndex);
        const auto& view=value.at("bufferViews").at(accessor.at("bufferView").get<std::size_t>());
        const auto& data=buffers.at(view.at("buffer").get<std::size_t>());
        const unsigned type=accessor.at("componentType").get<unsigned>();
        const std::size_t width=type==5126||type==5125?4:type==5123?2:type==5121?1:0;
        require(width!=0,"Unsupported room glTF component type.");
        const auto shape=accessor.at("type").get<std::string>();
        const std::size_t components=shape=="SCALAR"?1:shape=="VEC2"?2:shape=="VEC3"?3:shape=="VEC4"?4:0;
        require(components!=0&&component<components,"Unsupported room glTF accessor shape.");
        const auto offset=view.value("byteOffset",std::size_t{})+accessor.value("byteOffset",std::size_t{})+
            item*view.value("byteStride",width*components)+component*width;
        require(item<accessor.at("count").get<std::size_t>()&&offset+width<=data.size(),"Room glTF accessor exceeds its buffer.");
        if(type==5126){float result;std::memcpy(&result,data.data()+offset,4);return static_cast<T>(result);}
        if(type==5125){std::uint32_t result;std::memcpy(&result,data.data()+offset,4);return static_cast<T>(result);}
        if(type==5123){std::uint16_t result;std::memcpy(&result,data.data()+offset,2);return static_cast<T>(result);}
        return static_cast<T>(data[offset]);
    }
    ModelAsset model() const {
        ModelAsset result;result.id="readonly-decal-room-probe";
        require(value.at("meshes").size()==1,"Room probe expects one mesh.");
        for(const auto& input:value.at("meshes").at(0).at("primitives")) {
            ModelPrimitive primitive;primitive.material=input.value("material",0u);
            const auto& attributes=input.at("attributes");const auto positions=attributes.at("POSITION").get<std::size_t>();
            primitive.hasSkinning=attributes.contains("JOINTS_0");
            if(input.contains("targets")&&!input.at("targets").empty())throw std::runtime_error("Scene probe does not admit morph geometry.");
            primitive.vertices.resize(value.at("accessors").at(positions).at("count").get<std::size_t>());
            for(std::size_t i=0;i<primitive.vertices.size();++i)for(std::size_t axis=0;axis<3;++axis){
                primitive.vertices[i].position[axis]=scalar<float>(positions,i,axis);
                if(attributes.contains("NORMAL"))primitive.vertices[i].normal[axis]=scalar<float>(attributes.at("NORMAL").get<std::size_t>(),i,axis);
            }
            const auto indices=input.at("indices").get<std::size_t>();
            for(std::size_t i=0;i<value.at("accessors").at(indices).at("count").get<std::size_t>();++i)primitive.indices.push_back(scalar<std::uint32_t>(indices,i));
            if(input.contains("extras")&&input.at("extras").contains("crimsonSourceFaces"))for(const auto& face:input.at("extras").at("crimsonSourceFaces")) {
                ModelSourceFace output;output.cornerCount=face.at("cornerCount").get<std::uint8_t>();
                for(std::size_t i=0;i<output.cornerCount;++i)output.vertices[i]=face.at("vertices").at(i).get<std::uint32_t>();
                for(std::size_t i=0;i<2;++i)output.triangles[i]=face.at("triangles").at(i).get<std::uint32_t>();
                primitive.sourceFaces.push_back(output);
            }
            result.draws.push_back({static_cast<std::uint32_t>(result.primitives.size())});result.primitives.push_back(std::move(primitive));
        }
        return result;
    }
};
Json readJson(const fs::path& file) {std::ifstream stream(file);require(bool(stream),"Cannot read scene probe JSON.");Json value;stream>>value;return value;}
Mat4 jsonMatrix(const Json& value) {Mat4 result;require(value.size()==16,"Invalid scene probe affine matrix.");for(size_t i=0;i<16;++i)result[i]=value.at(i).get<float>();return result;}
struct ProbeOptions {fs::path exportFile;bool exportOnly{},filteredOnly{},expectFrontSide{},expectFullImage{};std::optional<uint64_t> onlyEntity;};
ProbeOptions probeOptions(int argc,char** argv,int first) {
    ProbeOptions result;
    for(int i=first;i<argc;++i) {
        const std::string argument=argv[i];
        if(argument=="--export"){require(i+1<argc,"--export requires a file.");result.exportFile=argv[++i];}
        else if(argument=="--export-only")result.exportOnly=true;
        else if(argument=="--filtered-only")result.filteredOnly=true;
        else if(argument=="--expect-front-side")result.expectFrontSide=true;
        else if(argument=="--expect-full-image")result.expectFullImage=true;
        else if(argument=="--entity"){require(i+1<argc,"--entity requires a hexadecimal entity ID.");result.onlyEntity=std::stoull(argv[++i],nullptr,16);}
        else throw std::runtime_error("Unknown scene/snapshot probe argument: "+argument);
    }
    require(!result.exportOnly||!result.exportFile.empty(),"--export-only requires --export.");return result;
}
void sceneProbe(const fs::path& sceneFile,const std::string& name,const ProbeOptions& options) {
    const auto root=sceneFile.parent_path().parent_path();const auto scene=readJson(sceneFile),locations=readJson(root/".crimson/asset-locations.json").at("assets");
    std::map<std::string,Json> entities;for(const auto& entity:scene.at("entities"))entities.emplace(entity.at("id").get<std::string>(),entity);
    const auto world=[&](auto&& self,const Json& entity)->Mat4 {const auto local=jsonMatrix(entity.at("local"));const auto parent=entities.find(entity.value("parent",std::string{}));return parent==entities.end()?local:self(self,parent->second)*local;};
    const auto visible=[&](auto&& self,const Json& entity)->bool {if(!entity.value("visible",true))return false;const auto parent=entities.find(entity.value("parent",std::string{}));return parent==entities.end()||self(self,parent->second);};
    const Json* decal{};for(const auto& [id,entity]:entities)if(entity.value("name",std::string{})==name&&entity.contains("decal")){require(!decal,"Scene probe name is ambiguous.");decal=&entity;}require(decal,"Scene probe decal was not found.");
    const auto frame=world(world,*decal);const auto center=math::transformPoint(frame,{}),x=math::transformDirection(frame,{1,0,0}),y=math::transformDirection(frame,{0,1,0});
    const double radius=.625*std::max(math::length(x+y),math::length(x-y))+.5*std::min(math::length(x),math::length(y));const float projection=static_cast<float>(.5*std::max(math::length(x),math::length(y))+.025);
    struct Source {std::string name;uint64_t entity{};Mat4 world;ModelSurface surface;};std::vector<Source> sources;
    for(const auto& [id,entity]:entities){if(!entity.contains("model")||!visible(visible,entity)||!entity.value("receiveDecals",true))continue;
        if(entity.contains("modelAnimation")&&!entity.at("modelAnimation").value("asset",std::string{}).empty())continue;
        const auto asset=entity.at("model").get<std::string>();if(!locations.contains(asset))continue;const auto manifest=root/locations.at(asset).get<std::string>();if(!fs::is_regular_file(manifest))continue;
        const auto metadata=readJson(manifest);if(!metadata.contains("document"))continue;const Gltf gltf(manifest.parent_path()/metadata.at("document").get<std::string>());auto model=gltf.model();
        const auto entityWorld=world(world,entity);Aabb bounds;for(const auto& primitive:model.primitives)for(const auto& vertex:primitive.vertices)math::include(bounds,math::transformPoint(entityWorld,vertex.position));
        const auto extent=Vec3{radius+projection,radius+projection,radius+projection};bool near=bounds.valid;for(size_t axis=0;axis<3;++axis)near&=bounds.minimum[axis]<=center[axis]+extent[axis]&&bounds.maximum[axis]>=center[axis]-extent[axis];if(!near)continue;
        const auto level=entity.contains("meshPaint")?entity.at("meshPaint").value("level",0u):0u;
        for(auto& primitive:model.primitives)if(level&&!primitive.hasSkinning){const auto geometry=buildMeshPaintGeometry(primitive,level);const auto proxy=simplifyDecalPaintPrimitive(primitive,*geometry);primitive=proxy?*proxy:meshPaintRenderPrimitive(primitive,*geometry);}
        auto source=buildModelSurface(model,1,static_cast<SourceNormals>(entity.value("meshShading",0)));if(!source.draws.empty())sources.push_back({entity.at("name").get<std::string>(),std::stoull(id,nullptr,16),entityWorld,std::move(source)});
    }
    const auto saved=std::stoull(decal->at("decal").at("seed").at("receiver").get<std::string>(),nullptr,16);std::optional<ModelSurfaceHit> seed;const Source* seedSource{};double distance=INFINITY;
    for(const auto& source:sources){const auto hit=projectModelSurfaceHit(source.surface,source.entity,source.world,center,projection);if(!hit.hit)continue;const auto d=math::length(math::transformPoint(source.world,hit.hit->localPoint)-center);if(d<distance-1e-5||(std::abs(d-distance)<1e-5&&source.entity==saved)){distance=d;seed=hit.hit;seedSource=&source;}}
    require(seed&&seedSource,"Scene probe could not reproject saved placement.");const auto normal=math::normalMatrix(seedSource->world);require(bool(normal),"Scene probe receiver transform is singular.");const auto effective=conformDecalFrame(frame,math::transformPoint(seedSource->world,seed->localPoint),math::transformDirection(*normal,seed->localNormal));require(bool(effective),"Scene probe footprint is singular.");
    DecalBuildRequest request;request.seed=seed->seed;request.frame=*effective;const auto origin=math::transformPoint(*effective,{});const Vec3 extent{radius,radius,radius};DecalSurfaceSnapshot snapshot;size_t count{};
    std::cout<<"scene="<<sceneFile.string()<<" decal="<<name<<" radius="<<radius<<" seed="<<seedSource->name<<" center="<<origin[0]<<','<<origin[1]<<','<<origin[2]<<'\n';
    for(const auto& source:sources)for(const auto& draw:source.surface.draws){const auto indices=querySurfaceTriangles(draw,source.world,{origin-extent,origin+extent,true});if(indices.empty())continue;std::cout<<"candidate="<<source.name<<" entity="<<std::hex<<source.entity<<std::dec<<" draw="<<draw.surface.id.draw<<" triangles="<<indices.size()<<'\n';auto receiver=draw.surface;receiver.id.entity=source.entity;receiver.localToWorld=source.world*receiver.localToWorld;receiver.triangles.clear();for(const auto index:indices)receiver.triangles.push_back(draw.surface.triangles[index]);count+=indices.size();snapshot.receivers.push_back(std::move(receiver));}
    request.limits.maximumTriangles=static_cast<uint32_t>(std::clamp<size_t>(count*2,16384,65536));std::cout<<"broadphase="<<count<<" oldHostRejected="<<(count>request.limits.maximumInputTriangles)<<'\n'<<std::flush;
    std::optional<DecalPatch> patch;
    if(!options.filteredOnly&&!options.exportOnly){const auto started=std::chrono::steady_clock::now();patch=buildDecalPatch(snapshot,request);std::cout<<"unfilteredStatus="<<decalPatchStatusName(patch->status)<<" input="<<patch->statistics.inputTriangles<<" analysis="<<patch->statistics.analysisTriangles<<" output="<<patch->statistics.outputTriangles<<" ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()<<" message="<<patch->message<<'\n'<<std::flush;}
    const auto query=decalSourceQuery(seedSource->surface,seedSource->world,request);DecalSurfaceSnapshot filtered;size_t selected{};
    for(const auto& source:sources)for(const auto& draw:source.surface.draws){const auto indices=querySurfaceTriangles(draw,source.world,query);if(indices.empty())continue;auto receiver=draw.surface;receiver.id.entity=source.entity;receiver.localToWorld=source.world*receiver.localToWorld;receiver.triangles.clear();for(const auto index:indices)receiver.triangles.push_back(draw.surface.triangles[index]);selected+=indices.size();filtered.receivers.push_back(std::move(receiver));}
    require(selected<=request.limits.maximumInputTriangles,"Exact scene source selection still exceeds the input budget.");request.limits.maximumTriangles=static_cast<uint32_t>(std::clamp<size_t>(selected*2,16384,65536));
    if(options.onlyEntity)std::erase_if(filtered.receivers,[&](const auto& receiver){return receiver.id.entity!=*options.onlyEntity;});
    std::cout<<"exactSelected="<<selected<<" seedEntity="<<std::hex<<request.seed.receiver.entity<<std::dec<<" seedTriangle="<<request.seed.triangle<<'\n'<<std::flush;
    if(!options.exportFile.empty())decal_probe::writeSnapshot(options.exportFile,filtered,request,{{"scene",sceneFile.generic_string()},{"decal",name},{"seedSource",seedSource->name},{"savedDecal",*decal}});
    if(options.exportOnly)return;
    const auto filteredStart=std::chrono::steady_clock::now();const auto output=buildDecalPatch(filtered,request);
    std::cout<<"selected="<<selected<<" status="<<decalPatchStatusName(output.status)<<" input="<<output.statistics.inputTriangles<<" analysis="<<output.statistics.analysisTriangles<<" output="<<output.statistics.outputTriangles<<" ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-filteredStart).count()<<" message="<<output.message<<'\n';
    decal_probe::reportCoverage(filtered,request,output,options.expectFrontSide,options.expectFullImage);
    if(patch&&!options.onlyEntity)require(output.status==patch->status&&output.statistics.inputTriangles==patch->statistics.inputTriangles&&output.statistics.outputTriangles==patch->statistics.outputTriangles,"Exact source selection changed scene chart behavior.");
}
void snapshotProbe(const fs::path& file,const ProbeOptions& options) {
    auto [snapshot,request]=decal_probe::snapshotInput(readJson(file));
    if(options.onlyEntity)std::erase_if(snapshot.receivers,[&](const auto& receiver){return receiver.id.entity!=*options.onlyEntity;});
    if(!options.exportFile.empty())decal_probe::writeSnapshot(options.exportFile,snapshot,request,{{"snapshot",file.generic_string()}});
    if(options.exportOnly)return;
    const auto started=std::chrono::steady_clock::now();const auto output=buildDecalPatch(snapshot,request);
    std::cout<<"snapshot="<<file.string()<<" status="<<decalPatchStatusName(output.status)<<" input="<<output.statistics.inputTriangles<<" analysis="<<output.statistics.analysisTriangles<<" output="<<output.statistics.outputTriangles<<" ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()<<" message="<<output.message<<'\n'<<std::flush;
    decal_probe::reportCoverage(snapshot,request,output,options.expectFrontSide,options.expectFullImage);
}
Mat4 roomWorld() {
    Mat4 result;const std::array<float,16> values{{5.960464477539063e-08f,0,-.9999999403953552f,0,
        0,1.3333333730697632f,0,0,1.4999998807907104f,0,8.940696716308594e-08f,0,3,.6666666865348816f,22.75f,1}};
    std::copy(values.begin(),values.end(),result.begin());return result;
}
void run(const ModelAsset& original,unsigned level,const std::vector<float>& sizes,bool useProxy,bool adaptiveBudget) {
    auto model=original;std::size_t triangles{};
    for(auto& primitive:model.primitives){if(level){const auto geometry=buildMeshPaintGeometry(primitive,level);auto proxy=useProxy?simplifyDecalPaintPrimitive(primitive,*geometry):std::optional<ModelPrimitive>{};primitive=proxy?std::move(*proxy):meshPaintRenderPrimitive(primitive,*geometry);}triangles+=primitive.indices.size()/3;}
    const auto source=buildModelSurface(model,level+1,SourceNormals::UseMeshNormals);const auto world=roomWorld();
    const auto point=math::transformPoint(world,{.371f,-.5f,-.231f});
    const auto hit=projectModelSurfaceHit(source,0x6f1f1f91d24b1ed1ull,world,point,.005f,[](std::uint32_t material){return material==1;});
    require(hit.hit.has_value(),"Room floor seed could not be resolved.");
    for(const auto size:sizes){
        DecalBuildRequest request;request.seed=hit.hit->seed;request.frame=surfacePlacementFrame(point,{0,1,0})*math::scaling({size,size,1});
        const auto x=math::transformDirection(request.frame,{1,0,0}),y=math::transformDirection(request.frame,{0,1,0});
        const float radius=static_cast<float>(.625*std::max(math::length(x+y),math::length(x-y))+.5*std::min(math::length(x),math::length(y)));
        const Vec3 extent{radius,radius,radius};const Aabb region{point-extent,point+extent,true};
        DecalSurfaceSnapshot snapshot;std::size_t inputCount{};
        for(const auto& draw:source.draws){const auto indices=querySurfaceTriangles(draw,world,region);if(indices.empty())continue;
            auto receiver=draw.surface;receiver.id.entity=0x6f1f1f91d24b1ed1ull;receiver.localToWorld=world*receiver.localToWorld;receiver.triangles.clear();
            for(const auto index:indices)receiver.triangles.push_back(draw.surface.triangles[index]);inputCount+=indices.size();snapshot.receivers.push_back(std::move(receiver));}
        if(adaptiveBudget)request.limits.maximumTriangles=static_cast<std::uint32_t>(std::clamp<std::size_t>(inputCount*2,16384,65536));
        const auto started=std::chrono::steady_clock::now();const auto patch=buildDecalPatch(snapshot,request);
        const auto milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
        double area{};std::size_t vertexCount{},indexCount{},cpuBytes=sizeof(patch);
        for(const auto& chunk:patch.chunks){vertexCount+=chunk.vertices.size();indexCount+=chunk.indices.size();cpuBytes+=sizeof(chunk)+chunk.vertices.size()*sizeof(crimson::decals::DecalPatchVertex)+chunk.indices.size()*sizeof(std::uint32_t);}
        for(const auto& chunk:patch.chunks)for(std::size_t i=0;i<chunk.indices.size();i+=3){const auto a=chunk.vertices[chunk.indices[i]].uv,b=chunk.vertices[chunk.indices[i+1]].uv,c=chunk.vertices[chunk.indices[i+2]].uv;area+=.5*std::abs((double(b[0])-a[0])*(double(c[1])-a[1])-(double(b[1])-a[1])*(double(c[0])-a[0]));}
        std::cout<<"level="<<level<<" proxy="<<useProxy<<" sourceTriangles="<<triangles<<" size="<<size<<" status="<<decalPatchStatusName(patch.status)
            <<" input="<<patch.statistics.inputTriangles<<" analysis="<<patch.statistics.analysisTriangles<<" output="<<patch.statistics.outputTriangles
            <<" budget="<<request.limits.maximumTriangles<<" vertices="<<vertexCount<<" cpuBytes="<<cpuBytes<<" uploadBytes="<<vertexCount*(2*sizeof(Vec3)+sizeof(Vec2))+indexCount*sizeof(std::uint32_t)
            <<" chunks="<<patch.chunks.size()<<" uvArea="<<area<<" ms="<<milliseconds<<" message="<<patch.message<<'\n'<<std::flush;
    }
}
}
int main(int argc,char** argv) {
    try {
        if(argc>=3&&std::string(argv[1])=="--scene"){const bool named=argc>3&&std::string(argv[3]).find("--")!=0;sceneProbe(argv[2],named?argv[3]:"pentacle",probeOptions(argc,argv,named?4:3));return 0;}
        if(argc>=3&&std::string(argv[1])=="--snapshot"){snapshotProbe(argv[2],probeOptions(argc,argv,3));return 0;}
        require(argc>=2,"Usage: decal_room_probe source.gltf [level=5] [square-size-metres ...] [--proxy] [--adaptive-budget]");
        const unsigned level=argc>2?static_cast<unsigned>(std::stoul(argv[2])):5;
        require(level<=MaximumMeshPaintLevel,"Invalid subdivision level.");
        bool useProxy=false,adaptiveBudget=false;std::vector<float> sizes;for(int i=3;i<argc;++i){const std::string argument=argv[i];if(argument=="--proxy")useProxy=true;else if(argument=="--adaptive-budget")adaptiveBudget=true;else sizes.push_back(std::stof(argument));}if(sizes.empty())sizes={1,2,4,8};
        const Gltf gltf{fs::path(argv[1])};run(gltf.model(),level,sizes,useProxy,adaptiveBudget);return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
