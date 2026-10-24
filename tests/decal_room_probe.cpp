#include "decals/DecalModelSurface.h"
#include "decals/DecalResolve.h"
#include "decals/DecalPaintSurface.h"
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
        require(argc>=2,"Usage: decal_room_probe source.gltf [level=5] [square-size-metres ...] [--proxy] [--adaptive-budget]");
        const unsigned level=argc>2?static_cast<unsigned>(std::stoul(argv[2])):5;
        require(level<=MaximumMeshPaintLevel,"Invalid subdivision level.");
        bool useProxy=false,adaptiveBudget=false;std::vector<float> sizes;for(int i=3;i<argc;++i){const std::string argument=argv[i];if(argument=="--proxy")useProxy=true;else if(argument=="--adaptive-budget")adaptiveBudget=true;else sizes.push_back(std::stof(argument));}if(sizes.empty())sizes={1,2,4,8};
        const Gltf gltf{fs::path(argv[1])};run(gltf.model(),level,sizes,useProxy,adaptiveBudget);return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
