#pragma once

#include <lodepng.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

// CPU-generated, self-contained fixtures shared by focused importer tests and
// the disposable native preview smoke. Nothing here writes authored assets.
namespace crimson::model_test {
using Json = nlohmann::json;
using Bytes = std::vector<uint8_t>;
struct FixturePaths { std::filesystem::path gltf, glb, image; };
inline void writeBytes(const std::filesystem::path& file,const Bytes& bytes) {
    std::filesystem::create_directories(file.parent_path()); std::ofstream out(file,std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    if (!out) throw std::runtime_error("Could not create model test fixture");
}
inline void writeJson(const std::filesystem::path& file,const Json& value) {
    const auto text=value.dump(2); writeBytes(file,Bytes(text.begin(),text.end()));
}
inline void appendU32(Bytes& bytes,uint32_t value) {
    for (int i=0;i<4;++i) bytes.push_back(static_cast<uint8_t>(value>>(i*8)));
}
template<class T> inline void append(Bytes& bytes,const T& object) {
    const auto* begin=reinterpret_cast<const uint8_t*>(&object); bytes.insert(bytes.end(),begin,begin+sizeof(object));
}
inline void writeGlb(const std::filesystem::path& file,Json json,Bytes buffer) {
    json["buffers"]=Json::array({{{"byteLength",buffer.size()}}});
    auto text=json.dump(); while (text.size()%4) text+=' '; while (buffer.size()%4) buffer.push_back(0);
    Bytes result; appendU32(result,0x46546c67); appendU32(result,2); appendU32(result,static_cast<uint32_t>(28+text.size()+buffer.size()));
    appendU32(result,static_cast<uint32_t>(text.size())); appendU32(result,0x4e4f534a); result.insert(result.end(),text.begin(),text.end());
    appendU32(result,static_cast<uint32_t>(buffer.size())); appendU32(result,0x004e4942); result.insert(result.end(),buffer.begin(),buffer.end()); writeBytes(file,result);
}
inline std::string base64(const Bytes& bytes) {
    constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; std::string result;
    for (size_t i=0;i<bytes.size();i+=3) {
        const auto a=bytes[i], b=i+1<bytes.size()?bytes[i+1]:uint8_t{0}, c=i+2<bytes.size()?bytes[i+2]:uint8_t{0};
        result+=alphabet[a>>2]; result+=alphabet[((a&3)<<4)|(b>>4)];
        result+=i+1<bytes.size()?alphabet[((b&15)<<2)|(c>>6)]:'='; result+=i+2<bytes.size()?alphabet[c&63]:'=';
    }
    return result;
}
inline Bytes png(uint32_t width,uint32_t height,const Bytes& rgba) {
    Bytes encoded; if (lodepng::encode(encoded,rgba,width,height)!=0) throw std::runtime_error("Fixture PNG encoding failed"); return encoded;
}
inline FixturePaths writeModelFixtures(const std::filesystem::path& folder) {
    std::filesystem::create_directories(folder);
    struct Vertex { float position[3], normal[3], uv0[2], uv1[2]; uint8_t color[4]; };
    static_assert(sizeof(Vertex)==44);
    // Counter-clockwise faces in glTF's right-handed coordinate system.
    constexpr std::array<std::array<std::array<float,3>,4>,6> faces{{
        {{{-.65f,0,.65f},{.65f,0,.65f},{.65f,1.3f,.65f},{-.65f,1.3f,.65f}}},
        {{{.65f,0,-.65f},{-.65f,0,-.65f},{-.65f,1.3f,-.65f},{.65f,1.3f,-.65f}}},
        {{{.65f,0,.65f},{.65f,0,-.65f},{.65f,1.3f,-.65f},{.65f,1.3f,.65f}}},
        {{{-.65f,0,-.65f},{-.65f,0,.65f},{-.65f,1.3f,.65f},{-.65f,1.3f,-.65f}}},
        {{{-.65f,1.3f,.65f},{.65f,1.3f,.65f},{.65f,1.3f,-.65f},{-.65f,1.3f,-.65f}}},
        {{{-.65f,0,-.65f},{.65f,0,-.65f},{.65f,0,.65f},{-.65f,0,.65f}}}
    }};
    constexpr std::array<std::array<float,3>,6> normals{{{0,0,1},{0,0,-1},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}}};
    constexpr std::array<std::array<float,2>,4> uv{{{0,1},{1,1},{1,0},{0,0}}}; Bytes buffer;
    for (size_t face=0;face<6;++face) for (size_t vertex=0;vertex<4;++vertex) {
        Vertex v{}; std::copy_n(faces[face][vertex].data(),3,v.position); std::copy_n(normals[face].data(),3,v.normal);
        std::copy_n(uv[vertex].data(),2,v.uv0); v.uv1[0]=uv[vertex][0]*2; v.uv1[1]=uv[vertex][1]*2;
        v.color[0]=255; v.color[1]=static_cast<uint8_t>(face%2?220:255); v.color[2]=255; v.color[3]=255; append(buffer,v);
    }
    const size_t indexOffset=buffer.size();
    for (uint16_t face=0;face<6;++face) for (const uint16_t index:std::array<uint16_t,6>{0,1,2,0,2,3}) append(buffer,static_cast<uint16_t>(face*4+index));
    Json json={
        {"asset",{{"version","2.0"},{"generator","Crimson focused model fixture"}}},
        {"buffers",Json::array({{{"uri","geometry.bin"},{"byteLength",buffer.size()}}})},
        {"bufferViews",Json::array({{{"buffer",0},{"byteOffset",0},{"byteLength",indexOffset},{"byteStride",44}},
                                   {{"buffer",0},{"byteOffset",indexOffset},{"byteLength",72}}})},
        {"accessors",Json::array({
            {{"bufferView",0},{"byteOffset",0},{"componentType",5126},{"count",24},{"type","VEC3"},{"min",{-.65,0,-.65}},{"max",{.65,1.3,.65}}},
            {{"bufferView",0},{"byteOffset",12},{"componentType",5126},{"count",24},{"type","VEC3"}},
            {{"bufferView",0},{"byteOffset",24},{"componentType",5126},{"count",24},{"type","VEC2"}},
            {{"bufferView",0},{"byteOffset",32},{"componentType",5126},{"count",24},{"type","VEC2"}},
            {{"bufferView",0},{"byteOffset",40},{"componentType",5121},{"normalized",true},{"count",24},{"type","VEC4"}},
            {{"bufferView",1},{"byteOffset",0},{"componentType",5123},{"count",18},{"type","SCALAR"}},
            {{"bufferView",1},{"byteOffset",36},{"componentType",5123},{"count",18},{"type","SCALAR"}}
        })},
        {"images",Json::array({{{"uri","checker.png"}},{{"uri","normal.png"}},{{"uri","packed.png"}}})},
        {"samplers",Json::array({{{"minFilter",9987},{"magFilter",9728},{"wrapS",10497},{"wrapT",33648}}})},
        {"textures",Json::array({{{"source",0},{"sampler",0}},{{"source",1}},{{"source",2}}})},
        {"materials",Json::array({
            {{"name","Copper cutout"},{"pbrMetallicRoughness",{{"baseColorFactor",{1,.28,.08,1}},{"metallicFactor",.55},{"roughnessFactor",.65},{"baseColorTexture",{{"index",0}}},{"metallicRoughnessTexture",{{"index",2}}}}},
             {"normalTexture",{{"index",1},{"scale",.5}}},{"occlusionTexture",{{"index",2},{"strength",.7}}},{"alphaMode","MASK"},{"alphaCutoff",.5},{"doubleSided",true}},
            {{"name","Blue enamel"},{"pbrMetallicRoughness",{{"baseColorFactor",{.05,.28,1,1}},{"metallicFactor",.15},{"roughnessFactor",.35},{"baseColorTexture",{{"index",0},{"texCoord",1}}}}},
             {"emissiveFactor",{.008,.01,.025}},{"normalTexture",{{"index",1},{"texCoord",1}}}}
        })},
        {"meshes",Json::array({{{"name","Two-material cube"},{"primitives",Json::array({
            {{"attributes",{{"POSITION",0},{"NORMAL",1},{"TEXCOORD_0",2},{"TEXCOORD_1",3},{"COLOR_0",4}}},{"indices",5},{"material",0}},
            {{"attributes",{{"POSITION",0},{"NORMAL",1},{"TEXCOORD_0",2},{"TEXCOORD_1",3},{"COLOR_0",4}}},{"indices",6},{"material",1}}
        })}}})},
        {"nodes",Json::array({{{"name","Authored root"},{"translation",{-1,0,0}},{"mesh",0},{"children",{1}}},
                              {{"name","Mirrored nonuniform child"},{"translation",{2.5,0,0}},{"scale",{-.65,1.45,.8}},{"mesh",0}}})},
        {"scenes",Json::array({{{"nodes",{0}}}})},{"scene",0}
    };
    Bytes pixels(8*8*4,255), normalPixels(8*8*4,255), packedPixels(8*8*4,255);
    for (size_t y=0;y<8;++y) for (size_t x=0;x<8;++x) {
        const auto offset=(y*8+x)*4; const uint8_t value=((x/2+y/2)%2)?255:150;
        pixels[offset]=pixels[offset+1]=pixels[offset+2]=value; if (x==3 && y==3) pixels[offset+3]=0;
        normalPixels[offset]=static_cast<uint8_t>((x%4<2)?110:146); normalPixels[offset+1]=128; normalPixels[offset+2]=253;
        packedPixels[offset]=230; packedPixels[offset+1]=static_cast<uint8_t>(x<4?90:230); packedPixels[offset+2]=static_cast<uint8_t>(y<4?255:80);
    }
    const std::array<Bytes,3> images{png(8,8,pixels),png(8,8,normalPixels),png(8,8,packedPixels)};
    writeBytes(folder/"geometry.bin",buffer); writeBytes(folder/"checker.png",images[0]); writeBytes(folder/"normal.png",images[1]); writeBytes(folder/"packed.png",images[2]);
    writeJson(folder/"hierarchy.gltf",json);
    auto glbJson=json;
    for (size_t i=0;i<images.size();++i) {
        while (buffer.size()%4) buffer.push_back(0); const auto offset=buffer.size(); buffer.insert(buffer.end(),images[i].begin(),images[i].end());
        const auto view=glbJson["bufferViews"].size(); glbJson["bufferViews"].push_back({{"buffer",0},{"byteOffset",offset},{"byteLength",images[i].size()}});
        glbJson["images"][i]={{"bufferView",view},{"mimeType","image/png"}};
    }
    glbJson["nodes"]=Json::array({{{"name","Embedded cube"},{"mesh",0},{"translation",{0,0,0}}}});
    writeGlb(folder/"embedded.glb",glbJson,buffer);
    return {folder/"hierarchy.gltf",folder/"embedded.glb",folder/"checker.png"};
}
} // namespace crimson::model_test
